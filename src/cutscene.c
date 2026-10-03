/* LAST AISLE - cutscenes: hand-drawn shots with parallax, weather and dithered transitions.
   A cutscene is a list of shots. Each shot draws itself from its own clock (so the outgoing shot
   keeps moving while the next one dissolves in), carries a few captions, and sets the ambience. */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"
#include "cutscene.h"

#define BAR_T 24            /* letterbox */
#define BAR_B 38
#define CAP_CPS 34.0f       /* caption letters per second */
#define OUT_DUR 0.9f        /* closing fade */
#define MAX_SHOTS 12
#define CAP_W 440           /* captions wrap to this width */

typedef void (*ShotFn)(float t);
typedef enum { TR_CUT, TR_DISSOLVE, TR_BLACK, TR_WIPE } Trans;

typedef struct {
    ShotFn draw;
    ShotFn cue;             /* optional: one-shot sounds, called with the clock window in cue_t0..cue_t1 */
    float dur;
    Trans tr;               /* how this shot replaces the one before */
    float tr_dur;
    char cap[4][256];       /* word-wrapped copies of the text */
    float cap_at[4], cap_len[4];
    int ncap;
    float wind, rain, fire, hum; /* ambience loops */
    bool open;              /* the letterbox opens during this shot */
} Shot;

static Shot shots[MAX_SHOTS];
static int nshots, cur, prev = -1;
static float vt, pvt;       /* picture clocks of the current and outgoing shot */
static float ct, pct;       /* caption clocks (current, outgoing): advance with time, jump forward on a key press */
static float out_t = -1;    /* closing fade, -1 while playing */
static float bars = 1;
static CutsceneId cut_id;
static int lv;              /* the level the cutscene is about */
static const char *title;   /* top bar, left */
static float cue_t0, cue_t1;

/* ----------------------------------------------------------------- building shots */
static Shot *shot(ShotFn draw, float dur, Trans tr, float tr_dur) {
    if (nshots >= MAX_SHOTS) return &shots[MAX_SHOTS - 1];
    Shot *s = &shots[nshots++];
    memset(s, 0, sizeof *s);
    s->draw = draw;
    s->dur = dur;
    s->tr = tr;
    s->tr_dur = tr_dur;
    return s;
}

/* greedy word wrap in place: a space becomes a line break where the line would run past maxw */
static int wrap(char *txt, int maxw) {
    int lines = 1;
    char *line = txt, *last_space = NULL;
    for (char *p = txt;; p++) {
        if (*p == ' ' || *p == '\n' || !*p) {
            char c = *p;
            *p = 0;
            bool over = gfx_text_w(FONT_SMALL, line) > maxw;
            *p = c;
            if (over && last_space) {
                *last_space = '\n';
                line = last_space + 1;
                lines++;
            }
            if (c == '\n') { line = p + 1; lines++; }
            last_space = c == ' ' ? p : NULL;
            if (!c) break;
        }
    }
    return lines;
}

/* wrap to the narrowest width that needs no more lines than CAP_W does: no lone word on a line of its own */
static void wrap_even(char *txt) {
    char orig[256], tmp[256];
    SDL_strlcpy(orig, txt, sizeof orig);
    int n = wrap(txt, CAP_W), best = CAP_W;
    for (int w = CAP_W - 8; n > 1 && w >= 120; w -= 8) {
        SDL_strlcpy(tmp, orig, sizeof tmp);
        if (wrap(tmp, w) > n) break;
        best = w;
    }
    SDL_strlcpy(txt, orig, 256);
    wrap(txt, best);
}

/* captions follow each other; the shot lasts at least until the last one has been read */
static void caption(Shot *s, const char *txt, int len) {
    if (!txt || !*txt || len <= 0 || s->ncap >= 4) return;
    char *c = s->cap[s->ncap];
    SDL_strlcpy(c, txt, MINF(len, 255) + 1);
    wrap_even(c);
    int n = 0;
    for (const char *p = c; *p; p++)
        if (*p == '^' && p[1]) p++;
        else if (*p != '\n') n++;
    float at = s->ncap ? s->cap_at[s->ncap - 1] + s->cap_len[s->ncap - 1] + 0.35f : MAXF(0.8f, s->tr_dur * 0.7f);
    s->cap_at[s->ncap] = at;
    s->cap_len[s->ncap] = 1.8f + n * 0.055f;
    s->ncap++;
    s->dur = MAXF(s->dur, at + s->cap_len[s->ncap - 1] + 0.5f);
}

/* the story texts break between thoughts (`sep`): each part becomes its own caption */
static void captions(Shot *s, const char *txt, const char *sep) {
    while (txt && *txt) {
        const char *e = strstr(txt, sep);
        caption(s, txt, e ? (int)(e - txt) : (int)strlen(txt));
        txt = e ? e + strlen(sep) : NULL;
    }
}

static bool at(float t) { return cue_t0 < t && t <= cue_t1; }

/* ----------------------------------------------------------------- drawing helpers */
static float hash(int i) {
    uint32_t h = (uint32_t)i * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (h & 0xFFFFFF) / 16777216.0f;
}

static float ease(float k) {
    k = CLAMP(k, 0.0f, 1.0f);
    return k * k * (3 - 2 * k);
}

static Color tinta(Color c, float a) {
    c.a = (Uint8)(CLAMP(a, 0.0f, 1.0f) * c.a);
    return c;
}

/* sky strip `k`, raised so its horizon glow sits behind the land at `horizon` */
static void sky(int k, float horizon) { gfx_spr_tile(SPR_CS_SKY + k, 0, horizon - VIEW_H, VIEW_W, VIEW_H, TINT_NONE); }

/* a horizontally repeating layer scrolled by `scroll` pixels */
static void band(int spr, float scroll, float y, Color tint) {
    float w = g_atlas[spr].w;
    float x = -fmodf(floorf(scroll), w);
    if (x > 0) x -= w;
    for (; x < VIEW_W; x += w) gfx_spr_c(spr, x, y, tint);
}

static void stars(float t, float bright, float ymax) {
    for (int i = 0; i < 90; i++) {
        float y = hash(i * 3 + 1) * ymax;
        float tw = 0.55f + 0.45f * sinf(t * (0.8f + hash(i) * 2.2f) + i);
        float a = bright * tw * (0.35f + 0.65f * hash(i * 5 + 2)) * (1.0f - y / ymax * 0.6f);
        if (a > 0.04f) gfx_fill(floorf(hash(i * 3) * VIEW_W), floorf(y), 1, 1, rgba(230, 226, 214, (Uint8)(a * 255)));
    }
}

/* snow drifting down in two depths; near flakes are bigger and faster */
static void snow(float t, int n, float wind, float a) {
    for (int i = 0; i < n; i++) {
        bool near = i % 5 == 0;
        float sp = near ? 26 + hash(i) * 10 : 9 + hash(i) * 8;
        float y = fmodf(hash(i * 7 + 3) * 300 + t * sp, 300) - 20;
        float x = hash(i * 11 + 5) * 520 + t * wind * (near ? 1.6f : 1) + sinf(t * (0.6f + hash(i) * 0.8f) + i) * (near ? 6 : 3);
        x = fmodf(x, 520) - 20;
        Color c = rgba(236, 232, 226, (Uint8)(a * (near ? 230 : 120 + hash(i * 13) * 80)));
        gfx_fill(floorf(x), floorf(y), near ? 2 : 1, near ? 2 : 1, c);
    }
}

