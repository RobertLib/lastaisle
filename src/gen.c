/* LAST AISLE - procedural level generation */
#include "world.h"
#include "gfx.h"
#include "hub.h"

static Rng R;

typedef struct { int x0, y0, x1, y1; } Rect;   /* inclusive tile rect */

static int rw(Rect r) { return r.x1 - r.x0 + 1; }
static int rh(Rect r) { return r.y1 - r.y0 + 1; }
static Rect rect(int x0, int y0, int x1, int y1) { Rect r = {x0, y0, x1, y1}; return r; }

static const uint8_t OBJ_FLAGS[OB_COUNT] = {
    [OB_SHELF_H] = CF_SOLID | CF_OPAQUE | CF_SHOT, [OB_SHELF_V] = CF_SOLID | CF_OPAQUE | CF_SHOT,
    [OB_SHELF_FALLEN] = CF_SOLID | CF_SHOT, [OB_FRIDGE_D] = CF_SOLID | CF_OPAQUE | CF_SHOT,
    [OB_FRIDGE_R] = CF_SOLID | CF_OPAQUE | CF_SHOT, [OB_FRIDGE_L] = CF_SOLID | CF_OPAQUE | CF_SHOT,
    [OB_COUNTER] = CF_SOLID | CF_SHOT, [OB_REGISTER] = CF_SOLID | CF_SHOT, [OB_CRATE] = CF_SOLID | CF_SHOT,
    [OB_BOX] = CF_SOLID, [OB_PALLET] = CF_SOLID | CF_SHOT, [OB_BARREL] = CF_SOLID | CF_SHOT,
    [OB_LOCKER] = CF_SOLID | CF_OPAQUE | CF_SHOT, [OB_TOILET] = CF_SOLID, [OB_SINK] = CF_SOLID,
    [OB_PEGBOARD] = CF_SOLID | CF_OPAQUE | CF_SHOT, [OB_VENDING] = CF_SOLID | CF_OPAQUE | CF_SHOT,
    [OB_CAMPFIRE] = 0, [OB_BUSH] = 0, [OB_LAMPPOST] = CF_SOLID | CF_SHOT, [OB_RACK] = CF_SOLID,
    [OB_MANNEQUIN] = 0, [OB_TV] = CF_SOLID, [OB_GLASS_H] = CF_SOLID | CF_SHOT, [OB_GLASS_V] = CF_SOLID | CF_SHOT,
    [OB_GLASS_BROKEN_H] = 0, [OB_GLASS_BROKEN_V] = 0, [OB_BLOCKER] = CF_SOLID | CF_SHOT,
};

/* ------------------------------------------------------------- basics */
static void floor_at(int x, int y, int f) {
    if (!in_map(x, y)) return;
    Cell *c = cell(x, y);
    c->floor = (uint8_t)f;
    int n = floor_frames(f);
    float vch = (f == FL_ASPHALT || f == FL_CONCRETE || f == FL_MALL) ? 0.14f : 0.24f;
    c->fvar = (n > 1 && rng_chance(&R, vch)) ? (uint8_t)rng_range(&R, 1, n - 1) : 0;
    if (f == FL_GRASS) c->fuel = 1;
}

static void floor_rect(Rect r, int f) {
    for (int y = r.y0; y <= r.y1; y++)
        for (int x = r.x0; x <= r.x1; x++) floor_at(x, y, f);
}

static void clear_at(int x, int y) {
    if (!in_map(x, y)) return;
    Cell *c = cell(x, y);
    c->wall = 0;
    c->obj = 0;
    c->ovar = 0;
    c->cont = -1;
    c->flags &= ~(CF_SOLID | CF_OPAQUE | CF_SHOT | CF_DOOR);
}

static void wall_at(int x, int y, int type) {
    if (!in_map(x, y)) return;
    Cell *c = cell(x, y);
    c->wall = (uint8_t)type;
    c->obj = 0;
    c->cont = -1;
    c->flags |= CF_SOLID | CF_OPAQUE | CF_SHOT;
    c->flags &= ~CF_DOOR;
}

static bool free_cell(int x, int y) {
    if (!in_map(x, y)) return false;
    Cell *c = cell(x, y);
    return !c->wall && !c->obj && !(c->flags & (CF_SOLID | CF_DOOR | CF_NOSPAWN));
}

static void obj_at(int x, int y, int obj, int var) {
    if (!in_map(x, y)) return;
    Cell *c = cell(x, y);
    c->wall = 0;
    c->obj = (uint8_t)obj;
    c->ovar = (uint8_t)var;
    c->flags &= ~(CF_SOLID | CF_OPAQUE | CF_SHOT);
    c->flags |= OBJ_FLAGS[obj];
}

static void mark_indoor(Rect r) {
    for (int y = r.y0; y <= r.y1; y++)
        for (int x = r.x0; x <= r.x1; x++)
            if (in_map(x, y)) cell(x, y)->flags |= CF_INDOOR;
}

static void set_zone(Rect r, int z) {
    for (int y = r.y0; y <= r.y1; y++)
        for (int x = r.x0; x <= r.x1; x++)
            if (in_map(x, y)) cell(x, y)->zone = (uint8_t)z;
}

static int cont_new(int tx, int ty, int zone, const char *name) {
    if (W.nconts >= ARRAY_LEN(W.conts)) return -1;
    int i = W.nconts++;
    Container *c = &W.conts[i];
    memset(c, 0, sizeof *c);
    c->tx = tx;
    c->ty = ty;
    c->pos = tile_center(tx, ty);
    c->zone = zone;
    c->prop = -1;
    c->name = name;
    c->glint_t = rng_rangef(&R, 0, 6);
    if (in_map(tx, ty)) cell(tx, ty)->cont = (int16_t)i;
    return i;
}

/* the cells each prop blocks (generation only), so a removed prop frees exactly its own footprint */
static Rect prop_fp[sizeof W.props / sizeof W.props[0]];
static bool prop_has_fp[sizeof W.props / sizeof W.props[0]];

static int prop_add(int spr, float x, float y, int layer) {
    if (W.nprops >= ARRAY_LEN(W.props)) return -1;
    Prop *p = &W.props[W.nprops];
    memset(p, 0, sizeof *p);
    p->spr = spr;
    p->x = x;
    p->y = y;
    p->layer = layer;
    p->cont = -1;
    p->tint = TINT_NONE;
    prop_has_fp[W.nprops] = false;
    return W.nprops++;
}

static void block_rect(Rect r, int flags) {
    for (int y = r.y0; y <= r.y1; y++)
        for (int x = r.x0; x <= r.x1; x++) {
            if (!in_map(x, y)) continue;
            Cell *c = cell(x, y);
            if (c->wall) continue;
            c->obj = OB_BLOCKER;
            c->flags |= flags;
        }
}

/* a prop that stands on the map: block its cells and remember them */
static void prop_block(int pi, Rect r, int flags) {
    block_rect(r, flags);
    if (pi < 0) return;
    prop_fp[pi] = r;
    prop_has_fp[pi] = true;
}

static int prop_covering(int x, int y) {
    for (int i = 0; i < W.nprops; i++) {
        Rect f = prop_fp[i];
        if (prop_has_fp[i] && x >= f.x0 && x <= f.x1 && y >= f.y0 && y <= f.y1) return i;
    }
    return -1;
}

/* take a prop off the map: free its footprint, empty its container, keep indices consistent */
static void prop_remove(int i) {
    if (prop_has_fp[i]) {
        Rect f = prop_fp[i];
        for (int y = f.y0; y <= f.y1; y++)
            for (int x = f.x0; x <= f.x1; x++)
                if (in_map(x, y) && cell(x, y)->obj == OB_BLOCKER) clear_at(x, y);
    }
    int last = --W.nprops;
    for (int c = 0; c < W.nconts; c++) {
        Container *k = &W.conts[c];
        if (k->prop == i) {
            k->prop = -1;
            if (in_map(k->tx, k->ty) && cell(k->tx, k->ty)->cont == c) cell(k->tx, k->ty)->cont = -1;
            k->n = 0;
            k->searched = true;
            k->dead = true;
        } else if (k->prop == last) k->prop = i;
    }
    W.props[i] = W.props[last];
    prop_fp[i] = prop_fp[last];
    prop_has_fp[i] = prop_has_fp[last];
}

/* clear a rect for something new: props standing there go whole, containers there go empty */
static void clear_rect(Rect r) {
    for (int y = r.y0; y <= r.y1; y++)
        for (int x = r.x0; x <= r.x1; x++) {
            if (!in_map(x, y)) continue;
            Cell *c = cell(x, y);
            int pc = c->obj == OB_BLOCKER ? prop_covering(x, y) : -1;
            if (pc >= 0) prop_remove(pc);
            if (c->cont >= 0) { W.conts[c->cont].dead = true; W.conts[c->cont].n = 0; }
            clear_at(x, y);
        }
}

static void decor(int spr, float x, float y, int flip, int alpha) {
    if (W.ndecor >= ARRAY_LEN(W.decor)) return;
    Decor *d = &W.decor[W.ndecor++];
    d->spr = (int16_t)spr;
    d->x = (int16_t)x;
    d->y = (int16_t)y;
    d->flip = (uint8_t)flip;
    d->alpha = (uint8_t)alpha;
}

static void overdecor(int spr, float x, float y, int flip) {
    if (W.noverdecor >= ARRAY_LEN(W.overdecor)) return;
    Decor *d = &W.overdecor[W.noverdecor++];
    d->spr = (int16_t)spr;
    d->x = (int16_t)x;
    d->y = (int16_t)y;
    d->flip = (uint8_t)flip;
    d->alpha = 255;
}

static void door_add(int tx, int ty, bool horizontal_wall, bool glass, int side) {
    clear_at(tx, ty);
    cell(tx, ty)->flags |= CF_DOOR;
    if (W.ndoors >= MAX_DOORS) return;
    Door *d = &W.doors[W.ndoors++];
    memset(d, 0, sizeof *d);
    d->len = 15;
    d->glass = glass;
    d->pusher = -1;
    if (horizontal_wall) {
        if (side == 0) { d->hinge = v2(tx * TILE + 0.5f, ty * TILE + 8); d->base = 0; }
        else { d->hinge = v2(tx * TILE + 15.5f, ty * TILE + 8); d->base = PI_F; }
    } else {
        if (side == 0) { d->hinge = v2(tx * TILE + 8, ty * TILE + 0.5f); d->base = PI_F * 0.5f; }
        else { d->hinge = v2(tx * TILE + 8, ty * TILE + 15.5f); d->base = -PI_F * 0.5f; }
    }
}

static void cart_add(V2 pos, float ang) {
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (c->alive) continue;
        memset(c, 0, sizeof *c);
        c->alive = true;
        c->pos = pos;
        c->angle = ang;
        c->holder = -1;
        c->pusher = -1;
        return;
    }
}

/* carts dropped before a car, a tree or a hedge bulge landed on them: nudge to the nearest free tile */
static void settle_carts(void) {
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *ct = &W.carts[i];
        if (!ct->alive) continue;
        int tx = tile_of(ct->pos.x), ty = tile_of(ct->pos.y);
        if (free_cell(tx, ty)) continue;
        bool moved = false;
        for (int r = 1; r <= 4 && !moved; r++)
            for (int oy = -r; oy <= r && !moved; oy++)
                for (int ox = -r; ox <= r && !moved; ox++)
                    if (free_cell(tx + ox, ty + oy)) { ct->pos = tile_center(tx + ox, ty + oy); moved = true; }
        if (!moved) ct->alive = false;
    }
}

static void light_prop(int pi) { if (pi >= 0) W.props[pi].glow = true; }

/* ----------------------------------------------------------- structure */
static void building_shell(Rect b, int wall, int fl) {
    for (int y = b.y0; y <= b.y1; y++)
        for (int x = b.x0; x <= b.x1; x++) {
            clear_at(x, y);
            if (x == b.x0 || x == b.x1 || y == b.y0 || y == b.y1) wall_at(x, y, wall);
            else floor_at(x, y, fl);
        }
    mark_indoor(b);
}

static void hwall(int x0, int x1, int y, int type) { for (int x = x0; x <= x1; x++) wall_at(x, y, type); }
static void vwall(int x, int y0, int y1, int type) { for (int y = y0; y <= y1; y++) wall_at(x, y, type); }

/* glass storefront along row y between x0..x1, with double doors at each x in doors[] */
static void storefront(int x0, int x1, int y, const int *doors, int nd, float broken) {
    for (int x = x0; x <= x1; x++) {
        bool pillar = ((x - x0) % 5) == 0 || x == x1;
        if (pillar) continue;
        clear_at(x, y);
        floor_at(x, y, cell(x, y + 1)->floor ? cell(x, y + 1)->floor : FL_SIDEWALK);
        if (rng_chance(&R, broken)) obj_at(x, y, OB_GLASS_BROKEN_H, 0);
        else obj_at(x, y, OB_GLASS_H, 0);
        cell(x, y)->flags |= CF_INDOOR;
    }
    for (int i = 0; i < nd; i++) {
        int dx = doors[i];
        for (int k = 0; k < 2; k++) {
            clear_at(dx + k, y);
            floor_at(dx + k, y, FL_CONCRETE);
            cell(dx + k, y)->flags |= CF_INDOOR;
        }
        door_add(dx, y, true, true, 0);
        door_add(dx + 1, y, true, true, 1);
        cell(dx, y + 1)->flags |= CF_NOSPAWN;
        cell(dx + 1, y + 1)->flags |= CF_NOSPAWN;
        cell(dx, y - 1)->flags |= CF_NOSPAWN;
        cell(dx + 1, y - 1)->flags |= CF_NOSPAWN;
    }
}

/* a run of shelves; vertical if v. dept zone. */
static void shelf_run(int x, int y, int len, bool v, int zone, int goods) {
    for (int i = 0; i < len; i++) {
        int tx = v ? x : x + i, ty = v ? y + i : y;
        if (!in_map(tx, ty) || cell(tx, ty)->wall) continue;
        int var = goods;
        if (i == 0) var = 16;
        else if (i == len - 1) var = 32;
        else if (rng_chance(&R, 0.35f)) var = rng_int(&R, 6);
        if (zone == Z_HARDWARE && !(var & 48)) var = rng_chance(&R, 0.7f) ? 4 : 3;
        if (zone == Z_PHARMACY && !(var & 48)) var = 5;
        if (zone == Z_DRINKS && !(var & 48)) var = rng_chance(&R, 0.7f) ? 2 : 0;
        if (rng_chance(&R, 0.07f) && !v && i > 0 && i < len - 1) {
            obj_at(tx, ty, OB_SHELF_FALLEN, rng_int(&R, 2));
            continue;
        }
        obj_at(tx, ty, v ? OB_SHELF_V : OB_SHELF_H, var);
        cell(tx, ty)->zone = (uint8_t)zone;
        if (!(var & 48)) cont_new(tx, ty, zone, "Shelf");
    }
}

