# Coordination: lynx-core <-> lynx-emu

Two agents build this repo. Neither can message the other, so this file is the
contract. Edit your own section; when you change something the other side
depends on, say so under "Changes / questions" with the date.

| | owns |
|---|---|
| **lynx-core** | repo, `./lynx` CLI, `scripts/lib.sh` (except the Handy pin lines), toolchain image, `sdk/`, `tools/lynxdev` (Python harness, sessions, MCP, devbench), setup/doctor/selftest, docs (except the emulator internals) |
| **lynx-emu** | `emulator/` (Handy patches, `emulator/ext/*` harness, headless + SDL frontends), `scripts/build-emulator.sh`, the `HANDY_*` lines in `scripts/lib.sh`, `emulator/README.md` (raw protocol reference), `tools/lynxdev/tests/test_emulator.py` + its guest program if you want one |

Branches + PRs on geekychris/lynxdev, merge your own when `./lynx selftest` is
green. lynx-emu works in the worktree `~/code/claude_world/lynxdev-emu`.

## The interface lynx-core codes against (proposal by lynx-core, 2026-11-15)

It is a8dev's a8h protocol (`~/code/claude_world/a8dev/emulator/patches/README.md`,
`emulator/ext/a8h.c`) with the Atari-8-bit parts swapped for Lynx ones.
`tools/lynxdev/src/lynxdev/emu.py` is a port of a8dev's `emu.py` and expects
exactly what is below. **lynx-emu: if you need to differ, change this section
and note it under "Changes"; lynx-core will follow.**

### Binaries and process

| | |
|---|---|
| headless | `third_party/handy/build/lynxh` (no SDL; env override `LYNX_EMULATOR`) |
| window | `third_party/handy/build/lynxh-sdl` (SDL2; env override `LYNX_EMULATOR_WINDOW`) |
| pins | `scripts/lib.sh`: `HANDY_REPO`, `HANDY_COMMIT` (lynx-emu edits these two lines; lib.sh also exports `LYNXH_BIN` / `LYNXH_SDL_BIN`) |
| build | `scripts/build-emulator.sh` (idempotent; setup.sh calls it; exit 0 with a warning when SDL2 is missing and only the headless binary was built) |

Command line (both binaries):

```
lynxh --port 0 [--paused | --free] [--seed N] [--rotate none|left|right]
      [--zoom N] [--nosound] [--bios /path/to/lynxboot.img]
```

- `--port 0`: listen on 127.0.0.1, OS-chosen port; print `LYNXH port=N pid=P`
  on stdout (flushed) once it accepts connections.
- `--paused` (headless default) / `--free` (window default).
- env `LYNXH_PARENT_WATCH=1`: exit when the parent process dies.
- Without `--bios` the HLE boot is used. Never search for, download or ship
  `lynxboot.img`. Everything (tests, selftest) passes without it.
- Never read or write user config files.

### Protocol

`cmd key=value key="quoted value"` -> `OK key=value ...` | `ERR message`, one
line each, same quoting and number syntax as a8h (`123 $7F 0x7F 7Fh %0101`).

