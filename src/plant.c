/* LAST AISLE - mutated vegetation. Three winters with nobody weeding: the flowerbeds spit at you, the nettles turn
 * to follow you past and drag themselves after you to sting, and some of the bushes have taken to walking. */
#include "world.h"
#include "gfx.h"
#include "audio.h"

typedef struct {
    float sense;       /* wakes for whoever comes this close (the locals have to come a good deal closer) */
    float reach;       /* spits / lashes at whoever is this close (the lash sprite reaches this far) */
    float windup;      /* the tell before it strikes, s */
    float recover;     /* between strikes, s */
    float turn;        /* how quickly its head follows them */
    float leash;       /* a nettle creeps after its prey, never further than this from where it grew */
    float mouth;       /* pivot to where the spit leaves / the lash starts, px */
    float size;        /* sap, rustle and shadow (1 = a rambler) */
} Kind;

static const Kind KINDS[] = {
    /*             sense reach windup recover turn leash mouth size */
    /* spitter */ {150,  140,  0.55f, 1.9f,   5,   0,    10,   0.6f},
    /* nettle  */ {88,   32,   0.34f, 0.9f,   9,   80,   3,    0.7f},
    /* rambler */ {80,   18,   0.30f, 0.9f,   8,   0,    8,    1.0f},
};
_Static_assert(AR_RAMBLER - AR_SPITTER + 1 == ARRAY_LEN(KINDS), "plant.c indexes KINDS by archetype, AR_SPITTER first");

#define SPIT_SPEED 170.0f
#define REST_SENSE 44.0f   /* a rambler dug in only notices you once you're practically standing in it */

static const char *const HINTS[] = {
    "That flower is swelling up. Step aside when it spits!",
    "Nettles lash further than you can swing. Let it miss, then go in.",
    "That bush just moved. It's slower than you are.",
};
static uint8_t hinted;   /* kinds the player has been warned about (once a session is plenty) */

static const Kind *kind(const Actor *a) { return &KINDS[a->arch - AR_SPITTER]; }

/* the walking kind, and the one that never moves at all */
static bool walks(const Actor *a) { return a->arch == AR_RAMBLER; }
static bool fixed(const Actor *a) { return !walks(a) && kind(a)->leash <= 0; }

/* a nettle's roots are where it grew: dragged off after somebody, it's loose until it has crept back */
bool plant_rooted(const Actor *a) {
    if (walks(a)) return a->br.state == AI_REST;
    return fixed(a) || (a->br.state != AI_CHASE && v2_dist2(a->pos, a->br.home) < 4 * 4);
}

/* nothing solid all round: a plant dug in here doesn't plug a gap (it won't be shoved aside) */
static bool open_ground(V2 p) {
    int tx = tile_of(p.x), ty = tile_of(p.y);
    for (int y = ty - 1; y <= ty + 1; y++)
        for (int x = tx - 1; x <= tx + 1; x++)
            if (!in_map(x, y) || (cell(x, y)->flags & (CF_SOLID | CF_DOOR))) return false;
    return true;
}

/* leaves and twigs: smaller plants rustle higher */
static void rustle(Actor *a, float vol) {
    float s = kind(a)->size;
    play_at(SFX_RUSTLE, a->pos, vol * (0.6f + 0.4f * s), (1.3f - 0.3f * s) * frange(0.9f, 1.12f));
}

/* torn leaves flutter down and stay where they land */
static void shed(V2 pos, V2 dir, int n) {
    for (int i = 0; i < n; i++) {
        V2 v = v2_add(v2_scale(v2_norm(dir), frange(30, 110)), v2(frange(-60, 60), frange(-60, 60)));
        Particle *p = particle_add(PT_DEBRIS, pos, v, frange(0.6f, 1.1f));
        if (!p) return;
        p->spr = SPR_FX_LEAF + irange(0, SPR_FX_LEAF_N - 1);
        p->z = frange(3, 8);
        p->vz = frange(30, 80);
        p->angle = frange(-PI_F, PI_F);
        p->spin = frange(-12, 12);
        p->bake = true;
    }
}

void plant_setup(Actor *a, Rng *r) {
    a->coat = (uint8_t)(rng_int(r, 2) | rng_int(r, 4) << 1);   /* which leaf rosette, and a quarter turn of it */
    a->temper = TEMP_AGGRESSIVE;
    a->atk_t = -1;   /* not in the middle of a lash */
    a->br.home = a->pos;
    a->br.state = walks(a) && rng_chance(r, 0.5f) ? AI_REST : AI_IDLE;
    a->br.timer = rng_rangef(r, 2, 14);
}

