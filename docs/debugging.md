# Debugging Lynx programs

The emulator is a debugger: every session (`./lynx serve`, `./lynx run`, the
arcade's window, DevBench's Boot, MCP `emu_boot`) answers the same commands.
Three ways in, same commands underneath:

| | how | example |
|---|---|---|
| shell | `./lynx ctl <command> key=value ...` (the newest session; `--port N` for another) | `./lynx ctl break addr=main` |
| Claude Code | MCP tools (`emu_*`), see [mcp.md](mcp.md) | `emu_break("main")` |
| browser | DevBench (`./lynx devbench --open`, :3690): buttons + the console box | Debugger panel, or `break {"addr":"main"}` in the console |
| Python | `lynxdev.emu.Emulator` in a test, see [emulator-harness.md](emulator-harness.md) | `emu.break_at("main")` |

`./lynx ctl help` lists every command. Addresses can be numbers, `$hex`,
symbols from your program's `.lbl` (C names with or without the leading
underscore: `main` = `_main`), `symbol+offset`, or a register name.

The examples below are a real session on Rock Blaster.

## 1. Start a session you can stop

```sh
./lynx serve rock_blaster &        # headless, paused: nothing runs until you say so
./lynx ctl run_until text="rb: title"
./lynx ctl press keys=A            # start a game
./lynx ctl run frames=30
```

A headless session is paused: frames advance only on `run`, `run_until`,
`press`, `continue`, `stepi`. So you can stop anywhere and nothing moves
while you look. `./lynx run <game>` gives a window that runs in real time
(the user can play it); a breakpoint stops it too, `resume` lets it go.

## 2. Breakpoints and stepping

```sh
./lynx ctl break addr=hit_scan
./lynx ctl continue max_frames=200
```
```
 "why": "breakpoint",
 "symbol": "_hit_scan",
 "frames": 66,
```

```sh
./lynx ctl disasm addr=pc count=6      # 65SC02 with your symbols
```
```
_hit_scan:
  28BF  8D 04 95  STA hr
  28C2  A2 02     LDX #$02
lims:
  28C4  BD 27 65  LDA rock_rad,X
  28C7  18        CLC
  28C8  6D 04 95  ADC hr
```

```sh
./lynx ctl backtrace items=3           # who called it
```
```
 {"caller_symbol": "_game_update+0x1b5", "called_symbol": "_hit_scan", ...},
 {"caller_symbol": "_main+0x130",        "called_symbol": "_game_update", ...}
```

| | |
|---|---|
| `stepi count=N` | N instructions |
| `step_over` | one instruction; a JSR runs until it returns |
| `run_to_addr addr=X` | a one-shot breakpoint |
| `regs`, `set_reg reg=A value=5` | registers; `PC` takes symbols |
| `break condition="A=5 AND PC=$0400"` | conditional: `A X Y S P PC`, flags, `FRAME`, `LINE`, `[addr]`, `DPEEK(addr)`, `AND OR NOT`, compare and arithmetic |
| `breakpoints`, `delete index=1`, `delete` | list (with hit counts), remove one, remove all |
| `resume` / `pause` | let a stopped window session run on / stop it |

C source lines: the `.lbl` gives function and label names; to find the C
behind an address, look the function up in `build/obj/<file>.lst` (the
compiler listing) or `build/<p>.map`. `DEBUG=1 ./lynx build x` (`-O0`) makes
the code follow the source more closely.

## 3. Watchpoints: who writes this?

```sh
./lynx ctl watch addr=gs_lives length=1 kind=w
./lynx ctl continue max_frames=3000
```
```
 "why": "watchpoint",
 "break": "gs_lives ($7FA0) w",
 "insn_pc": "076b",
 "old": "03",
 "value": "02",
 "symbol": "_game_update+0x222",
```

It stops *after* the instruction that wrote it (`insn_pc`), with the old
and new values. `kind=r` for reads, `rw` for both. Watches see the CPU's
accesses, not Suzy's sprite engine or the screen DMA.

## 4. Memory, Suzy and Mikey

```sh
./lynx ctl read_mem addr=ship_px length=4
#   6BE7  a0 00 66 00        ..f.
./lynx ctl write_mem addr=gs_lives hex=09      # cheat while debugging
./lynx ctl find_mem text="GAME OVER"
./lynx ctl memory_map                          # MAPCTL: Suzy/Mikey/ROM/vectors mapped?
./lynx ctl hw                                  # every Suzy and Mikey register
./lynx ctl palette                             # the 16 colours (RGB + 12-bit)
```

`read_mem` reads what the CPU sees (registers without side effects);
`space=ram` reads the RAM under Suzy, Mikey and the ROM.

`hw` has, among others: Suzy `sprsys`, `vidbas` (the buffer Suzy draws
into), `scbnext`, `last_chain` (the head of the last sprite chain), the
maths registers; Mikey's 8 timers (`tim0bkup`... timer 0 = lines, timer 2 =
frames), the 4 audio channels (`aud0vol`, `aud0ctl`, `aud0tback`...),
`dispadr` (the buffer on screen), the palette.

