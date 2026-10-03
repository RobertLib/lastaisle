/* LAST AISLE - static game data: items, weapons, recipes, archetypes, levels, perks, story */
#ifndef DATA_H
#define DATA_H

#include "common.h"

/* ------------------------------------------------------------------ items */
typedef enum {
    IT_NONE = 0,
    /* food */
    IT_BEANS, IT_SOUP, IT_TUNA, IT_PEACHES, IT_RICE, IT_PASTA, IT_PBUTTER, IT_CRACKERS,
    IT_CHOCOLATE, IT_CEREAL, IT_NOODLES, IT_WATER, IT_FORMULA, IT_HONEY, IT_DOGFOOD,
    IT_SODA, IT_JERKY, IT_COFFEE,
    /* supplies */
    IT_BATTERIES, IT_GASOLINE, IT_ANTIBIOTICS, IT_SEEDS, IT_TOILETPAPER, IT_CANDLES,
    IT_PROPANE, IT_FLASHLIGHT, IT_VITAMINS, IT_SOAP,
    /* medical */
    IT_BANDAGE, IT_PAINKILLERS, IT_MEDKIT,
    /* materials */
    IT_DUCTTAPE, IT_NAILS, IT_RAG, IT_LIGHTER, IT_SPRAYCAN, IT_BARBEDWIRE, IT_GUNPOWDER,
    IT_HACKSAW, IT_VODKA,
    /* ammo */
    IT_AMMO9, IT_SHELLS, IT_RIFLEAMMO,
    /* bags */
    IT_BACKPACK, IT_DUFFEL, IT_HIKINGPACK,
    /* weapons */
    IT_BROOM, IT_BAT, IT_NAILBAT, IT_BARBEDBAT, IT_CROWBAR, IT_PIPE, IT_GOLFCLUB, IT_PAN,
    IT_KNIFE, IT_MACHETE, IT_CLEAVER, IT_FIREAXE, IT_SLEDGE, IT_SPEAR, IT_CHAINSAW,
    IT_PISTOL, IT_REVOLVER, IT_SHOTGUN, IT_SAWNOFF, IT_RIFLE, IT_NAILGUN, IT_PIPEGUN, IT_TORCH,
    IT_BRICK, IT_BOTTLE, IT_MOLOTOV, IT_PIPEBOMB,
    IT_COUNT
} ItemId;

typedef enum { CAT_FOOD, CAT_SUPPLY, CAT_MED, CAT_MATERIAL, CAT_AMMO, CAT_BAG, CAT_WEAPON, CAT_COUNT } ItemCat;

typedef struct {
    const char *name;
    const char *desc;
    int spr;        /* icon sprite */
    ItemCat cat;
    int size;       /* bag space per unit */
    int value;      /* score when brought home as bonus */
    int weapon;     /* WeaponId, or W_NONE */
    int heal;       /* hp restored when used */
    int stack;      /* max units per bag stack */
} ItemDef;

/* ---------------------------------------------------------------- weapons */
typedef enum {
    W_NONE = -1,
    W_FISTS = 0, W_BROOM, W_BAT, W_NAILBAT, W_BARBEDBAT, W_CROWBAR, W_PIPE, W_GOLFCLUB, W_PAN,
    W_KNIFE, W_MACHETE, W_CLEAVER, W_FIREAXE, W_SLEDGE, W_SPEAR, W_CHAINSAW,
    W_PISTOL, W_REVOLVER, W_SHOTGUN, W_SAWNOFF, W_RIFLE, W_NAILGUN, W_PIPEGUN, W_TORCH,
    W_BRICK, W_BOTTLE, W_MOLOTOV, W_PIPEBOMB,
    W_COUNT
} WeaponId;

typedef enum { WK_FIST, WK_MELEE, WK_GUN, WK_THROWN, WK_CHAINSAW, WK_FLAME } WeaponKind;
typedef enum { ST_SWING, ST_STAB, ST_HEAVY } SwingStyle;

typedef struct {
    const char *name;
    ItemId item;
    WeaponKind kind;
    SwingStyle style;
    int spr;            /* held/floor sprite (w_*) */
    float damage;       /* per hit / pellet */
    float cooldown;     /* s between attacks */
    float range;        /* melee reach px / gun effective range */
    float arc;          /* melee arc, degrees */
    float knockdown;    /* chance */
    float knockback;    /* impulse px/s */
    int durability;     /* melee hits until it breaks (0 = never); fuel for chainsaw/torch */
    int mag;            /* gun magazine */
    ItemId ammo;
    int pellets;
    float spread;       /* degrees */
    float speed;        /* projectile speed */
    int pierce;
    float noise;        /* alert radius px */
    float throw_dmg;
    float windup;       /* NPC attack telegraph */
    bool two_handed;
    bool gore;          /* dismembers */
    bool lethal_throw;  /* bladed - thrown kills */
    float shake;
} WeaponDef;

