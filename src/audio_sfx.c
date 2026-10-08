/* LAST AISLE - audio: pre-rendered sound effects, all synthesized at init.
   Each sound is built from layers (pitched thumps, swept filtered noise, modal
   resonators, formant voices, FM bells, detuned saws) and post FX (saturation,
   bit-crush, reverb, echo), then trimmed and peak-normalised to a per-sound level. */
#include "audio_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SECONDS 4.0f

static float *gL, *gR, *tL, *tR, *cL, *cR;
static int gCap, curN;
static AuRng rng, vrng;
static int g_variant;

/* per-variant parameter jitter: variant 0 is the designed sound, others deviate by up to +-pct */
static float vr(float x, float pct) { return g_variant ? x * (1.0f + pct * au_rnd2(&vrng)) : x; }

static inline int T(float s) { return (int)(s * AU_FRATE); }
static inline float mtof(float m) { return au_mtof(m); }

static void begin(float seconds)
{
    curN = T(seconds);
    if (curN > gCap) curN = gCap;
    memset(gL, 0, sizeof(float) * (size_t)curN);
    memset(gR, 0, sizeof(float) * (size_t)curN);
    cL = gL; cR = gR;
}
static void layer_begin(void)
{
    memset(tL, 0, sizeof(float) * (size_t)curN);
    memset(tR, 0, sizeof(float) * (size_t)curN);
    cL = tL; cR = tR;
}
static void layer_end(float gain)
{
    for (int i = 0; i < curN; i++) { gL[i] += tL[i] * gain; gR[i] += tR[i] * gain; }
    cL = gL; cR = gR;
}

static void pan2(float pan, float *l, float *r)
{
    float th = (au_clampf(pan, -1.0f, 1.0f) + 1.0f) * (AU_PI * 0.25f);
    *l = cosf(th) * 1.41421356f; if (*l > 1.0f) *l = 1.0f;
    *r = sinf(th) * 1.41421356f; if (*r > 1.0f) *r = 1.0f;
}

/* attack (linear) then exponential decay (dec > 0, time constant) or a cosine
   fall to zero at dur (dec <= 0). Always fades out over the last 4 ms. */
static inline float envf(float t, float dur, float att, float dec)
{
    float e;
    if (att > 0.0f && t < att) e = t / att;
    else if (dec > 0.0f) e = expf(-(t - att) / dec);
    else {
        float span = dur - att; if (span < 1e-4f) span = 1e-4f;
        float u = (t - att) / span; if (u > 1.0f) u = 1.0f;
        e = 0.5f * (1.0f + cosf(AU_PI * u));
    }
    float tail = dur - t;
    if (tail < 0.004f) e *= tail > 0.0f ? tail / 0.004f : 0.0f;
    return e;
}

/* ------------------------------------------------------------------------ */
/* layers                                                                    */
/* ------------------------------------------------------------------------ */
enum { NZ_LP, NZ_BP, NZ_HP };

typedef struct {
    float t0, dur, amp;
    int type;
    float f0, fm, f1, fpos;   /* cutoff path f0 -> fm (at fpos) -> f1; fm <= 0: f0 -> f1 */
    float q;
    float att, dec;
    float pan, width;
    float brown;              /* 0 = white, 1 = brown-ish source */
} NoiseP;

static void l_noise_p(const NoiseP *p)
{
    int s0 = T(p->t0), n = T(p->dur);
    float q = p->q > 0.0f ? p->q : 0.707f;
    float fpos = p->fpos > 0.0f ? p->fpos : 0.5f;
    float gl, gr; pan2(p->pan, &gl, &gr);
    AuSvf fa = {0}, fb = {0};
    float b1 = 0.0f, b2 = 0.0f;
    float w = au_clampf(p->width, 0.0f, 1.0f);
    float f = p->f0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE;
        if ((i & 7) == 0) {
            float u = (float)i / (float)n;
            if (p->fm > 0.0f) {
                if (u < fpos) f = p->f0 * powf(p->fm / p->f0, u / fpos);
                else f = p->fm * powf(p->f1 / p->fm, (u - fpos) / (1.0f - fpos));
            } else f = p->f0 * powf(p->f1 / p->f0, u);
            au_svf_set(&fa, f, q);
            au_svf_copycoef(&fb, &fa);
        }
        float n1 = au_rnd2(&rng), n2 = au_rnd2(&rng);
        if (p->brown > 0.0f) {
            b1 = b1 * 0.985f + n1 * 0.12f; b2 = b2 * 0.985f + n2 * 0.12f;
            n1 = au_lerpf(n1, b1 * 3.0f, p->brown); n2 = au_lerpf(n2, b2 * 3.0f, p->brown);
        }
        float xl = n1, xr = au_lerpf(n1, n2, w);
        float yl, yr;
        if (p->type == NZ_LP) { yl = au_svf_lp(&fa, xl); yr = w > 0.0f ? au_svf_lp(&fb, xr) : yl; }
        else if (p->type == NZ_BP) { yl = au_svf_bp(&fa, xl); yr = w > 0.0f ? au_svf_bp(&fb, xr) : yl; }
        else { yl = au_svf_hp(&fa, xl); yr = w > 0.0f ? au_svf_hp(&fb, xr) : yl; }
        float e = envf(t, p->dur, p->att, p->dec) * p->amp;
        int k = s0 + i;
        if ((unsigned)k < (unsigned)curN) { cL[k] += yl * e * gl; cR[k] += yr * e * gr; }
    }
}
#define NOISE(...) l_noise_p(&(NoiseP){ __VA_ARGS__ })

/* pitched thump: sine with exponential pitch drop, fast attack, exp decay */
typedef struct { float t0, f0, f1, ptau, dec, amp, drive, pan, att, len; } ThumpP;
static void l_thump_p(const ThumpP *p)
{
    float att = p->att > 0.0f ? p->att : 0.0006f;
    float len = p->len > 0.0f ? p->len : att + p->dec * 7.0f;
    int s0 = T(p->t0), n = T(len);
    float gl, gr; pan2(p->pan, &gl, &gr);
    float ph = 0.0f, pe = 1.0f, pk = expf(-1.0f / (p->ptau * AU_FRATE));
    float dn = p->drive > 0.0f ? 1.0f / au_sat(p->drive) : 1.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE;
        float fr = p->f1 + (p->f0 - p->f1) * pe; pe *= pk;
        ph += fr / AU_FRATE; if (ph >= 1.0f) ph -= 1.0f;
        float e = t < att ? t / att : expf(-(t - att) / p->dec);
        float tail = len - t; if (tail < 0.004f) e *= tail / 0.004f;
        float x = au_sin(ph) * e;
        if (p->drive > 0.0f) x = au_sat(x * p->drive) * dn;
        x *= p->amp;
        int k = s0 + i;
        if ((unsigned)k < (unsigned)curN) { cL[k] += x * gl; cR[k] += x * gr; }
    }
}
#define THUMP(...) l_thump_p(&(ThumpP){ __VA_ARGS__ })

/* sharp transient: ~1 ms of differentiated noise */
static void l_click(float t0, float amp, float tau)
{
    if (tau <= 0.0f) tau = 0.00025f;
    int s0 = T(t0), n = T(tau * 8.0f) + 2;
    float prev = 0.0f, e = 1.0f, k = expf(-1.0f / (tau * AU_FRATE));
    for (int i = 0; i < n; i++) {
        float x = au_rnd2(&rng);
        float y = (x - prev) * 0.5f * e * amp; prev = x; e *= k;
        if (i == 0) y = amp;
        int j = s0 + i;
        if ((unsigned)j < (unsigned)curN) { cL[j] += y; cR[j] += y; }
    }
}

/* struck modal resonator bank */
static void l_modes(float t0, float amp, int nm, const float *fr, const float *ga, const float *dc,
                    float pan, float spread, float pdrop, float ptau)
{
    float gl, gr; pan2(pan, &gl, &gr);
    float maxd = 0.0f;
    for (int m = 0; m < nm; m++) if (dc[m] > maxd) maxd = dc[m];
    int s0 = T(t0), n = T(maxd * 6.5f + 0.01f);
    for (int m = 0; m < nm; m++) {
        float ph = 0.0f, e = ga[m] * amp, k = expf(-1.0f / (dc[m] * AU_FRATE));
        float pe = pdrop, pk = ptau > 0.0f ? expf(-1.0f / (ptau * AU_FRATE)) : 0.0f;
        float sp = spread * ((m & 1) ? 1.0f : -1.0f) * (0.4f + 0.6f * (float)m / (float)(nm > 1 ? nm - 1 : 1));
        float ml = gl * (1.0f - sp), mr = gr * (1.0f + sp);
        int mn = T(dc[m] * 6.5f + 0.005f); if (mn > n) mn = n;
        for (int i = 0; i < mn; i++) {
            float f = fr[m] * (1.0f + pe); pe *= pk;
            ph += f / AU_FRATE; if (ph >= 1.0f) ph -= 1.0f;
            float a = i < 24 ? (float)i / 24.0f : 1.0f;
            float x = au_sin(ph) * e * a; e *= k;
            int j = s0 + i;
            if ((unsigned)j < (unsigned)curN) { cL[j] += x * ml; cR[j] += x * mr; }
        }
    }
}

/* oscillator tone with exponential glide and envelope */
enum { TW_SINE, TW_TRI, TW_SAW, TW_SQUARE };
typedef struct { float t0, dur, amp; int wave; float f0, f1; float att, dec; float vib, vrate; float pan; float lp; float pw; } ToneP;
static void l_tone_p(const ToneP *p)
{
    int s0 = T(p->t0), n = T(p->dur);
    float gl, gr; pan2(p->pan, &gl, &gr);
    AuSvf f = {0};
    if (p->lp > 0.0f) au_svf_set(&f, p->lp, 0.707f);
    float ph = 0.0f, pw = p->pw > 0.0f ? p->pw : 0.5f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE, u = (float)i / (float)n;
        float fr = p->f0 * powf(p->f1 / p->f0, u);
        if (p->vib > 0.0f) fr *= 1.0f + p->vib * au_sin(t * p->vrate);
        float dt = fr / AU_FRATE;
        ph += dt; if (ph >= 1.0f) ph -= 1.0f;
        float x;
        switch (p->wave) {
        case TW_TRI: x = au_tri(ph); break;
        case TW_SAW: x = au_saw(ph, dt); break;
        case TW_SQUARE: x = au_pulse(ph, dt, pw); break;
        default: x = au_sin(ph); break;
        }
        if (p->lp > 0.0f) x = au_svf_lp(&f, x);
        x *= envf(t, p->dur, p->att, p->dec) * p->amp;
        int j = s0 + i;
        if ((unsigned)j < (unsigned)curN) { cL[j] += x * gl; cR[j] += x * gr; }
    }
}
#define TONE(...) l_tone_p(&(ToneP){ __VA_ARGS__ })

/* 2-operator FM bell / chime */
typedef struct { float t0, dur, amp, fc, ratio, i0, i1, idec, att, dec, pan; } FmP;
static void l_fm_p(const FmP *p)
{
    int s0 = T(p->t0), n = T(p->dur);
    float gl, gr; pan2(p->pan, &gl, &gr);
    float pc = 0.0f, pm = 0.0f;
    float ie = 1.0f, ik = expf(-1.0f / ((p->idec > 0.0f ? p->idec : 0.1f) * AU_FRATE));
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE;
        float idx = p->i1 + (p->i0 - p->i1) * ie; ie *= ik;
        pm += p->fc * p->ratio / AU_FRATE; if (pm >= 1.0f) pm -= 1.0f;
        pc += p->fc / AU_FRATE; if (pc >= 1.0f) pc -= 1.0f;
        float x = au_sin(pc + idx * au_sin(pm) * (1.0f / AU_TAU)) * envf(t, p->dur, p->att, p->dec) * p->amp;
        int j = s0 + i;
        if ((unsigned)j < (unsigned)curN) { cL[j] += x * gl; cR[j] += x * gr; }
    }
}
#define FM(...) l_fm_p(&(FmP){ __VA_ARGS__ })

/* detuned saw stack through a swept low-pass (synth stabs, pads) */
typedef struct {
    float t0, dur, amp, freq; int nv; float det;   /* det: total spread in cents */
    float att, dec, sus, rel;                      /* rel = release time after dur */
    float cut0, cut1, ctau, q; float width; float sq; /* sq: mix of square osc */
} SawsP;
static void l_saws_p(const SawsP *p)
{
    int nv = p->nv < 1 ? 1 : (p->nv > 7 ? 7 : p->nv);
    float rel = p->rel > 0.0f ? p->rel : 0.05f;
    int s0 = T(p->t0), n = T(p->dur + rel * 5.0f);
    float ph[7], inc[7], pl[7], pr[7];
    for (int v = 0; v < nv; v++) {
        float d = nv > 1 ? ((float)v / (float)(nv - 1) - 0.5f) * p->det : 0.0f;
        inc[v] = p->freq * exp2f(d / 1200.0f) / AU_FRATE;
        ph[v] = au_rnd(&rng);
        float pan = nv > 1 ? ((float)v / (float)(nv - 1) * 2.0f - 1.0f) * p->width : 0.0f;
        pan2(pan, &pl[v], &pr[v]);
    }
    AuSvf fl = {0}, fr = {0};
    float ctau = p->ctau > 0.0f ? p->ctau : 0.2f;
    float e = 0.0f, relk = expf(-1.0f / (rel * AU_FRATE)), deck = p->dec > 0.0f ? expf(-1.0f / (p->dec * AU_FRATE)) : 1.0f;
    float norm = 1.0f / sqrtf((float)nv);
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE;
        if ((i & 15) == 0) {
            float c = p->cut1 + (p->cut0 - p->cut1) * expf(-t / ctau);
            au_svf_set(&fl, c, p->q > 0.0f ? p->q : 0.8f);
            au_svf_copycoef(&fr, &fl);
        }
        if (t < p->att) e = t / p->att;
        else if (t < p->dur) e = p->sus + (e - p->sus) * deck;
        else e *= relk;
        float l = 0.0f, r = 0.0f;
        for (int v = 0; v < nv; v++) {
            float x = au_saw(ph[v], inc[v]);
            if (p->sq > 0.0f) x = au_lerpf(x, au_pulse(ph[v], inc[v], 0.5f), p->sq);
            ph[v] += inc[v]; if (ph[v] >= 1.0f) ph[v] -= 1.0f;
            l += x * pl[v]; r += x * pr[v];
        }
        l = au_svf_lp(&fl, l * norm) * e * p->amp;
        r = au_svf_lp(&fr, r * norm) * e * p->amp;
        int j = s0 + i;
        if ((unsigned)j < (unsigned)curN) { cL[j] += l; cR[j] += r; }
    }
}
#define SAWS(...) l_saws_p(&(SawsP){ __VA_ARGS__ })

