/* LAST AISLE - audio: shared DSP pieces (FDN reverb, sample helpers). */
#include "audio_internal.h"
#include <stdlib.h>
#include <string.h>

/* 8-line feedback delay network with Householder mixing, per-line damping,
   stereo input diffusion and pre-delay. Dense, smooth and cheap. */
static const int fdn_base[8] = { 1031, 1327, 1523, 1801, 2053, 2311, 2633, 2917 };
static const int ap_base[4]  = { 142, 379, 107, 277 };

int au_reverb_init(AuReverb *r, float size, float max_predelay_s)
{
    memset(r, 0, sizeof *r);
    r->size = size;
    size_t total = 0;
    for (int i = 0; i < 8; i++) { r->len[i] = (int)(fdn_base[i] * size) | 1; total += (size_t)r->len[i]; }
    for (int i = 0; i < 4; i++) { r->aplen[i] = (int)(ap_base[i] * (0.5f + 0.5f * size)) | 1; total += (size_t)r->aplen[i]; }
    r->premax = (int)(max_predelay_s * AU_FRATE) + 2;
    total += (size_t)r->premax * 2;
    r->mem = (float *)calloc(total, sizeof(float));
    if (!r->mem) return 0;
    float *p = r->mem;
    for (int i = 0; i < 8; i++) { r->d[i] = p; p += r->len[i]; }
    for (int i = 0; i < 4; i++) { r->ap[i] = p; p += r->aplen[i]; }
    r->pre[0] = p; p += r->premax;
    r->pre[1] = p;
    au_reverb_set(r, 2.0f, 6000.0f, 0.0f);
    return 1;
}

void au_reverb_set(AuReverb *r, float decay_s, float damp_hz, float predelay_s)
{
    if (decay_s < 0.05f) decay_s = 0.05f;
    for (int i = 0; i < 8; i++)
        r->g[i] = powf(10.0f, -3.0f * (float)r->len[i] / (decay_s * AU_FRATE));
    r->damp = au_op_coef(damp_hz);
    int pl = (int)(predelay_s * AU_FRATE);
    if (pl < 1) pl = 1;
    if (pl > r->premax - 1) pl = r->premax - 1;
    r->prelen = pl;
}

void au_reverb_clear(AuReverb *r)
{
    for (int i = 0; i < 8; i++) { memset(r->d[i], 0, sizeof(float) * (size_t)r->len[i]); r->lp[i] = 0; r->pos[i] = 0; }
    for (int i = 0; i < 4; i++) { memset(r->ap[i], 0, sizeof(float) * (size_t)r->aplen[i]); r->appos[i] = 0; }
    memset(r->pre[0], 0, sizeof(float) * (size_t)r->premax);
    memset(r->pre[1], 0, sizeof(float) * (size_t)r->premax);
    r->prepos = 0;
}

void au_reverb_free(AuReverb *r)
{
    free(r->mem);
    memset(r, 0, sizeof *r);
}

static inline float ap_tick(float *buf, int len, int *pos, float x, float g)
{
    float b = buf[*pos];
    float y = b - g * x;
    buf[*pos] = x + g * y;
    if (++*pos >= len) *pos = 0;
    return y;
}

void au_reverb_tick(AuReverb *r, float inl, float inr, float *outl, float *outr)
{
    /* pre-delay */
    int rp = r->prepos - r->prelen;
    if (rp < 0) rp += r->premax;
    r->pre[0][r->prepos] = inl;
    r->pre[1][r->prepos] = inr;
    float l = r->pre[0][rp], rr = r->pre[1][rp];
    if (++r->prepos >= r->premax) r->prepos = 0;

    /* input diffusion */
    l  = ap_tick(r->ap[0], r->aplen[0], &r->appos[0], l, 0.62f);
    l  = ap_tick(r->ap[1], r->aplen[1], &r->appos[1], l, 0.55f);
    rr = ap_tick(r->ap[2], r->aplen[2], &r->appos[2], rr, 0.62f);
    rr = ap_tick(r->ap[3], r->aplen[3], &r->appos[3], rr, 0.55f);

    float x[8], y[8], s = 0.0f;
    for (int i = 0; i < 8; i++) {
        x[i] = r->d[i][r->pos[i]];
        r->lp[i] += r->damp * (x[i] - r->lp[i]);
        y[i] = r->lp[i] * r->g[i];
        s += y[i];
    }
    s *= 0.25f;
    for (int i = 0; i < 8; i++) {
        float f = y[i] - s + ((i & 1) ? rr : l) * 0.5f;
        r->d[i][r->pos[i]] = f;
        if (++r->pos[i] >= r->len[i]) r->pos[i] = 0;
    }
    *outl = (x[0] - x[2] + x[4] - x[6]) * 0.5f;
    *outr = (x[1] - x[3] + x[5] - x[7]) * 0.5f;
}

void au_sample_free(AuSample *s)
{
    free(s->data);
    free(s->env);
    memset(s, 0, sizeof *s);
}

void au_sample_make_env(AuSample *s)
{
    s->env_n = (s->frames + AU_ENV_BLOCK - 1) / AU_ENV_BLOCK;
    if (s->env_n < 1) s->env_n = 1;
    s->env = (float *)calloc((size_t)s->env_n, sizeof(float));
    if (!s->env) { s->env_n = 0; return; }
    for (int b = 0; b < s->env_n; b++) {
        int a = b * AU_ENV_BLOCK, e = a + AU_ENV_BLOCK;
        if (e > s->frames) e = s->frames;
        float pk = 0.0f;
        for (int i = a * s->channels; i < e * s->channels; i++) {
            float v = fabsf(s->data[i]);
            if (v > pk) pk = v;
        }
        s->env[b] = pk;
    }
}
