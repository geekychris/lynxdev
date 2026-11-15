"""Frank the Frog (Atari Lynx) - plays a game through the harness.

1. Boots to the title, A starts a game.
2. Hops straight up into the traffic: a car gets Frank, a life is lost.
3. A bot plays carefully: it stops at the start of every game loop
   (`input_update`), reads the lanes from memory (`lane_*`, `frank_probe`,
   `home_filled`, by symbol) and hops when the next lane is safe: across the
   road, onto logs and rafts, into a free pond. Until all four ponds are
   filled (level 2).
4. PAUSE freezes the game; the timer runs out when it is set to 2 frames
   (`time_left`): a life lost to time.
5. Plays carelessly until the game is over, A returns to the title.

Frame rate: the game counts its loops and the vertical blanks it missed
(`loop_count`, `late_frames`). Pictures go to build/shots (with --shots also
docs/images/frank_the_frog.png and .gif in the repo root). --debug prints the bot's last moves
before each death.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lynxdev" / "src"))

from lynxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "frank_the_frog.lnx"
SHOTS = HERE / "build" / "shots"
SHOTS.mkdir(parents=True, exist_ok=True)
DOCS = ROOT / "docs" / "images" if "--shots" in sys.argv else None
DEBUG = "--debug" in sys.argv
NUM_LANES, MAXOBJ = 10, 3                 # lanes.h, game.h
HOMES = [40, 120, 216, 280]               # pond centres (lanes.c)
HZ = 50
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def arr(e, sym, n, fmt="B"):
    size = struct.calcsize("<" + fmt)
    return struct.unpack(f"<{n}{fmt}", e.read_mem(sym, n * size))


class World:
    """The lanes as the game keeps them (lanes.c): obj_x[lane * 3 + j], a
    circular track of lane_period pixels per lane."""

    def __init__(self, e):
        self.e = e
        self.static = None

    def read_static(self):
        e = self.e
        rows, dirs, sps = arr(e, "lane_row", 10), arr(e, "lane_dir", 10, "b"), arr(e, "lane_speed", 10)
        rivers, nums, ws = arr(e, "lane_river", 10), arr(e, "lane_num", 10), arr(e, "lane_width", 10)
        pers = arr(e, "lane_period", 10, "h")
        self.static = [dict(row=rows[i], d=dirs[i], sp=sps[i], river=rivers[i], w=ws[i], per=pers[i],
                            num=nums[i]) for i in range(NUM_LANES)]

    def read(self):
        e = self.e
        if self.static is None:
            self.read_static()
        xs = arr(e, "obj_x", NUM_LANES * MAXOBJ, "h")
        lanes = {}
        for i, L in enumerate(self.static):
            lanes[L["row"]] = dict(L, xs=xs[i * MAXOBJ:i * MAXOBJ + L["num"]])
        fx, frow, state = struct.unpack("<3h", e.read_mem("frank_probe", 6))
        filled = list(e.read_mem("home_filled", 4))
        return fx, frow, state, lanes, filled


def objs_at(L, t):
    """Object x positions after t more ticks (lanes.c lanes_tick)."""
    out = []
    lo = -L["w"] if L["d"] > 0 else -16 - L["w"]
    for x in L["xs"]:
        out.append((x + L["d"] * L["sp"] * t - lo) % L["per"] + lo)
    return out


def ride(L):
    return L["d"] * L["sp"]


class Planner:
    """Looks ahead through the game's own rules (main.c game_tick, lanes.c):
    a depth-first search over hops, one hop every other tick (the pad is
    released in between), for the moves that keep Frank alive longest and
    take him furthest up, into a free pond if it can."""

    DEPTH = 9                     # hops (2 ticks each)

    def __init__(self, lanes, filled):
        self.lanes, self.filled = lanes, filled
        self.cache = {}
        self.memo = {}

    def objs(self, row, k):
        key = (row, k)
        if key not in self.cache:
            self.cache[key] = objs_at(self.lanes[row], k)
        return self.cache[key]

    def tick(self, row, x, k):
        """Frank at (row, x) when the game checks at tick k: the new x, or
        None (dead), or "home"."""
        L = self.lanes.get(row)
        if row == 0:
            return "home" if any(not f and cx - 14 <= x + 8 <= cx + 14
                                 for cx, f in zip(HOMES, self.filled)) else None
        if L is None:
            return x
        w = L["w"]
        if L["river"]:              # on a log/raft (the game's test, 2 pixels of margin)
            if not any(x + 16 - 6 > ox and x + 6 < ox + w for ox in self.objs(row, k)):
                return None
            x += ride(L)
            return x if -4 <= x and x + 16 <= 324 else None
        for ox in self.objs(row, k):  # cars (the game's test, 2 pixels of margin)
            if x + 16 - 5 > ox + 2 - 2 and x + 5 < ox + w - 2 + 2:
                return None
        return x

    def hop(self, row, x, move):
        c = int(x / 16)               # C division: rounds toward 0
        if move == "UP":
            row -= 1
        elif move == "DOWN":
            row += 1
        elif move == "LEFT":
            c -= 1
        elif move == "RIGHT":
            c += 1
        if not (0 <= c < 20 and 0 <= row < 13):
            return row if move in ("LEFT", "RIGHT") else row, x
        return row, c * 16

    def leaf(self, row, x):
        """How good the end of a line of play is: high up, and not riding
        towards the edge (or along the top lane with no free pond ahead)."""
        sc = 100 - row * 10
        L = self.lanes.get(row)
        if L and L["river"]:
            d, sp = L["d"], L["sp"]
            left = (308 - x) // sp if d > 0 else (x + 4) // sp
            if left < 24:
                sc -= 60
            if row == 1 and not any(not f and (cx - 8 - x) * d >= -12 for cx, f in zip(HOMES, self.filled)):
                sc -= 60
        return sc

    def value(self, row, x, k, depth):
        """Best (score, first move) from Frank at (row, x) before tick k."""
        key = (row, x, depth)
        if key in self.memo:
            return self.memo[key]
        best = (-1000 - depth, None)                    # dies
        if depth == 0:
            best = (self.leaf(row, x), None)
        else:
            for move in ("UP", None, "LEFT", "RIGHT", "DOWN"):
                r, nx = (row, x) if move is None else self.hop(row, x, move)
                a = self.tick(r, nx, k)
                if a == "home":
                    sc = (10000 + depth, move)
                elif a is None:
                    continue
                else:
                    b = self.tick(r, a, k + 1)          # the released tick
                    if b == "home":
                        sc = (10000 + depth, move)
                    elif b is None:
                        continue
                    else:
                        sc = (self.value(r, b, k + 2, depth - 1)[0], move)
                if sc[0] > best[0]:
                    best = sc
        self.memo[key] = best
        return best


def choose(fx, frow, lanes, filled):
    """The hop to make (None: wait) for the world as the game will see it."""
    return Planner(lanes, filled).value(frow, fx, 1, Planner.DEPTH)[1]


def advance(fx, frow, lanes):
    """The pad set now is seen by the game's next tick (the emulator takes
    input at the frame boundary): move the world on by the tick in between."""
    L = lanes.get(frow)
    if L and L["river"] and any(fx + 12 > ox and fx + 4 < ox + L["w"] for ox in objs_at(L, 1)):
        fx += ride(L)
    for L in lanes.values():
        L["xs"] = objs_at(L, 1)
    return fx, frow, lanes


def tick(e, hop=None):
    """One game loop: the pad as given, then on to the next loop's input_update."""
    e.pad(hop)
    r = e.run_to_addr("input_update", 10)
    if r["pc"] != e.addr("input_update"):
        raise SystemExit(f"the game loop did not come round: {r}")
    return r


