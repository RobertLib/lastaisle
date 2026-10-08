/* LAST AISLE - the Greenhouse: the camp you walk round before every store.
 * Talk to people, lift with Dee, shoot a string on June's range, run the flags round the glass, put Gus's
 * workbench to use - then get in the van. One go at each kind of training an evening; it all stays in the run.
 * Rosa hands you the list (no list, no van); the others may ask a favour - something hard to get - and pay you back
 * when you bring it home (FAVOURS in data.c). */
#include "hub.h"
#include "gfx.h"
#include "input.h"
#include "audio.h"
#include "game.h"

HubLayout HUB;

extern void hud_cursor(void);

static const Color INK = {30, 45, 110, 255};
static const Color INK_FADE = {120, 110, 120, 255};

/* ================================================================ what people say */
/* a conversation: pages split by '|', for the evenings before stores lv0..lv1; gifts handed over at the end
 * (once an evening). The camp's people say their piece once, then an AGAIN line. Rosa ends hers with the list,
 * and whoever has a favour to ask asks it at the end of theirs. */
typedef struct {
    int arch;
    int lv0, lv1;
    const char *text;
    ItemCount gift[2];
} Line;

static const Line LINES[] = {
    {AR_ROSA, 0, 0, "So. You're up.|Gus swears the van will make Route 9 and back, as long as nobody asks it for more.|"
                    "Take the evening. Dee has the weights out in the yard, June runs the range past the workshop, and Gus "
                    "can do things to a bat you wouldn't believe.|The van leaves when you get in it."},
    {AR_ROSA, 1, 1, "The kids drank the whole jug. I'm not complaining.|Freshway next. Raiders have picked it over for weeks, "
                    "which means there's still something to pick.|See Gus before you go. He's been grinning at that scrap pile all day."},
    {AR_ROSA, 2, 2, "The generator's dead and winter's coming. Build-Rite has what we need.|Big ones hole up in there. Gas "
                    "masks. Sledgehammers.|If you're going to hit them, hit them hard. Dee's been waiting for you."},
    {AR_ROSA, 3, 3, "It's Theo. A hundred and four.|Marta's doing what she can. It isn't enough. Antibiotics - Medimart "
                    "has them, or had.|The Pigs moved in last week. Don't stop to talk. Don't stop at all."},
    {AR_ROSA, 4, 4, "He sat up this morning and asked for peaches. Peaches.|Two new families came to the gate last night. "
                    "Fourteen mouths now.|MegaMart. Everything we need, and everybody wants it. Let them fight each other."},
    {AR_ROSA, 5, 5, "Last run before the snow.|The Mall King sits on the only seed stock left in the county. Seeds mean "
                    "spring. Spring means we stop doing this.|Come back. That's not a request."},

    {AR_THEO, 0, 0, "Are you going shopping? Are you scared? I'd be scared. I'd still go.|"
                    "I found this in the dirt. It's a spring! It goes boing. You can have it.", {{IT_SPRINGS, 1}}},
    {AR_THEO, 1, 1, "I named the beans. That one's Kevin. That one's also Kevin.|Did you see any dogs? Nice ones? Rosa says "
                    "the nice ones are all gone.|Here. It was in the creek. Gus says it's for sharpening.", {{IT_WHETSTONE, 1}}},
    {AR_THEO, 2, 2, "When the lights come back on I'm going to watch cartoons for a hundred days.|I time people on the "
                    "flags! All the way round the greenhouse. You have to touch every flag. No cheating."},
    {AR_THEO, 3, 3, "...is it night already?|My head's hot. Marta keeps putting wet socks on it.|"
                    "Are you going to the medicine store? ...Okay. Come back after."},
    {AR_THEO, 4, 4, "I'm better! Rosa says no running yet. I'm only running a little bit.|Marta says you saved me. This is "
                    "my best one. It's for your gun thing.", {{IT_SODA, 1}}},
    {AR_THEO, 5, 5, "Bring back tomatoes. And beans. And... everything.|Theo's rules: you have to come back. That's the only rule."},

    {AR_GUS, 0, 0, "Gus. I fix things. I break things. Sometimes in that order.|That's my bench. Bring me scrap, springs, "
                   "tape, rags - I'll put nails through your bat, sights on a pistol, a soda bottle on the end of anything "
                   "that goes bang.|Here's a little something to start you off. Don't say I never gave you nothing.",
     {{IT_SCRAP, 2}, {IT_DUCTTAPE, 1}}},
    {AR_GUS, 1, 1, "Car trunks, hardware aisles, electronics counters - that's where the good junk lives.|Scrap pile's "
                   "growing. Help yourself, it's what it's for.", {{IT_SPRINGS, 1}}},
    {AR_GUS, 2, 2, "Build-Rite! Bring me everything. Bring me the building.|A whetstone and a blade - now that's a happy marriage.",
     {{IT_SCRAP, 1}}},
    {AR_GUS, 3, 3, "Can't sleep either. Kid's tough, though. Tougher than me.|Take this. Make it count.", {{IT_GUNPOWDER, 1}}},
    {AR_GUS, 4, 4, "Fourteen. Gonna need more beds. More everything.|Here. Don't spend it all at once.", {{IT_SCRAP, 2}}},
    {AR_GUS, 5, 5, "The Mall King. Heard he sits on a throne made of shopping carts.|Bring me back a wheel off it. "
                   "I'll put it on the van.", {{IT_SPRINGS, 2}}},

    {AR_JUNE, 0, 0, "June. I run the range.|Bottles on the planks, boards pop up in between. Hit what you aim at and "
                    "don't waste my rounds.|One string a night. Ammo doesn't grow on trees. Yet."},
    {AR_JUNE, 1, 1, "Raiders don't line up like bottles. Practise anyway.|Take these. Not for the range - for out there.",
     {{IT_AMMO9, 6}}},
    {AR_JUNE, 2, 2, "Gas masks. Aim for the parts that aren't mask."},
    {AR_JUNE, 3, 3, "Go. I'll keep a lamp lit on the road."},
    {AR_JUNE, 4, 4, "One of the new boys wants to learn to shoot. Not yet. Maybe never, if you do your job."},
    {AR_JUNE, 5, 5, "Last string before the snow. Make it a good one.", {{IT_AMMO9, 8}}},

    {AR_DEE, 0, 0, "Big Dee. I keep the iron warm.|Lift with me. Strong arms swing harder, throw further, and knock "
                   "people flat.|Press when the bar's in the green. Breathe out. Don't be a hero."},
    {AR_DEE, 1, 1, "You look strong. Look stronger."},
    {AR_DEE, 2, 2, "The brutes at Build-Rite? Big doesn't mean fast. Duck, then swing."},
    {AR_DEE, 3, 3, "I carried Theo to bed. He weighs nothing. Nothing at all. Go."},
    {AR_DEE, 4, 4, "The new fellas asked if I'd train them. I said after you."},
    {AR_DEE, 5, 5, "Whatever happens in there: lift with your legs. And come home."},

    {AR_MARTA, 0, 0, "Marta. I keep things growing. Beans, mostly. People, sometimes.|If you come back bleeding, you "
                     "come to me first, understand?|Here. Clean rags make clean wounds.", {{IT_BANDAGE, 1}}},
    {AR_MARTA, 1, 1, "Rice and beans. Do you know what I can do with rice and beans? Everything."},
    {AR_MARTA, 2, 2, "If we only had seeds. Real ones, not these tired old beans...|Don't mind me. Go on."},
    {AR_MARTA, 3, 3, "Antibiotics. Amoxicillin if they have it. Anything ending in -cillin.|I'm keeping him cool. "
                     "Go. And take this.", {{IT_BANDAGE, 1}}},
    {AR_MARTA, 4, 4, "He'll be fine. You were fast. I'll make soup for everyone.|For the road.", {{IT_PAINKILLERS, 1}}},
    {AR_MARTA, 5, 5, "Tomatoes. Beans. Squash. Bring me spring."},

    /* the neighbours (any AR_FOLK_*) */
    {AR_FOLK_A, 0, 5, "Evening. Rosa says you're the one going out. Rather you than me."},
    {AR_FOLK_A, 0, 5, "The roof leaks on the east side. Glass ain't what it used to be."},
    {AR_FOLK_A, 0, 5, "My boy wants to be you when he grows up. I told him to be a dentist."},
    {AR_FOLK_A, 1, 5, "You smell like a supermarket. A dead one."},
    {AR_FOLK_A, 0, 5, "Heard gunshots from the interstate last night. Stay off it if you can."},
    {AR_FOLK_A, 2, 5, "Generator or no generator, I'm sleeping by the fire."},
    {AR_FOLK_A, 3, 3, "Is Theo going to be alright? ...Sorry. Go. Go."},
    {AR_FOLK_A, 4, 5, "We came in last night. Thank you for taking us in. Thank you for... going."},
    {AR_FOLK_A, 5, 5, "Bring them home. The seeds. And yourself."},
    {AR_FOLK_A, 0, 5, "We're out of salt. It's not on the list. Just saying."},
};

static const struct { int arch; const char *text; } AGAIN[] = {
    {AR_ROSA, "The van's waiting. So am I."},
    {AR_THEO, "Kevin says hi. Kevin is a bean."},
    {AR_GUS, "Bench is yours. Mind your fingers."},
    {AR_JUNE, "Range is open. One string a night."},
    {AR_DEE, "One set a night. Muscles grow while you sleep."},
    {AR_MARTA, "Mind the beans."},
};

/* what they say when you take a favour on, or turn it down */
static const struct { int arch; const char *yes, *no; } REPLY[] = {
    {AR_ROSA, "...Thank you. Don't tell anyone I asked.", "No. Of course. The list comes first."},
    {AR_THEO, "YES! You're the best. Don't tell Rosa.", "Okay... Maybe next time. Rosa says that a lot."},
    {AR_GUS, "Atta kid. Mind the teeth on it.", "No skin off my nose. Padlocks fear me anyway."},
    {AR_JUNE, "Good. Don't get shot over it.", "Fair. Keep your eyes on the list."},
    {AR_DEE, "That's the spirit. Protein!", "Fair enough. Beans it is."},
    {AR_MARTA, "Bless you. Be careful back there.", "I understand. You've enough to carry."},
};

static bool is_folk(int arch) { return arch >= AR_FOLK_A; }
static int crew_of(int arch) { return arch >= AR_HOLLIS && arch <= AR_WES ? arch - AR_HOLLIS : -1; }

/* ================================================================== state */
typedef enum { HA_NONE, HA_TALK, HA_BENCH, HA_RANGE, HA_COURSE, HA_WORKBENCH } HubMode;

#define RANGE_TIME 25.0f
#define RANGE_MAG 10
#define BENCH_REPS 12
#define XP_CAP 120

typedef struct { bool up; V2 pos; float life, max, down_t; int kind; int spr; } Target;   /* kind 0 bottle, 1 board */

static struct {
    HubMode mode;
    float t;                       /* time in this mode */
    /* talking */
    int who;                       /* actor, or -1 for Theo in his sickbed */
    int arch;
    const Line *line;
    int ask, thank, missed;        /* the favour asked for / handed over / gone without in this talk, or -1 */
    int sel;                       /* the answer picked on the asking page: 0 take it on, 1 turn it down */
    bool answered;
    char text[1600];
    const char *pages[16];
    int npages, page;
    float type_t;
    /* the bench */
    float needle, ndir, speed, zone_c, zone_w, flash, shake;
    int reps, misses;
    float points;
    bool perfect;
    /* the range */
    int shots, hits, last_cond;
    float points_r, spawn_t, ready_t;
    Target tg[HUB_TARGETS];
    bool live[MAX_BULLETS];
    /* the course */
    int next;
    float countdown, run_t, par;
    /* the card after a session */
    int card_stat, card_xp, card_lv0, card_lv1;
    float card_t;
    char card_note[80];
    /* talking to one of the crew: who (CREW index, or -1), and whether they said tonight's piece in this talk */
    int crew;
    bool crew_line;
    int choice;                    /* what the last page asks: CH_* */
    /* the workbench */
    int wb_foc, wb_w, wb_m;
    char wb_msg[96];
    float wb_msg_t;
    float flag_t;
    /* nobody walks round the camp armed: your weapon is packed for the run, out only on the bag and workbench screens */
    Stack pack;
    bool unpacked;
} H;

/* the question on the last page of a talk: take a favour on, ask one of the crew along, keep them along or not */
enum { CH_NONE, CH_FAVOUR, CH_CREW_ASK, CH_CREW_STAY };

static bool done(uint32_t bit) { return (RUN.hub_done & bit) != 0; }

/* the camp's named people (and the crew) say their piece once an evening; the neighbours always have something */
static bool heard(int arch) {
    if (crew_of(arch) >= 0) return done(HD_CREW(crew_of(arch)));
    return !is_folk(arch) && done(HD_HEARD(arch - AR_ROSA));
}

static int have_mat(ItemId id);
static void take_mat(ItemId id, int n);
static int hub_stow(Stack w);

/* ================================================================== favours */
/* tonight's favour from them that you haven't taken on: not asked yet (or you walked off before answering), or turned
   down - they ask while they say their piece, and again whenever you come back */
static int favour_to_ask(int arch) {
    if (is_folk(arch)) return -1;
    for (int f = 0; f < NUM_FAVOURS; f++)
        if (FAVOURS[f].arch == arch && FAVOURS[f].level == RUN.level && (RUN.favour[f] == FS_NONE || RUN.favour[f] == FS_DECLINED))
            return f;
    return -1;
}

