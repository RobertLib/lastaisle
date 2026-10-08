/* LAST AISLE - audio internals shared between src/audio*.c and tools/audio_render.c.
   Not part of the public game API (see audio.h). */
#ifndef AUDIO_INTERNAL_H
#define AUDIO_INTERNAL_H

#include "audio.h"
#include <stdint.h>
#include <stddef.h>
#include <math.h>

#define AU_RATE   48000
#define AU_FRATE  48000.0f
#define AU_PI     3.14159265358979323846f
#define AU_TAU    6.28318530717958647692f

/* ------------------------------------------------------------------------ */
/* small inline DSP helpers                                                  */
/* ------------------------------------------------------------------------ */

typedef struct { uint32_t s; } AuRng;

static inline void au_rng_seed(AuRng *r, uint32_t s) { r->s = s ? s : 0x9E3779B9u; }
static inline uint32_t au_rng_next(AuRng *r)
{
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->s = x;
    return x;
}
/* uniform [0,1) */
static inline float au_rnd(AuRng *r) { return (float)(au_rng_next(r) >> 8) * (1.0f / 16777216.0f); }
/* uniform [-1,1) */
static inline float au_rnd2(AuRng *r) { return au_rnd(r) * 2.0f - 1.0f; }
static inline float au_rrange(AuRng *r, float a, float b) { return a + (b - a) * au_rnd(r); }

static inline float au_clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float au_lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float au_mtof(float m) { return 440.0f * exp2f((m - 69.0f) * (1.0f / 12.0f)); }
static inline float au_db(float db) { return powf(10.0f, db * 0.05f); }

/* cheap tanh-like saturator (Pade), exact-ish within +-3, hard limit beyond */
static inline float au_sat(float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* smooth saturator with no hard knee (fewer aliasing harmonics than a clipper) */
static inline float au_softsat(float x) { return x / sqrtf(1.0f + x * x); }

/* fast sine of a phase given in turns (any real value). ~1e-6 abs error. */
static inline float au_sin(float turns)
{
    float p = turns - floorf(turns + 0.5f);      /* [-0.5, 0.5) */
    float x = p * AU_TAU;                        /* [-pi, pi) */
    if (x > 1.5707963f) x = AU_PI - x;
    else if (x < -1.5707963f) x = -AU_PI - x;
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.6666667e-1f + x2 * (8.3333333e-3f + x2 * (-1.9841270e-4f + x2 * 2.7557319e-6f))));
}

/* polyBLEP residual */
static inline float au_blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static inline float au_saw(float ph, float dt) { return 2.0f * ph - 1.0f - au_blep(ph, dt); }
static inline float au_pulse(float ph, float dt, float pw)
{
    float v = ph < pw ? 1.0f : -1.0f;
    v += au_blep(ph, dt);
    float t2 = ph - pw; if (t2 < 0.0f) t2 += 1.0f;
    v -= au_blep(t2, dt);
    return v;
}
static inline float au_tri(float ph) { return 4.0f * fabsf(ph - 0.5f) - 1.0f; }

/* TPT state variable filter (Zavalishin / Cytomic) */
typedef struct { float ic1, ic2, a1, a2, a3, k; } AuSvf;

static inline void au_svf_set(AuSvf *f, float fc, float q)
{
    if (fc < 8.0f) fc = 8.0f;
    if (fc > AU_FRATE * 0.47f) fc = AU_FRATE * 0.47f;
    if (q < 0.3f) q = 0.3f;
    float g = tanf(AU_PI * fc / AU_FRATE);
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}
static inline void au_svf_copycoef(AuSvf *d, const AuSvf *s) { d->a1 = s->a1; d->a2 = s->a2; d->a3 = s->a3; d->k = s->k; }
static inline void au_svf_reset(AuSvf *f) { f->ic1 = f->ic2 = 0.0f; }
static inline void au_svf_tick(AuSvf *f, float x, float *lp, float *bp, float *hp)
{
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    if (lp) *lp = v2;
    if (bp) *bp = v1;
    if (hp) *hp = x - f->k * v1 - v2;
}
static inline float au_svf_lp(AuSvf *f, float x)
{
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    return v2;
}
static inline float au_svf_bp(AuSvf *f, float x)
{
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    return v1;
}
static inline float au_svf_hp(AuSvf *f, float x)
{
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    return x - f->k * v1 - v2;
}

/* one-pole smoothing coefficient for a cutoff in Hz */
static inline float au_op_coef(float fc) { return 1.0f - expf(-AU_TAU * fc / AU_FRATE); }
/* one-pole coefficient for a time constant in seconds over n samples */
static inline float au_tc_coef(float tau_s, int n) { return 1.0f - expf(-(float)n / (tau_s * AU_FRATE)); }

/* ------------------------------------------------------------------------ */
/* FDN reverb (audio_dsp.c)                                                  */
/* ------------------------------------------------------------------------ */
typedef struct {
    float *mem;
    float *d[8];  int len[8];  int pos[8];
    float g[8], lp[8];
    float damp;
    float *ap[4]; int aplen[4]; int appos[4];
    float *pre[2]; int prelen, premax, prepos;
    float size;
} AuReverb;

int  au_reverb_init(AuReverb *r, float size, float max_predelay_s);
void au_reverb_set(AuReverb *r, float decay_s, float damp_hz, float predelay_s);
void au_reverb_clear(AuReverb *r);
void au_reverb_free(AuReverb *r);
void au_reverb_tick(AuReverb *r, float inl, float inr, float *outl, float *outr);

