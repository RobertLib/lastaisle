/* LAST AISLE - audio: device, lock-free command queue, voice mixer, master bus.
   Everything audible is synthesized at init (SFX, drum kits) or in real time
   (loops, music); there are no audio asset files. */
#include "audio_internal.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

#define MAX_LIVE      32          /* audible one-shot voices */
#define MAX_SLOTS     44          /* + slots for voices fading out after being stolen */
#define MIX_BLOCK     256
#define CMD_RING      1024        /* power of two */
#define LIM_LA        72          /* limiter look-ahead (1.5 ms) */
#define LIM_THRESH    0.84f
#define LOOP_TIMEOUT  (AU_RATE / 4)  /* loops not refreshed for 250 ms fade out */
#define MERGE_WINDOW  (AU_RATE / 100) /* identical sfx within 10 ms are merged */
#define MAX_TALK      24          /* talk voices (audio_voice) */

enum { CMD_PLAY, CMD_MUSIC, CMD_SAY };
typedef struct { uint8_t type, id, var; float vol, pan, pitch; } Cmd;   /* CMD_SAY: id = voice, var = syllable */

typedef struct {
    const AuSample *s;
    double pos;
    float rate, vol, gl, gr, width;
    float fade, fade_step;
    uint64_t start;
    int id;
    uint8_t active, dying;
} Voice;

typedef struct { float vol, pitch; int active; } LoopState;

static struct {
    SDL_AudioStream *stream;
    bool inited;
    AuSample sfx[SFX_COUNT][AU_SFX_MAXVAR];
    int nvar[SFX_COUNT];
    AuSample talk[MAX_TALK][AUDIO_SYLLABLES];   /* written on the game thread before their handle is ever sent */
    TalkVoice talk_def[MAX_TALK];
    int ntalk;
    uint8_t last_var[SFX_COUNT];
    AuRng rng;
    Voice v[MAX_SLOTS];
    LoopState loop[LOOP_COUNT];
    uint64_t clock;
    float master_s, music_s, sfx_s, muffle_s, ts_s;
    AuSvf muf[4];
    float lim_dl[2][LIM_LA];
    int lim_pos, lim_cnt;
    float lim_hold, lim_g;
    double init_ms;
} M;

/* game thread -> audio thread shared state */
static Cmd ring[CMD_RING];
static atomic_uint ring_head, ring_tail;
static atomic_uint a_master, a_music, a_sfx, a_muffle, a_ts;
static atomic_uint a_loop_vol[LOOP_COUNT], a_loop_pitch[LOOP_COUNT];
static _Atomic uint64_t a_loop_stamp[LOOP_COUNT];
static _Atomic uint64_t a_clock;
static int g_music_req = MUS_NONE;   /* game-thread view of the requested track */
static bool g_vol_set, g_muffle_set, g_ts_set;   /* settings made before audio_init survive it */

static inline uint32_t f2u(float f) { union { float f; uint32_t u; } c; c.f = f; return c.u; }
static inline float u2f(uint32_t u) { union { float f; uint32_t u; } c; c.u = u; return c.f; }
static inline float ldf(atomic_uint *a) { return u2f(atomic_load_explicit(a, memory_order_relaxed)); }
static inline void stf(atomic_uint *a, float f) { atomic_store_explicit(a, f2u(f), memory_order_relaxed); }

/* max simultaneous instances per sfx (oldest instance is faded out beyond this); a talk voice's id is SFX_COUNT + its handle */
static int sfx_cap(int id)
{
    if (id >= SFX_COUNT) return 2;
    switch (id) {
    case SFX_FOOTSTEP: case SFX_CART_ROLL: case SFX_LOOT_RUMMAGE: return 3;
    case SFX_TYPE: case SFX_UI_MOVE: case SFX_COMBO: case SFX_HEARTBEAT: case SFX_RADIO: return 2;
    case SFX_ENGINE: case SFX_LEVEL_CLEAR: case SFX_GAME_OVER: case SFX_LIST_DONE: case SFX_VAN_DOOR: return 1;
    case SFX_SHELL: case SFX_GORE: return 6;
    default: return 5;
    }
}

/* ------------------------------------------------------------------------ */
/* voices                                                                    */
/* ------------------------------------------------------------------------ */
static void pan_gains(float pan, float *gl, float *gr)
{
    pan = au_clampf(pan, -1.0f, 1.0f);
    float th = (pan + 1.0f) * (AU_PI * 0.25f);
    float l = cosf(th) * 1.41421356f, r = sinf(th) * 1.41421356f;
    *gl = l > 1.0f ? 1.0f : l;
    *gr = r > 1.0f ? 1.0f : r;
}

