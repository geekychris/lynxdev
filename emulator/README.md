# lynxh: Handy with the lynxdev harness

Upstream: [libretro/libretro-handy](https://github.com/libretro/libretro-handy)
(zlib licence), pinned by `HANDY_COMMIT` (`scripts/lib.sh`, with a fallback in
`scripts/build-emulator.sh`). `scripts/build-emulator.sh` fetches that commit
into `third_party/handy`, copies it to `third_party/handy-build/src`, applies
`emulator/patches/*.patch`, adds `emulator/ext/*` and builds:

| Binary | |
|---|---|
| `third_party/handy/build/lynxh` | headless: no SDL, no window, no sound device |
| `third_party/handy/build/lynxh-sdl` | SDL2 window + keyboard + sound (only if `sdl2-config` is found) |

Both link the same core and harness (`-DLYNXH`), so a recording made in the
window replays headless hash for hash. Binaries are installed with a rename:
running emulators keep their old file. Handy's libretro frontend
(`libretro/libretro.cpp`) is not used: `emulator/ext/lynxh.cpp` drives
`CSystem` directly.

**Boot ROM:** the HLE boot is always used unless `--bios FILE` is given.
Nothing searches for, downloads or ships `lynxboot.img`. Under HLE, Handy's
fake ROM decrypts the cart's first loader block to `$0200` and jumps there,
which is exactly what cc65's `.lnx` files (LNX header + cc65's encrypted
micro-loader + directory, the standard `lynx.cfg` flow) need: they boot
unchanged. (Checked with cc65 2.19: hello, TGI, joystick driver.) A BLL
`.o` (`BS93` header) loads straight into RAM as Handy's "homebrew" type.

## The patch (`0001-lynxh-harness-hooks.patch`, ~25 lines)

Hook calls only, all under `#ifdef LYNXH`; the logic is in `ext/lynxh.cpp`.

| File | Hook | Why |
|---|---|---|
| `lynx/c65c02.h` `C65C02::Update()`, before the opcode fetch | `if (gLynxhCpuCheck && LYNXH_CpuHook(mPC)) return;` | breakpoints, conditions, watchpoints, steps, step-over, run-to, `at=`, frame boundaries and the stop after a log match/signal. A non-zero return leaves the instruction unexecuted (PC unchanged): the main loop then serves commands at that instruction boundary. Costs one test per instruction while off |
| `lynx/susie.cpp` `CSusie::Poke` default case | `LYNXH_SuzyPoke()` | the guest log: `$FCF0` one character, `$FCF1` a signal |
| `lynx/susie.cpp` `CSusie::Peek` (unassigned addresses) | `LYNXH_SuzyPeek()` | in the harness `$FCF0` reads `$4C` ('L'), `$FCF1` `$59` ('Y') (real hardware: `$FF`) |
| `lynx/susie.cpp` `CSusie::PaintSprites()` | `LYNXH_SpriteChain(SCBNEXT)` | `hw` → `suzy.last_chain` (head of the last sprite chain) and `suzy.chains_last_frame` |