/* --------------------------------------------------------------- states */
static void set_state(Actor *a, AiState s, float timer) {
    a->br.state = s;
    a->br.timer = timer;
    a->br.path_len = a->br.path_i = 0;
    a->br.repath = 0;
    a->br.wander_t = 0;
}

static void turn_to_move(Actor *a, float dt) {
    if (v2_len(a->vel) > 8) face_towards(a, v2_to_angle(a->vel), 6, dt);
}

static void wake(Actor *a, int target) {
    Brain *b = &a->br;
    bool startled = b->state != AI_CHASE;
    set_state(a, AI_CHASE, 0);
    b->target = target;
    b->aware = true;
    b->last_seen = W.actors[target].pos;
    if (!startled) return;
    a->alert_icon = 1;
    a->alert_icon_t = 0.9f;
    a->atk_cd = MAXF(a->atk_cd, 0.3f * diff_reaction());   /* it takes a moment to open up */
    rustle(a, 1.0f);
    play_at(SFX_HISS, a->pos, 0.4f, walks(a) ? 0.5f : 0.7f);
    int k = a->arch - AR_SPITTER;
    if (target == 0 && !(hinted & (1 << k))) {
        hinted |= (uint8_t)(1 << k);
        world_hint(HINTS[k]);
    }
}

/* lost them: a rooted one folds up again, a rambler stands there a while and then wanders off */
static void doze(Actor *a) {
    Brain *b = &a->br;
    b->target = -1;
    b->aware = false;
    a->windup = 0;
    set_state(a, AI_IDLE, frange(1.5f, 4));
}

/* the nearest person it can see within range. The locals have learnt to give the beds a wide berth - they have
 * to all but tread on one before it stirs; it's strangers it goes for */
static int nearest_prey(Actor *a, int idx, float range, float *out_d) {
    int best = -1;
    float bd = 1e9f;
    for (int j = 0; j < W.nactors; j++) {
        Actor *t = &W.actors[j];
        if (j == idx || !t->used || !t->alive || t->down_t > 0 || is_animal(t) || is_plant(t) || (j == 0 && W.exiting)) continue;
        if (t->arch == AR_BOSS) continue;   /* it's his garden */
        float d = v2_dist(t->pos, a->pos);
        float r = j == 0 ? range * (RUN.perks[PK_LIGHTFEET] ? 0.85f : 1.0f) : range * 0.35f;
        if (d >= r || d >= bd || !los_clear(a->pos, t->pos, false)) continue;
        bd = d;
        best = j;
    }
    if (out_d) *out_d = bd;
    return best;
}

static void sense(Actor *a, int idx) {
    Brain *b = &a->br;
    float d;
    if (b->state == AI_CHASE) {
        /* somebody much closer than the one it's after: them instead */
        int t = nearest_prey(a, idx, kind(a)->sense, &d);
        if (t >= 0 && t != b->target && b->target >= 0 && d < v2_dist(a->pos, W.actors[b->target].pos) * 0.5f) b->target = t;
        return;
    }
    int t = nearest_prey(a, idx, b->state == AI_REST ? REST_SENSE : kind(a)->sense, &d);
    if (t >= 0) wake(a, t);
}

/* ---------------------------------------------------------------- strikes */
static void strike(Actor *a, int idx) {
    const Kind *k = kind(a);
    a->atk_t = 0;
    a->hit_pending = false;
    if (a->arch == AR_SPITTER) {
        V2 mouth = v2_add(a->pos, v2_scale(v2_angle(a->face), k->mouth));
        spit_fire(idx, mouth, a->face + frange(-0.04f, 0.04f), SPIT_SPEED * frange(0.95f, 1.05f), k->reach * 1.3f);
        play_at(SFX_SPIT, a->pos, 0.9f, frange(0.9f, 1.1f));
        spray_sap(mouth, v2_angle(a->face), 2, 40);
        return;
    }
    a->hit_pending = true;
    play_at(SFX_LASH, a->pos, 0.85f, walks(a) ? frange(0.75f, 0.9f) : frange(1.0f, 1.15f));
    if (walks(a)) a->vel = v2_scale(v2_angle(a->face), 110);   /* throws its weight behind it */
}

