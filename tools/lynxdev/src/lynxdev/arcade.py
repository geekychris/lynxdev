"""The arcade: a terminal menu of every game in games/ (the lynx_games
submodule), built on demand and played in the emulator window.

    ./lynx arcade                 menu: pick a number to play, i<n> for controls, q quits
    ./lynx arcade --all           also the platform's projects/ (hello, sdktest)
    ./lynx arcade --list [--json] what's there, no menu
    ./lynx arcade --play rock_blaster          straight into one game (window)
    ./lynx arcade --play rock_blaster --headless --frames 300 [-o shot.png]
    ./lynx arcade --devbench      also run DevBench (:3690): the game shows in the web UI

Games are discovered every time the menu is shown, so new games pulled into
games/ appear without restarting. A game's title, one-line description and
controls come from `<game>/game.json` if it has one:

    {"title": "Rock Blaster", "description": "vector rock shooter", "controls": ["d-pad: turn", "A: fire"]}

otherwise from its README.md: the first heading (without "(Atari Lynx)"),
the first sentence of the first paragraph, and the rows of the table under
"## Controls".
"""
from __future__ import annotations

import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

from .emu import PROJECT_DIRS, repo_root

ROOT = repo_root()


# ---------------------------------------------------------------- discovery

def _readme_meta(readme: Path) -> dict:
    meta = {"title": None, "description": "", "controls": []}
    if not readme.is_file():
        return meta
    lines = readme.read_text(errors="replace").splitlines()
    para: list[str] = []
    i = 0
    while i < len(lines):
        ln = lines[i]
        if meta["title"] is None and ln.startswith("# "):
            meta["title"] = re.sub(r"\s*\((Atari )?Lynx\)\s*$", "", ln[2:].strip())
        elif meta["title"] is not None and not meta["description"]:
            if ln.strip() and not ln.startswith(("#", "!", "<", "```", "|", ">")):
                para.append(ln.strip())
            elif para:
                text = " ".join(para)
                m = re.match(r"(.+?[.!?])(\s|$)", text)
                meta["description"] = (m.group(1) if m else text).strip()
        if re.match(r"^##\s+Controls\b", ln, re.I):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("## "):
                row = lines[j].strip()
                if row.startswith("|") and not re.match(r"^\|[\s:|-]+\|$", row):
                    cells = [c.strip() for c in row.strip("|").split("|")]
                    meta["controls"].append(cells)
                elif row.startswith(("- ", "* ")):
                    meta["controls"].append([row[2:].strip()])
                j += 1
            if meta["controls"] and len(meta["controls"][0]) > 1:
                meta["controls"] = meta["controls"][1:]        # the table's header row
            meta["controls"] = [" · ".join(c for c in cells if c) for cells in meta["controls"]]
            i = j
            continue
        i += 1
    if not meta["description"] and para:
        meta["description"] = " ".join(para)
    d = re.sub(r"[`*]", "", meta["description"])
    d = re.sub(r"^(A |The )?[Pp]ort of [^:]+:\s*", "", d)        # "Port of amiga_mcp/...: hop Frank..."
    meta["description"] = d[:1].upper() + d[1:]
    return meta


def games(include_projects: bool = False) -> list[dict]:
    """Every game (games/*/ with a Makefile), plus projects/ with include_projects."""
    out = []
    for d in PROJECT_DIRS:
        if d == "projects" and not include_projects:
            continue
        base = ROOT / d
        if not base.is_dir():
            continue
        for p in sorted(base.iterdir()):
            if not (p / "Makefile").is_file():
                continue
            meta = _readme_meta(p / "README.md")
            gj = p / "game.json"
            if gj.is_file():
                try:
                    meta.update({k: v for k, v in json.loads(gj.read_text()).items() if v})
                except json.JSONDecodeError as ex:
                    meta["description"] = f"(bad game.json: {ex})"
            lnx = p / "build" / f"{p.name}.lnx"
            out.append({"name": p.name, "dir": d, "path": str(p),
                        "title": meta["title"] or p.name.replace("_", " ").title(),
                        "description": meta["description"], "controls": meta["controls"],
                        "built": lnx.is_file(), "has_test": (p / "test.py").is_file()})
    return out


