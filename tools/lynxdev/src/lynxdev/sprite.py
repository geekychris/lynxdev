"""Sprite converter: PNG or text art -> Suzy sprite data as C source.

    ./lynx sprite art/ship.txt -o src/ship.c            # writes src/ship.c + src/ship.h
    ./lynx sprite art/sheet.png -o src/en.c --frames 8x8 --name enemy
    lynxdev sprite in.png -o out.c [--bpp 4] [--palette default|image|0x223,0x011,...]

Text art (.txt): one row per line, one character per pixel: '.' or ' ' =
pen 0 (transparent), 0-9 A-F = pens 0-15. Lines starting with '#' are
comments; a blank line separates frames.

PNG: fully transparent pixels (alpha < 128) -> pen 0; every other pixel ->
the nearest colour of the palette (default: the TGI/SDK default palette,
pens 1-15; pen 0 is transparent in normal sprites). --palette image builds a
palette from the image's own colours (at most 15) and writes it to the
header as <name>_palette[16] (12-bit 0xGBR values for gfx_palette_gbr() /
the GREEN/BLUERED registers).

Output data: literal Suzy lines (length byte, packed pixels MSB first, one
0 pad byte) and a final 0 byte; header with <NAME>_W, <NAME>_H, <NAME>_BPP,
<name>_data (or <name>_data_0..n and <name>_frames[] with --frames).
"""
from __future__ import annotations

import re
from pathlib import Path

# The default palette (cc65 TGI driver, sdk gfx_init): 12-bit 0xGBR per pen
DEFAULT_GBR = [0x223, 0x011, 0x34D, 0x9AF, 0x9B8, 0x777, 0x335, 0x448,
               0x75E, 0xD5F, 0xC53, 0x822, 0x484, 0x8E5, 0xCF5, 0xFFF]


def gbr_to_rgb(v: int) -> tuple[int, int, int]:
    g, b, r = (v >> 8) & 15, (v >> 4) & 15, v & 15
    return r * 17, g * 17, b * 17


def rgb_to_gbr(r: int, g: int, b: int) -> int:
    return (round(g / 17) << 8) | (round(b / 17) << 4) | round(r / 17)


class SpriteError(ValueError):
    pass


def _parse_text(text: str) -> list[list[list[int]]]:
    frames, cur = [], []
    for raw in text.splitlines():
        line = raw.rstrip("\n")
        if line.startswith("#"):
            continue
        if not line.strip():
            if cur:
                frames.append(cur)
                cur = []
            continue
        row = []
        for ch in line.rstrip():
            if ch in ". ":
                row.append(0)
            elif ch.upper() in "0123456789ABCDEF":
                row.append(int(ch, 16))
            else:
                raise SpriteError(f"bad pixel {ch!r} in {line!r} (use . or 0-9 A-F)")
        cur.append(row)
    if cur:
        frames.append(cur)
    out = []
    for f in frames:
        w = max(len(r) for r in f)
        out.append([r + [0] * (w - len(r)) for r in f])
    return out


def _parse_png(path: Path, palette: str, frames: str | None):
    from PIL import Image
    img = Image.open(path).convert("RGBA")
    px = img.load()
    w, h = img.size
    pal_out = None
    if palette == "image":
        cols = []
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                if a >= 128:
                    v = rgb_to_gbr(r, g, b)
                    if v not in cols:
                        cols.append(v)
        if len(cols) > 15:
            raise SpriteError(f"{path}: {len(cols)} colours (at most 15 with --palette image)")
        pal_out = [0] + cols + [0] * (15 - len(cols))
        pens = {v: i + 1 for i, v in enumerate(cols)}

        def pen_of(r, g, b):
            return pens[rgb_to_gbr(r, g, b)]
    else:
        gbr = DEFAULT_GBR if palette == "default" else [int(x, 0) for x in palette.split(",")]
        if len(gbr) != 16:
            raise SpriteError("--palette wants 16 comma-separated 12-bit 0xGBR values")
        rgbs = [gbr_to_rgb(v) for v in gbr]

        def pen_of(r, g, b):
            best = min(range(1, 16), key=lambda i: (rgbs[i][0] - r) ** 2 + (rgbs[i][1] - g) ** 2 + (rgbs[i][2] - b) ** 2)
            return best
    grid = [[0 if px[x, y][3] < 128 else pen_of(*px[x, y][:3]) for x in range(w)] for y in range(h)]
    if frames:
        m = re.fullmatch(r"(\d+)x(\d+)", frames)
        if not m:
            raise SpriteError("--frames WxH, e.g. 8x8")
        fw, fh = int(m.group(1)), int(m.group(2))
        out = []
        for fy in range(0, h - fh + 1, fh):
            for fx in range(0, w - fw + 1, fw):
                out.append([row[fx:fx + fw] for row in grid[fy:fy + fh]])
        return out, pal_out
    return [grid], pal_out


