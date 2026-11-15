# Writing Lynx games with lynxdev

How a game is put together with this SDK, and what the two finished ports
do about each part. File references are to `games/` (the lynx_games
submodule): **Frank the Frog** (`games/frank_the_frog`), a lane game drawn
as one sprite chain, and **Rock Blaster** (`games/rock_blaster`), an
asteroids game with ~100 moving objects. Read [getting-started.md](getting-started.md)
first; [sdk.md](sdk.md) has every function.

## The machine in one table

| | |
|---|---|
| CPU | 65SC02 at 4 MHz: **~66,000 cycles a frame at 60 Hz, 80,000 at 50 Hz**, less what Suzy takes while it draws |
| RAM | 64 KB, all of it: program + data from `$0200`, C stack `$B838-$C037`, two screen buffers `$C038` / `$E018` |
| Screen | 160x102, 4 bits a pixel, 16 colours from 4096 (12-bit palette, changeable any time) |
| Suzy | the sprite engine: draws chains of sprites (any size, scaled, flipped, recoloured through a pen palette) into a buffer; also a 16x16 multiplier and 32/16 divider |
| Mikey | timers (frame rate, interrupts), 4 audio channels, the palette, the display DMA |
| Input | d-pad, A (outer), B (inner), OPTION 1, OPTION 2, PAUSE |
| Cartridge | cc65's `.lnx`: the program file is loaded into RAM at boot (so it must fit below `$B838`); more files can be loaded later with `lynx_load` |

## The frame loop

Every game here has the same skeleton:

```c
gfx_init();                    /* TGI driver, double buffering, frame counter */
gfx_set_rate(50);              /* optional: 50, 60 (default) or 75 Hz */
snd_init();
for (;;) {
    input_update();            /* input_held, input_new (edges) */
    update();                  /* the game's rules: one step per frame */
    draw();                    /* sprites into the back buffer */
    gfx_present();             /* swap at the next vertical blank: the frame clock */
    snd_tick();                /* sound envelopes */
}
```

- **Frank:** `games/frank_the_frog/src/main.c:236` (the loop) to `:318`
  (`gfx_present`); rules in `lanes.c`/`frank.c`, drawing in `render.c`.
- **Rock Blaster:** `games/rock_blaster/src/main.c:409` to `:504`;
  `game.c` keeps the Amiga original's rules almost unchanged and `main.c` is
  the Lynx front end.

`gfx_present()` waits for the vertical blank, so a loop that finishes in
time runs exactly once a frame. A loop that takes longer misses a blank and
the game slows down, without any other symptom. Both games measure this:
they compare `frame_count` (vertical blanks) with their loop count and log
`fps` and `late` frames (`frank_the_frog/src/main.c:324`,
`rock_blaster/src/main.c:516`), and Rock Blaster reads how far the display
got (`MIKEY.timer2`, `main.c:501`) for the busiest frame.

Keep rules and drawing apart: the rules run once a frame whatever the
drawing costs, and tests can read the rules' variables by symbol.

## Drawing: one sprite chain

Everything on screen is a Suzy sprite. A sprite is an SCB (sprite control
block: position, size, flags, pen palette, a pointer to the pixel data and a
link to the next SCB) and Suzy draws a whole linked list of them in one go.
**One long chain is much cheaper than many `spr_draw` calls**: each call is
a C call, a Suzy start and a wait.

Frank builds the whole screen as one chain once (`render.c:100`
`link_all`: background rows, 30 lane objects, ponds, Frank, HUD glyphs,
lives, the black strip under the playfield) and then each frame only changes positions, pictures
and colours of the SCBs (`render.c:218` `render_frame`) and draws the head.
Objects that leave the screen are clipped by Suzy; ones that aren't needed
get `spr_hide(s, 1)` (the SKIP bit) instead of being unlinked.

Rock Blaster has 113 SCBs a frame (24 rocks, 8 bullets, 64 particles, the
ship, flame, lives, HUD digits) in one chain, written by the asm that moves
the objects (`rock_blaster/src/objs.s:151` `rocks_move` writes each rock's
SCB position, picture and SKIP flag as it moves it).

SCBs must stay where they are while Suzy reads them: make them `static` or
global, never locals of a function that returns before drawing.

### Pictures

`./lynx sprite art.txt|art.png -o src/x.c` turns text art (`.` and `0-F`)
or a PNG into Suzy sprite data (`--frames 16x16` slices a sheet). Use the
fewest bits per pixel the art needs: 1 bpp for single-colour shapes,
recoloured per object with `spr_color` (Frank's cars are one picture,
recoloured per lane through their pen palette and flipped for the lanes
that go left; `render.c`). Frank's art is in `art/*.txt`, generated in part
by `tools/mkart.py`; Rock Blaster draws its pre-rotated ship and rocks with
`tools/mkart.py` into `src/art.c`.

Scaling is free: `spr_scale(s, 0x200, 0x200)` is twice the size. Rock
Blaster's title zooms its logo in by changing the scale each frame; Frank's
background rows and HUD strips are one pixel scaled to their size (`render.c:85` `bar`, or `spr_box`).

## Clearing vs erasing

The two buffers are drawn in turn, so each frame starts with what was
drawn two frames ago. Three ways to deal with that:

1. **Clear** (`gfx_clear(colour)`): one Suzy fill of the whole screen.
   Simple, but it costs about a third of a 50 Hz frame
   (`rock_blaster/README.md`, Speed).
