"""Rock Blaster (Lynx) - plays a game through the harness.

Title (OPTION 1 toggles the music), A starts. A bot reads the game's state
by symbol (ship_*, rk_*, gs_*), turns to the nearest rock and fires; it
thrusts (UP, and B for a while) now and then, and once wave 2 is reached
or time runs on it flies into the rocks, so lives are lost and the game
ends. PAUSE stops the game (nothing moves) and PAUSE again goes on. Checks
the log lines (rb: ...), the score sums (20/50/100 a rock), the frame rate
from the game's own "rb: fps=" lines, the screen, and that the game is
deterministic: it is recorded and replayed, every frame hash must match.

    .venv/bin/python projects/rock_blaster/test.py [--shots] [--log]

--shots also writes docs/images/rock_blaster.png, rock_blaster_title.png and
rock_blaster.gif
(otherwise every picture goes to build/shots).
"""
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lynxdev" / "src"))

from lynxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "rock_blaster.lnx"
SHOTS = HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
DOCS = ROOT / "docs" / "images" if "--shots" in sys.argv else SHOTS
REC = HERE / "build" / "bot-game.json"
W, H = 320, 204
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def arr(e, sym, n, fmt="B"):
    size = struct.calcsize("<" + fmt)
    return struct.unpack(f"<{n}{fmt}", e.read_mem(sym, n * size))


def state(e):
    act = arr(e, "rk_active", 24)
    px, py = arr(e, "rk_px", 24, "h"), arr(e, "rk_py", 24, "h")
    return {"x": arr(e, "ship_px", 1, "h")[0], "y": arr(e, "ship_py", 1, "h")[0],
            "angle": e.peek("ship_angle"), "alive": e.peek("ship_alive"),
            "rocks": [(px[i], py[i]) for i in range(24) if act[i]],
            "state": e.peek("gs_state"), "frame": arr(e, "gs_frame", 1, "H")[0]}


