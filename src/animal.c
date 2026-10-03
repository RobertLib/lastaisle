/* LAST AISLE - animals: strays, cats, foxes and rats. Most of them mind their own business - they sleep, potter
 * about, pick at the dead and keep out of people's way. The rabid ones go for anybody who comes too close. */
#include "world.h"
#include "gfx.h"
#include "audio.h"

typedef struct {
    float bite;        /* damage per bite */
    float reach;       /* springs at people this close */
    float lunge;       /* speed of the spring, px/s */
    float crouch;      /* the tell before it springs, s */
    float recover;     /* between bites, s */
    float aggro;       /* a rabid one goes for whoever comes this close (and snarls a little further out) */
    float shy;         /* a healthy one keeps this far from people (0: doesn't care) */
    bool scavenger;    /* eats from the dead */
    float voice;       /* pitch of its barks, growls and yelps */
    int alarm, snarl;  /* what it does when it goes for you / while it does */
    int coats;         /* healthy coats to pick from */
    float size;        /* 1 = a dog: shadow, blood, thud */
    float mouth;       /* pivot to the jaws, px */
    float stride;      /* px walked per walk frame */
} Species;

static const Species SPECIES[] = {
    /*            bite reach lunge crouch recover aggro shy  scav   voice  alarm     snarl      coats size   mouth stride */
    /* dog */    {1,   26,   230,  0.30f, 0.95f,  120,  0,   true,  1.0f,  SFX_BARK, SFX_GROWL, 3,    1.0f,  10,   6},
    /* cat */    {1,   20,   240,  0.22f, 0.75f,  80,   64,  false, 1.5f,  SFX_HISS, SFX_HISS,  3,    0.6f,  6,    4},
    /* fox */    {1,   24,   220,  0.26f, 0.85f,  100,  90,  true,  1.25f, SFX_BARK, SFX_GROWL, 1,    0.85f, 9,    6},
    /* rat */    {1,   15,   170,  0.24f, 0.85f,  64,   48,  true,  2.4f,  SFX_YELP, SFX_YELP,  1,    0.45f, 5,    3},
};

typedef struct { int walk, bite, rest, dead; } Look;
#define LOOK(n) {SPR_##n##_WALK, SPR_##n##_BITE, SPR_##n##_REST, SPR_##n##_DEAD}
static const Look LOOKS[][4] = {   /* [species][coat], [3] = rabid */
    {LOOK(DOG), LOOK(DOG2), LOOK(DOG3), LOOK(DOGR)},
    {LOOK(CAT), LOOK(CAT2), LOOK(CAT3), LOOK(CATR)},
    {LOOK(FOX), LOOK(FOX), LOOK(FOX), LOOK(FOXR)},
    {LOOK(RAT), LOOK(RAT), LOOK(RAT), LOOK(RATR)},
};
_Static_assert(AR_RAT - AR_DOG + 1 == ARRAY_LEN(SPECIES) && ARRAY_LEN(SPECIES) == ARRAY_LEN(LOOKS),
               "animal tables are indexed by archetype, AR_DOG first");

static const Species *species(const Actor *a) { return &SPECIES[a->arch - AR_DOG]; }
static const Look *look(const Actor *a) { return &LOOKS[a->arch - AR_DOG][a->rabid ? 3 : a->coat]; }

float animal_size(const Actor *a) { return species(a)->size; }
int animal_corpse(const Actor *a, bool torn) { return look(a)->dead + (torn ? 1 : 0); }

static V2 jaw(const Actor *a) { return v2_add(a->pos, v2_scale(v2_angle(a->face), species(a)->mouth)); }

/* voiced sounds follow the species' pitch; hisses and snaps just get smaller with the animal */
static void voice(Actor *a, int sfx, float vol) {
    const Species *s = species(a);
    float pitch = (sfx == SFX_HISS || sfx == SFX_BITE) ? 1.0f + (1.0f - s->size) * 0.8f : s->voice;
    play_at(sfx, a->pos, vol * (0.55f + 0.45f * s->size), pitch * frange(0.92f, 1.08f));
}

