#!/usr/bin/env python3
"""Rock Blaster (Lynx): the original's vector shapes -> Suzy sprites (src/art.c).

The Amiga original draws its ship, flame and rocks as polygon outlines
(amiga_mcp/examples/rock_blaster/draw.c). The Lynx shows the original's
320-pixel-wide world at half size (160 pixels), so every outline here is the
original polygon scaled by 1/2 and drawn with 1-pixel lines, 1 bit per pixel
(the game colours them with the sprite's pen palette).

- ship / flame: 17 frames for the angles 0..64 (0..90 degrees, a frame every
  4 angle units); the other three quarters are the same frames mirrored by
  Suzy (hflip / vflip). 13x13, centre pixel (6, 6).
- rocks: the original's 4 shapes per size, not rotated (as the original):
  large 21x21, medium 13x13, small 7x7, centred.
- title: "ROCK" and "BLASTER" in the original's 5x7 title font, drawn by
  Suzy at 2x and more.

Run: python3 tools/mkart.py [--show]
"""
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE.parents[1] / "tools" / "lynxdev" / "src"))
from lynxdev.sprite import encode  # noqa: E402

SHIP = [(10, 0), (-7, -6), (-4, -2), (-4, 2), (-7, 6)]
FLAME = [(-5, -3), (-12, 0), (-5, 3)]
ROCK_LARGE = [
    [(-8, -18), (8, -20), (20, -8), (18, 4), (12, 18), (-4, 20), (-18, 10), (-20, -6)],
    [(-12, -16), (4, -20), (18, -12), (20, 2), (10, 18), (-8, 16), (-20, 4), (-16, -10)],
    [(-6, -20), (10, -16), (20, -4), (16, 10), (4, 20), (-10, 16), (-20, 2), (-14, -12)],
    [(-14, -14), (2, -20), (16, -14), (20, 0), (14, 16), (-2, 20), (-18, 8), (-18, -8)],
]
ROCK_MEDIUM = [
    [(-4, -10), (4, -12), (12, -4), (10, 4), (6, 10), (-2, 12), (-10, 6), (-12, -2)],
    [(-6, -8), (2, -12), (10, -6), (12, 2), (6, 10), (-4, 10), (-12, 2), (-8, -6)],
    [(-2, -12), (6, -10), (12, -2), (8, 6), (2, 12), (-6, 8), (-12, 0), (-8, -8)],
    [(-8, -8), (2, -12), (10, -8), (12, 0), (8, 8), (0, 12), (-10, 4), (-10, -4)],
]
ROCK_SMALL = [
    [(-2, -5), (2, -6), (6, -2), (5, 2), (3, 5), (-1, 6), (-5, 3), (-6, -1)],
    [(-3, -4), (1, -6), (5, -3), (6, 1), (3, 5), (-2, 5), (-6, 1), (-4, -3)],
    [(-1, -6), (3, -5), (6, -1), (4, 3), (1, 6), (-3, 4), (-6, 0), (-4, -4)],
    [(-4, -4), (1, -6), (5, -4), (6, 0), (4, 4), (0, 6), (-5, 2), (-5, -2)],
]
FONT = {  # the original's 5x7 title font: 5 columns, bit 0 = top row
    "A": [0x7E, 0x11, 0x11, 0x11, 0x7E], "B": [0x7F, 0x49, 0x49, 0x49, 0x36],
    "C": [0x3E, 0x41, 0x41, 0x41, 0x22], "E": [0x7F, 0x49, 0x49, 0x49, 0x41],
    "K": [0x7F, 0x08, 0x14, 0x22, 0x41], "L": [0x7F, 0x40, 0x40, 0x40, 0x40],
    "O": [0x3E, 0x41, 0x41, 0x41, 0x3E], "R": [0x7F, 0x09, 0x19, 0x29, 0x46],
    "S": [0x46, 0x49, 0x49, 0x49, 0x31], "T": [0x01, 0x01, 0x7F, 0x01, 0x01],
}
DIGITS = [  # 5x7 digits in the same style, for the HUD (port: Suzy sprites, not TGI text)
    [0x3E, 0x51, 0x49, 0x45, 0x3E], [0x00, 0x42, 0x7F, 0x40, 0x00], [0x42, 0x61, 0x51, 0x49, 0x46],
    [0x21, 0x41, 0x45, 0x4B, 0x31], [0x18, 0x14, 0x12, 0x7F, 0x10], [0x27, 0x45, 0x45, 0x45, 0x39],
    [0x3C, 0x4A, 0x49, 0x49, 0x30], [0x01, 0x71, 0x09, 0x05, 0x03], [0x36, 0x49, 0x49, 0x49, 0x36],
    [0x06, 0x49, 0x49, 0x29, 0x1E], FONT["L"],
]