/* ======================================================================= the intro */
/* the night the grid went down: the city's windows go out district by district */
static void sh_city_dark(float t) {
    static const float off_at[6] = {2.4f, 3.5f, 1.7f, 4.1f, 2.9f, 4.5f};
    sky(0, 214);
    float glow = 0;
    for (int d = 0; d < 6; d++) glow += CLAMP((off_at[d] + 0.3f - t) / 0.6f, 0.0f, 1.0f);
    stars(t, 0.25f + 0.75f * ease((t - 4.6f) / 3.0f), 150);
    gfx_spr(SPR_CS_MOON, 386, 46);
    gfx_glow(395, 55, 40, rgb(120, 130, 150), 0.25f);
    float pan = t * 1.6f;
    int city_y = 112;
    /* the light the city threw up into the sky dies with it */
    for (int k = 0; k < 4; k++) gfx_glow(60 + k * 120 - fmodf(pan, 120), city_y + 70, 120, rgb(150, 90, 40), glow / 6 * 0.55f);
    band(SPR_CS_CITY, pan, city_y, rgb(78, 86, 128));
    for (int d = 0; d < 6; d++) {
        float left = off_at[d] - t;
        if (left < 0) continue;
        /* a last stutter before each district goes */
        if (left < 0.5f && hash((int)(t * 18) + d * 31) < 0.45f) continue;
        for (int k = -1; k <= 1; k++) {
            float x = d * 80 - fmodf(floorf(pan), 480) + k * 480;
            if (x > VIEW_W || x + 80 < 0) continue;
            gfx_spr_src(SPR_CS_CITY_LIT, d * 80, 0, 80, g_atlas[SPR_CS_CITY_LIT].h, x, city_y, TINT_NONE);
        }
    }
    band(SPR_CS_HILLS, t * 4.0f, city_y + 46, rgb(34, 36, 58));
    gfx_fill(0, city_y + 100, VIEW_W, VIEW_H, rgb(13, 14, 24));
    snow(t, 70, 6, ease(t / 2.0f));
}

static void cue_city_dark(float t) {
    /* a distant thunk as each district goes */
    if (at(1.7f) || at(2.4f) || at(2.9f) || at(3.5f) || at(4.1f) || at(4.5f)) audio_play(SFX_DOOR_SLAM, 0.16f, frange(-0.5f, 0.5f), 0.5f);
}

/* ======================================================================= on the road */
/* how each kind of day tints the layers, far to near */
typedef struct { int sky; Color far, mid, near, fg, van; } Look;
enum { LOOK_SUNSET = AMB_INFERNO + 1 };
static const Look LOOKS[LOOK_SUNSET + 1] = {
    [AMB_DUSK] = {3, {120, 84, 86, 255}, {214, 156, 128, 255}, {222, 178, 150, 255}, {44, 30, 30, 255}, {255, 216, 190, 255}},
    [AMB_OVERCAST] = {2, {150, 148, 160, 255}, {176, 176, 184, 255}, {196, 196, 200, 255}, {46, 44, 52, 255}, {226, 226, 230, 255}},
    [AMB_RAIN] = {4, {66, 76, 92, 255}, {100, 112, 128, 255}, {118, 130, 148, 255}, {22, 24, 32, 255}, {160, 170, 186, 255}},
    [AMB_NIGHT] = {0, {36, 42, 70, 255}, {50, 56, 90, 255}, {64, 70, 104, 255}, {12, 12, 22, 255}, {104, 110, 146, 255}},
    [AMB_FOG] = {5, {196, 200, 202, 255}, {150, 154, 158, 255}, {164, 166, 170, 255}, {58, 60, 64, 255}, {206, 206, 208, 255}},
    [AMB_INFERNO] = {6, {90, 28, 28, 255}, {150, 76, 58, 255}, {170, 100, 80, 255}, {34, 12, 10, 255}, {232, 162, 132, 255}},
    [LOOK_SUNSET] = {7, {150, 96, 92, 255}, {222, 160, 120, 255}, {236, 186, 148, 255}, {50, 32, 28, 255}, {255, 224, 194, 255}},
};
static int drive_look;
static float drive_dur;

static void rain(float t, float a) {
    for (int i = 0; i < 140; i++) {
        float sp = 300 + hash(i) * 120;
        float y = fmodf(hash(i * 3 + 1) * 320 + t * sp, 320) - 30;
        float x = fmodf(hash(i * 7 + 2) * 560 - t * sp * 0.25f, 560);
        if (x < 0) x += 560;
        x -= 40;
        Color c = rgba(170, 186, 206, (Uint8)(a * (60 + hash(i * 5) * 70)));
        gfx_line(x, y, x - 2, y + 7, c);
    }
}

/* sparks rising off a burning city */
static void embers(float t, float a) {
    for (int i = 0; i < 60; i++) {
        float sp = 14 + hash(i) * 22;
        float y = 260 - fmodf(hash(i * 3) * 300 + t * sp, 300);
        float x = fmodf(hash(i * 5 + 1) * 500 + sinf(t * 1.3f + i) * 8 - t * 20, 500);
        if (x < 0) x += 500;
        Color c = hash(i * 7) < 0.5f ? rgba(255, 196, 92, (Uint8)(a * 220)) : rgba(255, 120, 50, (Uint8)(a * 180));
        gfx_fill(floorf(x) - 10, floorf(y), 1, 1, c);
    }
}

/* fog lies in banks, thicker towards the ground (four flat steps, like the light) */
static void fog_bank(float y, float h, float level, float drift) {
    for (int k = 0; k < 4; k++) {
        float l = level * (0.45f + 0.18f * k) * (0.9f + 0.1f * sinf(drift * 0.4f + k));
        gfx_fill(0, floorf(y + k * h / 4), VIEW_W, ceilf(h / 4), rgba(200, 204, 202, (Uint8)(CLAMP(l, 0.0f, 1.0f) * 255)));
    }
}

/* draws the van with its wheels turning for `dist` pixels travelled; (x, y) = top-left of the body */
static void van(float x, float y, float dist, Color tint, bool lights) {
    int f = ((int)(dist / 5) % 4 + 4) % 4;
    gfx_spr_c(SPR_CS_VAN, x, y, tint);
    gfx_spr_c(SPR_CS_WHEEL + f, x + 20, y + 32, tint);
    gfx_spr_c(SPR_CS_WHEEL + f, x + 66, y + 32, tint);
    if (lights) {
        for (int k = 0; k < 3; k++) gfx_cone(x + 82, y + 24, 0.06f, 70 + k * 45, rgb(255, 226, 170), 0.22f);
        gfx_glow(x + 81, y + 24, 10, rgb(255, 236, 190), 0.8f);
        gfx_glow(x + 3, y + 16, 7, rgb(255, 40, 30), 0.6f);
    }
}

static void exhaust(float x, float y, float t, float drift, Color tint) {
    for (int i = 0; i < 5; i++) {
        float age = fmodf(t * 2.2f + i * 0.2f, 1.0f);
        float px = x - age * drift - age * 6, py = y - age * 7;
        Color c = tinta(tint, (1.0f - age) * 0.45f);
        gfx_spr_ex(SPR_FX_SMOKE + (i & 3), px, py, i * 1.3f, 0.5f + age * 0.9f, 0.5f + age * 0.9f, c);
    }
}