void animal_cry(Actor *a, float vol) { voice(a, SFX_YELP, vol); }

void animal_setup(Actor *a, Rng *r, bool rabid) {
    a->rabid = rabid;
    a->coat = (uint8_t)rng_int(r, species(a)->coats);
    a->temper = rabid ? TEMP_AGGRESSIVE : ARCH[a->arch].temper;
    a->br.state = rabid ? AI_WANDER : (rng_chance(r, 0.45f) ? AI_REST : AI_IDLE);
    a->br.timer = rng_rangef(r, 2, 12);
    a->br.home = a->pos;
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
    if (v2_len(a->vel) > 8) face_towards(a, v2_to_angle(a->vel), 10, dt);
}

static void go_for(Actor *a, int target, bool loud) {
    set_state(a, AI_CHASE, 6.0f);   /* a healthy one stays angry this long */
    a->br.target = target;
    a->br.aware = true;
    a->br.last_seen = W.actors[target].pos;
    if (loud) {
        a->alert_icon = 1;
        a->alert_icon_t = 1.0f;
        voice(a, species(a)->alarm, 0.95f);
        a->br.bark_t = frange(1.2f, 2.5f);
    }
}

static void flee_from(Actor *a, V2 from, float t) {
    set_state(a, AI_FLEE, t);
    a->br.last_seen = from;
    path_to(a, flee_point(a, from));
}

static void calm_down(Actor *a) {
    Brain *b = &a->br;
    b->target = -1;
    b->aware = false;
    if (a->rabid) set_state(a, AI_WANDER, 0);
    else flee_from(a, b->last_seen, frange(1.5f, 3.0f));   /* had enough: off it trots */
}

/* the nearest person it can see within range (others than you: within range * npc); one running at it counts from
 * further off */
static int nearest_person(Actor *a, int idx, float range, float npc, bool startle, float *out_d) {
    int best = -1;
    float bd = 1e9f;
    for (int j = 0; j < W.nactors; j++) {
        Actor *t = &W.actors[j];
        if (j == idx || !t->used || !t->alive || is_animal(t) || is_plant(t) || (j == 0 && W.exiting)) continue;
        float d = v2_dist(t->pos, a->pos);
        float r = (j == 0 ? range : range * npc) * (startle && v2_len(t->vel) > 60 ? 1.4f : 1.0f);
        if (d >= r || d >= bd || !los_clear(a->pos, t->pos, false)) continue;
        bd = d;
        best = j;
    }
    if (out_d) *out_d = bd;
    return best;
}

