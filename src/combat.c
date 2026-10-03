/* LAST AISLE - combat: melee, guns, throwing, damage, gore, explosions, fire, executions */
#include "world.h"
#include "gfx.h"
#include "audio.h"
#include "game.h"

/* ----------------------------------------------------------------- gore */
void spray_blood(V2 pos, V2 dir, int amount, float force) {
    float base = v2_len2(dir) > 0 ? v2_to_angle(dir) : frange(-PI_F, PI_F);
    for (int i = 0; i < amount; i++) {
        float a = base + frange(-0.7f, 0.7f) * (v2_len2(dir) > 0 ? 1.0f : 4.0f);
        float sp = frange(0.25f, 1.0f) * force;
        Particle *p = particle_add(PT_BLOOD, v2(pos.x + frange(-2, 2), pos.y + frange(-2, 2)), v2_scale(v2_angle(a), sp),
                                   frange(0.25f, 0.7f));
        if (!p) return;
        p->z = frange(4, 10);
        p->vz = frange(10, 70);
        p->spr = SPR_FX_BLOOD_DRIP + irange(0, SPR_FX_BLOOD_DRIP_N - 1);
        p->bake = true;
        p->scale = frange(0.8f, 1.4f);
    }
}

static void gibs(V2 pos, V2 dir, int n, bool head, int arch) {
    for (int i = 0; i < n; i++) {
        V2 v = v2_add(v2_scale(dir, frange(60, 200)), v2(frange(-80, 80), frange(-80, 80)));
        Particle *p = particle_add(PT_GIB, pos, v, frange(0.5f, 0.9f));
        if (!p) return;
        p->spr = SPR_FX_GIB + irange(0, SPR_FX_GIB_N - 1);
        p->z = 6;
        p->vz = frange(40, 110);
        p->spin = frange(-14, 14);
        p->bake = true;
    }
    if (head) {
        V2 v = v2_add(v2_scale(dir, frange(110, 200)), v2(frange(-40, 40), frange(-40, 40)));
        Particle *p = particle_add(PT_HEAD, pos, v, 1.0f);
        if (p) {
            int hs = arch == AR_FERAL || arch == AR_SCAV ? 0 : (arch == AR_LOOTER ? 2 : 1);
            p->spr = SPR_FX_HEAD + hs;
            p->z = 10;
            p->vz = 90;
            p->spin = frange(-10, 10);
            p->bake = true;
        }
    }
}

static void blood_pool(V2 pos, V2 dir, bool big) {
    float a = v2_len2(dir) > 0 ? v2_to_angle(dir) : frange(-PI_F, PI_F);
    if (big) decal(SPR_FX_BLOOD_BIG + irange(0, SPR_FX_BLOOD_BIG_N - 1), v2_add(pos, v2_scale(v2_angle(a), 6)), a, 1, TINT_NONE);
    decal(SPR_FX_BLOOD + irange(0, SPR_FX_BLOOD_N - 1), pos, frange(-PI_F, PI_F), frange(0.9f, 1.3f), TINT_NONE);
    if (chance(0.6f)) decal(SPR_FX_BLOOD_SMEAR + irange(0, 1), v2_add(pos, v2_scale(v2_angle(a), 10)), a, 1, TINT_NONE);
}

/* ---------------------------------------------------------------- damage */
static int corpse_frame(int flags, int weapon) {
    if (flags & (DMG_EXPLOSION | DMG_GORE)) return 2;
    if (weapon >= 0 && WEAPONS[weapon].gore && chance(0.6f)) return 2;
    return irange(0, 1);
}

void kill_actor(int vi, int attacker, V2 dir, int weapon, int flags) {
    Actor *v = &W.actors[vi];
    if (!v->alive) return;
    v->alive = false;
    v->hp = 0;
    const ArchDef *d = &ARCH[v->arch];
    bool gore = (flags & (DMG_EXPLOSION | DMG_GORE)) || (weapon >= 0 && WEAPONS[weapon].gore);
    int fr = corpse_frame(flags, weapon);
    if (v->arch == AR_BOSS) fr = 0;
    float ang = v2_len2(dir) > 0 ? v2_to_angle(dir) : v->face + PI_F;
    V2 cpos = v2_add(v->pos, v2_scale(v2_norm(dir), 4));
    float slide = (flags & DMG_EXPLOSION) ? 260 : (gore ? 170 : 120);
    if (v->arch == AR_BOSS || v->arch == AR_BRUTE) slide *= 0.4f;
    corpse_add(cpos, ang, d->spr_dead + (v->arch == AR_BOSS ? 0 : fr), fr == 2, v2_scale(v2_norm(dir), slide));
    blood_pool(v->pos, dir, true);
    spray_blood(v->pos, dir, gore ? 40 : 18, gore ? 260 : 170);
    if (gore) {
        gibs(v->pos, v2_norm(dir), irange(3, 6), fr == 2, v->arch);
        audio_play(SFX_GORE, 0.9f, 0, frange(0.85f, 1.1f));
    }
    play_at(SFX_DEATH, v->pos, 0.75f, (v->arch == AR_BRUTE || v->arch == AR_BOSS) ? 0.7f : frange(0.9f, 1.15f));
    play_at(SFX_BODYFALL, v->pos, 0.6f, 1);
    /* drop everything */
    if (v->weapon.id) {
        pickup_spawn(v->weapon, v->pos, v2(frange(-60, 60), frange(-60, 60)));
        v->weapon.id = IT_NONE;
    }
    for (int i = 0; i < v->ninv; i++)
        pickup_spawn(v->inv[i], v->pos, v2(frange(-70, 70), frange(-70, 70)));
    v->ninv = 0;
    if (v->cart >= 0) { W.carts[v->cart].holder = -1; v->cart = -1; }
    if (vi == 0) {
        if (RUN.mode == MODE_ROGUE) run_save_delete();   /* no save-scumming a death */
        SDL_Log("PLAYER KILLED on level %d at %.0fs by %s", W.level + 1, W.time,
                attacker > 0 ? ARCH[W.actors[attacker].arch].name : "the world");
        W.player_dead = true;
        W.dead_t = 0;
        slowmo(1.2f);
        add_shake(10);
        G.flash = 0.6f;
        G.flash_col = rgba(160, 0, 20, 255);
    } else {
        register_kill(vi, attacker, weapon, flags);
    }
    hitstop(gore ? 0.09f : 0.06f);
    add_shake(gore ? 6 : 4);
    make_noise(v->pos, 140, attacker);
}

