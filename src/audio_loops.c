/* LAST AISLE - audio: real-time loop generators (chainsaw, fire, rain, wind, hum).
   Generated sample by sample so they never repeat audibly and respond to
   pitch (RPM / intensity) smoothly. All run on the audio thread. */
#include "audio_internal.h"
#include <string.h>

typedef struct {
    AuRng rng;
    /* engine */
    float fire_ph, pulse, rpm_wob, wob_t, misfire, load;
    AuSvf r1, r2, r3, ex, chain, hp;
    float saw_ph;
    /* noise beds / generic */
    AuSvf fa[4], fb[4];
    float brown[2], lfo[4], lfo_t[4];
    float ev_t, ev_amp, ev_env, ev_pan;
    AuSvf ev_f;
    float ev2_t, ev2_env;
    AuSvf ev2_f;
    float ph[6];
    int ctl;
} LoopGen;

static LoopGen gens[LOOP_COUNT];

static const char *loop_names[LOOP_COUNT] = { "chainsaw_idle", "chainsaw_cut", "fire", "rain", "wind", "hum" };
const char *au_loop_name(int id) { return (id >= 0 && id < LOOP_COUNT) ? loop_names[id] : "?"; }

void au_loop_reset(int id)
{
    LoopGen *g = &gens[id];
    uint32_t seed = g->rng.s;
    memset(g, 0, sizeof *g);
    au_rng_seed(&g->rng, seed ? seed : 0xC0FFEEu + (uint32_t)id * 977u);
    g->load = 1.0f;
}

void au_loops_init(void)
{
    for (int i = 0; i < LOOP_COUNT; i++) { gens[i].rng.s = 0; au_loop_reset(i); }
}

/* smooth random walk in [-1,1] */
static inline float wander(LoopGen *g, int k, float rate, float rnd)
{
    g->lfo_t[k] += rate / AU_FRATE;
    if (g->lfo_t[k] >= 1.0f) { g->lfo_t[k] -= 1.0f; g->lfo[k] = rnd; }
    return g->lfo[k];
}

/* ---------------- chainsaw ---------------- */
static void render_chainsaw(LoopGen *g, float *L, float *R, int n, float rate, float v0, float v1, int cut)
{
    const float base = cut ? 158.0f : 46.0f;   /* combustion pulses per second */
    if (g->ctl == 0) {
        au_svf_set(&g->r1, cut ? 520.0f : 170.0f, 2.5f);
        au_svf_set(&g->r2, cut ? 1350.0f : 640.0f, 3.0f);
        au_svf_set(&g->r3, cut ? 3100.0f : 1900.0f, 2.0f);
        au_svf_set(&g->chain, 3800.0f, 1.2f);
        au_svf_set(&g->hp, 60.0f, 0.7f);
        g->ctl = 1;
    }
    const float sm = 1.0f - expf(-1.0f / ((cut ? 0.05f : 0.15f) * AU_FRATE));
    for (int i = 0; i < n; i++) {
        /* rpm wobble: slow drift + (cut) load sag as the chain bites; targets are
           re-rolled periodically and glided to so the engine never steps */
        g->wob_t += 1.0f / AU_FRATE;
        if (g->wob_t > (cut ? 0.11f : 0.35f)) {
            g->wob_t = 0.0f;
            g->lfo[0] = au_rnd2(&g->rng);
            if (cut) g->lfo[1] = 0.84f + 0.16f * au_rnd(&g->rng);
        }
        g->rpm_wob += (g->lfo[0] - g->rpm_wob) * sm;
        if (cut) g->load += ((g->lfo[1] > 0.0f ? g->lfo[1] : 1.0f) - g->load) * sm;
        float rpm = base * rate * (1.0f + (cut ? 0.035f : 0.06f) * g->rpm_wob) * (cut ? g->load : 1.0f);
        g->fire_ph += rpm / AU_FRATE;
        if (g->fire_ph >= 1.0f) {
            g->fire_ph -= 1.0f;
            /* two-strokes misfire a lot at idle */
            g->pulse = (!cut && au_rnd(&g->rng) < 0.12f) ? 0.25f : 0.75f + 0.25f * au_rnd(&g->rng);
        }
        g->pulse *= cut ? 0.991f : 0.9965f;
        float exc = g->pulse * g->pulse * (1.0f + 0.4f * au_rnd2(&g->rng));
        float body = au_svf_bp(&g->r1, exc) * 1.8f + au_svf_bp(&g->r2, exc) * 1.1f + au_svf_bp(&g->r3, exc) * 0.5f;
        /* buzzy exhaust saw locked to firing rate */
        float dt = rpm / AU_FRATE;
        g->saw_ph += dt; if (g->saw_ph >= 1.0f) g->saw_ph -= 1.0f;
        float buzz = au_saw(g->saw_ph, dt) * (cut ? 0.55f : 0.25f);
        float x = body + buzz;
        if (cut) {
            /* chain teeth grinding: band noise AM'd at firing rate + rough crackle */
            float ch = au_svf_bp(&g->chain, au_rnd2(&g->rng)) * (0.6f + 0.6f * g->pulse) * 1.4f;
            if (au_rnd(&g->rng) < 0.004f) ch += au_rnd2(&g->rng) * 2.0f;
            x += ch;
            x = au_sat(x * 2.6f) * 0.9f;
        } else {
            x = au_sat(x * 1.8f) * 0.9f;
        }
        x = au_svf_hp(&g->hp, x);
        float v = v0 + (v1 - v0) * (float)(i + 1) / (float)n;
        float o = x * v * (cut ? 0.4f : 0.4f);
        L[i] += o; R[i] += o;
    }
}

