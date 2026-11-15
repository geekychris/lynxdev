"""Symbols from ld65 output: the VICE label file (-Ln, build/<p>.lbl) and the
map (-m, build/<p>.map).

    al 002090 ._main        (.lbl: every label, C symbols with a leading _)
    _main  002090 RLA       (.map "Exports list": public symbols, 2 per line)

C symbols carry a leading underscore; lookups accept either spelling.
Addresses are 16-bit CPU addresses.
"""
from __future__ import annotations

import re
from pathlib import Path

_LBL = re.compile(r"^al\s+([0-9A-Fa-f]+)\s+\.(\S+)\s*$")
_MAP_EXPORT = re.compile(r"(\S+)\s+([0-9A-Fa-f]{6})\s+([RE][LA][AZI])")
_SEGMENT = re.compile(r"^(\w+)\s+([0-9A-Fa-f]{6})\s+([0-9A-Fa-f]{6})\s+([0-9A-Fa-f]{6})\s+[0-9A-Fa-f]{5}\s*$")


def _cpu(a: int, expr) -> int:
    if a > 0xFFFF or a < -0x8000:
        raise ValueError(f"{expr!r} is {a:#x}, outside the 64K CPU view")
    return a & 0xFFFF


class Symbols:
    def __init__(self):
        self.addr: dict[str, int] = {}
        self.info: dict[str, dict] = {}
        self.segments: list[dict] = []

    @classmethod
    def load(cls, path) -> "Symbols":
        """path: the program (.lnx), its .lbl or its .map; the siblings are read too."""
        s = cls()
        if path:
            p = Path(path)
            for ext in (".lbl", ".map"):
                s.add_file(p.with_suffix(ext))
        return s

    def add_file(self, path):
        p = Path(path)
        if not p.exists():
            return
        text = p.read_text(errors="replace")
        if p.suffix == ".lbl":
            for line in text.splitlines():
                m = _LBL.match(line)
                if m:
                    name, val = m.group(2), int(m.group(1), 16)
                    if name.startswith("__") and name.endswith("__"):
                        continue        # linker sizes/starts, not places
                    self.addr.setdefault(name, val)
                    self.info.setdefault(name, {"scope": "local" if "@" in name else "label"})
            return
        in_exports = in_segments = False
        for line in text.splitlines():
            if line.startswith("Exports list by name"):
                in_exports = True
                continue
            if line.startswith("Exports list by value") or line.startswith("Imports list"):
                in_exports = False
            if line.startswith("Segment list"):
                in_segments = True
                continue
            if in_segments:
                m = _SEGMENT.match(line)
                if m:
                    self.segments.append({"name": m.group(1), "start": int(m.group(2), 16),
                                          "end": int(m.group(3), 16), "size": int(m.group(4), 16)})
                elif line.strip() == "" and self.segments:
                    in_segments = False
            if in_exports:
                for name, val, kind in _MAP_EXPORT.findall(line):
                    if kind.endswith("A") and not (name.startswith("__") and name.endswith("__")):
                        self.addr[name] = int(val, 16)
                        self.info[name] = {"scope": "public"}

    def __len__(self):
        return len(self.addr)

    def lookup(self, name: str) -> int | None:
        for n in (name, "_" + name):
            if n in self.addr:
                return self.addr[n]
        return None

    def locate(self, expr, regs=None) -> tuple[int, int | None]:
        """(CPU address, None): kept for the session's read_mem (no banked symbols)."""
        return self.resolve(expr, regs), None

    def resolve(self, expr, regs=None) -> int:
        """Number, symbol, symbol+off, or a register name (pc, s) -> address."""
        if isinstance(expr, int):
            return _cpu(expr, expr)
        e = str(expr).strip()
        m = re.fullmatch(r"(.+?)\s*([+-])\s*((?:0x|\$)?[0-9A-Fa-f]+h?)", e)
        if m and not re.fullmatch(r"(0x|\$)?[0-9A-Fa-f]+h?", e):
            base = self.resolve(m.group(1), regs)
            off = _num(m.group(3))
            return _cpu(base + off if m.group(2) == "+" else base - off, expr)
        if regs is not None and e.upper() in regs:
            return regs[e.upper()]
        v = self.lookup(e)
        if v is not None:
            return _cpu(v, expr)
        try:
            n = _num(e)
        except ValueError:
            raise ValueError(f"unknown symbol or address: {expr!r}") from None
        return _cpu(n, expr)

    def segment_of(self, addr: int) -> str | None:
        for s in self.segments:
            if s["start"] <= addr <= s["end"] and s["size"]:
                return s["name"]
        return None

    def name_of(self, addr: int) -> str | None:
        """Nearest symbol at or below addr, as name or name+0xN. Public and
        named labels win over cc65's local labels (L0012, @loop)."""
        def named(n):
            return not (n[:1] == "L" and n[1:].isalnum() and any(c.isdigit() for c in n[1:])
                        or "@" in n or n.startswith("__"))
        for pick in (named, lambda n: True):
            best = None
            for n, a in self.addr.items():
                if a > addr or addr - a > 0x1000 or not pick(n):
                    continue
                if best is None or a > best[1] or (a == best[1] and n.startswith("_")):
                    best = (n, a)
            if best:
                n, a = best
                return n if addr == a else f"{n}+0x{addr - a:x}"
        return None

    def search(self, pattern: str = "", limit: int = 200) -> list[dict]:
        rx = re.compile(pattern or ".", re.I)
        out = [{"name": n, "addr": a, "hex": f"${a:04X}", **self.info.get(n, {})}
               for n, a in self.addr.items() if rx.search(n)]
        out.sort(key=lambda d: d["addr"])
        return out[:limit]


def _num(s: str) -> int:
    s = s.strip()
    if s.lower().startswith("0x"):
        return int(s, 16)
    if s.startswith("$") or s.startswith("#"):
        return int(s[1:], 16)
    if s.lower().endswith("h") and len(s) > 1:
        return int(s[:-1], 16)
    return int(s, 10)
