/* LAST AISLE - NPC brains */
#include "world.h"
#include "gfx.h"
#include "audio.h"

float diff_reaction(void) {
    float k = 1.25f - 0.07f * W.level;
    if (RUN.mode == MODE_STORY) k += 0.12f;
    return MAXF(0.7f, k);
}

static bool has_grudge(Actor *a, int who) {
    for (int k = 0; k < 4; k++)
        if (a->br.grudge[k] == who) return true;
    return false;
}

static void add_grudge(Actor *a, int who) {
    if (who < 0 || &W.actors[who] == a || has_grudge(a, who)) return;
    for (int k = 0; k < 4; k++)
        if (a->br.grudge[k] < 0) { a->br.grudge[k] = who; return; }
    a->br.grudge[3] = who;
}

bool actor_hostile(int ai, int bi) {
    if (ai == bi) return false;
    Actor *a = &W.actors[ai], *b = &W.actors[bi];
    if (!b->used || !b->alive) return false;
    if (has_grudge(a, bi)) return true;
    /* animals: a rabid one goes for anybody and anybody goes for it; the rest are left alone (and leave you alone) */
    if (is_animal(a) || is_animal(b)) {
        if (is_animal(a) && is_animal(b)) return false;
        if (is_plant(a) || is_plant(b)) return false;   /* the strays give the weeds a wide berth */
        if (ai == 0) return b->rabid || has_grudge(b, 0);
        return is_animal(a) ? a->rabid : b->rabid;
    }
    /* plants: they go for anybody who comes too close (plant.c decides how close); you cut them down, the locals
     * just step round them (see ai_on_hurt) */
    if (is_plant(a) || is_plant(b)) {
        if (is_plant(a)) return !is_plant(b);
        return ai == 0;
    }
    if (ai == 0) return faction_hostile(FAC_PLAYER, b->faction) || has_grudge(b, 0);
    if (a->temper == TEMP_TIMID || a->faction == FAC_SCAV) return false;
    return faction_hostile(a->faction, b->faction);
}

static void set_state(Actor *a, AiState s, float timer) {
    a->br.state = s;
    a->br.timer = timer;
    a->br.path_len = a->br.path_i = 0;
    a->br.repath = 0;
}

/* a thief minds nothing but the van, and then nothing but getting away with it */
static bool on_the_job(const Actor *a) { return a->br.state == AI_STEAL || a->br.state == AI_GETAWAY; }

static void alert_icon(Actor *a, int icon, float t) {
    a->alert_icon = icon;
    a->alert_icon_t = t;
}

/* ------------------------------------------------------------ movement */
bool path_to(Actor *a, V2 goal) {
    int len = 0;
    bool ok = path_find_r(a->pos, goal, a->radius, a->br.path, 48, &len);
    a->br.path_len = len;
    a->br.path_i = 0;
    /* a goal inside a shelf/wall can never be reached: aim for the last walkable node instead */
    if (!walkable_tile(tile_of(goal.x), tile_of(goal.y)) && len > 0)
        goal = tile_center(a->br.path[len - 1][0], a->br.path[len - 1][1]);
    a->br.goal = goal;
    return ok;
}

/* steer along path; returns true when arrived */
bool follow(Actor *a, float speed, float dt) {
    Brain *b = &a->br;
    V2 target;
    if (b->path_i < b->path_len) {
        target = tile_center(b->path[b->path_i][0], b->path[b->path_i][1]);
        if (b->path_i == b->path_len - 1) target = b->goal;
        if (v2_dist(a->pos, target) < 6) b->path_i++;
    } else {
        target = b->goal;
    }
    V2 d = v2_sub(target, a->pos);
    float l = v2_len(d);
    if (l < 5 && b->path_i >= b->path_len) {
        a->vel = v2_scale(a->vel, 0.5f);
        return true;
    }
    V2 want = v2_scale(v2_scale(d, 1.0f / MAXF(l, 0.01f)), speed);
    a->vel = v2_add(a->vel, v2_scale(v2_sub(want, a->vel), smooth_k(10, dt)));
    /* stuck detection */
    b->stuck_t += dt;
    if (b->stuck_t > 0.8f) {
        if (v2_dist(a->pos, b->last_pos) < 6) {
            b->repath = 0;
            a->push = v2_add(a->push, v2(frange(-40, 40), frange(-40, 40)));
        }
        b->stuck_t = 0;
        b->last_pos = a->pos;
    }
    return false;
}

/* straight walk from a to b is free of solid tiles (counters, glass and crates don't block sight) */
bool walk_clear(V2 a, V2 b, float r) {
    V2 d = v2_sub(b, a);
    float len = v2_len(d);
    if (len < 1) return true;
    V2 n = v2_scale(d, 1.0f / len), side = v2(-n.y * r, n.x * r);
    for (float t = 0; t < len; t += 6) {
        V2 p = v2_add(a, v2_scale(n, t));
        if (solid_at(p.x, p.y) || solid_at(p.x + side.x, p.y + side.y) || solid_at(p.x - side.x, p.y - side.y)) return false;
    }
    return true;
}

void face_towards(Actor *a, float ang, float rate, float dt) { a->face = lerp_angle(a->face, ang, smooth_k(rate, dt)); }

static void face_move(Actor *a, float dt) {
    if (v2_len(a->vel) > 8) face_towards(a, v2_to_angle(a->vel), 8, dt);
}

