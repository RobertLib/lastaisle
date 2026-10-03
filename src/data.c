/* LAST AISLE - static game data */
#include "data.h"
#include "audio.h"

#define FOOD(id, nm, ds, sp, sz, val) [id] = {nm, ds, sp, CAT_FOOD, sz, val, W_NONE, 0, 9}
#define SUPP(id, nm, ds, sp, sz, val) [id] = {nm, ds, sp, CAT_SUPPLY, sz, val, W_NONE, 0, 9}
#define MED(id, nm, ds, sp, heal) [id] = {nm, ds, sp, CAT_MED, 1, 40, W_NONE, heal, 9}
#define MAT(id, nm, ds, sp, sz) [id] = {nm, ds, sp, CAT_MATERIAL, sz, 20, W_NONE, 0, 9}
#define AMMO(id, nm, ds, sp) [id] = {nm, ds, sp, CAT_AMMO, 0, 10, W_NONE, 0, 99}
#define BAG(id, nm, ds, sp) [id] = {nm, ds, sp, CAT_BAG, 0, 0, W_NONE, 0, 1}
#define WPN(id, nm, ds, sp, sz, w, st) [id] = {nm, ds, sp, CAT_WEAPON, sz, 30, w, 0, st}

const ItemDef ITEMS[IT_COUNT] = {
    [IT_NONE] = {"Nothing", "", -1, CAT_MATERIAL, 0, 0, W_NONE, 0, 1},
    FOOD(IT_BEANS, "Canned Beans", "Protein in a tin. The currency of the end times.", SPR_I_BEANS, 1, 60),
    FOOD(IT_SOUP, "Canned Soup", "Chicken noodle. Tastes like a sick day from before.", SPR_I_SOUP, 1, 60),
    FOOD(IT_TUNA, "Canned Tuna", "Smells awful. Worth its weight in gold.", SPR_I_TUNA, 1, 70),
    FOOD(IT_PEACHES, "Canned Peaches", "Sweet syrup. The kids fight over the last one.", SPR_I_PEACHES, 1, 80),
    FOOD(IT_RICE, "Bag of Rice", "Five pounds of long grain. Feeds a family for a week.", SPR_I_RICE, 2, 120),
    FOOD(IT_PASTA, "Pasta", "Dry, light, keeps forever.", SPR_I_PASTA, 1, 60),
    FOOD(IT_PBUTTER, "Peanut Butter", "Calories, fat and a little bit of joy.", SPR_I_PBUTTER, 1, 90),
    FOOD(IT_CRACKERS, "Crackers", "Stale but crunchy.", SPR_I_CRACKERS, 1, 40),
    FOOD(IT_CHOCOLATE, "Chocolate Bar", "A morale weapon.", SPR_I_CHOCOLATE, 1, 70),
    FOOD(IT_CEREAL, "Cereal", "Sugar-frosted nostalgia.", SPR_I_CEREAL, 1, 50),
    FOOD(IT_NOODLES, "Instant Noodles", "Just add water. If you have any.", SPR_I_NOODLES, 1, 40),
    FOOD(IT_WATER, "Water Jug", "Clean drinking water. Worth more than gold now.", SPR_I_WATER, 2, 120),
    FOOD(IT_FORMULA, "Baby Formula", "For the youngest at the Greenhouse. Non-negotiable.", SPR_I_FORMULA, 1, 150),
    FOOD(IT_HONEY, "Honey", "Never spoils. Ever.", SPR_I_HONEY, 1, 90),
    FOOD(IT_DOGFOOD, "Dog Food", "Don't judge.", SPR_I_DOGFOOD, 1, 30),
    FOOD(IT_SODA, "Soda", "Warm and flat. Still a treat.", SPR_I_SODA, 1, 30),
    FOOD(IT_JERKY, "Beef Jerky", "Salted, smoked and chewy.", SPR_I_JERKY, 1, 60),
    FOOD(IT_COFFEE, "Coffee", "Rosa would kill for this. Possibly literally.", SPR_I_COFFEE, 1, 200),
    SUPP(IT_BATTERIES, "Batteries", "AA. For radios, flashlights, everything.", SPR_I_BATTERIES, 1, 80),
    SUPP(IT_GASOLINE, "Gas Can", "Generator fuel. Or chainsaw fuel. Or a bottle's best friend.", SPR_I_GASOLINE, 2, 120),
    SUPP(IT_ANTIBIOTICS, "Antibiotics", "Amoxicillin. Somebody's life in a little orange bottle.", SPR_I_ANTIBIOTICS, 1, 250),
    SUPP(IT_SEEDS, "Seed Packets", "Tomatoes, beans, squash. A future, folded in paper.", SPR_I_SEEDS, 1, 300),
    SUPP(IT_TOILETPAPER, "Toilet Paper", "The true luxury of the apocalypse.", SPR_I_TOILETPAPER, 1, 150),
    SUPP(IT_CANDLES, "Candles", "Light that doesn't need a grid.", SPR_I_CANDLES, 1, 60),
    SUPP(IT_PROPANE, "Propane Tank", "For the camp stove. Do not shoot it. Or do.", SPR_I_PROPANE, 2, 110),
    SUPP(IT_FLASHLIGHT, "Flashlight", "Carrying one lights your way in the dark.", SPR_I_FLASHLIGHT, 1, 80),
    SUPP(IT_VITAMINS, "Vitamins", "Keeps scurvy away. Probably.", SPR_I_VITAMINS, 1, 90),
    SUPP(IT_SOAP, "Soap", "Hygiene saves more lives than guns.", SPR_I_SOAP, 1, 60),
    MED(IT_BANDAGE, "Bandage", "Stops the bleeding. Mostly. Restores 2 health. [F / LB]", SPR_I_BANDAGE, 2),
    MED(IT_PAINKILLERS, "Painkillers", "Numbs the hurt. Restores 3 health. [F / LB]", SPR_I_PAINKILLERS, 3),
    MED(IT_MEDKIT, "First Aid Kit", "Patches you up completely. [F / LB]", SPR_I_MEDKIT, 99),
    MAT(IT_DUCTTAPE, "Duct Tape", "Fixes everything. The crafter's best friend.", SPR_I_DUCTTAPE, 1),
    MAT(IT_NAILS, "Box of Nails", "For bats. Or for feeding a nail gun.", SPR_I_NAILS, 1),
    MAT(IT_RAG, "Rag", "Filthy cloth. A good wick.", SPR_I_RAG, 1),
    MAT(IT_LIGHTER, "Lighter", "Click. Fire.", SPR_I_LIGHTER, 0),
    MAT(IT_SPRAYCAN, "Spray Can", "Aerosol paint. Extremely flammable.", SPR_I_SPRAYCAN, 1),
    MAT(IT_BARBEDWIRE, "Barbed Wire", "Wrap it around something. Swing.", SPR_I_BARBEDWIRE, 1),
    MAT(IT_GUNPOWDER, "Gunpowder", "Black powder scraped from fireworks. Careful.", SPR_I_GUNPOWDER, 1),
    MAT(IT_HACKSAW, "Hacksaw", "Cuts metal. Shortens barrels.", SPR_I_HACKSAW, 1),
    MAT(IT_VODKA, "Vodka", "Disinfectant, fuel or courage.", SPR_I_VODKA, 1),
    MAT(IT_SCRAP, "Scrap Metal", "Bent sheet, bolts, a hinge. Gus can make anything out of it. [workbench]", SPR_I_SCRAP, 1),
    MAT(IT_SPRINGS, "Springs & Screws", "The little parts that make guns work. [workbench]", SPR_I_SPRINGS, 1),
    MAT(IT_WHETSTONE, "Whetstone", "Puts the edge back on anything with a blade. [workbench]", SPR_I_WHETSTONE, 1),
    AMMO(IT_AMMO9, "Handgun Ammo", "Rounds for pistols and revolvers.", SPR_I_AMMO9),
    AMMO(IT_SHELLS, "Shotgun Shells", "12 gauge. Close and personal.", SPR_I_SHELLS),
    AMMO(IT_RIFLEAMMO, "Rifle Rounds", ".308. Rare as hen's teeth.", SPR_I_RIFLEAMMO),
    BAG(IT_BACKPACK, "Backpack", "Your trusty school bag. 8 space.", SPR_I_BACKPACK),
    BAG(IT_DUFFEL, "Duffel Bag", "Roomy. 12 space.", SPR_I_DUFFEL),
    BAG(IT_HIKINGPACK, "Hiking Pack", "A serious pack. 16 space.", SPR_I_HIKINGPACK),
    WPN(IT_BROOM, "Broom", "Long reach, weak hits. Sweeps people off their feet.", SPR_I_BROOM, 2, W_BROOM, 1),
    WPN(IT_BAT, "Baseball Bat", "America's pastime. Reliable.", SPR_I_BAT, 2, W_BAT, 1),
    WPN(IT_NAILBAT, "Nail Bat", "A bat with opinions.", SPR_I_NAILBAT, 2, W_NAILBAT, 1),
    WPN(IT_BARBEDBAT, "Barbed Bat", "Wrapped in wire. Leaves a mark.", SPR_I_BARBEDBAT, 2, W_BARBEDBAT, 1),
    WPN(IT_CROWBAR, "Crowbar", "Opens doors. And skulls. Very durable.", SPR_I_CROWBAR, 1, W_CROWBAR, 1),
    WPN(IT_PIPE, "Lead Pipe", "Heavy, simple, honest.", SPR_I_PIPE, 1, W_PIPE, 1),
    WPN(IT_GOLFCLUB, "Golf Club", "Fore. Breaks easily.", SPR_I_GOLFCLUB, 2, W_GOLFCLUB, 1),
    WPN(IT_PAN, "Frying Pan", "BONK. Knocks almost anyone down.", SPR_I_PAN, 1, W_PAN, 1),
    WPN(IT_KNIFE, "Kitchen Knife", "Fast and deadly. Lethal when thrown.", SPR_I_KNIFE, 1, W_KNIFE, 1),
    WPN(IT_MACHETE, "Machete", "Clears jungles. And aisles.", SPR_I_MACHETE, 1, W_MACHETE, 1),
    WPN(IT_CLEAVER, "Meat Cleaver", "The Pigs' favourite. Lethal when thrown.", SPR_I_CLEAVER, 1, W_CLEAVER, 1),
    WPN(IT_FIREAXE, "Fire Axe", "Break glass in case of emergency.", SPR_I_FIREAXE, 2, W_FIREAXE, 1),
    WPN(IT_SLEDGE, "Sledgehammer", "Slow. Devastating. Always knocks down.", SPR_I_SLEDGE, 3, W_SLEDGE, 1),
    WPN(IT_SPEAR, "Spear", "Broom + knife + tape. Huge reach.", SPR_I_SPEAR, 3, W_SPEAR, 1),
    WPN(IT_CHAINSAW, "Chainsaw", "Needs fuel. Needs nothing else.", SPR_I_CHAINSAW, 3, W_CHAINSAW, 1),
    WPN(IT_PISTOL, "Pistol", "9mm. Loud. Everyone will hear it.", SPR_I_PISTOL, 1, W_PISTOL, 1),
    WPN(IT_REVOLVER, "Revolver", "Six shots that go through people.", SPR_I_REVOLVER, 1, W_REVOLVER, 1),
    WPN(IT_SHOTGUN, "Shotgun", "Pump-action crowd control.", SPR_I_SHOTGUN, 2, W_SHOTGUN, 1),
    WPN(IT_SAWNOFF, "Sawn-off", "Two barrels. Huge spread. Short range.", SPR_I_SAWNOFF, 1, W_SAWNOFF, 1),
    WPN(IT_RIFLE, "Hunting Rifle", "One shot, one... several kills.", SPR_I_RIFLE, 3, W_RIFLE, 1),
    WPN(IT_NAILGUN, "Nail Gun", "Fires nails. Quiet. Reload with boxes of nails.", SPR_I_NAILGUN, 1, W_NAILGUN, 1),
    WPN(IT_PIPEGUN, "Pipe Gun", "Single-shot zip gun. Uses shells.", SPR_I_PIPEGUN, 1, W_PIPEGUN, 1),
    WPN(IT_TORCH, "Aerosol Torch", "Spray can + lighter. A tiny flamethrower.", SPR_I_TORCH, 1, W_TORCH, 1),
    WPN(IT_BRICK, "Brick", "Throw it. Knocks people down.", SPR_I_BRICK, 1, W_BRICK, 3),
    WPN(IT_BOTTLE, "Glass Bottle", "Throw it, or make a molotov.", SPR_I_BOTTLE, 1, W_BOTTLE, 3),
    WPN(IT_MOLOTOV, "Molotov", "Throw it. Watch it burn.", SPR_I_MOLOTOV, 1, W_MOLOTOV, 3),
    WPN(IT_PIPEBOMB, "Pipe Bomb", "Short fuse. Big boom.", SPR_I_PIPEBOMB, 1, W_PIPEBOMB, 3),
};