/* formant voice: band-limited glottal saw + aspiration through 3 formants */
enum { V_A, V_U, V_O, V_OO, V_E, V_I, V_AE, V_ER, V_COUNT };
static const float vowels[V_COUNT][3] = {
    { 730, 1090, 2440 }, { 640, 1190, 2390 }, { 570, 840, 2410 }, { 320, 870, 2240 },
    { 530, 1840, 2480 }, { 290, 2250, 3000 }, { 660, 1720, 2410 }, { 490, 1350, 1690 },
};
typedef struct {
    float t0, dur, amp;
    float p0, pm, p1;          /* pitch contour (Hz) start / 30% / end */
    int v0, v1;                /* vowel morph */
    float breath, drive, jitter, vib, vrate, gurgle;
    float att, rel, fshift;    /* fshift scales formants (1 = adult male) */
    float creak;               /* every other glottal pulse weaker by this much: a rough, creaky voice */
    float tilt;                /* one-pole low-pass on the glottal pulses (Hz, 0 = off): softer, breathier source */
    float nasal;               /* 0..1: a nasal resonance in, the first and third formants damped */
    float f2shift;             /* extra scale on the second formant (0 = 1): the vowels lean front or back */
    float body;                /* 0..1: the first formant as a low-pass (a real throat's) - keeps the low harmonics a deep voice needs */
} VoiceP;
static void l_voice_p(const VoiceP *p)
{
    int s0 = T(p->t0), n = T(p->dur);
    float att = p->att > 0.0f ? p->att : 0.015f, rel = p->rel > 0.0f ? p->rel : p->dur * 0.4f;
    float fs = p->fshift > 0.0f ? p->fshift : 1.0f, f2s = p->f2shift > 0.0f ? p->f2shift : 1.0f;
    AuSvf f1 = {0}, f2 = {0}, f3 = {0}, gb = {0}, fn = {0};
    au_svf_set(&gb, 25.0f, 0.7f);
    if (p->nasal > 0.0f) au_svf_set(&fn, 260.0f * fs, 3.0f);
    float tc = p->tilt > 0.0f ? au_op_coef(p->tilt) : 0.0f, tl = 0.0f;
    int cyc = 0;
    float ph = 0.0f, jit = 0.0f, gur = 0.0f;
    float dn = p->drive > 0.0f ? 1.0f / au_sat(p->drive) : 1.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE, u = (float)i / (float)n;
        if ((i & 7) == 0) {
            const float *a = vowels[p->v0], *b = vowels[p->v1];
            float k = u * u * (3.0f - 2.0f * u);
            float F1 = au_lerpf(a[0], b[0], k) * fs, F2 = au_lerpf(a[1], b[1], k) * fs * f2s, F3 = au_lerpf(a[2], b[2], k) * fs;
            au_svf_set(&f1, F1, F1 / 90.0f);
            au_svf_set(&f2, F2, F2 / 110.0f);
            au_svf_set(&f3, F3, F3 / 160.0f);
            jit += (au_rnd2(&rng) - jit * 0.2f) * 0.3f;
        }
        float pf = u < 0.3f ? au_lerpf(p->p0, p->pm, u / 0.3f) : au_lerpf(p->pm, p->p1, (u - 0.3f) / 0.7f);
        pf *= 1.0f + p->jitter * jit * 0.05f;
        if (p->vib > 0.0f) pf *= 1.0f + p->vib * au_sin(t * p->vrate);
        float dt = pf / AU_FRATE;
        ph += dt; if (ph >= 1.0f) { ph -= 1.0f; cyc ^= 1; }
        float glot = -au_saw(ph, dt);
        if (tc > 0.0f) { tl += tc * (glot - tl); glot = tl; }
        if (p->creak > 0.0f && cyc) glot *= 1.0f - p->creak;
        float src = glot + p->breath * au_rnd2(&rng) * 1.5f;
        float y, y1;
        if (p->body > 0.0f) { float lp1, bp1; au_svf_tick(&f1, src, &lp1, &bp1, NULL); y1 = au_lerpf(bp1, lp1, p->body); }
        else y1 = au_svf_bp(&f1, src);
        if (p->nasal > 0.0f)
            y = (1.0f - 0.5f * p->nasal) * y1 + 0.6f * au_svf_bp(&f2, src)
              + 0.25f * (1.0f - 0.4f * p->nasal) * au_svf_bp(&f3, src) + 0.9f * p->nasal * au_svf_bp(&fn, src);
        else y = y1 + 0.6f * au_svf_bp(&f2, src) + 0.25f * au_svf_bp(&f3, src);
        if (p->gurgle > 0.0f) {
            gur = au_svf_lp(&gb, au_rnd2(&rng) * 6.0f);
            y *= 1.0f - p->gurgle + p->gurgle * au_clampf(0.5f + gur * 2.0f, 0.0f, 1.5f);
        }
        float e = t < att ? t / att : 1.0f;
        float tr = p->dur - t; if (tr < rel) e *= tr / rel;
        y *= e;
        if (p->drive > 0.0f) y = au_sat(y * p->drive) * dn;
        y *= p->amp;
        int j = s0 + i;
        if ((unsigned)j < (unsigned)curN) { cL[j] += y; cR[j] += y; }
    }
}
#define VOICE(...) l_voice_p(&(VoiceP){ __VA_ARGS__ })

/* wet squelch: band-passed noise with bubbly amplitude modulation and a falling resonance */
static void l_squish(float t0, float dur, float amp, float f0, float f1, float q, float rate)
{
    int s0 = T(t0), n = T(dur);
    AuSvf bp = {0}, am = {0};
    au_svf_set(&am, rate, 0.9f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE, u = (float)i / (float)n;
        if ((i & 7) == 0) au_svf_set(&bp, f0 * powf(f1 / f0, u), q);
        float mod = au_svf_lp(&am, au_rnd2(&rng) * 8.0f);
        mod = au_clampf(0.3f + mod * 1.6f, 0.0f, 2.0f);
        float x = au_svf_bp(&bp, au_rnd2(&rng)) * mod * envf(t, dur, 0.004f, dur * 0.35f) * amp;
        int j = s0 + i;
        if ((unsigned)j < (unsigned)curN) { cL[j] += x; cR[j] += x; }
    }
}

