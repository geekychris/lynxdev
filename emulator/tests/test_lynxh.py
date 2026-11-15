#!/usr/bin/env python3
"""Standalone tests for the lynxh emulator (Handy + harness), over the raw
control-socket protocol. No dependency on tools/lynxdev: this checks the
emulator side of the contract on its own.

    python3 emulator/tests/test_lynxh.py            # headless
    LYNXH_TEST_WINDOW=1 python3 emulator/tests/test_lynxh.py   # + the SDL build

The guest program (emutest.c) is built with cc65: `cl65` on PATH when
LYNX_NATIVE=1, otherwise the Docker toolchain image (LYNX_TOOLCHAIN_IMAGE,
default: TOOLCHAIN_IMAGE from scripts/lib.sh, else lynx-dev-toolchain:1).
Outputs go to build/emutest/ (never into the source tree).
"""
import os
import re
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD = os.path.join(ROOT, "build", "emutest")
EMU = os.environ.get("LYNX_EMULATOR") or os.path.join(ROOT, "third_party", "handy", "build", "lynxh")
EMU_SDL = os.environ.get("LYNX_EMULATOR_WINDOW") or os.path.join(ROOT, "third_party", "handy", "build", "lynxh-sdl")

UP, DOWN, LEFT, RIGHT, OPT1, OPT2, B, A, PAUSE = 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x100


def toolchain_image():
    img = os.environ.get("LYNX_TOOLCHAIN_IMAGE")
    if img:
        return img
    try:
        m = re.search(r'^TOOLCHAIN_IMAGE="?([^"\s]+)"?', open(os.path.join(ROOT, "scripts", "lib.sh")).read(), re.M)
        if m:
            return m.group(1)
    except OSError:
        pass
    return "lynx-dev-toolchain:1"


def build_guest():
    """emutest.lnx + .lbl in build/emutest; returns the .lnx path."""
    os.makedirs(BUILD, exist_ok=True)
    src = os.path.join(HERE, "emutest.c")
    lnx = os.path.join(BUILD, "emutest.lnx")
    if os.path.exists(lnx) and os.path.getmtime(lnx) >= os.path.getmtime(src):
        return lnx
    shutil.copy(src, os.path.join(BUILD, "emutest.c"))
    cmd = "cl65 -t lynx -O -Ln emutest.lbl -o emutest.lnx emutest.c"
    if os.environ.get("LYNX_NATIVE") == "1" or (shutil.which("cl65") and not shutil.which("docker")):
        subprocess.run(shlex.split(cmd), cwd=BUILD, check=True)
    else:
        subprocess.run(["docker", "run", "--rm", "-u", "%d:%d" % (os.getuid(), os.getgid()), "-v", BUILD + ":/w",
                        "-w", "/w", toolchain_image(), "sh", "-c", cmd], check=True)
    return lnx


class Lynxh:
    """A minimal client for the lynxh protocol."""

    def __init__(self, binary=EMU, *args):
        env = dict(os.environ, LYNXH_PARENT_WATCH="1")
        self.p = subprocess.Popen([binary, "--port", "0", *args], stdout=subprocess.PIPE, text=True, env=env)
        line = self.p.stdout.readline()
        m = re.match(r"LYNXH port=(\d+) pid=(\d+)", line)
        if not m:
            raise RuntimeError("no banner: %r" % line)
        self.port = int(m.group(1))
        self.s = socket.create_connection(("127.0.0.1", self.port))
        self.f = self.s.makefile("r")

    def raw(self, c):
        self.s.sendall((c + "\n").encode())
        return self.f.readline().rstrip("\n")

    def cmd(self, c):
        r = self.raw(c)
        if not r.startswith("OK"):
            raise RuntimeError("%s -> %s" % (c, r))
        out = {}
        for t in shlex.split(r[2:]):
            if "=" in t:
                k, v = t.split("=", 1)
                out[k] = v
        return out

    def log(self, since=0):
        return bytes.fromhex(self.cmd("log since=%d" % since)["data"]).decode("latin1")

    def until(self, text, frames=600):
        return self.cmd("run frames=%d until=%s" % (frames, text.encode().hex()))

    def hashes(self, since=None):
        r = self.cmd("hash action=get" + ("" if since is None else " since=%d" % since))
        d = r["data"]
        return [d[i:i + 8] for i in range(0, len(d), 8)]

    def close(self):
        try:
            self.raw("quit")
        except OSError:
            pass
        try:
            self.p.wait(5)
        except subprocess.TimeoutExpired:
            self.p.kill()


