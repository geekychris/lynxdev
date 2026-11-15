# Lynx resources

Where to read more. Every link was checked on 2026-11-15; `scripts/check_links.py`
re-checks every link in the docs (`--offline` checks only the links inside the repo).

## The hardware

| | |
|---|---|
| [Epyx / Atari Lynx developer documentation](https://www.monlynx.de/lynx/lynxdoc.html) | the original developer docs (hardware, Suzy, Mikey, sprites, sound, the system) as HTML, hosted by Bastian Schick |
| [Hardware addresses](https://www.monlynx.de/lynx/hardware.html) | every Suzy and Mikey register, one page |
| [Sprite engine](https://www.monlynx.de/lynx/sprite.html) | SCBs, packed/literal data, scaling, collision |
| [monlynx.de/lynx](https://www.monlynx.de/lynx/) | Bastian Schick's Lynx pages: docs, BLL, tools, links |
| [Atari Lynx Dev Wiki](https://atarilynxdev.net/) | the community wiki: hardware, tools, cartridges, tutorials |
| [Atari Lynx (Wikipedia)](https://en.wikipedia.org/wiki/Atari_Lynx) | history and specifications |

## The CPU: 65SC02

| | |
|---|---|
| [65C02 opcodes (6502.org)](https://6502.org/tutorials/65c02opcodes.html) | what CMOS adds to the 6502 (`BRA PHX PLX PHY PLY STZ TSB TRB`, `(zp)`); the Lynx's 65SC02 has these, not the Rockwell bit instructions or `WAI`/`STP` |
| [W65C02S datasheet (WDC)](https://www.westerndesigncenter.com/wdc/documentation/w65c02s.pdf) | instruction set, cycle counts |

## cc65: the compiler and its Lynx library

| | |
|---|---|
| [cc65](https://cc65.github.io/) / [source](https://github.com/cc65/cc65) | the C compiler, ca65 assembler, ld65 linker (lynxdev pins a commit: scripts/lib.sh) |
| [cc65 Lynx target](https://cc65.github.io/doc/lynx.html) | memory, the .lnx format, drivers (TGI, joystick, ComLynx), EEPROM, cart files |
| [TGI graphics](https://cc65.github.io/doc/tgi.html) | the graphics API the SDK's lynxgfx is built on |
| [cc65 compiler](https://cc65.github.io/doc/cc65.html), [ca65 assembler](https://cc65.github.io/doc/ca65.html), [ld65 linker](https://cc65.github.io/doc/ld65.html), [cl65](https://cc65.github.io/doc/cl65.html), [function reference](https://cc65.github.io/doc/funcref.html) | |
| [lynx.h](https://github.com/cc65/cc65/blob/master/include/lynx.h), [_suzy.h](https://github.com/cc65/cc65/blob/master/include/_suzy.h), [_mikey.h](https://github.com/cc65/cc65/blob/master/include/_mikey.h) | register structs, SCB types, colour names, TGI extras |
| [libsrc/lynx](https://github.com/cc65/cc65/tree/master/libsrc/lynx) | crt0, the TGI driver, the sound engine, the boot loader: read these to see what runs before `main` |
| [samples/lynx](https://github.com/cc65/cc65/tree/master/samples/lynx) | cc65's own Lynx examples |

## Emulators

| | |
|---|---|
| [libretro-handy](https://github.com/libretro/libretro-handy) | the Handy core lynxdev patches (emulator/README.md) |
| [Mednafen Lynx docs](https://mednafen.github.io/documentation/lynx.html) | another Handy-derived emulator; its notes on accuracy and keys |

## Other toolchains and tools

| | |
|---|---|
| [BLL (new_bll)](https://github.com/42Bastian/new_bll) | Bastian Schick's assembler kit: macros, libraries and many examples in 65SC02 asm |
| [BLL page](https://www.monlynx.de/lynx/bll.html), [tools](https://www.monlynx.de/lynx/tools.html) | |
| [lyxass](https://github.com/42Bastian/lyxass) | the BLL assembler |
| [sprpck](https://github.com/42Bastian/sprpck) | the classic sprite packer (lynxdev's `./lynx sprite` does the literal format in Python) |
| [lynx_hacking](https://github.com/42Bastian/lynx_hacking) | small Lynx programs and size-coding tricks |

## Community and tutorials

| | |
|---|---|
| [Atari Lynx developer (blog)](https://atarilynxdeveloper.wordpress.com/), [its programming tutorial](https://atarilynxdeveloper.wordpress.com/series/atarilynxprogrammingtutorial/) | a programming tutorial series for the Lynx with cc65 |
| [AtariAge Lynx programming forum](https://forums.atariage.com/forum/134-atari-lynx-programming/) | where Lynx homebrew developers are. (The site answers scripted checks with 403; open it in a browser) |
| [Atari Lynx Vault](https://atarilynxvault.com/) | games, homebrew, history |

## In this repo

| | |
|---|---|
| [install.md](install.md) | setting up |
| [getting-started.md](getting-started.md) | the tutorial |
| [game-dev.md](game-dev.md) | how a game is built, with the two ports |
| [sdk.md](sdk.md) | the SDK reference |
| [debugging.md](debugging.md) | the debugger |
| [lynx-gotchas.md](lynx-gotchas.md) | what bites |
| [emulator-harness.md](emulator-harness.md), [mcp.md](mcp.md), [../emulator/README.md](../emulator/README.md) | the emulator, the Python API, MCP and REST |