def submodule_missing() -> bool:
    return not any((ROOT / "games").glob("*/Makefile"))


# ---------------------------------------------------------------- build + play

def build(game: dict, quiet: bool = True) -> bool:
    log = ROOT / "build" / f"arcade-build-{game['name']}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    with open(log, "w") as f:
        r = subprocess.run([str(ROOT / "bin" / "lynx-make"), "-C", game["path"]],
                           stdout=f, stderr=subprocess.STDOUT)
    if r.returncode:
        print(log.read_text()[-3000:])
        print(f"build of {game['name']} failed (log: {log.relative_to(ROOT)})")
        return False
    if not quiet:
        print(f"built {game['name']}")
    return True


def play_window(game: dict) -> int:
    """Open the game in a window; returns when the window is closed (Esc)."""
    from .cli import CONTROLS
    print(f"\n{game['title']}: window open. Esc (or closing the window) comes back here.", flush=True)
    for c in game["controls"][:10]:
        print(f"   {c}")
    print(f"   window keys: {CONTROLS}")
    lnx = Path(game["path"]) / "build" / f"{game['name']}.lnx"
    exe = ROOT / ".venv" / "bin" / "lynxdev"
    cmd = [str(exe) if exe.exists() else sys.executable, *([] if exe.exists() else ["-m", "lynxdev.cli"]),
           "serve", str(lnx), "--window", "--mode", "running", "--quiet"]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL)
    try:
        return proc.wait()
    except KeyboardInterrupt:
        proc.terminate()
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()
        return 130


def play_headless(game: dict, frames: int, shot: str | None) -> dict:
    from .emu import Emulator
    lnx = Path(game["path"]) / "build" / f"{game['name']}.lnx"
    with Emulator() as emu:
        emu.load(lnx)
        emu.run(frames)
        img = emu.screenshot(shot) if shot else emu.screenshot()
        log = emu.log(0)
        return {"game": game["name"], "frames": frames, "colors": len(img.getcolors(1 << 16) or []),
                "log_lines": len(log.splitlines()), "log_tail": log.splitlines()[-3:], "shot": shot}


# ---------------------------------------------------------------- devbench

def _port_busy(port: int) -> bool:
    with socket.socket() as s:
        return s.connect_ex(("127.0.0.1", port)) == 0


def _bench_root(port: int):
    """The repo root a DevBench on the port serves (None: not a DevBench)."""
    import urllib.request
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/api/health", timeout=2) as r:
            return json.loads(r.read()).get("root")
    except Exception:
        return None


def start_devbench(port: int = 3690):
    """Our own DevBench process, on the first free port from 3690 (or None if
    a DevBench of this checkout already runs: the game's session shows there)."""
    for p in range(port, port + 10):
        if not _port_busy(p):
            port = p
            break
        if _bench_root(p) == str(ROOT):
            print(f"DevBench already running: http://127.0.0.1:{p}/ (the game's session shows there)", flush=True)
            return None
    else:
        print("no free port for DevBench (3690-3699)", flush=True)
        return None
    log = open(ROOT / "build" / "arcade-devbench.log", "w")
    exe = ROOT / ".venv" / "bin" / "lynxdev"
    proc = subprocess.Popen([str(exe), "devbench", "--port", str(port)], stdout=log, stderr=subprocess.STDOUT)
    for _ in range(50):
        if _port_busy(port):
            break
        time.sleep(0.1)
    print(f"DevBench: http://127.0.0.1:{port}/ (pick the window session in the session list)", flush=True)
    return proc


# ---------------------------------------------------------------- menu

def _fit(s: str, n: int) -> str:
    return s if len(s) <= n else s[:n - 1] + "…"


