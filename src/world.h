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

typedef enum { WL_NONE, WL_CONCRETE, WL_BRICK, WL_MALL, WL_HEDGE, WL_RUIN, WL_COUNT } WallType;

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
    bool glow;         /* neon sign */
} Prop;

typedef struct { int16_t spr; int16_t x, y; uint8_t flip; uint8_t alpha; } Decor;

/* ============================================================== items */
typedef struct Stack { int16_t id, count, cond; } Stack;

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

typedef enum {
    AI_IDLE, AI_WANDER, AI_LOOT, AI_INVESTIGATE, AI_CHASE, AI_FLEE, AI_SURRENDER, AI_SEARCH, AI_GUARD
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
    V2 pos, vel, push;
    float radius;
    float face;               /* torso angle */
    float aim;                /* desired aim angle */
    float move_angle;
    float leg_anim;
    float speed_mul;
    int hp, maxhp;
    Stack weapon;             /* held weapon (id may be IT_NONE = fists) */
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
} Cart;

#define MAX_FIRES 220
typedef struct { bool alive; V2 pos; float t, life; float r; int owner; float spread_t; } Fire;

#define MAX_LIGHTS 96
typedef struct { V2 pos; float r; Color c; float k; bool cone; float ang; } Light;

#define MAX_FLOATERS 32
typedef struct { V2 pos; float t; char text[40]; Color c; bool big; } Floater;

/* ============================================================ shopping */
typedef struct { ItemId id; int need, have; bool done; float flash; } ListEntry;

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
    bool list_done;
    float list_done_t;

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
} World;

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
    Stack inv[INV_MAX];
    int ninv;
    ItemId bag;
    int kills, execs, deaths;
    float play_time;
    int retries;
    int level_scores[NUM_LEVELS];
    char level_grades[NUM_LEVELS][3];
    /* snapshot at level start (story-mode retry) */
    int snap_hp, snap_ninv;
    Stack snap_weapon, snap_inv[INV_MAX];
    ItemId snap_bag;
} Run;

extern Run RUN;

/* ======================================================= level.c */
static inline bool in_map(int tx, int ty) { return tx >= 0 && ty >= 0 && tx < W.w && ty < W.h; }
static inline Cell *cell(int tx, int ty) { return &W.cells[ty][tx]; }
static inline int tile_of(float p) { return (int)floorf(p / TILE); }
bool cell_flag(int tx, int ty, int f);
bool solid_at(float x, float y);
bool los_clear(V2 a, V2 b, bool for_bullets);     /* sight/shot line check incl. doors */
float ray_dist(V2 a, float ang, float maxd);
bool path_find(V2 from, V2 to, int out[][2], int max, int *len);
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
void equip(Actor *a, Stack st);
void drop_weapon(Actor *a, bool thrown);
int arch_pose_sprite(Actor *a, int *grip_x, int *grip_y);
bool can_craft(Actor *a, const Recipe *r);
bool craft(Actor *a, const Recipe *r);
int total_have(ItemId id);         /* player bag + cart in exit zone etc. */
void use_heal(Actor *a);
void swap_weapon(Actor *a);

/* ========================================================= ai.c */
void ai_update(Actor *a, int idx, float dt);
void ai_on_noise(V2 pos, float radius, int source);
void ai_on_hurt(Actor *a, int idx, int attacker);
bool actor_hostile(int a, int b);

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
void execute_begin(Actor *a, int idx, int target);
void execute_update(Actor *a, int idx, float dt);
void combat_reset_level_state(void);
void actor_reset_level_state(void);

enum { DMG_MELEE = 1, DMG_BULLET = 2, DMG_THROWN = 4, DMG_FIRE = 8, DMG_EXPLOSION = 16, DMG_EXEC = 32,
       DMG_DOOR = 64, DMG_CART = 128, DMG_SNEAK = 256, DMG_GORE = 512 };

/* ====================================================== world.c */
void world_start_level(int level);
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
void world_message(const char *text, Color c);
void world_hint(const char *text);
void add_score(int pts, V2 pos, const char *why);
void register_kill(int victim, int attacker, int weapon, int flags);
void list_recount(void);
void container_fill(Container *c, Rng *r);
int container_at(V2 pos, float reach, int *out_dist);
void search_container(Actor *a, int ci, bool by_player);
void doors_update(float dt);
void doors_draw(void);
void carts_update(float dt);
void carts_draw(void);
int cart_near(V2 pos, float r);
void corpse_add(V2 pos, float angle, int spr, bool gibbed, V2 vel);
void play_at(int sfx, V2 pos, float vol, float pitch);

#endif