/* the tendril (or the thorns) catch whoever is in the way - anybody, not just the one it was after */
static void lash(Actor *a, int idx) {
    const Kind *k = kind(a);
    int best = -1;
    float bd = 1e9f;
    for (int j = 0; j < W.nactors; j++) {
        Actor *t = &W.actors[j];
        if (j == idx || !t->used || !t->alive || is_animal(t) || is_plant(t)) continue;
        V2 d = v2_sub(t->pos, a->pos);
        float dist = v2_len(d);
        if (dist > k->reach + t->radius + 2 || dist >= bd) continue;
        if (dist > t->radius + 6 && fabsf(angle_diff(a->face, v2_to_angle(d))) > 0.5f) continue;
        if (!reach_clear(a->pos, t->pos)) continue;
        bd = dist;
        best = j;
    }
    if (best < 0) return;   /* lashed at thin air */
    Actor *t = &W.actors[best];
    damage_actor(best, idx, 1, v2_sub(t->pos, a->pos), 130, 0, W_NONE, DMG_MELEE);
    play_at(SFX_HIT_BLADE, t->pos, 0.55f, frange(1.3f, 1.5f));   /* it stings */
    if (walks(a)) a->vel = v2_scale(a->vel, 0.2f);
}

static void strike_update(Actor *a, int idx, float dt) {
    a->atk_t += dt;
    a->vel = v2_scale(a->vel, expf(-7 * dt));
    if (a->hit_pending && a->atk_t >= 0.06f) {
        a->hit_pending = false;
        lash(a, idx);
    }
    if (a->atk_t > 0.3f) {
        a->atk_t = -1;
        a->atk_cd = kind(a)->recover * frange(0.85f, 1.2f);
    }
}

/* ------------------------------------------------------------ behaviour */
/* spitters and nettles: the head follows them while they're about, and lets fly once they're close enough.
 * A nettle drags itself after them on its tendrils as well - never far from where it grew */
static void watch(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    const Kind *k = kind(a);
    Actor *t = b->target >= 0 ? &W.actors[b->target] : NULL;
    if (!t || !t->used || !t->alive || (b->target == 0 && W.exiting)) { doze(a); return; }
    float dist = v2_dist(a->pos, t->pos);
    bool vis = los_clear(a->pos, t->pos, false);
    if (vis && dist < k->sense * 1.3f) {
        b->last_seen = t->pos;
        b->wander_t = 0;
    } else if ((b->wander_t += dt) > 2.5f) {
        doze(a);
        return;
    }
    /* a spitter aims where they're going to be */
    V2 at = b->last_seen;
    if (a->arch == AR_SPITTER && vis) at = v2_add(t->pos, v2_scale(t->vel, dist / SPIT_SPEED * 0.5f));
    float ang = v2_to_angle(v2_sub(at, a->pos));
    if (a->windup > 0) {
        /* swelling up / rearing back: it turns slower now - this is your chance to get out of the way */
        a->vel = v2_scale(a->vel, expf(-12 * dt));
        face_towards(a, ang, k->turn * 0.4f, dt);
        if ((a->windup -= dt) <= 0) {
            a->windup = 0;
            strike(a, idx);
        }
        return;
    }
    face_towards(a, ang, k->turn, dt);
    if (k->leash > 0) {
        float speed = 0;
        V2 dir = v2_norm(v2_sub(t->pos, a->pos));
        if (vis && dist > k->reach * 0.7f) {
            V2 next = v2_add(a->pos, v2_scale(dir, 8));
            if (v2_dist(next, b->home) < k->leash && walk_clear(a->pos, next, a->radius)) speed = ARCH[a->arch].walk * a->speed_mul;
        }
        a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(dir, speed), a->vel), smooth_k(6, dt)));
    }
    if (!vis || a->atk_cd > 0 || dist > k->reach + t->radius || fabsf(angle_diff(a->face, ang)) > 0.35f) return;
    if (a->arch == AR_SPITTER && !los_clear(a->pos, t->pos, true)) return;   /* not through the glass */
    if (a->arch == AR_NETTLE && !reach_clear(a->pos, t->pos)) return;        /* not round the end of a shelf */
    a->windup = k->windup * diff_reaction() * frange(0.9f, 1.15f);
    rustle(a, 0.6f);
}