/* aisles of shelves filling a rect; zones picked per column band */
static void fill_aisles(Rect r, const int *zones, int nz, bool vertical) {
    if (vertical) {
        int seg = rng_range(&R, 5, 8);
        for (int x = r.x0 + 1; x <= r.x1 - 1; x += 3) {
            int zi = (int)((float)(x - r.x0) / (rw(r) + 1) * nz);
            int zone = zones[CLAMP(zi, 0, nz - 1)];
            int goods = rng_int(&R, 4);
            int y = r.y0 + 1;
            while (y + 2 <= r.y1 - 1) {
                /* roll once: MINF evaluates its arguments twice, which let runs overshoot the rect */
                int roll = seg + rng_range(&R, -1, 2), len = MINF(roll, r.y1 - 1 - y + 1);
                if (len >= 3) shelf_run(x, y, len, true, zone, goods);
                y += len + 2;
            }
        }
    } else {
        int seg = rng_range(&R, 6, 10);
        for (int y = r.y0 + 1; y <= r.y1 - 1; y += 3) {
            int zi = (int)((float)(y - r.y0) / (rh(r) + 1) * nz);
            int zone = zones[CLAMP(zi, 0, nz - 1)];
            int goods = rng_int(&R, 4);
            int x = r.x0 + 1;
            while (x + 2 <= r.x1 - 1) {
                int roll = seg + rng_range(&R, -1, 3), len = MINF(roll, r.x1 - 1 - x + 1);
                if (len >= 3) shelf_run(x, y, len, false, zone, goods);
                x += len + 2;
            }
        }
    }
}

static void checkouts(int x0, int x1, int y, int avoid0, int avoid1) {
    for (int x = x0; x + 3 <= x1; x += 5) {
        if (x + 3 >= avoid0 - 1 && x <= avoid1 + 1) continue;
        obj_at(x, y, OB_REGISTER, 0);
        cont_new(x, y, Z_CHECKOUT, "Register");
        obj_at(x + 1, y, OB_COUNTER, 1);
        obj_at(x + 2, y, OB_COUNTER, 1);
        obj_at(x + 3, y, OB_COUNTER, 2);
    }
}

/* how full each fridge is gets rolled later by container_fill (Z_FRIDGE odds) */
static void fridges_along(int x0, int x1, int y, int obj) {
    for (int x = x0; x <= x1; x++) {
        if (!free_cell(x, y)) continue;
        obj_at(x, y, obj, 0);
        cont_new(x, y, Z_FRIDGE, "Fridge");
    }
}

static void fridges_col(int x, int y0, int y1, int obj) {
    for (int y = y0; y <= y1; y++) {
        if (!free_cell(x, y)) continue;
        obj_at(x, y, obj, 0);
        cont_new(x, y, Z_FRIDGE, "Fridge");
    }
}

static void scatter_obj(Rect r, int obj, int n, int zone, const char *name) {
    for (int i = 0; i < n; i++) {
        for (int t = 0; t < 30; t++) {
            int x = rng_range(&R, r.x0, r.x1), y = rng_range(&R, r.y0, r.y1);
            if (!free_cell(x, y)) continue;
            obj_at(x, y, obj, rng_int(&R, 3));
            if (zone) cont_new(x, y, zone, name);
            break;
        }
    }
}

static void camp(Rect r) {
    int cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
    if (!free_cell(cx, cy)) return;
    obj_at(cx, cy, OB_CAMPFIRE, 0);
    int p = prop_add(SPR_P_MATTRESS, cx * TILE - 30, cy * TILE - 6, 0);
    (void)p;
    for (int i = 0; i < 2; i++) {
        int x = cx + (i ? 2 : -2), y = cy + 1;
        if (free_cell(x, y)) {
            prop_add(SPR_P_SLEEPINGBAG, x * TILE, y * TILE - 4, 0);
        }
    }
    int bx = cx + 1, by = cy - 1;
    if (free_cell(bx, by)) {
        obj_at(bx, by, OB_BOX, 1);
        cont_new(bx, by, Z_CAMP, "Stash");
    }
    for (int i = 0; i < 4; i++)
        decor(SPR_O_TRASH + rng_int(&R, SPR_O_TRASH_N), cx * TILE + rng_range(&R, -24, 24),
              cy * TILE + rng_range(&R, -20, 20), 0, 0);
}