/* scattered tiny resonant grains (glass shards, bone crackle, debris, rattles) */
static void l_grains(float t0, float span, int count, float density_tau, float amp,
                     float fmin, float fmax, float dmin, float dmax, int modal, float spread)
{
    for (int g = 0; g < count; g++) {
        float u = au_rnd(&rng);
        float t = density_tau > 0.0f ? -density_tau * logf(1.0f - u * (1.0f - expf(-span / density_tau))) : u * span;
        float a = amp * (0.25f + 0.75f * au_rnd(&rng)) * (density_tau > 0.0f ? expf(-t / (span * 0.8f)) : 1.0f);
        float pan = au_rnd2(&rng) * spread;
        if (modal) {
            float fr[3], ga[3], dc[3];
            for (int m = 0; m < 3; m++) {
                fr[m] = au_rrange(&rng, fmin, fmax);
                ga[m] = 1.0f / (1.0f + (float)m);
                dc[m] = au_rrange(&rng, dmin, dmax) / (1.0f + 0.5f * (float)m);
            }
            l_modes(t0 + t, a, 3, fr, ga, dc, pan, 0.2f, 0.0f, 0.0f);
            l_click(t0 + t, a * 0.3f, 0.0002f);
        } else {
            float f = au_rrange(&rng, fmin, fmax), d = au_rrange(&rng, dmin, dmax);
            NOISE(.t0 = t0 + t, .dur = d * 7.0f + 0.002f, .amp = a * 2.5f, .type = NZ_BP, .f0 = f, .f1 = f * 0.85f,
                  .q = 3.0f, .att = 0.0003f, .dec = d, .pan = pan);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* effects on the current target                                             */
/* ------------------------------------------------------------------------ */
static void fx_drive(float a, float b, float drive)
{
    int s = T(a), e = b > 0.0f ? T(b) : curN;
    if (e > curN) e = curN;
    float dn = 1.0f / au_sat(drive);
    for (int i = s; i < e; i++) { cL[i] = au_sat(cL[i] * drive) * dn; cR[i] = au_sat(cR[i] * drive) * dn; }
}

static float cur_peak(void)
{
    float pk = 1e-9f;
    for (int i = 0; i < curN; i++) { float v = fmaxf(fabsf(cL[i]), fabsf(cR[i])); if (v > pk) pk = v; }
    return pk;
}

static void fx_crush(float bits, int hold, float mix)
{
    float pk = cur_peak();
    float lv = powf(2.0f, bits - 1.0f) / pk;
    float hl = 0.0f, hr = 0.0f;
    for (int i = 0; i < curN; i++) {
        if (i % hold == 0) { hl = roundf(cL[i] * lv) / lv; hr = roundf(cR[i] * lv) / lv; }
        cL[i] = au_lerpf(cL[i], hl, mix); cR[i] = au_lerpf(cR[i], hr, mix);
    }
}

static void fx_filter(int type, float fc, float q)
{
    AuSvf l = {0}, r = {0};
    au_svf_set(&l, fc, q); au_svf_copycoef(&r, &l);
    for (int i = 0; i < curN; i++) {
        if (type == NZ_LP) { cL[i] = au_svf_lp(&l, cL[i]); cR[i] = au_svf_lp(&r, cR[i]); }
        else if (type == NZ_HP) { cL[i] = au_svf_hp(&l, cL[i]); cR[i] = au_svf_hp(&r, cR[i]); }
        else { cL[i] = au_svf_bp(&l, cL[i]); cR[i] = au_svf_bp(&r, cR[i]); }
    }
}

static void fx_reverb(float wet, float decay, float damp, float size, float predelay)
{
    AuReverb rv;
    if (!au_reverb_init(&rv, size, 0.1f)) return;
    au_reverb_set(&rv, decay, damp, predelay);
    for (int i = 0; i < curN; i++) {
        float l, r;
        au_reverb_tick(&rv, cL[i], cR[i], &l, &r);
        cL[i] += l * wet; cR[i] += r * wet;
    }
    au_reverb_free(&rv);
}

static void fx_echo(float tl, float tr, float fb, float wet, float damp_hz)
{
    int dl = T(tl), dr = T(tr);
    int mx = (dl > dr ? dl : dr) + 1;
    float *bl = (float *)calloc((size_t)mx * 2, sizeof(float));
    if (!bl) return;
    float *br = bl + mx;
    float lpl = 0.0f, lpr = 0.0f, c = au_op_coef(damp_hz);
    int pos = 0;
    for (int i = 0; i < curN; i++) {
        int il = pos - dl; if (il < 0) il += mx;
        int ir = pos - dr; if (ir < 0) ir += mx;
        float el = bl[il], er = br[ir];
        lpl += c * (el - lpl); lpr += c * (er - lpr);
        bl[pos] = cL[i] + lpr * fb;      /* ping-pong cross feed */
        br[pos] = cR[i] + lpl * fb;
        cL[i] += el * wet; cR[i] += er * wet;
        if (++pos >= mx) pos = 0;
    }
    free(bl);
}

/* tape-stop style variable rate resample of the current target from time a */
static void fx_tapestop(float a, float b, float end_rate)
{
    float *src = (float *)malloc(sizeof(float) * (size_t)curN * 2);
    if (!src) return;
    memcpy(src, cL, sizeof(float) * (size_t)curN);
    memcpy(src + curN, cR, sizeof(float) * (size_t)curN);
    int s = T(a), e = T(b);
    double pos = (double)s;
    for (int i = s; i < curN; i++) {
        float u = au_clampf((float)(i - s) / (float)(e - s), 0.0f, 1.0f);
        float rate = au_lerpf(1.0f, end_rate, 1.0f - (1.0f - u) * (1.0f - u));
        int k = (int)pos;
        if (k + 1 >= curN) { cL[i] = cR[i] = 0.0f; continue; }
        float f = (float)(pos - k);
        cL[i] = src[k] + (src[k + 1] - src[k]) * f;
        cR[i] = src[curN + k] + (src[curN + k + 1] - src[curN + k]) * f;
        pos += rate;
    }
    free(src);
}

/* ------------------------------------------------------------------------ */
/* finishing                                                                 */
/* ------------------------------------------------------------------------ */
static AuSample *g_out;
static const char *defs_name(int id);

/* loudest 50 ms RMS window of the (mono-summed) work buffer */
static float loudness(void)
{
    const int win = T(0.05f);
    double acc = 0.0, best = 0.0;
    for (int i = 0; i < curN; i++) {
        float m = 0.5f * (gL[i] + gR[i]);
        acc += (double)m * m;
        if (i >= win) { float o = 0.5f * (gL[i - win] + gR[i - win]); acc -= (double)o * o; }
        if (acc > best) best = acc;
    }
    int w = curN < win ? (curN > 0 ? curN : 1) : win;
    return (float)sqrt(best / (double)w) + 1e-9f;
}

/* offline look-ahead peak limiter: 1 ms attack ramp, 15 ms release; never exceeds ceiling */
static void limit_offline(float ceiling)
{
    float *g = (float *)malloc(sizeof(float) * (size_t)curN);
    if (!g) return;
    for (int i = 0; i < curN; i++) {
        float pk = fmaxf(fabsf(gL[i]), fabsf(gR[i]));
        g[i] = pk > ceiling ? ceiling / pk : 1.0f;
    }
    const float rel = au_tc_coef(0.015f, 1), att = au_tc_coef(0.001f, 1);
    for (int i = 1; i < curN; i++) { float r = g[i - 1] + (1.0f - g[i - 1]) * rel; if (r < g[i]) g[i] = r; }
    for (int i = curN - 2; i >= 0; i--) { float a = g[i + 1] + (1.0f - g[i + 1]) * att; if (a < g[i]) g[i] = a; }
    for (int i = 0; i < curN; i++) { gL[i] *= g[i]; gR[i] *= g[i]; }
    free(g);
}

static void finish_to(AuSample *s, const char *name, float loud_db, int stereo)
{
    /* DC / sub-rumble block */
    float hc = au_op_coef(18.0f), lpl = 0.0f, lpr = 0.0f;
    for (int i = 0; i < curN; i++) {
        lpl += hc * (gL[i] - lpl); gL[i] -= lpl;
        lpr += hc * (gR[i] - lpr); gR[i] -= lpr;
    }
    cL = gL; cR = gR;
    /* normalise short-term loudness, limiting peaks to -0.5 dBFS (a few passes to converge) */
    const float ceiling = 0.944f, target = au_db(loud_db);
    for (int pass = 0; pass < 4; pass++) {
        float g = target / loudness();
        if (fabsf(g - 1.0f) < 0.01f) break;
        for (int i = 0; i < curN; i++) { gL[i] *= g; gR[i] *= g; }
        if (cur_peak() > ceiling) limit_offline(ceiling); else break;
    }
    float pk = cur_peak();
    if (pk > ceiling) { float g = ceiling / pk; for (int i = 0; i < curN; i++) { gL[i] *= g; gR[i] *= g; } pk = ceiling; }
    /* trim trailing silence (-72 dB below peak) */
    float thr = pk * 2.5e-4f;
    int end = curN;
    while (end > 1 && fabsf(gL[end - 1]) < thr && fabsf(gR[end - 1]) < thr) end--;
    if (end >= curN - T(0.004f)) {
        /* content runs into the end of the work buffer: how loud is the tail? */
        double acc = 0.0;
        int w = T(0.02f);
        for (int i = curN - w; i < curN; i++) acc += 0.5 * ((double)gL[i] * gL[i] + (double)gR[i] * gR[i]);
        float tail = (float)sqrt(acc / w) / pk;
        int tf = (tail > 0.003f) ? (curN / 8 < T(0.25f) ? curN / 8 : T(0.25f)) : T(0.03f);
        for (int i = curN - tf; i < curN; i++) { float g = (float)(curN - i) / (float)tf; g *= g; gL[i] *= g; gR[i] *= g; }
        if (tail > 0.003f) fprintf(stderr, "audio: sfx '%s' tail still at %.0f dB when its buffer ends\n", name, 20.0 * log10(tail));
    }
    end += T(0.004f);
    if (end > curN) end = curN;
    int fade = T(0.004f);
    s->channels = stereo ? 2 : 1;
    s->frames = end + 1;
    s->data = (float *)calloc((size_t)s->frames * (size_t)s->channels, sizeof(float));
    if (!s->data) { s->frames = 0; return; }
    for (int i = 0; i < end; i++) {
        float f = 1.0f;
        if (i > end - fade) f = (float)(end - i) / (float)fade;
        if (stereo) { s->data[i * 2] = gL[i] * f; s->data[i * 2 + 1] = gR[i] * f; }
        else s->data[i] = 0.5f * (gL[i] + gR[i]) * f;
    }
    au_sample_make_env(s);
}

static void finish(int id, float loud_db, int stereo) { finish_to(&g_out[id * AU_SFX_MAXVAR + g_variant], defs_name(id), loud_db, stereo); }

/* ------------------------------------------------------------------------ */
/* sound designs                                                             */
/* ------------------------------------------------------------------------ */
typedef struct {
    float click;
    float crack_f, crack_amp, crack_dec;
    float body_f0, body_f1, body_ptau, body_dec, body_amp, body_drive;
    float blast_amp, blast_f0, blast_f1, blast_dec, blast_dur;
    float tail_amp, tail_dec;
    float glue;
    float rev_wet, rev_decay, rev_size;
    float echo, echo_t;
} GunP;

static void gun(const GunP *p)
{
    l_click(0.0f, p->click, 0.0002f);
    l_click(0.0012f, p->click * 0.6f, 0.0003f);
    NOISE(.t0 = 0, .dur = p->crack_dec * 8.0f, .amp = p->crack_amp, .type = NZ_BP, .f0 = p->crack_f * 1.3f, .f1 = p->crack_f * 0.7f,
          .q = 1.1f, .att = 0.0002f, .dec = p->crack_dec);
    THUMP(.t0 = 0, .f0 = p->body_f0, .f1 = p->body_f1, .ptau = p->body_ptau, .dec = p->body_dec, .amp = p->body_amp, .drive = p->body_drive);
    NOISE(.t0 = 0, .dur = p->blast_dur, .amp = p->blast_amp, .type = NZ_LP, .f0 = p->blast_f0, .f1 = p->blast_f1,
          .q = 0.9f, .att = 0.0004f, .dec = p->blast_dec, .width = 0.5f);
    NOISE(.t0 = 0.004f, .dur = p->tail_dec * 6.0f, .amp = p->tail_amp, .type = NZ_LP, .f0 = 900.0f, .f1 = 120.0f,
          .q = 0.8f, .att = 0.01f, .dec = p->tail_dec, .width = 0.9f, .brown = 0.6f);
    fx_drive(0.0f, 0.0f, p->glue);
    if (p->echo > 0.0f) fx_echo(p->echo_t, p->echo_t * 1.13f, 0.25f, p->echo, 3000.0f);
    fx_reverb(p->rev_wet, p->rev_decay, 5000.0f, p->rev_size, 0.008f);
}

static void sfx_pistol(void)
{
    begin(1.0f);
    gun(&(GunP){ .click = 1.0f, .crack_f = vr(2600, 0.12f), .crack_amp = 1.2f, .crack_dec = 0.010f,
                 .body_f0 = vr(170, 0.08f), .body_f1 = vr(62, 0.06f), .body_ptau = 0.010f, .body_dec = 0.045f, .body_amp = 1.3f, .body_drive = 3.0f,
                 .blast_amp = 1.0f, .blast_f0 = 7000, .blast_f1 = 500, .blast_dec = 0.05f, .blast_dur = 0.35f,
                 .tail_amp = 0.35f, .tail_dec = 0.09f, .glue = 2.2f, .rev_wet = 0.22f, .rev_decay = 0.8f, .rev_size = 0.8f });
    /* slide snap */
    float fr[] = { 2300, 3700 }, ga[] = { 1, 0.6f }, dc[] = { 0.012f, 0.008f };
    l_modes(0.055f, 0.10f, 2, fr, ga, dc, 0, 0.1f, 0, 0);
}

static void sfx_revolver(void)
{
    begin(1.4f);
    gun(&(GunP){ .click = 1.0f, .crack_f = 2000, .crack_amp = 1.2f, .crack_dec = 0.013f,
                 .body_f0 = 150, .body_f1 = 46, .body_ptau = 0.016f, .body_dec = 0.075f, .body_amp = 1.6f, .body_drive = 3.5f,
                 .blast_amp = 1.2f, .blast_f0 = 8000, .blast_f1 = 380, .blast_dec = 0.09f, .blast_dur = 0.6f,
                 .tail_amp = 0.5f, .tail_dec = 0.16f, .glue = 2.6f, .rev_wet = 0.3f, .rev_decay = 1.3f, .rev_size = 1.1f });
    float fr[] = { 2140, 3390, 5120 }, ga[] = { 1, 0.7f, 0.4f }, dc[] = { 0.09f, 0.06f, 0.04f };
    l_modes(0.0f, 0.05f, 3, fr, ga, dc, 0, 0.3f, 0, 0);
}

static void sfx_shotgun(void)
{
    begin(2.4f);
    l_click(0.0028f, 0.9f, 0.0003f);
    gun(&(GunP){ .click = 1.0f, .crack_f = 1700, .crack_amp = 1.3f, .crack_dec = 0.018f,
                 .body_f0 = 125, .body_f1 = 36, .body_ptau = 0.025f, .body_dec = 0.13f, .body_amp = 2.0f, .body_drive = 4.0f,
                 .blast_amp = 1.6f, .blast_f0 = 9500, .blast_f1 = 220, .blast_dec = 0.16f, .blast_dur = 0.95f,
                 .tail_amp = 0.9f, .tail_dec = 0.32f, .glue = 3.0f, .rev_wet = 0.36f, .rev_decay = 1.7f, .rev_size = 1.35f });
    THUMP(.t0 = 0.0f, .f0 = 70, .f1 = 32, .ptau = 0.08f, .dec = 0.22f, .amp = 0.35f, .drive = 1.5f);
    /* pellets / debris scatter */
    l_grains(0.01f, 0.25f, 14, 0.08f, 0.07f, 1500, 5000, 0.002f, 0.006f, 0, 0.6f);
}

static void sfx_rifle(void)
{
    begin(1.2f);
    NOISE(.t0 = 0, .dur = 0.03f, .amp = 1.3f, .type = NZ_HP, .f0 = 5000, .f1 = 3000, .q = 0.8f, .att = 0.0001f, .dec = 0.004f);
    gun(&(GunP){ .click = 1.2f, .crack_f = 3800, .crack_amp = 1.3f, .crack_dec = 0.008f,
                 .body_f0 = 190, .body_f1 = 70, .body_ptau = 0.008f, .body_dec = 0.04f, .body_amp = 1.2f, .body_drive = 3.0f,
                 .blast_amp = 1.1f, .blast_f0 = 10000, .blast_f1 = 700, .blast_dec = 0.045f, .blast_dur = 0.4f,
                 .tail_amp = 0.4f, .tail_dec = 0.12f, .glue = 2.4f, .rev_wet = 0.18f, .rev_decay = 1.0f, .rev_size = 1.0f,
                 .echo = 0.22f, .echo_t = 0.075f });
}

static void sfx_nailgun(void)
{
    begin(0.4f);
    l_click(0, 0.8f, 0.0002f);
    NOISE(.t0 = 0, .dur = 0.06f, .amp = 0.7f, .type = NZ_HP, .f0 = 3500, .f1 = 2500, .q = 0.9f, .att = 0.002f, .dec = 0.018f);
    float fr[] = { 1820, 2930, 4130, 5600 }, ga[] = { 1, 0.7f, 0.5f, 0.3f }, dc[] = { 0.04f, 0.03f, 0.022f, 0.015f };
    l_modes(0.002f, 0.6f, 4, fr, ga, dc, 0, 0.1f, 0.02f, 0.01f);
    THUMP(.t0 = 0, .f0 = 140, .f1 = 70, .ptau = 0.01f, .dec = 0.03f, .amp = 0.9f, .drive = 2.0f);
    NOISE(.t0 = 0.012f, .dur = 0.12f, .amp = 0.25f, .type = NZ_BP, .f0 = 6000, .f1 = 3000, .q = 1.0f, .att = 0.01f, .dec = 0.03f);
    fx_drive(0, 0, 1.6f);
}

static void sfx_empty(void)
{
    begin(0.15f);
    NOISE(.t0 = 0, .dur = 0.012f, .amp = 1.0f, .type = NZ_BP, .f0 = 4200, .f1 = 3800, .q = 2.5f, .att = 0.0001f, .dec = 0.0018f);
    float fr[] = { 2300, 3700 }, ga[] = { 1, 0.5f }, dc[] = { 0.012f, 0.008f };
    l_modes(0, 0.25f, 2, fr, ga, dc, 0, 0, 0, 0);
    NOISE(.t0 = 0.035f, .dur = 0.012f, .amp = 0.7f, .type = NZ_BP, .f0 = 3000, .f1 = 2800, .q = 2.5f, .att = 0.0001f, .dec = 0.0018f);
    float fr2[] = { 1900, 3100 };
    l_modes(0.035f, 0.18f, 2, fr2, ga, dc, 0, 0, 0, 0);
}

static void sfx_reload(void)
{
    begin(0.95f);
    /* mag release + slide out */
    NOISE(.t0 = 0, .dur = 0.02f, .amp = 0.8f, .type = NZ_BP, .f0 = 3600, .f1 = 3000, .q = 2.0f, .att = 0.0001f, .dec = 0.003f);
    float fa[] = { 1800, 2700, 4100 }, ga[] = { 1, 0.6f, 0.3f }, da[] = { 0.02f, 0.015f, 0.01f };
    l_modes(0, 0.4f, 3, fa, ga, da, 0, 0, 0, 0);
    NOISE(.t0 = 0.03f, .dur = 0.1f, .amp = 0.18f, .type = NZ_BP, .f0 = 2200, .f1 = 1500, .q = 2.0f, .att = 0.02f, .dec = -1);
    /* mag in: clack */
    THUMP(.t0 = 0.30f, .f0 = 230, .f1 = 120, .ptau = 0.01f, .dec = 0.025f, .amp = 0.6f, .drive = 2.0f);
    l_click(0.30f, 0.7f, 0.0003f);
    float fb[] = { 1150, 2620, 3900 }, gb[] = { 1, 0.7f, 0.4f }, db[] = { 0.05f, 0.035f, 0.02f };
    l_modes(0.30f, 0.6f, 3, fb, gb, db, 0, 0, 0, 0);
    NOISE(.t0 = 0.30f, .dur = 0.03f, .amp = 0.9f, .type = NZ_BP, .f0 = 2600, .f1 = 2000, .q = 1.5f, .att = 0.0001f, .dec = 0.006f);
    /* slide rack back / forward */
    float fc[] = { 1400, 3100, 4700 }, gc[] = { 1, 0.6f, 0.3f }, dcc[] = { 0.035f, 0.025f, 0.015f };
    NOISE(.t0 = 0.43f, .dur = 0.05f, .amp = 0.25f, .type = NZ_BP, .f0 = 2500, .f1 = 3500, .q = 2.0f, .att = 0.01f, .dec = -1);
    l_modes(0.47f, 0.45f, 3, fc, gc, dcc, 0, 0, 0, 0);
    l_click(0.47f, 0.5f, 0.0003f);
    l_modes(0.56f, 0.6f, 3, fc, gc, dcc, 0, 0, 0.03f, 0.01f);
    l_click(0.56f, 0.7f, 0.0003f);
    THUMP(.t0 = 0.56f, .f0 = 200, .f1 = 120, .ptau = 0.01f, .dec = 0.02f, .amp = 0.35f, .drive = 1.5f);
    fx_drive(0, 0, 1.4f);
}

static void sfx_shell(void)
{
    begin(0.5f);
    const float bt[] = { 0.0f, 0.085f, 0.15f, 0.195f, 0.225f, 0.245f };
    const float ba[] = { 1.0f, 0.6f, 0.42f, 0.28f, 0.17f, 0.1f };
    for (int b = 0; b < 6; b++) {
        float j = 1.0f + au_rnd2(&rng) * 0.03f;
        float fr[] = { 4150 * j, 6720 * j, 9050 * j, 11300 * j }, ga[] = { 1, 0.7f, 0.5f, 0.3f };
        float dc[] = { 0.09f * ba[b] + 0.01f, 0.06f * ba[b] + 0.01f, 0.04f * ba[b] + 0.008f, 0.03f * ba[b] + 0.006f };
        l_modes(bt[b], ba[b], 4, fr, ga, dc, 0, 0, 0, 0);
        l_click(bt[b], 0.3f * ba[b], 0.0002f);
    }
}

static void sfx_ricochet(void)
{
    begin(0.8f);
    l_click(0, 0.8f, 0.0002f);
    float fr[] = { 2800, 4400, 6100 }, ga[] = { 1, 0.6f, 0.3f }, dc[] = { 0.03f, 0.02f, 0.015f };
    l_modes(0, 0.5f, 3, fr, ga, dc, 0, 0, 0, 0);
    TONE(.t0 = 0.004f, .dur = 0.55f, .amp = 0.55f, .wave = TW_SINE, .f0 = vr(3600, 0.15f), .f1 = vr(1250, 0.15f), .att = 0.004f, .dec = 0.16f, .vib = 0.025f, .vrate = 38.0f);
    TONE(.t0 = 0.004f, .dur = 0.45f, .amp = 0.12f, .wave = TW_TRI, .f0 = 7200, .f1 = 2600, .att = 0.004f, .dec = 0.1f, .vib = 0.03f, .vrate = 31.0f);
    NOISE(.t0 = 0.004f, .dur = 0.4f, .amp = 0.5f, .type = NZ_BP, .f0 = 3600, .f1 = 1300, .q = 4.0f, .att = 0.004f, .dec = 0.12f);
    fx_reverb(0.22f, 0.9f, 6000.0f, 0.9f, 0.01f);
}

static void sfx_swing(void)
{
    begin(0.3f);
    NOISE(.t0 = 0, .dur = vr(0.24f, 0.12f), .amp = 1.0f, .type = NZ_BP, .f0 = vr(420, 0.15f), .fm = vr(2300, 0.15f), .f1 = vr(650, 0.15f), .fpos = vr(0.42f, 0.15f), .q = 2.0f, .att = vr(0.09f, 0.15f), .dec = -1);
    NOISE(.t0 = 0, .dur = 0.22f, .amp = 0.35f, .type = NZ_BP, .f0 = 900, .fm = 1500, .f1 = 800, .fpos = 0.45f, .q = 7.0f, .att = 0.09f, .dec = -1);
}

static void sfx_swing_heavy(void)
{
    begin(0.55f);
    NOISE(.t0 = 0, .dur = vr(0.46f, 0.1f), .amp = 1.0f, .type = NZ_BP, .f0 = vr(170, 0.15f), .fm = vr(950, 0.15f), .f1 = vr(240, 0.15f), .fpos = 0.5f, .q = 1.4f, .att = 0.22f, .dec = -1);
    NOISE(.t0 = 0, .dur = 0.46f, .amp = 0.5f, .type = NZ_LP, .f0 = 200, .fm = 500, .f1 = 180, .fpos = 0.5f, .q = 0.9f, .att = 0.22f, .dec = -1, .brown = 0.6f);
    NOISE(.t0 = 0, .dur = 0.42f, .amp = 0.3f, .type = NZ_BP, .f0 = 380, .fm = 620, .f1 = 330, .fpos = 0.5f, .q = 6.0f, .att = 0.2f, .dec = -1);
}

static void sfx_punch(void)
{
    begin(0.5f);
    l_click(0, 0.6f, 0.0003f);
    THUMP(.t0 = 0, .f0 = vr(200, 0.12f), .f1 = vr(55, 0.1f), .ptau = 0.012f, .dec = vr(0.055f, 0.15f), .amp = 1.0f, .drive = 2.5f);
    NOISE(.t0 = 0, .dur = 0.06f, .amp = vr(0.9f, 0.2f), .type = NZ_BP, .f0 = vr(1900, 0.2f), .f1 = vr(900, 0.2f), .q = 1.2f, .att = 0.0004f, .dec = 0.011f);
    NOISE(.t0 = 0, .dur = 0.15f, .amp = 0.5f, .type = NZ_LP, .f0 = 900, .f1 = 200, .q = 0.8f, .att = 0.001f, .dec = 0.03f);
    fx_drive(0, 0, 1.8f);
}

static void sfx_hit_blunt(void)
{
    begin(0.75f);
    l_click(0, 0.9f, 0.0003f);
    THUMP(.t0 = 0, .f0 = vr(150, 0.12f), .f1 = vr(48, 0.1f), .ptau = 0.014f, .dec = vr(0.085f, 0.15f), .amp = 1.1f, .drive = 3.0f);
    NOISE(.t0 = 0, .dur = 0.05f, .amp = 1.0f, .type = NZ_BP, .f0 = vr(3300, 0.2f), .f1 = vr(2000, 0.2f), .q = 1.4f, .att = 0.0002f, .dec = 0.007f);
    float wj = vr(1.0f, 0.15f);
    float fr[] = { 620 * wj, 1430 * wj, 2210 * wj }, ga[] = { 1, 0.6f, 0.35f }, dc[] = { 0.05f, 0.03f, 0.02f };
    l_modes(0, 0.35f, 3, fr, ga, dc, 0, 0, 0.04f, 0.01f);
    NOISE(.t0 = 0.002f, .dur = 0.2f, .amp = 0.7f, .type = NZ_LP, .f0 = 1300, .f1 = 280, .q = 0.8f, .att = 0.001f, .dec = 0.04f);
    l_squish(0.01f, 0.16f, 0.3f, 1100, 400, 3.0f, 45.0f);
    fx_drive(0, 0, 2.2f);
    fx_reverb(0.08f, 0.45f, 4000.0f, 0.7f, 0.004f);
}

static void sfx_hit_blade(void)
{
    begin(0.45f);
    NOISE(.t0 = 0, .dur = 0.13f, .amp = 1.0f, .type = NZ_BP, .f0 = vr(7500, 0.15f), .f1 = vr(2400, 0.2f), .q = 1.3f, .att = 0.003f, .dec = vr(0.032f, 0.2f));
    float fr[] = { 3150, 4720, 6230, 8100 }, ga[] = { 1, 0.8f, 0.6f, 0.4f }, dc[] = { 0.11f, 0.08f, 0.06f, 0.045f };
    l_modes(0.004f, 0.13f, 4, fr, ga, dc, 0, 0.2f, 0, 0);
    l_squish(0.012f, vr(0.3f, 0.15f), 0.9f, vr(1600, 0.2f), vr(380, 0.2f), 3.5f, vr(55.0f, 0.25f));
    l_squish(0.03f, 0.2f, 0.5f, 2600, 900, 2.5f, 80.0f);
    THUMP(.t0 = 0.008f, .f0 = 160, .f1 = 70, .ptau = 0.01f, .dec = 0.04f, .amp = 0.45f, .drive = 2.0f);
    fx_drive(0, 0, 1.6f);
}

static void sfx_hit_metal(void)
{
    begin(1.4f);
    l_click(0, 1.0f, 0.0002f);
    NOISE(.t0 = 0, .dur = 0.03f, .amp = 0.8f, .type = NZ_HP, .f0 = 2500, .f1 = 2000, .q = 0.8f, .att = 0.0001f, .dec = 0.004f);
    float mj = vr(1.0f, 0.12f);
    float fr[] = { 523 * mj, 1247 * mj, 1813 * mj, 2711 * mj, 3497 * mj, 4423 * mj, 5876 * mj };
    float ga[] = { 0.7f, 1.0f, 0.8f, 0.7f, 0.5f, 0.35f, 0.25f };
    float dc[] = { 0.22f, 0.18f, 0.14f, 0.11f, 0.08f, 0.06f, 0.045f };
    l_modes(0, 0.45f, 7, fr, ga, dc, 0, 0.35f, 0.012f, 0.008f);
    THUMP(.t0 = 0, .f0 = 180, .f1 = 110, .ptau = 0.01f, .dec = 0.03f, .amp = 0.5f, .drive = 2.0f);
    fx_drive(0, 0, 1.3f);
    fx_reverb(0.14f, 0.8f, 6000.0f, 0.8f, 0.006f);
}

static void sfx_gore(void)
{
    begin(0.6f);
    l_squish(0, vr(0.36f, 0.15f), 1.0f, vr(1400, 0.2f), vr(330, 0.2f), 3.0f, vr(42.0f, 0.25f));
    l_squish(vr(0.025f, 0.4f), 0.26f, 0.6f, vr(2600, 0.2f), vr(800, 0.2f), 2.5f, vr(75.0f, 0.25f));
    l_squish(0.09f, 0.3f, 0.45f, 900, 260, 4.0f, 30.0f);
    THUMP(.t0 = 0, .f0 = 120, .f1 = 50, .ptau = 0.012f, .dec = 0.05f, .amp = 0.6f, .drive = 2.0f);
    l_grains(0.03f, 0.4f, 16, 0.12f, 0.35f, 2500, 6500, 0.002f, 0.005f, 0, 0.0f);
    fx_drive(0, 0, 2.4f);
}

static void sfx_bone_crunch(void)
{
    begin(0.55f);
    l_click(0, 1.0f, 0.0002f);
    NOISE(.t0 = 0, .dur = 0.04f, .amp = 1.0f, .type = NZ_HP, .f0 = 2600, .f1 = 1800, .q = 0.9f, .att = 0.0001f, .dec = 0.006f);
    l_grains(0.002f, 0.16f, 34, 0.05f, 0.9f, 1200, 4800, 0.0015f, 0.004f, 0, 0.0f);
    THUMP(.t0 = 0, .f0 = vr(130, 0.12f), .f1 = vr(45, 0.1f), .ptau = 0.012f, .dec = vr(0.07f, 0.15f), .amp = 0.9f, .drive = 3.0f);
    l_squish(0.02f, 0.26f, 0.45f, vr(1200, 0.2f), vr(350, 0.2f), 3.0f, 50.0f);
    fx_drive(0, 0, 2.6f);
    fx_crush(9.0f, 2, 0.35f);
}

static void sfx_stun(void)
{
    begin(2.2f);
    l_click(0, 0.8f, 0.0003f);
    THUMP(.t0 = 0, .f0 = 220, .f1 = 95, .ptau = 0.01f, .dec = 0.035f, .amp = 0.7f, .drive = 2.0f);
    /* cast-iron pan: inharmonic plate modes, beating pair at the fundamental, comic pitch drop */
    const float f0 = 338.0f;
    float fr[] = { f0, f0 * 1.019f, f0 * 2.32f, f0 * 3.86f, f0 * 5.12f, f0 * 6.95f, f0 * 8.6f };
    float ga[] = { 1.0f, 0.85f, 0.6f, 0.38f, 0.24f, 0.15f, 0.08f };
    float dc[] = { 0.42f, 0.36f, 0.22f, 0.13f, 0.08f, 0.055f, 0.04f };
    l_modes(0.0f, 0.55f, 7, fr, ga, dc, 0, 0.25f, 0.075f, 0.035f);
    fx_drive(0, 0.12f, 1.5f);
    fx_reverb(0.12f, 0.9f, 5000.0f, 0.8f, 0.01f);
}

static void sfx_throw(void)
{
    begin(0.25f);
    NOISE(.t0 = 0, .dur = vr(0.17f, 0.12f), .amp = 1.0f, .type = NZ_BP, .f0 = vr(800, 0.15f), .fm = vr(3200, 0.15f), .f1 = vr(1800, 0.15f), .fpos = 0.6f, .q = 2.5f, .att = 0.08f, .dec = -1);
    NOISE(.t0 = 0, .dur = 0.17f, .amp = 0.4f, .type = NZ_LP, .f0 = 300, .fm = 700, .f1 = 400, .fpos = 0.6f, .q = 0.8f, .att = 0.08f, .dec = -1);
}

static void sfx_glass_break(void)
{
    begin(1.8f);
    l_click(0, 1.0f, 0.0002f);
    THUMP(.t0 = 0, .f0 = 170, .f1 = 60, .ptau = 0.01f, .dec = 0.05f, .amp = 0.7f, .drive = 2.0f);
    NOISE(.t0 = 0, .dur = 0.08f, .amp = 1.0f, .type = NZ_HP, .f0 = 1500, .f1 = 2500, .q = 0.7f, .att = 0.0002f, .dec = 0.02f, .width = 0.6f);
    NOISE(.t0 = 0.002f, .dur = 0.6f, .amp = 0.7f, .type = NZ_BP, .f0 = 4500, .f1 = 3000, .q = 0.7f, .att = 0.001f, .dec = 0.09f, .width = 0.9f);
    l_grains(0.004f, 1.15f, 90, 0.22f, 0.55f, 2600, 11000, 0.02f, 0.12f, 1, 0.9f);
    l_grains(0.004f, 0.5f, 40, 0.1f, 0.4f, 3000, 9000, 0.0015f, 0.004f, 0, 0.9f);
    fx_reverb(0.22f, 1.1f, 7000.0f, 0.9f, 0.01f);
}

static void sfx_bottle_break(void)
{
    begin(0.7f);
    l_click(0, 0.9f, 0.0002f);
    THUMP(.t0 = 0, .f0 = vr(470, 0.15f), .f1 = vr(330, 0.15f), .ptau = 0.008f, .dec = 0.022f, .amp = 0.6f, .drive = 1.5f);
    NOISE(.t0 = 0, .dur = 0.05f, .amp = 0.9f, .type = NZ_HP, .f0 = 2000, .f1 = 3000, .q = 0.7f, .att = 0.0002f, .dec = 0.012f);
    l_grains(0.003f, 0.45f, 32, 0.12f, 0.6f, 3000, 10500, 0.015f, 0.08f, 1, 0.5f);
    fx_reverb(0.14f, 0.7f, 7000.0f, 0.7f, 0.005f);
}

static void sfx_explosion(void)
{
    begin(3.9f);
    l_click(0, 1.0f, 0.0003f);
    NOISE(.t0 = 0, .dur = 0.06f, .amp = 1.2f, .type = NZ_HP, .f0 = 1200, .f1 = 800, .q = 0.8f, .att = 0.0001f, .dec = 0.012f, .width = 0.5f);
    THUMP(.t0 = 0, .f0 = 90, .f1 = 27, .ptau = 0.07f, .dec = 0.42f, .amp = 1.8f, .drive = 2.5f);
    NOISE(.t0 = 0, .dur = 2.0f, .amp = 1.6f, .type = NZ_LP, .f0 = 8500, .f1 = 140, .q = 0.9f, .att = 0.002f, .dec = 0.32f, .width = 0.9f);
    NOISE(.t0 = 0.01f, .dur = 2.9f, .amp = 1.1f, .type = NZ_LP, .f0 = 260, .f1 = 80, .q = 0.9f, .att = 0.06f, .dec = 0.75f, .width = 1.0f, .brown = 0.8f);
    l_grains(0.04f, 1.3f, 50, 0.35f, 0.3f, 900, 5000, 0.003f, 0.012f, 0, 0.9f);
    l_grains(0.08f, 1.0f, 14, 0.4f, 0.12f, 1500, 6000, 0.02f, 0.06f, 1, 0.9f);
    fx_drive(0, 0, 2.0f);
    fx_reverb(0.32f, 2.2f, 3500.0f, 1.5f, 0.015f);
}

static void sfx_ignite(void)
{
    begin(1.1f);
    NOISE(.t0 = 0, .dur = 0.95f, .amp = 1.0f, .type = NZ_LP, .f0 = 300, .fm = 3800, .f1 = 700, .fpos = 0.3f, .q = 1.2f, .att = 0.13f, .dec = -1, .width = 0.7f);
    NOISE(.t0 = 0, .dur = 0.95f, .amp = 0.8f, .type = NZ_LP, .f0 = 120, .fm = 300, .f1 = 150, .fpos = 0.3f, .q = 0.8f, .att = 0.12f, .dec = -1, .width = 0.8f, .brown = 0.8f);
    NOISE(.t0 = 0.05f, .dur = 0.6f, .amp = 0.25f, .type = NZ_HP, .f0 = 3000, .f1 = 5000, .q = 0.7f, .att = 0.1f, .dec = -1, .width = 0.9f);
    l_grains(0.12f, 0.8f, 14, 0.4f, 0.4f, 1200, 4500, 0.0015f, 0.005f, 0, 0.6f);
    fx_drive(0, 0, 1.5f);
}

static void sfx_flame_puff(void)
{
    begin(0.45f);
    NOISE(.t0 = 0, .dur = 0.33f, .amp = 0.6f, .type = NZ_HP, .f0 = 2500, .f1 = 1800, .q = 0.7f, .att = 0.01f, .dec = 0.08f, .width = 0.5f);
    NOISE(.t0 = 0, .dur = 0.35f, .amp = 1.0f, .type = NZ_LP, .f0 = 700, .f1 = 220, .q = 1.0f, .att = 0.018f, .dec = 0.08f, .brown = 0.5f);
    THUMP(.t0 = 0, .f0 = 95, .f1 = 60, .ptau = 0.02f, .dec = 0.06f, .amp = 0.35f, .drive = 1.2f);
    fx_drive(0, 0, 1.3f);
}

static void sfx_hurt_player(void)
{
    begin(0.75f);
    l_click(0, 0.7f, 0.0003f);
    THUMP(.t0 = 0, .f0 = 130, .f1 = 42, .ptau = 0.014f, .dec = 0.09f, .amp = 1.1f, .drive = 3.0f);
    NOISE(.t0 = 0, .dur = 0.08f, .amp = 0.6f, .type = NZ_BP, .f0 = 1500, .f1 = 700, .q = 1.2f, .att = 0.0004f, .dec = 0.015f);
    layer_begin();
    VOICE(.t0 = 0.012f, .dur = vr(0.22f, 0.12f), .amp = 1.0f, .p0 = vr(128, 0.08f), .pm = vr(118, 0.08f), .p1 = vr(82, 0.08f), .v0 = g_variant ? V_A : V_U, .v1 = V_ER,
          .breath = 0.25f, .drive = 4.0f, .jitter = 1.0f, .att = 0.008f, .rel = 0.09f);
    fx_crush(7.0f, 2, 0.5f);
    layer_end(0.8f);
    fx_drive(0, 0, 1.6f);
}

static void sfx_hurt_npc(void)
{
    begin(0.4f);
    THUMP(.t0 = 0, .f0 = 160, .f1 = 70, .ptau = 0.01f, .dec = 0.035f, .amp = 0.4f, .drive = 2.0f);
    layer_begin();
    VOICE(.t0 = 0.005f, .dur = vr(0.26f, 0.15f), .amp = 1.0f, .p0 = vr(165, 0.1f), .pm = vr(195, 0.1f), .p1 = vr(118, 0.1f), .v0 = g_variant == 1 ? V_AE : V_A, .v1 = g_variant == 2 ? V_ER : V_U,
          .breath = 0.3f, .drive = 2.8f, .jitter = 1.2f, .att = 0.01f, .rel = 0.11f);
    fx_crush(8.0f, 2, 0.35f);
    layer_end(1.0f);
}

static void sfx_death(void)
{
    begin(1.2f);
    VOICE(.t0 = 0, .dur = vr(0.95f, 0.1f), .amp = 1.0f, .p0 = vr(118, 0.08f), .pm = vr(100, 0.08f), .p1 = vr(62, 0.08f), .v0 = V_A, .v1 = V_OO,
          .breath = 0.55f, .drive = 2.2f, .jitter = 2.5f, .gurgle = 0.65f, .att = 0.03f, .rel = 0.5f);
    NOISE(.t0 = 0.05f, .dur = 0.9f, .amp = 0.35f, .type = NZ_BP, .f0 = 1300, .f1 = 500, .q = 1.5f, .att = 0.08f, .dec = -1);
    l_squish(0.15f, 0.5f, 0.25f, 700, 250, 4.0f, 22.0f);
    fx_crush(9.0f, 2, 0.3f);
}

static void sfx_bodyfall(void)
{
    begin(0.85f);
    THUMP(.t0 = 0, .f0 = 100, .f1 = 40, .ptau = 0.016f, .dec = 0.1f, .amp = 1.0f, .drive = 2.5f);
    NOISE(.t0 = 0, .dur = 0.15f, .amp = 0.75f, .type = NZ_LP, .f0 = 1600, .f1 = 300, .q = 0.8f, .att = 0.0006f, .dec = 0.033f);
    THUMP(.t0 = vr(0.085f, 0.3f), .f0 = vr(125, 0.15f), .f1 = 60, .ptau = 0.01f, .dec = 0.05f, .amp = 0.55f, .drive = 2.0f);
    NOISE(.t0 = vr(0.085f, 0.3f), .dur = 0.1f, .amp = 0.4f, .type = NZ_LP, .f0 = 1800, .f1 = 400, .q = 0.8f, .att = 0.0006f, .dec = 0.02f);
    NOISE(.t0 = 0.16f, .dur = 0.2f, .amp = 0.18f, .type = NZ_LP, .f0 = 700, .f1 = 300, .q = 0.8f, .att = 0.01f, .dec = 0.05f);
    fx_drive(0, 0, 1.7f);
    fx_reverb(0.07f, 0.5f, 3000.0f, 0.7f, 0.005f);
}

static void sfx_footstep(void)
{
    begin(0.16f);
    NOISE(.t0 = 0, .dur = 0.04f, .amp = vr(0.55f, 0.25f), .type = NZ_BP, .f0 = vr(2500, 0.2f), .f1 = vr(1800, 0.2f), .q = 1.2f, .att = 0.0004f, .dec = vr(0.006f, 0.25f));
    NOISE(.t0 = 0, .dur = 0.09f, .amp = 0.8f, .type = NZ_LP, .f0 = vr(600, 0.2f), .f1 = 250, .q = 0.8f, .att = 0.001f, .dec = vr(0.016f, 0.2f));
    THUMP(.t0 = 0, .f0 = vr(150, 0.12f), .f1 = 85, .ptau = 0.008f, .dec = 0.016f, .amp = vr(0.45f, 0.2f));
    NOISE(.t0 = vr(0.034f, 0.3f), .dur = 0.03f, .amp = vr(0.22f, 0.4f), .type = NZ_BP, .f0 = vr(3200, 0.2f), .f1 = 2600, .q = 1.5f, .att = 0.0004f, .dec = 0.004f);
}

static void sfx_alert(void)
{
    begin(0.8f);
    TONE(.t0 = 0, .dur = 0.03f, .amp = 0.5f, .wave = TW_SQUARE, .f0 = 600, .f1 = 1175, .att = 0.001f, .dec = -1, .lp = 6000);
    TONE(.t0 = 0.028f, .dur = 0.35f, .amp = 0.5f, .wave = TW_SQUARE, .f0 = 1175, .f1 = 1175, .att = 0.002f, .dec = 0.09f, .lp = 5000, .pw = 0.3f);
    TONE(.t0 = 0.028f, .dur = 0.35f, .amp = 0.38f, .wave = TW_SAW, .f0 = 1661, .f1 = 1661, .att = 0.002f, .dec = 0.08f, .lp = 6000);
    FM(.t0 = 0.028f, .dur = 0.45f, .amp = 0.45f, .fc = 2349, .ratio = 3.5f, .i0 = 2.5f, .i1 = 0.2f, .idec = 0.06f, .att = 0.001f, .dec = 0.12f);
    fx_echo(0.09f, 0.135f, 0.32f, 0.35f, 5000.0f);
}

static void sfx_surrender(void)
{
    begin(0.85f);
    VOICE(.t0 = 0, .dur = 0.2f, .amp = 0.8f, .p0 = 360, .pm = 380, .p1 = 330, .v0 = V_I, .v1 = V_E,
          .breath = 0.55f, .drive = 1.2f, .jitter = 1.5f, .vib = 0.035f, .vrate = 9.0f, .att = 0.02f, .rel = 0.06f, .fshift = 1.15f);
    VOICE(.t0 = 0.23f, .dur = 0.38f, .amp = 0.9f, .p0 = 340, .pm = 320, .p1 = 235, .v0 = V_O, .v1 = V_OO,
          .breath = 0.55f, .drive = 1.2f, .jitter = 1.5f, .vib = 0.045f, .vrate = 8.5f, .att = 0.02f, .rel = 0.18f, .fshift = 1.15f);
    TONE(.t0 = 0.5f, .dur = 0.28f, .amp = 0.22f, .wave = TW_TRI, .f0 = 880, .f1 = 415, .att = 0.004f, .dec = 0.1f);
}

/* talk (audio_voice): one syllable of made-up speech - a consonant (a voiced or a voiceless stop, a hum, a hiss,
   a breath, or a glide straight into it), then the vowel, sliding a little towards the next one */
enum { ON_VOICED, ON_STOP, ON_NASAL, ON_HISS, ON_BREATH, ON_GLIDE };
typedef struct { int on; float place; int v0, v1; } Syllable;
static const Syllable SYLLABLES[16] = {
    { ON_VOICED,  900, V_OO, V_A  },   /* ba */
    { ON_VOICED, 3400, V_ER, V_E  },   /* de */
    { ON_NASAL,     0, V_E,  V_I  },   /* mi */
    { ON_NASAL,     0, V_U,  V_O  },   /* no */
    { ON_GLIDE,     0, V_OO, V_A  },   /* wa */
    { ON_VOICED, 2100, V_AE, V_E  },   /* ge */
    { ON_GLIDE,     0, V_ER, V_U  },   /* lu */
    { ON_BREATH,    0, V_A,  V_O  },   /* ho */
    { ON_STOP,   1000, V_E,  V_I  },   /* pi */
    { ON_STOP,   3800, V_AE, V_A  },   /* ta */
    { ON_STOP,   1900, V_O,  V_OO },   /* ko */
    { ON_NASAL,     0, V_U,  V_A  },   /* ma */
    { ON_HISS,   5200, V_A,  V_AE },   /* sa */
    { ON_GLIDE,     0, V_I,  V_E  },   /* ye */
    { ON_GLIDE,     0, V_ER, V_OO },   /* ru */
    { ON_BREATH,    0, V_A,  V_I  },   /* hai */
};

static void talk_syllable(const TalkVoice *v, const Syllable *sy, float lean)
{
    const float f0 = v->f0, fs = v->throat;
    float t = 0, d = vr(0.078f, 0.12f) * v->drawl;
    begin(0.13f + d);
    switch (sy->on) {
    case ON_VOICED:   /* closed lips or tongue, the voice humming behind them, then the release */
        TONE(.t0 = 0, .dur = 0.018f, .amp = 0.18f, .wave = TW_SINE, .f0 = f0, .f1 = f0, .att = 0.004f, .dec = -1);
        NOISE(.t0 = 0.016f, .dur = 0.02f, .amp = 0.5f, .type = NZ_BP, .f0 = sy->place * fs, .f1 = sy->place * fs * 0.8f,
              .q = 1.4f, .att = 0.0004f, .dec = 0.004f);
        t = 0.017f;
        break;
    case ON_STOP:     /* the release with no voice behind it, and a puff of air before the vowel */
        NOISE(.t0 = 0, .dur = 0.02f, .amp = 0.7f, .type = NZ_BP, .f0 = sy->place * fs, .f1 = sy->place * fs * 0.85f,
              .q = 1.3f, .att = 0.0003f, .dec = 0.004f);
        NOISE(.t0 = 0.004f, .dur = 0.03f, .amp = 0.22f, .type = NZ_BP, .f0 = 1600 * fs, .f1 = 1300 * fs, .q = 0.9f, .att = 0.004f, .dec = -1);
        t = 0.026f;
        break;
    case ON_NASAL:    /* a hum through the nose, the mouth opening out of it */
        VOICE(.t0 = 0, .dur = 0.038f, .amp = 0.45f, .p0 = f0 * 0.97f, .pm = f0, .p1 = f0, .v0 = V_OO, .v1 = V_OO,
              .breath = 0.05f, .drive = 1.2f, .jitter = 0.4f, .att = 0.006f, .rel = 0.016f, .fshift = fs * 0.75f, .nasal = 1.0f, .body = 1.0f);
        t = 0.026f;
        break;
    case ON_HISS:
        NOISE(.t0 = 0, .dur = 0.06f, .amp = 0.3f, .type = NZ_BP, .f0 = sy->place * fs, .f1 = sy->place * fs * 1.1f, .q = 1.6f, .att = 0.012f, .dec = -1);
        t = 0.045f;
        break;
    case ON_BREATH:
        NOISE(.t0 = 0, .dur = 0.04f, .amp = 0.3f, .type = NZ_BP, .f0 = 1500 * fs, .f1 = 1100 * fs, .q = 0.8f, .att = 0.008f, .dec = -1);
        t = 0.024f;
        break;
    default: break;
    }
    /* the vowel: up a touch, then falling off - said, not sung (a sing-song voice climbs and falls further) */
    VOICE(.t0 = t, .dur = d, .amp = 1.0f, .p0 = vr(f0, 0.03f), .pm = f0 * (1.03f + 0.1f * v->lilt), .p1 = vr(f0 * (0.92f - 0.08f * v->lilt), 0.03f),
          .v0 = sy->v0, .v1 = sy->v1, .breath = 0.06f + 0.5f * v->breath, .drive = 1.4f + 1.8f * v->rasp, .jitter = 0.5f + 2.5f * v->rasp,
          .creak = 0.55f * v->rasp, .tilt = 4500.0f - 3000.0f * au_clampf(v->breath, 0.0f, 1.0f), .nasal = v->nasal, .f2shift = lean, .body = 0.8f,
          .att = sy->on == ON_GLIDE ? 0.014f : 0.007f, .rel = 0.032f * v->drawl, .fshift = fs);
    fx_drive(0, 0, 1.2f);
}

/* animals: small, open vocal tracts (formants shifted up) over chest thumps and breath */
static void sfx_bark(void)
{
    begin(0.6f);
    THUMP(.t0 = 0, .f0 = vr(230, 0.1f), .f1 = 110, .ptau = 0.012f, .dec = 0.03f, .amp = 0.55f, .drive = 2.0f);
    NOISE(.t0 = 0, .dur = 0.1f, .amp = 0.35f, .type = NZ_BP, .f0 = 1900, .f1 = 900, .q = 1.0f, .att = 0.003f, .dec = 0.025f);
    VOICE(.t0 = 0.004f, .dur = vr(0.15f, 0.12f), .amp = 1.0f, .p0 = vr(420, 0.1f), .pm = vr(520, 0.1f), .p1 = vr(250, 0.1f), .v0 = V_AE, .v1 = V_U,
          .breath = 0.45f, .drive = 4.0f, .jitter = 2.0f, .att = 0.006f, .rel = 0.06f, .fshift = 1.35f);
    fx_drive(0, 0, 1.4f);
    fx_reverb(0.12f, 0.6f, 4000.0f, 0.6f, 0.01f);
}

static void sfx_growl(void)
{
    begin(1.1f);
    VOICE(.t0 = 0, .dur = vr(0.85f, 0.12f), .amp = 1.0f, .p0 = vr(92, 0.1f), .pm = vr(108, 0.1f), .p1 = vr(84, 0.1f), .v0 = V_U, .v1 = V_ER,
          .breath = 0.6f, .drive = 3.5f, .jitter = 4.0f, .gurgle = 0.85f, .att = 0.12f, .rel = 0.25f, .fshift = 1.25f);
    NOISE(.t0 = 0.05f, .dur = 0.8f, .amp = 0.22f, .type = NZ_BP, .f0 = 700, .f1 = 480, .q = 2.0f, .att = 0.15f, .dec = -1);
    fx_crush(8.0f, 2, 0.25f);
}

static void sfx_yelp(void)
{
    begin(0.5f);
    VOICE(.t0 = 0, .dur = vr(0.26f, 0.15f), .amp = 1.0f, .p0 = vr(680, 0.1f), .pm = vr(1040, 0.1f), .p1 = vr(520, 0.1f), .v0 = V_I, .v1 = V_E,
          .breath = 0.25f, .drive = 1.8f, .jitter = 1.5f, .vib = 0.03f, .vrate = 12.0f, .att = 0.006f, .rel = 0.12f, .fshift = 1.7f);
    NOISE(.t0 = 0, .dur = 0.05f, .amp = 0.25f, .type = NZ_BP, .f0 = 3000, .f1 = 2000, .q = 1.2f, .att = 0.002f, .dec = 0.012f);
}

static void sfx_hiss(void)
{
    begin(0.8f);
    l_click(0, 0.45f, 0.0004f);
    NOISE(.t0 = 0, .dur = 0.06f, .amp = 0.8f, .type = NZ_BP, .f0 = 2500, .f1 = 1800, .q = 1.2f, .att = 0.001f, .dec = 0.015f);
    NOISE(.t0 = 0.02f, .dur = vr(0.6f, 0.15f), .amp = 0.9f, .type = NZ_HP, .f0 = 3200, .f1 = 4200, .q = 0.9f, .att = 0.04f, .dec = -1);
    NOISE(.t0 = 0.02f, .dur = 0.55f, .amp = 0.5f, .type = NZ_BP, .f0 = 5500, .f1 = 6500, .q = 2.5f, .att = 0.05f, .dec = -1);
}

static void sfx_bite(void)
{
    begin(0.3f);
    l_click(0, 0.9f, 0.0003f);
    float fr[3] = { vr(1450, 0.1f), vr(2300, 0.1f), 3700 }, ga[3] = { 1.0f, 0.6f, 0.3f }, dc[3] = { 0.012f, 0.008f, 0.005f };
    l_modes(0, 0.6f, 3, fr, ga, dc, 0, 0.1f, 0, 0);
    THUMP(.t0 = 0, .f0 = 300, .f1 = 140, .ptau = 0.006f, .dec = 0.02f, .amp = 0.5f, .drive = 2.0f);
    NOISE(.t0 = 0.003f, .dur = 0.08f, .amp = 0.4f, .type = NZ_BP, .f0 = 1200, .f1 = 600, .q = 1.5f, .att = 0.001f, .dec = 0.02f);
    l_squish(0.01f, 0.12f, 0.2f, 900, 400, 3.0f, 30.0f);
}

/* plants: wet lip-pops and bubbly squish, acid fizz, whipping tendrils, dry leafy crackle */
static void sfx_spit(void)
{
    begin(0.35f);
    /* "p": lip-pop release - a click on a short pressure thump */
    l_click(0, 0.55f, 0.0003f);
    THUMP(.t0 = 0, .f0 = vr(320, 0.12f), .f1 = 130, .ptau = 0.005f, .dec = 0.016f, .amp = 0.7f, .drive = 1.8f);
    /* "tuh": aspirated burst through a short throaty "uh" */
    NOISE(.t0 = 0.002f, .dur = 0.1f, .amp = 0.8f, .type = NZ_BP, .f0 = vr(2600, 0.15f), .f1 = 1100, .q = 1.1f, .att = 0.002f, .dec = vr(0.022f, 0.2f));
    VOICE(.t0 = 0.006f, .dur = vr(0.08f, 0.15f), .amp = 0.35f, .p0 = vr(210, 0.1f), .pm = 240, .p1 = 150, .v0 = V_U, .v1 = V_ER,
          .breath = 0.9f, .drive = 2.0f, .jitter = 3.0f, .gurgle = 0.7f, .att = 0.004f, .rel = 0.04f, .fshift = 1.4f);
    /* the glob leaving: gurgly squish and rising bubble "bloops" */
    l_squish(0.012f, vr(0.24f, 0.15f), 0.9f, vr(1700, 0.2f), vr(420, 0.2f), 3.0f, vr(55.0f, 0.25f));
    TONE(.t0 = vr(0.02f, 0.3f), .dur = 0.045f, .amp = 0.3f, .wave = TW_SINE, .f0 = vr(450, 0.15f), .f1 = vr(1300, 0.15f), .att = 0.002f, .dec = 0.014f);
    TONE(.t0 = vr(0.07f, 0.3f), .dur = 0.03f, .amp = 0.15f, .wave = TW_SINE, .f0 = 700, .f1 = 1800, .att = 0.001f, .dec = 0.008f);
    fx_drive(0, 0, 1.5f);
}

static void sfx_splat(void)
{
    begin(0.45f);
    /* impact: soft wet slap */
    l_click(0, 0.4f, 0.0004f);
    THUMP(.t0 = 0, .f0 = vr(260, 0.15f), .f1 = 120, .ptau = 0.007f, .dec = 0.018f, .amp = 0.5f, .drive = 2.0f);
    NOISE(.t0 = 0, .dur = 0.07f, .amp = 0.9f, .type = NZ_LP, .f0 = vr(3800, 0.2f), .f1 = 600, .q = 0.9f, .att = 0.0005f, .dec = vr(0.014f, 0.2f));
    l_squish(0.003f, vr(0.16f, 0.15f), 0.85f, vr(2200, 0.2f), vr(500, 0.2f), 2.8f, vr(70.0f, 0.25f));
    l_squish(0.02f, 0.12f, 0.4f, vr(1100, 0.2f), 350, 3.5f, 40.0f);
    /* spatter droplets */
    l_grains(0.006f, 0.08f, 8, 0.0f, 0.35f, 1800, 5000, 0.002f, 0.005f, 0, 0.0f);
    /* acid sizzle: hissy fizz with a dense crackle that thins out */
    NOISE(.t0 = 0.015f, .dur = vr(0.3f, 0.12f), .amp = 0.3f, .type = NZ_BP, .f0 = 5200, .f1 = 4200, .q = 0.9f, .att = 0.03f, .dec = 0.08f);
    l_grains(0.015f, 0.3f, 50, 0.1f, 0.35f, 2500, 7500, 0.0005f, 0.0015f, 0, 0.0f);
    fx_drive(0, 0, 1.6f);
}

static void sfx_lash(void)
{
    begin(0.3f);
    const float tc = vr(0.12f, 0.12f);   /* moment of the crack */
    /* rising airy swish: band-passed noise sweeping up into the crack */
    NOISE(.t0 = 0, .dur = tc + 0.01f, .amp = 0.5f, .type = NZ_BP, .f0 = vr(450, 0.15f), .f1 = vr(3600, 0.15f), .q = 1.8f, .att = tc * 0.8f, .dec = -1);
    NOISE(.t0 = 0, .dur = tc + 0.006f, .amp = 0.18f, .type = NZ_BP, .f0 = 900, .f1 = vr(5200, 0.12f), .q = 6.0f, .att = tc * 0.85f, .dec = -1);
    /* whip crack: near-instant snap */
    l_click(tc, 1.5f, 0.00015f);
    NOISE(.t0 = tc, .dur = 0.03f, .amp = 2.0f, .type = NZ_HP, .f0 = 2800, .f1 = 1800, .q = 0.8f, .att = 0.0001f, .dec = 0.0035f);
    NOISE(.t0 = tc, .dur = 0.06f, .amp = 0.8f, .type = NZ_BP, .f0 = vr(3200, 0.15f), .f1 = 1500, .q = 1.3f, .att = 0.0002f, .dec = 0.009f);
    THUMP(.t0 = tc, .f0 = 520, .f1 = 240, .ptau = 0.004f, .dec = 0.008f, .amp = 0.3f, .drive = 1.5f);
    /* leafy rattle after the snap */
    l_grains(tc + 0.004f, 0.14f, 18, 0.06f, 0.28f, 1500, 5500, 0.001f, 0.003f, 0, 0.0f);
    fx_drive(0, 0, 1.4f);
    fx_reverb(0.1f, 0.5f, 6000.0f, 0.6f, 0.006f);
}

static void sfx_rustle(void)
{
    begin(0.6f);
    /* a few irregular, overlapping shakes of leafy band-passed noise, each carrying
       a scatter of tiny dry leaf ticks; no pitched parts so it stays easy on the ear */
    float t = 0.0f, span = au_rrange(&rng, 0.22f, 0.28f);
    while (t < span) {
        float d = au_rrange(&rng, 0.06f, 0.12f), f = au_rrange(&rng, 1800, 3800);
        float a = au_rrange(&rng, 0.4f, 1.0f) * (1.0f - 0.5f * t / span);
        NOISE(.t0 = t, .dur = d, .amp = a * 0.5f, .type = NZ_BP, .f0 = f, .f1 = f * au_rrange(&rng, 0.75f, 1.25f), .q = 0.8f, .att = d * 0.35f, .dec = -1);
        l_grains(t, d, 18 + (int)(au_rnd(&rng) * 10.0f), 0.0f, a * 0.2f, 1500, 6500, 0.0005f, 0.0014f, 0, 0.0f);
        t += d * au_rrange(&rng, 0.45f, 0.8f);
    }
    /* now and then a twig ticks */
    if (au_rnd(&rng) < 0.6f) l_grains(au_rrange(&rng, 0.03f, t), 0.01f, 1, 0.0f, 0.05f, 1600, 3200, 0.003f, 0.007f, 1, 0.0f);
    /* soft mass of the foliage moving */
    NOISE(.t0 = 0, .dur = t + 0.05f, .amp = 0.4f, .type = NZ_BP, .f0 = 650, .f1 = 450, .q = 0.7f, .att = 0.04f, .dec = -1);
    fx_filter(NZ_LP, 6500.0f, 0.707f);
}

static void sfx_door_slam(void)
{
    begin(1.1f);
    l_click(0, 0.6f, 0.0003f);
    THUMP(.t0 = 0, .f0 = 80, .f1 = 42, .ptau = 0.02f, .dec = 0.15f, .amp = 1.1f, .drive = 2.5f);
    float fr[] = { 180, 410, 730, 1150 }, ga[] = { 1, 0.7f, 0.5f, 0.3f }, dc[] = { 0.15f, 0.1f, 0.07f, 0.05f };
    l_modes(0, 0.45f, 4, fr, ga, dc, 0, 0.2f, 0.03f, 0.01f);
    NOISE(.t0 = 0, .dur = 0.2f, .amp = 0.7f, .type = NZ_LP, .f0 = 2200, .f1 = 400, .q = 0.8f, .att = 0.0005f, .dec = 0.04f);
    NOISE(.t0 = 0.012f, .dur = 0.03f, .amp = 0.45f, .type = NZ_BP, .f0 = 3500, .f1 = 3000, .q = 2.0f, .att = 0.0001f, .dec = 0.004f);
    float fl[] = { 2100, 3300 }, gl[] = { 1, 0.6f }, dl[] = { 0.03f, 0.02f };
    l_modes(0.012f, 0.25f, 2, fl, gl, dl, 0, 0, 0, 0);
    fx_drive(0, 0, 1.6f);
    fx_reverb(0.22f, 0.9f, 4000.0f, 1.0f, 0.008f);
}

static void sfx_door_creak(void)
{
    begin(1.25f);
    /* stick-slip friction: irregular pulse train into wood resonances */
    AuSvf r1 = {0}, r2 = {0}, r3 = {0};
    au_svf_set(&r1, 640, 9.0f); au_svf_set(&r2, 1330, 11.0f); au_svf_set(&r3, 2600, 12.0f);
    const float dur = 1.15f;
    float next = 0.0f;
    int n = T(dur);
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE, u = t / dur;
        float exc = 0.0f;
        if (t >= next) {
            float f = u < 0.45f ? au_lerpf(150, 390, u / 0.45f) : au_lerpf(390, 240, (u - 0.45f) / 0.55f);
            f *= 1.0f + 0.15f * au_rnd2(&rng);
            next = t + 1.0f / f;
            exc = 0.7f + 0.3f * au_rnd(&rng);
        }
        float s = exc + au_rnd2(&rng) * 0.02f;
        float y = au_svf_bp(&r1, s) + 0.7f * au_svf_bp(&r2, s) + 0.35f * au_svf_bp(&r3, s);
        float e = sinf(AU_PI * u); e = e * (0.6f + 0.4f * sinf(AU_PI * 3.0f * u + 0.5f));
        y *= e * e;
        gL[i] += y; gR[i] += y;
    }
    fx_reverb(0.12f, 0.6f, 4000.0f, 0.8f, 0.005f);
}

static void sfx_cart_hit(void)
{
    begin(1.1f);
    l_click(0, 0.8f, 0.0002f);
    THUMP(.t0 = 0, .f0 = 120, .f1 = 55, .ptau = 0.01f, .dec = 0.06f, .amp = 0.7f, .drive = 2.0f);
    float fr[] = { 1180, 1730, 2480, 3310, 4560 }, ga[] = { 1, 0.8f, 0.6f, 0.45f, 0.3f }, dc[] = { 0.22f, 0.16f, 0.12f, 0.09f, 0.07f };
    l_modes(0, 0.35f, 5, fr, ga, dc, 0, 0.3f, 0.01f, 0.01f);
    l_grains(0.006f, 0.65f, 46, 0.2f, 0.4f, 1500, 5200, 0.008f, 0.03f, 1, 0.5f);
    NOISE(.t0 = 0, .dur = 0.12f, .amp = 0.5f, .type = NZ_HP, .f0 = 2000, .f1 = 1500, .q = 0.8f, .att = 0.0002f, .dec = 0.025f, .width = 0.5f);
    fx_drive(0, 0, 1.4f);
    fx_reverb(0.12f, 0.7f, 5000.0f, 0.8f, 0.006f);
}

static void sfx_cart_roll(void)
{
    begin(0.22f);
    NOISE(.t0 = 0, .dur = 0.2f, .amp = 0.5f, .type = NZ_LP, .f0 = 260, .f1 = 180, .q = 1.0f, .att = 0.01f, .dec = 0.07f, .brown = 0.5f);
    l_grains(0.0f, 0.14f, 6, 0.0f, 0.35f, 1800, 4800, 0.006f, 0.02f, 1, 0.0f);
}

static void sfx_loot_rummage(void)
{
    begin(0.5f);
    for (int k = 0; k < 7; k++) {
        float t = au_rrange(&rng, 0.0f, 0.36f), f = au_rrange(&rng, 1300, 5200), d = au_rrange(&rng, 0.03f, 0.08f);
        NOISE(.t0 = t, .dur = d, .amp = au_rrange(&rng, 0.4f, 0.9f), .type = NZ_BP, .f0 = f, .f1 = f * au_rrange(&rng, 0.7f, 1.3f),
              .q = 0.9f, .att = d * 0.3f, .dec = -1);
    }
    float fr[] = { 2650, 3920, 5400 }, ga[] = { 1, 0.6f, 0.3f }, dc[] = { 0.04f, 0.03f, 0.02f };
    l_modes(0.12f, 0.18f, 3, fr, ga, dc, 0, 0, 0, 0);
    l_modes(0.29f, 0.12f, 3, fr, ga, dc, 0, 0, 0.05f, 0.01f);
}

static void sfx_loot_found(void)
{
    begin(0.95f);
    FM(.t0 = 0, .dur = 0.4f, .amp = 0.6f, .fc = mtof(88), .ratio = 3.5f, .i0 = 2.2f, .i1 = 0.1f, .idec = 0.07f, .att = 0.001f, .dec = 0.12f, .pan = -0.2f);
    FM(.t0 = 0.065f, .dur = 0.5f, .amp = 0.6f, .fc = mtof(95), .ratio = 3.5f, .i0 = 2.2f, .i1 = 0.1f, .idec = 0.07f, .att = 0.001f, .dec = 0.16f, .pan = 0.2f);
    TONE(.t0 = 0.065f, .dur = 0.12f, .amp = 0.08f, .wave = TW_SINE, .f0 = 5200, .f1 = 6400, .att = 0.002f, .dec = 0.03f);
    fx_echo(0.08f, 0.12f, 0.3f, 0.3f, 6000.0f);
}

static void sfx_pickup(void)
{
    begin(0.25f);
    TONE(.t0 = 0, .dur = 0.035f, .amp = 0.4f, .wave = TW_SQUARE, .f0 = 660, .f1 = 990, .att = 0.001f, .dec = -1, .lp = 5000, .pw = 0.35f);
    TONE(.t0 = 0.03f, .dur = 0.16f, .amp = 0.45f, .wave = TW_SQUARE, .f0 = 1319, .f1 = 1319, .att = 0.001f, .dec = 0.045f, .lp = 5000, .pw = 0.35f);
    TONE(.t0 = 0.03f, .dur = 0.16f, .amp = 0.3f, .wave = TW_SINE, .f0 = 2638, .f1 = 2638, .att = 0.001f, .dec = 0.035f);
    NOISE(.t0 = 0, .dur = 0.05f, .amp = 0.3f, .type = NZ_BP, .f0 = 1500, .f1 = 1200, .q = 1.2f, .att = 0.002f, .dec = 0.015f);
}

static void sfx_pickup_weapon(void)
{
    begin(0.6f);
    NOISE(.t0 = 0, .dur = 0.13f, .amp = 0.4f, .type = NZ_BP, .f0 = 1000, .fm = 2600, .f1 = 1800, .fpos = 0.5f, .q = 1.5f, .att = 0.05f, .dec = -1);
    l_click(0.085f, 0.6f, 0.0003f);
    float fa[] = { 1650, 2850, 4300 }, ga[] = { 1, 0.7f, 0.4f }, da[] = { 0.05f, 0.04f, 0.03f };
    l_modes(0.085f, 0.75f, 3, fa, ga, da, 0, 0.2f, 0.02f, 0.008f);
    NOISE(.t0 = 0.085f, .dur = 0.03f, .amp = 0.7f, .type = NZ_BP, .f0 = 3200, .f1 = 2800, .q = 1.5f, .att = 0.0001f, .dec = 0.005f);
    l_click(0.2f, 0.6f, 0.0003f);
    float fb[] = { 1400, 2500, 3700 }, db[] = { 0.045f, 0.035f, 0.025f };
    l_modes(0.2f, 0.65f, 3, fb, ga, db, 0, 0.2f, 0.02f, 0.008f);
    THUMP(.t0 = 0.2f, .f0 = 190, .f1 = 100, .ptau = 0.01f, .dec = 0.03f, .amp = 0.45f, .drive = 1.5f);
    fx_drive(0, 0, 1.3f);
    fx_reverb(0.08f, 0.5f, 5000.0f, 0.7f, 0.004f);
}

static void sfx_list_tick(void)
{
    begin(1.2f);
    NOISE(.t0 = 0, .dur = 0.028f, .amp = 0.5f, .type = NZ_BP, .f0 = 5200, .f1 = 4400, .q = 2.0f, .att = 0.003f, .dec = 0.008f);
    NOISE(.t0 = 0.032f, .dur = 0.065f, .amp = 0.6f, .type = NZ_BP, .f0 = 3800, .f1 = 6500, .q = 2.0f, .att = 0.004f, .dec = -1);
    l_click(0.0f, 0.15f, 0.0002f);
    FM(.t0 = 0.06f, .dur = 0.55f, .amp = 0.55f, .fc = mtof(88), .ratio = 2.0f, .i0 = 1.6f, .i1 = 0.15f, .idec = 0.09f, .att = 0.001f, .dec = 0.2f, .pan = -0.15f);
    FM(.t0 = 0.125f, .dur = 0.65f, .amp = 0.55f, .fc = mtof(93), .ratio = 2.0f, .i0 = 1.6f, .i1 = 0.15f, .idec = 0.09f, .att = 0.001f, .dec = 0.26f, .pan = 0.15f);
    FM(.t0 = 0.125f, .dur = 0.4f, .amp = 0.16f, .fc = mtof(105), .ratio = 3.5f, .i0 = 1.0f, .i1 = 0.0f, .idec = 0.05f, .att = 0.001f, .dec = 0.08f);
    fx_echo(0.11f, 0.165f, 0.25f, 0.28f, 6000.0f);
}

static void sfx_list_done(void)
{
    begin(2.6f);
    const int notes[] = { 69, 73, 76, 81, 85 };
    for (int k = 0; k < 5; k++)
        SAWS(.t0 = 0.065f * (float)k, .dur = 0.12f, .amp = 0.55f, .freq = mtof((float)notes[k]), .nv = 3, .det = 14,
             .att = 0.002f, .dec = 0.08f, .sus = 0.2f, .rel = 0.12f, .cut0 = 6000, .cut1 = 1800, .ctau = 0.06f, .width = 0.5f, .sq = 0.3f);
    const int chord[] = { 81, 85, 88, 93 };
    for (int k = 0; k < 4; k++)
        SAWS(.t0 = 0.33f, .dur = 0.55f, .amp = 0.42f, .freq = mtof((float)chord[k]), .nv = 5, .det = 22,
             .att = 0.004f, .dec = 0.35f, .sus = 0.35f, .rel = 0.4f, .cut0 = 7000, .cut1 = 2200, .ctau = 0.25f, .width = 0.9f);
    FM(.t0 = 0.33f, .dur = 1.0f, .amp = 0.3f, .fc = mtof(93), .ratio = 3.5f, .i0 = 2.0f, .i1 = 0.1f, .idec = 0.1f, .att = 0.001f, .dec = 0.3f);
    THUMP(.t0 = 0.33f, .f0 = 160, .f1 = 50, .ptau = 0.02f, .dec = 0.12f, .amp = 0.6f, .drive = 2.0f);
    fx_echo(0.13f, 0.195f, 0.3f, 0.25f, 5000.0f);
    fx_reverb(0.25f, 1.4f, 6000.0f, 1.2f, 0.02f);
}

static void sfx_craft(void)
{
    begin(1.15f);
    /* duct tape rip: stick-slip AM on band-passed noise */
    AuSvf bp = {0}; au_svf_set(&bp, 2600, 1.2f);
    float next = 0.0f, pulse = 0.0f;
    int n = T(0.34f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE, u = t / 0.34f;
        if (t >= next) { float f = au_lerpf(170, 430, u) * (1.0f + 0.25f * au_rnd2(&rng)); next = t + 1.0f / f; pulse = 1.0f; }
        pulse *= 0.93f;
        float x = au_svf_bp(&bp, au_rnd2(&rng)) * (0.25f + pulse) * 1.6f;
        float e = (u < 0.15f ? u / 0.15f : 1.0f) * (u > 0.92f ? (1.0f - u) / 0.08f : 1.0f);
        x *= e;
        gL[i] += x; gR[i] += x;
    }
    l_click(0.45f, 0.8f, 0.0003f);
    float fr[] = { 900, 2100, 3400, 4700 }, ga[] = { 1, 0.7f, 0.45f, 0.25f }, dc[] = { 0.1f, 0.06f, 0.04f, 0.03f };
    l_modes(0.45f, 0.7f, 4, fr, ga, dc, 0, 0.15f, 0.02f, 0.01f);
    THUMP(.t0 = 0.45f, .f0 = 170, .f1 = 90, .ptau = 0.01f, .dec = 0.04f, .amp = 0.6f, .drive = 2.0f);
    fx_drive(0, 0, 1.3f);
}

static void sfx_heal(void)
{
    begin(1.3f);
    TONE(.t0 = 0, .dur = 0.85f, .amp = 0.45f, .wave = TW_TRI, .f0 = mtof(72), .f1 = mtof(79), .att = 0.18f, .dec = -1, .vib = 0.004f, .vrate = 5.0f, .pan = -0.2f);
    TONE(.t0 = 0, .dur = 0.85f, .amp = 0.4f, .wave = TW_TRI, .f0 = mtof(72) * 1.004f, .f1 = mtof(79) * 1.004f, .att = 0.18f, .dec = -1, .vib = 0.004f, .vrate = 4.3f, .pan = 0.2f);
    TONE(.t0 = 0.05f, .dur = 0.8f, .amp = 0.22f, .wave = TW_SINE, .f0 = mtof(79), .f1 = mtof(86), .att = 0.2f, .dec = -1);
    FM(.t0 = 0.30f, .dur = 0.5f, .amp = 0.12f, .fc = mtof(96), .ratio = 3.5f, .i0 = 1.5f, .i1 = 0.0f, .idec = 0.06f, .att = 0.001f, .dec = 0.12f, .pan = -0.4f);
    FM(.t0 = 0.42f, .dur = 0.5f, .amp = 0.12f, .fc = mtof(100), .ratio = 3.5f, .i0 = 1.5f, .i1 = 0.0f, .idec = 0.06f, .att = 0.001f, .dec = 0.12f, .pan = 0.4f);
    FM(.t0 = 0.54f, .dur = 0.5f, .amp = 0.12f, .fc = mtof(103), .ratio = 3.5f, .i0 = 1.5f, .i1 = 0.0f, .idec = 0.06f, .att = 0.001f, .dec = 0.14f);
    fx_reverb(0.3f, 1.4f, 6000.0f, 1.1f, 0.02f);
}

static void sfx_execute(void)
{
    begin(1.8f);
    l_click(0, 1.0f, 0.0003f);
    THUMP(.t0 = 0, .f0 = 110, .f1 = 30, .ptau = 0.03f, .dec = 0.17f, .amp = 1.6f, .drive = 4.0f);
    THUMP(.t0 = 0, .f0 = 58, .f1 = 36, .ptau = 0.05f, .dec = 0.3f, .amp = 0.5f, .drive = 1.2f);
    NOISE(.t0 = 0, .dur = 0.05f, .amp = 1.0f, .type = NZ_HP, .f0 = 2500, .f1 = 1800, .q = 0.9f, .att = 0.0001f, .dec = 0.007f);
    l_grains(0.003f, 0.14f, 30, 0.05f, 0.8f, 1200, 4500, 0.0015f, 0.004f, 0, 0.0f);
    l_squish(0.015f, 0.38f, 0.8f, 1500, 300, 3.0f, 45.0f);
    l_grains(0.04f, 0.4f, 12, 0.12f, 0.3f, 2500, 6000, 0.002f, 0.005f, 0, 0.5f);
    fx_drive(0, 0, 2.6f);
    fx_reverb(0.15f, 0.9f, 3500.0f, 1.0f, 0.008f);
}

static void sfx_combo(void)
{
    begin(0.2f);
    TONE(.t0 = 0, .dur = 0.14f, .amp = 0.5f, .wave = TW_SQUARE, .f0 = mtof(81), .f1 = mtof(84), .att = 0.001f, .dec = 0.05f, .lp = 6500, .pw = 0.25f);
    TONE(.t0 = 0, .dur = 0.14f, .amp = 0.35f, .wave = TW_SINE, .f0 = mtof(93), .f1 = mtof(96), .att = 0.001f, .dec = 0.04f);
}

static void sfx_van_door(void)
{
    begin(2.0f);
    /* roller slide with rattle */
    AuSvf rl = {0}, am = {0}; au_svf_set(&rl, 420, 0.9f); au_svf_set(&am, 28, 0.8f);
    int n = T(0.86f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE, u = t / 0.86f;
        float a = au_clampf(0.5f + au_svf_lp(&am, au_rnd2(&rng) * 10.0f), 0.1f, 1.6f);
        float x = au_svf_lp(&rl, au_rnd2(&rng)) * a * 2.2f;
        float e = (u < 0.1f ? u / 0.1f : 1.0f) * (0.6f + 0.4f * u);
        gL[i] += x * e; gR[i] += x * e;
    }
    TONE(.t0 = 0.05f, .dur = 0.8f, .amp = 0.03f, .wave = TW_TRI, .f0 = 1850, .f1 = 1650, .att = 0.2f, .dec = -1, .vib = 0.01f, .vrate = 6.0f);
    l_grains(0.0f, 0.8f, 18, 0.0f, 0.15f, 1500, 4000, 0.005f, 0.015f, 1, 0.3f);
    /* latch slam */
    l_click(0.86f, 0.9f, 0.0003f);
    THUMP(.t0 = 0.86f, .f0 = 95, .f1 = 45, .ptau = 0.02f, .dec = 0.1f, .amp = 1.0f, .drive = 2.2f);
    float fr[] = { 520, 1180, 1980, 2900 }, ga[] = { 1, 0.7f, 0.5f, 0.3f }, dc[] = { 0.2f, 0.12f, 0.08f, 0.05f };
    l_modes(0.86f, 0.45f, 4, fr, ga, dc, 0, 0.3f, 0.02f, 0.01f);
    NOISE(.t0 = 0.86f, .dur = 0.15f, .amp = 0.6f, .type = NZ_LP, .f0 = 2600, .f1 = 500, .q = 0.8f, .att = 0.0004f, .dec = 0.03f);
    fx_drive(0, 0, 1.4f);
    fx_reverb(0.12f, 0.7f, 4000.0f, 1.0f, 0.006f);
}

static void sfx_engine(void)
{
    begin(3.2f);
    const float dur = 3.0f;
    /* rpm-ish firing frequency envelope (Hz of combustion pulses) */
    AuSvf b1 = {0}, b2 = {0}, ex = {0}, st = {0};
    au_svf_set(&b1, 110, 3.0f); au_svf_set(&b2, 260, 4.0f); au_svf_set(&st, 900, 2.0f);
    float fire_ph = 0.0f, pulse = 0.0f, sph = 0.0f;
    int n = T(dur);
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE;
        float out = 0.0f;
        /* starter motor: whine with cranking compression cycles */
        if (t < 0.8f) {
            float crank = 0.55f + 0.45f * au_sin(t * 5.5f);
            float sf = 210.0f * (0.9f + 0.1f * crank);
            sph += sf / AU_FRATE; if (sph >= 1.0f) sph -= 1.0f;
            float w = (au_saw(sph, sf / AU_FRATE) * 0.5f + au_svf_bp(&st, au_rnd2(&rng)) * 0.6f) * crank;
            float e = (t < 0.05f ? t / 0.05f : 1.0f) * (t > 0.7f ? (0.8f - t) / 0.1f : 1.0f);
            out += w * e * 0.35f;
        }
        if (t > 0.7f) {
            float fz;
            if (t < 0.95f) fz = au_lerpf(12, 26, (t - 0.7f) / 0.25f);
            else if (t < 1.5f) fz = au_lerpf(26, 48, (t - 0.95f) / 0.55f);
            else if (t < 1.75f) fz = au_lerpf(48, 34, (t - 1.5f) / 0.25f);
            else fz = au_lerpf(34, 52, au_clampf((t - 1.75f) / 1.0f, 0, 1));
            fz *= 1.0f + 0.04f * au_rnd2(&rng);
            fire_ph += fz / AU_FRATE;
            if (fire_ph >= 1.0f) { fire_ph -= 1.0f; pulse = 0.8f + 0.2f * au_rnd(&rng); }
            pulse *= 0.9965f;
            float exc = pulse * pulse + au_rnd2(&rng) * pulse * 0.3f;
            float y = au_svf_bp(&b1, exc) * 1.6f + au_svf_bp(&b2, exc) * 0.9f;
            float lpc = t < 2.2f ? 1400.0f : au_lerpf(1400, 350, (t - 2.2f) / 0.8f);
            au_svf_set(&ex, lpc, 0.8f);
            y = au_svf_lp(&ex, y + au_rnd2(&rng) * pulse * 0.25f);
            float e = (t < 0.78f ? (t - 0.7f) / 0.08f : 1.0f) * (t > 2.1f ? au_clampf((dur - t) / 0.9f, 0, 1) : 1.0f);
            out += au_sat(y * 2.0f) * e * 0.8f;
        }
        gL[i] += out; gR[i] += out;
    }
    fx_reverb(0.12f, 0.8f, 3000.0f, 1.2f, 0.01f);
}

static void sfx_ui_move(void)
{
    begin(0.09f);
    TONE(.t0 = 0, .dur = 0.06f, .amp = 0.5f, .wave = TW_SINE, .f0 = 1900, .f1 = 1750, .att = 0.0008f, .dec = 0.012f);
    TONE(.t0 = 0, .dur = 0.06f, .amp = 0.3f, .wave = TW_TRI, .f0 = 950, .f1 = 900, .att = 0.0008f, .dec = 0.016f);
    l_click(0, 0.08f, 0.0002f);
}

static void sfx_ui_select(void)
{
    begin(0.75f);
    TONE(.t0 = 0, .dur = 0.08f, .amp = 0.45f, .wave = TW_SQUARE, .f0 = mtof(88), .f1 = mtof(88), .att = 0.001f, .dec = 0.04f, .lp = 7000, .pw = 0.3f, .pan = -0.15f);
    TONE(.t0 = 0.045f, .dur = 0.16f, .amp = 0.45f, .wave = TW_SQUARE, .f0 = mtof(95), .f1 = mtof(95), .att = 0.001f, .dec = 0.06f, .lp = 7000, .pw = 0.3f, .pan = 0.15f);
    TONE(.t0 = 0.045f, .dur = 0.16f, .amp = 0.25f, .wave = TW_SINE, .f0 = mtof(107), .f1 = mtof(107), .att = 0.001f, .dec = 0.04f);
    NOISE(.t0 = 0, .dur = 0.06f, .amp = 0.15f, .type = NZ_BP, .f0 = 2000, .f1 = 6500, .q = 1.5f, .att = 0.01f, .dec = -1);
    fx_echo(0.085f, 0.125f, 0.3f, 0.32f, 6000.0f);
}

static void sfx_ui_back(void)
{
    begin(0.55f);
    TONE(.t0 = 0, .dur = 0.07f, .amp = 0.4f, .wave = TW_SQUARE, .f0 = mtof(83), .f1 = mtof(83), .att = 0.001f, .dec = 0.035f, .lp = 4500, .pw = 0.3f);
    TONE(.t0 = 0.045f, .dur = 0.12f, .amp = 0.4f, .wave = TW_SQUARE, .f0 = mtof(76), .f1 = mtof(76), .att = 0.001f, .dec = 0.05f, .lp = 4000, .pw = 0.3f);
    fx_echo(0.08f, 0.12f, 0.2f, 0.2f, 4000.0f);
}

static void sfx_ui_error(void)
{
    begin(0.3f);
    for (int k = 0; k < 2; k++) {
        float t = 0.11f * (float)k;
        TONE(.t0 = t, .dur = 0.085f, .amp = 0.4f, .wave = TW_SQUARE, .f0 = 155, .f1 = 150, .att = 0.002f, .dec = -1, .lp = 2200);
        TONE(.t0 = t, .dur = 0.085f, .amp = 0.35f, .wave = TW_SAW, .f0 = 163, .f1 = 158, .att = 0.002f, .dec = -1, .lp = 2200);
    }
    fx_drive(0, 0, 1.8f);
}

static void sfx_type(void)
{
    begin(0.07f);
    NOISE(.t0 = 0, .dur = 0.02f, .amp = 0.7f, .type = NZ_BP, .f0 = vr(3100, 0.15f), .f1 = 2600, .q = 1.5f, .att = 0.0002f, .dec = 0.003f);
    THUMP(.t0 = 0, .f0 = vr(270, 0.12f), .f1 = 160, .ptau = 0.004f, .dec = 0.008f, .amp = 0.35f);
    float fr[] = { 1900 }, ga[] = { 1 }, dc[] = { 0.01f };
    l_modes(0, 0.12f, 1, fr, ga, dc, 0, 0, 0, 0);
}

static void sfx_radio(void)
{
    begin(0.75f);
    AuSvf bp = {0}, hp = {0}, fl = {0};
    au_svf_set(&bp, 1800, 0.7f); au_svf_set(&hp, 400, 0.7f); au_svf_set(&fl, 14, 0.8f);
    int n = T(0.62f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / AU_FRATE;
        float x = au_svf_hp(&hp, au_svf_bp(&bp, au_rnd2(&rng) * 1.8f));
        float fl_ = au_clampf(0.6f + au_svf_lp(&fl, au_rnd2(&rng) * 12.0f), 0.1f, 1.4f);
        if (au_rnd(&rng) < 0.0015f) x += au_rnd2(&rng) * 3.0f;
        float e = (t < 0.01f ? t / 0.01f : 1.0f) * (t > 0.54f ? (0.62f - t) / 0.08f : 1.0f);
        x *= fl_ * e;
        gL[i] += x; gR[i] += x;
    }
    l_click(0.0f, 0.6f, 0.0004f);
    NOISE(.t0 = 0.6f, .dur = 0.07f, .amp = 0.6f, .type = NZ_HP, .f0 = 2500, .f1 = 2500, .q = 0.7f, .att = 0.002f, .dec = 0.02f);
    TONE(.t0 = 0.6f, .dur = 0.05f, .amp = 0.12f, .wave = TW_SINE, .f0 = 1400, .f1 = 1400, .att = 0.001f, .dec = 0.02f);
    fx_crush(8.0f, 3, 0.7f);
}

static void sfx_level_clear(void)
{
    begin(3.3f);
    /* bVI - bVII - I (F - G - A) synthwave cadence */
    const int ch1[] = { 53, 57, 60, 65 }, ch2[] = { 55, 59, 62, 67 }, ch3[] = { 57, 61, 64, 69, 73 };
    for (int k = 0; k < 4; k++)
        SAWS(.t0 = 0.0f, .dur = 0.22f, .amp = 0.35f, .freq = mtof((float)ch1[k] + 12), .nv = 5, .det = 20,
             .att = 0.003f, .dec = 0.12f, .sus = 0.4f, .rel = 0.08f, .cut0 = 5000, .cut1 = 1600, .ctau = 0.08f, .width = 0.8f);
    for (int k = 0; k < 4; k++)
        SAWS(.t0 = 0.3f, .dur = 0.22f, .amp = 0.35f, .freq = mtof((float)ch2[k] + 12), .nv = 5, .det = 20,
             .att = 0.003f, .dec = 0.12f, .sus = 0.4f, .rel = 0.08f, .cut0 = 5000, .cut1 = 1600, .ctau = 0.08f, .width = 0.8f);
    for (int k = 0; k < 5; k++)
        SAWS(.t0 = 0.6f, .dur = 1.0f, .amp = 0.33f, .freq = mtof((float)ch3[k] + 12), .nv = 5, .det = 24,
             .att = 0.004f, .dec = 0.5f, .sus = 0.45f, .rel = 0.5f, .cut0 = 7000, .cut1 = 2400, .ctau = 0.3f, .width = 0.9f);
    SAWS(.t0 = 0.0f, .dur = 0.25f, .amp = 0.5f, .freq = mtof(41), .nv = 2, .det = 8, .att = 0.002f, .dec = 0.2f, .sus = 0.6f, .rel = 0.05f, .cut0 = 1200, .cut1 = 300, .ctau = 0.08f);
    SAWS(.t0 = 0.3f, .dur = 0.25f, .amp = 0.5f, .freq = mtof(43), .nv = 2, .det = 8, .att = 0.002f, .dec = 0.2f, .sus = 0.6f, .rel = 0.05f, .cut0 = 1200, .cut1 = 300, .ctau = 0.08f);
    SAWS(.t0 = 0.6f, .dur = 1.0f, .amp = 0.5f, .freq = mtof(45), .nv = 2, .det = 8, .att = 0.002f, .dec = 0.4f, .sus = 0.6f, .rel = 0.3f, .cut0 = 1400, .cut1 = 300, .ctau = 0.1f);
    const int arp[] = { 88, 93, 97, 100, 105 };
    for (int k = 0; k < 5; k++)
        FM(.t0 = 0.6f + 0.075f * (float)k, .dur = 0.5f, .amp = 0.16f, .fc = mtof((float)arp[k]), .ratio = 3.5f, .i0 = 1.8f, .i1 = 0.1f, .idec = 0.06f,
           .att = 0.001f, .dec = 0.14f, .pan = (k & 1) ? 0.4f : -0.4f);
    for (int k = 0; k < 3; k++)
        THUMP(.t0 = 0.3f * (float)k, .f0 = 170, .f1 = 48, .ptau = 0.02f, .dec = 0.12f, .amp = 0.8f, .drive = 2.0f);
    NOISE(.t0 = 0.6f, .dur = 0.45f, .amp = 0.5f, .type = NZ_BP, .f0 = 3500, .f1 = 2500, .q = 0.8f, .att = 0.001f, .dec = 0.12f, .width = 0.8f);
    NOISE(.t0 = 0.6f, .dur = 1.6f, .amp = 0.18f, .type = NZ_HP, .f0 = 6000, .f1 = 5000, .q = 0.7f, .att = 0.002f, .dec = 0.5f, .width = 1.0f);
    fx_echo(0.2f, 0.3f, 0.3f, 0.2f, 4500.0f);
    fx_reverb(0.25f, 1.6f, 6000.0f, 1.3f, 0.02f);
}

static void sfx_game_over(void)
{
    begin(3.2f);
    /* Am(add9) in detuned saws, then a tape-stop pitch collapse */
    const int ch[] = { 57, 60, 64, 71, 72 };
    for (int k = 0; k < 5; k++)
        SAWS(.t0 = 0.0f, .dur = 2.4f, .amp = 0.32f, .freq = mtof((float)ch[k]), .nv = 5, .det = 30,
             .att = 0.004f, .dec = 0.7f, .sus = 0.5f, .rel = 0.4f, .cut0 = 4500, .cut1 = 1000, .ctau = 0.4f, .width = 0.9f);
    SAWS(.t0 = 0.0f, .dur = 2.4f, .amp = 0.6f, .freq = mtof(33), .nv = 2, .det = 10, .att = 0.003f, .dec = 0.8f, .sus = 0.6f, .rel = 0.4f, .cut0 = 900, .cut1 = 250, .ctau = 0.2f);
    THUMP(.t0 = 0, .f0 = 90, .f1 = 30, .ptau = 0.06f, .dec = 0.4f, .amp = 1.0f, .drive = 2.0f);
    NOISE(.t0 = 0, .dur = 1.5f, .amp = 0.3f, .type = NZ_LP, .f0 = 6000, .f1 = 300, .q = 0.7f, .att = 0.002f, .dec = 0.4f, .width = 1.0f);
    fx_tapestop(0.45f, 2.3f, 0.12f);
    fx_drive(0, 0, 1.4f);
    /* the collapsing chord dies away as the "tape" stops */
    for (int i = T(1.3f); i < curN; i++) {
        float u = au_clampf((float)(i - T(1.3f)) / (float)T(1.2f), 0.0f, 1.0f);
        float g = (1.0f - u) * (1.0f - u);
        gL[i] *= g; gR[i] *= g;
    }
    fx_reverb(0.3f, 1.6f, 3500.0f, 1.4f, 0.02f);
}

static void sfx_heartbeat(void)
{
    begin(0.75f);
    THUMP(.t0 = 0, .f0 = 64, .f1 = 42, .ptau = 0.02f, .dec = 0.07f, .amp = 1.0f, .drive = 1.6f, .att = 0.004f);
    NOISE(.t0 = 0, .dur = 0.12f, .amp = 0.35f, .type = NZ_LP, .f0 = 180, .f1 = 120, .q = 0.8f, .att = 0.004f, .dec = 0.03f);
    THUMP(.t0 = 0.24f, .f0 = 72, .f1 = 46, .ptau = 0.02f, .dec = 0.06f, .amp = 0.75f, .drive = 1.6f, .att = 0.004f);
    NOISE(.t0 = 0.24f, .dur = 0.1f, .amp = 0.25f, .type = NZ_LP, .f0 = 200, .f1 = 120, .q = 0.8f, .att = 0.004f, .dec = 0.025f);
    fx_filter(NZ_LP, 400.0f, 0.7f);
}

static void sfx_perk(void)
{
    begin(3.1f);
    const int ch[] = { 74, 78, 81, 85, 88 };
    for (int k = 0; k < 5; k++)
        SAWS(.t0 = 0.0f, .dur = 0.9f, .amp = 0.26f, .freq = mtof((float)ch[k]), .nv = 4, .det = 18,
             .att = 0.03f, .dec = 0.6f, .sus = 0.35f, .rel = 0.5f, .cut0 = 6000, .cut1 = 2500, .ctau = 0.4f, .width = 0.9f);
    const int arp[] = { 86, 90, 93, 97, 100, 102 };
    for (int k = 0; k < 6; k++)
        FM(.t0 = 0.04f + 0.055f * (float)k, .dur = 0.6f, .amp = 0.17f, .fc = mtof((float)arp[k]), .ratio = 3.5f, .i0 = 2.0f, .i1 = 0.1f, .idec = 0.07f,
           .att = 0.001f, .dec = 0.18f, .pan = (k & 1) ? 0.5f : -0.5f);
    NOISE(.t0 = 0, .dur = 1.2f, .amp = 0.12f, .type = NZ_HP, .f0 = 5000, .f1 = 9000, .q = 0.7f, .att = 0.15f, .dec = -1, .width = 1.0f);
    /* shimmer tremolo */
    for (int i = 0; i < curN; i++) {
        float tr = 0.8f + 0.2f * au_sin((float)i / AU_FRATE * 7.0f);
        gL[i] *= tr; gR[i] *= tr;
    }
    fx_echo(0.15f, 0.225f, 0.3f, 0.25f, 6000.0f);
    fx_reverb(0.38f, 2.2f, 7000.0f, 1.4f, 0.02f);
}

/* ------------------------------------------------------------------------ */
/* table                                                                     */
/* ------------------------------------------------------------------------ */
typedef struct { const char *name; void (*fn)(void); float loud_db; int stereo; int variants; } SfxDef; /* loud_db: loudest 50 ms RMS */

static const SfxDef defs[SFX_COUNT] = {
    [SFX_SWING]         = { "swing",         sfx_swing,          -17.0f, 0, 3 },
    [SFX_SWING_HEAVY]   = { "swing_heavy",   sfx_swing_heavy,    -14.0f, 0, 2 },
    [SFX_PUNCH]         = { "punch",         sfx_punch,           -7.5f, 0, 3 },
    [SFX_HIT_BLUNT]     = { "hit_blunt",     sfx_hit_blunt,       -6.0f, 0, 3 },
    [SFX_HIT_BLADE]     = { "hit_blade",     sfx_hit_blade,       -9.0f, 0, 3 },
    [SFX_HIT_METAL]     = { "hit_metal",     sfx_hit_metal,      -11.0f, 1, 2 },
    [SFX_GORE]          = { "gore",          sfx_gore,            -8.5f, 0, 3 },
    [SFX_BONE_CRUNCH]   = { "bone_crunch",   sfx_bone_crunch,     -6.5f, 0, 3 },
    [SFX_STUN]          = { "stun",          sfx_stun,            -7.0f, 1, 1 },
    [SFX_PISTOL]        = { "pistol",        sfx_pistol,          -6.5f, 1, 2 },
    [SFX_REVOLVER]      = { "revolver",      sfx_revolver,        -5.0f, 1, 1 },
    [SFX_SHOTGUN]       = { "shotgun",       sfx_shotgun,         -3.5f, 1, 1 },
    [SFX_RIFLE]         = { "rifle",         sfx_rifle,           -6.0f, 1, 1 },
    [SFX_NAILGUN]       = { "nailgun",       sfx_nailgun,         -9.0f, 0, 1 },
    [SFX_EMPTY]         = { "empty",         sfx_empty,          -20.0f, 0, 1 },
    [SFX_RELOAD]        = { "reload",        sfx_reload,         -14.0f, 0, 1 },
    [SFX_SHELL]         = { "shell",         sfx_shell,         -24.0f, 0, 3 },
    [SFX_RICOCHET]      = { "ricochet",      sfx_ricochet,       -16.0f, 1, 2 },
    [SFX_THROW]         = { "throw",         sfx_throw,          -18.0f, 0, 2 },
    [SFX_GLASS_BREAK]   = { "glass_break",   sfx_glass_break,     -9.0f, 1, 1 },
    [SFX_BOTTLE_BREAK]  = { "bottle_break",  sfx_bottle_break,   -13.0f, 1, 2 },
    [SFX_EXPLOSION]     = { "explosion",     sfx_explosion,       -3.0f, 1, 1 },
    [SFX_IGNITE]        = { "ignite",        sfx_ignite,         -12.0f, 1, 1 },
    [SFX_FLAME_PUFF]    = { "flame_puff",    sfx_flame_puff,     -15.0f, 0, 1 },
    [SFX_HURT_PLAYER]   = { "hurt_player",   sfx_hurt_player,     -5.5f, 0, 2 },
    [SFX_HURT_NPC]      = { "hurt_npc",      sfx_hurt_npc,       -11.0f, 0, 3 },
    [SFX_DEATH]         = { "death",         sfx_death,          -12.0f, 0, 2 },
    [SFX_BODYFALL]      = { "bodyfall",      sfx_bodyfall,       -10.0f, 0, 3 },
    [SFX_FOOTSTEP]      = { "footstep",      sfx_footstep,      -27.0f, 0, 4 },
    [SFX_ALERT]         = { "alert",         sfx_alert,          -12.0f, 1, 1 },
    [SFX_SURRENDER]     = { "surrender",     sfx_surrender,      -13.0f, 0, 1 },
    [SFX_BARK]          = { "bark",          sfx_bark,            -9.0f, 0, 3 },
    [SFX_GROWL]         = { "growl",         sfx_growl,          -15.0f, 0, 2 },
    [SFX_YELP]          = { "yelp",          sfx_yelp,           -12.0f, 0, 3 },
    [SFX_HISS]          = { "hiss",          sfx_hiss,           -16.0f, 0, 2 },
    [SFX_BITE]          = { "bite",          sfx_bite,            -8.0f, 0, 3 },
    [SFX_DOOR_SLAM]     = { "door_slam",     sfx_door_slam,       -7.5f, 1, 1 },
    [SFX_DOOR_CREAK]    = { "door_creak",    sfx_door_creak,    -22.0f, 0, 1 },
    [SFX_CART_HIT]      = { "cart_hit",      sfx_cart_hit,       -11.0f, 1, 1 },
    [SFX_CART_ROLL]     = { "cart_roll",     sfx_cart_roll,     -24.0f, 0, 3 },
    [SFX_LOOT_RUMMAGE]  = { "loot_rummage",  sfx_loot_rummage,  -22.0f, 0, 3 },
    [SFX_LOOT_FOUND]    = { "loot_found",    sfx_loot_found,     -15.0f, 1, 1 },
    [SFX_PICKUP]        = { "pickup",        sfx_pickup,         -16.0f, 0, 1 },
    [SFX_PICKUP_WEAPON] = { "pickup_weapon", sfx_pickup_weapon,  -13.0f, 1, 1 },
    [SFX_LIST_TICK]     = { "list_tick",     sfx_list_tick,      -11.0f, 1, 1 },
    [SFX_LIST_DONE]     = { "list_done",     sfx_list_done,      -11.0f, 1, 1 },
    [SFX_CRAFT]         = { "craft",         sfx_craft,          -14.0f, 0, 1 },
    [SFX_HEAL]          = { "heal",          sfx_heal,           -15.0f, 1, 1 },
    [SFX_EXECUTE]       = { "execute",       sfx_execute,         -4.5f, 1, 1 },
    [SFX_COMBO]         = { "combo",         sfx_combo,         -16.0f, 0, 1 },
    [SFX_VAN_DOOR]      = { "van_door",      sfx_van_door,       -11.0f, 1, 1 },
    [SFX_ENGINE]        = { "engine",        sfx_engine,         -11.0f, 1, 1 },
    [SFX_UI_MOVE]       = { "ui_move",       sfx_ui_move,       -24.0f, 0, 1 },
    [SFX_UI_SELECT]     = { "ui_select",     sfx_ui_select,     -18.0f, 1, 1 },
    [SFX_UI_BACK]       = { "ui_back",       sfx_ui_back,       -20.0f, 1, 1 },
    [SFX_UI_ERROR]      = { "ui_error",      sfx_ui_error,      -16.0f, 0, 1 },
    [SFX_TYPE]          = { "type",          sfx_type,          -30.0f, 0, 3 },
    [SFX_RADIO]         = { "radio",         sfx_radio,         -24.0f, 0, 1 },
    [SFX_LEVEL_CLEAR]   = { "level_clear",   sfx_level_clear,    -10.0f, 1, 1 },
    [SFX_GAME_OVER]     = { "game_over",     sfx_game_over,       -9.0f, 1, 1 },
    [SFX_HEARTBEAT]     = { "heartbeat",     sfx_heartbeat,      -11.0f, 0, 1 },
    [SFX_PERK]          = { "perk",          sfx_perk,           -14.0f, 1, 1 },
    [SFX_SPIT]          = { "spit",          sfx_spit,           -12.0f, 0, 2 },
    [SFX_SPLAT]         = { "splat",         sfx_splat,          -14.0f, 0, 3 },
    [SFX_LASH]          = { "lash",          sfx_lash,           -12.0f, 0, 2 },
    [SFX_RUSTLE]        = { "rustle",        sfx_rustle,         -20.0f, 0, 3 },
};

static const char *defs_name(int id) { return au_sfx_name(id); }

const char *au_sfx_name(int id)
{
    if (id < 0 || id >= SFX_COUNT || !defs[id].name) return "?";
    return defs[id].name;
}

int au_sfx_build(AuSample out[SFX_COUNT][AU_SFX_MAXVAR], int nvar[SFX_COUNT])
{
    gCap = T(MAX_SECONDS);
    float *mem = (float *)malloc(sizeof(float) * (size_t)gCap * 4);
    if (!mem) return 0;
    gL = mem; gR = mem + gCap; tL = mem + 2 * gCap; tR = mem + 3 * gCap;
    g_out = &out[0][0];
    for (int i = 0; i < SFX_COUNT; i++) {
        memset(out[i], 0, sizeof out[i]);
        nvar[i] = 0;
        if (!defs[i].fn) continue;
        int nv = defs[i].variants < 1 ? 1 : (defs[i].variants > AU_SFX_MAXVAR ? AU_SFX_MAXVAR : defs[i].variants);
        for (int v = 0; v < nv; v++) {
            g_variant = v;
            au_rng_seed(&rng, 0x1234567u + (uint32_t)i * 7919u + (uint32_t)v * 104729u);
            au_rng_seed(&vrng, 0xBADC0DEu + (uint32_t)i * 31u + (uint32_t)v * 977u);
            defs[i].fn();
            finish(i, defs[i].loud_db, defs[i].stereo);
            if (out[i][v].data) nvar[i] = v + 1;
        }
    }
    g_variant = 0;
    free(mem);
    gL = gR = tL = tR = cL = cR = NULL;
    return 1;
}

int au_talk_build(const TalkVoice *v, AuSample out[AUDIO_SYLLABLES])
{
    if (v->f0 < 30.0f || v->f0 > 1000.0f || v->throat < 0.5f || v->throat > 2.0f || v->drawl < 0.3f || v->drawl > 3.0f) return 0;
    gCap = T(0.6f);
    float *mem = (float *)malloc(sizeof(float) * (size_t)gCap * 4);
    if (!mem) return 0;
    gL = mem; gR = mem + gCap; tL = mem + 2 * gCap; tR = mem + 3 * gCap;
    memset(out, 0, sizeof(AuSample) * AUDIO_SYLLABLES);
    /* the accent: which 8 of the 16 syllables are theirs, and whether their vowels sit forward or back in the mouth */
    AuRng ac;
    au_rng_seed(&ac, 0xACCE47u + (uint32_t)v->accent * 2654435761u);
    int pick[16];
    for (int i = 0; i < 16; i++) pick[i] = i;
    for (int i = 15; i > 0; i--) { int j = (int)(au_rng_next(&ac) % (uint32_t)(i + 1)), x = pick[i]; pick[i] = pick[j]; pick[j] = x; }
    float lean = au_rrange(&ac, 0.93f, 1.07f);
    int ok = 1;
    g_variant = 1;   /* every syllable a little different from the designed one */
    for (int k = 0; k < AUDIO_SYLLABLES; k++) {
        au_rng_seed(&rng, 0x7A1C5u + (uint32_t)v->accent * 7919u + (uint32_t)k * 104729u);
        au_rng_seed(&vrng, 0x5EED5u + (uint32_t)v->accent * 31u + (uint32_t)k * 977u);
        talk_syllable(v, &SYLLABLES[pick[k]], lean);
        finish_to(&out[k], "talk", -17.0f, 0);
        if (!out[k].data) ok = 0;
    }
    g_variant = 0;
    free(mem);
    gL = gR = tL = tR = cL = cR = NULL;
    if (!ok) for (int k = 0; k < AUDIO_SYLLABLES; k++) au_sample_free(&out[k]);
    return ok;
}