/*                  name            item         kind        style     sprite        dmg  cd    range arc  kd    kb   dur mag ammo        pel spread speed pierce noise thr  windup 2h    gore   lethal shake */
const WeaponDef WEAPONS[W_COUNT] = {
    [W_FISTS]     = {"Fists",        IT_NONE,     WK_FIST,    ST_STAB,  -1,           1,   0.30f, 13,   70,  0.25f, 110,  0, 0, IT_NONE,   0, 0,  0,    0, 70,  0,   0.30f, false, false, false, 1.5f},
    [W_BROOM]     = {"Broom",        IT_BROOM,    WK_MELEE,   ST_SWING, SPR_W_BROOM,  1,   0.32f, 22,   100, 0.55f, 170, 10, 0, IT_NONE,   0, 0,  0,    0, 90,  1,   0.30f, true,  false, false, 2},
    [W_BAT]       = {"Baseball Bat", IT_BAT,      WK_MELEE,   ST_SWING, SPR_W_BAT,    2,   0.38f, 18,   110, 0.45f, 190, 22, 0, IT_NONE,   0, 0,  0,    0, 110, 1,   0.32f, false, false, false, 3},
    [W_NAILBAT]   = {"Nail Bat",     IT_NAILBAT,  WK_MELEE,   ST_SWING, SPR_W_NAILBAT,3,   0.38f, 18,   110, 0.45f, 190, 28, 0, IT_NONE,   0, 0,  0,    0, 110, 2,   0.32f, false, true,  false, 3.5f},
    [W_BARBEDBAT] = {"Barbed Bat",   IT_BARBEDBAT,WK_MELEE,   ST_SWING, SPR_W_BARBEDBAT,3, 0.40f, 18,   110, 0.55f, 200, 32, 0, IT_NONE,   0, 0,  0,    0, 110, 2,   0.32f, false, true,  false, 3.5f},
    [W_CROWBAR]   = {"Crowbar",      IT_CROWBAR,  WK_MELEE,   ST_SWING, SPR_W_CROWBAR,2,   0.34f, 16,   95,  0.35f, 150, 45, 0, IT_NONE,   0, 0,  0,    0, 110, 2,   0.28f, false, false, false, 3},
    [W_PIPE]      = {"Lead Pipe",    IT_PIPE,     WK_MELEE,   ST_SWING, SPR_W_PIPE,   2,   0.36f, 17,   100, 0.40f, 160, 34, 0, IT_NONE,   0, 0,  0,    0, 110, 1,   0.30f, false, false, false, 3},
    [W_GOLFCLUB]  = {"Golf Club",    IT_GOLFCLUB, WK_MELEE,   ST_SWING, SPR_W_GOLFCLUB,2,  0.36f, 20,   110, 0.40f, 190, 16, 0, IT_NONE,   0, 0,  0,    0, 110, 1,   0.30f, false, false, false, 3},
    [W_PAN]       = {"Frying Pan",   IT_PAN,      WK_MELEE,   ST_SWING, SPR_W_PAN,    2,   0.40f, 15,   100, 0.85f, 150, 36, 0, IT_NONE,   0, 0,  0,    0, 140, 2,   0.32f, false, false, false, 3},
    [W_KNIFE]     = {"Kitchen Knife",IT_KNIFE,    WK_MELEE,   ST_STAB,  SPR_W_KNIFE,  3,   0.22f, 14,   55,  0.00f, 70,  30, 0, IT_NONE,   0, 0,  0,    0, 60,  5,   0.22f, false, false, true,  2},
    [W_MACHETE]   = {"Machete",      IT_MACHETE,  WK_MELEE,   ST_SWING, SPR_W_MACHETE,3,   0.30f, 17,   100, 0.10f, 110, 34, 0, IT_NONE,   0, 0,  0,    0, 80,  4,   0.28f, false, true,  true,  3},
    [W_CLEAVER]   = {"Meat Cleaver", IT_CLEAVER,  WK_MELEE,   ST_SWING, SPR_W_CLEAVER,3,   0.28f, 15,   80,  0.10f, 100, 30, 0, IT_NONE,   0, 0,  0,    0, 80,  5,   0.26f, false, true,  true,  3},
    [W_FIREAXE]   = {"Fire Axe",     IT_FIREAXE,  WK_MELEE,   ST_HEAVY, SPR_W_FIREAXE,5,   0.55f, 19,   110, 0.50f, 230, 28, 0, IT_NONE,   0, 0,  0,    0, 130, 6,   0.45f, true,  true,  true,  5},
    [W_SLEDGE]    = {"Sledgehammer", IT_SLEDGE,   WK_MELEE,   ST_HEAVY, SPR_W_SLEDGE, 6,   0.72f, 20,   120, 1.00f, 300, 32, 0, IT_NONE,   0, 0,  0,    0, 160, 3,   0.55f, true,  true,  false, 6},
    [W_SPEAR]     = {"Spear",        IT_SPEAR,    WK_MELEE,   ST_STAB,  SPR_W_SPEAR,  3,   0.36f, 28,   34,  0.20f, 160, 24, 0, IT_NONE,   0, 0,  0,    0, 80,  5,   0.32f, true,  false, true,  3},
    [W_CHAINSAW]  = {"Chainsaw",     IT_CHAINSAW, WK_CHAINSAW,ST_STAB,  SPR_W_CHAINSAW,1,  0.07f, 18,   70,  0.00f, 40, 100, 0, IT_GASOLINE,0,0,  0,    0, 260, 2,   0.40f, true,  true,  false, 1.5f},
    [W_PISTOL]    = {"Pistol",       IT_PISTOL,   WK_GUN,     ST_STAB,  SPR_W_PISTOL, 3,   0.20f, 400,  0,   0.00f, 60,   0, 12, IT_AMMO9,  1, 4,  760,  0, 330, 1,   0.45f, false, false, false, 2.5f},
    [W_REVOLVER]  = {"Revolver",     IT_REVOLVER, WK_GUN,     ST_STAB,  SPR_W_REVOLVER,5,  0.42f, 420,  0,   0.00f, 120,  0, 6,  IT_AMMO9,  1, 1.5f,860, 1, 350, 1,   0.50f, false, false, false, 4},
    [W_SHOTGUN]   = {"Shotgun",      IT_SHOTGUN,  WK_GUN,     ST_STAB,  SPR_W_SHOTGUN,2,   0.72f, 240,  0,   0.25f, 200,  0, 6,  IT_SHELLS, 7, 26, 620,  0, 380, 1,   0.55f, true,  true,  false, 6},
    [W_SAWNOFF]   = {"Sawn-off",     IT_SAWNOFF,  WK_GUN,     ST_STAB,  SPR_W_SAWNOFF,2,   0.42f, 150,  0,   0.35f, 240,  0, 2,  IT_SHELLS, 9, 44, 560,  0, 380, 1,   0.45f, false, true,  false, 7},
    [W_RIFLE]     = {"Hunting Rifle",IT_RIFLE,    WK_GUN,     ST_STAB,  SPR_W_RIFLE,  9,   0.95f, 600,  0,   0.20f, 260,  0, 5,  IT_RIFLEAMMO,1,0.5f,1100,2,460, 1,   0.70f, true,  true,  false, 6},
    [W_NAILGUN]   = {"Nail Gun",     IT_NAILGUN,  WK_GUN,     ST_STAB,  SPR_W_NAILGUN,2,   0.13f, 260,  0,   0.00f, 50,   0, 30, IT_NAILS,  1, 7,  480,  0, 90,  1,   0.35f, false, false, false, 1.2f},
    [W_PIPEGUN]   = {"Pipe Gun",     IT_PIPEGUN,  WK_GUN,     ST_STAB,  SPR_W_PIPEGUN,2,   0.30f, 180,  0,   0.30f, 220,  0, 1,  IT_SHELLS, 6, 32, 560,  0, 360, 1,   0.50f, false, true,  false, 6},
    [W_TORCH]     = {"Aerosol Torch",IT_TORCH,    WK_FLAME,   ST_STAB,  SPR_W_TORCH,  1,   0.05f, 44,   0,   0.00f, 0,  70, 0, IT_SPRAYCAN,0, 14, 160,  0, 80,  1,   0.40f, false, false, false, 0.5f},
    [W_BRICK]     = {"Brick",        IT_BRICK,    WK_THROWN,  ST_STAB,  SPR_W_BRICK,  1,   0.35f, 0,    0,   1.00f, 0,   0, 0, IT_NONE,   0, 0,  0,    0, 100, 2,   0.40f, false, false, false, 2},
    [W_BOTTLE]    = {"Glass Bottle", IT_BOTTLE,   WK_THROWN,  ST_STAB,  SPR_W_BOTTLE, 1,   0.35f, 0,    0,   0.80f, 0,   0, 0, IT_NONE,   0, 0,  0,    0, 140, 1,   0.40f, false, false, false, 2},
    [W_MOLOTOV]   = {"Molotov",      IT_MOLOTOV,  WK_THROWN,  ST_STAB,  SPR_W_MOLOTOV,1,   0.45f, 0,    0,   0.00f, 0,   0, 0, IT_NONE,   0, 0,  0,    0, 200, 1,   0.50f, false, false, false, 3},
    [W_PIPEBOMB]  = {"Pipe Bomb",    IT_PIPEBOMB, WK_THROWN,  ST_STAB,  SPR_W_PIPEBOMB,1,  0.45f, 0,    0,   0.00f, 0,   0, 0, IT_NONE,   0, 0,  0,    0, 100, 1,   0.50f, false, false, false, 3},
};

