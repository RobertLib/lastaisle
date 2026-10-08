/* LAST AISLE - world state shared by all gameplay modules */
#ifndef WORLD_H
#define WORLD_H

#include "common.h"
#include "data.h"

/* ================================================================ map */
#define MAP_MAX_W 112
#define MAP_MAX_H 88

typedef enum {
    FL_NONE, FL_ASPHALT, FL_ASPHALT_LINE, FL_ASPHALT_HLINE, FL_CURB, FL_SIDEWALK, FL_GRASS, FL_DIRT,
    FL_LINO, FL_WHITETILE, FL_CARPET, FL_MALL, FL_CONCRETE, FL_WOOD, FL_RUBBLE, FL_COUNT
} FloorType;

typedef enum { WL_NONE, WL_CONCRETE, WL_BRICK, WL_MALL, WL_HEDGE, WL_RUIN, WL_GLASS, WL_COUNT } WallType;

/* single-tile objects living in a cell */
typedef enum {
    OB_NONE, OB_SHELF_H, OB_SHELF_V, OB_SHELF_FALLEN, OB_FRIDGE_D, OB_FRIDGE_R, OB_FRIDGE_L,
    OB_COUNTER, OB_REGISTER, OB_CRATE, OB_BOX, OB_PALLET, OB_BARREL, OB_LOCKER, OB_TOILET, OB_SINK,
    OB_PEGBOARD, OB_VENDING, OB_CAMPFIRE, OB_BUSH, OB_LAMPPOST, OB_RACK, OB_MANNEQUIN, OB_TV,
    OB_GLASS_H, OB_GLASS_V, OB_GLASS_BROKEN_H, OB_GLASS_BROKEN_V, OB_BLOCKER, OB_COUNT
} ObjType;

enum {
    CF_SOLID = 1,      /* blocks movement */
    CF_OPAQUE = 2,     /* blocks sight */
    CF_SHOT = 4,       /* blocks bullets */
    CF_INDOOR = 8,     /* under a roof */
    CF_EXIT = 16,      /* van exit zone */
    CF_SKY = 32,       /* roof collapsed - light falls in */
    CF_NOSPAWN = 64,
    CF_DOOR = 128,
};

typedef struct {
    uint8_t floor, fvar;
    uint8_t wall;
    uint8_t obj, ovar;
    uint8_t flags;
    uint8_t zone;
    uint8_t room;
    int16_t cont;      /* container index or -1 */
    uint8_t wq[4];     /* wall autotile quadrant source indices */
    uint8_t fuel;      /* burnable (grass, boxes) */
    uint8_t reach;     /* walkable from the van (computed at generation) */
} Cell;

typedef struct {
    int spr;
    float x, y;        /* pivot position */
    float angle;
    int layer;         /* 0 = under actors, 1 = over actors (canopies, signs) */
    int cont;          /* container or -1 */
    int frame_anim;    /* >0 = animate this many frames */
    Color tint;
    bool glow;         /* sign lit by a generator */
} Prop;

typedef struct { int16_t spr; int16_t x, y; uint8_t flip; uint8_t alpha; } Decor;

/* ============================================================== items */
typedef struct Stack { int16_t id, count, cond, mods; /* mods: workbench ModId bits (weapons only) */ } Stack;

typedef struct {
    int tx, ty;        /* interaction anchor tile */
    V2 pos;            /* centre in px */
    int zone;
    int prop;          /* prop index or -1 */
    bool dead;         /* removed during generation */
    int n;
    Stack items[6];
    bool searched;
    float glint_t;
    const char *name;
} Container;

/* ============================================================= actors */
#define MAX_ACTORS 96
#define INV_MAX 24
#define WSLOTS 3   /* weapons the player carries: one in hand, the rest on the back - no room taken in the bag */

typedef enum {
    AI_IDLE, AI_WANDER, AI_LOOT, AI_INVESTIGATE, AI_CHASE, AI_FLEE, AI_SURRENDER, AI_SEARCH, AI_GUARD,
    AI_REST, AI_FEED,     /* animals: lying down, eating from the dead */
    AI_STEAL,             /* a thief after what you left at the van */
    AI_GETAWAY            /* ...and off round the level with it till somebody stops them */
} AiState;