/* ---------------------------------------------------------- perception */
static float vis_range(Actor *a, Actor *t, int ti) {
    float r = ARCH[a->arch].view;
    if (W.def->amb == AMB_NIGHT || W.def->amb == AMB_INFERNO) r *= 0.75f;
    if (W.def->amb == AMB_FOG) r *= 0.8f;
    if (ti == 0 && RUN.perks[PK_LIGHTFEET]) r *= 0.9f;
    if (t->burn_t > 0) r *= 1.5f;
    return r;
}

static bool can_see(Actor *a, int ti) {
    Actor *t = &W.actors[ti];
    V2 d = v2_sub(t->pos, a->pos);
    float dist = v2_len(d);
    float range = vis_range(a, t, ti);
    if (dist > range) return false;
    float fov = (a->br.aware || a->br.state == AI_CHASE) ? 1.7f : 1.15f;
    if (dist > 36 && fabsf(angle_diff(a->face, v2_to_angle(d))) > fov) return false;
    return los_clear(a->pos, t->pos, false);
}

static int find_threat(Actor *a, int idx, float *out_d) {
    int best = -1;
    float bd = 1e9f;
    for (int j = 0; j < W.nactors; j++) {
        if (j == idx) continue;
        Actor *t = &W.actors[j];
        if (!t->used || !t->alive) continue;
        if (!actor_hostile(idx, j)) continue;
        float d = v2_dist(t->pos, a->pos);
        if (d > 330 || d > bd) continue;
        if (is_animal(t) && d > 140) continue;   /* a rabid stray is only worth the trouble up close */
        if (!can_see(a, j)) continue;
        best = j;
        bd = d;
    }
    if (out_d) *out_d = bd;
    return best;
}

/* timid: anyone armed and hostile-looking near us? */
static int find_scary(Actor *a, int idx) {
    for (int j = 0; j < W.nactors; j++) {
        if (j == idx) continue;
        Actor *t = &W.actors[j];
        if (!t->used || !t->alive || t->down_t > 0) continue;
        bool scary = (j == 0 && (t->weapon.id || has_grudge(a, 0))) || faction_hostile(t->faction, a->faction) || has_grudge(a, j) ||
                     (is_animal(t) && t->rabid) || (is_plant(t) && !plant_rooted(t) && t->br.state == AI_CHASE);
        if (j == 0 && !t->weapon.id && !has_grudge(a, 0)) scary = false;
        if (!scary) continue;
        float d = v2_dist(t->pos, a->pos);
        float lim = j == 0 ? 85 : (is_animal(t) || is_plant(t) ? 110 : 150);
        if (d < lim && can_see(a, j)) return j;
    }
    return -1;
}

static void become_hostile(Actor *a, int idx, int target, bool icon) {
    if (a->br.state == AI_CHASE && a->br.target == target) return;
    a->br.target = target;
    a->br.aware = true;
    a->br.reaction = ARCH[a->arch].reaction * diff_reaction() * frange(0.8f, 1.3f);
    a->br.last_seen = W.actors[target].pos;
    a->br.aim_t = 0;
    set_state(a, AI_CHASE, 0);
    if (icon) {
        alert_icon(a, 1, 1.2f);
        if (target == 0) play_at(SFX_ALERT, a->pos, 0.8f, frange(0.95f, 1.1f));
    }
}

V2 flee_point(Actor *a, V2 from) {
    V2 best = a->pos;
    float bs = -1;
    for (int i = 0; i < 12; i++) {
        float ang = frange(-PI_F, PI_F);
        float dist = frange(90, 220);
        V2 p = v2_add(a->pos, v2_scale(v2_angle(ang), dist));
        int tx = tile_of(p.x), ty = tile_of(p.y);
        if (!walkable_tile(tx, ty)) continue;
        float s = v2_dist(p, from) - v2_dist(p, a->pos) * 0.3f;
        if (s > bs) { bs = s; best = p; }
    }
    return best;
}

/* the King gets off his throne */
static void boss_wake(Actor *a) {
    set_state(a, AI_CHASE, 0);
    a->br.target = 0;
    world_message("THE MALL KING", COL_YELLOW);
    audio_music(MUS_BOSS);
    alert_icon(a, 1, 1.5f);
    play_at(SFX_ALERT, a->pos, 1, 0.5f);
}

void ai_on_hurt(Actor *a, int idx, int attacker) {
    if (attacker < 0 || attacker == idx) return;
    if (attacker < W.nactors && !W.actors[attacker].alive) return;
    if (is_animal(a)) { add_grudge(a, attacker); animal_on_hurt(a, idx, attacker); return; }
    if (is_plant(a)) { plant_on_hurt(a, idx, attacker); return; }
    if (is_crew(a)) { crew_on_hurt(a, idx, attacker); return; }
    if (a->br.state == AI_GETAWAY) { add_grudge(a, attacker); return; }   /* hit or not, they keep running with it */
    if (is_plant(&W.actors[attacker])) {
        /* nobody picks a fight with a weed: out of its reach, and carry on (lure them through the beds) */
        Brain *b = &a->br;
        if (a->arch == AR_BOSS || b->state == AI_CHASE || b->state == AI_FLEE || b->state == AI_SURRENDER) return;
        set_state(a, AI_WANDER, 1.0f);
        path_to(a, flee_point(a, W.actors[attacker].pos));
        return;
    }
    /* friendly fire between gang members is shrugged off */
    if (attacker != 0 && W.actors[attacker].faction == a->faction && a->faction != FAC_SCAV) return;
    add_grudge(a, attacker);
    a->br.aware = true;
    if (a->arch == AR_BOSS) {
        if (a->br.state == AI_GUARD && !is_animal(&W.actors[attacker]) && !is_plant(&W.actors[attacker])) boss_wake(a);
        return;
    }
    if (a->temper == TEMP_TIMID && !(a->temper == TEMP_DEFENSIVE)) {
        a->br.target = attacker;
        set_state(a, AI_FLEE, frange(3, 5));
        path_to(a, flee_point(a, W.actors[attacker].pos));
        alert_icon(a, 1, 1.0f);
    } else {
        become_hostile(a, idx, attacker, a->br.target != attacker);
        a->br.reaction = MINF(a->br.reaction, 0.15f);
    }
    /* friends nearby join in (or scatter) */
    for (int j = 1; j < W.nactors; j++) {
        if (j == idx || j == attacker) continue;   /* a scav who hit a friend doesn't turn on himself */
        Actor *f = &W.actors[j];
        if (on_the_job(f)) continue;
        if (!f->used || !f->alive || f->faction != a->faction || f->down_t > 0) continue;
        if (v2_dist(f->pos, a->pos) > 150 || !los_clear(f->pos, a->pos, false)) continue;
        add_grudge(f, attacker);
        if (f->temper == TEMP_TIMID) {
            if (f->br.state != AI_FLEE) {
                set_state(f, AI_FLEE, frange(3, 5));
                path_to(f, flee_point(f, W.actors[attacker].pos));
            }
        } else if (f->br.state != AI_CHASE) {
            become_hostile(f, j, attacker, true);
        }
    }
}