const Recipe RECIPES[] = {
    {IT_NAILBAT,   1, {{IT_BAT, 1}, {IT_NAILS, 1}}, RF_NORMAL, "Hammer some nails through a bat."},
    {IT_BARBEDBAT, 1, {{IT_BAT, 1}, {IT_BARBEDWIRE, 1}}, RF_NORMAL, "Wrap barbed wire around a bat."},
    {IT_SPEAR,     1, {{IT_BROOM, 1}, {IT_KNIFE, 1}, {IT_DUCTTAPE, 1}}, RF_NORMAL, "Tape a knife to a broom handle."},
    {IT_MOLOTOV,   2, {{IT_BOTTLE, 2}, {IT_GASOLINE, 1}, {IT_RAG, 1}}, RF_NORMAL, "Two bottles of fire."},
    {IT_MOLOTOV,   1, {{IT_BOTTLE, 1}, {IT_VODKA, 1}, {IT_RAG, 1}}, RF_NORMAL, "Waste of good vodka. Worth it."},
    {IT_TORCH,     1, {{IT_SPRAYCAN, 1}, {IT_LIGHTER, 1}, {IT_DUCTTAPE, 1}}, RF_NORMAL, "Tape a lighter to a spray can."},
    {IT_PIPEBOMB,  2, {{IT_PIPE, 1}, {IT_GUNPOWDER, 1}, {IT_DUCTTAPE, 1}}, RF_NORMAL, "Pack powder in a pipe. Two of them."},
    {IT_PIPEGUN,   1, {{IT_PIPE, 1}, {IT_NAILS, 1}, {IT_DUCTTAPE, 1}}, RF_NORMAL, "A zip gun. Fires shotgun shells."},
    {IT_SAWNOFF,   1, {{IT_SHOTGUN, 1}, {IT_HACKSAW, 1}}, RF_NORMAL, "Shorter barrel, wider spread."},
    {IT_CHAINSAW,  1, {{IT_CHAINSAW, 1}, {IT_GASOLINE, 1}}, RF_REFUEL, "Refuel the chainsaw."},
    {IT_NONE,      1, {{IT_DUCTTAPE, 1}}, RF_REPAIR, "Tape up your held melee weapon."},
    {IT_BANDAGE,   2, {{IT_RAG, 1}, {IT_VODKA, 1}}, RF_NORMAL, "Sterilised rags."},
    {IT_BANDAGE,   1, {{IT_RAG, 1}, {IT_DUCTTAPE, 1}}, RF_NORMAL, "Field dressing."},
    {IT_MEDKIT,    1, {{IT_BANDAGE, 2}, {IT_PAINKILLERS, 1}}, RF_NORMAL, "Put together a proper kit."},
};
const int NUM_RECIPES = ARRAY_LEN(RECIPES);

