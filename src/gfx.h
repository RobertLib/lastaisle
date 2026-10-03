/* LAST AISLE - rendering: atlas sprites, pixel fonts, render targets, post fx */
#ifndef GFX_H
#define GFX_H

#include "common.h"

typedef enum { FONT_SMALL, FONT_BIG } FontId;

enum {
    TXT_LEFT = 0,
    TXT_CENTER = 1,
    TXT_RIGHT = 2,
    TXT_SHADOW = 4,   /* hard drop shadow */
    TXT_OUTLINE = 8,  /* 1px dark outline */
    TXT_SHAKE = 16,   /* jittery */
};

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *atlas;
    SDL_Texture *rt_world, *rt_light, *rt_hud;
    SDL_Texture *tex_glow, *tex_cone, *tex_vignette, *tex_grain, *tex_white;
    SDL_Texture *rt_cut[2];   /* cutscene shots (480x270), composited with dithered transitions */
    SDL_Texture *tex_dither;  /* 17 ordered-dither (4x4 Bayer) coverage levels side by side */
    SDL_BlendMode bm_mask;    /* multiplies the target's alpha by the source alpha (0 = unsupported) */
    int win_w, win_h;
    float scale;          /* window pixels per internal pixel */
    SDL_FRect view;       /* letterboxed 480x270 area in window pixels */
    float time;           /* real time seconds */

    /* camera (world space) */
    float cam_x, cam_y;   /* centre */
    int cam_ox, cam_oy;   /* world coord of rt_world pixel (0,0) */
    float cam_angle;      /* radians */
    float cam_zoom;

    /* current draw offset applied to sprite/rect calls */
    float off_x, off_y;

    /* post */
    float flash;  Color flash_col;
    float impact;       /* contrast punch on big hits 0..1 */
    float vignette;     /* extra vignette strength 0..1 */
    float red_pulse;    /* low hp edge pulse 0..1 */
    bool grain;         /* film grain over everything */
    const char *capture_path;  /* debug: save the next presented frame */
} Gfx;

extern Gfx G;

bool gfx_init(SDL_Window *win, SDL_Renderer *ren);
void gfx_shutdown(void);
void gfx_resize(void);

/* coordinate helpers */
V2 gfx_screen_to_view(float sx, float sy);   /* window px -> 480x270 HUD coords */
V2 gfx_screen_to_world(float sx, float sy);  /* window px -> world coords (camera rot/zoom aware) */
V2 gfx_world_to_view(V2 w);                  /* world -> approx HUD coords (ignores rotation) */

/* targets */
void gfx_begin_world(void);    /* bind rt_world, set camera offset */
void gfx_begin_light(Color ambient);
void gfx_end_light(void);      /* multiplies light over world */
void gfx_begin_hud(void);
void gfx_present(void);        /* composite world + hud to window */
void gfx_begin_cut(int i);     /* bind cutscene target i (cleared to black, no offset) */
void gfx_cut_mask(int i, float level, float wipe);   /* keep `level` of target i's pixels by ordered dither;
                                                         wipe > 0 sweeps a soft dithered edge left to right */
void gfx_draw_cut(int i, float x, float y);          /* composite target i onto the current target */

/* sprites (positions are pivot positions) */
void gfx_spr(int id, float x, float y);
void gfx_spr_c(int id, float x, float y, Color tint);
void gfx_spr_ex(int id, float x, float y, float angle, float sx, float sy, Color tint);
void gfx_spr_src(int id, int sx, int sy, int sw, int sh, float dx, float dy, Color tint);
void gfx_spr_stretch(int id, float x, float y, float w, float h, Color tint);  /* top-left anchored */
void gfx_nine(int id, float x, float y, float w, float h, Color tint);       /* 9-slice, 8px corners */
void gfx_spr_tile(int id, float x, float y, float w, float h, Color tint);   /* repeat the sprite over an area */

/* primitives */
void gfx_fill(float x, float y, float w, float h, Color c);
void gfx_rect(float x, float y, float w, float h, Color c);
void gfx_line(float x1, float y1, float x2, float y2, Color c);
void gfx_glow(float x, float y, float radius, Color c, float intensity); /* additive soft light */
void gfx_cone(float x, float y, float angle, float len, Color c, float intensity);
void gfx_dither(float x, float y, float w, float h, float level, Color c);  /* ordered-dither fill covering `level` 0..1 */

/* store ephemera: the UI is made of price stickers, marker swipes and rubber stamps */
void gfx_sticker(float x, float y, float w, float h, Color bg);   /* clipped-corner label, ink edge, drop shadow */
void gfx_marker(float x, float y, float w, float h, Color c);     /* highlighter swipe with ragged ends */
void gfx_ring(float cx, float cy, float r, float thick, Color c); /* pixel circle outline (stamps) */

/* text; returns advance width. '^x' switches colour (w y p c r g k o b d), '^0' resets */
int gfx_text(FontId f, const char *s, float x, float y, Color c, int flags);
int gfx_text_w(FontId f, const char *s);
/* integer-scaled text for big moments (pixel-perfect). Supports CENTER/RIGHT/SHADOW/OUTLINE/SHAKE */
int gfx_text_big(FontId f, const char *s, float x, float y, int scale, Color c, int flags);
/* scaled text centred on (cx, cy) and rotated about it: rubber stamps */
void gfx_text_rot(FontId f, const char *s, float cx, float cy, int scale, float angle, Color c);
int gfx_text_h(FontId f);
int gfx_text_wrap(FontId f, const char *s, float x, float y, int maxw, Color c, int flags, int line_h);
int gfx_text_wrap_h(FontId f, const char *s, int maxw, int line_h);
/* clip only the first n visible characters (typewriter effect) */
int gfx_text_wrap_n(FontId f, const char *s, float x, float y, int maxw, Color c, int flags, int line_h, int nchars);
/* centred lines whose letters fade in one after another: `shown` letters are fully in, the next few half way */
void gfx_text_reveal(FontId f, const char *s, float cx, float y, Color c, float shown, int line_h);

#endif