static int nearest_corpse(V2 pos, float range) {
    int best = -1;
    float bd = range * range;
    for (int i = 0; i < W.ncorpses; i++) {
        float d = v2_dist2(W.corpses[i].pos, pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void sense(Actor *a, int idx) {
    Brain *b = &a->br;
    const Species *s = species(a);
    float d;
    if (a->rabid) {
        if (b->state == AI_CHASE) return;
        /* it's the shopper it's out for: the locals have to step a lot closer before it goes for them */
        int t = nearest_person(a, idx, s->aggro * 1.7f, 0.6f, false, &d);
        if (t < 0) return;
        if (d < s->aggro * (t == 0 ? 1.0f : 0.6f)) { go_for(a, t, true); return; }
        /* close, not too close yet: it stops dead, stares and snarls - the only warning you get */
        if (b->state != AI_IDLE || b->target != t) set_state(a, AI_IDLE, frange(0.6f, 1.2f));
        b->target = t;
        b->goal = W.actors[t].pos;
        if (b->bark_t <= 0) { voice(a, s->snarl, 0.7f); b->bark_t = frange(1.5f, 2.5f); }
        return;
    }
    if (b->state == AI_CHASE || b->state == AI_FLEE) return;
    if (s->shy > 0) {
        int t = nearest_person(a, idx, s->shy, 1, true, &d);
        if (t < 0) return;
        if (a->arch == AR_CAT && d < s->shy * 0.6f && b->bark_t <= 0) { voice(a, SFX_HISS, 0.6f); b->bark_t = 3; }
        flee_from(a, W.actors[t].pos, frange(1.5f, 3.0f));
    } else if (b->state == AI_REST) {
        /* a dog asleep in the aisle lifts its head when somebody steps right over it */
        int t = nearest_person(a, idx, 26, 1, false, NULL);
        if (t < 0) return;
        set_state(a, AI_IDLE, frange(1.5f, 3.0f));
        b->goal = W.actors[t].pos;
    }
}

static void next_activity(Actor *a) {
    Brain *b = &a->br;
    if (a->rabid) { set_state(a, AI_WANDER, 0); return; }
    float r = frand();
    if (species(a)->scavenger && r < 0.35f) {
        int c = nearest_corpse(a->pos, 240);
        if (c >= 0) {
            set_state(a, AI_FEED, frange(4, 9));
            if (path_to(a, W.corpses[c].pos)) { b->goal = W.corpses[c].pos; return; }
        }
    }
    if (r < 0.6f) set_state(a, AI_REST, frange(6, 18));
    else set_state(a, AI_WANDER, 0);
}

static void wander(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    float walk = ARCH[a->arch].walk * a->speed_mul, run = ARCH[a->arch].run * a->speed_mul;
    if (b->path_len == 0) {
        /* a healthy one potters about its own patch; a rabid one roams */
        V2 c = a->rabid ? a->pos : b->home;
        float range = a->rabid ? 160 : 110;
        bool ok = false;
        for (int k = 0; k < 6 && !ok; k++) {
            V2 g = v2_add(c, v2(frange(-range, range), frange(-range, range)));
            int tx = tile_of(g.x), ty = tile_of(g.y);
            if (!walkable_tile(tx, ty) || !cell(tx, ty)->reach) continue;
            ok = path_to(a, g) && b->path_len > 0;
        }
        if (!ok) { set_state(a, AI_IDLE, frange(1, 3)); return; }
    }
    b->wander_t += dt;
    float speed = a->rabid ? (walk + run) * 0.5f : walk;
    bool there = follow(a, speed, dt);
    if (a->rabid) {
        /* lurching, never quite in a straight line */
        V2 side = v2_norm(v2(-a->vel.y, a->vel.x));
        a->vel = v2_add(a->vel, v2_scale(side, sinf(W.time * 7.3f + idx * 1.9f) * 0.35f * speed * smooth_k(10, dt)));
    }
    turn_to_move(a, dt);
    if (there || b->wander_t > 12) set_state(a, AI_IDLE, a->rabid ? frange(0.3f, 1.2f) : frange(1.5f, 5));
}

static void feed(Actor *a, float dt) {
    Brain *b = &a->br;
    if (v2_dist(a->pos, b->goal) > 6 + species(a)->mouth) {
        b->wander_t += dt;
        bool there = follow(a, ARCH[a->arch].walk * a->speed_mul * 1.3f, dt);
        turn_to_move(a, dt);
        if (there || b->wander_t > 12) set_state(a, AI_IDLE, frange(1, 2));   /* can't get to it */
        return;
    }
    /* head down in it: tug, chew, look up now and then */
    a->vel = v2_scale(a->vel, expf(-10 * dt));
    face_towards(a, v2_to_angle(v2_sub(b->goal, a->pos)), 6, dt);
    if (chance(dt * 1.5f)) spray_blood(jaw(a), v2_angle(a->face + PI_F + frange(-1, 1)), 1, 40);
    if (chance(dt * 0.4f)) play_at(SFX_GORE, a->pos, 0.25f * species(a)->size, frange(1.1f, 1.4f));
    if ((b->timer -= dt) <= 0) set_state(a, AI_IDLE, frange(2, 4));
}

/* ----------------------------------------------------------------- bites */
static void spring(Actor *a, V2 at) {
    a->face = v2_to_angle(v2_sub(at, a->pos));
    a->atk_t = 0;
    a->hit_pending = true;
    a->vel = v2_scale(v2_angle(a->face), species(a)->lunge);
    play_at(SFX_SWING, a->pos, 0.45f * species(a)->size, 1.3f);
}

/* the jaws close on whoever they touch during the lunge; returns false while they haven't touched anybody */
static bool bite(Actor *a, int idx, bool last_chance) {
    const Species *s = species(a);
    V2 j = jaw(a);
    int best = -1;
    float bd = 3;
    for (int i = 0; i < W.nactors; i++) {
        Actor *t = &W.actors[i];
        if (i == idx || !t->used || !t->alive || is_animal(t) || is_plant(t)) continue;
        if (!a->rabid && i != a->br.target) continue;   /* a dog biting back only goes for whoever hurt it */
        float d = v2_dist(j, t->pos) - t->radius;
        if (d >= bd || !reach_clear(a->pos, t->pos)) continue;
        bd = d;
        best = i;
    }
    if (best < 0) {
        if (!last_chance) return false;
        voice(a, SFX_BITE, 0.45f);   /* snaps at thin air */
        return true;
    }
    V2 dir = v2_sub(W.actors[best].pos, a->pos);
    damage_actor(best, idx, s->bite, dir, 90, 0, -1, DMG_MELEE);
    voice(a, SFX_BITE, 1.0f);
    a->push = v2_scale(v2_norm(dir), -80);   /* lets go and bounces off */
    a->vel = v2_scale(a->vel, 0.2f);
    return true;
}

static void lunge_update(Actor *a, int idx, float dt) {
    a->atk_t += dt;
    a->vel = v2_scale(a->vel, expf(-4 * dt));
    if (a->hit_pending && a->atk_t >= 0.03f && bite(a, idx, a->atk_t >= 0.2f)) a->hit_pending = false;
    if (a->atk_t > 0.28f) {
        a->atk_t = -1;
        a->atk_cd = species(a)->recover * frange(0.85f, 1.2f);
    }
}

static void chase(Actor *a, float dt) {
    Brain *b = &a->br;
    const Species *s = species(a);
    Actor *t = b->target >= 0 ? &W.actors[b->target] : NULL;
    if (!t || !t->used || !t->alive || (b->target == 0 && W.exiting)) { calm_down(a); return; }
    if (!a->rabid && (b->timer -= dt) <= 0) { calm_down(a); return; }
    float dist = v2_dist(a->pos, t->pos);
    bool vis = los_clear(a->pos, t->pos, false);
    if (vis) {
        b->last_seen = t->pos;
        b->wander_t = 0;
    } else if ((b->wander_t += dt) > 4) {
        calm_down(a);
        return;
    }
    float ang = v2_to_angle(v2_sub(t->pos, a->pos));
    if (a->windup > 0) {
        /* crouched, quivering, about to spring: this is your moment */
        a->vel = v2_scale(a->vel, expf(-14 * dt));
        face_towards(a, ang, 16, dt);
        if ((a->windup -= dt) <= 0) {
            a->windup = 0;
            if (dist < s->reach + t->radius + 16) spring(a, t->pos);   /* still in range - or they got away and it runs on */
        }
        return;
    }
    if (b->bark_t <= 0) { voice(a, s->snarl, 0.65f); b->bark_t = frange(1.6f, 3.2f); }
    float run = ARCH[a->arch].run * a->speed_mul;
    float reach = s->reach + t->radius;
    if (vis && dist < reach) {
        if (a->atk_cd <= 0) {
            /* someone standing their ground sees it crouch first; someone running away gets it in the back */
            bool running = v2_dot(t->vel, v2_norm(v2_sub(t->pos, a->pos))) > 40;
            a->windup = running ? 0.06f : s->crouch * diff_reaction() * frange(0.85f, 1.2f);
            return;
        }
        /* waiting to go again: circle round them, keep a little distance */
        V2 away = v2_norm(v2_sub(a->pos, t->pos)), side = v2(-away.y, away.x);
        V2 mv = v2_add(v2_scale(side, b->strafe_dir < 0 ? -1.0f : 1.0f), v2_scale(away, dist < reach * 0.7f ? 0.8f : 0));
        a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(v2_norm(mv), run * 0.5f), a->vel), smooth_k(8, dt)));
        face_towards(a, ang, 10, dt);
        if (chance(dt * 0.8f)) b->strafe_dir = -b->strafe_dir;
        return;
    }
    /* run them down: straight at them when nothing's in the way, round the shelves otherwise */
    b->repath -= dt;
    if (vis && walk_clear(a->pos, t->pos, a->radius * 0.8f)) {
        V2 d = v2_norm(v2_sub(t->pos, a->pos));
        a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(d, run), a->vel), smooth_k(10, dt)));
        b->path_len = 0;
    } else {
        if (b->repath <= 0) {
            path_to(a, b->last_seen);
            b->repath = 0.4f;
        }
        if (follow(a, run, dt) && !vis) { calm_down(a); return; }
    }
    if (vis) face_towards(a, ang, 10, dt);
    else turn_to_move(a, dt);
}