typedef struct {
    AiState state;
    float timer, think, repath;
    int target;               /* actor index or -1 */
    V2 goal;                  /* movement goal */
    V2 last_seen;
    int path[48][2];
    int path_len, path_i;
    float reaction;           /* countdown before acting */
    float suspicion;
    int container;
    int grudge[4];            /* actors we hate personally */
    float strafe_dir, strafe_t;
    float burst_t;
    int burst_n;
    float stuck_t;
    V2 last_pos;
    V2 home;
    int leader;               /* squad leader or -1 */
    float fear;
    bool aware;               /* knows the player is a threat */
    float bark_t;
    float wander_t;
    bool alerted_by_noise;
    float aim_t;              /* gunners: aim before firing */
} Brain;

typedef struct {
    bool alive;               /* slot in use and not dead */
    bool used;
    int arch;
    Faction faction;
    Temper temper;
    bool rabid;               /* animals: goes for anybody who comes close */
    uint8_t coat;             /* animals: which healthy coat */
    V2 pos, vel, push;
    float radius;
    float face;               /* torso angle */
    float aim;                /* desired aim angle */
    float move_angle;
    float leg_anim;
    float speed_mul;
    int hp, maxhp;
    Stack weapon;             /* held weapon (id may be IT_NONE = fists) */
    Stack slots[WSLOTS];      /* player: the weapons on your back, each in its own slot (keys 1-3); slots[wslot] stays empty */
    int wslot;                /* player: the slot the weapon in hand belongs to */
    float atk_cd;             /* time until next attack */
    float atk_t;              /* attack animation time (counts up while >=0) */
    bool hit_pending;         /* melee hit not yet resolved */
    float hit_at;             /* atk_t when the melee hit lands */
    int atk_dir;              /* alternating swing direction */
    float windup;             /* NPC telegraph countdown */
    float reload_t;
    float down_t;             /* knocked down */
    float stun_t;
    float hurt_t;
    float invuln;
    float burn_t;
    float exec_t;             /* executing someone */
    int exec_target;
    int exec_hits;
    int cart;                 /* pushing a cart */
    Stack inv[INV_MAX];
    int ninv;
    Brain br;
    int blood_steps;
    float step_t;
    float alert_icon_t;
    int alert_icon;           /* 0 none, 1 '!', 2 '?', 3 hands */
    bool unaware_hit;         /* was hit while unaware */
    float saw_t;              /* chainsaw rev */
    float flame_t;
    float spawn_grace;
    int last_hit_by;
    bool boss_enraged;
    int boss_phase;
    float boss_t;
    float last_shot_t;
} Actor;

/* ========================================================== dynamics */
#define MAX_PICKUPS 384
#define PICK_REACH 22.0f    /* how far you reach for something on the floor */
typedef struct {
    bool alive;
    Stack st;
    V2 pos, vel;
    float angle, spin;
    float z, vz;
    bool flying;           /* thrown: hurts on contact */
    int thrower;
    float fuse;            /* pipe bomb */
    float age;
    float bob;
    bool dropped;          /* dropped by the player: no auto pickup */
} Pickup;

#define MAX_BULLETS 256
typedef struct {
    bool alive;
    V2 pos, prev, vel;
    float dmg, kb, kd;
    int owner;
    int pierce;
    float life;
    bool nail;
    bool flame;
    bool spit;             /* a plant's glob of acid: slow, lobbed, splats on whatever it hits */
    float life0;           /* life it was fired with (the spit's arc) */
    int weapon;
} Bullet;

#define MAX_PARTICLES 3000
typedef enum {
    PT_BLOOD, PT_GIB, PT_SHELL, PT_SMOKE, PT_SPARK, PT_DUST, PT_GLASS, PT_FIRE, PT_EMBER, PT_DEBRIS,
    PT_HEAD, PT_RAIN, PT_FLAME, PT_IMPACT
} PartKind;
typedef struct {
    V2 pos, vel;
    float z, vz;
    float life, max;
    float angle, spin;
    float scale;
    int spr, frames;
    Color col;
    uint8_t kind;
    bool bake;
} Particle;