#define SPRS(a) SPR_##a##_IDLE, SPR_##a##_1H, SPR_##a##_2H, SPR_##a##_PUNCH, SPR_##a##_DOWN, SPR_##a##_DEAD, SPR_LEGS_##a
const ArchDef ARCH[AR_COUNT] = {
    /* name, sprites, hp, walk, run, radius, faction, temper, view, reaction, aim, heavy, weapons, loot, score, loots */
    [AR_PLAYER] = {"You", SPRS(PLAYER), 8, 0, 96, 5.5f, FAC_PLAYER, TEMP_AGGRESSIVE, 0, 0, 1, false,
                   {IT_NONE}, 0, 0, false},
    [AR_SCAV]   = {"Scavenger", SPRS(SCAV), 3, 38, 88, 5.5f, FAC_SCAV, TEMP_TIMID, 170, 0.55f, 1.6f, false,
                   {IT_NONE, IT_NONE, IT_BROOM, IT_KNIFE, IT_PIPE, IT_NONE}, 0.65f, 100, true},
    [AR_LOOTER] = {"Looter", SPRS(LOOTER), 3, 40, 90, 5.5f, FAC_SCAV, TEMP_DEFENSIVE, 180, 0.45f, 1.4f, false,
                   {IT_BAT, IT_CROWBAR, IT_KNIFE, IT_GOLFCLUB, IT_PAN, IT_NONE}, 0.7f, 120, true},
    [AR_RAIDER] = {"Raider", SPRS(RAIDER), 4, 42, 92, 5.5f, FAC_RAIDER, TEMP_AGGRESSIVE, 200, 0.40f, 1.2f, false,
                   {IT_BAT, IT_MACHETE, IT_CROWBAR, IT_PIPE, IT_NAILBAT, IT_KNIFE}, 0.35f, 150, true},
    [AR_BRUTE]  = {"Brute", SPRS(BRUTE), 12, 34, 70, 7.0f, FAC_RAIDER, TEMP_AGGRESSIVE, 180, 0.55f, 1.3f, true,
                   {IT_SLEDGE, IT_FIREAXE, IT_SLEDGE, IT_PIPE, IT_SLEDGE, IT_FIREAXE}, 0.4f, 300, false},
    [AR_GUNNER] = {"Gunman", SPRS(GUNNER), 4, 40, 84, 5.5f, FAC_RAIDER, TEMP_AGGRESSIVE, 230, 0.55f, 1.0f, false,
                   {IT_PISTOL, IT_PISTOL, IT_SHOTGUN, IT_REVOLVER, IT_PISTOL, IT_RIFLE}, 0.3f, 200, false},
    [AR_PIG]    = {"Pig", SPRS(PIG), 5, 46, 104, 5.5f, FAC_GANG, TEMP_AGGRESSIVE, 210, 0.32f, 1.1f, false,
                   {IT_CLEAVER, IT_KNIFE, IT_CLEAVER, IT_MACHETE, IT_SAWNOFF, IT_CHAINSAW}, 0.3f, 220, true},
    [AR_FERAL]  = {"Feral", SPRS(FERAL), 3, 50, 118, 5.0f, FAC_FERAL, TEMP_AGGRESSIVE, 190, 0.25f, 2.0f, false,
                   {IT_NONE, IT_NONE, IT_BRICK, IT_KNIFE, IT_PIPE, IT_NONE}, 0.2f, 180, false},
    [AR_BOSS]   = {"The Mall King", SPRS(BOSS), 48, 40, 82, 10.0f, FAC_GANG, TEMP_AGGRESSIVE, 300, 0.30f, 0.9f, true,
                   {IT_SHOTGUN}, 1.0f, 3000, false},
    /* animals: walk / bite / dead sprites are their first coat (animal.c picks the coat); score only for rabid ones */
#define ANIMAL(a) SPR_##a##_WALK, -1, -1, SPR_##a##_BITE, -1, SPR_##a##_DEAD, -1
    [AR_DOG]    = {"Stray dog", ANIMAL(DOG), 2, 30, 124, 4.5f, FAC_ANIMAL, TEMP_DEFENSIVE, 0, 0, 1, false, {IT_NONE}, 0, 80, false, true},
    [AR_CAT]    = {"Cat", ANIMAL(CAT), 1, 28, 132, 3.5f, FAC_ANIMAL, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 50, false, true},
    [AR_FOX]    = {"Fox", ANIMAL(FOX), 2, 32, 118, 4.0f, FAC_ANIMAL, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 70, false, true},
    [AR_RAT]    = {"Rat", ANIMAL(RAT), 1, 24, 92, 2.5f, FAC_ANIMAL, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 30, false, true},
#undef ANIMAL
    /* plants: plant.c draws them (rooted ones as a leaf rosette under a head that turns); only the rambler walks */
#define PLANT(a, idle, atk) SPR_##a##_##idle, -1, -1, SPR_##a##_##atk, -1, SPR_##a##_DEAD, -1
    [AR_SPITTER] = {"Spitter", PLANT(SPITTER, BASE, HEAD), 2, 0, 0, 5.0f, FAC_PLANT, TEMP_AGGRESSIVE, 0, 0, 1, false, {IT_NONE}, 0, 60, false, false, true},
    [AR_NETTLE]  = {"Nettle", PLANT(NETTLE, BASE, HEAD), 3, 16, 0, 5.5f, FAC_PLANT, TEMP_AGGRESSIVE, 0, 0, 1, false, {IT_NONE}, 0, 80, false, false, true},
    [AR_RAMBLER] = {"Rambler", PLANT(RAMBLER, WALK, BITE), 4, 24, 66, 6.5f, FAC_PLANT, TEMP_AGGRESSIVE, 0, 0, 1, false, {IT_NONE}, 0, 120, false, false, true},
#undef PLANT
    /* the crew: the camp's own gear in their own colours; they fight beside you if you ask them along (crew.c) */
    [AR_HOLLIS] = {"Hollis", SPRS(HOLLIS), 6, 40, 100, 5.5f, FAC_PLAYER, TEMP_AGGRESSIVE, 230, 0.30f, 0.85f, false, {IT_PISTOL}, 0, 0, false},
    [AR_BEX]    = {"Bex", SPRS(BEX), 5, 44, 106, 5.0f, FAC_PLAYER, TEMP_AGGRESSIVE, 210, 0.20f, 1.2f, false, {IT_MACHETE}, 0, 0, false},
    [AR_OZZIE]  = {"Ozzie", SPRS(OZZIE), 8, 36, 92, 6.0f, FAC_PLAYER, TEMP_AGGRESSIVE, 200, 0.35f, 1.3f, true, {IT_FIREAXE}, 0, 0, false},
    [AR_CARMEN] = {"Carmen", SPRS(CARMEN), 6, 40, 98, 5.5f, FAC_PLAYER, TEMP_AGGRESSIVE, 230, 0.30f, 1.0f, false, {IT_SHOTGUN}, 0, 0, false},
    [AR_WES]    = {"Wes", SPRS(WES), 6, 38, 96, 5.5f, FAC_PLAYER, TEMP_AGGRESSIVE, 200, 0.30f, 1.2f, false, {IT_NAILBAT}, 0, 0, false},
    /* the people at the Greenhouse: hub.c walks them about, nobody fights there (one standing pose) */
#define CAMP(a, l) SPR_##a##_IDLE, SPR_##a##_IDLE, SPR_##a##_IDLE, SPR_##a##_IDLE, SPR_SCAV_DOWN, SPR_SCAV_DEAD, SPR_LEGS_##l
    [AR_ROSA]   = {"Rosa", CAMP(ROSA, ROSA), 3, 30, 70, 5.5f, FAC_SCAV, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 0, false},
    [AR_THEO]   = {"Theo", CAMP(THEO, THEO), 3, 40, 90, 4.0f, FAC_SCAV, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 0, false},
    [AR_GUS]    = {"Gus", CAMP(GUS, GUS), 3, 26, 60, 5.5f, FAC_SCAV, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 0, false},
    [AR_JUNE]   = {"June", CAMP(JUNE, JUNE), 3, 32, 80, 5.5f, FAC_SCAV, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 0, false},
    [AR_DEE]    = {"Big Dee", CAMP(DEE, DEE), 3, 28, 70, 6.5f, FAC_SCAV, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 0, false},
    [AR_MARTA]  = {"Marta", CAMP(MARTA, MARTA), 3, 26, 60, 5.5f, FAC_SCAV, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 0, false},
    [AR_FOLK_A] = {"Neighbour", CAMP(FOLK_A, FOLK_A), 3, 28, 70, 5.5f, FAC_SCAV, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 0, false},
    [AR_FOLK_B] = {"Neighbour", CAMP(FOLK_B, FOLK_B), 3, 28, 70, 5.5f, FAC_SCAV, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 0, false},
    [AR_FOLK_C] = {"Neighbour", CAMP(FOLK_C, FOLK_C), 3, 28, 70, 5.5f, FAC_SCAV, TEMP_TIMID, 0, 0, 1, false, {IT_NONE}, 0, 0, false},
#undef CAMP
};

/* faction hostility: row attacks column on sight (animals: only the rabid ones; plants: see actor_hostile) */
static const bool HOSTILE[FAC_COUNT][FAC_COUNT] = {
    /*            PLAYER SCAV   RAIDER GANG   FERAL  ANIMAL PLANT */
    /* PLAYER */ {false, false, true,  true,  true,  false, false},
    /* SCAV   */ {false, false, false, false, false, false, false},
    /* RAIDER */ {true,  true,  false, true,  true,  false, false},
    /* GANG   */ {true,  true,  true,  false, true,  false, false},
    /* FERAL  */ {true,  true,  true,  true,  false, false, false},
    /* ANIMAL */ {false, false, false, false, false, false, false},
    /* PLANT  */ {false, false, false, false, false, false, false},
};
bool faction_hostile(Faction a, Faction b) { return HOSTILE[a][b]; }

/* ----------------------------------------------------------------- workbench */
const ModDef MODS[MOD_COUNT] = {
    [MOD_SPIKES]     = {"Spikes", "SPIKED", "Nails through the business end. +1 damage, and it tears.", {{IT_NAILS, 1}, {IT_DUCTTAPE, 1}}},
    [MOD_HONED]      = {"Honed Edge", "HONED", "Ground back to a razor edge. +1 damage.", {{IT_WHETSTONE, 1}}},
    [MOD_GRIP]       = {"Wrapped Grip", "GRIP", "Rag and tape round the handle. Swings 15% faster.", {{IT_RAG, 1}, {IT_DUCTTAPE, 1}}},
    [MOD_REINFORCED] = {"Reinforced", "REINF", "Scrap plates bolted on. Lasts 60% longer, and leaves the bench like new.",
                        {{IT_SCRAP, 1}, {IT_DUCTTAPE, 1}}},
    [MOD_WEIGHTED]   = {"Weighted Head", "HEAVY", "A lump of iron on the end. Knocks people down far more often.", {{IT_SCRAP, 2}}},
    [MOD_HOTLOADS]   = {"Hot Loads", "HOT", "A chamber that takes hand-packed rounds. +1 damage a shot, a bit louder.",
                        {{IT_GUNPOWDER, 1}, {IT_SCRAP, 1}}},
    [MOD_SIGHTS]     = {"Iron Sights", "SIGHTS", "Filed and pinned sights. 40% tighter spread.", {{IT_SCRAP, 1}, {IT_SPRINGS, 1}}},
    [MOD_EXTMAG]     = {"Extended Mag", "+MAG", "A longer magazine, a stronger spring. Holds 50% more.", {{IT_SPRINGS, 2}, {IT_DUCTTAPE, 1}}},
    [MOD_SUPPRESSOR] = {"Bottle Suppressor", "QUIET", "A soda bottle stuffed with rags. Shots carry a third as far.",
                        {{IT_SODA, 1}, {IT_RAG, 1}, {IT_DUCTTAPE, 1}}},
};

bool mod_fits(ItemId id, int m) {
    if (!item_is_weapon(id) || ITEMS[id].stack > 1) return false;
    const WeaponDef *w = item_weapon(id);
    switch (m) {
    case MOD_SPIKES: return w->kind == WK_MELEE && !w->lethal_throw && !w->gore;
    case MOD_HONED: return w->kind == WK_MELEE && w->lethal_throw;
    case MOD_GRIP:
    case MOD_REINFORCED: return w->kind == WK_MELEE;
    case MOD_WEIGHTED: return w->kind == WK_MELEE && w->style != ST_STAB;
    case MOD_HOTLOADS: return w->kind == WK_GUN && w->ammo != IT_NAILS;
    case MOD_SIGHTS: return w->kind == WK_GUN;
    case MOD_EXTMAG: return w->kind == WK_GUN && w->mag > 2;
    case MOD_SUPPRESSOR: return w->kind == WK_GUN && (w->ammo == IT_AMMO9 || w->ammo == IT_RIFLEAMMO);
    default: return false;
    }
}

WeaponDef weapon_modded(ItemId id, int mods) {
    WeaponDef w = *item_weapon(id);
    for (int m = 0; m < MOD_COUNT; m++)
        if (((mods >> m) & 1) && !mod_fits(id, m)) mods &= ~(1 << m);
    if (mods & (1 << MOD_SPIKES)) { w.damage += 1; w.gore = true; }
    if (mods & (1 << MOD_HONED)) w.damage += 1;
    if (mods & (1 << MOD_GRIP)) w.cooldown *= 0.85f;
    if (mods & (1 << MOD_REINFORCED)) w.durability = (int)(w.durability * 1.6f + 0.5f);
    if (mods & (1 << MOD_WEIGHTED)) { w.knockdown = MINF(1.0f, w.knockdown + 0.25f); w.knockback *= 1.4f; w.shake += 1; }
    if (mods & (1 << MOD_HOTLOADS)) { w.damage += 1; w.noise *= 1.15f; w.shake += 0.5f; }
    if (mods & (1 << MOD_SIGHTS)) w.spread *= 0.6f;
    if (mods & (1 << MOD_EXTMAG)) w.mag = (int)(w.mag * 1.5f + 0.5f);
    if (mods & (1 << MOD_SUPPRESSOR)) w.noise *= 0.35f;
    return w;
}

