"""65SC02 opcode table (the Lynx CPU: CMOS 65C02 without the Rockwell bit
instructions or WAI/STP) and a disassembler. Undefined opcodes are NOPs."""

_ROWS = """
BRK imp|ORA izx|JAM imp|SLO izx|NOP zp|ORA zp|ASL zp|SLO zp|PHP imp|ORA imm|ASL acc|ANC imm|NOP abs|ORA abs|ASL abs|SLO abs
BPL rel|ORA izy|JAM imp|SLO izy|NOP zpx|ORA zpx|ASL zpx|SLO zpx|CLC imp|ORA aby|NOP imp|SLO aby|NOP abx|ORA abx|ASL abx|SLO abx
JSR abs|AND izx|JAM imp|RLA izx|BIT zp|AND zp|ROL zp|RLA zp|PLP imp|AND imm|ROL acc|ANC imm|BIT abs|AND abs|ROL abs|RLA abs
BMI rel|AND izy|JAM imp|RLA izy|NOP zpx|AND zpx|ROL zpx|RLA zpx|SEC imp|AND aby|NOP imp|RLA aby|NOP abx|AND abx|ROL abx|RLA abx
RTI imp|EOR izx|JAM imp|SRE izx|NOP zp|EOR zp|LSR zp|SRE zp|PHA imp|EOR imm|LSR acc|ALR imm|JMP abs|EOR abs|LSR abs|SRE abs
BVC rel|EOR izy|JAM imp|SRE izy|NOP zpx|EOR zpx|LSR zpx|SRE zpx|CLI imp|EOR aby|NOP imp|SRE aby|NOP abx|EOR abx|LSR abx|SRE abx
RTS imp|ADC izx|JAM imp|RRA izx|NOP zp|ADC zp|ROR zp|RRA zp|PLA imp|ADC imm|ROR acc|ARR imm|JMP ind|ADC abs|ROR abs|RRA abs
BVS rel|ADC izy|JAM imp|RRA izy|NOP zpx|ADC zpx|ROR zpx|RRA zpx|SEI imp|ADC aby|NOP imp|RRA aby|NOP abx|ADC abx|ROR abx|RRA abx
NOP imm|STA izx|NOP imm|SAX izx|STY zp|STA zp|STX zp|SAX zp|DEY imp|NOP imm|TXA imp|XAA imm|STY abs|STA abs|STX abs|SAX abs
BCC rel|STA izy|JAM imp|AHX izy|STY zpx|STA zpx|STX zpy|SAX zpy|TYA imp|STA aby|TXS imp|TAS aby|SHY abx|STA abx|SHX aby|AHX aby
LDY imm|LDA izx|LDX imm|LAX izx|LDY zp|LDA zp|LDX zp|LAX zp|TAY imp|LDA imm|TAX imp|LAX imm|LDY abs|LDA abs|LDX abs|LAX abs
BCS rel|LDA izy|JAM imp|LAX izy|LDY zpx|LDA zpx|LDX zpy|LAX zpy|CLV imp|LDA aby|TSX imp|LAS aby|LDY abx|LDA abx|LDX aby|LAX aby
CPY imm|CMP izx|NOP imm|DCP izx|CPY zp|CMP zp|DEC zp|DCP zp|INY imp|CMP imm|DEX imp|AXS imm|CPY abs|CMP abs|DEC abs|DCP abs
BNE rel|CMP izy|JAM imp|DCP izy|NOP zpx|CMP zpx|DEC zpx|DCP zpx|CLD imp|CMP aby|NOP imp|DCP aby|NOP abx|CMP abx|DEC abx|DCP abx
CPX imm|SBC izx|NOP imm|ISC izx|CPX zp|SBC zp|INC zp|ISC zp|INX imp|SBC imm|NOP imp|SBC imm|CPX abs|SBC abs|INC abs|ISC abs
BEQ rel|SBC izy|JAM imp|ISC izy|NOP zpx|SBC zpx|INC zpx|ISC zpx|SED imp|SBC aby|NOP imp|ISC aby|NOP abx|SBC abx|INC abx|ISC abx
"""

