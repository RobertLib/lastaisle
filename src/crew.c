/* LAST AISLE - the crew: the people from the Greenhouse you take along (CREW in data.c, asked at the camp in hub.c).
 * In the store they work on their own: each goes and looks round a bit of the store near you, away from the others, and
 * takes on any raider, Pig or feral they come across - and whoever is after you, which they'll hear round a corner, or a
 * thief running off with your shopping. Too far from you, badly hurt, or the list done: they come back to you.
 * Your swings, shots and throws pass them by (and theirs pass you); blasts and fire burn anybody.
 * Dead is dead: crew_killed writes it into the run, and with the last of them gone nobody holds the gate (screens.c). */
#include "world.h"
#include "gfx.h"
#include "audio.h"
#include "game.h"

bool allied(int a, int b) {
    if (a < 0 || b < 0 || a >= W.nactors || b >= W.nactors || a == b) return false;
    return (a == 0 || is_crew(&W.actors[a])) && (b == 0 || is_crew(&W.actors[b]));
}

int crew_alive(void) {
    int n = 0;
    for (int k = 0; k < MAX_CREW; k++) n += RUN.crew[k] != CR_DEAD;
    return n;
}

int crew_squad(void) {
    int n = 0;
    for (int k = 0; k < MAX_CREW; k++) n += RUN.crew[k] == CR_SQUAD;
    return n;
}

/* ------------------------------------------------------------------ getting out of the van */
void crew_spawn(void) {
    Actor *p = player();
    int n = crew_squad(), m = 0;
    for (int k = 0; k < MAX_CREW; k++) {
        W.crew_actor[k] = -1;
        if (RUN.crew[k] != CR_SQUAD) continue;
        const CrewDef *c = &CREW[k];
        /* round you, a step behind */
        V2 at = p->pos;
        for (int t = 0; t < 16; t++) {
            float ang = p->face + PI_F + (m - (n - 1) * 0.5f) * 0.8f + (t / 2) * 0.5f * (t & 1 ? -1 : 1);
            V2 g = v2_add(p->pos, v2_scale(v2_angle(ang), 20 + (t / 6) * 8));
            if (walkable_tile(tile_of(g.x), tile_of(g.y)) && walk_clear(p->pos, g, 5)) { at = g; break; }
        }
        int i = actor_spawn(c->arch, at);
        if (i < 0) continue;
        Actor *a = &W.actors[i];
        Stack w = {(int16_t)c->weapon, 1, 0, (int16_t)c->mods};
        WeaponDef wd = weapon_stats(&w);
        w.cond = (int16_t)(wd.kind == WK_GUN ? wd.mag : wd.durability);
        a->weapon = w;
        if (c->ammo.id) a->inv[a->ninv++] = (Stack){(int16_t)c->ammo.id, (int16_t)c->ammo.n, 0, 0};
        a->face = p->face;
        a->spawn_grace = 0;
        a->br.state = AI_IDLE;   /* a moment by the van, then off to look round */
        a->br.timer = frange(0.8f, 2.5f);
        a->br.fear = a->face;
        a->br.wander_t = p->face + PI_F;   /* the side of you they keep to when they're with you */
        W.crew_actor[k] = i;
        m++;
        SDL_Log("CREW: %s gets out of the van (%s, %d hp)", ARCH[c->arch].name, ITEMS[c->weapon].name, a->hp);
    }
}

/* ------------------------------------------------------------------ in the store */
static bool grudge_on(const Actor *a, int who) {
    for (int k = 0; k < 4; k++) if (a->br.grudge[k] == who) return true;
    return false;
}

static void face_move(Actor *a, float dt) {
    if (v2_len(a->vel) > 8) face_towards(a, v2_to_angle(a->vel), 8, dt);
}

static bool has_ammo(Actor *a) {
    const WeaponDef *w = item_weapon(a->weapon.id);
    return w->kind == WK_GUN && a->weapon.cond + inv_count(a, w->ammo) > 0;
}

/* badly hurt (half gone - out there a second pistol round finishes anybody): they call it in and come back to you, and
   start nothing new on the way - with a gun they still shoot whoever's after you or them, with anything else they only
   fight whoever is right on top of them */
static bool hurt_bad(const Actor *a) { return a->hp <= MAXF(2, a->maxhp / 2); }
bool crew_badly_hurt(const Actor *a) { return is_crew(a) && a->alive && hurt_bad(a); }