#define MAX_CORPSES 160
typedef struct { V2 pos; V2 vel; float angle; int spr; float t; bool gibbed; float shake; float smear_t; } Corpse;

#define MAX_DOORS 48
typedef struct {
    V2 hinge;
    float base;        /* closed angle */
    float ang;         /* offset from closed, -1.75..1.75 rad */
    float av;
    float len;
    bool glass;
    float slam_cd;
    int pusher;        /* last actor to shove it (-1 none) */
} Door;

#define MAX_CARTS 24
#define CART_CAP 20
#define VAN_CAP 99
typedef struct {
    bool alive;
    V2 pos, vel;
    float angle;
    float av;
    int holder;
    Stack items[16];
    int n;
    float crash_cd;
    float rattle_t;
    int pusher;        /* who set it rolling (-1 none) */
    float near_until;  /* counts for the list while W.time < this (seen near the player) */
} Cart;

#define MAX_FIRES 220
typedef struct { bool alive; V2 pos; float t, life; float r; int owner; float spread_t; } Fire;

#define MAX_LIGHTS 96
typedef struct { V2 pos; float r; Color c; float k; bool cone; float ang; } Light;

#define MAX_FLOATERS 32
typedef enum { FL_TEXT, FL_PRICE, FL_SALE } FloaterKind;  /* plain text, yellow price sticker, red sale sticker */
typedef struct { V2 pos; float t; char text[40]; Color c; bool big; FloaterKind kind; } Floater;

/* ============================================================ shopping */
typedef struct { ItemId id; int need, have; bool done; float flash; bool ticked; /* tick sound played */ } ListEntry;
/* where a still-missing list item is right now */
typedef enum { TR_NONE, TR_SHELF, TR_FLOOR, TR_CARRIED, TR_CART } TrailKind;

/* =========================================================== the world */
typedef struct {
    int w, h;
    Cell cells[MAP_MAX_H][MAP_MAX_W];
    Prop props[640];
    int nprops;
    Decor decor[3000];          /* baked into the ground texture */
    int ndecor;
    Decor overdecor[700];       /* drawn above walls (vines) */
    int noverdecor;
    Container conts[700];
    int nconts;
    Door doors[MAX_DOORS];
    int ndoors;
    Cart carts[MAX_CARTS];
    Actor actors[MAX_ACTORS];
    int nactors;
    Pickup pickups[MAX_PICKUPS];
    Bullet bullets[MAX_BULLETS];
    Particle parts[MAX_PARTICLES];
    int npart_next;
    Corpse corpses[MAX_CORPSES];
    int ncorpses;
    Fire fires[MAX_FIRES];
    Light lights[MAX_LIGHTS];
    int nlights;
    Floater floaters[MAX_FLOATERS];

    /* level meta */
    int level;
    const LevelDef *def;
    V2 spawn;            /* player start */
    V2 van;              /* van centre */
    SDL_FRect exit_rect;
    SDL_FRect building;  /* main building bounds (px) */
    ListEntry list[8];
    int nlist;
    ListEntry bonus[4];
    int nbonus;
    ListEntry fav[4];           /* the camp's favours for this store (off the list: nice to have, hard to get) */
    int fav_of[4];              /* ...which of FAVOURS each one is */
    int nfav;
    bool list_done;
    float list_done_t;
    /* loaded into the van before the list is done: it counts and goes home - unless somebody gets to it first */
    Stack van_load[16];
    int nvan;
    float van_alone;            /* s the load has been left unwatched (the longer, the likelier a thief) */
    int thief;                  /* the actor on the way to the van, or -1 */
    bool van_hinted;
    /* the crew you brought (crew.c) */
    int crew_actor[MAX_CREW];   /* their actors, or -1 */
    bool crew_wiped;            /* the last of the crew died: nobody holds the gate any more */
    float crew_wipe_t;

    /* baked textures */
    SDL_Texture *ground;     /* floor + overlays + blood decals */
    SDL_Texture *ambient;    /* 1px per tile ambient colours */
    bool ground_dirty;

    /* runtime */
    float time;
    float timescale, slowmo_t, hitstop;
    float shake;
    float cam_kick_x, cam_kick_y;
    int kills, execs, max_combo, combo;
    float combo_t;
    int score;
    int score_kills, score_style;
    bool took_damage;
    uint64_t weapons_used;
    int throws_kills, door_kills;
    float msg_t;
    char msg[96];
    Color msg_col;
    float hint_t;
    char hint[96];
    bool exiting;
    float exit_t;
    float dead_t;
    bool player_dead;
    float intro_t;
    float rain_t;
    Color amb_out, amb_in;
    float boss_bar;
    int boss;
    float heartbeat_t;
    int noise_events;
    float heat;          /* loud noise made by the player attracts reinforcements */
    int waves;
    float loot_cd;
    bool list_announced;  /* "list complete" fanfare already played this level */
    /* stuck on the list: first the camp radio says where to look, then the list points the way */
    float stall_t;        /* time since the list last got further */
    int list_best;        /* most list units held at once this level */
    int tip_stage;        /* 0 nothing yet, 1 radio tip given, 2 pointing the way */
    TrailKind trail;      /* the missing unit the tips are about */
    int trail_i;          /* its container / pickup / actor / cart */
    ItemId trail_id;
    char radio[96];
    float radio_t;
    /* the Greenhouse between stores (hub.c): nobody fights, the player just walks about */
    bool hub;
    uint8_t hub_lock;     /* HUB_FREE, or the player is held: talking / lifting (HUB_HELD) or on the range line (HUB_AIM) */
    bool cam_hold;        /* the camera looks at cam_at instead of following the player (the range) */
    V2 cam_at;
} World;
enum { HUB_FREE, HUB_HELD, HUB_AIM };

