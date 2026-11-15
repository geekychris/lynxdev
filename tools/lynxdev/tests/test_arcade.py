"""./lynx arcade: --list finds every game (dynamically, from games/ and with
--all projects/) and a game launches headless from the arcade's build+play
path. Run by ./lynx selftest."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
LYNX = str(ROOT / "lynx")
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{'' if ok or not detail else '  (' + str(detail)[:400] + ')'}")
    if not ok:
        failures.append(name)


r = subprocess.run([LYNX, "arcade", "--list", "--json", "--all"], capture_output=True, text=True)
check("arcade --list --json runs", r.returncode == 0, r.stderr)
data = json.loads(r.stdout) if r.returncode == 0 else {"games": []}
names = {g["name"] for g in data["games"]}
expected = {p.parent.name for p in (ROOT / "games").glob("*/Makefile")} | {"hello", "sdktest"}
check("lists every game in games/ and projects/", names == expected, f"{sorted(names)} vs {sorted(expected)}")
check("every entry has a title and a description", all(g["title"] and g["description"] for g in data["games"]),
      [g["name"] for g in data["games"] if not (g["title"] and g["description"])])
check("submodule flag matches games/", data.get("submodule") == bool(expected - {"hello", "sdktest"}))

r = subprocess.run([LYNX, "arcade", "--list"], capture_output=True, text=True)
lines = [ln for ln in r.stdout.splitlines() if ln.strip()[:1].isdigit()]
check("arcade --list (games only) prints one numbered line per game", len(lines) == len(expected) - 2,
      r.stdout)
if len(expected) == 2:
    check("arcade --list says when games/ isn't checked out", "isn't checked out" in r.stdout, r.stdout)

target = sorted(expected - {"hello", "sdktest"})[:1] or ["hello"]
shot = ROOT / "build" / "arcade-test.png"
r = subprocess.run([LYNX, "arcade", "--play", target[0], "--headless", "--frames", "240", "-o", str(shot)],
                   capture_output=True, text=True, timeout=600)
res = json.loads(r.stdout.strip().splitlines()[-1]) if r.returncode == 0 and r.stdout.strip() else {}
check(f"arcade --play {target[0]} --headless builds and runs it", r.returncode == 0 and res.get("frames") == 240,
      r.stdout[-400:] + r.stderr[-400:])
check("the game drew something and logged", res.get("colors", 0) >= 2 and res.get("log_lines", 0) >= 1, res)
r = subprocess.run([LYNX, "arcade", "--play", "no_such_game"], capture_output=True, text=True)
check("unknown game is an error", r.returncode != 0)
print(f"{'FAIL' if failures else 'PASS'}: arcade")
sys.exit(1 if failures else 0)