def bot(e, until, careless=False, max_ticks=20000, gif=None):
    """Play until `until` is in the log (or game over). Returns the log."""
    w = World(e)
    log, history, last = "", [], None
    for t in range(max_ticks):
        fx, frow, state, lanes, filled = w.read()
        hop = None
        if state == 1 and last is None:          # one tick released between hops
            hop = "UP" if careless else choose(*advance(fx, frow, lanes), filled)
        last = hop
        history.append((t, frow, fx, hop, lanes[frow]["xs"] if frow in lanes else None))
        tick(e, hop)
        if gif is not None and t % 2 == 0 and len(gif) < 250:
            gif.append(e.screenshot(None).convert("RGB"))
        new = e.log()
        log += new
        if DEBUG:
            lf = e.peek16("late_frames")
            if lf != getattr(bot, "lf", lf):
                print("LATE", lf - bot.lf, "state", state, "row", frow, repr(new[-60:]))
            bot.lf = lf
        if "level=" in new:
            w.static = None                      # new lanes for the new level
        if DEBUG and "death" in new:
            print("DEATH", [ln for ln in new.splitlines() if "death" in ln])
            for h in history[-16:]:
                print("   ", h)
        if until in log or "state=gameover" in log:
            break
    e.pad(None)
    return log


def save(img, name, scale=3):
    img = img.resize((img.width * scale, img.height * scale), 0)
    img.save(SHOTS / name)
    return img


