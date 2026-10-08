/* LAST AISLE - world simulation + rendering */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"
#include "hub.h"

World W;
Run RUN;

extern float g_search_progress;
extern int g_search_cont;

/* -------------------------------------------------------------- decals */
typedef struct { int spr; V2 pos; float angle, scale; Color tint; } DecalQ;
static DecalQ dq[768];
static int ndq;

void decal(int spr, V2 pos, float angle, float scale, Color tint) {
    if (ndq >= ARRAY_LEN(dq) || spr < 0) return;
    dq[ndq].spr = spr;
    dq[ndq].pos = pos;
    dq[ndq].angle = angle;
    dq[ndq].scale = scale;
    dq[ndq].tint = tint;
    ndq++;
}

static void flush_decals(void) {
    if (!ndq || !W.ground) { ndq = 0; return; }
    SDL_Texture *prev = SDL_GetRenderTarget(G.ren);
    float ox = G.off_x, oy = G.off_y;
    SDL_SetRenderTarget(G.ren, W.ground);
    G.off_x = G.off_y = 0;
    for (int i = 0; i < ndq; i++) {
        DecalQ *d = &dq[i];
        gfx_spr_ex(d->spr, d->pos.x, d->pos.y, d->angle, d->scale, d->scale, d->tint);
    }
    SDL_SetRenderTarget(G.ren, prev);
    G.off_x = ox;
    G.off_y = oy;
    ndq = 0;
}

/* ---------------------------------------------------------- feedback */
void add_shake(float amount) { W.shake = MINF(W.shake + amount, 22); }
void hitstop(float t) { W.hitstop = MAXF(W.hitstop, t); }
void slowmo(float t) { W.slowmo_t = MAXF(W.slowmo_t, t); }

static Floater *floater_new(V2 pos, const char *text, bool big) {
    int best = 0;
    for (int i = 0; i < MAX_FLOATERS; i++) {
        if (W.floaters[i].t <= 0) { best = i; break; }
        if (W.floaters[i].t < W.floaters[best].t) best = i;
    }
    Floater *f = &W.floaters[best];
    f->pos = pos;
    f->t = big ? 1.6f : 1.1f;
    f->c = COL_WHITE;
    f->big = big;
    f->kind = FL_TEXT;
    SDL_strlcpy(f->text, text, sizeof f->text);
    return f;
}

void floater(V2 pos, const char *text, Color c, bool big) { floater_new(pos, text, big)->c = c; }

void floater_sticker(V2 pos, const char *text, FloaterKind kind) { floater_new(pos, text, kind == FL_SALE)->kind = kind; }

void world_message(const char *text, Color c) {
    SDL_strlcpy(W.msg, text, sizeof W.msg);
    W.msg_t = 2.6f;
    W.msg_col = c;
}

void world_hint(const char *text) {
    SDL_strlcpy(W.hint, text, sizeof W.hint);
    W.hint_t = 2.2f;
}

void play_at(int sfx, V2 pos, float vol, float pitch) {
    V2 lp = W.actors[0].used ? W.actors[0].pos : v2(G.cam_x, G.cam_y);
    float d = v2_dist(pos, lp);
    float att = CLAMP(1.0f - (d - 60) / 520.0f, 0.0f, 1.0f);
    if (att <= 0.01f) return;
    float pan = CLAMP((pos.x - lp.x) / 260.0f, -0.8f, 0.8f);
    audio_play((SfxId)sfx, vol * att, pan, pitch);
}

void make_noise(V2 pos, float radius, int source) {
    if (W.hub) return;   /* the range at the Greenhouse: everybody's used to it */
    ai_on_noise(pos, radius, source);
    if ((source == 0 || allied(0, source)) && radius >= 300) W.heat += radius >= 450 ? 2.0f : 1.0f;   /* your crew's guns carry too */
}

/* gunfire carries across the dead city: a squad comes to see what the noise is about */
static void reinforcements(void) {
    static const float thresholds[] = {7, 16, 28};
    if (W.hub || W.level < 1 || W.waves >= 3 || W.heat < thresholds[W.waves] || W.list_done || W.list_announced) return;
    W.waves++;
    Actor *p = player();
    /* enter from the map edge furthest from the player, outdoors */
    V2 best = W.spawn;
    float bd = -1;
    for (int t = 0; t < 40; t++) {
        int tx = chance(0.5f) ? irange(2, 4) : irange(W.w - 5, W.w - 3);
        int ty = irange(2, W.h - 3);
        if (!walkable_tile(tx, ty) || !cell(tx, ty)->reach || cell_flag(tx, ty, CF_INDOOR)) continue;
        float d = v2_dist(tile_center(tx, ty), p->pos);
        if (d > bd) { bd = d; best = tile_center(tx, ty); }
    }
    if (bd < 0) return;
    int arch = W.level >= 3 ? AR_PIG : AR_RAIDER;
    int n = 2 + (W.level >= 3) + (W.waves >= 2);
    for (int i = 0; i < n; i++) {
        V2 sp = v2(best.x + frange(-20, 20), best.y + frange(-20, 20));
        if (solid_at(sp.x, sp.y)) sp = best;
        int ai = gen_spawn_npc(i == 0 && W.level >= 2 ? AR_GUNNER : arch, sp);
        if (ai < 0) continue;
        Actor *a = &W.actors[ai];
        a->br.state = AI_INVESTIGATE;
        a->br.goal = p->pos;
        a->br.repath = 0;
        a->br.aware = true;
        a->spawn_grace = 0;
    }
    world_message(W.waves == 1 ? "SOMEONE HEARD THAT..." : "MORE OF THEM ARE COMING", COL_ORANGE);
    audio_play(SFX_ALERT, 0.8f, 0, 0.7f);
}

/* the last slots are kept for the player's own light and flashlight */
static int light_reserve;
/* gameplay buttons pressed while a hitstop freezes the world (see world_update) */
static bool held_press[ACT_COUNT];

static bool light_slot(V2 pos, float r) {
    if (W.nlights >= MAX_LIGHTS - light_reserve) return false;
    /* off-screen lights can't be seen: don't let them use up the budget */
    float hw = WORLD_RT_W * 0.5f + 48 + r, hh = WORLD_RT_H * 0.5f + 48 + r;
    return !(fabsf(pos.x - G.cam_x) >= hw || fabsf(pos.y - G.cam_y) >= hh);
}

void add_light(V2 pos, float r, Color c, float k) {
    if (!light_slot(pos, r)) return;
    Light *l = &W.lights[W.nlights++];
    l->pos = pos;
    l->r = r;
    l->c = c;
    l->k = k;
    l->cone = false;
}

void add_cone(V2 pos, float ang, float len, Color c, float k) {
    if (!light_slot(pos, len)) return;
    Light *l = &W.lights[W.nlights++];
    l->pos = pos;
    l->r = len;
    l->c = c;
    l->k = k;
    l->cone = true;
    l->ang = ang;
}

/* ---------------------------------------------------------- particles */
Particle *particle_add(PartKind k, V2 pos, V2 vel, float life) {
    Particle *p = &W.parts[W.npart_next];
    W.npart_next = (W.npart_next + 1) % MAX_PARTICLES;
    memset(p, 0, sizeof *p);
    p->kind = (uint8_t)k;
    p->pos = pos;
    p->vel = vel;
    p->life = p->max = life;
    p->scale = 1;
    p->frames = 1;
    p->col = TINT_NONE;
    p->spr = -1;
    return p;
}

void particles_spawn(PartKind k, V2 pos, V2 vel, float z, float vz, float life, int spr, int frames) {
    Particle *p = particle_add(k, pos, vel, life);
    p->z = z;
    p->vz = vz;
    p->spr = spr;
    p->frames = frames;
}

static bool emissive(int k) { return k == PT_FIRE || k == PT_FLAME || k == PT_EMBER || k == PT_SPARK || k == PT_IMPACT; }

static void particles_update(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &W.parts[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        switch (p->kind) {
        case PT_BLOOD:
        case PT_GIB:
        case PT_HEAD:
        case PT_SHELL:
        case PT_GLASS:
        case PT_DEBRIS: {
            p->pos = v2_add(p->pos, v2_scale(p->vel, dt));
            p->z += p->vz * dt;
            p->vz -= 420 * dt;
            p->angle += p->spin * dt;
            if (solid_at(p->pos.x, p->pos.y)) {
                p->pos = v2_sub(p->pos, v2_scale(p->vel, dt));
                p->vel = v2_scale(p->vel, -0.3f);
                if (p->kind == PT_BLOOD) {
                    /* splash onto the wall foot */
                    decal(p->spr, p->pos, 0, p->scale, TINT_NONE);
                    p->life = 0;
                    break;
                }
            }
            if (p->z <= 0) {
                p->z = 0;
                if (p->kind == PT_BLOOD) {
                    if (p->bake) decal(p->spr, p->pos, frange(0, 6.28f), p->scale, TINT_NONE);
                    p->life = 0;
                } else if (fabsf(p->vz) > 40) {
                    p->vz = -p->vz * 0.35f;
                    p->vel = v2_scale(p->vel, 0.55f);
                    p->spin *= 0.6f;
                    if (p->kind == PT_GIB) decal(SPR_FX_BLOOD_DRIP + irange(0, 3), p->pos, 0, 1, TINT_NONE);
                    if (p->kind == PT_SHELL && chance(0.4f)) play_at(SFX_SHELL, p->pos, 0.6f, frange(0.9f, 1.3f));
                } else {
                    p->vz = 0;
                    p->vel = v2_scale(p->vel, MAXF(0.0f, 1.0f - 6 * dt));
                    p->spin *= MAXF(0.0f, 1.0f - 6 * dt);
                    if (v2_len2(p->vel) < 4 && p->bake) {
                        decal(p->spr, p->pos, p->angle, p->scale, p->col);
                        p->life = 0;
                    }
                }
            }
            if (p->life <= 0 && p->bake && p->kind != PT_BLOOD) decal(p->spr, p->pos, p->angle, p->scale, p->col);
            break;
        }
        case PT_SMOKE:
            p->pos = v2_add(p->pos, v2_scale(p->vel, dt));
            p->vel = v2_scale(p->vel, MAXF(0.0f, 1.0f - 1.5f * dt));
            p->scale += dt * 0.6f;
            break;
        case PT_RAIN:
            p->pos = v2_add(p->pos, v2_scale(p->vel, dt));
            if (p->life <= 0) {
                Particle *s = particle_add(PT_DUST, p->pos, v2(0, 0), 0.15f);
                s->spr = SPR_FX_IMPACT;
                s->frames = 3;
                s->col = rgba(180, 200, 230, 140);
            }
            break;
        default:
            p->pos = v2_add(p->pos, v2_scale(p->vel, dt));
            p->vel = v2_scale(p->vel, MAXF(0.0f, 1.0f - 3 * dt));
            if (p->kind == PT_EMBER) { p->z += p->vz * dt; p->vz -= 60 * dt; if (p->z < 0) p->z = 0; }
            break;
        }
    }
}

static void particle_draw(Particle *p) {
    if (p->kind == PT_RAIN) {
        gfx_line(p->pos.x, p->pos.y, p->pos.x - p->vel.x * 0.02f, p->pos.y - p->vel.y * 0.02f, rgba(170, 190, 230, 110));
        return;
    }
    if (p->spr < 0) return;
    int fr = 0;
    if (p->frames > 1) fr = CLAMP((int)((1.0f - p->life / p->max) * p->frames), 0, p->frames - 1);
    if (p->kind == PT_FIRE && p->spr == SPR_FX_FIRE) fr = ((int)(W.time * 14) + (int)(p->pos.x)) % 4;
    Color c = p->col;
    if (p->kind == PT_SMOKE) c.a = (Uint8)(c.a * CLAMP(p->life / p->max, 0.0f, 1.0f));
    if (p->kind == PT_EMBER || p->kind == PT_SPARK) c.a = (Uint8)(255 * CLAMP(p->life / p->max * 1.5f, 0.0f, 1.0f));
    gfx_spr_ex(p->spr + fr, p->pos.x, p->pos.y - p->z, p->angle, p->scale, p->scale, c);
}

static void particles_draw(bool emit) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &W.parts[i];
        if (p->life <= 0) continue;
        if (emissive(p->kind) != emit) continue;
        particle_draw(p);
    }
}

/* ------------------------------------------------------------ corpses */
void corpse_add(V2 pos, float angle, int spr, bool gibbed, V2 vel) {
    Corpse *c;
    if (W.ncorpses < MAX_CORPSES) c = &W.corpses[W.ncorpses++];
    else {
        /* bake the oldest into the floor */
        Corpse *o = &W.corpses[0];
        decal(o->spr, o->pos, o->angle, 1, TINT_NONE);
        memmove(&W.corpses[0], &W.corpses[1], sizeof(Corpse) * (MAX_CORPSES - 1));
        c = &W.corpses[MAX_CORPSES - 1];
    }
    c->pos = pos;
    c->angle = angle;
    c->spr = spr;
    c->t = 0;
    c->gibbed = gibbed;
    c->shake = 0.25f;
    c->vel = vel;
    c->smear_t = 0;
}

