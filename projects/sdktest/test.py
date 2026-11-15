"""SDK checks: sprites (1:1, 2x, flipped, recoloured, chained), palette,
text, sound registers, rnd, buttons. Run by `./lynx test sdktest`."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lynxdev" / "src"))

from lynxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{'' if ok or not detail else '  (' + str(detail) + ')'}")
    if not ok:
        failures.append(name)


with Emulator() as emu:
    emu.load(HERE / "build" / "sdktest.lnx")
    check("rnd is deterministic", emu.run_until("sdktest: rnd ", 600))
    emu.run_until("\n", 5)
    check("Suzy multiply and divide", emu.run_until("sdktest: math mul=120000 div=14285 rem=5", 5))
    check("Suzy multiply 0xFFFF^2", emu.run_until("sdktest: math big=4294836225", 5))
    check("box_hit", emu.run_until("sdktest: box 1 0", 5))
    check("tone starts at volume 100", emu.run_until("sdktest: sound on vol=100", 5))
    check("drawn", emu.run_until("sdktest: drawn", 60))
    h = emu.hw()
    check("audio channel 0 counts (hw)", h["mikey"].get("aud0ctl", h["mikey"].get("aud0ctla", 0)) & 0x18 == 0x18, h["mikey"])
    emu.run(2)
    img = emu.screenshot(HERE / "build" / "sdktest.png")
    pal = emu.palette()
    px = img.load()

    def is_pen(x, y, pen):
        return px[x, y] == tuple(pal[pen])

    check("palette entry 14 set to magenta", tuple(pal[14]) == (255, 0, 255), pal[14])
    check("ship 1:1: white nose at (13,10)", is_pen(13, 10, 15), px[13, 10])
    check("ship 1:1: yellow body at (13,12)", is_pen(13, 12, 9), px[13, 12])
    check("ship 1:1: transparent corner (10,10) shows background", is_pen(10, 10, 1), px[10, 10])
    check("ship 2x: white nose at (36,10) and (37,11)", is_pen(36, 10, 15) and is_pen(37, 11, 15))
    check("ship 2x: nose ends at x=40", is_pen(40, 10, 1), px[40, 10])
    check("ship flipped: nose at the bottom (63,40)", is_pen(63, 40, 15), px[63, 40])
    check("ship flipped: red feet at the top (61,33)", is_pen(61, 33, 2), px[61, 33])
    check("recoloured 4x block red at (107,17)", is_pen(107, 17, 2) and is_pen(100, 10, 2) and is_pen(115, 25, 2))
    check("4x block ends at x=116", is_pen(116, 17, 1), px[116, 17])
    check("chain: green, blue, white blocks", is_pen(11, 61, 11) and is_pen(21, 61, 13) and is_pen(31, 61, 15),
          (px[11, 61], px[21, 61], px[31, 61]))
    check("rect in recoloured pen 14", is_pen(135, 65, 14), px[135, 65])
    check("text drawn", any(is_pen(x, y, 15) for x in range(8, 56) for y in range(88, 96)))
    # small font: "Score" at (80,90) yellow; "00042" at (104,90) white
    check("small text: 'S' top bar at (81,90) yellow", is_pen(81, 90, 9) and is_pen(82, 90, 9), (px[81, 90], px[82, 90]))
    check("small text: transparent cell gap (83,90)", is_pen(83, 90, 1), px[83, 90])
    check("small text: zero-padded '0' at (104..106,90) white", all(is_pen(x, 90, 15) for x in (104, 105, 106)))
    check("small text: '4' of 42 at (120,90) white", is_pen(120, 90, 15) and is_pen(122, 90, 15), (px[120, 90], px[122, 90]))
    check("sound decays to off after ~50 frames", emu.run_until("sdktest: sound off", 120))
    emu.press("A")
    check("A -> noise", emu.run_until("sdktest: noise", 10))
    emu.press("OPTION1")
    check("OPTION1", emu.run_until("sdktest: option1", 10))
    emu.press("PAUSE")
    check("PAUSE", emu.run_until("sdktest: pause", 10))
    if failures:
        print("\n".join(emu.debug_log[-15:]))

print(f"{'FAIL' if failures else 'PASS'}: sdktest")
sys.exit(1 if failures else 0)
