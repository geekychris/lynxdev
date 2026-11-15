"""Session process: one Emulator + a JSON-lines control port + a registry entry.

    lynxdev serve projects/hello/build/hello.lnx [--window] [--mode running]

Clients (./lynx ctl, the MCP server, DevBench, scripts) send one JSON object per
line and get one back:

    {"id": 1, "cmd": "press", "args": {"keys": "SPACE"}}
    {"id": 1, "ok": true, "result": {...}}

Commands run one at a time (a lock), so any number of clients can share a
session. The emulator's own control port (emu_port in the registry) is the
patched Handy's harness protocol (emu_cmd("raw", line="help")).
"""
from __future__ import annotations

import base64
import io
import json
import os
import signal
import socket
import socketserver
import sys
import threading
import time
from pathlib import Path

from .emu import LOADABLE, Emulator, EmulatorError, project_dir, repo_root
try:
    from .emu import ProtocolError as ZrcpError   # the emulator link's error
except ImportError:  # pragma: no cover
    class ZrcpError(RuntimeError):
        pass

SESSIONS_DIR = repo_root() / "build" / "sessions"


def resolve_program(target: str | None) -> str | None:
    """Project name or file path -> loadable file (newest .lnx in build/)."""
    if not target:
        return None
    p = Path(target)
    if p.suffix.lower() in LOADABLE:
        if not p.exists():
            raise FileNotFoundError(f"{p} not found")
        return str(p.resolve())
    proj = project_dir(target)
    if proj is None:
        raise FileNotFoundError(f"no project {target!r} in projects/ or games/ "
                                "(games/ is the lynx_games submodule: git submodule update --init)")
    d = proj / "build"
    cands = [c for ext in (".lnx", ".lyx") for c in d.glob(f"*{ext}")]
    if not cands:
        raise FileNotFoundError(f"nothing built for project {target!r} (./lynx build {target})")
    return str(max(cands, key=lambda c: c.stat().st_mtime))