/* a rambler lumbers after them: straight at them when nothing's in the way, round the shelves otherwise */
static void chase(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    const Kind *k = kind(a);
    Actor *t = b->target >= 0 ? &W.actors[b->target] : NULL;
    if (!t || !t->used || !t->alive || (b->target == 0 && W.exiting)) { doze(a); return; }
    float dist = v2_dist(a->pos, t->pos);
    bool vis = los_clear(a->pos, t->pos, false);
    if (vis && dist < 320) {
        b->last_seen = t->pos;
        b->wander_t = 0;
    } else if ((b->wander_t += dt) > 4) {
        doze(a);
        return;
    }
    float ang = v2_to_angle(v2_sub(t->pos, a->pos));
    if (a->windup > 0) {
        /* bristling, thorns up */
        a->vel = v2_scale(a->vel, expf(-12 * dt));
        face_towards(a, ang, k->turn * 0.5f, dt);
        if ((a->windup -= dt) <= 0) {
            a->windup = 0;
            strike(a, idx);
        }
        return;
    }
    float run = ARCH[a->arch].run * a->speed_mul;
    if (vis && dist < k->reach + t->radius + 4) {
        if (a->atk_cd <= 0) {
            a->windup = k->windup * diff_reaction() * frange(0.9f, 1.15f);
            rustle(a, 0.8f);
            return;
        }
        /* getting its breath back: it squares up to them */
        a->vel = v2_scale(a->vel, expf(-8 * dt));
        face_towards(a, ang, k->turn, dt);
        return;
    }
    b->repath -= dt;
    if (vis && walk_clear(a->pos, t->pos, a->radius * 0.8f)) {
        V2 d = v2_norm(v2_sub(t->pos, a->pos));
        a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(d, run), a->vel), smooth_k(6, dt)));
        b->path_len = 0;
    } else {
        if (b->repath <= 0) {
            path_to(a, b->last_seen);
            b->repath = 0.5f;
        }
        if (follow(a, run, dt) && !vis) { doze(a); return; }
    }
    if (vis) face_towards(a, ang, k->turn, dt);
    else turn_to_move(a, dt);
}

/* a rambler on its own: walks somewhere, stands about, now and then digs in and is just a bush for a while */
static void wander(Actor *a, float dt) {
    Brain *b = &a->br;
    if (b->path_len == 0) {
        bool ok = false;
        for (int k = 0; k < 6 && !ok; k++) {
            V2 g = v2_add(a->pos, v2(frange(-170, 170), frange(-170, 170)));
            int tx = tile_of(g.x), ty = tile_of(g.y);
            if (!walkable_tile(tx, ty) || !cell(tx, ty)->reach) continue;
            ok = path_to(a, g) && b->path_len > 0;
        }
        if (!ok) { set_state(a, AI_IDLE, frange(1, 3)); return; }
    }
    b->wander_t += dt;
    bool there = follow(a, ARCH[a->arch].walk * a->speed_mul, dt);
    turn_to_move(a, dt);
    if (there || b->wander_t > 14) set_state(a, AI_IDLE, frange(1.5f, 4.5f));
}

/* ---------------------------------------------------------------- events */
void plant_on_hurt(Actor *a, int idx, int attacker) {
    Brain *b = &a->br;
    Actor *t = &W.actors[attacker];
    b->aware = true;
    if (b->state == AI_REST) set_state(a, AI_IDLE, frange(1, 2));   /* no more pretending */
    if (is_animal(t) || is_plant(t)) return;
    if (b->state == AI_CHASE && b->target >= 0 && b->target != attacker &&
        v2_dist(W.actors[b->target].pos, a->pos) <= v2_dist(t->pos, a->pos)) return;
    /* a rooted one only turns on whoever it can do something about; a rambler goes after them wherever they are */
    if (!walks(a) && v2_dist(t->pos, a->pos) > kind(a)->sense) return;
    wake(a, attacker);
}

void plant_on_noise(Actor *a, int idx, V2 pos, float radius, int source) {
    Brain *b = &a->br;
    /* the rooted ones feel footsteps right by them and nothing else; a rambler stops dead at a bang and turns to it */
    if (!walks(a) || radius < 200 || a->spawn_grace > 0) return;
    if (b->state == AI_WANDER || b->state == AI_IDLE) {
        set_state(a, AI_IDLE, frange(2, 4));
        b->goal = pos;
    }
}

