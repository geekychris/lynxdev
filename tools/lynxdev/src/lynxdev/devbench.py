"""DevBench: web UI + REST API + MCP over HTTP, on one port (default :3690).

    ./lynx devbench [--port 3690] [--open]

    /                                        web UI (screen, log, input, debugger, memory,
                                             Suzy/Mikey registers, palette, sprite chain, console)
    /api/health
    /api/commands                            every session command with help
    /api/projects                            projects (projects/ and games/) and their build output
    /api/projects/{name}/build   POST        build (cc65 in Docker)
    /api/projects/{name}/test    POST        ./lynx test <name>
    /arcade                                  the arcade page: every game in games/ with its picture,
                                             description, controls, Build / Play in page / Play in window
    /api/arcade                              the games (?all=1: also projects/), built or not, last error
    /api/arcade/picture/{name}.png           a game's picture (games/<g>/images/, docs/images/games/)
    /api/arcade/build            POST        {"game"}: build one game ({} = every game)
    /api/arcade/play             POST        {"game", "window": false}: build if needed, start a running
                                             session ({"url"}: the DevBench page showing it)
    /api/sessions                GET|POST    list | start {"project", "window", "rotate", "mode"}
    /api/sessions/{pid}          DELETE      stop a session
    /api/sessions/{pid}/select   POST        make it the default for /api/cmd
    /api/cmd/{command}           GET|POST    run a session command (JSON body or ?k=v)
    /api/sessions/{pid}/cmd/{command}        ... on a specific session
    /api/screen.png                          current frame (?scale=3)
    /api/sessions/{pid}/screen.png           ... of a specific session
    /mcp                                     the MCP server (streamable HTTP)
"""
from __future__ import annotations

import base64
import json
import os
import subprocess
import threading
from pathlib import Path

from starlette.concurrency import run_in_threadpool
from starlette.requests import Request
from starlette.responses import FileResponse, JSONResponse, Response
from starlette.routing import Route

from .control import ProtocolError, SessionClient, list_sessions
from .emu import all_projects, project_dir, repo_root
from .session import resolve_program

ROOT = repo_root()
WEB = Path(__file__).parent / "web"


class Bench:
    def __init__(self):
        self.lock = threading.Lock()
        self.clients: dict[int, SessionClient] = {}
        self.locks: dict[int, threading.Lock] = {}
        self.selected: int | None = None
        self.arcade_pid: int | None = None      # the headless session the arcade page started last
        self.arcade_errors: dict[str, str] = {}  # game -> why its last build/play failed

    def pid_for(self, pid: int | None) -> int:
        ss = list_sessions()
        pids = [s["pid"] for s in ss]
        if pid is None:
            pid = self.selected if self.selected in pids else (pids[-1] if pids else None)
        if pid is None:
            raise LookupError("no running session: start one (POST /api/sessions) or ./lynx run <project>")
        if pid not in pids:
            raise LookupError(f"no session {pid}")
        return pid

    def client(self, pid: int) -> tuple[SessionClient, threading.Lock]:
        with self.lock:
            c = self.clients.get(pid)
            if c is None or not c.alive:
                info = next(s for s in list_sessions() if s["pid"] == pid)
                c = SessionClient(info["control_port"])
                c.info = info
                self.clients[pid] = c
                self.locks[pid] = threading.Lock()
            return c, self.locks[pid]

    def call(self, pid: int | None, cmd: str, args: dict):
        pid = self.pid_for(pid)
        c, lk = self.client(pid)
        with lk:
            try:
                return c.call(cmd, **args)
            except (OSError, ProtocolError) as ex:
                if isinstance(ex, OSError):
                    self.clients.pop(pid, None)
                raise


bench = Bench()


def ok(data, status: int = 200):
    return JSONResponse(data, status_code=status)


def err(msg: str, status: int):
    return JSONResponse({"error": msg}, status_code=status)


def _coerce(v: str):
    try:
        return json.loads(v)
    except json.JSONDecodeError:
        return v


async def args_of(request: Request) -> dict:
    args = {k: _coerce(v) for k, v in request.query_params.items()}
    if request.method == "POST":
        body = await request.body()
        if body.strip():
            data = json.loads(body)
            if not isinstance(data, dict):
                raise ValueError("body must be a JSON object")
            args.update(data)
    return args


async def call(pid, cmd: str, args: dict):
    try:
        return ok(await run_in_threadpool(bench.call, pid, cmd, args))
    except LookupError as ex:
        return err(str(ex), 404)
    except ProtocolError as ex:
        return err(str(ex), 400)
    except OSError as ex:
        return err(f"session connection failed: {ex}", 502)


async def index(request: Request):
    return FileResponse(WEB / "index.html")


async def health(request: Request):
    return ok({"ok": True, "sessions": len(list_sessions()), "root": str(ROOT)})