static void sh_drive(float t) {
    const Look *L = &LOOKS[drive_look];
    int amb = drive_look;
    const float speed = 80;
    float d = t * speed;
    sky(L->sky, 200);
    if (amb == AMB_NIGHT) {
        stars(t, 0.9f, 150);
        gfx_spr(SPR_CS_MOON, 92, 40);
        gfx_glow(101, 49, 34, rgb(120, 130, 150), 0.22f);
    } else if (amb == AMB_DUSK || amb == LOOK_SUNSET) {
        float sy = amb == LOOK_SUNSET ? 112 + t * 1.2f : 84 + t * 1.5f;
        gfx_glow(312, sy, 90, rgb(255, 150, 60), 0.4f);
        gfx_spr(SPR_CS_SUN, 300, sy - 12);
    }
    band(SPR_CS_CITY, d * 0.05f, 108, L->far);
    if (amb == AMB_INFERNO) {
        /* the city burns on the horizon */
        for (int i = 0; i < 6; i++) {
            float fx = fmodf(i * 97.0f - d * 0.05f, 480);
            if (fx < 0) fx += 480;
            gfx_glow(fx, 180, 40 + hash(i) * 30, rgb(255, 110, 40), 0.35f + 0.15f * sinf(t * 7 + i * 2));
        }
    }
    if (amb == AMB_FOG) fog_bank(120, 80, 0.55f, t);
    band(SPR_CS_ROADSIDE, d * 0.35f, 100, L->mid);
    if (amb == AMB_FOG) fog_bank(150, 50, 0.4f, t + 3);
    /* power poles, the dead lines sagging between them */
    const float gap = 230;
    float off = fmodf(d * 0.7f, gap);
    for (int k = -1; k < 4; k++) {
        float px = k * gap - off;
        Color wire = tinta(rgb(30, 26, 30), 0.85f);
        for (int s = 0; s < 8; s++) {
            float a0 = s / 8.0f, a1 = (s + 1) / 8.0f;
            float y0 = 60 + 18 * 4 * a0 * (1 - a0), y1 = 60 + 18 * 4 * a1 * (1 - a1);
            gfx_line(px + 12 + a0 * gap, y0, px + 12 + a1 * gap, y1, wire);
        }
        gfx_spr_c(SPR_CS_POLE, px, 50, L->mid);
    }
    band(SPR_CS_ROAD, d, 184, L->near);
    /* the van rolls in, holds the frame, then pulls away */
    float in = ease(t / 2.4f), outk = CLAMP((t - (drive_dur - 2.0f)) / 2.0f, 0.0f, 1.0f);
    float vx = lerpf(-110, 186, in) + outk * outk * 360 + sinf(t * 0.7f) * 3;
    float vy = 184 + 30 - 39 + ((hash((int)(t * 9)) < 0.18f) ? -1 : 0);
    bool lights = amb == AMB_NIGHT || amb == AMB_RAIN || amb == AMB_FOG || amb == AMB_INFERNO;
    exhaust(vx + 2, vy + 33, t, 30, L->van);
    van(vx, vy, d, L->van, lights);
    if (amb == AMB_FOG) fog_bank(196, 30, 0.22f, t + 5);
    band(SPR_CS_WEEDS + ((int)(t * 1.5f) & 1), d * 1.7f, 208, L->fg);
    if (amb == AMB_RAIN) rain(t, 1);
    if (amb == AMB_INFERNO) embers(t, 1);
    if (amb == AMB_DUSK || amb == LOOK_SUNSET) {
        /* dust hanging in the low sun */
        for (int i = 0; i < 30; i++) {
            float x = fmodf(hash(i) * 480 - t * (8 + hash(i * 3) * 10), 480);
            if (x < 0) x += 480;
            float y = 60 + hash(i * 5) * 160 + sinf(t + i) * 4;
            gfx_fill(floorf(x), floorf(y), 1, 1, rgba(255, 226, 170, (Uint8)(60 + hash(i * 7) * 90)));
        }
    }
}

static void cue_drive(float t) {
    if (at(0.05f)) audio_play(SFX_ENGINE, 0.75f, -0.3f, 1);
    if (at(drive_dur - 2.0f)) audio_play(SFX_ENGINE, 0.6f, 0.4f, 1.1f);
    if (drive_look == AMB_RAIN && at(2.7f)) {
        G.flash = 0.55f;
        G.flash_col = rgba(220, 226, 240, 255);
        audio_play(SFX_EXPLOSION, 0.22f, 0.5f, 0.35f);
    }
}

static char drive_title[64];

static void build_drive(void) {
    const LevelDef *d = &LEVELS[lv];
    drive_look = d->amb;
    SDL_snprintf(drive_title, sizeof drive_title, "STORE %d OF %d   ^y%s^0   %s", lv + 1, NUM_LEVELS, d->name, d->place);
    Shot *s = shot(sh_drive, 7.0f, TR_BLACK, 1.2f);
    s->cue = cue_drive;
    s->wind = 0.4f;
    s->rain = drive_look == AMB_RAIN ? 0.9f : 0;
    s->fire = drive_look == AMB_INFERNO ? 0.4f : 0;
    captions(s, d->drive, "\n\n");
    drive_dur = s->dur;
}

/* ======================================================================= home */
/* where the people stand inside the glass: x along the building, folk frame, bob (children hop) */
typedef struct { short x, f, hop; } Folk;
static const Folk HOME_FOLK[NUM_LEVELS][8] = {
    {{60, 1, 0}, {88, 3, 1}, {104, 2, 1}, {176, 0, 0}, {-1}},                                  /* the kids have hiccups */
    {{30, 0, 0}, {52, 4, 0}, {86, 2, 0}, {188, 1, 0}, {222, 0, 0}, {-1}},                      /* singing in the kitchen */
    {{26, 6, 0}, {58, 3, 1}, {78, 0, 0}, {112, 7, 0}, {174, 6, 0}, {202, 3, 1}, {236, 0, 0}, {-1}}, /* lights */
    {{40, 0, 0}, {184, 5, 0}, {200, 4, 0}, {-1}},                                              /* Theo, at dawn */
    {{18, 0, 0}, {40, 2, 0}, {62, 4, 0}, {84, 1, 0}, {110, 7, 0}, {176, 0, 0}, {200, 4, 0}, {246, 3, 1}}, /* fourteen */
};

static void string_lights(float gx, float gy, float t, float on_at) {
    /* festoon along the eaves: left wing, up over the gable, right wing */
    static const float pts[][2] = {{8, 60}, {104, 52}, {144, 16}, {184, 52}, {280, 60}};
    int bulb = 0;
    for (int s = 0; s < 4; s++) {
        float x0 = pts[s][0], y0 = pts[s][1], x1 = pts[s + 1][0], y1 = pts[s + 1][1];
        int n = (int)(fabsf(x1 - x0) / 8);
        for (int i = 0; i < n; i++, bulb++) {
            float a = (i + 0.5f) / n;
            float x = gx + lerpf(x0, x1, a), y = gy + lerpf(y0, y1, a) + 3 * 4 * a * (1 - a);
            bool on = t > on_at + bulb * 0.05f;
            static const Color bc[4] = {{255, 212, 71, 255}, {255, 140, 46, 255}, {255, 239, 168, 255}, {232, 41, 63, 255}};
            Color c = on ? bc[bulb % 4] : rgb(60, 52, 50);
            gfx_fill(floorf(x), floorf(y), 2, 2, c);
            if (on) gfx_glow(x + 1, y + 1, 7, c, 0.35f + 0.1f * sinf(t * 3 + bulb));
        }
    }
}

/* the house itself: glass, glow by sections, the people inside, then the frames over them */
typedef struct {
    float warm[4];      /* glow of each section: left wing, centre left, centre right, right wing */
    float flicker;      /* lamps and candles waver; electric light doesn't */
    float on_at;        /* >= 0: the generator kicks in at this time */
    const Folk *folk;
    Color tint, folk_tint;
} House;

static void greenhouse(float t, float gx, float gy, const House *h) {
    static const short secs[5] = {0, 104, 144, 184, 288};
    gfx_spr_c(SPR_CS_GREENHOUSE_GLASS, gx, gy, h->tint);
    for (int k = 0; k < 4; k++) {
        float a = h->warm[k] * (1.0f - h->flicker + h->flicker * (0.5f + 0.5f * sinf(t * (5.3f + k) + k * 1.7f) * sinf(t * 3.1f + k)));
        if (h->on_at >= 0) {
            /* the generator coughs, then roars */
            float on = h->on_at + k * 0.25f;
            a = t < on ? 0.15f : (t < on + 0.6f && hash((int)(t * 20) + k) < 0.5f ? 0.3f : h->warm[k]);
        }
        if (a <= 0.01f) continue;
        gfx_spr_src(SPR_CS_GREENHOUSE_LIT, secs[k], 0, secs[k + 1] - secs[k], g_atlas[SPR_CS_GREENHOUSE_LIT].h,
                    gx + secs[k], gy, rgba(255, 255, 255, (Uint8)(CLAMP(a, 0.0f, 1.0f) * 255)));
    }
    for (int i = 0; h->folk && i < 8; i++) {
        const Folk *f = &h->folk[i];
        if (f->x < 0) break;
        float hop = f->hop ? -floorf(fabsf(sinf(t * 5 + i)) * 3) : 0;
        gfx_spr_c(SPR_CS_FOLK + f->f, gx + f->x, gy + 77 + hop, h->folk_tint);
    }
    gfx_spr_c(SPR_CS_GREENHOUSE, gx, gy, h->tint);
}

