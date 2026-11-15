#!/usr/bin/env python3
"""Writes the generated art (logs, the background rows) to art/*.txt and
converts every art/*.txt to src/spr_<name>.c/.h with `./lynx sprite`.

    projects/frank_the_frog/tools/mkart.py      (re-run after editing art/)
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
ART = HERE / "art"


def write(name, rows, note):
    (ART / f"{name}.txt").write_text(f"# {note} (made by tools/mkart.py)\n" + "\n".join(rows) + "\n")


def log(w):
    """A log of w pixels: 6 bark, 8 highlight, 5 dark end caps and knots."""
    rows = []
    for y in range(7):
        r = []
        for x in range(w):
            if y in (0, 6):
                c = "." if x < 2 or x >= w - 2 else ("5" if y == 6 else ".")
            elif x == 0 or x == w - 1:
                c = "5" if 1 < y < 5 else "."
            elif x in (1, w - 2):
                c = "5"
            elif y == 1:
                c = "8"
            elif (x == w // 3 and y in (3, 4)) or (x == 2 * w // 3 and y in (2, 3, 4)):
                c = "5"
            elif y == 5:
                c = "5" if x % 5 == 2 else "6"
            else:
                c = "6"
            r.append(c)
        rows.append("".join(r))
    return rows


# pond centres in the original's coordinates (lanes.c home_cx), halved
PONDS = [40 // 2, 120 // 2, 216 // 2, 280 // 2]


def home_row():
    """The far bank: grass (1) with reeds (5), ponds of water (3) in a muddy
    rim (6). No pen 0: the row is opaque."""
    g = [["1"] * 160 for _ in range(7)]
    for x in range(3, 160, 6):
        h = 2 + (x * 7) % 3
        for y in range(6 - h, 7):
            g[y][x + (1 if y < 3 and x % 4 == 1 else 0)] = "5"
    for cx in PONDS:
        for y in range(7):
            half = 7 if 0 < y < 6 else 5
            for x in range(cx - half, cx + half + 1):
                g[y][x] = "6"
        for y in range(1, 6):
            half = 5 if y in (1, 5) else 6
            for x in range(cx - half, cx + half + 1):
                g[y][x] = "3"
    return ["".join(r) for r in g]


def grass_row():
    """1 bpp (background sprite: pen 0 grass, pen 1 dark tufts)."""
    rows = []
    for y in range(7):
        rows.append("".join("1" if (y == 2 and x % 12 == 3) or (y == 3 and x % 12 in (2, 4))
                            or (y == 5 and x % 12 == 9) or (y == 6 and x % 12 in (8, 10))
                            else "0" for x in range(160)))
    return rows


def water_row():
    """1 bpp: pen 0 water, pen 1 light ripples."""
    rows = []
    for y in range(7):
        rows.append("".join("1" if (y == 1 and x % 20 in (3, 4, 5)) or (y == 4 and x % 20 in (13, 14, 15))
                            else "0" for x in range(160)))
    return rows


def road_row(dashes):
    """1 bpp: pen 0 asphalt, pen 1 the dashed lane line on the bottom line."""
    rows = ["0" * 160 for _ in range(6)]
    rows.append("".join("1" if dashes and x % 12 < 5 else "0" for x in range(160)))
    return rows


def main():
    write("log24", log(24), "a log, 24 wide (the original's 48)")
    write("log32", log(32), "a log, 32 wide (the original's 64)")
    write("home", home_row(), "the far bank with four ponds, 160x7")
    write("grass", grass_row(), "median / start grass, 1 bpp")
    write("water", water_row(), "river, 1 bpp")
    write("road", road_row(True), "road lane with a dashed line below, 1 bpp")
    lynx = ROOT / "lynx"
    for f in sorted(ART.glob("*.txt")):
        out = HERE / "src" / f"spr_{f.stem}.c"
        r = subprocess.run([str(lynx), "sprite", str(f), "-o", str(out), "--name", f.stem],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit(f"{f.name}: {r.stdout}{r.stderr}")
    print("art converted")


if __name__ == "__main__":
    main()
