/* LAST AISLE - audio: real-time music engine.
   Songs (audio_songs.c) are text patterns parsed at init into step events.
   Two players allow ~1 s crossfades. Each player runs a 16th-note step
   sequencer driving subtractive/FM synth voices and pre-rendered drum kit
   samples, with per-channel sends into a tempo-synced ping-pong tape delay
   and an FDN reverb, kick side-chain ducking and a section-automated low-pass.

   Pattern language (one token = one 16th step, whitespace separated):
     .        rest (releases the held note)          -   tie (extends the previous note)
     A4 C#5 Bb3   absolute notes (C4 = MIDI 60)       1..9  chord tone index (1 = root, 2 = 3rd ...)
     ' / ,    octave up / down suffix for chord tones   x / X  full chord / chord + bass root
     A3+C4    several notes at once                  ~     glide (legato) into the note
     !  ?     accent / soft                          :N    duration suffix (N steps)
     ( ... )N repeat group N times
   Drum patterns: "k:x...x...|s:....x..." lanes, one char per step:
     . none  x hit  X accent  o ghost  1-9 velocity;  lane letters below in kit_letters. */
#include "audio_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NVOICE   36
#define NDVOICE  20
#define MBLOCK   256
#define NPLAYER  2

/* ======================================================================== */
/* parsed song data                                                          */
/* ======================================================================== */
enum { EV_NONE, EV_TIE, EV_NOTE, EV_CHORD, EV_CHORD_BASS };
#define EV_MAXN 6
typedef struct {
    uint8_t type, n, glide;
    float vel;
    int16_t val[EV_MAXN];
    uint8_t rel[EV_MAXN];
    int8_t oct[EV_MAXN];
} MEv;
typedef struct { int len; MEv *ev; } MPat;
typedef struct { int piece; int len; uint8_t *vel; } DLane;
typedef struct { int nl; DLane lane[16]; } DPat;
typedef struct { int root; int n; int8_t iv[6]; } Chord;
typedef struct { int nhalf; Chord *ch; MPat *mp[AU_MCH]; DPat *dp; int steps, start; } PSec;
typedef struct { PSec *sec; int nsec, len, loop_step; } PSong;

static PSong psongs[MUS_COUNT];
static int parse_errors;

/* pattern cache (patterns are shared between sections by pointer) */
#define MAXCACHE 512
static struct { const char *src; void *pat; int drum; } cache[MAXCACHE];
static int ncache;

/* ---------- string expansion of (...)N groups ---------- */
typedef struct { char *s; size_t n, cap; } SBuf;
static void sb_putc(SBuf *b, char c)
{
    if (b->n + 2 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 256;
        char *ns = (char *)realloc(b->s, nc);
        if (!ns) return;
        b->s = ns; b->cap = nc;
    }
    b->s[b->n++] = c; b->s[b->n] = 0;
}
static void exp_rec(const char **pp, SBuf *out)
{
    const char *p = *pp;
    while (*p && *p != ')') {
        if (*p == '(') {
            p++;
            SBuf in = {0};
            exp_rec(&p, &in);
            if (*p == ')') p++;
            int rep = 0;
            while (*p >= '0' && *p <= '9') rep = rep * 10 + (*p++ - '0');
            if (rep <= 0) rep = 1;
            for (int r = 0; r < rep; r++) { for (size_t k = 0; k < in.n; k++) sb_putc(out, in.s[k]); sb_putc(out, ' '); }
            free(in.s);
        } else sb_putc(out, *p++);
    }
    *pp = p;
}
static char *expand(const char *s, size_t len)
{
    char *tmp = (char *)malloc(len + 1);
    if (!tmp) return NULL;
    memcpy(tmp, s, len); tmp[len] = 0;
    SBuf out = {0};
    const char *p = tmp;
    while (*p) { exp_rec(&p, &out); if (*p == ')') p++; }
    free(tmp);
    if (!out.s) { out.s = (char *)calloc(1, 1); }
    return out.s;
}

/* portable strtok_r replacement (C11 has no reentrant tokenizer) */
static char *tok_next(char **sp, const char *delim)
{
    char *s = *sp;
    if (!s) return NULL;
    s += strspn(s, delim);
    if (!*s) { *sp = s; return NULL; }
    char *e = s + strcspn(s, delim);
    if (*e) { *e = 0; *sp = e + 1; } else *sp = e;
    return s;
}

static void perr(const char *what, const char *tok)
{
    parse_errors++;
    fprintf(stderr, "audio: pattern error: %s '%s'\n", what, tok);
}

/* ---------- melodic patterns ---------- */
static int parse_note(const char *s, int *val, int *rel, int *oct)
{
    *rel = 0; *oct = 0;
    if (*s >= 'A' && *s <= 'G') {
        static const int pcs[7] = { 9, 11, 0, 2, 4, 5, 7 };
        int pc = pcs[*s - 'A']; s++;
        if (*s == '#') { pc++; s++; } else if (*s == 'b') { pc--; s++; }
        if (*s < '0' || *s > '9') return 0;
        int o = 0; while (*s >= '0' && *s <= '9') o = o * 10 + (*s++ - '0');
        *val = (o + 1) * 12 + pc;
        return 1;
    }
    if (*s >= '1' && *s <= '9') {
        *val = *s - '0'; *rel = 1; s++;
        while (*s == '\'' || *s == ',') { *oct += (*s == '\'') ? 1 : -1; s++; }
        return 1;
    }
    return 0;
}

static MPat *parse_mpat(const char *src)
{
    char *s = expand(src, strlen(src));
    if (!s) return NULL;
    int cap = 64, len = 0;
    MEv *ev = (MEv *)calloc((size_t)cap, sizeof(MEv));
    char *cur = s;
    for (char *tok = tok_next(&cur, " \t\n\r"); tok; tok = tok_next(&cur, " \t\n\r")) {
        MEv e; memset(&e, 0, sizeof e);
        e.vel = 0.8f;
        int steps = 1;
        char *colon = strchr(tok, ':');
        if (colon) { steps = atoi(colon + 1); *colon = 0; if (steps < 1) steps = 1; }
        size_t tl = strlen(tok);
        while (tl > 0 && (tok[tl - 1] == '!' || tok[tl - 1] == '?')) {
            if (tok[tl - 1] == '!') e.vel = 1.0f; else e.vel = e.vel < 0.79f ? 0.35f : 0.5f;
            tok[--tl] = 0;
        }
        char *p = tok;
        if (*p == '~') { e.glide = 1; p++; }
        if (!strcmp(p, ".")) e.type = EV_NONE;
        else if (!strcmp(p, "-")) e.type = EV_TIE;
        else if (!strcmp(p, "x")) e.type = EV_CHORD;
        else if (!strcmp(p, "X")) e.type = EV_CHORD_BASS;
        else {
            e.type = EV_NOTE;
            char *cur2 = p;
            for (char *nt = tok_next(&cur2, "+"); nt && e.n < EV_MAXN; nt = tok_next(&cur2, "+")) {
                int v, r, o;
                if (!parse_note(nt, &v, &r, &o)) { perr("bad note", nt); continue; }
                e.val[e.n] = (int16_t)v; e.rel[e.n] = (uint8_t)r; e.oct[e.n] = (int8_t)o; e.n++;
            }
            if (e.n == 0) e.type = EV_NONE;
        }
        for (int k = 0; k < steps; k++) {
            if (len >= cap) { cap *= 2; MEv *ne = (MEv *)realloc(ev, sizeof(MEv) * (size_t)cap); if (!ne) break; ev = ne; }
            if (k == 0) ev[len++] = e;
            else { MEv t; memset(&t, 0, sizeof t); t.type = (e.type == EV_NONE) ? EV_NONE : EV_TIE; ev[len++] = t; }
        }
    }
    free(s);
    if (len == 0) { free(ev); return NULL; }
    MPat *mp = (MPat *)malloc(sizeof(MPat));
    if (!mp) { free(ev); return NULL; }
    mp->len = len; mp->ev = ev;
    return mp;
}

/* ---------- drum patterns ---------- */
enum { K_KICK, K_SNARE, K_CLAP, K_HAT, K_OHAT, K_TOML, K_TOMH, K_RIM, K_SHAKER, K_CRASH, K_RISER, K_REV, K_METAL, K_BOOM, K_COUNT };
static const char kit_letters[K_COUNT] = { 'k', 's', 'c', 'h', 'o', 't', 'm', 'r', 'y', 'x', 'n', 'z', 'i', 'b' };