void damage_actor(int vi, int attacker, float dmg, V2 dir, float kb, float kd, int weapon, int flags) {
    Actor *v = &W.actors[vi];
    if (!v->used || !v->alive) return;
    if (vi == 0 && W.exiting) return;
    if (v->invuln > 0 && !(flags & (DMG_EXEC | DMG_EXPLOSION))) return;
    if (vi == 0) {
        if (v->exec_t > 0 && !(flags & (DMG_BULLET | DMG_EXPLOSION))) dmg *= 0.5f;
        v->invuln = 0.45f;
        W.took_damage = true;
        add_shake(7);
        G.chroma = 1.0f;
        G.flash = 0.35f;
        G.flash_col = rgba(200, 20, 40, 255);
        audio_play(SFX_HURT_PLAYER, 0.9f, 0, frange(0.95f, 1.05f));
        hitstop(0.05f);
    } else {
        play_at(SFX_HURT_NPC, v->pos, 0.85f, (v->arch == AR_BRUTE || v->arch == AR_BOSS) ? 0.65f : frange(0.9f, 1.3f));
    }
    if (v->down_t > 0 && (flags & DMG_MELEE)) dmg *= 2;
    int idmg = (int)ceilf(dmg);
    if (idmg < 1) idmg = 1;
    /* last stand: a single hit can't take the player from full health to dead */
    if (vi == 0 && v->hp == v->maxhp && idmg >= v->hp) {
        idmg = v->hp - 1;
        v->invuln = 0.9f;
        slowmo(0.5f);
    }
    v->hp -= idmg;
    v->hurt_t = 0.15f;
    v->last_hit_by = attacker;
    V2 n = v2_norm(dir);
    float kbm = (v->arch == AR_BRUTE || v->arch == AR_BOSS) ? 0.35f : 1.0f;
    v->push = v2_add(v->push, v2_scale(n, kb * kbm));
    spray_blood(v->pos, n, 6 + idmg * 4, 140 + idmg * 20);
    if (idmg >= 2 || chance(0.5f)) decal(SPR_FX_BLOOD + irange(0, SPR_FX_BLOOD_N - 1), v2_add(v->pos, v2_scale(n, 8)), frange(-PI_F, PI_F), 1, TINT_NONE);
    if (vi != 0) ai_on_hurt(v, vi, attacker);
    if (v->hp <= 0) {
        kill_actor(vi, attacker, n, weapon, flags);
        return;
    }
    /* knockdown */
    bool heavy = ARCH[v->arch].heavy;
    float kdc = kd;
    if (heavy) kdc = (weapon == W_SLEDGE || (flags & (DMG_EXPLOSION | DMG_CART | DMG_THROWN))) ? kd * 0.6f : 0;
    if (v->arch == AR_BOSS) kdc = (flags & DMG_EXPLOSION) ? 0.5f : 0;
    if (vi == 0) kdc = (flags & (DMG_EXPLOSION | DMG_DOOR)) || weapon == W_SLEDGE ? 0.8f : 0;
    if (kdc > 0 && chance(kdc)) {
        v->down_t = vi == 0 ? 0.7f : frange(2.2f, 3.2f) * (heavy ? 0.5f : 1.0f);
        v->atk_t = -1;
        v->windup = 0;
        v->hit_pending = false;
        v->exec_t = 0;
        v->exec_target = -1;
        if (vi != 0 && v->weapon.id) {
            pickup_spawn(v->weapon, v->pos, v2_add(v2_scale(n, 60), v2(frange(-40, 40), frange(-40, 40))));
            v->weapon.id = IT_NONE;
        }
        if (v->cart >= 0) { W.carts[v->cart].holder = -1; v->cart = -1; }
        play_at(SFX_BODYFALL, v->pos, 0.6f, 1);
    }
    if (weapon == W_PAN && v->down_t <= 0 && !heavy) v->stun_t = 1.0f;
}

