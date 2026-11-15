# The emulator harness (Python API)

lynxdev runs programs in **Handy (libretro-handy) patched with the lynxh
harness** (`emulator/`). The patch gives Handy a control socket and makes it
deterministic, so Claude (MCP), tests, the CLI and DevBench can drive it:
run exact frames, wait for log lines, press buttons, set breakpoints, read
memory and Suzy/Mikey registers, take screenshots, record and replay.

```
 MCP (Claude) ──┐   ┌──── lynxdev session process ───┐        ┌─────────────────────┐
 ./lynx ctl ────┼──▶│ JSON control port ─ Emulator ──┼─ TCP ─▶│ lynxh (headless)    │
 DevBench ──────┘   │ registry build/sessions/*.json │ lynxh  │ lynxh-sdl (window)  │
                    └────────────────────────────────┘        └─────────────────────┘
```

`lynxdev.emu.Emulator` (`tools/lynxdev/src/lynxdev/emu.py`) starts the
emulator and drives it; tests use it in-process, sessions wrap it
(docs/mcp.md). This page is the Emulator API. The raw line protocol, the
patch and the build are in [emulator/README.md](../emulator/README.md).

## Building

`scripts/build-emulator.sh` (run by setup) fetches the pinned Handy
(`HANDY_COMMIT` in scripts/lib.sh), applies the patch and builds
`third_party/handy/build/lynxh` (headless) and `lynxh-sdl` (window, needs
SDL2). `LYNX_EMULATOR` / `LYNX_EMULATOR_WINDOW` point the harness at other
binaries.

**No boot ROM.** Cartridges boot through a high-level emulation of the boot
ROM: cc65's `.lnx` (header, encrypted loader, directory) runs unchanged.
`LYNX_BIOS=/path/to/lynxboot.img` makes sessions use a real one if you own
it. Nothing ever searches for, downloads or ships a ROM.

## Sessions

```python
from lynxdev.emu import Emulator
with Emulator() as emu:                      # headless, paused
    emu.load("projects/hello/build/hello.lnx")
    assert emu.run_until("hello: ready", 600)
    emu.press("A")
    emu.screenshot("build/shot.png")
```

`Emulator(window=False, paused=None, rotate="none", zoom=None, audio=None,
seed=0, extra_args=(), log_path=None, bios=None)`

| | |
|---|---|
| headless (default) | **paused**: frames advance only on `run`, `run_until`, `press`, `continue_`, `stepi`..., as fast as the host allows, identically every time |
| `window=True` | an SDL window for the human, **free-running** in real time. `run`/`run_until` fast-forward it (the window keeps showing frames and the user's keys stay live), then real time resumes. A breakpoint, watchpoint or step **holds** the window until `resume()` |
| `pause()` / `resume(turbo=False)` | switch either kind |
| `rotate` | `none`, `left`, `right` (vertical games); screenshots and the window follow it; `set_rotate()` changes it |
| `zoom` | window scale (default 4) |

Each emulator is our own child process. `close()` (or the `with` block)
quits it, and it exits by itself if the Python process dies. Nothing ever
stops emulators by name.

## Programs

| | |
|---|---|
| `load(path)` | power-on, then boot a `.lnx` (HLE), or restore a state (`.lys`). Symbols come from the `.lbl`/`.map` beside it |
| `reset()` | power-on with the same cartridge (the Lynx has no warm reset) |
| `reload()` | power-on and load the program file again (picks up a rebuild) |
| `power_cycle()` / `reboot()` | a fresh emulator process (reboot also reloads; use it on a wedged emulator) |

## Running

| | |
|---|---|
| `run(frames=1)` | exactly N frames (a frame = one display refresh); stops early at a breakpoint |
| `run_until(text, max_frames=3000, since=None)` | until the guest log contains `text` -> bool. Only text written after the **mark** counts: the last `log()`, `load`/`reset`, or the previous match (`since=` an absolute offset from `log_cursor()` overrides). Stops on the instruction after the one that wrote its last byte |
| `run_to(frame, tstates=0)` | to frame N since load, `tstates` CPU cycles into it |
| `run_to_addr(addr, max_frames)` / `run_until_signal(max_frames)` | to an address / to the next signal |
| `position()` | `frame`, `since_load`, `cycles` (4 MHz CPU cycles since load), `tstates` (cycles into the frame), `midframe` |
| `status()` | mode (`paused`/`free`/`running`), position, PC + symbol, log end, signal, `hz`, `fps`, `rotate`, `last_stop` |

Every run returns a dict: `why` (`frames`, `frame_limit`, `breakpoint`,
`watchpoint`, `step`, `log_match`, `position`, `signal`), `frames` run,
`frame`, `since_load`, `cycles`, `midframe` (+ `line`), `pc`, `symbol`,
`regs`, `fps` and `speed` (x real time), and the breakpoint/watch details.

**Frame rate.** A frame is one LCD refresh, so its length depends on the
program's Mikey timer 0/2 setup: 60 Hz with `gfx_init()` (and cc65's crt0),
50 or 75 Hz with `gfx_set_rate()`. `status()["hz"]` gives the current rate;
videos use it.

