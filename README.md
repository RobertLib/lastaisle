# LAST AISLE

*A game about going shopping.*

Three winters after the grid went dark, the cities are rotting and the weeds are taking them back.
The shelves of the old world are the last mines left — and everybody is digging. Twelve people
wait for you at the Greenhouse. Somebody has to go shopping.

**Last Aisle** is a top-down, ultra-violent roguelike in the spirit of Hotline Miami. Each run takes
you through six procedurally generated stores — a gas station, a supermarket, a hardware
superstore, a pharmacy strip mall, a big-box megamart and finally the Mall King's palace. Every
store comes with a **shopping list**. Find the items on shelves, in fridges, crates, lockers and
car trunks — or take them from the scavengers, raiders and masked gangs who got there first —
then make it back to your van alive.

## Features

* **Six handcrafted level types, procedurally generated every run** — storefronts with
  breakable glass, aisles, checkouts, fridges, back rooms, offices, restrooms, loading docks,
  garden centres, food courts — all rotting and overgrown, with collapsed roofs letting the light in.
* **Shopping lists** with must-have items and bonus loot. Your bag is small; shopping carts
  hold much more (search shelves straight into the basket) and make a great battering ram.
  Other scavengers shop too — and they'll take the last can of beans if you're slow.
* **Noise matters**: gunfire carries across the dead city. Shoot too much and a squad comes in
  from outside to see what the fuss is about. Melee is quiet; silent takedowns are instant.
* **27 weapons**: brooms, bats, crowbars, frying pans, machetes, fire axes, sledgehammers,
  chainsaws, pistols, shotguns, rifles, nail guns, molotovs, pipe bombs, bricks...
* **Crafting**: nail bats, barbed bats, spears, molotovs, aerosol flamethrowers, pipe bombs,
  zip guns, sawn-offs, bandages, first aid kits — and duct tape to keep it all together.
* **Factions with their own politics**: timid scavengers who flee (or drop their loot at
  gunpoint), looters who fight back, raiders, brutes in gas masks, gunmen who lead their shots,
  the pig-masked Butchers and feral crazies. They hate each other too — make some noise and
  let them sort it out.
* **Brutal, stylish combat**: knockdowns, executions, silent takedowns, swinging-door slams,
  thrown weapons, dismemberment, persistent blood that never cleans itself up, fire spreading
  through dry grass, chain-reacting barrels, hit-stop, slow motion and screen shake.
* **Roguelike progression**: choose one of three perks at the Greenhouse after every store.
  Two modes — *Story* (retry a level when you die) and *Roguelike* (permadeath).
* **A boss**: the Mall King, sitting on the county's last seed stock.
* **Everything is hand-made pixel art** — every sprite, tile, font glyph and UI element is
  authored pixel by pixel in `art/*.art`. Music and sound are synthesized in real time.

## Controls

| Action | Keyboard & mouse | Gamepad |
|---|---|---|
| Move | WASD / arrows | Left stick |
| Aim | Mouse | Right stick |
| Attack / shoot | Left mouse | RT |
| Throw weapon | Right mouse | LT |
| Search / take / push cart / leave | E | A |
| Execute downed enemy | Space | X |
| Look further | Shift | L3 |
| Bag & crafting | Tab / I | Back |
| Heal | F | LB |
| Swap weapon with bag | Q | Y |
| Reload | R | RB |
| Pause | Esc / P | Start |
| Fullscreen | F11 / Alt+Enter | |

## Building

Requirements: a C11 compiler, **SDL 3.4+** and Python 3 (only for the asset compiler).

```sh
# macOS
brew install sdl3
make            # compiles the art atlas, then the game
./lastaisle
```

To build a double-clickable macOS app (SDL3 bundled inside, icon made from the pixel-art portrait):

```sh
make app        # -> LastAisle.app
```

Or with CMake (any platform with SDL3 installed):

```sh
cmake -B build-cmake && cmake --build build-cmake
./build-cmake/lastaisle
```

## Project layout

```
art/            hand-authored pixel art (one character = one pixel), palette, manifest, style guide
tools/          build_atlas.py — compiles art/*.art into assets/atlas.png + src/gen/atlas.{h,c}
assets/         generated sprite atlas
src/            the game (C11 + SDL3)
  main.c        window, main loop, settings & save files
  gfx.c         sprites, pixel fonts, render targets, lighting, post effects
  audio*.c      real-time synthesizer, sound effects, music sequencer and songs
  gen.c         procedural store generator
  level.c       tile map, collision, line of sight, A* pathfinding, wall autotiling
  world.c       simulation: pickups, doors, carts, particles, decals, scoring, rendering
  actor.c       player control, inventory, crafting, character rendering
  ai.c          NPC perception, factions, looting, combat behaviour, the boss
  combat.c      melee, guns, thrown weapons, damage, gore, explosions, fire, executions
  hud.c         HUD, bag & crafting screen, pause menu
  screens.c     title, story, briefing, results, safehouse, game over, ending, options
  data.c        items, weapons, recipes, enemies, levels, perks, story text
```

Art previews for artists: `make previews` renders labelled sheets into `build/preview/`.

Debug flags: `--level N` (jump to a level), `--seed S`, `--god`, `--autoplay` (QA bot plays the
game), `--mapshot out.png` (render a whole generated level), `--frames N --shot out.png`.

Saves and settings live in the platform's user data folder (`SDL_GetPrefPath`). Runs are saved at
the start of each level; *Continue* resumes from there.