/* ------------------------------------------------------------------------ */
/* sample buffers                                                            */
/* ------------------------------------------------------------------------ */
#define AU_ENV_BLOCK 256
typedef struct {
    float *data;      /* interleaved when channels == 2 */
    int frames;
    int channels;
    float *env;       /* coarse peak envelope, one value per AU_ENV_BLOCK frames */
    int env_n;
} AuSample;

void au_sample_free(AuSample *s);
void au_sample_make_env(AuSample *s);

/* audio_sfx.c - frequently repeated sounds get several round-robin variants */
#define AU_SFX_MAXVAR 4
int  au_sfx_build(AuSample out[SFX_COUNT][AU_SFX_MAXVAR], int nvar[SFX_COUNT]);
const char *au_sfx_name(int id);
/* audio_sfx.c - a talk voice's syllables (audio_voice) */
int  au_talk_build(const TalkVoice *v, AuSample out[AUDIO_SYLLABLES]);

/* audio_loops.c - real-time loop generators. Adds into L/R. rate = pitch*timescale.
   vol0 -> vol1 is ramped linearly across the n samples. */
void au_loops_init(void);
void au_loop_reset(int id);
void au_loop_render(int id, float *L, float *R, int n, float rate, float vol0, float vol1);
const char *au_loop_name(int id);

/* ------------------------------------------------------------------------ */
/* music (audio_synth.c engine, audio_songs.c data)                          */
/* ------------------------------------------------------------------------ */
#define AU_MCH 8          /* channel 0 = drums, 1..7 melodic */

enum { AW_SAW, AW_SQUARE, AW_PULSE, AW_TRI, AW_SINE, AW_NOISE, AW_NONE };

typedef struct {
    uint8_t w1, w2;            /* waveforms */
    float mix2, semi2, det2;   /* osc2 level, transpose (semitones), detune (cents) */
    int   uni;                 /* osc1 unison count 1..7 */
    float uni_det, uni_width;  /* total unison spread (cents), stereo width 0..1 */
    float sub, noise;          /* sine sub-octave and noise levels */
    float pw, pwm, pwm_rate;   /* pulse width, PWM depth, PWM rate (Hz) */
    float fm_ratio, fm_index, fm_sus, fm_decay; /* 2-op FM when fm_index > 0 (w1 = carrier) */
    float cutoff, reso, fenv, keytrk; /* Hz, 0..1, octaves, 0..1 */
    float fa, fd, fs, fr;      /* filter envelope ADSR (seconds / level) */
    float aa, ad, as, ar;      /* amp envelope ADSR */
    float glide;               /* portamento time (s) for '~' notes */
    float vib, vib_rate, vib_delay; /* vibrato depth (semitones), rate, onset delay */
    float wow;                 /* slow random pitch drift (cents) - woozy tape feel */
    float drive;               /* saturation amount (0 = clean) */
    float penv, pdecay;        /* pitch envelope (semitones) and decay (s) */
    float flfo, flfo_rate;     /* filter LFO depth (octaves) and rate */
    float vel_filt;            /* velocity -> cutoff (octaves) */
    float gain;
    uint8_t mono, poles4;
} AuPatch;

typedef struct {
    int bars;
    const char *chords;            /* one token per bar ("Am", "F_G" = half bars, "(...)N" repeats) */
    const char *pat[AU_MCH];       /* per-channel pattern (NULL = silent). [0] = drum lanes */
    float lp0, lp1;                /* music low-pass openness over the section (0..1). 0,0 = open */
    int xpose;                     /* section transposition in semitones */
} AuSection;

typedef struct {
    const char *name;
    float bpm, swing;
    int kit;
    const AuPatch *patch[AU_MCH];
    int   center[AU_MCH];          /* MIDI centre for chord-relative notes */
    int   xp[AU_MCH];              /* channel transposition (semitones) */
    float gain[AU_MCH], pan[AU_MCH], dsend[AU_MCH], rsend[AU_MCH], duck[AU_MCH];
    float delay_beats, delay_fb, delay_wow;
    float rev_decay, rev_damp;
    float duck_time;
    float master;
    const AuSection *sec;
    int nsec, loop_to;
} AuSong;

enum { AU_KIT_SYNTHWAVE, AU_KIT_HYPNO, AU_KIT_INDUSTRIAL, AU_KIT_SOFT, AU_KIT_COUNT };

extern const AuSong *const au_songs[MUS_COUNT];

int  au_music_init(void);
void au_music_shutdown(void);
void au_music_command(int id);                        /* audio thread only */
void au_music_render(float *L, float *R, int n, float rate); /* adds into L/R */
const char *au_music_name(int id);
int  au_music_validate(void);                         /* prints pattern problems; returns count */
double au_music_song_seconds(int id);                 /* nominal length of one pass */
/* tools: per-channel metering (after channel gain + song master; RMS only while the channel sounds) */
void au_music_meter(int enable, float rms_db[AU_MCH], float peak_db[AU_MCH]);
/* tools: print note events of steps [from, to) of the given song while rendering; returns max voices used */
int  au_music_trace(int song, int from_step, int to_step);
/* tools: drum kit sample access (piece 0..13), NULL when out of range */
const AuSample *au_music_kit_sample(int kit, int piece, const char **name);

/* ------------------------------------------------------------------------ */
/* offline entry points (for tools/audio_render.c)                           */
/* ------------------------------------------------------------------------ */
bool audio_init_offline(void);              /* full init (synthesis) without opening a device */
void audio_render_offline(float *interleaved_stereo, int frames);   /* runs the real mixer */
const AuSample *audio_get_sfx(SfxId id);
const AuSample *audio_get_sfx_variant(SfxId id, int variant);   /* NULL past the last variant */
double audio_init_ms(void);
uint64_t audio_mixed_frames(void);   /* frames produced by the mixer so far */

#endif