## 5. The sprite chain inspector

The sprites Suzy drew last, decoded:

```sh
./lynx ctl scb_chain limit=2
```
```
{"addr": 32712, "bpp": 1, "type": 4, "literal": true, "skip": false,
 "hpos": 13, "vpos": 21, "hsize": 256, "vsize": 256, "penpal": "08",
 "data": 23222, "next": 32735}, ...
```

Wrong place, wrong size (`hsize` 256 = 1.0), wrong colours (`penpal`),
`skip` set, a broken `next` link, or the chain stopping early: all visible
here. MCP `emu_scb_chain`, DevBench's **Sprites (SCB chain)** tab.
`scb_chain start=my_scb` walks a chain you built that hasn't been drawn.

## 6. The log channel

The cheapest debugger is a log line:

```c
lynx_log("enemy: spawn i=%u x=%u\n", i, x);   /* printf-style; ~25k cycles a line */
lynx_log_s("enemy: hit\n");                   /* ~20 cycles a character */
lynx_signal(7);                               /* an event the harness can stop on */
```

```sh
./lynx log mygame --frames 600 --press A@100   # the log of a scripted run
./lynx ctl log                                 # new lines since you last asked
./lynx ctl run_until text="enemy: hit" max_frames=5000
./lynx ctl run_until_signal                    # stop at the next lynx_signal/done/fail
```

`run_until` stops on the instruction right after the one that wrote the
line's last character, so you can inspect the state that produced it.
Release builds (`RELEASE=1`) compile all logging away.

## 7. Reproducing a bug: record and replay

```sh
./lynx record rock_blaster -o build/r.json --press A@60 --hold LEFT@80 --release ALL@120 --frames 300
./lynx replay build/r.json --runs 2
#   PASS 2 replays give identical hash sequences (300 frames)
```

A paused session is deterministic: the same inputs give the same frames,
hash for hash. So a recording reproduces a bug exactly, and the per-frame
hashes tell you the first frame where a fix changed anything.

From a window (the user playing): `./lynx ctl record_input action=start`,
play until the bug, `./lynx ctl record_input action=save path=build/bug.json`.
Replay it headless with breakpoints set first.

## 8. Save states

```sh
./lynx ctl save_state path=before_boss       # build/states/before_boss.lys
./lynx ctl load_state path=before_boss
```

Exact at a frame boundary: load it, and the game continues frame for frame
as before. Combine with a breakpoint to retry a crash many times.

## 9. Screens

```sh
./lynx ctl screenshot path=build/now.png scale=3
./lynx ctl record_video path=build/clip.gif frames=120
./lynx ctl screen_hash
```

## Common crashes and how to find them

| Symptom | Likely cause | Find it |
|---|---|---|
| Hangs, PC loops in a few instructions | waiting for something that never comes: `tgi_busy`, a VBL wait with interrupts off (`sei` left on), Suzy maths busy | `./lynx ctl regs` + `disasm addr=pc`; check the I flag in `FLAGS` |
| Garbage on screen / the program's own code overwritten | a write past an array, or the C stack (2 KB, `$B838-$C037`) running into data: deep recursion, big local arrays | `watch` the overwritten address; `read_mem addr=$B838` to see how deep the stack went; keep big buffers static |
| Sprites missing | `skip` set, `next` chain broken, pen 0 (transparent), position off screen, SCB on the stack and gone | `scb_chain`; `palette` |
| Sprites in the wrong colour | pen palette: one byte holds two pens (`spr_pen`), 1-bpp art uses pen 1 | `scb_chain` (`penpal`) |
| Bars 8x too wide | 1-bpp *literal* art in a background sprite type | use `spr_box` ([lynx-gotchas.md](lynx-gotchas.md)) |
| Slows down at busy moments | per-frame `lynx_log` (printf), 32-bit maths, division in C | count cycles: `break` at the start and end of a function, compare `cycles` in `position` |
| Wrong after the collision buffer is turned on | `tgi_setcollisiondetection(1)` uses `$A058-$C037` | link with `CFG=$(LYNX_SDK)/cfg/lynx-coll.cfg` |
| BRK / jump into nowhere | a bad function pointer or a stack imbalance in asm | `break condition="PC<$0200"`; `backtrace` |
| Works in the harness, not in a release build | code that depends on log side effects or timing | `./lynx build x RELEASE=1` then test |

## Timing a piece of code

```sh
./lynx ctl break addr=snd_tone
./lynx ctl continue
./lynx ctl delete
./lynx ctl position              # "cycles": N at the entry
./lynx ctl backtrace items=1     # entry 1's "return" is where the call comes back to
./lynx ctl run_to_addr addr=RETURN
./lynx ctl position              # the difference = the call's cost in CPU cycles
```

A 60 Hz frame is about 66,000 cycles, a 50 Hz one 80,000 (4 MHz CPU,
less what Suzy takes while drawing). [game-dev.md](game-dev.md) has a
cycle budget.