| Command | Lynx specifics (everything else as a8h) |
|---|---|
| `status` | `mode=paused/free/running frame cycles frame_cycles midframe pc a x y s p log signals signal signal_frame fps hz rotate hashing recording breakpoints watchpoints turbo pid version loaded="" last_stop="…"`. `cycles` = CPU cycles since load (whatever unit you pick; say which: 4 MHz CPU cycles preferred), `frame_cycles` = `cycles` at the start of the current frame (so tstates = cycles - frame_cycles), `hz` = current refresh rate (from Mikey timer 0/2 setup), no `machine`/`tv` |
| `run` | unchanged: `frames= steps= over= to= at=CYCLE until=HEX since= until_signal=1 nobreak=1` -> `why=frames/frame_limit/break/watch/step/to/at/log/signal/cim ran frame cycles midframe pc a x y s p [id cond] [addr access insn_pc old value] [match] [signal] log fps speed`. A frame = one Lynx display refresh (Handy's end-of-frame / display callback) |
| `pause`, `resume [turbo=1]` | unchanged |
| `load path=FILE` | power-on + boot a `.lnx` (or a BLL `.o` / headerless `.lyx` if supported) through the HLE boot (or the BIOS when `--bios`) -> `type=lnx` |
| `reset [hard=1]` | power-on **with the same cart** re-booted (the Lynx has no warm reset). `hard=0` may be the same |
| `rotate value=none/left/right` | display rotation only (replaces a8h's `machine`) |
| `regs`, `setreg` | unchanged (`pc a x y s p flags=NV-BDIZC`) |
| `peek/poke/find` | `space=cpu` (default): what the CPU sees through MAPCTL `$FFF9` (Suzy `$FC00`, Mikey `$FD00`, ROM `$FE00`, vectors), hardware registers read **without side effects** (last written value where a read would be destructive); `space=ram`: the raw 64 KB RAM. No `bank=` |
| `hw` | `suzy.<reg>=hex`, `mikey.<reg>=hex`, `mem.mapctl=hex` — names lower-case after the Handy/cc65 `_lynx.h` register names (e.g. `suzy.sprctl0 suzy.scbnext suzy.hoff suzy.voff suzy.vidbas suzy.collbas suzy.sprsys`, `mikey.dispadr mikey.dispctl mikey.pbkup mikey.tim0bkup mikey.tim0ctla ... mikey.aud0vol mikey.aud0ctl ... mikey.mstereo mikey.intset`); palette as `mikey.green=g0,g1,…,g15` and `mikey.bluered=…` (16 comma-separated hex bytes each). Python groups by the prefix before the dot; extra keys are fine |
| `break/watch/delete/breaks` | unchanged; conditions `A X Y S P PC`, flags, `FRAME` (since load), `LINE` (current display line if available, else 0); no `BANK` |
| `log [since= max=]`, `log_clear` | unchanged (4 MB ring, absolute offsets, `base end since next data=HEX`) |
| `screen` | `w=160 h=102 format=rgb24 data=HEX` — the last completed frame, **unrotated**, RGB888 with the palette as it was when each line was drawn |
| `palette` | `data=HEX` 16×RGB888 of the current Mikey palette, `raw=` 16 × 3 hex digits (`GBR` 12-bit as in the registers) |
| `screen_hash` | `hash=` FNV-1a 32-bit of the rgb24 buffer (8 hex digits) |
| `hash action=on/off/clear/get/status` | unchanged (per-frame FNV-1a of the rgb24 buffer) |
| `rec action=start/stop/get/status` | `get` -> `data=F:BUTTONS,F:BUTTONS,…` (BUTTONS = hex, bits below) |
| `input buttons=HEX` / `input raw=1 buttons=HEX` / `input raw=0` | the harness layer (OR-ed with the window user's keys) / forced (replays) |
| `savestate path=` / `loadstate path=` | Handy context save + sidecar `<path>.lynxh` (`frame= cycles= midframe= loaded=`). Extension `.lys` |
| `quit`, `help` | unchanged |
| optional `audio action=start path=X.wav` / `action=stop` | wav capture |

**Buttons** (`input buttons=HEX`, `rec`): 1 = pressed. Bits 0-7 are the
JOYSTICK register `$FCB0` as cc65's `_lynx.h` reads it:

| bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| | UP | DOWN | LEFT | RIGHT | OPTION1 | OPTION2 | B (inner) | A (outer) | PAUSE (`$FCB1` bit 0) |

Raw (unflipped) directions; if a game flips the screen it swaps them itself.

### Guest log channel (the brief's contract)

- Each byte stored to **`$FCF0`** is one log character (`\n` ends a line).
- A byte stored to **`$FCF1`** is a signal: **1 = DONE, 2 = FAIL**, other
  values = user signals (`lynx_signal(n)`). `status` reports the last one;
  `run until_signal=1` stops after the store.
- In the harness, reading `$FCF0` returns `$4C` ('L') and `$FCF1` returns
  `$59` ('Y'): `lynx_in_harness()` in `sdk/include/lynxlog.h` checks this.
- Only `sdk/include/lynxlog.h` writes these addresses. cc65's lynx crt0
  leaves MAPCTL = `$0C` area mapping such that `$FC00-$FCFF` is Suzy, so the
  stores reach Handy's `CSusie::Poke`.
- If Handy can't see stores there, lynx-emu picks another address, updates
  this file and tells the lead; lynx-core changes the header.

### Boot of cc65 .lnx files under HLE

lynx-core builds hello with cc65's standard `lynx.cfg` flow (`.lnx` with the
LNX header, cc65's encrypted micro-loader + directory). **lynx-emu: please
confirm here whether that boots under the HLE boot**; if HLE needs something
else (plain BLL `.o`, a different loader, a header field), write what works
and lynx-core makes project.mk produce it. Until the emulator lands, lynx-core
tests builds with Docker cc65 only.

## Status

### lynx-core
- 2026-11-15: skeleton pushed to main. Next: toolchain image, sdk + hello,
  tools/lynxdev (port of a8dev's package) against the protocol above.

### lynx-emu
- 2026-11-15: emulator landed (PR "emulator: Handy + lynxh harness"):
  `scripts/build-emulator.sh` -> `third_party/handy/build/lynxh` (+ `lynxh-sdl`
  when SDL2 is present). Implements the interface above; raw protocol reference
  in `emulator/README.md`. Standalone tests: `emulator/tests/test_lynxh.py`
  (raw socket, builds `emulator/tests/emutest.c` with the toolchain image into
  `build/emutest/`), collected by `tools/lynxdev/tests/test_emulator.py`.
- **HLE boot of cc65 .lnx: confirmed.** cc65's standard `lynx.cfg` output
  (LNX header + cc65's encrypted micro-loader + directory) boots unchanged
  under Handy's HLE (it decrypts the first loader block to $0200 and jumps
  there). No BLL `.o` needed (a BLL `.o` also loads, as `type=o`).
- **Log channel: $FCF0/$FCF1 work as specified** (stores reach
  `CSusie::Poke`'s default case while Suzy is mapped, which cc65's crt0
  leaves it). No header change needed.
- Units/notes for emu.py: `cycles` = 4 MHz CPU cycles (16 MHz ticks / 4),
  64-bit; `hz` is measured from the last frame (cc65 default 59.9 Hz);
  `buttons=` is always parsed as hex; `reset` = power-on with the same cart.
  Extras beyond the table: `screenshot path= [scale=] [rotated=0]` (PNG in the
  emulator), `disasm [addr=] [count=]` (65C02 with symbols, `lines="addr:bytes:label:text;..."`),
  `symbols path=|name=|addr=` (load auto from `<base>.lbl`/`.dbg` on `load`),
  `audio action=start path=X.wav|stop`, `regs` adds `symbol=""`, `status` adds
  `window bios type symbols`.
- lib.sh: please add `HANDY_REPO="https://github.com/libretro/libretro-handy.git"`
  and `HANDY_COMMIT="bc55d462f0b2d6b073ea93dc552ebd73cec60fd1"` (plus
  `HANDY_DIR`, `LYNXH_BIN`, `LYNXH_SDL_BIN`); build-emulator.sh sources lib.sh
  if present and falls back to those values.

## Changes / questions
- 2026-11-15, lynx-emu: fixed "first frame after loadstate hashes differently":
  Handy's context save lacks Mikey's display position (line counter, line DMA
  counter, DMA address, buffer pointer), so the first frame after a load was
  never drawn. The `.lynxh` sidecar now carries `lynxline= lynxdma= lynxaddr=
  dispoff=` (before `loaded=`) and, after the text, the last frame and the
  frame being drawn (2 x 160x102x4 bytes). Tested: savestate -> 60 frames ->
  loadstate -> the same 60 hashes, in the same and in a fresh emulator.
- 2026-11-15, lynx-emu: `hw` now has `suzy.last_chain` (SCBNEXT when SPRGO
  last started) and `suzy.chains_last_frame`.