static void corpses_update(float dt) {
    for (int i = 0; i < W.ncorpses; i++) {
        Corpse *c = &W.corpses[i];
        if (c->shake > 0) c->shake -= dt;
        float sp = v2_len(c->vel);
        if (sp < 2) continue;
        c->pos = v2_add(c->pos, v2_scale(c->vel, dt));
        collide_circle(&c->pos, 5, &c->vel);
        c->vel = v2_scale(c->vel, expf(-7 * dt));
        c->smear_t -= dt * sp;
        if (c->smear_t <= 0 && sp > 25) {
            c->smear_t = 9;
            decal(SPR_FX_BLOOD_SMEAR + irange(0, 1), c->pos, v2_to_angle(c->vel) + PI_F, 0.7f, rgba(255, 255, 255, 200));
        }
    }
}

/* ------------------------------------------------------------- pickups */
static bool is_list_item(ItemId id);

/* an arm (or a body) gets from a to b: no wall, window, tall furniture or door leaf in between.
 * Low furniture (counters, crates, car bodies) can be reached over. */
bool reach_clear(V2 a, V2 b) {
    for (int i = 0; i < W.ndoors; i++) {
        Door *d = &W.doors[i];
        if (seg_cross(a, b, d->hinge, v2_add(d->hinge, v2_scale(v2_angle(d->base + d->ang), d->len)))) return false;
    }
    int ax = tile_of(a.x), ay = tile_of(a.y), bx = tile_of(b.x), by = tile_of(b.y);
    if (abs(ax - bx) <= 1 && abs(ay - by) <= 1) return true;   /* touching tiles, corners included (a fridge in a corner) */
    float l = v2_dist(a, b);
    for (float t = 0; t < l; t += 2) {
        V2 q = v2_add(a, v2_scale(v2_sub(b, a), t / l));
        int tx = tile_of(q.x), ty = tile_of(q.y);
        if ((tx == ax && ty == ay) || (tx == bx && ty == by)) continue;
        if (!in_map(tx, ty)) return false;
        Cell *c = cell(tx, ty);
        if (c->wall || c->obj == OB_GLASS_H || c->obj == OB_GLASS_V) return false;
        /* tall furniture only blocks further out - reaching past the end of the same shelf run is fine */
        if ((c->flags & CF_OPAQUE) && (abs(tx - bx) > 1 || abs(ty - by) > 1)) return false;
    }
    return true;
}

/* a point stuck inside solid geometry: move it to the closest open spot, floor the player can reach first */
static bool unstick(V2 *pos, float margin) {
    int tx = tile_of(pos->x), ty = tile_of(pos->y);
    if (!cell_flag(tx, ty, CF_SOLID)) return false;
    float bd = 1e18f;
    V2 best = *pos;
    for (int y = ty - 3; y <= ty + 3; y++)
        for (int x = tx - 3; x <= tx + 3; x++) {
            if (!walkable_tile(x, y)) continue;
            V2 q = v2(CLAMP(pos->x, x * TILE + margin, x * TILE + TILE - margin), CLAMP(pos->y, y * TILE + margin, y * TILE + TILE - margin));
            float d = v2_dist2(q, *pos) + (cell(x, y)->reach ? 0 : 4096);
            if (d < bd) { bd = d; best = q; }
        }
    if (bd >= 1e18f) return false;
    *pos = best;
    return true;
}

int pickup_spawn(Stack st, V2 pos, V2 vel) {
    if (st.id <= IT_NONE || st.id >= IT_COUNT || st.count <= 0) return -1;
    int best = -1;
    for (int i = 0; i < MAX_PICKUPS; i++)
        if (!W.pickups[i].alive) { best = i; break; }
    if (best < 0) {
        /* pool full: the same thing the player dropped close by just gets one more (only dropped piles, since
           callers mark what they get back as dropped and an auto-collecting pile must not lose that) */
        if (ITEMS[st.id].stack > 1 && ITEMS[st.id].cat != CAT_WEAPON)
            for (int i = 0; i < MAX_PICKUPS; i++) {
                Pickup *q = &W.pickups[i];
                if (q->st.id != st.id || !q->dropped || q->flying || q->fuse > 0 || v2_dist2(q->pos, pos) > 24 * 24) continue;
                q->st.count = (int16_t)MINF(q->st.count + st.count, 999);
                return i;
            }
        /* recycle the oldest junk; food and supplies only if there's nothing else; never list items */
        float oldest = -1;
        int tier = 3;
        for (int i = 0; i < MAX_PICKUPS; i++) {
            Pickup *q = &W.pickups[i];
            ItemId id = (ItemId)q->st.id;
            if (q->flying || q->fuse > 0 || is_list_item(id)) continue;
            int t = (ITEMS[id].cat == CAT_FOOD || ITEMS[id].cat == CAT_SUPPLY) ? 2 : 1;
            if (t < tier || (t == tier && q->age > oldest)) { tier = t; oldest = q->age; best = i; }
        }
        if (best < 0) return -1;
    }
    Pickup *p = &W.pickups[best];
    memset(p, 0, sizeof *p);
    p->alive = true;
    p->st = st;
    p->pos = pos;
    p->vel = vel;
    p->angle = frange(-PI_F, PI_F);
    p->thrower = -1;
    p->bob = frange(0, 6);
    collide_circle(&p->pos, 3, NULL);
    unstick(&p->pos, 4);
    return best;
}

static void shatter(Pickup *p, bool molotov) {
    for (int i = 0; i < 10; i++) {
        Particle *g = particle_add(PT_GLASS, p->pos, v2(frange(-90, 90), frange(-90, 90)), frange(0.4f, 0.9f));
        g->spr = SPR_FX_GLASS + irange(0, 2);
        g->z = 3;
        g->vz = frange(20, 60);
        g->bake = true;
    }
    play_at(SFX_BOTTLE_BREAK, p->pos, 0.9f, frange(0.9f, 1.1f));
    if (molotov) {
        play_at(SFX_IGNITE, p->pos, 1, 1);
        float r = RUN.perks[PK_FIREBUG] && p->thrower == 0 ? 34 : 24;
        fire_spawn(p->pos, 7, p->thrower);
        for (int i = 0; i < 9; i++) {
            V2 o = v2_add(p->pos, v2_scale(v2_angle(i * 0.7f + frange(0, 0.5f)), frange(8, r)));
            if (los_clear(p->pos, o, true)) fire_spawn(o, frange(5, 8), p->thrower);
        }
        make_noise(p->pos, 200, p->thrower);
    }
    p->alive = false;
}

/* a pile spreads out a little until nothing lies on top of something else - every item shows, and you can point at it */
#define PILE_GAP 10.0f
static void pickups_spread(float dt) {
    int rest[MAX_PICKUPS], n = 0;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &W.pickups[i];
        if (p->alive && !p->flying && p->fuse <= 0) rest[n++] = i;
    }
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++) {
            Pickup *p = &W.pickups[rest[a]], *q = &W.pickups[rest[b]];
            V2 d = v2_sub(q->pos, p->pos);
            float l2 = v2_len2(d);
            if (l2 >= PILE_GAP * PILE_GAP) continue;
            float l = sqrtf(l2);
            V2 n2 = l > 0.01f ? v2_scale(d, 1 / l) : v2_angle(rest[b] * 2.39996f);   /* right on top: a fixed way apart */
            V2 push = v2_scale(n2, 260 * (1 - l / PILE_GAP) * dt);
            p->vel = v2_sub(p->vel, push);
            q->vel = v2_add(q->vel, push);
        }
}

static void pickups_update(float dt) {
    pickups_spread(dt);
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &W.pickups[i];
        if (!p->alive) continue;
        p->age += dt;
        if (p->st.id == IT_PIPEBOMB && p->fuse > 0) {
            p->fuse -= dt;
            if (chance(dt * 20)) {
                Particle *s = particle_add(PT_SPARK, p->pos, v2(frange(-30, 30), frange(-30, 30)), 0.2f);
                s->spr = SPR_FX_SPARK;
                s->frames = 3;
            }
            if (p->fuse <= 0) {
                p->alive = false;
                explode(p->pos, 64, p->thrower);
                continue;
            }
        }
        /* never leave anything inside a shelf or a wall */
        if (unstick(&p->pos, 4)) p->vel = v2(0, 0);
        float sp = v2_len(p->vel);
        if (sp < 1 && !p->flying) continue;
        V2 p0 = p->pos;
        /* substep so fast throws can't tunnel through walls */
        int steps = (int)(sp * dt / 5.0f) + 1;
        float sdt = dt / steps;
        V2 np = p->pos;
        bool gone = false;
        for (int st = 0; st < steps && !gone; st++) {
            np = v2_add(p->pos, v2_scale(p->vel, sdt));
            /* door leaves stop throws too (and get knocked about by them) */
            bool hit_door = false;
            for (int d = 0; d < W.ndoors; d++) {
                Door *dr = &W.doors[d];
                V2 e = v2_add(dr->hinge, v2_scale(v2_angle(dr->base + dr->ang), dr->len));
                if (!seg_cross(p->pos, np, dr->hinge, e)) continue;
                V2 u = v2_norm(v2_sub(e, dr->hinge)), n = v2(-u.y, u.x);
                float vn = v2_dot(p->vel, n);
                float lever = CLAMP(v2_dot(v2_sub(np, dr->hinge), u) / dr->len, 0.0f, 1.0f);
                dr->av += vn * lever * 0.02f;
                if (p->flying && fabsf(vn) > 60) {
                    dr->pusher = p->thrower;
                    play_at(SFX_DOOR_SLAM, np, 0.5f, frange(1.2f, 1.4f));
                }
                if (p->flying && (p->st.id == IT_MOLOTOV || p->st.id == IT_BOTTLE)) { shatter(p, p->st.id == IT_MOLOTOV); gone = true; break; }
                p->vel = v2_sub(p->vel, v2_scale(n, vn * 1.35f));
                hit_door = true;
                break;
            }
            if (gone) break;
            if (hit_door) { np = p->pos; break; }
            int tx = tile_of(np.x), ty = tile_of(np.y);
            if (cell_flag(tx, ty, CF_SOLID) && !cell_flag(tx, ty, CF_DOOR)) {
                Cell *c = in_map(tx, ty) ? cell(tx, ty) : NULL;
                if (p->flying && c && (c->obj == OB_GLASS_H || c->obj == OB_GLASS_V)) {
                    break_glass(tx, ty, p->pos);
                    p->vel = v2_scale(p->vel, 0.6f);
                } else {
                    if (p->flying && (p->st.id == IT_MOLOTOV || p->st.id == IT_BOTTLE)) { shatter(p, p->st.id == IT_MOLOTOV); gone = true; break; }
                    bool hx = cell_flag(tile_of(np.x), tile_of(p->pos.y), CF_SOLID);
                    bool hy = cell_flag(tile_of(p->pos.x), tile_of(np.y), CF_SOLID);
                    if (hx || !hy) p->vel.x = -p->vel.x * 0.35f;
                    if (hy || !hx) p->vel.y = -p->vel.y * 0.35f;
                    if (p->flying && sp > 150) play_at(SFX_HIT_METAL, p->pos, 0.7f, frange(1.0f, 1.4f));
                    np = p->pos;
                    break;
                }
            }
            p->pos = np;
        }
        if (gone) continue;
        p->pos = np;
        p->angle += p->spin * dt;
        if (p->flying) {
            p->vel = v2_scale(p->vel, MAXF(0.0f, 1.0f - 0.9f * dt));
            p->spin *= MAXF(0.0f, 1.0f - 0.5f * dt);
            /* hit actors: the first one along the whole path this frame (fast throws can't skip anyone) */
            int hj = -1;
            float ht = 2;
            V2 seg = v2_sub(p->pos, p0);
            float sl2 = v2_len2(seg);
            for (int j = 0; j < W.nactors; j++) {
                Actor *a = &W.actors[j];
                if (!a->used || !a->alive || a->down_t > 0) continue;
                if (j == p->thrower || allied(p->thrower, j)) continue;   /* your own throw never comes back to bite you (or yours) */
                float t = sl2 > 1e-6f ? CLAMP(v2_dot(v2_sub(a->pos, p0), seg) / sl2, 0.0f, 1.0f) : 1.0f;
                V2 cp = v2_add(p0, v2_scale(seg, t));
                if (v2_dist2(a->pos, cp) > (a->radius + 4) * (a->radius + 4)) continue;
                if (t < ht) { ht = t; hj = j; }
            }
            if (hj >= 0) {
                Actor *a = &W.actors[hj];
                p->pos = v2_add(p0, v2_scale(seg, ht));
                const WeaponDef *w = item_weapon(p->st.id);
                float dmg = w->throw_dmg;
                if (p->thrower == 0 && chance(0.12f * train_level(STAT_STR))) dmg += 1;
                bool heavy = ARCH[a->arch].heavy;
                int flags = DMG_THROWN;
                if (w->lethal_throw && !heavy) { dmg = 99; flags |= DMG_GORE; }
                if (p->thrower >= 0) a->last_hit_by = p->thrower;
                damage_actor(hj, p->thrower, dmg, p->vel, 140, heavy ? 0.3f : 1.0f, (int)(w - WEAPONS), flags);
                play_at(SFX_HIT_BLUNT, p->pos, 0.8f, 1.2f);
                hitstop(0.04f);
                if (p->st.id == IT_MOLOTOV || p->st.id == IT_BOTTLE) shatter(p, p->st.id == IT_MOLOTOV);
                else {
                    p->vel = v2_scale(p->vel, -0.2f);
                    p->flying = false;
                }
            }
            if (!p->alive) continue;
            if (v2_len(p->vel) < 110) {
                p->flying = false;
                if (p->st.id == IT_MOLOTOV) { shatter(p, true); continue; }
            }
        } else {
            p->vel = v2_scale(p->vel, MAXF(0.0f, 1.0f - 7 * dt));
            p->spin *= MAXF(0.0f, 1.0f - 7 * dt);
        }
    }
}