/* a favour you brought home and have on you (bag or locker): talk to them to hand it over */
static int favour_to_give(int arch) {
    for (int f = 0; f < NUM_FAVOURS; f++)
        if (FAVOURS[f].arch == arch && RUN.favour[f] == FS_FOUND && have_mat(FAVOURS[f].item) >= FAVOURS[f].n) return f;
    return -1;
}

/* a favour you came home without: they'll mention it */
static int favour_missed(int arch) {
    for (int f = 0; f < NUM_FAVOURS; f++)
        if (FAVOURS[f].arch == arch && RUN.favour[f] == FS_MISSED) return f;
    return -1;
}

/* ================================================================== people */
static void spawn_person(int arch, V2 at) {
    int i = actor_spawn(arch, at);
    if (i < 0) return;
    Actor *a = &W.actors[i];
    a->br.home = at;
    a->br.state = AI_IDLE;
    a->br.timer = frange(0.5f, 4);
    a->br.wander_t = frange(-PI_F, PI_F);
    a->face = a->br.wander_t;
    a->spawn_grace = 0;
}

static void spawn_camp(void) {
    static const int named[] = {AR_ROSA, AR_GUS, AR_JUNE, AR_DEE, AR_MARTA, AR_THEO};
    for (int i = 0; i < ARRAY_LEN(named); i++) {
        if (named[i] == AR_THEO && HUB.theo_sick) continue;   /* in bed: talked to where he lies */
        spawn_person(named[i], HUB.post[named[i]]);
    }
    /* the crew still living: on watch, or waiting at the van if they're coming along */
    for (int k = 0; k < MAX_CREW; k++)
        if (RUN.crew[k] != CR_DEAD) spawn_person(AR_HOLLIS + k, RUN.crew[k] == CR_SQUAD ? HUB.crew_van[k] : HUB.post[AR_HOLLIS + k]);
    /* the neighbours: more of them as the camp grows (two new families before MegaMart) */
    static const int folk[] = {AR_FOLK_A, AR_FOLK_B, AR_FOLK_C, AR_FOLK_B, AR_FOLK_A};
    V2 extra[2] = {v2(21 * TILE, 29 * TILE + 8), v2(38 * TILE, 13 * TILE + 8)};
    int n = RUN.level < 1 ? 1 : RUN.level < 2 ? 2 : RUN.level < 4 ? 3 : 5;
    for (int i = 0; i < n; i++) spawn_person(folk[i], i < 3 ? HUB.post[folk[i]] : extra[i - 3]);
}

static bool station_spot(V2 g);

void hub_npc_update(Actor *a, int idx, float dt) {
    Actor *p = player();
    Brain *b = &a->br;
    V2 to_p = v2_sub(p->pos, a->pos);
    float d = v2_len(to_p);
    if (H.mode == HA_TALK && H.who == idx) {
        a->vel = v2_scale(a->vel, expf(-12 * dt));
        face_towards(a, v2_to_angle(to_p), 10, dt);
        return;
    }
    /* somebody walks up to them: they stop what they're doing and look round (not when you're tearing past, and not
       on a station - they'd stand between you and it, and [E] would only ever talk to them) */
    if (d < 34 && v2_len(p->vel) < 70 && !station_spot(a->pos)) {
        a->vel = v2_scale(a->vel, expf(-12 * dt));
        face_towards(a, v2_to_angle(to_p), 6, dt);
        if (b->state == AI_WANDER) b->path_i = b->path_len;   /* forget the walk */
        return;
    }
    int k = crew_of(a->arch);
    if (k >= 0) b->home = RUN.crew[k] == CR_SQUAD ? HUB.crew_van[k] : HUB.post[a->arch];
    b->timer -= dt;
    if (b->state == AI_WANDER) {
        bool theo = a->arch == AR_THEO;
        float sp = theo && b->burst_n ? ARCH[a->arch].run : ARCH[a->arch].walk;
        if (follow(a, sp * a->speed_mul, dt) || b->timer < -8) {
            b->state = AI_IDLE;
            b->timer = theo ? frange(1, 3) : frange(3, 8);
            b->wander_t = a->face;
        } else if (v2_len(a->vel) > 5) face_towards(a, v2_to_angle(a->vel), 8, dt);
        return;
    }
    a->vel = v2_scale(a->vel, expf(-10 * dt));
    if (chance(dt * 0.25f)) b->wander_t = a->face + frange(-1.2f, 1.2f);   /* look about */
    face_towards(a, b->wander_t, 3, dt);
    if (b->timer > 0) return;
    float roam = a->arch == AR_THEO ? 110 : (a->arch == AR_ROSA ? 22 : 44);
    if (k >= 0) roam = RUN.crew[k] == CR_SQUAD ? 6 : 28;   /* on watch they pace; at the van they wait */
    for (int t = 0; t < 8; t++) {
        V2 g = v2_add(b->home, v2(frange(-roam, roam), frange(-roam, roam)));
        int tx = tile_of(g.x), ty = tile_of(g.y);
        if (!in_map(tx, ty) || !walkable_tile(tx, ty) || station_spot(g)) continue;
        if (!path_to(a, g)) continue;
        b->state = AI_WANDER;
        b->timer = 0;
        b->burst_n = a->arch == AR_THEO && chance(0.5f);   /* Theo runs everywhere */
        return;
    }
    b->timer = frange(1, 3);
}

/* who [E] would talk to: the closest of the camp's people in reach (Theo in his sickbed has no body to walk) */
static int talk_target(int *arch, V2 *at) {
    Actor *p = player();
    int best = -2;
    float bd = 26 * 26;
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive || !(is_camp(a) || is_crew(a))) continue;
        float d = v2_dist2(a->pos, p->pos);
        if (d < bd && reach_clear(p->pos, a->pos)) { bd = d; best = i; *arch = a->arch; *at = a->pos; }
    }
    if (HUB.theo_sick && v2_dist2(HUB.post[AR_THEO], p->pos) < (best >= 0 ? bd : 30 * 30)) {
        best = -1;
        *arch = AR_THEO;
        *at = HUB.post[AR_THEO];
    }
    return best;
}

static const Line *line_for(int arch, int who) {
    int lv = RUN.level;
    if (is_folk(arch)) {
        const Line *pick[ARRAY_LEN(LINES)];
        int n = 0;
        for (int i = 0; i < ARRAY_LEN(LINES); i++)
            if (LINES[i].arch == AR_FOLK_A && lv >= LINES[i].lv0 && lv <= LINES[i].lv1) pick[n++] = &LINES[i];
        return n ? pick[(who * 7 + lv * 3) % n] : NULL;
    }
    for (int i = 0; i < ARRAY_LEN(LINES); i++)
        if (LINES[i].arch == arch && lv >= LINES[i].lv0 && lv <= LINES[i].lv1) return &LINES[i];
    return NULL;
}

static const char *name_of(int arch) { return ARCH[arch].name; }

/* ================================================================== the card after a session */
static void award(int stat, int xp, const char *note) {
    xp = CLAMP(xp, 0, XP_CAP);
    H.card_stat = stat;
    H.card_xp = xp;
    H.card_lv0 = train_level(stat);
    RUN.xp[stat] += xp;
    H.card_lv1 = train_level(stat);
    H.card_t = H.card_lv1 > H.card_lv0 ? 4.5f : 3.2f;
    SDL_strlcpy(H.card_note, note, sizeof H.card_note);
    SDL_Log("HUB: %s +%d xp (%s) -> level %d", TRAIN[stat].name, xp, note, H.card_lv1);
    audio_play(H.card_lv1 > H.card_lv0 ? SFX_PERK : SFX_LEVEL_CLEAR, 0.75f, 0, 1);
    hub_sync_run();
    run_save();   /* the evening's work is written down at once */
}

/* ================================================================== talking */
/* Rosa's last page: tonight's list, read out */
static void list_page(char *buf, int n) {
    const LevelDef *d = &LEVELS[RUN.level];
    int nm = 0, k = 0;
    for (int i = 0; i < 3; i++) if (d->must[i].id) nm++;
    SDL_strlcpy(buf, "Here's the list: ", n);
    for (int i = 0; i < 3; i++) {
        if (!d->must[i].id) continue;
        char one[48];
        if (d->must[i].n > 1) SDL_snprintf(one, sizeof one, "%s x%d", ITEMS[d->must[i].id].name, d->must[i].n);
        else SDL_strlcpy(one, ITEMS[d->must[i].id].name, sizeof one);
        if (k) SDL_strlcat(buf, k == nm - 1 ? " and " : ", ", n);
        SDL_strlcat(buf, one, n);
        k++;
    }
    SDL_strlcat(buf, nm > 1 || d->must[0].n > 1 ? ". Bring those home, and whatever else you can carry."
                                                : ". Bring that home, and whatever else you can carry.", n);
}

/* Rosa, after the list: how many of the crew may come (the first evening they can, what that means) */
static void crew_rule_page(char *buf, int n) {
    const LevelDef *d = &LEVELS[RUN.level];
    buf[0] = 0;
    if (d->crew_max <= 0) return;
    bool first = RUN.level == 0 || LEVELS[RUN.level - 1].crew_max <= 0;
    if (first)
        SDL_strlcpy(buf, "One more thing. From tonight you don't have to go alone. The crew who keep our gate will go with you "
                         "if you ask them - and they'll fight beside you.|", n);
    char rule[192];
    if (d->crew_min > 0)
        SDL_snprintf(rule, sizeof rule, "Not alone, not to %s. Take at least %s of the crew - %s at most. The rest hold the gate.",
                     STORE_SAID[RUN.level], NUM_WORD[d->crew_min], NUM_WORD[d->crew_max]);
    else if (d->crew_max == 1)
        SDL_strlcpy(rule, "Take one of the crew along if you want. Just the one - the gate doesn't keep itself.", sizeof rule);
    else SDL_snprintf(rule, sizeof rule, "Take up to %s of the crew if you want them. The rest hold the gate.", NUM_WORD[d->crew_max]);
    SDL_strlcat(buf, rule, n);
    if (first) SDL_strlcat(buf, "|And listen. Whoever dies out there is gone. If there's nobody left on the gate, so are we.", n);
}

/* one of the crew: a word for whoever died last time, tonight's piece, then the question - along, or on the gate */
static void crew_talk(int k) {
    const CrewDef *c = &CREW[k];
    const LevelDef *d = &LEVELS[RUN.level];
    char page[256];
    int gone = -1;
    for (int j = 0; j < MAX_CREW && gone < 0; j++)
        if (j != k && RUN.crew[j] == CR_DEAD && RUN.crew_fell[j] + 1 >= RUN.level) gone = j;
    if (!heard(c->arch)) {
        if (gone >= 0) {
            SDL_snprintf(page, sizeof page, c->grief, ARCH[CREW[gone].arch].name);
            SDL_strlcat(H.text, page, sizeof H.text);
            SDL_strlcat(H.text, "|", sizeof H.text);
        }
        SDL_strlcat(H.text, c->line[RUN.level], sizeof H.text);
        H.crew_line = true;
    } else {
        SDL_strlcat(H.text, c->again, sizeof H.text);
    }
    if (d->crew_max <= 0) return;   /* tonight's a job for one: they've said so */
    if (RUN.crew[k] == CR_SQUAD) {
        SDL_snprintf(page, sizeof page, "|I'm with you for %s. Still want me along?", STORE_SAID[RUN.level]);
        H.choice = CH_CREW_STAY;
    } else if (crew_squad() >= d->crew_max) {
        SDL_snprintf(page, sizeof page, "|The van's full tonight - %s of us is all Rosa lets go. I'll keep the gate.",
                     NUM_WORD[d->crew_max]);
    } else {
        page[0] = '|';
        SDL_snprintf(page + 1, sizeof page - 1, c->offer, STORE_SAID[RUN.level]);
        H.choice = CH_CREW_ASK;
    }
    SDL_strlcat(H.text, page, sizeof H.text);
}

static void talk_open(int who, int arch) {
    H.mode = HA_TALK;
    H.t = 0;
    H.who = who;
    H.arch = arch;
    H.line = NULL;
    H.type_t = 0;
    H.page = 0;
    H.sel = 0;
    H.answered = false;
    H.text[0] = 0;
    H.crew = crew_of(arch);
    H.crew_line = false;
    H.choice = CH_NONE;
    H.thank = H.missed = H.ask = -1;
    if (H.crew >= 0) crew_talk(H.crew);
    else {
        /* a favour comes first - handed over, or gone without - then whatever they meant to say tonight */
        H.thank = favour_to_give(arch);
        H.missed = H.thank < 0 ? favour_missed(arch) : -1;
        H.ask = favour_to_ask(arch);
        if (H.thank >= 0) SDL_strlcpy(H.text, FAVOURS[H.thank].thanks, sizeof H.text);
        else if (H.missed >= 0) SDL_strlcpy(H.text, FAVOURS[H.missed].missed, sizeof H.text);
        const char *text = NULL;
        if (!heard(arch)) {
            H.line = line_for(arch, who);
            if (H.line) text = H.line->text;
        }
        if (!text && !H.text[0] && H.ask < 0) {
            for (int i = 0; i < ARRAY_LEN(AGAIN); i++) if (AGAIN[i].arch == arch) text = AGAIN[i].text;
            if (arch == AR_THEO && HUB.theo_sick) text = "...zzz. ...Kevin...";
        }
        if (text) {
            if (H.text[0]) SDL_strlcat(H.text, "|", sizeof H.text);
            SDL_strlcat(H.text, text, sizeof H.text);
        }
        if (H.line && arch == AR_ROSA) {
            char list[200], crew[480];
            list_page(list, sizeof list);
            SDL_strlcat(H.text, "|", sizeof H.text);
            SDL_strlcat(H.text, list, sizeof H.text);
            crew_rule_page(crew, sizeof crew);
            if (crew[0]) {
                SDL_strlcat(H.text, "|", sizeof H.text);
                SDL_strlcat(H.text, crew, sizeof H.text);
            }
        }
        if (H.ask >= 0) {   /* the last page asks: take it on, or not */
            char again[96];
            SDL_snprintf(again, sizeof again, "So... the %s. Changed your mind?", ITEMS[FAVOURS[H.ask].item].name);
            if (H.text[0]) SDL_strlcat(H.text, "|", sizeof H.text);
            SDL_strlcat(H.text, RUN.favour[H.ask] == FS_DECLINED ? again : FAVOURS[H.ask].ask, sizeof H.text);
            H.choice = CH_FAVOUR;
        }
    }
    if (!H.text[0]) SDL_strlcpy(H.text, "...", sizeof H.text);
    H.npages = 0;
    char *s = H.text;
    H.pages[H.npages++] = s;
    for (; *s && H.npages < ARRAY_LEN(H.pages); s++)
        if (*s == '|') { *s = 0; H.pages[H.npages++] = s + 1; }
    W.hub_lock = HUB_HELD;
    audio_play(SFX_UI_SELECT, 0.6f, 0, 0.9f);
}