/* smoke from the stove, drifting off with the wind */
static void stove_smoke(float gx, float gy, float t, Color c) {
    for (int i = 0; i < 6; i++) {
        float age = fmodf(t * 0.35f + i / 6.0f, 1.0f);
        float sx = gx + 42 + age * 26 + sinf(t * 0.8f + i) * 3, sy = gy + 16 - age * 60;
        gfx_spr_ex(SPR_FX_SMOKE + (i & 3), sx, sy, age * 2 + i, 0.8f + age * 1.8f, 0.8f + age * 1.8f, tinta(c, (1 - age) * 0.6f));
    }
}

static void sh_home(float t) {
    static const float warm[NUM_LEVELS][4] = {
        {0.55f, 0.9f, 0.6f, 0.0f}, {0.9f, 0.9f, 0.7f, 0.35f}, {1, 1, 1, 1}, {0.4f, 0.5f, 0.5f, 0.45f}, {1, 1, 1, 1}};
    bool dawn = lv == 3;
    Color tint = dawn ? rgb(176, 160, 168) : rgb(70, 78, 116);
    sky(dawn ? 1 : 0, 196);
    if (!dawn) {
        stars(t, 0.85f, 150);
        gfx_spr(SPR_CS_MOON, 404, 40);
        gfx_glow(413, 49, 34, rgb(120, 130, 150), 0.2f);
    }
    band(SPR_CS_CITY, 0, 140, dawn ? rgb(150, 150, 170) : rgb(34, 40, 66));
    band(SPR_CS_HILLS, t * 0.6f, 154, dawn ? rgb(96, 88, 104) : rgb(30, 32, 52));
    band(SPR_CS_GROUND, 0, 198, tint);
    const float gx = 40, gy = 200 - 111;
    /* more of the house is lived in after every store */
    House h = {{0}, lv < 2 ? 0.14f : 0, lv == 2 ? 2.4f : -1, HOME_FOLK[lv], tint, rgba(26, 18, 14, 255)};
    memcpy(h.warm, warm[lv], sizeof h.warm);
    greenhouse(t, gx, gy, &h);
    if (lv >= 2) string_lights(gx, gy, t, lv == 2 ? 3.6f : -10);
    /* light spilling out of the door onto the yard */
    gfx_glow(gx + 145, gy + 112, 34, rgb(255, 170, 80), dawn ? 0.2f : 0.45f);
    stove_smoke(gx, gy, t, dawn ? rgb(200, 190, 190) : rgb(120, 120, 140));
    /* the van comes home and parks beside the house */
    float k = ease(t / 3.4f);
    float vx = lerpf(-120, 346, k);
    van(vx, 200 - 37, vx, dawn ? rgb(200, 186, 190) : rgb(92, 98, 136), !dawn && t < 4.0f);
    if (t > 3.6f) {
        /* someone comes to the door */
        float a = CLAMP((t - 3.6f) / 0.8f, 0.0f, 1.0f);
        gfx_spr_c(SPR_CS_FOLK + 6, gx + 137, gy + 74, rgba(26, 18, 14, (Uint8)(a * 255)));
    }
    band(SPR_CS_WEEDS, 0, 214, dawn ? rgb(60, 50, 56) : rgb(14, 14, 24));
}

static void cue_home(float t) {
    if (at(0.1f)) audio_play(SFX_ENGINE, 0.55f, -0.5f, 0.95f);
    if (at(3.5f)) audio_play(SFX_VAN_DOOR, 0.6f, 0.4f, 1);
    if (at(4.2f)) audio_play(SFX_DOOR_CREAK, 0.4f, -0.1f, 1);
    if (lv == 2 && at(2.4f)) audio_play(SFX_ENGINE, 0.5f, -0.2f, 0.6f);
}

static void build_home(void) {
    Shot *s = shot(sh_home, 7.5f, TR_BLACK, 1.4f);
    s->cue = cue_home;
    s->wind = 0.35f;
    s->fire = 0.12f;
    captions(s, LEVELS[lv].after, "\n\n");
}

/* ======================================================================= the intro, continued */
/* the cities just stopped: a downtown street, the weeds coming up one crack at a time */
static void sh_street(float t) {
    float pan = t * 7;
    sky(2, 200);
    band(SPR_CS_CITY, pan * 0.1f, 70, rgb(178, 176, 188));
    /* crows crossing high up */
    for (int i = 0; i < 3; i++) {
        float x = -30 + (t - 1.0f) * 46 + i * 15, y = 52 + i * 5 + sinf(t * 2.2f + i) * 3;
        if (x > -20 && x < VIEW_W) gfx_spr_c(SPR_CS_CROW + ((int)(t * 9 + i * 2) % 3), x, y, rgb(40, 36, 44));
    }
    band(SPR_CS_DOWNTOWN, pan * 0.5f, 197 - 128, rgb(206, 200, 196));
    band(SPR_CS_STREET, pan, 186, rgb(214, 208, 200));
    float sx = -floorf(pan);
    /* what stands in the street, all scrolling with it */
    gfx_spr_c(SPR_CS_TRAFFIC, 270 + sx, 197 - 104, rgb(206, 200, 196));
    gfx_spr_c(SPR_CS_DEER + (t > 6.2f), 156 + sx, 197 - 25, rgb(214, 206, 196));
    for (int i = 0; i < 8; i++) {
        float stage = (t - 0.9f - i * 0.6f) / 0.22f;
        if (stage < 0) continue;
        float wx = 40 + i * 52 + hash(i) * 14 + sx;
        gfx_spr_c(SPR_CS_WEED + (int)MINF(stage, 4), wx, 197 - 15 - (i % 2) * 6, rgb(214, 208, 200));
    }
    gfx_spr_c(SPR_CS_CAR, 34 + sx, 226 - 29, rgb(210, 204, 198));
    gfx_spr_c(SPR_CS_BEDTREE, 352 + sx, 226 - 29 - 36, rgb(210, 204, 198));
    gfx_spr_c(SPR_CS_CAR + 1, 334 + sx, 226 - 29, rgb(210, 204, 198));
    band(SPR_CS_WEEDS + ((int)(t * 1.2f) & 1), pan * 1.5f, 214, rgb(40, 40, 44));
}

/* the last mines: an aisle picked clean, light coming in where the roof gave way */
static void aisle(float t, Color tint, Color beam, float scav_a) {
    gfx_spr_c(SPR_CS_AISLE, 0, 24, tint);
    /* the shaft from the hole in the roof down to the floor, dust turning in it */
    for (int y = 40; y < 210; y += 2) {
        float k = (y - 40) / 170.0f;
        float x0 = lerpf(298, 270, k), x1 = lerpf(372, 392, k);
        float a = (1.0f - k * 0.6f) * (0.85f + 0.15f * sinf(t * 0.7f + y * 0.05f));
        gfx_fill(x0, y, x1 - x0, 2, tinta(beam, a * 0.16f));
        gfx_fill(x0 + 8, y, x1 - x0 - 16, 2, tinta(beam, a * 0.1f));
    }
    gfx_glow(332, 202, 70, beam, 0.35f);
    for (int i = 0; i < 40; i++) {
        float k = hash(i * 3);
        float y = 44 + fmodf(hash(i) * 170 + t * (3 + hash(i * 7) * 4), 170);
        float x = lerpf(300, 272, (y - 40) / 170) + k * 90 + sinf(t * 0.6f + i) * 4;
        gfx_fill(floorf(x), floorf(y), 1, 1, tinta(beam, 0.5f + hash(i * 5) * 0.5f));
    }
    /* far down the aisle, two scavengers sweep their flashlights across the empty shelves */
    for (int i = 0; scav_a > 0 && i < 2; i++) {
        float ph = t * 0.22f + i * 0.5f;
        float x = 226 + sinf(ph * PI_F) * 16 + i * 10;
        float dir = cosf(ph * PI_F) * (i ? -1 : 1);
        int f = (int)(t * 4 + i) % 3;
        float y = 24 + 92 + 104 / 2.4f - 22 + i * 3;
        Color sc = tinta(rgb(16, 12, 14), scav_a);
        gfx_spr_ex(SPR_CS_SCAV + f, x, y, 0, dir < 0 ? -1 : 1, 1, sc);
        float aim = (dir < 0 ? PI_F : 0) + sinf(t * 0.9f + i * 2) * 0.5f + 0.25f;
        gfx_cone(x + (dir < 0 ? 1 : 9), y + 7, aim, 70, rgb(255, 230, 170), 0.22f * scav_a);
    }
}