static bool is_list_item(ItemId id) {
    for (int i = 0; i < W.nlist; i++)
        if (W.list[i].id == id && !W.list[i].done) return true;
    return false;
}

static void pickups_draw(void) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &W.pickups[i];
        if (!p->alive) continue;
        const ItemDef *it = &ITEMS[p->st.id];
        float lift = p->flying ? 4 : 0;
        if (item_is_weapon(p->st.id) && item_weapon(p->st.id)->spr >= 0) {
            gfx_spr_ex(item_weapon(p->st.id)->spr, p->pos.x + 1, p->pos.y + 2, p->angle, 1, 1, rgba(0, 0, 0, 90));
            gfx_spr_ex(item_weapon(p->st.id)->spr, p->pos.x, p->pos.y - lift, p->angle, 1, 1, TINT_NONE);
        } else {
            float bob = p->flying ? 0 : sinf(W.time * 3 + p->bob) * 1.0f;
            gfx_spr_ex(SPR_FX_SHADOW, p->pos.x, p->pos.y + 5, 0, 0.6f, 0.6f, TINT_NONE);
            gfx_spr(it->spr, p->pos.x, p->pos.y - 2 + bob - lift);
        }
    }
}

/* the van's square while the list isn't done: a dashed bay to unload into, like lines painted on the tarmac - lit up
   and running round while you've something for it, red while a thief's on the way */
static void van_bay_draw(void) {
    if (W.hub || W.list_done || W.exiting) return;
    Actor *p = player();
    bool load = p->alive && van_loadable(), thief = W.thief >= 0;
    Color c = thief ? COL_RED : load ? COL_YELLOW : COL_WHITE;
    c.a = thief ? (Uint8)(150 + 90 * sinf(W.time * 9)) : load ? 190 : 60;
    float x0 = W.exit_rect.x + 2, y0 = W.exit_rect.y + 2, w = W.exit_rect.w - 6, h = W.exit_rect.h - 6;
    int run = (load || thief) ? (int)(W.time * 12) : 0;   /* the dashes march round */
    int len = (int)(2 * (w + h));
    for (int i = 0; i < len; i++) {
        if (((i - run) & 7) >= 4) continue;
        float x, y;
        bool horiz = true;
        if (i < w) { x = x0 + i; y = y0; }
        else if (i < w + h) { x = x0 + w; y = y0 + (i - w); horiz = false; }
        else if (i < 2 * w + h) { x = x0 + w - (i - w - h); y = y0 + h; }
        else { x = x0; y = y0 + h - (i - 2 * w - h); horiz = false; }
        gfx_fill(x, y, horiz ? 1 : 2, horiz ? 2 : 1, c);
    }
}

static int prompt_pk = -1;   /* the floor item the prompt offers: marked in the world */

/* the floor item [E] would take: corner brackets round it, so in a pile you see which one your hand's on */
static void pick_marker_draw(void) {
    if (prompt_pk < 0 || !W.pickups[prompt_pk].alive) return;
    Pickup *p = &W.pickups[prompt_pk];
    ItemId id = (ItemId)p->st.id;
    bool weapon = item_is_weapon(id) && item_weapon(id)->spr >= 0;
    const AtlasSprite *a = &g_atlas[weapon ? item_weapon(id)->spr : ITEMS[id].spr];
    float cx = floorf(p->pos.x), cy = floorf(p->pos.y - (weapon ? 0 : 2));
    float r = floorf(MINF(10, MAXF(a->w, a->h) * (weapon ? 0.4f : 0.5f)) + 2 + (sinf(W.time * 8) > 0 ? 1 : 0));
    Color c = is_list_item(id) ? COL_YELLOW : COL_WHITE;
    for (int k = 0; k < 4; k++) {
        float sx = (k & 1) ? 1 : -1, sy = (k & 2) ? 1 : -1;
        float x = cx + sx * r, y = cy + sy * r;
        gfx_fill(sx > 0 ? x - 2 : x, y, 3, 1, c);
        gfx_fill(x, sy > 0 ? y - 2 : y, 1, 3, c);
    }
}

/* ...and over everything: an arrow down at it once your bag's full of things for it */
static void van_bay_arrow(void) {
    Actor *p = player();
    if (W.hub || W.list_done || W.exiting || !p->alive || W.thief >= 0) return;
    if (inv_used(p) < inv_capacity(p) - 1 || !van_loadable()) return;
    float k = 0.5f + 0.5f * sinf(W.time * 6);
    gfx_spr_ex(SPR_UI_ARROW, W.exit_rect.x + W.exit_rect.w / 2, W.exit_rect.y - 6 - k * 3, PI_F / 2, 1, 1, COL_YELLOW);
}

/* what you've loaded, piled by the van's side door */
static void van_load_draw(void) {
    V2 at = van_spot();
    for (int k = 0; k < W.nvan && k < 6; k++) {
        float x = at.x - (k / 3) * 9, y = at.y - 9 + (k % 3) * 9;
        gfx_spr_ex(SPR_FX_SHADOW, x, y + 5, 0, 0.6f, 0.6f, TINT_NONE);
        gfx_spr(ITEMS[W.van_load[k].id].spr, x, y - 2);
    }
}

static void pickups_draw_glow(void) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &W.pickups[i];
        if (!p->alive || p->flying) continue;
        if (is_list_item((ItemId)p->st.id)) {
            float k = 0.45f + 0.25f * sinf(W.time * 5 + i);
            gfx_glow(p->pos.x, p->pos.y - 2, 16, COL_YELLOW, k);
        }
    }
}

/* -------------------------------------------------------------- loot */
void container_fill(Container *c, Rng *r) {
    if (c->dead) return;
    c->n = 0;
    c->searched = false;
    int z = c->zone;
    if (!LOOT[z]) z = Z_STORAGE;
    float p = 0.36f;
    if (c->zone == Z_FRIDGE) p = 0.22f;
    if (c->zone == Z_CAMP) p = 0.95f;
    if (c->zone == Z_CAR) p = 0.3f;
    if (c->zone == Z_CHECKOUT) p = 0.5f;
    if (c->zone == Z_OFFICE || c->zone == Z_STAFF) p = 0.55f;
    if (RUN.perks[PK_HOARDER]) p += 0.22f;
    if (!rng_chance(r, p)) return;
    int count = rng_chance(r, 0.3f + (RUN.perks[PK_HOARDER] ? 0.3f : 0)) ? 2 : 1;
    if (c->zone == Z_CAMP) count = rng_range(r, 2, 3);
    const LootEntry *t = LOOT[z];
    int tot = 0;
    for (int k = 0; k < LOOT_N[z]; k++) tot += t[k].weight;
    for (int n = 0; n < count && c->n < 6; n++) {
        int roll = rng_int(r, tot);
        for (int k = 0; k < LOOT_N[z]; k++) {
            roll -= t[k].weight;
            if (roll >= 0) continue;
            Stack s = {t[k].id, (int16_t)rng_range(r, t[k].lo, t[k].hi), 0};
            if (item_is_weapon(t[k].id)) {
                const WeaponDef *w = item_weapon(t[k].id);
                if (w->kind == WK_GUN) s.cond = (int16_t)rng_range(r, 1, w->mag);
                else if (w->kind != WK_THROWN) s.cond = (int16_t)w->durability;
            }
            bool merged = false;
            for (int m = 0; m < c->n; m++)
                if (c->items[m].id == s.id && ITEMS[s.id].stack > 1) { c->items[m].count += s.count; merged = true; }
            if (!merged) c->items[c->n++] = s;
            break;
        }
    }
}

int container_seen(V2 eye, V2 pos, float reach, int *out_dist) {
    int best = -1;
    float bd = reach * reach;
    int tx = tile_of(pos.x), ty = tile_of(pos.y);
    for (int y = ty - 2; y <= ty + 2; y++)
        for (int x = tx - 2; x <= tx + 2; x++) {
            if (!in_map(x, y)) continue;
            int ci = cell(x, y)->cont;
            if (ci < 0) continue;
            Container *c = &W.conts[ci];
            if (c->dead) continue;
            /* closest point of the tile */
            float cx = CLAMP(pos.x, x * TILE, x * TILE + TILE), cy = CLAMP(pos.y, y * TILE, y * TILE + TILE);
            float d = (cx - pos.x) * (cx - pos.x) + (cy - pos.y) * (cy - pos.y);
            if (c->prop >= 0) d = MINF(d, v2_dist2(c->pos, pos));
            /* an emptied shelf doesn't hide a full one behind it (corner fridges reached diagonally) */
            if (c->searched && c->n == 0) d += 400 + 40 * sqrtf(d);
            if (d >= bd) continue;
            /* not through a wall, a window or a closed door */
            V2 near = v2(CLAMP(eye.x, x * TILE + 1, x * TILE + TILE - 1), CLAMP(eye.y, y * TILE + 1, y * TILE + TILE - 1));
            if (!reach_clear(eye, near) && !(c->prop >= 0 && reach_clear(eye, c->pos))) continue;
            bd = d;
            best = ci;
        }
    /* multi-tile props (cars, desks) keyed by their anchor container */
    for (int i = 0; i < W.nconts; i++) {
        Container *c = &W.conts[i];
        if (c->prop < 0 || c->dead) continue;
        float d = v2_dist2(c->pos, pos) - 14 * 14;
        if (c->searched && c->n == 0) d += 400 + 40 * sqrtf(MAXF(0, d));
        if (d >= bd || !reach_clear(eye, c->pos)) continue;
        bd = d;
        best = i;
    }
    if (out_dist) *out_dist = (int)sqrtf(MAXF(0, bd));
    return best;
}

void search_container(Actor *a, int ci, bool by_player) {
    Container *c = &W.conts[ci];
    c->searched = true;
    if (!by_player) {
        int keep = 0;
        for (int k = 0; k < c->n; k++) {
            if (!inv_add(a, c->items[k])) {
                c->items[keep++] = c->items[k];   /* pockets full: it stays on the shelf */
                continue;
            }
            if (is_list_item((ItemId)c->items[k].id) && v2_dist(a->pos, player()->pos) < 200 &&
                los_clear(a->pos, player()->pos, false)) {
                char buf[64];
                SDL_snprintf(buf, sizeof buf, "THEY TOOK THE %s!", ITEMS[c->items[k].id].name);
                for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
                world_message(buf, COL_ORANGE);
            }
        }
        c->n = keep;
        return;
    }
    if (c->n == 0) {
        floater(v2(c->pos.x, c->pos.y - 10), "Nothing useful", COL_GREY, false);
        audio_play(SFX_UI_BACK, 0.4f, 0, 0.8f);
        return;
    }
    float y = c->pos.y - 10;
    /* what doesn't fit tumbles off the shelf edge on the searcher's side - never into the shelf or the wall behind */
    V2 to = v2_norm(v2_sub(c->pos, a->pos)), edge = a->pos;
    for (float t = 0, l = v2_dist(a->pos, c->pos); t < l; t += 2) {
        V2 q = v2_add(a->pos, v2_scale(to, t));
        if (solid_at(q.x, q.y)) break;
        edge = q;
    }
    edge = v2_sub(edge, v2_scale(to, 3));
    for (int k = 0; k < c->n; k++) {
        Stack s = c->items[k];
        bool stored = false;
        if (a->cart >= 0 && ITEMS[s.id].cat != CAT_WEAPON && ITEMS[s.id].cat != CAT_BAG) {
            Cart *ct = &W.carts[a->cart];
            int used = 0;
            for (int m = 0; m < ct->n; m++) used += ITEMS[ct->items[m].id].size * ct->items[m].count;
            int same = -1;
            for (int m = 0; m < ct->n && ITEMS[s.id].stack > 1; m++) if (ct->items[m].id == s.id) same = m;
            bool fits = used + ITEMS[s.id].size * s.count <= CART_CAP && (ct->n < 16 || same >= 0);
            if (!fits && item_needed((ItemId)s.id) && !inv_can_fit(a, (ItemId)s.id, s.count) &&
                cart_make_room(ct, (ItemId)s.id, s.count)) {
                fits = true;
                same = -1;
                for (int m = 0; m < ct->n && ITEMS[s.id].stack > 1; m++) if (ct->items[m].id == s.id) same = m;
            }
            if (fits) {
                if (same >= 0) ct->items[same].count += s.count;
                else ct->items[ct->n++] = s;
                stored = true;
            }
        }
        if (!stored && ITEMS[s.id].cat != CAT_WEAPON && ITEMS[s.id].cat != CAT_BAG) {
            stored = inv_add(a, s);
            if (!stored && item_needed((ItemId)s.id) && make_room(a, (ItemId)s.id, s.count)) stored = inv_add(a, s);
        }
        if (!stored) {
            V2 side = v2(-to.y, to.x);
            V2 at = v2_add(edge, v2_scale(side, frange(-5, 5)));
            pickup_spawn(s, at, v2_add(v2_scale(to, -frange(20, 45)), v2_scale(side, frange(-25, 25))));
        }
        char buf[48];
        if (s.count > 1) SDL_snprintf(buf, sizeof buf, "%s x%d", ITEMS[s.id].name, s.count);
        else SDL_snprintf(buf, sizeof buf, "%s", ITEMS[s.id].name);
        floater(v2(c->pos.x, y), buf, is_list_item((ItemId)s.id) ? COL_YELLOW : COL_WHITE, false);
        y -= 9;
    }
    audio_play(SFX_LOOT_FOUND, 0.8f, 0, 1);
    c->n = 0;
    list_recount();
}