/* ---------------------------------------------------------------- events */
void animal_on_hurt(Actor *a, int idx, int attacker) {
    Brain *b = &a->br;
    Actor *t = &W.actors[attacker];
    b->aware = true;
    if (is_animal(t)) return;
    if (a->rabid) {
        bool closer = b->state == AI_CHASE && b->target >= 0 &&
                      v2_dist(t->pos, a->pos) < v2_dist(W.actors[b->target].pos, a->pos);
        if (b->state != AI_CHASE || closer) go_for(a, attacker, b->state != AI_CHASE);
        return;
    }
    /* a dog bites back at whoever's close enough to bite; everything else runs */
    if (a->temper == TEMP_DEFENSIVE && v2_dist(t->pos, a->pos) < 80) go_for(a, attacker, true);
    else flee_from(a, t->pos, frange(2.5f, 4.0f));
}

void animal_on_noise(Actor *a, int idx, V2 pos, float radius, int source) {
    Brain *b = &a->br;
    if (radius < 90 || a->spawn_grace > 0 || b->state == AI_CHASE) return;   /* footsteps and rummaging: nothing */
    if (a->rabid) {
        /* a fight or a shot: it comes to see */
        if (b->state != AI_INVESTIGATE) set_state(a, AI_INVESTIGATE, 0);
        b->goal = pos;
        return;
    }
    float d = v2_dist(a->pos, pos);
    if (radius >= 200 || (species(a)->shy > 0 && d < radius * 0.6f)) {
        if (b->state != AI_FLEE) flee_from(a, pos, frange(2.5f, 4.5f));   /* a bang, or a brawl too close: gone */
    } else if (b->state == AI_REST || b->state == AI_IDLE) {
        set_state(a, AI_IDLE, frange(1.5f, 3.0f));   /* lifts its head and looks */
        b->goal = pos;
    }
}