static float voice_loudness(const Voice *v)
{
    const AuSample *s = v->s;
    int b = (int)(v->pos / AU_ENV_BLOCK);
    float e = (s->env && b < s->env_n) ? s->env[b] : 0.0f;
    return e * v->vol * v->fade;
}

static void voice_kill(Voice *v)
{
    if (!v->active || v->dying) return;
    v->dying = 1;
    v->fade_step = v->fade / (0.006f * AU_FRATE);  /* 6 ms fade */
}

/* subtle built-in pitch variation for sounds that repeat a lot (on top of the caller's pitch) */
static float sfx_jitter(int id)
{
    switch (id) {
    case SFX_FOOTSTEP: case SFX_SHELL: case SFX_CART_ROLL: case SFX_LOOT_RUMMAGE: return 0.06f;
    case SFX_SWING: case SFX_SWING_HEAVY: case SFX_GORE: case SFX_BODYFALL: case SFX_TYPE: case SFX_THROW: return 0.05f;
    case SFX_PUNCH: case SFX_HIT_BLUNT: case SFX_HIT_BLADE: case SFX_HIT_METAL: case SFX_BONE_CRUNCH: case SFX_GLASS_BREAK:
    case SFX_BOTTLE_BREAK: case SFX_CART_HIT: case SFX_DOOR_SLAM: case SFX_RICOCHET: return 0.035f;
    case SFX_PISTOL: case SFX_REVOLVER: case SFX_SHOTGUN: case SFX_RIFLE: case SFX_NAILGUN: case SFX_EMPTY: case SFX_RELOAD:
    case SFX_EXECUTE: case SFX_FLAME_PUFF: case SFX_EXPLOSION: return 0.02f;
    default: return 0.0f;   /* musical / UI / voice sounds: the caller decides */
    }
}

static const AuSample *pick_sfx(int id)
{
    int nv = M.nvar[id] > 0 ? M.nvar[id] : 1;
    int var = 0;
    if (nv > 1) {   /* round robin without immediate repeats */
        var = (int)(au_rng_next(&M.rng) % (uint32_t)(nv - 1));
        if (var >= M.last_var[id]) var++;
        M.last_var[id] = (uint8_t)var;
    }
    return &M.sfx[id][var];
}

static void start_voice(int id, const AuSample *s, float vol, float pan, float pitch)
{
    if (!s->data || vol <= 0.0001f) return;
    float jit = sfx_jitter(id);
    if (jit > 0.0f) pitch *= 1.0f + jit * au_rnd2(&M.rng);
    int live = 0, same = 0;
    Voice *oldest_same = NULL;
    for (int i = 0; i < MAX_SLOTS; i++) {
        Voice *v = &M.v[i];
        if (!v->active || v->dying) continue;
        live++;
        if (v->id != id) continue;
        if (M.clock - v->start < MERGE_WINDOW) {
            /* stacking identical sounds on the same sample just makes them louder and
               phasey - keep one, at the louder volume, averaged position */
            if (vol > v->vol) v->vol = vol;
            float pl, pr; pan_gains(pan, &pl, &pr);
            v->gl = 0.5f * (v->gl + pl); v->gr = 0.5f * (v->gr + pr);
            return;
        }
        same++;
        if (!oldest_same || v->start < oldest_same->start) oldest_same = v;
    }
    if (same >= sfx_cap(id) && oldest_same) { voice_kill(oldest_same); live--; }
    if (live >= MAX_LIVE) {
        Voice *victim = NULL; float best = 1e30f;
        for (int i = 0; i < MAX_SLOTS; i++) {
            Voice *v = &M.v[i];
            if (!v->active || v->dying) continue;
            float l = voice_loudness(v);
            /* bias towards stealing older voices on ties */
            l *= 1.0f + 1e-7f * (float)(v->start & 0xFFFFFF);
            if (l < best) { best = l; victim = v; }
        }
        if (victim) voice_kill(victim);
    }
    Voice *slot = NULL;
    for (int i = 0; i < MAX_SLOTS; i++) if (!M.v[i].active) { slot = &M.v[i]; break; }
    if (!slot) { /* every slot busy: reuse the most-faded dying voice */
        float lo = 2.0f;
        for (int i = 0; i < MAX_SLOTS; i++)
            if (M.v[i].dying && M.v[i].fade < lo) { lo = M.v[i].fade; slot = &M.v[i]; }
        if (!slot) return;
    }
    memset(slot, 0, sizeof *slot);
    slot->s = s;
    slot->id = id;
    slot->vol = vol;
    slot->rate = pitch;
    slot->fade = 1.0f;
    slot->start = M.clock;
    pan_gains(pan, &slot->gl, &slot->gr);
    slot->width = 1.0f - 0.6f * fabsf(au_clampf(pan, -1.0f, 1.0f));
    slot->active = 1;
}