## The guest log and signals

Programs store a character to **`$FCF0`** for each byte of the log (`\n`
ends a line) and a status byte to **`$FCF1`** (a *signal*: `lynx_done()`
sends 1, `lynx_fail()` 2). Both are unused Suzy addresses that a real Lynx
ignores. In the harness `$FCF0` reads `$4C` ('L') and `$FCF1` reads `$59`
('Y') (`lynx_in_harness()`). The SDK wrappers are in sdk/include/lynxlog.h;
release builds compile them out.

| | |
|---|---|
| `log(since=None)` | text since the mark (moves the mark to the end) |
| `log_text(since)`, `log_cursor()`, `debug_log` | without moving the mark / the end offset / every line so far |
| `status()["signal"]`, `run_until_signal()` | the last signal |

## Input

Buttons: `UP DOWN LEFT RIGHT A B OPTION1 OPTION2 PAUSE` (aliases: `OPT1`,
`OPT2`, `FIRE` = A, `START` = PAUSE), combined with `+`. They reach the
JOYSTICK (`$FCB0`) and SWITCHES (`$FCB1`) registers raw: a game that flips
the screen swaps directions itself (the window's arrow keys turn with the
rotation).

| | |
|---|---|
| `press(keys, hold_frames=3, release_frames=3)` | press together, hold, release, wait |
| `hold(keys)` / `release(keys=None)` | open-ended (None = everything) |
| `pad(dirs)` | set the d-pad and A/B (`"UP+A"`, `None` = released) and keep them |
| `sequence(steps)` | `[{"press": "A"}, {"run": 20}, {"until": "x", "max": 500}, {"hold": "UP"}, {"release": "UP"}, {"pad": "LEFT+B"}]`; any step may carry `"at": N` (frames since load) |

In a window, the user's keys and the harness's buttons are OR-ed.

## Screen

| | |
|---|---|
| `screenshot(path=None, scale=1, rotate=None)` | PIL RGB image, 160x102 (102x160 rotated) |
| `screen_hash()` | md5 of the pixels |
| `frame_hash()` | the emulator's FNV-1a of the frame (what recordings compare) |
| `record_video(path, frames=100, every=1, scale=1)` | `.gif`, `.mp4` (ffmpeg) or a directory of PNGs |
| `frame_buffer()` | the raw RGB888 frame, unrotated |
| `palette()`, `palette_raw()` | the 16 Mikey colours as RGB tuples / 12-bit 0xGBR register values |

The frame is the last completed one, with the palette as it was when each
line was drawn (mid-frame palette changes show).

## Memory and hardware

