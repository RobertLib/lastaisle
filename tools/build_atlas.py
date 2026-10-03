#!/usr/bin/env python3
"""LAST AISLE asset compiler.

Reads hand-authored pixel art from art/*.art (one character per pixel, colours
from art/palette.txt), packs every frame into assets/atlas.png and emits
src/gen/atlas.h + src/gen/atlas.c with sprite ids, rects and pivots.

Art file directives
-------------------
  # comment                         (only at line start)
  @sprite NAME W H                  followed by H rows of W characters
  @frames NAME N W H [cols=K]       N frames of WxH laid side by side, K per
                                    block-row (default N), frames separated by
                                    one space; blocks of H rows follow each other
  @swap NEW OLD ab cd ...           palette swap copy of OLD (a->b, c->d ...)
  @flip NEW OLD h|v                 mirrored copy
  @rot NEW OLD 90|180|270           rotated copy (clockwise)
Blank lines inside pixel data are ignored.

The manifest (art/manifest.txt) lists every sprite the game code uses:
  NAME FRAMES W H PX PY
Pivots come from the manifest. Missing / mismatching sprites are errors.

Usage: tools/build_atlas.py [--previews] [--tiletest] [--only FILE.art] [--scale N]
  --tiletest renders floors as seamless patches and walls through the autotiler.
  --only parses just that file (no atlas output) - safe while other files are WIP.
"""
import glob
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "art")
ATLAS_W = 1024


# ----------------------------------------------------------------------------
# PNG
# ----------------------------------------------------------------------------
def write_png(path, w, h, rgba_rows):
    """rgba_rows: list of bytearrays (w*4 each)."""
    raw = bytearray()
    for row in rgba_rows:
        raw.append(0)
        raw.extend(row)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


# ----------------------------------------------------------------------------
# palette / manifest
# ----------------------------------------------------------------------------
def load_palette():
    pal = {".": (0, 0, 0, 0)}
    with open(os.path.join(ART, "palette.txt")) as f:
        for ln, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            ch, hexv = line[0], line[2:].strip()
            hexv = hexv.lstrip("#")
            r, g, b = int(hexv[0:2], 16), int(hexv[2:4], 16), int(hexv[4:6], 16)
            a = int(hexv[6:8], 16) if len(hexv) >= 8 else 255
            pal[ch] = (r, g, b, a)
    return pal


def load_manifest():
    man = {}
    order = []
    path = os.path.join(ART, "manifest.txt")
    if not os.path.exists(path):
        return man, order
    with open(path) as f:
        for ln, line in enumerate(f, 1):
            s = line.split("#", 1)[0].strip()
            if not s:
                continue
            p = s.split()
            if len(p) != 6:
                die(f"manifest.txt:{ln}: expected NAME FRAMES W H PX PY")
            name = p[0]
            man[name] = dict(frames=int(p[1]), w=int(p[2]), h=int(p[3]),
                             px=int(p[4]), py=int(p[5]), line=ln)
            order.append(name)
    return man, order


def die(msg):
    print("ERROR:", msg, file=sys.stderr)
    sys.exit(1)


# ----------------------------------------------------------------------------
# art parsing
# ----------------------------------------------------------------------------
class Group:
    def __init__(self, name, w, h, frames, src):
        self.name, self.w, self.h, self.frames, self.src = name, w, h, frames, src