static DPat *parse_dpat(const char *src)
{
    DPat *dp = (DPat *)calloc(1, sizeof(DPat));
    if (!dp) return NULL;
    const char *p = src;
    while (*p && dp->nl < 16) {
        while (*p == ' ' || *p == '|' || *p == '\n') p++;
        if (!*p) break;
        const char *end = strchr(p, '|');
        size_t ln = end ? (size_t)(end - p) : strlen(p);
        int piece = -1;
        for (int k = 0; k < K_COUNT; k++) if (kit_letters[k] == p[0]) piece = k;
        if (piece < 0 || p[1] != ':') { perr("bad drum lane", p); p += ln; continue; }
        char *s = expand(p + 2, ln - 2);
        if (!s) break;
        int cap = (int)strlen(s) + 1, len = 0;
        uint8_t *vel = (uint8_t *)calloc((size_t)cap, 1);
        for (char *c = s; *c; c++) {
            int v;
            switch (*c) {
            case '.': v = 0; break;
            case 'x': v = 205; break;
            case 'X': v = 255; break;
            case 'o': v = 110; break;
            case ' ': case '\t': case '\n': continue;
            default:
                if (*c >= '1' && *c <= '9') { v = (*c - '0') * 28; break; }
                perr("bad drum char", c); continue;
            }
            vel[len++] = (uint8_t)v;
        }
        free(s);
        if (len > 0) { dp->lane[dp->nl].piece = piece; dp->lane[dp->nl].len = len; dp->lane[dp->nl].vel = vel; dp->nl++; }
        else free(vel);
        p += ln;
    }
    return dp;
}

/* ---------- chords ---------- */
static int parse_chord(const char *s, Chord *c)
{
    static const int pcs[7] = { 9, 11, 0, 2, 4, 5, 7 };
    if (*s < 'A' || *s > 'G') return 0;
    int pc = pcs[*s - 'A']; s++;
    if (*s == '#') { pc++; s++; } else if (*s == 'b') { pc--; s++; }
    c->root = (pc + 12) % 12;
    static const struct { const char *q; int n; int8_t iv[6]; } qs[] = {
        { "", 3, { 0, 4, 7 } },          { "m", 3, { 0, 3, 7 } },          { "5", 2, { 0, 7 } },
        { "7", 4, { 0, 4, 7, 10 } },     { "m7", 4, { 0, 3, 7, 10 } },     { "maj7", 4, { 0, 4, 7, 11 } },
        { "m9", 5, { 0, 3, 7, 10, 14 } }, { "maj9", 5, { 0, 4, 7, 11, 14 } }, { "add9", 4, { 0, 4, 7, 14 } },
        { "madd9", 4, { 0, 3, 7, 14 } }, { "sus2", 3, { 0, 2, 7 } },       { "sus4", 3, { 0, 5, 7 } },
        { "7sus4", 4, { 0, 5, 7, 10 } }, { "6", 4, { 0, 4, 7, 9 } },       { "m6", 4, { 0, 3, 7, 9 } },
        { "dim", 3, { 0, 3, 6 } },       { "aug", 3, { 0, 4, 8 } },        { "m7b5", 4, { 0, 3, 6, 10 } },
        { "9", 5, { 0, 4, 7, 10, 14 } },
    };
    for (size_t k = 0; k < sizeof qs / sizeof qs[0]; k++)
        if (!strcmp(s, qs[k].q)) { c->n = qs[k].n; memcpy(c->iv, qs[k].iv, sizeof c->iv); return 1; }
    return 0;
}

static int parse_chords(const char *src, Chord **out)
{
    if (!src) { *out = NULL; return 0; }
    char *s = expand(src, strlen(src));
    if (!s) return 0;
    int cap = 32, n = 0;
    Chord *ch = (Chord *)calloc((size_t)cap, sizeof(Chord));
    char *cur = s;
    for (char *tok = tok_next(&cur, " \t\n"); tok; tok = tok_next(&cur, " \t\n")) {
        char *us = strchr(tok, '_');
        Chord a, b;
        if (us) *us = 0;
        if (!parse_chord(tok, &a)) { perr("bad chord", tok); continue; }
        b = a;
        if (us && !parse_chord(us + 1, &b)) { perr("bad chord", us + 1); b = a; }
        if (n + 2 > cap) { cap *= 2; Chord *nc = (Chord *)realloc(ch, sizeof(Chord) * (size_t)cap); if (!nc) break; ch = nc; }
        ch[n++] = a; ch[n++] = b;
    }
    free(s);
    *out = ch;
    return n;
}

static void *cached(const char *src, int drum)
{
    for (int i = 0; i < ncache; i++) if (cache[i].src == src) return cache[i].pat;
    void *p = drum ? (void *)parse_dpat(src) : (void *)parse_mpat(src);
    if (ncache < MAXCACHE) { cache[ncache].src = src; cache[ncache].pat = p; cache[ncache].drum = drum; ncache++; }
    return p;
}

static void parse_song(int id)
{
    const AuSong *s = au_songs[id];
    PSong *ps = &psongs[id];
    memset(ps, 0, sizeof *ps);
    if (!s) return;
    ps->nsec = s->nsec;
    ps->sec = (PSec *)calloc((size_t)s->nsec, sizeof(PSec));
    if (!ps->sec) return;
    int pos = 0;
    for (int i = 0; i < s->nsec; i++) {
        const AuSection *as = &s->sec[i];
        PSec *p = &ps->sec[i];
        p->steps = as->bars * 16;
        p->start = pos;
        pos += p->steps;
        p->nhalf = parse_chords(as->chords, &p->ch);
        if (p->nhalf == 0) {
            free(p->ch);
            p->ch = (Chord *)calloc(2, sizeof(Chord));
            p->ch[0].n = p->ch[1].n = 3;
            p->ch[0].iv[1] = p->ch[1].iv[1] = 3; p->ch[0].iv[2] = p->ch[1].iv[2] = 7;
            p->nhalf = 2;
        }
        if (as->pat[0]) p->dp = (DPat *)cached(as->pat[0], 1);
        for (int c = 1; c < AU_MCH; c++) if (as->pat[c]) p->mp[c] = (MPat *)cached(as->pat[c], 0);
    }
    ps->len = pos;
    ps->loop_step = (s->loop_to >= 0 && s->loop_to < s->nsec) ? ps->sec[s->loop_to].start : 0;
}

int au_music_validate(void)
{
    int problems = parse_errors;
    for (int id = 1; id < MUS_COUNT; id++) {
        const AuSong *s = au_songs[id];
        if (!s) { fprintf(stderr, "audio: song %d missing\n", id); problems++; continue; }
        for (int i = 0; i < s->nsec; i++) {
            PSec *p = &psongs[id].sec[i];
            if (p->nhalf % 2 != 0 || ((p->steps / 8) % p->nhalf) != 0)
                { fprintf(stderr, "audio: %s sec %d: %d chord halves vs %d bars\n", s->name, i, p->nhalf / 2, s->sec[i].bars); problems++; }
            for (int c = 1; c < AU_MCH; c++) {
                if (!p->mp[c]) continue;
                if (p->steps % p->mp[c]->len != 0)
                    { fprintf(stderr, "audio: %s sec %d ch %d: pattern len %d does not divide %d steps\n", s->name, i, c, p->mp[c]->len, p->steps); problems++; }
                if (!s->patch[c]) { fprintf(stderr, "audio: %s ch %d has notes but no patch\n", s->name, c); problems++; }
            }
            if (p->dp) for (int l = 0; l < p->dp->nl; l++)
                if (p->steps % p->dp->lane[l].len != 0)
                    { fprintf(stderr, "audio: %s sec %d drum lane '%c': len %d does not divide %d steps\n", s->name, i,
                              kit_letters[p->dp->lane[l].piece], p->dp->lane[l].len, p->steps); problems++; }
        }
    }
    return problems;
}

double au_music_song_seconds(int id)
{
    if (id <= 0 || id >= MUS_COUNT || !au_songs[id]) return 0.0;
    return (double)psongs[id].len * 15.0 / (double)au_songs[id]->bpm;
}

const char *au_music_name(int id)
{
    if (id <= 0 || id >= MUS_COUNT || !au_songs[id]) return "none";
    return au_songs[id]->name;
}

/* ======================================================================== */
/* drum kits                                                                 */
/* ======================================================================== */
typedef struct {
    float kf0, kf1, kptau, kdec, kdrive, kclick;
    float st1, st2, stdec, sndec, snhp, snlp, sngate, snrev, sndrive, sntone;
    float hdec, ohdec, hbright;
    float cldec, clrev;
    float tomf;
    float crush;
    float peak[K_COUNT];
} KitStyle;

static const float kit_pan[K_COUNT] = { 0, 0, 0.05f, 0.28f, 0.28f, -0.35f, 0.3f, -0.18f, -0.3f, -0.1f, 0, 0, 0.22f, 0 };
static const float base_peak[K_COUNT] = { 0.48f, 0.39f, 0.35f, 0.15f, 0.135f, 0.35f, 0.33f, 0.21f, 0.11f, 0.2f, 0.16f, 0.18f, 0.28f, 0.43f };

