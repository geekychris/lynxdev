# The lynxdev SDK

`sdk/` is linked into every project (`sdk/project.mk`). `#include "lynxgame.h"`
pulls in everything below; each header can also be included alone. A
program links only the modules it uses (they're in a library).

| Header | What |
|---|---|
| `lynxhw.h` | SDK version, screen size, register addresses (`LYNX_JOYSTICK`, `LYNX_GREEN(i)`, `LYNX_AUD(ch, reg)`...), includes cc65's `<lynx.h>` (MIKEY/SUZY structs, `SCB_*` types, `COLOR_*`) |
| `lynxlog.h` | the harness log: `lynx_log`, `lynx_done`, `lynx_fail`, `lynx_signal` |
| `lynxgfx.h` | screen, frame clock, palette, rectangles, lines, text |
| `lynxspr.h` | Suzy sprites: SCB setup, position, scale, flip, pens, chains |
| `lynxinput.h` | the pad, buttons, PAUSE, with edge detection |
| `lynxsnd.h` | Mikey sound effects: tones, noise, sweeps, decay |
| `lynxrnd.h` | xorshift random numbers |

cc65's own Lynx library is there too: TGI (`<tgi.h>`), `lynx_snd_*` (ABC
music), `lynx_eeprom_*`, `lynx_load`/`lynx_exec` (files on the cart),
ComLynx serial.

## Log (lynxlog.h)

| | |
|---|---|
| `lynx_log(fmt, ...)` | printf-style line to the harness log (cut at 127 chars; pulls in vsnprintf ~2 KB) |
| `lynx_log_s(s)`, `lynx_log_u(v)`, `lynx_log_x(v)`, `lynx_log_c(c)` | string / decimal / 4-digit hex / char, no printf |
| `lynx_done()` | `LYNX:DONE` + signal 1: `./lynx test` passes |
| `lynx_fail(why)` | `LYNX:FAIL why` + signal 2: `./lynx test` fails |
| `lynx_signal(n)` | a status event (`emu_run_until_signal` stops on it) |
| `lynx_in_harness()` | 1 under the harness ($FCF0 reads 'L', $FCF1 'Y'); test conveniences only |

The channel is `$FCF0` (characters) and `$FCF1` (signals), unused Suzy
addresses a real Lynx ignores. `RELEASE=1` (or `LYNX_NO_LOG=1`, `NDEBUG`)
compiles every call away.

## Screen (lynxgfx.h)

The display is 160x102, 4 bits per pixel. cc65's TGI driver keeps two
buffers (`$C038`, `$E018`) and draws with Suzy; the SDK wraps it.

| | |
|---|---|
| `gfx_init()` | driver, 60 Hz, default palette, both buffers cleared, frame counter on |
| `gfx_set_rate(hz)` | 50, 60 or 75 Hz |
| `gfx_clear(color)` | fill the back buffer |
| `gfx_present()` | show the back buffer at the next vertical blank (waits): call once per frame |
| `frame_count` | vertical blanks since `gfx_init` (16-bit, an interrupt counts them) |
| `frame_wait()` | wait for the next vertical blank without swapping |
| `gfx_rect(x, y, w, h, c)`, `gfx_frame(...)`, `gfx_line(x1, y1, x2, y2, c)`, `gfx_pixel(x, y, c)` | shapes (Suzy) |
| `gfx_text(x, y, c, s)`, `gfx_text_u(x, y, c, v)`, `gfx_text_centre(y, c, s)` | 8x8 font, 20x12 characters, transparent background |
| `gfx_color(i, r, g, b)` | palette entry i, 4 bits each |
| `gfx_palette(rgb16)` | 16 entries as 0xRGB; `gfx_palette_gbr(gbr16)` as the registers' 0xGBR; `gfx_palette_default()` |

