/* LAST AISLE - tile map: collision, sight, pathfinding, autotiling, rendering */
#include "world.h"
#include "gfx.h"
#include "audio.h"

bool cell_flag(int tx, int ty, int f) {
    if (!in_map(tx, ty)) return (f & (CF_SOLID | CF_OPAQUE | CF_SHOT)) != 0;
    return (W.cells[ty][tx].flags & f) != 0;
}

bool solid_at(float x, float y) { return cell_flag(tile_of(x), tile_of(y), CF_SOLID); }

bool walkable_tile(int tx, int ty) { return in_map(tx, ty) && !(W.cells[ty][tx].flags & CF_SOLID); }

V2 tile_center(int tx, int ty) { return v2(tx * TILE + TILE * 0.5f, ty * TILE + TILE * 0.5f); }

/* ------------------------------------------------------------- collision */
void collide_circle(V2 *pos, float r, V2 *vel) {
    for (int iter = 0; iter < 2; iter++) {
        int x0 = tile_of(pos->x - r), x1 = tile_of(pos->x + r);
        int y0 = tile_of(pos->y - r), y1 = tile_of(pos->y + r);
        for (int ty = y0; ty <= y1; ty++)
            for (int tx = x0; tx <= x1; tx++) {
                if (!cell_flag(tx, ty, CF_SOLID)) continue;
                float bx0 = tx * TILE, by0 = ty * TILE;
                float cx = CLAMP(pos->x, bx0, bx0 + TILE), cy = CLAMP(pos->y, by0, by0 + TILE);
                float dx = pos->x - cx, dy = pos->y - cy;
                float d2 = dx * dx + dy * dy;
                if (d2 >= r * r) continue;
                float d = sqrtf(d2);
                V2 n;
                if (d < 1e-4f) {
                    /* centre inside the tile: push out along the shallowest axis */
                    float l = pos->x - bx0, rr = bx0 + TILE - pos->x, t = pos->y - by0, b = by0 + TILE - pos->y;
                    float m = MINF(MINF(l, rr), MINF(t, b));
                    if (m == l) n = v2(-1, 0); else if (m == rr) n = v2(1, 0);
                    else if (m == t) n = v2(0, -1); else n = v2(0, 1);
                    d = -m;
                } else {
                    n = v2(dx / d, dy / d);
                }
                float pen = r - d;
                pos->x += n.x * pen;
                pos->y += n.y * pen;
                if (vel) {
                    float vn = v2_dot(*vel, n);
                    if (vn < 0) *vel = v2_sub(*vel, v2_scale(n, vn));
                }
            }
    }
}

/* ------------------------------------------------------------ sight lines */
static bool seg_intersect(V2 p, V2 p2, V2 q, V2 q2) {
    V2 r = v2_sub(p2, p), s = v2_sub(q2, q);
    float den = r.x * s.y - r.y * s.x;
    if (fabsf(den) < 1e-6f) return false;
    V2 qp = v2_sub(q, p);
    float t = (qp.x * s.y - qp.y * s.x) / den;
    float u = (qp.x * r.y - qp.y * r.x) / den;
    return t >= 0 && t <= 1 && u >= 0 && u <= 1;
}

static bool doors_block(V2 a, V2 b) {
    for (int i = 0; i < W.ndoors; i++) {
        Door *d = &W.doors[i];
        if (d->glass) continue;
        V2 e = v2_add(d->hinge, v2_scale(v2_angle(d->base + d->ang), d->len));
        if (seg_intersect(a, b, d->hinge, e)) return true;
    }
    return false;
}

bool los_clear(V2 a, V2 b, bool for_bullets) {
    int flag = for_bullets ? CF_SHOT : CF_OPAQUE;
    /* DDA over the grid */
    float dx = b.x - a.x, dy = b.y - a.y;
    int tx = tile_of(a.x), ty = tile_of(a.y);
    int ex = tile_of(b.x), ey = tile_of(b.y);
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    float tdx = dx != 0 ? fabsf(TILE / dx) : 1e9f, tdy = dy != 0 ? fabsf(TILE / dy) : 1e9f;
    float nx = dx > 0 ? (tx + 1) * TILE : tx * TILE, ny = dy > 0 ? (ty + 1) * TILE : ty * TILE;
    float tmx = dx != 0 ? fabsf((nx - a.x) / dx) : 1e9f, tmy = dy != 0 ? fabsf((ny - a.y) / dy) : 1e9f;
    for (int guard = 0; guard < 400; guard++) {
        if (tx == ex && ty == ey) break;
        if (tmx < tmy) { tmx += tdx; tx += sx; }
        else { tmy += tdy; ty += sy; }
        if (tx == ex && ty == ey) break;
        if (cell_flag(tx, ty, flag)) return false;
    }
    if (doors_block(a, b)) return false;
    return true;
}