/* ----------------------------------------------------------------- melee */
static void melee_hit(Actor *a, int idx) {
    const WeaponDef *w = item_weapon(a->weapon.id);
    int widx = (int)(w - WEAPONS);
    float range = w->range + 5;
    float arc = (w->arc > 0 ? w->arc : 60) * 0.5f * DEG2RAD;
    float dmg = w->damage;
    float kd = w->knockdown;
    if (idx == 0 && !a->weapon.id && RUN.perks[PK_BRAWLER]) { dmg *= 3; kd = 1.0f; }
    if (a->arch == AR_BRUTE) dmg += 1;
    if (a->arch == AR_BOSS) dmg += 2;
    bool hit_any = false;
    for (int j = 0; j < W.nactors; j++) {
        if (j == idx) continue;
        Actor *t = &W.actors[j];
        if (!t->used || !t->alive) continue;
        if (idx != 0 && j != a->br.target && !actor_hostile(idx, j)) continue;
        V2 d = v2_sub(t->pos, a->pos);
        float dist = v2_len(d);
        if (dist > range + t->radius) continue;
        float ad = fabsf(angle_diff(a->face, v2_to_angle(d)));
        if (dist > t->radius + 4 && ad > arc) continue;
        if (!los_clear(a->pos, t->pos, true)) continue;
        int flags = DMG_MELEE;
        float dd = dmg;
        /* sneak attack: silent takedown on an unaware victim */
        if (idx == 0 && j != 0 && w->kind == WK_MELEE && !t->br.aware && t->br.state != AI_CHASE && t->down_t <= 0 &&
            t->arch != AR_BOSS && t->arch != AR_BRUTE) {
            dd = 99;
            flags |= DMG_SNEAK;
        }
        if (w->gore && dd >= t->hp) flags |= (chance(0.5f) ? DMG_GORE : 0);
        damage_actor(j, idx, dd, d, w->knockback, kd, widx, flags);
        hit_any = true;
        if (idx == 0) {
            G.chroma = MAXF(G.chroma, 0.35f);
        }
    }
    /* swinging into glass */
    V2 tip = v2_add(a->pos, v2_scale(v2_angle(a->face), range));
    int tx = tile_of(tip.x), ty = tile_of(tip.y);
    if (in_map(tx, ty) && (W.cells[ty][tx].obj == OB_GLASS_H || W.cells[ty][tx].obj == OB_GLASS_V) && a->weapon.id) {
        break_glass(tx, ty, a->pos);
        hit_any = true;
    }
    if (hit_any) {
        int sfx = SFX_HIT_BLUNT;
        if (!a->weapon.id) sfx = SFX_PUNCH;
        else if (w->gore && w->style != ST_HEAVY) sfx = SFX_HIT_BLADE;
        else if (a->weapon.id == IT_PAN) sfx = SFX_STUN;
        else if (a->weapon.id == IT_KNIFE || a->weapon.id == IT_SPEAR) sfx = SFX_HIT_BLADE;
        play_at(sfx, a->pos, 0.95f, frange(0.9f, 1.1f));
        if (w->gore) play_at(SFX_GORE, a->pos, 0.5f, frange(0.9f, 1.2f));
        hitstop(w->style == ST_HEAVY ? 0.08f : 0.045f);
        add_shake(w->shake * (idx == 0 ? 1.0f : 0.5f));
        make_noise(a->pos, w->noise, idx);
        /* durability */
        if (a->weapon.id && w->durability > 0 && w->kind == WK_MELEE) {
            bool wear = idx != 0 || !(RUN.perks[PK_IRONGRIP] && chance(0.5f));
            if (wear) a->weapon.cond--;
            if (a->weapon.cond <= 0) {
                char buf[48];
                SDL_snprintf(buf, sizeof buf, "%s BROKE!", w->name);
                if (idx == 0) floater(v2(a->pos.x, a->pos.y - 16), buf, COL_ORANGE, false);
                play_at(SFX_HIT_METAL, a->pos, 0.8f, 0.7f);
                for (int k = 0; k < 6; k++) {
                    Particle *p = particle_add(PT_DEBRIS, a->pos, v2(frange(-80, 80), frange(-80, 80)), 0.5f);
                    if (p) { p->spr = SPR_FX_GIB + 1; p->col = rgb(176, 125, 79); p->bake = false; }
                }
                a->weapon.id = IT_NONE;
            }
        }
    }
}

static void muzzle(Actor *a, const WeaponDef *w, V2 at, float ang) {
    Particle *p = particle_add(PT_FLAME, at, v2(0, 0), 0.06f);
    if (p) { p->spr = SPR_FX_MUZZLE + irange(0, 2); p->angle = ang; p->scale = w->pellets > 1 ? 1.3f : 1.0f; }
    add_light(at, 70, COL_YELLOW, 1.0f);
    /* shell casing */
    if (a->weapon.id != IT_NAILGUN) {
        V2 side = v2_angle(ang + PI_F * 0.5f);
        Particle *s = particle_add(PT_SHELL, a->pos, v2_add(v2_scale(side, frange(50, 90)), v2_scale(v2_angle(ang), -20)), 1.2f);
        if (s) {
            s->spr = SPR_FX_SHELL + (w->ammo == IT_SHELLS ? 1 : 0);
            s->z = 8;
            s->vz = 50;
            s->spin = frange(-20, 20);
            s->bake = true;
        }
    }
    for (int i = 0; i < 3; i++) {
        Particle *sm = particle_add(PT_SMOKE, at, v2_add(v2_scale(v2_angle(ang), frange(10, 40)), v2(frange(-8, 8), frange(-8, 8))), frange(0.3f, 0.6f));
        if (sm) { sm->spr = SPR_FX_SMOKE; sm->frames = 4; sm->scale = 0.5f; sm->col = rgba(200, 200, 200, 120); }
    }
}