/* ---------------- fire ---------------- */
static void render_fire(LoopGen *g, float *L, float *R, int n, float rate, float v0, float v1)
{
    if (g->ctl == 0) {
        au_svf_set(&g->fa[0], 380.0f, 0.7f); au_svf_copycoef(&g->fb[0], &g->fa[0]);
        au_svf_set(&g->fa[1], 2600.0f, 0.6f); au_svf_copycoef(&g->fb[1], &g->fa[1]);
        g->ctl = 1;
    }
    for (int i = 0; i < n; i++) {
        /* roar: brown-ish noise with slow flicker */
        float fl = 0.65f + 0.35f * wander(g, 0, 3.0f * rate, au_rnd(&g->rng));
        g->lfo[1] += (fl - g->lfo[1]) * 0.0006f;
        float nl = au_rnd2(&g->rng), nr = au_rnd2(&g->rng);
        g->brown[0] = g->brown[0] * 0.99f + nl * 0.1f;
        g->brown[1] = g->brown[1] * 0.99f + nr * 0.1f;
        float rl = au_svf_lp(&g->fa[0], g->brown[0]) * 2.2f * g->lfo[1];
        float rr = au_svf_lp(&g->fb[0], g->brown[1]) * 2.2f * g->lfo[1];
        /* hiss */
        float hl = au_svf_hp(&g->fa[1], nl) * 0.03f, hr = au_svf_hp(&g->fb[1], nr) * 0.03f;
        /* crackles */
        if (au_rnd(&g->rng) < 16.0f * rate / AU_FRATE) {
            int big = au_rnd(&g->rng) < 0.15f;
            g->ev_env = 1.0f;
            g->ev_amp = big ? 0.9f : 0.15f + 0.45f * au_rnd(&g->rng) * au_rnd(&g->rng);
            g->ev_pan = au_rnd2(&g->rng) * 0.7f;
            au_svf_set(&g->ev_f, big ? au_rrange(&g->rng, 300, 900) : au_rrange(&g->rng, 1200, 5000), big ? 1.5f : 2.5f);
            g->ev_t = big ? 0.9985f : 0.993f;
        }
        float c = 0.0f;
        if (g->ev_env > 1e-4f) {
            c = au_svf_bp(&g->ev_f, au_rnd2(&g->rng) * g->ev_env) * g->ev_amp * 3.0f;
            g->ev_env *= g->ev_t;
        }
        float v = v0 + (v1 - v0) * (float)(i + 1) / (float)n;
        L[i] += (rl + hl + c * (1.0f - g->ev_pan)) * v * 0.15f;
        R[i] += (rr + hr + c * (1.0f + g->ev_pan)) * v * 0.15f;
    }
}