static void sh_aisle(float t) { aisle(t, rgb(150, 132, 120), rgb(255, 226, 170), 1); }

/* the greenhouse at dusk: twelve people behind the glass */
static const Folk INTRO_FOLK[8] = {{30, 2, 0}, {48, 0, 0}, {86, 4, 0}, {120, 7, 0}, {176, 5, 0}, {196, 4, 0}, {232, 1, 0}, {-1}};

static void sh_house_dusk(float t) {
    sky(3, 200);
    gfx_glow(420, 176, 80, rgb(255, 150, 60), 0.35f);
    band(SPR_CS_CITY, 0, 140, rgb(96, 70, 76));
    band(SPR_CS_HILLS, t * 0.6f, 154, rgb(66, 50, 56));
    Color tint = rgb(160, 128, 124);
    band(SPR_CS_GROUND, 0, 198, tint);
    const float gx = 96, gy = 200 - 111;
    House h = {{0.7f, 0.85f, 0.75f, 0.45f}, 0.16f, -1, INTRO_FOLK, tint, rgba(30, 20, 16, 255)};
    greenhouse(t, gx, gy, &h);
    gfx_glow(gx + 145, gy + 112, 30, rgb(255, 170, 80), 0.3f);
    stove_smoke(gx, gy, t, rgb(150, 130, 130));
    van(398, 200 - 37, 0, rgb(170, 140, 136), false);
    band(SPR_CS_WEEDS, t * 0.5f, 214, rgb(40, 28, 26));
}

/* somebody has to go shopping: the list gets written by lantern light */
static const char LIST_TEXT[] = "SHOPPING LIST\n- water\n- cans, any kind\n- bandages\n- batteries";
#define LIST_CPS 13.0f
#define LIST_X 162
#define LIST_Y 62

static int list_chars(float t) { return (int)MAXF(0, (t - 0.8f) * LIST_CPS); }

static void sh_list(float t) {
    float fl = 0.85f + 0.15f * sinf(t * 7.1f) * sinf(t * 3.3f + 1);
    gfx_spr_tile(SPR_CS_WOOD, 0, 0, VIEW_W, VIEW_H, rgb(92, 66, 54));
    gfx_glow(118, 92, 230, rgb(255, 170, 90), 0.5f * fl);
    /* the radio, the van keys */
    gfx_spr_ex(SPR_UI_RADIO, 392, 168, -0.35f, 1, 1, rgb(200, 170, 150));
    /* the paper, taped down */
    const float px = 150, py = 44, pw = 186, ph = 172;
    gfx_fill(px + 4, py + 5, pw, ph, rgba(0, 0, 0, 90));
    gfx_nine(SPR_UI_PAPER, px, py, pw, ph, rgb(250, 232, 204));
    gfx_spr(SPR_UI_TAPE, px + pw / 2, py + 1);
    int n = list_chars(t), total = 0;
    for (const char *c = LIST_TEXT; *c; c++) total += *c != '\n';
    gfx_text_wrap_n(FONT_BIG, LIST_TEXT, LIST_X, LIST_Y, 170, rgb(30, 45, 110), 0, 18, n);
    /* then, in another hand, underlined twice */
    float come = t - (0.8f + total / LIST_CPS + 1.0f);
    const char *cb = "COME BACK.";
    int m = (int)MAXF(0, come * 9);
    char buf[16];
    SDL_strlcpy(buf, cb, MINF(m, (int)strlen(cb)) + 1);
    float cx = LIST_X + 26, cy = LIST_Y + 104;
    if (m > 0) gfx_text(FONT_BIG, buf, cx, cy, rgb(190, 30, 40), 0);
    float under = CLAMP((come - 1.4f) / 0.5f, 0.0f, 1.0f);
    int uw = (int)(gfx_text_w(FONT_BIG, cb) * under);
    if (uw > 0) {
        gfx_fill(cx - 2, cy + 14, uw + 2, 1, rgb(190, 30, 40));
        gfx_fill(cx, cy + 16, uw, 1, rgb(190, 30, 40));
    }
    /* the pencil follows the writing */
    float tipx, tipy;
    if (m > 0 && m <= (int)strlen(cb)) {
        tipx = cx + gfx_text_w(FONT_BIG, buf) + 2;
        tipy = cy + 10;
    } else if (come > 0) {
        tipx = cx + uw;
        tipy = cy + 16 + (under >= 1 ? 30 * ease(come - 1.9f) : 0);
    } else {
        /* just after the last letter written */
        int line = 0, col = 0, k = 0;
        for (const char *c = LIST_TEXT; *c && k < n; c++) {
            if (*c == '\n') { line++; col = 0; continue; }
            col++;
            k++;
        }
        const char *ls = LIST_TEXT;
        for (int l = 0; l < line; l++) ls = strchr(ls, '\n') + 1;
        char lb[32];
        int len = MINF(col, 31);
        memcpy(lb, ls, len);
        lb[len] = 0;
        tipx = LIST_X + gfx_text_w(FONT_BIG, lb) + 2;
        tipy = LIST_Y + line * 18 + 10;
        if (n >= total) { tipx += 6 * ease((t - 0.8f - total / LIST_CPS) * 2); tipy += 4; }
    }
    float wob = sinf(t * 23) * 0.6f;
    gfx_spr_ex(SPR_CS_PENCIL, tipx + 3, tipy + 3, 0.62f, 1, 1, rgba(0, 0, 0, 80));
    gfx_spr_ex(SPR_CS_PENCIL, tipx, tipy + wob, 0.62f, 1, 1, rgb(240, 220, 200));
    /* the lantern, top left */
    gfx_spr_c(SPR_CS_LANTERN + ((int)(t * 10) % 3), 92, 66, TINT_NONE);
    gfx_glow(92, 66, 40, rgb(255, 210, 140), 0.6f * fl);
}

static void cue_list(float t) {
    int a = list_chars(cue_t0), b = list_chars(cue_t1);
    if (b > a && b <= 54 && b % 2 == 0) audio_play(SFX_TYPE, 0.3f, 0.1f, frange(0.55f, 0.7f));
}

/* today, that's you */
static void sh_hero(float t) {
    sky(1, 196);
    gfx_glow(120, 190, 120, rgb(255, 210, 150), 0.3f + 0.1f * ease(t / 6));
    band(SPR_CS_CITY, 0, 124, rgb(104, 110, 146));
    band(SPR_CS_HILLS, t * 0.4f, 150, rgb(70, 66, 88));
    band(SPR_CS_GROUND, 0, 198, rgb(150, 126, 116));
    van(52, 214 - 39, 0, rgb(196, 176, 172), false);
    gfx_spr_c(SPR_CS_HERO_BACK + ((int)(t * 1.3f) & 1), 300, 214 - 62, rgb(232, 214, 206));
    /* leaves blown past on the wind */
    for (int i = 0; i < 14; i++) {
        float x = fmodf(hash(i) * 520 + t * (40 + hash(i * 3) * 30), 520) - 20;
        float y = 120 + hash(i * 5) * 100 + sinf(t * 3 + i) * 6;
        gfx_fill(floorf(x), floorf(y), 2, 1, hash(i * 7) < 0.5f ? rgb(196, 86, 28) : rgb(124, 75, 34));
    }
    band(SPR_CS_WEEDS + ((int)(t * 1.6f) & 1), t * 2, 212, rgb(48, 40, 44));
}

static void build_intro(void) {
    Shot *s = shot(sh_city_dark, 8.0f, TR_BLACK, 1.6f);
    s->cue = cue_city_dark;
    s->wind = 0.5f;
    captions(s, INTRO_PAGES[0], "\n\n");
    s = shot(sh_street, 9.0f, TR_DISSOLVE, 1.6f);
    s->wind = 0.4f;
    captions(s, INTRO_PAGES[1], "\n\n");
    s = shot(sh_aisle, 7.5f, TR_BLACK, 1.2f);
    s->wind = 0.15f;
    captions(s, INTRO_PAGES[2], "\n\n");
    s = shot(sh_house_dusk, 8.0f, TR_DISSOLVE, 1.8f);
    s->wind = 0.3f;
    s->fire = 0.1f;
    captions(s, INTRO_PAGES[3], "\n\n");
    s = shot(sh_list, 9.5f, TR_BLACK, 1.2f);
    s->cue = cue_list;
    s->fire = 0.2f;
    captions(s, INTRO_PAGES[4], "\n\n");
    s = shot(sh_hero, 6.0f, TR_DISSOLVE, 2.0f);
    s->wind = 0.6f;
    captions(s, INTRO_PAGES[5], "\n\n");
}

