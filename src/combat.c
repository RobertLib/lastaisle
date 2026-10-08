/* LAST AISLE - combat: melee, guns, throwing, damage, gore, explosions, fire, executions */
#include "world.h"
#include "gfx.h"
#include "audio.h"
#include "game.h"

_Static_assert(MAX_ACTORS <= 128, "bullet hit masks hold 128 actors");
static uint64_t bullet_hit[MAX_BULLETS][2];   /* actors each bullet already went through */
static int16_t swing_item[MAX_ACTORS];        /* weapon a pending melee hit was swung with */

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

void spray_sap(V2 pos, V2 dir, int amount, float force) {
    float base = v2_len2(dir) > 0 ? v2_to_angle(dir) : frange(-PI_F, PI_F);
    for (int i = 0; i < amount; i++) {
        float a = base + frange(-0.8f, 0.8f) * (v2_len2(dir) > 0 ? 1.0f : 4.0f);
        Particle *p = particle_add(PT_BLOOD, v2(pos.x + frange(-2, 2), pos.y + frange(-2, 2)), v2_scale(v2_angle(a), frange(0.2f, 0.9f) * force),
                                   frange(0.25f, 0.6f));
        if (!p) return;
        p->z = frange(3, 8);
        p->vz = frange(10, 60);
        p->spr = SPR_FX_SAP_DRIP + irange(0, SPR_FX_SAP_DRIP_N - 1);
        p->bake = true;
        p->scale = frange(0.8f, 1.3f);
    }
}

/* people and animals bleed, plants ooze */
static void bleed(Actor *v, V2 dir, int amount, float force) {
    if (is_plant(v)) spray_sap(v->pos, dir, amount, force);
    else spray_blood(v->pos, dir, amount, force);
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
    if (v->br.state == AI_GETAWAY) SDL_Log("VAN: thief down at %.0fs, %d stacks drop", W.time, v->ninv);
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
    if (is_plant(v)) {
        plant_die(v, dir, fr == 2);   /* no body to drag about: it goes into the ground where it stood */
    } else {
        float size = is_animal(v) ? animal_size(v) : 1.0f;   /* a rat doesn't paint the room */
        int spr = is_animal(v) ? animal_corpse(v, fr == 2) : d->spr_dead + (v->arch == AR_BOSS ? 0 : fr);
        corpse_add(cpos, ang, spr, fr == 2, v2_scale(v2_norm(dir), slide));
        blood_pool(v->pos, dir, size > 0.8f);
        spray_blood(v->pos, dir, (int)((gore ? 40 : 18) * size), gore ? 260 : 170);
        if (gore) {
            gibs(v->pos, v2_norm(dir), (int)(irange(3, 6) * size + 0.5f), fr == 2 && !is_animal(v), v->arch);
            audio_play(SFX_GORE, 0.9f, 0, frange(0.85f, 1.1f));
        }
        if (is_animal(v)) animal_cry(v, 0.9f);
        else play_at(SFX_DEATH, v->pos, 0.75f, (v->arch == AR_BRUTE || v->arch == AR_BOSS) ? 0.7f : frange(0.9f, 1.15f));
        play_at(SFX_BODYFALL, v->pos, 0.6f * size, 1);
    }
    /* drop everything */
    if (v->weapon.id) {
        pickup_spawn(v->weapon, v->pos, v2(frange(-60, 60), frange(-60, 60)));
        v->weapon.id = IT_NONE;
    }
    for (int k = 0; k < WSLOTS; k++)
        if (v->slots[k].id) { pickup_spawn(v->slots[k], v->pos, v2(frange(-60, 60), frange(-60, 60))); v->slots[k].id = IT_NONE; }
    for (int i = 0; i < v->ninv; i++)
        pickup_spawn(v->inv[i], v->pos, v2(frange(-70, 70), frange(-70, 70)));
    v->ninv = 0;
    if (v->cart >= 0) { W.carts[v->cart].holder = -1; v->cart = -1; }
    if (is_crew(v)) crew_killed(vi);
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
    if (!(flags & DMG_SNEAK)) make_noise(v->pos, is_plant(v) ? 70 : 140, attacker);   /* silent takedowns stay silent */
}