/* ------------------------------------------------------------ shopping */
void list_recount(void) {
    /* a parked cart counts while you can see it nearby - and for a moment after (no flicker walking round a shelf) */
    Actor *p = player();
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (!c->alive || !p->alive) continue;
        if (c->holder == 0 || (v2_dist(c->pos, p->pos) < 72 && los_clear(p->pos, c->pos, false))) c->near_until = W.time + 1.0f;
    }
    bool all = true;
    int got = 0;
    for (int i = 0; i < W.nlist; i++) {
        ListEntry *e = &W.list[i];
        int have = total_have(e->id);
        bool done = have >= e->need;
        if (done && !e->done) {
            e->flash = 1.0f;
            if (!e->ticked) audio_play(SFX_LIST_TICK, 0.9f, 0, 1);
            e->ticked = true;
        }
        e->have = have;
        e->done = done;
        if (!done) all = false;
        got += MINF(have, e->need);
    }
    /* real progress (not a parked cart coming back into view) starts the tips over */
    if (got > W.list_best) {
        W.list_best = got;
        W.stall_t = 0;
        W.tip_stage = 0;
        W.trail = TR_NONE;
    }
    for (int i = 0; i < W.nbonus; i++) {
        ListEntry *e = &W.bonus[i];
        e->have = total_have(e->id);
        bool d = e->have >= e->need;
        if (d && !e->done) e->flash = 1.0f;
        e->done = d;
    }
    for (int i = 0; i < W.nfav; i++) {
        ListEntry *e = &W.fav[i];
        e->have = total_have(e->id);
        bool d = e->have >= e->need;
        if (d && !e->done) {
            e->flash = 1.0f;
            if (!e->ticked) {
                char buf[40];
                SDL_snprintf(buf, sizeof buf, "FOR %s!", ARCH[FAVOURS[W.fav_of[i]].arch].name);
                for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
                floater(v2(p->pos.x, p->pos.y - 22), buf, COL_GREEN, false);
                audio_play(SFX_LIST_TICK, 0.9f, 0, 1.15f);
            }
            e->ticked = true;
        }
        e->done = d;
    }
    if (all && !W.list_done && W.nlist > 0) {
        W.list_done = true;
        W.list_done_t = 0;
        if (!W.list_announced) {
            W.list_announced = true;
            world_message("LIST COMPLETE - BACK TO THE VAN!", COL_GREEN);
            audio_play(SFX_LIST_DONE, 1, 0, 1);
        }
    } else if (!all && W.list_done) {
        W.list_done = false;
    }
}

int list_want(ItemId id) {
    int n = 0;
    for (int i = 0; i < W.nlist; i++) if (W.list[i].id == id) n += W.list[i].need;
    for (int i = 0; i < W.nfav; i++) if (W.fav[i].id == id) n += W.fav[i].need;
    return n;
}

/* ----------------------------------------------------- stuck on the list */
#define TIP_RADIO_AT 40.0f     /* s without list progress before the camp radio says where to look */
#define TIP_TRACKER_AT 75.0f   /* ... and before the list starts pointing the way */

/* does that container / pickup / actor / cart still hold a unit of `id` the list needs? */
static bool trail_holds(TrailKind k, int i, ItemId id, V2 *pos) {
    if (!item_needed(id)) return false;
    switch (k) {
    case TR_SHELF: {
        Container *c = &W.conts[i];
        if (c->dead) return false;
        for (int m = 0; m < c->n; m++)
            if (c->items[m].id == id) { *pos = c->pos; return true; }
        return false;
    }
    case TR_FLOOR:
        if (!W.pickups[i].alive || W.pickups[i].st.id != id) return false;
        *pos = W.pickups[i].pos;
        return true;
    case TR_CARRIED:
        if (!W.actors[i].used || !W.actors[i].alive || inv_count(&W.actors[i], id) <= 0) return false;
        *pos = W.actors[i].pos;
        return true;
    case TR_CART: {
        Cart *c = &W.carts[i];
        if (!c->alive || cart_counts(c)) return false;   /* a cart that counts is already ticked off */
        for (int m = 0; m < c->n; m++)
            if (c->items[m].id == id) { *pos = c->pos; return true; }
        return false;
    }
    default:
        return false;
    }
}

/* the closest missing unit: on a shelf, on the floor, in somebody's pockets or in a cart left behind */
static void trail_find(void) {
    int n[] = {[TR_SHELF] = W.nconts, [TR_FLOOR] = MAX_PICKUPS, [TR_CARRIED] = W.nactors, [TR_CART] = MAX_CARTS};
    V2 from = player()->pos;
    float bd = 1e18f;
    W.trail = TR_NONE;
    for (int k = TR_SHELF; k <= TR_CART; k++)
        for (int i = k == TR_CARRIED ? 1 : 0; i < n[k]; i++)
            for (int e = 0; e < W.nlist; e++) {
                V2 at;
                if (!trail_holds((TrailKind)k, i, W.list[e].id, &at) || v2_dist2(at, from) >= bd) continue;
                bd = v2_dist2(at, from);
                W.trail = (TrailKind)k;
                W.trail_i = i;
                W.trail_id = W.list[e].id;
            }
}

bool list_trail(V2 *pos) {
    return W.trail != TR_NONE && trail_holds(W.trail, W.trail_i, W.trail_id, pos);
}

static const char *trail_place(const Container *c) {
    static const char *const PLACE[Z_COUNT] = {
        [Z_GROCERY] = "the grocery shelves", [Z_DRINKS] = "the drinks aisle", [Z_FRIDGE] = "the fridges",
        [Z_HARDWARE] = "the tool aisles", [Z_PHARMACY] = "the pharmacy shelves", [Z_STORAGE] = "the stockroom",
        [Z_OFFICE] = "the office", [Z_STAFF] = "the staff lockers", [Z_CHECKOUT] = "the checkouts",
        [Z_GARDEN] = "the garden centre", [Z_CLOTHES] = "the clothes racks", [Z_ELECTRONICS] = "electronics",
        [Z_RESTROOM] = "the restrooms", [Z_CAMP] = "somebody's stash", [Z_CAR] = "the cars outside",
        [Z_SNACKS] = "the snack racks",
    };
    if (c->name && !strcmp(c->name, "Dumpster")) return "the dumpsters";
    if (c->name && !strcmp(c->name, "Vending machine")) return "the vending machines";
    if (c->name && !strcmp(c->name, "Washer")) return "the washing machines";
    if (c->name && !strcmp(c->name, "Freezer")) return "the freezers";
    return c->zone > Z_NONE && c->zone < Z_COUNT && PLACE[c->zone] ? PLACE[c->zone] : "the shelves";
}

static const char *trail_carrier(const Actor *a) {
    switch (a->arch) {
    case AR_SCAV: return "A scavenger";
    case AR_LOOTER: return "A looter";
    case AR_RAIDER: return "A raider";
    case AR_BRUTE: return "A brute";
    case AR_GUNNER: return "A gunman";
    case AR_PIG: return "A Butcher";
    case AR_FERAL: return "A feral";
    case AR_BOSS: return "The Mall King";
    default: return "Somebody";
    }
}

/* "north-west of you" */
static void where_from_player(V2 at, char *where, int n) {
    static const char *const DIR[8] = {"east", "south-east", "south", "south-west", "west", "north-west", "north", "north-east"};
    V2 d = v2_sub(at, player()->pos);
    if (v2_len(d) < 48) SDL_strlcpy(where, "right next to you", n);
    else SDL_snprintf(where, n, "%s of you", DIR[(int)floorf(v2_to_angle(d) / (PI_F / 4) + 0.5f) & 7]);
}

static void radio_tip(void) {
    V2 at;
    if (!list_trail(&at)) return;
    char where[24];
    where_from_player(at, where, sizeof where);
    const char *what = ITEMS[W.trail_id].name;
    char buf[96];
    if (W.tip_stage >= 2) SDL_snprintf(buf, sizeof buf, "Still no %s? Follow the arrow.", what);
    else switch (W.trail) {
        case TR_SHELF: SDL_snprintf(buf, sizeof buf, "%s? Try %s, %s.", what, trail_place(&W.conts[W.trail_i]), where); break;
        case TR_FLOOR: SDL_snprintf(buf, sizeof buf, "%s? There's one on the ground, %s.", what, where); break;
        case TR_CARRIED: SDL_snprintf(buf, sizeof buf, "%s has the %s, %s.", trail_carrier(&W.actors[W.trail_i]), what, where); break;
        case TR_CART: SDL_snprintf(buf, sizeof buf, "The %s is in a cart you left, %s.", what, where); break;
        default: return;
    }
    SDL_strlcpy(W.radio, buf, sizeof W.radio);
    W.radio_t = 6;
    audio_play(SFX_RADIO, 0.7f, 0, 1);
}

/* the list hasn't moved for a while: the radio says where to look, later the list points the way */
static void list_tips(float dt) {
    if (W.list_done || W.nlist == 0 || !player()->alive || W.exiting || W.intro_t < 3.6f) return;
    W.stall_t += dt;
    int stage = W.stall_t >= TIP_TRACKER_AT ? 2 : (W.stall_t >= TIP_RADIO_AT ? 1 : 0);
    V2 at;
    if (stage > W.tip_stage) {
        W.tip_stage = stage;
        if (!list_trail(&at)) trail_find();   /* keep on about the one the radio already named */
        radio_tip();
    } else if (W.tip_stage > 0 && !list_trail(&at)) {
        trail_find();   /* somebody took it, dropped it or wheeled it off: follow it */
    }
}

/* -------------------------------------------------------------- the van */
#define VAN_WATCH 150.0f   /* px: while you're this close to the van nobody tries it */

V2 van_spot(void) { return v2(W.exit_rect.x + W.exit_rect.w - 10, W.exit_rect.y + W.exit_rect.h / 2); }

int van_count(ItemId id) {
    int n = 0;
    for (int k = 0; k < W.nvan; k++) if (W.van_load[k].id == id) n += W.van_load[k].count;
    return n;
}

bool cart_at_van(const Cart *c) {
    if (!c->alive || c->holder == 0) return false;
    return v2_dist(c->pos, W.van) <= 90 ||
           (c->pos.x > W.exit_rect.x - 16 && c->pos.x < W.exit_rect.x + W.exit_rect.w + 16 &&
            c->pos.y > W.exit_rect.y - 16 && c->pos.y < W.exit_rect.y + W.exit_rect.h + 16);
}

int van_haul(void) {
    int n = 0;
    for (int k = 0; k < W.nvan; k++) n += W.van_load[k].count;
    for (int i = 0; i < MAX_CARTS; i++)
        if (cart_at_van(&W.carts[i]))
            for (int k = 0; k < W.carts[i].n; k++) n += W.carts[i].items[k].count;
    return n;
}

/* units the list, the favours and the bonus want in all */
static int van_wanted(ItemId id) {
    int n = list_want(id);
    for (int i = 0; i < W.nbonus; i++) if (W.bonus[i].id == id) n += W.bonus[i].need;
    return n;
}

static bool van_put(Stack s) {
    int used = 0;
    for (int k = 0; k < W.nvan; k++) used += ITEMS[W.van_load[k].id].size * W.van_load[k].count;
    if (used + ITEMS[s.id].size * s.count > VAN_CAP) return false;
    if (ITEMS[s.id].stack > 1)
        for (int k = 0; k < W.nvan; k++)
            if (W.van_load[k].id == s.id) { W.van_load[k].count += s.count; return true; }
    if (W.nvan >= ARRAY_LEN(W.van_load)) return false;
    W.van_load[W.nvan++] = s;
    return true;
}

bool van_loadable(void) {
    Actor *p = player();
    for (int k = 0; k < p->ninv; k++)
        if (van_wanted((ItemId)p->inv[k].id) > van_count((ItemId)p->inv[k].id)) return true;
    return false;
}

/* spares stay in the bag: only what's still short in the van goes in */
int van_load_bag(void) {
    Actor *p = player();
    int moved = 0;
    for (int i = 0; i < p->ninv;) {
        Stack *s = &p->inv[i];
        int n = MINF(s->count, van_wanted((ItemId)s->id) - van_count((ItemId)s->id));
        Stack part = *s;
        part.count = (int16_t)n;
        if (n <= 0 || !van_put(part)) { i++; continue; }
        moved += n;
        s->count -= n;
        if (s->count > 0) { i++; continue; }
        memmove(&p->inv[i], &p->inv[i + 1], sizeof(Stack) * (p->ninv - i - 1));
        p->ninv--;
    }
    if (moved) list_recount();
    return moved;
}