GUEST = None


def setUpModule():
    global GUEST
    if not os.path.exists(EMU):
        raise unittest.SkipTest("no emulator at %s (run scripts/build-emulator.sh)" % EMU)
    GUEST = build_guest()


class Base(unittest.TestCase):
    def setUp(self):
        self.e = Lynxh()
        self.addCleanup(self.e.close)
        self.r = self.e.cmd("load path=%s" % GUEST)

    def ready(self):
        r = self.e.until("emutest: ready")
        self.assertEqual(r["why"], "log")
        return r


class TestBoot(Base):
    def test_hle_boot_log_signature_signal(self):
        self.assertEqual(self.r["type"], "lnx")
        self.assertEqual(self.r["bios"], "0")          # HLE boot, no lynxboot.img
        self.ready()
        self.assertIn("emutest: harness=LY\n", self.e.log())   # $FCF0/$FCF1 read 'L' 'Y'
        r = self.e.cmd("run frames=10 until_signal=1")
        st = self.e.cmd("status")
        self.assertEqual(st["signal"], "1")
        self.assertGreaterEqual(int(st["signals"]), 1)
        self.assertAlmostEqual(float(st["hz"]), 59.9, delta=1.0)   # cc65's default display rate
        self.assertEqual(st["loaded"], GUEST)

    def test_fail_signal_and_stop_after_store(self):
        self.ready()
        self.e.cmd("run frames=2")
        self.e.cmd("input buttons=%x" % (OPT1 | OPT2))
        r = self.e.cmd("run frames=50 until_signal=1")
        self.assertEqual(r["why"], "signal")
        self.assertEqual(r["signal"], "2")
        self.assertEqual(r["midframe"], "1")
        self.assertTrue(self.e.log().endswith("emutest: fail\n"))

    def test_reset_reboots_same_cart(self):
        self.ready()
        self.e.cmd("run frames=100")
        h1 = self.e.cmd("screen_hash")["hash"]
        self.e.cmd("reset")
        st = self.e.cmd("status")
        self.assertEqual(st["frame"], "0")
        self.ready()
        self.e.cmd("run frames=100")
        self.assertEqual(self.e.cmd("screen_hash")["hash"], h1)


class TestInput(Base):
    def test_every_button_bit(self):
        self.ready()
        self.e.cmd("run frames=2")
        for bit, name in ((UP, "80"), (DOWN, "40"), (LEFT, "20"), (RIGHT, "10"), (OPT1, "08"), (OPT2, "04"),
                          (B, "02"), (A, "01")):
            self.e.cmd("log_clear")
            mark = int(self.e.cmd("status")["log"])
            self.e.cmd("input buttons=%x" % bit)
            self.e.cmd("run frames=3")
            self.e.cmd("input buttons=0")
            self.e.cmd("run frames=3")
            self.assertIn("input: joy=%s pause=0" % name, self.e.log(mark), name)
        mark = int(self.e.cmd("status")["log"])
        self.e.cmd("input buttons=100")
        self.e.cmd("run frames=3")
        self.assertIn("input: joy=00 pause=1", self.e.log(mark))
        self.e.cmd("input buttons=0")
        self.assertEqual(self.e.raw("input buttons=zz")[:3], "ERR")

    def test_recorder_and_forced_replay(self):
        self.ready()
        self.e.cmd("run frames=1")
        self.e.cmd("rec action=start")
        self.e.cmd("hash action=on")
        first = int(self.e.cmd("hash action=status")["first"])
        for b, n in ((UP, 5), (0, 3), (A | RIGHT, 4), (0, 6)):
            self.e.cmd("input buttons=%x" % b)
            self.e.cmd("run frames=%d" % n)
        rec = self.e.cmd("rec action=get")
        events = [(int(f), int(v, 16)) for f, v in (x.split(":") for x in rec["data"].split(","))]
        self.assertEqual([v for _, v in events], [UP, 0, A | RIGHT, 0])
        want = self.e.hashes()
        start = int(rec["start"])
        # replay in a fresh emulator: same frames, same input forced -> same hashes
        e2 = Lynxh()
        try:
            e2.cmd("load path=%s" % GUEST)
            e2.cmd("run frames=%d" % start)
            e2.cmd("hash action=on")
            self.assertEqual(int(e2.cmd("hash action=status")["first"]), first)
            ev = dict(events)
            for f in range(start, start + len(want)):
                if f in ev:
                    e2.cmd("input raw=1 buttons=%x" % ev[f])
                e2.cmd("run frames=1")
            self.assertEqual(e2.hashes(), want)
        finally:
            e2.close()