static const KitStyle kit_styles[AU_KIT_COUNT] = {
    [AU_KIT_SYNTHWAVE] = { .kf0 = 175, .kf1 = 48, .kptau = 0.026f, .kdec = 0.3f, .kdrive = 2.2f, .kclick = 0.5f,
        .st1 = 185, .st2 = 330, .stdec = 0.07f, .sndec = 0.15f, .snhp = 1100, .snlp = 9000, .sngate = 0.3f, .snrev = 1.0f, .sndrive = 1.6f, .sntone = 0.6f,
        .hdec = 0.028f, .ohdec = 0.3f, .hbright = 1.0f, .cldec = 0.12f, .clrev = 0.55f, .tomf = 105, .crush = 0,
        .peak = { [K_SNARE] = 1.0f } },
    [AU_KIT_HYPNO] = { .kf0 = 125, .kf1 = 44, .kptau = 0.04f, .kdec = 0.55f, .kdrive = 1.6f, .kclick = 0.25f,
        .st1 = 200, .st2 = 285, .stdec = 0.05f, .sndec = 0.12f, .snhp = 900, .snlp = 6000, .sngate = 0.0f, .snrev = 0.8f, .sndrive = 1.2f, .sntone = 0.5f,
        .hdec = 0.024f, .ohdec = 0.22f, .hbright = 0.75f, .cldec = 0.1f, .clrev = 0.7f, .tomf = 95, .crush = 0,
        .peak = { [K_SNARE] = 0.9f, [K_HAT] = 0.85f } },
    [AU_KIT_INDUSTRIAL] = { .kf0 = 210, .kf1 = 50, .kptau = 0.02f, .kdec = 0.34f, .kdrive = 5.0f, .kclick = 1.0f,
        .st1 = 330, .st2 = 540, .stdec = 0.05f, .sndec = 0.2f, .snhp = 1400, .snlp = 11000, .sngate = 0.22f, .snrev = 0.75f, .sndrive = 3.5f, .sntone = 0.45f,
        .hdec = 0.04f, .ohdec = 0.26f, .hbright = 1.25f, .cldec = 0.14f, .clrev = 0.5f, .tomf = 85, .crush = 7.0f,
        .peak = { [K_SNARE] = 1.0f, [K_HAT] = 1.1f } },
    [AU_KIT_SOFT] = { .kf0 = 95, .kf1 = 50, .kptau = 0.03f, .kdec = 0.32f, .kdrive = 1.1f, .kclick = 0.05f,
        .st1 = 190, .st2 = 260, .stdec = 0.04f, .sndec = 0.09f, .snhp = 700, .snlp = 4500, .sngate = 0.0f, .snrev = 0.35f, .sndrive = 1.0f, .sntone = 0.35f,
        .hdec = 0.02f, .ohdec = 0.16f, .hbright = 0.6f, .cldec = 0.08f, .clrev = 0.3f, .tomf = 100, .crush = 0,
        .peak = { [K_KICK] = 0.85f, [K_SNARE] = 0.6f, [K_HAT] = 0.7f, [K_RIM] = 0.8f } },
};

static AuSample kits[AU_KIT_COUNT][K_COUNT];
static AuSample song_fx[MUS_COUNT][2];   /* riser, reverse crash (tempo dependent) */

static float *kbuf_alloc(AuSample *s, float seconds, int ch)
{
    s->frames = (int)(seconds * AU_FRATE);
    s->channels = ch;
    s->data = (float *)calloc((size_t)s->frames * (size_t)ch, sizeof(float));
    if (!s->data) s->frames = 0;
    return s->data;
}

static void knorm(AuSample *s, float peak)
{
    if (!s->data) return;
    float pk = 1e-9f;
    int tot = s->frames * s->channels;
    for (int i = 0; i < tot; i++) { float v = fabsf(s->data[i]); if (v > pk) pk = v; }
    float g = peak / pk;
    /* DC block + gain + trim */
    float hc = au_op_coef(20.0f);
    for (int c = 0; c < s->channels; c++) {
        float lp = 0.0f;
        for (int i = 0; i < s->frames; i++) { float *x = &s->data[i * s->channels + c]; lp += hc * (*x - lp); *x = (*x - lp) * g; }
    }
    int end = s->frames;
    float thr = peak * 2e-4f;
    while (end > 64) {
        int ok = 1;
        for (int c = 0; c < s->channels; c++) if (fabsf(s->data[(end - 1) * s->channels + c]) > thr) ok = 0;
        if (!ok) break;
        end--;
    }
    s->frames = end;
    int fade = 96;
    for (int i = 0; i < fade && i < end; i++)
        for (int c = 0; c < s->channels; c++) s->data[(end - 1 - i) * s->channels + c] *= (float)i / (float)fade;
}

static void k_reverb(float *mono, int n, float *st, float wet, float decay, float gate, float damp, float size)
{
    AuReverb rv;
    if (!au_reverb_init(&rv, size, 0.05f)) return;
    au_reverb_set(&rv, decay, damp, 0.006f);
    int gs = gate > 0.0f ? (int)(gate * AU_FRATE) : n, gl = (int)(0.035f * AU_FRATE);
    for (int i = 0; i < n; i++) {
        float l, r;
        au_reverb_tick(&rv, mono[i], mono[i], &l, &r);
        float g = 1.0f;
        if (i > gs) g = i > gs + gl ? 0.0f : 1.0f - (float)(i - gs) / (float)gl;
        st[i * 2] = mono[i] + l * wet * g;
        st[i * 2 + 1] = mono[i] + r * wet * g;
    }
    au_reverb_free(&rv);
}