/* sky holes + vegetation reclaiming the interior */
static void decay(Rect r, int holes) {
    for (int h = 0; h < holes; h++) {
        int cx = rng_range(&R, r.x0 + 2, r.x1 - 2), cy = rng_range(&R, r.y0 + 2, r.y1 - 2);
        int rad = rng_range(&R, 1, 3);
        for (int y = cy - rad - 1; y <= cy + rad + 1; y++)
            for (int x = cx - rad - 1; x <= cx + rad + 1; x++) {
                if (!in_map(x, y) || x <= r.x0 || x >= r.x1 || y <= r.y0 || y >= r.y1) continue;
                float d = sqrtf((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
                if (d > rad + 0.6f) continue;
                Cell *c = cell(x, y);
                c->flags |= CF_SKY;
                if (c->wall || c->obj) continue;
                if (d < rad - 0.4f && rng_chance(&R, 0.55f)) floor_at(x, y, rng_chance(&R, 0.6f) ? FL_GRASS : FL_RUBBLE);
                V2 p = tile_center(x, y);
                if (rng_chance(&R, 0.8f)) decor(SPR_O_MOSS + rng_int(&R, SPR_O_MOSS_N), p.x + rng_range(&R, -6, 6), p.y + rng_range(&R, -6, 6), rng_int(&R, 2), 0);
                if (rng_chance(&R, 0.5f)) decor(SPR_O_GRASS_TUFT + rng_int(&R, SPR_O_GRASS_TUFT_N), p.x + rng_range(&R, -8, 8), p.y + rng_range(&R, -8, 8), 0, 0);
                if (rng_chance(&R, 0.3f)) decor(SPR_O_RUBBLE + rng_int(&R, SPR_O_RUBBLE_N), p.x, p.y, rng_int(&R, 2), 0);
            }
        V2 cp = tile_center(cx, cy);
        if (rng_chance(&R, 0.45f)) decor(SPR_O_PUDDLE + rng_int(&R, SPR_O_PUDDLE_N), cp.x, cp.y + 8, 0, 0);
        if (rad >= 2 && rng_chance(&R, 0.35f) && free_cell(cx, cy)) {
            /* a tree has pushed through the floor */
            obj_at(cx, cy, OB_BLOCKER, 0);
            floor_at(cx, cy, FL_DIRT);
            decor(SPR_O_ROOTS + rng_int(&R, SPR_O_ROOTS_N), cp.x, cp.y, rng_int(&R, 2), 0);
            int pi = prop_add(SPR_P_TREE + rng_int(&R, 2), cp.x, cp.y, 1);
            prop_block(pi, rect(cx, cy, cx, cy), 0);
            if (pi >= 0) W.props[pi].angle = rng_rangef(&R, 0, 6.28f);
        }
    }
    /* grime: cracks, trash, stains, moss along walls */
    for (int y = r.y0; y <= r.y1; y++)
        for (int x = r.x0; x <= r.x1; x++) {
            Cell *c = cell(x, y);
            if (c->wall) continue;
            V2 p = tile_center(x, y);
            if (rng_chance(&R, 0.07f)) decor(SPR_O_CRACK + rng_int(&R, SPR_O_CRACK_N), p.x, p.y, rng_int(&R, 2), 0);
            if (rng_chance(&R, 0.10f)) decor(SPR_O_TRASH + rng_int(&R, SPR_O_TRASH_N), p.x + rng_range(&R, -6, 6), p.y + rng_range(&R, -6, 6), 0, 0);
            if (rng_chance(&R, 0.025f)) decor(SPR_O_STAIN + rng_int(&R, SPR_O_STAIN_N), p.x, p.y, rng_int(&R, 2), 0);
            bool near_wall = cell_flag(x - 1, y, CF_SOLID) || cell_flag(x + 1, y, CF_SOLID) ||
                             cell_flag(x, y - 1, CF_SOLID) || cell_flag(x, y + 1, CF_SOLID);
            if (near_wall && rng_chance(&R, 0.06f))
                decor(SPR_O_MOSS + rng_int(&R, SPR_O_MOSS_N), p.x, p.y, rng_int(&R, 2), 0);
            if (c->wall == 0 && rng_chance(&R, 0.012f)) decor(SPR_O_LEAVES + rng_int(&R, SPR_O_LEAVES_N), p.x, p.y, rng_int(&R, 2), 0);
        }
}

/* vines over walls that touch the outside or sky */
static void vines(void) {
    for (int y = 0; y < W.h; y++)
        for (int x = 0; x < W.w; x++) {
            Cell *c = cell(x, y);
            if (!c->wall) continue;
            bool outside = false;
            for (int k = 0; k < 4; k++) {
                int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
                if (in_map(nx, ny) && !cell(nx, ny)->wall &&
                    (!(cell(nx, ny)->flags & CF_INDOOR) || (cell(nx, ny)->flags & CF_SKY)))
                    outside = true;
            }
            float ch = c->wall == WL_HEDGE ? 0.0f : (outside ? 0.22f : 0.03f);
            if (rng_chance(&R, ch)) {
                V2 p = tile_center(x, y);
                overdecor(SPR_O_VINES + rng_int(&R, SPR_O_VINES_N), p.x + rng_range(&R, -3, 3), p.y + rng_range(&R, -3, 3), rng_int(&R, 2));
            }
        }
}

/* ------------------------------------------------------------- outside */
static void border(int wall) {
    for (int x = 0; x < W.w; x++) { wall_at(x, 0, wall); wall_at(x, W.h - 1, wall); }
    for (int y = 0; y < W.h; y++) { wall_at(0, y, wall); wall_at(W.w - 1, y, wall); }
    /* organic bulges */
    for (int i = 0; i < W.w + W.h; i++) {
        int side = rng_int(&R, 4);
        int x, y;
        if (side == 0) { x = rng_range(&R, 1, W.w - 2); y = 1; }
        else if (side == 1) { x = rng_range(&R, 1, W.w - 2); y = W.h - 2; }
        else if (side == 2) { x = 1; y = rng_range(&R, 1, W.h - 2); }
        else { x = W.w - 2; y = rng_range(&R, 1, W.h - 2); }
        if (!cell(x, y)->wall && !cell(x, y)->obj && !(cell(x, y)->flags & (CF_INDOOR | CF_NOSPAWN | CF_EXIT)))
            wall_at(x, y, wall);
    }
}

static void tree_at(int tx, int ty) {
    if (!free_cell(tx, ty)) return;
    for (int y = ty - 1; y <= ty + 1; y++)
        for (int x = tx - 1; x <= tx + 1; x++)
            if (free_cell(x, y) && rng_chance(&R, 0.8f)) floor_at(x, y, rng_chance(&R, 0.6f) ? FL_GRASS : FL_DIRT);
    V2 p = tile_center(tx, ty);
    decor(SPR_O_ROOTS + rng_int(&R, SPR_O_ROOTS_N), p.x, p.y, rng_int(&R, 2), 0);
    int pi = prop_add(rng_chance(&R, 0.15f) ? SPR_P_DEADTREE : SPR_P_TREE + rng_int(&R, 2), p.x, p.y, 1);
    obj_at(tx, ty, OB_BLOCKER, 0);
    prop_block(pi, rect(tx, ty, tx, ty), 0);
    if (pi >= 0) W.props[pi].angle = rng_rangef(&R, 0, 6.28f);
}

static void car_at(int tx, int ty, bool flip) {
    for (int y = ty; y < ty + 3; y++)
        for (int x = tx; x < tx + 2; x++)
            if (!free_cell(x, y)) return;
    int spr = rng_chance(&R, 0.2f) ? SPR_P_CAR_BURNT : SPR_P_CAR + rng_int(&R, SPR_P_CAR_N);
    int pi = prop_add(spr, tx * TILE, ty * TILE, 0);
    if (pi >= 0 && flip) W.props[pi].angle = PI_F;
    prop_block(pi, rect(tx, ty, tx + 1, ty + 2), CF_SOLID | CF_SHOT);
    int ci = cont_new(tx, ty + 1, Z_CAR, "Car");
    if (ci >= 0) {
        W.conts[ci].pos = v2(tx * TILE + 16, ty * TILE + 24);
        W.conts[ci].prop = pi;
    }
}

static void parking(Rect lot, bool stalls) {
    floor_rect(lot, FL_ASPHALT);
    if (stalls) {
        for (int row = lot.y0 + 1; row + 2 <= lot.y1 - 6; row += 7) {
            for (int x = lot.x0 + 1; x <= lot.x1 - 1; x += 3) {
                for (int k = 0; k < 3; k++) floor_at(x, row + k, FL_ASPHALT_LINE);
                if (x + 2 < lot.x1 && rng_chance(&R, 0.26f)) car_at(x + 1, row, rng_chance(&R, 0.5f));
            }
            for (int x = lot.x0 + 1; x <= lot.x1 - 1; x += 3) {
                for (int k = 0; k < 3; k++) floor_at(x, row + 3 + k, FL_ASPHALT_LINE);
                if (x + 2 < lot.x1 && rng_chance(&R, 0.2f)) car_at(x + 1, row + 3, rng_chance(&R, 0.5f));
            }
        }
    }
    /* weeds and cracks everywhere */
    for (int y = lot.y0; y <= lot.y1; y++)
        for (int x = lot.x0; x <= lot.x1; x++) {
            V2 p = tile_center(x, y);
            if (rng_chance(&R, 0.12f)) decor(SPR_O_CRACK + rng_int(&R, SPR_O_CRACK_N), p.x, p.y, rng_int(&R, 2), 0);
            if (rng_chance(&R, 0.16f)) decor(SPR_O_GRASS_TUFT + rng_int(&R, SPR_O_GRASS_TUFT_N), p.x + rng_range(&R, -7, 7), p.y + rng_range(&R, -7, 7), 0, 0);
            if (rng_chance(&R, 0.03f)) decor(SPR_O_LEAVES + rng_int(&R, SPR_O_LEAVES_N), p.x, p.y, rng_int(&R, 2), 0);
            if (rng_chance(&R, 0.025f)) decor(SPR_O_TRASH + rng_int(&R, SPR_O_TRASH_N), p.x, p.y, 0, 0);
        }
    /* grass islands */
    for (int i = 0; i < rw(lot) * rh(lot) / 90 + 2; i++) {
        int cx = rng_range(&R, lot.x0, lot.x1), cy = rng_range(&R, lot.y0, lot.y1);
        int rr = rng_range(&R, 1, 3);
        for (int y = cy - rr; y <= cy + rr; y++)
            for (int x = cx - rr; x <= cx + rr; x++)
                if (in_map(x, y) && free_cell(x, y) && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= rr * rr + rng_int(&R, 2))
                    floor_at(x, y, FL_GRASS);
    }
    for (int i = 0; i < rw(lot) / 9 + 1; i++)
        tree_at(rng_range(&R, lot.x0 + 1, lot.x1 - 1), rng_range(&R, lot.y0 + 1, lot.y1 - 1));
    for (int i = 0; i < rw(lot) / 7; i++) {
        int x = rng_range(&R, lot.x0, lot.x1), y = rng_range(&R, lot.y0, lot.y1);
        if (free_cell(x, y)) obj_at(x, y, OB_BUSH, rng_int(&R, 3));
    }
    for (int x = lot.x0 + 3; x < lot.x1 - 2; x += rng_range(&R, 9, 13)) {
        int y = lot.y0 + rng_range(&R, 0, rh(lot) - 1);
        if (free_cell(x, y)) obj_at(x, y, OB_LAMPPOST, 0);
    }
    int carts = rng_range(&R, 2, 4);
    for (int i = 0; i < carts; i++) {
        int x = rng_range(&R, lot.x0 + 1, lot.x1 - 1), y = rng_range(&R, lot.y0, lot.y1 - 2);
        if (free_cell(x, y)) cart_add(tile_center(x, y), rng_rangef(&R, 0, 6.28f));
    }
    for (int i = 0; i < 2; i++) {
        int x = rng_range(&R, lot.x0 + 1, lot.x1 - 1), y = rng_range(&R, lot.y0, lot.y1);
        if (free_cell(x, y)) {
            int pi = prop_add(SPR_P_TIRE, x * TILE + 8, y * TILE + 8, 0);
            if (pi >= 0) W.props[pi].angle = rng_rangef(&R, 0, 6.28f);
        }
    }
}

/* keep the van's parking spot (and the air above it) free of cars, trees, lamps and carts:
 * call before dressing the lot, with the same arguments place_van gets */
static void reserve_van(int cx, int by) {
    int vx = cx, vy = by - 4;
    for (int y = vy - 3; y <= by; y++)
        for (int x = vx - 6; x <= vx + 4; x++)
            if (in_map(x, y)) cell(x, y)->flags |= CF_NOSPAWN;
}

/* the getaway van + exit zone at bottom centre */
static void place_van(int cx, int by) {
    int vx = cx, vy = by - 4;
    clear_rect(rect(vx - 4, vy - 1, vx + 3, vy + 4));
    for (int y = vy - 1; y <= vy + 4; y++)
        for (int x = vx - 4; x <= vx + 3; x++)
            if (in_map(x, y)) { floor_at(x, y, FL_ASPHALT); cell(x, y)->flags |= CF_NOSPAWN; }
    /* nothing may stand or hang over the parking spot (reserve_van normally keeps it empty) */
    float rx0 = (vx - 6) * TILE, rx1 = (vx + 5) * TILE, ry0 = (vy - 3) * TILE, ry1 = (vy + 6) * TILE;
    for (int i = 0; i < W.nprops; i++) {
        Prop *p = &W.props[i];
        float px = p->x, py = p->y;
        if (g_atlas[p->spr].px == 0 && g_atlas[p->spr].py == 0) { px += g_atlas[p->spr].w / 2; py += g_atlas[p->spr].h / 2; }
        if (px > rx0 && px < rx1 && py > ry0 && py < ry1) {
            prop_remove(i);
            i--;
        }
    }
    for (int i = 0; i < MAX_CARTS; i++)
        if (W.carts[i].alive && W.carts[i].pos.x > rx0 && W.carts[i].pos.x < rx1 && W.carts[i].pos.y > ry0 && W.carts[i].pos.y < ry1)
            W.carts[i].alive = false;
    int pi = prop_add(SPR_P_VAN, vx * TILE - 2, vy * TILE - 4, 0);
    prop_block(pi, rect(vx, vy, vx + 1, vy + 2), CF_SOLID | CF_SHOT);
    for (int y = vy; y <= vy + 2; y++)
        for (int x = vx - 3; x <= vx - 1; x++) cell(x, y)->flags |= CF_EXIT;
    W.van = v2(vx * TILE + 16, vy * TILE + 24);
    W.exit_rect = (SDL_FRect){(vx - 3) * TILE, vy * TILE, 3 * TILE, 3 * TILE};
    W.spawn = v2((vx - 2) * TILE + 8, (vy + 1) * TILE + 8);
}

/* ------------------------------------------------------------- rooms */
static void room_storage(Rect r) {
    floor_rect(r, FL_CONCRETE);
    set_zone(r, Z_STORAGE);
    int n = rw(r) * rh(r) / 7;
    for (int i = 0; i < n; i++) {
        int x = rng_range(&R, r.x0, r.x1), y = rng_range(&R, r.y0, r.y1);
        if (!free_cell(x, y)) continue;
        /* keep a walkable border next to doors */
        if (cell_flag(x, y - 1, CF_DOOR) || cell_flag(x, y + 1, CF_DOOR) || cell_flag(x - 1, y, CF_DOOR) || cell_flag(x + 1, y, CF_DOOR)) continue;
        int k = rng_int(&R, 10);
        if (k < 4) { obj_at(x, y, OB_BOX, rng_int(&R, 3)); if (rng_chance(&R, 0.6f)) cont_new(x, y, Z_STORAGE, "Box"); }
        else if (k < 7) { obj_at(x, y, OB_CRATE, 0); cont_new(x, y, Z_STORAGE, "Crate"); }
        else if (k < 9) { obj_at(x, y, OB_PALLET, 0); cont_new(x, y, Z_STORAGE, "Pallet"); }
        else obj_at(x, y, OB_BARREL, 0);
    }
}

static void room_office(Rect r) {
    floor_rect(r, FL_CARPET);
    set_zone(r, Z_OFFICE);
    int x = r.x0 + 1, y = r.y0 + 1;
    if (rw(r) >= 4 && free_cell(x, y) && free_cell(x + 1, y)) {
        int pi = prop_add(SPR_P_DESK, x * TILE, y * TILE, 0);
        prop_block(pi, rect(x, y, x + 1, y), CF_SOLID);
        int ci = cont_new(x, y, Z_OFFICE, "Desk");
        if (ci >= 0) { W.conts[ci].prop = pi; W.conts[ci].pos = v2(x * TILE + 16, y * TILE + 8); }
        int ch = prop_add(SPR_P_CHAIR, x * TILE + 16, (y + 1) * TILE + 6, 0);
        if (ch >= 0) W.props[ch].angle = -PI_F * 0.5f + rng_rangef(&R, -0.5f, 0.5f);
    }
    for (int i = 0; i < 2; i++) {
        int lx = r.x1 - i, ly = r.y0;
        if (free_cell(lx, ly)) { obj_at(lx, ly, OB_LOCKER, 0); cont_new(lx, ly, Z_OFFICE, "Cabinet"); }
    }
    if (free_cell(r.x0, r.y1)) obj_at(r.x0, r.y1, OB_TV, 0);
}

static void room_restroom(Rect r) {
    floor_rect(r, FL_WHITETILE);
    set_zone(r, Z_RESTROOM);
    for (int x = r.x0; x <= r.x1; x += 2) {
        if (free_cell(x, r.y0)) { obj_at(x, r.y0, OB_TOILET, 0); }
    }
    for (int x = r.x0 + 1; x <= r.x1; x += 3)
        if (free_cell(x, r.y1)) { obj_at(x, r.y1, OB_SINK, 0); cont_new(x, r.y1, Z_RESTROOM, "Cabinet"); }
}

static void room_staff(Rect r) {
    floor_rect(r, FL_WOOD);
    set_zone(r, Z_STAFF);
    for (int x = r.x0; x <= r.x1 && x < r.x0 + 4; x++)
        if (free_cell(x, r.y0)) { obj_at(x, r.y0, OB_LOCKER, 0); cont_new(x, r.y0, Z_STAFF, "Locker"); }
    if (free_cell(r.x1, r.y1)) { obj_at(r.x1, r.y1, OB_VENDING, 0); cont_new(r.x1, r.y1, Z_SNACKS, "Vending machine"); }
    int tx = (r.x0 + r.x1) / 2, ty = (r.y0 + r.y1) / 2 + 1;
    if (free_cell(tx, ty) && rw(r) >= 4) {
        prop_block(prop_add(SPR_P_TABLE, tx * TILE - 4, ty * TILE - 4, 0), rect(tx, ty, tx, ty), CF_SOLID);
    }
}

static void room_freezer(Rect r) {
    floor_rect(r, FL_CONCRETE);
    set_zone(r, Z_FRIDGE);
    fridges_along(r.x0, r.x1, r.y0, OB_FRIDGE_D);
    scatter_obj(rect(r.x0, r.y0 + 2, r.x1, r.y1), OB_CRATE, 2, Z_STORAGE, "Crate");
}

/* door helper in a horizontal wall at row y, somewhere in [x0,x1] */
static int hdoor_between(int x0, int x1, int y) {
    if (x1 < x0) return -1;
    int x = rng_range(&R, x0, x1);
    door_add(x, y, true, false, rng_int(&R, 2));
    floor_at(x, y, FL_CONCRETE);
    cell(x, y)->flags |= CF_INDOOR;
    return x;
}

static int vdoor_between(int x, int y0, int y1) {
    if (y1 < y0) return -1;
    int y = rng_range(&R, y0, y1);
    door_add(x, y, false, false, rng_int(&R, 2));
    floor_at(x, y, FL_CONCRETE);
    cell(x, y)->flags |= CF_INDOOR;
    return y;
}

/* split the back strip into rooms; returns number of rooms */
static void back_rooms(Rect strip, int divider_y, int wall, bool back_exit, int y_back_wall, bool has_freezer) {
    int x = strip.x0;
    int kinds[8];
    int nk = 0;
    kinds[nk++] = 0; /* storage first, big */
    kinds[nk++] = 1; /* office */
    kinds[nk++] = 2; /* restroom */
    kinds[nk++] = 3; /* staff */
    if (has_freezer) kinds[nk++] = 4;
    for (int i = nk - 1; i > 1; i--) { int j = rng_range(&R, 1, i); int t = kinds[i]; kinds[i] = kinds[j]; kinds[j] = t; }
    int k = 0;
    bool exit_made = false;
    while (x <= strip.x1 - 3) {
        int remaining = strip.x1 - x + 1;
        int width = kinds[k % nk] == 0 ? rng_range(&R, 8, 13) : rng_range(&R, 5, 8);
        if (remaining - width < 5) width = remaining;
        int x1 = MINF(x + width - 1, strip.x1);
        Rect room = rect(x, strip.y0, x1, strip.y1);
        int kind = kinds[k % nk];
        if (k >= nk) kind = rng_chance(&R, 0.6f) ? 0 : 3;
        switch (kind) {
        case 0: room_storage(room); break;
        case 1: room_office(room); break;
        case 2: room_restroom(room); break;
        case 3: room_staff(room); break;
        default: room_freezer(room); break;
        }
        /* door into the sales floor */
        hdoor_between(x + 1, x1 - 1, divider_y);
        if (x1 < strip.x1) {
            vwall(x1 + 1, strip.y0, strip.y1, wall);
            if (rng_chance(&R, 0.55f)) vdoor_between(x1 + 1, strip.y0, strip.y1);
        }
        if (back_exit && kind == 0 && !exit_made) {
            int dx = rng_range(&R, x + 1, x1 - 1);
            door_add(dx, y_back_wall, true, false, 0);
            floor_at(dx, y_back_wall, FL_CONCRETE);
            exit_made = true;
        }
        x = x1 + 2;
        k++;
    }
    if (back_exit && !exit_made) {
        int dx = rng_range(&R, strip.x0 + 1, strip.x1 - 1);
        door_add(dx, y_back_wall, true, false, 0);
    }
}

/* full standard store: shell + front + back rooms + sales floor */
typedef struct {
    int floor;
    const int *zones;
    int nz;
    bool vertical_aisles;
    int back_depth;
    bool freezer;
    int camps;
    int holes;
    int entrances;
} StoreStyle;

static Rect store(Rect b, const StoreStyle *st, int sign_spr) {
    building_shell(b, WL_BRICK, st->floor);
    W.building = (SDL_FRect){b.x0 * TILE, b.y0 * TILE, rw(b) * TILE, rh(b) * TILE};
    /* entrances */
    int doors[3];
    int nd = st->entrances;
    for (int i = 0; i < nd; i++) doors[i] = b.x0 + (rw(b) * (i + 1)) / (nd + 1) - 1;
    storefront(b.x0 + 1, b.x1 - 1, b.y1, doors, nd, 0.25f);
    /* sign over the entrance */
    if (sign_spr >= 0) {
        const AtlasSprite *a = &g_atlas[sign_spr];
        int pi = prop_add(sign_spr, (doors[0] + 1) * TILE - a->w / 2, b.y1 * TILE - 4, 1);
        light_prop(pi);
    }
    /* back rooms */
    int div = b.y0 + st->back_depth + 1;
    hwall(b.x0 + 1, b.x1 - 1, div, WL_CONCRETE);
    Rect strip = rect(b.x0 + 1, b.y0 + 1, b.x1 - 1, div - 1);
    back_rooms(strip, div, WL_CONCRETE, true, b.y0, st->freezer);
    /* sales floor */
    Rect sales = rect(b.x0 + 1, div + 1, b.x1 - 1, b.y1 - 1);
    set_zone(sales, Z_GROCERY);
    /* fridges along the side walls */
    fridges_col(sales.x0, sales.y0 + 1, sales.y0 + rh(sales) / 2, OB_FRIDGE_R);
    fridges_col(sales.x1, sales.y0 + 1, sales.y0 + rh(sales) / 2, OB_FRIDGE_L);
    /* checkouts near the front */
    int dmin = doors[0], dmax = doors[nd - 1] + 1;
    checkouts(sales.x0 + 2, sales.x1 - 2, sales.y1 - 3, dmin, dmax);
    /* aisles; a small shop keeps a two-tile walkway to the checkouts instead of three */
    Rect aisles = rect(sales.x0 + 2, sales.y0 + 1, sales.x1 - 2, sales.y1 - (rh(sales) >= 14 ? 6 : 5));
    if (rw(aisles) > 4 && rh(aisles) > 4) fill_aisles(aisles, st->zones, st->nz, st->vertical_aisles);
    for (int i = 0; i < st->camps; i++) {
        int cx = rng_chance(&R, 0.5f) ? sales.x0 + 3 : sales.x1 - 3;
        camp(rect(cx - 2, sales.y1 - 9, cx + 2, sales.y1 - 6));
    }
    decay(rect(b.x0, b.y0, b.x1, b.y1), st->holes);
    return sales;
}

/* ============================================================ levels */
static void base_outdoor(void) {
    for (int y = 0; y < W.h; y++)
        for (int x = 0; x < W.w; x++) {
            Cell *c = cell(x, y);
            memset(c, 0, sizeof *c);
            c->cont = -1;
            floor_at(x, y, rng_chance(&R, 0.7f) ? FL_GRASS : FL_DIRT);
        }
}

static void alley_dressing(Rect r) {
    floor_rect(r, FL_ASPHALT);
    for (int y = r.y0; y <= r.y1; y++)
        for (int x = r.x0; x <= r.x1; x++) {
            V2 p = tile_center(x, y);
            if (rng_chance(&R, 0.2f)) decor(SPR_O_GRASS_TUFT + rng_int(&R, SPR_O_GRASS_TUFT_N), p.x + rng_range(&R, -6, 6), p.y, 0, 0);
            if (rng_chance(&R, 0.06f)) decor(SPR_O_TRASH + rng_int(&R, SPR_O_TRASH_N), p.x, p.y, 0, 0);
            if (rng_chance(&R, 0.08f)) decor(SPR_O_MOSS + rng_int(&R, SPR_O_MOSS_N), p.x, p.y, rng_int(&R, 2), 0);
        }
    int n = MAXF(1, (rw(r) * rh(r)) / 160);
    for (int i = 0; i < n; i++) {
        int x = rng_range(&R, r.x0, r.x1 - 1), y = rng_range(&R, r.y0, r.y1 - 1);
        if (!free_cell(x, y) || !free_cell(x + 1, y) || !free_cell(x, y + 1) || !free_cell(x + 1, y + 1)) continue;
        int pi = prop_add(SPR_P_DUMPSTER, x * TILE, y * TILE + 4, 0);
        prop_block(pi, rect(x, y, x + 1, y + 1), CF_SOLID | CF_SHOT);
        int ci = cont_new(x, y, Z_STORAGE, "Dumpster");
        if (ci >= 0) { W.conts[ci].prop = pi; W.conts[ci].pos = v2(x * TILE + 16, y * TILE + 16); }
    }
    for (int i = 0; i < n; i++) {
        int x = rng_range(&R, r.x0, r.x1), y = rng_range(&R, r.y0, r.y1);
        if (free_cell(x, y) && rng_chance(&R, 0.6f)) {
            obj_at(x, y, OB_BARREL, rng_chance(&R, 0.35f) ? 1 : 0);
        }
    }
}

static void gen_gasstation(void) {
    base_outdoor();
    /* tall enough for a few short aisles between the back rooms and the counter */
    int bw = rng_range(&R, 20, 24), bh = rng_range(&R, 19, 20);
    int bx0 = (W.w - bw) / 2 + rng_range(&R, -3, 3), by0 = 4;
    Rect b = rect(bx0, by0, bx0 + bw - 1, by0 + bh - 1);
    static const int zones[] = {Z_GROCERY, Z_SNACKS, Z_DRINKS};
    StoreStyle st = {FL_LINO, zones, 3, true, 4, false, 0, 2, 1};
    int vanx = W.w / 2 + rng_range(&R, -6, 6);
    reserve_van(vanx, W.h - 2);
    /* pump islands claim their spot before the lot grows trees and carts */
    int py = b.y1 + 6, pumpx[3], npumps = 0;
    for (int i = 0; i < 3; i++) {
        int px = b.x0 + 3 + i * 6;
        if (px + 1 >= W.w - 2) break;
        pumpx[npumps++] = px;
        for (int y = py - 1; y <= py + 2; y++)
            for (int x = px - 1; x <= px + 1; x++)
                if (in_map(x, y)) cell(x, y)->flags |= CF_NOSPAWN;
    }
    /* outside first so the store overwrites */
    Rect lot = rect(1, b.y1 + 3, W.w - 2, W.h - 2);
    parking(lot, false);
    alley_dressing(rect(1, 1, W.w - 2, by0 - 1));
    alley_dressing(rect(1, by0, bx0 - 1, b.y1 + 2));
    alley_dressing(rect(b.x1 + 1, by0, W.w - 2, b.y1 + 2));
    floor_rect(rect(b.x0 - 1, b.y1 + 1, b.x1 + 1, b.y1 + 2), FL_SIDEWALK);
    store(b, &st, SPR_P_SIGN_QUICKSTOP);
    for (int i = 0; i < npumps; i++) {
        int px = pumpx[i];
        clear_rect(rect(px - 1, py - 1, px + 1, py + 2));
        floor_rect(rect(px - 1, py - 1, px + 1, py + 2), FL_CONCRETE);
        prop_block(prop_add(SPR_P_GASPUMP, px * TILE, py * TILE, 0), rect(px, py, px, py + 1), CF_SOLID | CF_SHOT);
    }
    place_van(vanx, W.h - 2);
    border(WL_HEDGE);
}

static void gen_market(void) {
    base_outdoor();
    int bw = MINF(W.w - 8, rng_range(&R, 40, 46)), bh = rng_range(&R, 24, 27);
    int bx0 = (W.w - bw) / 2, by0 = 4;
    Rect b = rect(bx0, by0, bx0 + bw - 1, by0 + bh - 1);
    static const int zones[] = {Z_GROCERY, Z_GROCERY, Z_SNACKS, Z_DRINKS, Z_GROCERY};
    StoreStyle st = {FL_LINO, zones, 5, true, 6, true, 1, 4, 2};
    int vanx = W.w / 2 + rng_range(&R, -8, 8);
    reserve_van(vanx, W.h - 2);
    Rect lot = rect(1, b.y1 + 3, W.w - 2, W.h - 2);
    parking(lot, true);
    alley_dressing(rect(1, 1, W.w - 2, by0 - 1));
    alley_dressing(rect(1, by0, bx0 - 1, b.y1 + 2));
    alley_dressing(rect(b.x1 + 1, by0, W.w - 2, b.y1 + 2));
    floor_rect(rect(b.x0 - 1, b.y1 + 1, b.x1 + 1, b.y1 + 2), FL_SIDEWALK);
    store(b, &st, SPR_P_SIGN_MARKET);
    place_van(vanx, W.h - 2);
    border(WL_HEDGE);
}

static void gen_hardware(void) {
    base_outdoor();
    int bw = MINF(W.w - 16, rng_range(&R, 44, 48)), bh = rng_range(&R, 27, 30);
    int bx0 = 3, by0 = 4;
    Rect b = rect(bx0, by0, bx0 + bw - 1, by0 + bh - 1);
    static const int zones[] = {Z_HARDWARE, Z_HARDWARE, Z_ELECTRONICS, Z_HARDWARE};
    StoreStyle st = {FL_CONCRETE, zones, 4, false, 6, false, 1, 4, 2};
    int vanx = W.w / 2 + rng_range(&R, -8, 8);
    reserve_van(vanx, W.h - 2);
    Rect lot = rect(1, b.y1 + 3, W.w - 2, W.h - 2);
    parking(lot, true);
    alley_dressing(rect(1, 1, W.w - 2, by0 - 1));
    floor_rect(rect(b.x0 - 1, b.y1 + 1, b.x1 + 1, b.y1 + 2), FL_SIDEWALK);
    Rect sales = store(b, &st, SPR_P_SIGN_HARDWARE);
    /* pegboards along the side walls */
    for (int y = sales.y0 + 1; y < sales.y1 - 4; y++) {
        if (free_cell(sales.x0, y) && rng_chance(&R, 0.7f)) { obj_at(sales.x0, y, OB_PEGBOARD, 0); cont_new(sales.x0, y, Z_HARDWARE, "Tool wall"); }
    }
    /* lumber stacks */
    for (int i = 0; i < 4; i++) {
        int x = rng_range(&R, sales.x0 + 2, sales.x1 - 3), y = rng_range(&R, sales.y0 + 1, sales.y1 - 6);
        if (free_cell(x, y) && free_cell(x + 1, y)) {
            prop_block(prop_add(SPR_P_LUMBER, x * TILE, y * TILE, 0), rect(x, y, x + 1, y), CF_SOLID | CF_SHOT);
        }
    }
    /* garden centre: hedged yard on the right */
    Rect gc = rect(b.x1 + 2, by0 + 2, W.w - 3, b.y1);
    if (rw(gc) >= 6) {
        floor_rect(gc, FL_DIRT);
        for (int y = gc.y0; y <= gc.y1; y++)
            for (int x = gc.x0; x <= gc.x1; x++) {
                if (rng_chance(&R, 0.45f)) floor_at(x, y, FL_GRASS);
                if (x == gc.x0 || x == gc.x1 || y == gc.y0) { if (rng_chance(&R, 0.85f)) wall_at(x, y, WL_HEDGE); }
                else if (rng_chance(&R, 0.12f) && free_cell(x, y)) obj_at(x, y, OB_BUSH, rng_int(&R, 3));
            }
        set_zone(gc, Z_GARDEN);
        for (int i = 0; i < 5; i++) {
            int x = rng_range(&R, gc.x0 + 1, gc.x1 - 1), y = rng_range(&R, gc.y0 + 1, gc.y1 - 1);
            if (free_cell(x, y)) { obj_at(x, y, OB_CRATE, 0); cont_new(x, y, Z_GARDEN, "Garden crate"); }
        }
        /* side door from the store into the garden */
        int dy = rng_range(&R, sales.y0 + 2, sales.y1 - 2);
        door_add(b.x1, dy, false, false, 0);
        clear_at(b.x1 + 1, dy);
        floor_at(b.x1 + 1, dy, FL_DIRT);
        clear_at(gc.x0, dy);
        floor_at(gc.x0, dy, FL_DIRT);
        tree_at(rng_range(&R, gc.x0 + 2, gc.x1 - 2), rng_range(&R, gc.y0 + 2, gc.y1 - 2));
    }
    place_van(vanx, W.h - 2);
    border(WL_RUIN);
}

static void pharmacy_shop(Rect r) {
    /* pharmacy counter across the back with meds shelves behind */
    int cy = r.y0 + 3;
    for (int x = r.x0 + 2; x <= r.x1 - 2; x++) obj_at(x, cy, OB_COUNTER, x == r.x0 + 2 ? 0 : (x == r.x1 - 2 ? 2 : 1));
    clear_at(r.x0 + 1, cy);
    shelf_run(r.x0 + 1, r.y0, rw(r) - 2, false, Z_PHARMACY, 5);
    set_zone(rect(r.x0, r.y0, r.x1, cy), Z_PHARMACY);
    static const int zones[] = {Z_PHARMACY, Z_GROCERY, Z_PHARMACY};
    fill_aisles(rect(r.x0 + 1, cy + 2, r.x1 - 1, r.y1 - 4), zones, 3, true);
    checkouts(r.x0 + 1, r.x1 - 1, r.y1 - 2, r.x0 + rw(r) / 2 - 2, r.x0 + rw(r) / 2 + 1);
}

static void liquor_shop(Rect r) {
    static const int zones[] = {Z_DRINKS};
    fill_aisles(rect(r.x0 + 1, r.y0 + 1, r.x1 - 1, r.y1 - 4), zones, 1, true);
    fridges_along(r.x0 + 1, r.x1 - 1, r.y0, OB_FRIDGE_D);
    obj_at(r.x0 + 1, r.y1 - 2, OB_REGISTER, 0);
    cont_new(r.x0 + 1, r.y1 - 2, Z_CHECKOUT, "Register");
    obj_at(r.x0 + 2, r.y1 - 2, OB_COUNTER, 2);
}

static void laundromat_shop(Rect r) {
    floor_rect(r, FL_WHITETILE);
    for (int x = r.x0 + 1; x <= r.x1 - 1; x++) {
        if (free_cell(x, r.y0)) { obj_at(x, r.y0, OB_VENDING, 1); if (rng_chance(&R, 0.3f)) cont_new(x, r.y0, Z_CLOTHES, "Washer"); }
    }
    for (int x = r.x0 + 2; x <= r.x1 - 2; x += 3)
        for (int y = r.y0 + 3; y <= r.y1 - 3; y += 3)
            if (free_cell(x, y)) { obj_at(x, y, OB_RACK, 0); cont_new(x, y, Z_CLOTHES, "Clothes"); }
    camp(rect(r.x0 + 1, r.y1 - 5, r.x1 - 1, r.y1 - 2));
}

static void gen_pharmacy(void) {
    base_outdoor();
    int bw = MINF(W.w - 6, 66), bh = rng_range(&R, 22, 25);
    int bx0 = (W.w - bw) / 2, by0 = 6;
    Rect b = rect(bx0, by0, bx0 + bw - 1, by0 + bh - 1);
    int vanx = W.w / 2 + rng_range(&R, -10, 10);
    reserve_van(vanx, W.h - 2);
    Rect lot = rect(1, b.y1 + 3, W.w - 2, W.h - 2);
    parking(lot, true);
    alley_dressing(rect(1, 1, W.w - 2, by0 - 1));
    alley_dressing(rect(1, by0, bx0 - 1, b.y1 + 2));
    alley_dressing(rect(b.x1 + 1, by0, W.w - 2, b.y1 + 2));
    floor_rect(rect(b.x0 - 1, b.y1 + 1, b.x1 + 1, b.y1 + 2), FL_SIDEWALK);
    building_shell(b, WL_BRICK, FL_WHITETILE);
    W.building = (SDL_FRect){b.x0 * TILE, b.y0 * TILE, rw(b) * TILE, rh(b) * TILE};
    /* service corridor along the back */
    int cy = b.y0 + 3;
    hwall(b.x0 + 1, b.x1 - 1, cy, WL_CONCRETE);
    floor_rect(rect(b.x0 + 1, b.y0 + 1, b.x1 - 1, cy - 1), FL_CONCRETE);
    set_zone(rect(b.x0 + 1, b.y0 + 1, b.x1 - 1, cy - 1), Z_STORAGE);
    scatter_obj(rect(b.x0 + 1, b.y0 + 1, b.x1 - 1, b.y0 + 1), OB_BOX, rw(b) / 6, Z_STORAGE, "Box");
    door_add(b.x0, b.y0 + 2, false, false, 0);
    door_add(b.x1, b.y0 + 2, false, false, 1);
    /* three shops */
    int w1 = rw(b) * 45 / 100, w2 = rw(b) * 25 / 100;
    int x1 = b.x0 + w1, x2 = x1 + w2;
    vwall(x1, cy, b.y1, WL_BRICK);
    vwall(x2, cy, b.y1, WL_BRICK);
    Rect s1 = rect(b.x0 + 1, cy + 1, x1 - 1, b.y1 - 1);
    Rect s2 = rect(x1 + 1, cy + 1, x2 - 1, b.y1 - 1);
    Rect s3 = rect(x2 + 1, cy + 1, b.x1 - 1, b.y1 - 1);
    floor_rect(s1, FL_WHITETILE);
    floor_rect(s2, FL_WOOD);
    pharmacy_shop(s1);
    liquor_shop(s2);
    laundromat_shop(s3);
    hdoor_between(s1.x0 + 1, s1.x1 - 1, cy);
    hdoor_between(s2.x0 + 1, s2.x1 - 1, cy);
    hdoor_between(s3.x0 + 1, s3.x1 - 1, cy);
    int d1[1] = {s1.x0 + rw(s1) / 2 - 1}, d2[1] = {s2.x0 + rw(s2) / 2 - 1}, d3[1] = {s3.x0 + rw(s3) / 2 - 1};
    storefront(s1.x0, s1.x1, b.y1, d1, 1, 0.3f);
    storefront(s2.x0, s2.x1, b.y1, d2, 1, 0.3f);
    storefront(s3.x0, s3.x1, b.y1, d3, 1, 0.3f);
    int pi = prop_add(SPR_P_SIGN_PHARMACY, (d1[0] + 1) * TILE - 32, b.y1 * TILE - 4, 1);
    light_prop(pi);
    decay(b, 5);
    place_van(vanx, W.h - 2);
    border(WL_RUIN);
}

static void gen_megamart(void) {
    base_outdoor();
    int bw = W.w - 8, bh = rng_range(&R, 40, 44);
    int bx0 = 4, by0 = 4;
    Rect b = rect(bx0, by0, bx0 + bw - 1, by0 + bh - 1);
    static const int zones[] = {Z_GROCERY, Z_GROCERY, Z_DRINKS, Z_SNACKS, Z_PHARMACY, Z_ELECTRONICS, Z_HARDWARE, Z_HARDWARE};
    StoreStyle st = {FL_LINO, zones, 8, true, 8, true, 2, 8, 3};
    int vanx = W.w / 2 + rng_range(&R, -14, 14);
    reserve_van(vanx, W.h - 2);
    Rect lot = rect(1, b.y1 + 3, W.w - 2, W.h - 2);
    parking(lot, true);
    alley_dressing(rect(1, 1, W.w - 2, by0 - 1));
    floor_rect(rect(b.x0 - 1, b.y1 + 1, b.x1 + 1, b.y1 + 2), FL_SIDEWALK);
    Rect sales = store(b, &st, SPR_P_SIGN_MEGAMART);
    /* freezer islands in the middle aisle gaps */
    for (int i = 0; i < 4; i++) {
        int x = rng_range(&R, sales.x0 + 2, sales.x1 - 4), y = rng_range(&R, sales.y0 + 2, sales.y1 - 6);
        if (free_cell(x, y) && free_cell(x + 1, y)) {
            int pi = prop_add(SPR_P_FREEZER + rng_int(&R, 2), x * TILE, y * TILE, 0);
            prop_block(pi, rect(x, y, x + 1, y), CF_SOLID);
            int ci = cont_new(x, y, Z_FRIDGE, "Freezer");
            if (ci >= 0) { W.conts[ci].prop = pi; W.conts[ci].pos = v2(x * TILE + 16, y * TILE + 8); }
        }
    }
    place_van(vanx, W.h - 2);
    border(WL_HEDGE);
}

static void mall_shop(Rect r, int kind, bool door_bottom, int concourse_side) {
    /* kind: 0 clothes, 1 electronics, 2 sports, 3 food, 4 toys/general */
    int fl[] = {FL_CARPET, FL_WHITETILE, FL_WOOD, FL_WHITETILE, FL_LINO};
    floor_rect(r, fl[kind]);
    switch (kind) {
    case 0:
        set_zone(r, Z_CLOTHES);
        for (int y = r.y0 + 1; y <= r.y1 - 1; y += 2)
            for (int x = r.x0 + 1; x <= r.x1 - 1; x += 2)
                if (free_cell(x, y) && rng_chance(&R, 0.55f)) {
                    if (rng_chance(&R, 0.2f)) obj_at(x, y, OB_MANNEQUIN, 0);
                    else { obj_at(x, y, OB_RACK, 0); cont_new(x, y, Z_CLOTHES, "Clothes rack"); }
                }
        break;
    case 1: {
        set_zone(r, Z_ELECTRONICS);
        static const int z[] = {Z_ELECTRONICS};
        /* first shelf row one lower: a walkway in front of the TV wall instead of sealed pockets */
        fill_aisles(rect(r.x0, r.y0 + 1, r.x1, r.y1), z, 1, false);
        for (int x = r.x0; x <= r.x1; x++) if (free_cell(x, r.y0) && rng_chance(&R, 0.5f)) obj_at(x, r.y0, OB_TV, 0);
        break;
    }
    case 2: {
        set_zone(r, Z_STAFF);
        static const int z[] = {Z_STAFF, Z_HARDWARE};
        fill_aisles(r, z, 2, true);
        break;
    }
    case 3:
        set_zone(r, Z_SNACKS);
        for (int x = r.x0 + 1; x <= r.x1 - 1; x++) obj_at(x, r.y0 + 2, OB_COUNTER, x == r.x0 + 1 ? 0 : (x == r.x1 - 1 ? 2 : 1));
        fridges_along(r.x0, r.x1, r.y0, OB_FRIDGE_D);
        scatter_obj(rect(r.x0, r.y0 + 4, r.x1, r.y1), OB_BOX, 2, Z_SNACKS, "Box");
        break;
    default: {
        static const int z[] = {Z_GROCERY, Z_STORAGE};
        fill_aisles(r, z, 2, true);
        break;
    }
    }
    (void)door_bottom;
    (void)concourse_side;
}

/* row nearest `want` in y0..y1 whose inside cell (column inx) isn't wall */
static int side_door_row(int inx, int y0, int y1, int want) {
    for (int d = 0; d <= y1 - y0; d++)
        for (int s = -1; s <= 1; s += 2) {
            int y = want + d * s;
            if (y >= y0 && y <= y1 && !cell(inx, y)->wall) return y;
        }
    return want;
}

static void gen_mall(void) {
    base_outdoor();
    int bw = MINF(W.w - 24, 52), bh = W.h - 20;
    int bx0 = (W.w - bw) / 2, by0 = 3;
    Rect b = rect(bx0, by0, bx0 + bw - 1, by0 + bh - 1);
    int vanx = W.w / 2 + rng_range(&R, -12, 12);
    reserve_van(vanx, W.h - 2);
    Rect lot = rect(1, b.y1 + 3, W.w - 2, W.h - 2);
    parking(lot, true);
    /* overgrown flanks with abandoned cars */
    Rect lf = rect(1, 1, bx0 - 2, b.y1 + 2), rf = rect(b.x1 + 2, 1, W.w - 2, b.y1 + 2);
    parking(lf, false);
    parking(rf, false);
    for (int i = 0; i < 4; i++) {
        car_at(rng_range(&R, lf.x0 + 1, lf.x1 - 2), rng_range(&R, lf.y0 + 1, lf.y1 - 3), rng_chance(&R, 0.5f));
        car_at(rng_range(&R, rf.x0 + 1, rf.x1 - 2), rng_range(&R, rf.y0 + 1, rf.y1 - 3), rng_chance(&R, 0.5f));
    }
    alley_dressing(rect(bx0 - 1, 1, b.x1 + 1, by0 - 1));
    floor_rect(rect(b.x0 - 1, b.y1 + 1, b.x1 + 1, b.y1 + 2), FL_SIDEWALK);
    building_shell(b, WL_BRICK, FL_MALL);
    W.building = (SDL_FRect){b.x0 * TILE, b.y0 * TILE, rw(b) * TILE, rh(b) * TILE};
    int cx = (b.x0 + b.x1) / 2;
    int half = 5;                     /* concourse half width */
    int garden_h = 14;
    Rect garden = rect(b.x0 + 1, b.y0 + 1, b.x1 - 1, b.y0 + garden_h);
    /* wall separating the garden centre (the King's court) */
    hwall(b.x0 + 1, b.x1 - 1, garden.y1 + 1, WL_MALL);
    for (int x = cx - 2; x <= cx + 1; x++) { clear_at(x, garden.y1 + 1); floor_at(x, garden.y1 + 1, FL_MALL); }
    floor_rect(garden, FL_DIRT);
    set_zone(garden, Z_GARDEN);
    for (int y = garden.y0; y <= garden.y1; y++)
        for (int x = garden.x0; x <= garden.x1; x++) {
            if (rng_chance(&R, 0.5f)) floor_at(x, y, FL_GRASS);
            cell(x, y)->flags |= CF_SKY;
        }
    for (int i = 0; i < 8; i++) {
        int x = rng_range(&R, garden.x0 + 2, garden.x1 - 4), y = rng_range(&R, garden.y0 + 1, garden.y1 - 3);
        if (abs(x - cx) < 6 && y > garden.y0 + 2) continue;
        if (free_cell(x, y) && free_cell(x + 1, y) && free_cell(x, y + 1) && free_cell(x + 1, y + 1)) {
            prop_block(prop_add(SPR_P_PLANTER, x * TILE, y * TILE, 0), rect(x, y, x + 1, y + 1), CF_SOLID);
        }
    }
    for (int i = 0; i < 6; i++) {
        int x = rng_range(&R, garden.x0 + 1, garden.x1 - 1), y = rng_range(&R, garden.y0 + 1, garden.y1 - 1);
        if (free_cell(x, y)) { obj_at(x, y, OB_CRATE, 0); cont_new(x, y, Z_GARDEN, "Seed crate"); }
    }
    for (int i = 0; i < 10; i++) {
        int x = rng_range(&R, garden.x0 + 1, garden.x1 - 1), y = rng_range(&R, garden.y0 + 1, garden.y1 - 1);
        if (free_cell(x, y) && abs(x - cx) > 3) obj_at(x, y, OB_BUSH, rng_int(&R, 3));
    }
    /* the throne */
    int ty = garden.y0 + 2;
    clear_rect(rect(cx - 2, ty - 1, cx + 1, ty + 2));
    prop_block(prop_add(SPR_P_THRONE, (cx - 1) * TILE, ty * TILE, 0), rect(cx - 1, ty, cx, ty + 1), CF_SOLID);
    for (int k = -1; k <= 1; k += 2) {
        int bx = cx + k * 4;
        if (free_cell(bx, ty + 1)) obj_at(bx, ty + 1, OB_BARREL, 1);
    }
    /* concourse */
    Rect conc = rect(cx - half, garden.y1 + 2, cx + half - 1, b.y1 - 1);
    floor_rect(conc, FL_MALL);
    int fy = conc.y0 + rh(conc) / 3;
    prop_block(prop_add(SPR_P_FOUNTAIN, (cx - 1.5f) * TILE, fy * TILE, 0), rect(cx - 1, fy, cx + 1, fy + 2), CF_SOLID);
    /* food court: tables in the lower concourse */
    for (int y = fy + 6; y < conc.y1 - 4; y += 3)
        for (int x = conc.x0 + 1; x <= conc.x1 - 1; x += 4) {
            if (!free_cell(x, y) || rng_chance(&R, 0.25f)) continue;
            prop_block(prop_add(SPR_P_TABLE, x * TILE - 4, y * TILE - 4, 0), rect(x, y, x, y), CF_SOLID);
        }
    for (int y = conc.y0 + 2; y < fy - 1; y += 4) {
        if (free_cell(conc.x0 + 1, y) && free_cell(conc.x0 + 2, y) && free_cell(conc.x0 + 1, y + 1) && free_cell(conc.x0 + 2, y + 1)) {
            prop_block(prop_add(SPR_P_PLANTER, (conc.x0 + 1) * TILE, y * TILE, 0), rect(conc.x0 + 1, y, conc.x0 + 2, y + 1), CF_SOLID);
        }
        if (free_cell(conc.x1 - 2, y + 2) && free_cell(conc.x1 - 1, y + 2)) {
            prop_block(prop_add(SPR_P_BENCH, (conc.x1 - 2) * TILE, (y + 2) * TILE + 2, 0), rect(conc.x1 - 2, y + 2, conc.x1 - 1, y + 2), CF_SOLID);
        }
    }
    prop_block(prop_add(SPR_P_ESCALATOR, (conc.x1 - 1) * TILE, (fy + 1) * TILE, 0), rect(conc.x1 - 1, fy + 1, conc.x1, fy + 3), CF_SOLID);
    /* shops on both sides */
    int kinds[] = {0, 1, 2, 3, 4, 0, 3, 1, 2};
    for (int side = 0; side < 2; side++) {
        int sx0 = side == 0 ? b.x0 + 1 : cx + half + 1;
        int sx1 = side == 0 ? cx - half - 2 : b.x1 - 1;
        int wallx = side == 0 ? cx - half - 1 : cx + half;
        vwall(wallx, conc.y0, conc.y1, WL_MALL);
        int y = conc.y0;
        int k = side * 4 + rng_int(&R, 3);
        while (y < conc.y1 - 4) {
            int h = rng_range(&R, 8, 11);
            int y1 = MINF(y + h - 1, conc.y1);
            if (conc.y1 - y1 < 6) y1 = conc.y1;
            Rect shop = rect(sx0, y, sx1, y1);
            if (y1 < conc.y1) hwall(sx0, sx1, y1 + 1, WL_MALL);
            mall_shop(shop, kinds[k % 9], false, side);
            /* wide opening onto the concourse with a glass display beside it - never behind the escalator */
            int olo = y + 1, ohi = MAXF(y + 1, y1 - 4), cside = side == 0 ? wallx + 1 : wallx - 1;
            int o0 = rng_range(&R, olo, ohi), oy = o0, best = -1;
            for (int t = 0; t <= ohi - olo; t++) {
                int cand = olo + (o0 - olo + t) % (ohi - olo + 1), open = 0;
                for (int k2 = 0; k2 < 3; k2++) if (!cell_flag(cside, MINF(cand + k2, y1), CF_SOLID)) open++;
                if (open > best) { best = open; oy = cand; }
            }
            for (int k2 = 0; k2 < 3; k2++) {
                int yy = MINF(oy + k2, y1);
                clear_at(wallx, yy);
                floor_at(wallx, yy, FL_MALL);
            }
            for (int gy = y; gy <= y1; gy++) {
                if (gy >= oy - 1 && gy <= oy + 3) continue;
                if (rng_chance(&R, 0.45f)) { clear_at(wallx, gy); obj_at(wallx, gy, rng_chance(&R, 0.3f) ? OB_GLASS_BROKEN_V : OB_GLASS_V, 0); cell(wallx, gy)->flags |= CF_INDOOR; }
            }
            if (y1 < conc.y1 && rng_chance(&R, 0.5f)) hdoor_between(sx0 + 1, sx1 - 1, y1 + 1);
            y = y1 + 2;
            k++;
        }
    }
    /* mall entrance: wide glass front */
    int doors[2] = {cx - 4, cx + 2};
    storefront(b.x0 + 1, b.x1 - 1, b.y1, doors, 2, 0.3f);
    int pi = prop_add(SPR_P_SIGN_MALL, cx * TILE - 40, b.y1 * TILE - 4, 1);
    light_prop(pi);
    /* side doors to the flanks, opening into a shop rather than a wall between two */
    int sdy = side_door_row(b.x0 + 1, conc.y0 + 1, conc.y1 - 1, (conc.y0 + conc.y1) / 2);
    int sdy2 = side_door_row(b.x1 - 1, conc.y0 + 1, conc.y1 - 1, (conc.y0 + conc.y1) / 2 + 3);
    door_add(b.x0, sdy, false, false, 0);
    door_add(b.x1, sdy2, false, false, 1);
    clear_rect(rect(b.x0 - 1, sdy, b.x0 - 1, sdy)); floor_at(b.x0 - 1, sdy, FL_ASPHALT);
    clear_rect(rect(b.x1 + 1, sdy2, b.x1 + 1, sdy2)); floor_at(b.x1 + 1, sdy2, FL_ASPHALT);
    /* the King's lights: burning barrels along the concourse, against solid wall (not a shop front) */
    for (int y = conc.y0 + 2; y < conc.y1; y += 7) {
        int x = rng_chance(&R, 0.5f) ? conc.x0 : conc.x1;
        if (free_cell(x, y) && cell(x == conc.x0 ? x - 1 : x + 1, y)->wall) obj_at(x, y, OB_BARREL, 1);
    }
    decay(b, 6);
    place_van(vanx, W.h - 2);
    border(WL_RUIN);
}

/* ============================================================ reachability */
static void compute_reach(void) {
    static int q[MAP_MAX_W * MAP_MAX_H];
    for (int y = 0; y < W.h; y++)
        for (int x = 0; x < W.w; x++) cell(x, y)->reach = 0;
    int sx = tile_of(W.spawn.x), sy = tile_of(W.spawn.y);
    int head = 0, tail = 0;
    cell(sx, sy)->reach = 1;
    q[tail++] = sy * MAP_MAX_W + sx;
    while (head < tail) {
        int c = q[head++];
        int x = c % MAP_MAX_W, y = c / MAP_MAX_W;
        for (int k = 0; k < 4; k++) {
            int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
            if (!in_map(nx, ny)) continue;
            Cell *n = cell(nx, ny);
            if (n->reach) continue;
            /* glass can be smashed, so it doesn't block reachability */
            bool pass = !(n->flags & CF_SOLID) || n->obj == OB_GLASS_H || n->obj == OB_GLASS_V;
            if (!pass) continue;
            n->reach = 1;
            q[tail++] = ny * MAP_MAX_W + nx;
        }
    }
}

/* someone can step up to it: an open, reachable tile orthogonally next to the container - for a big prop,
 * next to any cell of its footprint and close enough to its centre to search. A corner touch doesn't count. */
static bool cont_reachable(Container *c) {
    if (c->dead) return false;
    Rect f = rect(c->tx, c->ty, c->tx, c->ty);
    if (c->prop >= 0 && prop_has_fp[c->prop]) f = prop_fp[c->prop];
    for (int y = f.y0; y <= f.y1; y++)
        for (int x = f.x0; x <= f.x1; x++)
            for (int k = 0; k < 4; k++) {
                int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
                if (!in_map(nx, ny) || !cell(nx, ny)->reach || (cell(nx, ny)->flags & CF_SOLID)) continue;
                if (c->prop >= 0 && v2_dist(tile_center(nx, ny), c->pos) > 36) continue;
                return true;
            }
    return false;
}

/* still searchable through its cell (or its prop)? anything a later pass built over has lost it */
static bool cont_linked(int i) {
    Container *c = &W.conts[i];
    if (c->prop >= 0) return c->prop < W.nprops;
    return in_map(c->tx, c->ty) && cell(c->tx, c->ty)->cont == i;
}

/* ============================================================ population */
static int spawn_npc(int arch, V2 pos) {
    int i = actor_spawn(arch, pos);
    if (i < 0) return -1;
    Actor *a = &W.actors[i];
    const ArchDef *ad = &ARCH[arch];
    /* actor_spawn rolls these on the fx RNG; re-roll them from the level seed so a retry meets the same people */
    a->face = rng_rangef(&R, -PI_F, PI_F);
    a->speed_mul = rng_rangef(&R, 0.92f, 1.08f);
    a->br.think = rng_rangef(&R, 0, 0.5f);
    a->br.strafe_dir = rng_chance(&R, 0.5f) ? 1.0f : -1.0f;
    a->temper = ad->temper;
    if (arch == AR_SCAV && rng_chance(&R, 0.35f)) a->temper = TEMP_DEFENSIVE;
    if (arch == AR_LOOTER && rng_chance(&R, 0.3f)) a->temper = TEMP_TIMID;
    ItemId wpn = ad->weapons[rng_int(&R, 6)];
    if (W.level <= 1 && (wpn == IT_SHOTGUN || wpn == IT_RIFLE || wpn == IT_CHAINSAW)) wpn = IT_PISTOL;
    if (arch == AR_PIG && wpn == IT_CHAINSAW && W.level < 4) wpn = IT_CLEAVER;
    if (arch == AR_RAIDER && W.level >= 2 && rng_chance(&R, 0.2f)) wpn = IT_PISTOL;
    if (arch == AR_BOSS) wpn = IT_SHOTGUN;
    if (wpn != IT_NONE) {
        Stack s = {wpn, 1, 0};
        const WeaponDef *wd = item_weapon(wpn);
        if (wd->kind == WK_GUN) s.cond = (int16_t)(wd->mag);
        else if (wd->kind == WK_MELEE) s.cond = (int16_t)wd->durability;
        else if (wd->kind == WK_CHAINSAW) s.cond = 100;
        else if (wd->kind == WK_THROWN) s.count = (int16_t)rng_range(&R, 1, 2);
        a->weapon = s;
    }
    if (rng_chance(&R, ad->loot_chance)) {
        int z = rng_chance(&R, 0.5f) ? Z_CAMP : Z_GROCERY;
        const LootEntry *t = LOOT[z];
        int tot = 0;
        for (int k = 0; k < LOOT_N[z]; k++) tot += t[k].weight;
        int roll = rng_int(&R, tot);
        for (int k = 0; k < LOOT_N[z]; k++) {
            roll -= t[k].weight;
            if (roll < 0) {
                Stack s = {t[k].id, (int16_t)rng_range(&R, t[k].lo, t[k].hi), 0};
                if (item_is_weapon(t[k].id) && ITEMS[t[k].id].stack == 1) break;
                inv_add(a, s);
                break;
            }
        }
    }
    if (a->weapon.id && item_weapon(a->weapon.id)->kind == WK_GUN && rng_chance(&R, 0.5f)) {
        Stack s = {item_weapon(a->weapon.id)->ammo, (int16_t)rng_range(&R, 2, 6), 0};
        inv_add(a, s);
    }
    a->br.home = pos;
    return i;
}

int gen_spawn_npc(int arch, V2 pos) { return spawn_npc(arch, pos); }

/* a squad member's spot: same rules as the leader's (random_floor_spot), and not behind a wall from them */
static bool follower_spot_ok(V2 p, V2 leader, bool indoor, float min_dist) {
    int tx = tile_of(p.x), ty = tile_of(p.y);
    if (!in_map(tx, ty)) return false;
    Cell *c = cell(tx, ty);
    if ((c->flags & (CF_SOLID | CF_NOSPAWN | CF_EXIT)) || c->obj || !c->reach) return false;
    if (indoor != ((c->flags & CF_INDOOR) != 0)) return false;
    if (v2_dist(p, W.spawn) < min_dist) return false;
    return los_clear(leader, p, true);
}

/* strays where people used to live: dogs and foxes mostly outside, rats in the back rooms, cats anywhere.
 * Healthy dogs and rats keep together; the rabid ones roam alone and start further from the van.
 * Exactly the level's share of them is rabid (at least one): *rabid of the *left still to place. */
static void populate_animals(int ar, int n, int *rabid_left, int *left) {
    float outside = ar == AR_RAT ? 0.15f : (ar == AR_CAT ? 0.5f : 0.8f);
    int i = 0;
    while (i < n) {
        bool rabid = rng_int(&R, *left) < *rabid_left;
        bool outdoor = rng_chance(&R, outside);
        float mind = rabid ? 260 : 170;
        V2 p = random_floor_spot(&R, !outdoor, mind, W.spawn);
        int group = 1;
        if (!rabid && (ar == AR_DOG || ar == AR_RAT) && n - i >= 2 && rng_chance(&R, 0.5f)) group = rng_range(&R, 2, MINF(3, n - i));
        if (!rabid) group = MINF(group, *left - *rabid_left);   /* leave room for the rabid ones still to come */
        for (int g = 0; g < group; g++) {
            V2 gp = p;
            for (int t = 0; g > 0 && t < 10; t++) {
                gp = v2(p.x + rng_rangef(&R, -20, 20), p.y + rng_rangef(&R, -20, 20));
                if (follower_spot_ok(gp, p, !outdoor, mind)) break;
                gp = p;
            }
            int ai = spawn_npc(ar, gp);
            if (ai < 0) return;   /* actor table full */
            animal_setup(&W.actors[ai], &R, rabid);
            if (rabid) (*rabid_left)--;
            (*left)--;
            i++;
        }
    }
}

/* three winters of nobody weeding. A plant takes root on open floor (a rooted one must never plug a gap - it
 * won't be shoved aside), well away from the van and from the other weeds; somewhere green if it can: the garden
 * centre, grass and dirt, the rubble under a hole in the roof */
static bool plant_spot(float min_dist, bool indoor, int *ox, int *oy) {
    for (int pass = 0; pass < 3; pass++)
        for (int t = 0; t < 400; t++) {
            int tx = rng_range(&R, 2, W.w - 3), ty = rng_range(&R, 2, W.h - 3);
            Cell *c = cell(tx, ty);
            if ((c->flags & (CF_SOLID | CF_NOSPAWN | CF_EXIT | CF_DOOR)) || c->obj || !c->reach) continue;
            bool green = c->zone == Z_GARDEN || c->floor == FL_GRASS || c->floor == FL_DIRT || (c->flags & CF_SKY);
            if (pass == 0 && !green) continue;
            if (pass < 2 && indoor != ((c->flags & CF_INDOOR) != 0)) continue;
            V2 p = tile_center(tx, ty);
            if (v2_dist(p, W.spawn) < min_dist) continue;
            if (W.boss >= 0 && v2_dist(p, W.actors[W.boss].pos) < 170) continue;   /* the King keeps his court weeded */
            bool open = true;
            for (int y = ty - 1; y <= ty + 1 && open; y++)
                for (int x = tx - 1; x <= tx + 1 && open; x++) open = !(cell(x, y)->flags & (CF_SOLID | CF_DOOR));
            for (int i = 1; i < W.nactors && open; i++)
                if (W.actors[i].used && is_plant(&W.actors[i]) && v2_dist(W.actors[i].pos, p) < 56) open = false;
            if (!open) continue;
            *ox = tx;
            *oy = ty;
            return true;
        }
    return false;
}

/* the spitters' bed: a patch of turned earth gone to weed */
static void flowerbed(int cx, int cy) {
    for (int y = cy - 1; y <= cy + 1; y++)
        for (int x = cx - 1; x <= cx + 1; x++) {
            if (!free_cell(x, y) || (cell(x, y)->flags & CF_EXIT)) continue;
            if (abs(x - cx) + abs(y - cy) == 2 && rng_chance(&R, 0.6f)) continue;   /* round the corners off */
            floor_at(x, y, FL_DIRT);
            V2 p = tile_center(x, y);
            if (rng_chance(&R, 0.6f)) decor(SPR_O_GRASS_TUFT + rng_int(&R, SPR_O_GRASS_TUFT_N), p.x + rng_range(&R, -6, 6), p.y + rng_range(&R, -6, 6), 0, 0);
        }
}

/* spitters in beds of two or three, nettles alone or in pairs - half of them indoors where the roof gave way or
 * the floor cracked - and ramblers wherever they last stopped, half of them dug in and passing for a bush */
static void populate_plants(int ar, int n) {
    float indoors = ar == AR_NETTLE ? 0.5f : (ar == AR_SPITTER ? 0.2f : 0.35f);
    float mind = ar == AR_NETTLE ? 170 : 230;
    for (int i = 0, tries = 0; i < n && tries < 40; tries++) {
        int tx, ty;
        if (!plant_spot(mind, rng_chance(&R, indoors), &tx, &ty)) return;
        int group = 1;
        if (ar == AR_SPITTER && n - i >= 2 && rng_chance(&R, 0.65f)) group = rng_range(&R, 2, MINF(3, n - i));
        if (ar == AR_NETTLE && n - i >= 2 && rng_chance(&R, 0.3f)) group = 2;
        if (ar == AR_SPITTER) flowerbed(tx, ty);
        /* a bed of them stands round its middle, a stalk's width apart */
        V2 c = tile_center(tx, ty);
        float turn = rng_rangef(&R, -PI_F, PI_F);
        for (int g = 0; g < group; g++) {
            V2 p = c;
            if (group > 1) p = v2_add(c, v2_scale(v2_angle(turn + g * 2 * PI_F / group + rng_rangef(&R, -0.2f, 0.2f)), rng_rangef(&R, 9.5f, 11.5f)));
            int ai = spawn_npc(ar, p);
            if (ai < 0) return;   /* actor table full */
            plant_setup(&W.actors[ai], &R);
            i++;
        }
    }
}

static void populate(void) {
    const LevelDef *d = W.def;
    int animals[AR_COUNT] = {0}, nanimals = 0, plants[AR_COUNT] = {0};
    for (int ar = AR_SCAV; ar < AR_COUNT; ar++) {
        int n = d->npcs[ar];
        if (n <= 0) continue;
        if (ar != AR_BOSS && n > 1) n += rng_range(&R, -1, 1);
        if (ARCH[ar].animal) { animals[ar] = n; nanimals += n; continue; }
        if (ARCH[ar].plant) { plants[ar] = n; continue; }
        int i = 0;
        while (i < n) {
            if (ar == AR_BOSS) {
                V2 p = v2(W.building.x + W.building.w * 0.5f, W.building.y + 5.5f * TILE);
                int bi = spawn_npc(AR_BOSS, p);
                if (bi >= 0) {
                    W.boss = bi;
                    W.actors[bi].br.state = AI_GUARD;
                    W.actors[bi].face = PI_F * 0.5f;
                    Stack s = {IT_SEEDS, 4, 0};
                    inv_add(&W.actors[bi], s);
                }
                /* royal guard */
                for (int g = 0; g < 3; g++) {
                    V2 gp = v2(p.x + rng_rangef(&R, -60, 60), p.y + rng_rangef(&R, 30, 90));
                    if (solid_at(gp.x, gp.y)) continue;
                    int gi = spawn_npc(g == 0 ? AR_BRUTE : AR_PIG, gp);
                    if (gi >= 0) W.actors[gi].br.state = AI_GUARD;
                }
                i++;
                continue;
            }
            bool outdoor = (ar == AR_RAIDER || ar == AR_FERAL) && rng_chance(&R, 0.25f);
            float mind = outdoor ? 300 : 230;
            V2 p = random_floor_spot(&R, !outdoor, mind, W.spawn);
            int group = 1;
            if ((ar == AR_RAIDER || ar == AR_PIG) && n - i >= 2 && rng_chance(&R, 0.5f)) group = rng_range(&R, 2, MINF(3, n - i));
            int leader = -1;
            for (int g = 0; g < group; g++) {
                V2 gp = p;
                if (g > 0) {
                    for (int t = 0; t < 10; t++) {
                        gp = v2(p.x + rng_rangef(&R, -28, 28), p.y + rng_rangef(&R, -28, 28));
                        if (follower_spot_ok(gp, p, !outdoor, mind)) break;
                        gp = p;
                    }
                }
                int ai = spawn_npc(ar, gp);
                if (ai < 0) { i = n; break; }   /* actor table full: give up on this kind */
                if (g == 0) leader = ai;
                else W.actors[ai].br.leader = leader;
                if ((ar == AR_GUNNER || ar == AR_BRUTE) && rng_chance(&R, 0.6f)) W.actors[ai].br.state = AI_GUARD;
                i++;
            }
        }
    }
    /* then the weeds, and the animals last: people get the good spots */
    for (int ar = AR_SCAV; ar < AR_COUNT; ar++)
        if (plants[ar] > 0) populate_plants(ar, plants[ar]);
    int rabid = nanimals > 0 ? MAXF(1, (int)(nanimals * d->rabid + 0.5f)) : 0;
    for (int ar = AR_SCAV; ar < AR_COUNT; ar++)
        if (animals[ar] > 0) populate_animals(ar, animals[ar], &rabid, &nanimals);
}

/* ------------------------------------------------------- shopping list */
static bool list_has(ItemId id) {
    for (int i = 0; i < W.nlist; i++) if (W.list[i].id == id) return true;
    return false;
}

/* asked for by somebody at the camp: never on the list or the bonus line, only where place_favours puts it */
static bool favour_has(ItemId id) {
    for (int i = 0; i < W.nfav; i++) if (W.fav[i].id == id) return true;
    return false;
}

static int random_container_for(ItemId id) {
    /* prefer containers whose zone table contains this item */
    int cands[700];
    int n = 0;
    for (int i = 0; i < W.nconts; i++) {
        Container *c = &W.conts[i];
        if (c->zone == Z_CAR || c->n >= 5 || !cont_reachable(c)) continue;
        const LootEntry *t = LOOT[c->zone];
        for (int k = 0; t && k < LOOT_N[c->zone]; k++)
            if (t[k].id == id) { cands[n++] = i; break; }
    }
    if (n == 0)
        for (int i = 0; i < W.nconts; i++)
            if (W.conts[i].zone != Z_CAR && W.conts[i].n < 5 && cont_reachable(&W.conts[i]) &&
                cell_flag(W.conts[i].tx, W.conts[i].ty, CF_INDOOR)) cands[n++] = i;
    if (n == 0) return -1;
    return cands[rng_int(&R, n)];
}

static void cont_put(int ci, ItemId id, int count) {
    Container *c = &W.conts[ci];
    for (int k = 0; k < c->n; k++)
        if (c->items[k].id == id && ITEMS[id].stack > 1) { c->items[k].count += count; return; }
    if (c->n >= 6) return;
    Stack s = {id, (int16_t)count, 0};
    if (item_is_weapon(id)) {
        const WeaponDef *w = item_weapon(id);
        if (w->kind == WK_GUN) s.cond = (int16_t)w->mag;
        else if (w->kind == WK_MELEE) s.cond = (int16_t)w->durability;
        else if (w->kind == WK_CHAINSAW || w->kind == WK_FLAME) s.cond = (int16_t)w->durability;
    }
    c->items[c->n++] = s;
}

/* the k-th toughest person here who'd keep something in their pockets (the hardest to take it off), or -1 */
static int favour_carrier(int k) {
    int cand[MAX_ACTORS], n = 0;
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive || a->arch == AR_BOSS || a->arch == AR_FERAL || is_animal(a) || is_plant(a) || is_camp(a)) continue;
        if (!cell(tile_of(a->pos.x), tile_of(a->pos.y))->reach) continue;
        cand[n++] = i;
    }
    if (n == 0) return -1;
    /* toughest first (what they're worth to beat), the ones further from the van before the nearer */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            Actor *a = &W.actors[cand[j]], *b = &W.actors[cand[j - 1]];
            int sa = ARCH[a->arch].score, sb = ARCH[b->arch].score;
            if (sa < sb || (sa == sb && v2_dist2(a->pos, W.van) <= v2_dist2(b->pos, W.van))) break;
            int t = cand[j]; cand[j] = cand[j - 1]; cand[j - 1] = t;
        }
    return cand[k % n];
}