float ray_dist(V2 a, float ang, float maxd) {
    V2 d = v2_angle(ang);
    for (float t = 0; t < maxd; t += 4) {
        V2 p = v2_add(a, v2_scale(d, t));
        if (cell_flag(tile_of(p.x), tile_of(p.y), CF_OPAQUE)) return t;
    }
    return maxd;
}

/* ---------------------------------------------------------------- A* */
static int g_cost[MAP_MAX_H * MAP_MAX_W];
static int g_from[MAP_MAX_H * MAP_MAX_W];
static unsigned g_stamp[MAP_MAX_H * MAP_MAX_W];
static unsigned g_closed[MAP_MAX_H * MAP_MAX_W];
static unsigned g_gen = 1;
static int heap[MAP_MAX_H * MAP_MAX_W * 2];
static int heap_f[MAP_MAX_H * MAP_MAX_W * 2];
static int heap_n;

static void heap_push(int node, int f) {
    int i = heap_n++;
    heap[i] = node;
    heap_f[i] = f;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (heap_f[p] <= heap_f[i]) break;
        int t = heap[p]; heap[p] = heap[i]; heap[i] = t;
        t = heap_f[p]; heap_f[p] = heap_f[i]; heap_f[i] = t;
        i = p;
    }
}

static int heap_pop(void) {
    int top = heap[0];
    heap_n--;
    heap[0] = heap[heap_n];
    heap_f[0] = heap_f[heap_n];
    int i = 0;
    for (;;) {
        int l = i * 2 + 1, r = l + 1, m = i;
        if (l < heap_n && heap_f[l] < heap_f[m]) m = l;
        if (r < heap_n && heap_f[r] < heap_f[m]) m = r;
        if (m == i) break;
        int t = heap[m]; heap[m] = heap[i]; heap[i] = t;
        t = heap_f[m]; heap_f[m] = heap_f[i]; heap_f[i] = t;
        i = m;
    }
    return top;
}

static bool walk_line(V2 a, V2 b, float r) {
    float d = v2_dist(a, b);
    int steps = (int)(d / 4) + 1;
    for (int i = 0; i <= steps; i++) {
        V2 p = v2_add(a, v2_scale(v2_sub(b, a), (float)i / steps));
        if (solid_at(p.x - r, p.y - r) || solid_at(p.x + r, p.y - r) || solid_at(p.x - r, p.y + r) ||
            solid_at(p.x + r, p.y + r))
            return false;
    }
    return true;
}