class TestDebugger(Base):
    def test_break_step_over_to_symbols(self):
        self.ready()
        tick = int(self.e.cmd("symbols name=tick")["value"], 16)
        self.e.cmd("break id=1 pc=_tick")
        r = self.e.cmd("run frames=5")
        self.assertEqual(r["why"], "break")
        self.assertEqual(int(r["pc"], 16), tick)
        self.assertEqual(r["midframe"], "1")
        self.assertIn("1:break:%d::1" % tick, self.e.cmd("breaks")["list"])
        regs = self.e.cmd("regs")
        self.assertEqual(regs["symbol"], "_tick")
        # resume: doesn't re-fire on the same instruction; next hit a frame later
        f0 = int(r["frame"])
        r = self.e.cmd("run frames=5")
        self.assertEqual(r["why"], "break")
        self.assertEqual(int(r["frame"]), f0 + 1)
        self.e.cmd("delete id=1")
        # steps
        r = self.e.cmd("run frames=1 steps=1")
        self.assertEqual(r["why"], "step")
        self.assertNotEqual(int(r["pc"], 16), tick)
        # step over a jsr: run to a jsr _tick and step over it
        callers = [l for l in self.e.cmd("disasm addr=_main count=200")["lines"].split(";") if "jsr _tick" in l]
        self.assertTrue(callers, "a jsr _tick in main")
        jsr = int(callers[0].split(":")[0], 16)
        r = self.e.cmd("run frames=5 to=%d" % jsr)
        self.assertEqual((r["why"], int(r["pc"], 16)), ("to", jsr))
        c0 = int(self.e.cmd("peek addr=_counter")["data"], 16)
        r = self.e.cmd("run frames=5 over=1")
        self.assertEqual((r["why"], int(r["pc"], 16)), ("step", jsr + 3))
        self.assertEqual(int(self.e.cmd("peek addr=_counter")["data"], 16), (c0 + 1) & 0xff)

    def test_conditional_break_and_watch(self):
        self.ready()
        self.e.cmd("break id=2 cond=\"[_counter]=$30 AND PC=_tick\"")
        r = self.e.cmd("run frames=200")
        self.assertEqual(r["why"], "break")
        self.assertEqual(self.e.cmd("peek addr=_counter")["data"], "30")
        self.e.cmd("delete all=1")
        self.e.cmd("watch id=3 addr=_counter kind=w")
        r = self.e.cmd("run frames=5")
        self.assertEqual(r["why"], "watch")
        self.assertEqual(r["old"], "30")
        self.assertEqual(r["value"], "31")
        self.assertEqual(r["access"], "w")
        tick = int(self.e.cmd("symbols name=_tick")["value"], 16)
        self.assertTrue(tick <= int(r["insn_pc"], 16) < tick + 16)
        self.e.cmd("delete all=1")
        self.e.cmd("watch id=4 addr=_counter kind=r")
        r = self.e.cmd("run frames=5")
        self.assertEqual(r["why"], "watch")
        self.assertEqual(r["access"], "r")

    def test_setreg_regs_at(self):
        self.ready()
        self.e.cmd("break id=1 pc=_tick")
        self.e.cmd("run frames=5")
        self.e.cmd("delete all=1")
        self.e.cmd("setreg name=a value=$5a")
        self.e.cmd("setreg name=x value=7")
        r = self.e.cmd("regs")
        self.assertEqual((r["a"], r["x"]), ("5a", "07"))
        self.assertRegex(r["flags"], r"^[N-][V-]-[B-][D-][I-][Z-][C-]$")
        target = int(r["cycles"]) + 1000
        r = self.e.cmd("run frames=5 at=%d" % target)
        self.assertEqual(r["why"], "at")
        self.assertTrue(target <= int(r["cycles"]) < target + 20)