/* the deepest back room: indoors, stockrooms, offices and staff lockers first, then the furthest from the van */
static int favour_stash(void) {
    int best = -1;
    float bs = -1;
    for (int i = 0; i < W.nconts; i++) {
        Container *c = &W.conts[i];
        if (c->zone == Z_CAR || c->n >= 6 || !cont_reachable(c)) continue;
        float s = v2_dist(c->pos, W.van);
        if (c->zone == Z_STORAGE || c->zone == Z_OFFICE || c->zone == Z_STAFF) s += 2000;
        if (!cell_flag(c->tx, c->ty, CF_INDOOR)) s -= 4000;   /* not out by the bins */
        if (s > bs) { bs = s; best = i; }
    }
    return best;
}

/* the camp's favours: whatever of it the store already had goes, then exactly what was asked for is put where it's
   hard to get - in the toughest pockets here, or at the back of the deepest back room */
static void place_favours(void) {
    for (int f = 0; f < W.nfav; f++) {
        ItemId id = W.fav[f].id;
        for (int i = 0; i < W.nconts; i++) {
            Container *c = &W.conts[i];
            for (int k = c->n - 1; k >= 0; k--)
                if (c->items[k].id == id) { memmove(&c->items[k], &c->items[k + 1], sizeof(Stack) * (c->n - k - 1)); c->n--; }
        }
        for (int i = 1; i < MAX_ACTORS; i++)
            if (W.actors[i].used) inv_remove(&W.actors[i], id, inv_count(&W.actors[i], id));
        for (int i = 0; i < MAX_CARTS; i++) {
            Cart *c = &W.carts[i];
            for (int k = c->n - 1; k >= 0; k--)
                if (c->items[k].id == id) { memmove(&c->items[k], &c->items[k + 1], sizeof(Stack) * (c->n - k - 1)); c->n--; }
        }
        for (int i = 0; i < MAX_PICKUPS; i++)
            if (W.pickups[i].alive && W.pickups[i].st.id == id) W.pickups[i].alive = false;
        const FavourDef *fd = &FAVOURS[W.fav_of[f]];
        for (int u = 0; u < W.fav[f].need; u++) {
            int ai = fd->where == FW_CARRIED ? favour_carrier(u) : -1;
            int ci = ai < 0 ? favour_stash() : -1;
            Stack s = {(int16_t)id, 1, 0, 0};
            if (ai >= 0) inv_add(&W.actors[ai], s);
            else if (ci >= 0) cont_put(ci, id, 1);
            SDL_Log("FAVOUR: %s for %s -> %s", ITEMS[id].name, ARCH[fd->arch].name,
                    ai >= 0 ? ARCH[W.actors[ai].arch].name : ci >= 0 ? (W.conts[ci].name ? W.conts[ci].name : "a container") : "nowhere");
        }
    }
}

