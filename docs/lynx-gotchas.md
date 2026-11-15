# Lynx gotchas

Things that cost time, with what to do instead.

## Hardware

- **The CPU is a 65SC02** (CMOS: `BRA PHX PHY PLX PLY STZ TSB TRB`, `(zp)`
  addressing, `INC A`), but without the Rockwell bit instructions
  (`RMB/SMB/BBR/BBS`) or `WAI/STP`. cc65's `-t lynx` knows this.
- **4 MHz, but Suzy steals the bus** while it draws: a frame full of big
  sprites leaves the CPU less time. Draw fewer, smaller sprites; scale them
  in hardware instead of drawing big art.
- **Suzy's maths unit**: 16x16 multiply (`MATHA..D` -> `MATHE..H`) and
  32/16 divide, much faster than cc65's software routines. Writing `MATHA`
  starts a multiply, `MATHE` a divide; wait while `SPRSYS & 0x80`.
- **The palette is 12-bit and per pen**: GREEN `$FDA0+n` (low nibble),
  BLUERED `$FDB0+n` (blue high, red low). 16 colours on screen, but you can
  change them every frame (or mid-frame on a timer interrupt).
- **Pen 0 is transparent** in normal sprites (and TGI text). Clear the
  screen with colour 1 or another, not 0.
- **MAPCTL `$FFF9`**: cc65's crt0 writes `$0C`: ROM and vectors are RAM,
  Suzy and Mikey stay mapped. The IRQ vector `$FFFE` is in RAM: cc65's
  irq.s installs its dispatcher there (`.interruptor` in asm adds a handler;
  return with carry clear so the others run).
- **The vertical blank is Mikey timer 2** (`INTSET` bit 2); the frame rate
  comes from timers 0 and 2 (`gfx_set_rate(50|60|75)`). cc65's crt0 starts
  at 60 Hz.
- **Left-handed play**: the Lynx can be turned over; games flip the display
  (`tgi_flip()`: SPRSYS bit 3 + DISPCTL) and must swap the directions
  themselves. The harness's buttons are always the raw register.

## Sprites

- **Literal data needs its length bytes**: each line starts with its length
  (including that byte); a 0 ends the sprite and a 1 means "next quadrant".
  The converter (`./lynx sprite`) writes them, plus a 0 pad byte on every
  line (the usual guard against Suzy's end-of-line quirks; it only adds
  transparent pixels).
- **SCBs and data must stay put while Suzy draws.** `spr_draw` waits, so a
  local SCB is fine for that call, but a chain built over several frames
  must be static.
- **Flips mirror around the position**: hflip draws leftwards from `hpos`,
  vflip upwards from `vpos`. Offset the position by the width/height.
- **Scaling is 8.8 fixed point** (`0x100` = 1.0) and applies from the
  position (top-left) unless the art was drawn around an action point.
- **Collision detection** needs a collision buffer: `tgi_setcollisiondetection(1)`
  puts it at `$A058-$C037`, over the end of the program and the C stack in
  the default map. Link with `CFG=$(LYNX_SDK)/cfg/lynx-coll.cfg`, or use
  bounding boxes (cheaper on a small number of objects anyway).
- **A full-screen clear is about a third of a 50 Hz frame** (Suzy fills
  8 KB). With mostly empty screens, erase instead: keep one SCB set per
  screen buffer and, before updating it, draw the set that went into this
  buffer two frames ago with every pen mapped to the background colour
  (projects/rock_blaster: `erase_begin`/`erase_end` in `src/objs.s`).

## cc65

- `%d/%u/%x` are 16-bit; use `%ld/%lu` with `long`.
- `int` is 16 bits; `char` is unsigned.
- Hot code: `#pragma static-locals (on)`, `uint8_t` loop counters and
  indexes, arrays of bytes, struct-of-arrays (`ex[i]`, `ey[i]`) instead of
  pointers to structs. Multiplying by a non-constant calls a library
  routine: use Suzy's maths or shifts.
- `lynx_log` pulls in vsnprintf (~2 KB); `lynx_log_s` / `lynx_log_u` don't.
  `lynx_log_u` still divides by 10 per digit (16-bit divides): a few
  thousand cycles; log in a busy frame with strings or subtract loops.
- A 32-bit multiply (`(int32_t)a * b`) costs ~1000 cycles and a 16-bit
  divide/modulo several hundred: a handful in one frame shows. Measure with
  the harness: `run_to_addr(fn)`, then to its return address, and compare
  `status()["cycles"]` (projects/rock_blaster README, Speed).

## The harness

- Frames are display refreshes; a program that changes its rate changes
  how long `run(60)` is in seconds (`status()["hz"]`).
- `run_until` matches only text after the mark (last `log()`, load, or
  match). Call `emu_log` to see everything since.
- Input is read when the program reads `$FCB0`, so a press shorter than
  the program's polling interval can be missed: `press(keys, hold_frames=3)`
  holds for 3 frames by default.
- Never write `$FCF0/$FCF1` yourself: use lynxlog.h, which compiles away in
  release builds.
- No boot ROM is needed or wanted: don't fetch `lynxboot.img`.