void ai_on_noise(V2 pos, float radius, int source) {
    for (int j = 1; j < W.nactors; j++) {
        Actor *a = &W.actors[j];
        if (!a->used || !a->alive || a->down_t > 0) continue;
        if (j == source) continue;
        float d = v2_dist(a->pos, pos);
        if (d > radius) continue;
        /* walls muffle */
        if (!los_clear(a->pos, pos, false) && d > radius * 0.6f) continue;
        if (is_animal(a)) { animal_on_noise(a, j, pos, radius, source); continue; }
        if (is_plant(a)) { plant_on_noise(a, j, pos, radius, source); continue; }
        if (is_crew(a)) continue;   /* they keep their eyes on you, not on every bang down the aisles */
        if (a->br.state == AI_CHASE || a->br.state == AI_SURRENDER) continue;
        if (on_the_job(a)) continue;   /* busy - and a racket elsewhere is just what a thief wants */
        bool hostile_src = source >= 0 && source < W.nactors && actor_hostile(j, source);
        if (a->arch == AR_BOSS) {
            if (a->br.state == AI_GUARD && hostile_src && d < 260) a->face = v2_to_angle(v2_sub(pos, a->pos));
            continue;
        }
        if (a->temper == TEMP_TIMID || a->faction == FAC_SCAV) {
            if (radius > 200 && d < 220) {
                set_state(a, AI_FLEE, frange(2.5f, 4));
                path_to(a, flee_point(a, pos));
                alert_icon(a, 2, 0.8f);
            }
            continue;
        }
        if (source >= 0 && !hostile_src && source != 0) continue;
        if (a->br.state == AI_INVESTIGATE && v2_dist(a->br.goal, pos) < 40) continue;
        set_state(a, AI_INVESTIGATE, 0);
        a->br.wander_t = 0;
        a->br.alerted_by_noise = true;
        a->br.goal = pos;
        a->br.repath = 0;
        a->br.suspicion = 1;
        alert_icon(a, 2, 1.0f);
    }
}

