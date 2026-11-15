# Atari Lynx development workspace (lynxdev)

Homebrew for the Atari Lynx (65SC02 @ 4 MHz, 64 KB RAM, Suzy sprite engine
with hardware scaling, Mikey timers/audio/palette, 160x102 LCD with 16 of
4096 colours). C and asm built with cc65 in Docker; runs in a patched Handy
(libretro-handy) with an HLE boot, so no copyrighted boot ROM is needed,
scriptable deterministically.

## Commands

```sh
./lynx build <project>      # -> <dir>/build/<p>.lnx + .map + .lbl (symbols) + .dbg
./lynx test <project>       # <dir>/test.py if present, else waits for LYNX:DONE / LYNX:FAIL
                            # <dir> = projects/<p> (platform: hello, sdktest) or games/<p> (the games)
./lynx log <project> --frames 600 [--press A@400]
./lynx shot <project> -o out.png --until "ready" [--press PAUSE@100] [--scale 3]
./lynx video <project> -o out.gif|.mp4 --until "ready" --frames 150 [--press A@200 --hold LEFT@220 --release ALL@260]
./lynx record <project> -o r.json --press A@200   # input recorder + per-frame hashes
./lynx replay r.json --runs 2   # re-run it twice: every frame hash must match
./lynx run <project>        # window for the human (free-running; registered session)
./lynx serve <project>      # headless paused session (prints ports)
./lynx ctl <cmd> key=value  # remote-control the newest session (./lynx ctl help)
./lynx sprite art/x.txt -o <dir>/src/x.c   # PNG / text art -> Suzy sprite data
./lynx new <name>           # projects/<name> from sdk/template
./lynx new --game <name>    # games/<name> (a game: lives in the lynx_games repo)
./lynx selftest             # build + test everything (do this before saying "done"), games/ too
                            # when the submodule is checked out (it says so when it isn't);
                            # fails if a test changed a tracked file (write output to build/)
./lynx doctor               # environment problems
./lynx devbench             # web UI + REST (/api) + MCP over HTTP (/mcp) on :3690
```

Builds run in Docker (image `lynx-dev-toolchain:N`, cc65 pinned in
scripts/lib.sh); `LYNX_NATIVE=1` builds with a cc65 on PATH instead.

## Two repos: the platform and the games

- **lynxdev** (this repo): SDK, toolchain, emulator, `./lynx`, harness, MCP, DevBench,
  docs, and the platform's own test programs in `projects/` (hello, sdktest, template).
- **lynx_games** (github.com/geekychris/lynx_games, private): every game, one directory
  each, checked out here as the **`games/` submodule** (`git submodule update --init`;
  setup.sh does it). Same layout as amiga_mcp + its `examples/` submodule (amiga_games).

All tooling finds a project by name in `projects/` then `games/`: `./lynx build|test|run|
shot|video|log <name>`, devbench `/api/projects`, MCP `build`/`emu_boot`.

## Porting into lynx_games

- **Where a game lives**: `games/<game>/` = `<game>/` at the root of lynx_games.
  `./lynx new --game <game>` makes one from the template. Makefile: `NAME := <game>`,
  `LYNX_SDK ?= ../../sdk`, `include $(LYNX_SDK)/project.mk` (games/<game> is two levels
  below the lynxdev root, like projects/<p>). test.py / tools find the lynxdev root by
  walking up to the directory with `lynx` and `sdk/` (games/rock_blaster/test.py
  `_lynxdev_root()`). Pictures in `<game>/images/`; outputs in `<game>/build/`.
- **Test**: `./lynx build <game>`, `./lynx test <game>`, `./lynx shot <game> -o
  games/<game>/images/play.png`, MCP `build("<game>")` + `emu_boot("<game>")`; then
  `./lynx selftest` (runs every game).
- **PRs go to geekychris/lynx_games**: branch inside `games/`, commit, push, PR there,
  merge when the game's test and `./lynx selftest` pass. **Then bump the submodule
  pointer in lynxdev** (`cd games && git checkout main && git pull && cd .. && git add
  games`), add the game's row to README's Games table (thumbnail in
  docs/images/games/), PR to lynxdev.
- A game in a worktree of lynxdev: worktrees don't share submodule checkouts; run
  `git submodule update --init` in the worktree first.

## MCP tools (server "lynx", .mcp.json)

Inner loop: `build` -> `emu_boot(project)` -> `emu_run_until("<your log line>")` ->
`emu_look` / `emu_press` / `emu_log`. `emu_boot(p)` = private headless session
(paused: frames advance only on run/press/continue); `emu_boot(p, window=True)` =
user-visible, free-running; `emu_attach()` = join `./lynx run X`. Also
`emu_run(frames)`, `emu_hold`/`emu_release`, `emu_pad(dirs)`, `emu_sequence`,
`emu_pause`/`emu_resume`, `emu_load`, `emu_rotate`, `emu_save_state`/`emu_load_state`,
`emu_screenshot(path)`. Restart: `emu_reset`, `emu_reload`, `emu_power_cycle`,
`emu_reboot`. Screens: `emu_look`, `emu_record_video(path.gif|.mp4)`,
`emu_screen_hash`. Recording: `emu_record_input("start"|"stop"|"save", path,
from_reset)` + `emu_replay(path)`.