static void give_mods(ItemCount g, int mods) {
    if (!g.id || g.n <= 0) return;
    Actor *p = player();
    char buf[64];
    if (ITEMS[g.id].cat == CAT_BAG) {   /* a bag: worn instead of yours, if it's bigger */
        if (bag_capacity(g.id) <= bag_capacity(RUN.bag)) return;
        RUN.bag = g.id;
        SDL_snprintf(buf, sizeof buf, "%s! (%d SPACE)", ITEMS[g.id].name, bag_capacity(g.id));
        floater(v2(p->pos.x, p->pos.y - 25), buf, COL_YELLOW, false);
        return;
    }
    Stack s = {(int16_t)g.id, (int16_t)g.n, 0, (int16_t)(item_is_weapon(g.id) ? mods : 0)};
    if (item_is_weapon(g.id)) {
        s.cond = (int16_t)weapon_max_cond(&s);
        hub_stow(s);
    } else if (!inv_add(p, s)) {
        int pi = pickup_spawn(s, p->pos, v2(frange(-20, 20), frange(10, 30)));
        if (pi >= 0) W.pickups[pi].dropped = true;
    }
    if (g.n > 1) SDL_snprintf(buf, sizeof buf, "+ %s x%d", ITEMS[g.id].name, g.n);
    else SDL_snprintf(buf, sizeof buf, "+ %s", ITEMS[g.id].name);
    floater(v2(p->pos.x, p->pos.y - 16 - 9 * (p->ninv & 1)), buf, COL_YELLOW, false);
}

static void give(ItemCount g) { give_mods(g, 0); }

static void talk_close(void) {
    Actor *p = player();
    bool save = false;
    if (H.thank >= 0) {   /* a favour handed over: they take it, you get what they promised */
        const FavourDef *f = &FAVOURS[H.thank];
        RUN.favour[H.thank] = FS_DONE;
        take_mat(f->item, f->n);
        give_mods(f->reward[0], f->mods);
        give_mods(f->reward[1], f->mods);
        audio_play(SFX_PERK, 0.8f, 0, 1.1f);
        SDL_Log("HUB: favour - %s x%d handed to %s", ITEMS[f->item].name, f->n, name_of(f->arch));
        if (f->xp > 0) {
            char note[80];
            SDL_snprintf(note, sizeof note, "A favour for %s", name_of(f->arch));
            award(f->stat, f->xp, note);
        }
        save = true;
    }
    if (H.missed >= 0) { RUN.favour[H.missed] = FS_DONE; save = true; }
    if (H.crew_line) RUN.hub_done |= HD_CREW(H.crew);
    if (H.line && !is_folk(H.arch)) {
        RUN.hub_done |= HD_HEARD(H.arch - AR_ROSA);
        uint32_t bit = HD_GIFT(H.arch - AR_ROSA);
        if ((H.line->gift[0].id || H.line->gift[1].id) && !done(bit)) {
            RUN.hub_done |= bit;
            give(H.line->gift[0]);
            give(H.line->gift[1]);
            audio_play(SFX_PICKUP, 0.9f, 0, 1);
        }
        if (H.arch == AR_ROSA) {   /* no list, no van */
            floater(v2(p->pos.x, p->pos.y - 34), "+ SHOPPING LIST", COL_RECEIPT, false);
            audio_play(SFX_LIST_TICK, 0.9f, 0, 1);
            save = true;
        }
    }
    H.mode = HA_NONE;
    W.hub_lock = HUB_FREE;
    if (save) { hub_sync_run(); run_save(); }
}

static int page_len(const char *s) { int n = 0; for (; *s; s++) if (*s != '\n') n++; return n; }

/* on the page that asks a favour, typed out: the answer is up to you */
static bool talk_choosing(void) {
    return H.mode == HA_TALK && H.choice != CH_NONE && !H.answered && H.page == H.npages - 1 && H.type_t >= page_len(H.pages[H.page]);
}

/* the two answers, bottom right of the talk box: [0] yes / keep them along, [1] no / leave them home */
static const char *answer_label(int i) {
    static const char *const LABEL[][2] = {
        [CH_FAVOUR] = {"I'LL LOOK", "NOT THIS TIME"},
        [CH_CREW_ASK] = {"COME WITH ME", "NOT TONIGHT"},
        [CH_CREW_STAY] = {"YOU'RE COMING", "STAY HERE"},
    };
    return LABEL[H.choice == CH_NONE ? CH_FAVOUR : H.choice][i & 1];
}

static SDL_FRect answer_rect(int i) {
    float x = 40, w = VIEW_W - 80, h = 66, y = VIEW_H - h - 40;
    float w1 = gfx_text_w(FONT_SMALL, answer_label(1)) + 10, w0 = gfx_text_w(FONT_SMALL, answer_label(0)) + 10;
    float r = x + w - 10;
    return i == 1 ? (SDL_FRect){r - w1, y + h - 16, w1, 11} : (SDL_FRect){r - w1 - 6 - w0, y + h - 16, w0, 11};
}

/* one of the crew: along tonight (they go and wait at the van), or on the gate */
static void crew_answer(bool yes) {
    int k = H.crew;
    const CrewDef *c = &CREW[k];
    Actor *p = player();
    const char *say;
    if (H.choice == CH_CREW_ASK && yes) {
        RUN.crew[k] = CR_SQUAD;
        say = c->yes;
        char buf[48];
        SDL_snprintf(buf, sizeof buf, "+ %s COMES ALONG", ARCH[c->arch].name);
        for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
        floater(v2(p->pos.x, p->pos.y - 43), buf, COL_GREEN, false);
        audio_play(SFX_PICKUP_WEAPON, 0.8f, 0, 0.9f);
    } else if (H.choice == CH_CREW_STAY && !yes) {
        RUN.crew[k] = CR_HOME;
        say = c->stay;
        audio_play(SFX_UI_BACK, 0.8f, 0, 1);
    } else {
        say = yes ? c->yes : c->no;
        audio_play(yes ? SFX_UI_SELECT : SFX_UI_BACK, 0.8f, 0, 1);
    }
    SDL_Log("HUB: crew - %s %s (%d of %d along, %d needed)", ARCH[c->arch].name, RUN.crew[k] == CR_SQUAD ? "comes along" : "stays on the gate",
            crew_squad(), LEVELS[RUN.level].crew_max, LEVELS[RUN.level].crew_min);
    if (H.who >= 0) W.actors[H.who].br.timer = 0;   /* off to the van, or back to their watch, once you step away */
    SDL_strlcpy(H.text, say, sizeof H.text);
    H.pages[0] = H.text;
    H.npages = 1;
    H.page = 0;
    H.type_t = 0;
    hub_sync_run();
    run_save();
}

/* took it on (it goes on the list, off the must-haves) or turned it down (ask them again tonight if you change your mind) */
static void answer(bool yes) {
    H.answered = true;
    if (H.choice != CH_FAVOUR) { crew_answer(yes); return; }
    const FavourDef *f = &FAVOURS[H.ask];
    Actor *p = player();
    RUN.favour[H.ask] = yes ? FS_OPEN : FS_DECLINED;
    SDL_Log("HUB: favour - %s %s %s x%d (%s)", yes ? "took on" : "turned down", name_of(f->arch), ITEMS[f->item].name, f->n,
            f->where == FW_CARRIED ? "carried" : "hidden");
    if (yes) {
        char buf[64];
        SDL_snprintf(buf, sizeof buf, "+ FAVOUR: %s", ITEMS[f->item].name);
        floater(v2(p->pos.x, p->pos.y - 43), buf, COL_GREEN, false);
        audio_play(SFX_LIST_TICK, 0.9f, 0, 1.15f);
    } else {
        audio_play(SFX_UI_BACK, 0.8f, 0, 1);
    }
    /* their answer to yours */
    const char *say = yes ? "Thanks." : "Alright.";
    for (int i = 0; i < ARRAY_LEN(REPLY); i++) if (REPLY[i].arch == f->arch) say = yes ? REPLY[i].yes : REPLY[i].no;
    SDL_strlcpy(H.text, say, sizeof H.text);
    H.pages[0] = H.text;
    H.npages = 1;
    H.page = 0;
    H.type_t = 0;
    hub_sync_run();
    run_save();
}

/* [ESC]: walk off - or, where they're asking, say no (one of the crew already coming stays coming) */
static void talk_back(void) {
    if (talk_choosing()) answer(H.choice == CH_CREW_STAY);
    else talk_close();
}

static void talk_update(float dt) {
    int before = (int)H.type_t;
    H.type_t += dt * 60;
    int len = page_len(H.pages[H.page]);
    if ((int)H.type_t != before && (int)H.type_t <= len && ((int)H.type_t % 3) == 0)
        audio_play(SFX_TYPE, 0.6f, 0, frange(0.95f, 1.2f));
    if (talk_choosing()) {
        if (IN.repeat[ACT_MENU_LEFT] || IN.repeat[ACT_MENU_RIGHT] || IN.repeat[ACT_MENU_UP] || IN.repeat[ACT_MENU_DOWN]) {
            H.sel ^= 1;
            audio_play(SFX_UI_MOVE, 0.7f, 0, 1);
        }
        V2 m = IN.mouse_view;
        for (int i = 0; i < 2; i++) {
            SDL_FRect r = answer_rect(i);
            if (m.x < r.x - 2 || m.x >= r.x + r.w + 2 || m.y < r.y - 3 || m.y >= r.y + r.h + 3) continue;
            if (IN.mouse_moved && H.sel != i) { H.sel = i; audio_play(SFX_UI_MOVE, 0.7f, 0, 1); }
            if (IN.click) { answer(i == 0); return; }
        }
        if (IN.pressed[ACT_INTERACT] || IN.pressed[ACT_CONFIRM]) answer(H.sel == 0);
        return;
    }
    if (IN.pressed[ACT_INTERACT] || IN.pressed[ACT_CONFIRM] || IN.click || IN.pressed[ACT_ATTACK]) {
        if (H.type_t < len) { H.type_t = 9999; return; }
        if (++H.page >= H.npages) { talk_close(); return; }
        H.type_t = 0;
        audio_play(SFX_UI_MOVE, 0.6f, 0, 1);
    }
}