extern World W;

/* ============================================================== run */
typedef enum { MODE_STORY, MODE_ROGUE } GameMode;
typedef struct {
    bool active;
    GameMode mode;
    uint64_t seed;
    int level;
    int score;
    int perks[PK_COUNT];
    int hp, maxhp;
    Stack weapon;
    Stack slots[WSLOTS];      /* the rest of your weapons, and which slot the one in hand is (Actor.slots / wslot) */
    int wslot;
    Stack inv[INV_MAX];
    int ninv;
    ItemId bag;
    int kills, execs, deaths;
    float play_time;
    int retries;
    int level_scores[NUM_LEVELS];
    char level_grades[NUM_LEVELS][3];
    /* snapshot at level start (story-mode retry) */
    int snap_hp, snap_ninv, snap_wslot;
    Stack snap_weapon, snap_slots[WSLOTS], snap_inv[INV_MAX];
    ItemId snap_bag;
    int snap_kills, snap_execs;
    /* between stores: RS_LEVEL resumes at the briefing, RS_HOME (store cleared) at the greenhouse */
    int stage;
    int offer[3];             /* perks offered at the greenhouse */
    ItemCount home[8];        /* what the last list brought home */
    int nhome;
    /* the Greenhouse */
    int xp[STAT_COUNT];       /* training: strength, aim, fitness */
    uint32_t hub_done;        /* this evening: trained / searched / given (HD_* bits in hub.c); a cleared store resets it */
    Stack locker[16];         /* your locker at the Greenhouse */
    int nlocker;
    uint8_t favour[MAX_FAVOURS];   /* how each of FAVOURS stands (FS_*) */
    /* the crew (CREW in data.c): how each stands (CR_*), and the store each dead one fell in; as they were at the store's door */
    uint8_t crew[MAX_CREW];
    uint8_t crew_fell[MAX_CREW];
    uint8_t snap_crew[MAX_CREW], snap_crew_fell[MAX_CREW];
} Run;
/* a favour: not asked yet, taken on (for the store RUN.level), brought home (hand it over), came home without it (they'll
   say so), over, turned down (tonight you can still change your mind) */
enum { FS_NONE, FS_OPEN, FS_FOUND, FS_MISSED, FS_DONE, FS_DECLINED, FS_COUNT };
/* a member of the crew: at the Greenhouse holding the gate, asked along tonight, dead (for good) */
enum { CR_HOME, CR_SQUAD, CR_DEAD, CR_COUNT };

extern Run RUN;