/* ------------------------------------------------------------- combat */
static int nearest_weapon_pickup(V2 pos, float r) {
    int best = -1;
    float bd = r * r;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &W.pickups[i];
        if (!p->alive || p->flying || p->fuse > 0 || ITEMS[p->st.id].cat != CAT_WEAPON) continue;
        const WeaponDef *w = item_weapon(p->st.id);
        if ((w->kind == WK_GUN || w->kind == WK_CHAINSAW || w->kind == WK_FLAME) && p->st.cond <= 0) continue;
        float d = v2_dist2(p->pos, pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void npc_attack(Actor *a, int idx, Actor *t, float dist, bool visible, float dt) {
    const WeaponDef *w = item_weapon(a->weapon.id);
    float slow = is_crew(a) ? 0.85f : diff_reaction();   /* the crew don't get slower in story mode */
    float aim_ang = v2_to_angle(v2_sub(t->pos, a->pos));
    if (w->kind == WK_GUN) {
        /* lead the target a little */
        V2 lead = v2_add(t->pos, v2_scale(t->vel, dist / MAXF(300, w->speed) * 0.6f));
        aim_ang = v2_to_angle(v2_sub(lead, a->pos));
    }
    face_towards(a, aim_ang, w->kind == WK_GUN ? 9 : 14, dt);
    if (!visible) return;
    if (a->br.reaction > 0) return;
    if (a->stun_t > 0) return;

    if (w->kind == WK_GUN) {
        if (a->weapon.cond <= 0) {
            /* reload from pockets, otherwise throw the empty gun */
            if (inv_count(a, w->ammo) > 0 && a->reload_t <= 0) {
                int mag = w->mag;
                int per = w->ammo == IT_NAILS ? 15 : 1;
                int take = MINF((mag + per - 1) / per, inv_count(a, w->ammo));
                inv_remove(a, w->ammo, take);
                a->weapon.cond = (int16_t)MINF(mag, take * per);
                a->reload_t = 1.1f;
                play_at(SFX_RELOAD, a->pos, 0.5f, 1);
            } else if (a->reload_t <= 0 && dist < 160) {
                Stack s = a->weapon;
                a->weapon.id = IT_NONE;
                throw_item(a, idx, s, aim_ang, 380);
            }
            return;
        }
        if (a->reload_t > 0) return;
        float need_aim = a->weapon.id == IT_RIFLE ? 0.9f : 0.35f;
        need_aim *= slow;
        a->br.aim_t += dt;
        if (a->br.aim_t < need_aim) return;
        if (a->atk_cd > 0 || a->br.burst_t > 0) return;
        if (fabsf(angle_diff(a->face, aim_ang)) > 0.25f) return;
        if (!los_clear(a->pos, t->pos, true) && !los_clear(a->pos, t->pos, false)) return;
        float save = ARCH[a->arch].aim;
        (void)save;
        attack_begin(a, idx);
        a->last_shot_t = W.time;
        a->br.burst_n++;
        int burst = w->mag <= 2 || w->pellets > 1 ? 1 : (a->weapon.id == IT_RIFLE ? 1 : irange(2, 3));
        if (a->br.burst_n >= burst) {
            a->br.burst_n = 0;
            a->br.burst_t = frange(0.7f, 1.3f) * slow;
            if (a->weapon.id == IT_RIFLE) a->br.aim_t = 0;
        }
        return;
    }
    if (w->kind == WK_THROWN) {
        if (dist < 170 && dist > 40 && a->atk_cd <= 0 && a->br.aim_t > 0.5f) {
            attack_begin(a, idx);
            a->br.aim_t = 0;
        }
        a->br.aim_t += dt;
        return;
    }
    /* out of fuel: chuck it at them and fight with whatever's left */
    if ((w->kind == WK_CHAINSAW || w->kind == WK_FLAME) && a->weapon.cond <= 0) {
        Stack s = a->weapon;
        a->weapon.id = IT_NONE;
        a->windup = 0;
        throw_item(a, idx, s, aim_ang, 380);
        return;
    }
    /* melee / chainsaw */
    float reach = w->range + t->radius + 3;
    if (w->kind == WK_CHAINSAW) {
        if (dist < reach + 4 && a->weapon.cond > 0) {
            if (a->atk_cd <= 0) attack_begin(a, idx);
        }
        return;
    }
    if (a->windup > 0) {
        a->windup -= dt;
        if (a->windup <= 0) {
            a->windup = 0;
            attack_begin(a, idx);
        }
        return;
    }
    if (dist < reach && a->atk_cd <= 0 && a->atk_t < 0) {
        a->windup = MAXF(0.08f, w->windup * slow);
        if (a->arch == AR_FERAL) a->windup *= 0.6f;
    }
}

static void chase(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    if (b->target < 0 || !W.actors[b->target].used || !W.actors[b->target].alive) {
        b->target = -1;
        set_state(a, AI_SEARCH, frange(2, 3.5f));
        return;
    }
    Actor *t = &W.actors[b->target];
    float dist = v2_dist(a->pos, t->pos);
    bool visible = dist < 360 && los_clear(a->pos, t->pos, false);
    if (visible) {
        b->last_seen = t->pos;
        b->timer = 0;
    } else {
        b->timer += dt;
        b->aim_t = 0;
        if (b->timer > 5.0f) {
            set_state(a, AI_SEARCH, frange(2.5f, 4));
            b->goal = b->last_seen;
            return;
        }
    }
    if (b->reaction > 0) b->reaction -= dt;
    if (b->burst_t > 0) b->burst_t -= dt;
    if (a->reload_t > 0) a->reload_t -= dt;
    const WeaponDef *w = item_weapon(a->weapon.id);
    float run = ARCH[a->arch].run * a->speed_mul;
    if (a->burn_t > 0) run *= 1.15f;

    /* unarmed: grab a weapon if one is close */
    if (!a->weapon.id && a->arch != AR_FERAL && a->arch != AR_BOSS) {
        int pk = nearest_weapon_pickup(a->pos, 110);
        if (pk >= 0 && dist > 50) {
            Pickup *p = &W.pickups[pk];
            b->goal = p->pos;
            if (b->repath <= 0) { path_to(a, p->pos); b->repath = 0.5f; }
            b->repath -= dt;
            follow(a, run, dt);
            face_move(a, dt);
            if (v2_dist(a->pos, p->pos) < 10) {
                a->weapon = p->st;
                p->alive = false;
                play_at(SFX_PICKUP_WEAPON, a->pos, 0.8f, 1);
            }
            return;
        }
    }

    bool ranged = w->kind == WK_GUN && a->weapon.cond + inv_count(a, w->ammo) > 0;
    bool thrower = w->kind == WK_THROWN;
    V2 goal = b->last_seen;
    float speed = run;
    if ((ranged || thrower) && visible) {
        /* keep a comfortable distance and strafe */
        float want = a->weapon.id == IT_RIFLE ? 180 : (w->pellets > 1 ? 80 : 120);
        V2 dir = v2_norm(v2_sub(a->pos, t->pos));
        b->strafe_t -= dt;
        if (b->strafe_t <= 0) { b->strafe_t = frange(0.8f, 1.8f); b->strafe_dir = -b->strafe_dir; }
        /* only the sign: guards park a look angle in strafe_dir */
        V2 side = v2_scale(v2(-dir.y, dir.x), b->strafe_dir < 0 ? -1.0f : 1.0f);
        V2 mv;
        if (dist < want * 0.7f) mv = v2_add(dir, v2_scale(side, 0.5f));
        else if (dist > want * 1.3f) mv = v2_add(v2_scale(dir, -1), v2_scale(side, 0.3f));
        else mv = side;
        mv = v2_scale(v2_norm(mv), ARCH[a->arch].walk * 1.3f);
        V2 probe = v2_add(a->pos, v2_scale(v2_norm(mv), 10));
        if (solid_at(probe.x, probe.y)) mv = v2_scale(mv, -0.5f);
        a->vel = v2_add(a->vel, v2_scale(v2_sub(mv, a->vel), smooth_k(8, dt)));
        npc_attack(a, idx, t, dist, visible, dt);
        return;
    }
    /* melee: close in */
    float reach = w->range + t->radius;
    if (w->kind == WK_GUN) reach = 130;   /* empty gun, no ammo: get close enough to throw it */
    if (visible && dist < reach + 2) {
        a->vel = v2_scale(a->vel, 0.6f);
    } else {
        b->repath -= dt;
        /* straight at them only if nothing solid is in the way - counters and glass don't block sight */
        bool direct = visible && dist < 120 && walk_clear(a->pos, goal, a->radius * 0.8f);
        if (direct) {
            V2 d = v2_norm(v2_sub(goal, a->pos));
            a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(d, speed), a->vel), smooth_k(10, dt)));
            b->path_len = 0;
        } else {
            if (b->repath <= 0) {
                path_to(a, goal);
                b->repath = 0.45f;
            }
            if (follow(a, speed, dt) && !visible) {
                set_state(a, AI_SEARCH, frange(2, 3));
                return;
            }
        }
    }
    if (visible) npc_attack(a, idx, t, dist, visible, dt);
    else face_move(a, dt);
}