static void make_list(void) {
    const LevelDef *d = W.def;
    W.nlist = 0;
    for (int i = 0; i < 3; i++) {
        if (!d->must[i].id) continue;
        ListEntry *e = &W.list[W.nlist++];
        memset(e, 0, sizeof *e);
        e->id = d->must[i].id;
        e->need = d->must[i].n;
    }
    /* the favours the camp asked for this store (the title screen's backdrop store has nobody to ask) */
    W.nfav = 0;
    for (int f = 0; RUN.active && f < NUM_FAVOURS && W.nfav < ARRAY_LEN(W.fav); f++) {
        if (FAVOURS[f].level != W.level || RUN.favour[f] != FS_OPEN || list_has(FAVOURS[f].item) || favour_has(FAVOURS[f].item)) continue;
        ListEntry *e = &W.fav[W.nfav];
        memset(e, 0, sizeof *e);
        e->id = FAVOURS[f].item;
        e->need = FAVOURS[f].n;
        W.fav_of[W.nfav++] = f;
    }
    int npool = 0;
    while (npool < 10 && d->pool[npool]) npool++;
    for (int k = 0; k < d->pool_picks && npool > 0; k++) {
        for (int t = 0; t < 20; t++) {
            ItemId id = d->pool[rng_int(&R, npool)];
            if (list_has(id) || favour_has(id)) continue;
            ListEntry *e = &W.list[W.nlist++];
            memset(e, 0, sizeof *e);
            e->id = id;
            e->need = (ITEMS[id].size >= 2 || W.level < 1) ? 1 : rng_range(&R, 1, 2);
            break;
        }
    }
    W.nbonus = 0;
    int nb = 0;
    while (nb < 8 && d->bonus[nb]) nb++;
    for (int k = 0; k < d->bonus_picks && nb > 0; k++) {
        for (int t = 0; t < 20; t++) {
            ItemId id = d->bonus[rng_int(&R, nb)];
            bool dup = list_has(id) || favour_has(id);
            for (int j = 0; j < W.nbonus; j++) if (W.bonus[j].id == id) dup = true;
            if (dup) continue;
            ListEntry *e = &W.bonus[W.nbonus++];
            memset(e, 0, sizeof *e);
            e->id = id;
            e->need = 1;
            break;
        }
    }
    /* distribute: some carried by NPCs, rest in containers, plus a spare of each */
    int carriers[MAX_ACTORS];
    int nc = 0;
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive || a->arch == AR_BOSS || a->arch == AR_FERAL || is_animal(a) || is_plant(a)) continue;
        if (!cell(tile_of(a->pos.x), tile_of(a->pos.y))->reach) continue;
        carriers[nc++] = i;
    }
    for (int i = 0; i < W.nlist; i++) {
        ItemId id = W.list[i].id;
        if (W.def->kind == LK_MALL && id == IT_SEEDS) continue;   /* the King has them all */
        for (int u = 0; u < W.list[i].need + 1; u++) {
            if (nc > 0 && rng_chance(&R, d->carried)) {
                Stack s = {id, 1, 0};
                inv_add(&W.actors[carriers[rng_int(&R, nc)]], s);
            } else {
                int ci = random_container_for(id);
                if (ci >= 0) cont_put(ci, id, 1);
            }
        }
    }
    for (int i = 0; i < W.nbonus; i++) {
        int ci = random_container_for(W.bonus[i].id);
        if (ci >= 0) cont_put(ci, W.bonus[i].id, 1);
    }
    place_favours();
}