/* ======================================================= level.c */
static inline bool in_map(int tx, int ty) { return tx >= 0 && ty >= 0 && tx < W.w && ty < W.h; }
static inline Cell *cell(int tx, int ty) { return &W.cells[ty][tx]; }
static inline int tile_of(float p) { return (int)floorf(p / TILE); }
static inline bool in_exit(V2 p) {   /* standing on the van's square */
    return p.x > W.exit_rect.x && p.x < W.exit_rect.x + W.exit_rect.w && p.y > W.exit_rect.y && p.y < W.exit_rect.y + W.exit_rect.h;
}
bool cell_flag(int tx, int ty, int f);
bool solid_at(float x, float y);
bool los_clear(V2 a, V2 b, bool for_bullets);     /* sight/shot line check incl. doors */
bool seg_cross(V2 p, V2 p2, V2 q, V2 q2);    /* segment p-p2 crosses segment q-q2 */
float ray_dist(V2 a, float ang, float maxd);
bool path_find(V2 from, V2 to, int out[][2], int max, int *len);
bool path_find_r(V2 from, V2 to, float radius, int out[][2], int max, int *len);  /* smoothing fits this radius */
bool walkable_tile(int tx, int ty);
void map_compute_autotile(void);
int floor_frames(int f);
void map_bake_ground(void);
void map_build_ambient(void);
void map_draw_floor(void);
void map_draw_walls(void);
void map_draw_objects(void);
void map_draw_overlays(void);
int obj_sprite(Cell *c);
V2 tile_center(int tx, int ty);
V2 random_floor_spot(Rng *r, bool indoor, float min_dist_from, V2 from);
void break_glass(int tx, int ty, V2 from);
void collide_circle(V2 *pos, float r, V2 *vel);

/* ======================================================== gen.c */
void gen_level(int level, uint64_t seed);
void gen_hub(uint64_t seed);
int gen_spawn_npc(int arch, V2 pos);

/* ====================================================== actor.c */
int actor_spawn(int arch, V2 pos);
void actor_update(Actor *a, int idx, float dt);
void actor_draw(Actor *a);
void actor_draw_shadow(Actor *a);
void player_update(Actor *p, float dt);
Actor *player(void);
int inv_used(Actor *a);
int inv_capacity(Actor *a);
int inv_count(Actor *a, ItemId id);
bool inv_add(Actor *a, Stack st);   /* false if no room */
bool inv_remove(Actor *a, ItemId id, int n);
bool inv_can_fit(Actor *a, ItemId id, int n);
bool make_room(Actor *a, ItemId id, int n);      /* drops junk to fit a needed item */
bool item_needed(ItemId id);
bool cart_make_room(Cart *c, ItemId id, int n);
void drop_weapon(Actor *a, bool thrown);
/* the player's weapon slots: slot k's weapon (the one in hand for k == wslot) */
static inline Stack *weapon_slot(Actor *a, int k) { return k == a->wslot ? &a->weapon : &a->slots[k]; }
int weapon_free_slot(Actor *a);                 /* an empty slot (the hands first), or -1 */
bool weapon_select(Actor *a, int k);            /* take slot k's weapon in hand, the one in hand back to its slot */
void weapon_cycle(Actor *a, int dir);           /* the next (1) / previous (-1) weapon you carry */
Stack weapon_take(Actor *a, Stack *st);         /* a weapon into your slots and in hand; returns the one it replaced */
Stack weapon_slot_remove(Actor *a, int k);      /* empty slot k, returning what was in it */
int arch_pose_sprite(Actor *a, int *grip_x, int *grip_y);
bool can_craft(Actor *a, const Recipe *r);
bool craft(Actor *a, const Recipe *r);
int total_have(ItemId id);         /* player bag + cart in exit zone etc. */
void use_heal(Actor *a);
bool cart_counts(const Cart *c);    /* this cart's load counts towards the list (and goes home with you) */
int craft_have(Actor *a, ItemId id);   /* units crafting may use (shopping-list items the list still needs are kept) */
bool craft_list_blocked(Actor *a, const Recipe *r);  /* would have the ingredients, but they're on the list */
int interact_pickup(Actor *p);     /* the floor item [E] would take right now, or -1 */
int floor_items(Actor *p, int *out, int max);   /* what lies within reach, oldest first; returns how many */
void pickup_take_weapon(Actor *p, int pi);      /* a floor weapon into your slots (the one it replaces drops) */
void pickup_collect(Actor *p, int pi);          /* a floor item into the bag / the cart you push / on your back (a bag) */
int execute_target(Actor *p);      /* the downed enemy [SPACE] would finish, or -1 */

