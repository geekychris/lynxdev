# Getting started: from nothing to your own Lynx game

This walks you through installing the tools, playing the games, then making,
running, testing, inspecting and debugging a game of your own. Allow an
hour. You need to know some C; you don't need to know the Lynx yet.

## 1. Install

```sh
gh api -H "Accept: application/vnd.github.raw" repos/geekychris/lynxdev/contents/scripts/install.sh | bash
```

(While the repos are private you need the GitHub CLI logged in; see
[install.md](install.md) for the curl variant, SSH, manual install per OS
and troubleshooting.) It clones to `~/lynxdev` with the games in `games/`,
builds the toolchain image and the emulator, runs the selftest and opens the
games menu. From now on, work in that directory:

```sh
cd ~/lynxdev
./lynx doctor          # everything green?
```

## 2. Play the games

```sh
./lynx arcade
```

```
Lynx arcade  (2 games)
  1  Frank the Frog  Hop Frank across five lanes of traffic and a river of logs…
  2  Rock Blaster    A vector rock shooter.

number = play, i<number> = info/controls, r = rescan, q = quit >
```

Type `i2` for a game's controls, `2` to build it and play it in a window.
**Window keys:** arrows = d-pad, Z = A (outer button), X = B (inner),
1 / 2 = OPTION 1 / 2, P or Return = PAUSE, Esc closes the window and
returns to the menu. `q` leaves the arcade. `./lynx arcade --list` lists
the games without the menu; `--all` adds the platform's own test programs.

## 3. Make your game

```sh
./lynx new --game mygame
```

That copies `sdk/template` to `games/mygame/`:

```
games/mygame/Makefile     NAME := mygame + include the SDK's project.mk
games/mygame/src/main.c   the game
games/mygame/test.py      a headless test
```

`games/` is the [lynx_games](https://github.com/geekychris/lynx_games) repo,
where the games live (see "Sharing it" below). For experiments that aren't
games, `./lynx new scratch` puts them in `projects/` instead.

Open `games/mygame/src/main.c`. The whole program is a loop that runs once
per frame (60 times a second):

```c
#include "lynxgame.h"

int main(void)
{
    uint8_t x = 76, y = 47, c = COLOR_YELLOW;

    gfx_init();                               /* 160x102 screen, 60 Hz, default palette */
    lynx_log("mygame: ready\n");              /* a line the tests (and you) can wait for */

    for (;;) {
        input_update();                       /* input_held / input_new */
        if ((input_held & IN_LEFT) && x > 0) --x;
        ...
        if (input_new & IN_A) { ...; lynx_log("mygame: colour c=%u frame=%u\n", c, frame_count); }
        gfx_clear(COLOR_DARKGREY);            /* draw into the back buffer... */
        gfx_text(4, 2, COLOR_WHITE, "mygame");
        gfx_rect(x, y, 8, 8, c);
        gfx_present();                        /* ...and show it at the next vertical blank */
    }
}
```

## 4. Build and run it

```sh
./lynx build mygame      # -> games/mygame/build/mygame.lnx (+ .map, .lbl symbols)
./lynx run mygame        # rebuilds if needed, opens a window
```

Move the box with the arrows, Z changes its colour. Close the window with
Esc. The terminal shows the window's session (other tools can attach to it,
see step 8).

## 5. Add a sprite

Draw it as text: `.` is transparent, `0`-`9` `A`-`F` are colours of the
default palette (`F` white, `9` yellow, `2` red, ...; the full list is in
[sdk.md](sdk.md)).

```sh
mkdir -p games/mygame/art
cat > games/mygame/art/ship.txt <<'EOF'
...FF...
..F99F..
.F9999F.
F992299F
.2....2.
EOF
./lynx sprite games/mygame/art/ship.txt -o games/mygame/src/ship.c
```

Then in `main.c`:

```c
#include "ship.h"
static SCB_REHV_PAL ship;                   /* a Suzy sprite control block: keep it static */
...
spr_init(&ship, ship_data, SHIP_BPP);       /* once */
...
spr_pos(&ship, x, y);                       /* every frame, after gfx_clear */
spr_draw(&ship);
```

