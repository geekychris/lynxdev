# Harness, sessions and MCP

```
Claude Code ──stdio──> lynxdev-mcp ─┐
browser ─────HTTP────> devbench ────┼──JSON lines──> session process ──lynxh protocol──> patched Handy
./lynx ctl / scripts ───────────────┘   (lynxdev serve)    (one Emulator)
```

- **Emulator** (`lynxdev.emu.Emulator`): starts the patched Handy headless or
  in a window and drives it over its harness protocol
  (docs/emulator-harness.md): frames, the guest log, input, screenshots,
  memory, Suzy/Mikey, debugger, states, recording.
- **Session** (`./lynx serve`, `./lynx run`, `emu_boot`): one process owning
  one Emulator, with a JSON-lines control port
  (`{"id":1,"cmd":"press","args":{"keys":"A"}}`) and a registry file in
  `build/sessions/`. Many clients can share it; commands run one at a time.
  `./lynx sessions` lists them; `./lynx ctl help` lists commands.
- **MCP server** (`.venv/bin/lynxdev-mcp`, server `lynx` in .mcp.json, stdio):
  the tools below, on the newest session or the one you `emu_boot`/`emu_attach`.
- **DevBench** (`./lynx devbench`, :3690): web UI + REST (`/api/...`) + the
  same MCP over streamable HTTP at `http://127.0.0.1:3690/mcp`. To use that
  instead of stdio: `{"type": "http", "url": "http://127.0.0.1:3690/mcp"}`.

## Tools

| Group | Tools |
|---|---|
| projects | `list_projects`, `build(project, clean)`, `new_project(name)`, `run_tests(project)` |
| sessions | `emu_boot(project, window, rotate)`, `emu_attach(pid)`, `emu_sessions`, `emu_close`, `emu_status` |
| machine | `emu_load(path)`, `emu_reset`, `emu_reload`, `emu_power_cycle`, `emu_reboot`, `emu_rotate(value)` |
| time | `emu_run(frames)`, `emu_run_until(text, max_frames)`, `emu_run_until_signal`, `emu_run_to_addr(addr)`, `emu_run_to(frame, tstates)`, `emu_position`, `emu_pause`, `emu_resume(turbo)` |
| log | `emu_log(all)` |
| screen | `emu_look(scale)`, `emu_screenshot(path, scale)`, `emu_screen_hash`, `emu_record_video(path)`, `emu_record_gif` |
| input | `emu_press(keys)`, `emu_hold`, `emu_release`, `emu_pad(dirs)`, `emu_sequence(steps)` |
| replay | `emu_record_input(action, path, from_reset)`, `emu_replay(path)`, `emu_input_log`, `emu_checkpoint`, `emu_save_replay` |
| debugger | `emu_break(addr, condition)`, `emu_watch(addr, length, kind)`, `emu_delete`, `emu_continue(max_frames)`, `emu_stepi`, `emu_step_over`, `emu_regs`, `emu_set_reg`, `emu_disasm`, `emu_backtrace`, `emu_symbols` |
| memory + chips | `emu_read_mem(addr, length, space)`, `emu_write_mem(addr, hex, space)`, `emu_find_mem`, `emu_memory_map` (MAPCTL), `emu_hw` (Suzy/Mikey), `emu_palette`, `emu_scb_chain(start)` |
| state | `emu_save_state(name)`, `emu_load_state(name)` |
| anything | `emu_cmd(cmd, args)` (`emu_cmd("help")`; `emu_cmd("raw", {"line": "help"})` reaches the emulator) |

Buttons: `UP DOWN LEFT RIGHT A B OPTION1 OPTION2 PAUSE`, combined with `+`.
Addresses take numbers, `$hex`, `0x..`, symbols from the `.lbl`/`.map`
(`main` or `_main`, `buf+16`) and register names.

## REST (DevBench)

| | |
|---|---|
| `GET /` | the web UI: screen, run controls, pad, log, registers, disassembly, memory, Suzy/Mikey registers, palette, sprite chain, console |
| `GET /api/health`, `GET /api/commands` | |
| `GET /api/projects`, `POST /api/projects/{name}/build`, `POST /api/projects/{name}/test` | projects/ and games/ (`dir` says which) |
| `GET/POST /api/sessions` | list / start `{"project", "window", "rotate"}` |
| `DELETE /api/sessions/{pid}`, `POST /api/sessions/{pid}/select` | |
| `GET/POST /api/cmd/{command}` | any session command (JSON body or `?k=v`), on the selected session |
| `/api/sessions/{pid}/cmd/{command}` | on a given session |
| `GET /api/screen.png?scale=3` | the current frame |

```sh
curl -s localhost:3690/api/cmd/run_until -d '{"text":"hello: ready"}'
curl -s localhost:3690/api/cmd/hw | jq .suzy
curl -s 'localhost:3690/api/screen.png?scale=4' -o shot.png
```

## Writing tests

A project's `test.py` drives an `Emulator` directly (sdk/template/test.py,
projects/sdktest/test.py):

```python
with Emulator() as emu:
    emu.load(PROG)
    assert emu.run_until("game: ready", 600)
    emu.press("PAUSE")
    assert emu.run_until("game: level 1", 100)
    img = emu.screenshot(HERE / "build" / "test.png")
    pal = emu.palette()
    assert img.getpixel((10, 10)) == pal[15]       # pen 15 there
```

Without a test.py, `./lynx test` passes when the program logs `LYNX:DONE`
(`lynx_done()`) and fails on `LYNX:FAIL` (`lynx_fail("why")`) or a timeout.
Tests write only under `build/` (selftest checks).