void van_rob(Actor *a, int idx) {
    int took = 0;
    ItemId what = IT_NONE;   /* the one to name: something the list wants, else anything */
    for (int k = 0; k < W.nvan;) {
        Stack s = W.van_load[k];
        if (!inv_add(a, s)) { k++; continue; }   /* pockets full: it stays */
        took += s.count;
        if (!what || (list_want((ItemId)s.id) && !list_want(what))) what = (ItemId)s.id;
        memmove(&W.van_load[k], &W.van_load[k + 1], sizeof(Stack) * (W.nvan - k - 1));
        W.nvan--;
    }
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (!cart_at_van(c)) continue;
        for (int k = 0; k < c->n;) {
            Stack s = c->items[k];
            if (!inv_add(a, s)) { k++; continue; }
            took += s.count;
            if (!what || (list_want((ItemId)s.id) && !list_want(what))) what = (ItemId)s.id;
            memmove(&c->items[k], &c->items[k + 1], sizeof(Stack) * (c->n - k - 1));
            c->n--;
        }
    }
    list_recount();
    if (!took) return;
    /* getting it back counts as progress on the list (the tips start over) */
    int got = 0;
    for (int i = 0; i < W.nlist; i++) got += MINF(W.list[i].have, W.list[i].need);
    W.list_best = MINF(W.list_best, got);
    SDL_Log("VAN: robbed by %s (%d things, %s among them)", ARCH[a->arch].name, took, ITEMS[what].name);
    world_message("THE VAN'S BEEN ROBBED!", COL_RED);
    audio_play(SFX_ALERT, 0.8f, 0, 0.8f);
    char where[24], buf[96];
    where_from_player(a->pos, where, sizeof where);
    if (took > 1) SDL_snprintf(buf, sizeof buf, "%s cleaned out your van - %s and more, %s.", trail_carrier(a), ITEMS[what].name, where);
    else SDL_snprintf(buf, sizeof buf, "%s took the %s from your van, %s.", trail_carrier(a), ITEMS[what].name, where);
    SDL_strlcpy(W.radio, buf, sizeof W.radio);
    W.radio_t = 6;
    audio_play(SFX_RADIO, 0.7f, 0, 1);
    /* the list marks what went and keeps track of who has it */
    if (item_needed(what)) {
        W.trail = TR_CARRIED;
        W.trail_i = idx;
        W.trail_id = what;
        W.tip_stage = MAXF(W.tip_stage, 1);
    }
}

/* where a thief walks in from: the edge of the lot, out of your sight, as close to the van as that allows -
   failing that, anywhere along the edge, as far from you as it gets */
static bool thief_entry(V2 *out) {
    Actor *p = player();
    float bd = 1e18f, fd = -1;
    V2 far = v2(0, 0);
    for (int t = 0; t < 80; t++) {
        int side = irange(0, 3), tx, ty;
        if (side < 2) { tx = side ? irange(W.w - 5, W.w - 3) : irange(2, 4); ty = irange(2, W.h - 3); }
        else { ty = side == 2 ? irange(2, 4) : irange(W.h - 5, W.h - 3); tx = irange(2, W.w - 3); }
        if (!walkable_tile(tx, ty) || !cell(tx, ty)->reach || cell_flag(tx, ty, CF_INDOOR)) continue;
        V2 at = tile_center(tx, ty);
        float dp = v2_dist(at, p->pos);
        if (dp > fd) { fd = dp; far = at; }
        if (dp < 260 || los_clear(p->pos, at, false)) continue;
        float d = v2_dist(at, W.van);
        if (d < 160) d = 2000 - d;   /* not right on top of it */
        if (d < bd) { bd = d; *out = at; }
    }
    if (bd < 1e18f) return true;
    *out = far;
    return fd >= 0;
}

/* every slot taken (a long, bloody visit): a dead one's goes to the newcomer - and nobody may still mean them */
static void free_dead_slot(void) {
    for (int i = W.nactors - 1; i >= 1; i--) {
        Actor *d = &W.actors[i];
        if (!d->used || d->alive || i == W.boss) continue;
        bool busy = false;
        for (int j = 0; j < W.nactors; j++) if (W.actors[j].exec_t > 0 && W.actors[j].exec_target == i) busy = true;
        if (busy) continue;
        for (int j = 0; j < W.nactors; j++) {
            Actor *o = &W.actors[j];
            if (o->br.target == i) o->br.target = -1;
            if (o->br.leader == i) o->br.leader = -1;
            for (int k = 0; k < 4; k++) if (o->br.grudge[k] == i) o->br.grudge[k] = -1;
            if (o->last_hit_by == i) o->last_hit_by = -1;
        }
        for (int k = 0; k < MAX_BULLETS; k++) if (W.bullets[k].alive && W.bullets[k].owner == i) W.bullets[k].alive = false;
        for (int k = 0; k < MAX_PICKUPS; k++) if (W.pickups[k].thrower == i) W.pickups[k].thrower = -1;
        for (int k = 0; k < MAX_FIRES; k++) if (W.fires[k].owner == i) W.fires[k].owner = -1;
        for (int k = 0; k < MAX_CARTS; k++) if (W.carts[k].pusher == i) W.carts[k].pusher = -1;
        for (int k = 0; k < W.ndoors; k++) if (W.doors[k].pusher == i) W.doors[k].pusher = -1;
        if (W.trail == TR_CARRIED && W.trail_i == i) W.trail = TR_NONE;
        d->used = false;
        return;
    }
}

/* what's left at the van while you're off shopping: the longer it's alone, the likelier somebody new turns up for it */
static void van_thieves(float dt) {
    Actor *p = player();
    if (W.hub || W.exiting || !p->alive) return;
    if (W.thief >= 0) {
        Actor *t = &W.actors[W.thief];
        if (t->used && t->alive && t->br.state == AI_STEAL) return;   /* on the way */
        W.thief = -1;   /* got it, got scared off or got killed: the van's still alone, so the next one won't be long */
    }
    if (!van_haul() || v2_dist(p->pos, van_spot()) < VAN_WATCH) {
        W.van_alone = 0;
        return;
    }
    /* chance per second grows with the time alone: by T s about 4 in 10 vans are hit, by 2T nearly 9 in 10 */
    W.van_alone += dt;
    float T = MAXF(8.0f, 14.0f - W.level);
    if (!chance(dt * W.van_alone / (T * T))) return;
    V2 at;
    if (!thief_entry(&at)) return;
    int arch = W.level >= 4 && chance(0.5f) ? AR_RAIDER : (W.level >= 2 ? AR_LOOTER : AR_SCAV);
    int ai = gen_spawn_npc(arch, at);
    if (ai < 0) {
        free_dead_slot();
        ai = gen_spawn_npc(arch, at);
        if (ai < 0) return;
    }
    Actor *a = &W.actors[ai];
    a->br.state = AI_STEAL;
    a->br.timer = a->br.wander_t = a->br.repath = 0;
    a->br.path_len = a->br.path_i = 0;
    a->spawn_grace = 0;
    W.thief = ai;
    SDL_Log("VAN: a %s comes for the van after %.0fs alone (%d things in it)", ARCH[arch].name, W.van_alone, van_haul());
    SDL_strlcpy(W.radio, "Somebody's heading for your van!", sizeof W.radio);
    W.radio_t = 5;
    audio_play(SFX_RADIO, 0.7f, 0, 1);
}

/* -------------------------------------------------------------- scoring */
void add_score(int pts, V2 pos, const char *why) {
    W.score += pts;
    char buf[40];
    if (why && *why) SDL_snprintf(buf, sizeof buf, "+%d %s", pts, why);
    else SDL_snprintf(buf, sizeof buf, "+%d", pts);
    floater_sticker(v2(pos.x, pos.y - 12), buf, FL_PRICE);
}

void register_kill(int vi, int attacker, int weapon, int flags) {
    Actor *v = &W.actors[vi];
    if (v->arch == AR_BOSS) {
        world_message("THE KING IS DEAD", COL_TAG);
        slowmo(2.0f);
        add_shake(16);
        audio_music(MUS_LEVEL_C);
    }
    bool by_player = attacker == 0;
    if (!by_player || is_crew(v)) return;
    if (is_animal(v) && !v->rabid) return;   /* a stray that never hurt anybody isn't worth a thing */
    W.kills++;
    RUN.kills++;
    W.combo = W.combo_t > 0 ? W.combo + 1 : 1;
    W.combo_t = 3.2f;
    W.max_combo = MAXF(W.max_combo, W.combo);
    int base = ARCH[v->arch].score;
    const char *why = "";
    int bonus = 0;
    if (flags & DMG_EXEC) { bonus = 200; why = "EXECUTION"; W.execs++; RUN.execs++; }
    else if (flags & DMG_SNEAK) { bonus = 150; why = "SILENT"; }
    else if (flags & DMG_DOOR) { bonus = 300; why = "DOOR SLAM"; W.door_kills++; }
    else if (flags & DMG_CART) { bonus = 250; why = "CLEAN-UP"; }
    else if (flags & DMG_THROWN) { bonus = 150; why = "THROWN"; W.throws_kills++; }
    else if (flags & DMG_EXPLOSION) { bonus = 100; why = "BOOM"; }
    else if (flags & DMG_FIRE) { bonus = 100; why = "WELL DONE"; }
    else if (flags & DMG_GORE) { bonus = 50; why = "BRUTAL"; }
    int pts = (base + bonus) * MINF(W.combo, 8);
    W.score_kills += pts;
    add_score(pts, v->pos, why);
    if (W.combo >= 2) {
        char buf[24];
        SDL_snprintf(buf, sizeof buf, "%dX COMBO", W.combo);
        floater_sticker(v2(v->pos.x, v->pos.y - 26), buf, FL_SALE);
        audio_play(SFX_COMBO, 0.85f, 0, 1.0f + 0.08f * MINF(W.combo, 10));
    }
    if (RUN.perks[PK_ADRENALINE]) slowmo(0.7f);
    (void)weapon;
}

/* -------------------------------------------------------------- doors */
void doors_update(float dt) {
    for (int i = 0; i < W.ndoors; i++) {
        Door *d = &W.doors[i];
        d->slam_cd -= dt;
        V2 dir = v2_angle(d->base + d->ang);
        V2 e = v2_add(d->hinge, v2_scale(dir, d->len));
        for (int j = 0; j < W.nactors; j++) {
            Actor *a = &W.actors[j];
            if (!a->used || !a->alive) continue;
            /* closest point on the door segment */
            V2 ab = v2_sub(e, d->hinge);
            float t = CLAMP(v2_dot(v2_sub(a->pos, d->hinge), ab) / v2_len2(ab), 0.0f, 1.0f);
            V2 cp = v2_add(d->hinge, v2_scale(ab, t));
            V2 diff = v2_sub(a->pos, cp);
            float dist = v2_len(diff);
            float r = a->radius + 1.5f;
            if (dist >= r || dist < 1e-4f) continue;
            V2 n = v2_scale(diff, 1.0f / dist);
            float side = ab.x * (a->pos.y - d->hinge.y) - ab.y * (a->pos.x - d->hinge.x);
            /* fast-swinging door slams into whoever is in the way */
            if (fabsf(d->av) > 7.0f && a->down_t <= 0 && t > 0.35f && d->slam_cd <= 0 && j != d->pusher) {
                bool towards = (side > 0) == (d->av < 0);
                if (towards) {
                    damage_actor(j, d->pusher, 1, v2_scale(n, -1), 160, 1.0f, -1, DMG_DOOR);
                    play_at(SFX_DOOR_SLAM, cp, 1, frange(0.9f, 1.1f));
                    add_shake(5);
                    d->av *= -0.4f;
                    d->slam_cd = 0.3f;
                    continue;
                }
            }
            if (a->down_t > 0) continue;
            /* actor pushes the door open */
            V2 vel = a->vel;
            float push = (side > 0 ? -1.0f : 1.0f);
            float sp = MAXF(v2_len(vel), 30);
            float torque = push * sp * t * 0.09f * (dt * 60.0f);
            if (j == 0 && sp > 80) torque *= 1.6f;
            d->av += torque;
            if (fabsf(torque) > 0.3f * (dt * 60.0f)) d->pusher = j;
            if (fabsf(d->av) > 4 && d->slam_cd <= 0 && fabsf(torque) > 3 * (dt * 60.0f)) {
                play_at(SFX_DOOR_SLAM, cp, 0.7f, frange(1.1f, 1.3f));
                d->slam_cd = 0.4f;
            } else if (fabsf(torque) > 0.5f && chance(dt * 1.8f)) {
                play_at(SFX_DOOR_CREAK, cp, 0.6f, frange(0.9f, 1.2f));
            }
            /* step out of the leaf - but never into a wall: if the leaf pins someone there, it gives way */
            V2 np = v2_add(cp, v2_scale(n, r));
            collide_circle(&np, a->radius, NULL);
            a->pos = np;
            V2 off = v2_sub(np, d->hinge);
            float along = CLAMP(v2_dot(off, ab) / v2_len2(ab), 0.0f, 1.0f);
            float gap = v2_dist(np, v2_add(d->hinge, v2_scale(ab, along)));
            if (gap < r - 0.5f && along > 0.05f) {
                V2 tang = v2(-ab.y, ab.x);   /* the way a positive swing moves the leaf */
                float away = v2_dot(n, tang) > 0 ? -1.0f : 1.0f;
                d->ang += away * (r - gap) / (along * d->len);
                d->av = away * MAXF(fabsf(d->av) * 0.3f, 0.5f);
            }
        }
        d->ang += d->av * dt;
        d->av *= expf(-3.0f * dt);
        /* a little past square, never folded back into the wall beside the frame */
        const float lim = 1.6f;
        if (d->ang > lim) { d->ang = lim; d->av = -fabsf(d->av) * 0.3f; }
        if (d->ang < -lim) { d->ang = -lim; d->av = fabsf(d->av) * 0.3f; }
        /* once it has settled, nobody "owns" the swing any more (no credit, no immunity for an old shove) */
        if (fabsf(d->av) < 0.5f) d->pusher = -1;
    }
}