void fire_bullet(int owner, V2 pos, float ang, const WeaponDef *w) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &W.bullets[i];
        if (b->alive) continue;
        memset(b, 0, sizeof *b);
        b->alive = true;
        b->pos = b->prev = pos;
        float sp = w->speed * frange(0.92f, 1.06f);
        b->vel = v2_scale(v2_angle(ang), sp);
        b->dmg = w->damage;
        b->kb = w->knockback;
        b->kd = w->knockdown;
        b->owner = owner;
        b->pierce = w->pierce;
        b->life = w->range / sp * frange(0.9f, 1.1f);
        b->nail = w->ammo == IT_NAILS;
        b->flame = w->kind == WK_FLAME;
        b->weapon = (int)(w - WEAPONS);
        return;
    }
}

void attack_begin(Actor *a, int idx) {
    const WeaponDef *w = item_weapon(a->weapon.id);
    int widx = (int)(w - WEAPONS);
    float cd = w->cooldown;
    if (idx == 0 && RUN.perks[PK_QUICKHANDS] && (w->kind == WK_MELEE || w->kind == WK_FIST)) cd *= 0.75f;
    if (idx != 0) cd *= 1.25f;
    a->atk_cd = cd;
    if (idx == 0) W.weapons_used |= 1ull << widx;
    V2 fwd = v2_angle(a->face);
    switch (w->kind) {
    case WK_FIST:
    case WK_MELEE:
        a->atk_t = 0;
        a->atk_dir++;
        a->hit_pending = true;
        a->hit_at = w->style == ST_HEAVY ? 0.09f : 0.045f;
        play_at(w->style == ST_HEAVY ? SFX_SWING_HEAVY : SFX_SWING, a->pos, 0.8f, frange(0.9f, 1.15f));
        break;
    case WK_GUN: {
        if (a->weapon.cond <= 0) { play_at(SFX_EMPTY, a->pos, 0.6f, 1); return; }
        a->weapon.cond--;
        a->atk_t = 0;
        V2 muzzle_pos = v2_add(a->pos, v2_rot(v2(14, 1), a->face));
        if (!los_clear(a->pos, muzzle_pos, true)) muzzle_pos = a->pos;
        float npc_aim = ARCH[a->arch].aim * MAXF(0.8f, 1.35f - 0.08f * W.level + (RUN.mode == MODE_STORY ? 0.15f : 0.0f));
        float spread = w->spread * (idx == 0 ? (RUN.perks[PK_STEADYAIM] ? 0.55f : 1.0f) : npc_aim);
        for (int p = 0; p < MAXF(1, w->pellets); p++) {
            float off = w->pellets > 1 ? frange(-0.5f, 0.5f) * spread : frange(-0.5f, 0.5f) * spread;
            fire_bullet(idx, muzzle_pos, a->face + off * DEG2RAD, w);
        }
        muzzle(a, w, muzzle_pos, a->face);
        int sfx = SFX_PISTOL;
        if (a->weapon.id == IT_REVOLVER) sfx = SFX_REVOLVER;
        else if (w->ammo == IT_SHELLS) sfx = SFX_SHOTGUN;
        else if (a->weapon.id == IT_RIFLE) sfx = SFX_RIFLE;
        else if (a->weapon.id == IT_NAILGUN) sfx = SFX_NAILGUN;
        play_at(sfx, a->pos, 1.0f, frange(0.95f, 1.05f));
        make_noise(a->pos, w->noise, idx);
        if (idx == 0) {
            add_shake(w->shake);
            W.cam_kick_x -= fwd.x * w->shake * 1.2f;
            W.cam_kick_y -= fwd.y * w->shake * 1.2f;
        }
        a->push = v2_sub(a->push, v2_scale(fwd, w->pellets > 1 ? 70 : 20));
        break;
    }
    case WK_CHAINSAW: {
        if (a->weapon.cond <= 0) {
            if (idx == 0) world_hint("Out of fuel. Craft: chainsaw + gas can.");
            a->atk_cd = 0.5f;
            return;
        }
        a->atk_t = 0;
        if (((int)(W.time * 30) & 1) == 0) a->weapon.cond--;
        for (int j = 0; j < W.nactors; j++) {
            if (j == idx) continue;
            Actor *t = &W.actors[j];
            if (!t->used || !t->alive) continue;
            if (idx != 0 && !actor_hostile(idx, j)) continue;
            V2 d = v2_sub(t->pos, a->pos);
            if (v2_len(d) > w->range + t->radius) continue;
            if (fabsf(angle_diff(a->face, v2_to_angle(d))) > 0.7f) continue;
            t->push = v2_scale(t->push, 0.3f);
            damage_actor(j, idx, w->damage, d, 20, 0, widx, DMG_MELEE | DMG_GORE);
            spray_blood(t->pos, v2_scale(d, -1), 8, 200);
            if (chance(0.3f)) gibs(t->pos, v2_norm(d), 1, false, t->arch);
            add_shake(1.5f);
        }
        break;
    }
    case WK_FLAME: {
        if (a->weapon.cond <= 0) {
            if (idx == 0) world_hint("The can is empty.");
            a->atk_cd = 0.4f;
            return;
        }
        if (((int)(W.time * 20) % 3) == 0) a->weapon.cond--;
        a->atk_t = 0;
        V2 at = v2_add(a->pos, v2_rot(v2(12, 1), a->face));
        for (int k = 0; k < 2; k++) {
            float ang = a->face + frange(-0.2f, 0.2f);
            fire_bullet(idx, at, ang, w);
            Particle *p = particle_add(PT_FLAME, at, v2_add(v2_scale(v2_angle(ang), frange(110, 180)), v2_scale(a->vel, 0.5f)), frange(0.2f, 0.32f));
            if (p) { p->spr = SPR_FX_FIRE; p->frames = 4; p->scale = 0.5f; p->spin = 0; p->angle = 0; }
        }
        add_light(v2_add(at, v2_scale(fwd, 20)), 60, COL_ORANGE, 0.9f);
        if (chance(0.25f)) play_at(SFX_FLAME_PUFF, a->pos, 0.8f, frange(0.9f, 1.1f));
        make_noise(a->pos, w->noise, idx);
        break;
    }
    case WK_THROWN: {
        Stack one = a->weapon;
        one.count = 1;
        a->weapon.count--;
        if (a->weapon.count <= 0) a->weapon.id = IT_NONE;
        throw_item(a, idx, one, a->face, 380);
        break;
    }
    }
}

