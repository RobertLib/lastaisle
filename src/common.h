/* LAST AISLE - shared core definitions */
#ifndef COMMON_H
#define COMMON_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "atlas.h"

#define GAME_TITLE   "LAST AISLE"
#define GAME_VERSION "1.0"

/* Internal resolution: everything is pixel art rendered at this size and scaled up. */
#define VIEW_W 480
#define VIEW_H 270
/* World target is larger so the camera can sway/rotate without exposing edges. */
#define WORLD_RT_W 528
#define WORLD_RT_H 320
#define TILE 16

#define PI_F 3.14159265358979f
#define DEG2RAD (PI_F / 180.0f)
#define RAD2DEG (180.0f / PI_F)

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MINF(a, b) ((a) < (b) ? (a) : (b))
#define MAXF(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

typedef struct { float x, y; } V2;
typedef struct { Uint8 r, g, b, a; } Color;

static inline V2 v2(float x, float y) { V2 v = {x, y}; return v; }
static inline V2 v2_add(V2 a, V2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline V2 v2_sub(V2 a, V2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline V2 v2_scale(V2 a, float s) { return v2(a.x * s, a.y * s); }
static inline float v2_dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
static inline float v2_len(V2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static inline float v2_len2(V2 a) { return a.x * a.x + a.y * a.y; }
static inline float v2_dist(V2 a, V2 b) { return v2_len(v2_sub(a, b)); }
static inline float v2_dist2(V2 a, V2 b) { return v2_len2(v2_sub(a, b)); }
static inline V2 v2_norm(V2 a) {
    float l = v2_len(a);
    return l > 1e-6f ? v2_scale(a, 1.0f / l) : v2(0, 0);
}
static inline V2 v2_angle(float a) { return v2(cosf(a), sinf(a)); }
static inline float v2_to_angle(V2 a) { return atan2f(a.y, a.x); }
static inline V2 v2_rot(V2 v, float a) {
    float c = cosf(a), s = sinf(a);
    return v2(v.x * c - v.y * s, v.x * s + v.y * c);
}
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float approachf(float v, float target, float step) {
    if (v < target) return MINF(v + step, target);
    return MAXF(v - step, target);
}
/* wrap angle to -PI..PI */
static inline float wrap_angle(float a) {
    while (a > PI_F) a -= 2 * PI_F;
    while (a < -PI_F) a += 2 * PI_F;
    return a;
}
static inline float angle_diff(float a, float b) { return wrap_angle(b - a); }
static inline float lerp_angle(float a, float b, float t) { return a + angle_diff(a, b) * t; }
/* frame-rate independent exponential smoothing factor */
static inline float smooth_k(float rate, float dt) { return 1.0f - expf(-rate * dt); }

static inline Color rgba(int r, int g, int b, int a) {
    Color c = {(Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a};
    return c;
}
static inline Color rgb(int r, int g, int b) { return rgba(r, g, b, 255); }
static inline Color color_lerp(Color a, Color b, float t) {
    return rgba((int)lerpf(a.r, b.r, t), (int)lerpf(a.g, b.g, t), (int)lerpf(a.b, b.b, t),
                (int)lerpf(a.a, b.a, t));
}

/* Hotline-Miami flavoured accent colours (match art/palette.txt) */
#define TINT_NONE   rgb(255, 255, 255)
#define COL_WHITE   rgb(251, 248, 242)
#define COL_BLACK   rgb(11, 10, 16)
#define COL_PINK    rgb(255, 95, 149)
#define COL_MAGENTA rgb(192, 29, 98)
#define COL_CYAN    rgb(98, 236, 208)
#define COL_YELLOW  rgb(255, 212, 71)
#define COL_ORANGE  rgb(255, 140, 46)
#define COL_RED     rgb(232, 41, 63)
#define COL_BLOOD   rgb(168, 21, 46)
#define COL_GREEN   rgb(162, 211, 76)
#define COL_GREY    rgb(138, 130, 148)
#define COL_DARK    rgb(26, 23, 36)
#define COL_PAPER   rgb(227, 215, 188)
#define COL_INK     rgb(30, 61, 128)

/* ---------------------------------------------------------------- RNG */
typedef struct { uint64_t s; } Rng;
static inline uint32_t rng_u32(Rng *r) {
    r->s ^= r->s << 13;
    r->s ^= r->s >> 7;
    r->s ^= r->s << 17;
    return (uint32_t)(r->s >> 11);
}
static inline void rng_seed(Rng *r, uint64_t seed) {
    r->s = seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
    if (!r->s) r->s = 1;
    for (int i = 0; i < 4; i++) rng_u32(r);
}
static inline float rng_float(Rng *r) { return (rng_u32(r) & 0xFFFFFF) / 16777216.0f; }
static inline int rng_int(Rng *r, int n) { return n <= 0 ? 0 : (int)(rng_u32(r) % (uint32_t)n); }
static inline int rng_range(Rng *r, int lo, int hi) { return lo + rng_int(r, hi - lo + 1); }
static inline float rng_rangef(Rng *r, float lo, float hi) { return lo + (hi - lo) * rng_float(r); }
static inline bool rng_chance(Rng *r, float p) { return rng_float(r) < p; }

extern Rng g_rng;      /* gameplay / fx randomness */
#define frand() rng_float(&g_rng)
#define frange(a, b) rng_rangef(&g_rng, (a), (b))
#define irange(a, b) rng_range(&g_rng, (a), (b))
#define chance(p) rng_chance(&g_rng, (p))

#endif