class TestMemoryScreen(Base):
    def test_peek_poke_find_hw(self):
        self.ready()
        self.e.cmd("poke addr=$3000 data=deadbeef space=ram")
        self.assertEqual(self.e.cmd("peek addr=$3000 len=4")["data"], "deadbeef")
        self.assertIn("3000", self.e.cmd("find data=deadbeef")["addrs"].split(","))
        self.assertEqual(self.e.cmd("peek addr=$fcf0 len=2")["data"], "4c59")   # harness signature via Suzy
        hw = self.e.cmd("hw")
        for k in ("suzy.sprctl0", "suzy.scbnext", "suzy.vidbas", "suzy.sprsys", "mikey.dispadr", "mikey.dispctl",
                  "mikey.tim0bkup", "mikey.tim2bkup", "mikey.aud0vol", "mikey.mstereo", "mikey.intset", "mem.mapctl", "suzy.last_chain"):
            self.assertIn(k, hw)
        self.assertNotEqual(hw["suzy.last_chain"], "0000")       # TGI draws with sprites
        self.assertEqual(len(hw["mikey.green"].split(",")), 16)
        self.assertEqual(len(hw["mikey.bluered"].split(",")), 16)
        # reading the timer count through peek has no side effects
        a = self.e.cmd("peek addr=$fd02 len=1")["data"]
        self.assertEqual(self.e.cmd("peek addr=$fd02 len=1")["data"], a)
        # ram space: the RAM under Suzy, not the signature
        self.assertNotEqual(self.e.cmd("peek addr=$fcf0 len=2 space=ram")["data"], "4c59")

    def test_screen_palette_screenshot(self):
        self.ready()
        self.e.cmd("run frames=30")
        s = self.e.cmd("screen")
        self.assertEqual((s["w"], s["h"], s["format"]), ("160", "102", "rgb24"))
        data = bytes.fromhex(s["data"])
        self.assertEqual(len(data), 160 * 102 * 3)
        self.assertEqual(s["hash"], self.e.cmd("screen_hash")["hash"])
        self.assertEqual("%08x" % (fnv1a(data)), s["hash"])
        self.assertGreater(len(set(data[i:i + 3] for i in range(0, len(data), 3))), 1)
        pal = self.e.cmd("palette")
        self.assertEqual(len(bytes.fromhex(pal["data"])), 48)
        self.assertEqual(len(pal["raw"].split(",")), 16)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "s.png")
            r = self.e.cmd("screenshot path=%s scale=2" % p)
            self.assertEqual((r["w"], r["h"]), ("320", "204"))
            png = open(p, "rb").read()
            self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(png_rgb(png, 320, 204)[:6], data[:3] * 2)
            self.e.cmd("rotate value=left")
            r = self.e.cmd("screenshot path=%s" % p)
            self.assertEqual((r["w"], r["h"]), ("102", "160"))
            self.assertEqual(self.e.cmd("status")["rotate"], "left")
            self.assertEqual(self.e.cmd("screen")["hash"], s["hash"])   # screen stays unrotated

    def test_audio_capture(self):
        self.ready()
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "a.wav")
            self.e.cmd("audio action=start path=%s" % p)
            self.e.cmd("run frames=60")
            r = self.e.cmd("audio action=stop")
            self.assertAlmostEqual(float(r["seconds"]), 1.0, delta=0.1)
            w = open(p, "rb").read()
            self.assertEqual(w[:4], b"RIFF")
            self.assertEqual(len(w), 44 + int(r["frames"]) * 4)