void doors_draw(void) {
    for (int i = 0; i < W.ndoors; i++) {
        Door *d = &W.doors[i];
        gfx_spr_ex(d->glass ? SPR_P_DOOR_GLASS : SPR_P_DOOR, d->hinge.x, d->hinge.y, d->base + d->ang, 1, 1, TINT_NONE);
    }
}

/* -------------------------------------------------------------- carts */
int cart_near(V2 pos, float r) {
    int best = -1;
    float bd = r * r;
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (!c->alive || c->holder >= 0) continue;
        float d = v2_dist2(c->pos, pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void carts_update(float dt) {
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (!c->alive) continue;
        c->crash_cd -= dt;
        Actor *h = c->holder >= 0 ? &W.actors[c->holder] : NULL;
        if (h && (!h->alive || h->cart != i)) { c->holder = -1; h = NULL; }
        if (h) {
            /* the cart rides in front; in tight spots (doorways) it tucks in closer */
            V2 dir = v2_angle(h->face);
            V2 want = v2_add(h->pos, v2_scale(dir, 13));
            bool placed = false;
            for (float dist = 13; dist >= 6; dist -= 3.5f) {
                V2 t = v2_add(h->pos, v2_scale(dir, dist));
                V2 before = t;
                collide_circle(&t, 5.5f, NULL);
                if (v2_dist2(before, t) < 0.25f) { want = t; placed = true; break; }
            }
            if (!placed) {
                V2 before = want;
                collide_circle(&want, 5.5f, NULL);
                V2 corr = v2_sub(want, before);
                if (v2_len2(corr) > 0.01f) h->pos = v2_add(h->pos, v2_scale(corr, 0.6f));
            }
            c->vel = h->vel;
            c->pos = v2_add(c->pos, v2_scale(v2_sub(want, c->pos), smooth_k(30, dt)));
            c->angle = h->face;
        } else {
            V2 vb0 = c->vel;
            bool hit = false;
            int steps = (int)(v2_len(c->vel) * dt / 5.0f) + 1;
            for (int k = 0; k < steps; k++) {
                c->pos = v2_add(c->pos, v2_scale(c->vel, dt / steps));
                V2 before = c->pos;
                collide_circle(&c->pos, 6, &c->vel);   /* also kills the speed going into the wall */
                if (v2_dist2(before, c->pos) > 1e-4f) hit = true;
            }
            c->angle += c->av * dt;
            c->av *= expf(-2 * dt);
            if (hit) {
                /* the wall soaked up `lost`: bounce a bit of it back, and crash if it was a real hit */
                V2 lost = v2_sub(vb0, c->vel);
                float imp = v2_len(lost);
                c->vel = v2_sub(c->vel, v2_scale(lost, 0.3f));
                if (imp > 90 && c->crash_cd <= 0) {
                    play_at(SFX_CART_HIT, c->pos, 0.8f, frange(0.9f, 1.1f));
                    c->crash_cd = 0.25f;
                    c->vel = v2_scale(c->vel, 0.6f);
                    c->av += frange(-3, 3);
                }
            }
            c->vel = v2_scale(c->vel, expf(-1.4f * dt));
            if (v2_len(c->vel) < 8) c->pusher = -1;   /* at rest: an old shove no longer owns it */
        }
        float sp = v2_len(c->vel);
        if (sp > 40) {
            c->rattle_t -= dt * sp / 60;
            if (c->rattle_t <= 0) { c->rattle_t = 1; play_at(SFX_CART_ROLL, c->pos, 0.6f, frange(0.9f, 1.15f)); }
        }
        /* ramming */
        if (sp > 70 && c->crash_cd <= 0) {
            for (int j = 0; j < W.nactors; j++) {
                Actor *a = &W.actors[j];
                if (!a->used || !a->alive || a->down_t > 0 || j == c->holder) continue;
                if (h == NULL && j == c->pusher) continue;
                if (v2_dist2(a->pos, c->pos) > (a->radius + 8) * (a->radius + 8)) continue;
                int owner = h ? c->holder : c->pusher;
                if (owner == j) continue;
                float dmg = h ? 1 : (sp > 200 ? 3 : 2);
                damage_actor(j, owner, dmg, c->vel, sp * 0.8f, h ? 0.7f : 1.0f, -1, DMG_CART);
                play_at(SFX_CART_HIT, c->pos, 1, 1);
                add_shake(4);
                c->crash_cd = 0.4f;
                if (!h) c->vel = v2_scale(c->vel, 0.4f);
                break;
            }
        }
        /* push carts out of actors when idle */
        if (!h) {
            for (int j = 0; j < W.nactors; j++) {
                Actor *a = &W.actors[j];
                if (!a->used || !a->alive) continue;
                V2 d = v2_sub(c->pos, a->pos);
                float l = v2_len(d);
                float r = a->radius + 6;
                if (l < r && l > 0.01f) {
                    V2 nrm = v2_scale(d, 1.0f / l);
                    c->pos = v2_add(a->pos, v2_scale(nrm, r));
                    collide_circle(&c->pos, 6, NULL);
                    /* it rolls off a little faster than it was bumped - the same at any frame rate */
                    float want = MAXF(v2_dot(a->vel, nrm) * 1.15f + 15, 20);
                    float vc = v2_dot(c->vel, nrm);
                    if (vc < want) c->vel = v2_add(c->vel, v2_scale(nrm, want - vc));
                    if (v2_len(a->vel) > 30) c->pusher = j;
                }
            }
        }
    }
}

void carts_draw(void) {
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (!c->alive) continue;
        gfx_spr_ex(SPR_FX_SHADOW, c->pos.x + 2, c->pos.y + 3, c->angle, 1.4f, 1.0f, TINT_NONE);
        gfx_spr_ex(SPR_P_CART, c->pos.x, c->pos.y, c->angle, 1, 1, TINT_NONE);
    }
}

/* --------------------------------------------------------------- props */
static void prop_draw(Prop *p) {
    if (p->angle != 0 && g_atlas[p->spr].px == 0 && g_atlas[p->spr].py == 0) {
        /* top-left anchored prop rotated 180 degrees: draw flipped in its footprint */
        const AtlasSprite *a = &g_atlas[p->spr];
        gfx_spr_ex(p->spr, p->x + a->w, p->y + a->h, 0, -1, -1, p->tint);
        return;
    }
    if (p->angle != 0) gfx_spr_ex(p->spr, p->x, p->y, p->angle, 1, 1, p->tint);
    else gfx_spr_c(p->spr, p->x, p->y, p->tint);
}

extern bool g_draw_all;

static void props_draw(int layer) {
    float x0 = G.cam_ox - 64, y0 = G.cam_oy - 64, x1 = G.cam_ox + WORLD_RT_W + 64, y1 = G.cam_oy + WORLD_RT_H + 64;
    if (g_draw_all) { x0 = -99; y0 = -99; x1 = 1e9f; y1 = 1e9f; }
    for (int i = 0; i < W.nprops; i++) {
        Prop *p = &W.props[i];
        if (p->layer != layer) continue;
        if (p->x < x0 || p->x > x1 || p->y < y0 || p->y > y1) continue;
        prop_draw(p);
    }
}

static char prompt_buf[96];
#define KEY_USE ctl("E", "A")
static V2 prompt_pos;

/* ---------------------------------------------------------- level start */
void world_free(void) {
    if (W.ground) SDL_DestroyTexture(W.ground);
    if (W.ambient) SDL_DestroyTexture(W.ambient);
    W.ground = W.ambient = NULL;
}

static void set_ambience(void) {
    switch (W.def->amb) {
    case AMB_DUSK: W.amb_out = rgb(255, 202, 170); W.amb_in = rgb(178, 150, 166); break;
    case AMB_OVERCAST: W.amb_out = rgb(224, 228, 236); W.amb_in = rgb(162, 162, 180); break;
    case AMB_RAIN: W.amb_out = rgb(162, 172, 202); W.amb_in = rgb(130, 130, 156); break;
    case AMB_NIGHT: W.amb_out = rgb(80, 100, 122); W.amb_in = rgb(80, 86, 102); break;
    case AMB_FOG: W.amb_out = rgb(202, 206, 210); W.amb_in = rgb(152, 152, 168); break;
    case AMB_INFERNO: W.amb_out = rgb(136, 92, 70); W.amb_in = rgb(122, 88, 72); break;
    }
}

static void world_begin(void) {
    world_free();
    memset(&W, 0, sizeof W);
    ndq = 0;
    memset(held_press, 0, sizeof held_press);
    combat_reset_level_state();
    actor_reset_level_state();
    hud_reset_level_state();
    prompt_buf[0] = 0;
    prompt_pk = -1;
    for (int i = 0; i < MAX_DOORS; i++) W.doors[i].len = 15;
    W.timescale = 1;
    W.boss = -1;
    W.thief = -1;
    for (int k = 0; k < MAX_CREW; k++) W.crew_actor[k] = -1;
}

/* the player as the run left them: health, hands and bag */
static void world_place_player(void) {
    Actor *p = player();
    p->maxhp = RUN.maxhp;
    p->hp = RUN.hp;
    p->wslot = CLAMP(RUN.wslot, 0, WSLOTS - 1);
    for (int k = 0; k < WSLOTS; k++) {
        Stack s = k == p->wslot ? RUN.weapon : RUN.slots[k];
        if (!item_is_weapon((ItemId)s.id) || s.count <= 0) s = (Stack){IT_NONE, 0, 0, 0};
        p->slots[k] = (Stack){IT_NONE, 0, 0, 0};
        *weapon_slot(p, k) = s;
    }
    /* never trust the count (or the ids) blindly: the run may come from a save file */
    p->ninv = 0;
    for (int k = 0; k < CLAMP(RUN.ninv, 0, INV_MAX); k++)
        if (RUN.inv[k].id > IT_NONE && RUN.inv[k].id < IT_COUNT && RUN.inv[k].count > 0) p->inv[p->ninv++] = RUN.inv[k];
    p->face = -PI_F * 0.5f;
    p->spawn_grace = 0;
}

static void world_finish(void) {
    Actor *p = player();
    map_bake_ground();
    map_build_ambient();
    G.cam_x = p->pos.x;
    G.cam_y = p->pos.y - 40;
    G.cam_angle = 0;
    G.cam_zoom = 1;
    W.intro_t = 0;
    list_recount();
    audio_music((MusicId)W.def->music);
}

void world_start_level(int level) {
    world_begin();
    gen_level(level, RUN.seed);
    set_ambience();
    world_place_player();
    crew_spawn();
    Actor *p = player();
    if (RUN.perks[PK_GUNSLINGER]) {
        /* top every pistol up to a full magazine (STEADY AIM's bigger one, an extended mag too) - never take rounds away */
        Stack g = {IT_PISTOL, 1, 0};
        g.cond = (int16_t)weapon_max_cond(&g);
        bool have = false;
        for (int k = 0; k < WSLOTS; k++) {
            Stack *s = weapon_slot(p, k);
            if (s->id == IT_PISTOL) { s->cond = MAXF(s->cond, weapon_max_cond(s)); have = true; }
        }
        for (int k = 0; k < p->ninv; k++)
            if (p->inv[k].id == IT_PISTOL) { p->inv[k].cond = MAXF(p->inv[k].cond, weapon_max_cond(&p->inv[k])); have = true; }
        int free = weapon_free_slot(p);   /* the hands if they're free, otherwise your back, otherwise the bag */
        if (!have && free >= 0) *weapon_slot(p, free) = g;
        else if (!have && !inv_add(p, g)) pickup_spawn(g, p->pos, v2(0, 0));
        Stack am = {IT_AMMO9, 12, 0};
        inv_add(p, am);
    }
    world_finish();
}

/* the Greenhouse between two stores: same world, nobody to fight */
void world_start_hub(void) {
    world_begin();
    W.hub = true;
    gen_hub(RUN.seed);
    set_ambience();
    world_place_player();
    world_finish();
    W.intro_t = 99;   /* no store title card */
}

/* ---------------------------------------------------------- interaction */

const char *world_prompt(V2 *pos) {
    if (pos) *pos = prompt_pos;
    return prompt_buf[0] ? prompt_buf : NULL;
}