/* -------------------------------------------------------------- boss */
static void boss_update(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    Actor *p = player();
    float hpk = (float)a->hp / a->maxhp;
    int phase = hpk > 0.6f ? 0 : (hpk > 0.28f ? 1 : 2);
    if (phase != a->boss_phase) {
        a->boss_phase = phase;
        world_message(phase == 1 ? "THE KING IS ANGRY" : "LONG LIVE THE KING", COL_TAG);
        add_shake(8);
        play_at(SFX_ALERT, a->pos, 1, 0.6f);
        if (phase == 2) {
            Stack saw = {IT_CHAINSAW, 1, 100};
            if (a->weapon.id) drop_weapon(a, false);
            a->weapon = saw;
        }
        /* call the guard */
        for (int k = 0; k < 2 + phase; k++) {
            V2 sp = v2(a->pos.x + frange(-140, 140), a->pos.y + frange(40, 140));
            if (solid_at(sp.x, sp.y)) continue;
            int ni = actor_spawn(AR_PIG, sp);
            if (ni >= 0) {
                Stack c = {IT_CLEAVER, 1, 30};
                W.actors[ni].weapon = c;
                become_hostile(&W.actors[ni], ni, 0, true);
            }
        }
    }
    if (b->state == AI_GUARD) {
        face_towards(a, PI_F * 0.5f, 3, dt);
        float d = v2_dist(a->pos, p->pos);
        if (p->alive && d < 230 && los_clear(a->pos, p->pos, false)) boss_wake(a);
        return;
    }
    if (!p->alive) { a->vel = v2_scale(a->vel, 0.8f); return; }
    b->target = 0;
    float dist = v2_dist(a->pos, p->pos);
    bool vis = los_clear(a->pos, p->pos, false);
    float ang = v2_to_angle(v2_sub(p->pos, a->pos));
    a->boss_t -= dt;
    if (b->burst_t > 0) b->burst_t -= dt;
    if (a->reload_t > 0) a->reload_t -= dt;
    /* charge wind-up: roar, plant feet, then go */
    if (b->aim_t > 0) {
        b->aim_t -= dt;
        a->vel = v2_scale(a->vel, 0.7f);
        face_towards(a, ang, 10, dt);
        a->stun_t = 0;
        if (b->aim_t <= 0) {
            b->timer = 1.0f;
            play_at(SFX_SWING_HEAVY, a->pos, 1, 0.45f);
        }
        return;
    }
    /* charge attack */
    if (b->timer > 0) {
        b->timer -= dt;
        V2 d = v2_angle(a->face);
        a->vel = v2_scale(d, 175);
        if (dist < 16 && p->invuln <= 0) {
            damage_actor(0, idx, 3, d, 300, 1, W_SLEDGE, DMG_MELEE);
            b->timer = 0;
            add_shake(10);
        }
        if (solid_at(a->pos.x + d.x * 14, a->pos.y + d.y * 14)) {
            b->timer = 0;
            add_shake(10);
            a->down_t = 1.6f;   /* dazed on the floor: execute him! */
            a->push = v2_scale(d, -120);
            play_at(SFX_CART_HIT, a->pos, 1, 0.6f);
            play_at(SFX_BODYFALL, a->pos, 1, 0.7f);
            world_hint("He's down - SPACE!");
        }
        return;
    }
    if (a->stun_t > 0) { a->vel = v2(0, 0); return; }
    const WeaponDef *w = item_weapon(a->weapon.id);
    /* no straight run at the player: walk round whatever is in the way (with a gun, only when he can't see you -
       otherwise he keeps his distance and shoots over the planters) */
    bool direct = vis && walk_clear(a->pos, p->pos, a->radius * 0.8f);
    bool hunt = w->kind == WK_GUN ? !vis : !direct;
    b->repath -= dt;
    if (hunt) {
        if (b->repath <= 0) {
            /* nowhere he fits through: wait where he is */
            if (!path_to(a, p->pos)) { b->path_len = 0; b->goal = a->pos; }
            b->repath = 0.5f;
        }
        follow(a, ARCH[AR_BOSS].run, dt);
        if (!vis) { face_move(a, dt); return; }
    }
    face_towards(a, ang, 6, dt);
    if (a->boss_t <= 0 && vis) {
        int move = irange(0, phase >= 1 ? 2 : 1);
        if (move == 0 && dist > 60) {
            /* wind up, then charge */
            b->aim_t = 0.7f;
            a->face = ang;
            a->boss_t = frange(3.0f, 4.5f);
            alert_icon(a, 1, 0.8f);
            play_at(SFX_ALERT, a->pos, 1, 0.5f);
            return;
        }
        if ((move == 2 && dist > 70) || (phase >= 1 && dist > 90)) {   /* never at point blank: he'd burn too */
            Stack mol = {IT_MOLOTOV, 1, 0};
            throw_item(a, idx, mol, ang + frange(-0.15f, 0.15f), MINF(420, dist * 1.6f + 80));
            a->boss_t = frange(2.0f, 3.0f);
            return;
        }
        a->boss_t = frange(1.5f, 2.5f);
    }
    if (w->kind == WK_GUN) {
        if (a->weapon.cond <= 0 && a->reload_t <= 0) { a->weapon.cond = (int16_t)w->mag; a->reload_t = 1.4f; play_at(SFX_RELOAD, a->pos, 0.8f, 0.8f); }
        if (vis) {
            float want = 110;
            V2 dir = v2_norm(v2_sub(a->pos, p->pos));
            V2 mv = dist < want ? dir : (dist > want * 1.5f ? v2_scale(dir, -1) : v2(-dir.y, dir.x));
            a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(mv, ARCH[AR_BOSS].walk), a->vel), smooth_k(6, dt)));
        }
        if (vis && a->reload_t <= 0 && a->atk_cd <= 0 && b->burst_t <= 0 && fabsf(angle_diff(a->face, ang)) < 0.3f) {
            attack_begin(a, idx);
            if (++b->burst_n >= 2 + phase) { b->burst_n = 0; b->burst_t = frange(1.0f, 1.6f); }
        }
    } else {
        /* chainsaw rampage */
        if (direct) {
            V2 d = v2_norm(v2_sub(p->pos, a->pos));
            a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(d, ARCH[AR_BOSS].run), a->vel), smooth_k(6, dt)));
        }
        if (dist < 30 && a->atk_cd <= 0) attack_begin(a, idx);
        a->weapon.cond = 100;
    }
}