| | |
|---|---|
| `read_mem(addr, length, space="cpu")` | the CPU's view through MAPCTL (Suzy `$FC00`, Mikey `$FD00`, ROM, vectors; registers read without side effects); `space="ram"`: the raw 64 KB RAM |
| `write_mem(addr, data, space="cpu")` | a cpu-space write to a register takes effect |
| `peek`, `peek16`, `poke`, `find_mem(bytes or text, start, end)` | |
| `hw()` | `{"suzy": {...}, "mikey": {...}, "mem": {...}}`: Suzy (SCB registers, `vidbas`, `sprsys`, `last_chain`...), Mikey (timers, audio channels, `dispadr`, palette...), MAPCTL |
| `memory_map()` | MAPCTL decoded: whether Suzy, Mikey, ROM and the vectors are mapped |
| `scb_chain(start=None, limit=64)` | decode a sprite chain (default: the one Suzy last drew): control bits, bpp, type, position, size, stretch/tilt, pens, data pointer, next |
| `regs()` / `set_reg(name, value)` | `A X Y S P PC` and `FLAGS` (`NV-BDIZC`); values may be symbols |

Addresses anywhere: numbers, `$hex`/`0xhex`/`NNh`, symbols (`main` or
`_main`), `symbol+0x10`, or a register name.

## Debugging

| | |
|---|---|
| `break_at(addr)` | before the instruction at addr runs |
| `break_if(condition)` | `"A=5 AND PC=$0400"`, `"PC=_main AND [counter]>=$10"`, `"[$FCB0]&$01"`, `"FRAME>=200"` |
| `watch(addr, length=1, kind="w")` | `r`, `w`, `rw`: stops **after** the CPU instruction that accessed it (Suzy's DMA doesn't count), with `insn_pc`, `access`, `old`, `value` |
| `delete(index=None, addr=None)`, `breakpoints()` | everything if no argument; hits counted |
| `continue_(max_frames=500)` | until one fires (`stopped`) |
| `stepi(count=1)`, `step_over()` | `step_over` runs a JSR until it returns |
| `disasm(addr="pc", count=12)` | 65SC02 (the CMOS opcodes: `BRA PHX STZ TSB (zp)`...), with symbols |
| `backtrace(items=8)` | best effort: return addresses on the stack, with callers and callees |

Conditions: `A X Y S P PC`, flags `N V B D I Z C`, `FRAME` (since load),
`LINE` (display line); numbers; `[addr]`/`PEEK(addr)`, `DPEEK(addr)`;
`= == != <> < > <= >=`, `+ - & | ^`, `AND OR NOT && || !`, parentheses.

## States

`save_state(path)` writes a Handy state (`.lys`) plus `<path>.lynxh` (frame
counter, the display position Handy's own state lacks, the program).
`load_state(path)` restores both: the session continues frame for frame as
the original did.

## Recording and replay

**Input recorder** (`record_input`, MCP `emu_record_input`, `./lynx record`):
the patched core records the buttons the machine sees each frame, from the
harness and from the user's keys in a window, and hashes every frame.

- `record_input("start")`: from here (a state is saved to replay from);
  `from_reset=True`: reload the program and record from the load.
- `record_input("save", path)`: JSON (events, per-frame hashes) + `.lys`.
- `replay(path)` (`./lynx replay r.json --runs 2`): restores, forces each
  frame's recorded buttons, compares every frame hash -> `ok`, `compared`,
  `mismatches`, `first_mismatch`.

A window recording replays headless with every frame hash matching.

**Checkpoint replays** (`checkpoint(label)`, `save_replay`, `replay` of a
version-1 file): the agent's own inputs since `load` plus screen hashes at
labelled points.

## Determinism

A paused session is a pure function of its inputs: power-on is a fixed
state, nothing reads the wall clock, and input changes only between
frames. Two emulators given the same program and inputs produce the same
frame hashes, registers and log. tools/lynxdev/tests/test_emulator.py
(emulator/tests) and test_remote.py check it.