def line(g, x0, y0, x1, y1):
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        g[y0][x0] = 1
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def outline(pts, size, c, angle=0.0, closed=True):
    """Polygon (original pixels, rotated by angle in 0..256 units) at half
    size into a size x size grid centred on (c, c)."""
    g = [[0] * size for _ in range(size)]
    a = angle * 2 * math.pi / 256
    ca, sa = math.cos(a), math.sin(a)
    q = []
    for x, y in pts:
        rx, ry = x * ca - y * sa, x * sa + y * ca
        q.append((c + math.floor(rx / 2 + 0.5), c + math.floor(ry / 2 + 0.5)))
    n = len(q) if closed else len(q) - 1
    for i in range(n):
        line(g, *q[i], *q[(i + 1) % len(q)])
    return g


def inside(pts, x, y):
    n, r = len(pts), False
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            r = not r
    return r


def filled(pts, size, c, angle=0.0, scale=0.5, ss=4, keep=0.4):
    """Polygon filled (supersampled ss x ss per pixel, a pixel is set when
    more than `keep` of it is inside), rotated, scaled, centred on (c, c)."""
    a = angle * 2 * math.pi / 256
    ca, sa = math.cos(a), math.sin(a)
    q = [((x * ca - y * sa) * scale, (x * sa + y * ca) * scale) for x, y in pts]
    g = [[0] * size for _ in range(size)]
    for py in range(size):
        for px in range(size):
            n = sum(inside(q, px - c - 0.5 + (i + 0.5) / ss, py - c - 0.5 + (j + 0.5) / ss)
                    for i in range(ss) for j in range(ss))
            g[py][px] = 1 if n > keep * ss * ss else 0
    return g


def text_grid(s):
    w = 6 * len(s) - 1
    g = [[0] * w for _ in range(7)]
    for i, ch in enumerate(s):
        for col, bits in enumerate(FONT[ch]):
            for row in range(7):
                if bits >> row & 1:
                    g[row][6 * i + col] = 1
    return g


def glyph(cols):
    return [[cols[c] >> r & 1 for c in range(5)] for r in range(7)]


def show(name, g):
    print(name)
    for r in g:
        print("  " + "".join("#" if p else "." for p in r))


SHIP_SCALE = 0.6   # port: a little over half size, so the notch shows on the LCD
SHIP_FRAMES = [filled(SHIP, 13, 6, a * 4, scale=SHIP_SCALE) for a in range(17)]
FLAME_FRAMES = [filled(FLAME, 13, 6, a * 4, scale=SHIP_SCALE) for a in range(17)]
ROCKS = ([outline(p, 21, 10) for p in ROCK_LARGE] + [outline(p, 13, 6) for p in ROCK_MEDIUM]
         + [outline(p, 7, 3) for p in ROCK_SMALL])
TITLE = {"title_rock": text_grid("ROCK"), "title_blaster": text_grid("BLASTER")}
DOT = [[1]]

if "--show" in sys.argv:
    for i in (0, 4, 8, 16):
        show(f"ship {i * 4}", SHIP_FRAMES[i])
        show(f"flame {i * 4}", FLAME_FRAMES[i])
    for i, g in enumerate(ROCKS):
        show(f"rock {i}", g)
    for k, g in TITLE.items():
        show(k, g)
    sys.exit()

c = ["/* generated by tools/mkart.py - do not edit, re-run it */", '#include "art.h"', ""]
h = ["/* generated by tools/mkart.py - do not edit, re-run it */", "#ifndef ART_H", "#define ART_H", "",
     "/* 1 bpp Suzy sprite data (literal lines); colour them with spr_color */"]
total = 0


def arr(name, data):
    global total
    total += len(data)
    rows = [", ".join(f"0x{b:02X}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    c.extend([f"static const unsigned char {name}[{len(data)}] = {{"] + ["    " + r + "," for r in rows] + ["};"])


def table(name, grids, comment):
    for i, g in enumerate(grids):
        arr(f"{name}_{i}", encode(g, 1))
    c.append(f"const unsigned char * const {name}[{len(grids)}] = {{ " +
             ", ".join(f"{name}_{i}" for i in range(len(grids))) + " };")
    c.append("")
    h.append(f"extern const unsigned char * const {name}[{len(grids)}];   /* {comment} */")


table("ship_frames", SHIP_FRAMES, "13x13, centre (6,6), angle 4*i")
table("flame_frames", FLAME_FRAMES, "13x13, centre (6,6), angle 4*i")
table("digit_frames", [glyph(d) for d in DIGITS], "5x7: 0-9, 10 = L")
table("rock_frames", ROCKS, "large 0-3 21x21 c10, medium 4-7 13x13 c6, small 8-11 7x7 c3")
for k, g in list(TITLE.items()) + [("dot", DOT)]:
    d = encode(g, 1)
    total += len(d)
    rows = [", ".join(f"0x{b:02X}" for b in d[i:i + 16]) for i in range(0, len(d), 16)]
    c.extend([f"const unsigned char {k}[{len(d)}] = {{"] + ["    " + r + "," for r in rows] + ["};", ""])
    h.append(f"extern const unsigned char {k}[{len(d)}];   /* {len(g[0])}x{len(g)} */")
    h.append(f"#define {k.upper()}_W {len(g[0])}")
h += ["", "#endif", ""]
(HERE / "src" / "art.c").write_text("\n".join(c) + "\n")
(HERE / "src" / "art.h").write_text("\n".join(h))
print(f"src/art.c: {total} bytes of sprite data")