static void mix_voice(Voice *v, float *L, float *R, int n, float ts)
{
    const AuSample *s = v->s;
    const float *d = s->data;
    const int last = s->frames - 1;
    double pos = v->pos;
    const double step = (double)(v->rate * ts);
    const float gl = v->gl * v->vol, gr = v->gr * v->vol;
    float fade = v->fade;
    const float fstep = v->dying ? v->fade_step : 0.0f;
    int i = 0;
    if (s->channels == 1) {
        for (; i < n; i++) {
            int idx = (int)pos;
            if (idx >= last) { v->active = 0; break; }
            float f = (float)(pos - (double)idx);
            float a = d[idx], b = d[idx + 1];
            float x = (a + (b - a) * f) * fade;
            L[i] += x * gl; R[i] += x * gr;
            pos += step;
            if (fstep > 0.0f) { fade -= fstep; if (fade <= 0.0f) { v->active = 0; break; } }
        }
    } else {
        const float w = v->width;
        for (; i < n; i++) {
            int idx = (int)pos;
            if (idx >= last) { v->active = 0; break; }
            float f = (float)(pos - (double)idx);
            const float *p = d + idx * 2;
            float l = p[0] + (p[2] - p[0]) * f;
            float r = p[1] + (p[3] - p[1]) * f;
            float m = 0.5f * (l + r), sd = 0.5f * (l - r) * w;
            L[i] += (m * gl + sd * v->vol) * fade;
            R[i] += (m * gr - sd * v->vol) * fade;
            pos += step;
            if (fstep > 0.0f) { fade -= fstep; if (fade <= 0.0f) { v->active = 0; break; } }
        }
    }
    v->pos = pos;
    v->fade = fade;
}

/* ------------------------------------------------------------------------ */
/* master mix                                                                */
/* ------------------------------------------------------------------------ */
static void drain_commands(void)
{
    unsigned head = atomic_load_explicit(&ring_head, memory_order_acquire);
    unsigned tail = atomic_load_explicit(&ring_tail, memory_order_relaxed);
    while (tail != head) {
        Cmd c = ring[tail & (CMD_RING - 1)];
        if (c.type == CMD_PLAY) start_voice(c.id, pick_sfx(c.id), c.vol, c.pan, c.pitch);
        else if (c.type == CMD_SAY) start_voice(SFX_COUNT + c.id, &M.talk[c.id][c.var], c.vol, c.pan, c.pitch);
        else if (c.type == CMD_MUSIC) au_music_command(c.id);
        tail++;
    }
    atomic_store_explicit(&ring_tail, tail, memory_order_release);
}

static inline float soft_clip(float x)
{
    float a = fabsf(x);
    if (a <= 0.9f) return x;
    float y = 0.9f + 0.1f * au_sat((a - 0.9f) * 10.0f);
    return x < 0.0f ? -y : y;
}