`$FCF0`/`$FCF1` are unassigned Suzy addresses: with Suzy mapped (MAPCTL bit 0
clear, as cc65's crt0 leaves it) CPU stores reach `CSusie::Poke`'s default
case. With Suzy mapped out they go to RAM and nothing is logged.

## How it runs

One thread. The control socket (TCP, 127.0.0.1, port chosen by the OS,
printed as `LYNXH port=N pid=P`) is served between frames and while the CPU
is stopped.

- **A frame** is one Lynx display refresh: it ends when Mikey's end-of-frame
  calls the display callback (timer 2 underflow), at the next instruction
  boundary. The rate is the program's (Mikey timers 0/2): cc65's default is
  **59.9 Hz** (`status` → `hz`). With no display running, a frame is cut every
  1/15 s.
- **cycles** are 4 MHz CPU cycles = Handy's 16 MHz system ticks / 4, since
  load, 64-bit. Handy's own 32-bit counter rebases by `$80000000` after
  ~4.5 emulated minutes; the harness accounts for that (tested over 20,000
  frames).
- **Paused** (headless default): frames advance only on `run`.
  **Free** (window default, or `resume`): real time (paced to `hz`); a
  client's `run` takes over at full speed (the window repaints ~30×/s).
- **Stopped mid-frame** (break, watch, step, log match, signal, `at`): the
  loop serves commands at that instruction; `run` continues the same frame.
  A free window keeps going after a log match / `at` / signal; breaks,
  watches and steps hold it (mode becomes paused) until `resume`.
- **Power-on** builds a new `CSystem` from the cart bytes (all Handy globals
  zeroed first), so a load in an emulator that ran other programs is
  identical to one in a fresh process. `reset` = power-on with the same cart
  (the Lynx has no warm reset). HLE boot clears RAM.
- **Input** is applied at the start of each frame (`SetButtonData`), so
  replays are exact at frame granularity.

## Protocol

`cmd key=value key="quoted value" ...` → `OK key=value ...` or `ERR message`,
one line each (a8dev's a8h protocol). Numbers: `123`, `$7F`, `0x7F`, `7Fh`,
`%0101`, or a symbol (`main`, `_main`) once symbols are loaded. `help` lists
the commands.

| Command | Arguments → reply |
|---|---|
| `status` | `mode=paused/free/running frame cycles frame_cycles midframe [line] pc a x y s p log signals signal signal_frame fps hz rotate hashing recording breakpoints watchpoints turbo window bios type symbols pid version loaded="" last_stop="…"` |
| `run` | `frames=N` (max, ≥ 1) `steps=N` `over=1` `to=ADDR` `at=CYCLE` `until=HEX since=OFF` `until_signal=1` `nobreak=1` → `why=frames/frame_limit/break/watch/step/to/at/log/signal ran frame cycles frame_cycles midframe [line] pc a x y s p [id cond] [addr access insn_pc old value] [match] [signal] log fps speed` |
| `pause` / `resume [turbo=1]` | mode |
| `load path=FILE` | power-on + boot `.lnx` / BLL `.o` → `type=lnx/o size bios symbols name=""`. Symbols are read from `<base>.lbl` (ld65 `-Ln`) or `<base>.dbg` |
| `reset` | power-on, same cart |
| `rotate [value=none/left/right]` | display rotation for screenshots and the window (screen data and hashes stay unrotated) → `rotate cart_rotate` |
| `regs` | `pc a x y s p flags=NV-BDIZC sleep irq frame cycles … symbol=""` |
| `setreg name=a/x/y/s/p/pc value=N` | |
| `peek addr= len= [space=cpu/ram]` | `data=HEX`. cpu: what the CPU sees through MAPCTL (Suzy, Mikey, ROM, vectors) **without side effects** (timer counts read from the timer state, `SERDAT`/`IODAT` without popping, `RCART0/1` read `$FF` because a read advances the cart); ram: the 64 KB RAM |
| `poke addr= data=HEX [space=]` | cpu space: hardware registers take effect |
| `find data=HEX [start end max space]` | `addrs=a,b,… count=` |
| `hw` | `suzy.*` (16-bit SCB/engine registers, `sprctl0/1 sprcoll sprsys sprsys_w suzybusen sprgo joystick switches math_* penindex last_chain chains_last_frame`), `mikey.*` (`tim0bkup…tim7ctlb`, `aud0vol…aud3misc`, `atten mpan mstereo intset intmask iodir iodat serctl dispctl dispadr pbkup line cpusleep`, `green=16 bytes`, `bluered=16 bytes`), `mem.mapctl/suzy/mikey/rom/vectors`, `cart.bank0/bank1/rotate` |
| `break id= [pc=] [cond=EXPR] [once=1]` / `watch id= addr= [len=] [kind=r/w/rw]` / `delete id=` / `delete all=1` / `breaks` | as a8h. A watch decodes the instruction about to run (65C02 modes incl. `(zp)`), stops **after** it with `insn_pc access old value` |
| `log [since= max=]` / `log_clear` | `base end since next data=HEX` (4 MB ring, absolute offsets) |
| `screen` | `w=160 h=102 format=rgb24 rotate frame hash data=HEX` (last completed frame, unrotated) |
| `palette` | `data=` 16 × RGB888, `raw=` 16 × `GBR` |
| `screen_hash` | `hash=` FNV-1a 32 of the rgb24 frame |
| `screenshot path=PNG [scale=N] [rotated=0]` | writes a PNG (rotated per `rotate` unless `rotated=0`) |
| `hash action=on/off/clear/get/status [since=]` | per-frame hashes, `data=hhhhhhhh…` (entry i = frame `first+i`) |
| `rec action=start/stop/get/status` | `data=F:BUTTONS,…` (one entry per change; F = frame the input applied to) |
| `input buttons=HEX` / `input raw=1 buttons=HEX` / `input raw=0` | harness layer (OR-ed with the window's keys) / forced (replays). `buttons` is always hex |
| `savestate path=` / `loadstate path=` | Handy context (`.lys`) + sidecar `<path>.lynxh` (key=value lines, then the last frame and the frame being drawn as raw 0RGB). `loadstate` without a loaded cart loads the sidecar's `loaded=` |
| `audio action=start path=X.wav` / `action=stop` / status | 48 kHz 16-bit stereo capture |
| `disasm [addr=A/pc] [count=N]` | `lines="addr:bytes:label:text;…" next=` (65C02 as Handy implements it, with symbols) |
| `symbols path=FILE` / `name=SYM` / `addr=A` | load .lbl/.dbg / look up |
| `quit` | |

**Buttons** (1 = pressed): bits 0-7 as cc65 reads `JOYSTICK` `$FCB0` in the
normal orientation (SPRSYS lefthand clear): `80` UP, `40` DOWN, `20` LEFT,
`10` RIGHT, `08` OPTION1, `04` OPTION2, `02` B (inner), `01` A (outer);
`100` PAUSE (`SWITCHES` `$FCB1` bit 0). (Handy stores the pad un-swapped and
swaps up/down, left/right on a lefthand-clear read; the harness converts.)

**Conditions:** `A X Y S P PC`, flags `N V B D I Z C`, `FRAME` (since load),
`LINE` (display line, from Mikey's line counter); numbers and symbols;
`[addr]`/`PEEK(addr)`, `DPEEK(addr)`; `= == != <> < > <= >=`, `+ - & | ^`,
`AND OR NOT && || !`, parentheses. Resuming from a stop doesn't re-check
breakpoints on the instruction it stopped on (a watch still sees its access).

## Command line

```
lynxh --port 0 [--paused | --free] [--rotate none|left|right] [--zoom N]
      [--nosound] [--bios lynxboot.img] [--host 127.0.0.1] [--load FILE] [--seed N]
```

`--seed` is accepted and ignored (the Lynx has no random source; power-on is
fixed). `LYNXH_PARENT_WATCH=1`: exit when the parent dies. `LYNXH_VERBOSE=1`:
Handy's own log on stderr. No config files are read or written; the cart's
EEPROM is never saved to disk.

Window keys: arrows = pad (turned with `--rotate`), Z / Left Alt = A,
X / Left Cmd / Left Ctrl = B, 1 = OPTION1, 2 = OPTION2, P / Return = PAUSE,
Esc closes.

## States and determinism

A paused session is a pure function of its inputs: same cart + same input
per frame → same frame hashes, cycles, registers and log, in two emulators
or one that ran other programs first. Handy's context save lacks Mikey's
display position (line counter, line DMA, address, buffer pointer); the
sidecar adds it, so a state continues frame for frame as the original did,
the first frame included (states from a plain `.lss` start at the top of a
frame).

## Tests

`emulator/tests/test_lynxh.py` (standalone, raw protocol; in `./lynx
selftest` via `tools/lynxdev/tests/test_emulator.py`) builds
`emulator/tests/emutest.c` with cc65 into `build/emutest/` and checks boot,
log/signature/signals, every button bit, break/step/over/to/at/cond/watch,
setreg, peek/poke/find/hw, screen/palette/PNG/rotation, wav, recording +
forced replay, determinism across emulators, savestate continuation, and a
20,000-frame run past Handy's counter rebase. `LYNXH_TEST_WINDOW=1` adds the
SDL window test.

## Updating Handy

1. Change `HANDY_COMMIT` in `scripts/lib.sh` (and the fallback in
   `scripts/build-emulator.sh`).
2. `scripts/build-emulator.sh`; if the patch no longer applies, fix it in a
   checkout and regenerate `emulator/patches/0001-lynxh-harness-hooks.patch`
   with `git diff`.
3. `python3 emulator/tests/test_lynxh.py`.