def encode(grid: list[list[int]], bpp: int) -> bytes:
    """Literal Suzy sprite data for a pen grid."""
    out = bytearray()
    for row in grid:
        bits, n, data = 0, 0, bytearray()
        for p in row:
            if p >= 1 << bpp:
                raise SpriteError(f"pen {p} needs more than {bpp} bpp")
            bits = (bits << bpp) | p
            n += bpp
            while n >= 8:
                n -= 8
                data.append((bits >> n) & 0xFF)
        if n:
            data.append((bits << (8 - n)) & 0xFF)
        data.append(0)                      # pad: guards against Suzy's end-of-line quirks (transparent pixels)
        if len(data) + 1 > 255:
            raise SpriteError("a line is too long (more than 253 bytes)")
        out.append(len(data) + 1)
        out += data
    out.append(0)                           # end of sprite
    return bytes(out)


def convert(src, out_c, name: str | None = None, bpp: int | None = None, palette: str = "default",
            frames: str | None = None) -> dict:
    src, out_c = Path(src), Path(out_c)
    name = name or re.sub(r"\W", "_", out_c.stem)
    if src.suffix.lower() == ".png":
        grids, pal = _parse_png(src, palette, frames)
    else:
        grids, pal = _parse_text(src.read_text()), None
        if frames:
            raise SpriteError("--frames is for PNG sheets; separate text frames with a blank line")
    if not grids:
        raise SpriteError(f"{src}: no pixels")
    maxpen = max(max(max(r) for r in g) for g in grids)
    need = 1 if maxpen < 2 else 2 if maxpen < 4 else 3 if maxpen < 8 else 4
    bpp = bpp or need
    if bpp < need:
        raise SpriteError(f"pen {maxpen} needs {need} bpp")
    W, H = len(grids[0][0]), len(grids[0])
    up = name.upper()
    datas = [encode(g, bpp) for g in grids]
    c = [f"/* {out_c.name}: made by `./lynx sprite {src.name}` - don't edit, re-run it. */",
         f'#include "{out_c.stem}.h"', ""]

    def arr(n, d):
        rows = [", ".join(f"0x{b:02X}" for b in d[i:i + 12]) for i in range(0, len(d), 12)]
        return [f"const unsigned char {n}[{len(d)}] = {{"] + ["    " + r + "," for r in rows] + ["};", ""]

    h = [f"/* {out_c.stem}.h: made by `./lynx sprite {src.name}` - don't edit, re-run it. */",
         f"#ifndef {up}_SPRITE_H", f"#define {up}_SPRITE_H", "",
         f"#define {up}_W {W}", f"#define {up}_H {H}", f"#define {up}_BPP {bpp}"]
    if len(datas) == 1:
        c += arr(f"{name}_data", datas[0])
        h.append(f"extern const unsigned char {name}_data[{len(datas[0])}];")
    else:
        for i, d in enumerate(datas):
            c += arr(f"{name}_data_{i}", d)
            h.append(f"extern const unsigned char {name}_data_{i}[{len(d)}];")
        h.append(f"#define {up}_FRAMES {len(datas)}")
        h.append(f"extern const unsigned char * const {name}_frames[{len(datas)}];")
        c.append(f"const unsigned char * const {name}_frames[{len(datas)}] = {{ " +
                 ", ".join(f"{name}_data_{i}" for i in range(len(datas))) + " };")
        c.append("")
    if pal:
        h.append(f"extern const unsigned int {name}_palette[16];   /* 12-bit 0xGBR */")
        c.append(f"const unsigned int {name}_palette[16] = {{ " + ", ".join(f"0x{v:03X}" for v in pal) + " };")
        c.append("")
    h += ["", "#endif", ""]
    out_c.parent.mkdir(parents=True, exist_ok=True)
    out_c.write_text("\n".join(c))
    out_c.with_suffix(".h").write_text("\n".join(h))
    return {"c": str(out_c), "h": str(out_c.with_suffix(".h")), "w": W, "h_px": H, "bpp": bpp,
            "frames": len(datas), "bytes": sum(len(d) for d in datas)}