/* how far they go from you: a bit of the store round you of their own, never the far end of it */
#define PATROL_NEAR 90       /* where they go and look round: this far from you... */
#define PATROL_FAR 240       /* ...to this far */
#define LEASH 380            /* further than this from you and they come back */
#define FIGHT_LEASH 480      /* a fight this far from you is none of theirs */

/* who to go for, on their own: any raider, Pig, feral or rabid stray they see (and anybody with a grudge on them),
   whoever is after you or the crew - you first, and a fight on you they hear even round a corner - and a thief with your
   shopping. Not the scavengers minding their own business, not a weed unless it got them. Badly hurt and with nothing
   to shoot: only whoever is right on top of them. */
static int crew_threat(Actor *a, int idx) {
    Actor *p = player();
    bool gun = has_ammo(a);
    bool cornered_only = hurt_bad(a) && !gun;
    float view = ARCH[a->arch].view * (W.def->amb == AMB_NIGHT || W.def->amb == AMB_INFERNO ? 0.8f : 1.0f);
    int best = -1;
    float bs = 1e9f;
    for (int j = 1; j < W.nactors; j++) {
        Actor *t = &W.actors[j];
        if (j == idx || !t->used || !t->alive || is_crew(t) || t->br.state == AI_SURRENDER) continue;
        if (t->down_t > 0 && gun) continue;   /* shots go over the fallen */
        int tt = t->br.target;
        bool after_us = t->br.state == AI_CHASE && tt >= 0 && tt < W.nactors && (tt == 0 || is_crew(&W.actors[tt]));
        bool after_you = after_us && tt == 0;
        bool thief = t->br.state == AI_GETAWAY;
        bool foe = actor_hostile(idx, j);   /* the factions you're at war with, rabid strays, whoever hurt them */
        if (!after_us && !thief && !foe) continue;
        if (is_plant(t) && plant_rooted(t) && !gun && !grudge_on(a, j)) continue;   /* no walking into a flowerbed with a bat */
        float d = v2_dist(a->pos, t->pos);
        if (cornered_only && (d > 34 || tt != idx)) continue;
        /* picking a fight of their own: not hurt, and with a blade or a bat not with a brute, nor the length of the store away */
        bool own = !after_us && !thief && !grudge_on(a, j);
        if (own && (hurt_bad(a) || (!gun && (ARCH[t->arch].heavy || d > 160)))) continue;
        if (p->alive && v2_dist(p->pos, t->pos) > FIGHT_LEASH) continue;
        bool sees = d < view && los_clear(a->pos, t->pos, false);
        bool hears = after_you && d < 360 && !cornered_only;   /* somebody's on you: they come running */
        if (!sees && !hears) continue;
        float score = d - (after_you ? 120 : 0) - (after_us ? 30 : 0) - (grudge_on(a, j) ? 50 : 0) + (sees ? 0 : 60);
        if (score < bs) { bs = score; best = j; }
    }
    return best;
}

void crew_on_hurt(Actor *a, int idx, int attacker) {
    if (attacker < 0 || attacker >= W.nactors || allied(idx, attacker)) return;   /* your molotov: they'll live with it, maybe */
    if (!W.actors[attacker].alive) return;
    if (!grudge_on(a, attacker)) {
        int slot = 3;
        for (int k = 0; k < 4; k++) if (a->br.grudge[k] < 0) { slot = k; break; }
        a->br.grudge[slot] = attacker;
    }
    if (a->br.target < 0) {
        a->br.target = attacker;
        a->br.reaction = 0.1f;
        a->br.aim_t = 0;
    }
    a->alert_icon = 1;
    a->alert_icon_t = 0.8f;
    /* out there on their own, badly hurt: they say so (once), and make their way back to you */
    if (a->hp > 0 && hurt_bad(a) && a->br.bark_t <= 0) {
        a->br.bark_t = 1;
        SDL_snprintf(W.radio, sizeof W.radio, "%s: I'm hit bad - coming back to you.", ARCH[a->arch].name);
        W.radio_t = 4;
        audio_play(SFX_RADIO, 0.6f, 0, 1.1f);
        SDL_Log("CREW: %s badly hurt (%d hp), falls back", ARCH[a->arch].name, a->hp);
    }
}

/* as fast as you, when they're falling behind - nobody gets left in the store because their legs are short */
static float catch_up_speed(Actor *a) {
    float you = ARCH[AR_PLAYER].run * (RUN.perks[PK_LIGHTFEET] ? 1.12f : 1.0f) * (1.0f + 0.03f * train_level(STAT_FIT));
    return MAXF(ARCH[a->arch].run * a->speed_mul, you * 1.05f);
}