async def commands(request: Request):
    return await call(None, "help", {})


async def projects(request: Request):
    out = []
    for d in all_projects():
        built = sorted(str(p.relative_to(ROOT)) for ext in ("*.lnx",) for p in (d / "build").glob(ext))
        out.append({"name": d.name, "dir": d.parent.name, "built": built, "test": (d / "test.py").exists()})
    return ok(out)


def _run(cmd, timeout=1800):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    return {"ok": r.returncode == 0, "exit": r.returncode, "output": (r.stdout + r.stderr)[-20000:]}


async def build(request: Request):
    name = request.path_params["name"]
    proj = project_dir(name)
    if proj is None:
        return err(f"no project {name} in projects/ or games/", 404)
    return ok(await run_in_threadpool(_run, [str(ROOT / "bin" / "lynx-make"), "-C", str(proj)]))


async def test(request: Request):
    name = request.path_params["name"]
    if project_dir(name) is None:
        return err(f"no project {name} in projects/ or games/", 404)
    return ok(await run_in_threadpool(_run, [str(ROOT / "lynx"), "test", name]))


async def sessions(request: Request):
    if request.method == "GET":
        ss = list_sessions()
        for s in ss:
            s["selected"] = s["pid"] == (bench.selected or (ss[-1]["pid"] if ss else None))
        return ok(ss)
    a = await args_of(request)
    try:
        prog = resolve_program(a.get("project")) if a.get("project") else None
    except FileNotFoundError as ex:
        return err(str(ex), 404)

    def spawn():
        c = SessionClient.spawn(prog, window=bool(a.get("window")), rotate=a.get("rotate"), mode=a.get("mode"))
        with bench.lock:
            bench.clients[c.info["pid"]] = c
            bench.locks[c.info["pid"]] = threading.Lock()
            bench.selected = c.info["pid"]
        return c.info
    try:
        return ok(await run_in_threadpool(spawn))
    except ProtocolError as ex:
        return err(str(ex), 500)


async def session_one(request: Request):
    pid = request.path_params["pid"]
    res = await call(pid, "quit", {})
    with bench.lock:
        c = bench.clients.pop(pid, None)
    if c is not None and c.proc is not None:
        await run_in_threadpool(c.close, True)
    return res


async def select(request: Request):
    pid = request.path_params["pid"]
    try:
        bench.pid_for(pid)
    except LookupError as ex:
        return err(str(ex), 404)
    bench.selected = pid
    return ok({"selected": pid})


async def run_cmd(request: Request):
    try:
        a = await args_of(request)
    except (ValueError, json.JSONDecodeError) as ex:
        return err(str(ex), 400)
    return await call(request.path_params.get("pid"), request.path_params["command"], a)


async def screen_png(request: Request):
    # only "scale": the UI adds a cache-busting ?t= that screenshot() would reject
    a = {k: _coerce(v) for k, v in request.query_params.items() if k == "scale"}
    try:
        res = await run_in_threadpool(bench.call, request.path_params.get("pid"), "screenshot", a)
    except LookupError as ex:
        return err(str(ex), 404)
    except (ProtocolError, OSError) as ex:
        return err(str(ex), 400)
    return Response(base64.b64decode(res["png_b64"]), media_type="image/png",
                    headers={"Cache-Control": "no-store"})


# ---------------------------------------------------------------- arcade

async def arcade_page(request: Request):
    return FileResponse(WEB / "arcade.html")


def _arcade_entry(g: dict) -> dict:
    out = {k: g[k] for k in ("name", "dir", "title", "description", "controls", "built", "has_test")}
    out["picture"] = f"/api/arcade/picture/{g['name']}.png" if g["picture"] else None
    out["error"] = bench.arcade_errors.get(g["name"])
    return out


async def arcade_list(request: Request):
    from . import arcade
    include = request.query_params.get("all") in ("1", "true", "yes")
    gs = await run_in_threadpool(arcade.games, include)
    return ok({"games": [_arcade_entry(g) for g in gs], "submodule": not arcade.submodule_missing(),
               "window_keys": _window_keys()})


def _window_keys() -> str:
    from .cli import CONTROLS
    return CONTROLS


async def arcade_picture(request: Request):
    from . import arcade
    g = await run_in_threadpool(arcade.find, request.path_params["name"])
    if g is None or not g["picture"]:
        return err(f"no picture for {request.path_params['name']}", 404)
    return FileResponse(ROOT / g["picture"], headers={"Cache-Control": "max-age=60"})


def _build_game(g: dict) -> dict:
    res = _run([str(ROOT / "bin" / "lynx-make"), "-C", g["path"]])
    if res["ok"]:
        bench.arcade_errors.pop(g["name"], None)
    else:
        tail = [ln for ln in res["output"].splitlines() if ln.strip()][-3:]
        bench.arcade_errors[g["name"]] = "build failed: " + " / ".join(tail)[-300:]
    return {"game": g["name"], **res}