/* ======================================================================= the ending */
/* the Mall King's crown rolls across the food court floor, and nobody picks it up */
static void sh_crown(float t) {
    float fl = 0.8f + 0.2f * sinf(t * 6.3f) * sinf(t * 2.7f + 1);
    gfx_spr_c(SPR_CS_FOODCOURT, 0, 24, rgb(140, 92, 80));
    /* grey daylight through the broken skylight */
    for (int y = 28; y < 222; y += 2) {
        float k = (y - 28) / 194.0f;
        float x0 = lerpf(184, 150, k), x1 = lerpf(300, 336, k);
        gfx_fill(x0, y, x1 - x0, 2, rgba(200, 210, 226, (Uint8)(26 * (1 - k * 0.5f))));
    }
    gfx_glow(240, 214, 90, rgb(190, 200, 220), 0.3f);
    /* the mall is burning somewhere off to the right */
    gfx_glow(500, 150, 170, rgb(255, 110, 40), 0.42f * fl);
    gfx_glow(-20, 120, 90, rgb(255, 90, 30), 0.2f * fl);
    for (int i = 0; i < 7; i++) {
        float age = fmodf(t * 0.12f + i / 7.0f, 1.0f);
        gfx_spr_ex(SPR_FX_SMOKE + (i & 3), 480 - age * 520, 40 + sinf(i * 1.7f) * 8 + age * 6, i + age, 3 + age * 2, 2 + age,
                   rgba(30, 20, 20, (Uint8)(110 * sinf(age * PI_F))));
    }
    gfx_spr_c(SPR_CS_CAN, 120, 196, rgb(170, 120, 100));
    gfx_spr_c(SPR_CS_CAN + 1, 352, 208, rgb(170, 120, 100));
    gfx_spr_c(SPR_CS_CAN + 1, 70, 214, rgb(150, 110, 96));
    /* it rolls, slows, wobbles and stands still: four turns exactly, so it ends upright */
    const float r = 9.5f, dist = 2 * PI_F * r * 4;
    float k = 1.0f - expf(-t * 1.05f);
    float x = 40 + dist * k;
    float ang = -(x - 40) / r;
    if (t > 2.6f) ang += 0.22f * expf(-(t - 2.6f) * 1.6f) * sinf((t - 2.6f) * 10);
    gfx_spr_c(SPR_FX_SHADOW, x, 213, rgba(0, 0, 0, 120));
    gfx_spr_ex(SPR_CS_CROWN, x, 203, ang, 1, 1, rgb(230, 190, 150));
    embers(t, 0.6f);
}

/* a clink each time one of its five cans meets the floor, quieter as it slows */
static void cue_crown(float t) {
    const float r = 9.5f, step = 2 * PI_F * r / 5, dist = 2 * PI_F * r * 4;
    float d0 = dist * (1.0f - expf(-cue_t0 * 1.05f)), d1 = dist * (1.0f - expf(-cue_t1 * 1.05f));
    if ((int)(d1 / step) > (int)(d0 / step)) audio_play(SFX_HIT_METAL, 0.05f + 0.2f * expf(-t * 1.05f), -0.4f + d1 / dist * 0.6f, 1.7f);
}

/* the drive home from the driver's seat: seed packets on the dashboard */
static void sh_dash(float t) {
    float bounce = (hash((int)(t * 7)) < 0.2f) ? 1 : 0;
    const float hz = 132;   /* horizon */
    sky(7, hz + 10);
    gfx_glow(196, hz - 14, 110, rgb(255, 170, 80), 0.45f);
    gfx_spr(SPR_CS_SUN, 184, hz - 26);
    band(SPR_CS_HILLS, t * 1.5f, hz - 32, rgb(150, 98, 92));
    band(SPR_CS_GROUND, t * 5, hz, rgb(200, 150, 110));
    gfx_fill(0, hz + 38, VIEW_W, 80, rgb(90, 70, 50));
    /* the road runs on ahead to the hills */
    for (int y = hz + 2; y < 240; y++) {
        float d = y - hz;
        float hw = 3 + d * 2.4f;
        gfx_fill(240 - hw, y, hw * 2, 1, rgb(84, 74, 70));
        float z = 60.0f / d;
        if (fmodf(z + t * 2.2f, 1.0f) < 0.45f) gfx_fill(240 - d * 0.05f - 1, y, MAXF(1, d * 0.1f), 1, rgb(220, 196, 150));
        gfx_fill(240 - hw - MAXF(1, d * 0.08f), y, MAXF(1, d * 0.08f), 1, rgb(150, 132, 110));
    }
    /* inside: the roof, the mirror and the little tree swinging under it */
    gfx_fill(0, 24, VIEW_W, 12, rgb(20, 16, 18));
    gfx_spr_c(SPR_CS_MIRROR, 216, 32, rgb(200, 170, 150));
    gfx_spr_ex(SPR_CS_FRESHENER, 236, 50, sinf(t * 2.1f) * 0.28f + sinf(t * 5.3f) * 0.05f, 1, 1, rgb(220, 190, 160));
    gfx_spr_c(SPR_CS_PILLAR, 0, 24, rgb(170, 140, 130));
    gfx_spr_ex(SPR_CS_PILLAR, VIEW_W, 24, 0, -1, 1, rgb(170, 140, 130));
    float dy = 160 + bounce;
    gfx_spr_c(SPR_CS_DASH, 0, dy, rgb(190, 150, 130));
    /* tomatoes, beans, squash - leaned against the glass */
    static const float ang[3] = {-0.12f, 0.04f, 0.18f};
    for (int i = 0; i < 3; i++)
        gfx_spr_ex(SPR_CS_SEEDS + i, 262 + i * 30, dy + 4 - (i == 1 ? 2 : 0), ang[i], 1, 1, rgb(255, 236, 210));
    gfx_glow(300, 200, 90, rgb(255, 180, 100), 0.25f);
}

static void cue_dash(float t) {
    if (at(0.1f)) audio_play(SFX_ENGINE, 0.4f, 0, 0.9f);
}

/* spring comes late that year, but it comes */
static void spring_house(float t, bool spring) {
    sky(spring ? 1 : 2, 196);
    if (spring) gfx_glow(420, 100, 90, rgb(255, 230, 170), 0.35f);
    band(SPR_CS_CITY, 0, 140, spring ? rgb(176, 184, 200) : rgb(150, 150, 160));
    band(SPR_CS_HILLS, 0, 154, spring ? rgb(110, 140, 100) : rgb(170, 170, 180));
    Color tint = spring ? rgb(236, 230, 214) : rgb(186, 186, 196);
    band(SPR_CS_GROUND + (spring ? 2 : 1), 0, 198, tint);
    const float gx = 96, gy = 200 - 111;
    House h = {{0.12f, 0.2f, 0.2f, 0.12f}, 0, -1, NULL, tint, rgba(26, 18, 14, 255)};
    greenhouse(t, gx, gy, &h);
    gfx_spr_c(spring ? SPR_CS_GREENHOUSE_SPRING : SPR_CS_GREENHOUSE_SNOW, gx, gy, tint);
    if (!spring) stove_smoke(gx, gy, t, rgb(200, 200, 210));
    van(398, 200 - 37, 0, tint, false);
    if (!spring) {
        snow(t, 110, 10, 1);
    } else {
        /* swallows back over the yard, butterflies in the flowers */
        for (int i = 0; i < 3; i++) {
            float bx = fmodf(t * (50 + i * 9) + i * 160, 600) - 60, by = 60 + i * 14 + sinf(t * 1.7f + i) * 10;
            gfx_spr_c(SPR_CS_CROW + ((int)(t * 10 + i) % 3), bx, by, rgb(40, 40, 60));
        }
        for (int i = 0; i < 6; i++) {
            float bx = 110 + hash(i) * 280 + sinf(t * 0.9f + i * 2) * 20, by = 180 + sinf(t * 1.3f + i) * 10;
            bool up = (int)(t * 12 + i) & 1;
            Color c = i % 2 ? rgb(255, 239, 168) : rgb(242, 166, 196);
            gfx_fill(floorf(bx), floorf(by), 1, 1, c);
            gfx_fill(floorf(bx) + (up ? -1 : 1), floorf(by) - (up ? 1 : 0), 1, 1, c);
        }
    }
    band(SPR_CS_WEEDS, 0, 214, spring ? rgb(60, 96, 50) : rgb(200, 200, 210));
}

