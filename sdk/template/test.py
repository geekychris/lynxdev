"""Smoke test for __NAME__ (run by `./lynx test __NAME__`).

Boots headless, waits for the ready line, moves with the joypad, presses A,
checks the log and that something was drawn. Extend it as the program grows.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lynxdev" / "src"))

from lynxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "__NAME__.lnx"
failures = []


def check(name, ok):
    print(f"  {'PASS' if ok else 'FAIL'} {name}")
    if not ok:
        failures.append(name)


with Emulator() as emu:
    emu.load(PROG)
    check("boots to ready", emu.run_until("__NAME__: ready", 600))
    emu.run(5)
    emu.press("RIGHT", hold_frames=1)
    check("joypad moves right", emu.run_until("__NAME__: move x=77", 20))
    emu.press("A")
    check("A changes colour", emu.run_until("__NAME__: colour", 20))
    emu.run(2)
    img = emu.screenshot(HERE / "build" / "test.png")
    check("screen is 160x102", img.size == (160, 102))
    check("screen has content", len(img.getcolors(1 << 16) or []) >= 3)
    if failures:
        print("\n".join(emu.debug_log[-15:]))

print(f"{'FAIL' if failures else 'PASS'}: __NAME__")
sys.exit(1 if failures else 0)