/* ---------------------------------------------------------------- thief */
/* sent by van_thieves (world.c) once the van's been left alone a while: walk in, rummage, grab the lot, run */
static void steal(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    Actor *p = player();
    float walk = ARCH[a->arch].walk * a->speed_mul, run = ARCH[a->arch].run * a->speed_mul;
    /* caught at it, or nothing left to take: forget it */
    if (p->alive && v2_dist(p->pos, a->pos) < 110 && los_clear(p->pos, a->pos, false)) {
        b->target = -1;
        set_state(a, AI_FLEE, frange(3, 5));
        path_to(a, flee_point(a, p->pos));
        alert_icon(a, 1, 0.8f);
        return;
    }
    if (!van_haul()) { set_state(a, AI_WANDER, 0); return; }
    V2 at = van_spot();
    if (v2_dist(a->pos, at) > 14) {
        b->wander_t += dt;
        b->repath -= dt;
        if (b->repath <= 0) { path_to(a, at); b->repath = 2.0f; }
        follow(a, (walk + run) * 0.5f, dt);
        face_move(a, dt);
        b->timer = 0;
        if (b->wander_t > 45) set_state(a, AI_WANDER, 0);   /* can't get there */
        return;
    }
    a->vel = v2_scale(a->vel, 0.6f);
    face_towards(a, v2_to_angle(v2_sub(W.van, a->pos)), 8, dt);
    if (b->timer == 0) play_at(SFX_VAN_DOOR, a->pos, 0.8f, 1.15f);
    b->timer += dt;
    if (fmodf(b->timer, 0.6f) < dt) play_at(SFX_LOOT_RUMMAGE, a->pos, 0.6f, frange(0.9f, 1.2f));
    if (b->timer > 2.5f) {
        van_rob(a, idx);
        b->target = -1;
        set_state(a, AI_GETAWAY, 0);
        path_to(a, flee_point(a, W.van));
        b->timer = 4;
    }
}

/* with your shopping: on the move round the level till somebody takes them down - flat out away from you when you're
   close, a jog from one end of the place to the other when you're not. Weighed down with it, so you can run them down */
static void getaway(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    Actor *p = player();
    float walk = ARCH[a->arch].walk * a->speed_mul, run = ARCH[a->arch].run * a->speed_mul;
    bool chased = p->alive && v2_dist(p->pos, a->pos) < 180 && los_clear(p->pos, a->pos, false);
    b->timer -= dt;
    b->repath -= dt;
    if (chased) {
        if (!b->aware) { alert_icon(a, 1, 0.8f); b->aware = true; }
        if (b->repath <= 0 || b->path_i >= b->path_len) { path_to(a, flee_point(a, p->pos)); b->repath = 0.6f; }
        follow(a, run * 0.8f, dt);
    } else {
        b->aware = false;
        if (b->timer <= 0 || b->path_i >= b->path_len) {
            V2 g = random_floor_spot(&g_rng, chance(0.5f), 220, a->pos);
            b->timer = path_to(a, g) ? 14 : 0.5f;
        }
        follow(a, (walk + run) * 0.5f, dt);
    }
    face_move(a, dt);
}


