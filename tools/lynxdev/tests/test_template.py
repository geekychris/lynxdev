"""./lynx new from the template builds and passes its own test.py."""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
name = "zz_template_check"
dest = ROOT / "projects" / name
shutil.rmtree(dest, ignore_errors=True)
ok = True
try:
    r = subprocess.run([str(ROOT / "lynx"), "new", name], capture_output=True, text=True)
    print(r.stdout + r.stderr)
    r = subprocess.run([str(ROOT / "lynx"), "test", name], capture_output=True, text=True, timeout=600)
    print(r.stdout + r.stderr)
    ok = r.returncode == 0
    print(f"  {'PASS' if ok else 'FAIL'} template builds and passes its test")
finally:
    shutil.rmtree(dest, ignore_errors=True)
sys.exit(0 if ok else 1)
