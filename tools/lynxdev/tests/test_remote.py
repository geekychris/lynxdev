"""End-to-end tests for sessions, the control port, the debugger, the MCP
server (stdio) and DevBench (REST + MCP over HTTP). Run by ./lynx selftest.

Needs projects/hello built (selftest builds every project first).
"""
import asyncio
import json
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "lynxdev" / "src"))

from lynxdev.control import ProtocolError, SessionClient, list_sessions  # noqa: E402

HELLO = ROOT / "projects" / "hello" / "build" / "hello.lnx"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def section(t):
    print(f"\n== {t}")


def test_session():
    section("session process + control port")
    c = SessionClient.spawn(str(HELLO))
    try:
        pid = c.info["pid"]
        check("session registered", any(s["pid"] == pid for s in list_sessions()))
        st = c.call("status")
        check("status: paused, symbols", st["paused"] and st["symbols"] > 50, str(st))
        r = c.call("run_until", text="hello: ready", max_frames=600)
        check("run_until", r["found"], str(r))
        f0 = c.call("status")["frame"]
        c.call("run", frames=7)
        check("run advances exactly N frames", c.call("status")["frame"] == f0 + 7)
        c.call("log")
        c.call("press", keys="RIGHT", hold_frames=3)
        r = c.call("run_until", text="hello: move x=", max_frames=20)
        check("press (d-pad) + log", r["found"], str(r))
        c.call("hold", keys="A")
        r = c.call("run_until", text="hello: button a", max_frames=20)
        c.call("release")
        check("hold / release", r["found"])
        c.call("press", keys="B")
        check("button B", c.call("run_until", text="hello: button b", max_frames=20)["found"])
        r = c.call("pad", dirs="LEFT")
        c.call("run", frames=3)
        c.call("pad", dirs="")
        check("pad holds a direction", "hello: move" in c.call("log")["text"], str(r))
        r = c.call("screenshot", scale=1)
        check("screenshot 160x102 with content", r["width"] == 160 and r["height"] == 102 and r["colors"] >= 3,
              str({k: v for k, v in r.items() if k != "png_b64"}))
        r = c.call("palette")
        check("palette has 16 colours", len(r["rgb"]) == 16, str(r)[:200])
        h = c.call("hw")
        check("hw has suzy and mikey", "suzy" in h and "mikey" in h, str(list(h))[:200])

        section("debugger")
        r = c.call("symbols", pattern="^_main$")
        check("symbols", r and r[0]["name"] == "_main", str(r))
        r = c.call("disasm", addr="main", count=2)
        check("disasm by symbol", "_main" in r["listing"].splitlines()[0], r["listing"])
        r = c.call("read_mem", addr="frame_count", length=2)
        check("read_mem by symbol", len(r["hex"]) == 4, str(r))
        c.call("write_mem", addr="rnd_state", hex="3412")
        check("write_mem", c.call("read_mem", addr="rnd_state", length=2)["hex"] == "3412")
        c.call("watch", addr="frame_count", length=1, kind="w")
        r = c.call("continue", max_frames=5)
        check("watchpoint stops on the write (VBL interrupt)", r["stopped"], str(r)[:300])
        c.call("delete")
        r = c.call("continue", max_frames=5)
        check("delete all -> runs freely", not r["stopped"] and r["frames"] == 5, str(r)[:200])
        c.call("break", addr="gfx_present")
        r = c.call("continue", max_frames=3)
        check("breakpoint on a function", r["stopped"] and "gfx_present" in (r.get("symbol") or ""), str(r)[:300])
        c.call("step_over")
        r = c.call("stepi", count=3)
        check("stepi", "PC" in r["regs"] and r.get("next"), str(r)[:200])
        check("backtrace", isinstance(c.call("backtrace", items=4), (list, dict)))
        c.call("delete")
        regs = c.call("set_reg", reg="X", value=0x12)
        check("set_reg", regs["X"] == 0x12, str(regs))
        mm = c.call("memory_map")
        check("memory_map: Suzy and Mikey mapped", mm.get("suzy") and mm.get("mikey"), str(mm))

        section("states")
        p = c.call("save_state", path="selftest")["path"]
        v = c.call("read_mem", addr="frame_count", length=2)["hex"]
        c.call("run", frames=20)
        c.call("load_state", path="selftest")
        check("save_state / load_state round trip", c.call("read_mem", addr="frame_count", length=2)["hex"] == v, p)
        c.call("reload")
        r = c.call("run_until_signal", max_frames=600)
        check("run_until_signal: lynx_done -> 1", str(r.get("signal")) == "1", str(r)[:300])
        try:
            c.call("nope")
            check("unknown command is an error", False)
        except ProtocolError:
            check("unknown command is an error", True)
    finally:
        c.close(stop=True)
    time.sleep(0.5)
    check("session unregistered on quit", not any(s["pid"] == pid for s in list_sessions()))