void attack_update(Actor *a, int idx, float dt) {
    if (a->atk_t >= 0) {
        a->atk_t += dt;
        if (a->hit_pending && a->atk_t >= a->hit_at) {
            a->hit_pending = false;
            melee_hit(a, idx);
        }
        if (a->atk_t > 0.6f) a->atk_t = -1;
    }
}

/* --------------------------------------------------------------- thrown */
void throw_item(Actor *a, int idx, Stack st, float ang, float speed) {
    int pi = pickup_spawn(st, v2_add(a->pos, v2_scale(v2_angle(ang), 6)), v2_add(v2_scale(v2_angle(ang), speed), v2_scale(a->vel, 0.3f)));
    if (pi < 0) return;
    Pickup *p = &W.pickups[pi];
    p->flying = true;
    p->thrower = idx;
    p->spin = (chance(0.5f) ? 1 : -1) * frange(14, 20);
    p->z = 6;
    if (st.id == IT_PIPEBOMB) { p->fuse = 1.5f; p->spin *= 0.5f; }
    play_at(SFX_THROW, a->pos, 0.7f, frange(0.9f, 1.1f));
}

/* --------------------------------------------------------------- bullets */
static int barrel_queue_n;
static struct { V2 pos; float t; int owner; } barrel_queue[32];

void combat_reset_level_state(void) { barrel_queue_n = 0; }

static void barrel_hit(int tx, int ty, int owner) {
    Cell *c = cell(tx, ty);
    if (c->obj != OB_BARREL) return;
    c->obj = OB_NONE;
    c->flags &= ~(CF_SOLID | CF_SHOT);
    if (barrel_queue_n < 32) {
        barrel_queue[barrel_queue_n].pos = tile_center(tx, ty);
        barrel_queue[barrel_queue_n].t = 0.08f;
        barrel_queue[barrel_queue_n].owner = owner;
        barrel_queue_n++;
    }
}

static void impact(V2 pos, V2 vel, bool sparks) {
    Particle *p = particle_add(PT_IMPACT, pos, v2(0, 0), 0.2f);
    if (p) { p->spr = SPR_FX_IMPACT; p->frames = 3; }
    for (int i = 0; i < (sparks ? 4 : 2); i++) {
        Particle *s = particle_add(sparks ? PT_SPARK : PT_DUST, pos,
                                   v2_add(v2_scale(v2_norm(vel), -frange(30, 90)), v2(frange(-50, 50), frange(-50, 50))), frange(0.15f, 0.35f));
        if (s) { s->spr = sparks ? SPR_FX_SPARK : SPR_FX_DUST; s->frames = 3; }
    }
}

static bool seg_circle(V2 a, V2 b, V2 c, float r) {
    V2 ab = v2_sub(b, a);
    float t = v2_len2(ab) > 0 ? CLAMP(v2_dot(v2_sub(c, a), ab) / v2_len2(ab), 0.0f, 1.0f) : 0;
    V2 p = v2_add(a, v2_scale(ab, t));
    return v2_dist2(p, c) <= r * r;
}