/* ------------------------------------------------------------------ training */
const TrainDef TRAIN[STAT_COUNT] = {
    [STAT_STR] = {"STRENGTH", "the weight bench", "Harder hits - a chance of +1 damage - and more knockdowns."},
    [STAT_AIM] = {"AIM", "the shooting range", "Tighter spread and faster reloads."},
    [STAT_FIT] = {"FITNESS", "the running course", "You run faster."},
};

/* ------------------------------------------------------------- favours */
/* none for Paradise Mall: nobody would be home to hand it to */
const FavourDef FAVOURS[] = {
    {AR_THEO, 0, IT_CHOCOLATE, 1, FW_CARRIED,
     "Can you get chocolate? Rosa says no, but you can say yes.|Chocolate always goes first. Somebody big will have it in "
     "his pocket. Make him give it back.",
     "CHOCOLATE! You actually found some!|I'm going to share it with Kevin. Kevin's a bean, so it's all mine really.|"
     "Marta gives me these for being brave. You were braver. And now race me round the flags! ...You can win.",
     "No chocolate? ...That's okay. Rosa says it rots your teeth. I don't believe her.",
     {{IT_PAINKILLERS, 1}}, 0, STAT_FIT, 40},
    {AR_MARTA, 1, IT_HONEY, 1, FW_HIDDEN,
     "One thing more, if you've room. Honey. Real honey - for coughs, for burns, for the little ones.|Freshway kept the good "
     "jars out back, where the staff could get at them. Not in the aisles - try the back rooms.",
     "Oh, the real thing. Look at the colour of it.|That's every cough this winter taken care of.|Here - I've been keeping "
     "this for whoever needs it most. Out there, that's you.",
     "No honey? Never mind. Salt water and stubbornness. It's worked so far.",
     {{IT_MEDKIT, 1}}, 0, 0, 0},
    {AR_DEE, 1, IT_PBUTTER, 2, FW_CARRIED,
     "And, uh. Peanut butter. Two jars. Protein.|The raiders down there live on it. The big ones keep it in their "
     "pockets. Take it out of their pockets.",
     "Two jars. You're a saint.|Come here. Lift with me - no, like this. Elbows in. Breathe. Feel that? That's the trick "
     "nobody taught you.",
     "No peanut butter? Beans have protein. Sad protein.",
     {{IT_NONE}}, 0, STAT_STR, 70},
    {AR_GUS, 2, IT_HACKSAW, 1, FW_HIDDEN,
     "Oh - and a hacksaw. Mine snapped clean in half on a padlock.|Build-Rite kept the good tools locked up in the back. "
     "Stockroom, staff room, office - the shelves'll be bare.",
     "Now THAT's a saw. Look at them teeth.|Don't look at me like that, I was gonna give you this anyway.|Machete. Honed "
     "the edge, wrapped the grip. She'll go through a car door.",
     "No saw? I'll keep chewing through padlocks, then. Kidding. Mostly.",
     {{IT_MACHETE, 1}}, (1 << MOD_HONED) | (1 << MOD_GRIP), 0, 0},
    {AR_JUNE, 3, IT_FLASHLIGHT, 1, FW_CARRIED,
     "One more thing. The night watch is blind out here. Find me a flashlight.|The Pigs took every one on 5th, so they can "
     "see what they're cutting. Take one back.",
     "A light. Now I see them coming before they see me.|Twelve rounds I've been hiding from myself. Take them. And come "
     "to the range - I'll show you how to breathe before the shot.",
     "No light? I'll learn to see in the dark. I'm halfway there.",
     {{IT_AMMO9, 12}}, 0, STAT_AIM, 60},
    {AR_ROSA, 4, IT_COFFEE, 1, FW_HIDDEN,
     "And... if you see coffee. Real coffee. Beans, ground, I don't care.|I'm not asking. I'm not NOT asking.|A place like "
     "that kept the good stuff out back, behind a door that says STAFF ONLY. Of course it did.",
     "Is that... oh. Oh, that smell.|I'm not crying. It's the steam.|Here. It was Danny's. He carried everything in it. "
     "He'd want it carried.",
     "No coffee. Of course not. ...It's fine. I'm fine.",
     {{IT_HIKINGPACK, 1}, {IT_BANDAGE, 2}}, 0, 0, 0},
    {AR_THEO, 4, IT_DOGFOOD, 1, FW_CARRIED,
     "When I find a nice dog he has to bring his own food, or Rosa says no.|The bad men eat dog food. Gus says so. So "
     "one of them has it. Probably the scariest one.",
     "Dog food! Now I only need a dog!|Rosa said 'we'll see'. 'We'll see' means yes.|Marta showed me how to roll these. "
     "Race you round the flags! Loser eats the dog food.",
     "No dog food? ...Then the dog can share my beans. Not Kevin, though.",
     {{IT_BANDAGE, 1}}, 0, STAT_FIT, 60},
};
const int NUM_FAVOURS = ARRAY_LEN(FAVOURS);
_Static_assert(ARRAY_LEN(FAVOURS) <= MAX_FAVOURS, "Run.favour has a slot per favour");

/* --------------------------------------------------------------- the crew */
/* the line for the Quick Stop is said knowing they can't come: that one's yours alone */
const CrewDef CREW[MAX_CREW] = {
    {AR_HOLLIS, IT_PISTOL, (1 << MOD_SUPPRESSOR) | (1 << MOD_SIGHTS), {IT_AMMO9, 18},
     {"Hollis. I keep the gate.|Twenty-two years a deputy in this county. Now I count who goes up the road and who comes back.|"
      "Quick Stop's a one-man job. Go quiet. Come back.",
      "Freshway. I bought my wife's birthday cake there. Lemon. She hated lemon.|Raiders shoot first and count later. I count first.",
      "Gus put a soda bottle on the end of my pistol. Sounds like somebody shutting a book.|In a place full of brutes, a quiet gun "
      "is a long life.",
      "The Pigs don't take prisoners. Neither do I, these days.|For the boy, I'd walk in there with a spoon.",
      "Raiders and Pigs under one roof. Let them do the work. We pick up after.",
      "The King had a badge once. Mall security. It gave him ideas.|Let's go and take them back."},
     "Keep your head down and your eyes up.",
     "%s. You want a second gun on it?",
     "I'll be at the van. Don't keep me waiting.",
     "Then I've got the gate. Go careful.",
     "Suits me. Somebody has to watch the road.",
     "%s. I keep counting heads at the gate and coming up one short.",
     "Kept the gate."},
    {AR_BEX, IT_MACHETE, (1 << MOD_HONED) | (1 << MOD_GRIP), {IT_NONE, 0},
     {"Bex. I'd come with you, but Rosa says I'm 'too eager'. What does that even mean.|Next time. Next time I'm coming.",
      "Take me. I'm faster than you. I'm faster than everyone.|...Okay, maybe not Theo.",
      "Gus honed my machete. I cut a cabbage in half just by looking at it.|The big ones are slow. I'm not.",
      "Theo taught me a card game. He cheats. He's six.|Get me in there. Please.",
      "I've never seen the interstate. Not since, I mean. I was twelve when it all stopped.",
      "I worked the pretzel stand at Paradise Mall. Security made me take my nose ring out.|I'm going to enjoy this."},
     "Still here. Still ready.",
     "%s? Take me. I'm fast, I'm quiet, I'm bored.",
     "YES. Okay. Okay. Van. I'll be at the van.",
     "...Fine. I'll guard the gate. Again.",
     "Seriously? ...Fine.",
     "%s used to race me to the gate every morning. I always won.|I'd let them win now.",
     "Fastest one there was."},
    {AR_OZZIE, IT_FIREAXE, 1 << MOD_REINFORCED, {IT_NONE, 0},
     {"Ozzie. I used to work the door at the Crown, downtown. Now I work this one.|Go on. I've got the gate.",
      "I used to throw out drunks. Raiders are drunks with knives.|I'm slow, but I'm in the way. That's the job.",
      "The big ones at Build-Rite? 'Brute' is just a word for somebody nobody ever stood up to.",
      "I read Theo a story. He fell asleep in the middle. Best review I ever got.",
      "MegaMart had a security guard called Lou. Biggest man I ever saw. I hope he got out.",
      "Last one, Rosa says. I'll believe it when we're planting."},
     "I'm not going anywhere. Unless you want me to.",
     "%s. You want somebody big between you and them?",
     "I'll get the axe. Meet you at the van.",
     "Fair. The gate's got me.",
     "No hard feelings. I'll hold the door.",
     "We buried %s by the beans. I dug. It's what my hands are for, I guess.",
     "Stood in the way."},
    {AR_CARMEN, IT_SHOTGUN, 1 << MOD_SIGHTS, {IT_SHELLS, 8},
     {"Carmen. I hunted deer before I hunted cans.|Shells are short. I save them for the walk back.",
      "A shotgun in a supermarket aisle. You only have to point it down the right row.",
      "Noise brings company, and my gun makes noise. Your call.|I've shells for a short fight, not a long one.",
      "Pigs. Masks and cleavers. A shotgun doesn't care what you wear.",
      "Out on the interstate you can hear the whole war. Two gangs shooting at each other. Good. Fewer for us.",
      "My father kept seeds in jars in the cellar. Every year, the best of the crop.|We'll start again. Somebody has to."},
     "Shells counted. Barrel clean.",
     "%s. Want a shotgun with you? It's loud. It works.",
     "Van. Two minutes.",
     "Alright. I'll keep the gate.",
     "Your call. The shells stay dry.",
     "%s. ...Don't. I don't want to talk about it. Go and train or something.",
     "Never wasted a shell."},
    {AR_WES, IT_NAILBAT, (1 << MOD_REINFORCED) | (1 << MOD_WEIGHTED), {IT_NONE, 0},
     {"Wes. Thirty years on roofs. Fell off two. Still here.|You're going alone tonight, Rosa says. Come back, and next time take "
      "somebody who knows which end of a bat to hold.",
      "I put the roof on Freshway in '09. It leaks over aisle four. You're welcome.",
      "Build-Rite. Contractor discount, nine percent. I'd like to see them stop me now.",
      "The boy's got a fever. I've got a bat with nails in it. Seems a fair trade.",
      "Fourteen mouths. My knees are fifty-eight. Take me anyway, if you need me.",
      "If I don't come back, my tools go to Gus. Except the good hammer. That goes to Theo."},
     "Still here. Knees and all.",
     "%s? I could use the walk. Want me along?",
     "Let me find my good gloves. See you at the van.",
     "Probably for the best. My knees thank you.",
     "Changed your mind? Happens to me twice a day.",
     "Thirty years on roofs and I never lost a man. Then %s.|Go easy out there.",
     "Fixed what he could."},
};
_Static_assert(AR_WES - AR_HOLLIS + 1 == MAX_CREW, "one archetype per member of the crew, in CREW order");

