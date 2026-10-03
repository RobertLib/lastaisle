/* LAST AISLE - rendering */
#include "gfx.h"

#define GRAIN_N 97  /* odd size so the tiling never lines up with the 480x270 grid */

Gfx G;

static SDL_Texture *make_target(int w, int h) {
    SDL_Texture *t = SDL_CreateTexture(G.ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    return t;
}

static SDL_Texture *surface_tex(SDL_Surface *s, SDL_ScaleMode mode) {
    SDL_Texture *t = SDL_CreateTextureFromSurface(G.ren, s);
    SDL_DestroySurface(s);
    if (t) SDL_SetTextureScaleMode(t, mode);
    return t;
}

/* soft radial light, slightly posterised so it sits well with the pixel art */
static SDL_Texture *make_glow(void) {
    const int N = 64;
    SDL_Surface *s = SDL_CreateSurface(N, N, SDL_PIXELFORMAT_RGBA32);
    Uint8 *px = (Uint8 *)s->pixels;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float dx = (x + 0.5f) / N * 2 - 1, dy = (y + 0.5f) / N * 2 - 1;
            float d = sqrtf(dx * dx + dy * dy);
            float v = CLAMP(1.0f - d, 0.0f, 1.0f);
            v = v * v * (3 - 2 * v);
            v = floorf(v * 10.0f + 0.5f) / 10.0f;
            Uint8 *p = px + y * s->pitch + x * 4;
            p[0] = p[1] = p[2] = 255;
            p[3] = (Uint8)(v * 255);
        }
    SDL_Texture *t = surface_tex(s, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    return t;
}

/* flashlight cone pointing right, origin at left-middle */
static SDL_Texture *make_cone(void) {
    const int W = 64, H = 64;
    SDL_Surface *s = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGBA32);
    Uint8 *px = (Uint8 *)s->pixels;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float fx = (x + 0.5f) / W, fy = ((y + 0.5f) / H) * 2 - 1;
            float ang = fabsf(atan2f(fy * 0.5f, fx));
            float a = CLAMP(1.0f - ang / 0.42f, 0.0f, 1.0f);
            float d = CLAMP(1.0f - fx, 0.0f, 1.0f);
            float v = a * (0.35f + 0.65f * d) * CLAMP(fx * 6.0f, 0.0f, 1.0f);
            v = floorf(v * 8.0f + 0.5f) / 8.0f;
            Uint8 *p = px + y * s->pitch + x * 4;
            p[0] = p[1] = p[2] = 255;
            p[3] = (Uint8)(CLAMP(v, 0.0f, 1.0f) * 255);
        }
    SDL_Texture *t = surface_tex(s, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    return t;
}

static SDL_Texture *make_vignette(void) {
    const int W = 160, H = 90;
    SDL_Surface *s = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGBA32);
    Uint8 *px = (Uint8 *)s->pixels;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = ((x + 0.5f) / W * 2 - 1), dy = ((y + 0.5f) / H * 2 - 1);
            float d = sqrtf(dx * dx * 0.8f + dy * dy);
            float v = CLAMP((d - 0.55f) / 0.75f, 0.0f, 1.0f);
            v = v * v;
            Uint8 *p = px + y * s->pitch + x * 4;
            p[0] = p[1] = p[2] = 0;
            p[3] = (Uint8)(v * 255);
        }
    return surface_tex(s, SDL_SCALEMODE_LINEAR);
}

/* film grain: sparse warm-light and dark specks, one speck per internal pixel */
static SDL_Texture *make_grain(void) {
    const int N = GRAIN_N;
    SDL_Surface *s = SDL_CreateSurface(N, N, SDL_PIXELFORMAT_RGBA32);
    Uint8 *px = (Uint8 *)s->pixels;
    Rng r;
    rng_seed(&r, 0x6A41);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            Uint8 *p = px + y * s->pitch + x * 4;
            float v = rng_float(&r);
            if (v < 0.10f) { p[0] = 255; p[1] = 236; p[2] = 204; p[3] = (Uint8)rng_range(&r, 8, 20); }
            else if (v < 0.32f) { p[0] = 20; p[1] = 12; p[2] = 6; p[3] = (Uint8)rng_range(&r, 10, 30); }
            else { p[0] = p[1] = p[2] = 0; p[3] = 0; }
        }
    return surface_tex(s, SDL_SCALEMODE_NEAREST);
}

/* ordered dither: level L (0..16) keeps the pixels whose Bayer rank is below L */
static const int BAYER4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static SDL_Texture *make_dither(void) {
    SDL_Surface *s = SDL_CreateSurface(17 * 4, 4, SDL_PIXELFORMAT_RGBA32);
    Uint8 *px = (Uint8 *)s->pixels;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 17 * 4; x++) {
            Uint8 *p = px + y * s->pitch + x * 4;
            p[0] = p[1] = p[2] = 255;
            p[3] = BAYER4[y * 4 + x % 4] < x / 4 ? 255 : 0;
        }
    return surface_tex(s, SDL_SCALEMODE_NEAREST);
}