/* ---------------------------------------------------------------- update */
void animal_update(Actor *a, int idx, float dt) {
    Brain *b = &a->br;
    float run = ARCH[a->arch].run * a->speed_mul;
    b->bark_t -= dt;
    if (a->rabid && chance(dt * 3)) {
        /* flecks of foam */
        Particle *p = particle_add(PT_DUST, jaw(a), v2_add(v2_scale(a->vel, 0.3f), v2(frange(-12, 12), frange(-12, 12))), frange(0.25f, 0.45f));
        p->spr = SPR_FX_DUST;
        p->frames = 3;
        p->scale = 0.3f;
        p->col = rgba(255, 255, 255, 230);
    }
    if (a->stun_t > 0) { a->vel = v2_scale(a->vel, expf(-8 * dt)); return; }
    if (a->atk_t >= 0) { lunge_update(a, idx, dt); return; }
    if (a->burn_t > 0) {
        /* on fire: bolts about blindly */
        a->windup = 0;
        if ((b->strafe_t -= dt) <= 0) {
            b->strafe_t = 0.35f;
            b->goal = v2_add(a->pos, v2(frange(-60, 60), frange(-60, 60)));
        }
        V2 d = v2_norm(v2_sub(b->goal, a->pos));
        a->vel = v2_add(a->vel, v2_scale(v2_sub(v2_scale(d, run), a->vel), smooth_k(8, dt)));
        turn_to_move(a, dt);
        return;
    }
    b->think -= dt;
    if (b->think <= 0) {
        b->think = 0.12f + frand() * 0.08f;
        if (a->spawn_grace <= 0) sense(a, idx);
    }
    switch (b->state) {
    case AI_CHASE:
        chase(a, dt);
        break;
    case AI_FLEE:
        if (b->path_i >= b->path_len && (b->repath -= dt) <= 0) {
            path_to(a, flee_point(a, b->last_seen));
            b->repath = 0.3f;
        }
        follow(a, run, dt);
        turn_to_move(a, dt);
        if ((b->timer -= dt) <= 0) set_state(a, AI_IDLE, frange(1, 3));
        break;
    case AI_INVESTIGATE:
        if (b->path_len == 0 && b->repath <= 0) {
            path_to(a, b->goal);
            b->repath = 2;
        }
        b->repath -= dt;
        b->wander_t += dt;
        if (follow(a, run * 0.8f, dt) || b->wander_t > 8) set_state(a, AI_WANDER, 0);
        turn_to_move(a, dt);
        break;
    case AI_REST:
        a->vel = v2_scale(a->vel, expf(-10 * dt));
        if ((b->timer -= dt) <= 0) set_state(a, AI_IDLE, frange(1, 3));
        break;
    case AI_FEED:
        feed(a, dt);
        break;
    case AI_WANDER:
        wander(a, idx, dt);
        break;
    default:
        /* stands about, sniffs, looks round - a rabid one stares at whoever it's snarling at */
        a->vel = v2_scale(a->vel, expf(-10 * dt));
        if (!a->rabid && chance(dt * 0.5f)) b->goal = v2_add(a->pos, v2_angle(frange(-PI_F, PI_F)));
        face_towards(a, v2_to_angle(v2_sub(b->goal, a->pos)), a->rabid ? 8 : 2.5f, dt);
        if (a->arch == AR_DOG && !a->rabid && b->bark_t <= 0 && chance(dt * 0.015f)) {
            voice(a, SFX_BARK, 0.5f);   /* the odd bark at nothing in particular */
            b->bark_t = 8;
        }
        if ((b->timer -= dt) <= 0) next_activity(a);
        break;
    }
}