bool path_find(V2 from, V2 to, int out[][2], int max, int *len) {
    *len = 0;
    int sx = tile_of(from.x), sy = tile_of(from.y), gx = tile_of(to.x), gy = tile_of(to.y);
    if (!in_map(sx, sy) || !in_map(gx, gy)) return false;
    if (!walkable_tile(gx, gy)) {
        /* goal inside an obstacle: pick nearest walkable neighbour */
        bool found = false;
        for (int r = 1; r <= 2 && !found; r++)
            for (int oy = -r; oy <= r && !found; oy++)
                for (int ox = -r; ox <= r && !found; ox++)
                    if (walkable_tile(gx + ox, gy + oy)) { gx += ox; gy += oy; found = true; }
        if (!found) return false;
    }
    g_gen++;
    if (g_gen == 0) { memset(g_stamp, 0, sizeof g_stamp); memset(g_closed, 0, sizeof g_closed); g_gen = 1; }
    heap_n = 0;
    int start = sy * MAP_MAX_W + sx, goal = gy * MAP_MAX_W + gx;
    g_stamp[start] = g_gen;
    g_cost[start] = 0;
    g_from[start] = -1;
    heap_push(start, 0);
    int expanded = 0;
    bool ok = false;
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (heap_n > 0 && expanded < 6000) {
        int cur = heap_pop();
        if (g_closed[cur] == g_gen) continue;
        g_closed[cur] = g_gen;
        expanded++;
        if (cur == goal) { ok = true; break; }
        int cx = cur % MAP_MAX_W, cy = cur / MAP_MAX_W;
        for (int k = 0; k < 8; k++) {
            int nx = cx + DX[k], ny = cy + DY[k];
            if (!walkable_tile(nx, ny)) continue;
            if (k >= 4 && (!walkable_tile(cx + DX[k], cy) || !walkable_tile(cx, cy + DY[k]))) continue;
            int n = ny * MAP_MAX_W + nx;
            if (g_closed[n] == g_gen) continue;
            int step = k < 4 ? 10 : 14;
            /* discourage hugging glass / doors a bit */
            if (W.cells[ny][nx].flags & CF_DOOR) step += 4;
            int nc = g_cost[cur] + step;
            if (g_stamp[n] != g_gen || nc < g_cost[n]) {
                g_stamp[n] = g_gen;
                g_cost[n] = nc;
                g_from[n] = cur;
                int h = (abs(nx - gx) + abs(ny - gy)) * 10;
                heap_push(n, nc + h);
            }
        }
    }
    if (!ok) return false;
    /* reconstruct */
    static int tmp[MAP_MAX_H * MAP_MAX_W];
    int n = 0;
    for (int c = goal; c != -1 && n < MAP_MAX_H * MAP_MAX_W; c = g_from[c]) tmp[n++] = c;
    /* tmp is goal..start; smooth by string pulling */
    int outn = 0;
    int anchor = n - 1;
    V2 apos = from;
    while (anchor > 0 && outn < max) {
        int best = anchor - 1;
        for (int j = 0; j < anchor; j++) {
            V2 p = tile_center(tmp[j] % MAP_MAX_W, tmp[j] / MAP_MAX_W);
            if (anchor - j > 24) continue;
            if (walk_line(apos, p, 4.5f)) { best = j; break; }
        }
        out[outn][0] = tmp[best] % MAP_MAX_W;
        out[outn][1] = tmp[best] / MAP_MAX_W;
        outn++;
        apos = tile_center(out[outn - 1][0], out[outn - 1][1]);
        anchor = best;
    }
    *len = outn;
    return outn > 0 || (sx == gx && sy == gy);
}

/* -------------------------------------------------------------- autotile */
static bool wall_same(int tx, int ty, int type) {
    if (!in_map(tx, ty)) return true;
    return W.cells[ty][tx].wall != WL_NONE;
    (void)type;
}

void map_compute_autotile(void) {
    for (int ty = 0; ty < W.h; ty++)
        for (int tx = 0; tx < W.w; tx++) {
            Cell *c = &W.cells[ty][tx];
            if (!c->wall) continue;
            for (int qy = 0; qy < 2; qy++)
                for (int qx = 0; qx < 2; qx++) {
                    int dx = qx ? 1 : -1, dy = qy ? 1 : -1;
                    bool h = wall_same(tx + dx, ty, c->wall);
                    bool v = wall_same(tx, ty + dy, c->wall);
                    bool d = wall_same(tx + dx, ty + dy, c->wall);
                    int sx, sy; /* in 8px units */
                    if (!h && !v) { sx = qx ? 3 : 0; sy = 2 + (qy ? 3 : 0); }
                    else if (v && !h) { sx = qx ? 3 : 0; sy = 2 + 1 + qy; }
                    else if (h && !v) { sx = 1 + qx; sy = 2 + (qy ? 3 : 0); }
                    else if (!d) { sx = 2 + qx; sy = qy; }
                    else { sx = 1 + qx; sy = 2 + 1 + qy; }
                    c->wq[qy * 2 + qx] = (uint8_t)(sy * 4 + sx);
                }
        }
}

static int wall_sprite(int type) {
    switch (type) {
    case WL_BRICK: return SPR_WT_BRICK;
    case WL_MALL: return SPR_WT_MALL;
    case WL_HEDGE: return SPR_WT_HEDGE;
    case WL_RUIN: return SPR_WT_RUIN;
    default: return SPR_WT_CONCRETE;
    }
}