def wrapd(d, size):
    return (d + size // 2) % size - size // 2


def keys(s, t, suicide):
    out = ["A"]
    if s["alive"] and s["rocks"]:
        dx, dy = min(((wrapd(rx - s["x"], W), wrapd(ry - s["y"], H)) for rx, ry in s["rocks"]),
                     key=lambda r: r[0] ** 2 + r[1] ** 2)
        want = round(math.atan2(dy, dx) * 128 / math.pi) & 255
        diff = (want - s["angle"] + 128) % 256 - 128
        if diff < -2:
            out.append("LEFT")
        elif diff > 2:
            out.append("RIGHT")
        if suicide and abs(diff) < 20:
            out.append("UP")
        elif 400 < t < 440:
            out.append("B")              # B thrusts too
    return "+".join(out)


def bot_game(e, max_frames=15000):
    log, t, gif, shot = "", 0, [], False
    while "rb: gameover" not in log and t < max_frames:
        s = state(e)
        e.release()
        e.hold(keys(s, t, suicide="rb: level=3" in log or t > 7000))
        e.run(4)
        t += 4
        if 150 <= t < 550:
            gif.append(e.screenshot(None, scale=2).convert("RGB"))
        if not shot and t >= 300 and "score=" in log and len(s["rocks"]) >= 5:
            e.screenshot(DOCS / "rock_blaster.png", scale=3)
            shot = True
        log += e.log()
    e.release()
    e.run(5)
    if gif:
        small = [g.convert("P", palette=1, colors=32) for g in gif]
        small[0].save(DOCS / "rock_blaster.gif", save_all=True, append_images=small[1:],
                      duration=80, loop=0, optimize=True)
    return log + e.log(), t


with Emulator() as e:
    e.load(PROG)
    e.record_input("start", from_reset=True)
    check("boots to the title", e.run_until("rb: title", 600))
    e.run(60)
    e.screenshot(DOCS / ("rock_blaster_title.png" if DOCS != SHOTS else "title.png"), scale=3)
    pal = e.palette()
    small = e.screenshot(None)
    cols = {small.getpixel((x, y)) for x in range(160) for y in range(0, 30)}
    check("title: the yellow ROCK logo", tuple(pal[11]) in cols)
    m = e.hw()["mikey"]
    check("the tune plays on Mikey channels 2 and 3", m.get("aud2ctl", 0) & 0x18 == 0x18 and m.get("aud3ctl", 0) & 0x18 == 0x18, m)
    e.press("OPTION1")
    check("OPTION 1 turns the music off", e.run_until("rb: music=0", 20))
    m = e.hw()["mikey"]
    check("music off: channels 2 and 3 silent", m.get("aud2vol", 1) == 0 and m.get("aud3vol", 1) == 0, m)
    e.press("OPTION1")
    check("and on", e.run_until("rb: music=1", 20))
    e.press("A")
    check("A starts the game", e.run_until("rb: state=playing level=1 lives=3", 30))
    e.run(20)
    hud = e.screenshot(None)
    check("HUD: score digits drawn in white", any(hud.getpixel((x, y)) == tuple(pal[2])
                                                    for x in range(2, 44) for y in range(2, 9)))
    s = state(e)
    check("four large rocks in wave 1", len(s["rocks"]) == 4, s["rocks"])

    # pause: nothing moves while paused
    e.press("PAUSE")
    check("PAUSE pauses", e.run_until("rb: paused=1", 10))
    f0 = state(e)["frame"]
    e.run(50)
    check("paused: the game doesn't advance", state(e)["frame"] == f0)
    e.screenshot(SHOTS / "paused.png", scale=3)
    e.press("PAUSE")
    check("PAUSE again goes on", e.run_until("rb: paused=0", 10))

    log, t = bot_game(e)
    if "--log" in sys.argv:
        print(log)
    scores = [int(v) for v in re.findall(r"rb: score=(\d+)", log)]
    steps = {b - a for a, b in zip([0] + scores, scores)}
    lives = [int(v) for v in re.findall(r"rb: lives=(\d+)", log)]
    fps = [(int(f), int(late), int(b), int(r), int(p)) for f, late, b, r, p in
           re.findall(r"rb: fps=(\d+) late=(\d+) busy=(\d+)/\d+ rocks=(\d+) parts=(\d+)", log)]
    check("shooting rocks scores", scores and max(scores) >= 200, str(scores[-5:]))
    check("large 20, medium 50, small 100", {20, 50, 100} <= steps, sorted(steps))
    check("wave 2", "rb: level=2" in log, log[-300:])
    check("a life is lost, the ship respawns", lives and min(lives) < 3 and "rb: respawn" in log, str(lives))
    m = re.search(r"rb: gameover score=(\d+) level=(\d+) hi=(\d+) new=(\d)", log)
    check("game over", m is not None)
    final = int(m.group(1)) if m else 0
    check("game over score = the last score line", scores and final == scores[-1], f"{final} {scores[-1:]}")
    check("a new high score (the table starts at 1000)", m and m.group(4) == ("1" if final > 1000 else "0"))
    print(f"  played {t} frames, final score {final}, wave {m.group(2) if m else '?'}")
    print(f"  frame rate (fps, late frames, busiest frame in display lines /105, rocks, particles): {fps}")
    avg = sum(f for f, *_ in fps) / max(1, len(fps))
    check("frame rate: 50 fps average, never below 40", fps and avg >= 48 and min(f for f, *_ in fps) >= 40,
          f"avg {avg:.1f}")
    e.run(20)
    go = e.screenshot(SHOTS / "gameover.png", scale=3)
    small = e.screenshot(None)
    check("GAME OVER in red", any(small.getpixel((x, y)) == tuple(pal[9]) for x in range(40, 120) for y in range(34, 42)))
    e.press("A")
    check("A goes back to the title", e.run_until("rb: title", 200))
    e.run(60)
    e.screenshot(SHOTS / "title-hi.png", scale=3)
    rec = e.record_input("save", REC)
    print(f"  recorded {rec['frames']} frames, {rec['events']} input changes")

with Emulator() as e:
    r = e.replay(REC)
    check("replay: every frame hash matches the recorded game", r["ok"],
          f"{r.get('mismatches')} mismatches, first at {r.get('first_mismatch')}")

print(f"{'FAIL' if failures else 'PASS'}: rock_blaster")
sys.exit(1 if failures else 0)