static void talk_draw(void) {
    float x = 40, w = VIEW_W - 80, h = 66, y = VIEW_H - h - 40;
    gfx_nine(SPR_UI_PAPER, x, y, w, h, TINT_NONE);
    gfx_spr(SPR_UI_TAPE, x + w / 2, y + 1);
    char name[32];
    SDL_strlcpy(name, is_folk(H.arch) ? "Neighbour" : name_of(H.arch), sizeof name);
    for (char *q = name; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
    float nw = gfx_text_w(FONT_SMALL, name) + 8;
    gfx_sticker(x + 10, y - 6, nw, 11, COL_YELLOW);
    gfx_text(FONT_SMALL, name, x + 14, y - 4, COL_BLACK, 0);
    gfx_text_wrap_n(FONT_SMALL, H.pages[H.page], x + 14, y + 12, (int)w - 28, INK, 0, 10, (int)H.type_t);
    if (talk_choosing()) {
        for (int i = 0; i < 2; i++) {
            SDL_FRect r = answer_rect(i);
            bool sel = H.sel == i;
            gfx_sticker(r.x, r.y, r.w, r.h, sel ? COL_YELLOW : rgba(214, 206, 188, 255));
            gfx_text(FONT_SMALL, answer_label(i), r.x + 5, r.y + 2, sel ? COL_BLACK : INK_FADE, 0);
        }
        gfx_text(FONT_SMALL, ctl("^k[E] answer", "^k[A] answer"), answer_rect(0).x - 8, answer_rect(0).y + 2, INK_FADE, TXT_RIGHT);
    } else if (H.type_t >= page_len(H.pages[H.page])) {
        bool last = H.page >= H.npages - 1;
        Color c = ((int)(G.time * 2) & 1) ? INK : INK_FADE;
        gfx_text(FONT_SMALL, last ? ctl("^0[E] done", "^0[A] done") : ctl("^0[E] more", "^0[A] more"), x + w - 12, y + h - 13, c, TXT_RIGHT);
    }
}

/* ================================================================== the weight bench (strength) */
static void bench_new_zone(void) {
    H.zone_w = MAXF(0.10f, 0.30f - H.reps * 0.018f);
    H.zone_c = frange(0.15f + H.zone_w / 2, 0.85f - H.zone_w / 2);
}

static void bench_start(void) {
    Actor *p = player();
    RUN.hub_done |= HD_TRAINED(STAT_STR);   /* the evening's set is used the moment you lie down */
    H.mode = HA_BENCH;
    H.t = 0;
    H.reps = H.misses = 0;
    H.points = 0;
    H.needle = 0;
    H.ndir = 1;
    H.speed = 0.85f;
    H.flash = 0;
    bench_new_zone();
    p->pos = v2(HUB.bench.x + 7, HUB.bench.y);
    p->vel = v2(0, 0);
    p->face = p->aim = PI_F;
    W.hub_lock = HUB_HELD;
    audio_play(SFX_PICKUP_WEAPON, 0.7f, 0, 0.6f);
}

static void bench_finish(void) {
    Actor *p = player();
    H.mode = HA_NONE;
    W.hub_lock = HUB_FREE;
    p->pos = v2(HUB.bench.x + 24, HUB.bench.y + 4);
    char note[80];
    SDL_snprintf(note, sizeof note, "%d rep%s with Dee", H.reps, H.reps == 1 ? "" : "s");
    award(STAT_STR, (int)(H.points * 9), note);
}

static void bench_update(float dt) {
    Actor *p = player();
    p->pos = v2(HUB.bench.x + 7, HUB.bench.y);   /* nobody shoves you off the bench */
    p->push = v2(0, 0);
    H.flash = MAXF(0, H.flash - dt * 3);
    H.shake = MAXF(0, H.shake - dt * 4);
    if (H.t < 0.8f) return;   /* settle on the bench */
    H.needle += H.ndir * H.speed * dt;
    if (H.needle > 1) { H.needle = 1; H.ndir = -1; }
    if (H.needle < 0) { H.needle = 0; H.ndir = 1; }
    if (IN.pressed[ACT_INTERACT] || IN.pressed[ACT_CONFIRM] || IN.pressed[ACT_ATTACK] || IN.click) {
        float off = fabsf(H.needle - H.zone_c);
        if (off <= H.zone_w / 2) {
            H.perfect = off <= H.zone_w * 0.15f;
            H.points += H.perfect ? 1.5f : 1.0f;
            H.reps++;
            H.flash = 1;
            H.speed += 0.09f;
            audio_play(SFX_HIT_METAL, 0.6f, 0, H.perfect ? 0.75f : 0.6f);
            floater(v2(player()->pos.x, player()->pos.y - 18), H.perfect ? "PERFECT" : "REP", H.perfect ? COL_YELLOW : COL_WHITE, false);
            bench_new_zone();
        } else {
            H.misses++;
            H.shake = 1;
            add_shake(3);
            audio_play(SFX_UI_ERROR, 0.6f, 0, 0.8f);
        }
        if (H.misses >= 3 || H.reps >= BENCH_REPS) bench_finish();
    }
}

static void bench_draw(void) {
    float w = 220, h = 62, x = VIEW_W / 2 - w / 2, y = 34;
    float sh = H.shake > 0 ? sinf(G.time * 70) * 2 * H.shake : 0;
    gfx_nine(SPR_UI_PANEL, x + sh, y, w, h, TINT_NONE);
    gfx_text(FONT_SMALL, "BENCH PRESS", x + 10 + sh, y + 7, COL_YELLOW, 0);
    char buf[48];
    SDL_snprintf(buf, sizeof buf, "REPS %d/%d", H.reps, BENCH_REPS);
    gfx_text(FONT_SMALL, buf, x + w - 10 + sh, y + 7, COL_WHITE, TXT_RIGHT);
    /* breaths left: three strained presses and you're done */
    for (int i = 0; i < 3; i++) gfx_spr(SPR_UI_HEART + (i < 3 - H.misses ? 0 : 2), x + w / 2 - 14 + i * 10 + sh, y + 6);
    float bx = x + 14 + sh, by = y + 24, bw = w - 28, bh = 12;
    gfx_fill(bx - 1, by - 1, bw + 2, bh + 2, COL_BLACK);
    gfx_fill(bx, by, bw, bh, rgba(60, 52, 60, 255));
    float zx = bx + (H.zone_c - H.zone_w / 2) * bw, zw = H.zone_w * bw;
    gfx_fill(zx, by, zw, bh, color_lerp(rgb(70, 120, 50), COL_GREEN, H.flash));
    gfx_fill(bx + H.zone_c * bw - zw * 0.15f, by, zw * 0.3f, bh, color_lerp(COL_GREEN, COL_YELLOW, 0.5f));
    float nx = bx + H.needle * bw;
    gfx_fill(nx - 1, by - 3, 3, bh + 6, COL_WHITE);
    gfx_fill(nx, by - 3, 1, bh + 6, COL_BLACK);
    const char *help = H.t < 0.8f ? "Get a grip..." : ctl("^yE / LMB^0 press when it's in the green", "^yA / RT^0 press when it's in the green");
    gfx_text(FONT_SMALL, help, x + w / 2 + sh, y + h - 16, COL_WHITE, TXT_CENTER);
}

/* ================================================================== the range (aim) */
static const int BOTTLES[] = {SPR_I_BOTTLE, SPR_I_SODA, SPR_I_BEANS, SPR_I_SOUP, SPR_I_VODKA};

static void target_show(int i) {
    Target *t = &H.tg[i];
    int pi = HUB.target_prop[i];
    if (pi < 0) return;
    Prop *pr = &W.props[pi];
    pr->spr = t->spr;
    pr->x = t->pos.x;
    pr->y = t->pos.y;
    float a = 0;
    if (t->up && t->kind == 0) a = 1;
    if (t->up && t->kind == 1) a = MINF(CLAMP((t->max - t->life) * 8, 0.0f, 1.0f), CLAMP(t->life * 4, 0.0f, 1.0f));   /* pops up, drops */
    pr->tint = rgba(255, 255, 255, (Uint8)(255 * a));
}

static void range_bottle(int i) {
    Target *t = &H.tg[i];
    t->kind = 0;
    t->up = true;
    t->max = t->life = 999;
    t->spr = BOTTLES[irange(0, ARRAY_LEN(BOTTLES) - 1)];
    V2 plank = HUB.planks[i / 3];
    t->pos = v2(plank.x - 10 + (i % 3) * 10, plank.y - 2);
}

static void range_board(int i) {
    Target *t = &H.tg[i];
    for (int tries = 0; tries < 20; tries++) {
        V2 p = v2(HUB.range.x + frange(8, HUB.range.w - 8), HUB.range.y + frange(8, HUB.range.h - 8));
        if (fabsf(p.y - HUB.planks[0].y) < 14 || fabsf(p.y - HUB.planks[1].y) < 14) continue;
        bool clash = false;
        for (int k = 0; k < HUB_TARGETS; k++) if (k != i && H.tg[k].up && v2_dist(H.tg[k].pos, p) < 18) clash = true;
        if (clash) continue;
        t->kind = 1;
        t->up = true;
        t->max = t->life = frange(1.6f, 2.2f);
        t->spr = SPR_P_TARGET;
        t->pos = p;
        play_at(SFX_DOOR_CREAK, p, 0.35f, 1.6f);
        return;
    }
}

static void range_start(void) {
    Actor *p = player();
    RUN.hub_done |= HD_TRAINED(STAT_AIM);
    H.mode = HA_RANGE;
    H.t = 0;
    H.shots = H.hits = 0;
    H.points_r = 0;
    H.ready_t = 2.0f;
    H.spawn_t = 0.5f;
    memset(H.tg, 0, sizeof H.tg);
    p->weapon = (Stack){IT_PISTOL, 1, RANGE_MAG, 0};   /* June's .22 - well, it's a 9mm, but June calls it that */
    p->reload_t = 0;
    p->atk_cd = 0.3f;
    H.last_cond = RANGE_MAG;
    p->pos = HUB.range_line;
    p->vel = v2(0, 0);
    p->face = p->aim = 0;
    W.hub_lock = HUB_HELD;
    W.cam_hold = true;   /* the whole lane in view */
    W.cam_at = v2(HUB.range_line.x + 76, HUB.range_line.y);
    for (int i = 0; i < 6; i++) range_bottle(i);
    for (int i = 0; i < HUB_TARGETS; i++) target_show(i);
    audio_play(SFX_RELOAD, 0.8f, 0, 1);
}

static void range_end(bool award_it) {
    Actor *p = player();
    p->weapon = (Stack){IT_NONE, 0, 0, 0};   /* June keeps her pistol */
    p->reload_t = 0;
    for (int i = 0; i < HUB_TARGETS; i++) { H.tg[i].up = false; target_show(i); }
    for (int i = 0; i < MAX_BULLETS; i++) W.bullets[i].alive = false;
    H.mode = HA_NONE;
    W.hub_lock = HUB_FREE;
    W.cam_hold = false;
    if (!award_it) return;
    float acc = H.shots ? (float)H.hits / H.shots : 0;
    char note[80];
    SDL_snprintf(note, sizeof note, "%d hits from %d shots (%d%%)", H.hits, H.shots, (int)(acc * 100 + 0.5f));
    award(STAT_AIM, (int)(H.points_r * 3 + acc * 30), note);
}

static void range_pre(float dt) {
    for (int i = 0; i < MAX_BULLETS; i++) H.live[i] = W.bullets[i].alive;
    if (H.ready_t > 0) {
        H.ready_t -= dt;
        if (H.ready_t <= 0) { W.hub_lock = HUB_AIM; H.t = 0; audio_play(SFX_ALERT, 0.6f, 0, 1.3f); }
    }
}

static void range_hit(int i, V2 at) {
    Target *t = &H.tg[i];
    t->up = false;
    t->down_t = t->kind == 0 ? 1.4f : frange(0.5f, 1.1f);
    H.hits++;
    int pts = t->kind == 0 ? 1 : 2;
    H.points_r += pts;
    if (t->kind == 0) {
        for (int k = 0; k < 6; k++) {   /* June sweeps up: the shards don't stay */
            Particle *g = particle_add(PT_GLASS, t->pos, v2(frange(10, 120), frange(-70, 70)), frange(0.4f, 0.8f));
            if (!g) break;
            g->spr = SPR_FX_GLASS + irange(0, 2);
            g->z = 6;
            g->vz = frange(30, 80);
        }
        play_at(t->spr == SPR_I_BOTTLE || t->spr == SPR_I_VODKA ? SFX_BOTTLE_BREAK : SFX_HIT_METAL, at, 0.8f, frange(1.0f, 1.3f));
    } else {
        for (int k = 0; k < 6; k++) {
            Particle *d = particle_add(PT_DEBRIS, t->pos, v2(frange(20, 110), frange(-60, 60)), 0.6f);
            if (!d) break;
            d->spr = SPR_FX_GIB + 1;
            d->col = rgb(176, 125, 79);
            d->z = 6;
            d->vz = frange(30, 70);
        }
        play_at(SFX_HIT_BLUNT, at, 0.8f, 1.3f);
    }
    char buf[8];
    SDL_snprintf(buf, sizeof buf, "+%d", pts);
    floater_sticker(v2(t->pos.x, t->pos.y - 6), buf, FL_PRICE);
}

static void range_post(float dt) {
    Actor *p = player();
    p->pos = HUB.range_line;   /* feet on the line: no drifting back with the recoil */
    p->push = v2(0, 0);
    if (p->weapon.cond < H.last_cond) H.shots += H.last_cond - p->weapon.cond;
    /* June hands you a fresh magazine every time */
    if (p->weapon.cond <= 0 && p->reload_t <= 0) {
        p->weapon.cond = RANGE_MAG;
        p->reload_t = 0.9f * (1.0f - 0.07f * train_level(STAT_AIM));
        audio_play(SFX_RELOAD, 0.7f, 0, 1);
    }
    H.last_cond = p->weapon.cond;
    /* what the shots went through this frame */
    for (int b = 0; b < MAX_BULLETS; b++) {
        Bullet *bl = &W.bullets[b];
        if (!H.live[b] || bl->owner != 0) continue;
        for (int i = 0; i < HUB_TARGETS; i++) {
            Target *t = &H.tg[i];
            if (!t->up) continue;
            float r = t->kind == 0 ? 5.5f : 7.5f;
            V2 ab = v2_sub(bl->pos, bl->prev);
            float l2 = v2_len2(ab);
            float k = l2 > 0 ? CLAMP(v2_dot(v2_sub(t->pos, bl->prev), ab) / l2, 0.0f, 1.0f) : 0;
            V2 c = v2_add(bl->prev, v2_scale(ab, k));
            if (v2_dist2(c, t->pos) > r * r) continue;
            range_hit(i, c);
            bl->alive = false;
            break;
        }
    }
    if (W.hub_lock == HUB_AIM) {
        float left = RANGE_TIME - H.t;
        /* boards pop up while there's time; knocked-off bottles go back on the plank */
        H.spawn_t -= dt;
        if (H.spawn_t <= 0 && left > 1.5f) {
            H.spawn_t = frange(1.2f, 2.0f);
            for (int i = 6; i < HUB_TARGETS; i++)
                if (!H.tg[i].up && H.tg[i].down_t <= 0) { range_board(i); break; }
        }
        for (int i = 0; i < HUB_TARGETS; i++) {
            Target *t = &H.tg[i];
            if (t->up && t->kind == 1 && (t->life -= dt) <= 0) { t->up = false; t->down_t = 0.4f; }
            if (!t->up && t->down_t > 0 && (t->down_t -= dt) <= 0 && i < 6 && left > 1.0f) range_bottle(i);
        }
        if (left <= 0) range_end(true);
    }
    for (int i = 0; i < HUB_TARGETS; i++) target_show(i);
}

static void range_draw(void) {
    char buf[96];
    if (H.ready_t > 0) {
        gfx_text_big(FONT_BIG, "READY...", VIEW_W / 2, 60, 2, COL_YELLOW, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
        gfx_text(FONT_SMALL, ctl("^yMOUSE^0 aim  ^yLMB^0 shoot - bottles 1, boards 2", "^yR STICK^0 aim  ^yRT^0 shoot - bottles 1, boards 2"),
                 VIEW_W / 2, 90, COL_WHITE, TXT_CENTER | TXT_OUTLINE);
        return;
    }
    float left = MAXF(0, RANGE_TIME - H.t);
    int acc = H.shots ? (int)(100.0f * H.hits / H.shots + 0.5f) : 0;
    SDL_snprintf(buf, sizeof buf, "%d.%d", (int)left, (int)(left * 10) % 10);
    gfx_text_big(FONT_BIG, buf, VIEW_W / 2, 8, 2, left < 5 ? COL_RED : COL_WHITE, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
    SDL_snprintf(buf, sizeof buf, "POINTS %d   HITS %d/%d   %d%%", (int)H.points_r, H.hits, H.shots, acc);
    gfx_text(FONT_SMALL, buf, VIEW_W / 2, 36, COL_YELLOW, TXT_CENTER | TXT_OUTLINE);
}

/* ================================================================== the course (fitness) */
static void course_start(void) {
    RUN.hub_done |= HD_TRAINED(STAT_FIT);
    H.mode = HA_COURSE;
    H.t = 0;
    H.countdown = 3.0f;
    H.next = 1;
    H.run_t = 0;
    /* par: the straight lines flag to flag at a plain run, with a bit for the corners */
    float len = 0;
    for (int i = 0; i < HUB.nflags; i++) len += v2_dist(HUB.flags[i], HUB.flags[(i + 1) % HUB.nflags]);
    float speed = ARCH[AR_PLAYER].run * (RUN.perks[PK_LIGHTFEET] ? 1.12f : 1.0f);
    H.par = len * 1.18f / speed;
    W.hub_lock = HUB_HELD;
    Actor *p = player();
    p->pos = v2(HUB.flags[0].x - 10, HUB.flags[0].y + 4);
    p->vel = v2(0, 0);
    audio_play(SFX_UI_SELECT, 0.8f, 0, 0.8f);
}

static void course_end(bool finished) {
    H.mode = HA_NONE;
    W.hub_lock = HUB_FREE;
    if (!finished) { award(STAT_FIT, 10, "Out of breath - didn't make it round"); return; }
    float k = CLAMP((H.par * 2.0f - H.run_t) / H.par, 0.0f, 1.0f);   /* par or better: full marks, twice par: nothing */
    char note[80];
    SDL_snprintf(note, sizeof note, "Round the flags in %d.%d s (Theo's par %d s)", (int)H.run_t, (int)(H.run_t * 10) % 10, (int)(H.par + 0.5f));
    award(STAT_FIT, 15 + (int)(k * (XP_CAP - 15)), note);
}

static void course_pre(float dt) {
    if (H.countdown > 0) {
        int before = (int)ceilf(H.countdown);
        H.countdown -= dt;
        int after = (int)ceilf(H.countdown);
        if (after != before) audio_play(after > 0 ? SFX_UI_MOVE : SFX_ALERT, 0.8f, 0, after > 0 ? 1.0f : 1.3f);
        if (H.countdown <= 0) W.hub_lock = HUB_FREE;
        return;
    }
    H.run_t += dt;
}

static void course_post(float dt) {
    (void)dt;
    if (H.countdown > 0) return;
    Actor *p = player();
    if (v2_dist(p->pos, HUB.flags[H.next]) < 18) {
        if (H.next == 0) { course_end(true); return; }
        audio_play(SFX_LIST_TICK, 0.8f, 0, 1.0f + H.next * 0.05f);
        H.next = (H.next + 1) % HUB.nflags;
    }
    if (H.run_t > H.par * 3) course_end(false);
}

static void arrow_to(V2 world, Color c, int icon) {
    V2 v = gfx_world_to_view(world);
    float m = 22;
    bool off = v.x < m || v.y < m || v.x > VIEW_W - m || v.y > VIEW_H - m;
    float k = 0.5f + 0.5f * sinf(G.time * 6);
    if (!off) {
        gfx_spr_ex(SPR_UI_ARROW, v.x, v.y - 26 - k * 3, PI_F / 2, 1, 1, c);
        return;
    }
    V2 ctr = v2(VIEW_W / 2, VIEW_H / 2);
    V2 d = v2_sub(v, ctr);
    float s = MINF((VIEW_W / 2 - m) / MAXF(fabsf(d.x), 0.01f), (VIEW_H / 2 - m) / MAXF(fabsf(d.y), 0.01f));
    V2 p = v2_add(ctr, v2_scale(d, s));
    float ang = v2_to_angle(d);
    gfx_spr_ex(SPR_UI_ARROW, p.x + cosf(ang) * k * 3, p.y + sinf(ang) * k * 3, ang, 1, 1, c);
    if (icon >= 0) gfx_spr(icon, p.x - cosf(ang) * 14, p.y - sinf(ang) * 14);
}

static void course_draw(void) {
    char buf[64];
    if (H.countdown > 0) {
        SDL_snprintf(buf, sizeof buf, "%d", (int)ceilf(H.countdown));
        gfx_text_big(FONT_BIG, buf, VIEW_W / 2, 50, 4, COL_YELLOW, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
        gfx_text(FONT_SMALL, "Round the greenhouse: touch every flag in order, then back here.", VIEW_W / 2, 104, COL_WHITE,
                 TXT_CENTER | TXT_OUTLINE);
        return;
    }
    SDL_snprintf(buf, sizeof buf, "%d.%d", (int)H.run_t, (int)(H.run_t * 10) % 10);
    gfx_text_big(FONT_BIG, buf, VIEW_W / 2, 8, 2, H.run_t > H.par ? COL_ORANGE : COL_WHITE, TXT_CENTER | TXT_SHADOW | TXT_OUTLINE);
    SDL_snprintf(buf, sizeof buf, H.next == 0 ? "BACK TO THE START   PAR %d" : "FLAG %d/%d   PAR %d", H.next, HUB.nflags - 1, (int)(H.par + 0.5f));
    gfx_text(FONT_SMALL, buf, VIEW_W / 2, 36, COL_YELLOW, TXT_CENTER | TXT_OUTLINE);
    arrow_to(HUB.flags[H.next], COL_YELLOW, -1);
}

/* ================================================================== the workbench */
typedef struct { Stack *st; const char *where; } WbItem;

static bool moddable(ItemId id) {
    for (int m = 0; m < MOD_COUNT; m++) if (mod_fits(id, m)) return true;
    return false;
}

static int wb_items(WbItem *out) {
    Actor *p = player();
    int n = 0;
    static const char *const where[WSLOTS] = {"SLOT 1", "SLOT 2", "SLOT 3"};
    for (int k = 0; k < WSLOTS; k++) {
        Stack *s = weapon_slot(p, k);
        if (s->id && moddable((ItemId)s->id)) out[n++] = (WbItem){s, k == p->wslot ? "HANDS" : where[k]};
    }
    for (int k = 0; k < p->ninv; k++) if (moddable((ItemId)p->inv[k].id)) out[n++] = (WbItem){&p->inv[k], "BAG"};
    for (int k = 0; k < RUN.nlocker; k++) if (moddable((ItemId)RUN.locker[k].id)) out[n++] = (WbItem){&RUN.locker[k], "LOCKER"};
    return n;
}

static int wb_mods(ItemId id, int *out) {
    int n = 0;
    for (int m = 0; m < MOD_COUNT; m++) if (mod_fits(id, m)) out[n++] = m;
    return n;
}

static int locker_count(ItemId id) {
    int n = 0;
    for (int k = 0; k < RUN.nlocker; k++) if (RUN.locker[k].id == id) n += RUN.locker[k].count;
    return n;
}

/* the workbench takes from your bag first, then from your locker */
static int have_mat(ItemId id) { return inv_count(player(), id) + locker_count(id); }

static void take_mat(ItemId id, int n) {
    int bag = MINF(n, inv_count(player(), id));
    inv_remove(player(), id, bag);
    n -= bag;
    for (int k = RUN.nlocker - 1; k >= 0 && n > 0; k--) {
        if (RUN.locker[k].id != id) continue;
        int mv = MINF(n, RUN.locker[k].count);
        RUN.locker[k].count -= mv;
        n -= mv;
        if (RUN.locker[k].count <= 0) {
            memmove(&RUN.locker[k], &RUN.locker[k + 1], sizeof(Stack) * (RUN.nlocker - k - 1));
            RUN.nlocker--;
        }
    }
}

static bool mod_affordable(int m) {
    for (int k = 0; k < 3; k++) if (MODS[m].in[k].id && have_mat(MODS[m].in[k].id) < MODS[m].in[k].n) return false;
    return true;
}

static void wb_note(const char *s) { SDL_strlcpy(H.wb_msg, s, sizeof H.wb_msg); H.wb_msg_t = 3; }

static void hands_sync(void);

static void wb_open(void) {
    H.mode = HA_WORKBENCH;
    hands_sync();   /* your weapon's out on the bench with the rest */
    H.wb_foc = 0;
    H.wb_w = 0;
    H.wb_m = 0;
    H.wb_msg_t = 0;
    W.hub_lock = HUB_HELD;
    audio_play(SFX_CRAFT, 0.5f, 0, 1.2f);
    WbItem items[WSLOTS + INV_MAX + 16];
    if (wb_items(items) == 0) wb_note("Nothing here Gus can work on. Bring him a bat, a blade or a gun.");
}

static void wb_close(void) {
    H.mode = HA_NONE;
    W.hub_lock = HUB_FREE;
    audio_play(SFX_UI_BACK, 0.85f, 0, 1);
}

static void wb_fit(Stack *s, int m) {
    char buf[96];
    if ((s->mods >> m) & 1) { wb_note("That one's already on it."); audio_play(SFX_UI_ERROR, 0.5f, 0, 1); return; }
    if (!mod_affordable(m)) { wb_note("You're short of parts for that. Search the yard, or bring more home."); audio_play(SFX_UI_ERROR, 0.5f, 0, 1); return; }
    s->mods |= (int16_t)(1 << m);
    if (m == MOD_REINFORCED) s->cond = (int16_t)weapon_max_cond(s);   /* off the bench like new */
    SDL_snprintf(buf, sizeof buf, "%s: %s fitted.", ITEMS[s->id].name, MODS[m].name);
    /* the parts go last: using them up shuffles the bag and the locker, and s points into one of them */
    for (int k = 0; k < 3; k++) if (MODS[m].in[k].id) take_mat(MODS[m].in[k].id, MODS[m].in[k].n);
    audio_play(SFX_CRAFT, 0.9f, 0, 0.9f);
    SDL_Log("HUB: workbench - %s", buf);
    wb_note(buf);
}

static void workbench_update(float dt) {
    H.wb_msg_t -= dt;
    if (IN.pressed[ACT_BACK] || IN.pressed[ACT_PAUSE] || IN.pressed[ACT_INVENTORY]) { wb_close(); return; }
    WbItem items[WSLOTS + INV_MAX + 16];
    int n = wb_items(items);
    if (n == 0) {
        if (IN.pressed[ACT_INTERACT] || IN.pressed[ACT_CONFIRM] || IN.click) wb_close();
        return;
    }
    H.wb_w = CLAMP(H.wb_w, 0, n - 1);
    int mods[MOD_COUNT];
    int nm = wb_mods((ItemId)items[H.wb_w].st->id, mods);
    H.wb_m = CLAMP(H.wb_m, 0, MAXF(0, nm - 1));
    if (IN.repeat[ACT_MENU_LEFT]) { H.wb_foc = 0; audio_play(SFX_UI_MOVE, 0.7f, 0, 1); }
    if (IN.repeat[ACT_MENU_RIGHT]) { H.wb_foc = 1; audio_play(SFX_UI_MOVE, 0.7f, 0, 1); }
    int dir = IN.repeat[ACT_MENU_DOWN] ? 1 : (IN.repeat[ACT_MENU_UP] ? -1 : 0);
    if (dir) {
        if (H.wb_foc == 0) { H.wb_w = (H.wb_w + dir + n) % n; H.wb_m = 0; }
        else if (nm) H.wb_m = (H.wb_m + dir + nm) % nm;
        audio_play(SFX_UI_MOVE, 0.7f, 0, 1);
    }
    bool act = IN.pressed[ACT_INTERACT] || IN.pressed[ACT_CONFIRM];
    V2 m = IN.mouse_view;
    if (IN.mouse_moved || IN.click) {
        for (int i = 0; i < n && i < 12; i++)
            if (m.x >= 14 && m.x < 190 && m.y >= 46 + i * 16 && m.y < 62 + i * 16) {
                if (H.wb_w != i) H.wb_m = 0;
                H.wb_foc = 0;
                H.wb_w = i;
                if (IN.click) audio_play(SFX_UI_MOVE, 0.7f, 0, 1);
            }
        for (int i = 0; i < nm; i++)
            if (m.x >= 200 && m.x < 468 && m.y >= 119 + i * 15 && m.y < 134 + i * 15) {
                H.wb_foc = 1;
                H.wb_m = i;
                if (IN.click) act = true;
            }
    }
    if (act) {
        if (H.wb_foc == 0) { H.wb_foc = 1; audio_play(SFX_UI_MOVE, 0.7f, 0, 1); }
        else if (nm) wb_fit(items[H.wb_w].st, mods[H.wb_m]);
    }
}

/* one line of the stat block: now, and with the highlighted mod if it would change */
static void wb_stat(float x, float y, const char *label, float now, float with, const char *fmt, bool lower_better) {
    char a[16], b[24];
    SDL_snprintf(a, sizeof a, fmt, now);
    gfx_text(FONT_SMALL, label, x, y, COL_GREY, 0);
    gfx_text(FONT_SMALL, a, x + 70, y, COL_WHITE, 0);
    if (fabsf(with - now) > 0.001f) {
        bool better = lower_better ? with < now : with > now;
        SDL_snprintf(b, sizeof b, "> ");
        size_t l = strlen(b);
        SDL_snprintf(b + l, sizeof b - l, fmt, with);
        gfx_text(FONT_SMALL, b, x + 104, y, better ? COL_GREEN : COL_RED, 0);
    }
}

static void workbench_draw(void) {
    gfx_fill(0, 0, VIEW_W, VIEW_H, rgba(11, 10, 16, 232));
    gfx_text(FONT_BIG, "GUS'S WORKBENCH", 14, 10, COL_KRAFT, TXT_SHADOW);
    gfx_text(FONT_SMALL, "Mods stay on the weapon for good. Parts come from your bag, then your locker.", 14, 26, COL_GREY, 0);
    WbItem items[WSLOTS + INV_MAX + 16];
    int n = wb_items(items);
    if (H.wb_msg_t > 0) gfx_text(FONT_SMALL, H.wb_msg, VIEW_W / 2, 228, COL_ORANGE, TXT_CENTER | TXT_OUTLINE);
    gfx_text(FONT_SMALL, ctl("^yE/LMB^0 fit mod   ^yARROWS^0 choose   ^yESC^0 done", "^yA^0 fit mod   ^yD-PAD^0 choose   ^yB^0 done"),
             VIEW_W / 2, 252, COL_GREY, TXT_CENTER);
    if (n == 0) { hud_cursor(); return; }
    char buf[128];
    /* your weapons */
    gfx_text(FONT_SMALL, "YOUR WEAPONS", 14, 38, COL_YELLOW, 0);
    for (int i = 0; i < n && i < 12; i++) {
        Stack *s = items[i].st;
        float y = 46 + i * 16;
        bool sel = i == H.wb_w;
        if (sel) gfx_fill(12, y, 180, 15, H.wb_foc == 0 ? rgba(255, 212, 71, 60) : rgba(255, 255, 255, 22));
        gfx_spr(ITEMS[s->id].spr, 22, y + 7);
        gfx_text(FONT_SMALL, ITEMS[s->id].name, 34, y + 4, sel ? COL_WHITE : COL_GREY, 0);
        gfx_text(FONT_SMALL, items[i].where, 188, y + 4, rgba(110, 104, 120, 255), TXT_RIGHT);
        int pip = 0;
        for (int m = 0; m < MOD_COUNT; m++)
            if ((s->mods >> m) & 1) { gfx_fill(34 + pip * 4, y + 12, 3, 2, COL_YELLOW); pip++; }
    }
    /* the chosen one */
    Stack *s = items[CLAMP(H.wb_w, 0, n - 1)].st;
    int mods[MOD_COUNT];
    int nm = wb_mods((ItemId)s->id, mods);
    int hm = nm && H.wb_foc == 1 ? mods[CLAMP(H.wb_m, 0, nm - 1)] : -1;
    WeaponDef now = weapon_stats(s);
    Stack w2 = *s;
    if (hm >= 0) w2.mods |= (int16_t)(1 << hm);
    WeaponDef with = weapon_stats(&w2);
    float x = 200;
    gfx_spr_stretch(ITEMS[s->id].spr, x, 38, 32, 32, TINT_NONE);
    char name[48];
    SDL_strlcpy(name, ITEMS[s->id].name, sizeof name);
    for (char *q = name; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
    gfx_text(FONT_BIG, name, x + 38, 40, COL_WHITE, TXT_SHADOW);
    if (now.kind == WK_GUN) {
        wb_stat(x + 38, 56, "DAMAGE", now.damage * MAXF(1, now.pellets), with.damage * MAXF(1, with.pellets), "%.0f", false);
        wb_stat(x + 38, 66, "SPREAD", now.spread, with.spread, "%.1f", true);
        wb_stat(x + 38, 76, "MAGAZINE", (float)weapon_max_cond(s), (float)weapon_max_cond(&w2), "%.0f", false);
        wb_stat(x + 38, 86, "HEARD AT", now.noise, with.noise, "%.0f", true);
    } else {
        wb_stat(x + 38, 56, "DAMAGE", now.damage, with.damage, "%.0f", false);
        wb_stat(x + 38, 66, "SWINGS/S", 1.0f / now.cooldown, 1.0f / with.cooldown, "%.1f", false);
        wb_stat(x + 38, 76, "DURABILITY", (float)weapon_max_cond(s), (float)weapon_max_cond(&w2), "%.0f", false);
        wb_stat(x + 38, 86, "KNOCKDOWN", now.knockdown * 100, with.knockdown * 100, "%.0f%%", false);
    }
    gfx_text(FONT_SMALL, "MODS", x, 108, COL_YELLOW, 0);
    for (int i = 0; i < nm; i++) {
        int m = mods[i];
        float y = 120 + i * 15;
        bool on = (s->mods >> m) & 1;
        bool sel = H.wb_foc == 1 && i == H.wb_m;
        bool ok = mod_affordable(m);
        if (sel) gfx_fill(x - 2, y - 1, 270, 14, rgba(255, 212, 71, 50));
        gfx_text(FONT_SMALL, MODS[m].name, x + 2, y + 2, on ? COL_GREEN : (ok ? COL_WHITE : COL_GREY), 0);
        if (on) {
            gfx_text(FONT_SMALL, "FITTED", x + 248, y + 2, COL_GREEN, TXT_RIGHT);
            gfx_spr(SPR_UI_CHECK, x + 254, y + 2);
            continue;
        }
        float ix = x + 266;
        for (int k = 2; k >= 0; k--) {
            ItemId id = MODS[m].in[k].id;
            if (!id) continue;
            bool enough = have_mat(id) >= MODS[m].in[k].n;
            if (MODS[m].in[k].n > 1) {
                SDL_snprintf(buf, sizeof buf, "%d", MODS[m].in[k].n);
                gfx_text(FONT_SMALL, buf, ix, y + 1, enough ? COL_GREEN : COL_RED, TXT_RIGHT);
                ix -= 6;
            }
            gfx_spr_c(ITEMS[id].spr, ix - 8, y + 5, enough ? TINT_NONE : rgba(130, 90, 100, 150));
            ix -= 18;
        }
    }
    /* what the highlighted mod does, and what it takes */
    if (hm >= 0) {
        gfx_nine(SPR_UI_PANEL, 18, 192, 444, 32, TINT_NONE);
        gfx_text(FONT_SMALL, MODS[hm].name, 26, 197, COL_YELLOW, 0);
        char need[96] = "";
        for (int k = 0; k < 3; k++) {
            if (!MODS[hm].in[k].id) continue;
            char one[40];
            SDL_snprintf(one, sizeof one, "%s%s x%d (have %d)", need[0] ? ", " : "", ITEMS[MODS[hm].in[k].id].name, MODS[hm].in[k].n,
                         have_mat(MODS[hm].in[k].id));
            SDL_strlcat(need, one, sizeof need);
        }
        gfx_text(FONT_SMALL, need, 454, 197, COL_GREY, TXT_RIGHT);
        gfx_text(FONT_SMALL, MODS[hm].desc, 26, 208, COL_WHITE, 0);
    }
    hud_cursor();
}

/* ================================================================== interaction */
bool hub_near_locker(void) { return W.hub && v2_dist(player()->pos, HUB.locker) < 30; }

static bool in_van_zone(V2 p) {
    return p.x > W.exit_rect.x && p.x < W.exit_rect.x + W.exit_rect.w && p.y > W.exit_rect.y && p.y < W.exit_rect.y + W.exit_rect.h;
}

typedef enum { AT_NONE, AT_TALK, AT_VAN, AT_BENCH, AT_RANGE, AT_COURSE, AT_WORKBENCH, AT_LOCKER } Spot;

/* what [E] does here: the van, or whichever is closest of the people and the stations in reach */
static Spot spot_here(int *who, int *arch, V2 *at) {
    Actor *p = player();
    if (in_van_zone(p->pos)) return AT_VAN;
    const struct { Spot s; V2 pos; float r; } st[] = {
        {AT_BENCH, HUB.bench, 30}, {AT_WORKBENCH, HUB.workbench, 26}, {AT_RANGE, HUB.range_line, 26},
        {AT_COURSE, HUB.flags[0], 24}, {AT_LOCKER, HUB.locker, 30},
    };
    Spot best = AT_NONE;
    float bd = 1e9f;
    for (int i = 0; i < ARRAY_LEN(st); i++) {
        float d = v2_dist(p->pos, st[i].pos);
        if (d < st[i].r && d < bd) { bd = d; best = st[i].s; }
    }
    *who = talk_target(arch, at);
    if (*who >= -1 && v2_dist(p->pos, *at) < bd) best = AT_TALK;
    return best;
}

/* the camp's people don't loiter where you train or tinker */
static bool station_spot(V2 g) {
    V2 st[] = {HUB.bench, HUB.workbench, HUB.range_line, HUB.flags[0], HUB.locker};
    for (int i = 0; i < ARRAY_LEN(st); i++) if (v2_dist(g, st[i]) < 28) return true;
    return g.x > W.exit_rect.x - 8 && g.x < W.exit_rect.x + W.exit_rect.w + 8 && g.y > W.exit_rect.y - 8 && g.y < W.exit_rect.y + W.exit_rect.h + 8;
}

static void leave(void) {
    SDL_Log("HUB: into the van for level %d (strength %d, aim %d, fitness %d)", RUN.level + 1, train_level(STAT_STR),
            train_level(STAT_AIM), train_level(STAT_FIT));
    for (int f = 0; f < NUM_FAVOURS; f++)   /* gone without, or turned down: that's settled now */
        if (RUN.favour[f] == FS_MISSED || RUN.favour[f] == FS_DECLINED) RUN.favour[f] = FS_DONE;
    hub_sync_run();
    run_save();
    audio_play(SFX_VAN_DOOR, 1, 0, 1);
    scene_set(SC_BRIEFING);
}

bool hub_interact(Actor *p) {
    (void)p;
    if (H.mode != HA_NONE) return true;
    int who = -2, arch = 0;
    V2 at;
    switch (spot_here(&who, &arch, &at)) {
    case AT_VAN:
        if (!heard(AR_ROSA)) { world_hint("Rosa has the list. Talk to her before you go."); audio_play(SFX_UI_ERROR, 0.5f, 0, 1); }
        else if (crew_squad() < LEVELS[RUN.level].crew_min) {
            char buf[96];
            int more = LEVELS[RUN.level].crew_min - crew_squad();
            SDL_snprintf(buf, sizeof buf, "Not alone, not to %s. Ask %s more of the crew along.", STORE_SAID[RUN.level], NUM_WORD[more]);
            world_hint(buf);
            audio_play(SFX_UI_ERROR, 0.5f, 0, 1);
        } else leave();
        return true;
    case AT_TALK: talk_open(who, arch); return true;
    case AT_BENCH:
        if (done(HD_TRAINED(STAT_STR))) { world_hint("Your arms are jelly. Tomorrow."); audio_play(SFX_UI_ERROR, 0.5f, 0, 1); }
        else bench_start();
        return true;
    case AT_RANGE:
        if (done(HD_TRAINED(STAT_AIM))) { world_hint("One string a night. June's counting the rounds."); audio_play(SFX_UI_ERROR, 0.5f, 0, 1); }
        else range_start();
        return true;
    case AT_COURSE:
        if (done(HD_TRAINED(STAT_FIT))) { world_hint("Your legs have had enough for tonight."); audio_play(SFX_UI_ERROR, 0.5f, 0, 1); }
        else course_start();
        return true;
    case AT_WORKBENCH: wb_open(); return true;
    case AT_LOCKER:
        g_inventory_open = true;
        audio_play(SFX_UI_SELECT, 0.85f, 0, 1.1f);
        return true;
    default: return false;
    }
}

#define KEY_USE ctl("E", "A")

bool hub_prompt(char *buf, int n, V2 *pos) {
    if (H.mode != HA_NONE) { buf[0] = 0; return true; }
    Actor *p = player();
    int who = -2, arch = 0;
    V2 at;
    *pos = v2(p->pos.x, p->pos.y + 14);
    switch (spot_here(&who, &arch, &at)) {
    case AT_VAN:
        if (!heard(AR_ROSA)) SDL_strlcpy(buf, "^kNo list yet - Rosa has it", n);
        else if (crew_squad() < LEVELS[RUN.level].crew_min)
            SDL_snprintf(buf, n, "^kAsk %d more of the crew along first", LEVELS[RUN.level].crew_min - crew_squad());
        else if (crew_squad()) SDL_snprintf(buf, n, "^y[%s]^0 Drive to %s  ^kwith %d of the crew", KEY_USE, LEVELS[RUN.level].name, crew_squad());
        else SDL_snprintf(buf, n, "^y[%s]^0 Drive to %s", KEY_USE, LEVELS[RUN.level].name);
        return true;
    case AT_TALK:
        SDL_snprintf(buf, n, "^y[%s]^0 Talk to %s", KEY_USE, is_folk(arch) ? "your neighbour" : name_of(arch));
        if (crew_of(arch) >= 0 && LEVELS[RUN.level].crew_max > 0)
            SDL_strlcat(buf, RUN.crew[crew_of(arch)] == CR_SQUAD ? "  ^gcoming along" : "  ^kcrew", n);
        *pos = v2(at.x, at.y + 14);
        return true;
    case AT_BENCH:
        if (done(HD_TRAINED(STAT_STR))) SDL_strlcpy(buf, "^kWeight bench - done for tonight", n);
        else SDL_snprintf(buf, n, "^y[%s]^0 Lift weights  ^kSTRENGTH", KEY_USE);
        return true;
    case AT_RANGE:
        if (done(HD_TRAINED(STAT_AIM))) SDL_strlcpy(buf, "^kShooting range - done for tonight", n);
        else SDL_snprintf(buf, n, "^y[%s]^0 Shoot a string  ^kAIM", KEY_USE);
        return true;
    case AT_COURSE:
        if (done(HD_TRAINED(STAT_FIT))) SDL_strlcpy(buf, "^kThe flags - done for tonight", n);
        else SDL_snprintf(buf, n, "^y[%s]^0 Run the flags  ^kFITNESS", KEY_USE);
        return true;
    case AT_WORKBENCH: SDL_snprintf(buf, n, "^y[%s]^0 Workbench  ^kmod your weapons", KEY_USE); return true;
    case AT_LOCKER: SDL_snprintf(buf, n, "^y[%s]^0 Your locker", KEY_USE); return true;
    default: return false;   /* the yard's crates and anything lying about: the usual prompts */
    }
}

/* ================================================================== the scene */
const Stack *hub_packed(void) { return H.unpacked ? &player()->weapon : &H.pack; }

/* the weapon you'll take comes out only where you choose it - the bag screen, Gus's bench - and goes back in after.
   Anything else that ends up in your hands (one picked up off the grass) is packed, or bagged if one's packed already. */
static void hands_sync(void) {
    Actor *p = player();
    bool out = g_inventory_open || H.mode == HA_WORKBENCH;
    if (out && !H.unpacked) {
        p->weapon = H.pack;
        H.pack = (Stack){IT_NONE, 0, 0, 0};
        H.unpacked = true;
    } else if (!out && H.unpacked) {
        H.pack = p->weapon;
        p->weapon = (Stack){IT_NONE, 0, 0, 0};
        H.unpacked = false;
    }
    if (H.unpacked || !p->weapon.id || H.mode == HA_RANGE) return;   /* June's pistol is the range's */
    Stack w = p->weapon;
    p->weapon = (Stack){IT_NONE, 0, 0, 0};
    if (hub_stow(w) <= 1) floater(v2(p->pos.x, p->pos.y - 16), "Packed for the run", COL_GREY, false);
}

/* a weapon that comes your way at the camp: the hands' slot if it's empty (packed, unless you've the bag screen open),
   else a free slot on your back, else the bag, else the grass. Says where: 0 hands, 1 back, 2 bag, 3 dropped */
static int hub_stow(Stack w) {
    Actor *p = player();
    Stack *hands = H.unpacked ? &p->weapon : &H.pack;
    if (!hands->id) { *hands = w; return 0; }
    for (int k = 0; k < WSLOTS; k++)
        if (k != p->wslot && !p->slots[k].id) { p->slots[k] = w; return 1; }
    if (inv_add(p, w)) return 2;
    int pi = pickup_spawn(w, p->pos, v2(frange(-20, 20), frange(10, 30)));
    if (pi >= 0) W.pickups[pi].dropped = true;
    world_hint("No room in the bag for it.");
    return 3;
}

void hub_enter(void) {
    memset(&H, 0, sizeof H);
    H.who = -2;
    world_start_hub();
    spawn_camp();
    Actor *p = player();
    H.pack = p->weapon;   /* the run's weapon: in your hands again at the store */
    p->weapon = (Stack){IT_NONE, 0, 0, 0};
    p->face = p->aim = PI_F * 0.5f;
    G.cam_x = p->pos.x;
    G.cam_y = p->pos.y + 20;
}

void hub_sync_run(void) {
    if (!W.hub) return;
    Actor *p = player();
    RUN.hp = MAXF(1, p->hp);
    RUN.weapon = *hub_packed();   /* June's pistol stays at the range */
    memcpy(RUN.slots, p->slots, sizeof RUN.slots);
    RUN.wslot = p->wslot;
    RUN.ninv = p->ninv;
    memcpy(RUN.inv, p->inv, sizeof(Stack) * p->ninv);
}

static void camp_tick(float dt) {
    /* the yard's crates: searched stays searched this evening */
    int k = 0;
    for (int i = 0; i < W.nconts && k < 6; i++) {
        Container *c = &W.conts[i];
        if (c->zone != Z_HUB) continue;
        if (c->searched) RUN.hub_done |= HD_CONT(k);
        k++;
    }
    /* flags flutter */
    H.flag_t += dt;
    for (int i = 0; i < HUB.nflags; i++)
        if (HUB.flag_prop[i] >= 0) W.props[HUB.flag_prop[i]].spr = SPR_P_FLAG + (((int)(H.flag_t * 5) + i) & 1);
    /* lamps round the yard, a lantern at each station */
    for (int ty = 0; ty < W.h; ty++)
        for (int tx = 0; tx < W.w; tx++)
            if (cell(tx, ty)->obj == OB_LAMPPOST) add_light(tile_center(tx, ty), 74, rgb(255, 214, 150), 0.5f + 0.03f * sinf(W.time * 3 + tx));
    add_light(v2(HUB.workbench.x, HUB.workbench.y - 4), 60, COL_AMBER, 0.6f);
    add_light(HUB.locker, 40, COL_AMBER, 0.35f);
    for (int k = 0; k < MAX_CREW; k++)   /* a candle on every grave */
        if (RUN.crew[k] == CR_DEAD) add_light(v2(HUB.grave[k].x + 4, HUB.grave[k].y - 8), 22 + sinf(W.time * 9 + k) * 2, COL_AMBER, 0.55f);
    add_light(v2(HUB.range.x + HUB.range.w / 2, HUB.range.y + HUB.range.h / 2), 120, rgb(255, 222, 170), 0.35f);
}

void hub_update(float dt) {
    if (g_paused) { pause_update(dt); audio_set_muffle(0.7f); return; }
    hands_sync();
    if (g_inventory_open) { inventory_update(dt); hands_sync(); audio_set_muffle(0.55f); return; }
    if (H.mode == HA_WORKBENCH) { workbench_update(dt); hands_sync(); audio_set_muffle(0.4f); return; }
    audio_set_muffle(0);
    if (H.mode == HA_TALK && IN.pressed[ACT_BACK]) { talk_back(); return; }
    if (IN.pressed[ACT_PAUSE]) { pause_open(); audio_play(SFX_UI_SELECT, 0.85f, 0, 0.8f); return; }
    if (IN.pressed[ACT_INVENTORY] && H.mode == HA_NONE) { g_inventory_open = true; hands_sync(); audio_play(SFX_UI_SELECT, 0.85f, 0, 1.1f); return; }
    H.t += dt;
    H.card_t -= dt;
    HubMode was = H.mode;
    switch (H.mode) {
    case HA_TALK: talk_update(dt); break;
    case HA_BENCH: bench_update(dt); break;
    case HA_RANGE: range_pre(dt); break;
    case HA_COURSE: course_pre(dt); break;
    default: break;
    }
    if (was != HA_NONE && H.mode == HA_NONE) {
        /* the press that finished it doesn't also start the next thing */
        IN.pressed[ACT_INTERACT] = IN.pressed[ACT_CONFIRM] = IN.pressed[ACT_ATTACK] = false;
        IN.click = false;
    }
    if (g_scene != SC_HUB) return;
    world_update(dt);
    if (g_scene != SC_HUB) return;   /* drove off */
    if (H.mode == HA_RANGE) range_post(dt);
    if (H.mode == HA_COURSE) course_post(dt);
    hands_sync();   /* the locker or the workbench just opened, or something got picked up */
    camp_tick(dt);
}

/* ------------------------------------------------------------------ drawing */
/* the sticker over somebody's head: what you brought them, a favour to ask (!), something to say (...) - or nothing.
   Returns how far up their name goes to clear it (0: no sticker). */
static int talk_tag(int arch, float x, float y) {
    if (is_folk(arch)) return 0;
    int k = crew_of(arch);
    if (k >= 0 && RUN.crew[k] == CR_SQUAD) {   /* coming along tonight */
        gfx_sticker(x - 5, y, 10, 9, COL_GREEN);
        gfx_spr(SPR_UI_CHECK, x - 3, y + 1);
        return 17;
    }
    int give_f = favour_to_give(arch);
    if (give_f >= 0) {
        gfx_sticker(x - 9, y - 5, 18, 14, COL_GREEN);
        gfx_spr(ITEMS[FAVOURS[give_f].item].spr, x, y + 2);
        return 22;
    }
    int ask_f = favour_to_ask(arch);
    if (ask_f >= 0 && RUN.favour[ask_f] == FS_NONE) {
        gfx_sticker(x - 4, y, 8, 9, COL_YELLOW);
        gfx_text(FONT_SMALL, "!", x - 1, y - 1, COL_BLACK, 0);
        return 17;
    }
    if (!heard(arch) || favour_missed(arch) >= 0) {
        gfx_sticker(x - 7, y, 14, 9, COL_RECEIPT);
        gfx_text(FONT_SMALL, "...", x - 4, y - 1, COL_BLACK, 0);
        return 17;
    }
    return 0;
}

/* over the world (unlit): who has something to say, names, the course's next flag */
static void world_overlay(void) {
    Actor *p = player();
    for (int i = 1; i < W.nactors; i++) {
        Actor *a = &W.actors[i];
        if (!a->used || !a->alive || !(is_camp(a) || is_crew(a))) continue;
        float d = v2_dist(a->pos, p->pos);
        float bob = sinf(G.time * 4 + i) * 1.0f;
        int lift = H.mode == HA_TALK && H.who == i ? 0 : talk_tag(a->arch, a->pos.x, a->pos.y - 25 + bob);
        if (d < 56 && H.mode != HA_TALK)
            gfx_text(FONT_SMALL, is_folk(a->arch) ? "Neighbour" : name_of(a->arch), a->pos.x, a->pos.y - 16 - lift,
                     COL_RECEIPT, TXT_CENTER | TXT_OUTLINE);
    }
    if (HUB.theo_sick && !(H.mode == HA_TALK && H.who == -1)) talk_tag(AR_THEO, HUB.post[AR_THEO].x, HUB.post[AR_THEO].y - 22);
    /* the graves: who, and where they fell */
    for (int k = 0; k < MAX_CREW; k++) {
        if (RUN.crew[k] != CR_DEAD || v2_dist(HUB.grave[k], p->pos) > 44 || H.mode == HA_TALK) continue;
        char buf[48];
        SDL_snprintf(buf, sizeof buf, "%s - %s", ARCH[CREW[k].arch].name, STORE_SAID[RUN.crew_fell[k]]);
        gfx_text(FONT_SMALL, buf, HUB.grave[k].x, HUB.grave[k].y - 24, COL_RECEIPT, TXT_CENTER | TXT_OUTLINE);
        gfx_text(FONT_SMALL, CREW[k].epitaph, HUB.grave[k].x, HUB.grave[k].y - 15, COL_GREY, TXT_CENTER | TXT_OUTLINE);
    }
    if (H.mode == HA_COURSE && H.countdown <= 0) {
        V2 f = HUB.flags[H.next];
        float k = 0.5f + 0.5f * sinf(G.time * 8);
        gfx_glow(f.x, f.y, 26, COL_YELLOW, 0.35f + 0.25f * k);
        gfx_ring(f.x, f.y + 4, 14 + k * 2, 1, rgba(255, 212, 71, 200));
    }
}

static void stats_panel(void) {
    float x = 4, y = 16, w = 112, h = 40;
    gfx_nine(SPR_UI_PANEL, x, y, w, h, TINT_NONE);
    for (int s = 0; s < STAT_COUNT; s++) {
        float ry = y + 5 + s * 11;
        int lv = train_level(s);
        gfx_spr(SPR_UI_STAT + s, x + 5, ry - 1);
        gfx_text(FONT_SMALL, TRAIN[s].name, x + 19, ry + 1, COL_WHITE, 0);
        for (int k = 0; k < STAT_MAX; k++)
            gfx_fill(x + 74 + k * 6, ry + 2, 4, 5, k < lv ? COL_YELLOW : rgba(70, 62, 76, 255));
        if (done(HD_TRAINED(s))) gfx_spr_c(SPR_UI_CHECK, x + w - 9, ry, COL_GREEN);
    }
}

/* the favours: what's been asked for tonight's store, and what you brought home for somebody */
static void favours_panel(void) {
    static const Color INK_FAV = {40, 104, 58, 255};
    int list[MAX_FAVOURS], n = 0;
    for (int f = 0; f < NUM_FAVOURS; f++)
        if ((RUN.favour[f] == FS_OPEN && FAVOURS[f].level == RUN.level) || RUN.favour[f] == FS_FOUND) list[n++] = f;
    if (n == 0) return;
    float x = 4, y = 60, w = 112, h = 16 + n * 21;
    gfx_nine(SPR_UI_PAPER, x, y, w, h, TINT_NONE);
    gfx_text(FONT_SMALL, "FAVOURS", x + 9, y + 5, INK, 0);
    char buf[48];
    for (int i = 0; i < n; i++) {
        const FavourDef *f = &FAVOURS[list[i]];
        float ry = y + 17 + i * 21;
        bool found = RUN.favour[list[i]] == FS_FOUND, ready = found && have_mat(f->item) >= f->n;
        gfx_spr(ITEMS[f->item].spr, x + 13, ry + 5);
        if (ready) gfx_spr(SPR_UI_CHECK, x + 14, ry + 7);
        if (f->n > 1) SDL_snprintf(buf, sizeof buf, "%s x%d", ITEMS[f->item].name, f->n);
        else SDL_strlcpy(buf, ITEMS[f->item].name, sizeof buf);
        gfx_text(FONT_SMALL, buf, x + 24, ry, found ? INK_FAV : INK, 0);
        SDL_snprintf(buf, sizeof buf, found ? "give it to %s" : "for %s", name_of(f->arch));
        gfx_text(FONT_SMALL, buf, x + 24, ry + 9, ready ? INK_FAV : INK_FADE, 0);
    }
}

/* the crew, top right: who's coming along tonight, who keeps the gate, who's under the trees - and how many Rosa lets go */
static void crew_panel(void) {
    const LevelDef *d = &LEVELS[RUN.level];
    float w = 116, h = 34 + MAX_CREW * 10, x = VIEW_W - w - 4, y = 16;
    gfx_nine(SPR_UI_PANEL, x, y, w, h, TINT_NONE);
    gfx_text(FONT_SMALL, "THE CREW", x + 7, y + 5, COL_YELLOW, 0);
    char buf[48];
    int sq = crew_squad();
    if (d->crew_max > 0) {
        SDL_snprintf(buf, sizeof buf, "%d/%d along", sq, d->crew_max);
        gfx_text(FONT_SMALL, buf, x + w - 7, y + 5, sq < d->crew_min ? COL_RED : COL_GREEN, TXT_RIGHT);
    }
    for (int k = 0; k < MAX_CREW; k++) {
        float ry = y + 17 + k * 10;
        const char *name = ARCH[CREW[k].arch].name;
        const char *st;
        Color c = COL_WHITE, sc = COL_GREY;
        if (RUN.crew[k] == CR_DEAD) { st = "dead"; c = COL_GREY; sc = rgba(200, 30, 60, 255); }
        else if (RUN.crew[k] == CR_SQUAD) { st = "coming"; sc = COL_GREEN; }
        else st = "on the gate";
        gfx_text(FONT_SMALL, name, x + 7, ry, c, 0);
        if (RUN.crew[k] == CR_DEAD) gfx_fill(x + 6, ry + 3, gfx_text_w(FONT_SMALL, name) + 2, 1, rgba(200, 30, 60, 220));
        gfx_text(FONT_SMALL, st, x + w - 7, ry, sc, TXT_RIGHT);
    }
    if (d->crew_max <= 0) SDL_strlcpy(buf, "tonight you go alone", sizeof buf);
    else if (d->crew_min > 0) SDL_snprintf(buf, sizeof buf, "take %d to %d with you", d->crew_min, d->crew_max);
    else SDL_snprintf(buf, sizeof buf, "take up to %d with you", d->crew_max);
    gfx_text(FONT_SMALL, buf, x + 7, y + h - 14, rgba(110, 104, 120, 255), 0);
}

static void objective(void) {
    if (H.mode != HA_NONE || H.card_t > 0) return;
    char buf[128];
    int need = LEVELS[RUN.level].crew_min - crew_squad();
    if (!heard(AR_ROSA)) SDL_strlcpy(buf, "Get the list from ^yROSA^0 - the radio desk, in the greenhouse", sizeof buf);
    else if (need > 0) SDL_snprintf(buf, sizeof buf, "Ask ^y%d^0 more of the ^yCREW^0 along - they keep watch round the yard", need);
    else SDL_snprintf(buf, sizeof buf, "Train, tinker, then take the ^yVAN^0 to %s", LEVELS[RUN.level].name);
    gfx_text(FONT_SMALL, buf, VIEW_W / 2, 6, COL_RECEIPT, TXT_CENTER | TXT_OUTLINE);
    if (!heard(AR_ROSA)) {
        for (int i = 1; i < W.nactors; i++)
            if (W.actors[i].used && W.actors[i].arch == AR_ROSA) arrow_to(W.actors[i].pos, COL_RECEIPT, -1);
    } else if (v2_dist(player()->pos, W.van) > 200) {
        arrow_to(v2(W.exit_rect.x + W.exit_rect.w / 2, W.exit_rect.y), rgba(162, 211, 76, 160), SPR_UI_VAN);
    }
}

static void card_draw(void) {
    if (H.card_t <= 0) return;
    float a = CLAMP(H.card_t * 3, 0.0f, 1.0f);
    float w = 280, h = 72, x = VIEW_W / 2 - w / 2, y = 54 - (1 - a) * 12;
    Color bg = COL_RECEIPT;
    bg.a = (Uint8)(245 * a);
    gfx_sticker(x, y, w, h, bg);
    char buf[96];
    Color c = rgba(44, 38, 46, (Uint8)(255 * a));
    SDL_snprintf(buf, sizeof buf, "%s  +%d", TRAIN[H.card_stat].name, H.card_xp);
    gfx_text(FONT_BIG, buf, x + 10, y + 6, c, 0);
    gfx_text(FONT_SMALL, H.card_note, x + 10, y + 22, rgba(128, 118, 112, (Uint8)(255 * a)), 0);
    /* the bar to the next level */
    int lv = H.card_lv1;
    int x0 = stat_xp_at(lv), x1 = stat_xp_at(lv + 1);
    float k = lv >= STAT_MAX ? 1.0f : CLAMP((float)(RUN.xp[H.card_stat] - x0) / MAXF(1, x1 - x0), 0.0f, 1.0f);
    gfx_fill(x + 10, y + 36, w - 20, 6, rgba(44, 38, 46, (Uint8)(200 * a)));
    gfx_fill(x + 11, y + 37, (w - 22) * k, 4, rgba(162, 211, 76, (Uint8)(255 * a)));
    if (H.card_lv1 > H.card_lv0) {
        SDL_snprintf(buf, sizeof buf, "LEVEL %d!  %s", H.card_lv1, TRAIN[H.card_stat].desc);
        gfx_text_wrap(FONT_SMALL, buf, x + 10, y + 47, (int)w - 20, rgba(232, 41, 63, (Uint8)(255 * a)), 0, 10);
    } else {
        SDL_snprintf(buf, sizeof buf, lv >= STAT_MAX ? "LEVEL %d - as good as it gets" : "LEVEL %d", lv);
        gfx_text(FONT_SMALL, buf, x + 10, y + 47, c, 0);
    }
}

void hub_draw(void) {
    world_draw();
    world_overlay();
    gfx_begin_hud();
    if (g_inventory_open || H.mode == HA_WORKBENCH) {
        if (g_inventory_open) inventory_draw();
        else workbench_draw();
        if (g_paused) pause_draw();
        return;
    }
    hud_draw_gear();
    stats_panel();
    favours_panel();
    crew_panel();
    objective();
    switch (H.mode) {
    case HA_TALK: talk_draw(); break;
    case HA_BENCH: bench_draw(); break;
    case HA_RANGE: range_draw(); break;
    case HA_COURSE: course_draw(); break;
    default: break;
    }
    card_draw();
    if (g_paused) pause_draw();
    else hud_cursor();
}

/* ================================================================== the QA autopilot (--autoplay) */
/* walks the evening the way a player would: Rosa, the bench, the range, the flags, Gus and his bench, the favours, as many
   of the crew as may come, the van */
extern bool g_aim_override;
extern V2 g_aim_world;

static struct {
    int step;
    float cd, repath, stuck;
    int path[48][2];
    int len, i;
    V2 goal, last;
} BOT;

static void bot_go(V2 g) {
    Actor *p = player();
    if (v2_dist(g, BOT.goal) > 8 || BOT.repath <= 0) {
        BOT.goal = g;
        path_find(p->pos, g, BOT.path, 48, &BOT.len);
        BOT.i = 0;
        BOT.repath = 0.8f;
    }
    V2 t = g;
    if (BOT.i < BOT.len) {
        t = BOT.i == BOT.len - 1 ? g : tile_center(BOT.path[BOT.i][0], BOT.path[BOT.i][1]);
        if (v2_dist(p->pos, t) < 7) BOT.i++;
    }
    V2 d = v2_sub(t, p->pos);
    IN.move = v2_len(d) < 3 ? v2(0, 0) : v2_norm(d);
    g_aim_world = g;
}

static V2 bot_person(int arch) {
    for (int i = 1; i < W.nactors; i++)
        if (W.actors[i].used && W.actors[i].arch == arch) return W.actors[i].pos;
    return HUB.post[arch];
}

void hub_bot(float dt) {
    Actor *p = player();
    if (W.time < 0.05f) memset(&BOT, 0, sizeof BOT);   /* a new evening */
    memset(IN.pressed, 0, sizeof IN.pressed);
    IN.move = v2(0, 0);
    IN.down[ACT_ATTACK] = false;
    g_aim_override = true;
    BOT.cd -= dt;
    BOT.repath -= dt;
    if (g_paused || g_inventory_open) { IN.pressed[ACT_BACK] = true; return; }
    switch (H.mode) {
    case HA_TALK:
        /* favours: the odd ones turned down first and taken on when asked again, so both answers get played */
        if (talk_choosing()) H.sel = H.choice != CH_FAVOUR || RUN.favour[H.ask] == FS_DECLINED || H.ask % 2 == 0 ? 0 : 1;
        if (BOT.cd <= 0) { IN.pressed[ACT_INTERACT] = true; BOT.cd = 0.25f; }
        return;
    case HA_BENCH:
        if (H.t > 0.8f && BOT.cd <= 0 && fabsf(H.needle - H.zone_c) < H.zone_w * 0.3f) { IN.pressed[ACT_INTERACT] = true; BOT.cd = 0.2f; }
        return;
    case HA_RANGE: {
        float bd = 1e9f;
        for (int i = 0; i < HUB_TARGETS; i++)
            if (H.tg[i].up && v2_dist(H.tg[i].pos, p->pos) < bd) { bd = v2_dist(H.tg[i].pos, p->pos); g_aim_world = H.tg[i].pos; }
        if (bd < 1e9f && W.hub_lock == HUB_AIM) {
            IN.down[ACT_ATTACK] = true;
            if (p->atk_cd <= 0 && BOT.cd <= 0) { IN.pressed[ACT_ATTACK] = true; BOT.cd = 0.3f; }
        }
        return;
    }
    case HA_COURSE:
        if (H.countdown <= 0) bot_go(HUB.flags[H.next]);
        return;
    case HA_WORKBENCH: {
        WbItem items[WSLOTS + INV_MAX + 16];
        int n = wb_items(items), mods[MOD_COUNT], fit = -1;
        for (int w = 0; w < n && fit < 0; w++) {
            int nm = wb_mods((ItemId)items[w].st->id, mods);
            for (int k = 0; k < nm; k++)
                if (!((items[w].st->mods >> mods[k]) & 1) && mod_affordable(mods[k])) { H.wb_w = w; H.wb_m = k; fit = k; break; }
        }
        if (fit >= 0 && BOT.cd <= 0) { H.wb_foc = 1; IN.pressed[ACT_INTERACT] = true; BOT.cd = 0.5f; }
        else if (fit < 0 && BOT.cd <= 0) { IN.pressed[ACT_BACK] = true; BOT.step++; BOT.cd = 0.5f; }
        return;
    }
    default: break;
    }
    if (H.card_t > 2.5f) return;   /* read the card */
    V2 goal = W.van;
    float reach = 20;
    bool step_done = false;
    switch (BOT.step) {
    case 0: goal = bot_person(AR_ROSA); step_done = heard(AR_ROSA); break;
    /* the stations: right up to them, so nobody standing near is closer and gets talked to instead */
    case 1: goal = v2(HUB.bench.x + 22, HUB.bench.y + 4); reach = 8; step_done = done(HD_TRAINED(STAT_STR)); break;
    case 2: goal = HUB.range_line; reach = 8; step_done = done(HD_TRAINED(STAT_AIM)); break;
    case 3: goal = HUB.flags[0]; reach = 8; step_done = done(HD_TRAINED(STAT_FIT)); break;
    case 4: goal = bot_person(AR_GUS); step_done = heard(AR_GUS); break;
    case 5: goal = HUB.workbench; reach = 14; break;   /* done when the bench closes (above) */
    case 6: {   /* whoever has a favour to ask (or asks again, after a no), or one to be handed */
        int who = -1;
        for (int i = 1; i < W.nactors && who < 0; i++) {
            Actor *a = &W.actors[i];
            if (a->used && a->alive && is_camp(a) && (favour_to_ask(a->arch) >= 0 || favour_to_give(a->arch) >= 0)) who = i;
        }
        if (who < 0) { step_done = true; break; }
        goal = W.actors[who].pos;
        break;
    }
    case 7: {   /* the crew: as many as the store lets go */
        int who = -1;
        for (int i = 1; i < W.nactors && who < 0 && crew_squad() < LEVELS[RUN.level].crew_max; i++) {
            Actor *a = &W.actors[i];
            if (a->used && a->alive && is_crew(a) && RUN.crew[crew_index(a)] == CR_HOME) who = i;
        }
        if (who < 0) { step_done = true; break; }
        goal = W.actors[who].pos;
        break;
    }
    default: goal = v2(W.exit_rect.x + W.exit_rect.w / 2, W.exit_rect.y + W.exit_rect.h / 2); reach = 10; break;
    }
    if (step_done) { BOT.step++; return; }
    if (v2_dist(p->pos, goal) > reach) { bot_go(goal); return; }
    if (BOT.cd <= 0) { IN.pressed[ACT_INTERACT] = true; BOT.cd = 0.6f; }
}