static void update_prompt(void) {
    Actor *p = player();
    prompt_buf[0] = 0;
    prompt_pk = -1;
    if (!p->alive || W.exiting) return;
    prompt_pos = v2(p->pos.x, p->pos.y + 14);
    if (p->exec_t > 0) return;
    if (W.hub && (W.hub_lock != HUB_FREE || hub_prompt(prompt_buf, sizeof prompt_buf, &prompt_pos))) return;
    /* same order and same tests as the [E] / [SPACE] handling in player_update */
    if (execute_target(p) >= 0) {
        SDL_snprintf(prompt_buf, sizeof prompt_buf, "^y[%s]^0 Execute", ctl("SPACE", "X"));
        return;
    }
    if (!W.hub && in_exit(p->pos) && (W.list_done || p->cart >= 0 || van_loadable() || interact_pickup(p) < 0)) {
        if (W.list_done) SDL_snprintf(prompt_buf, sizeof prompt_buf, "^y[%s]^0 Drive home", KEY_USE);
        else if (p->cart >= 0) SDL_snprintf(prompt_buf, sizeof prompt_buf, "^kThe list isn't done.  ^y[%s]^0 Let go", KEY_USE);
        else if (van_loadable()) SDL_snprintf(prompt_buf, sizeof prompt_buf, "^kThe list isn't done.  ^y[%s]^0 Load the van", KEY_USE);
        else if (W.nvan) SDL_snprintf(prompt_buf, sizeof prompt_buf, "^kThe list isn't done.  ^y[%s]^0 What's in the van", ctl("TAB", "BACK"));
        else if (W.time > 12) SDL_strlcpy(prompt_buf, "^kYour van. Come back when the list is done.", sizeof prompt_buf);
        return;
    }
    if (p->cart >= 0) {
        int ci = container_seen(p->pos, v2_add(p->pos, v2_scale(v2_angle(p->face), 14)), 30, NULL);
        if (ci >= 0 && !(W.conts[ci].searched && W.conts[ci].n == 0) && g_search_cont < 0) {
            SDL_snprintf(prompt_buf, sizeof prompt_buf, "^y[%s]^0 Search %s (into cart)", KEY_USE, W.conts[ci].name ? W.conts[ci].name : "");
            prompt_pos = v2(W.conts[ci].pos.x, W.conts[ci].pos.y + 12);
        } else if (g_search_cont < 0)
            SDL_snprintf(prompt_buf, sizeof prompt_buf, "^y[%s]^0 Shove cart  ^y[%s]^0 Let go", ctl("LMB", "RT"), KEY_USE);
        return;
    }
    int pk = interact_pickup(p);
    int here = floor_items(p, NULL, 0);
    /* a pile: the bag screen lays it all out */
    char more[32] = "";
    if (here > 1) SDL_snprintf(more, sizeof more, "  ^y[%s]^0 See all %d", ctl("TAB", "BACK"), here);
    if (pk >= 0) {
        ItemId id = (ItemId)W.pickups[pk].st.id;
        const char *verb = "Pick up";
        if (ITEMS[id].cat == CAT_WEAPON) {
            /* a free slot takes it; every slot full, it takes the place of the one in your hands */
            verb = "Swap for";
            for (int k = 0; k < WSLOTS; k++) {
                Stack *s = weapon_slot(p, k);
                if (!s->id || (s->id == id && s->count < ITEMS[id].stack)) verb = "Take";
            }
        }
        SDL_snprintf(prompt_buf, sizeof prompt_buf, "^y[%s]^0 %s %s%s", KEY_USE, verb, ITEMS[id].name, more);
        prompt_pk = pk;
        return;
    }
    if (here > 1) {
        SDL_snprintf(prompt_buf, sizeof prompt_buf, "^kNo room.%s", more);
        return;
    }
    if (g_search_cont >= 0) return;
    int ci = container_seen(p->pos, v2_add(p->pos, v2_scale(v2_angle(p->face), 6)), 30, NULL);
    if (ci >= 0) {
        Container *c = &W.conts[ci];
        if (!(c->searched && c->n == 0)) {
            SDL_snprintf(prompt_buf, sizeof prompt_buf, "^y[%s]^0 Search %s", KEY_USE, c->name ? c->name : "");
            prompt_pos = v2(c->pos.x, c->pos.y + 12);
        }
        return;
    }
    int cn = cart_near(p->pos, 22);
    if (cn >= 0) SDL_snprintf(prompt_buf, sizeof prompt_buf, "^y[%s]^0 Push cart", KEY_USE);
}

/* ------------------------------------------------------------- camera */
static void camera_update(float dt) {
    Actor *p = player();
    V2 target = p->pos;
    if (p->alive) {
        V2 m = gfx_screen_to_world(IN.mouse_screen.x, IN.mouse_screen.y);
        V2 d = v2_sub(m, p->pos);
        float maxlook = IN.down[ACT_LOOK] ? 170.0f : 55.0f;
        float k = IN.down[ACT_LOOK] ? 0.75f : 0.28f;
        if (IN.pad_active) d = v2_scale(v2_angle(p->face), IN.down[ACT_LOOK] ? 220 : 120);
        float l = v2_len(d) * k;
        if (l > maxlook) l = maxlook;
        target = v2_add(p->pos, v2_scale(v2_norm(d), l));
    }
    if (W.cam_hold) target = W.cam_at;
    /* keep the view inside the map so the void never shows */
    float mw = W.w * TILE, mh = W.h * TILE;
    float hx = VIEW_W * 0.5f - 6, hy = VIEW_H * 0.5f - 6;
    if (mw > hx * 2) target.x = CLAMP(target.x, hx, mw - hx);
    if (mh > hy * 2) target.y = CLAMP(target.y, hy, mh - hy);
    float rate = W.exiting ? 2 : 7;
    G.cam_x = lerpf(G.cam_x, target.x, smooth_k(rate, dt));
    G.cam_y = lerpf(G.cam_y, target.y, smooth_k(rate, dt));
    W.cam_kick_x *= expf(-12 * dt);
    W.cam_kick_y *= expf(-12 * dt);
    W.shake *= expf(-7 * dt);
    float sh = W.shake * SET.shake;
    float t = W.time;
    float ox = (sinf(t * 47.3f) + sinf(t * 31.1f)) * 0.5f * sh * 0.6f + W.cam_kick_x * SET.shake;
    float oy = (sinf(t * 41.7f) + sinf(t * 27.9f)) * 0.5f * sh * 0.6f + W.cam_kick_y * SET.shake;
    G.cam_x += ox * 0.12f;
    G.cam_y += oy * 0.12f;
    /* the camera stays level; only heavy shake knocks it a hair off axis */
    G.cam_angle = lerpf(G.cam_angle, sh * 0.0025f * sinf(t * 23), smooth_k(6, dt));
    float zt = p->exec_t > 0 ? 1.08f : 1.0f;
    if (W.player_dead) zt = 1.15f;
    G.cam_zoom = lerpf(G.cam_zoom, zt, smooth_k(4, dt));
}

/* ------------------------------------------------------------- update */
/* buttons hit during a hitstop freeze are kept for the first frame after it, not dropped */
static void buffer_presses(bool frozen) {
    static const Action acts[] = {ACT_ATTACK, ACT_THROW, ACT_INTERACT, ACT_EXECUTE, ACT_HEAL, ACT_SWAP, ACT_RELOAD,
                                  ACT_SLOT1, ACT_SLOT2, ACT_SLOT3, ACT_WEAPON_NEXT, ACT_WEAPON_PREV};
    for (int i = 0; i < ARRAY_LEN(acts); i++) {
        Action a = acts[i];
        if (frozen) {
            if (IN.pressed[a]) held_press[a] = true;
        } else {
            if (held_press[a]) IN.pressed[a] = true;
            held_press[a] = false;
        }
    }
}

void world_update(float rdt) {
    /* time control (the frozen frame keeps last frame's lights) */
    if (W.hitstop > 0) {
        W.hitstop -= rdt;
        buffer_presses(true);
        camera_update(rdt);
        return;
    }
    buffer_presses(false);
    W.nlights = 0;
    light_reserve = 2;
    if (W.slowmo_t > 0) W.slowmo_t -= rdt;
    float target_ts = W.slowmo_t > 0 ? 0.35f : 1.0f;
    W.timescale = lerpf(W.timescale, target_ts, smooth_k(10, rdt));
    audio_set_timescale(W.timescale);
    float dt = rdt * W.timescale;
    W.time += dt;
    W.intro_t += rdt;

    Actor *p = player();
    if (!W.player_dead) player_update(p, dt);
    else {
        W.dead_t += rdt;
        audio_loop(LOOP_CHAINSAW_IDLE, 0, 1);
        audio_loop(LOOP_CHAINSAW_CUT, 0, 1);
    }
    actor_update(p, 0, dt);
    if (p->alive) attack_update(p, 0, dt);
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive) continue;
        actor_update(a, i, dt);
        if (!a->alive) continue;
        if (W.hub) hub_npc_update(a, i, dt);
        else ai_update(a, i, dt);
    }
    /* actor separation */
    for (int i = 0; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive || a->down_t > 0) continue;
        for (int j = i + 1; j < W.nactors; j++) {
            Actor *b = &W.actors[j];
            if (!b->used || !b->alive || b->down_t > 0) continue;
            V2 d = v2_sub(b->pos, a->pos);
            float r = a->radius + b->radius;
            float l2 = v2_len2(d);
            if (l2 >= r * r || l2 < 1e-6f) continue;
            /* a rooted plant doesn't budge: whoever bumps into it does all the moving - and your crew step aside for you */
            float wa = is_plant(a) && plant_rooted(a) ? 0.0f : 1.0f, wb = is_plant(b) && plant_rooted(b) ? 0.0f : 1.0f;
            if (i == 0 && is_crew(b)) wa = 0;
            if (wa + wb <= 0) continue;
            float l = sqrtf(l2);
            V2 n = v2_scale(d, 1.0f / l);
            float pen = (r - l) / (wa + wb);
            a->pos = v2_sub(a->pos, v2_scale(n, pen * wa));
            b->pos = v2_add(b->pos, v2_scale(n, pen * wb));
        }
    }
    doors_update(dt);
    carts_update(dt);
    pickups_update(dt);
    bullets_update(dt);
    fires_update(dt);
    particles_update(dt);
    for (int i = 0; i < MAX_FLOATERS; i++) if (W.floaters[i].t > 0) W.floaters[i].t -= rdt;
    corpses_update(dt);
    if (W.combo_t > 0) { W.combo_t -= dt; if (W.combo_t <= 0) W.combo = 0; }
    if (W.msg_t > 0) W.msg_t -= rdt;
    if (W.hint_t > 0) W.hint_t -= rdt;
    if (W.radio_t > 0) W.radio_t -= rdt;
    for (int i = 0; i < W.nlist; i++) if (W.list[i].flash > 0) W.list[i].flash -= rdt;
    for (int i = 0; i < W.nbonus; i++) if (W.bonus[i].flash > 0) W.bonus[i].flash -= rdt;
    for (int i = 0; i < W.nfav; i++) if (W.fav[i].flash > 0) W.fav[i].flash -= rdt;

    static float recount_t;
    static int last_craftable = -1;
    recount_t -= rdt;
    if (recount_t <= 0) {
        recount_t = 0.25f;
        list_recount();
        list_tips(0.25f);
        /* tell the player when their junk turns into a recipe */
        int craftable = 0;
        for (int r = 0; r < NUM_RECIPES; r++)
            if (RECIPES[r].flag == RF_NORMAL && can_craft(p, &RECIPES[r])) craftable |= 1 << r;
        if (last_craftable >= 0 && (craftable & ~last_craftable) && p->alive) {
            for (int r = 0; r < NUM_RECIPES; r++)
                if ((craftable & ~last_craftable) & (1 << r)) {
                    char buf[80];
                    SDL_snprintf(buf, sizeof buf, "You can craft: %s  [%s]", ITEMS[RECIPES[r].out].name, ctl("TAB", "BACK"));
                    world_hint(buf);
                    audio_play(SFX_LOOT_FOUND, 0.6f, 0, 1.3f);
                    break;
                }
        }
        last_craftable = craftable;
    }
    if (W.time < 0.5f) last_craftable = -1;
    if (W.list_done) W.list_done_t += rdt;

    if (W.exiting) W.exit_t += rdt;
    reinforcements();
    van_thieves(dt);

    /* weather */
    if (W.def->amb == AMB_RAIN) {
        /* ~360 drops a second, whatever the frame rate */
        for (W.rain_t += dt * 360; W.rain_t >= 1; W.rain_t -= 1) {
            V2 pos = v2(G.cam_x + frange(-280, 300), G.cam_y + frange(-180, 140));
            if (cell_flag(tile_of(pos.x), tile_of(pos.y), CF_INDOOR) && !cell_flag(tile_of(pos.x), tile_of(pos.y), CF_SKY)) continue;
            Particle *r = particle_add(PT_RAIN, pos, v2(-60, 420), frange(0.12f, 0.3f));
            (void)r;
        }
        bool in = cell_flag(tile_of(p->pos.x), tile_of(p->pos.y), CF_INDOOR);
        audio_loop(LOOP_RAIN, in ? 0.45f : 0.9f, 1);
    } else {
        audio_loop(LOOP_RAIN, 0, 1);
    }
    {
        bool in = cell_flag(tile_of(p->pos.x), tile_of(p->pos.y), CF_INDOOR);
        audio_loop(LOOP_WIND, in ? 0.25f : 0.75f, 1);
        audio_loop(LOOP_HUM, in && !W.hub ? 0.55f : 0.0f, 1);
    }

    /* low health heartbeat */
    if (p->alive && p->hp <= 2) {
        W.heartbeat_t -= rdt;
        if (W.heartbeat_t <= 0) { W.heartbeat_t = 0.85f; audio_play(SFX_HEARTBEAT, 0.7f, 0, 1); }
        G.red_pulse = 0.35f + 0.25f * sinf(G.time * 7.4f);
    } else {
        G.red_pulse = MAXF(0, G.red_pulse - rdt);
    }

    /* static lights */
    for (int ty = tile_of((float)G.cam_oy) - 2; ty < tile_of((float)G.cam_oy + WORLD_RT_H) + 2; ty++)
        for (int tx = tile_of((float)G.cam_ox) - 2; tx < tile_of((float)G.cam_ox + WORLD_RT_W) + 2; tx++) {
            if (!in_map(tx, ty)) continue;
            Cell *c = cell(tx, ty);
            if (c->obj == OB_CAMPFIRE) add_light(tile_center(tx, ty), 64 + sinf(W.time * 11 + tx) * 6, COL_ORANGE, 0.9f);
            if (c->obj == OB_BARREL && c->ovar == 1) add_light(tile_center(tx, ty), 58 + sinf(W.time * 13 + ty) * 5, COL_ORANGE, 0.85f);
            if (c->obj == OB_LAMPPOST && (W.def->amb == AMB_NIGHT || W.def->amb == AMB_INFERNO) && ((tx * 7 + ty) % 3 == 0))
                add_light(tile_center(tx, ty), 70, rgb(255, 220, 160), 0.55f);
        }
    for (int i = 0; i < W.nprops; i++)
        if (W.props[i].glow) {
            const AtlasSprite *a = &g_atlas[W.props[i].spr];
            float flick = (sinf(W.time * 23 + i) > 0.92f) ? 0.3f : 1.0f;
            add_light(v2(W.props[i].x + a->w / 2, W.props[i].y + a->h / 2), 80, COL_AMBER, 0.55f * flick);
        }
    /* player flashlight (these two use the slots kept free for them) */
    light_reserve = 0;
    if (p->alive && inv_count(p, IT_FLASHLIGHT) > 0) add_cone(p->pos, p->face, 150, rgb(255, 240, 210), 0.65f);
    bool dark = W.def->amb == AMB_NIGHT || W.def->amb == AMB_INFERNO || W.def->amb == AMB_RAIN;
    add_light(p->pos, dark ? 64 : 30, rgb(255, 230, 220), dark ? 0.32f : 0.22f);

    update_prompt();
    camera_update(rdt);

    /* glints */
    for (int i = 0; i < W.nconts; i++) {
        Container *c = &W.conts[i];
        c->glint_t -= dt;
        if (c->glint_t < -0.5f) c->glint_t = frange(2.5f, 6.0f);
    }
}