static void mix_block(float *out, int n)
{
    float sL[MIX_BLOCK], sR[MIX_BLOCK], mL[MIX_BLOCK], mR[MIX_BLOCK];
    drain_commands();

    const float cvol = au_tc_coef(0.04f, n);
    float master0 = M.master_s, music0 = M.music_s, sfx0 = M.sfx_s;
    M.master_s += (au_clampf(ldf(&a_master), 0.0f, 1.0f) - M.master_s) * cvol;
    M.music_s  += (au_clampf(ldf(&a_music), 0.0f, 1.0f) - M.music_s) * cvol;
    M.sfx_s    += (au_clampf(ldf(&a_sfx), 0.0f, 1.0f) - M.sfx_s) * cvol;
    M.ts_s     += (au_clampf(ldf(&a_ts), 0.05f, 2.0f) - M.ts_s) * au_tc_coef(0.09f, n);
    M.muffle_s += (au_clampf(ldf(&a_muffle), 0.0f, 1.0f) - M.muffle_s) * au_tc_coef(0.10f, n);
    const float ts = M.ts_s;

    memset(sL, 0, sizeof(float) * (size_t)n); memset(sR, 0, sizeof(float) * (size_t)n);
    memset(mL, 0, sizeof(float) * (size_t)n); memset(mR, 0, sizeof(float) * (size_t)n);

    for (int i = 0; i < MAX_SLOTS; i++)
        if (M.v[i].active) mix_voice(&M.v[i], sL, sR, n, ts);

    const float cl = au_tc_coef(0.05f, n), cp = au_tc_coef(0.07f, n);
    for (int i = 0; i < LOOP_COUNT; i++) {
        LoopState *ls = &M.loop[i];
        float tv = au_clampf(ldf(&a_loop_vol[i]), 0.0f, 2.0f);
        float tp = au_clampf(ldf(&a_loop_pitch[i]), 0.1f, 4.0f);
        uint64_t st = atomic_load_explicit(&a_loop_stamp[i], memory_order_relaxed);
        if (M.clock > st + LOOP_TIMEOUT) tv = 0.0f;
        if (!ls->active) {
            if (tv < 0.0005f) continue;
            ls->active = 1; ls->vol = 0.0f; ls->pitch = tp;
            au_loop_reset(i);
        }
        float v0 = ls->vol;
        ls->vol += (tv - ls->vol) * cl;
        ls->pitch += (tp - ls->pitch) * cp;
        au_loop_render(i, sL, sR, n, ls->pitch * ts, v0, ls->vol);
        if (tv < 0.0005f && ls->vol < 0.0005f) ls->active = 0;
    }

    au_music_render(mL, mR, n, ts);

    /* muffle: 4th-order Butterworth low-pass, 22 kHz (transparent) .. ~280 Hz */
    float mu = M.muffle_s;
    float fc = 22000.0f * powf(280.0f / 22000.0f, powf(mu, 0.75f));
    au_svf_set(&M.muf[0], fc, 0.5412f); au_svf_copycoef(&M.muf[2], &M.muf[0]);
    au_svf_set(&M.muf[1], fc, 1.3066f); au_svf_copycoef(&M.muf[3], &M.muf[1]);
    const float mgain = 1.0f - 0.22f * mu;
    /* fully transparent when not muffled: blend the filter in over the first 4% */
    const float mwet = au_clampf(mu * 25.0f, 0.0f, 1.0f);

    const float inv = 1.0f / (float)n;
    const float att = 1.0f - expf(-6.0f / (float)LIM_LA);
    const float rel = au_tc_coef(0.12f, 1);
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) * inv;
        float gs = au_lerpf(sfx0, M.sfx_s, t), gm = au_lerpf(music0, M.music_s, t);
        float gmaster = au_lerpf(master0, M.master_s, t) * mgain;
        float l = sL[i] * gs + mL[i] * gm;
        float r = sR[i] * gs + mR[i] * gm;
        float fl = au_svf_lp(&M.muf[1], au_svf_lp(&M.muf[0], l));
        float fr = au_svf_lp(&M.muf[3], au_svf_lp(&M.muf[2], r));
        l += (fl - l) * mwet;
        r += (fr - r) * mwet;
        l *= gmaster; r *= gmaster;

        /* look-ahead peak limiter followed by a soft clipper */
        float pk = fmaxf(fabsf(l), fabsf(r));
        float tgt = pk > LIM_THRESH ? LIM_THRESH / pk : 1.0f;
        if (tgt <= M.lim_hold) { M.lim_hold = tgt; M.lim_cnt = LIM_LA; }
        else if (M.lim_cnt > 0) M.lim_cnt--;
        else M.lim_hold += (1.0f - M.lim_hold) * rel;
        if (M.lim_hold < M.lim_g) M.lim_g += (M.lim_hold - M.lim_g) * att;
        else M.lim_g += (M.lim_hold - M.lim_g) * 0.02f;
        float dl = M.lim_dl[0][M.lim_pos], dr = M.lim_dl[1][M.lim_pos];
        M.lim_dl[0][M.lim_pos] = l; M.lim_dl[1][M.lim_pos] = r;
        if (++M.lim_pos >= LIM_LA) M.lim_pos = 0;
        out[i * 2 + 0] = soft_clip(dl * M.lim_g);
        out[i * 2 + 1] = soft_clip(dr * M.lim_g);
    }
    M.clock += (uint64_t)n;
    atomic_store_explicit(&a_clock, M.clock, memory_order_relaxed);
}