static const int STAT_XP[STAT_MAX + 1] = {0, 60, 150, 270, 420, 600};

int stat_level(int xp) {
    int l = 0;
    while (l < STAT_MAX && xp >= STAT_XP[l + 1]) l++;
    return l;
}

int stat_xp_at(int level) { return STAT_XP[CLAMP(level, 0, STAT_MAX)]; }

int bag_capacity(ItemId bag) {
    switch (bag) {
    case IT_DUFFEL: return 12;
    case IT_HIKINGPACK: return 16;
    default: return 8;
    }
}

/* ------------------------------------------------------------------ levels */
const LevelDef HUB_DEF = {
    .name = "THE GREENHOUSE", .place = "Route 9", .tagline = "\"Home.\"", .kind = LK_GREENHOUSE, .amb = AMB_DUSK,
    .music = MUS_SAFEHOUSE, .map_w = 60, .map_h = 44, .radio = "", .tip = "", .after = "", .drive = "",
};

const char *const STORE_SAID[NUM_LEVELS] = {"Quick Stop", "Freshway", "Build-Rite", "Medimart", "MegaMart", "Paradise Mall"};
const char *const NUM_WORD[6] = {"none", "one", "two", "three", "four", "five"};

const LevelDef LEVELS[NUM_LEVELS] = {
    {"QUICK STOP", "Route 9 gas station", "\"Open 24 hours. Still open.\"",
     LK_GASSTATION, AMB_DUSK, MUS_LEVEL_A, 50, 40,
     {[AR_SCAV] = 3, [AR_LOOTER] = 1, [AR_RAIDER] = 1, [AR_DOG] = 2, [AR_CAT] = 1, [AR_FOX] = 1, [AR_RAT] = 2,
      [AR_SPITTER] = 2, [AR_NETTLE] = 1, [AR_RAMBLER] = 1},
     {{IT_WATER, 1}},
     {IT_BEANS, IT_SOUP, IT_NOODLES, IT_CRACKERS, IT_SODA, IT_JERKY, IT_CHOCOLATE}, 2,
     {IT_COFFEE, IT_CANDLES, IT_TOILETPAPER, IT_BATTERIES}, 1,
     0.0f, 1,
     "Start small. The Quick Stop on Route 9 still has a stockroom nobody's cracked. "
     "Water first - the well's gone brown. Grab whatever cans you can carry.\n\n"
     "The scavengers there are skittish. But skittish people still bite.",
     "Search shelves with ^yE^0 / ^yA^0. List items tick off on their own. When the list is done, get back to the ^yVAN^0.",
     "The water's clean. The kids drank until they got the hiccups. Rosa says you did good.\n\nRosa never says that.",
     "Route 9. Dead cars on both shoulders,\nand a sun going down like it's tired too.",
     0.2f, 0, 0},
    {"FRESHWAY", "Downtown market", "\"Fresh every day!\"",
     LK_MARKET, AMB_OVERCAST, MUS_LEVEL_B, 62, 48,
     {[AR_SCAV] = 3, [AR_LOOTER] = 2, [AR_RAIDER] = 4, [AR_GUNNER] = 1, [AR_DOG] = 3, [AR_CAT] = 2, [AR_RAT] = 3,
      [AR_SPITTER] = 2, [AR_NETTLE] = 2, [AR_RAMBLER] = 1},
     {{IT_RICE, 1}, {IT_BEANS, 2}},
     {IT_PASTA, IT_TUNA, IT_PEACHES, IT_PBUTTER, IT_CEREAL, IT_SOUP, IT_HONEY}, 2,
     {IT_COFFEE, IT_TOILETPAPER, IT_CHOCOLATE, IT_SOAP}, 2,
     0.2f, 2,
     "Freshway Market, downtown. Raiders have been picking it over for weeks, which means "
     "there's still something worth picking. Rice. Beans. The basics.\n\n"
     "Raiders don't talk. Don't give them the chance.",
     "Grab a ^yshopping cart^0 with ^yE^0 / ^yA^0 to haul more. You can't swing while pushing - but you can ^yram^0.",
     "Rice for a month. Somebody's singing in the kitchen.\n\nYou can't remember the last time you heard that.",
     "Downtown. The traffic lights still hang over the streets.\nNobody waits for green anymore.",
     0.25f, 0, 1},
    {"BUILD-RITE", "Hardware superstore", "\"Build it right. Build it tonight.\"",
     LK_HARDWARE, AMB_RAIN, MUS_LEVEL_A, 66, 50,
     {[AR_SCAV] = 2, [AR_LOOTER] = 2, [AR_RAIDER] = 4, [AR_BRUTE] = 2, [AR_GUNNER] = 2, [AR_DOG] = 2, [AR_CAT] = 1, [AR_FOX] = 1, [AR_RAT] = 3,
      [AR_SPITTER] = 4, [AR_NETTLE] = 3, [AR_RAMBLER] = 2},
     {{IT_GASOLINE, 1}, {IT_BATTERIES, 2}},
     {IT_PROPANE, IT_CANDLES, IT_FLASHLIGHT, IT_NOODLES, IT_CRACKERS, IT_JERKY}, 1,
     {IT_COFFEE, IT_TOILETPAPER, IT_SODA}, 2,
     0.3f, 3,
     "The generator's dead and winter's coming. Build-Rite should still have fuel and batteries.\n\n"
     "Big ones hole up in there. Gas masks. Sledgehammers. Use your head. Use the duct tape.",
     "Open your bag with ^yTAB^0 / ^yBACK^0 to ^ycraft^0. A bat and a box of nails make a much better bat.",
     "The generator coughs, then roars. Lights in the greenhouse.\n\nFor one night, it feels like before.",
     "Rain drums on the roof of the van.\nRosa says it's good for the beans. Someday.",
     0.3f, 0, 2},
    {"MEDIMART", "Strip mall on 5th", "\"We care. Every day.\"",
     LK_PHARMACY, AMB_NIGHT, MUS_LEVEL_B, 76, 47,
     {[AR_SCAV] = 2, [AR_LOOTER] = 2, [AR_RAIDER] = 2, [AR_GUNNER] = 2, [AR_PIG] = 5, [AR_BRUTE] = 1, [AR_FERAL] = 2,
      [AR_DOG] = 3, [AR_CAT] = 2, [AR_FOX] = 1, [AR_RAT] = 3, [AR_SPITTER] = 2, [AR_NETTLE] = 2, [AR_RAMBLER] = 2},
     {{IT_ANTIBIOTICS, 1}, {IT_FORMULA, 1}},
     {IT_VITAMINS, IT_SOAP, IT_HONEY, IT_PBUTTER, IT_PEACHES}, 2,
     {IT_COFFEE, IT_TOILETPAPER, IT_CHOCOLATE}, 2,
     0.35f, 4,
     "It's Theo. The fever won't break.\n\nMedimart, the strip mall on 5th. Antibiotics - now. "
     "And the Pigs have moved in. Masks, cleavers, no mercy.\n\nGo.",
     "Knocked-down enemies die to ^ySPACE^0 / ^yX^0. Aim a gun at a frightened scavenger and they ^ydrop their loot^0.",
     "Theo's fever broke at dawn. He asked for you.\n\nYou pretended there was something in your eye.",
     "A hundred and four, Rosa said. Theo's six.\nYou don't slow down for anything.",
     0.35f, 1, 2},
    {"MEGAMART", "Big-box on the interstate", "\"Everything. Every day. Everywhere.\"",
     LK_MEGAMART, AMB_FOG, MUS_LEVEL_C, 94, 66,
     {[AR_SCAV] = 3, [AR_LOOTER] = 3, [AR_RAIDER] = 6, [AR_GUNNER] = 3, [AR_BRUTE] = 2, [AR_PIG] = 6, [AR_FERAL] = 3,
      [AR_DOG] = 4, [AR_CAT] = 2, [AR_FOX] = 2, [AR_RAT] = 4, [AR_SPITTER] = 3, [AR_NETTLE] = 3, [AR_RAMBLER] = 3},
     {{IT_FORMULA, 1}, {IT_WATER, 2}, {IT_RICE, 1}},
     {IT_PEACHES, IT_TUNA, IT_PBUTTER, IT_PASTA, IT_SOUP, IT_BEANS, IT_BATTERIES, IT_CEREAL}, 3,
     {IT_COFFEE, IT_TOILETPAPER, IT_SOAP, IT_CANDLES}, 2,
     0.35f, 5,
     "Theo's going to be okay. Thank you.\n\nBad news: two new families showed up at the gate. "
     "We need everything. Food for weeks. MegaMart on the interstate.\n\n"
     "It's a war zone - Raiders and Pigs are fighting over it. Let them.",
     "Factions hate each other. Make some ^ynoise^0, then let them sort it out.",
     "Fourteen mouths now. The storeroom's full.\n\nRosa stares at the map, at the mall circled in red, "
     "and doesn't say anything for a long time.",
     "Fog on the interstate, thick as wool.\nSomewhere up ahead, gunfire. Raiders and Pigs, as promised.",
     0.35f, 1, 3},
    {"PARADISE MALL", "The Mall King's palace", "\"Shop till you drop.\"",
     LK_MALL, AMB_INFERNO, MUS_LEVEL_C, 84, 80,
     {[AR_SCAV] = 2, [AR_LOOTER] = 2, [AR_RAIDER] = 3, [AR_GUNNER] = 3, [AR_BRUTE] = 3, [AR_PIG] = 8, [AR_FERAL] = 3, [AR_BOSS] = 1,
      [AR_DOG] = 2, [AR_CAT] = 2, [AR_RAT] = 5, [AR_SPITTER] = 4, [AR_NETTLE] = 3, [AR_RAMBLER] = 2},
     {{IT_SEEDS, 3}},
     {IT_HONEY, IT_COFFEE, IT_PBUTTER, IT_FORMULA}, 1,
     {IT_TOILETPAPER, IT_CHOCOLATE, IT_SOAP}, 2,
     0.0f, 5,
     "Last run before the snow.\n\nThe Mall King sits on the only seed stock left in the county, in the "
     "garden center at the back of Paradise Mall. Seeds mean spring. Spring means we stop doing this.\n\n"
     "Bring them home.",
     "The ^yMall King^0 keeps the seeds on him. You'll have to ^ytake^0 them.",
     "",
     "The sky over Paradise Mall has been burning for a week.\nLast run before the snow.",
     0.4f, 2, 3},
};