with Emulator() as e:
    e.load(PROG)
    check("boots to the title", e.run_until("frank: title", 600))
    e.run(60)
    title = e.screenshot(SHOTS / "title.png", scale=3)
    check("title screen drawn", len(e.screenshot(None).getcolors(1 << 16) or []) >= 8)

    e.press("A")
    check("A starts the game", e.run_until("frank: state=playing score=0 lives=3 level=1", 30))
    fc0 = struct.unpack("<HH", e.read_mem("loop_count", 2) + e.read_mem("late_frames", 2))

    # 2. straight up into the traffic
    e.run_to_addr("input_update", 5)
    log = bot(e, "death splat", careless=True, max_ticks=600)
    check("hopping scores 10 a row", "row=11 col=10 score=10" in log and "row=10 col=10 score=20" in log, log[-300:])
    check("a car hits Frank (death splat)", "death splat" in log)
    check("a life is lost", e.run_until("frank: lives=2", 60))
    e.run_until("frank: state=playing", 60)
    e.run_to_addr("input_update", 5)

    # 3. the careful bot: all four ponds
    gif = [] if DOCS else None
    log = bot(e, "frank: level=2", gif=gif)
    homes = re.findall(r"home slot=(\d)", log)
    check("Frank reaches a pond (+50)", bool(homes), log[-400:])
    check("all four ponds: level 2 (+1000)", "frank: level=2" in log,
          "; ".join(ln for ln in log.splitlines() if "death" in ln or "home" in ln))
    scores = [int(s) for s in re.findall(r"frank: (?:row=\d+ col=\d+ )?score=(\d+)", log)]
    check("score passes 1000 with the level bonus", scores and max(scores) >= 1000, str(scores[-5:]))
    if DOCS:
        gif and gif[0].save(DOCS / "frank_the_frog.gif", save_all=True, append_images=gif[1:],
                            duration=40, loop=0, optimize=True)

    # a picture mid-game: a few careful ticks on level 2
    bot(e, "frank: row=6", max_ticks=1500)
    e.run(1)
    play = e.screenshot(SHOTS / "play.png", scale=3)
    if DOCS:
        e.screenshot(DOCS / "frank_the_frog.png", scale=3)

    # 4. pause, then the timer
    e.press("PAUSE")
    check("PAUSE pauses", e.run_until("frank: paused", 10))
    probe = e.read_mem("frank_probe", 6)
    xs = e.read_mem("obj_x", 60)
    e.run(30)
    check("nothing moves while paused", e.read_mem("obj_x", 60) == xs and e.read_mem("frank_probe", 6) == probe)
    e.run(1)
    e.screenshot(SHOTS / "paused.png", scale=3)
    e.press("PAUSE")
    check("PAUSE again resumes", e.run_until("frank: resumed", 10))
    lives = e.peek("score_lives")
    e.run_until("frank: state=playing", 200)
    e.write_mem("time_left", struct.pack("<H", 2))
    check("the timer runs out: death time", e.run_until("death time", 10))
    check("... and costs a life", e.run_until(f"frank: lives={lives - 1}", 60) or lives == 1)

    # 5. careless to the end
    e.run_until("frank: state=playing", 200)
    e.run_to_addr("input_update", 5)
    log = bot(e, "state=gameover", careless=True)
    check("game over", "state=gameover" in log)
    deaths = re.findall(r"death (\w+)", log)
    check("deaths by traffic or water", deaths and set(deaths) <= {"splat", "splash", "carried", "bank", "time"},
          str(deaths))
    m = re.search(r"gameover score=(\d+)", log)
    final = int(m.group(1)) if m else 0
    check("final score kept", final >= 1000, str(final))
    e.run(40)
    e.screenshot(SHOTS / "gameover.png", scale=3)
    loops, late = struct.unpack("<HH", e.read_mem("loop_count", 2) + e.read_mem("late_frames", 2))
    loops, late = loops - fc0[0], late - fc0[1]
    fps = HZ * loops / (loops + late) if loops else 0
    print(f"  frame rate: {fps:.1f} fps at {HZ} Hz ({late} late frames in {loops + late})")
    check("steady frame rate (>= 49 fps)", fps >= 49, f"{fps:.1f}")
    e.press("A")
    check("A returns to the title", e.run_until("frank: title", 60))
    e.run(30)
    hi = e.read_mem("score_hi", 4)
    check("high score kept", struct.unpack("<I", hi)[0] == final, str(struct.unpack("<I", hi)[0]))
    e.screenshot(SHOTS / "title-hi.png", scale=3)
    if failures:
        print("\n".join(e.debug_log[-20:]))

print(f"{'FAIL' if failures else 'PASS'}: frank_the_frog")
sys.exit(1 if failures else 0)