void bullets_update(float dt) {
    for (int i = 0; i < barrel_queue_n; i++) {
        barrel_queue[i].t -= dt;
        if (barrel_queue[i].t <= 0) {
            V2 p = barrel_queue[i].pos;
            int o = barrel_queue[i].owner;
            barrel_queue[i] = barrel_queue[--barrel_queue_n];
            i--;
            explode(p, 52, o);
            fire_spawn(p, 6, o);
        }
    }
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &W.bullets[i];
        if (!b->alive) continue;
        b->life -= dt;
        if (b->life <= 0) { b->alive = false; continue; }
        b->prev = b->pos;
        V2 step = v2_scale(b->vel, dt);
        float len = v2_len(step);
        int sub = (int)(len / 5) + 1;
        V2 sp = v2_scale(step, 1.0f / sub);
        for (int s = 0; s < sub && b->alive; s++) {
            V2 np = v2_add(b->pos, sp);
            int tx = tile_of(np.x), ty = tile_of(np.y);
            if (!b->flame && cell_flag(tx, ty, CF_SHOT)) {
                if (in_map(tx, ty)) {
                    Cell *c = cell(tx, ty);
                    if (c->obj == OB_GLASS_H || c->obj == OB_GLASS_V) {
                        break_glass(tx, ty, b->pos);
                        b->vel = v2_scale(b->vel, 0.85f);
                        b->pos = np;
                        continue;
                    }
                    if (c->obj == OB_BARREL) barrel_hit(tx, ty, b->owner);
                    if (c->obj == OB_SHELF_H || c->obj == OB_SHELF_V) {
                        for (int k = 0; k < 2; k++) {
                            Particle *p = particle_add(PT_DEBRIS, np, v2(frange(-60, 60), frange(-60, 60)), 0.6f);
                            if (p) { p->spr = SPR_O_TRASH + irange(0, SPR_O_TRASH_N - 1); p->scale = 0.6f; p->z = 4; p->vz = 40; p->bake = true; }
                        }
                    }
                }
                impact(b->pos, b->vel, true);
                if (chance(0.15f)) play_at(SFX_RICOCHET, b->pos, 0.7f, frange(0.8f, 1.3f));
                b->alive = false;
                break;
            }
            if (b->flame && cell_flag(tx, ty, CF_SOLID) && !cell_flag(tx, ty, CF_DOOR)) { b->alive = false; break; }
            /* doors */
            for (int d = 0; d < W.ndoors && b->alive && !b->flame; d++) {
                Door *dr = &W.doors[d];
                V2 e = v2_add(dr->hinge, v2_scale(v2_angle(dr->base + dr->ang), dr->len));
                if (seg_circle(dr->hinge, e, np, 1.5f)) {
                    impact(np, b->vel, false);
                    dr->av += (chance(0.5f) ? 1 : -1) * 2.0f;
                    b->alive = false;
                }
            }
            if (!b->alive) break;
            /* actors */
            for (int j = 0; j < W.nactors && b->alive; j++) {
                Actor *t = &W.actors[j];
                if (!t->used || !t->alive || j == b->owner) continue;
                if (t->down_t > 0) continue;
                if (!seg_circle(b->pos, np, t->pos, t->radius + 1)) continue;
                if (b->flame) {
                    if (!(j == 0 && RUN.perks[PK_FIREBUG])) {
                        t->burn_t = MAXF(t->burn_t, 2.5f);
                        t->last_hit_by = b->owner;
                        if (j != 0) ai_on_hurt(t, j, b->owner);
                    }
                    b->alive = false;
                    break;
                }
                int flags = DMG_BULLET;
                if (WEAPONS[b->weapon].pellets > 1 && v2_dist(W.actors[b->owner].pos, t->pos) < 50) flags |= DMG_GORE;
                if (b->weapon == W_RIFLE) flags |= DMG_GORE;
                damage_actor(j, b->owner, b->dmg, b->vel, b->kb, b->kd, b->weapon, flags);
                if (b->pierce-- <= 0) b->alive = false;
            }
            if (b->alive) b->pos = np;
        }
        if (b->flame && b->alive && chance(0.1f)) {
            int tx = tile_of(b->pos.x), ty = tile_of(b->pos.y);
            if (in_map(tx, ty) && cell(tx, ty)->fuel && chance(0.3f)) fire_spawn(b->pos, 4, b->owner);
        }
    }
}

void bullets_draw(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &W.bullets[i];
        if (!b->alive || b->flame) continue;
        float ang = v2_to_angle(b->vel);
        V2 tail = v2_sub(b->pos, v2_scale(v2_norm(b->vel), 14));
        gfx_line(tail.x, tail.y, b->pos.x, b->pos.y, rgba(255, 230, 150, 110));
        gfx_spr_ex(b->nail ? SPR_FX_NAIL : SPR_FX_BULLET, b->pos.x, b->pos.y, ang, 1, 1, TINT_NONE);
    }
}

/* ------------------------------------------------------------ explosions */
void explode(V2 pos, float radius, int owner) {
    play_at(SFX_EXPLOSION, pos, 1.0f, frange(0.9f, 1.05f));
    make_noise(pos, 480, owner);
    add_shake(14);
    hitstop(0.07f);
    G.flash = MAXF(G.flash, 0.45f);
    G.flash_col = rgba(255, 230, 180, 255);
    G.chroma = 1.0f;
    add_light(pos, radius * 3, COL_ORANGE, 1.0f);
    Particle *ex = particle_add(PT_FIRE, pos, v2(0, 0), 0.5f);
    if (ex) { ex->spr = SPR_FX_EXPLOSION; ex->frames = SPR_FX_EXPLOSION_N; ex->scale = radius / 30.0f; ex->kind = PT_IMPACT; }
    for (int i = 0; i < 14; i++) {
        Particle *s = particle_add(PT_SMOKE, pos, v2(frange(-80, 80), frange(-80, 80)), frange(0.6f, 1.4f));
        if (s) { s->spr = SPR_FX_SMOKE; s->frames = 4; s->scale = frange(1.0f, 1.8f); s->col = rgba(70, 60, 70, 200); }
    }
    for (int i = 0; i < 18; i++) {
        Particle *e = particle_add(PT_EMBER, pos, v2(frange(-220, 220), frange(-220, 220)), frange(0.3f, 0.8f));
        if (e) { e->spr = SPR_FX_SPARK; e->frames = 3; e->z = 4; e->vz = frange(20, 80); }
    }
    decal(SPR_FX_SCORCH + irange(0, 1), pos, frange(-PI_F, PI_F), radius / 24.0f, TINT_NONE);
    for (int j = 0; j < W.nactors; j++) {
        Actor *t = &W.actors[j];
        if (!t->used || !t->alive) continue;
        float d = v2_dist(t->pos, pos);
        if (d > radius + t->radius) continue;
        if (!los_clear(pos, t->pos, true)) continue;
        float k = 1.0f - d / (radius + t->radius);
        float dmg = 2 + 9 * k;
        if (t->arch == AR_BOSS) dmg *= 1.5f;
        damage_actor(j, owner, dmg, v2_sub(t->pos, pos), 260 * k + 60, 1.0f, -1, DMG_EXPLOSION);
    }
    /* glass and barrels nearby */
    int r = (int)(radius / TILE) + 1;
    int cx = tile_of(pos.x), cy = tile_of(pos.y);
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            if (!in_map(x, y)) continue;
            if (v2_dist(tile_center(x, y), pos) > radius) continue;
            Cell *c = cell(x, y);
            if (c->obj == OB_GLASS_H || c->obj == OB_GLASS_V) break_glass(x, y, pos);
            if (c->obj == OB_BARREL) barrel_hit(x, y, owner);
        }
    for (int d = 0; d < W.ndoors; d++) {
        Door *dr = &W.doors[d];
        if (v2_dist(dr->hinge, pos) < radius * 1.5f) dr->av += (chance(0.5f) ? 1 : -1) * 12;
    }
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (!c->alive) continue;
        float d = v2_dist(c->pos, pos);
        if (d < radius * 1.6f) c->vel = v2_add(c->vel, v2_scale(v2_norm(v2_sub(c->pos, pos)), 300 * (1 - d / (radius * 1.6f))));
    }
}