/* their place by you when they stay close: behind where you look, fanned out - never in your line of fire */
static V2 crew_slot(Actor *a, int idx, float *spread_out) {
    Actor *p = player();
    int n = 0, me = 0;
    for (int k = 0; k < MAX_CREW; k++) {
        int i = W.crew_actor[k];
        if (i < 0 || !W.actors[i].alive) continue;
        if (i == idx) me = n;
        n++;
    }
    float spread = (me - (n - 1) * 0.5f) * 0.75f;
    *spread_out = spread;
    for (float r = 32; r >= 16; r -= 16) {
        V2 g = v2_add(p->pos, v2_scale(v2_angle(a->br.wander_t + spread), r));
        if (walkable_tile(tile_of(g.x), tile_of(g.y)) && walk_clear(p->pos, g, a->radius)) return g;
    }
    return p->pos;
}

static void steer_to(Actor *a, V2 g, float speed, float dt) {
    Brain *b = &a->br;
    b->repath -= dt;
    if (los_clear(a->pos, g, false) && walk_clear(a->pos, g, a->radius * 0.8f)) {
        V2 dir = v2_norm(v2_sub(g, a->pos));
        a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(dir, speed), a->vel), smooth_k(10, dt)));
        b->path_len = b->path_i = 0;
    } else {
        if (b->repath <= 0 || b->path_i >= b->path_len) { path_to(a, g); b->repath = 0.5f; }
        follow(a, speed, dt);
    }
}

/* back with you: catching up (alerted_by_noise: on the way), then a step or two behind */
static void stay_close(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    Actor *p = player();
    b->wander_t = lerp_angle(b->wander_t, p->face + PI_F, smooth_k(1.2f, dt));
    float spread;
    V2 g = crew_slot(a, idx, &spread);
    float d = v2_dist(a->pos, p->pos), gd = v2_dist(a->pos, g);
    bool sees = d < 300 && los_clear(a->pos, p->pos, false);
    bool *running = &b->alerted_by_noise;
    if (!*running && (d > 80 || (!sees && d > 36))) { *running = true; b->repath = 0; b->path_len = 0; }
    if (*running && d < 52 && sees) *running = false;
    if (*running) {
        float run = catch_up_speed(a);
        steer_to(a, g, d > 120 ? run * 1.1f : run * 0.95f, dt);
        face_move(a, dt);
        return;
    }
    if (gd > 12 && walk_clear(a->pos, g, a->radius * 0.8f)) {
        V2 dir = v2_norm(v2_sub(g, a->pos));
        float sp = MINF(ARCH[a->arch].walk * 1.4f, gd * 3);
        a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(dir, sp), a->vel), smooth_k(8, dt)));
    } else {
        a->vel = v2_scale(a->vel, expf(-10 * dt));
    }
    face_towards(a, p->face + spread * 0.4f, 4, dt);   /* eyes out the way you're looking */
}

/* somewhere of their own to go and look round: in your part of the store, away from where the others are going, indoors
   if it can be - the aisles are where the trouble is */
static bool pick_patrol(Actor *a, int idx) {
    Actor *p = player();
    V2 best = a->pos;
    float bs = -1e9f;
    for (int t = 0; t < 16; t++) {
        V2 g = v2_add(p->pos, v2_scale(v2_angle(frange(-PI_F, PI_F)), frange(PATROL_NEAR, PATROL_FAR)));
        int tx = tile_of(g.x), ty = tile_of(g.y);
        if (!in_map(tx, ty) || !walkable_tile(tx, ty) || !cell(tx, ty)->reach || cell_flag(tx, ty, CF_EXIT)) continue;
        float sc = MINF(v2_dist(g, a->pos), 200) * 0.4f + (cell_flag(tx, ty, CF_INDOOR) ? 50 : 0);
        for (int k = 0; k < MAX_CREW; k++) {
            int i = W.crew_actor[k];
            if (i < 0 || i == idx || !W.actors[i].alive) continue;
            const Actor *o = &W.actors[i];
            sc += MINF(v2_dist(g, o->br.state == AI_WANDER ? o->br.goal : o->pos), 160);
        }
        if (sc > bs) { bs = sc; best = g; }
    }
    if (bs < -1e8f || !path_to(a, best)) return false;
    a->br.state = AI_WANDER;
    a->br.suspicion = 0;   /* time on the way there */
    return true;
}