static void sh_winter(float t) { spring_house(t, false); }
static void sh_spring(float t) { spring_house(t, true); }

/* the beans come up first; Theo names every single one, on price stickers from the stores */
static const char *const BEAN_NAMES[6] = {"BOB", "LUCKY", "ROSA", "MR BEAN", "SPROUT", "<3"};

static void sh_beans(float t) {
    gfx_spr_tile(SPR_CS_GLASSWALL, -fmodf(t * 2, 160), 24, VIEW_W + 160, 176, rgb(236, 226, 200));
    gfx_glow(140, 60, 120, rgb(255, 236, 190), 0.45f);
    band(SPR_CS_SOIL, 0, 172, rgb(220, 200, 180));
    for (int i = 0; i < 6; i++) {
        float x = 52 + i * 72 + hash(i) * 8;
        float stage = CLAMP((t - 0.6f - i * 0.75f) / 0.4f, 0.0f, 5.0f);
        gfx_spr_c(SPR_CS_SPROUT + (int)stage, x - 8, 176 - 25, TINT_NONE);
        /* a stick pushed in beside it, a name stuck on it */
        float tt = t - 2.2f - i * 0.75f;
        if (tt > 0) {
            float drop = 10 * (1 - ease(tt / 0.25f));
            float sx = x + 12, sy = 160 - drop;
            gfx_spr_c(SPR_CS_TAG, sx, sy, TINT_NONE);
            float w = gfx_text_w(FONT_SMALL, BEAN_NAMES[i]) + 7;
            gfx_sticker(sx + 3 - w / 2, sy - 12, w, 11, i == 5 ? COL_TAG : COL_YELLOW);
            gfx_text(FONT_SMALL, BEAN_NAMES[i], sx + 3 - w / 2 + 4, sy - 10, i == 5 ? COL_WHITE : COL_BLACK, 0);
        }
    }
    for (int i = 0; i < 26; i++) {
        float x = fmodf(hash(i) * 480 + t * (3 + hash(i * 3) * 4), 480);
        float y = 40 + hash(i * 5) * 130 + sinf(t * 0.7f + i) * 5;
        gfx_fill(floorf(x), floorf(y), 1, 1, rgba(255, 246, 220, (Uint8)(90 + hash(i * 7) * 120)));
    }
}

static void cue_beans(float t) {
    for (int i = 0; i < 6; i++)
        if (at(2.2f + i * 0.75f)) audio_play(SFX_LIST_TICK, 0.35f, -0.6f + i * 0.24f, 1.0f + i * 0.06f);
}

/* you still dream about the aisles sometimes */
static void sh_dream(float t) {
    aisle(t, rgb(70, 84, 124), rgb(150, 176, 230), CLAMP((t - 1.5f) / 2.0f, 0.0f, 1.0f) * 0.8f);
    /* the strip lights try to come on and can't */
    if (hash((int)(t * 15)) < 0.12f) gfx_fill(0, 24, VIEW_W, 208, rgba(170, 200, 255, 26));
    gfx_fill(0, 24, VIEW_W, 208, rgba(8, 10, 22, (Uint8)(70 + 25 * sinf(t * 0.8f))));
}

/* but you don't have to go shopping anymore */
static void sh_rest(float t) {
    sky(7, 198);
    gfx_glow(424, 168, 110, rgb(255, 170, 80), 0.5f);
    gfx_spr(SPR_CS_SUN, 412, 152);
    band(SPR_CS_CITY, 0, 142, rgb(150, 98, 96));
    band(SPR_CS_HILLS, 0, 156, rgb(120, 80, 70));
    Color tint = rgb(236, 186, 150);
    band(SPR_CS_GROUND + 2, 0, 198, tint);
    const float gx = 10, gy = 200 - 111;
    static const Folk inside[8] = {{40, 4, 0}, {70, 1, 0}, {188, 0, 0}, {-1}};
    House h = {{0.5f, 0.6f, 0.6f, 0.5f}, 0, -1, inside, tint, rgba(40, 24, 18, 255)};
    greenhouse(t, gx, gy, &h);
    gfx_spr_c(SPR_CS_GREENHOUSE_SPRING, gx, gy, tint);
    string_lights(gx, gy, t, -10);
    /* the van hasn't moved in a while */
    van(404, 200 - 37, 0, rgb(220, 170, 140), false);
    gfx_spr_src(SPR_CS_WEEDS + 1, 0, 0, 96, 40, 398, 172, rgb(96, 112, 56));
    /* Theo, running circles in the yard */
    float kx = 300 + sinf(t * 0.8f) * 70;
    gfx_spr_ex(SPR_CS_FOLK + 3, kx, 182 - floorf(fabsf(sinf(t * 6)) * 3), 0, cosf(t * 0.8f) < 0 ? -1 : 1, 1, rgb(60, 34, 26));
    gfx_spr_c(SPR_CS_TOMATO + ((int)(t * 1.1f) & 1), 384, 200 - 64, tint);
    gfx_spr_c(SPR_CS_HERO_SIT + ((int)(t * 1.4f) & 1), 340, 200 - 47, tint);
    /* fireflies come out as the sun goes */
    float ff = CLAMP((t - 2.0f) / 3.0f, 0.0f, 1.0f);
    for (int i = 0; i < 18; i++) {
        float x = fmodf(hash(i) * 480 + sinf(t * 0.4f + i) * 30 + t * 3, 480);
        float y = 150 + hash(i * 3) * 70 + sinf(t * 0.9f + i * 2) * 8;
        float a = ff * (0.5f + 0.5f * sinf(t * (1.5f + hash(i) * 2) + i * 3));
        if (a < 0.1f) continue;
        gfx_fill(floorf(x), floorf(y), 1, 1, rgba(255, 239, 168, (Uint8)(a * 255)));
        gfx_glow(x, y, 6, rgb(220, 230, 120), a * 0.5f);
    }
    band(SPR_CS_WEEDS + ((int)(t * 1.2f) & 1), t * 0.5f, 214, rgb(50, 46, 30));
}

static void build_ending(void) {
    Shot *s = shot(sh_crown, 7.0f, TR_BLACK, 1.6f);
    s->cue = cue_crown;
    s->fire = 0.5f;
    captions(s, ENDING_PAGES[0], "\n\n");
    drive_look = LOOK_SUNSET;
    s = shot(sh_dash, 9.0f, TR_BLACK, 1.4f);
    s->cue = cue_dash;
    s->wind = 0.25f;
    captions(s, ENDING_PAGES[1], "\n\n");
    /* "Spring comes late that year. / But it comes." - the second line waits for the spring */
    const char *spring = ENDING_PAGES[2], *nl = strchr(spring, '\n');
    s = shot(sh_winter, 5.0f, TR_DISSOLVE, 1.6f);
    s->wind = 0.7f;
    caption(s, spring, nl ? (int)(nl - spring) : (int)strlen(spring));
    s = shot(sh_spring, 6.5f, TR_WIPE, 3.0f);
    s->wind = 0.15f;
    if (nl) captions(s, nl + 1, "\n\n");
    s = shot(sh_beans, 9.0f, TR_DISSOLVE, 1.6f);
    s->cue = cue_beans;
    captions(s, ENDING_PAGES[3], "\n\n");
    s = shot(sh_dream, 6.5f, TR_BLACK, 2.0f);
    s->hum = 0.5f;
    captions(s, ENDING_PAGES[4], "\n\n");
    s = shot(sh_rest, 11.0f, TR_BLACK, 2.4f);
    s->wind = 0.15f;
    s->open = true;
    captions(s, ENDING_PAGES[5], "\n\n");
}