void plant_wound(Actor *a, V2 dir, int amount) {
    float s = kind(a)->size;
    spray_sap(a->pos, dir, (int)((4 + amount * 3) * s) + 1, 110 + amount * 15);
    shed(a->pos, dir, 1 + amount);
    if (amount >= 2 || chance(0.5f))
        decal(SPR_FX_SAP + irange(0, SPR_FX_SAP_N - 1), v2_add(a->pos, v2_scale(v2_norm(dir), 6)), frange(-PI_F, PI_F), 0.8f, TINT_NONE);
    rustle(a, 0.9f);
    play_at(SFX_GORE, a->pos, 0.3f * s, frange(1.3f, 1.6f));
}

void plant_die(Actor *a, V2 dir, bool torn) {
    float s = kind(a)->size;
    decal(SPR_FX_SAP + irange(0, SPR_FX_SAP_N - 1), a->pos, frange(-PI_F, PI_F), 0.6f + 0.5f * s, TINT_NONE);
    decal(ARCH[a->arch].spr_dead, a->pos, walks(a) ? a->face : (a->coat >> 1) * PI_F * 0.5f, 1, TINT_NONE);
    spray_sap(a->pos, dir, (int)((torn ? 30 : 14) * s), torn ? 220 : 150);
    shed(a->pos, dir, (int)((torn ? 10 : 5) * s) + 2);
    rustle(a, 1.0f);
    play_at(SFX_GORE, a->pos, 0.6f, frange(1.1f, 1.4f));
    if (a->arch == AR_SPITTER) play_at(SFX_SPLAT, a->pos, 0.8f, 0.8f);   /* the pod bursts */
}

/* ---------------------------------------------------------------- update */
void plant_update(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    if (fixed(a)) {
        /* whatever hit it, it's still where it grew */
        a->pos = b->home;
        a->vel = a->push = v2(0, 0);
    } else if (plant_rooted(a)) {
        a->vel = a->push = v2(0, 0);
    }
    if (a->burn_t > 0) {
        /* goes up like a torch - and takes the grass round it along */
        a->windup = 0;
        if (chance(dt * 1.5f)) fire_spawn(v2_add(a->pos, v2(frange(-4, 4), frange(-4, 4))), frange(2, 4), a->last_hit_by);
        if (!walks(a)) return;
        if (b->state == AI_REST) set_state(a, AI_IDLE, 0);
        if ((b->strafe_t -= dt) <= 0) {
            b->strafe_t = 0.35f;
            b->goal = v2_add(a->pos, v2(frange(-60, 60), frange(-60, 60)));
        }
        V2 d = v2_norm(v2_sub(b->goal, a->pos));
        a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(d, ARCH[a->arch].run), a->vel), smooth_k(8, dt)));
        turn_to_move(a, dt);
        return;
    }
    if (a->stun_t > 0) { a->vel = v2_scale(a->vel, expf(-8 * dt)); return; }
    if (a->atk_t >= 0) { strike_update(a, idx, dt); return; }
    b->think -= dt;
    if (b->think <= 0) {
        b->think = 0.12f + frand() * 0.08f;
        if (a->spawn_grace <= 0) sense(a, idx);
    }
    switch (b->state) {
    case AI_CHASE:
        if (walks(a)) chase(a, idx, dt);
        else watch(a, idx, dt);
        break;
    case AI_REST:
        if ((b->timer -= dt) <= 0) {
            set_state(a, AI_IDLE, frange(1, 3));   /* pulls its roots up */
            rustle(a, 0.5f);
        }
        break;
    case AI_WANDER:
        wander(a, dt);
        break;
    default:
        if (!walks(a)) {
            /* asleep: just a weed - one that crept off after somebody drags itself back to its roots */
            float d = v2_dist(a->pos, b->home), speed = 0;
            V2 dir = v2_norm(v2_sub(b->home, a->pos));
            if (d > 2 && walk_clear(a->pos, b->home, a->radius * 0.8f)) speed = MINF(ARCH[a->arch].walk * a->speed_mul, d * 4);
            a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(dir, speed), a->vel), smooth_k(6, dt)));
            break;
        }
        a->vel = v2_scale(a->vel, expf(-10 * dt));
        if (chance(dt * 0.4f)) b->goal = v2_add(a->pos, v2_angle(frange(-PI_F, PI_F)));
        face_towards(a, v2_to_angle(v2_sub(b->goal, a->pos)), 2, dt);
        if ((b->timer -= dt) <= 0) {
            if (chance(0.35f) && open_ground(a->pos)) set_state(a, AI_REST, frange(6, 16));   /* digs in: just a bush again */
            else set_state(a, AI_WANDER, 0);
        }
        break;
    }
    /* whatever moves rustles */
    float sp = v2_len(a->vel);
    if (sp > 8 && (b->strafe_t -= sp * dt) <= 0) {
        b->strafe_t = walks(a) ? 24 : 14;
        rustle(a, walks(a) ? 0.3f : 0.2f);
    }
}