/* ========================================================= ai.c */
void ai_update(Actor *a, int idx, float dt);
void ai_on_noise(V2 pos, float radius, int source);
void ai_on_hurt(Actor *a, int idx, int attacker);
bool actor_hostile(int a, int b);
/* steering shared with animal.c */
float diff_reaction(void);
void npc_attack(Actor *a, int idx, Actor *t, float dist, bool visible, float dt);   /* shared with crew.c */
bool path_to(Actor *a, V2 goal);
bool follow(Actor *a, float speed, float dt);   /* true when arrived */
bool walk_clear(V2 a, V2 b, float r);
void face_towards(Actor *a, float ang, float rate, float dt);
V2 flee_point(Actor *a, V2 from);

/* ====================================================== hub.c */
static inline bool is_camp(const Actor *a) { return a->arch >= AR_ROSA; }

/* ====================================================== crew.c */
static inline bool is_crew(const Actor *a) { return a->arch >= AR_HOLLIS && a->arch <= AR_WES; }
static inline int crew_index(const Actor *a) { return is_crew(a) ? a->arch - AR_HOLLIS : -1; }
bool allied(int a, int b);                       /* you and your crew: no friendly fire (blasts and fire burn anybody) */
int crew_alive(void);                            /* members of the crew still living */
int crew_squad(void);                            /* ...asked along tonight */
void crew_spawn(void);                           /* the ones you asked along get out of the van with you */
void crew_update(Actor *a, int idx, float dt);   /* stay close, go for whoever goes for you */
void crew_on_hurt(Actor *a, int idx, int attacker);
void crew_killed(int idx);                       /* dead for good */
void crew_draw_markers(void);                    /* who's yours, over their heads */
bool crew_badly_hurt(const Actor *a);            /* low enough that they're on their way back to you */

/* ===================================================== animal.c */
static inline bool is_animal(const Actor *a) { return ARCH[a->arch].animal; }
void animal_setup(Actor *a, Rng *r, bool rabid);   /* coat, temper and first state for a freshly spawned animal */
void animal_update(Actor *a, int idx, float dt);
void animal_on_hurt(Actor *a, int idx, int attacker);
void animal_on_noise(Actor *a, int idx, V2 pos, float radius, int source);
void animal_cry(Actor *a, float vol);              /* yelp / yowl / squeak when hurt or killed */
float animal_size(const Actor *a);                 /* 1 = a dog, smaller for cats and rats */
int animal_corpse(const Actor *a, bool torn);      /* corpse sprite in its own coat */
void animal_draw(Actor *a);
void animal_draw_shadow(Actor *a);

/* ====================================================== plant.c */
static inline bool is_plant(const Actor *a) { return ARCH[a->arch].plant; }
void plant_setup(Actor *a, Rng *r);                 /* look and first state for a freshly planted one */
bool plant_rooted(const Actor *a);                  /* won't budge: the rooted kinds, and a rambler dug in */
void plant_update(Actor *a, int idx, float dt);
void plant_on_hurt(Actor *a, int idx, int attacker);
void plant_on_noise(Actor *a, int idx, V2 pos, float radius, int source);
void plant_wound(Actor *a, V2 dir, int amount);     /* sap, torn leaves and a rustle where it was hit */
void plant_die(Actor *a, V2 dir, bool torn);        /* what's left of it is trodden into the ground */
void plant_draw(Actor *a);
void plant_draw_shadow(Actor *a);