static int floor_sprite(int f) {
    switch (f) {
    case FL_ASPHALT: return SPR_T_ASPHALT;
    case FL_ASPHALT_LINE: return SPR_T_ASPHALT_LINE;
    case FL_ASPHALT_HLINE: return SPR_T_ASPHALT_HLINE;
    case FL_CURB: return SPR_T_CURB;
    case FL_SIDEWALK: return SPR_T_SIDEWALK;
    case FL_GRASS: return SPR_T_GRASS;
    case FL_DIRT: return SPR_T_DIRT;
    case FL_LINO: return SPR_T_LINO;
    case FL_WHITETILE: return SPR_T_WHITETILE;
    case FL_CARPET: return SPR_T_CARPET;
    case FL_MALL: return SPR_T_MALL;
    case FL_CONCRETE: return SPR_T_CONCRETE;
    case FL_WOOD: return SPR_T_WOOD;
    case FL_RUBBLE: return SPR_T_RUBBLE;
    default: return -1;
    }
}

int floor_frames(int f) {
    switch (f) {
    case FL_ASPHALT: return SPR_T_ASPHALT_N;
    case FL_ASPHALT_LINE: return SPR_T_ASPHALT_LINE_N;
    case FL_ASPHALT_HLINE: return SPR_T_ASPHALT_HLINE_N;
    case FL_CURB: return SPR_T_CURB_N;
    case FL_SIDEWALK: return SPR_T_SIDEWALK_N;
    case FL_GRASS: return SPR_T_GRASS_N;
    case FL_DIRT: return SPR_T_DIRT_N;
    case FL_LINO: return SPR_T_LINO_N;
    case FL_WHITETILE: return SPR_T_WHITETILE_N;
    case FL_CARPET: return SPR_T_CARPET_N;
    case FL_MALL: return SPR_T_MALL_N;
    case FL_CONCRETE: return SPR_T_CONCRETE_N;
    case FL_WOOD: return SPR_T_WOOD_N;
    case FL_RUBBLE: return SPR_T_RUBBLE_N;
    default: return 1;
    }
}

/* ------------------------------------------------------------- objects */
int obj_sprite(Cell *c) {
    bool looted = c->cont >= 0 && W.conts[c->cont].n == 0 && W.conts[c->cont].searched;
    bool empty = c->cont >= 0 && W.conts[c->cont].n == 0;
    int v = c->ovar;
    switch (c->obj) {
    case OB_SHELF_H:
        if (v & 16) return SPR_P_SHELF_END;
        if (v & 32) return SPR_P_SHELF_END + 1;
        if (empty || (v & 64)) return SPR_P_SHELF_EMPTY + (v & 1);
        return SPR_P_SHELF + (v & 15) % SPR_P_SHELF_N;
    case OB_SHELF_V:
        if (v & 16) return SPR_P_SHELFV_END;
        if (v & 32) return SPR_P_SHELFV_END + 1;
        if (empty || (v & 64)) return SPR_P_SHELFV_EMPTY + (v & 1);
        return SPR_P_SHELFV + (v & 15) % SPR_P_SHELFV_N;
    case OB_SHELF_FALLEN: return SPR_P_SHELF_FALLEN + (v & 1);
    case OB_FRIDGE_D: return SPR_P_FRIDGE + (empty ? 1 : 0);
    case OB_FRIDGE_R: return SPR_P_FRIDGE_R + (empty ? 1 : 0);
    case OB_FRIDGE_L: return SPR_P_FRIDGE_L + (empty ? 1 : 0);
    case OB_COUNTER: return SPR_P_COUNTER + v % 3;
    case OB_REGISTER: return SPR_P_REGISTER;
    case OB_CRATE: return SPR_P_CRATE + (looted ? 1 : 0);
    case OB_BOX: return SPR_P_BOX + v % 3;
    case OB_PALLET: return empty ? SPR_P_PALLET : SPR_P_PALLET_GOODS;
    case OB_BARREL: return SPR_P_BARREL;
    case OB_LOCKER: return SPR_P_LOCKER + (looted ? 1 : 0);
    case OB_TOILET: return SPR_P_TOILET;
    case OB_SINK: return SPR_P_SINK;
    case OB_PEGBOARD: return SPR_P_PEGBOARD + (empty ? 1 : 0);
    case OB_VENDING: return SPR_P_VENDING + (looted ? 1 : 0);
    case OB_CAMPFIRE: return SPR_P_CAMPFIRE + ((int)(W.time * 8) % 3);
    case OB_BUSH: return SPR_P_BUSH + v % 3;
    case OB_LAMPPOST: return SPR_P_LAMPPOST;
    case OB_RACK: return SPR_P_CLOTHES_RACK;
    case OB_MANNEQUIN: return SPR_P_MANNEQUIN;
    case OB_TV: return SPR_P_TV;
    case OB_GLASS_H: return SPR_T_GLASS_H;
    case OB_GLASS_V: return SPR_T_GLASS_V;
    case OB_GLASS_BROKEN_H: return SPR_T_GLASS_BROKEN_H;
    case OB_GLASS_BROKEN_V: return SPR_T_GLASS_BROKEN_V;
    default: return -1;
    }
}