static SDL_Texture *make_white(void) {
    SDL_Surface *s = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
    memset(s->pixels, 255, (size_t)s->pitch * 4);
    return surface_tex(s, SDL_SCALEMODE_NEAREST);
}

static SDL_Surface *load_atlas(void) {
    char path[1024];
    const char *base = SDL_GetBasePath();
    const char *tries[] = {"assets/atlas.png", "../assets/atlas.png", "../Resources/assets/atlas.png"};
    for (int i = 0; i < ARRAY_LEN(tries); i++) {
        if (base) {
            SDL_snprintf(path, sizeof path, "%s%s", base, tries[i]);
            SDL_Surface *s = SDL_LoadPNG(path);
            if (s) return s;
        }
        SDL_Surface *s = SDL_LoadPNG(tries[i]);
        if (s) return s;
    }
    return NULL;
}

bool gfx_init(SDL_Window *win, SDL_Renderer *ren) {
    memset(&G, 0, sizeof G);
    G.win = win;
    G.ren = ren;
    G.cam_zoom = 1.0f;
    G.grain = true;
    SDL_Surface *s = load_atlas();
    if (!s) {
        SDL_Log("Cannot load assets/atlas.png: %s", SDL_GetError());
        return false;
    }
    /* window icon: the protagonist portrait, straight from the atlas */
    {
        const AtlasSprite *a = &g_atlas[SPR_UI_PORTRAIT];
        SDL_Surface *icon = SDL_CreateSurface(a->w, a->h, SDL_PIXELFORMAT_RGBA32);
        if (icon) {
            SDL_Rect src = {a->x, a->y, a->w, a->h};
            SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
            SDL_BlitSurface(s, &src, icon, NULL);
            SDL_SetWindowIcon(win, icon);
            SDL_DestroySurface(icon);
        }
    }
    G.atlas = surface_tex(s, SDL_SCALEMODE_NEAREST);
    if (!G.atlas) return false;
    SDL_SetTextureBlendMode(G.atlas, SDL_BLENDMODE_BLEND);

    G.rt_world = make_target(WORLD_RT_W, WORLD_RT_H);
    G.rt_light = make_target(WORLD_RT_W, WORLD_RT_H);
    G.rt_hud = make_target(VIEW_W, VIEW_H);
    if (!G.rt_world || !G.rt_light || !G.rt_hud) return false;
    SDL_SetTextureScaleMode(G.rt_world, SDL_SCALEMODE_PIXELART);
    SDL_SetTextureScaleMode(G.rt_hud, SDL_SCALEMODE_PIXELART);
    SDL_SetTextureBlendMode(G.rt_light, SDL_BLENDMODE_MOD);
    SDL_SetTextureBlendMode(G.rt_hud, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(G.rt_world, SDL_BLENDMODE_BLEND);

    G.tex_glow = make_glow();
    G.tex_cone = make_cone();
    G.tex_vignette = make_vignette();
    G.tex_grain = make_grain();
    G.tex_white = make_white();
    G.tex_dither = make_dither();
    for (int i = 0; i < 2; i++) {
        G.rt_cut[i] = make_target(VIEW_W, VIEW_H);
        if (!G.rt_cut[i]) return false;
        SDL_SetTextureBlendMode(G.rt_cut[i], SDL_BLENDMODE_BLEND);
    }
    /* dst.alpha *= src.alpha, colour untouched: stencils a dither pattern into a shot */
    G.bm_mask = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                           SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    if (!G.tex_dither || !SDL_SetTextureBlendMode(G.tex_dither, G.bm_mask)) G.bm_mask = 0;
    if (G.tex_dither) SDL_SetTextureBlendMode(G.tex_dither, SDL_BLENDMODE_BLEND);
    gfx_resize();
    return true;
}

void gfx_shutdown(void) {
    SDL_Texture *ts[] = {G.atlas, G.rt_world, G.rt_light, G.rt_hud, G.tex_glow, G.tex_cone, G.tex_vignette,
                         G.tex_grain, G.tex_white, G.tex_dither, G.rt_cut[0], G.rt_cut[1]};
    for (int i = 0; i < ARRAY_LEN(ts); i++)
        if (ts[i]) SDL_DestroyTexture(ts[i]);
}

void gfx_resize(void) {
    SDL_GetRenderOutputSize(G.ren, &G.win_w, &G.win_h);
    float sx = (float)G.win_w / VIEW_W, sy = (float)G.win_h / VIEW_H;
    G.scale = MINF(sx, sy);
    /* prefer integer scale when it costs little space: crisper pixels */
    float is = floorf(G.scale);
    if (is >= 2 && G.scale - is < 0.18f) G.scale = is;
    G.view.w = VIEW_W * G.scale;
    G.view.h = VIEW_H * G.scale;
    G.view.x = floorf((G.win_w - G.view.w) * 0.5f);
    G.view.y = floorf((G.win_h - G.view.h) * 0.5f);
}

static float pixel_density(void) {
    int ww, wh;
    SDL_GetWindowSize(G.win, &ww, &wh);
    return ww > 0 ? (float)G.win_w / ww : 1.0f;
}

V2 gfx_screen_to_view(float sx, float sy) {
    float d = pixel_density();
    sx *= d;
    sy *= d;
    return v2((sx - G.view.x) / G.scale, (sy - G.view.y) / G.scale);
}

V2 gfx_screen_to_world(float sx, float sy) {
    float d = pixel_density();
    sx *= d;
    sy *= d;
    float cx = G.view.x + G.view.w * 0.5f, cy = G.view.y + G.view.h * 0.5f;
    V2 r = v2((sx - cx) / (G.scale * G.cam_zoom), (sy - cy) / (G.scale * G.cam_zoom));
    r = v2_rot(r, -G.cam_angle);
    return v2(G.cam_x + r.x, G.cam_y + r.y);
}

V2 gfx_world_to_view(V2 w) {
    V2 r = v2_sub(w, v2(G.cam_x, G.cam_y));
    r = v2_rot(r, G.cam_angle);
    r = v2_scale(r, G.cam_zoom);
    return v2(VIEW_W * 0.5f + r.x, VIEW_H * 0.5f + r.y);
}

/* ------------------------------------------------------------------ targets */
void gfx_begin_world(void) {
    G.cam_ox = (int)floorf(G.cam_x) - WORLD_RT_W / 2;
    G.cam_oy = (int)floorf(G.cam_y) - WORLD_RT_H / 2;
    SDL_SetRenderTarget(G.ren, G.rt_world);
    SDL_SetRenderDrawColor(G.ren, 11, 10, 16, 255);
    SDL_RenderClear(G.ren);
    G.off_x = (float)-G.cam_ox;
    G.off_y = (float)-G.cam_oy;
}

void gfx_begin_light(Color amb) {
    SDL_SetRenderTarget(G.ren, G.rt_light);
    SDL_SetRenderDrawColor(G.ren, amb.r, amb.g, amb.b, 255);
    SDL_RenderClear(G.ren);
}

void gfx_end_light(void) {
    SDL_SetRenderTarget(G.ren, G.rt_world);
    SDL_RenderTexture(G.ren, G.rt_light, NULL, NULL);
}

void gfx_begin_hud(void) {
    SDL_SetRenderTarget(G.ren, G.rt_hud);
    SDL_SetRenderDrawColor(G.ren, 0, 0, 0, 0);
    SDL_RenderClear(G.ren);
    G.off_x = G.off_y = 0;
}

void gfx_begin_cut(int i) {
    SDL_SetRenderTarget(G.ren, G.rt_cut[i]);
    SDL_SetRenderDrawColor(G.ren, 11, 10, 16, 255);
    SDL_RenderClear(G.ren);
    G.off_x = G.off_y = 0;
}

static void dither_cell(int level, SDL_FRect *src) {
    *src = (SDL_FRect){(float)(CLAMP(level, 0, 16) * 4), 0, 4, 4};
}

void gfx_cut_mask(int i, float level, float wipe) {
    if (level >= 1.0f && wipe <= 0) return;
    SDL_SetRenderTarget(G.ren, G.rt_cut[i]);
    if (!G.bm_mask) {
        /* no custom blending on this renderer: a plain cross-fade */
        SDL_SetTextureAlphaMod(G.rt_cut[i], (Uint8)(CLAMP(level, 0.0f, 1.0f) * 255));
        return;
    }
    SDL_SetTextureAlphaMod(G.rt_cut[i], 255);
    SDL_SetTextureBlendMode(G.tex_dither, G.bm_mask);
    SDL_SetTextureColorMod(G.tex_dither, 255, 255, 255);
    SDL_SetTextureAlphaMod(G.tex_dither, 255);
    SDL_FRect src;
    if (wipe <= 0) {
        dither_cell((int)floorf(level * 16 + 0.5f), &src);
        SDL_FRect dst = {0, 0, VIEW_W, VIEW_H};
        SDL_RenderTextureTiled(G.ren, G.tex_dither, &src, 1.0f, &dst);
    } else {
        /* a soft edge `wipe` screens wide travels across: fully in on its left, untouched on its right */
        for (int x = 0; x < VIEW_W; x += 4) {
            float k = CLAMP((level * (1 + wipe) - (float)x / VIEW_W) / wipe, 0.0f, 1.0f);
            dither_cell((int)floorf(k * 16 + 0.5f), &src);
            SDL_FRect dst = {(float)x, 0, 4, VIEW_H};
            SDL_RenderTextureTiled(G.ren, G.tex_dither, &src, 1.0f, &dst);
        }
    }
    SDL_SetTextureBlendMode(G.tex_dither, SDL_BLENDMODE_BLEND);
}

void gfx_draw_cut(int i, float x, float y) {
    SDL_FRect dst = {floorf(x + G.off_x), floorf(y + G.off_y), VIEW_W, VIEW_H};
    SDL_RenderTexture(G.ren, G.rt_cut[i], NULL, &dst);
    SDL_SetTextureAlphaMod(G.rt_cut[i], 255);
}

static void overlay_fill(Color c) {
    SDL_SetRenderDrawBlendMode(G.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(G.ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(G.ren, &G.view);
}

void gfx_present(void) {
    SDL_SetRenderTarget(G.ren, NULL);
    SDL_SetRenderDrawColor(G.ren, 0, 0, 0, 255);
    SDL_RenderClear(G.ren);
    SDL_Rect clip = {(int)G.view.x, (int)G.view.y, (int)G.view.w, (int)G.view.h};
    SDL_SetRenderClipRect(G.ren, &clip);

    /* world layer: rotated about the camera centre, sub-pixel smooth scrolling */
    float s = G.scale * G.cam_zoom;
    float fx = G.cam_x - floorf(G.cam_x), fy = G.cam_y - floorf(G.cam_y);
    float cx = G.view.x + G.view.w * 0.5f, cy = G.view.y + G.view.h * 0.5f;
    SDL_FPoint centre = {(WORLD_RT_W * 0.5f + fx) * s, (WORLD_RT_H * 0.5f + fy) * s};
    SDL_FRect dst = {cx - centre.x, cy - centre.y, WORLD_RT_W * s, WORLD_RT_H * s};
    double ang = G.cam_angle * RAD2DEG;
    SDL_SetTextureColorMod(G.rt_world, 255, 255, 255);
    SDL_SetTextureAlphaMod(G.rt_world, 255);
    SDL_SetTextureBlendMode(G.rt_world, SDL_BLENDMODE_BLEND);
    SDL_RenderTextureRotated(G.ren, G.rt_world, NULL, &dst, ang, &centre, SDL_FLIP_NONE);
    if (G.impact > 0.01f) {
        /* contrast punch: multiplying the frame by itself crushes the mids and burns the highlights */
        SDL_SetTextureBlendMode(G.rt_world, SDL_BLENDMODE_MUL);
        SDL_SetTextureAlphaMod(G.rt_world, (Uint8)CLAMP(G.impact * 190, 0, 255));
        SDL_RenderTextureRotated(G.ren, G.rt_world, NULL, &dst, ang, &centre, SDL_FLIP_NONE);
        SDL_SetTextureAlphaMod(G.rt_world, 255);
        SDL_SetTextureBlendMode(G.rt_world, SDL_BLENDMODE_BLEND);
    }
    /* sun-bleached grade: warm the frame and lift the blacks like an old print */
    SDL_SetRenderDrawBlendMode(G.ren, SDL_BLENDMODE_MUL);
    SDL_SetRenderDrawColor(G.ren, 255, 243, 222, 255);
    SDL_RenderFillRect(G.ren, &G.view);
    SDL_SetRenderDrawBlendMode(G.ren, SDL_BLENDMODE_ADD);
    SDL_SetRenderDrawColor(G.ren, 16, 12, 7, 255);
    SDL_RenderFillRect(G.ren, &G.view);

    /* vignette + damage pulses */
    float vig = CLAMP(0.55f + G.vignette, 0.0f, 1.0f);
    SDL_SetTextureAlphaMod(G.tex_vignette, (Uint8)(vig * 255));
    SDL_SetTextureColorMod(G.tex_vignette, 0, 0, 0);
    SDL_RenderTexture(G.ren, G.tex_vignette, NULL, &G.view);
    if (G.red_pulse > 0.01f) {
        SDL_SetTextureColorMod(G.tex_vignette, 200, 10, 40);
        SDL_SetTextureAlphaMod(G.tex_vignette, (Uint8)(CLAMP(G.red_pulse, 0, 1) * 255));
        SDL_RenderTexture(G.ren, G.tex_vignette, NULL, &G.view);
        SDL_SetTextureColorMod(G.tex_vignette, 0, 0, 0);
    }
    if (G.flash > 0.01f) {
        Color c = G.flash_col;
        c.a = (Uint8)CLAMP(G.flash * 255, 0, 255);
        overlay_fill(c);
    }

    /* HUD */
    SDL_RenderTexture(G.ren, G.rt_hud, NULL, &G.view);

    if (G.grain) {
        /* re-roll the grain at film rate, not every frame */
        uint32_t f = (uint32_t)(G.time * 24.0f) * 2654435761u;
        float tile = GRAIN_N * G.scale;
        float ox = (float)((f >> 8) % GRAIN_N) * G.scale, oy = (float)((f >> 20) % GRAIN_N) * G.scale;
        SDL_FRect d = {G.view.x - ox, G.view.y - oy, G.view.w + tile, G.view.h + tile};
        SDL_SetTextureBlendMode(G.tex_grain, SDL_BLENDMODE_BLEND);
        SDL_RenderTextureTiled(G.ren, G.tex_grain, NULL, G.scale, &d);
    }
    SDL_SetRenderClipRect(G.ren, NULL);
    if (G.capture_path) {
        SDL_Surface *shot = SDL_RenderReadPixels(G.ren, NULL);
        if (shot) {
            SDL_SavePNG(shot, G.capture_path);
            SDL_DestroySurface(shot);
            SDL_Log("saved %s", G.capture_path);
        }
        G.capture_path = NULL;
    }
    SDL_RenderPresent(G.ren);
}

/* ------------------------------------------------------------------ sprites */
static inline void tint(Color c) {
    SDL_SetTextureColorMod(G.atlas, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(G.atlas, c.a);
}

void gfx_spr(int id, float x, float y) { gfx_spr_c(id, x, y, TINT_NONE); }

void gfx_spr_c(int id, float x, float y, Color c) {
    if (id < 0 || id >= SPR_COUNT) return;
    const AtlasSprite *a = &g_atlas[id];
    SDL_FRect src = {a->x, a->y, a->w, a->h};
    SDL_FRect dst = {floorf(x + G.off_x - a->px + 0.5f), floorf(y + G.off_y - a->py + 0.5f), a->w, a->h};
    tint(c);
    SDL_RenderTexture(G.ren, G.atlas, &src, &dst);
}

void gfx_spr_ex(int id, float x, float y, float angle, float sx, float sy, Color c) {
    if (id < 0 || id >= SPR_COUNT) return;
    const AtlasSprite *a = &g_atlas[id];
    SDL_FRect src = {a->x, a->y, a->w, a->h};
    float asx = fabsf(sx), asy = fabsf(sy);
    float px = sx < 0 ? a->w - a->px : a->px;
    float py = sy < 0 ? a->h - a->py : a->py;
    SDL_FRect dst = {x + G.off_x - px * asx, y + G.off_y - py * asy, a->w * asx, a->h * asy};
    if (angle == 0.0f) {
        dst.x = floorf(dst.x + 0.5f);
        dst.y = floorf(dst.y + 0.5f);
    }
    SDL_FPoint centre = {px * asx, py * asy};
    int flip = (sx < 0 ? SDL_FLIP_HORIZONTAL : 0) | (sy < 0 ? SDL_FLIP_VERTICAL : 0);
    tint(c);
    SDL_RenderTextureRotated(G.ren, G.atlas, &src, &dst, angle * RAD2DEG, &centre, (SDL_FlipMode)flip);
}

void gfx_spr_src(int id, int sx, int sy, int sw, int sh, float dx, float dy, Color c) {
    if (id < 0 || id >= SPR_COUNT) return;
    const AtlasSprite *a = &g_atlas[id];
    SDL_FRect src = {a->x + sx, a->y + sy, sw, sh};
    SDL_FRect dst = {floorf(dx + G.off_x + 0.5f), floorf(dy + G.off_y + 0.5f), sw, sh};
    tint(c);
    SDL_RenderTexture(G.ren, G.atlas, &src, &dst);
}

void gfx_spr_stretch(int id, float x, float y, float w, float h, Color c) {
    if (id < 0 || id >= SPR_COUNT) return;
    const AtlasSprite *a = &g_atlas[id];
    SDL_FRect src = {a->x, a->y, a->w, a->h};
    SDL_FRect dst = {x + G.off_x, y + G.off_y, w, h};
    tint(c);
    SDL_RenderTexture(G.ren, G.atlas, &src, &dst);
}

void gfx_spr_tile(int id, float x, float y, float w, float h, Color c) {
    if (id < 0 || id >= SPR_COUNT || w <= 0 || h <= 0) return;
    const AtlasSprite *a = &g_atlas[id];
    SDL_FRect src = {a->x, a->y, a->w, a->h};
    SDL_FRect dst = {floorf(x + G.off_x + 0.5f), floorf(y + G.off_y + 0.5f), floorf(w), floorf(h)};
    tint(c);
    SDL_RenderTextureTiled(G.ren, G.atlas, &src, 1.0f, &dst);
}

void gfx_nine(int id, float x, float y, float w, float h, Color c) {
    if (id < 0 || id >= SPR_COUNT) return;
    const AtlasSprite *a = &g_atlas[id];
    const float k = 8;
    float sx[3] = {0, k, a->w - k}, sw[3] = {k, a->w - 2 * k, k};
    float sy[3] = {0, k, a->h - k}, sh[3] = {k, a->h - 2 * k, k};
    float dx[3] = {x, x + k, x + w - k}, dw[3] = {k, w - 2 * k, k};
    float dy[3] = {y, y + k, y + h - k}, dh[3] = {k, h - 2 * k, k};
    tint(c);
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            if (dw[i] <= 0 || dh[j] <= 0) continue;
            SDL_FRect src = {a->x + sx[i], a->y + sy[j], sw[i], sh[j]};
            SDL_FRect dst = {floorf(dx[i] + G.off_x), floorf(dy[j] + G.off_y), dw[i], dh[j]};
            if (i == 1 || j == 1)
                SDL_RenderTextureTiled(G.ren, G.atlas, &src, 1.0f, &dst);
            else
                SDL_RenderTexture(G.ren, G.atlas, &src, &dst);
        }
}

/* --------------------------------------------------------------- primitives */
void gfx_fill(float x, float y, float w, float h, Color c) {
    SDL_SetRenderDrawBlendMode(G.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(G.ren, c.r, c.g, c.b, c.a);
    SDL_FRect r = {floorf(x + G.off_x), floorf(y + G.off_y), w, h};
    SDL_RenderFillRect(G.ren, &r);
}

void gfx_rect(float x, float y, float w, float h, Color c) {
    SDL_SetRenderDrawBlendMode(G.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(G.ren, c.r, c.g, c.b, c.a);
    SDL_FRect r = {floorf(x + G.off_x), floorf(y + G.off_y), w, h};
    SDL_RenderRect(G.ren, &r);
}

void gfx_line(float x1, float y1, float x2, float y2, Color c) {
    SDL_SetRenderDrawBlendMode(G.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(G.ren, c.r, c.g, c.b, c.a);
    SDL_RenderLine(G.ren, floorf(x1 + G.off_x) + 0.5f, floorf(y1 + G.off_y) + 0.5f,
                   floorf(x2 + G.off_x) + 0.5f, floorf(y2 + G.off_y) + 0.5f);
}

void gfx_glow(float x, float y, float radius, Color c, float intensity) {
    float k = CLAMP(intensity, 0.0f, 1.0f);
    SDL_SetTextureColorMod(G.tex_glow, (Uint8)(c.r * k), (Uint8)(c.g * k), (Uint8)(c.b * k));
    SDL_FRect dst = {x + G.off_x - radius, y + G.off_y - radius, radius * 2, radius * 2};
    SDL_RenderTexture(G.ren, G.tex_glow, NULL, &dst);
}

void gfx_cone(float x, float y, float angle, float len, Color c, float intensity) {
    float k = CLAMP(intensity, 0.0f, 1.0f);
    SDL_SetTextureColorMod(G.tex_cone, (Uint8)(c.r * k), (Uint8)(c.g * k), (Uint8)(c.b * k));
    SDL_FRect dst = {x + G.off_x, y + G.off_y - len * 0.5f, len, len};
    SDL_FPoint centre = {0, len * 0.5f};
    SDL_RenderTextureRotated(G.ren, G.tex_cone, NULL, &dst, angle * RAD2DEG, &centre, SDL_FLIP_NONE);
}

void gfx_dither(float x, float y, float w, float h, float level, Color c) {
    int l = (int)floorf(CLAMP(level, 0.0f, 1.0f) * 16 + 0.5f);
    if (l <= 0 || w <= 0 || h <= 0) return;
    /* snap to the 4px grid so neighbouring fills share one pattern */
    float x0 = floorf((x + G.off_x) / 4) * 4, y0 = floorf((y + G.off_y) / 4) * 4;
    float x1 = ceilf((x + G.off_x + w) / 4) * 4, y1 = ceilf((y + G.off_y + h) / 4) * 4;
    SDL_FRect src, dst = {x0, y0, x1 - x0, y1 - y0};
    dither_cell(l, &src);
    SDL_SetTextureBlendMode(G.tex_dither, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(G.tex_dither, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(G.tex_dither, c.a);
    SDL_RenderTextureTiled(G.ren, G.tex_dither, &src, 1.0f, &dst);
}

/* ------------------------------------------------------------ store ephemera */
static Color shade(Color c, float k) {
    return rgba((int)CLAMP(c.r * k, 0, 255), (int)CLAMP(c.g * k, 0, 255), (int)CLAMP(c.b * k, 0, 255), c.a);
}

void gfx_sticker(float x, float y, float w, float h, Color bg) {
    x = floorf(x);
    y = floorf(y);
    Color ink = rgba(11, 10, 16, bg.a);
    gfx_fill(x + 2, y + h, w - 2, 1, rgba(0, 0, 0, (Uint8)(bg.a * 0.45f)));
    gfx_fill(x + w, y + 2, 1, h - 2, rgba(0, 0, 0, (Uint8)(bg.a * 0.45f)));
    gfx_fill(x + 1, y, w - 2, h, ink);
    gfx_fill(x, y + 1, w, h - 2, ink);
    gfx_fill(x + 1, y + 1, w - 2, h - 2, bg);
    gfx_fill(x + 2, y + 1, w - 4, 1, color_lerp(bg, rgba(255, 255, 255, bg.a), 0.35f));
    gfx_fill(x + 1, y + h - 2, w - 2, 1, shade(bg, 0.78f));
}

void gfx_marker(float x, float y, float w, float h, Color c) {
    x = floorf(x);
    y = floorf(y);
    gfx_fill(x, y, w, h, c);
    /* felt-tip ends: each row of the stroke starts and stops a little differently */
    for (int i = 0; i < (int)h; i++) {
        int l = (i * 5 + 1) % 4, r = (i * 3 + 2) % 4;
        gfx_fill(x - l, y + i, l, 1, c);
        gfx_fill(x + w, y + i, r, 1, c);
    }
    gfx_fill(x, y + h - 1, w, 1, shade(c, 0.85f));
}

void gfx_ring(float cx, float cy, float r, float thick, Color c) {
    int ir = (int)ceilf(r);
    for (int y = -ir; y <= ir; y++)
        for (int x = -ir; x <= ir; x++) {
            float d = sqrtf((x + 0.5f) * (x + 0.5f) + (y + 0.5f) * (y + 0.5f));
            if (d <= r && d > r - thick) gfx_fill(cx + x, cy + y, 1, 1, c);
        }
}

/* --------------------------------------------------------------------- text */
static int glyph_id(FontId f, int ch) {
    if (f == FONT_BIG) {
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch == '{') ch = '(';
        if (ch == '}') ch = ')';
        if (ch < 32 || ch > 95) ch = '?';
        return SPR_FONT_BIG + (ch - 32);
    }
    if (ch < 32 || ch > 126) ch = '?';
    return SPR_FONT_SMALL + (ch - 32);
}

static int glyph_adv(FontId f, int ch) {
    if (ch == ' ') return f == FONT_BIG ? 6 : 3;
    int id = glyph_id(f, ch);
    int ow = g_atlas[id].ow;
    if (ow <= 0) ow = f == FONT_BIG ? 8 : 4;
    return ow + 1;
}

int gfx_text_h(FontId f) { return f == FONT_BIG ? 12 : 9; }

static Color code_color(char k, Color base) {
    switch (k) {
    case 'w': return rgba(251, 248, 242, base.a);
    case 'y': return rgba(255, 212, 71, base.a);
    case 'p': return rgba(240, 74, 44, base.a);   /* price-tag red */
    case 'c': return rgba(222, 184, 135, base.a); /* cardboard */
    case 'r': return rgba(232, 41, 63, base.a);
    case 'g': return rgba(162, 211, 76, base.a);
    case 'k': return rgba(138, 130, 148, base.a);
    case 'o': return rgba(255, 140, 46, base.a);
    case 'b': return rgba(82, 200, 255, base.a);
    case 'd': return rgba(30, 61, 128, base.a);
    default: return base;
    }
}

int gfx_text_w(FontId f, const char *s) {
    int w = 0, best = 0;
    for (; *s; s++) {
        if (*s == '^' && s[1]) { s++; continue; }
        if (*s == '\n') { best = MAXF(best, w); w = 0; continue; }
        w += glyph_adv(f, (unsigned char)*s);
    }
    best = MAXF(best, w);
    return best > 0 ? best - 1 : 0;
}

static int text_line(FontId f, const char *s, int len, float x, float y, Color c, Color start, int flags, int *idx, int limit) {
    float cx = x;
    Color cur = start;
    Color shadow = rgba(11, 10, 16, c.a);
    int sh = f == FONT_BIG ? 2 : 1;
    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '^' && i + 1 < len) {
            i++;
            cur = s[i] == '0' ? c : code_color(s[i], c);
            continue;
        }
        if (limit >= 0 && *idx >= limit) break;
        (*idx)++;
        int adv = glyph_adv(f, ch);
        if (ch != ' ') {
            int id = glyph_id(f, ch);
            float gx = cx, gy = y;
            if (flags & TXT_SHAKE) { gx += (float)irange(-1, 1); gy += (float)irange(-1, 1); }
            Color col = cur;
            if (flags & TXT_OUTLINE) {
                for (int oy = -1; oy <= 1; oy++)
                    for (int ox = -1; ox <= 1; ox++)
                        if (ox || oy) gfx_spr_c(id, gx + ox, gy + oy, shadow);
            }
            if (flags & TXT_SHADOW) gfx_spr_c(id, gx + sh, gy + sh, shadow);
            if ((flags & TXT_OUTLINE) && (flags & TXT_SHADOW)) gfx_spr_c(id, gx + sh + 1, gy + sh + 1, shadow);
            gfx_spr_c(id, gx, gy, col);
        }
        cx += adv;
    }
    return (int)(cx - x);
}

int gfx_text(FontId f, const char *s, float x, float y, Color c, int flags) {
    int lh = gfx_text_h(f) + (f == FONT_BIG ? 3 : 2);
    int idx = 0, maxw = 0;
    const char *line = s;
    while (1) {
        const char *e = strchr(line, '\n');
        int len = e ? (int)(e - line) : (int)strlen(line);
        char buf[512];
        int n = MINF(len, 511);
        memcpy(buf, line, n);
        buf[n] = 0;
        int w = gfx_text_w(f, buf);
        float lx = x;
        if (flags & TXT_CENTER) lx = x - w / 2;
        else if (flags & TXT_RIGHT) lx = x - w;
        text_line(f, buf, n, floorf(lx), floorf(y), c, c, flags, &idx, -1);
        maxw = MAXF(maxw, w);
        if (!e) break;
        line = e + 1;
        y += lh;
    }
    return maxw;
}

/* word wrap: calls emit for each line */
static int wrap_lines(FontId f, const char *s, int maxw, int line_h, float x, float y,
                      void (*emit)(FontId, const char *, int, float, float, void *), void *ud) {
    int lines = 0;
    const char *p = s;
    while (*p) {
        if (*p == '\n') {
            /* blank line */
            lines++;
            p++;
            continue;
        }
        /* find longest prefix that fits */
        const char *best = NULL;
        const char *q = p;
        int w = 0;
        while (*q && *q != '\n') {
            if (*q == '^' && q[1]) { q += 2; continue; }
            int adv = glyph_adv(f, (unsigned char)*q);
            if (w + adv - 1 > maxw && best) break;
            if (w + adv - 1 > maxw && !best) { best = q; break; }
            w += adv;
            q++;
            if (*q == ' ' || *q == '\n' || *q == 0) best = q;
        }
        if (!*q || *q == '\n') best = q;
        if (!best || best == p) best = q > p ? q : p + 1;
        if (emit) emit(f, p, (int)(best - p), x, y + lines * line_h, ud);
        lines++;
        p = best;
        if (*p == '\n') p++;
        else while (*p == ' ') p++;
    }
    return lines * line_h;
}

typedef struct { Color c; int flags; int idx; int limit; Color carry; } WrapCtx;

static void emit_line(FontId f, const char *s, int len, float x, float y, void *ud) {
    WrapCtx *w = (WrapCtx *)ud;
    /* colour codes carry across wrapped lines */
    char buf[600];
    int n = MINF(len, 590);
    memcpy(buf, s, n);
    buf[n] = 0;
    int flags = w->flags;
    float lx = x;
    if (flags & TXT_CENTER) lx = x - gfx_text_w(f, buf) / 2;
    text_line(f, buf, n, floorf(lx), floorf(y), w->c, w->carry, flags, &w->idx, w->limit);
    /* track last colour code in this line for the next one */
    for (int i = 0; i + 1 < n; i++)
        if (buf[i] == '^') w->carry = buf[i + 1] == '0' ? w->c : code_color(buf[i + 1], w->c);
}

int gfx_text_wrap(FontId f, const char *s, float x, float y, int maxw, Color c, int flags, int line_h) {
    return gfx_text_wrap_n(f, s, x, y, maxw, c, flags, line_h, -1);
}

int gfx_text_wrap_n(FontId f, const char *s, float x, float y, int maxw, Color c, int flags, int line_h, int n) {
    WrapCtx w = {c, flags, 0, n, c};
    return wrap_lines(f, s, maxw, line_h, x, y, emit_line, &w);
}

int gfx_text_wrap_h(FontId f, const char *s, int maxw, int line_h) {
    return wrap_lines(f, s, maxw, line_h, 0, 0, NULL, NULL);
}

void gfx_text_reveal(FontId f, const char *s, float cx, float y, Color c, float shown, int line_h) {
    const float soft = 6;   /* letters fade over this many letter-times */
    int idx = 0;
    const char *line = s;
    while (*line) {
        const char *e = strchr(line, '\n');
        int len = e ? (int)(e - line) : (int)strlen(line);
        char buf[256];
        int n = MINF(len, 255);
        memcpy(buf, line, n);
        buf[n] = 0;
        float x = floorf(cx - gfx_text_w(f, buf) / 2);
        Color cur = c;
        for (int i = 0; i < n; i++) {
            unsigned char ch = (unsigned char)buf[i];
            if (ch == '^' && i + 1 < n) {
                i++;
                cur = buf[i] == '0' ? c : code_color(buf[i], c);
                continue;
            }
            float a = CLAMP((shown - idx) / soft + 1.0f, 0.0f, 1.0f);
            idx++;
            if (ch != ' ' && a > 0) {
                Color k = cur;
                k.a = (Uint8)(cur.a * a);
                gfx_spr_c(glyph_id(f, ch), x, floorf(y), k);
            }
            x += glyph_adv(f, ch);
        }
        if (!e) break;
        line = e + 1;
        y += line_h;
    }
}

int gfx_text_big(FontId f, const char *s, float x, float y, int scale, Color c, int flags) {
    int w = gfx_text_w(f, s) * scale;
    float cx = x;
    if (flags & TXT_CENTER) cx = x - w / 2;
    else if (flags & TXT_RIGHT) cx = x - w;
    cx = floorf(cx);
    Color shadow = rgba(11, 10, 16, c.a);
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        int adv = glyph_adv(f, ch) * scale;
        if (ch != ' ') {
            int id = glyph_id(f, ch);
            float gx = cx, gy = y;
            if (flags & TXT_SHAKE) { gx += (float)irange(-scale, scale) * 0.5f; gy += (float)irange(-scale, scale) * 0.5f; }
            if (flags & TXT_SHADOW) {
                gfx_spr_ex(id, gx + scale * 2, gy + scale * 2, 0, (float)scale, (float)scale, shadow);
                gfx_spr_ex(id, gx + scale, gy + scale, 0, (float)scale, (float)scale, shadow);
            }
            if (flags & TXT_OUTLINE) {
                for (int oy = -1; oy <= 1; oy++)
                    for (int ox = -1; ox <= 1; ox++)
                        if (ox || oy) gfx_spr_ex(id, gx + ox * scale, gy + oy * scale, 0, (float)scale, (float)scale, shadow);
            }
            gfx_spr_ex(id, gx, gy, 0, (float)scale, (float)scale, c);
        }
        cx += adv;
    }
    return w;
}

void gfx_text_rot(FontId f, const char *s, float cx, float cy, int scale, float angle, Color c) {
    float w = gfx_text_w(f, s) * scale, h = gfx_text_h(f) * scale;
    float ca = cosf(angle), sa = sinf(angle);
    float x = -w * 0.5f;
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch != ' ') {
            /* each glyph's top-left corner, walked along the rotated baseline */
            float lx = x, ly = -h * 0.5f;
            gfx_spr_ex(glyph_id(f, ch), cx + lx * ca - ly * sa, cy + lx * sa + ly * ca, angle, (float)scale, (float)scale, c);
        }
        x += glyph_adv(f, ch) * scale;
    }
}