2. **Draw a background that covers everything.** Frank's rows are
   background-type sprites (pen 0 drawn too, `render.c:73` `bg_sprite`):
   river, road and grass rows cover the whole screen every frame, so
   nothing needs clearing (`render.c:1-15`).
3. **Erase what moved.** Rock Blaster keeps one SCB set per buffer, and
   before updating a set it draws it again with every pen mapped to the
   background colour (`main.c:31` comment, `main.c:416` `erase_begin`;
   `objs.s:592`). That erases exactly the old objects at a fraction of a
   clear. Frames with text over the playfield clear in full.

## Input

`input_update()` once per frame; then `input_held` (what's down) and
`input_new` (what went down this frame). Use `input_new` for one action per
press (Frank's hops: `main.c:237-243`), `input_held` for continuous ones
(Rock Blaster's turn, thrust and autofire: `main.c:449`). The Lynx
convention: **PAUSE** pauses, **PAUSE then OPTION 1** restarts (Rock
Blaster `main.c:439`), OPTION 1/2 for settings on the title screen.

## Sound

`lynxsnd.h`: four Mikey channels, square tones and noise with a volume
decay and a frequency sweep, ticked once a frame. `snd_tone(ch, hz, vol,
decay)` costs ~1.3k cycles; for fixed notes compute the period at compile
time with `SND_PERIOD(hz)` and use `snd_tone_p`. Frank has its own small
driver with compile-time periods (`frank_the_frog/src/sound.c:25` `PER`,
`:62` `tone`). Rock Blaster plays the original's ProTracker tune on two
channels from a table converted by `tools/mkmusic.py` (`src/music.c`) and
effects on the other two.

## Numbers: fixed point, no division

There is no FPU, and cc65's 32-bit maths and any division are slow
(hundreds to thousands of cycles). What the games do:

- **Positions as fixed point whose high byte is the screen pixel.** Rock
  Blaster keeps objects in 1/128 world pixel (`game.h:25` `P7`): the
  high byte is the screen coordinate, so drawing needs no shift. Only the
  ship keeps the original's 16.16 (`game.h:22`).
- **Tables instead of trigonometry** (the ship's 256 directions, the
  rock radii and squares in `objs.s`).
- **Suzy's maths unit** for the multiplies and divides you can't avoid:
  `math_mul16`, `math_div32` (~200-270 cycles).
- **Decimal kept as digits** (Rock Blaster's score string) instead of
  dividing by 10 to print.

## Hot loops in assembly

cc65 C is often several times slower than hand-written 65SC02. Profile first (see
[debugging.md](debugging.md), "Timing a piece of code"), then move the loops
that run per object per frame into ca65:

- Frank: `src/lanes_asm.s:21` `lanes_tick` (move 30 objects, wrap them)
  and `render_objs` (write their SCBs); the C they replace is beside them in
  `lanes.c`/`render.c` under `#if 0`.
- Rock Blaster: `src/objs.s` (moves, particle updates, bullet and ship vs
  rock tests with an 8-bit test first, then the exact sum of squares from a
  table).

Data the asm and C share are struct-of-arrays (`rk_x[i]`, `rk_y[i]`), not
arrays of structs: an index register reaches them directly. In C use
`#pragma static-locals (on)`, `uint8_t` loop counters and indexes.

## The cycle budget

| | cycles |
|---|---|
| a frame at 50 Hz / 60 Hz | 80,000 / 66,000 |
| Suzy filling the whole screen (`gfx_clear`) | ~27,000 |
| Frank's typical frame (all of it) / its Suzy drawing | ~50,000 / ~35,000 of 80,000 |
| `lynx_log` printf line (harness build only) | ~25,000-30,000 |
| `snd_tone` / `snd_period` | ~1,300 / ~170 |
| `math_mul16` / `math_div32` | ~200 / ~270 |
| a C function call with a few arguments | ~100-300 |

Measure, don't guess: the harness counts CPU cycles exactly
(`./lynx ctl position`), and both games log their frame rate.

## Logging and tests

Log state changes, not every frame: `lynx_log` is printf (expensive), use
`lynx_log_s`/`lynx_log_u`/`lynx_log_c` in play (Rock Blaster's `logb`,
`main.c:284`). A game's `test.py` boots it headless, plays it (often with a
small bot that reads the game's variables by symbol: Frank's bot reads
`lane_*` and `frank_probe` and hops when the next lane is safe;
`frank_the_frog/test.py`), and asserts on log lines and screenshots. Rock
Blaster's test also records the whole game and replays it, every frame
hash matching.

## Release builds and cartridge size

```sh
./lynx build mygame RELEASE=1      # no harness log: lynx_log compiles to nothing
```

Measure speed in a release build too: logging costs nothing there. The
`.lnx` holds cc65's loader and your program, which is copied into RAM at
boot, so the program, data and BSS must end below `$B838` (look at
`build/<game>.map`: the `MAIN` area). Frank is 26 KB, Rock Blaster 27 KB.
If a game outgrows RAM, keep levels or pictures as separate files on the
cartridge and load them with cc65's `lynx_load` (see cc65's lynx docs in
[resources.md](resources.md)).

## Porting a game from another machine

What the two ports did, in their READMEs (`games/*/README.md`, "What was
kept"): keep the original's rules and units (Rock Blaster's world is the
Amiga's 320 pixels wide, shown at half size), redo the screen for 160x102,
replace per-pixel drawing with sprites, and write a test that plays it.
Then measure the frame rate and move what's slow into tables or asm.