const PerkDef PERKS[PK_COUNT] = {
    [PK_THICKSKIN] = {"THICK SKIN", "+2 max health. Can be taken again.", true},
    [PK_PACKMULE] = {"PACK MULE", "+4 bag space. Can be taken again.", true},
    [PK_BRAWLER] = {"BRAWLER", "Fists hit three times as hard and always knock down.", false},
    [PK_BUTCHER] = {"BUTCHER", "Executions are faster and heal 1 health.", false},
    [PK_QUICKHANDS] = {"QUICK HANDS", "Melee attacks are 25% faster.", false},
    [PK_STEADYAIM] = {"STEADY AIM", "Tighter spread. Guns hold 50% more ammo.", false},
    [PK_LIGHTFEET] = {"LIGHT FEET", "Move 12% faster. Enemies hear you less.", false},
    [PK_HOARDER] = {"HOARDER", "Shelves and boxes hold more loot.", false},
    [PK_FIREBUG] = {"FIREBUG", "Fire can't hurt you. Molotovs burn wider.", false},
    [PK_TINKERER] = {"TINKERER", "Crafted and taped weapons last twice as long.", false},
    [PK_EYE] = {"SCAVENGER'S EYE", "See which containers hold loot and what people carry.", false},
    [PK_ADRENALINE] = {"ADRENALINE", "Every kill slows time for a moment.", false},
    [PK_IRONGRIP] = {"IRON GRIP", "Melee weapons wear out half as fast.", false},
    [PK_GUNSLINGER] = {"GUNSLINGER", "Start every level with a loaded pistol in your bag.", false},
};