/* ----------------------------------------------------------------- fire */
void fire_spawn(V2 pos, float life, int owner) {
    int tx = tile_of(pos.x), ty = tile_of(pos.y);
    if (cell_flag(tx, ty, CF_SOLID) && !cell_flag(tx, ty, CF_DOOR)) return;
    for (int i = 0; i < MAX_FIRES; i++) {
        Fire *f = &W.fires[i];
        if (f->alive && v2_dist2(f->pos, pos) < 6 * 6) { f->life = MAXF(f->life, f->t + life); return; }
    }
    for (int i = 0; i < MAX_FIRES; i++) {
        Fire *f = &W.fires[i];
        if (f->alive) continue;
        memset(f, 0, sizeof *f);
        f->alive = true;
        f->pos = pos;
        f->life = life;
        f->r = 9;
        f->owner = owner;
        f->spread_t = frange(0.5f, 1.2f);
        return;
    }
}

void fires_update(float dt) {
    int burning = 0;
    V2 near = v2(0, 0);
    float nd = 1e9f;
    for (int i = 0; i < MAX_FIRES; i++) {
        Fire *f = &W.fires[i];
        if (!f->alive) continue;
        burning++;
        float d = v2_dist(f->pos, player()->pos);
        if (d < nd) { nd = d; near = f->pos; }
        f->t += dt;
        if (f->t >= f->life) {
            f->alive = false;
            decal(SPR_FX_SCORCH + irange(0, 1), f->pos, frange(-PI_F, PI_F), 0.5f, rgba(255, 255, 255, 170));
            int tx = tile_of(f->pos.x), ty = tile_of(f->pos.y);
            if (in_map(tx, ty)) cell(tx, ty)->fuel = 0;
            continue;
        }
        add_light(f->pos, 46 + sinf(W.time * 13 + i) * 6, COL_ORANGE, 0.8f);
        if (chance(dt * 6)) {
            Particle *e = particle_add(PT_EMBER, f->pos, v2(frange(-15, 15), frange(-40, -15)), frange(0.4f, 0.9f));
            if (e) { e->spr = SPR_FX_SPARK; e->frames = 3; }
        }
        if (chance(dt * 3)) {
            Particle *s = particle_add(PT_SMOKE, v2(f->pos.x, f->pos.y - 6), v2(frange(-6, 6), frange(-24, -10)), frange(0.8f, 1.4f));
            if (s) { s->spr = SPR_FX_SMOKE; s->frames = 4; s->scale = frange(0.6f, 1.0f); s->col = rgba(50, 45, 55, 150); }
        }
        /* burn actors */
        for (int j = 0; j < W.nactors; j++) {
            Actor *t = &W.actors[j];
            if (!t->used || !t->alive) continue;
            if (v2_dist2(t->pos, f->pos) > (f->r + t->radius) * (f->r + t->radius)) continue;
            if (j == 0 && RUN.perks[PK_FIREBUG]) continue;
            if (t->burn_t <= 0) {
                t->last_hit_by = f->owner;
                if (j != 0) ai_on_hurt(t, j, f->owner);
            }
            t->burn_t = MAXF(t->burn_t, 2.5f);
        }
        /* spread across grass and boxes */
        f->spread_t -= dt;
        if (f->spread_t <= 0) {
            f->spread_t = frange(0.7f, 1.4f);
            int tx = tile_of(f->pos.x), ty = tile_of(f->pos.y);
            for (int k = 0; k < 4; k++) {
                int nx = tx + (k == 0) - (k == 1), ny = ty + (k == 2) - (k == 3);
                if (in_map(nx, ny) && cell(nx, ny)->fuel && chance(0.4f)) {
                    fire_spawn(v2(tile_center(nx, ny).x + frange(-4, 4), tile_center(nx, ny).y + frange(-4, 4)), frange(4, 7), f->owner);
                    cell(nx, ny)->fuel = 0;
                }
            }
        }
    }
    audio_loop(LOOP_FIRE, burning > 0 ? CLAMP(1.0f - nd / 260.0f, 0.0f, 0.7f) : 0, 1);
    (void)near;
}