/* flush denormals to zero on the audio thread (decaying filter/reverb tails) */
static void denormals_off(void)
{
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#elif defined(__aarch64__) && (defined(__clang__) || defined(__GNUC__))
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (uint64_t)1 << 24;
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
}

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    (void)userdata; (void)total_amount;
    denormals_off();
    int frames = (additional_amount + 7) / 8;
    float out[MIX_BLOCK * 2];
    while (frames > 0) {
        int n = frames < MIX_BLOCK ? frames : MIX_BLOCK;
        mix_block(out, n);
        SDL_PutAudioStreamData(stream, out, n * 2 * (int)sizeof(float));
        frames -= n;
    }
}

/* ------------------------------------------------------------------------ */
/* init / shutdown                                                           */
/* ------------------------------------------------------------------------ */
static bool init_common(void)
{
    if (M.inited) return true;
    Uint64 t0 = SDL_GetPerformanceCounter();
    memset(&M, 0, sizeof M);
    atomic_store(&ring_head, 0); atomic_store(&ring_tail, 0);
    if (!g_vol_set) { stf(&a_master, 1.0f); stf(&a_music, 0.8f); stf(&a_sfx, 1.0f); }
    if (!g_muffle_set) stf(&a_muffle, 0.0f);
    if (!g_ts_set) stf(&a_ts, 1.0f);
    for (int i = 0; i < LOOP_COUNT; i++) {
        stf(&a_loop_vol[i], 0.0f); stf(&a_loop_pitch[i], 1.0f);
        atomic_store(&a_loop_stamp[i], 0);
    }
    atomic_store(&a_clock, 0);
    M.master_s = ldf(&a_master); M.music_s = ldf(&a_music); M.sfx_s = ldf(&a_sfx);
    M.muffle_s = ldf(&a_muffle); M.ts_s = ldf(&a_ts);
    M.lim_hold = 1.0f; M.lim_g = 1.0f;
    g_music_req = MUS_NONE;

    au_rng_seed(&M.rng, 0x51F15EEDu);
    if (!au_sfx_build(M.sfx, M.nvar)) return false;
    au_loops_init();
    if (!au_music_init()) return false;
    M.init_ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    M.inited = true;
    return true;
}

static void free_all(void)
{
    au_music_shutdown();
    for (int i = 0; i < SFX_COUNT; i++)
        for (int v = 0; v < AU_SFX_MAXVAR; v++) au_sample_free(&M.sfx[i][v]);
    for (int i = 0; i < M.ntalk; i++)
        for (int v = 0; v < AUDIO_SYLLABLES; v++) au_sample_free(&M.talk[i][v]);
    M.ntalk = 0;
    M.inited = false;
}

bool audio_init(void)
{
    if (M.inited) return true;
    if (!init_common()) { free_all(); return false; }
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512", SDL_HINT_DEFAULT);
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = AU_RATE;
    M.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
    if (!M.stream) {
        SDL_Log("audio: could not open device: %s", SDL_GetError());
        free_all();
        return false;
    }
    SDL_ResumeAudioStreamDevice(M.stream);
    SDL_Log("audio: ready (synthesis %.0f ms)", M.init_ms);
    return true;
}

void audio_shutdown(void)
{
    if (!M.inited) return;
    if (M.stream) { SDL_DestroyAudioStream(M.stream); M.stream = NULL; }
    free_all();
}

/* ------------------------------------------------------------------------ */
/* public API (game thread)                                                  */
/* ------------------------------------------------------------------------ */
/* reject NaN / inf coming from game code (one NaN would poison the whole mix) */
static float sane(float x, float def) { return (x == x && x > -1e30f && x < 1e30f) ? x : def; }

static void push_cmd(Cmd c)
{
    unsigned head = atomic_load_explicit(&ring_head, memory_order_relaxed);
    unsigned tail = atomic_load_explicit(&ring_tail, memory_order_acquire);
    if (head - tail >= CMD_RING) return;   /* full: drop */
    ring[head & (CMD_RING - 1)] = c;
    atomic_store_explicit(&ring_head, head + 1, memory_order_release);
}

void audio_play(SfxId id, float vol, float pan, float pitch)
{
    if (!M.inited || (int)id < 0 || id >= SFX_COUNT) return;
    vol = sane(vol, 0.0f); pan = sane(pan, 0.0f); pitch = sane(pitch, 1.0f);
    if (!(vol > 0.0f)) return;
    if (!(pitch > 0.0f)) pitch = 1.0f;
    Cmd c = { CMD_PLAY, (uint8_t)id, 0, au_clampf(vol, 0.0f, 2.0f), au_clampf(pan, -1.0f, 1.0f), au_clampf(pitch, 0.05f, 8.0f) };
    push_cmd(c);
}