def _hexdump(addr: int, data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{(addr + i) & 0xFFFF:04X}  {chunk.hex(' '):<47}  {asc}")
    return "\n".join(lines)


class Session:
    def __init__(self, emu: Emulator, info: dict):
        self.emu = emu
        self.info = info
        self.lock = threading.RLock()
        self.quit = threading.Event()
        self.registry: Path | None = None

    # each command: (function, help)
    def commands(self):
        e = self.emu
        return {
            "help": (lambda: {k: v[1] for k, v in sorted(self.commands().items())}, "this list"),
            "status": (lambda: {**e.status(), "session": self.info}, "frame, pc, program, ports, signal"),
            "load": (lambda path: e.load(resolve_program(path)),
                     "load(path|project): power on + boot a .lnx through the HLE boot (or load a .lys state)"),
            "reset": (lambda hard=True: (e.reset(hard), e.status())[1],
                      "reset(): power-on with the same cartridge (the Lynx has no warm reset)"),
            "reload": (lambda: e.reload(), "power on + load the same program again"),
            "power_cycle": (lambda: e.power_cycle(), "fresh emulator process, no program"),
            "reboot": (lambda: e.reboot(), "kill + restart the emulator and reload the program (wedged session)"),
            "position": (lambda: e.position(), "frame, frames since load, cycles into the frame"),
            "run_to": (lambda frame, tstates=0: e.run_to(frame, tstates),
                       "run_to(frame, tstates=0): to an exact position (frames since load)"),
            "input_log": (lambda: {"events": e.input_log()}, "input changes since load, with positions"),
            "checkpoint": (lambda label="": e.checkpoint(label), "checkpoint(label): record the screen hash here"),
            "save_replay": (lambda path: {"path": path, **{k: v for k, v in e.save_replay(self._rel(path)).items() if k != "events"},
                                          "events": len(e.input_log())},
                            "save_replay(path): input log + checkpoints since load -> JSON"),
            "replay": (lambda path, verify=True: e.replay(self._rel(path), verify),
                       "replay(path, verify=true): restart from the recording's reset/snapshot, re-apply "
                       "inputs, compare per-frame screen hashes"),
            "record_input": (lambda action="start", path=None, from_reset=False:
                             e.record_input(action, self._rel(path) if path else None, from_reset),
                             "record_input(action=start|stop|save|status, path?, from_reset=false): "
                             "record agent + window input and per-frame hashes"),
            "screen_hash": (lambda layer="screen": {"md5": e.screen_hash(layer)}, "screen_hash()"),
            "record_video": (lambda path, frames=100, every=1, scale=1, layer="screen":
                             e.record_video(self._rel(path), frames, every, scale, layer),
                             "record_video(path .gif|.mp4|dir, frames=100, every=1, scale=1)"),
            "memory_map": (lambda: e.memory_map(), "MAPCTL: whether Suzy, Mikey, ROM and the vectors are mapped"),
            "rotate": (lambda value=None: e.set_rotate(value) if value else {"rotate": e.rotate},
                       "rotate(value): none, left, right (display only)"),
            "palette": (lambda: {"rgb": e.palette(), "raw": e.palette_raw()},
                        "the 16-colour Mikey palette: rgb tuples and the 12-bit register values"),
            "scb_chain": (lambda start=None, limit=64: {"scbs": e.scb_chain(start, limit)},
                          "scb_chain(start?, limit=64): decode the Suzy sprite chain (default: from SCBNEXT)"),
            "pause": (lambda: (e.pause(), e.status())[1], "stop free-running"),
            "resume": (lambda turbo=False: (e.resume(turbo), e.status())[1], "resume(turbo=false): free-run"),
            "run": (lambda frames=1: e.run(frames), "run(frames): advance N frames (stops at breakpoints)"),
            "run_until": (lambda text, max_frames=3000, since=None: {
                "found": e.run_until(text, max_frames, since), **e.last_run},
                "run_until(text, max_frames=3000, since?): run until the guest log contains text"),
            "log": (lambda since=None, all=False: {
                "text": "\n".join(e.debug_log) if all else e.log(since), "cursor": e.log_cursor()},
                "log(since?, all=false): guest log since the last log call"),
            "log_cursor": (lambda: {"cursor": e.log_cursor()}, "current end of the log"),
            "press": (lambda keys, hold_frames=3, release_frames=3: e.press(keys, hold_frames, release_frames),
                      "press(keys, hold_frames=3, release_frames=3): e.g. 'A', 'UP+B', 'OPTION1', 'PAUSE'"),
            "hold": (lambda keys: (e.hold(keys), {"held": e._held})[1], "hold(keys): press and keep held"),
            "release": (lambda keys=None: (e.release(keys), {"held": e._held})[1], "release(keys?): release (all)"),
            "pad": (lambda dirs=None: {"pad": e.pad(dirs)},
                    "pad(dirs?): set the d-pad and A/B to e.g. 'UP+A', '' = centre"),
            "sequence": (lambda steps: e.sequence(steps),
                         "sequence(steps): [{press,hold}, {run}, {until,max}, {pad}, {hold}, {release}]"),
            "screenshot": (self._screenshot,
                           "screenshot(path?, scale=1): 160x102 PNG (png_b64)"),
            "regs": (lambda: {**e.regs(), "pc_symbol": e.symbols.name_of(e.regs()["PC"])}, "CPU registers"),
            "set_reg": (lambda reg, value: e.set_reg(reg, value), "set_reg(reg, value)"),
            "read_mem": (self._read_mem,
                         "read_mem(addr, length=64, space=cpu|ram): hex + dump; addr may be a symbol; "
                         "space=ram: the raw RAM under Suzy/Mikey/ROM"),
            "write_mem": (self._write_mem, "write_mem(addr, hex, space=cpu|ram)"),
            "run_until_signal": (lambda max_frames=3000: e.run_until_signal(max_frames),
                                 "run_until_signal(max_frames=3000): run until the program writes a "
                                 "status byte to $FCF1 (lynx_signal/lynx_done=1/lynx_fail=2); status() has it"),
            "run_to_addr": (lambda addr, max_frames=500: e.run_to_addr(addr, max_frames),
                            "run_to_addr(addr|symbol, max_frames=500): run until PC = addr"),
            "hw": (lambda: e.hw(), "Suzy/Mikey registers as last written, the palette and MAPCTL"),
            "find_mem": (self._find_mem, "find_mem(hex|text, start=0, end=65536)"),
            "disasm": (lambda addr="pc", count=12: {"listing": "\n".join(e.disasm(addr, count))},
                       "disasm(addr=pc, count=12)"),
            "symbols": (lambda pattern="", limit=200: e.symbols.search(pattern, limit),
                        "symbols(pattern): from the program's .map"),
            "addr2sym": (lambda addr: {"addr": e.addr(addr), "symbol": e.symbols.name_of(e.addr(addr))},
                         "addr2sym(addr)"),
            "break": (lambda addr=None, condition=None: e.break_if(condition) if condition else e.break_at(addr),
                      "break(addr|condition): e.g. 'main', '$0400', condition='A=5 AND PC=$0400'"),
            "watch": (lambda addr, length=1, kind="w": e.watch(addr, length, kind),
                      "watch(addr, length=1, kind=r|w|rw)"),
            "delete": (lambda index=None, addr=None: (e.delete(index, addr), e.breakpoints())[1],
                       "delete(index?|addr?): no args = everything"),
            "breakpoints": (lambda: e.breakpoints(), "list breakpoints and watchpoints"),
            "continue": (lambda max_frames=500: e.continue_(max_frames),
                         "continue(max_frames=500): run until a breakpoint/watchpoint"),
            "stepi": (lambda count=1: e.stepi(count), "stepi(count=1)"),
            "step_over": (lambda: e.step_over(), "one instruction; a JSR runs until it returns"),
            "backtrace": (lambda items=8: e.backtrace(items), "backtrace(items=8): stack words + symbols"),
            "save_state": (lambda path: {"path": e.save_state(self._state_path(path))},
                           "save_state(path|name): Handy state (.lys) + .lynxh sidecar"),
            "load_state": (lambda path: e.load_state(self._state_path(path)), "load_state(path|name)"),
            "raw": (lambda line, timeout=30: {"output": e.raw(line, timeout)},
                    "raw(line): a raw harness-protocol command to the patched Handy (raw('help'))"),
            "quit": (self._quit, "stop the session and the emulator"),
        }

    def _rel(self, p) -> str:
        """Relative paths are relative to the repo root (sessions have no useful cwd)."""
        return str(p if Path(p).is_absolute() else repo_root() / p)

    def _state_path(self, p) -> str:
        p = Path(p)
        return str(p if p.is_absolute() or p.parent != Path(".") else repo_root() / "build" / "states" / p)

    def _screenshot(self, path=None, border=None, scale=1, layer="screen"):
        path = self._rel(path) if path else None
        img = self.emu.screenshot(path, scale=scale)
        buf = io.BytesIO()
        img.save(buf, "PNG")
        return {"png_b64": base64.b64encode(buf.getvalue()).decode(), "width": img.width,
                "height": img.height, "path": path, "colors": len(img.getcolors(1 << 16) or [])}

    def _read_mem(self, addr, length=64, space="cpu"):
        a = self.emu.addr(addr)
        data = self.emu.read_mem(a, int(length), space=space)
        return {"addr": a, "space": space, "hex": data.hex(), "dump": _hexdump(a, data)}

    def _write_mem(self, addr, hex, space="cpu"):
        data = bytes.fromhex(hex.replace(" ", ""))
        a = self.emu.addr(addr)
        self.emu.write_mem(a, data, space=space)
        return {"addr": a, "space": space, "written": len(data)}

    def _find_mem(self, hex=None, text=None, start=0, end=0x10000):
        needle = bytes.fromhex(hex.replace(" ", "")) if hex else text.encode("latin-1")
        return {"matches": [{"addr": a, "symbol": self.emu.symbols.name_of(a)}
                            for a in self.emu.find_mem(needle, self.emu.addr(start), int(end))]}

    def _quit(self):
        self.quit.set()
        return {"bye": True}

    def call(self, cmd: str, args: dict):
        table = self.commands()
        if cmd not in table:
            raise ValueError(f"unknown command {cmd!r} (try help)")
        if cmd == "reboot" and not self.lock.acquire(timeout=3):
            # another command is stuck inside the emulator: kill it so that
            # command fails and lets go of the lock, then restart
            try:
                self.emu.proc.kill()
            except OSError:
                pass
            self.lock.acquire()
        elif cmd != "reboot":
            self.lock.acquire()
        try:
            try:
                return table[cmd][0](**(args or {}))
            finally:
                # keep the registry current (the emulator may restart, e.g. reboot)
                now = {"emu_port": self.emu.port, "rotate": self.emu.rotate,
                       "emulator_pid": self.emu.proc.pid, "program": self.emu.program}
                if any(self.info.get(k) != v for k, v in now.items()):
                    self.info.update(now)
                    if self.registry:
                        self.registry.write_text(json.dumps(self.info))
        finally:
            self.lock.release()


class _Handler(socketserver.StreamRequestHandler):
    def handle(self):
        sess: Session = self.server.session
        for raw in self.rfile:
            try:
                req = json.loads(raw)
            except json.JSONDecodeError as ex:
                self._send({"ok": False, "error": f"bad json: {ex}"})
                continue
            rid = req.get("id")
            try:
                res = sess.call(req.get("cmd", ""), req.get("args") or {})
                self._send({"id": rid, "ok": True, "result": res})
            except (EmulatorError, ZrcpError, ValueError, TypeError, KeyError, FileNotFoundError) as ex:
                self._send({"id": rid, "ok": False, "error": f"{type(ex).__name__}: {ex}"})
            if sess.quit.is_set():
                return

    def _send(self, obj):
        self.wfile.write((json.dumps(obj) + "\n").encode())
        self.wfile.flush()


class _Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
    daemon_threads = True
    allow_reuse_address = True


def serve(program: str | None = None, rotate: str | None = None, window: bool = False,
          mode: str | None = None, port: int = 0, zoom: int | None = None, quiet: bool = False):
    paused = (mode or ("running" if window else "paused")) == "paused"
    SESSIONS_DIR.mkdir(parents=True, exist_ok=True)
    for old in sorted(SESSIONS_DIR.glob("lynxh-*.log"), key=lambda p: p.stat().st_mtime)[:-20]:
        old.unlink(missing_ok=True)   # keep the 20 newest emulator logs
    emu = Emulator(window=window, paused=True, zoom=zoom, rotate=rotate or "none",
                   log_path=SESSIONS_DIR / f"lynxh-{os.getpid()}.log")
    if program:
        emu.load(program)
    if not paused:
        emu.resume()
    srv = _Server(("127.0.0.1", port), _Handler)
    info = {"pid": os.getpid(), "control_port": srv.server_address[1], "emu_port": emu.port,
            "program": program, "rotate": emu.rotate, "window": window,
            "mode": "paused" if paused else "running", "started": time.time(),
            "emulator_pid": emu.proc.pid}
    sess = Session(emu, info)
    srv.session = sess
    reg = SESSIONS_DIR / f"{os.getpid()}.json"
    reg.write_text(json.dumps(info))
    sess.registry = reg
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print("LYNXDEV-SESSION " + json.dumps(info), flush=True)
    if not quiet:
        print(f"session pid {info['pid']}: control :{info['control_port']}  emulator :{info['emu_port']}  "
              f"({'window' if window else 'headless'}, {info['mode']})", file=sys.stderr, flush=True)

    def _stop(*_):
        sess.quit.set()
    signal.signal(signal.SIGTERM, _stop)
    signal.signal(signal.SIGINT, _stop)
    try:
        # exit when told to, or when the emulator goes away (window closed)
        while not sess.quit.wait(0.5):
            # (under the lock: reboot restarts the emulator inside a command)
            if sess.lock.acquire(blocking=False):
                gone = not emu.alive
                sess.lock.release()
                if gone:
                    break
    finally:
        reg.unlink(missing_ok=True)
        srv.shutdown()
        with sess.lock:
            emu.close()