async def arcade_build(request: Request):
    from . import arcade
    try:
        a = await args_of(request)
    except (ValueError, json.JSONDecodeError) as ex:
        return err(str(ex), 400)
    if a.get("game"):
        g = await run_in_threadpool(arcade.find, a["game"])
        if g is None:
            return err(f"no game {a['game']} (GET /api/arcade)", 404)
        return ok(await run_in_threadpool(_build_game, g))
    results = [await run_in_threadpool(_build_game, g) for g in await run_in_threadpool(arcade.games)]
    return ok({"ok": all(r["ok"] for r in results),
               "results": [{"game": r["game"], "ok": r["ok"], "exit": r["exit"]} for r in results]})


async def arcade_play(request: Request):
    """Build the game if it isn't built, start it running in a session (headless: shown in
    the DevBench page, the previous one the arcade started is stopped; window: its own window)."""
    from . import arcade
    try:
        a = await args_of(request)
    except (ValueError, json.JSONDecodeError) as ex:
        return err(str(ex), 400)
    g = await run_in_threadpool(arcade.find, a.get("game") or "")
    if g is None:
        return err(f"no game {a.get('game')!r} (GET /api/arcade)", 404)
    window = bool(a.get("window"))
    if not g["built"] or a.get("rebuild"):
        b = await run_in_threadpool(_build_game, g)
        if not b["ok"]:
            return JSONResponse({"error": bench.arcade_errors[g["name"]], "output": b["output"][-3000:]},
                                status_code=500)
    lnx = Path(g["path"]) / "build" / f"{g['name']}.lnx"

    def spawn():
        c = SessionClient.spawn(str(lnx), window=window, mode="running")
        with bench.lock:
            bench.clients[c.info["pid"]] = c
            bench.locks[c.info["pid"]] = threading.Lock()
            bench.selected = c.info["pid"]
        return c

    try:
        c = await run_in_threadpool(spawn)
    except ProtocolError as ex:
        bench.arcade_errors[g["name"]] = f"didn't start: {ex}"
        return err(bench.arcade_errors[g["name"]], 500)
    bench.arcade_errors.pop(g["name"], None)
    old, pid = bench.arcade_pid, c.info["pid"]
    if not window:
        bench.arcade_pid = pid
        if old and old != pid and any(s["pid"] == old for s in list_sessions()):
            with bench.lock:
                oc = bench.clients.pop(old, None)
            try:
                if oc is not None:
                    await run_in_threadpool(oc.close, True)
                else:
                    await run_in_threadpool(bench.call, old, "quit", {})
            except (LookupError, OSError, ProtocolError):
                pass
    return ok({"game": g["name"], "title": g["title"], "window": window, "session": c.info,
               "stopped": old if (not window and old and old != pid) else None,
               "url": f"/?session={pid}&play=1"})


ROUTES = [
    Route("/", index),
    Route("/arcade", arcade_page),
    Route("/api/arcade", arcade_list),
    Route("/api/arcade/picture/{name}.png", arcade_picture),
    Route("/api/arcade/build", arcade_build, methods=["POST"]),
    Route("/api/arcade/play", arcade_play, methods=["POST"]),
    Route("/api/health", health),
    Route("/api/commands", commands),
    Route("/api/projects", projects),
    Route("/api/projects/{name}/build", build, methods=["POST"]),
    Route("/api/projects/{name}/test", test, methods=["POST"]),
    Route("/api/sessions", sessions, methods=["GET", "POST"]),
    Route("/api/sessions/{pid:int}", session_one, methods=["DELETE"]),
    Route("/api/sessions/{pid:int}/select", select, methods=["POST"]),
    Route("/api/sessions/{pid:int}/screen.png", screen_png),
    Route("/api/sessions/{pid:int}/cmd/{command}", run_cmd, methods=["GET", "POST"]),
    Route("/api/screen.png", screen_png),
    Route("/api/cmd/{command}", run_cmd, methods=["GET", "POST"]),
]


def make_app():
    """Our REST routes on the MCP server's Starlette app (keeps its lifespan for /mcp)."""
    from .mcp_server import server as mcp
    app = mcp.streamable_http_app(streamable_http_path="/mcp")
    app.router.routes[0:0] = ROUTES
    return app


def main(port: int = 3690, host: str = "127.0.0.1", open_browser: bool = False):
    import uvicorn
    url = f"http://{host}:{port}/"
    print(f"Lynx DevBench on {url}  (REST /api, MCP /mcp)", flush=True)
    if open_browser:
        threading.Timer(1.0, lambda: os.system(f"open {url} >/dev/null 2>&1 || xdg-open {url} >/dev/null 2>&1")).start()
    uvicorn.run(make_app(), host=host, port=port, log_level="warning")