/* -------------------------------------------------------------- update */
void ai_update(Actor *a, int idx, float dt) {
    if (!a->alive || a->down_t > 0) return;
    if (is_animal(a)) { animal_update(a, idx, dt); return; }
    if (is_plant(a)) { plant_update(a, idx, dt); return; }
    Brain *b = &a->br;
    attack_update(a, idx, dt);
    if (a->arch == AR_BOSS) { boss_update(a, idx, dt); return; }
    if (a->stun_t > 0) { a->vel = v2_scale(a->vel, 0.8f); return; }
    float walk = ARCH[a->arch].walk * a->speed_mul;
    float run = ARCH[a->arch].run * a->speed_mul;

    /* burning: panic */
    if (a->burn_t > 0 && a->arch != AR_BRUTE) {
        b->wander_t -= dt;
        if (b->wander_t <= 0) {
            b->wander_t = 0.4f;
            b->goal = v2_add(a->pos, v2(frange(-60, 60), frange(-60, 60)));
        }
        V2 d = v2_norm(v2_sub(b->goal, a->pos));
        a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(d, run), a->vel), smooth_k(8, dt)));
        face_move(a, dt);
        return;
    }
    if (is_crew(a)) { crew_update(a, idx, dt); return; }
    if (b->state == AI_STEAL) { steal(a, idx, dt); return; }
    if (b->state == AI_GETAWAY) { getaway(a, idx, dt); return; }

    /* perception */
    b->think -= dt;
    if (b->think <= 0) {
        b->think = 0.12f + frand() * 0.06f;
        if (a->spawn_grace <= 0 && b->state != AI_SURRENDER) {
            if (a->temper == TEMP_TIMID || (a->faction == FAC_SCAV && !has_grudge(a, 0))) {
                Actor *p = player();
                /* a gun pointed at a timid scavenger: hands up */
                const WeaponDef *pw = item_weapon(p->weapon.id);
                float pd = v2_dist(p->pos, a->pos);
                if (p->alive && pw->kind == WK_GUN && pd < 150 && b->state != AI_FLEE && a->temper == TEMP_TIMID &&
                    fabsf(angle_diff(p->face, v2_to_angle(v2_sub(a->pos, p->pos)))) < 0.22f && los_clear(p->pos, a->pos, false)) {
                    set_state(a, AI_SURRENDER, 4.0f);
                    alert_icon(a, 3, 4.0f);
                    play_at(SFX_SURRENDER, a->pos, 0.95f, frange(0.95f, 1.1f));
                    for (int i = 0; i < a->ninv; i++) pickup_spawn(a->inv[i], a->pos, v2(frange(-50, 50), frange(-50, 50)));
                    a->ninv = 0;
                    if (a->weapon.id) drop_weapon(a, false);
                    a->vel = v2(0, 0);
                    return;
                }
                int s = find_scary(a, idx);
                if (s >= 0 && b->state != AI_FLEE) {
                    if (a->temper == TEMP_DEFENSIVE && has_grudge(a, s)) become_hostile(a, idx, s, true);
                    else {
                        set_state(a, AI_FLEE, frange(3, 5));
                        path_to(a, flee_point(a, W.actors[s].pos));
                        alert_icon(a, 1, 0.8f);
                    }
                }
                if (a->temper == TEMP_DEFENSIVE) {
                    float d;
                    int t = find_threat(a, idx, &d);
                    if (t >= 0 && b->state != AI_CHASE) become_hostile(a, idx, t, true);
                }
            } else if (b->state != AI_CHASE) {
                float d;
                int t = find_threat(a, idx, &d);
                if (t >= 0) become_hostile(a, idx, t, true);
            } else {
                /* already fighting: switch to a much closer threat */
                float d;
                int t = find_threat(a, idx, &d);
                if (t >= 0 && t != b->target && b->target >= 0 && d < v2_dist(a->pos, W.actors[b->target].pos) * 0.5f) {
                    b->target = t;
                }
            }
        }
    }

    switch (b->state) {
    case AI_IDLE:
        a->vel = v2_scale(a->vel, 0.85f);
        b->timer -= dt;
        if (chance(dt * 0.6f)) b->goal = v2_add(a->pos, v2_angle(frange(-PI_F, PI_F)));
        face_towards(a, v2_to_angle(v2_sub(b->goal, a->pos)), 2, dt);
        if (b->timer <= 0) set_state(a, AI_WANDER, 0);
        break;
    case AI_GUARD:
        a->vel = v2_scale(a->vel, 0.85f);
        if (v2_dist(a->pos, b->home) > 24) {
            if (b->repath <= 0) { path_to(a, b->home); b->repath = 1.0f; }
            b->repath -= dt;
            follow(a, walk, dt);
            face_move(a, dt);
        } else {
            b->timer -= dt;
            if (b->timer <= 0) { b->timer = frange(1.5f, 4); b->strafe_dir = frange(-PI_F, PI_F); }
            face_towards(a, b->strafe_dir, 2, dt);
        }
        break;
    case AI_WANDER: {
        if (b->path_len == 0 && b->timer <= 0) {
            /* follow the squad leader */
            if (b->leader >= 0 && W.actors[b->leader].alive) {
                Actor *l = &W.actors[b->leader];
                if (l->br.state == AI_CHASE && l->br.target >= 0) { become_hostile(a, idx, l->br.target, true); break; }
                V2 g = v2_add(l->pos, v2(frange(-24, 24), frange(-24, 24)));
                path_to(a, g);
                b->timer = 0.8f;
            } else if (ARCH[a->arch].loots && chance(0.6f)) {
                /* go shopping */
                int best = -1;
                float bd = 1e9f;
                for (int k = 0; k < 8 && W.nconts > 0; k++) {
                    int ci = irange(0, W.nconts - 1);
                    if (ci == b->container) continue;   /* the last one: looted, or we couldn't get to it */
                    Container *c = &W.conts[ci];
                    if (c->dead || c->searched || c->n == 0) continue;
                    float d = v2_dist(c->pos, a->pos);
                    if (d < bd && d < 420) { bd = d; best = ci; }
                }
                if (best >= 0) {
                    b->container = best;
                    set_state(a, AI_LOOT, 0);
                    b->wander_t = 0;
                    if (!path_to(a, W.conts[best].pos)) set_state(a, AI_WANDER, 0);
                    break;
                }
                V2 g = random_floor_spot(&g_rng, cell_flag(tile_of(a->pos.x), tile_of(a->pos.y), CF_INDOOR), 0, a->pos);
                if (v2_dist(g, a->pos) < 350) path_to(a, g);
                b->timer = 1;
            } else {
                V2 g = v2_add(a->pos, v2(frange(-140, 140), frange(-140, 140)));
                if (walkable_tile(tile_of(g.x), tile_of(g.y))) path_to(a, g);
                b->timer = 0.5f;
            }
        }
        b->timer -= dt;
        if (b->path_len > 0) {
            b->wander_t += dt;
            if (follow(a, walk, dt) || b->wander_t > 10 || (b->path_i >= b->path_len && v2_dist(a->pos, b->goal) < 20)) {
                b->path_len = 0;
                b->wander_t = 0;
                set_state(a, AI_IDLE, frange(1.0f, 3.5f));
            }
            face_move(a, dt);
        } else {
            a->vel = v2_scale(a->vel, 0.8f);
        }
        break;
    }
    case AI_LOOT: {
        Container *c = b->container >= 0 ? &W.conts[b->container] : NULL;
        if (!c || (c->searched && c->n == 0)) { b->wander_t = 0; set_state(a, AI_WANDER, 0); break; }
        float cd = v2_dist(a->pos, c->pos);
        /* cars, dumpsters and desks are big: their centre is further from where you can stand */
        float reach = c->prop >= 0 ? 36 : 24;
        if (cd < reach) {
            a->vel = v2_scale(a->vel, 0.6f);
            face_towards(a, v2_to_angle(v2_sub(c->pos, a->pos)), 8, dt);
            b->timer += dt;
            if (fmodf(b->timer, 0.6f) < dt) play_at(SFX_LOOT_RUMMAGE, a->pos, 0.6f, frange(0.9f, 1.2f));
            if (b->timer > 1.6f) {
                search_container(a, b->container, false);
                b->wander_t = 0;
                set_state(a, AI_IDLE, frange(0.5f, 1.5f));
            }
        } else {
            /* can't get there (no path, path ends far off, or taking forever): give up on it -
               b->container stays set so it isn't picked again straight away */
            b->wander_t += dt;
            bool lost = b->wander_t > 15;
            b->repath -= dt;
            if (b->repath <= 0 && b->path_len == 0) {
                if (!path_to(a, c->pos)) lost = true;
                b->repath = 1.5f;
            }
            if (follow(a, walk, dt)) {
                if (cd > reach + 12) lost = true;
                else a->vel = v2_scale(v2_norm(v2_sub(c->pos, a->pos)), walk);   /* shuffle the last bit */
            }
            face_move(a, dt);
            b->timer = 0;
            if (lost) { b->wander_t = 0; set_state(a, AI_WANDER, 0); }
        }
        break;
    }
    case AI_INVESTIGATE:
        b->repath -= dt;
        b->wander_t += dt;
        if (b->repath <= 0 && b->path_len == 0) { path_to(a, b->goal); b->repath = 2.0f; }
        if (follow(a, (walk + run) * 0.5f, dt) || b->wander_t > 9 ||
            (b->path_i >= b->path_len && v2_dist(a->pos, b->goal) < 20)) {
            b->wander_t = 0;
            set_state(a, AI_SEARCH, frange(2, 3.5f));
        }
        face_move(a, dt);
        break;
    case AI_SEARCH:
        a->vel = v2_scale(a->vel, 0.85f);
        b->timer -= dt;
        a->face += sinf(W.time * 2 + idx) * dt * 2.5f;
        if (a->alert_icon_t <= 0.1f) alert_icon(a, 2, 0.3f);
        if (b->timer <= 0) { b->aware = false; set_state(a, AI_WANDER, 0); }
        break;
    case AI_CHASE:
        chase(a, idx, dt);
        break;
    case AI_FLEE:
        b->timer -= dt;
        if (b->path_len == 0 || b->path_i >= b->path_len) {
            V2 from = b->target >= 0 ? W.actors[b->target].pos : player()->pos;
            path_to(a, flee_point(a, from));
        }
        follow(a, run, dt);
        face_move(a, dt);
        if (b->timer <= 0) {
            if (a->temper != TEMP_TIMID && b->target >= 0 && W.actors[b->target].alive) become_hostile(a, idx, b->target, false);
            else set_state(a, AI_WANDER, 0);
        }
        break;
    case AI_SURRENDER:
        a->vel = v2(0, 0);
        b->timer -= dt;
        face_towards(a, v2_to_angle(v2_sub(player()->pos, a->pos)), 6, dt);
        if (b->timer <= 0) {
            set_state(a, AI_FLEE, 4);
            path_to(a, flee_point(a, player()->pos));
            alert_icon(a, 0, 0);
        }
        break;
    default:
        break;
    }
}