/* ===================================================== combat.c */
void attack_begin(Actor *a, int idx);
void attack_update(Actor *a, int idx, float dt);
void damage_actor(int victim, int attacker, float dmg, V2 dir, float knockback, float knockdown, int weapon, int flags);
void kill_actor(int victim, int attacker, V2 dir, int weapon, int flags);
void fire_bullet(int owner, V2 pos, float ang, const WeaponDef *w);
void bullets_update(float dt);
void bullets_draw(void);
void explode(V2 pos, float radius, int owner);
void fire_spawn(V2 pos, float life, int owner);
void fires_update(float dt);
void fires_draw(void);
void throw_item(Actor *a, int idx, Stack st, float ang, float speed);
void spray_blood(V2 pos, V2 dir, int amount, float force);
void spray_sap(V2 pos, V2 dir, int amount, float force);              /* plants bleed green */
void spit_fire(int owner, V2 pos, float ang, float speed, float range);  /* a glob of acid, see Bullet.spit */
void execute_begin(Actor *a, int idx, int target);
void execute_update(Actor *a, int idx, float dt);
void combat_reset_level_state(void);
void actor_reset_level_state(void);
/* the weapon as it handles: workbench mods on top of the base stats */
static inline WeaponDef weapon_stats(const Stack *s) { return weapon_modded((ItemId)s->id, s->mods); }
int weapon_index(ItemId id);                 /* WeaponId of an item (W_FISTS for bare hands) */
int weapon_max_cond(const Stack *s);         /* full durability / fuel / magazine for this exact weapon in the player's hands */
int train_level(int stat);                   /* the player's training level 0..STAT_MAX */

enum { DMG_MELEE = 1, DMG_BULLET = 2, DMG_THROWN = 4, DMG_FIRE = 8, DMG_EXPLOSION = 16, DMG_EXEC = 32,
       DMG_DOOR = 64, DMG_CART = 128, DMG_SNEAK = 256, DMG_GORE = 512 };

/* ====================================================== world.c */
void world_start_level(int level);
void world_start_hub(void);
void world_update(float dt);
void world_draw(void);
void world_free(void);
int pickup_spawn(Stack st, V2 pos, V2 vel);
void particles_spawn(PartKind k, V2 pos, V2 vel, float z, float vz, float life, int spr, int frames);
Particle *particle_add(PartKind k, V2 pos, V2 vel, float life);
void decal(int spr, V2 pos, float angle, float scale, Color tint);
void make_noise(V2 pos, float radius, int source);
void add_light(V2 pos, float r, Color c, float k);
void add_cone(V2 pos, float ang, float len, Color c, float k);
void add_shake(float amount);
void hitstop(float t);
void slowmo(float t);
void floater(V2 pos, const char *text, Color c, bool big);
void floater_sticker(V2 pos, const char *text, FloaterKind kind);
void world_message(const char *text, Color c);
void world_hint(const char *text);
void add_score(int pts, V2 pos, const char *why);
void register_kill(int victim, int attacker, int weapon, int flags);
void list_recount(void);
int list_want(ItemId id);           /* units the list and the favours want in all (0: not on either) */
/* the van: what you load before the list is done sits there unwatched (thieves: van_thieves in world.c, AI_STEAL in ai.c) */
int van_count(ItemId id);           /* units of it loaded in the van */
int van_haul(void);                 /* units at the van a thief could take: the load and any cart left standing there */
bool cart_at_van(const Cart *c);    /* a cart left standing at the van (goes home with you - or with a thief) */
bool van_loadable(void);            /* the bag holds something the van still wants */
int van_load_bag(void);             /* [E] at the van: what the list, favours and bonus want goes in; units moved */
V2 van_spot(void);                  /* where the load is piled */
void van_rob(Actor *a, int idx);    /* a thief at the van takes the lot */
bool list_trail(V2 *pos);           /* where the tracked missing list item is now (tip stage 2 points there) */
void container_fill(Container *c, Rng *r);
int container_seen(V2 eye, V2 pos, float reach, int *out_dist);  /* container within reach of pos, reachable from eye */
bool reach_clear(V2 a, V2 b);      /* an arm/body fits from a to b: no walls, glass, solid furniture or doors */
void search_container(Actor *a, int ci, bool by_player);
void doors_update(float dt);
void doors_draw(void);
void carts_update(float dt);
void carts_draw(void);
int cart_near(V2 pos, float r);
void corpse_add(V2 pos, float angle, int spr, bool gibbed, V2 vel);
void play_at(int sfx, V2 pos, float vol, float pitch);

#endif