/* ============================================================== entry */
void gen_level(int level, uint64_t seed) {
    rng_seed(&R, seed ^ ((uint64_t)(level + 1) * 0x9E3779B97F4A7C15ull));
    W.level = level;
    W.def = &LEVELS[level];
    W.w = W.def->map_w;
    W.h = W.def->map_h;
    W.boss = -1;
    switch (W.def->kind) {
    case LK_GASSTATION: gen_gasstation(); break;
    case LK_MARKET: gen_market(); break;
    case LK_HARDWARE: gen_hardware(); break;
    case LK_PHARMACY: gen_pharmacy(); break;
    case LK_MEGAMART: gen_megamart(); break;
    case LK_MALL: gen_mall(); break;
    case LK_GREENHOUSE: break;   /* gen_hub */
    }
    /* doors must never open straight into furniture: big props (dumpsters, cars, trees) go whole */
    for (int i = 0; i < W.ndoors; i++) {
        Door *d = &W.doors[i];
        int tx = tile_of(d->hinge.x + cosf(d->base) * 7), ty = tile_of(d->hinge.y + sinf(d->base) * 7);
        bool horiz = fabsf(sinf(d->base)) < 0.5f;
        for (int k = -1; k <= 1; k += 2) {
            int nx = horiz ? tx : tx + k, ny = horiz ? ty + k : ty;
            if (!in_map(nx, ny)) continue;
            Cell *c = cell(nx, ny);
            if (c->wall || !c->obj) continue;
            clear_rect(rect(nx, ny, nx, ny));
        }
    }
    settle_carts();
    vines();
    compute_reach();
    /* containers that lost their cell, or that nobody can step up to (walled in by clutter, or only touched at
     * a corner): they stay empty - drawn emptied - and off the AI's and the shopping list's books */
    for (int i = 0; i < W.nconts; i++) {
        Container *c = &W.conts[i];
        if (c->dead || (cont_linked(i) && cont_reachable(c))) continue;
        c->dead = true;
        c->searched = true;
        c->n = 0;
    }
    /* fill containers with random loot */
    for (int i = 0; i < W.nconts; i++) container_fill(&W.conts[i], &R);
    /* the player first, then everybody else */
    int pi = actor_spawn(AR_PLAYER, W.spawn);
    (void)pi;
    populate();
    make_list();
    map_compute_autotile();
}