def print_list(gs: list[dict], width: int | None = None):
    width = width or shutil.get_terminal_size((100, 24)).columns
    names = [g["title"] + ("" if g["dir"] == "games" else " (projects/)") for g in gs]
    tw = max([len(n) for n in names] + [5])
    for i, (g, name) in enumerate(zip(gs, names), 1):
        line = f"{i:>3}  {name:<{tw}}  "
        print(line + _fit(g["description"], max(20, width - len(line) - 1)))


def menu(include_projects: bool, devbench: bool) -> int:
    bench = start_devbench() if devbench else None
    try:
        while True:
            gs = games(include_projects)
            print("\n\033[1mLynx arcade\033[0m" + (f"  ({len(gs)} games)" if gs else ""))
            if submodule_missing():
                print("  games/ (the lynx_games submodule) isn't checked out: git submodule update --init"
                      + ("" if include_projects else "  (--all shows hello and sdktest)"))
            if not gs:
                return 1
            print_list(gs)
            try:
                choice = input("\nnumber = play, i<number> = info/controls, r = rescan, q = quit > ").strip().lower()
            except (EOFError, KeyboardInterrupt):
                print()
                return 0
            if choice in ("q", "quit", "exit"):
                return 0
            if choice in ("", "r"):
                continue
            info = choice.startswith("i")
            num = choice[1:].strip() if info else choice
            g = next((x for x in gs if x["name"] == num), None)
            if g is None and num.isdigit() and 1 <= int(num) <= len(gs):
                g = gs[int(num) - 1]
            if g is None:
                print(f"no game {choice!r}")
                continue
            if info:
                print(f"\n{g['title']} ({g['dir']}/{g['name']})\n  {g['description']}")
                for c in g["controls"]:
                    print(f"  - {c}")
                print(f"  README: {g['path']}/README.md")
                continue
            print(f"building {g['name']}…")
            if build(g):
                play_window(g)
    finally:
        if bench is not None:
            bench.terminate()
            try:
                bench.wait(5)
            except subprocess.TimeoutExpired:
                bench.kill()


def main(argv=None) -> int:
    import argparse
    ap = argparse.ArgumentParser(prog="lynx arcade", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--all", action="store_true", help="include projects/ (hello, sdktest)")
    ap.add_argument("--list", action="store_true", help="list the games and exit")
    ap.add_argument("--json", action="store_true", help="with --list: JSON")
    ap.add_argument("--play", metavar="GAME", help="play one game (name or number) without the menu")
    ap.add_argument("--headless", action="store_true", help="with --play: run headless (tests)")
    ap.add_argument("--frames", type=int, default=300, help="with --headless: frames to run")
    ap.add_argument("-o", "--output", help="with --headless: screenshot path")
    ap.add_argument("--devbench", action="store_true", help="also run DevBench (:3690) so the game shows in the web UI")
    a = ap.parse_args(argv)
    if a.list:
        gs = games(a.all)
        if a.json:
            print(json.dumps({"games": gs, "submodule": not submodule_missing()}, indent=1))
        else:
            if submodule_missing():
                print("games/ (the lynx_games submodule) isn't checked out: git submodule update --init")
            print_list(gs, 200 if not sys.stdout.isatty() else None)
        return 0
    if a.play:
        gs = games(True)
        g = next((x for x in gs if x["name"] == a.play), None)
        if g is None and a.play.isdigit():
            only = games(a.all)
            g = only[int(a.play) - 1] if 1 <= int(a.play) <= len(only) else None
        if g is None:
            print(f"no game {a.play!r}: ./lynx arcade --list", file=sys.stderr)
            return 1
        if not build(g):
            return 1
        if a.headless:
            print(json.dumps(play_headless(g, a.frames, a.output)))
            return 0
        bench = start_devbench() if a.devbench else None
        try:
            play_window(g)
        finally:
            if bench is not None:
                bench.terminate()
        return 0
    if not sys.stdin.isatty():
        print("the arcade menu needs a terminal (use --list or --play GAME)", file=sys.stderr)
        return 2
    return menu(a.all, a.devbench)


if __name__ == "__main__":
    sys.exit(main())