def test_cli_ctl():
    section("./lynx ctl against a served session")
    c = SessionClient.spawn(str(HELLO))
    try:
        port = str(c.info["control_port"])
        cli = [str(ROOT / ".venv" / "bin" / "lynxdev"), "ctl", "--port", port]
        r = subprocess.run(cli + ["run_until", "text=hello: ready"], capture_output=True, text=True)
        check("ctl run_until", r.returncode == 0 and '"found": true' in r.stdout, r.stdout + r.stderr)
        r = subprocess.run(cli + ["disasm", "addr=main", "count=2"], capture_output=True, text=True)
        check("ctl prints listings plainly", "_main" in r.stdout.splitlines()[0] if r.stdout else False, r.stdout)
    finally:
        c.close(stop=True)


def test_mcp():
    section("MCP server (stdio)")
    from mcp.client.session import ClientSession
    from mcp.client.stdio import StdioServerParameters, stdio_client

    async def run():
        p = StdioServerParameters(command=str(ROOT / ".venv" / "bin" / "lynxdev-mcp"), args=[])
        async with stdio_client(p) as (r, w):
            async with ClientSession(r, w) as s:
                await s.initialize()
                names = {t.name for t in (await s.list_tools()).tools}
                check("MCP exposes build/run/debug tools",
                      {"build", "emu_boot", "emu_run_until", "emu_look", "emu_press", "emu_read_mem",
                       "emu_break", "emu_watch", "emu_continue", "emu_regs", "emu_cmd", "emu_pad",
                       "emu_hw", "emu_palette", "emu_scb_chain", "emu_record_input", "emu_replay",
                       "emu_screen_hash", "emu_record_video"} <= names, str(sorted(names)))

                async def call(n, **a):
                    return await s.call_tool(n, a)

                def text(res):
                    return res.content[0].text

                res = await call("build", project="hello")
                check("MCP build", "build hello: OK" in text(res), text(res)[-300:])
                await call("emu_boot", project="hello")
                res = await call("emu_run_until", text="hello: ready", max_frames=600)
                check("MCP emu_run_until", '"found": true' in text(res), text(res)[:200])
                res = await call("emu_look", scale=1)
                check("MCP emu_look returns an image", [c.type for c in res.content] == ["image"])
                await call("emu_press", keys="A")
                res = await call("emu_run_until", text="hello: button a", max_frames=20)
                check("MCP emu_press", '"found": true' in text(res))
                await call("emu_break", addr="input_update")
                res = await call("emu_press", keys="B")
                check("MCP breakpoint hit during press", '"why": "breakpoint"' in text(res)
                      and "input_update" in text(res), text(res)[:300])
                await call("emu_delete")
                res = await call("emu_read_mem", addr="frame_count", length=2)
                check("MCP emu_read_mem", len(text(res).split()) >= 3, text(res))
                res = await call("emu_screen_hash")
                check("MCP emu_screen_hash", len(text(res).strip()) >= 8)
                res = await call("emu_hw")
                check("MCP emu_hw", '"suzy"' in text(res) and '"mikey"' in text(res), text(res)[:200])
                res = await call("emu_palette")
                check("MCP emu_palette", '"rgb"' in text(res), text(res)[:200])
                await call("emu_record_input", action="start")
                await call("emu_press", keys="LEFT")
                await call("emu_run", frames=20)
                rp = str(ROOT / "build" / "mcp-rec.json")
                res = await call("emu_record_input", action="save", path=rp)
                check("MCP emu_record_input start/save", '"hashes"' in text(res), text(res)[:300])
                res = await call("emu_replay", path=rp)
                check("MCP emu_replay (per-frame hashes)", '"ok": true' in text(res)
                      and '"mismatches": 0' in text(res), text(res)[:300])
                res = await call("emu_record_video", path=str(ROOT / "build" / "mcp.gif"), frames=10)
                check("MCP emu_record_video", '"frames": 10' in text(res), text(res)[:200])
                await call("emu_load", path="hello")
                res = await call("emu_run_until_signal", max_frames=600)
                check("MCP emu_run_until_signal (lynx_done -> 1)", '"signal": "1"' in text(res)
                      or '"signal": 1' in text(res), text(res)[:400])
                res = await call("emu_run_to_addr", addr="gfx_present", max_frames=5)
                check("MCP emu_run_to_addr", "gfx_present" in text(res), text(res)[:300])
                res = await call("emu_position")
                check("MCP emu_position", '"since_load"' in text(res))
                await call("emu_close")

    asyncio.run(asyncio.wait_for(run(), 300))