/* -------------------------------------------------------------- baking */
void map_bake_ground(void) {
    if (W.ground) SDL_DestroyTexture(W.ground);
    W.ground = SDL_CreateTexture(G.ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, W.w * TILE, W.h * TILE);
    SDL_SetTextureScaleMode(W.ground, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(W.ground, SDL_BLENDMODE_NONE);
    SDL_Texture *prev = SDL_GetRenderTarget(G.ren);
    float ox = G.off_x, oy = G.off_y;
    SDL_SetRenderTarget(G.ren, W.ground);
    SDL_SetRenderDrawColor(G.ren, 20, 18, 26, 255);
    SDL_RenderClear(G.ren);
    G.off_x = G.off_y = 0;
    for (int ty = 0; ty < W.h; ty++)
        for (int tx = 0; tx < W.w; tx++) {
            Cell *c = &W.cells[ty][tx];
            int s = floor_sprite(c->floor);
            if (s < 0) continue;
            int n = floor_frames(c->floor);
            gfx_spr(s + c->fvar % n, tx * TILE, ty * TILE);
        }
    for (int i = 0; i < W.ndecor; i++) {
        Decor *d = &W.decor[i];
        Color t = rgba(255, 255, 255, d->alpha ? d->alpha : 255);
        if (d->flip) gfx_spr_ex(d->spr, d->x, d->y, d->flip == 2 ? PI_F * 0.5f : 0, d->flip == 1 ? -1 : 1, 1, t);
        else gfx_spr_c(d->spr, d->x, d->y, t);
    }
    SDL_SetRenderTarget(G.ren, prev);
    G.off_x = ox;
    G.off_y = oy;
    SDL_SetTextureBlendMode(W.ground, SDL_BLENDMODE_BLEND);
}

void map_build_ambient(void) {
    if (W.ambient) SDL_DestroyTexture(W.ambient);
    SDL_Surface *s = SDL_CreateSurface(W.w + 2, W.h + 2, SDL_PIXELFORMAT_RGBA32);
    Uint8 *px = (Uint8 *)s->pixels;
    for (int y = 0; y < W.h + 2; y++)
        for (int x = 0; x < W.w + 2; x++) {
            int tx = CLAMP(x - 1, 0, W.w - 1), ty = CLAMP(y - 1, 0, W.h - 1);
            Cell *c = &W.cells[ty][tx];
            Color col = (c->flags & CF_INDOOR) ? W.amb_in : W.amb_out;
            if ((c->flags & CF_INDOOR) && (c->flags & CF_SKY)) col = color_lerp(W.amb_in, W.amb_out, 0.75f);
            Uint8 *p = px + y * s->pitch + x * 4;
            p[0] = col.r; p[1] = col.g; p[2] = col.b; p[3] = 255;
        }
    W.ambient = SDL_CreateTextureFromSurface(G.ren, s);
    SDL_DestroySurface(s);
    SDL_SetTextureScaleMode(W.ambient, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(W.ambient, SDL_BLENDMODE_NONE);
}

/* ------------------------------------------------------------- drawing */
bool g_draw_all;

static void visible_range(int *x0, int *y0, int *x1, int *y1) {
    if (g_draw_all) { *x0 = 0; *y0 = 0; *x1 = W.w - 1; *y1 = W.h - 1; return; }
    *x0 = MAXF(0, tile_of((float)G.cam_ox) - 1);
    *y0 = MAXF(0, tile_of((float)G.cam_oy) - 1);
    *x1 = MINF(W.w - 1, tile_of((float)G.cam_ox + WORLD_RT_W) + 1);
    *y1 = MINF(W.h - 1, tile_of((float)G.cam_oy + WORLD_RT_H) + 1);
}

void map_draw_floor(void) {
    SDL_FRect src = {(float)G.cam_ox, (float)G.cam_oy, WORLD_RT_W, WORLD_RT_H};
    SDL_FRect dst = {0, 0, WORLD_RT_W, WORLD_RT_H};
    /* clamp to texture */
    float tw = W.w * TILE, th = W.h * TILE;
    if (src.x < 0) { dst.x -= src.x; dst.w += src.x; src.w += src.x; src.x = 0; }
    if (src.y < 0) { dst.y -= src.y; dst.h += src.y; src.h += src.y; src.y = 0; }
    if (src.x + src.w > tw) { float d = src.x + src.w - tw; src.w -= d; dst.w -= d; }
    if (src.y + src.h > th) { float d = src.y + src.h - th; src.h -= d; dst.h -= d; }
    if (src.w > 0 && src.h > 0) SDL_RenderTexture(G.ren, W.ground, &src, &dst);
}

void map_draw_objects(void) {
    int x0, y0, x1, y1;
    visible_range(&x0, &y0, &x1, &y1);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            Cell *c = &W.cells[ty][tx];
            if (!c->obj || c->obj == OB_BLOCKER) continue;
            int s = obj_sprite(c);
            if (s >= 0) gfx_spr(s, tx * TILE, ty * TILE);
        }
}

void map_draw_walls(void) {
    int x0, y0, x1, y1;
    visible_range(&x0, &y0, &x1, &y1);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            Cell *c = &W.cells[ty][tx];
            if (!c->wall) continue;
            int s = wall_sprite(c->wall);
            for (int q = 0; q < 4; q++) {
                int src = c->wq[q];
                gfx_spr_src(s, (src % 4) * 8, (src / 4) * 8, 8, 8, tx * TILE + (q % 2) * 8, ty * TILE + (q / 2) * 8,
                            TINT_NONE);
            }
        }
}