/* ======================================================================= flow */
static void finish(void) {
    nshots = 0;
    switch (cut_id) {
    case CUT_INTRO: scene_set(SC_HUB); break;
    case CUT_DRIVE: start_level(); break;
    case CUT_HOME: scene_set(SC_SAFEHOUSE); break;
    case CUT_ENDING: scene_set(SC_ENDING); break;
    default: scene_set(SC_TITLE); break;
    }
}

void cutscene_start(CutsceneId id, int level) {
    cut_id = id;
    lv = CLAMP(level, 0, NUM_LEVELS - 1);
    nshots = 0;
    cur = 0;
    prev = -1;
    vt = pvt = ct = pct = 0;
    out_t = -1;
    bars = 1;
    title = NULL;
    switch (id) {
    case CUT_DRIVE: build_drive(); title = drive_title; break;
    case CUT_HOME: build_home(); title = "THE GREENHOUSE"; break;
    case CUT_ENDING: build_ending(); break;
    default: build_intro(); break;
    }
    scene_set(SC_CUTSCENE);
    /* the drive has no score: the engine, the weather, then the store's own music */
    static const MusicId score[CUT_COUNT] = {MUS_STORY, MUS_NONE, MUS_SAFEHOUSE, MUS_ENDING};
    audio_music(score[id]);
}

void cutscene_seek(float t) {
    while (cur < nshots - 1 && t >= shots[cur].dur) {
        t -= shots[cur].dur;
        cur++;
    }
    prev = -1;
    vt = ct = t;
}

static void advance(void) {
    if (cur + 1 < nshots) {
        prev = cur;
        pvt = vt;
        pct = ct;
        cur++;
        vt = ct = 0;
    } else if (out_t < 0) {
        out_t = 0;
    }
}

void cutscene_update(float dt) {
    if (nshots == 0) build_intro();
    Shot *s = &shots[cur];
    cue_t0 = vt;
    vt += dt;
    pvt += dt;
    pct += dt;
    ct += dt;
    cue_t1 = vt;
    if (s->cue && out_t < 0) s->cue(vt);
    if (prev >= 0 && vt >= shots[cur].tr_dur) prev = -1;

    if (out_t >= 0) {
        out_t += dt;
        if (out_t >= OUT_DUR) { finish(); return; }
    } else if (IN.pressed[ACT_BACK]) {
        out_t = OUT_DUR * 0.4f;   /* skip it all: a quicker fade */
    } else if (IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_INTERACT] || IN.click) {
        /* finish the caption being written, else on to the next caption or shot */
        float next = s->dur;
        for (int i = 0; i < s->ncap; i++) {
            float full = s->cap_at[i] + (6.0f + strlen(s->cap[i])) / CAP_CPS;
            if (ct >= s->cap_at[i] && ct < full) { next = full; break; }
            if (s->cap_at[i] > ct) { next = s->cap_at[i]; break; }
        }
        ct = MAXF(ct, next);
    }
    if (out_t < 0 && ct >= s->dur) advance();

    /* ambience follows the picture */
    float k = prev >= 0 ? ease(vt / MAXF(0.01f, shots[cur].tr_dur)) : 1;
    const Shot *a = prev >= 0 ? &shots[prev] : &shots[cur], *b = &shots[cur];
    float fade = out_t >= 0 ? 1.0f - out_t / OUT_DUR : 1.0f;
    for (int l = 0; l < LOOP_COUNT; l++) {
        float v = 0;
        if (l == LOOP_WIND) v = lerpf(a->wind, b->wind, k);
        if (l == LOOP_RAIN) v = lerpf(a->rain, b->rain, k);
        if (l == LOOP_FIRE) v = lerpf(a->fire, b->fire, k);
        if (l == LOOP_HUM) v = lerpf(a->hum, b->hum, k);
        audio_loop((LoopId)l, v * fade, 1);
    }
    /* the letterbox opens once the last words have been read */
    if (b->open && b->ncap && ct > b->cap_at[b->ncap - 1] + b->cap_len[b->ncap - 1]) bars = MAXF(0, bars - dt * 0.3f);
}

/* ----------------------------------------------------------------- drawing */
static void draw_caption(const Shot *s, float clock, float fade) {
    for (int i = 0; i < s->ncap; i++) {
        float lt = clock - s->cap_at[i];
        if (lt < 0 || lt > s->cap_len[i]) continue;
        float a = CLAMP((s->cap_len[i] - lt) / 0.4f, 0.0f, 1.0f) * fade;
        int lines = 1;
        for (const char *p = s->cap[i]; *p; p++) lines += *p == '\n';
        float y = VIEW_H - BAR_B * bars + (BAR_B - (lines * 12 - 3)) / 2.0f;
        gfx_text_reveal(FONT_SMALL, s->cap[i], VIEW_W / 2, floorf(y), tinta(COL_RECEIPT, a), lt * CAP_CPS, 12);
    }
}

void cutscene_draw(void) {
    if (nshots == 0) build_intro();
    G.cam_x = WORLD_RT_W / 2;
    G.cam_y = WORLD_RT_H / 2;
    G.cam_angle = 0;
    G.cam_zoom = 1;
    const Shot *s = &shots[cur];
    float p = prev >= 0 ? CLAMP(vt / MAXF(0.01f, s->tr_dur), 0.0f, 1.0f) : 1;
    gfx_begin_cut(0);
    s->draw(vt);
    if (prev >= 0) {
        gfx_begin_cut(1);
        shots[prev].draw(pvt);
    }
    /* first shot of all: it rises out of the dark */
    bool from_black = cur == 0 && s->tr != TR_CUT && p < 1;
    bool mixing = prev >= 0 && s->tr != TR_BLACK;
    if (mixing) gfx_cut_mask(0, s->tr == TR_CUT ? 1.0f : p, s->tr == TR_WIPE ? 0.35f : 0);

    /* the composite goes through the world layer, so it gets the film grade and vignette */
    gfx_begin_world();
    const float ox = (WORLD_RT_W - VIEW_W) / 2, oy = (WORLD_RT_H - VIEW_H) / 2;
    if (mixing) {
        gfx_draw_cut(1, ox, oy);
        gfx_draw_cut(0, ox, oy);
    } else if (prev >= 0 || from_black) {
        if (from_black || p >= 0.5f) {
            float k = from_black ? p : (p - 0.5f) * 2;
            gfx_draw_cut(0, ox, oy);
            gfx_dither(ox, oy, VIEW_W, VIEW_H, 1.0f - k, COL_BLACK);
        } else {
            gfx_draw_cut(1, ox, oy);
            gfx_dither(ox, oy, VIEW_W, VIEW_H, p * 2, COL_BLACK);
        }
    } else {
        gfx_draw_cut(0, ox, oy);
    }
    if (out_t >= 0) gfx_dither(ox, oy, VIEW_W, VIEW_H, out_t / OUT_DUR, COL_BLACK);

    gfx_begin_hud();
    float bt = floorf(BAR_T * bars), bb = floorf(BAR_B * bars);
    gfx_fill(0, 0, VIEW_W, bt, COL_BLACK);
    gfx_fill(0, VIEW_H - bb, VIEW_W, bb, COL_BLACK);
    if (out_t < 0) {
        /* a caption cut short by a key press fades out over the first half of the transition */
        if (prev >= 0 && vt < s->tr_dur * 0.5f) draw_caption(&shots[prev], pct, 1.0f - vt / (s->tr_dur * 0.5f));
        else draw_caption(s, ct, 1);
    }
    if (bars > 0.5f) {
        Color dim = rgba(110, 104, 116, 255);
        gfx_text(FONT_SMALL, ctl("ESC skip", "B skip"), VIEW_W - 8, floorf(bt / 2 - 4), dim, TXT_RIGHT);
        if (title) {
            float a = (cur > 0 ? 1.0f : CLAMP((vt - 0.6f) / 0.8f, 0.0f, 1.0f)) * (out_t >= 0 ? 1.0f - out_t / OUT_DUR : 1.0f);
            gfx_text(FONT_SMALL, title, 10, floorf(bt / 2 - 4), tinta(COL_KRAFT, a), 0);
        }
    }
}