/* ----------------------------------------------------------------- draw */
void animal_draw_shadow(Actor *a) {
    float s = species(a)->size;
    gfx_spr_ex(SPR_FX_SHADOW, a->pos.x + 1.5f, a->pos.y + 2.5f, a->face, 1.35f * s, 0.75f * s, TINT_NONE);
}

void animal_draw(Actor *a) {
    const Species *s = species(a);
    const Look *l = look(a);
    Brain *b = &a->br;
    Color tint = TINT_NONE;
    if (a->hurt_t > 0) tint = rgb(255, 90, 90);
    if (a->burn_t > 0 && ((int)(W.time * 20) & 1)) tint = rgb(255, 170, 90);
    float sp = v2_len(a->vel), ang = a->face, x = a->pos.x;
    int spr;
    if (a->atk_t >= 0) spr = l->bite + 1;
    else if (a->windup > 0) {
        spr = l->bite;
        x += sinf(W.time * 70) * 0.5f;
    } else if (b->state == AI_REST && sp < 6) spr = l->rest;
    else if (sp > 4) spr = l->walk + ((int)(a->leg_anim / s->stride) & 3);
    else if (b->state == AI_FEED && v2_dist(a->pos, b->goal) < 6 + s->mouth + 2) spr = ((int)(W.time * 3 + a->pos.x) & 1) ? l->bite : l->walk + 1;
    else spr = l->walk + 1;
    /* rabid: twitching, now and then a violent shake of the head */
    if (a->rabid) ang += sinf(W.time * 23 + a->pos.y) * 0.05f + (sinf(W.time * 2.3f + a->pos.x) > 0.93f ? sinf(W.time * 60) * 0.25f : 0);
    if (a->stun_t > 0) ang += sinf(W.time * 18) * 0.3f;
    gfx_spr_ex(spr, x, a->pos.y, ang, 1, 1, tint);
    if (a->stun_t > 0) gfx_spr(SPR_UI_STARS + (int)(W.time * 8) % 3, a->pos.x, a->pos.y - 6 - 6 * s->size);
}