OPCODES = []          # index = opcode: (mnemonic, mode)
for _row in _ROWS.strip().splitlines():
    for _cell in _row.split("|"):
        _m, _mode = _cell.split()
        OPCODES.append((_m, _mode))
assert len(OPCODES) == 256

# 65SC02: undocumented NMOS opcodes become NOPs of the CMOS sizes...
_NMOS_DOC = {"ADC", "AND", "ASL", "BCC", "BCS", "BEQ", "BIT", "BMI", "BNE", "BPL", "BRK",
             "BVC", "BVS", "CLC", "CLD", "CLI", "CLV", "CMP", "CPX", "CPY", "DEC", "DEX",
             "DEY", "EOR", "INC", "INX", "INY", "JMP", "JSR", "LDA", "LDX", "LDY", "LSR",
             "NOP", "ORA", "PHA", "PHP", "PLA", "PLP", "ROL", "ROR", "RTI", "RTS", "SBC",
             "SEC", "SED", "SEI", "STA", "STX", "STY", "TAX", "TAY", "TSX", "TXA", "TXS", "TYA"}
for _op in range(256):
    _m, _mode = OPCODES[_op]
    if _m not in _NMOS_DOC or (_m == "NOP" and _op != 0xEA) or (_op == 0xEB):
        lo = _op & 0x0F
        if lo == 0x02:
            OPCODES[_op] = ("NOP", "imm")
        elif lo in (0x03, 0x07, 0x0B, 0x0F):
            OPCODES[_op] = ("NOP", "imp")
        elif _op == 0x44:
            OPCODES[_op] = ("NOP", "zp")
        elif _op in (0x54, 0xD4, 0xF4):
            OPCODES[_op] = ("NOP", "zpx")
        elif _op in (0x5C, 0xDC, 0xFC):
            OPCODES[_op] = ("NOP", "abs")
        else:
            OPCODES[_op] = ("NOP", "imp")
# ...and the CMOS additions
for _op, _m, _mode in [
        (0x04, "TSB", "zp"), (0x0C, "TSB", "abs"), (0x14, "TRB", "zp"), (0x1C, "TRB", "abs"),
        (0x12, "ORA", "izp"), (0x32, "AND", "izp"), (0x52, "EOR", "izp"), (0x72, "ADC", "izp"),
        (0x92, "STA", "izp"), (0xB2, "LDA", "izp"), (0xD2, "CMP", "izp"), (0xF2, "SBC", "izp"),
        (0x1A, "INC", "acc"), (0x3A, "DEC", "acc"), (0x34, "BIT", "zpx"), (0x3C, "BIT", "abx"),
        (0x89, "BIT", "imm"), (0x5A, "PHY", "imp"), (0x7A, "PLY", "imp"), (0xDA, "PHX", "imp"),
        (0xFA, "PLX", "imp"), (0x64, "STZ", "zp"), (0x74, "STZ", "zpx"), (0x9C, "STZ", "abs"),
        (0x9E, "STZ", "abx"), (0x7C, "JMP", "iax"), (0x80, "BRA", "rel")]:
    OPCODES[_op] = (_m, _mode)

SIZE = {"imp": 1, "acc": 1, "imm": 2, "zp": 2, "zpx": 2, "zpy": 2, "izx": 2, "izy": 2,
        "rel": 2, "abs": 3, "abx": 3, "aby": 3, "ind": 3, "izp": 2, "iax": 3}