/* ---------------- rain ---------------- */
static void render_rain(LoopGen *g, float *L, float *R, int n, float rate, float v0, float v1)
{
    if (g->ctl == 0) {
        au_svf_set(&g->fa[0], 5200.0f, 0.6f); au_svf_copycoef(&g->fb[0], &g->fa[0]);
        au_svf_set(&g->fa[1], 500.0f, 0.6f); au_svf_copycoef(&g->fb[1], &g->fa[1]);
        au_svf_set(&g->fa[2], 260.0f, 0.8f); au_svf_copycoef(&g->fb[2], &g->fa[2]);
        g->ctl = 1;
    }
    for (int i = 0; i < n; i++) {
        float nl = au_rnd2(&g->rng), nr = au_rnd2(&g->rng);
        /* wash: band-limited noise with slow swell */
        float sw = 0.8f + 0.2f * wander(g, 0, 0.4f, au_rnd(&g->rng));
        g->lfo[1] += (sw - g->lfo[1]) * 0.0002f;
        float wl = au_svf_hp(&g->fa[1], au_svf_lp(&g->fa[0], nl)) * 0.32f * g->lfo[1];
        float wr = au_svf_hp(&g->fb[1], au_svf_lp(&g->fb[0], nr)) * 0.32f * g->lfo[1];
        /* roof drumming: dull low thuds */
        g->brown[0] = g->brown[0] * 0.995f + au_rnd2(&g->rng) * 0.05f;
        float rumble = au_svf_lp(&g->fa[2], g->brown[0]) * 1.2f;
        /* droplets */
        if (au_rnd(&g->rng) < 260.0f * rate / AU_FRATE) {
            g->ev_env = 1.0f;
            g->ev_amp = 0.05f + 0.3f * au_rnd(&g->rng) * au_rnd(&g->rng);
            g->ev_pan = au_rnd2(&g->rng);
            au_svf_set(&g->ev_f, au_rrange(&g->rng, 1800, 7500) * rate, 4.0f);
        }
        float d = 0.0f;
        if (g->ev_env > 1e-4f) { d = au_svf_bp(&g->ev_f, au_rnd2(&g->rng) * g->ev_env) * g->ev_amp * 1.8f; g->ev_env *= 0.985f; }
        float v = v0 + (v1 - v0) * (float)(i + 1) / (float)n;
        L[i] += (wl + rumble + d * (1.0f - 0.6f * g->ev_pan)) * v * 0.28f;
        R[i] += (wr + rumble + d * (1.0f + 0.6f * g->ev_pan)) * v * 0.28f;
    }
}

/* ---------------- wind ---------------- */
static void render_wind(LoopGen *g, float *L, float *R, int n, float rate, float v0, float v1)
{
    for (int i = 0; i < n; i++) {
        if (g->ctl-- <= 0) {
            g->ctl = 32;
            /* centre frequency and gust level random-walk slowly */
            float c0 = wander(g, 0, 0.25f, au_rnd(&g->rng));
            float c1 = wander(g, 1, 0.21f, au_rnd(&g->rng));
            float gu = wander(g, 2, 0.15f, au_rnd(&g->rng));
            g->lfo[3] += (c0 - g->lfo[3]) * 0.004f;
            g->ph[0] += (c1 - g->ph[0]) * 0.004f;
            g->ph[1] += (gu - g->ph[1]) * 0.003f;
            au_svf_set(&g->fa[0], (260.0f + 520.0f * g->lfo[3]) * rate, 2.2f);
            au_svf_set(&g->fb[0], (300.0f + 480.0f * g->ph[0]) * rate, 2.2f);
            au_svf_set(&g->fa[1], 110.0f * rate, 0.7f); au_svf_copycoef(&g->fb[1], &g->fa[1]);
            au_svf_set(&g->fa[2], (1150.0f + 600.0f * g->ph[1]) * rate, 14.0f);
        }
        float nl = au_rnd2(&g->rng), nr = au_rnd2(&g->rng);
        g->brown[0] = g->brown[0] * 0.996f + nl * 0.06f;
        g->brown[1] = g->brown[1] * 0.996f + nr * 0.06f;
        float gust = 0.35f + 0.65f * g->ph[1];
        float l = au_svf_bp(&g->fa[0], g->brown[0]) * 3.0f + au_svf_lp(&g->fa[1], g->brown[0]) * 1.4f;
        float r = au_svf_bp(&g->fb[0], g->brown[1]) * 3.0f + au_svf_lp(&g->fb[1], g->brown[1]) * 1.4f;
        float wh = au_svf_bp(&g->fa[2], nl) * 0.05f * g->ph[1] * g->ph[1];
        float v = v0 + (v1 - v0) * (float)(i + 1) / (float)n;
        L[i] += (l * gust + wh) * v * 0.28f;
        R[i] += (r * gust + wh) * v * 0.28f;
    }
}