The default palette (cc65's) gives the `COLOR_*` names: 0 transparent, 1
black, 2 red, 3 pink, 4 light grey, 5 grey, 6 dark grey, 7 brown, 8 peach,
9 yellow, 10 light green, 11 green, 12 purple, 13 blue, 14 light blue,
15 white. Pen 0 is transparent in sprites and text, so a clear uses colour 1
(black) or any other.

## Sprites (lynxspr.h)

Suzy draws chains of sprite control blocks (SCBs) with hardware scaling.
A sprite = an SCB (`SCB_REHV_PAL`: position, size, 8-byte pen palette) +
data from the converter.

```sh
./lynx sprite art/ship.txt -o src/ship.c         # text art: . = 0, 0-9 A-F = pens
./lynx sprite art/ship.png -o src/ship.c          # PNG: nearest default-palette colour
./lynx sprite art/sheet.png -o src/en.c --frames 16x16 --name enemy   # en_frames[]
./lynx sprite art/pic.png -o src/pic.c --palette image   # + pic_palette[16] (0xGBR)
```

The header gives `<NAME>_W`, `<NAME>_H`, `<NAME>_BPP` and `<name>_data` (or
`<name>_data_0..` + `<name>_frames[]`). Bits per pixel are the fewest that
hold the highest pen (or `--bpp`).

| | |
|---|---|
| `spr_init(&scb, data, bpp)` | normal sprite (pen 0 transparent), literal data, 1:1, identity pens, at 0,0 |
| `spr_init_nopal(&scb, data, bpp)` | `SCB_REHV` without its own pens (reuses the previous ones: smaller) |
| `spr_pos(s, x, y)` | signed 16-bit: sprites may hang off any edge |
| `spr_scale(s, sx, sy)` | 8.8 fixed point: `SPR_SCALE_1` (0x100), `SPR_SCALE_2`, `SPR_SCALE_HALF`, any value |
| `spr_flip(s, h, v)` | mirror around the position (v: drawn upward from y) |
| `spr_color(s, c)` | every non-zero pen -> colour c (recolour 1-bpp art) |
| `spr_pen(s, p, c)` | pen p -> colour c |
| `spr_link(s, next)`, `spr_hide(s, on)` | build a chain; skip one without unlinking |
| `spr_data(s, d)` | change the picture (animation frames) |
| `spr_draw(s)` | draw the chain s heads into the back buffer (waits for Suzy) |

SCBs must stay in RAM while Suzy draws: globals or statics. One chain of
many sprites is faster than many `spr_draw` calls.

## Input (lynxinput.h)

`input_update()` once per frame, then `input_held` (down now) and
`input_new` (went down since the last update). Bits: `IN_UP IN_DOWN IN_LEFT
IN_RIGHT IN_A IN_B IN_OPT1 IN_OPT2 IN_PAUSE`, `IN_DIRS`. They are the
JOYSTICK register's bits (`$FCB0`) plus PAUSE as bit 8: the same as the
harness's buttons. `input_read()` reads without edge tracking.

## Sound (lynxsnd.h)

Four Mikey channels; each is a timer clocking a shift register.

| | |
|---|---|
| `snd_init()` | all off, both speakers |
| `snd_tone(ch, hz, vol, decay)` | square wave, `vol` 0-127 loses `decay` per tick (0 = until `snd_off`) |
| `snd_noise(ch, pitch, vol, decay)` | noise, pitch 0 (hiss) .. 7 (rumble) |
| `snd_freq(ch, hz)`, `snd_sweep(ch, hz_per_tick)` | slide a tone (laser, falling bomb) |
| `snd_tick()` | once per frame: decay and sweep |
| `snd_off(ch)`, `snd_all_off()`, `snd_active()` | bit n = channel n playing |

For music, cc65's `lynx_snd_init/lynx_snd_play` (ABC tunes) runs on a
timer interrupt.

## Random numbers (lynxrnd.h)

`rnd_seed(s)`, `rnd8()`, `rnd16()`, `rnd_range(n)` (0..n-1). Deterministic:
seed from `frame_count` when the player presses a button.

## Build options (project.mk)

| | |
|---|---|
| `RELEASE=1` | no harness log (`LYNX_RELEASE`) |
| `DEBUG=1` | `-O0` |
| `OPT=...` | default `-Oirs -g` (`-g` only adds symbols) |
| `EXTRA_CFLAGS`, `EXTRA_ASFLAGS`, `EXTRA_LDFLAGS`, `EXTRA_OBJS` | |
| `EXTRA_SRC_DIRS` | more source directories (shared code) |
| `CFG` | another linker config (default `sdk/cfg/lynx.cfg`) |

Changing flags rebuilds everything (build/flags remembers them).

## Memory

| | |
|---|---|
| `$0000-$00FF` | zero page (cc65 + your `APPZP`) |
| `$0100-$01FF` | CPU stack |
| `$0200-$B837` | program, data, BSS (`build/<p>.map`: MAIN) |
| `$B838-$C037` | C stack (2 KB) |
| `$C038`, `$E018` | the two screen buffers |
| `$A058-$C037` | collision buffer, only with `tgi_setcollisiondetection(1)`: link with `CFG=$(LYNX_SDK)/cfg/lynx-coll.cfg` (MAIN to `$9657`, stack below `$A058`) |
| `$FC00`/`$FD00` | Suzy / Mikey registers (MAPCTL `$FFF9` = `$0C`: ROM and vectors are RAM) |

## Versioning

sdk/README.md: `LYNX_SDK_VERSION`, what a patch / minor / major change
means, the changelog.
