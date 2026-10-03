/* LAST AISLE - world simulation + rendering */
#include "world.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"

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

void floater(V2 pos, const char *text, Color c, bool big) {
    int best = 0;
    for (int i = 0; i < MAX_FLOATERS; i++) {
        if (W.floaters[i].t <= 0) { best = i; break; }
        if (W.floaters[i].t < W.floaters[best].t) best = i;
    }
    Floater *f = &W.floaters[best];
    f->pos = pos;
    f->t = big ? 1.6f : 1.1f;
    f->c = c;
    f->big = big;
    SDL_strlcpy(f->text, text, sizeof f->text);
}

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
    ai_on_noise(pos, radius, source);
    if (source == 0 && radius >= 300) W.heat += radius >= 450 ? 2.0f : 1.0f;
}

/* gunfire carries across the dead city: a squad comes to see what the noise is about */
static void reinforcements(void) {
    static const float thresholds[] = {7, 16, 28};
    if (W.level < 1 || W.waves >= 3 || W.heat < thresholds[W.waves] || W.list_done) return;
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

void add_light(V2 pos, float r, Color c, float k) {
    if (W.nlights >= MAX_LIGHTS) return;
    Light *l = &W.lights[W.nlights++];
    l->pos = pos;
    l->r = r;
    l->c = c;
    l->k = k;
    l->cone = false;
}

void add_cone(V2 pos, float ang, float len, Color c, float k) {
    if (W.nlights >= MAX_LIGHTS) return;
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

int pickup_spawn(Stack st, V2 pos, V2 vel) {
    if (st.id <= IT_NONE || st.count <= 0) return -1;
    int best = -1;
    for (int i = 0; i < MAX_PICKUPS; i++)
        if (!W.pickups[i].alive) { best = i; break; }
    if (best < 0) {
        /* recycle the oldest non-list item */
        float oldest = -1;
        for (int i = 0; i < MAX_PICKUPS; i++) {
            ItemId id = (ItemId)W.pickups[i].st.id;
            if (W.pickups[i].flying || is_list_item(id) || ITEMS[id].cat == CAT_FOOD || ITEMS[id].cat == CAT_SUPPLY) continue;
            if (W.pickups[i].age > oldest) { oldest = W.pickups[i].age; best = i; }
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

static void pickups_update(float dt) {
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
        float sp = v2_len(p->vel);
        if (sp < 1 && !p->flying) continue;
        /* substep so fast throws can't tunnel through walls */
        int steps = (int)(sp * dt / 5.0f) + 1;
        float sdt = dt / steps;
        V2 np = p->pos;
        bool gone = false;
        for (int st = 0; st < steps && !gone; st++) {
            np = v2_add(p->pos, v2_scale(p->vel, sdt));
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
            /* hit actors */
            for (int j = 0; j < W.nactors; j++) {
                Actor *a = &W.actors[j];
                if (!a->used || !a->alive || a->down_t > 0) continue;
                if (j == p->thrower && p->age < 0.4f) continue;
                if (v2_dist2(a->pos, p->pos) > (a->radius + 4) * (a->radius + 4)) continue;
                const WeaponDef *w = item_weapon(p->st.id);
                float dmg = w->throw_dmg;
                bool heavy = ARCH[a->arch].heavy;
                int flags = DMG_THROWN;
                if (w->lethal_throw && !heavy) { dmg = 99; flags |= DMG_GORE; }
                if (p->thrower >= 0 && p->thrower != j) {
                    W.actors[j].last_hit_by = p->thrower;
                }
                damage_actor(j, p->thrower, dmg, p->vel, 140, heavy ? 0.3f : 1.0f, (int)(w - WEAPONS), flags);
                play_at(SFX_HIT_BLUNT, p->pos, 0.8f, 1.2f);
                hitstop(0.04f);
                if (p->st.id == IT_MOLOTOV || p->st.id == IT_BOTTLE) { shatter(p, p->st.id == IT_MOLOTOV); break; }
                p->vel = v2_scale(p->vel, -0.2f);
                p->flying = false;
                break;
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

int container_at(V2 pos, float reach, int *out_dist) {
    int best = -1;
    float bd = reach * reach;
    int tx = tile_of(pos.x), ty = tile_of(pos.y);
    for (int y = ty - 2; y <= ty + 2; y++)
        for (int x = tx - 2; x <= tx + 2; x++) {
            if (!in_map(x, y)) continue;
            int ci = cell(x, y)->cont;
            if (ci < 0) continue;
            Container *c = &W.conts[ci];
            /* closest point of the tile */
            float cx = CLAMP(pos.x, x * TILE, x * TILE + TILE), cy = CLAMP(pos.y, y * TILE, y * TILE + TILE);
            float d = (cx - pos.x) * (cx - pos.x) + (cy - pos.y) * (cy - pos.y);
            if (c->prop >= 0) d = MINF(d, v2_dist2(c->pos, pos));
            if (d < bd) { bd = d; best = ci; }
        }
    /* multi-tile props (cars, desks) keyed by their anchor container */
    for (int i = 0; i < W.nconts; i++) {
        Container *c = &W.conts[i];
        if (c->prop < 0 || c->dead) continue;
        float d = v2_dist2(c->pos, pos) - 14 * 14;
        if (d < bd) { bd = d; best = i; }
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
    for (int k = 0; k < c->n; k++) {
        Stack s = c->items[k];
        bool stored = false;
        if (a->cart >= 0 && ITEMS[s.id].cat != CAT_WEAPON && ITEMS[s.id].cat != CAT_BAG) {
            Cart *ct = &W.carts[a->cart];
            int used = 0;
            for (int m = 0; m < ct->n; m++) used += ITEMS[ct->items[m].id].size * ct->items[m].count;
            bool fits = used + ITEMS[s.id].size * s.count <= CART_CAP && ct->n < 16;
            if (!fits && item_needed((ItemId)s.id) && !inv_can_fit(a, (ItemId)s.id, s.count))
                fits = cart_make_room(ct, (ItemId)s.id, s.count);
            if (fits) {
                ct->items[ct->n++] = s;
                stored = true;
            }
        }
        if (!stored && ITEMS[s.id].cat != CAT_WEAPON && ITEMS[s.id].cat != CAT_BAG) {
            stored = inv_add(a, s);
            if (!stored && item_needed((ItemId)s.id) && make_room(a, (ItemId)s.id, s.count)) stored = inv_add(a, s);
        }
        if (!stored) pickup_spawn(s, v2_add(c->pos, v2(frange(-6, 6), frange(-6, 6))), v2_scale(v2_sub(a->pos, c->pos), 2.5f));
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
    bool all = true;
    for (int i = 0; i < W.nlist; i++) {
        ListEntry *e = &W.list[i];
        int have = total_have(e->id);
        bool done = have >= e->need;
        if (done && !e->done) {
            e->flash = 1.0f;
            audio_play(SFX_LIST_TICK, 0.9f, 0, 1);
        }
        e->have = have;
        e->done = done;
        if (!done) all = false;
    }
    for (int i = 0; i < W.nbonus; i++) {
        ListEntry *e = &W.bonus[i];
        e->have = total_have(e->id);
        bool d = e->have >= e->need;
        if (d && !e->done) e->flash = 1.0f;
        e->done = d;
    }
    if (all && !W.list_done && W.nlist > 0) {
        W.list_done = true;
        W.list_done_t = 0;
        world_message("LIST COMPLETE - BACK TO THE VAN!", COL_GREEN);
        audio_play(SFX_LIST_DONE, 1, 0, 1);
    } else if (!all && W.list_done) {
        W.list_done = false;
    }
}

/* -------------------------------------------------------------- scoring */
void add_score(int pts, V2 pos, const char *why) {
    W.score += pts;
    char buf[40];
    if (why && *why) SDL_snprintf(buf, sizeof buf, "+%d %s", pts, why);
    else SDL_snprintf(buf, sizeof buf, "+%d", pts);
    floater(v2(pos.x, pos.y - 12), buf, gfx_rainbow(W.time * 0.5f), false);
}

void register_kill(int vi, int attacker, int weapon, int flags) {
    Actor *v = &W.actors[vi];
    if (v->arch == AR_BOSS) {
        world_message("THE KING IS DEAD", COL_PINK);
        slowmo(2.0f);
        add_shake(16);
        audio_music(MUS_LEVEL_C);
    }
    bool by_player = attacker == 0;
    if (!by_player) return;
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
        floater(v2(v->pos.x, v->pos.y - 24), buf, COL_PINK, true);
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
            } else if (fabsf(torque) > 0.5f && chance(0.03f)) {
                play_at(SFX_DOOR_CREAK, cp, 0.6f, frange(0.9f, 1.2f));
            }
            a->pos = v2_add(cp, v2_scale(n, r));
        }
        d->ang += d->av * dt;
        d->av *= expf(-3.0f * dt);
        const float lim = 1.75f;
        if (d->ang > lim) { d->ang = lim; d->av = -fabsf(d->av) * 0.3f; }
        if (d->ang < -lim) { d->ang = -lim; d->av = fabsf(d->av) * 0.3f; }
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
            int steps = (int)(v2_len(c->vel) * dt / 5.0f) + 1;
            for (int k = 0; k < steps; k++) {
                c->pos = v2_add(c->pos, v2_scale(c->vel, dt / steps));
                collide_circle(&c->pos, 6, &c->vel);
            }
            c->vel = vb0;
            c->angle += c->av * dt;
            c->av *= expf(-2 * dt);
            V2 before = c->pos;
            V2 vb = c->vel;
            collide_circle(&c->pos, 6, &c->vel);
            if (v2_dist2(before, c->pos) > 0.01f && v2_len(vb) > 120 && c->crash_cd <= 0) {
                play_at(SFX_CART_HIT, c->pos, 0.8f, frange(0.9f, 1.1f));
                c->crash_cd = 0.25f;
                c->vel = v2_scale(v2_sub(c->vel, v2_scale(vb, 0.3f)), 0.5f);
                c->av += frange(-3, 3);
            }
            c->vel = v2_scale(c->vel, expf(-1.4f * dt));
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
                    c->pos = v2_add(a->pos, v2_scale(d, r / l));
                    c->vel = v2_add(c->vel, v2_scale(d, 60 / l * (dt * 60.0f)));
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
    case AMB_NIGHT: W.amb_out = rgb(88, 98, 152); W.amb_in = rgb(84, 82, 126); break;
    case AMB_FOG: W.amb_out = rgb(202, 206, 210); W.amb_in = rgb(152, 152, 168); break;
    case AMB_INFERNO: W.amb_out = rgb(122, 86, 114); W.amb_in = rgb(114, 86, 110); break;
    }
}

void world_start_level(int level) {
    world_free();
    memset(&W, 0, sizeof W);
    ndq = 0;
    combat_reset_level_state();
    actor_reset_level_state();
    hud_reset_level_state();
    prompt_buf[0] = 0;
    for (int i = 0; i < MAX_DOORS; i++) W.doors[i].len = 15;
    W.timescale = 1;
    W.boss = -1;
    gen_level(level, RUN.seed);
    set_ambience();
    /* restore the player from the run */
    Actor *p = player();
    p->maxhp = RUN.maxhp;
    p->hp = RUN.hp;
    p->weapon = RUN.weapon;
    memcpy(p->inv, RUN.inv, sizeof(Stack) * RUN.ninv);
    p->ninv = RUN.ninv;
    p->face = -PI_F * 0.5f;
    p->spawn_grace = 0;
    if (RUN.perks[PK_GUNSLINGER]) {
        Stack g = {IT_PISTOL, 1, 12};
        if (p->weapon.id == IT_PISTOL) p->weapon.cond = 12;
        else if (inv_count(p, IT_PISTOL) > 0) { for (int k = 0; k < p->ninv; k++) if (p->inv[k].id == IT_PISTOL) p->inv[k].cond = 12; }
        else if (!p->weapon.id) p->weapon = g;
        else if (!inv_add(p, g)) pickup_spawn(g, p->pos, v2(0, 0));
        Stack am = {IT_AMMO9, 12, 0};
        inv_add(p, am);
    }
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

/* ---------------------------------------------------------- interaction */

const char *world_prompt(V2 *pos) {
    if (pos) *pos = prompt_pos;
    return prompt_buf[0] ? prompt_buf : NULL;
}

static void update_prompt(void) {
    Actor *p = player();
    prompt_buf[0] = 0;
    if (!p->alive || W.exiting) return;
    prompt_pos = v2(p->pos.x, p->pos.y + 14);
    if (p->exec_t > 0) return;
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (a->used && a->alive && a->down_t > 0 && v2_dist(a->pos, p->pos) < 18) {
            SDL_strlcpy(prompt_buf, "^y[SPACE]^0 Execute", sizeof prompt_buf);
            return;
        }
    }
    if (p->pos.x > W.exit_rect.x && p->pos.x < W.exit_rect.x + W.exit_rect.w && p->pos.y > W.exit_rect.y &&
        p->pos.y < W.exit_rect.y + W.exit_rect.h) {
        if (W.list_done) SDL_strlcpy(prompt_buf, "^y[E]^0 Drive home", sizeof prompt_buf);
        else if (W.time > 12) SDL_strlcpy(prompt_buf, "^kYour van. Come back when the list is done.", sizeof prompt_buf);
        return;
    }
    if (p->cart >= 0) {
        int ci = container_at(v2_add(p->pos, v2_scale(v2_angle(p->face), 14)), 30, NULL);
        if (ci >= 0 && !(W.conts[ci].searched && W.conts[ci].n == 0) && g_search_cont < 0) {
            SDL_snprintf(prompt_buf, sizeof prompt_buf, "^y[E]^0 Search %s (into cart)", W.conts[ci].name ? W.conts[ci].name : "");
            prompt_pos = v2(W.conts[ci].pos.x, W.conts[ci].pos.y + 12);
        } else if (g_search_cont < 0)
            SDL_strlcpy(prompt_buf, "^y[LMB]^0 Shove cart  ^y[E]^0 Let go", sizeof prompt_buf);
        return;
    }
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &W.pickups[i];
        if (!pk->alive || pk->flying || ITEMS[pk->st.id].cat != CAT_WEAPON) continue;
        if (v2_dist(pk->pos, p->pos) < 18) {
            SDL_snprintf(prompt_buf, sizeof prompt_buf, "^y[E]^0 Take %s", ITEMS[pk->st.id].name);
            return;
        }
    }
    if (g_search_cont >= 0) return;
    int ci = container_at(v2_add(p->pos, v2_scale(v2_angle(p->face), 6)), 30, NULL);
    if (ci >= 0) {
        Container *c = &W.conts[ci];
        if (!(c->searched && c->n == 0)) {
            SDL_snprintf(prompt_buf, sizeof prompt_buf, "^y[E]^0 Search %s", c->name ? c->name : "");
            prompt_pos = v2(c->pos.x, c->pos.y + 12);
        }
        return;
    }
    int cn = cart_near(p->pos, 22);
    if (cn >= 0) SDL_strlcpy(prompt_buf, "^y[E]^0 Push cart", sizeof prompt_buf);
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
    float sway = SET.sway ? sinf(G.time * 0.35f) * 0.018f + (p->alive ? p->vel.x * 0.00012f : 0) : 0;
    G.cam_angle = lerpf(G.cam_angle, sway + sh * 0.0025f * sinf(t * 23), smooth_k(6, dt));
    float zt = p->exec_t > 0 ? 1.08f : 1.0f;
    if (W.player_dead) zt = 1.15f;
    G.cam_zoom = lerpf(G.cam_zoom, zt, smooth_k(4, dt));
}

/* ------------------------------------------------------------- update */
void world_update(float rdt) {
    W.nlights = 0;
    /* time control */
    if (W.hitstop > 0) {
        W.hitstop -= rdt;
        camera_update(rdt);
        return;
    }
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
        ai_update(a, i, dt);
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
            float l = sqrtf(l2);
            V2 n = v2_scale(d, 1.0f / l);
            float pen = (r - l) * 0.5f;
            a->pos = v2_sub(a->pos, v2_scale(n, pen));
            b->pos = v2_add(b->pos, v2_scale(n, pen));
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
    for (int i = 0; i < W.nlist; i++) if (W.list[i].flash > 0) W.list[i].flash -= rdt;
    for (int i = 0; i < W.nbonus; i++) if (W.bonus[i].flash > 0) W.bonus[i].flash -= rdt;

    static float recount_t;
    static int last_craftable = -1;
    recount_t -= rdt;
    if (recount_t <= 0) {
        recount_t = 0.25f;
        list_recount();
        /* tell the player when their junk turns into a recipe */
        int craftable = 0;
        for (int r = 0; r < NUM_RECIPES; r++)
            if (RECIPES[r].flag == RF_NORMAL && can_craft(p, &RECIPES[r])) craftable |= 1 << r;
        if (last_craftable >= 0 && (craftable & ~last_craftable) && p->alive) {
            for (int r = 0; r < NUM_RECIPES; r++)
                if ((craftable & ~last_craftable) & (1 << r)) {
                    char buf[80];
                    SDL_snprintf(buf, sizeof buf, "You can craft: %s  [TAB]", ITEMS[RECIPES[r].out].name);
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

    /* weather */
    if (W.def->amb == AMB_RAIN) {
        for (int k = 0; k < 6; k++) {
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
        audio_loop(LOOP_HUM, in ? 0.55f : 0.0f, 1);
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
            add_light(v2(W.props[i].x + a->w / 2, W.props[i].y + a->h / 2), 80, COL_PINK, 0.55f * flick);
        }
    /* player flashlight */
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
    bool see = RUN.perks[PK_EYE] || d < 110;
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
            gfx_glow(W.props[i].x + a->w / 2, W.props[i].y + a->h / 2, 50, COL_PINK, flick * 0.5f);
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
    /* exit zone marker */
    if (W.list_done) {
        float k = 0.5f + 0.5f * sinf(W.time * 6);
        SDL_FRect r = W.exit_rect;
        Color c = rgba(162, 211, 76, (Uint8)(90 + 120 * k));
        gfx_rect(r.x, r.y, r.w, r.h, c);
        gfx_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, rgba(162, 211, 76, (Uint8)(60 * k)));
        gfx_glow(r.x + r.w / 2, r.y + r.h / 2, 40, COL_GREEN, 0.3f * k);
    }
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
        Color c = f->c;
        c.a = (Uint8)(255 * CLAMP(f->t * 3, 0.0f, 1.0f));
        float y = f->pos.y - k * 14;
        gfx_text(f->big ? FONT_BIG : FONT_SMALL, f->text, f->pos.x, y, c, TXT_CENTER | TXT_OUTLINE | (f->big ? TXT_WAVE : 0));
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
