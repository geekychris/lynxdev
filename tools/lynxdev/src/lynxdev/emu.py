"""Drive the patched Handy (the lynxh harness, emulator/).

    from lynxdev.emu import Emulator
    with Emulator() as emu:                          # headless, paused, deterministic
        emu.load("projects/hello/build/hello.lnx")
        assert emu.run_until("hello: ready", 600)
        emu.press("A")
        emu.break_at("_main"); print(emu.continue_(50))
        emu.screenshot("shot.png")

The emulator speaks a line protocol on a TCP port (127.0.0.1, chosen by the
emulator and printed on stdout as `LYNXH port=N pid=P`): `cmd key=value ...`
-> `OK key=value ...` or `ERR message`. Emulator.raw("help") lists the
commands; COORDINATION.md / emulator/README.md describe them. It is a8dev's
a8h protocol with the Atari 8-bit parts replaced by Lynx ones.

Headless sessions are paused: frames advance only on run/run_until/press/
continue/stepi, as fast as the host allows, and identically every time.
Window sessions free-run in real time; run/run_until fast-forward them
(the window keeps showing frames, the user's input keeps working) and then
let them go again. A breakpoint stops a free-running window too.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import tempfile
import time
from contextlib import contextmanager
from pathlib import Path

from .symbols import Symbols
from . import cpu6502

# ---------------------------------------------------------------- files

LOADABLE = (".lnx", ".lyx", ".o", ".lys")
STATE_EXT = ".lys"
ROTATIONS = ("none", "left", "right")
SCREEN_W, SCREEN_H = 160, 102


def repo_root() -> Path:
    env = os.environ.get("LYNX_ROOT")
    if env:
        return Path(env)
    here = Path(__file__).resolve()
    for p in here.parents:
        if (p / "lynx").exists() and (p / "sdk").is_dir():
            return p
    return here.parents[4]


def emulator_binary(window: bool = False) -> Path:
    env = os.environ.get("LYNX_EMULATOR_WINDOW" if window else "LYNX_EMULATOR")
    if env:
        return Path(env)
    return repo_root() / "third_party" / "handy" / "build" / ("lynxh-sdl" if window else "lynxh")


def bios_path() -> str | None:
    """An optional real boot ROM (never needed: the HLE boot is the default).
    Only LYNX_BIOS is consulted; nothing is searched for or downloaded."""
    p = os.environ.get("LYNX_BIOS")
    return p if p and Path(p).is_file() else None


class EmulatorError(RuntimeError):
    pass


class ProtocolError(EmulatorError):
    """The emulator answered ERR (or the link broke)."""


# ---------------------------------------------------------------- the line protocol

_KV = re.compile(r'(\w[\w.]*)=("(?:[^"\\]|\\.)*"|\S*)')


def parse_reply(line: str) -> dict:
    """'OK a=1 b="x y"' -> {'a': '1', 'b': 'x y'} (values stay strings)."""
    out = {}
    for k, v in _KV.findall(line):
        if v.startswith('"'):
            v = re.sub(r'\\(.)', lambda m: "\n" if m.group(1) == "n" else m.group(1), v[1:-1])
        out[k] = v
    return out


def _q(v) -> str:
    """Quote an argument value for the protocol."""
    s = str(v)
    if s == "" or any(c in s for c in ' "\\\n'):
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'
    return s


class Link:
    def __init__(self, port: int, timeout: float = 8.0):
        t0 = time.monotonic()
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=2)
                break
            except OSError as ex:
                if time.monotonic() - t0 > timeout:
                    raise ProtocolError(f"cannot connect to the emulator on :{port}: {ex}") from None
                time.sleep(0.05)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""

    def cmd(self, line: str, timeout: float = 60.0) -> str:
        self.sock.settimeout(timeout)
        try:
            self.sock.sendall(line.encode("latin-1") + b"\n")
            while b"\n" not in self.buf:
                chunk = self.sock.recv(1 << 20)
                if not chunk:
                    raise ProtocolError("the emulator closed the connection")
                self.buf += chunk
        except socket.timeout:
            raise ProtocolError(f"no answer to {line.split()[0]!r} within {timeout:.0f}s") from None
        except OSError as ex:
            raise ProtocolError(f"emulator link: {ex}") from None
        reply, self.buf = self.buf.split(b"\n", 1)
        text = reply.decode("latin-1")
        if text.startswith("ERR"):
            raise ProtocolError(text[4:])
        return text

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------- buttons

# The harness's button bits (`input buttons=HEX`): the JOYSTICK register $FCB0
# as cc65's _lynx.h reads it, plus PAUSE ($FCB1 bit 0) as bit 8. 1 = pressed.
BUTTONS = {"UP": 0x80, "DOWN": 0x40, "LEFT": 0x20, "RIGHT": 0x10, "OPTION1": 0x08, "OPTION2": 0x04,
           "B": 0x02, "A": 0x01, "PAUSE": 0x100}
BUTTON_ALIASES = {"OPT1": "OPTION1", "OPT2": "OPTION2", "1": "OPTION1", "2": "OPTION2", "FIRE": "A",
                  "OUTER": "A", "INNER": "B", "START": "PAUSE", "U": "UP", "D": "DOWN", "L": "LEFT",
                  "R": "RIGHT", "FIRE2": "B"}
DIRS = ("UP", "DOWN", "LEFT", "RIGHT")


def parse_keys(keys) -> list[str]:
    """'A' / 'UP+A' / ['LEFT', 'B'] -> normalised button names."""
    if keys is None:
        return []
    if isinstance(keys, str):
        parts = [p for p in re.split(r"\s*\+\s*|\s*,\s*", keys.strip()) if p]
    else:
        parts = [str(k) for k in keys]
    out = []
    for p in parts:
        n = p.strip().upper()
        n = BUTTON_ALIASES.get(n, n)
        if n not in BUTTONS:
            raise ValueError(f"unknown button {p!r} (UP DOWN LEFT RIGHT A B OPTION1 OPTION2 PAUSE; "
                             f"combine with +, e.g. 'UP+A')")
        out.append(n)
    return out


def buttons_mask(names: list[str]) -> int:
    m = 0
    for n in names:
        m |= BUTTONS[n]
    return m


def buttons_names(mask: int) -> list[str]:
    return [n for n, b in BUTTONS.items() if mask & b]


# ---------------------------------------------------------------- the emulator

def save_video(shots, path, fps: float, tmpdir=None) -> None:
    """PIL images -> .gif, .mp4/.mov/.webm (ffmpeg) or a directory of PNGs."""
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.suffix.lower() == ".gif":
        shots[0].save(p, save_all=True, append_images=shots[1:], loop=0,
                      duration=int(round(1000 / fps)), optimize=False)
    elif p.suffix.lower() in (".mp4", ".mov", ".webm"):
        ff = shutil.which("ffmpeg")
        if not ff:
            raise EmulatorError("ffmpeg not found: use a .gif path, or install ffmpeg")
        with tempfile.TemporaryDirectory(dir=tmpdir) as tmp:
            for i, im in enumerate(shots):
                im.save(Path(tmp) / f"{i:05d}.png")
            r = subprocess.run([ff, "-y", "-loglevel", "error", "-framerate", f"{fps:g}",
                                "-i", str(Path(tmp) / "%05d.png"), "-vf",
                                "scale=trunc(iw/2)*2:trunc(ih/2)*2:flags=neighbor",
                                "-pix_fmt", "yuv420p", str(p)], capture_output=True, text=True)
            if r.returncode:
                raise EmulatorError(f"ffmpeg failed: {r.stderr.strip()[-500:]}")
    else:
        p.mkdir(parents=True, exist_ok=True)
        for i, im in enumerate(shots):
            im.save(p / f"{i:05d}.png")


class Emulator:
    def __init__(self, window: bool = False, paused: bool | None = None, rotate: str = "none",
                 zoom: int | None = None, audio: bool | None = None, seed: int = 0,
                 extra_args=(), log_path=None, bios: str | None = None):
        self.rotate = rotate.lower()
        if self.rotate not in ROTATIONS:
            raise EmulatorError(f"rotate: {', '.join(ROTATIONS)}")
        self.window = window
        self.paused = (not window) if paused is None else paused
        self.program: str | None = None
        self.load_frame = 0
        self.symbols = Symbols()
        self.mark = 0
        self._logtext = bytearray()
        self._logbase = 0
        self._held: list[str] = []
        self._inputs: list[dict] = []
        self._checkpoints: list[dict] = []
        self._seq = 0
        self.last_stop: dict | None = None
        self.last_run: dict = {}
        self._bps: dict[int, dict] = {}
        self._watches: dict[int, dict] = {}
        self._next_id = 1
        self._recording: dict | None = None
        self.hz = 60.0
        self._tmp = Path(tempfile.mkdtemp(prefix="lynxdev-"))
        self.log_path = Path(log_path) if log_path else self._tmp / "lynxh.log"
        self.link: Link | None = None
        self.port = 0
        self.proc = None
        self._opts = {"zoom": zoom, "audio": audio, "seed": seed, "extra": list(extra_args),
                      "bios": bios or bios_path()}
        self._start()

    # ---------------------------------------------------- process

    def _args(self) -> list[str]:
        exe = emulator_binary(self.window)
        if not exe.exists():
            raise EmulatorError(f"emulator not built: {exe} (run scripts/build-emulator.sh)")
        o = self._opts
        args = [str(exe), "--port", "0", "--seed", str(int(o["seed"])), "--rotate", self.rotate,
                "--paused" if self.paused else "--free"]
        if self.window:
            args += ["--zoom", str(int(o["zoom"] or 4))]
            if o["audio"] is False:
                args.append("--nosound")
        else:
            args.append("--nosound")
        if o["bios"]:
            args += ["--bios", str(o["bios"])]
        return args + o["extra"]

    def _start(self):
        args = self._args()
        out = open(self.log_path, "ab")
        start = out.tell()
        self.proc = subprocess.Popen(args, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                     start_new_session=True, env={**os.environ, "LYNXH_PARENT_WATCH": "1"})
        out.close()
        t0 = time.monotonic()
        port = None
        while time.monotonic() - t0 < 15:
            text = self.log_path.read_bytes()[start:].decode("latin-1", "replace")
            m = re.search(r"LYNXH port=(\d+) pid=(\d+)", text)
            if m:
                port = int(m.group(1))
                if int(m.group(2)) != self.proc.pid:
                    self._kill()
                    raise EmulatorError("the emulator reported another pid")
                break
            if self.proc.poll() is not None:
                break
            time.sleep(0.02)
        if port is None:
            tail = self.log_path.read_bytes()[start:][-1500:].decode("latin-1", "replace")
            code = self.proc.poll()
            self._kill()
            raise EmulatorError(f"the emulator did not start (exit code {code}). Log:\n{tail}")
        self.port = port
        self.link = Link(port)
        st = self._kv("status")
        if st.get("pid") != str(self.proc.pid):
            self.link.close()
            self.link = None
            self._kill()
            raise EmulatorError(f"port {port} answered for pid {st.get('pid')}, not ours ({self.proc.pid})")
        self.mark = int(st["log"])

    def _kill(self):
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass

    def _stop_process(self):
        if self.link is not None:
            try:
                self.link.sock.sendall(b"quit\n")
            except OSError:
                pass
            self.link.close()
            self.link = None
        if self.proc is None:
            return
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    @property
    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def close(self):
        self._stop_process()
        shutil.rmtree(self._tmp, ignore_errors=True)

    def _cmd(self, line: str, timeout: float = 60.0) -> str:
        if self.link is None or not self.alive:
            raise EmulatorError(f"emulator has exited (log: {self.log_path})")
        return self.link.cmd(line, timeout=timeout)

    def _kv(self, line: str, timeout: float = 60.0) -> dict:
        return parse_reply(self._cmd(line, timeout))

    def raw(self, line: str, timeout: float = 30.0) -> str:
        """A raw harness-protocol command (raw('help') lists them)."""
        return self._cmd(line, timeout=timeout)

    @contextmanager
    def held(self):
        """Pause a free-running session for the duration (deterministic steps)."""
        if self.paused:
            yield
            return
        self._cmd("pause")
        try:
            yield
        finally:
            self._cmd("resume")

    def pause(self):
        """Stop free-running (frames then only advance on run/press/continue)."""
        self.paused = True
        self._cmd("pause")

    def resume(self, turbo: bool = False):
        """Free-run in real time (turbo=True: as fast as possible)."""
        self.paused = False
        self._cmd(f"resume turbo={1 if turbo else 0}")

    # ---------------------------------------------------- programs

    def load(self, path) -> dict:
        """Power on (RAM cleared, registers fixed) and boot a cartridge (.lnx)
        through the HLE boot (or the BIOS given with LYNX_BIOS), or restore a
        state (.lys). Symbols come from the .lbl/.map beside it."""
        p = Path(path).resolve()
        if not p.exists():
            raise EmulatorError(f"{p} not found")
        if p.suffix.lower() == STATE_EXT:
            return self.load_state(p)
        self._cmd(f"load path={_q(p)}")
        self.program = str(p)
        self.symbols = Symbols.load(p)
        self._after_reset()
        return self.status()

    def _after_reset(self):
        self.load_frame = 0
        self._inputs = []
        self._checkpoints = []
        self._held = []
        self._sync_log()
        self.mark = self._logbase + len(self._logtext)
        self._push_input(log=False)
        # breakpoints survive a load (re-armed in the emulator: ids are ours)

    def set_rotate(self, rotate: str) -> dict:
        """Display rotation: none, left or right (the screenshot follows it)."""
        r = rotate.lower()
        if r not in ROTATIONS:
            raise EmulatorError(f"rotate: {', '.join(ROTATIONS)}")
        self._cmd(f"rotate value={r}")
        self.rotate = r
        return self.status()

    def reset(self, hard: bool = True):
        """Power-on with the same cartridge re-booted (the Lynx has no warm reset)."""
        self._cmd(f"reset hard={1 if hard else 0}")
        self._after_reset()

    def reload(self) -> dict:
        if not self.program:
            raise EmulatorError("no program loaded")
        return self.load(self.program)

    def power_cycle(self) -> dict:
        """A fresh emulator process (no program)."""
        paused = self.paused
        self._stop_process()
        self.paused = True
        self._logtext, self._logbase = bytearray(), 0
        self._bps.clear()
        self._watches.clear()
        self._held = []
        self.program = None
        self.symbols = Symbols()
        self.load_frame = 0
        self._start()
        if not paused:
            self.resume()
        return self.status()

    def reboot(self) -> dict:
        """Recover a wedged emulator: kill it, start a fresh one, reload the program."""
        prog = self.program
        bps, watches = dict(self._bps), dict(self._watches)
        try:
            self.proc.kill()
        except OSError:
            pass
        paused = self.paused
        self.paused = True
        self.power_cycle()
        for b in bps.values():
            self._arm(b)
        for w in watches.values():
            self._arm(w)
        if prog:
            self.load(prog)
        if not paused:
            self.resume()
        return self.status()

    # ---------------------------------------------------- running

    _WHY = {"break": "breakpoint", "watch": "watchpoint", "step": "step", "frames": "frames",
            "frame_limit": "frame_limit", "log": "log_match", "at": "position", "to": "breakpoint",
            "signal": "signal", "cim": "cpu_jam"}

    def _stop_info(self, text: str) -> dict | None:
        """The emulator's last_stop (status) as a dict, without side effects."""
        r = parse_reply(text)
        if not r.get("why") or r["why"] == "none":
            return None
        pc = int(r.get("pc", "0"), 16)
        out = {"why": self._WHY.get(r["why"], r["why"]), "reason": r["why"], "pc": pc,
               "symbol": self.symbols.name_of(pc), "frame": int(r.get("frame", 0)),
               "since_load": int(r.get("frame", 0)) - self.load_frame, "cycles": int(r.get("cycles", 0)),
               "midframe": r.get("midframe") == "1"}
        if "line" in r:
            out["line"], out["xpos"] = int(r["line"]), int(r["xpos"])
        if "id" in r:
            i = int(r["id"])
            out["break"] = (self._bps.get(i) or self._watches.get(i) or {}).get("desc")
        return out

    def _result(self, r: dict) -> dict:
        res = {"reason": r.get("why"), "frames": int(r.get("ran", 0)), "frame": int(r.get("frame", 0)),
               "cycles": int(r.get("cycles", 0)), "pc": int(r.get("pc", "0"), 16),
               "midframe": r.get("midframe") == "1"}
        res["since_load"] = res["frame"] - self.load_frame
        res["why"] = self._WHY.get(res["reason"], res["reason"])
        if "line" in r:
            res["line"], res["xpos"] = int(r["line"]), int(r["xpos"])
        for k in ("id", "cond", "addr", "access", "insn_pc", "value", "old", "match", "signal", "opcode"):
            if k in r:
                res[k] = r[k]
        if "match" in res:
            res["match"] = int(res["match"])
        if "id" in res:
            i = int(res["id"])
            res["id"] = i
            res["break"] = (self._bps.get(i) or self._watches.get(i) or {}).get("desc")
        if r.get("fps"):
            res["fps"] = float(r["fps"])
            res["speed"] = float(r.get("speed", 0))
        res["regs"] = {"A": int(r.get("a", "0"), 16), "X": int(r.get("x", "0"), 16), "Y": int(r.get("y", "0"), 16),
                       "S": int(r.get("s", "0"), 16), "P": int(r.get("p", "0"), 16), "PC": res["pc"]}
        res["symbol"] = self.symbols.name_of(res["pc"])
        self.last_stop = {k: res.get(k) for k in ("why", "reason", "pc", "symbol", "break", "frame",
                                                   "since_load", "cycles", "line", "xpos", "midframe")}
        if res["why"] in ("breakpoint", "watchpoint", "cpu_jam"):
            self.paused = True        # a stop holds a free-running window too
        return res

    def _run(self, frames: int, timeout: float | None = None, **kw) -> dict:
        frames = max(1, int(frames))
        line = f"run frames={frames}" + "".join(f" {k}={_q(v)}" for k, v in kw.items() if v is not None)
        r = parse_reply(self._cmd(line, timeout=timeout or (30 + frames / 20)))
        return self._result(r)

    def run(self, frames: int = 1) -> dict:
        """Advance `frames` frames (stops early at a breakpoint: see ['why']).
        In a free-running window this fast-forwards, then real time resumes."""
        return self._run(frames)

    def run_until(self, text: str, max_frames: int = 3000, since: int | None = None) -> bool:
        """Run until the guest log contains `text` (written after `since`, default:
        after the last log()/reset/load/previous match). Stops on the instruction
        after the one that wrote its last byte. False if max_frames pass first
        (or a breakpoint stops it: see last_run). Text already written after
        `since` (e.g. during a press()) matches without running."""
        since = self.mark if since is None else since
        pat = text.encode("latin-1")
        self._sync_log()
        start = max(0, since - self._logbase)
        i = self._logtext.find(pat, start)
        if i >= 0:
            pos = self.position()
            self.mark = self._logbase + i + len(pat)
            self.last_run = {"reason": "log", "why": "log_match", "frames": 0, "already": True,
                             "match": self._logbase + i, **pos}
            return True
        # a match may straddle what is already there and what comes next
        since = max(since, self._logbase + len(self._logtext) - len(pat) + 1)
        r = self._run(max_frames, until=pat.hex(), since=since)
        self.last_run = r
        if r["reason"] == "log":
            self.mark = r["match"] + len(text.encode("latin-1"))
            return True
        return False

    def run_to(self, since_load: int, tstates: int = 0) -> dict:
        """Run to frame `since_load` (frames since load) and `tstates` CPU
        cycles into it (0: the frame boundary). Breakpoints still stop it."""
        pos = self.position()
        target_f = int(since_load)
        if pos["since_load"] > target_f or (pos["since_load"] == target_f and pos["tstates"] >= tstates):
            return {"reason": "at", "frames": 0, **pos}
        if pos["since_load"] < target_f:
            if pos["midframe"]:                 # finish this frame first
                r = self._run(1)
                if r["why"] in ("breakpoint", "watchpoint", "cpu_jam"):
                    return r
                pos = self.position()
            n = target_f - pos["since_load"]
            if n > 0:
                r = self._run(n)
                if r["why"] in ("breakpoint", "watchpoint", "cpu_jam") or not tstates:
                    return r
        if not tstates:
            return {"reason": "at", "frames": 0, **self.position()}
        pos = self.position()
        return self._run(2, at=pos["cycles"] - pos["tstates"] + int(tstates))

    def continue_(self, max_frames: int = 500) -> dict:
        """Run until a breakpoint/watchpoint fires (or max_frames pass)."""
        r = self._run(max_frames)
        r["stopped"] = r["why"] in ("breakpoint", "watchpoint", "cpu_jam")
        return r

    def stepi(self, count: int = 1) -> dict:
        """Execute `count` instructions."""
        r = self._run(1 + count // 2000, steps=max(1, int(count)))
        return {**r, "pc_symbol": r["symbol"], "next": self.disasm("pc", 1)}

    def step_over(self) -> dict:
        """One instruction; a JSR runs until it returns."""
        r = self._run(200, over=1)
        return {**r, "pc_symbol": r["symbol"], "next": self.disasm("pc", 1)}

    def run_to_addr(self, addr, max_frames: int = 500) -> dict:
        return self._run(max_frames, to=self.addr(addr))

    def run_until_signal(self, max_frames: int = 3000) -> dict:
        """Run until the program writes a byte to $FCF1 (lynx_signal/lynx_done/lynx_fail)."""
        return self._run(max_frames, until_signal=1)

    def position(self) -> dict:
        r = self._kv("status")
        f = int(r["frame"])
        cyc = int(r["cycles"])
        mid = r.get("midframe") == "1"
        return {"frame": f, "since_load": f - self.load_frame, "cycles": cyc,
                "tstates": cyc - int(r.get("frame_cycles", cyc)) if mid else 0, "midframe": mid}

    def frame(self) -> int:
        return int(self._kv("status")["frame"])

    def status(self) -> dict:
        r = self._kv("status")
        pc = int(r["pc"], 16)
        mode = r.get("mode")
        if mode == "paused":
            self.paused = True
        if r.get("hz"):
            self.hz = float(r["hz"]) or self.hz
        return {"mode": mode, "frame": int(r["frame"]), "rotate": r.get("rotate", self.rotate),
                "since_load": int(r["frame"]) - self.load_frame, "cycles": int(r["cycles"]),
                "midframe": r.get("midframe") == "1", "pc": pc, "pc_symbol": self.symbols.name_of(pc),
                "paused": self.paused, "window": self.window, "program": self.program,
                "log_end": int(r["log"]), "symbols": len(self.symbols), "emu_port": self.port,
                "breakpoints": len(self._bps), "watchpoints": len(self._watches),
                "signal": int(r.get("signal", -1)), "signals": int(r.get("signals", 0)),
                "fps": float(r.get("fps", 0)), "hz": self.hz,
                "inputs_logged": len(self._inputs), "last_stop": self._stop_info(r.get("last_stop", ""))}

    # ---------------------------------------------------- guest log

    def log_end(self) -> int:
        return int(self._kv("status")["log"])

    def log_cursor(self) -> int:
        """Absolute offset of the end of the log (pass as since= to run_until)."""
        return self.log_end()

    def _sync_log(self):
        end = self._logbase + len(self._logtext)
        while True:
            r = self._kv(f"log since={end} max=1000000")
            base, nxt = int(r["base"]), int(r["next"])
            if int(r["since"]) > end:            # the ring moved past what we had
                self._logtext, self._logbase = bytearray(), int(r["since"])
            self._logtext += bytes.fromhex(r["data"])
            end = nxt
            if nxt >= int(r["end"]):
                break
        if len(self._logtext) > 8 << 20:
            cut = len(self._logtext) - (4 << 20)
            del self._logtext[:cut]
            self._logbase += cut

    def log_text(self, since: int | None = None) -> str:
        self._sync_log()
        start = max(0, (self.mark if since is None else since) - self._logbase)
        return self._logtext[start:].decode("latin-1")

    def log(self, since: int | None = None) -> str:
        """Guest log text written since `since` (default: since the last log()
        call / reset / load / match); moves the mark to the end."""
        t = self.log_text(since)
        self.mark = self._logbase + len(self._logtext)
        return t

    @property
    def debug_log(self) -> list[str]:
        self._sync_log()
        return self._logtext.decode("latin-1").splitlines()

    # ---------------------------------------------------- input

    def _push_input(self, log: bool = True, mask: int | None = None):
        m = buttons_mask(self._held) if mask is None else mask
        self._cmd(f"input buttons={m:x}")
        if log:
            pos = self.position()
            self._seq += 1
            self._inputs.append({"seq": self._seq, "f": pos["since_load"], "t": pos["tstates"],
                                 "keys": list(self._held), "buttons": m})

    @property
    def held_buttons(self) -> list[str]:
        return list(self._held)

    def hold(self, keys):
        """Press and keep holding buttons ('A', 'UP+B', 'PAUSE')."""
        for k in parse_keys(keys):
            if k not in self._held:
                self._held.append(k)
        self._push_input()

    def release(self, keys=None):
        """Release some (or all) held buttons."""
        drop = parse_keys(keys) if keys is not None else None
        self._held = [] if drop is None else [k for k in self._held if k not in drop]
        self._push_input()

    def press(self, keys, hold_frames: int = 3, release_frames: int = 3) -> dict:
        """Press buttons together for hold_frames, release, run release_frames.
        Programs read the pad when they like; most once per frame."""
        ks = parse_keys(keys)
        saved = list(self._held)
        self._held = saved + [k for k in ks if k not in saved]
        self._push_input()
        r = self.run(hold_frames)
        self._held = saved
        self._push_input()
        if release_frames and r.get("why") not in ("breakpoint", "watchpoint", "cpu_jam"):
            r = self.run(release_frames)
        return r

    def pad(self, dirs=None) -> str:
        """Set the d-pad and A/B to e.g. 'UP+A' ('' or None = centre, released);
        OPTION1/OPTION2/PAUSE stay as they are."""
        mine = set(DIRS) | {"A", "B"}
        self._held = [k for k in self._held if k not in mine]
        for d in parse_keys(dirs or ""):
            if d not in mine:
                raise ValueError(f"{d!r} is not a d-pad direction or A/B")
            self._held.append(d)
        self._push_input()
        return "+".join(k for k in self._held if k in mine) or "centre"

    joystick = pad

    def sequence(self, steps) -> list:
        """[{"press": "A", "hold": 3}, {"run": 20}, {"until": "text", "max": 500}, {"hold": "UP"},
        {"release": "UP"}, {"pad": "LEFT+A"}].
        Any step may carry "at": N (frames since load) to run there first."""
        out = []
        for st in steps:
            if "at" in st:
                self.run_to(int(st["at"]), int(st.get("t", 0)))
            if "press" in st:
                out.append(self.press(st["press"], st.get("hold", 3), st.get("release", 3)))
            elif "run" in st:
                out.append(self.run(st["run"]))
            elif "until" in st:
                out.append(self.run_until(st["until"], st.get("max", 3000)))
            elif "pad" in st or "joystick" in st:
                out.append(self.pad(st.get("pad", st.get("joystick"))))
            elif "hold" in st:
                self.hold(st["hold"])
                out.append(True)
            elif "release" in st:
                self.release(st["release"] or None)
                out.append(True)
            elif "at" in st:
                out.append(self.position())
            else:
                raise ValueError(f"bad step {st!r}")
        return out

    # ---------------------------------------------------- screen

    def palette(self) -> list[tuple[int, int, int]]:
        """The current Mikey palette: 16 (r, g, b) tuples, 0-255."""
        d = bytes.fromhex(self._kv("palette")["data"])
        return [tuple(d[i:i + 3]) for i in range(0, 48, 3)]

    def palette_raw(self) -> list[int]:
        """The 16 palette entries as the registers hold them, 12-bit 0xGBR."""
        raw = self._kv("palette").get("raw", "")
        return [int(raw[i:i + 3], 16) for i in range(0, len(raw), 3)] if raw else []

    def frame_buffer(self) -> tuple[bytes, dict]:
        """The last completed frame as RGB888 (160x102, unrotated) and its geometry."""
        r = self._kv("screen")
        return bytes.fromhex(r["data"]), {"w": int(r.get("w", SCREEN_W)), "h": int(r.get("h", SCREEN_H))}

    def screenshot(self, path=None, border=None, scale: int = 1, layer: str = "screen", rotate=None):
        """The displayed frame as a PIL RGB image (160x102; rotated with the
        session's rotation, or `rotate`). `border` is accepted for a8dev
        compatibility and ignored (the Lynx LCD has no border)."""
        from PIL import Image
        if layer not in ("screen",):
            raise EmulatorError("layer: screen")
        data, g = self.frame_buffer()
        img = Image.frombytes("RGB", (g["w"], g["h"]), data)
        rot = (rotate or self.rotate or "none").lower()
        if rot == "left":
            img = img.transpose(Image.ROTATE_90)
        elif rot == "right":
            img = img.transpose(Image.ROTATE_270)
        if scale and scale != 1:
            img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
        if path:
            Path(path).parent.mkdir(parents=True, exist_ok=True)
            img.save(path)
        return img

    def screen_hash(self, layer: str = "screen") -> str:
        """md5 of the visible frame's RGB pixels."""
        return hashlib.md5(self.screenshot(layer=layer).tobytes()).hexdigest()

    def frame_hash(self) -> str:
        """The emulator's FNV-1a hash of the frame buffer (what recordings compare)."""
        return self._kv("screen_hash")["hash"]

    def record_video(self, path, frames: int = 100, every: int = 1, scale: int = 1,
                     layer: str = "screen", border=None) -> dict:
        """Record `frames` frames (one picture every `every`) to .gif, .mp4 or a
        directory of PNGs. Paused sessions advance deterministically."""
        p = Path(path)
        shots = []
        t0 = time.monotonic()
        for i in range(0, max(1, frames), max(1, every)):
            if i:
                self.run(every)
            shots.append(self.screenshot(scale=scale, layer=layer))
        fps = self.hz / max(1, every)
        save_video(shots, p, fps, self._tmp)
        return {"path": str(p), "frames": len(shots), "fps": fps, "size": list(shots[0].size),
                "seconds": round(time.monotonic() - t0, 2)}

    # ---------------------------------------------------- checkpoint replays (agent input)

    def input_log(self) -> list[dict]:
        return list(self._inputs)

    def checkpoint(self, label: str = "") -> dict:
        pos = self.position()
        self._seq += 1
        cp = {"seq": self._seq, "f": pos["since_load"], "t": pos["tstates"],
              "label": label or f"cp{len(self._checkpoints) + 1}", "screen": self.frame_hash(),
              "pc": self.regs()["PC"]}
        self._checkpoints.append(cp)
        return cp

    def save_replay(self, path=None) -> dict:
        """Input log + checkpoints + the final position/hash since the last load (JSON)."""
        if not self.program:
            raise EmulatorError("no program loaded: a replay starts from load()")
        end = self.checkpoint("end")
        self._checkpoints.pop()
        rec = {"version": 1, "program": self.program, "rotate": self.rotate,
               "events": self.input_log(), "checkpoints": list(self._checkpoints), "end": end}
        if path:
            Path(path).parent.mkdir(parents=True, exist_ok=True)
            Path(path).write_text(json.dumps(rec, indent=1))
        return rec

    def replay(self, rec, verify: bool = True) -> dict:
        """Replay a recording: version 1 (save_replay: agent input + checkpoints) or
        version 2 (record_input: every input source + every frame's hash)."""
        base = None
        if not isinstance(rec, dict):
            base = Path(rec).resolve().parent
            rec = json.loads(Path(rec).read_text())
        if rec.get("version") == 2:
            return self._replay_v2(rec, base, verify)
        if not Path(rec["program"]).exists():
            raise EmulatorError(f"replay program {rec['program']} not found")
        self.delete()
        self.load(rec["program"])
        self._inputs = []
        items = [("event", e) for e in rec["events"]] + [("check", c) for c in rec["checkpoints"]]
        items.append(("check", rec["end"]))
        items.sort(key=lambda it: (it[1]["f"], it[1]["t"], it[1].get("seq", 0)))
        results = []
        with self.held():
            for kind, it in items:
                self.run_to(it["f"], it["t"])
                if kind == "event":
                    self._held = list(it.get("keys", []))
                    self._push_input(mask=int(it["buttons"]))
                elif verify:
                    got = self.frame_hash()
                    results.append({"label": it["label"], "f": it["f"], "t": it["t"],
                                    "expected": it["screen"], "got": got, "ok": got == it["screen"]})
        ok = all(r["ok"] for r in results)
        return {"ok": ok, "checkpoints": results, "events": len(rec["events"]), "position": self.position()}

    # ---------------------------------------------------- input recorder (all sources)

    def record_input(self, action: str = "start", path=None, from_reset: bool = False) -> dict:
        """Record every input change the machine sees (agent-injected AND the user's
        keyboard/joystick in the window), per frame, plus a hash of every frame.

        start: from here (a state is saved to replay from; stopped mid-frame, the
               frame is finished first, breakpoints off), or
               from_reset=True: reload the program and record from the load.
        stop / save (path: JSON + .lys state beside it) / status."""
        if action == "start":
            if from_reset and not self.program:
                raise EmulatorError("from_reset needs a loaded program")
            with self.held():
                if from_reset:
                    self.reload()
                    snap = None
                else:
                    # input is sampled per frame and states are exact at frame
                    # boundaries: a recording starts at one (finish this frame)
                    if self.position()["midframe"]:
                        self._run(1, nobreak=1)
                    snap = self.save_state(self._tmp / f"rec-start-{time.monotonic_ns()}{STATE_EXT}")
                pos = self.position()
                self._cmd("hash action=clear")
                self._cmd("hash action=on")
                self._cmd("rec action=start")
            self._recording = {"program": self.program, "rotate": self.rotate,
                               "snapshot": snap, "from_reset": bool(from_reset), "frame": pos["frame"],
                               "since_load": pos["since_load"], "done": False}
            return {"recording": True, **pos}
        rec = self._recording
        if rec is None:
            raise EmulatorError("not recording (record_input start first)")
        if action == "status":
            return {"recording": not rec["done"], **self._kv("rec action=status")}
        if action in ("stop", "save") and not rec["done"]:
            with self.held():
                pos = self.position()
                self._cmd("rec action=stop")
                self._cmd("hash action=off")
                rec["events"] = self._rec_events(rec["frame"])
                rec["hashes"] = self._hashes(rec["frame"])
                rec["end"] = {"f": pos["frame"] - rec["frame"]}
            rec["done"] = True
        if action == "stop":
            return {"recording": False, "events": len(rec["events"]), "frames": rec["end"]["f"]}
        if action == "save":
            if not path:
                raise ValueError("save needs a path")
            p = Path(path).resolve()
            p.parent.mkdir(parents=True, exist_ok=True)
            out = {"version": 2, "program": rec["program"], "rotate": rec["rotate"],
                   "start": {"from_reset": rec["from_reset"], "since_load": rec["since_load"]},
                   "events": rec["events"], "hashes": rec["hashes"], "end": rec["end"]}
            if rec["snapshot"]:
                snap = p.with_suffix(STATE_EXT)
                shutil.copy(rec["snapshot"], snap)
                shutil.copy(rec["snapshot"] + ".lynxh", str(snap) + ".lynxh")
                out["start"]["snapshot"] = snap.name
            p.write_text(json.dumps(out, indent=None, separators=(",", ":")))
            return {"path": str(p), "events": len(out["events"]), "frames": rec["end"]["f"],
                    "hashes": len(out["hashes"]), "snapshot": out["start"].get("snapshot")}
        raise ValueError(f"unknown action {action!r}: start, stop, save, status")

    def _rec_events(self, base_frame: int) -> list:
        r = self._kv("rec action=get since=0", timeout=60)
        evs = []
        for item in filter(None, r.get("data", "").split(",")):
            f, buttons = item.split(":")[:2]
            evs.append({"f": int(f) - base_frame, "buttons": int(buttons, 16)})
        return evs

    def _hashes(self, base_frame: int) -> list:
        r = self._kv("hash action=get", timeout=60)
        first, data = int(r["first"]), r.get("data", "")
        return [[first + i - base_frame, data[8 * i:8 * i + 8]] for i in range(len(data) // 8)]

    def _replay_v2(self, rec: dict, base: Path | None, verify: bool) -> dict:
        start = rec["start"]
        self.delete()
        if start.get("snapshot"):
            snap = Path(start["snapshot"])
            if not snap.is_absolute() and base is not None:
                snap = base / snap
            self.load_state(snap)
            self.program = rec["program"]
            self.symbols = Symbols.load(rec["program"])
        else:
            self.load(rec["program"])
            if start.get("since_load"):
                self._run(start["since_load"], nobreak=1)
        with self.held():
            f0 = self.position()["frame"]
            self._cmd("hash action=clear")
            self._cmd("hash action=on")
            for ev in rec["events"]:
                n = f0 + ev["f"] - self.position()["frame"]
                if n > 0:
                    self._run(n, nobreak=1)
                self._cmd(f"input raw=1 buttons={int(ev['buttons']):x}")
            n = f0 + rec["end"]["f"] - self.position()["frame"]
            if n > 0:
                self._run(n, nobreak=1)
            self._cmd("input raw=0")
            self._cmd("hash action=off")
            got = self._hashes(f0)
        res = {"ok": True, "frames": rec["end"]["f"], "events": len(rec["events"]), "hashes": got}
        if verify and rec.get("hashes"):
            want = {f: h for f, h in rec["hashes"]}
            have = {f: h for f, h in got}
            common = sorted(set(want) & set(have))
            bad = [f for f in common if want[f] != have[f]]
            res.update(ok=not bad and len(common) > 0, compared=len(common),
                       first_mismatch=bad[0] if bad else None, mismatches=len(bad))
        return res

    # ---------------------------------------------------- registers / memory

    def addr(self, expr) -> int:
        """Number, $hex/0xhex/NNh, symbol (main or _main), symbol+off, or register."""
        regs = None
        if isinstance(expr, str) and expr.strip().upper() in ("PC", "A", "X", "Y", "S", "SP", "P"):
            regs = self.regs()
            regs["SP"] = 0x100 + regs["S"]
        try:
            return self.symbols.resolve(expr, regs)
        except ValueError as ex:
            raise EmulatorError(str(ex)) from None

    def regs(self) -> dict:
        r = self._kv("regs")
        out = {k.upper(): int(r[k], 16) for k in ("a", "x", "y", "s", "p", "pc")}
        out["FLAGS"] = r["flags"]
        return out

    def set_reg(self, name: str, value) -> dict:
        v = self.addr(value) if isinstance(value, str) else int(value)
        n = name.strip().lower()
        self._cmd(f"setreg name={'s' if n == 'sp' else n} value={v}")
        return self.regs()

    def read_mem(self, addr, length: int = 16, space: str = "cpu") -> bytes:
        """CPU view by default (through MAPCTL $FFF9: Suzy $FC00, Mikey $FD00,
        ROM $FE00, vectors; hardware registers read without side effects);
        space='ram': the raw 64 KB RAM underneath."""
        a = self.addr(addr)
        return bytes.fromhex(self._kv(f"peek addr={a} len={int(length)} space={space}")["data"])

    def write_mem(self, addr, data: bytes, space: str = "cpu"):
        a = self.addr(addr)
        data = bytes(data)
        for i in range(0, len(data), 4096):
            self._cmd(f"poke addr={a + i} data={data[i:i + 4096].hex()} space={space}")

    def peek(self, addr) -> int:
        return self.read_mem(addr, 1)[0]

    def peek16(self, addr) -> int:
        b = self.read_mem(addr, 2)
        return b[0] | b[1] << 8

    def poke(self, addr, value: int):
        self.write_mem(addr, bytes([value & 0xFF]))

    def find_mem(self, needle: bytes, start: int = 0, end: int = 0x10000, limit: int = 64,
                 space: str = "ram") -> list[int]:
        if isinstance(needle, str):
            needle = needle.encode("latin-1")
        r = self._kv(f"find data={bytes(needle).hex()} start={int(start)} end={min(int(end), 0x10000) - 1} "
                     f"max={limit} space={space}")
        return [int(a, 16) for a in r.get("addrs", "").split(",") if a]

    def hw(self) -> dict:
        """Suzy and Mikey registers (write registers as last written) and the
        memory map, grouped by chip: {"suzy": {...}, "mikey": {...}, "mem": {...}}.
        Multi-value registers (the palette) are lists."""
        r = self._kv("hw")
        out: dict = {}
        for k, v in r.items():
            chip, _, name = k.partition(".")
            if not name:
                continue
            vals = v.split(",")
            try:
                parsed = [int(x, 16) for x in vals]
            except ValueError:
                parsed = vals
            out.setdefault(chip, {})[name] = parsed[0] if len(parsed) == 1 else parsed
        return out

    def memory_map(self) -> dict:
        """MAPCTL ($FFF9) decoded: which of Suzy, Mikey, ROM and the vectors the
        CPU sees (otherwise RAM)."""
        m = self.read_mem(0xFFF9, 1, space="ram")[0]
        try:
            m = self.hw().get("mem", {}).get("mapctl", m)
        except EmulatorError:
            pass
        return {"mapctl": m, "suzy": not (m & 1), "mikey": not (m & 2), "rom": not (m & 4),
                "vectors": not (m & 8)}

    def scb_chain(self, start=None, limit: int = 64) -> list[dict]:
        """Walk a Suzy sprite chain (SCBs) from `start` (default: the SCBNEXT
        register Suzy last started from) and decode each control block."""
        if start is None:
            try:
                start = self.hw().get("suzy", {}).get("scbnext")
            except EmulatorError:
                start = None
            if start is None:
                raise EmulatorError("no start address (the emulator's hw has no suzy.scbnext)")
        a = self.addr(start) if isinstance(start, str) else int(start)
        out, seen = [], set()
        while a and (a >> 8) and a not in seen and len(out) < limit:
            seen.add(a)
            b = self.read_mem(a, 27, space="ram")
            ctl0, ctl1, coll = b[0], b[1], b[2]
            nxt = b[3] | b[4] << 8
            e = {"addr": a, "sprctl0": ctl0, "sprctl1": ctl1, "sprcoll": coll, "next": nxt,
                 "data": b[5] | b[6] << 8, "bpp": (ctl0 >> 6) + 1, "type": ctl0 & 7,
                 "hflip": bool(ctl0 & 0x20), "vflip": bool(ctl0 & 0x10),
                 "literal": bool(ctl1 & 0x80), "skip": bool(ctl1 & 0x04)}
            reload = (ctl1 >> 4) & 3
            i = 7
            if not (ctl1 & 0x04):
                e["hpos"] = int.from_bytes(b[i:i + 2], "little", signed=True)
                e["vpos"] = int.from_bytes(b[i + 2:i + 4], "little", signed=True)
                i += 4
                if reload >= 1:
                    e["hsize"] = b[i] | b[i + 1] << 8
                    e["vsize"] = b[i + 2] | b[i + 3] << 8
                    i += 4
                if reload >= 2:
                    e["stretch"] = b[i] | b[i + 1] << 8
                    i += 2
                if reload >= 3:
                    e["tilt"] = b[i] | b[i + 1] << 8
                    i += 2
                if not (ctl1 & 0x08):
                    e["penpal"] = b[i:i + (1 << (e["bpp"] - 1))].hex()
            out.append(e)
            a = nxt
        return out

    def disasm(self, addr="pc", count: int = 12) -> list[str]:
        a = self.addr(addr)
        data = self.read_mem(a, min(3 * count + 3, 0x10000 - a) or 1)
        mem = {(a + i) & 0xFFFF: b for i, b in enumerate(data)}

        def rd(x):
            if x not in mem:
                mem.update({(x + i) & 0xFFFF: b for i, b in enumerate(self.read_mem(x, 3))})
            return mem[x]

        def sym(v):
            n = self.symbols.name_of(v)
            return n if n and "+" not in n else None

        out = []
        for _ in range(count):
            name = self.symbols.name_of(a)
            if name and "+" not in name:
                out.append(f"{name}:")
            n, b, text = cpu6502.disasm_one(rd, a, sym)
            out.append(f"  {a:04X}  {' '.join(f'{x:02X}' for x in b):<9} {text}")
            a = (a + n) & 0xFFFF
        return out

    # ---------------------------------------------------- debugger

    def _arm(self, b: dict):
        if b.get("kind"):
            self._cmd(f"watch id={b['index']} addr={b['addr']} len={b['length']} kind={b['kind']}")
        else:
            line = f"break id={b['index']}"
            if b.get("addr") is not None:
                line += f" pc={b['addr']}"
            if b.get("expr"):
                line += f" cond={_q(b['expr'])}"
            self._cmd(line)

    def _new_id(self) -> int:
        i = self._next_id
        self._next_id += 1
        return i

    _COND_WORDS = {"A", "X", "Y", "S", "P", "PC", "N", "V", "B", "D", "I", "Z", "C", "FRAME", "LINE",
                   "AND", "OR", "NOT", "PEEK", "DPEEK"}

    def _cond_expr(self, condition: str) -> str:
        """Symbols in a condition -> numbers ('PC=_main AND A=5' -> 'PC=$2090 AND A=5')."""
        def rep(m):
            w = m.group(0)
            if w.upper() in self._COND_WORDS:
                return w
            a = self.symbols.lookup(w)
            if a is None:
                raise EmulatorError(f"unknown symbol {w!r} in condition")
            return f"${a:04X}"
        # $hex / 0x / %bin numbers are left alone by the identifier pattern
        return re.sub(r"(?<![$%\w])[A-Za-z_][\w@.]*", rep, condition)

    def break_at(self, addr) -> dict:
        """Breakpoint at an address/symbol (stops before executing it)."""
        a = self.addr(addr)
        i = self._new_id()
        b = {"index": i, "addr": a, "condition": f"PC=${a:04X}", "desc": f"{addr} (${a:04X})"}
        self._arm(b)
        self._bps[i] = b
        return b

    def break_if(self, condition: str, desc: str | None = None, addr: int | None = None) -> dict:
        """Breakpoint on a condition: 'A=5 AND PC=$2000', 'PC=_main', '[$FCB0]&$80',
        'DPEEK($FC10)=$2000', 'FRAME>=200'. Registers A X Y S P PC, flags N V B D I Z C,
        FRAME LINE; = == != < > <= >= + - & | ^ AND OR NOT; [a]/PEEK(a), DPEEK(a)."""
        i = self._new_id()
        b = {"index": i, "condition": condition, "expr": self._cond_expr(condition),
             "desc": desc or condition, "addr": addr}
        self._arm(b)
        self._bps[i] = b
        return b

    def watch(self, addr, length: int = 1, kind: str = "w") -> dict:
        """Stop after the instruction that reads ('r'), writes ('w') or accesses ('rw')
        [addr, addr+length) (CPU data accesses through the operand; not DMA or the stack)."""
        a = self.addr(addr)
        kind = {"wr": "rw"}.get(kind, kind)
        if kind not in ("r", "w", "rw"):
            raise EmulatorError("kind: r, w or rw")
        i = self._new_id()
        w = {"index": i, "addr": a, "length": int(length), "kind": kind, "desc": f"{addr} (${a:04X}) {kind}"}
        self._arm(w)
        self._watches[i] = w
        return w

    def delete(self, index: int | None = None, addr=None):
        """Remove a breakpoint/watchpoint (by index, or every one at addr), or everything."""
        if index is None and addr is None:
            self._cmd("delete all=1")
            self._bps.clear()
            self._watches.clear()
            return
        ids = []
        if index is not None:
            ids.append(int(index))
        if addr is not None:
            a = self.addr(addr)
            ids += [i for i, b in {**self._bps, **self._watches}.items() if b.get("addr") == a]
        for i in ids:
            if i in self._bps or i in self._watches:
                self._cmd(f"delete id={i}")
                self._bps.pop(i, None)
                self._watches.pop(i, None)

    def breakpoints(self) -> dict:
        hits = {}
        for item in filter(None, self._kv("breaks").get("list", "").split(";")):
            f = item.split(":")
            hits[int(f[0])] = int(f[-1])
        return {"breakpoints": [{**b, "hits": hits.get(i, 0)} for i, b in self._bps.items()],
                "watchpoints": [{**w, "hits": hits.get(i, 0)} for i, w in self._watches.items()]}

    def backtrace(self, items: int = 8) -> list[dict]:
        """Best effort: return addresses on the 6502 stack. A word is a return
        address if the three bytes before its target are a JSR; each entry gives
        the caller (the JSR) and the routine it called, with symbols."""
        r = self.regs()
        s = r["S"]
        stack = self.read_mem(0x100 + s + 1, 0xFF - s) if s < 0xFF else b""
        out = [{"frame": 0, "pc": r["PC"], "symbol": self.symbols.name_of(r["PC"])}]
        i = 0
        while i + 1 < len(stack) and len(out) <= items:
            w = stack[i] | stack[i + 1] << 8
            jsr = (w - 2) & 0xFFFF
            code = self.read_mem(jsr, 3)
            if code[0] == 0x20:
                target = code[1] | code[2] << 8
                out.append({"frame": len(out), "sp": 0x100 + s + 1 + i, "return": (w + 1) & 0xFFFF,
                            "caller": jsr, "caller_symbol": self.symbols.name_of(jsr),
                            "called": target, "called_symbol": self.symbols.name_of(target)})
                i += 2
            else:
                i += 1
        return out

    # ---------------------------------------------------- states

    def save_state(self, path) -> str:
        """A Handy state file (.lys) plus <file>.lynxh (the harness's frame counter
        and program). Exact at a frame boundary; one taken mid-frame says so
        (self.last_state_midframe)."""
        p = Path(path)
        if p.suffix == "":
            p = p.with_suffix(STATE_EXT)
        p = p.resolve()
        p.parent.mkdir(parents=True, exist_ok=True)
        r = self._kv(f"savestate path={_q(p)}")
        self.last_state_midframe = r.get("midframe") == "1"
        if not p.exists():
            raise EmulatorError(f"state not written: {p}")
        return str(p)

    def load_state(self, path) -> dict:
        p = Path(path)
        if p.suffix == "":
            p = p.with_suffix(STATE_EXT)
        p = p.resolve()
        if not p.exists():
            raise EmulatorError(f"{p} not found")
        self._cmd(f"loadstate path={_q(p)}")
        side = Path(str(p) + ".lynxh")
        if side.exists():
            info = dict(ln.split("=", 1) for ln in side.read_text().splitlines() if "=" in ln)
            if info.get("loaded"):
                self.program = info["loaded"]
                self.symbols = Symbols.load(self.program)
        self.load_frame = 0
        self._held = []
        self._push_input(log=False)
        return self.status()