/* 808-style metallic oscillator bank */
static float metal_osc(float *ph, float scale)
{
    static const float mf[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
    float s = 0.0f;
    for (int k = 0; k < 6; k++) {
        ph[k] += mf[k] * scale / AU_FRATE; if (ph[k] >= 1.0f) ph[k] -= 1.0f;
        s += ph[k] < 0.5f ? 1.0f : -1.0f;
    }
    return s / 6.0f;
}

static void render_kit(int k)
{
    const KitStyle *ks = &kit_styles[k];
    AuSample *K = kits[k];
    AuRng r; au_rng_seed(&r, 777u + (uint32_t)k * 131u);
    float peak[K_COUNT];
    for (int i = 0; i < K_COUNT; i++) peak[i] = base_peak[i] * (ks->peak[i] > 0.0f ? ks->peak[i] : 1.0f);

    /* kick */
    {
        float *o = kbuf_alloc(&K[K_KICK], ks->kdec * 6.0f + 0.05f, 1);
        float ph = 0.0f, dn = 1.0f / au_sat(ks->kdrive);
        AuSvf hp = {0}; au_svf_set(&hp, 1500, 0.7f);
        for (int i = 0; o && i < K[K_KICK].frames; i++) {
            float t = (float)i / AU_FRATE;
            float f = ks->kf1 + (ks->kf0 - ks->kf1) * expf(-t / ks->kptau);
            ph += f / AU_FRATE; if (ph >= 1.0f) ph -= 1.0f;
            float e = (t < 0.0015f ? t / 0.0015f : 1.0f) * expf(-t / (ks->kdec * 0.45f));
            float x = au_sin(ph) * e;
            x += au_svf_hp(&hp, au_rnd2(&r)) * expf(-t / 0.0025f) * ks->kclick;
            o[i] = au_sat(x * ks->kdrive) * dn;
        }
        if (ks->crush > 0.0f && o) for (int i = 0; i < K[K_KICK].frames; i++) o[i] = roundf(o[i] * 24.0f) / 24.0f * 0.5f + o[i] * 0.5f;
    }
    /* snare (+ gated reverb) */
    {
        int n = (int)(0.75f * AU_FRATE);
        float *mono = (float *)calloc((size_t)n, sizeof(float));
        float *o = kbuf_alloc(&K[K_SNARE], 0.75f, 2);
        AuSvf hp = {0}, lp = {0}; au_svf_set(&hp, ks->snhp, 0.7f); au_svf_set(&lp, ks->snlp, 0.7f);
        float p1 = 0.0f, p2 = 0.0f, dn = 1.0f / au_sat(ks->sndrive);
        for (int i = 0; mono && i < n; i++) {
            float t = (float)i / AU_FRATE;
            float pd = 1.0f + 0.25f * expf(-t / 0.01f);
            p1 += ks->st1 * pd / AU_FRATE; if (p1 >= 1.0f) p1 -= 1.0f;
            p2 += ks->st2 * pd / AU_FRATE; if (p2 >= 1.0f) p2 -= 1.0f;
            float tone = (au_sin(p1) + 0.6f * au_sin(p2)) * expf(-t / ks->stdec) * ks->sntone;
            float nz = au_svf_lp(&lp, au_svf_hp(&hp, au_rnd2(&r))) * expf(-t / ks->sndec) * 1.4f;
            float x = (tone + nz) * (t < 0.0008f ? t / 0.0008f : 1.0f);
            mono[i] = au_sat(x * ks->sndrive) * dn;
        }
        if (mono && o) k_reverb(mono, n, o, ks->snrev, ks->sngate > 0.0f ? 2.2f : 1.4f, ks->sngate, 7000.0f, 1.3f);
        if (ks->crush > 0.0f && o) {
            float hl = 0, hr = 0;
            for (int i = 0; i < K[K_SNARE].frames; i++) {
                if (i % 3 == 0) { hl = roundf(o[i * 2] * 20.0f) / 20.0f; hr = roundf(o[i * 2 + 1] * 20.0f) / 20.0f; }
                o[i * 2] = 0.6f * o[i * 2] + 0.4f * hl; o[i * 2 + 1] = 0.6f * o[i * 2 + 1] + 0.4f * hr;
            }
        }
        free(mono);
    }
    /* clap */
    {
        int n = (int)(0.6f * AU_FRATE);
        float *mono = (float *)calloc((size_t)n, sizeof(float));
        float *o = kbuf_alloc(&K[K_CLAP], 0.6f, 2);
        AuSvf bp = {0}; au_svf_set(&bp, 1150, 1.3f);
        const float bt[4] = { 0.0f, 0.009f, 0.018f, 0.027f };
        for (int i = 0; mono && i < n; i++) {
            float t = (float)i / AU_FRATE, e = 0.0f;
            for (int b = 0; b < 4; b++) if (t >= bt[b]) e += expf(-(t - bt[b]) / (b == 3 ? ks->cldec : 0.0045f)) * (b == 3 ? 1.0f : 0.8f);
            mono[i] = au_svf_bp(&bp, au_rnd2(&r)) * e * 2.0f;
        }
        if (mono && o) k_reverb(mono, n, o, ks->clrev, 1.0f, 0.0f, 6000.0f, 1.0f);
        free(mono);
    }
    /* hats */
    for (int h = 0; h < 2; h++) {
        int id = h ? K_OHAT : K_HAT;
        float dec = h ? ks->ohdec : ks->hdec;
        float *o = kbuf_alloc(&K[id], dec * 6.0f + 0.02f, 1);
        float ph[6] = { 0 };
        AuSvf bp = {0}, hp = {0};
        au_svf_set(&bp, 9500.0f * ks->hbright, 1.0f); au_svf_set(&hp, 7000.0f * ks->hbright, 0.7f);
        for (int i = 0; o && i < K[id].frames; i++) {
            float t = (float)i / AU_FRATE;
            float x = metal_osc(ph, 1.0f + 0.4f * ks->hbright) * 0.8f + au_rnd2(&r) * 0.5f;
            x = au_svf_hp(&hp, au_svf_bp(&bp, x));
            o[i] = x * (t < 0.0005f ? t / 0.0005f : 1.0f) * expf(-t / dec);
        }
    }
    /* toms */
    for (int h = 0; h < 2; h++) {
        int id = h ? K_TOMH : K_TOML;
        float f0 = ks->tomf * (h ? 1.5f : 1.0f);
        int n = (int)(0.7f * AU_FRATE);
        float *mono = (float *)calloc((size_t)n, sizeof(float));
        float *o = kbuf_alloc(&K[id], 0.7f, 2);
        float ph = 0.0f;
        AuSvf lp = {0}; au_svf_set(&lp, 3000, 0.7f);
        for (int i = 0; mono && i < n; i++) {
            float t = (float)i / AU_FRATE;
            float f = f0 * (0.75f + 0.5f * expf(-t / 0.05f));
            ph += f / AU_FRATE; if (ph >= 1.0f) ph -= 1.0f;
            float x = au_sin(ph) * expf(-t / 0.18f) + au_svf_lp(&lp, au_rnd2(&r)) * expf(-t / 0.02f) * 0.5f;
            mono[i] = au_sat(x * 1.5f);
        }
        if (mono && o) k_reverb(mono, n, o, 0.45f, 1.6f, ks->sngate > 0.0f ? 0.28f : 0.0f, 6000.0f, 1.2f);
        free(mono);
    }
    /* rim */
    {
        float *o = kbuf_alloc(&K[K_RIM], 0.08f, 1);
        float p1 = 0.0f, p2 = 0.0f;
        for (int i = 0; o && i < K[K_RIM].frames; i++) {
            float t = (float)i / AU_FRATE;
            p1 += 1720.0f / AU_FRATE; if (p1 >= 1.0f) p1 -= 1.0f;
            p2 += 505.0f / AU_FRATE; if (p2 >= 1.0f) p2 -= 1.0f;
            o[i] = (au_sin(p1) * 0.6f + au_sin(p2) * 0.8f) * expf(-t / 0.012f) + au_rnd2(&r) * expf(-t / 0.002f) * 0.6f;
        }
    }
    /* shaker */
    {
        float *o = kbuf_alloc(&K[K_SHAKER], 0.12f, 1);
        AuSvf hp = {0}; au_svf_set(&hp, 5500, 0.8f);
        for (int i = 0; o && i < K[K_SHAKER].frames; i++) {
            float t = (float)i / AU_FRATE;
            float e = t < 0.012f ? t / 0.012f : expf(-(t - 0.012f) / 0.025f);
            o[i] = au_svf_hp(&hp, au_rnd2(&r)) * e;
        }
    }
    /* crash */
    {
        float *o = kbuf_alloc(&K[K_CRASH], 2.4f, 2);
        float phl[6] = { 0 }, phr[6] = { 0.3f, 0.1f, 0.7f, 0.2f, 0.5f, 0.9f };
        AuSvf hl = {0}, hr = {0};
        au_svf_set(&hl, 4200, 0.7f); au_svf_copycoef(&hr, &hl);
        for (int i = 0; o && i < K[K_CRASH].frames; i++) {
            float t = (float)i / AU_FRATE;
            float e = (t < 0.002f ? t / 0.002f : 1.0f) * (0.6f * expf(-t / 0.5f) + 0.4f * expf(-t / 0.08f));
            float xl = metal_osc(phl, 2.1f) * 0.5f + au_rnd2(&r);
            float xr = metal_osc(phr, 2.17f) * 0.5f + au_rnd2(&r);
            o[i * 2] = au_svf_hp(&hl, xl) * e;
            o[i * 2 + 1] = au_svf_hp(&hr, xr) * e;
        }
    }
    /* industrial metal hit */
    {
        float *o = kbuf_alloc(&K[K_METAL], 0.6f, 1);
        static const float mf[5] = { 410, 1130, 1870, 2730, 3920 }, md[5] = { 0.12f, 0.08f, 0.06f, 0.045f, 0.03f };
        float ph[5] = { 0 };
        for (int i = 0; o && i < K[K_METAL].frames; i++) {
            float t = (float)i / AU_FRATE, x = 0.0f;
            for (int m = 0; m < 5; m++) { ph[m] += mf[m] / AU_FRATE; if (ph[m] >= 1.0f) ph[m] -= 1.0f; x += au_sin(ph[m]) * expf(-t / md[m]); }
            x += au_rnd2(&r) * expf(-t / 0.01f) * 1.2f;
            o[i] = au_sat(x * 1.8f);
        }
    }
    /* boom */
    {
        float *o = kbuf_alloc(&K[K_BOOM], 2.0f, 1);
        float ph = 0.0f;
        for (int i = 0; o && i < K[K_BOOM].frames; i++) {
            float t = (float)i / AU_FRATE;
            float f = 30.0f + 40.0f * expf(-t / 0.12f);
            ph += f / AU_FRATE; if (ph >= 1.0f) ph -= 1.0f;
            o[i] = au_sat(au_sin(ph) * 2.0f * (t < 0.003f ? t / 0.003f : 1.0f) * expf(-t / 0.5f));
        }
    }
    for (int i = 0; i < K_COUNT; i++) {
        if (i == K_RISER || i == K_REV) continue;
        knorm(&K[i], peak[i]);
        au_sample_make_env(&K[i]);
    }
}

const AuSample *au_music_kit_sample(int kit, int piece, const char **name)
{
    static const char *names[K_COUNT] = { "kick", "snare", "clap", "hat", "ohat", "tom_lo", "tom_hi", "rim", "shaker", "crash", "riser", "revcrash", "metal", "boom" };
    if (kit < 0 || kit >= AU_KIT_COUNT || piece < 0 || piece >= K_COUNT) return NULL;
    if (name) *name = names[piece];
    if (piece == K_RISER || piece == K_REV) return &song_fx[MUS_LEVEL_A][piece == K_RISER ? 0 : 1];
    return &kits[kit][piece];
}

static void render_song_fx(int id)
{
    const AuSong *s = au_songs[id];
    if (!s) return;
    float bar = 240.0f / s->bpm;
    AuRng r; au_rng_seed(&r, 4242u + (uint32_t)id);
    /* riser: one bar of rising filtered noise + whine, ends at the next downbeat */
    {
        AuSample *S = &song_fx[id][0];
        float *o = kbuf_alloc(S, bar, 2);
        AuSvf bl = {0}, br = {0};
        float ph = 0.0f;
        for (int i = 0; o && i < S->frames; i++) {
            float u = (float)i / (float)S->frames;
            if ((i & 15) == 0) { au_svf_set(&bl, 250.0f * powf(36.0f, u), 2.2f); au_svf_copycoef(&br, &bl); }
            float f = 220.0f * powf(8.0f, u * u);
            ph += f / AU_FRATE; if (ph >= 1.0f) ph -= 1.0f;
            float e = u * u * (u > 0.97f ? (1.0f - u) / 0.03f : 1.0f);
            float tone = au_saw(ph, f / AU_FRATE) * 0.08f;
            o[i * 2] = (au_svf_bp(&bl, au_rnd2(&r)) * 1.6f + tone) * e;
            o[i * 2 + 1] = (au_svf_bp(&br, au_rnd2(&r)) * 1.6f + tone) * e;
        }
        knorm(S, base_peak[K_RISER]);
        au_sample_make_env(S);
    }
    /* reverse crash over two beats */
    {
        AuSample *S = &song_fx[id][1];
        float *o = kbuf_alloc(S, bar * 0.5f, 2);
        AuSvf hl = {0}, hr = {0};
        au_svf_set(&hl, 3500, 0.7f); au_svf_copycoef(&hr, &hl);
        int n = S->frames;
        for (int i = 0; o && i < n; i++) {
            float t = (float)(n - i) / AU_FRATE;
            float e = expf(-t / 0.35f) * ((n - i) < 64 ? (float)(n - i) / 64.0f : 1.0f);
            o[i * 2] = au_svf_hp(&hl, au_rnd2(&r)) * e;
            o[i * 2 + 1] = au_svf_hp(&hr, au_rnd2(&r)) * e;
        }
        knorm(S, base_peak[K_REV]);
        au_sample_make_env(S);
    }
}

/* ======================================================================== */
/* synth voices                                                              */
/* ======================================================================== */
typedef struct {
    const AuPatch *p;
    uint8_t active, held, ch, stereo;
    float note, target, vel, glide_tau;
    float ph[7], mul[7], upl[7], upr[7];
    float ph2, phs, phm;
    float aenv, fenv;
    uint8_t astage, fstage;
    float t;
    float wow, wow_tgt, wow_t;
    AuSvf f1l, f2l, f1r, f2r;
    uint32_t serial;
    int ctl;
    float inc1, inc2, incs, incm, fmi, pw, ampv;
    float a_att, a_dec, a_rel, f_att, f_dec, f_rel;
    AuRng rng;
} SVoice;

typedef struct {
    const AuSample *s;
    double pos;
    float vol, gl, gr, rmul, fade, fstep;
    int piece;
    uint8_t active;
} DVoice;

typedef struct {
    int id;
    const AuSong *s;
    const PSong *ps;
    int active;
    float fade, fade_tgt, fade_speed;
    double pos;
    int next_step;
    SVoice v[NVOICE];
    uint32_t serial;
    SVoice *mono[AU_MCH];
    DVoice dv[NDVOICE];
    float duck, duck_s;
    int duck_trig[16], nduck;
    AuReverb rev;
    float *dbuf; int dlen, dpos; float dlp[2], dhp[2], wph;
    AuSvf lpf[2][2];
    float pan_l[AU_MCH], pan_r[AU_MCH];
    int cur_sec;
    AuRng rng;
} Player;

static Player players[NPLAYER];
static int music_ready;

/* optional metering for the offline tool */
static int meter_on;
static double meter_sum[AU_MCH];
static float meter_peak[AU_MCH];
static long meter_n, meter_act[AU_MCH];

static int trace_song, trace_from, trace_to, trace_maxv;
int au_music_trace(int song, int from_step, int to_step)
{
    int r = trace_maxv;
    trace_song = song; trace_from = from_step; trace_to = to_step; trace_maxv = 0;
    return r;
}
static const char *nname(float m)
{
    static char buf[8][8]; static int k;
    static const char *nn[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    int mi = (int)lrintf(m);
    k = (k + 1) & 7;
    snprintf(buf[k], sizeof buf[k], "%s%d", nn[((mi % 12) + 12) % 12], mi / 12 - 1);
    return buf[k];
}

void au_music_meter(int enable, float rms_db[AU_MCH], float peak_db[AU_MCH])
{
    if (rms_db && peak_db) {
        for (int c = 0; c < AU_MCH; c++) {
            double r = meter_act[c] ? sqrt(meter_sum[c] / (double)meter_act[c]) : 0.0;   /* RMS while playing */
            rms_db[c] = r > 1e-9 ? (float)(20.0 * log10(r)) : -180.0f;
            peak_db[c] = meter_peak[c] > 1e-9f ? 20.0f * log10f(meter_peak[c]) : -180.0f;
        }
    }
    meter_on = enable;
    memset(meter_sum, 0, sizeof meter_sum);
    memset(meter_peak, 0, sizeof meter_peak);
    memset(meter_act, 0, sizeof meter_act);
    meter_n = 0;
}

static inline float reso_q(float r) { return 0.6f * powf(25.0f, au_clampf(r, 0.0f, 1.0f)); }

static void voice_control(SVoice *v, float rate)
{
    const AuPatch *p = v->p;
    const float dt = 16.0f / AU_FRATE * rate;
    v->t += dt;
    if (v->note != v->target) {
        if (v->glide_tau > 0.0f) {
            v->note += (v->target - v->note) * (1.0f - expf(-dt / v->glide_tau));
            if (fabsf(v->target - v->note) < 0.001f) v->note = v->target;
        } else v->note = v->target;
    }
    float m = v->note;
    if (p->vib > 0.0f) {
        float vd = au_clampf((v->t - p->vib_delay) / 0.4f, 0.0f, 1.0f);
        m += p->vib * vd * au_sin(v->t * p->vib_rate);
    }
    if (p->wow > 0.0f) {
        v->wow_t -= dt;
        if (v->wow_t <= 0.0f) { v->wow_t = 0.25f + 0.4f * au_rnd(&v->rng); v->wow_tgt = au_rnd2(&v->rng); }
        v->wow += (v->wow_tgt - v->wow) * (1.0f - expf(-dt / 0.35f));
        m += p->wow * 0.01f * v->wow;
    }
    if (p->penv != 0.0f) m += p->penv * expf(-v->t / (p->pdecay > 0.0f ? p->pdecay : 0.05f));
    float f = au_mtof(m) * rate;
    v->inc1 = f / AU_FRATE;
    v->inc2 = v->inc1 * exp2f(p->semi2 / 12.0f + p->det2 / 1200.0f);
    v->incs = v->inc1 * 0.5f;
    v->incm = v->inc1 * p->fm_ratio;
    if (p->fm_index > 0.0f) v->fmi = p->fm_sus + (p->fm_index - p->fm_sus) * expf(-v->t / (p->fm_decay > 0.0f ? p->fm_decay : 0.2f));
    v->pw = p->pw > 0.0f ? p->pw : 0.5f;
    if (p->pwm > 0.0f) v->pw = au_clampf(v->pw + p->pwm * 0.5f * au_sin(v->t * p->pwm_rate), 0.06f, 0.94f);
    float oct = p->fenv * v->fenv + p->keytrk * (v->note - 60.0f) / 12.0f + p->vel_filt * (v->vel - 0.8f);
    if (p->flfo > 0.0f) oct += p->flfo * au_sin(v->t * p->flfo_rate + 0.25f);
    float fc = p->cutoff * exp2f(oct) * rate;
    au_svf_set(&v->f1l, fc, reso_q(p->reso));
    au_svf_copycoef(&v->f2l, &v->f1l); au_svf_copycoef(&v->f1r, &v->f1l); au_svf_copycoef(&v->f2r, &v->f1l);
    v->a_att = rate / ((p->aa > 0.0005f ? p->aa : 0.0005f) * AU_FRATE);
    v->a_dec = 1.0f - expf(-5.0f * rate / ((p->ad > 0.001f ? p->ad : 0.001f) * AU_FRATE));
    v->a_rel = 1.0f - expf(-5.0f * rate / ((p->ar > 0.002f ? p->ar : 0.002f) * AU_FRATE));
    v->f_att = rate / ((p->fa > 0.0005f ? p->fa : 0.0005f) * AU_FRATE);
    v->f_dec = 1.0f - expf(-5.0f * rate / ((p->fd > 0.001f ? p->fd : 0.001f) * AU_FRATE));
    v->f_rel = 1.0f - expf(-5.0f * rate / ((p->fr > 0.002f ? p->fr : 0.002f) * AU_FRATE));
}

static inline float osc(int w, float ph, float dt, float pw, AuRng *r)
{
    switch (w) {
    case AW_SAW: return au_saw(ph, dt);
    case AW_SQUARE: return au_pulse(ph, dt, 0.5f);
    case AW_PULSE: return au_pulse(ph, dt, pw);
    case AW_TRI: return au_tri(ph);
    case AW_SINE: return au_sin(ph);
    case AW_NOISE: return au_rnd2(r);
    default: return 0.0f;
    }
}

static void voice_render(SVoice *v, float *L, float *R, int n, float rate)
{
    const AuPatch *p = v->p;
    const int uni = p->uni < 1 ? 1 : (p->uni > 7 ? 7 : p->uni);
    const float un = 1.0f / sqrtf((float)uni);
    const float dn = p->drive > 0.0f ? 1.0f / au_softsat(p->drive) : 1.0f;
    const float gain = p->gain * v->ampv;
    for (int i = 0; i < n; i++) {
        if (v->ctl <= 0) { voice_control(v, rate); v->ctl = 16; }
        v->ctl--;
        float sl, sr;
        if (p->fm_index > 0.0f) {
            v->phm += v->incm; v->phm -= floorf(v->phm);
            v->ph[0] += v->inc1; if (v->ph[0] >= 1.0f) v->ph[0] -= 1.0f;
            float x = au_sin(v->ph[0] + v->fmi * au_sin(v->phm) * (1.0f / AU_TAU));
            if (p->mix2 > 0.0f) {
                v->ph2 += v->inc2; if (v->ph2 >= 1.0f) v->ph2 -= 1.0f;
                x += osc(p->w2, v->ph2, v->inc2, v->pw, &v->rng) * p->mix2;
            }
            sl = sr = x;
        } else {
            sl = sr = 0.0f;
            for (int j = 0; j < uni; j++) {
                float d = v->inc1 * v->mul[j];
                v->ph[j] += d; if (v->ph[j] >= 1.0f) v->ph[j] -= 1.0f;
                float x = osc(p->w1, v->ph[j], d, v->pw, &v->rng);
                sl += x * v->upl[j]; sr += x * v->upr[j];
            }
            sl *= un; sr *= un;
            float c = 0.0f;
            if (p->mix2 > 0.0f) {
                v->ph2 += v->inc2; if (v->ph2 >= 1.0f) v->ph2 -= 1.0f;
                c += osc(p->w2, v->ph2, v->inc2, v->pw, &v->rng) * p->mix2;
            }
            if (p->sub > 0.0f) { v->phs += v->incs; if (v->phs >= 1.0f) v->phs -= 1.0f; c += au_sin(v->phs) * p->sub; }
            if (p->noise > 0.0f) c += au_rnd2(&v->rng) * p->noise;
            sl += c; sr += c;
        }
        /* filter */
        if (v->stereo) {
            sl = au_svf_lp(&v->f1l, sl); sr = au_svf_lp(&v->f1r, sr);
            if (p->poles4) { sl = au_svf_lp(&v->f2l, sl); sr = au_svf_lp(&v->f2r, sr); }
        } else {
            sl = au_svf_lp(&v->f1l, sl);
            if (p->poles4) sl = au_svf_lp(&v->f2l, sl);
        }
        if (p->drive > 0.0f) { sl = au_softsat(sl * p->drive) * dn; if (v->stereo) sr = au_softsat(sr * p->drive) * dn; }
        if (!v->stereo) sr = sl;
        /* envelopes */
        switch (v->fstage) {
        case 0: v->fenv += v->f_att; if (v->fenv >= 1.0f) { v->fenv = 1.0f; v->fstage = 1; } break;
        case 1: v->fenv += (p->fs - v->fenv) * v->f_dec; break;
        default: v->fenv -= v->fenv * v->f_rel; break;
        }
        switch (v->astage) {
        case 0: v->aenv += v->a_att; if (v->aenv >= 1.0f) { v->aenv = 1.0f; v->astage = 1; } break;
        case 1: v->aenv += (p->as - v->aenv) * v->a_dec; break;
        default:
            v->aenv -= v->aenv * v->a_rel;
            if (v->aenv < 2e-4f) { v->active = 0; return; }
            break;
        }
        float a = v->aenv * gain;
        L[i] += sl * a; R[i] += sr * a;
    }
}

static void voice_release(SVoice *v)
{
    if (!v->active || !v->held) return;
    v->held = 0;
    v->astage = 2; v->fstage = 2;
}

static SVoice *voice_alloc(Player *P)
{
    SVoice *best = NULL;
    for (int i = 0; i < NVOICE; i++) if (!P->v[i].active) return &P->v[i];
    /* steal: oldest released voice, else oldest held poly voice */
    for (int i = 0; i < NVOICE; i++) {
        SVoice *v = &P->v[i];
        if (v->held) continue;
        if (!best || v->serial < best->serial) best = v;
    }
    if (best) return best;
    for (int i = 0; i < NVOICE; i++) {
        SVoice *v = &P->v[i];
        if (P->mono[v->ch] == v) continue;
        if (!best || v->serial < best->serial) best = v;
    }
    return best ? best : &P->v[0];
}

static void voice_start(Player *P, SVoice *v, int ch, const AuPatch *p, float note, float vel, int retrig_keep)
{
    if (retrig_keep && v->active && v->p == p) {
        /* mono retrigger: restart envelopes from the current level, keep oscillator
           phases and filter state running so there is no discontinuity */
        v->held = 1;
        v->note = v->target = note; v->vel = vel;
        v->glide_tau = 0.0f;
        v->ampv = 0.3f + 0.7f * vel;
        v->astage = 0; v->fstage = 0;
        v->fenv = 0.0f;
        v->t = 0.0f;
        v->serial = ++P->serial;
        v->ctl = 0;
        return;
    }
    AuRng r = v->rng;
    memset(v, 0, sizeof *v);
    v->rng = r;
    if (!v->rng.s) au_rng_seed(&v->rng, 0xABCDu + P->serial * 2654435761u);
    v->p = p; v->active = 1; v->held = 1; v->ch = (uint8_t)ch;
    v->note = note; v->target = note; v->vel = vel;
    v->ampv = 0.3f + 0.7f * vel;
    v->serial = ++P->serial;
    int uni = p->uni < 1 ? 1 : (p->uni > 7 ? 7 : p->uni);
    v->stereo = (uni > 1 && p->uni_width > 0.0f) ? 1 : 0;
    for (int j = 0; j < uni; j++) {
        float d = uni > 1 ? ((float)j / (float)(uni - 1) - 0.5f) * p->uni_det : 0.0f;
        v->mul[j] = exp2f(d / 1200.0f);
        v->ph[j] = uni > 1 ? au_rnd(&v->rng) : 0.0f;
        float pan = uni > 1 ? ((float)j / (float)(uni - 1) * 2.0f - 1.0f) * p->uni_width : 0.0f;
        float th = (pan + 1.0f) * AU_PI * 0.25f;
        v->upl[j] = cosf(th) * 1.41421356f; v->upr[j] = sinf(th) * 1.41421356f;
    }
    v->ph2 = au_rnd(&v->rng);
    v->wow = au_rnd2(&v->rng); v->wow_tgt = v->wow;
    v->ctl = 0;
}

static void chan_release(Player *P, int ch)
{
    for (int i = 0; i < NVOICE; i++) if (P->v[i].active && P->v[i].ch == ch) voice_release(&P->v[i]);
    P->mono[ch] = NULL;
}

static void note_on(Player *P, int ch, float note, float vel, int glide)
{
    const AuPatch *p = P->s->patch[ch];
    if (!p) return;
    if (p->mono) {
        SVoice *v = P->mono[ch];
        if (v && v->active && v->held && v->ch == ch && v->p == p) {
            if (glide && p->glide > 0.0f) {
                v->target = note; v->glide_tau = p->glide * 0.4f; v->vel = vel;
                return;
            }
            voice_start(P, v, ch, p, note, vel, 1);
            return;
        }
        if (v) voice_release(v);
        v = voice_alloc(P);
        voice_start(P, v, ch, p, note, vel, 0);
        P->mono[ch] = v;
    } else {
        SVoice *v = voice_alloc(P);
        voice_start(P, v, ch, p, note, vel, 0);
    }
}

static void drum_hit(Player *P, int piece, float vel, int off)
{
    const AuSample *s;
    if (piece == K_RISER) s = &song_fx[P->id][0];
    else if (piece == K_REV) s = &song_fx[P->id][1];
    else s = &kits[P->s->kit][piece];
    if (!s->data) return;
    if (piece == K_HAT || piece == K_OHAT) {    /* hats choke each other */
        for (int i = 0; i < NDVOICE; i++)
            if (P->dv[i].active && P->dv[i].piece == K_OHAT && P->dv[i].fstep == 0.0f) P->dv[i].fstep = 1.0f / (0.012f * AU_FRATE);
    }
    if (piece == K_KICK && P->nduck < 16) P->duck_trig[P->nduck++] = off;
    DVoice *d = NULL;
    for (int i = 0; i < NDVOICE; i++) if (!P->dv[i].active) { d = &P->dv[i]; break; }
    if (!d) { /* steal the one furthest through its sample */
        double best = -1.0;
        for (int i = 0; i < NDVOICE; i++) {
            double prog = P->dv[i].pos / (double)P->dv[i].s->frames;
            if (prog > best) { best = prog; d = &P->dv[i]; }
        }
    }
    memset(d, 0, sizeof *d);
    d->s = s; d->piece = piece; d->active = 1; d->fade = 1.0f;
    d->vol = vel * vel * 0.85f + 0.15f * vel;
    d->rmul = 1.0f;
    if (piece == K_HAT || piece == K_OHAT || piece == K_SHAKER || piece == K_RIM) {
        /* humanize: tiny level / pitch variation so repeated hats don't machine-gun */
        d->vol *= 0.88f + 0.2f * au_rnd(&P->rng);
        d->rmul = 1.0f + 0.012f * au_rnd2(&P->rng);
    }
    float th = (kit_pan[piece] + 1.0f) * AU_PI * 0.25f;
    d->gl = fminf(1.0f, cosf(th) * 1.41421356f); d->gr = fminf(1.0f, sinf(th) * 1.41421356f);
}

static void drum_render(DVoice *d, float *L, float *R, int n, float rate)
{
    const AuSample *s = d->s;
    const float *x = s->data;
    const int last = s->frames - 1;
    for (int i = 0; i < n; i++) {
        int k = (int)d->pos;
        if (k >= last) { d->active = 0; return; }
        float f = (float)(d->pos - k);
        float l, r;
        if (s->channels == 1) { l = r = x[k] + (x[k + 1] - x[k]) * f; }
        else {
            l = x[k * 2] + (x[k * 2 + 2] - x[k * 2]) * f;
            r = x[k * 2 + 1] + (x[k * 2 + 3] - x[k * 2 + 1]) * f;
        }
        float g = d->vol * d->fade;
        L[i] += l * g * d->gl; R[i] += r * g * d->gr;
        d->pos += rate * d->rmul;
        if (d->fstep > 0.0f) { d->fade -= d->fstep; if (d->fade <= 0.0f) { d->active = 0; return; } }
    }
}

/* ======================================================================== */
/* sequencer                                                                 */
/* ======================================================================== */
static int find_sec(const PSong *ps, int step)
{
    for (int i = ps->nsec - 1; i >= 0; i--) if (step >= ps->sec[i].start) return i;
    return 0;
}

static float place(int pc, int center)
{
    int lo = center - 6;
    int r = ((pc - lo) % 12 + 12) % 12;
    return (float)(lo + r);
}

static void fire_step(Player *P, int gs, int off)
{
    const PSong *ps = P->ps;
    const AuSong *s = P->s;
    int si = find_sec(ps, gs);
    const PSec *S = &ps->sec[si];
    const AuSection *as = &s->sec[si];
    P->cur_sec = si;
    int local = gs - S->start;
    const Chord *chd = &S->ch[(local / 8) % S->nhalf];
    int xp = as->xpose;

    if (S->dp) {
        for (int l = 0; l < S->dp->nl; l++) {
            const DLane *ln = &S->dp->lane[l];
            int v = ln->vel[local % ln->len];
            if (v) drum_hit(P, ln->piece, (float)v / 255.0f, off);
        }
    }
    for (int c = 1; c < AU_MCH; c++) {
        const MPat *mp = S->mp[c];
        if (!mp) { chan_release(P, c); continue; }
        const MEv *e = &mp->ev[local % mp->len];
        if (e->type == EV_TIE) continue;
        if (e->type == EV_NONE) { chan_release(P, c); continue; }
        const AuPatch *pt = s->patch[c];
        if (!pt) continue;
        int center = s->center[c] ? s->center[c] : 60;
        int cxp = s->xp[c];
        float notes[12]; int nn = 0;
        int root_pc = ((chd->root + xp) % 12 + 12) % 12;
        if (e->type == EV_NOTE) {
            for (int k = 0; k < e->n && nn < 12; k++) {
                if (e->rel[k]) {
                    int idx = e->val[k] - 1;
                    float root = place(root_pc, center);
                    notes[nn++] = root + (float)chd->iv[idx % chd->n] + 12.0f * (float)(idx / chd->n + e->oct[k]) + (float)cxp;
                } else notes[nn++] = (float)(e->val[k] + xp + cxp);
            }
        } else {
            for (int k = 0; k < chd->n && nn < 11; k++) notes[nn++] = place((root_pc + chd->iv[k]) % 12, center) + (float)cxp;
            if (e->type == EV_CHORD_BASS) notes[nn++] = place(root_pc, center) - 12.0f + (float)cxp;
        }
        if (trace_song == P->id && gs >= trace_from && gs < trace_to) {
            /* held length (for the harmony check) */
            int len = 1;
            while (len < 64 && mp->ev[(local + len) % mp->len].type == EV_TIE) len++;
            int nct = 0;
            for (int k = 0; k < (pt->mono ? 1 : nn); k++) {
                int pc = ((int)lrintf(notes[k]) % 12 + 12) % 12, in = 0;
                for (int q = 0; q < chd->n; q++) if ((root_pc + chd->iv[q]) % 12 == pc) in = 1;
                if (!in) nct++;
            }
            printf("      %4d bar%3d.%-2d ch%d len%2d chord %s+%d:", gs, gs / 16 + 1, gs % 16, c, len, nname((float)(root_pc + 60)), chd->n);
            for (int k = 0; k < (pt->mono ? 1 : nn); k++) printf(" %s", nname(notes[k] + 0.0f));
            printf("%s%s\n", e->glide ? " (glide)" : "", nct ? "  [non-chord]" : "");
        }
        if (pt->mono) {
            note_on(P, c, notes[0], e->vel, e->glide);
        } else {
            if (pt->aa >= 0.1f && e->type != EV_NOTE) {
                /* slow pads: if exactly this chord is already held, let it sustain */
                int held = 0, same = 1;
                for (int i = 0; i < NVOICE; i++) {
                    SVoice *v = &P->v[i];
                    if (!v->active || !v->held || v->ch != c) continue;
                    held++;
                    int found = 0;
                    for (int k = 0; k < nn; k++) if (fabsf(notes[k] - v->target) < 0.01f) found = 1;
                    if (!found) same = 0;
                }
                if (held == nn && same) continue;
            }
            chan_release(P, c);
            for (int k = 0; k < nn; k++) note_on(P, c, notes[k], e->vel, 0);
        }
    }
}

/* ======================================================================== */
/* player                                                                    */
/* ======================================================================== */
#define DELAY_MAX (AU_RATE * 3 / 2)

static void player_start(Player *P, int id)
{
    AuReverb rv = P->rev;
    float *db = P->dbuf;
    memset(P, 0, sizeof *P);
    P->rev = rv; P->dbuf = db;
    P->id = id;
    au_rng_seed(&P->rng, 0x600DF00Du + (uint32_t)id * 7u);
    P->s = au_songs[id];
    P->ps = &psongs[id];
    P->active = 1;
    P->pos = 0.0;
    P->next_step = 0;
    au_reverb_clear(&P->rev);
    au_reverb_set(&P->rev, P->s->rev_decay > 0.0f ? P->s->rev_decay : 2.5f, P->s->rev_damp > 0.0f ? P->s->rev_damp : 5000.0f, 0.02f);
    memset(P->dbuf, 0, sizeof(float) * DELAY_MAX * 2);
    P->dlen = (int)(P->s->delay_beats * 60.0f / P->s->bpm * AU_FRATE);
    if (P->dlen < 16) P->dlen = 16;
    if (P->dlen > DELAY_MAX - 400) P->dlen = DELAY_MAX - 400;
    for (int c = 0; c < AU_MCH; c++) {
        float th = (au_clampf(P->s->pan[c], -1.0f, 1.0f) + 1.0f) * AU_PI * 0.25f;
        P->pan_l[c] = fminf(1.0f, cosf(th) * 1.41421356f);
        P->pan_r[c] = fminf(1.0f, sinf(th) * 1.41421356f);
    }
}

void au_music_command(int id)
{
    if (!music_ready) return;
    if (id <= MUS_NONE || id >= MUS_COUNT || !au_songs[id] || !psongs[id].len) {
        for (int i = 0; i < NPLAYER; i++) if (players[i].active) { players[i].fade_tgt = 0.0f; players[i].fade_speed = 1.0f / AU_FRATE; }
        return;
    }
    /* already playing (or fading out): bring it back */
    for (int i = 0; i < NPLAYER; i++) {
        Player *P = &players[i];
        if (P->active && P->id == id) {
            P->fade_tgt = 1.0f; P->fade_speed = 1.0f / AU_FRATE;
            for (int j = 0; j < NPLAYER; j++) if (j != i && players[j].active) { players[j].fade_tgt = 0.0f; players[j].fade_speed = 1.0f / AU_FRATE; }
            return;
        }
    }
    Player *slot = NULL;
    int audible = 0;
    for (int i = 0; i < NPLAYER; i++) if (!players[i].active) { slot = &players[i]; break; }
    if (!slot) {
        slot = &players[0];
        for (int i = 1; i < NPLAYER; i++) if (players[i].fade < slot->fade) slot = &players[i];
    }
    for (int i = 0; i < NPLAYER; i++) {
        Player *P = &players[i];
        if (P == slot || !P->active) continue;
        if (P->fade > 0.05f) audible = 1;
        P->fade_tgt = 0.0f; P->fade_speed = 1.0f / AU_FRATE;
    }
    player_start(slot, id);
    slot->fade = 0.0f; slot->fade_tgt = 1.0f;
    slot->fade_speed = audible ? 1.0f / AU_FRATE : 1.0f / (0.25f * AU_FRATE);
}

static void player_render(Player *P, float *outL, float *outR, int n, float rate)
{
    static float chL[AU_MCH][MBLOCK], chR[AU_MCH][MBLOCK];
    const AuSong *s = P->s;
    const PSong *ps = P->ps;
    for (int c = 0; c < AU_MCH; c++) { memset(chL[c], 0, sizeof(float) * (size_t)n); memset(chR[c], 0, sizeof(float) * (size_t)n); }
    P->nduck = 0;

    const double inc = (double)s->bpm * 4.0 / 60.0 / AU_FRATE * (double)rate;
    const float swing = s->swing;
    int off = 0;
    while (off < n) {
        double tnext = (double)P->next_step + ((P->next_step & 1) ? (double)swing : 0.0);
        double ds = (tnext - P->pos) / inc;
        int k = ds <= 0.0 ? 0 : (int)ceil(ds);
        if (k == 0) {
            fire_step(P, P->next_step, off);
            P->next_step++;
            if (P->next_step >= ps->len) { P->pos -= (double)(ps->len - ps->loop_step); P->next_step = ps->loop_step; }
            continue;
        }
        int m = k < n - off ? k : n - off;
        for (int i = 0; i < NVOICE; i++) {
            SVoice *v = &P->v[i];
            if (v->active) voice_render(v, chL[v->ch] + off, chR[v->ch] + off, m, rate);
        }
        for (int i = 0; i < NDVOICE; i++)
            if (P->dv[i].active) drum_render(&P->dv[i], chL[0] + off, chR[0] + off, m, rate);
        P->pos += inc * (double)m;
        off += m;
    }

    /* section automation */
    const PSec *S = &ps->sec[P->cur_sec];
    const AuSection *as = &s->sec[P->cur_sec];
    float open = 1.0f;
    if (as->lp0 != 0.0f || as->lp1 != 0.0f) {
        float u = au_clampf((float)((P->pos - (double)S->start) / (double)S->steps), 0.0f, 1.0f);
        open = au_lerpf(as->lp0, as->lp1, u);
    }
    float fc = 120.0f * powf(21000.0f / 120.0f, au_clampf(open, 0.0f, 1.0f));
    au_svf_set(&P->lpf[0][0], fc, 0.8f);
    au_svf_copycoef(&P->lpf[0][1], &P->lpf[0][0]);
    au_svf_copycoef(&P->lpf[1][0], &P->lpf[0][0]);
    au_svf_copycoef(&P->lpf[1][1], &P->lpf[0][0]);
    const int use_lpf = open < 0.995f;

    const float duck_k = expf(-1.0f / ((s->duck_time > 0.0f ? s->duck_time : 0.12f) * AU_FRATE / rate));
    const float duck_a = 1.0f - expf(-1.0f / (0.003f * AU_FRATE));
    const float dfb = s->delay_fb, dw = s->delay_wow;
    const float dlp_c = au_op_coef(3200.0f), dhp_c = au_op_coef(180.0f);
    const float master = s->master;
    int dt = 0;
    for (int i = 0; i < n; i++) {
        while (dt < P->nduck && P->duck_trig[dt] <= i) { P->duck = 1.0f; dt++; }
        P->duck_s += (P->duck - P->duck_s) * duck_a;
        P->duck *= duck_k;
        float dl = 0, dr = 0, ds = 0, rl = 0, rr = 0;
        for (int c = 0; c < AU_MCH; c++) {
            float g = s->gain[c] * (1.0f - s->duck[c] * P->duck_s);
            float l = chL[c][i] * g * P->pan_l[c], r = chR[c][i] * g * P->pan_r[c];
            if (meter_on) {
                float mm = 0.5f * (l + r) * master;
                if (fabsf(mm) > 1e-4f) { meter_sum[c] += (double)mm * mm; meter_act[c]++; }
                if (fabsf(mm) > meter_peak[c]) meter_peak[c] = fabsf(mm);
            }
            dl += l; dr += r;
            ds += (l + r) * 0.5f * s->dsend[c];
            rl += l * s->rsend[c]; rr += r * s->rsend[c];
        }
        /* ping-pong tape delay with wow and darkening repeats */
        float el = 0.0f, er = 0.0f;
        if (P->dlen > 0) {
            P->wph += 0.55f / AU_FRATE; if (P->wph >= 1.0f) P->wph -= 1.0f;
            double dd = (double)P->dlen + (double)(dw * 70.0f * au_sin(P->wph));
            double rp = (double)P->dpos - dd;
            while (rp < 0.0) rp += (double)DELAY_MAX;
            int i0 = (int)rp;
            float fr = (float)(rp - (double)i0);
            if (i0 >= DELAY_MAX) i0 -= DELAY_MAX;
            int i1 = i0 + 1; if (i1 >= DELAY_MAX) i1 = 0;
            float *bl = P->dbuf, *br = P->dbuf + DELAY_MAX;
            el = bl[i0] + (bl[i1] - bl[i0]) * fr;
            er = br[i0] + (br[i1] - br[i0]) * fr;
            P->dlp[0] += dlp_c * (el - P->dlp[0]); P->dlp[1] += dlp_c * (er - P->dlp[1]);
            P->dhp[0] += dhp_c * (P->dlp[0] - P->dhp[0]); P->dhp[1] += dhp_c * (P->dlp[1] - P->dhp[1]);
            float fl = P->dlp[0] - P->dhp[0], frr = P->dlp[1] - P->dhp[1];
            bl[P->dpos] = ds + frr * dfb;
            br[P->dpos] = fl * dfb;
            if (++P->dpos >= DELAY_MAX) P->dpos = 0;
        }
        float wl, wr;
        au_reverb_tick(&P->rev, rl + el * 0.25f, rr + er * 0.25f, &wl, &wr);
        float L = dl + el + wl, R = dr + er + wr;
        if (use_lpf) {
            L = au_svf_lp(&P->lpf[0][1], au_svf_lp(&P->lpf[0][0], L));
            R = au_svf_lp(&P->lpf[1][1], au_svf_lp(&P->lpf[1][0], R));
        } else {
            /* keep filter state warm so re-engaging is click free */
            au_svf_lp(&P->lpf[0][1], au_svf_lp(&P->lpf[0][0], L));
            au_svf_lp(&P->lpf[1][1], au_svf_lp(&P->lpf[1][0], R));
        }
        /* crossfade (equal power) */
        if (P->fade != P->fade_tgt) {
            if (P->fade < P->fade_tgt) { P->fade += P->fade_speed; if (P->fade > P->fade_tgt) P->fade = P->fade_tgt; }
            else { P->fade -= P->fade_speed; if (P->fade < P->fade_tgt) P->fade = P->fade_tgt; }
        }
        float g = sinf(P->fade * AU_PI * 0.5f) * master;
        outL[i] += L * g; outR[i] += R * g;
    }
    if (meter_on) meter_n += n;
    if (trace_song == P->id) {
        int nv = 0;
        for (int i = 0; i < NVOICE; i++) nv += P->v[i].active;
        if (nv > trace_maxv) trace_maxv = nv;
    }
    if (P->fade <= 0.0f && P->fade_tgt <= 0.0f) P->active = 0;
}

void au_music_render(float *L, float *R, int n, float rate)
{
    if (!music_ready) return;
    for (int i = 0; i < NPLAYER; i++) if (players[i].active) player_render(&players[i], L, R, n, rate);
}

/* ======================================================================== */
/* init                                                                      */
/* ======================================================================== */
int au_music_init(void)
{
    parse_errors = 0;
    ncache = 0;
    for (int id = 1; id < MUS_COUNT; id++) parse_song(id);
    for (int k = 0; k < AU_KIT_COUNT; k++) render_kit(k);
    for (int id = 1; id < MUS_COUNT; id++) render_song_fx(id);
    for (int i = 0; i < NPLAYER; i++) {
        memset(&players[i], 0, sizeof players[i]);
        if (!au_reverb_init(&players[i].rev, 1.6f, 0.05f)) return 0;
        players[i].dbuf = (float *)calloc((size_t)DELAY_MAX * 2, sizeof(float));
        if (!players[i].dbuf) return 0;
    }
    music_ready = 1;
    return 1;
}

void au_music_shutdown(void)
{
    music_ready = 0;
    for (int i = 0; i < NPLAYER; i++) {
        au_reverb_free(&players[i].rev);
        free(players[i].dbuf);
        memset(&players[i], 0, sizeof players[i]);
    }
    for (int k = 0; k < AU_KIT_COUNT; k++) for (int i = 0; i < K_COUNT; i++) au_sample_free(&kits[k][i]);
    for (int id = 0; id < MUS_COUNT; id++) { au_sample_free(&song_fx[id][0]); au_sample_free(&song_fx[id][1]); }
    for (int i = 0; i < ncache; i++) {
        if (!cache[i].pat) continue;
        if (cache[i].drum) {
            DPat *d = (DPat *)cache[i].pat;
            for (int l = 0; l < d->nl; l++) free(d->lane[l].vel);
            free(d);
        } else {
            MPat *m = (MPat *)cache[i].pat;
            free(m->ev); free(m);
        }
    }
    ncache = 0;
    for (int id = 0; id < MUS_COUNT; id++) {
        for (int i = 0; i < psongs[id].nsec; i++) free(psongs[id].sec[i].ch);
        free(psongs[id].sec);
        memset(&psongs[id], 0, sizeof psongs[id]);
    }
}