/* ============================================================ the Greenhouse */
/* The camp between the stores: the same yard every evening (hand laid, one char per tile):
 *   # hedge   G greenhouse glass   B brick (the workshop, the range wall)
 *   . grass   : dirt   d a bed of crops   c concrete   w boards   s path   a asphalt   l the range's firing line */
static const char *const HUB_MAP[44] = {
    "############################################################",
    "#..........................................................#",
    "#..........................................................#",
    "#...........GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG...........#",
    "#...........GcccccccccccccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcddddcddddcccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcddddcddddcccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcccccccccccccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcddddcddddcccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcddddcddddcccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcccccccccccccccccccccccwwwwwwwwwwwG...........#",
    "#...........ccccccccccccccccccccccccwwwwwwwwwwwc...........#",
    "#...........ccccccccccccccccccccccccwwwwwwwwwwwc...........#",
    "#...........GcccccccccccccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcddddcddddcccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcddddcddddcccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcccccccccccccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcddddcddddcccccccccccccwwwwwwwwwwwG...........#",
    "#...........GcddddcddddcccccccccccccwwwwwwwwwwwG...........#",
    "#...........GGGGGGGGGGGGGGGGGccGGGGGGGGGGGGGGGGG...........#",
    "#............ssssssssssssssssssssssssssssssssss............#",
    "#............ssssssssssssssssssssssssssssssssss.BBBBBBBBBB.#",
    "#..ccccccccc.ss..............ss.................BccccccccB.#",
    "#..ccccccccc.................ss.................BccccccccB.#",
    "#..ccccccccc.................ss.................BccccccccB.#",
    "#..ccccccccc.........:::.....ssssssssssssssssssscccccccccB.#",
    "#..ccccccccc........:::::....ssssssssssssssssssscccccccccB.#",
    "#..ccccccccc.......:::::::...ss.................BccccccccB.#",
    "#..ccccccccc.......:::::::...ss.................BccccccccB.#",
    "#..ccccccccc........:::::....ss.................BccccccccB.#",
    "#..ccccccccc.........:::.....ss.............BBBBBBBBBBBBBB.#",
    "#............................ss...............:::::::::::::#",
    "#............................ss...............:l:::::::::::#",
    "#............................ss...............:l:::::::::::#",
    "#..dddddd....................ss...............:l:::::::::::#",
    "#..dddddd................aaaaaaaaaa...........:l:::::::::::#",
    "#...........:::..........aaaaaaaaaa...........:l:::::::::::#",
    "#...........:::..........aaaaaaaaaa...........:l:::::::::::#",
    "#..dddddd................aaaaaaaaaa...........:l:::::::::::#",
    "#..dddddd................aaaaaaaaaa...........:l:::::::::::#",
    "#........................aaaaaaaaaa...........:l:::::::::::#",
    "#........................aaaaaaaaaa...........:l:::::::::::#",
    "#........................aaaaaaaaaa...........:::::::::::::#",
    "###########################aaaaaa###########################",
};

static void hub_tile(int x, int y, char ch) {
    Cell *c = cell(x, y);
    memset(c, 0, sizeof *c);
    c->cont = -1;
    switch (ch) {
    case '#': floor_at(x, y, FL_GRASS); wall_at(x, y, WL_HEDGE); break;
    case 'G': floor_at(x, y, FL_CONCRETE); wall_at(x, y, WL_GLASS); break;
    case 'B': floor_at(x, y, FL_CONCRETE); wall_at(x, y, WL_BRICK); break;
    case ':': floor_at(x, y, FL_DIRT); break;
    case 'd': floor_at(x, y, FL_DIRT); break;
    case 'c': floor_at(x, y, FL_CONCRETE); break;
    case 'w': floor_at(x, y, FL_WOOD); break;
    case 's': floor_at(x, y, FL_SIDEWALK); break;
    case 'a': floor_at(x, y, FL_ASPHALT); break;
    case 'l': floor_at(x, y, FL_ASPHALT_LINE); break;
    default: floor_at(x, y, rng_chance(&R, 0.93f) ? FL_GRASS : FL_DIRT); break;
    }
}