void map_draw_overlays(void) {
    float x0 = G.cam_ox - 32, y0 = G.cam_oy - 32, x1 = G.cam_ox + WORLD_RT_W + 32, y1 = G.cam_oy + WORLD_RT_H + 32;
    if (g_draw_all) { x0 = -99; y0 = -99; x1 = 1e9f; y1 = 1e9f; }
    for (int i = 0; i < W.noverdecor; i++) {
        Decor *d = &W.overdecor[i];
        if (d->x < x0 || d->x > x1 || d->y < y0 || d->y > y1) continue;
        Color t = rgba(255, 255, 255, d->alpha ? d->alpha : 255);
        if (d->flip) gfx_spr_ex(d->spr, d->x, d->y, 0, d->flip == 1 ? -1 : 1, 1, t);
        else gfx_spr_c(d->spr, d->x, d->y, t);
    }
}

V2 random_floor_spot(Rng *r, bool indoor, float min_dist, V2 from) {
    for (int tries = 0; tries < 400; tries++) {
        int tx = rng_range(r, 1, W.w - 2), ty = rng_range(r, 1, W.h - 2);
        Cell *c = &W.cells[ty][tx];
        if (c->flags & (CF_SOLID | CF_NOSPAWN | CF_EXIT)) continue;
        if (!c->reach) continue;
        if (indoor != ((c->flags & CF_INDOOR) != 0)) continue;
        V2 p = tile_center(tx, ty);
        if (v2_dist(p, from) < min_dist) continue;
        return p;
    }
    return W.spawn;
}

void break_glass(int tx, int ty, V2 from) {
    if (!in_map(tx, ty)) return;
    Cell *c = &W.cells[ty][tx];
    if (c->obj != OB_GLASS_H && c->obj != OB_GLASS_V) return;
    c->obj = c->obj == OB_GLASS_H ? OB_GLASS_BROKEN_H : OB_GLASS_BROKEN_V;
    c->flags &= ~(CF_SOLID | CF_SHOT);
    V2 p = tile_center(tx, ty);
    V2 dir = v2_norm(v2_sub(p, from));
    for (int i = 0; i < 18; i++) {
        V2 v = v2_add(v2_scale(dir, frange(30, 160)), v2(frange(-60, 60), frange(-60, 60)));
        Particle *pt = particle_add(PT_GLASS, v2(p.x + frange(-7, 7), p.y + frange(-7, 7)), v, frange(0.6f, 1.4f));
        if (pt) {
            pt->spr = SPR_FX_GLASS + irange(0, 2);
            pt->vz = frange(20, 90);
            pt->bake = true;
        }
    }
    play_at(SFX_GLASS_BREAK, p, 0.9f, frange(0.9f, 1.1f));
    make_noise(p, 220, -1);
}