/* ---------------- hum ---------------- */
static void render_hum(LoopGen *g, float *L, float *R, int n, float rate, float v0, float v1)
{
    if (g->ctl == 0) { au_svf_set(&g->fa[0], 2400.0f, 3.0f); g->ctl = 1; g->ev2_env = 1.0f; }
    const float f0 = 60.0f * rate;
    for (int i = 0; i < n; i++) {
        /* mains hum: fundamental + odd/even harmonics */
        g->ph[0] += f0 / AU_FRATE; if (g->ph[0] >= 1.0f) g->ph[0] -= 1.0f;
        float p = g->ph[0];
        float hum = au_sin(p) * 0.5f + au_sin(2 * p) * 0.35f + au_sin(3 * p) * 0.22f + au_sin(4 * p) * 0.1f + au_sin(5 * p) * 0.08f;
        /* fluorescent buzz: rectified 120 Hz shaped through a resonance */
        float rect = fabsf(au_sin(p)); rect = rect * rect * rect * rect;
        float buzz = au_svf_bp(&g->fa[0], rect - 0.37f + au_rnd2(&g->rng) * 0.05f) * 0.6f;
        /* creepy detuned drone + faint dissonant whine */
        g->ph[1] += 55.3f * rate / AU_FRATE; if (g->ph[1] >= 1.0f) g->ph[1] -= 1.0f;
        g->ph[2] += 58.1f * rate / AU_FRATE; if (g->ph[2] >= 1.0f) g->ph[2] -= 1.0f;
        g->ph[3] += 1371.0f * rate / AU_FRATE; if (g->ph[3] >= 1.0f) g->ph[3] -= 1.0f;
        float sw = 0.5f + 0.5f * wander(g, 0, 0.12f, au_rnd(&g->rng));
        g->lfo[1] += (sw - g->lfo[1]) * 0.0001f;
        float drone = (au_sin(g->ph[1]) + au_sin(g->ph[2])) * 0.25f;
        float whine = au_sin(g->ph[3]) * 0.006f * g->lfo[1];
        /* occasional flicker dropout */
        if (au_rnd(&g->rng) < 0.35f / AU_FRATE) g->ev2_t = 0.06f + 0.1f * au_rnd(&g->rng);
        if (g->ev2_t > 0.0f) { g->ev2_t -= 1.0f / AU_FRATE; g->ev2_env += (0.35f - g->ev2_env) * 0.01f; }
        else g->ev2_env += (1.0f - g->ev2_env) * 0.002f;
        float x = (hum * 0.6f + buzz * g->ev2_env + drone) * 0.14f;
        float v = v0 + (v1 - v0) * (float)(i + 1) / (float)n;
        L[i] += (x + whine) * v;
        R[i] += (x - whine) * v;
    }
}

void au_loop_render(int id, float *L, float *R, int n, float rate, float v0, float v1)
{
    LoopGen *g = &gens[id];
    switch (id) {
    case LOOP_CHAINSAW_IDLE: render_chainsaw(g, L, R, n, rate, v0, v1, 0); break;
    case LOOP_CHAINSAW_CUT:  render_chainsaw(g, L, R, n, rate, v0, v1, 1); break;
    case LOOP_FIRE:          render_fire(g, L, R, n, rate, v0, v1); break;
    case LOOP_RAIN:          render_rain(g, L, R, n, rate, v0, v1); break;
    case LOOP_WIND:          render_wind(g, L, R, n, rate, v0, v1); break;
    case LOOP_HUM:           render_hum(g, L, R, n, rate, v0, v1); break;
    default: break;
    }
}
