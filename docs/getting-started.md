# Getting started

## 1. Set up

```sh
git clone --recurse-submodules https://github.com/geekychris/lynxdev && cd lynxdev
./setup.sh            # games/ submodule, toolchain image, emulator, Python env, then ./lynx selftest
./lynx doctor         # anything missing, with the command that fixes it
```

The games are in their own repo (geekychris/lynx_games), checked out as the
`games/` submodule; setup.sh fetches it (`git submodule update --init` by
hand). Without it everything else works and selftest says the games weren't
tested. `./setup.sh --quick` skips the final selftest. Everything is idempotent: run
it again after pulling.

## 2. Your first project

```sh
./lynx new mygame     # projects/mygame from sdk/template (--game: games/mygame, the lynx_games repo)
./lynx run mygame     # builds, then opens a window (arrows move the box, Z = A)
./lynx test mygame    # headless: runs projects/mygame/test.py
```

A project is a directory with a two-line Makefile and `src/`:

```make
NAME     := mygame
LYNX_SDK ?= ../../sdk
include $(LYNX_SDK)/project.mk
```

Every `src/*.c` and `src/*.s` is compiled and linked with the SDK library
into `build/mygame.lnx`, a cartridge image with cc65's boot loader. The
build also writes `build/mygame.map` (segments and sizes), `.lbl` (symbols
for the debugger) and `.dbg`.

## 3. The main loop

```c
#include "lynxgame.h"

int main(void)
{
    gfx_init();                           /* 160x102, 60 Hz, default palette */
    lynx_log("mygame: ready\n");
    for (;;) {
        input_update();                   /* input_held, input_new */
        if (input_new & IN_A) lynx_log("mygame: fire\n");
        gfx_clear(COLOR_BLACK);
        /* ... draw sprites, text ... */
        gfx_present();                    /* swap at the vertical blank: the frame clock */
    }
}
```

docs/sdk.md lists everything the SDK offers.

## 4. Sprites

Draw the art as text (or a PNG):

```
# art/ship.txt: '.' = transparent, 0-9 A-F = pens (colours)
...FF...
..F99F..
.F9999F.
F92..29F
```

```sh
./lynx sprite art/ship.txt -o projects/mygame/src/ship.c   # writes ship.c + ship.h
```

```c
#include "ship.h"
static SCB_REHV_PAL ship;                 /* a Suzy sprite control block */
spr_init(&ship, ship_data, SHIP_BPP);
spr_pos(&ship, x, y);
spr_scale(&ship, SPR_SCALE_2, SPR_SCALE_2);   /* hardware scaling */
spr_draw(&ship);
```

## 5. Make it testable

Log what happens (`lynx_log("mygame: score=%u\n", score)`) and drive it
from `test.py`:

```python
with Emulator() as emu:
    emu.load(HERE / "build" / "mygame.lnx")
    assert emu.run_until("mygame: ready", 600)
    emu.press("A")
    assert emu.run_until("mygame: fire", 10)
    emu.screenshot(HERE / "build" / "test.png")
```

Headless sessions are paused and deterministic: the same inputs give the
same frames, every time. Without a test.py, `./lynx test` passes when the
program calls `lynx_done()` and fails on `lynx_fail("why")`.

## 6. With Claude Code

Open the repo with `claude`. The "lynx" MCP server (.mcp.json) gives Claude
`build`, `emu_boot`, `emu_run_until`, `emu_look`, `emu_press`, the debugger
(`emu_break`, `emu_watch`, `emu_stepi`, ...), Suzy/Mikey inspection
(`emu_hw`, `emu_palette`, `emu_scb_chain`) and more: docs/mcp.md.

`./lynx devbench --open` shows the same in a browser (screen, log,
registers, memory, Suzy/Mikey registers, palette, sprite chain) on :3690.

## 7. Before you say "done"

```sh
./lynx selftest       # builds and tests every project + the harness
```