/* ------------------------------------------------------------------- loot */
static const LootEntry L_GROCERY[] = {
    {IT_BEANS, 10, 1, 2}, {IT_SOUP, 10, 1, 2}, {IT_TUNA, 6, 1, 2}, {IT_PEACHES, 5, 1, 1},
    {IT_RICE, 4, 1, 1}, {IT_PASTA, 7, 1, 2}, {IT_PBUTTER, 4, 1, 1}, {IT_CRACKERS, 6, 1, 1},
    {IT_CEREAL, 6, 1, 1}, {IT_NOODLES, 7, 1, 2}, {IT_HONEY, 2, 1, 1}, {IT_DOGFOOD, 3, 1, 1},
    {IT_COFFEE, 1, 1, 1}, {IT_FORMULA, 1, 1, 1}, {IT_CANDLES, 2, 1, 1}, {IT_BOTTLE, 3, 1, 1},
};
static const LootEntry L_DRINKS[] = {
    {IT_WATER, 8, 1, 1}, {IT_SODA, 10, 1, 2}, {IT_VODKA, 5, 1, 1}, {IT_BOTTLE, 8, 1, 2},
    {IT_COFFEE, 1, 1, 1},
};
static const LootEntry L_FRIDGE[] = {
    {IT_SODA, 8, 1, 2}, {IT_WATER, 4, 1, 1}, {IT_BOTTLE, 6, 1, 1}, {IT_JERKY, 3, 1, 1},
    {IT_VODKA, 2, 1, 1},
};
static const LootEntry L_HARDWARE[] = {
    {IT_DUCTTAPE, 10, 1, 2}, {IT_NAILS, 9, 1, 2}, {IT_BARBEDWIRE, 5, 1, 1}, {IT_SPRAYCAN, 6, 1, 1},
    {IT_LIGHTER, 4, 1, 1}, {IT_BATTERIES, 6, 1, 2}, {IT_GASOLINE, 3, 1, 1}, {IT_HACKSAW, 3, 1, 1},
    {IT_PIPE, 4, 1, 1}, {IT_CROWBAR, 3, 1, 1}, {IT_FIREAXE, 1, 1, 1}, {IT_SLEDGE, 1, 1, 1},
    {IT_PROPANE, 3, 1, 1}, {IT_FLASHLIGHT, 3, 1, 1}, {IT_CANDLES, 2, 1, 1}, {IT_NAILGUN, 1, 1, 1},
    {IT_GUNPOWDER, 1, 1, 1}, {IT_SCRAP, 5, 1, 1}, {IT_SPRINGS, 4, 1, 1}, {IT_WHETSTONE, 3, 1, 1},
};
static const LootEntry L_PHARMACY[] = {
    {IT_BANDAGE, 10, 1, 2}, {IT_PAINKILLERS, 8, 1, 1}, {IT_VITAMINS, 6, 1, 1}, {IT_SOAP, 6, 1, 1},
    {IT_ANTIBIOTICS, 2, 1, 1}, {IT_MEDKIT, 2, 1, 1}, {IT_TOILETPAPER, 2, 1, 1}, {IT_FORMULA, 2, 1, 1},
    {IT_RAG, 3, 1, 1}, {IT_VODKA, 2, 1, 1},
};
static const LootEntry L_STORAGE[] = {
    {IT_BEANS, 5, 1, 3}, {IT_RICE, 4, 1, 1}, {IT_WATER, 4, 1, 1}, {IT_DUCTTAPE, 4, 1, 1},
    {IT_RAG, 5, 1, 2}, {IT_BOTTLE, 4, 1, 2}, {IT_NAILS, 3, 1, 1}, {IT_TOILETPAPER, 2, 1, 1},
    {IT_CANDLES, 2, 1, 1}, {IT_BATTERIES, 2, 1, 1}, {IT_BRICK, 3, 1, 2}, {IT_PASTA, 3, 1, 2},
    {IT_GASOLINE, 1, 1, 1}, {IT_DUFFEL, 1, 1, 1}, {IT_SCRAP, 3, 1, 1},
};
static const LootEntry L_OFFICE[] = {
    {IT_BATTERIES, 6, 1, 1}, {IT_LIGHTER, 5, 1, 1}, {IT_CHOCOLATE, 4, 1, 1}, {IT_COFFEE, 3, 1, 1},
    {IT_AMMO9, 4, 4, 9}, {IT_PISTOL, 1, 1, 1}, {IT_PAINKILLERS, 3, 1, 1}, {IT_FLASHLIGHT, 2, 1, 1},
    {IT_VODKA, 2, 1, 1}, {IT_SPRINGS, 3, 1, 1},
};
static const LootEntry L_STAFF[] = {
    {IT_CHOCOLATE, 5, 1, 1}, {IT_SODA, 5, 1, 1}, {IT_RAG, 4, 1, 1}, {IT_LIGHTER, 4, 1, 1},
    {IT_KNIFE, 3, 1, 1}, {IT_BAT, 2, 1, 1}, {IT_BANDAGE, 3, 1, 1}, {IT_AMMO9, 2, 3, 6},
    {IT_SHELLS, 2, 2, 4}, {IT_DUFFEL, 1, 1, 1}, {IT_HIKINGPACK, 1, 1, 1}, {IT_REVOLVER, 1, 1, 1},
    {IT_WHETSTONE, 1, 1, 1},
};
static const LootEntry L_CHECKOUT[] = {
    {IT_CHOCOLATE, 8, 1, 2}, {IT_BATTERIES, 6, 1, 1}, {IT_LIGHTER, 6, 1, 1}, {IT_SODA, 4, 1, 1},
    {IT_JERKY, 4, 1, 1}, {IT_AMMO9, 2, 3, 6}, {IT_PISTOL, 1, 1, 1},
};
static const LootEntry L_GARDEN[] = {
    {IT_GASOLINE, 4, 1, 1}, {IT_BARBEDWIRE, 4, 1, 1}, {IT_MACHETE, 2, 1, 1},
    {IT_RAG, 3, 1, 1}, {IT_DUCTTAPE, 3, 1, 1}, {IT_CHAINSAW, 1, 1, 1}, {IT_WHETSTONE, 3, 1, 1},
    {IT_SCRAP, 2, 1, 1},
};
static const LootEntry L_CLOTHES[] = {
    {IT_RAG, 10, 1, 2}, {IT_DUFFEL, 2, 1, 1}, {IT_HIKINGPACK, 1, 1, 1}, {IT_SOAP, 2, 1, 1},
    {IT_GOLFCLUB, 2, 1, 1},
};
static const LootEntry L_ELECTRONICS[] = {
    {IT_BATTERIES, 10, 1, 2}, {IT_FLASHLIGHT, 6, 1, 1}, {IT_LIGHTER, 2, 1, 1}, {IT_DUCTTAPE, 2, 1, 1},
    {IT_SPRINGS, 5, 1, 1}, {IT_SCRAP, 2, 1, 1},
};
static const LootEntry L_RESTROOM[] = {
    {IT_TOILETPAPER, 6, 1, 1}, {IT_SOAP, 6, 1, 1}, {IT_RAG, 4, 1, 1}, {IT_BANDAGE, 2, 1, 1},
    {IT_PAINKILLERS, 1, 1, 1},
};
static const LootEntry L_CAMP[] = {
    {IT_BEANS, 4, 1, 2}, {IT_JERKY, 4, 1, 1}, {IT_BANDAGE, 4, 1, 1}, {IT_AMMO9, 4, 4, 8},
    {IT_SHELLS, 3, 2, 4}, {IT_VODKA, 3, 1, 1}, {IT_MOLOTOV, 2, 1, 2}, {IT_PIPEBOMB, 1, 1, 1},
    {IT_BATTERIES, 3, 1, 1}, {IT_DUCTTAPE, 3, 1, 1}, {IT_PISTOL, 1, 1, 1}, {IT_GUNPOWDER, 2, 1, 1},
    {IT_PEACHES, 2, 1, 1}, {IT_MEDKIT, 1, 1, 1}, {IT_SCRAP, 2, 1, 1}, {IT_SPRINGS, 2, 1, 1},
};
static const LootEntry L_CAR[] = {
    {IT_GASOLINE, 3, 1, 1}, {IT_RAG, 4, 1, 1}, {IT_DUCTTAPE, 3, 1, 1}, {IT_CROWBAR, 2, 1, 1},
    {IT_AMMO9, 2, 3, 6}, {IT_SHELLS, 2, 2, 3}, {IT_BOTTLE, 3, 1, 1}, {IT_WATER, 2, 1, 1},
    {IT_FLASHLIGHT, 2, 1, 1}, {IT_GOLFCLUB, 1, 1, 1}, {IT_BAT, 1, 1, 1}, {IT_SCRAP, 3, 1, 1},
    {IT_SPRINGS, 3, 1, 1},
};
static const LootEntry L_SNACKS[] = {
    {IT_CHOCOLATE, 8, 1, 2}, {IT_CRACKERS, 6, 1, 1}, {IT_SODA, 8, 1, 2}, {IT_JERKY, 5, 1, 1},
    {IT_NOODLES, 4, 1, 1},
};
/* the odds and ends lying about the Greenhouse yard: what a workbench eats */
static const LootEntry L_HUB[] = {
    {IT_SCRAP, 9, 1, 2}, {IT_SPRINGS, 6, 1, 1}, {IT_DUCTTAPE, 6, 1, 1}, {IT_RAG, 6, 1, 2},
    {IT_NAILS, 4, 1, 1}, {IT_WHETSTONE, 3, 1, 1}, {IT_BOTTLE, 3, 1, 1}, {IT_GUNPOWDER, 1, 1, 1},
    {IT_BANDAGE, 2, 1, 1}, {IT_SODA, 2, 1, 1},
};
#define LT(z, t) [z] = t
const LootEntry *LOOT[Z_COUNT] = {
    LT(Z_GROCERY, L_GROCERY), LT(Z_DRINKS, L_DRINKS), LT(Z_FRIDGE, L_FRIDGE), LT(Z_HARDWARE, L_HARDWARE),
    LT(Z_PHARMACY, L_PHARMACY), LT(Z_STORAGE, L_STORAGE), LT(Z_OFFICE, L_OFFICE), LT(Z_STAFF, L_STAFF),
    LT(Z_CHECKOUT, L_CHECKOUT), LT(Z_GARDEN, L_GARDEN), LT(Z_CLOTHES, L_CLOTHES),
    LT(Z_ELECTRONICS, L_ELECTRONICS), LT(Z_RESTROOM, L_RESTROOM), LT(Z_CAMP, L_CAMP), LT(Z_CAR, L_CAR),
    LT(Z_SNACKS, L_SNACKS), LT(Z_HUB, L_HUB),
};
#define LN(z, t) [z] = ARRAY_LEN(t)
const int LOOT_N[Z_COUNT] = {
    LN(Z_GROCERY, L_GROCERY), LN(Z_DRINKS, L_DRINKS), LN(Z_FRIDGE, L_FRIDGE), LN(Z_HARDWARE, L_HARDWARE),
    LN(Z_PHARMACY, L_PHARMACY), LN(Z_STORAGE, L_STORAGE), LN(Z_OFFICE, L_OFFICE), LN(Z_STAFF, L_STAFF),
    LN(Z_CHECKOUT, L_CHECKOUT), LN(Z_GARDEN, L_GARDEN), LN(Z_CLOTHES, L_CLOTHES),
    LN(Z_ELECTRONICS, L_ELECTRONICS), LN(Z_RESTROOM, L_RESTROOM), LN(Z_CAMP, L_CAMP), LN(Z_CAR, L_CAR),
    LN(Z_SNACKS, L_SNACKS), LN(Z_HUB, L_HUB),
};

/* ------------------------------------------------------------------ story */
const char *const INTRO_PAGES[] = {
    "Three winters since the grid went dark.",
    "The cities didn't burn. They just stopped.\nNow the weeds are taking them back, one crack at a time.",
    "The shelves of the old world are the last mines left.\nEverybody's digging. Nobody's sharing.",
    "Twelve of us hold out in the old greenhouse on Route 9.\nKids. The sick. People who can't fight.",
    "Every few days, somebody has to go shopping.",
    "Today, that's you.",
};
const int NUM_INTRO_PAGES = ARRAY_LEN(INTRO_PAGES);

const char *const ENDING_PAGES[] = {
    "The Mall King's crown rolls across the food court floor.\nNobody picks it up.",
    "You drive home with a bag full of paper packets.\nTomatoes. Beans. Squash. Things that grow.",
    "Spring comes late that year.\nBut it comes.",
    "The beans come up first.\nTheo gives every single one a name.",
    "You still dream about the aisles sometimes.",
    "But you don't have to go shopping anymore.",
};
const int NUM_ENDING_PAGES = ARRAY_LEN(ENDING_PAGES);

const char *const DEATH_LINES[] = {
    "The van waits until dark.\nNobody comes back to it.",
    "Somewhere, Rosa crosses another name off the list.",
    "The store keeps what it takes.",
    "Clean-up on aisle nine.",
    "The weeds will cover you by spring.",
    "Everything must go.",
};
const int NUM_DEATH_LINES = ARRAY_LEN(DEATH_LINES);

const char *const CREDITS[] = {
    "^pLAST AISLE",
    "",
    "^yA game about going shopping",
    "",
    "^cDESIGN, CODE & PIXELS",
    "Made with C and SDL3",
    "Every pixel placed by hand",
    "",
    "^cMUSIC & SOUND",
    "Synthesised in real time",
    "No samples were harmed",
    "",
    "^cTHE GREENHOUSE",
    "Rosa",
    "Theo",
    "and twelve, then fourteen others",
    "",
    "^cSPECIAL THANKS",
    "Duct tape",
    "Canned beans",
    "Everyone who still shares",
    "",
    "",
    "^wThanks for playing.",
};
const int NUM_CREDITS = ARRAY_LEN(CREDITS);

const char *const LOADING_TIPS[] = {
    "Thrown weapons knock people down. Then finish them with SPACE / X.",
    "Hit someone from behind before they notice you: instant takedown.",
    "Gunshots carry. Everyone in the store will come to see.",
    "Shopping carts hold a lot. Shove one into a crowd at full speed.",
    "Swinging doors hit hard. Kick one into someone's face.",
    "Duct tape repairs your melee weapon. Check the craft menu.",
    "Frightened scavengers drop their loot if you aim a gun at them.",
    "Raiders and Pigs hate each other more than they hate you.",
    "An exclamation mark means they've seen you. Run or fight.",
    "Red barrels and propane tanks explode when shot.",
    "Hold SHIFT / L3 to look further ahead.",
    "Knives and cleavers kill when thrown.",
    "Fire spreads to dry grass. Plan accordingly.",
    "Red eyes, foam at the mouth: that one's rabid. Hit it before it springs.",
    "Most strays just want to be left alone. The rabid ones don't.",
    "A flower swelling up is about to spit. Step aside - the spit is slow.",
    "Nettles lash further than you can swing. Let one miss, then go in.",
    "If a bush has roses on it in winter, give it room. Some of them walk.",
    "Plants burn. A molotov is the best weedkiller there is.",
    "At the Greenhouse: lift with Dee, shoot on June's range, run Theo's flags. One go at each, every evening.",
    "Gus's workbench puts spikes, grips, sights and silencers on your weapons. Bring scrap and springs home.",
    "Your locker at the Greenhouse keeps what your bag can't carry.",
};
const int NUM_LOADING_TIPS = ARRAY_LEN(LOADING_TIPS);