/* ----------------------------------------------------------------- crafting */
typedef enum { RF_NORMAL = 0, RF_REFUEL, RF_REPAIR } RecipeFlag;
typedef struct { ItemId id; int n; } ItemCount;
typedef struct {
    ItemId out;
    int out_n;
    ItemCount in[3];
    RecipeFlag flag;
    const char *hint;
} Recipe;

/* --------------------------------------------------------------- archetypes */
typedef enum { AR_PLAYER, AR_SCAV, AR_LOOTER, AR_RAIDER, AR_BRUTE, AR_GUNNER, AR_PIG, AR_FERAL, AR_BOSS, AR_COUNT } Archetype;
typedef enum { FAC_PLAYER, FAC_SCAV, FAC_RAIDER, FAC_GANG, FAC_FERAL, FAC_COUNT } Faction;
typedef enum { TEMP_TIMID, TEMP_DEFENSIVE, TEMP_AGGRESSIVE } Temper;

typedef struct {
    const char *name;
    int spr_idle, spr_1h, spr_2h, spr_punch, spr_down, spr_dead, spr_legs;
    int hp;
    float walk, run;      /* px/s */
    float radius;
    Faction faction;
    Temper temper;
    float view;           /* sight distance px */
    float reaction;       /* s before acting on a sighting */
    float aim;            /* extra spread multiplier for guns */
    bool heavy;           /* resists knockdown */
    ItemId weapons[6];    /* weapon pool (IT_NONE = fists) */
    float loot_chance;
    int score;
    bool loots;           /* goes shopping too */
} ArchDef;

/* ------------------------------------------------------------------- levels */
typedef enum { LK_GASSTATION, LK_MARKET, LK_HARDWARE, LK_PHARMACY, LK_MEGAMART, LK_MALL } LevelKind;
typedef enum { AMB_DUSK, AMB_OVERCAST, AMB_RAIN, AMB_NIGHT, AMB_FOG, AMB_INFERNO } Ambience;

typedef struct {
    const char *name;       /* store name */
    const char *place;      /* where */
    const char *tagline;    /* old slogan */
    LevelKind kind;
    Ambience amb;
    int music;
    int map_w, map_h;
    int npcs[AR_COUNT];
    ItemCount must[3];
    ItemId pool[10];
    int pool_picks;
    ItemId bonus[8];
    int bonus_picks;
    float carried;          /* share of list items held by NPCs */
    int threat;             /* 1..5 */
    const char *radio;      /* briefing from camp */
    const char *tip;
    const char *after;      /* camp text after success */
} LevelDef;

#define NUM_LEVELS 6

/* -------------------------------------------------------------------- perks */
typedef enum {
    PK_THICKSKIN, PK_PACKMULE, PK_BRAWLER, PK_BUTCHER, PK_QUICKHANDS, PK_STEADYAIM, PK_LIGHTFEET,
    PK_HOARDER, PK_FIREBUG, PK_TINKERER, PK_EYE, PK_ADRENALINE, PK_IRONGRIP, PK_GUNSLINGER, PK_COUNT
} PerkId;
typedef struct { const char *name; const char *desc; bool stacks; } PerkDef;

/* ------------------------------------------------------------- loot tables */
typedef enum {
    Z_NONE, Z_GROCERY, Z_DRINKS, Z_FRIDGE, Z_HARDWARE, Z_PHARMACY, Z_STORAGE, Z_OFFICE,
    Z_STAFF, Z_CHECKOUT, Z_GARDEN, Z_CLOTHES, Z_ELECTRONICS, Z_RESTROOM, Z_CAMP, Z_CAR,
    Z_SNACKS, Z_COUNT
} Zone;
typedef struct { ItemId id; int weight; int lo, hi; } LootEntry;

extern const ItemDef ITEMS[IT_COUNT];
extern const WeaponDef WEAPONS[W_COUNT];
extern const Recipe RECIPES[];
extern const int NUM_RECIPES;
extern const ArchDef ARCH[AR_COUNT];
extern const LevelDef LEVELS[NUM_LEVELS];
extern const PerkDef PERKS[PK_COUNT];
extern const LootEntry *LOOT[Z_COUNT];
extern const int LOOT_N[Z_COUNT];
extern const char *const INTRO_PAGES[];
extern const int NUM_INTRO_PAGES;
extern const char *const ENDING_PAGES[];
extern const int NUM_ENDING_PAGES;
extern const char *const DEATH_LINES[];
extern const int NUM_DEATH_LINES;
extern const char *const CREDITS[];
extern const int NUM_CREDITS;
extern const char *const LOADING_TIPS[];
extern const int NUM_LOADING_TIPS;

bool faction_hostile(Faction a, Faction b);
static inline bool item_is_weapon(ItemId id) { return id > IT_NONE && id < IT_COUNT && ITEMS[id].weapon != W_NONE; }
static inline const WeaponDef *item_weapon(ItemId id) {
    return item_is_weapon(id) ? &WEAPONS[ITEMS[id].weapon] : &WEAPONS[W_FISTS];
}
int bag_capacity(ItemId bag);

#endif