def test_devbench():
    section("DevBench REST + MCP over HTTP")
    sk = socket.socket()
    sk.bind(("127.0.0.1", 0))
    port = sk.getsockname()[1]
    sk.close()
    base = f"http://127.0.0.1:{port}"
    srv = subprocess.Popen([str(ROOT / ".venv" / "bin" / "lynxdev"), "devbench", "--port", str(port)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def req(method, path, body=None, raw=False):
        r = urllib.request.Request(base + path, method=method,
                                   data=json.dumps(body).encode() if body is not None else None,
                                   headers={"content-type": "application/json"} if body is not None else {})
        try:
            with urllib.request.urlopen(r, timeout=120) as resp:
                data = resp.read()
                return resp.status, (data if raw else json.loads(data))
        except urllib.error.HTTPError as ex:
            return ex.code, json.loads(ex.read() or b"{}")

    pid = None
    try:
        for _ in range(100):
            try:
                if req("GET", "/api/health")[1]["ok"]:
                    break
            except OSError:
                time.sleep(0.2)
        check("devbench serves /api/health", req("GET", "/api/health")[0] == 200)
        st, ui = req("GET", "/", raw=True)
        check("devbench serves the web UI", st == 200 and b"Lynx DevBench" in ui)
        st, ps = req("GET", "/api/projects")
        check("projects listed", st == 200 and "hello" in {p["name"] for p in ps})
        st, r = req("POST", "/api/sessions", {"project": "hello"})
        pid = r.get("pid")
        check("POST /api/sessions starts a session", st == 200 and pid, str(r))
        st, r = req("POST", f"/api/sessions/{pid}/cmd/run_until", {"text": "hello: ready"})
        check("REST run_until", st == 200 and r["found"], str(r))
        st, png = req("GET", f"/api/sessions/{pid}/screen.png", raw=True)
        check("screen.png is a PNG", st == 200 and png[:4] == b"\x89PNG")
        st, r = req("GET", "/api/cmd/regs")
        check("GET on the selected session", st == 200 and "PC" in r)
        st, r = req("GET", "/api/cmd/scb_chain")
        check("REST scb_chain", st == 200 and "scbs" in r, str(r)[:200])
        check("unknown command -> 400", req("GET", "/api/cmd/nope")[0] == 400)
        check("bad session -> 404", req("GET", "/api/sessions/1/cmd/status")[0] == 404)
        init = {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t", "version": "1"}}}
        r = urllib.request.Request(base + "/mcp", data=json.dumps(init).encode(), method="POST", headers={
            "content-type": "application/json", "accept": "application/json, text/event-stream"})
        with urllib.request.urlopen(r, timeout=20) as resp:
            check("MCP over HTTP at /mcp", b'"serverInfo"' in resp.read())
    finally:
        if pid:
            req("DELETE", f"/api/sessions/{pid}")
        srv.terminate()
        srv.wait(10)


def main():
    if not HELLO.exists():
        print(f"build first: {HELLO} missing (./lynx build hello)")
        return 2
    test_session()
    test_cli_ctl()
    test_mcp()
    test_devbench()
    print(f"\nFAIL: {len(failures)} failure(s)" if failures else "\nPASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
