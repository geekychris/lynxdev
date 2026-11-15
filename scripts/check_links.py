#!/usr/bin/env python3
"""Check the links in the docs: README.md, CLAUDE.md, docs/*.md, sdk/README.md,
emulator/README.md. Relative links must point at files in the repo (a
#anchor is checked against the headings); http(s) links must answer < 400.

    python3 scripts/check_links.py            # everything
    python3 scripts/check_links.py --offline  # only the links inside the repo

Hosts that block scripted requests (they answer 403 to anything but a
browser) are listed in KNOWN_403 and reported, not failed.
"""
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = [ROOT / "README.md", ROOT / "CLAUDE.md", ROOT / "sdk" / "README.md", ROOT / "emulator" / "README.md",
         *sorted((ROOT / "docs").glob("*.md"))]
KNOWN_403 = ("forums.atariage.com",)
LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)|<img [^>]*src=\"([^\"]+)\"|<a [^>]*href=\"([^\"]+)\"")


def anchors(md: Path) -> set:
    out = set()
    for ln in md.read_text().splitlines():
        if ln.startswith("#"):
            t = ln.lstrip("#").strip().lower()
            out.add(re.sub(r"[^\w\- ]", "", t).replace(" ", "-"))
    return out


def head(url: str) -> int:
    """HTTP status (curl if present: it has the system's CA certificates)."""
    m = re.match(r"https://github\.com/geekychris/([\w.-]+)", url)
    if m and shutil.which("gh"):
        # our own repos are private for now: ask the API as the logged-in user
        r = subprocess.run(["gh", "api", f"repos/geekychris/{m.group(1)}", "-q", ".name"],
                           capture_output=True, text=True)
        if r.returncode == 0:
            return 200
    if shutil.which("curl"):
        r = subprocess.run(["curl", "-s", "-o", "/dev/null", "-L", "-m", "25", "-A", "Mozilla/5.0 (lynxdev link check)",
                            "-w", "%{http_code}", url], capture_output=True, text=True)
        return int(r.stdout or 0)
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (lynxdev link check)"})
    try:
        with urllib.request.urlopen(req, timeout=25) as r:
            return r.status
    except urllib.error.HTTPError as ex:
        return ex.code
    except Exception:
        return 0


def main() -> int:
    offline = "--offline" in sys.argv
    bad, seen, n = [], {}, 0
    for f in FILES:
        if not f.exists():
            continue
        for m in LINK.finditer(f.read_text()):
            url = next(g for g in m.groups() if g)
            n += 1
            if url.startswith(("http://", "https://")):
                if offline:
                    continue
                if url not in seen:
                    seen[url] = head(url)
                code = seen[url]
                if code >= 400 or code == 0:
                    if code == 403 and any(h in url for h in KNOWN_403):
                        print(f"  note {f.relative_to(ROOT)}: {url} answers 403 to scripts (known; check in a browser)")
                    else:
                        bad.append(f"{f.relative_to(ROOT)}: {url} -> {code or 'no answer'}")
            elif url.startswith("mailto:"):
                continue
            else:
                path, _, frag = url.partition("#")
                target = (f.parent / path).resolve() if path else f
                if not target.exists():
                    bad.append(f"{f.relative_to(ROOT)}: {url} -> no such file")
                elif frag and target.suffix == ".md" and frag.lower() not in anchors(target):
                    bad.append(f"{f.relative_to(ROOT)}: {url} -> no heading #{frag}")
    web = len(seen)
    print(f"{n} links in {len(FILES)} files" + ("" if offline else f", {web} distinct web links checked"))
    for b in bad:
        print(f"  FAIL {b}")
    print("PASS: links" if not bad else f"FAIL: {len(bad)} broken link(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