int audio_voice(const TalkVoice *v)
{
    if (!M.inited || !v) return -1;
    for (int i = 0; i < M.ntalk; i++) if (!memcmp(&M.talk_def[i], v, sizeof *v)) return i;
    if (M.ntalk >= MAX_TALK) { SDL_Log("audio: no room for another talk voice"); return -1; }
    int h = M.ntalk;
    if (!au_talk_build(v, M.talk[h])) return -1;
    M.talk_def[h] = *v;
    M.ntalk++;   /* the audio thread only ever looks at it once a CMD_SAY for it comes through the ring */
    return h;
}

void audio_say(int voice, int syllable, float vol, float pan, float pitch)
{
    if (!M.inited || voice < 0 || voice >= M.ntalk) return;
    vol = sane(vol, 0.0f); pan = sane(pan, 0.0f); pitch = sane(pitch, 1.0f);
    if (!(vol > 0.0f)) return;
    if (!(pitch > 0.0f)) pitch = 1.0f;
    syllable %= AUDIO_SYLLABLES;
    if (syllable < 0) syllable += AUDIO_SYLLABLES;
    Cmd c = { CMD_SAY, (uint8_t)voice, (uint8_t)syllable, au_clampf(vol, 0.0f, 2.0f), au_clampf(pan, -1.0f, 1.0f), au_clampf(pitch, 0.05f, 8.0f) };
    push_cmd(c);
}

void audio_loop(LoopId id, float vol, float pitch)
{
    if (!M.inited || (int)id < 0 || id >= LOOP_COUNT) return;
    vol = sane(vol, 0.0f); pitch = sane(pitch, 1.0f);
    if (!(vol >= 0.0f)) vol = 0.0f;
    if (!(pitch > 0.0f)) pitch = 1.0f;
    stf(&a_loop_vol[id], vol);
    stf(&a_loop_pitch[id], pitch);
    atomic_store_explicit(&a_loop_stamp[id], atomic_load_explicit(&a_clock, memory_order_relaxed), memory_order_relaxed);
}

void audio_music(MusicId id)
{
    if (!M.inited || (int)id < 0 || id >= MUS_COUNT) return;
    if ((int)id == g_music_req) return;
    g_music_req = (int)id;
    Cmd c = { CMD_MUSIC, (uint8_t)id, 0, 0, 0, 0 };
    push_cmd(c);
}

void audio_set_volumes(float master, float music, float sfx)
{
    g_vol_set = true;
    stf(&a_master, au_clampf(sane(master, 1.0f), 0.0f, 1.0f));
    stf(&a_music, au_clampf(sane(music, 0.8f), 0.0f, 1.0f));
    stf(&a_sfx, au_clampf(sane(sfx, 1.0f), 0.0f, 1.0f));
}

void audio_set_muffle(float amount) { g_muffle_set = true; stf(&a_muffle, au_clampf(sane(amount, 0.0f), 0.0f, 1.0f)); }
void audio_set_timescale(float scale) { g_ts_set = true; stf(&a_ts, au_clampf(sane(scale, 1.0f), 0.05f, 2.0f)); }

/* ------------------------------------------------------------------------ */
/* offline (tools)                                                           */
/* ------------------------------------------------------------------------ */
bool audio_init_offline(void)
{
    if (M.inited) return true;
    if (!init_common()) { free_all(); return false; }
    return true;
}

void audio_render_offline(float *out, int frames)
{
    if (!M.inited) { memset(out, 0, sizeof(float) * 2 * (size_t)frames); return; }
    while (frames > 0) {
        int n = frames < MIX_BLOCK ? frames : MIX_BLOCK;
        mix_block(out, n);
        out += n * 2;
        frames -= n;
    }
}

const AuSample *audio_get_sfx(SfxId id)
{
    if ((int)id < 0 || id >= SFX_COUNT) return NULL;
    return &M.sfx[id][0];
}

const AuSample *audio_get_sfx_variant(SfxId id, int variant)
{
    if ((int)id < 0 || id >= SFX_COUNT || variant < 0 || variant >= M.nvar[id]) return NULL;
    return &M.sfx[id][variant];
}

double audio_init_ms(void) { return M.init_ms; }
uint64_t audio_mixed_frames(void) { return atomic_load_explicit(&a_clock, memory_order_relaxed); }