void damage_actor(int vi, int attacker, float dmg, V2 dir, float kb, float kd, int weapon, int flags) {
    Actor *v = &W.actors[vi];
    if (!v->used || !v->alive) return;
    if (W.hub) return;   /* nobody gets hurt at the Greenhouse */
    if (vi == 0 && W.exiting) return;
    if (allied(attacker, vi) && !(flags & (DMG_EXPLOSION | DMG_FIRE))) return;   /* a door or a cart you shoved: no harm done */
    if (v->invuln > 0 && !(flags & (DMG_EXEC | DMG_EXPLOSION))) return;
    if (vi == 0) {
        if (v->exec_t > 0 && !(flags & (DMG_BULLET | DMG_EXPLOSION))) dmg *= 0.5f;
        v->invuln = 0.45f;
        W.took_damage = true;
        add_shake(7);
        G.impact = 1.0f;
        G.flash = 0.35f;
        G.flash_col = rgba(200, 20, 40, 255);
        audio_play(SFX_HURT_PLAYER, 0.9f, 0, frange(0.95f, 1.05f));
        hitstop(0.05f);
    } else if (is_animal(v)) {
        if (v->hp > (int)ceilf(dmg)) animal_cry(v, 0.8f);   /* a killing blow cries out in kill_actor */
    } else if (!is_plant(v)) {   /* plants rustle in plant_wound */
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
    int bl = MINF(idmg, 5);   /* overkill (sneak kills, thrown blades) shouldn't paint the whole room */
    if (is_plant(v)) plant_wound(v, n, bl);
    else {
        spray_blood(v->pos, n, (int)((6 + bl * 4) * (is_animal(v) ? animal_size(v) : 1.0f)), 140 + bl * 20);
        if (idmg >= 2 || chance(0.5f)) decal(SPR_FX_BLOOD + irange(0, SPR_FX_BLOOD_N - 1), v2_add(v->pos, v2_scale(n, 8)), frange(-PI_F, PI_F), 1, TINT_NONE);
    }
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
    if (kdc > 0 && (is_animal(v) || is_plant(v)) && chance(kdc)) {
        /* animals don't lie there waiting to be finished off: they reel, then come back (or run) - and a plant
         * has nothing to fall over with */
        v->stun_t = MAXF(v->stun_t, frange(0.6f, 0.9f));
        v->atk_t = -1;
        v->windup = 0;
        v->hit_pending = false;
    } else if (kdc > 0 && chance(kdc)) {
        v->down_t = vi == 0 ? 0.7f : frange(2.2f, 3.2f) * (heavy ? 0.5f : 1.0f);
        v->atk_t = -1;
        v->windup = 0;
        v->hit_pending = false;
        v->exec_t = 0;
        v->exec_target = -1;
        if (vi != 0 && v->weapon.id && v->arch != AR_BOSS && !is_crew(v)) {   /* the King never lets go of his gun, the crew of theirs */
            pickup_spawn(v->weapon, v->pos, v2_add(v2_scale(n, 60), v2(frange(-40, 40), frange(-40, 40))));
            v->weapon.id = IT_NONE;
        }
        if (v->cart >= 0) { W.carts[v->cart].holder = -1; v->cart = -1; }
        play_at(SFX_BODYFALL, v->pos, 0.6f, 1);
    }
    if (weapon == W_PAN && v->down_t <= 0 && !heavy) v->stun_t = 1.0f;
}

/* ------------------------------------------------------------ weapon stats */
int weapon_index(ItemId id) { return item_is_weapon(id) ? ITEMS[id].weapon : W_FISTS; }

int train_level(int stat) { return stat_level(RUN.xp[CLAMP(stat, 0, STAT_COUNT - 1)]); }

int weapon_max_cond(const Stack *s) {
    WeaponDef w = weapon_stats(s);
    if (w.kind == WK_GUN) return item_weapon((ItemId)s->id)->mag == 1 ? 1 : (int)(w.mag * (RUN.perks[PK_STEADYAIM] ? 1.5f : 1.0f));
    if (w.kind == WK_MELEE) return w.durability * (RUN.perks[PK_TINKERER] ? 2 : 1);
    return w.durability;
}

/* ----------------------------------------------------------------- melee */
static void melee_hit(Actor *a, int idx) {
    WeaponDef wd = weapon_stats(&a->weapon);
    const WeaponDef *w = &wd;
    int widx = weapon_index((ItemId)a->weapon.id);
    float range = w->range + 5;
    float arc = (w->arc > 0 ? w->arc : 60) * 0.5f * DEG2RAD;
    float dmg = w->damage;
    float kd = w->knockdown;
    float kb = w->knockback;
    int str = idx == 0 ? train_level(STAT_STR) : 0;   /* the weight bench at the Greenhouse */
    if (idx == 0 && !a->weapon.id && RUN.perks[PK_BRAWLER]) { dmg *= 3; kd = 1.0f; }
    kd = MINF(1.0f, kd + 0.03f * str);
    kb *= 1.0f + 0.08f * str;
    if (a->arch == AR_BRUTE) dmg += 1;
    if (a->arch == AR_BOSS) dmg += 2;
    bool hit_any = false, loud = false;
    for (int j = 0; j < W.nactors; j++) {
        if (j == idx) continue;
        Actor *t = &W.actors[j];
        if (!t->used || !t->alive) continue;
        if (idx != 0 && j != a->br.target && !actor_hostile(idx, j)) continue;
        if (allied(idx, j)) continue;   /* you swing past your own */
        V2 d = v2_sub(t->pos, a->pos);
        float dist = v2_len(d);
        if (dist > range + t->radius) continue;
        float ad = fabsf(angle_diff(a->face, v2_to_angle(d)));
        if (dist > t->radius + 4 && ad > arc) continue;
        if (!los_clear(a->pos, t->pos, true)) continue;
        int flags = DMG_MELEE;
        float dd = dmg;
        if (str > 0 && chance(0.12f * str)) { dd += 1; add_shake(1); }
        /* sneak attack: silent takedown on an unaware victim */
        if (idx == 0 && j != 0 && w->kind == WK_MELEE && !t->br.aware && t->br.state != AI_CHASE && t->down_t <= 0 &&
            t->arch != AR_BOSS && t->arch != AR_BRUTE) {
            dd = 99;
            flags |= DMG_SNEAK;
        }
        if (w->gore && dd >= t->hp) flags |= (chance(0.5f) ? DMG_GORE : 0);
        damage_actor(j, idx, dd, d, kb, kd, widx, flags);
        hit_any = true;
        if (!(flags & DMG_SNEAK)) loud = true;
        if (idx == 0) {
            G.impact = MAXF(G.impact, 0.35f);
        }
    }
    /* swinging into glass */
    V2 tip = v2_add(a->pos, v2_scale(v2_angle(a->face), range));
    int tx = tile_of(tip.x), ty = tile_of(tip.y);
    if (in_map(tx, ty) && (W.cells[ty][tx].obj == OB_GLASS_H || W.cells[ty][tx].obj == OB_GLASS_V) && a->weapon.id) {
        break_glass(tx, ty, a->pos);
        hit_any = loud = true;
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
        if (loud) make_noise(a->pos, w->noise, idx);
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
        bullet_hit[i][0] = bullet_hit[i][1] = 0;
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
        b->weapon = weapon_index(w->item);
        return;
    }
}

void spit_fire(int owner, V2 pos, float ang, float speed, float range) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &W.bullets[i];
        if (b->alive) continue;
        memset(b, 0, sizeof *b);
        bullet_hit[i][0] = bullet_hit[i][1] = 0;
        b->alive = true;
        b->pos = b->prev = pos;
        b->vel = v2_scale(v2_angle(ang), speed);
        b->dmg = 1;
        b->kb = 70;
        b->owner = owner;
        b->life = b->life0 = range / speed;
        b->spit = true;
        b->weapon = W_NONE;
        return;
    }
}