/* lost their weapon (it broke, or they threw the empty gun): the nearest one lying about will do */
static bool rearm(Actor *a, float dt) {
    if (a->weapon.id) return false;
    int best = -1;
    float bd = 90 * 90;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &W.pickups[i];
        if (!pk->alive || pk->flying || pk->fuse > 0 || pk->dropped || ITEMS[pk->st.id].cat != CAT_WEAPON) continue;
        const WeaponDef *w = item_weapon(pk->st.id);
        if (w->kind == WK_THROWN || ((w->kind == WK_GUN || w->kind == WK_CHAINSAW || w->kind == WK_FLAME) && pk->st.cond <= 0)) continue;
        float d = v2_dist2(pk->pos, a->pos);
        if (d < bd && los_clear(a->pos, pk->pos, false)) { bd = d; best = i; }
    }
    if (best < 0) return false;
    Pickup *pk = &W.pickups[best];
    if (v2_dist(a->pos, pk->pos) < 10) {
        a->weapon = pk->st;
        pk->alive = false;
        play_at(SFX_PICKUP_WEAPON, a->pos, 0.8f, 1);
        return true;
    }
    steer_to(a, pk->pos, ARCH[a->arch].run * a->speed_mul, dt);
    face_move(a, dt);
    return true;
}

static void fight(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    Actor *t = &W.actors[b->target];
    float dist = v2_dist(a->pos, t->pos);
    bool visible = dist < 300 && los_clear(a->pos, t->pos, false);
    /* in sight - or out of it but on you, and they go by the noise */
    if (visible || (t->br.state == AI_CHASE && t->br.target == 0)) b->last_seen = t->pos;
    const WeaponDef *w = item_weapon(a->weapon.id);
    if (has_ammo(a) && visible) {
        /* keep a comfortable distance and strafe, like anybody with a gun */
        float want = w->pellets > 1 ? 70 : 110;
        V2 dir = v2_norm(v2_sub(a->pos, t->pos));
        b->strafe_t -= dt;
        if (b->strafe_t <= 0) { b->strafe_t = frange(0.8f, 1.8f); b->strafe_dir = -b->strafe_dir; }
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
    float reach = w->range + t->radius;
    if (w->kind == WK_GUN) reach = 130;   /* empty: close enough to throw it */
    if (visible && dist < reach + 2) a->vel = v2_scale(a->vel, 0.6f);
    else steer_to(a, b->last_seen, ARCH[a->arch].run * a->speed_mul, dt);
    if (visible) npc_attack(a, idx, t, dist, visible, dt);
    else face_move(a, dt);
}

/* AI_WANDER: on the way to a spot of their own, AI_IDLE: looking round there, AI_GUARD: back with you (too far off, badly
   hurt, or the list is done and it's time to go) */
void crew_update(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    Actor *p = player();
    bool fought = b->target >= 0;
    if (b->reaction > 0) b->reaction -= dt;
    if (b->burst_t > 0) b->burst_t -= dt;
    if (a->reload_t > 0) a->reload_t -= dt;
    if (b->target >= 0 && (!W.actors[b->target].used || !W.actors[b->target].alive)) b->target = -1;
    b->think -= dt;
    if (b->think <= 0) {
        b->think = 0.15f + frand() * 0.05f;
        int t = crew_threat(a, idx);
        if (t >= 0 && t != b->target) {
            if (b->target < 0) {
                a->alert_icon = 1;
                a->alert_icon_t = 0.9f;
            }
            b->target = t;
            b->reaction = ARCH[a->arch].reaction * frange(0.8f, 1.2f);
            b->aim_t = 0;
            b->last_seen = W.actors[t].pos;
        } else if (t < 0 && b->target >= 0) {
            /* out of sight and nothing to do with you, gone too far, or they're in no state for it */
            Actor *o = &W.actors[b->target];
            bool still = grudge_on(a, b->target) && v2_dist(o->pos, a->pos) < 260 && (!p->alive || v2_dist(o->pos, p->pos) < FIGHT_LEASH) &&
                         !(hurt_bad(a) && !has_ammo(a));
            if (!still) b->target = -1;
        }
    }
    if (b->target >= 0 && p->alive && v2_dist(a->pos, p->pos) > FIGHT_LEASH) b->target = -1;
    if (fought && b->target < 0 && b->state != AI_GUARD) {   /* over: a look round before they move on */
        b->state = AI_IDLE;
        b->timer = frange(0.8f, 2.0f);
        b->fear = a->face;
    }
    bool close = b->target >= 0 && v2_dist(W.actors[b->target].pos, a->pos) < 50;
    if (!close && rearm(a, dt)) return;
    if (b->target >= 0) { fight(a, idx, dt); return; }
    /* back to you: too far off, badly hurt, or the list is done and it's time to go home */
    float d = v2_dist(a->pos, p->pos);
    bool with_you = p->alive && (hurt_bad(a) || W.list_done);
    if (p->alive && (with_you || d > LEASH) && b->state != AI_GUARD) {
        b->state = AI_GUARD;
        b->path_len = b->path_i = 0;
        b->repath = 0;
        b->alerted_by_noise = true;
    }
    switch (b->state) {
    case AI_GUARD:
        if (!with_you && d < 150 && los_clear(a->pos, p->pos, false)) { b->state = AI_IDLE; b->timer = frange(0.5f, 1.5f); b->fear = a->face; break; }
        stay_close(a, idx, dt);
        break;
    case AI_WANDER: {
        float sp = (ARCH[a->arch].walk + ARCH[a->arch].run) * 0.5f * a->speed_mul;
        b->suspicion += dt;
        if (follow(a, sp, dt) || b->suspicion > 14 || (b->path_i >= b->path_len && v2_dist(a->pos, b->goal) < 16)) {
            b->state = AI_IDLE;
            b->timer = frange(2.0f, 5.0f);
            b->fear = a->face;
        }
        face_move(a, dt);
        break;
    }
    default:   /* AI_IDLE */
        a->vel = v2_scale(a->vel, expf(-8 * dt));
        b->timer -= dt;
        if (chance(dt * 0.7f)) b->fear = a->face + frange(-1.6f, 1.6f);   /* fear: where they're looking */
        face_towards(a, b->fear, 3, dt);
        if (b->timer <= 0 && !pick_patrol(a, idx)) b->timer = 1;
        break;
    }
}

/* ------------------------------------------------------------------ dead for good */
void crew_killed(int idx) {
    Actor *a = &W.actors[idx];
    int k = crew_index(a);
    if (k < 0 || W.hub || RUN.crew[k] == CR_DEAD) return;
    RUN.crew[k] = CR_DEAD;
    RUN.crew_fell[k] = (uint8_t)W.level;
    W.crew_actor[k] = -1;
    char buf[48];
    SDL_snprintf(buf, sizeof buf, "%s IS DEAD", ARCH[a->arch].name);
    for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
    world_message(buf, COL_RED);
    slowmo(0.8f);
    int left = crew_alive();
    int by = a->last_hit_by;
    SDL_Log("CREW: %s died on level %d at %.0fs, killed by %s (%d of the crew left)", ARCH[a->arch].name, W.level + 1, W.time,
            by == 0 ? "you" : by > 0 && by < W.nactors ? ARCH[W.actors[by].arch].name : "the world", left);
    if (left == 0) {
        W.crew_wiped = true;
        W.crew_wipe_t = 0;
        SDL_strlcpy(W.radio, "...Rosa? Is anybody... nobody's on the gate. Nobody's left.", sizeof W.radio);
        W.radio_t = 5;
        audio_play(SFX_RADIO, 0.8f, 0, 0.9f);
    }
    if (RUN.mode == MODE_ROGUE) run_save();   /* written down at once: quitting doesn't bring them back */
}

/* ------------------------------------------------------------------ drawing */
/* a green tick over each of yours (red while they're getting hurt), their name for the first moments in the store */
void crew_draw_markers(void) {
    if (W.hub) return;
    for (int k = 0; k < MAX_CREW; k++) {
        int i = W.crew_actor[k];
        if (i < 0) continue;
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive || !is_crew(a)) continue;
        if (a->alert_icon_t > 0 && a->alert_icon) continue;   /* the '!' is up there */
        float x = floorf(a->pos.x), y = floorf(a->pos.y - 15 + sinf(W.time * 3 + k) * 0.8f);
        Color c = a->hurt_t > 0 ? COL_RED : COL_GREEN;
        gfx_fill(x - 2, y, 5, 1, c);
        gfx_fill(x - 1, y + 1, 3, 1, c);
        gfx_fill(x, y + 2, 1, 1, c);
        if (W.time < 6 || a->down_t > 0)
            gfx_text(FONT_SMALL, ARCH[a->arch].name, x, y - 10, a->down_t > 0 ? COL_RED : COL_RECEIPT, TXT_CENTER | TXT_OUTLINE);
    }
}