class TestDeterminism(unittest.TestCase):
    def run_session(self, e):
        mark = int(e.cmd("status")["log"])
        e.cmd("load path=%s" % GUEST)
        e.cmd("hash action=on")
        e.cmd("run frames=120")
        e.cmd("input buttons=%x" % (RIGHT | A))
        e.cmd("run frames=40")
        e.cmd("input buttons=0")
        e.cmd("run frames=40")
        st = e.cmd("status")
        return e.hashes(), st["cycles"], st["pc"], e.log(mark)

    def test_same_inputs_same_hashes(self):
        a, b = Lynxh(), Lynxh()
        try:
            ra = self.run_session(a)
            rb = self.run_session(b)
            self.assertEqual(len(ra[0]), 200)
            self.assertGreater(len(set(ra[0])), 10)
            self.assertEqual(ra, rb)
            # the same emulator again, after it ran something: identical too
            rc = self.run_session(a)
            self.assertEqual(rc, ra)
        finally:
            a.close()
            b.close()

    def test_savestate_continues_identically(self):
        e = Lynxh()
        try:
            e.cmd("load path=%s" % GUEST)
            e.cmd("run frames=150")
            with tempfile.TemporaryDirectory() as d:
                p = os.path.join(d, "s.lys")
                e.cmd("savestate path=%s" % p)
                e.cmd("hash action=on")
                e.cmd("input buttons=%x" % UP)
                e.cmd("run frames=60")
                want = e.hashes()
                cyc = e.cmd("status")["cycles"]
                e.cmd("input buttons=0")
                e.cmd("run frames=500")      # wander off
                r = e.cmd("loadstate path=%s" % p)
                self.assertEqual(r["frame"], "150")
                e.cmd("hash action=clear")
                e.cmd("input buttons=%x" % UP)
                e.cmd("run frames=60")
                self.assertEqual(e.hashes(), want)
                self.assertEqual(e.cmd("status")["cycles"], cyc)
                # and in a fresh emulator (the state names its cart)
                e2 = Lynxh()
                try:
                    e2.cmd("loadstate path=%s" % p)
                    e2.cmd("hash action=on")
                    e2.cmd("input buttons=%x" % UP)
                    e2.cmd("run frames=60")
                    self.assertEqual(e2.hashes(), want)
                finally:
                    e2.close()
        finally:
            e.close()

    def test_long_run_counter_wrap(self):
        # Handy's 32-bit 16 MHz counter rebases after ~4.5 emulated minutes
        e = Lynxh()
        try:
            e.cmd("load path=%s" % GUEST)
            prev = 0
            for _ in range(20):
                c = int(e.cmd("run frames=1000")["cycles"])
                self.assertAlmostEqual(c - prev, 66780000, delta=100000)
                prev = c
            self.assertIn("frames: 19968\n", e.log())
        finally:
            e.close()


@unittest.skipUnless(os.environ.get("LYNXH_TEST_WINDOW") == "1", "LYNXH_TEST_WINDOW=1 opens a window")
class TestWindow(unittest.TestCase):
    def test_window_free_run_and_hold(self):
        if not os.path.exists(EMU_SDL):
            self.skipTest("no window build")
        e = Lynxh(EMU_SDL, "--nosound")
        try:
            e.cmd("load path=%s" % GUEST)
            self.assertEqual(e.cmd("status")["mode"], "free")
            time.sleep(1.0)
            self.assertGreater(int(e.cmd("status")["frame"]), 30)
            r = e.cmd("run frames=600 until=%s" % b"frames: 512".hex())
            self.assertEqual(r["why"], "log")
            e.cmd("break id=1 pc=_tick")
            self.assertEqual(e.cmd("run frames=10")["why"], "break")
            f = e.cmd("status")["frame"]
            time.sleep(0.3)
            self.assertEqual(e.cmd("status")["frame"], f)      # held
            e.cmd("delete all=1")
            e.cmd("resume")
            time.sleep(0.5)
            self.assertGreater(int(e.cmd("status")["frame"]), int(f))
        finally:
            e.close()


def fnv1a(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def png_rgb(png, w, h):
    pos, idat = 8, b""
    while pos < len(png):
        n = int.from_bytes(png[pos:pos + 4], "big")
        t = png[pos + 4:pos + 8]
        if t == b"IDAT":
            idat += png[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    return b"".join(raw[y * (w * 3 + 1) + 1:(y + 1) * (w * 3 + 1)] for y in range(h))


if __name__ == "__main__":
    unittest.main(verbosity=2)
