#!/usr/bin/env python3
"""Builds a macOS .iconset from the hand-drawn ui_portrait sprite (nearest-neighbour scaled).

Usage: tools/make_icon.py OUT_DIR.iconset
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_atlas as ba  # noqa: E402


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    pal = ba.load_palette()
    groups = {}
    errors = []
    ba.parse_art(os.path.join(ba.ART, "ui.art"), pal, groups, errors)
    g = groups["ui_portrait"]
    rows = g.frames[0]
    src = len(rows)
    for size in (16, 32, 64, 128, 256, 512, 1024):
        px_rows = []
        for y in range(size):
            sy = y * src // size
            row = bytearray()
            for x in range(size):
                sx = x * src // size
                c = rows[sy][sx]
                row.extend(bytes(pal[c]) if c != "." else b"\x0b\x0a\x10\xff")
            px_rows.append(row)
        valid = {16, 32, 128, 256, 512}
        names = []
        if size in valid:
            names.append(f"icon_{size}x{size}.png")
        if size // 2 in valid:
            names.append(f"icon_{size // 2}x{size // 2}@2x.png")
        for name in names:
            ba.write_png(os.path.join(out, name), size, size, px_rows)
    print("iconset:", out)


if __name__ == "__main__":
    main()
