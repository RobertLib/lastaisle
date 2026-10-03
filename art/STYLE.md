# LAST AISLE — Pixel Art Style Guide

All game graphics are **hand-authored pixel art**: every pixel is placed by hand in
the `art/*.art` text files (one character = one pixel, colours from
`palette.txt`). Nothing is drawn procedurally. Treat each sprite like you would
in Aseprite — deliberate clusters, clean silhouettes, intentional shading.

## The look
* **Hotline Miami meets overgrown ruins.** Strict top-down (bird's-eye) view,
  bold readable silhouettes, saturated accents (neon pink / cyan / yellow,
  blood red) against grimy desaturated urban tones and creeping greens.
* Light comes from the **top-left**: highlights on top/left edges, shade on
  bottom/right. Keep it consistent across all sprites.
* Characters, weapons and items get a **1px dark outline** (`0` or `1`).
  Tiles do not get outlines (walls get a dark edge from the autotile template).
* Shade with 2–4 tones per material. No random noise, no pillow shading, no
  1px "jaggies" in curves, no orphan pixels. Use the translucent chars
  (`,` `:` `;` `~`) only for shadows, glass and glows.
* The world renders at 480x270 and is scaled up ×3–×4, so a 24px character is
  ~90px tall on screen. Detail must read at 1:1 — favour big shapes.

## File format (`tools/build_atlas.py`)
```
# comment
@sprite name W H            -> H rows of W chars
@frames name N W H [cols=K] -> frames side by side, separated by ONE space
@swap new old ab cd         -> palette-swapped copy (a->b, c->d)
@flip new old h|v           -> mirrored copy
@rot  new old 90|180|270    -> rotated copy (clockwise)
```
`.` = transparent. Blank lines inside pixel data are ignored. Names must match
`art/manifest.txt` exactly (frames, width, height). Build + previews:

```
python3 tools/build_atlas.py --previews          # all files
python3 tools/build_atlas.py --previews --only items.art
```
Previews land in `build/preview/<file>.png` (4x, labelled) — open them and
critique your work visually. Iterate until it looks like a shipped indie game.

## Orientation & anchors
Everything that rotates in-game is drawn **facing RIGHT (+x)**: characters,
held weapons, muzzle flashes, slashes. The engine rotates sprites freely.

### Characters (24x24, pivot 12,12 = centre of the body)
Seen from directly above. Facing right means:
* **Head**: roughly 7–8px circle centred around (12–13, 12). We see the top of
  the head: hair / hood / cap / mask top. A hint of face (nose/brow skin) can
  peek out on the right edge of the head since they look right.
* **Shoulders**: an ellipse ~7px deep (x) and ~16px wide (y), spanning y≈4..19,
  behind/around the head. Shoulders are the widest part. Top shoulder (y<12) is
  the character's LEFT side, bottom shoulder (y>12) the RIGHT side.
* **Arms**: come out of each shoulder and point forward (+x).
  * `_idle`: arms relaxed, hands near the shoulder fronts (x≈15–16).
  * `_1h`: right arm (bottom) reaches forward; right hand is a 2x2 skin/glove
    blob whose bottom-right pixel is **(18,16)** — the weapon grip. Left arm
    relaxed.
  * `_2h`: both arms forward like holding a rifle: right hand at **(17,13)**
    (grip, bottom-right pixel of the hand blob), left hand further forward
    around (21,11)–(22,12) to support the barrel.
  * `_punch` frame 0: right fist fully extended to x≈21–22 at y≈14;
    frame 1: left fist extended at y≈9–10.
* **Backpack/gear** sits on the back (left side, x≈5–8).
* Each archetype must be identifiable instantly by silhouette + colour.
* `_down` (32x32): knocked out, lying face-down, limbs bent, NO blood.
* `_dead` (32x32): [0] on the back, arms flung out; [1] face-down sprawled;
  [2] mangled / headless with neck stump and gore (for heavy hits).
  Little blood on the body only — pools are added by the engine.
* `legs_*` (24x24): only legs/feet visible under where the torso goes (the
  torso covers the centre ±6px). Walk cycle in +x: [0] left foot forward
  (foot ≈ x 17–19, y 9–10) right foot back (x 5–7, y 14–15), [1] feet together
  under the body, [2] mirrored step, [3] together. Shoes dark, pants in the
  archetype's colour.

Archetypes:
| name   | look | colours |
|--------|------|---------|
| player | red hoodie with the hood UP, black bandana over the face, olive backpack, fingerless gloves | `r R x` hoodie, `Z T` pack, `0 1` bandana |
| scav   | scruffy hair, green parka, bedroll on back | `G v V`, hair `n N` |
| looter | yellow raincoat with hood, pale face, cheap gloves | `y Y O` |
| raider | black leather jacket, red mohawk, spiked shoulder | `1 2 3`, `r` mohawk, studs `z` |
| brute  | huge shoulders filling the canvas, gas mask, tank top | skin `S k K`, mask `j J z` |
| gunner | tactical vest, backwards cap, sunglasses | olive `Z T`, cap `B d` |
| pig    | pink pig mask with snout + ears, bloody butcher apron | `P Q p`, apron `h H R` |
| feral  | bare skin with war paint, wild long hair, bone necklace | skin `s S k`, paint `c`, hair `N 0` |
| boss   | "The Mall King" (32x32): purple fur coat, golden crown made of cans, huge | `u U q`, crown `y Y X` |

### Weapons (`w_*`, pointing right, pivot = grip)
Drawn horizontally, handle on the left. The pivot point in the manifest is
where the hand grips — the handle must be at that pixel. Real-world objects
at character scale (a bat is ~20px). Floor items reuse these sprites.

### Item icons (`i_*`, 16x16)
Inventory icons that are also drawn on the floor. Keep the object ~10–13px,
centred, with a 1px dark outline and strong silhouette; label colours
(brands) suggested by 1–3px stripes, never text. Weapons shown diagonally
(bottom-left to top-right).

### Tiles (16x16, pivot 0,0)
Must tile seamlessly with themselves and each other variant. Variants differ
subtly (a crack, a stain, a missing tile) — frame 0 is the cleanest and the
most common. Floors are slightly desaturated and darker than characters so
actors pop.

### Wall autotile templates (`wt_*`, 32x48)
```
 x:0..15     x:16..31
+----------+----------+  y 0..15
| solo     | inner    |  solo  = a lone 1x1 pillar of this wall (preview)
| pillar   | corners  |  inner = 4 quadrants (8x8) = inner-corner pieces
+----------+----------+
|                     |  y 16..47: a 2x2-tile block of wall seen from
|   2x2 wall block    |  above, outlined on its outer edge. The engine cuts
|                     |  it into 8x8 quadrants: corners, edges, interior.
+---------------------+
```
* The wall top surface fills the block; the outer 2px edge is a dark rim
  (`0`/`1`) with a 1px highlight inside the top/left rim and shade inside
  the bottom/right rim.
* The **inner corner** tile (x16..31, y0..15): its top-left 8x8 quadrant is
  used when a wall continues up and left but not diagonally up-left, so it
  shows wall surface with just a tiny dark notch in its top-left corner pixel
  area. Same for the other 3 quadrants (notch in their own outer corner).
* Edge quadrants must tile along their edge (interior and edges repeat every
  8px), so keep surface texture 8px-periodic.

### Props
`p_shelf*`: double-sided store gondola seen from above, long axis horizontal;
a dark metal spine down the middle, products packed along both long edges.
Frames: [0] red/white cans, [1] colourful cereal boxes, [2] blue/green bottles,
[3] mixed groceries, [4] hardware (tools, tape, paint), [5] pharmacy (small
white/blue boxes). `_empty`: same shelf looted bare with 1–3 stray items/dust.
`_end` [0] left end cap, [1] right end cap. `p_shelfv*` = `@rot` 90 copies.
`p_fridge`: wall fridge seen from above, back against the TOP of the tile,
glass doors along the bottom edge ([0] stocked, [1] empty, door ajar).
`p_fridge_r`/`p_fridge_l` = rotated copies (back against left / right wall).
`p_counter` checkout [0] left end, [1] belt middle, [2] right end.
Cars/van: top-down, nose pointing UP. Signs: chunky pixel lettering of the
store name (QUICK STOP / FRESHWAY / BUILD-RITE / MEDIMART / MEGAMART /
PARADISE MALL), faded paint or dead neon, cracked.

### FX
Blood is the star: rich `r R x` splats with irregular, organic edges and a few
satellite droplets. Gibs are small, chunky and readable. Explosions are bold
cartoony bursts (yellow-white core -> orange -> red -> dark smoke).

### UI
Sharp, neon-edged, Hotline-Miami flavoured. `ui_paper` is a crumpled notebook
page 9-slice (8px corners). `ui_panel` a dark translucent panel with 1px neon
edge 9-slice. The font must be crisp and highly legible.