/* ----------------------------------------------------------------- draw */
void plant_draw_shadow(Actor *a) {
    if (walks(a)) {
        gfx_spr_ex(SPR_FX_SHADOW, a->pos.x + 1.5f, a->pos.y + 2.5f, a->face, 1.5f, 1.1f, TINT_NONE);
        return;
    }
    /* the leaves lie flat; only the head stands up off them */
    V2 h = v2_add(a->pos, v2_scale(v2_angle(a->face), 4));
    gfx_spr_ex(SPR_FX_SHADOW, h.x + 1.5f, h.y + 2, a->face, 0.6f, 0.5f, rgba(255, 255, 255, 170));
}

void plant_draw(Actor *a) {
    Brain *b = &a->br;
    Color tint = TINT_NONE;
    if (a->hurt_t > 0) tint = rgb(255, 120, 120);
    if (a->burn_t > 0 && ((int)(W.time * 20) & 1)) tint = rgb(255, 170, 90);
    float x = a->pos.x, y = a->pos.y, ang = a->face;
    if (a->burn_t > 0) x += sinf(W.time * 40) * 0.6f;   /* writhing */
    if (a->stun_t > 0) ang += sinf(W.time * 18) * 0.3f;
    if (!walks(a)) {
        /* the rosette lies still (a creeping nettle shuffles it along); the head turns */
        float base = (a->coat >> 1) * PI_F * 0.5f;
        if (v2_len(a->vel) > 4) base += sinf(W.time * 9 + a->pos.x) * 0.08f;
        gfx_spr_ex(ARCH[a->arch].spr_idle + (a->coat & 1), x, y, base, 1, 1, tint);
        bool spitter = a->arch == AR_SPITTER, awake = b->state == AI_CHASE;
        int fr = 0;
        if (a->atk_t >= 0) fr = spitter ? (a->atk_t < 0.16f ? 3 : 1) : (a->atk_t < 0.14f ? 2 : 3);
        else if (a->windup > 0) {
            fr = spitter ? 2 : 1;
            x += sinf(W.time * 70) * 0.5f;
        } else if (awake) {
            fr = spitter ? 1 : 0;
            if (!spitter) ang += sinf(W.time * 6 + a->pos.y) * 0.08f;   /* the tendrils twitch */
        } else ang += sinf(W.time * 1.1f + a->pos.x * 0.13f) * 0.12f;   /* asleep, swaying a little */
        gfx_spr_ex(ARCH[a->arch].spr_punch + fr, x, y, ang, 1, 1, tint);
        if (a->stun_t > 0) gfx_spr(SPR_UI_STARS + (int)(W.time * 8) % 3, a->pos.x, a->pos.y - 10);
        return;
    }
    float sp = v2_len(a->vel);
    int spr;
    if (a->atk_t >= 0) spr = SPR_RAMBLER_BITE + (a->atk_t < 0.18f ? 1 : 0);
    else if (a->windup > 0) {
        spr = SPR_RAMBLER_BITE;
        x += sinf(W.time * 70) * 0.5f;
    } else if (b->state == AI_REST && sp < 6) {
        spr = SPR_RAMBLER_REST;
        ang = 0;   /* just a bush - with its shadow to the bottom right, like every other bush */
    }
    else if (sp > 4) spr = SPR_RAMBLER_WALK + ((int)(a->leg_anim / 5) & 3);
    else {
        spr = SPR_RAMBLER_WALK + 1;
        ang += sinf(W.time * 1.7f + a->pos.x) * 0.05f;   /* stands there, swaying like any bush */
    }
    gfx_spr_ex(spr, x, y, ang, 1, 1, tint);
    if (a->stun_t > 0) gfx_spr(SPR_UI_STARS + (int)(W.time * 8) % 3, a->pos.x, a->pos.y - 14);
}