Buttons: `UP DOWN LEFT RIGHT A B OPTION1 OPTION2 PAUSE`, combined with `+`
(`UP+A`). A is the outer button, B the inner one.

Window keys (the user): arrows = d-pad, Z / Left Alt = A, X / Left Ctrl /
Cmd = B, 1 = OPTION1, 2 = OPTION2, P / Return = PAUSE, Esc quits.

Debugging: `emu_break("main")` (C names: `main` or `_main`), `emu_continue(max_frames)`,
`emu_watch("global", 2, "w")`, `emu_stepi`, `emu_step_over`, `emu_regs`, `emu_set_reg`,
`emu_disasm`, `emu_symbols`, `emu_read_mem("sym")`, `emu_write_mem`, `emu_find_mem`,
`emu_backtrace`, `emu_hw` (Suzy/Mikey registers), `emu_palette`, `emu_scb_chain`
(decode the sprite chain), `emu_memory_map` (MAPCTL), `emu_read_mem(addr, space="ram")`
(RAM under Suzy/Mikey/ROM), `emu_run_until_signal` (lynx_done=1/lynx_fail=2/lynx_signal),
`emu_run_to_addr`. `emu_cmd("help")` lists every session command.
Details: docs/mcp.md, docs/emulator-harness.md, emulator/README.md (raw protocol).

- Frames are display refreshes: 60 Hz by default (gfx_init), 50 or 75 with
  `gfx_set_rate`. `emu_status()["hz"]` reports the current rate.
- `emu_run_until` only matches output written after the last `emu_log` call, the last
  load/reset, or its own previous match.
- Make programs observable: log state changes with `lynx_log("tag: key=%u\n", v)`.

## Rules learned the hard way

- **Harness log channel**: one char per store to `$FCF0` (`LYNX_LOG_ADDR`), a status
  code to `$FCF1` (`LYNX_SIGNAL_ADDR`; 1 = DONE, 2 = FAIL), defined only in
  sdk/include/lynxlog.h. Unused Suzy space, ignored by a real Lynx. Release builds
  (`RELEASE=1`, `LYNX_RELEASE`, `NDEBUG`, `LYNX_NO_LOG`) compile every log call away.
  Never write these addresses directly.
- **No boot ROM**: never download, commit or ask for `lynxboot.img`. The HLE boot
  runs cc65's .lnx (its encrypted loader included). `LYNX_BIOS=/path` uses a real
  one if the user has it; tests must pass without.
- **Memory map** (sdk/cfg/lynx.cfg): program + data + BSS `$0200-$B837`, 2 KB C stack
  `$B838-$C037`, screen buffers `$C038` and `$E018` (8160 bytes each). The collision
  buffer (only with `tgi_setcollisiondetection(1)`) covers `$A058-$C037`: link with
  `CFG=$(LYNX_SDK)/cfg/lynx-coll.cfg` then.
- **Drawing**: everything goes through Suzy into the back buffer; `gfx_present()`
  swaps at the next vertical blank and waits for it: call it once per frame, it is
  the frame clock. Don't also call `frame_wait()` in the same frame.
- **Sprites** (sdk/include/lynxspr.h): SCBs must stay in RAM while Suzy draws (globals
  or statics, not stack temporaries you return from). Pen 0 is transparent; data
  from `./lynx sprite` (literal lines + a 0 pad byte each). A flipped sprite mirrors
  around its position (vflip draws upward from vpos).
- **Text**: `gfx_text` is TGI's 8x8 font (20 columns); `text_small` (lynxtext.h) is a
  3x5 Suzy font (40 columns) for HUDs. **Maths**: `math_mul16`/`math_div32` (lynxmath.h)
  use Suzy's hardware unit; much faster than cc65's `*` and `/` on variables.
- **Bars/boxes as sprites**: `spr_box()` (exact w x h). Scaling 1-bpp *literal* art makes
  bars ~8x too wide in background sprite types (each byte is 8 pixels).
- **Costs** (harness build): `lynx_log` ~25-30k cycles a line (vsnprintf), `lynx_log_s`
  ~20 a char; `snd_tone` ~1.3k, `snd_tone_p(ch, SND_PERIOD(hz), ...)` less. A 60 Hz frame
  is ~66k cycles.
- **Palette**: 12-bit, GREEN registers `$FDA0+n`, BLUERED `$FDB0+n`. `gfx_palette`
  takes 0xRGB, `gfx_palette_gbr` the registers' 0xGBR (what the sprite tool writes).
- cc65 printf: `%d/%u/%x` are 16-bit; use `%ld/%lu` with `long`. `lynx_log` output
  is cut at 127 characters.
- `-g` is always on: it only adds symbols to the .lbl/.dbg, not code.
- cc65 hot code: `#pragma static-locals (on)`; `uint8_t` indexes and byte arrays;
  entities as struct-of-arrays (`ent_x[i]`), not pointer-to-struct. Inner loops in
  asm or memcpy/memset. Suzy's hardware multiply/divide (MATHA..MATHP) is there for
  maths heavy code. docs/lynx-gotchas.md.
- Other agents and the user run emulators on this machine: sessions stop their own
  PID only (selftest greps for pkill/killall).
