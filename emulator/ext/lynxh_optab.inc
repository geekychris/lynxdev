static const char *const op_name[256] = {
	"brk", "ora", "???", "???", "tsb", "ora", "asl", "???", "php", "ora", "asl", "???", "tsb", "ora", "asl", "???",
	"bpl", "ora", "ora", "???", "trb", "ora", "asl", "???", "clc", "ora", "inc", "???", "trb", "ora", "asl", "???",
	"jsr", "and", "???", "???", "bit", "and", "rol", "???", "plp", "and", "rol", "???", "bit", "and", "rol", "???",
	"bmi", "and", "and", "???", "bit", "and", "rol", "???", "sec", "and", "dec", "???", "bit", "and", "rol", "???",
	"rti", "eor", "???", "???", "???", "eor", "lsr", "???", "pha", "eor", "lsr", "???", "jmp", "eor", "lsr", "???",
	"bvc", "eor", "eor", "???", "???", "eor", "lsr", "???", "cli", "eor", "phy", "???", "???", "eor", "lsr", "???",
	"rts", "adc", "???", "???", "stz", "adc", "ror", "???", "pla", "adc", "ror", "???", "jmp", "adc", "ror", "???",
	"bvs", "adc", "adc", "???", "stz", "adc", "ror", "???", "sei", "adc", "ply", "???", "jmp", "adc", "ror", "???",
	"bra", "sta", "???", "???", "sty", "sta", "stx", "???", "dey", "bit", "txa", "???", "sty", "sta", "stx", "???",
	"bcc", "sta", "sta", "???", "sty", "sta", "stx", "???", "tya", "sta", "txs", "???", "stz", "sta", "stz", "???",
	"ldy", "lda", "ldx", "???", "ldy", "lda", "ldx", "???", "tay", "lda", "tax", "???", "ldy", "lda", "ldx", "???",
	"bcs", "lda", "lda", "???", "ldy", "lda", "ldx", "???", "clv", "lda", "tsx", "???", "ldy", "lda", "ldx", "???",
	"cpy", "cmp", "???", "???", "cpy", "cmp", "dec", "???", "iny", "cmp", "dex", "wai", "cpy", "cmp", "dec", "???",
	"bne", "cmp", "cmp", "???", "???", "cmp", "dec", "???", "cld", "cmp", "phx", "stp", "???", "cmp", "dec", "???",
	"cpx", "sbc", "???", "???", "cpx", "sbc", "inc", "???", "inx", "sbc", "nop", "???", "cpx", "sbc", "inc", "???",
	"beq", "sbc", "sbc", "???", "???", "sbc", "inc", "???", "sed", "sbc", "plx", "???", "???", "sbc", "inc", "???",
};
/* i impl, # imm, a abs, z zp, x zp,X, y zp,Y, X abs,X, Y abs,Y, j (abs,X), r rel, p (zp,X), q (zp),Y, n (abs), d (zp) */
static const char op_mode[257] =
	"ipiizzzii#iiaaairqdizxxiiYiiaXXiapiizzzii#iiaaairqdixxxiiYiiXXXi"
	"ipiiizzii#iiaaairqdiixxiiYiiiXXiipiizzzii#iinaairqdixxxiiYiijXXi"
	"rpiizzzii#iiaaairqdixxyiiYiiaXXi#p#izzzii#iiaaairqdixxyiiYiiXXYi"
	"#piizzzii#iiaaairqdiixxiiYiiiXXi#piizzzii#iiaaairqdiixxiiYiiiXXi"
;
/* data access through the operand: R read, W write, M read-modify-write */
static const char op_access[257] =
	"-R--MRM-----MRM--RR-MRM--R--MRM--R--RRM-----RRM--RR-RRM--R--RRM-"
	"-R---RM------RM--RR--RM--R---RM--R--WRM------RM--RR-WRM--R---RM-"
	"-W--WWW-----WWW--WW-WWW--W--WWW--R--RRR-----RRR--RR-RRR--R--RRR-"
	"-R--RRM-----RRM--RR--RM--R---RM--R--RRM-----RRM--RR--RM--R---RM-"
;