DOCUMENTED = {"ADC", "AND", "ASL", "BCC", "BCS", "BEQ", "BIT", "BMI", "BNE", "BPL", "BRK",
              "BVC", "BVS", "CLC", "CLD", "CLI", "CLV", "CMP", "CPX", "CPY", "DEC", "DEX",
              "DEY", "EOR", "INC", "INX", "INY", "JMP", "JSR", "LDA", "LDX", "LDY", "LSR",
              "NOP", "ORA", "PHA", "PHP", "PLA", "PLP", "ROL", "ROR", "RTI", "RTS", "SBC",
              "SEC", "SED", "SEI", "STA", "STX", "STY", "TAX", "TAY", "TSX", "TXA", "TXS", "TYA",
              "TSB", "TRB", "PHX", "PHY", "PLX", "PLY", "STZ", "BRA"}

READS = {"ORA", "AND", "EOR", "ADC", "SBC", "CMP", "CPX", "CPY", "BIT", "LDA", "LDX", "LDY",
         "LAX", "LAS", "NOP"}
WRITES = {"STA", "STX", "STY", "STZ"}
RMW = {"ASL", "LSR", "ROL", "ROR", "INC", "DEC", "TSB", "TRB"}


def access(op):
    """'R', 'W', 'M' (read-modify-write) or '-' for a data access through the operand."""
    m, mode = OPCODES[op]
    if mode in ("imp", "acc", "imm", "rel") or m in ("JMP", "JSR") or (m == "NOP"):
        return "-"
    if m in WRITES:
        return "W"
    if m in RMW:
        return "M"
    if m in READS:
        return "R"
    return "-"


def disasm_one(read, addr, sym=None):
    """Disassemble one instruction. read(addr) -> byte; sym(addr) -> name or None.
    Returns (size, bytes list, text)."""
    op = read(addr)
    m, mode = OPCODES[op]
    n = SIZE[mode]
    b = [read((addr + i) & 0xFFFF) for i in range(n)]
    lo = b[1] if n > 1 else 0
    word = lo | (b[2] << 8) if n > 2 else lo

    def name(v, digits):
        s = sym(v) if sym else None
        return s if s else ("$%0*X" % (digits, v))

    if mode in ("imp",):
        arg = ""
    elif mode == "acc":
        arg = "A"
    elif mode == "imm":
        arg = "#$%02X" % lo
    elif mode == "zp":
        arg = name(lo, 2)
    elif mode == "zpx":
        arg = name(lo, 2) + ",X"
    elif mode == "zpy":
        arg = name(lo, 2) + ",Y"
    elif mode == "izx":
        arg = "(%s,X)" % name(lo, 2)
    elif mode == "izp":
        arg = "(%s)" % name(lo, 2)
    elif mode == "iax":
        arg = "(%s,X)" % name(word, 4)
    elif mode == "izy":
        arg = "(%s),Y" % name(lo, 2)
    elif mode == "rel":
        tgt = (addr + 2 + (lo - 256 if lo >= 128 else lo)) & 0xFFFF
        arg = name(tgt, 4)
    elif mode == "abs":
        arg = name(word, 4)
    elif mode == "abx":
        arg = name(word, 4) + ",X"
    elif mode == "aby":
        arg = name(word, 4) + ",Y"
    else:  # ind
        arg = "(%s)" % name(word, 4)
    text = m if m in DOCUMENTED else m.lower()
    return n, b, (text + " " + arg).rstrip()


def c_tables():
    """C source for a watchpoint decoder (mode and access per opcode), as a8dev's a8h.c has."""
    code = {"imp": "i", "acc": "i", "imm": "#", "zp": "z", "zpx": "x", "zpy": "y", "abs": "a",
            "abx": "X", "aby": "Y", "ind": "n", "izx": "p", "izy": "q", "rel": "r", "izp": "o", "iax": "m"}
    modes = "".join(code[OPCODES[i][1]] for i in range(256))
    acc = "".join(access(i) for i in range(256))
    out = []
    for nm, s in (("lynxh_opmode", modes), ("lynxh_opaccess", acc)):
        out.append("static const char %s[257] =" % nm)
        for i in range(0, 256, 64):
            out.append('\t"%s"' % s[i:i + 64])
        out[-1] += ";"
    return "\n".join(out)


if __name__ == "__main__":
    print(c_tables())
