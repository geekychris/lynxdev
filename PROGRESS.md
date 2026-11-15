# Progress

## Milestone 1 (done 2026-11-15): hello builds, boots under HLE, logs, test passes, screenshot, MCP green

- [x] Repo, MIT licence, private GitHub repo geekychris/lynxdev
- [x] Toolchain image lynx-dev-toolchain:1 (cc65 eebf54a, debian bookworm, native arm64/amd64);
      `bin/lynx-make` (Docker, or LYNX_NATIVE=1)
- [x] SDK 1.0: lynxlog.h, gfx (TGI), frame counter, palette, text, Suzy sprites, input,
      Mikey sound, rnd; project.mk; template; projects/hello, projects/sdktest
- [x] `./lynx` CLI, tools/lynxdev (Emulator, sessions, CLI, MCP server "lynx",
      DevBench :3690 with web UI + REST + MCP over HTTP), sprite converter
- [x] Emulator (lynx-emu, PR #2): Handy bc55d46 + lynxh harness, headless + SDL, HLE boot
- [x] `./lynx selftest` green (hello, sdktest, test_emulator, test_remote 55, test_template)
- [x] Docs: README, CLAUDE.md, getting-started, sdk, emulator-harness, mcp, lynx-gotchas

## Next

- [x] frank_the_frog (lynx-port-frog): projects/frank_the_frog, bot test (level 2), 50 fps
- [ ] Game ports: rock_blaster (projects/<name>, test.py, README, screenshot)
- [ ] SDK: small font, Suzy maths helpers, sprite collision helpers, music