/* one of the yard's crates or piles: a few odds and ends for the workbench, new every evening */
static void hub_cont(int k, int ci, Rng *loot) {
    if (ci < 0) return;
    Container *c = &W.conts[ci];
    const LootEntry *t = LOOT[Z_HUB];
    int tot = 0;
    for (int i = 0; i < LOOT_N[Z_HUB]; i++) tot += t[i].weight;
    int n = rng_chance(loot, 0.45f) ? 2 : 1;
    for (int j = 0; j < n; j++) {
        int roll = rng_int(loot, tot);
        for (int i = 0; i < LOOT_N[Z_HUB]; i++) {
            if ((roll -= t[i].weight) >= 0) continue;
            cont_put(ci, t[i].id, rng_range(loot, t[i].lo, t[i].hi));
            break;
        }
    }
    if (RUN.hub_done & HD_CONT(k)) { c->n = 0; c->searched = true; }   /* already went through it this evening */
}

static int hub_obj_cont(int x, int y, int obj, int var, const char *name) {
    obj_at(x, y, obj, var);
    return cont_new(x, y, Z_HUB, name);
}

static int hub_prop(int spr, int tx, int ty, Rect block, int flags) {
    int pi = prop_add(spr, tx * TILE, ty * TILE, 0);
    if (block.x1 >= block.x0) prop_block(pi, block, flags);
    return pi;
}

void gen_hub(uint64_t seed) {
    rng_seed(&R, seed ^ 0x6A09E667F3BCC909ull);   /* the same weeds and puddles every evening of a run */
    W.level = RUN.level;
    W.def = &HUB_DEF;
    W.w = W.def->map_w;
    W.h = W.def->map_h;
    W.boss = -1;
    memset(&HUB, 0, sizeof HUB);
    for (int y = 0; y < W.h; y++)
        for (int x = 0; x < W.w; x++) hub_tile(x, y, HUB_MAP[y][x]);
    /* under the glass: indoors, but the evening light falls straight in */
    for (int y = 3; y <= 19; y++)
        for (int x = 12; x <= 47; x++) cell(x, y)->flags |= CF_INDOOR | CF_SKY;
    mark_indoor(rect(48, 21, 57, 30));
    /* dressing */
    for (int y = 1; y < W.h - 1; y++)
        for (int x = 1; x < W.w - 1; x++) {
            Cell *c = cell(x, y);
            if (c->wall) continue;
            V2 p = tile_center(x, y);
            char ch = HUB_MAP[y][x];
            if (ch == 'd') decor(SPR_O_CROP + (y / 3 + x / 5) % SPR_O_CROP_N, p.x, p.y, 0, 0);
            else if (c->floor == FL_GRASS || c->floor == FL_DIRT) {
                if (rng_chance(&R, 0.18f)) decor(SPR_O_GRASS_TUFT + rng_int(&R, SPR_O_GRASS_TUFT_N), p.x + rng_range(&R, -6, 6), p.y + rng_range(&R, -6, 6), 0, 0);
                if (rng_chance(&R, 0.03f)) decor(SPR_O_LEAVES + rng_int(&R, SPR_O_LEAVES_N), p.x, p.y, rng_int(&R, 2), 0);
            } else if (c->floor == FL_ASPHALT || c->floor == FL_SIDEWALK) {
                if (rng_chance(&R, 0.08f)) decor(SPR_O_CRACK + rng_int(&R, SPR_O_CRACK_N), p.x, p.y, rng_int(&R, 2), 0);
                if (rng_chance(&R, 0.10f)) decor(SPR_O_GRASS_TUFT + rng_int(&R, SPR_O_GRASS_TUFT_N), p.x + rng_range(&R, -6, 6), p.y, 0, 0);
            } else if ((c->flags & CF_INDOOR) && rng_chance(&R, 0.04f))
                decor(SPR_O_LEAVES + rng_int(&R, SPR_O_LEAVES_N), p.x, p.y, rng_int(&R, 2), 0);
        }
    decor(SPR_O_PUDDLE, 27 * TILE, 41 * TILE, 0, 0);
    decor(SPR_O_PUDDLE + 1, 38 * TILE, 22 * TILE + 8, 0, 0);

    int pantry = -1, junk = -1, toolbox = -1, scrap = -1, car = -1;
    /* the greenhouse: Rosa's radio desk, the kitchen and the stove, the beds of crops, the sleeping boards */
    hub_prop(SPR_P_DESK, 27, 4, rect(27, 4, 28, 4), CF_SOLID | CF_SHOT);
    for (int k = 0; k < 3; k++) obj_at(32 + k, 4, OB_COUNTER, k);
    pantry = hub_obj_cont(35, 4, OB_CRATE, 0, "Pantry crate");
    hub_prop(SPR_P_TABLE, 25, 9, rect(25, 9, 26, 10), CF_SOLID);
    obj_at(31, 11, OB_CAMPFIRE, 0);
    hub_prop(SPR_P_BENCH, 24, 15, rect(24, 15, 25, 15), CF_SOLID);
    hub_prop(SPR_P_PLANTER, 28, 14, rect(28, 14, 29, 15), CF_SOLID);   /* seedlings, the start of next year */
    hub_prop(SPR_P_PLANTER, 32, 14, rect(32, 14, 33, 15), CF_SOLID);
    obj_at(35, 17, OB_BOX, 0);
    obj_at(35, 18, OB_CRATE, 0);
    junk = hub_obj_cont(23, 18, OB_CRATE, 0, "Junk crate");
    obj_at(13, 18, OB_BOX, 0);
    obj_at(13, 4, OB_BOX, 2);
    HUB.theo_sick = RUN.level == 3;   /* the fever the Medimart briefing is about */
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < 4; i++) {
            int spr = row == 1 && i == 3 && HUB.theo_sick ? SPR_P_SICKBED : SPR_P_MATTRESS;
            prop_add(spr, 37 * TILE + i * 28, (row ? 16 : 4) * TILE, 0);
        }
    prop_add(SPR_P_SLEEPINGBAG, 38 * TILE, 10 * TILE, 0);
    prop_add(SPR_P_SLEEPINGBAG, 41 * TILE + 6, 9 * TILE + 4, 0);
    obj_at(46, 8, OB_LOCKER, 0);
    obj_at(46, 4, OB_BOX, 1);
    HUB.locker = tile_center(46, 8);
    /* the gym */
    hub_prop(SPR_P_BENCHPRESS, 5, 24, rect(5, 24, 5, 25), CF_SOLID);   /* the rack; you lie on the pad */
    HUB.bench = v2(5 * TILE + 16, 24 * TILE + 12);
    hub_prop(SPR_P_WEIGHTS, 9, 23, rect(9, 23, 9, 23), CF_SOLID);
    for (int i = 0; i < 2; i++) {
        int pi = prop_add(SPR_P_TIRE, (4 + i * 6) * TILE + 8, 29 * TILE + 8, 0);
        if (pi >= 0) W.props[pi].angle = rng_rangef(&R, 0, 6.28f);
    }
    /* the fire in the yard, where people sit in the evening */
    obj_at(22, 27, OB_CAMPFIRE, 0);
    hub_prop(SPR_P_BENCH, 20, 24, rect(20, 24, 21, 24), CF_SOLID);
    hub_prop(SPR_P_BENCH, 20, 30, rect(20, 30, 21, 30), CF_SOLID);
    /* Gus's workshop */
    hub_prop(SPR_P_WORKBENCH, 50, 22, rect(50, 22, 51, 22), CF_SOLID | CF_SHOT);
    HUB.workbench = v2(51 * TILE, 23 * TILE + 6);
    obj_at(52, 22, OB_PEGBOARD, 0);
    obj_at(53, 22, OB_PEGBOARD, 1);
    toolbox = hub_obj_cont(55, 22, OB_CRATE, 0, "Toolbox");
    {
        int pi = hub_prop(SPR_P_SCRAP, 53, 27, rect(53, 27, 54, 28), CF_SOLID | CF_SHOT);
        scrap = cont_new(53, 27, Z_HUB, "Scrap pile");
        if (scrap >= 0) { W.conts[scrap].prop = pi; W.conts[scrap].pos = v2(54 * TILE, 28 * TILE + 4); }
    }
    obj_at(49, 29, OB_BOX, 1);
    /* the range: bottles on two planks, boards that pop up between them, the hedge for a backstop */
    HUB.range_line = v2(46 * TILE + 8, 36 * TILE + 8);
    HUB.range = (SDL_FRect){50 * TILE, 31 * TILE + 8, 7 * TILE, 10 * TILE};
    prop_add(SPR_P_BENCH, 53 * TILE, 33 * TILE + 2, 0);
    prop_add(SPR_P_BENCH, 53 * TILE, 39 * TILE + 2, 0);
    HUB.planks[0] = v2(54 * TILE - 1, 33 * TILE + 4);
    HUB.planks[1] = v2(54 * TILE - 1, 39 * TILE + 4);
    for (int i = 0; i < HUB_TARGETS; i++) {
        HUB.target_prop[i] = prop_add(SPR_P_TARGET, 0, 0, 0);
        if (HUB.target_prop[i] >= 0) W.props[HUB.target_prop[i]].tint = rgba(255, 255, 255, 0);
    }
    /* the old car nobody can fix (Gus keeps trying) */
    {
        int pi = hub_prop(SPR_P_CAR_BURNT, 12, 34, rect(12, 34, 13, 36), CF_SOLID | CF_SHOT);
        car = cont_new(12, 35, Z_HUB, "Old car");
        if (car >= 0) { W.conts[car].prop = pi; W.conts[car].pos = v2(12 * TILE + 16, 35 * TILE + 8); }
    }
    /* lamps, trees, bushes */
    static const int lamps[][2] = {{28, 22}, {31, 33}, {12, 24}, {45, 32}, {36, 24}, {15, 31}};
    for (int i = 0; i < ARRAY_LEN(lamps); i++) obj_at(lamps[i][0], lamps[i][1], OB_LAMPPOST, 0);
    static const int trees[][2] = {{5, 6}, {8, 16}, {52, 8}, {55, 14}, {3, 41}, {19, 40}, {40, 39}, {50, 18}};
    for (int i = 0; i < ARRAY_LEN(trees); i++) tree_at(trees[i][0], trees[i][1]);
    static const int bushes[][2] = {{1, 9}, {1, 27}, {58, 5}, {58, 12}, {10, 1}, {44, 1}, {24, 42}, {36, 42}, {10, 42}};
    for (int i = 0; i < ARRAY_LEN(bushes); i++) if (free_cell(bushes[i][0], bushes[i][1])) obj_at(bushes[i][0], bushes[i][1], OB_BUSH, i % 3);
    /* the running course: round the greenhouse, flag to flag */
    static const int course[][2] = {{16, 23}, {3, 15}, {4, 2}, {30, 1}, {56, 2}, {57, 19}, {40, 23}};
    HUB.nflags = ARRAY_LEN(course);
    for (int i = 0; i < HUB.nflags; i++) {
        HUB.flags[i] = tile_center(course[i][0], course[i][1]);
        HUB.flag_prop[i] = prop_add(SPR_P_FLAG, HUB.flags[i].x, HUB.flags[i].y + 6, 0);
    }
    /* the van, at the gate */
    int vx = 30, vy = 37;
    int van = hub_prop(SPR_P_VAN, vx, vy, rect(vx, vy, vx + 1, vy + 2), CF_SOLID | CF_SHOT);
    if (van >= 0) { W.props[van].x -= 2; W.props[van].y -= 4; }
    W.van = v2(vx * TILE + 16, vy * TILE + 24);
    W.exit_rect = (SDL_FRect){(vx - 3) * TILE, vy * TILE, 3 * TILE, 3 * TILE};
    for (int y = vy; y <= vy + 2; y++)
        for (int x = vx - 3; x <= vx - 1; x++) cell(x, y)->flags |= CF_EXIT;
    W.spawn = v2(30 * TILE, 21 * TILE + 8);
    /* where everybody hangs about */
    HUB.post[AR_ROSA] = v2(28 * TILE, 6 * TILE + 8);
    HUB.post[AR_THEO] = HUB.theo_sick ? v2(37 * TILE + 3 * 28 + 12, 16 * TILE + 12) : v2(20 * TILE, 12 * TILE);
    HUB.post[AR_GUS] = v2(51 * TILE + 8, 25 * TILE + 8);
    HUB.post[AR_JUNE] = v2(44 * TILE, 35 * TILE + 8);
    HUB.post[AR_DEE] = v2(8 * TILE + 8, 28 * TILE);
    HUB.post[AR_MARTA] = v2(18 * TILE + 8, 11 * TILE + 8);
    HUB.post[AR_FOLK_A] = v2(23 * TILE + 8, 25 * TILE + 8);
    HUB.post[AR_FOLK_B] = v2(33 * TILE, 10 * TILE);
    HUB.post[AR_FOLK_C] = v2(40 * TILE + 8, 11 * TILE + 8);
    /* the crew keep watch round the yard, most of them by the gate; the ones coming along wait at the van */
    HUB.post[AR_HOLLIS] = v2(24 * TILE + 8, 41 * TILE + 8);
    HUB.post[AR_BEX] = v2(36 * TILE + 8, 41 * TILE);
    HUB.post[AR_OZZIE] = v2(25 * TILE + 8, 35 * TILE + 8);
    HUB.post[AR_CARMEN] = v2(15 * TILE + 8, 41 * TILE);
    HUB.post[AR_WES] = v2(14 * TILE, 21 * TILE + 8);
    for (int k = 0; k < MAX_CREW; k++) HUB.crew_van[k] = v2((33 + (k & 1)) * TILE + 8, (36 + k) * TILE + 8);
    /* and the ones who didn't come back lie in the quiet corner behind the glass, a board and a candle each */
    for (int k = 0; k < MAX_CREW; k++) {
        HUB.grave[k] = v2((5 + (k % 3) * 2) * TILE + 8, (k < 3 ? 11 : 13) * TILE + 8);
        if (RUN.crew[k] != CR_DEAD) continue;
        prop_add(SPR_P_GRAVE, HUB.grave[k].x - 8, HUB.grave[k].y - 12, 0);
    }
    /* tonight's odds and ends (seeded per evening, so leaving and coming back doesn't restock them) */
    Rng loot;
    rng_seed(&loot, seed ^ ((uint64_t)(RUN.level + 1) * 0xBB67AE8584CAA73Bull));
    int conts[] = {pantry, junk, toolbox, scrap, car};
    for (int k = 0; k < ARRAY_LEN(conts); k++) hub_cont(k, conts[k], &loot);
    vines();
    compute_reach();
    actor_spawn(AR_PLAYER, W.spawn);
    map_compute_autotile();
}