`spr_scale(&ship, SPR_SCALE_2, SPR_SCALE_2)` doubles it in hardware;
`spr_flip(&ship, 1, 0)` mirrors it. PNGs work too
(`./lynx sprite ship.png -o ...`). [game-dev.md](game-dev.md) explains
sprite chains, sound and the frame budget.

## 6. Test it

```sh
./lynx test mygame
```

That runs `games/mygame/test.py`: the emulator, headless and paused,
boots the game, waits for its log lines and presses buttons:

```python
with Emulator() as emu:
    emu.load(PROG)
    check("boots to ready", emu.run_until("mygame: ready", 600))
    emu.press("RIGHT", hold_frames=1)
    check("joypad moves right", emu.run_until("mygame: move x=77", 20))
    emu.press("A")
    check("A changes colour", emu.run_until("mygame: colour", 20))
    img = emu.screenshot(HERE / "build" / "test.png")
```

Runs are deterministic: the same inputs give the same frames every time,
much faster than real time. Log what your game does
(`lynx_log("mygame: score=%u\n", score)`) and assert on it.

## 7. Screenshots and videos

```sh
./lynx shot mygame -o games/mygame/images/play.png --until "mygame: ready" --press A@30 --scale 3
./lynx video mygame -o build/mygame.gif --until "mygame: ready" --frames 180 --hold RIGHT@10 --release ALL@90
./lynx log mygame --frames 300 --press A@100      # just the log
```

`@N` is a frame number counted from the load.

## 8. Look inside: DevBench

```sh
./lynx devbench --open        # http://127.0.0.1:3690/
```

Pick `mygame`, **Boot headless** (or **Boot in window**), then step frames,
press buttons, watch the log, set breakpoints, read memory, look at the
Suzy/Mikey registers, the palette and the sprite chain Suzy drew. The same
things are a REST API (`curl localhost:3690/api/cmd/regs`) and a command line:

```sh
./lynx run mygame &           # a window session
./lynx ctl status             # talks to the newest session
./lynx ctl break addr=main
./lynx ctl continue
./lynx ctl regs
```

[debugging.md](debugging.md) covers breakpoints, watchpoints, stepping,
memory, Suzy/Mikey views, record and replay.

## 9. With Claude Code

Open the repo with `claude`. The MCP server "lynx" (in `.mcp.json`) gives
Claude the same tools: `build("mygame")`, `emu_boot("mygame")`,
`emu_run_until("mygame: ready")`, `emu_look()` (a screenshot it can see),
`emu_press("A")`, `emu_break("main")`, `emu_hw()`, `emu_scb_chain()`: 60
tools in all ([mcp.md](mcp.md)). Ask it to add a feature and test it; it
builds, boots, plays and checks the screen itself. `CLAUDE.md` holds the
rules it follows.

## 10. Before you call it done

```sh
./lynx selftest       # builds and tests every project and game
```

## 11. Sharing it

Your game lives in the lynx_games repo (`games/` is a checkout of it):

```sh
cd games
git checkout -b mygame
git add mygame && git commit -m "mygame: first version"
git push -u origin mygame && gh pr create
```

After it's merged, bump the submodule in lynxdev
(`cd games && git checkout main && git pull && cd .. && git add games`)
and add a row to the README's Games table. Details: "Porting into
lynx_games" in CLAUDE.md.

## Where next

| | |
|---|---|
| [game-dev.md](game-dev.md) | how a Lynx game is put together: frame loop, sprite chains, erasing, sound, fixed point, asm, cycle budgets, with Frank the Frog and Rock Blaster as examples |
| [sdk.md](sdk.md) | every SDK function |
| [debugging.md](debugging.md) | finding bugs |
| [lynx-gotchas.md](lynx-gotchas.md) | what bites |
| [resources.md](resources.md) | the Lynx hardware docs, cc65, the community |