void fires_draw(void) {
    for (int i = 0; i < MAX_FIRES; i++) {
        Fire *f = &W.fires[i];
        if (!f->alive) continue;
        int fr = ((int)(W.time * 12) + i) % 4;
        float s = CLAMP((f->life - f->t) * 1.5f, 0.3f, 1.0f);
        gfx_spr_ex(SPR_FX_FIRE + fr, f->pos.x, f->pos.y + 4, 0, s, s, TINT_NONE);
        gfx_spr_ex(SPR_FX_FIRE + (fr + 2) % 4, f->pos.x + 5, f->pos.y + 2, 0, -s * 0.7f, s * 0.7f, TINT_NONE);
    }
}

/* ------------------------------------------------------------ executions */
void execute_begin(Actor *a, int idx, int target) {
    Actor *t = &W.actors[target];
    const WeaponDef *w = item_weapon(a->weapon.id);
    float dur = 0.75f;
    int hits = 3;
    if (w->kind == WK_MELEE) { dur = w->style == ST_HEAVY ? 0.55f : 0.5f; hits = w->style == ST_STAB ? 2 : (w->style == ST_HEAVY ? 1 : 2); }
    if (w->kind == WK_GUN && a->weapon.cond > 0) { dur = 0.35f; hits = 1; }
    if (w->kind == WK_CHAINSAW) { dur = 0.6f; hits = 6; }
    if (idx == 0 && RUN.perks[PK_BUTCHER]) dur *= 0.6f;
    a->exec_t = dur;
    a->exec_target = target;
    a->exec_hits = hits;
    a->br.timer = dur;   /* total */
    a->pos = v2_add(t->pos, v2_scale(v2_angle(t->face + PI_F), 0));
    t->down_t = MAXF(t->down_t, dur + 0.3f);
    a->vel = v2(0, 0);
    G.cam_zoom = 1.0f;
}

void execute_update(Actor *a, int idx, float dt) {
    Actor *t = &W.actors[a->exec_target];
    const WeaponDef *w = item_weapon(a->weapon.id);
    float total = a->br.timer;
    float before = a->exec_t;
    a->exec_t -= dt;
    a->face = v2_to_angle(v2_sub(t->pos, a->pos)) + sinf(W.time * 30) * 0.05f;
    if (!t->alive) { a->exec_t = 0; return; }
    t->down_t = MAXF(t->down_t, 0.3f);
    /* hits are spread evenly through the animation */
    for (int h = 0; h < a->exec_hits; h++) {
        float at = total * (1.0f - (h + 1.0f) / (a->exec_hits + 0.5f));
        if (before > at && a->exec_t <= at) {
            a->atk_t = 0;
            a->atk_dir++;
            V2 dir = v2_sub(t->pos, a->pos);
            if (v2_len2(dir) < 1) dir = v2_angle(a->face);
            spray_blood(t->pos, v2_scale(v2_norm(dir), 1), 14, 160);
            decal(SPR_FX_BLOOD + irange(0, SPR_FX_BLOOD_N - 1), t->pos, frange(-PI_F, PI_F), 1, TINT_NONE);
            int sfx = !a->weapon.id ? SFX_PUNCH : (w->gore ? SFX_HIT_BLADE : SFX_HIT_BLUNT);
            if (w->kind == WK_GUN && a->weapon.cond > 0) {
                sfx = w->ammo == IT_SHELLS ? SFX_SHOTGUN : SFX_PISTOL;
                a->weapon.cond--;
                muzzle(a, w, v2_add(a->pos, v2_rot(v2(12, 1), a->face)), a->face);
                make_noise(a->pos, w->noise, idx);
            }
            play_at(sfx, a->pos, 1.0f, frange(0.85f, 1.05f));
            play_at(SFX_BONE_CRUNCH, a->pos, 0.6f, frange(0.9f, 1.2f));
            hitstop(0.05f);
            add_shake(4);
            G.chroma = MAXF(G.chroma, 0.5f);
        }
    }
    if (a->exec_t <= 0) {
        a->exec_t = 0;
        V2 dir = v2_sub(t->pos, a->pos);
        if (v2_len2(dir) < 1) dir = v2_angle(a->face);
        int widx = a->weapon.id ? (int)(w - WEAPONS) : W_FISTS;
        if (t->arch == AR_BOSS && t->hp > 20) {
            /* the King doesn't die that easily */
            t->down_t = 0.1f;
            damage_actor(a->exec_target, idx, 20, dir, 200, 0, widx, DMG_EXEC);
            play_at(SFX_EXECUTE, t->pos, 1.0f, 0.8f);
            a->exec_target = -1;
            a->invuln = 0.6f;
            return;
        }
        play_at(SFX_EXECUTE, t->pos, 1.0f, 1);
        bool gore = w->gore || w->kind == WK_GUN || w->kind == WK_CHAINSAW;
        kill_actor(a->exec_target, idx, dir, widx, DMG_EXEC | (gore ? DMG_GORE : 0));
        if (idx == 0 && RUN.perks[PK_BUTCHER]) a->hp = MINF(a->maxhp, a->hp + 1);
        a->exec_target = -1;
        a->invuln = 0.3f;
    }
}