def parse_art(path, pal, groups, errors):
    fname = os.path.basename(path)
    with open(path) as f:
        lines = [l.rstrip("\n").rstrip("\r") for l in f]
    i = 0
    n = len(lines)

    def take_rows(count, width, start_ln):
        nonlocal i
        rows = []
        while len(rows) < count:
            if i >= n:
                errors.append(f"{fname}:{start_ln}: unexpected end of file "
                              f"({len(rows)}/{count} rows)")
                return None
            raw = lines[i]
            i += 1
            if raw.strip() == "":
                continue
            if raw.startswith("@"):
                errors.append(f"{fname}:{i}: directive inside pixel data "
                              f"(only {len(rows)}/{count} rows given)")
                i -= 1
                return None
            if len(raw) != width:
                errors.append(f"{fname}:{i}: row is {len(raw)} chars, "
                              f"expected {width}")
                raw = (raw + "." * width)[:width]
            for c in raw:
                if c not in pal and c != " ":
                    errors.append(f"{fname}:{i}: unknown palette char {c!r}")
                    break
            rows.append(raw)
        return rows

    while i < n:
        line = lines[i]
        i += 1
        if not line.strip() or line.startswith("#"):
            continue
        if not line.startswith("@"):
            errors.append(f"{fname}:{i}: stray line outside a sprite: {line[:40]!r}")
            continue
        p = line.split()
        d = p[0]
        ln = i
        try:
            if d == "@sprite":
                name, w, h = p[1], int(p[2]), int(p[3])
                rows = take_rows(h, w, ln)
                if rows is None:
                    continue
                add_group(groups, Group(name, w, h, [rows], f"{fname}:{ln}"), errors)
            elif d == "@frames":
                name, cnt, w, h = p[1], int(p[2]), int(p[3]), int(p[4])
                cols = cnt
                for extra in p[5:]:
                    if extra.startswith("cols="):
                        cols = int(extra[5:])
                frames = [[] for _ in range(cnt)]
                blocks = (cnt + cols - 1) // cols
                ok = True
                for b in range(blocks):
                    in_block = min(cols, cnt - b * cols)
                    width = in_block * w + (in_block - 1)
                    rows = take_rows(h, width, ln)
                    if rows is None:
                        ok = False
                        break
                    for r in rows:
                        for k in range(in_block):
                            seg = r[k * (w + 1): k * (w + 1) + w]
                            frames[b * cols + k].append(seg)
                        for k in range(in_block - 1):
                            sep = r[k * (w + 1) + w]
                            if sep != " ":
                                errors.append(f"{fname}:{ln}: frame separator "
                                              f"column must be a space")
                                break
                if ok:
                    add_group(groups, Group(name, w, h, frames, f"{fname}:{ln}"), errors)
            elif d == "@swap":
                new, old = p[1], p[2]
                if old not in groups:
                    errors.append(f"{fname}:{ln}: @swap source {old!r} not defined yet")
                    continue
                m = {}
                for pair in p[3:]:
                    if len(pair) != 2:
                        errors.append(f"{fname}:{ln}: bad swap pair {pair!r}")
                        continue
                    if pair[1] not in pal:
                        errors.append(f"{fname}:{ln}: unknown palette char {pair[1]!r}")
                    m[pair[0]] = pair[1]
                g = groups[old]
                fr = [["".join(m.get(c, c) for c in row) for row in f] for f in g.frames]
                add_group(groups, Group(new, g.w, g.h, fr, f"{fname}:{ln}"), errors)
            elif d == "@flip":
                new, old, axis = p[1], p[2], p[3]
                if old not in groups:
                    errors.append(f"{fname}:{ln}: @flip source {old!r} not defined yet")
                    continue
                g = groups[old]
                if axis == "h":
                    fr = [[row[::-1] for row in f] for f in g.frames]
                else:
                    fr = [list(reversed(f)) for f in g.frames]
                add_group(groups, Group(new, g.w, g.h, fr, f"{fname}:{ln}"), errors)
            elif d == "@rot":
                new, old, deg = p[1], p[2], int(p[3])
                if old not in groups:
                    errors.append(f"{fname}:{ln}: @rot source {old!r} not defined yet")
                    continue
                g = groups[old]
                fr = []
                for f in g.frames:
                    cur = f
                    for _ in range((deg // 90) % 4):
                        h_ = len(cur)
                        w_ = len(cur[0])
                        cur = ["".join(cur[h_ - 1 - y][x] for y in range(h_)) for x in range(w_)]
                    fr.append(cur)
                w, h = (g.h, g.w) if (deg // 90) % 2 else (g.w, g.h)
                add_group(groups, Group(new, w, h, fr, f"{fname}:{ln}"), errors)
            else:
                errors.append(f"{fname}:{ln}: unknown directive {d}")
        except (IndexError, ValueError):
            errors.append(f"{fname}:{ln}: malformed directive: {line}")


def add_group(groups, g, errors):
    if g.name in groups:
        errors.append(f"{g.src}: sprite {g.name!r} already defined at {groups[g.name].src}")
        return
    if not g.name.replace("_", "").isalnum() or g.name.lower() != g.name:
        errors.append(f"{g.src}: sprite name {g.name!r} must be lower_snake_case")
    groups[g.name] = g


# ----------------------------------------------------------------------------
# packing
# ----------------------------------------------------------------------------
def pack(items):
    """items: list of (key, w, h). Returns {key:(x,y)}, height."""
    pad = 1
    order = sorted(items, key=lambda t: (-t[2], -t[1], t[0]))
    pos = {}
    x = y = 0
    shelf_h = 0
    for key, w, h in order:
        if w + 2 * pad > ATLAS_W:
            die(f"sprite {key} too wide for atlas")
        if x + w + 2 * pad > ATLAS_W:
            x = 0
            y += shelf_h
            shelf_h = 0
        pos[key] = (x + pad, y + pad)
        x += w + 2 * pad
        shelf_h = max(shelf_h, h + 2 * pad)
    total = y + shelf_h
    hh = 64
    while hh < total:
        hh *= 2
    return pos, hh


# ----------------------------------------------------------------------------
# tiny preview font (3x5) for labels
# ----------------------------------------------------------------------------
MINI = {
    "A": "010101111101101", "B": "110101110101110", "C": "011100100100011",
    "D": "110101101101110", "E": "111100110100111", "F": "111100110100100",
    "G": "011100101101011", "H": "101101111101101", "I": "111010010010111",
    "J": "001001001101010", "K": "101101110101101", "L": "100100100100111",
    "M": "101111111101101", "N": "110101101101101", "O": "010101101101010",
    "P": "110101110100100", "Q": "010101101110011", "R": "110101110101101",
    "S": "011100010001110", "T": "111010010010010", "U": "101101101101111",
    "V": "101101101101010", "W": "101101111111101", "X": "101101010101101",
    "Y": "101101010010010", "Z": "111001010100111", "0": "111101101101111",
    "1": "010110010010111", "2": "110001010100111", "3": "110001010001110",
    "4": "101101111001001", "5": "111100110001110", "6": "011100111101111",
    "7": "111001010010010", "8": "111101111101111", "9": "111101111001110",
    "_": "000000000000111", "-": "000000111000000", ".": "000000000000010",
}


def draw_label(canvas, cw, x0, y0, text, col):
    x = x0
    for ch in text.upper():
        bits = MINI.get(ch)
        if bits:
            for yy in range(5):
                for xx in range(3):
                    if bits[yy * 3 + xx] == "1":
                        px, py = x + xx, y0 + yy
                        if 0 <= px < cw and 0 <= py < len(canvas):
                            canvas[py][px] = col
        x += 4
    return x


def blend_into(canvas, x0, y0, rows, pal):
    H = len(canvas)
    W = len(canvas[0])
    for yy, row in enumerate(rows):
        for xx, c in enumerate(row):
            if c in (".", " "):
                continue
            r, gg, b, a = pal[c]
            py, px = y0 + yy, x0 + xx
            if 0 <= py < H and 0 <= px < W:
                d = canvas[py][px]
                al = a / 255.0
                canvas[py][px] = (int(r * al + d[0] * (1 - al)),
                                  int(gg * al + d[1] * (1 - al)),
                                  int(b * al + d[2] * (1 - al)), 255)


def save_canvas(path, canvas, scale):
    H = len(canvas)
    W = len(canvas[0])
    rows = []
    for yy in range(H):
        big = bytearray()
        for xx in range(W):
            big.extend(bytes(canvas[yy][xx]) * scale)
        for _ in range(scale):
            rows.append(big)
    write_png(path, W * scale, H * scale, rows)


def new_canvas(W, H):
    bg1, bg2 = (40, 36, 52, 255), (52, 48, 64, 255)
    return [[bg1 if ((xx // 4 + yy // 4) % 2) else bg2 for xx in range(W)] for yy in range(H)]


def write_preview(path, glist, pal, scale=4):
    """Lay groups out in rows with labels (frames wrap); then upscale."""
    gap = 6
    maxw = 300
    for g in glist:
        maxw = max(maxw, min(g.w + 2 * gap + 4, 1000))
    cells = []
    x = gap
    y = gap
    rowh = 0
    for g in glist:
        per = max(1, min(len(g.frames), (maxw - 2 * gap) // (g.w + 2)))
        lines = (len(g.frames) + per - 1) // per
        fw = per * (g.w + 2) + 2
        label_w = len(g.name) * 4
        cw = max(fw, label_w) + gap
        ch = lines * (g.h + 2) + 8
        if x + cw > maxw and x > gap:
            x = gap
            y += rowh
            rowh = 0
        cells.append((g, x, y, per))
        x += cw
        rowh = max(rowh, ch + gap)
    W = maxw
    H = y + rowh + gap
    canvas = new_canvas(W, H)
    for g, cx, cy, per in cells:
        draw_label(canvas, W, cx, cy, g.name, (255, 220, 120, 255))
        for k, f in enumerate(g.frames):
            fx = cx + (k % per) * (g.w + 2)
            fy = cy + 7 + (k // per) * (g.h + 2)
            for yy in range(g.h + 2):
                for xx in range(g.w + 2):
                    py, px = fy + yy, fx + xx
                    if py < H and px < W:
                        c = canvas[py][px]
                        canvas[py][px] = (c[0] - 12, c[1] - 12, c[2] - 8, 255)
            blend_into(canvas, fx + 1, fy + 1, f, pal)
    save_canvas(path, canvas, scale)


def compose_wall(tpl, solid, tx, ty):
    """Autotile: build a 16x16 wall tile from a 32x48 template.
    Mirrors the C implementation in src/level.c."""
    out = [["."] * 16 for _ in range(16)]
    for qy in (0, 1):
        for qx in (0, 1):
            dx = -1 if qx == 0 else 1
            dy = -1 if qy == 0 else 1
            h = solid(tx + dx, ty)
            v = solid(tx, ty + dy)
            d = solid(tx + dx, ty + dy)
            if not h and not v:
                sx, sy = (0 if qx == 0 else 3) * 8, 16 + (0 if qy == 0 else 3) * 8
            elif v and not h:
                sx, sy = (0 if qx == 0 else 3) * 8, 16 + (1 + qy) * 8
            elif h and not v:
                sx, sy = (1 + qx) * 8, 16 + (0 if qy == 0 else 3) * 8
            elif not d:
                sx, sy = 16 + qx * 8, qy * 8
            else:
                sx, sy = (1 + qx) * 8, 16 + (1 + qy) * 8
            for yy in range(8):
                for xx in range(8):
                    out[qy * 8 + yy][qx * 8 + xx] = tpl[sy + yy][sx + xx]
    return ["".join(r) for r in out]


WALL_TEST = [
    "..............",
    ".############.",
    ".#..........#.",
    ".#..##..#...#.",
    ".#..##..#...#.",
    ".#......###.#.",
    ".#..........#.",
    ".######.#####.",
    "..............",
]


def write_tiletest(path, groups, pal, scale=3):
    import random
    rnd = random.Random(7)
    floors = [g for g in groups if g.name.startswith("t_") and g.w == 16 and g.h == 16]
    walls = [g for g in groups if g.name.startswith("wt_")]
    pw, ph = 6, 4
    cells = []
    y = 6
    x = 6
    W = 6 + 3 * (pw * 16 + 8)
    for g in floors:
        if x + pw * 16 + 6 > W:
            x = 6
            y += ph * 16 + 14
        cells.append(("floor", g, x, y))
        x += pw * 16 + 8
    if floors:
        y += ph * 16 + 14
    x = 6
    for g in walls:
        if x + 14 * 16 + 6 > max(W, 14 * 16 + 12):
            x = 6
            y += 9 * 16 + 14
        cells.append(("wall", g, x, y))
        x += 14 * 16 + 8
    if walls:
        y += 9 * 16 + 14
    W = max(W, 14 * 16 + 12)
    canvas = new_canvas(W, y + 6)
    floor_for_walls = next((g for g in floors if g.name == "t_lino"), floors[0] if floors else None)
    for kind, g, cx, cy in cells:
        draw_label(canvas, W, cx, cy, g.name, (255, 220, 120, 255))
        if kind == "floor":
            for ty in range(ph):
                for tx in range(pw):
                    k = 0 if rnd.random() < 0.55 else rnd.randrange(len(g.frames))
                    blend_into(canvas, cx + tx * 16, cy + 7 + ty * 16, g.frames[k], pal)
        else:
            tpl = g.frames[0]

            def solid(tx, ty):
                if ty < 0 or ty >= len(WALL_TEST) or tx < 0 or tx >= len(WALL_TEST[0]):
                    return False
                return WALL_TEST[ty][tx] == "#"
            for ty in range(len(WALL_TEST)):
                for tx in range(len(WALL_TEST[0])):
                    if floor_for_walls:
                        blend_into(canvas, cx + tx * 16, cy + 7 + ty * 16, floor_for_walls.frames[0], pal)
                    if solid(tx, ty):
                        blend_into(canvas, cx + tx * 16, cy + 7 + ty * 16,
                                   compose_wall(tpl, solid, tx, ty), pal)
    save_canvas(path, canvas, scale)


# ----------------------------------------------------------------------------
def main():
    previews = "--previews" in sys.argv
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    pal = load_palette()
    man, man_order = load_manifest()
    groups = {}
    errors = []
    files = sorted(glob.glob(os.path.join(ART, "*.art")))
    if only:
        files = [os.path.join(ART, only)]
    scale = 4
    if "--scale" in sys.argv:
        scale = int(sys.argv[sys.argv.index("--scale") + 1])
    per_file = {}
    for path in files:
        before = set(groups)
        parse_art(path, pal, groups, errors)
        per_file[os.path.basename(path)] = [groups[k] for k in groups if k not in before]

    # manifest validation
    missing = []
    for name in man_order:
        m = man[name]
        g = groups.get(name)
        if g is None:
            if not only:
                missing.append(name)
            continue
        if len(g.frames) != m["frames"] or g.w != m["w"] or g.h != m["h"]:
            errors.append(f"{g.src}: {name} is {len(g.frames)}x {g.w}x{g.h} but manifest "
                          f"line {m['line']} wants {m['frames']}x {m['w']}x{m['h']}")
    extra = [k for k in groups if k not in man]

    if previews:
        for fname, glist in per_file.items():
            if only and fname != only:
                continue
            if glist:
                out = os.path.join(ROOT, "build", "preview", fname.replace(".art", ".png"))
                write_preview(out, glist, pal, scale)
                print("preview:", os.path.relpath(out, ROOT))
                if "--tiletest" in sys.argv and any(g.name.startswith(("t_", "wt_")) for g in glist):
                    out2 = out.replace(".png", "_tiletest.png")
                    write_tiletest(out2, glist, pal)
                    print("tiletest:", os.path.relpath(out2, ROOT))

    for e in errors:
        print("ERROR:", e, file=sys.stderr)
    if missing:
        print(f"MISSING ({len(missing)}): " + " ".join(missing), file=sys.stderr)
    if extra:
        print(f"note: {len(extra)} sprites not in manifest (packed anyway): "
              + " ".join(extra[:30]) + (" ..." if len(extra) > 30 else ""))
    if errors:
        sys.exit(1)
    if only:
        print(f"{only}: OK ({sum(len(g.frames) for g in groups.values())} frames)")
        return

    # placeholder for missing sprites so the game still builds while art is WIP
    for name in missing:
        m = man[name]
        fr = []
        for k in range(m["frames"]):
            rows = []
            for y in range(m["h"]):
                rows.append("".join("p" if (x + y) % 2 == 0 else "." for x in range(m["w"])))
            fr.append(rows)
        groups[name] = Group(name, m["w"], m["h"], fr, "placeholder")

    # emit order: manifest order first, then extras
    names = [n for n in man_order] + [k for k in groups if k not in man]
    items = []
    for name in names:
        g = groups[name]
        for k in range(len(g.frames)):
            items.append(((name, k), g.w, g.h))
    pos, atlas_h = pack(items)

    canvas = [bytearray(ATLAS_W * 4) for _ in range(atlas_h)]
    for (name, k), (x, y) in pos.items():
        g = groups[name]
        for yy, row in enumerate(g.frames[k]):
            line = canvas[y + yy]
            for xx, c in enumerate(row):
                if c in (".", " "):
                    continue
                o = (x + xx) * 4
                line[o:o + 4] = bytes(pal[c])
    write_png(os.path.join(ROOT, "assets", "atlas.png"), ATLAS_W, atlas_h, canvas)

    # header + data
    hdr = ["/* GENERATED by tools/build_atlas.py - do not edit */",
           "#ifndef ATLAS_GEN_H", "#define ATLAS_GEN_H", "",
           "typedef struct { short x, y, w, h, px, py, ow; } AtlasSprite;", "",
           "enum {"]
    data = ["/* GENERATED by tools/build_atlas.py - do not edit */",
            '#include "atlas.h"', "",
            "const AtlasSprite g_atlas[SPR_COUNT] = {"]
    names_c = ["const char *const g_atlas_names[SPR_COUNT] = {"]
    defines = []
    idx = 0
    for name in names:
        g = groups[name]
        m = man.get(name)
        px, py = (m["px"], m["py"]) if m else (g.w // 2, g.h // 2)
        cname = "SPR_" + name.upper()
        hdr.append(f"    {cname} = {idx},")
        if len(g.frames) > 1:
            defines.append(f"#define {cname}_N {len(g.frames)}")
        for k in range(len(g.frames)):
            x, y = pos[(name, k)]
            ow = 0
            for row in g.frames[k]:
                r = row.rstrip(". ")
                ow = max(ow, len(r))
            data.append(f"    {{{x},{y},{g.w},{g.h},{px},{py},{ow}}}, /* {name}[{k}] */")
            names_c.append(f'    "{name}{"" if len(g.frames) == 1 else "_" + str(k)}",')
            idx += 1
    hdr.append(f"    SPR_COUNT = {idx}")
    hdr.append("};")
    hdr.append("")
    hdr.extend(defines)
    hdr += ["", f"#define ATLAS_W {ATLAS_W}", f"#define ATLAS_H {atlas_h}", "",
            "extern const AtlasSprite g_atlas[SPR_COUNT];",
            "extern const char *const g_atlas_names[SPR_COUNT];", "", "#endif", ""]
    data.append("};")
    data.append("")
    names_c.append("};")
    os.makedirs(os.path.join(ROOT, "src", "gen"), exist_ok=True)
    with open(os.path.join(ROOT, "src", "gen", "atlas.h"), "w") as f:
        f.write("\n".join(hdr))
    with open(os.path.join(ROOT, "src", "gen", "atlas.c"), "w") as f:
        f.write("\n".join(data + names_c) + "\n")
    print(f"atlas: {idx} frames, {ATLAS_W}x{atlas_h}, {len(missing)} placeholder(s)")


if __name__ == "__main__":
    main()