/* a glob of spit bursts: on a wall, a door, somebody - or the floor where it came down */
static void spit_splat(V2 pos, V2 vel, bool hit) {
    play_at(SFX_SPLAT, pos, hit ? 0.8f : 0.5f, frange(0.9f, 1.15f));
    spray_sap(pos, v2_scale(vel, -1), hit ? 5 : 3, 60);
    decal(SPR_FX_SAP + irange(0, SPR_FX_SAP_N - 1), pos, frange(-PI_F, PI_F), frange(0.5f, 0.7f), TINT_NONE);
}

void attack_begin(Actor *a, int idx) {
    WeaponDef wd = weapon_stats(&a->weapon);
    const WeaponDef *w = &wd;
    int widx = weapon_index((ItemId)a->weapon.id);
    float cd = w->cooldown;
    if (idx == 0 && RUN.perks[PK_QUICKHANDS] && (w->kind == WK_MELEE || w->kind == WK_FIST)) cd *= 0.75f;
    if (idx != 0) cd *= 1.25f;
    a->atk_cd = cd;
    if (idx == 0) W.weapons_used |= 1ull << widx;
    V2 fwd = v2_angle(a->face);
    a->hit_pending = false;   /* a gun shot must never resolve a stale swing with gun stats */
    switch (w->kind) {
    case WK_FIST:
    case WK_MELEE:
        a->atk_t = 0;
        a->atk_dir++;
        a->hit_pending = true;
        swing_item[idx] = a->weapon.id;
        a->hit_at = w->style == ST_HEAVY ? 0.09f : 0.045f;
        play_at(w->style == ST_HEAVY ? SFX_SWING_HEAVY : SFX_SWING, a->pos, 0.8f, frange(0.9f, 1.15f));
        break;
    case WK_GUN: {
        if (a->weapon.cond <= 0) { play_at(SFX_EMPTY, a->pos, 0.6f, 1); return; }
        a->weapon.cond--;
        a->atk_t = 0;
        V2 muzzle_pos = v2_add(a->pos, v2_rot(v2(14, 1), a->face));
        if (!los_clear(a->pos, muzzle_pos, true)) muzzle_pos = a->pos;
        float npc_aim = ARCH[a->arch].aim * (is_crew(a) ? 1.0f : MAXF(0.8f, 1.35f - 0.08f * W.level + (RUN.mode == MODE_STORY ? 0.15f : 0.0f)));
        float aim = 1.0f - 0.07f * train_level(STAT_AIM);   /* hours at the Greenhouse range */
        float spread = w->spread * (idx == 0 ? (RUN.perks[PK_STEADYAIM] ? 0.55f : 1.0f) * aim : npc_aim);
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
            if ((idx != 0 && !actor_hostile(idx, j)) || allied(idx, j)) continue;
            V2 d = v2_sub(t->pos, a->pos);
            if (v2_len(d) > w->range + t->radius) continue;
            if (fabsf(angle_diff(a->face, v2_to_angle(d))) > 0.7f) continue;
            if (!los_clear(a->pos, t->pos, true)) continue;   /* not through closed doors */
            t->push = v2_scale(t->push, 0.3f);
            damage_actor(j, idx, w->damage, d, 20, 0, widx, DMG_MELEE | DMG_GORE);
            bleed(t, v2_scale(d, -1), 8, 200);
            if (chance(0.3f) && !is_plant(t)) gibs(t->pos, v2_norm(d), 1, false, t->arch);
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
    /* swapped, thrown or dropped the weapon mid-swing: the swing is lost */
    if (a->hit_pending && a->weapon.id != swing_item[idx]) a->hit_pending = false;
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
    if (idx == 0) speed *= 1.0f + 0.04f * train_level(STAT_STR);
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

static void queue_blast(V2 pos, int owner) {
    if (barrel_queue_n < 32) {
        barrel_queue[barrel_queue_n].pos = pos;
        barrel_queue[barrel_queue_n].t = 0.08f;
        barrel_queue[barrel_queue_n].owner = owner;
        barrel_queue_n++;
    }
}

static void barrel_hit(int tx, int ty, int owner) {
    Cell *c = cell(tx, ty);
    if (c->obj != OB_BARREL) return;
    c->obj = OB_NONE;
    c->flags &= ~(CF_SOLID | CF_SHOT);
    queue_blast(tile_center(tx, ty), owner);
}

/* a propane tank lying on the floor goes up like a barrel - unless the list still needs it */
static bool propane_hit(int pi, int owner) {
    Pickup *p = &W.pickups[pi];
    if (!p->alive || p->st.id != IT_PROPANE || item_needed(IT_PROPANE)) return false;
    p->alive = false;
    queue_blast(p->pos, owner);
    return true;
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
    int tanks[16], ntanks = 0;
    for (int k = 0; k < MAX_PICKUPS && ntanks < 16; k++)
        if (W.pickups[k].alive && W.pickups[k].st.id == IT_PROPANE) tanks[ntanks++] = k;
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &W.bullets[i];
        if (!b->alive) continue;
        b->life -= dt;
        if (b->life <= 0) {
            if (b->spit) spit_splat(b->pos, b->vel, false);
            b->alive = false;
            continue;
        }
        if (b->spit && chance(dt * 5)) {
            /* it drips on the way */
            Particle *d = particle_add(PT_BLOOD, b->pos, v2_scale(b->vel, 0.2f), 0.2f);
            if (d) { d->spr = SPR_FX_SAP_DRIP + irange(0, SPR_FX_SAP_DRIP_N - 1); d->z = 4; d->bake = true; d->scale = 0.7f; }
        }
        b->prev = b->pos;
        V2 step = v2_scale(b->vel, dt);
        float len = v2_len(step);
        int sub = (int)(len / 5) + 1;
        V2 sp = v2_scale(step, 1.0f / sub);
        for (int s = 0; s < sub && b->alive; s++) {
            V2 np = v2_add(b->pos, sp);
            int tx = tile_of(np.x), ty = tile_of(np.y);
            if (b->spit && cell_flag(tx, ty, CF_SHOT)) {
                spit_splat(b->pos, b->vel, true);   /* spit doesn't break windows or set off barrels */
                b->alive = false;
                break;
            }
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
            /* doors: test the whole step so fast bullets can't skip the thin leaf */
            for (int d = 0; d < W.ndoors && b->alive && !b->flame; d++) {
                Door *dr = &W.doors[d];
                V2 e = v2_add(dr->hinge, v2_scale(v2_angle(dr->base + dr->ang), dr->len));
                if (seg_cross(b->pos, np, dr->hinge, e) || seg_circle(dr->hinge, e, np, 1.5f)) {
                    if (b->spit) spit_splat(np, b->vel, true);
                    else impact(np, b->vel, false);
                    dr->av += (chance(0.5f) ? 1 : -1) * 2.0f;
                    b->alive = false;
                }
            }
            if (!b->alive) break;
            /* propane tanks on the floor */
            for (int k = 0; k < ntanks && !b->flame && !b->spit; k++) {
                Pickup *pk = &W.pickups[tanks[k]];
                if (!pk->alive || !seg_circle(b->pos, np, pk->pos, 5) || !propane_hit(tanks[k], b->owner)) continue;
                impact(np, b->vel, true);
                b->alive = false;
                break;
            }
            if (!b->alive) break;
            /* actors */
            for (int j = 0; j < W.nactors && b->alive; j++) {
                Actor *t = &W.actors[j];
                if (!t->used || !t->alive || j == b->owner) continue;
                if (t->down_t > 0 || allied(b->owner, j)) continue;
                uint64_t bit = 1ull << (j & 63);
                if (bullet_hit[i][j >> 6] & bit) continue;   /* pierce goes through, not into the same body twice */
                if (b->spit && (is_plant(t) || is_animal(t))) continue;   /* lobbed over the leaves and the strays */
                if (!seg_circle(b->pos, np, t->pos, t->radius + 1)) continue;
                if (b->spit) {
                    damage_actor(j, b->owner, b->dmg, b->vel, b->kb, 0, W_NONE, DMG_BULLET);
                    spit_splat(np, b->vel, true);
                    b->alive = false;
                    break;
                }
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
                bullet_hit[i][j >> 6] |= bit;
                if (b->pierce-- <= 0) b->alive = false;
            }
            if (b->alive) b->pos = np;
        }
        if (b->flame && b->alive && chance(dt * 6)) {   /* ~0.1 per frame at 60 fps, at any frame rate */
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
        if (b->spit) {
            /* lobbed: a shadow on the floor under a wobbling glob */
            float z = 3 + sinf(CLAMP(1.0f - b->life / b->life0, 0.0f, 1.0f) * PI_F) * 5;
            gfx_spr_ex(SPR_FX_SHADOW, b->pos.x, b->pos.y + 1, 0, 0.3f, 0.3f, rgba(255, 255, 255, 170));
            gfx_spr_ex(SPR_FX_SPIT + ((int)(W.time * 14 + i) & 1), b->pos.x, b->pos.y - z, ang, 1, 1, TINT_NONE);
            continue;
        }
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
    G.impact = 1.0f;
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
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &W.pickups[i];
        if (p->alive && p->st.id == IT_PROPANE && v2_dist(p->pos, pos) < radius && los_clear(pos, p->pos, true)) propane_hit(i, owner);
    }
    /* whatever the blast flings is the blaster's doing */
    for (int d = 0; d < W.ndoors; d++) {
        Door *dr = &W.doors[d];
        if (v2_dist(dr->hinge, pos) < radius * 1.5f) { dr->av += (chance(0.5f) ? 1 : -1) * 12; dr->pusher = owner; }
    }
    for (int i = 0; i < MAX_CARTS; i++) {
        Cart *c = &W.carts[i];
        if (!c->alive) continue;
        float d = v2_dist(c->pos, pos);
        if (d < radius * 1.6f) {
            c->vel = v2_add(c->vel, v2_scale(v2_norm(v2_sub(c->pos, pos)), 300 * (1 - d / (radius * 1.6f))));
            if (c->holder < 0) c->pusher = owner;
        }
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
            G.impact = MAXF(G.impact, 0.5f);
        }
    }
    if (a->exec_t <= 0) {
        a->exec_t = 0;
        V2 dir = v2_sub(t->pos, a->pos);
        if (v2_len2(dir) < 1) dir = v2_angle(a->face);
        int widx = a->weapon.id ? (int)(w - WEAPONS) : W_FISTS;
        if (t->arch == AR_BOSS && t->hp > 20) {
            /* the King doesn't die that easily - and he's back on his feet at once, so no chaining */
            t->down_t = 0;
            t->stun_t = 0.35f;
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