/* -------------------------------------------------------------- draw */
static void draw_icon_over(Actor *a) {
    if (a->alert_icon_t > 0 && a->alert_icon) {
        int spr = a->alert_icon == 1 ? SPR_UI_EXCLAIM : (a->alert_icon == 2 ? SPR_UI_QUESTION : SPR_UI_HANDS);
        float bob = sinf(W.time * 10) * 1.0f;
        gfx_spr(spr, a->pos.x, a->pos.y - 13 + bob);
        return;
    }
    /* what they carry: list items glow above heads */
    Actor *p = player();
    float d = v2_dist(a->pos, p->pos);
    bool see = RUN.perks[PK_EYE] || d < 110 || a->br.state == AI_GETAWAY;   /* a thief with your shopping: always */
    if (!see) return;
    for (int k = 0; k < a->ninv; k++) {
        if (is_list_item((ItemId)a->inv[k].id)) {
            float bob = sinf(W.time * 4 + a->pos.x) * 1.5f;
            gfx_glow(a->pos.x, a->pos.y - 22, 14, COL_YELLOW, 0.5f);
            gfx_spr(ITEMS[a->inv[k].id].spr, a->pos.x, a->pos.y - 22 + bob);
            return;
        }
    }
}

static void draw_lasers(void) {
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive || a->down_t > 0 || a->weapon.id != IT_RIFLE) continue;
        if (a->br.state != AI_CHASE || a->br.target < 0 || a->br.aim_t <= 0.05f || a->br.reaction > 0) continue;
        V2 dir = v2_angle(a->face);
        float len = ray_dist(a->pos, a->face, 400);
        V2 e = v2_add(a->pos, v2_scale(dir, len));
        Uint8 al = (Uint8)(80 + 120 * CLAMP(a->br.aim_t / 0.9f, 0.0f, 1.0f));
        gfx_line(a->pos.x, a->pos.y, e.x, e.y, rgba(255, 40, 60, al));
    }
}

void world_draw(void) {
    flush_decals();
    gfx_begin_world();
    map_draw_floor();
    props_draw(0);
    map_draw_objects();
    /* corpses */
    for (int i = 0; i < W.ncorpses; i++) {
        Corpse *c = &W.corpses[i];
        float sh = c->shake > 0 ? frange(-1, 1) : 0;
        gfx_spr_ex(c->spr, c->pos.x + sh, c->pos.y, c->angle, 1, 1, TINT_NONE);
    }
    pickups_draw_glow();
    pickups_draw();
    van_bay_draw();
    van_load_draw();
    carts_draw();
    for (int i = 0; i < W.nactors; i++)
        if (W.actors[i].used && W.actors[i].alive && W.actors[i].down_t <= 0) actor_draw_shadow(&W.actors[i]);
    for (int i = 0; i < W.nactors; i++)
        if (W.actors[i].used && W.actors[i].alive && W.actors[i].down_t > 0) actor_draw(&W.actors[i]);
    for (int i = W.nactors - 1; i >= 0; i--)
        if (W.actors[i].used && W.actors[i].alive && W.actors[i].down_t <= 0) actor_draw(&W.actors[i]);
    doors_draw();
    particles_draw(false);
    bullets_draw();
    map_draw_walls();
    map_draw_overlays();
    props_draw(1);

    /* lighting */
    gfx_begin_light(rgb(0, 0, 0));
    {
        float s = TILE;
        SDL_FRect dst = {(float)(-G.cam_ox) - s * 1.5f, (float)(-G.cam_oy) - s * 1.5f, (W.w + 2) * s, (W.h + 2) * s};
        SDL_RenderTexture(G.ren, W.ambient, NULL, &dst);
    }
    G.off_x = (float)-G.cam_ox;
    G.off_y = (float)-G.cam_oy;
    for (int i = 0; i < W.nlights; i++) {
        Light *l = &W.lights[i];
        if (l->cone) gfx_cone(l->pos.x, l->pos.y, l->ang, l->r, l->c, l->k);
        else gfx_glow(l->pos.x, l->pos.y, l->r, l->c, l->k);
    }
    gfx_end_light();

    /* unlit: fire, muzzle flashes, glints, markers */
    fires_draw();
    particles_draw(true);
    for (int i = 0; i < W.nconts; i++) {
        Container *c = &W.conts[i];
        if (c->dead || c->n == 0 || c->glint_t > 0) continue;
        if (!RUN.perks[PK_EYE] && v2_dist(c->pos, player()->pos) > 120) continue;
        int fr = CLAMP((int)((-c->glint_t) / 0.5f * 4), 0, 3);
        gfx_spr(SPR_P_LOOT_GLINT + fr, c->pos.x + 3, c->pos.y - 5);
    }
    for (int i = 0; i < W.nprops; i++)
        if (W.props[i].glow) {
            const AtlasSprite *a = &g_atlas[W.props[i].spr];
            float flick = (sinf(W.time * 23 + i) > 0.92f) ? 0.2f : 0.55f;
            gfx_glow(W.props[i].x + a->w / 2, W.props[i].y + a->h / 2, 50, COL_AMBER, flick * 0.5f);
        }
    /* drifting fog banks */
    if (W.def->amb == AMB_FOG || W.def->amb == AMB_RAIN) {
        int n = W.def->amb == AMB_FOG ? 14 : 6;
        for (int i = 0; i < n; i++) {
            float sp = 6 + (i % 4) * 3;
            float fx = fmodf(i * 97.3f + W.time * sp, WORLD_RT_W + 160) - 80 + G.cam_ox;
            float fy = fmodf(i * 53.1f + sinf(W.time * 0.1f + i) * 20, WORLD_RT_H + 120) - 60 + G.cam_oy;
            Color c = W.def->amb == AMB_FOG ? rgba(210, 214, 220, 34) : rgba(150, 160, 190, 22);
            gfx_spr_ex(SPR_FX_SMOKE + (i % 4), fx, fy, i * 0.7f, 9 + (i % 3) * 2, 6 + (i % 2) * 2, c);
        }
    }
    draw_lasers();
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (a->used && a->alive) draw_icon_over(a);
    }
    crew_draw_markers();
    /* exit zone marker */
    if (W.list_done) {
        float k = 0.5f + 0.5f * sinf(W.time * 6);
        SDL_FRect r = W.exit_rect;
        Color c = rgba(162, 211, 76, (Uint8)(90 + 120 * k));
        gfx_rect(r.x, r.y, r.w, r.h, c);
        gfx_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, rgba(162, 211, 76, (Uint8)(60 * k)));
        gfx_glow(r.x + r.w / 2, r.y + r.h / 2, 40, COL_GREEN, 0.3f * k);
    }
    van_bay_arrow();
    pick_marker_draw();
    /* search progress ring */
    if (g_search_cont >= 0 && g_search_progress > 0) {
        Container *c = &W.conts[g_search_cont];
        float w = 18;
        gfx_fill(c->pos.x - w / 2 - 1, c->pos.y - 15, w + 2, 4, rgba(11, 10, 16, 220));
        gfx_fill(c->pos.x - w / 2, c->pos.y - 14, w * CLAMP(g_search_progress, 0.0f, 1.0f), 2, COL_YELLOW);
    }
    /* floating texts */
    for (int i = 0; i < MAX_FLOATERS; i++) {
        Floater *f = &W.floaters[i];
        if (f->t <= 0) continue;
        float life = f->big ? 1.6f : 1.1f;
        float k = 1.0f - f->t / life;
        Uint8 a = (Uint8)(255 * CLAMP(f->t * 3, 0.0f, 1.0f));
        if (f->kind == FL_TEXT) {
            Color c = f->c;
            c.a = a;
            gfx_text(f->big ? FONT_BIG : FONT_SMALL, f->text, f->pos.x, f->pos.y - k * 14, c, TXT_CENTER | TXT_OUTLINE);
            continue;
        }
        /* stickers slap down from a little above, hold, then drift off */
        bool sale = f->kind == FL_SALE;
        FontId fn = sale ? FONT_BIG : FONT_SMALL;
        float h = sale ? 15 : 11, w = gfx_text_w(fn, f->text) + 7;
        float slap = k < 0.06f ? (0.06f - k) / 0.06f : 0;
        float x = floorf(f->pos.x - w / 2), y = floorf(f->pos.y - h / 2 - k * k * 12 - slap * 6);
        Color bg = sale ? COL_TAG : COL_YELLOW, ink = sale ? COL_WHITE : COL_BLACK;
        bg.a = ink.a = a;
        gfx_sticker(x, y, w, h, bg);
        gfx_text(fn, f->text, x + 4, y + 2, ink, 0);
    }
    /* interaction prompt */
    V2 pp;
    const char *pr = world_prompt(&pp);
    if (pr && SET.hints) gfx_text(FONT_SMALL, pr, pp.x, pp.y, COL_WHITE, TXT_CENTER | TXT_OUTLINE);
}

/* debug: render the whole level into a PNG */
void world_mapshot(const char *path) {
    int w = W.w * TILE, h = W.h * TILE;
    SDL_Texture *t = SDL_CreateTexture(G.ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!t) return;
    flush_decals();
    g_draw_all = true;
    SDL_SetRenderTarget(G.ren, t);
    SDL_SetRenderDrawColor(G.ren, 0, 0, 0, 255);
    SDL_RenderClear(G.ren);
    G.off_x = G.off_y = 0;
    SDL_RenderTexture(G.ren, W.ground, NULL, NULL);
    props_draw(0);
    map_draw_objects();
    carts_draw();
    for (int i = 0; i < W.nactors; i++) if (W.actors[i].used && W.actors[i].alive) actor_draw(&W.actors[i]);
    doors_draw();
    pickups_draw();
    map_draw_walls();
    map_draw_overlays();
    props_draw(1);
    SDL_Surface *s = SDL_RenderReadPixels(G.ren, NULL);
    if (s) { SDL_SavePNG(s, path); SDL_DestroySurface(s); SDL_Log("saved %s", path); }
    SDL_SetRenderTarget(G.ren, NULL);
    SDL_DestroyTexture(t);
    g_draw_all = false;
}
