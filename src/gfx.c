/* LAST AISLE - rendering */
#include "gfx.h"

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

static SDL_Texture *make_scan(void) {
    SDL_Surface *s = SDL_CreateSurface(1, 2, SDL_PIXELFORMAT_RGBA32);
    Uint8 *px = (Uint8 *)s->pixels;
    px[0] = px[1] = px[2] = 0; px[3] = 0;
    Uint8 *p = px + s->pitch;
    p[0] = p[1] = p[2] = 0; p[3] = 46;
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
    G.scanlines = true;
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
    G.tex_scan = make_scan();
    G.tex_white = make_white();
    gfx_resize();
    return true;
}

void gfx_shutdown(void) {
    SDL_Texture *ts[] = {G.atlas, G.rt_world, G.rt_light, G.rt_hud, G.tex_glow,
                         G.tex_cone, G.tex_vignette, G.tex_scan, G.tex_white};
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
    if (G.chroma > 0.01f) {
        float o = G.chroma * 2.5f * G.scale;
        SDL_SetTextureBlendMode(G.rt_world, SDL_BLENDMODE_ADD);
        Uint8 a = (Uint8)CLAMP(G.chroma * 140, 0, 255);
        SDL_SetTextureAlphaMod(G.rt_world, a);
        SDL_FRect d2 = dst;
        d2.x += o;
        SDL_SetTextureColorMod(G.rt_world, 255, 0, 40);
        SDL_RenderTextureRotated(G.ren, G.rt_world, NULL, &d2, ang, &centre, SDL_FLIP_NONE);
        d2.x = dst.x - o;
        SDL_SetTextureColorMod(G.rt_world, 0, 60, 255);
        SDL_RenderTextureRotated(G.ren, G.rt_world, NULL, &d2, ang, &centre, SDL_FLIP_NONE);
        SDL_SetTextureColorMod(G.rt_world, 255, 255, 255);
        SDL_SetTextureAlphaMod(G.rt_world, 255);
        SDL_SetTextureBlendMode(G.rt_world, SDL_BLENDMODE_BLEND);
    }

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

    if (G.scanlines) {
        SDL_SetTextureBlendMode(G.tex_scan, SDL_BLENDMODE_BLEND);
        SDL_RenderTextureTiled(G.ren, G.tex_scan, NULL, G.scale * 0.5f, &G.view);
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
    case 'p': return rgba(255, 95, 149, base.a);
    case 'c': return rgba(98, 236, 208, base.a);
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

Color gfx_rainbow(float t) {
    /* Hotline Miami-ish neon cycle: pink -> orange -> yellow -> cyan -> purple */
    static const Color stops[] = {{255, 95, 149, 255}, {255, 140, 46, 255}, {255, 212, 71, 255},
                                  {98, 236, 208, 255}, {167, 100, 234, 255}};
    int n = ARRAY_LEN(stops);
    t = t - floorf(t);
    float f = t * n;
    int i = (int)f;
    return color_lerp(stops[i % n], stops[(i + 1) % n], f - i);
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
            if (flags & TXT_WAVE) gy += sinf(G.time * 5.0f + *idx * 0.55f) * (f == FONT_BIG ? 1.6f : 1.0f);
            if (flags & TXT_SHAKE) { gx += (float)irange(-1, 1); gy += (float)irange(-1, 1); }
            Color col = cur;
            if (flags & TXT_RAINBOW) {
                col = gfx_rainbow(G.time * 0.35f + *idx * 0.04f);
                col.a = c.a;
            }
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

int gfx_text_big(FontId f, const char *s, float x, float y, int scale, Color c, int flags) {
    int w = gfx_text_w(f, s) * scale;
    float cx = x;
    if (flags & TXT_CENTER) cx = x - w / 2;
    else if (flags & TXT_RIGHT) cx = x - w;
    cx = floorf(cx);
    Color shadow = rgba(11, 10, 16, c.a);
    int idx = 0;
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        int adv = glyph_adv(f, ch) * scale;
        if (ch != ' ') {
            int id = glyph_id(f, ch);
            float gx = cx, gy = y;
            if (flags & TXT_WAVE) gy += floorf(sinf(G.time * 4.0f + idx * 0.6f) * scale * 1.2f);
            if (flags & TXT_SHAKE) { gx += (float)irange(-scale, scale) * 0.5f; gy += (float)irange(-scale, scale) * 0.5f; }
            Color col = c;
            if (flags & TXT_RAINBOW) { col = gfx_rainbow(G.time * 0.35f + idx * 0.05f); col.a = c.a; }
            if (flags & TXT_SHADOW) {
                gfx_spr_ex(id, gx + scale * 2, gy + scale * 2, 0, (float)scale, (float)scale, shadow);
                gfx_spr_ex(id, gx + scale, gy + scale, 0, (float)scale, (float)scale, shadow);
            }
            if (flags & TXT_OUTLINE) {
                for (int oy = -1; oy <= 1; oy++)
                    for (int ox = -1; ox <= 1; ox++)
                        if (ox || oy) gfx_spr_ex(id, gx + ox * scale, gy + oy * scale, 0, (float)scale, (float)scale, shadow);
            }
            gfx_spr_ex(id, gx, gy, 0, (float)scale, (float)scale, col);
        }
        idx++;
        cx += adv;
    }
    return w;
}
