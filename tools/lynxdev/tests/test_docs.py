"""The docs' links inside the repo resolve (files and #headings). The web
links are checked by `python3 scripts/check_links.py` (needs the network,
so not in selftest)."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
r = subprocess.run([sys.executable, str(ROOT / "scripts" / "check_links.py"), "--offline"],
                   capture_output=True, text=True)
print(r.stdout + r.stderr)
print(f"  {'PASS' if r.returncode == 0 else 'FAIL'} every relative link in the docs resolves")
sys.exit(r.returncode)
