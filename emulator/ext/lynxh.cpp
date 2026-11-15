/* lynxh.cpp - lynxdev harness for Handy (libretro-handy core): a control
 * socket plus deterministic frame stepping, breakpoints, watchpoints, a guest
 * log channel, input injection and recording, screen hashes, states.
 *
 * Handy is driven by this file's main loop (no libretro frontend). The
 * protocol mirrors a8dev's a8h (see emulator/README.md and COORDINATION.md).
 * Everything runs on one thread: commands are served between frames, or
 * while the CPU is stopped at an instruction boundary inside a frame (the
 * CPU hook makes C65C02::Update() return without executing the instruction,
 * and this loop serves commands until a run resumes it).
 *
 * zlib licence, like Handy.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <string>
#include <vector>
#include <map>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
typedef SOCKET lh_sock;
#define LH_BADSOCK INVALID_SOCKET
#define lh_close closesocket
#define getpid _getpid
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
typedef int lh_sock;
#define LH_BADSOCK (-1)
#define lh_close close
#endif

/* the harness reads Mikey/Suzy internals for `hw` and side-effect-free peeks */
#include "machine.h"
#define private public
#define protected public
#include "system.h"
#undef private
#undef protected
#include "lynxdef.h"
#include "lynxh.h"

#define LYNXH_VERSION "1"

int gLynxhCpuCheck = 0;

/* Handy logs through libretro's handy_log; ours goes to stderr when
   LYNXH_VERBOSE is set. */
void handy_log(enum retro_log_level level, const char *format, ...)
{
	static int verbose = -1;
	va_list ap;
	if (verbose < 0)
		verbose = getenv("LYNXH_VERBOSE") != NULL;
	if (!verbose && level < RETRO_LOG_ERROR)
		return;
	if (!verbose)
		return;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
}

/* ------------------------------------------------------------------ */
/* options and global state */

static const struct lynxh_frontend *fe = NULL;
static int opt_port = 0;
static char opt_host[64] = "127.0.0.1";
static int opt_free = -1;           /* -1: default (window free, headless paused) */
static unsigned long opt_seed = 0;
static int opt_rotate = 0;          /* 0 none, 1 left, 2 right */
static int opt_zoom = 3;
static int opt_sound = 1;
static char opt_bios[1024] = "";
static volatile int quit_req = 0;

enum { MODE_PAUSED, MODE_FREE };
static int mode = MODE_PAUSED;
static int free_turbo = 0;

static CSystem *lx = NULL;
static std::vector<UBYTE> cart;     /* the loaded program, kept for reset/reload */
static char loaded_path[1024] = "";
static char loaded_type[16] = "";

static long long frame = 0;         /* frames since load/power-on */
static long long ticks = 0;         /* 16 MHz system ticks since load, at the last sync */
static ULONG ticks_seen = 0;        /* gSystemCycleCount at the last sync */
static long long frame_ticks = 0;   /* ticks at the start of the current frame */
static long long last_frame_len = 0;
static int in_cpu = 0;              /* stopped mid-frame (at an instruction boundary) */
static int resume_skip = 0;         /* first hook call after a mid-frame stop: run the instruction */
static int frame_pending = 0;       /* the display callback ended a frame */
static int stop_pending = 0;        /* the hook asked to stop */
static double last_fps = 0.0;

/* frames: 160x102, 0x00RRGGBB */
#define W 160
#define H 102
static ULONG draw_buf[W * H];      /* Handy renders lines into this */
static ULONG last_frame[W * H];    /* the last completed frame */
static int display_ready = 0;

/* audio: stereo int16 at HANDY_AUDIO_SAMPLE_FREQ */
static FILE *wav = NULL;
static long wav_frames = 0;
static char wav_path[1024] = "";

static double now_s(void)
{
#ifdef _WIN32
	return (double) GetTickCount64() / 1000.0;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
#endif
}

static void sleep_s(double s)
{
	if (s <= 0)
		return;
#ifdef _WIN32
	Sleep((DWORD) (s * 1000));
#else
	usleep((useconds_t) (s * 1e6));
#endif
}

/* Handy keeps a 32-bit 16 MHz counter and subtracts $80000000 from it (and
   every time stamp) once it passes $F0000000 (CMikie::Update). A step can't
   be anywhere near 2^30 ticks, so a huge delta is that rebase. */
static ULONG tick_delta(void)
{
	ULONG d = (ULONG) (gSystemCycleCount - ticks_seen);
	if (d >= 0x40000000u)
		d += 0x80000000u;
	return d;
}

static long long ticks_now(void)
{
	return ticks + tick_delta();
}

static void sync_ticks(void)
{
	ticks += tick_delta();
	ticks_seen = gSystemCycleCount;
}

/* reported cycles: 4 MHz CPU cycles (16 MHz system ticks / 4) */
static long long cycles_now(void) { return ticks_now() >> 2; }
static long long frame_cycles(void) { return frame_ticks >> 2; }

static double cur_hz(void)
{
	if (last_frame_len > 0)
		return (double) HANDY_SYSTEM_FREQ / (double) last_frame_len;
	return 75.0;
}

/* a run: frames executed for a client (client >= 0) or free-running (client < 0) */
static struct {
	int active;
	int client;
	long long max_frames;
	long long frames_done;
	int has_cond;
	long steps;                     /* -1: not stepping */
	int over;
	int over_pc;
	int over_s;
	int to;
	int to_pc;
	long long at;                   /* stop at the first boundary at/after this cycle (-1: off) */
	int nobreak;
	int until_signal;
	unsigned char until[256];
	int until_len;
	long long until_since;
	double t0;
} run;

static struct {
	int active;
	char reason[16];
	char detail[200];
	int ea;
} pend;

static char last_stop[700] = "why=none";
static long parent_pid = 0;

static void check_parent(void)
{
#ifndef _WIN32
	if (parent_pid && (long) getppid() != parent_pid)
		exit(0);
#endif
}

/* ------------------------------------------------------------------ */
/* small helpers */

struct sbuf {
	std::string s;
	void addf(const char *fmt, ...)
	{
		char tmp[1024];
		va_list ap;
		va_start(ap, fmt);
		int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
		va_end(ap);
		if (n < 0)
			return;
		if ((size_t) n < sizeof(tmp)) {
			s.append(tmp, n);
			return;
		}
		std::vector<char> big(n + 1);
		va_start(ap, fmt);
		vsnprintf(big.data(), n + 1, fmt, ap);
		va_end(ap);
		s.append(big.data(), n);
	}
	void hex(const UBYTE *p, size_t n)
	{
		static const char hx[] = "0123456789abcdef";
		size_t o = s.size();
		s.resize(o + n * 2);
		for (size_t i = 0; i < n; i++) {
			s[o + 2 * i] = hx[p[i] >> 4];
			s[o + 2 * i + 1] = hx[p[i] & 15];
		}
	}
	void kv_str(const char *key, const char *v)
	{
		addf(" %s=\"", key);
		for (; *v; v++) {
			if (*v == '"' || *v == '\\')
				addf("\\%c", *v);
			else if (*v == '\n')
				addf("\\n");
			else
				s.push_back(*v);
		}
		s.push_back('"');
	}
};

static int hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static long unhex(const char *s, UBYTE *out, long max)
{
	long n = 0;
	while (s[0] && s[1]) {
		int h = hexval(s[0]), l = hexval(s[1]);
		if (h < 0 || l < 0 || n >= max)
			return -1;
		out[n++] = (UBYTE) (h << 4 | l);
		s += 2;
	}
	return s[0] ? -1 : n;
}

/* symbols: name -> value (loaded from cc65 .lbl / .dbg) */
static std::map<std::string, int> sym_by_name;
static std::map<int, std::string> sym_by_addr;

/* numbers: 123, $7f, 0x7f, 7fh, %0101, or a symbol (name / _name) */
static int parse_num(const char *s, long long *out)
{
	char *end;
	size_t n;
	int neg = 0;
	if (s == NULL || !*s)
		return 0;
	if (*s == '-') {
		neg = 1;
		s++;
	}
	n = strlen(s);
	if (*s == '$')
		*out = strtoll(s + 1, &end, 16);
	else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		*out = strtoll(s + 2, &end, 16);
	else if (*s == '%')
		*out = strtoll(s + 1, &end, 2);
	else if (n > 1 && (s[n - 1] == 'h' || s[n - 1] == 'H') && isxdigit((unsigned char) s[0])) {
		*out = strtoll(s, &end, 16);
		if (end == s + n - 1)
			end++;
	}
	else if (isdigit((unsigned char) *s))
		*out = strtoll(s, &end, 10);
	else {
		std::map<std::string, int>::iterator it = sym_by_name.find(s);
		if (it == sym_by_name.end())
			it = sym_by_name.find(std::string("_") + s);
		if (it == sym_by_name.end())
			return 0;
		*out = it->second;
		if (neg)
			*out = -*out;
		return 1;
	}
	if (*end)
		return 0;
	if (neg)
		*out = -*out;
	return 1;
}

static unsigned long fnv1a(const UBYTE *p, size_t n, unsigned long h = 2166136261UL)
{
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h = (h * 16777619UL) & 0xffffffffUL;
	}
	return h;
}

/* rgb24 of the last frame */
static void frame_rgb(UBYTE *out)
{
	for (int i = 0; i < W * H; i++) {
		ULONG p = last_frame[i];
		out[i * 3] = (UBYTE) (p >> 16);
		out[i * 3 + 1] = (UBYTE) (p >> 8);
		out[i * 3 + 2] = (UBYTE) p;
	}
}

static unsigned long screen_hash(void)
{
	UBYTE rgb[W * H * 3];
	frame_rgb(rgb);
	return fnv1a(rgb, sizeof(rgb));
}

/* ------------------------------------------------------------------ */
/* CPU access */

static C6502_REGS get_regs(void)
{
	C6502_REGS r;
	memset(&r, 0, sizeof(r));
	if (lx)
		lx->GetRegs(r);
	return r;
}

/* what the CPU sees, without side effects */
static UBYTE safe_peek(UWORD addr)
{
	if (!lx)
		return 0xff;
	CLynxBase *h = lx->mMemoryHandlers[addr];
	if (h == lx->mSusie) {
		if (addr == RCART0 || addr == RCART1)
			return 0xff;    /* reading the cart port advances its counter */
		return lx->mSusie->Peek(addr);
	}
	if (h == lx->mMikie) {
		CMikie *m = lx->mMikie;
		switch (addr) {
		case TIM0CNT: return (UBYTE) m->mTIM_0_CURRENT;
		case TIM1CNT: return (UBYTE) m->mTIM_1_CURRENT;
		case TIM2CNT: return (UBYTE) m->mTIM_2_CURRENT;
		case TIM3CNT: return (UBYTE) m->mTIM_3_CURRENT;
		case TIM4CNT: return (UBYTE) m->mTIM_4_CURRENT;
		case TIM5CNT: return (UBYTE) m->mTIM_5_CURRENT;
		case TIM6CNT: return (UBYTE) m->mTIM_6_CURRENT;
		case TIM7CNT: return (UBYTE) m->mTIM_7_CURRENT;
		case SERDAT: return (UBYTE) (m->mUART_RX_DATA & 0xff);
		case IODAT: return (UBYTE) m->mIODAT;
		case DISPCTL:
			return (UBYTE) ((m->mDISPCTL_DMAEnable ? 1 : 0) | (m->mDISPCTL_Flip ? 2 : 0)
			                | (m->mDISPCTL_FourColour ? 4 : 0) | (m->mDISPCTL_Colour ? 8 : 0));
		}
		return m->Peek(addr);
	}
	return h->Peek(addr);
}

static UBYTE peek_ram(UWORD addr)
{
	return lx ? lx->mRam->Peek(addr) : 0;
}

/* ------------------------------------------------------------------ */
/* the guest log: bytes stored to $FCF0, signals to $FCF1 */

#define LOG_SIZE (4 * 1024 * 1024)
static UBYTE *log_ring = NULL;
static long long log_end = 0;
static long long log_mark = 0;
static long long signal_count = 0;
static int signal_last = -1;
static long long signal_frame = -1;

static long long log_base(void)
{
	long long b = log_end - LOG_SIZE;
	if (b < log_mark)
		b = log_mark;
	return b < 0 ? 0 : b;
}

static void set_pending(const char *reason, const char *fmt, ...)
{
	va_list ap;
	if (pend.active)
		return;
	pend.active = 1;
	pend.ea = -1;
	snprintf(pend.reason, sizeof(pend.reason), "%s", reason);
	va_start(ap, fmt);
	vsnprintf(pend.detail, sizeof(pend.detail), fmt, ap);
	va_end(ap);
	gLynxhCpuCheck = 1;
}

static void log_byte(UBYTE c)
{
	log_ring[log_end % LOG_SIZE] = c;
	log_end++;
	if (run.active && run.until_len > 0 && log_end - run.until_since >= run.until_len
	    && log_end - run.until_len >= log_base()) {
		int i;
		long long start = log_end - run.until_len;
		for (i = 0; i < run.until_len; i++)
			if (log_ring[(start + i) % LOG_SIZE] != run.until[i])
				break;
		if (i == run.until_len) {
			run.until_len = 0;
			set_pending("log", "match=%lld", start);
		}
	}
}

void LYNXH_SuzyPoke(unsigned long addr, unsigned char data)
{
	if (addr == LYNXH_LOG_ADDR_C)
		log_byte(data);
	else if (addr == LYNXH_SIGNAL_ADDR_C) {
		signal_count++;
		signal_last = data;
		signal_frame = frame;
		if (run.active && run.until_signal)
			set_pending("signal", "signal=%d", data);
	}
}

static unsigned last_chain = 0;
static long long chains_this_frame = 0, chains_last_frame = 0;

void LYNXH_SpriteChain(unsigned scb)
{
	last_chain = scb & 0xffff;
	chains_this_frame++;
}

int LYNXH_SuzyPeek(unsigned long addr)
{
	if (addr == LYNXH_LOG_ADDR_C)
		return 0x4c;   /* 'L' */
	if (addr == LYNXH_SIGNAL_ADDR_C)
		return 0x59;   /* 'Y' */
	return -1;
}

/* ------------------------------------------------------------------ */
/* breakpoint conditions (a8h's expression language)
 *   A X Y S P PC  N V B D I Z C  FRAME LINE
 *   numbers, [addr] / PEEK(addr), DPEEK(addr)
 *   = == != <> < > <= >=  + - & | ^  AND OR NOT && || !  ( )
 */

enum {
	E_NUM, E_REG, E_PEEK, E_DPEEK, E_NOT,
	E_ADD, E_SUB, E_BAND, E_BOR, E_BXOR,
	E_EQ, E_NE, E_LT, E_GT, E_LE, E_GE, E_LAND, E_LOR
};
enum { R_A, R_X, R_Y, R_S, R_P, R_PC, R_N, R_V, R_B, R_D, R_I, R_Z, R_C, R_FRAME, R_LINE };
static const char *const reg_names[] = { "A", "X", "Y", "S", "P", "PC", "N", "V", "B", "D", "I", "Z", "C",
                                         "FRAME", "LINE", NULL };

#define EXPR_MAX 64
struct enode { int op; long long v; };
struct expr_t { enode n[EXPR_MAX]; int len; };
struct eparser {
	const char *p;
	expr_t *e;
	int err;
	char tok[64];
	int tk;
	long long num;
};

static void enext(eparser *ps)
{
	const char *p = ps->p;
	while (*p == ' ' || *p == '\t')
		p++;
	ps->tok[0] = 0;
	if (!*p)
		ps->tk = 0;
	else if (isdigit((unsigned char) *p) || *p == '$' || *p == '%') {
		int n = 0;
		char buf[40];
		if (*p == '$' || *p == '%')
			buf[n++] = *p++;
		while (isalnum((unsigned char) *p) && n < 38)
			buf[n++] = *p++;
		buf[n] = 0;
		if (!parse_num(buf, &ps->num))
			ps->err = 1;
		ps->tk = 1;
	}
	else if (isalpha((unsigned char) *p) || *p == '_') {
		int n = 0;
		char raw[64];
		while ((isalnum((unsigned char) *p) || *p == '_') && n < 62) {
			raw[n] = *p;
			ps->tok[n++] = (char) toupper((unsigned char) *p++);
		}
		raw[n] = 0;
		ps->tok[n] = 0;
		ps->tk = 2;
		/* a symbol that isn't a register/keyword becomes a number */
		int i;
		for (i = 0; reg_names[i]; i++)
			if (!strcmp(ps->tok, reg_names[i]))
				break;
		if (!reg_names[i] && strcmp(ps->tok, "PEEK") && strcmp(ps->tok, "DPEEK") && strcmp(ps->tok, "AND")
		    && strcmp(ps->tok, "OR") && strcmp(ps->tok, "NOT") && parse_num(raw, &ps->num))
			ps->tk = 1;
	}
	else {
		static const char *const two[] = { "==", "!=", "<>", "<=", ">=", "&&", "||", NULL };
		int i;
		ps->tk = 3;
		for (i = 0; two[i]; i++)
			if (p[0] == two[i][0] && p[1] == two[i][1]) {
				ps->tok[0] = p[0];
				ps->tok[1] = p[1];
				ps->tok[2] = 0;
				p += 2;
				break;
			}
		if (!two[i]) {
			ps->tok[0] = *p++;
			ps->tok[1] = 0;
		}
	}
	ps->p = p;
}

static void emit(eparser *ps, int op, long long v)
{
	if (ps->e->len >= EXPR_MAX) {
		ps->err = 1;
		return;
	}
	ps->e->n[ps->e->len].op = op;
	ps->e->n[ps->e->len].v = v;
	ps->e->len++;
}

static int is_tok(eparser *ps, const char *t)
{
	return ps->tk != 0 && ps->tk != 1 && strcmp(ps->tok, t) == 0;
}

static void p_or(eparser *ps);

static void p_primary(eparser *ps)
{
	if (ps->err)
		return;
	if (ps->tk == 1) {
		emit(ps, E_NUM, ps->num);
		enext(ps);
	}
	else if (is_tok(ps, "(")) {
		enext(ps);
		p_or(ps);
		if (!is_tok(ps, ")")) { ps->err = 1; return; }
		enext(ps);
	}
	else if (is_tok(ps, "[")) {
		enext(ps);
		p_or(ps);
		if (!is_tok(ps, "]")) { ps->err = 1; return; }
		enext(ps);
		emit(ps, E_PEEK, 0);
	}
	else if (is_tok(ps, "PEEK") || is_tok(ps, "DPEEK")) {
		int op = is_tok(ps, "PEEK") ? E_PEEK : E_DPEEK;
		enext(ps);
		if (!is_tok(ps, "(")) { ps->err = 1; return; }
		enext(ps);
		p_or(ps);
		if (!is_tok(ps, ")")) { ps->err = 1; return; }
		enext(ps);
		emit(ps, op, 0);
	}
	else if (ps->tk == 2) {
		int i;
		for (i = 0; reg_names[i]; i++)
			if (strcmp(ps->tok, reg_names[i]) == 0)
				break;
		if (!reg_names[i]) { ps->err = 1; return; }
		emit(ps, E_REG, i);
		enext(ps);
	}
	else
		ps->err = 1;
}

static void p_not(eparser *ps)
{
	if (is_tok(ps, "NOT") || is_tok(ps, "!")) {
		enext(ps);
		p_not(ps);
		emit(ps, E_NOT, 0);
	}
	else
		p_primary(ps);
}

static void p_sum(eparser *ps)
{
	p_not(ps);
	while (!ps->err && ps->tk == 3 && (is_tok(ps, "+") || is_tok(ps, "-") || is_tok(ps, "&")
	                                   || is_tok(ps, "|") || is_tok(ps, "^"))) {
		int op = is_tok(ps, "+") ? E_ADD : is_tok(ps, "-") ? E_SUB : is_tok(ps, "&") ? E_BAND
		         : is_tok(ps, "|") ? E_BOR : E_BXOR;
		enext(ps);
		p_not(ps);
		emit(ps, op, 0);
	}
}

static void p_cmp(eparser *ps)
{
	p_sum(ps);
	if (!ps->err && ps->tk == 3) {
		int op = -1;
		if (is_tok(ps, "=") || is_tok(ps, "==")) op = E_EQ;
		else if (is_tok(ps, "!=") || is_tok(ps, "<>")) op = E_NE;
		else if (is_tok(ps, "<")) op = E_LT;
		else if (is_tok(ps, ">")) op = E_GT;
		else if (is_tok(ps, "<=")) op = E_LE;
		else if (is_tok(ps, ">=")) op = E_GE;
		if (op >= 0) {
			enext(ps);
			p_sum(ps);
			emit(ps, op, 0);
		}
	}
}

static void p_and(eparser *ps)
{
	p_cmp(ps);
	while (!ps->err && (is_tok(ps, "AND") || is_tok(ps, "&&"))) {
		enext(ps);
		p_cmp(ps);
		emit(ps, E_LAND, 0);
	}
}

static void p_or(eparser *ps)
{
	p_and(ps);
	while (!ps->err && (is_tok(ps, "OR") || is_tok(ps, "||"))) {
		enext(ps);
		p_and(ps);
		emit(ps, E_LOR, 0);
	}
}

static int expr_compile(const char *src, expr_t *e)
{
	eparser ps;
	memset(&ps, 0, sizeof(ps));
	ps.p = src;
	ps.e = e;
	e->len = 0;
	enext(&ps);
	p_or(&ps);
	if (ps.tk != 0)
		ps.err = 1;
	return !ps.err && e->len > 0;
}

static int display_line(void)
{
	if (!lx)
		return 0;
	CMikie *m = lx->mMikie;
	int l = (int) m->mTIM_2_BKUP - (int) m->mLynxLine;
	return l < 0 ? 0 : l;
}

static C6502_REGS hook_regs;   /* registers at the hook (valid while in the hook) */
static int hook_valid = 0;

static long long reg_value(int r)
{
	C6502_REGS g = hook_valid ? hook_regs : get_regs();
	switch (r) {
	case R_A: return g.A;
	case R_X: return g.X;
	case R_Y: return g.Y;
	case R_S: return g.SP;
	case R_P: return g.PS;
	case R_PC: return g.PC;
	case R_N: return (g.PS >> 7) & 1;
	case R_V: return (g.PS >> 6) & 1;
	case R_B: return (g.PS >> 4) & 1;
	case R_D: return (g.PS >> 3) & 1;
	case R_I: return (g.PS >> 2) & 1;
	case R_Z: return (g.PS >> 1) & 1;
	case R_C: return g.PS & 1;
	case R_FRAME: return frame;
	case R_LINE: return display_line();
	}
	return 0;
}

static long long expr_eval(const expr_t *e)
{
	long long st[EXPR_MAX];
	int sp = 0, i;
	for (i = 0; i < e->len; i++) {
		const enode *n = &e->n[i];
		long long a, b;
		switch (n->op) {
		case E_NUM: st[sp++] = n->v; break;
		case E_REG: st[sp++] = reg_value((int) n->v); break;
		case E_PEEK: st[sp - 1] = safe_peek((UWORD) st[sp - 1]); break;
		case E_DPEEK:
			a = (UWORD) st[sp - 1];
			st[sp - 1] = safe_peek((UWORD) a) | (safe_peek((UWORD) (a + 1)) << 8);
			break;
		case E_NOT: st[sp - 1] = !st[sp - 1]; break;
		default:
			b = st[--sp];
			a = st[--sp];
			switch (n->op) {
			case E_ADD: a = a + b; break;
			case E_SUB: a = a - b; break;
			case E_BAND: a = a & b; break;
			case E_BOR: a = a | b; break;
			case E_BXOR: a = a ^ b; break;
			case E_EQ: a = a == b; break;
			case E_NE: a = a != b; break;
			case E_LT: a = a < b; break;
			case E_GT: a = a > b; break;
			case E_LE: a = a <= b; break;
			case E_GE: a = a >= b; break;
			case E_LAND: a = a && b; break;
			case E_LOR: a = a || b; break;
			}
			st[sp++] = a;
		}
	}
	return sp ? st[sp - 1] : 0;
}

/* ------------------------------------------------------------------ */
/* breakpoints and watchpoints */

#define MAX_BP 64
struct bp_t {
	int used;
	long id;
	int pc;
	int has_cond;
	expr_t cond;
	char src[200];
	int once;
	long long hits;
	int watch;
	UWORD lo, hi;
	int kind;
};
static bp_t bps[MAX_BP];
static int n_bp = 0, n_wp = 0;

static void recount_bp(void)
{
	n_bp = n_wp = 0;
	for (int i = 0; i < MAX_BP; i++)
		if (bps[i].used) {
			if (bps[i].watch) n_wp++;
			else n_bp++;
		}
}

static void update_cpu_check(void)
{
	gLynxhCpuCheck = frame_pending || resume_skip
	                 || (run.active && (pend.active || run.steps >= 0 || run.over || run.to || run.at >= 0
	                                    || (!run.nobreak && (n_bp > 0 || n_wp > 0))));
}

/* 65C02 as Handy implements it (generated from lynx/c65c02.h) */
#include "lynxh_optab.inc"

static int insn_len(char m)
{
	switch (m) {
	case 'i': return 1;
	case 'a': case 'X': case 'Y': case 'j': case 'n': return 3;
	default: return 2;
	}
}

/* effective data address of the instruction at pc; returns 1 read, 2 write, 3 rmw, 0 none */
static int insn_access(const C6502_REGS &g, UWORD *ea)
{
	UWORD pc = (UWORD) g.PC;
	UBYTE op = safe_peek(pc);
	char acc = op_access[op];
	UWORD lo = safe_peek((UWORD) (pc + 1));
	UWORD w = lo | (safe_peek((UWORD) (pc + 2)) << 8);
	UWORD a;
	if (acc == '-')
		return 0;
	switch (op_mode[op]) {
	case 'z': a = lo; break;
	case 'x': a = (UBYTE) (lo + g.X); break;
	case 'y': a = (UBYTE) (lo + g.Y); break;
	case 'a': a = w; break;
	case 'X': a = (UWORD) (w + g.X); break;
	case 'Y': a = (UWORD) (w + g.Y); break;
	case 'p': {
		UBYTE zp = (UBYTE) (lo + g.X);
		a = safe_peek(zp) | (safe_peek((UBYTE) (zp + 1)) << 8);
		break;
	}
	case 'q':
		a = (UWORD) ((safe_peek(lo) | (safe_peek((UBYTE) (lo + 1)) << 8)) + g.Y);
		break;
	case 'd':
		a = (UWORD) (safe_peek(lo) | (safe_peek((UBYTE) (lo + 1)) << 8));
		break;
	default:
		return 0;
	}
	*ea = a;
	return acc == 'R' ? 1 : acc == 'W' ? 2 : 3;
}

static std::string sym_for(int addr, int exact_only)
{
	std::map<int, std::string>::iterator it = sym_by_addr.upper_bound(addr);
	if (it == sym_by_addr.begin())
		return "";
	--it;
	if (it->first == addr)
		return it->second;
	if (exact_only || addr - it->first > 0x100)
		return "";
	char b[200];
	snprintf(b, sizeof(b), "%s+%d", it->second.c_str(), addr - it->first);
	return b;
}

/* one instruction: text into out, returns its length */
static int disasm_one(UWORD pc, char *out, size_t outsz)
{
	UBYTE op = safe_peek(pc);
	char m = op_mode[op];
	int len = insn_len(m);
	UWORD lo = safe_peek((UWORD) (pc + 1));
	UWORD w = lo | (safe_peek((UWORD) (pc + 2)) << 8);
	char arg[160] = "";
	std::string s;
	char num[16];
	switch (m) {
	case '#': snprintf(arg, sizeof(arg), "#$%02x", lo); break;
	case 'z': case 'x': case 'y': case 'p': case 'q': case 'd':
		s = sym_for(lo, 1);
		snprintf(num, sizeof(num), "$%02x", lo);
		snprintf(arg, sizeof(arg), m == 'z' ? "%s" : m == 'x' ? "%s,x" : m == 'y' ? "%s,y" : m == 'p' ? "(%s,x)"
		         : m == 'q' ? "(%s),y" : "(%s)", s.empty() ? num : s.c_str());
		break;
	case 'a': case 'X': case 'Y': case 'j': case 'n':
		s = sym_for(w, 0);
		snprintf(num, sizeof(num), "$%04x", w);
		snprintf(arg, sizeof(arg), m == 'a' ? "%s" : m == 'X' ? "%s,x" : m == 'Y' ? "%s,y" : m == 'j' ? "(%s,x)" : "(%s)",
		         s.empty() ? num : s.c_str());
		break;
	case 'r': {
		UWORD t = (UWORD) (pc + 2 + (signed char) lo);
		s = sym_for(t, 0);
		snprintf(num, sizeof(num), "$%04x", t);
		snprintf(arg, sizeof(arg), "%s", s.empty() ? num : s.c_str());
		break;
	}
	}
	snprintf(out, outsz, "%s%s%s", op_name[op], arg[0] ? " " : "", arg);
	return len;
}

/* ------------------------------------------------------------------ */
/* input: buttons as cc65 reads JOYSTICK $FCB0 (bits 0-7) + PAUSE (bit 8) */

static unsigned h_buttons = 0;      /* harness layer (OR-ed with the window user's) */
static int f_active = 0;            /* forced (replay) */
static unsigned f_buttons = 0;
static unsigned cur_buttons = 0;    /* what the machine sees this frame */

/* Handy stores the joystick un-swapped: with SPRSYS lefthand clear (the
   normal orientation), a JOYSTICK read swaps up/down and left/right. */
static ULONG to_handy(unsigned b)
{
	return (b & 0x10f) | ((b & 0x80) >> 1) | ((b & 0x40) << 1) | ((b & 0x20) >> 1) | ((b & 0x10) << 1);
}

static void apply_input(void)
{
	unsigned b;
	if (f_active)
		b = f_buttons;
	else {
		b = h_buttons;
		if (fe && fe->buttons)
			b |= fe->buttons();
	}
	cur_buttons = b & 0x1ff;
	if (lx)
		lx->SetButtonData(to_handy(cur_buttons));
}

/* recorder: one entry per change */
struct recent { long long frame; unsigned buttons; };
static std::vector<recent> rec;
static int rec_on = 0;
static long long rec_start_frame = 0;
static unsigned rec_last = 0xffffffffu;

static std::vector<unsigned long> hashes;
static long long hash_first = 0;
static int hash_on = 0;

/* ------------------------------------------------------------------ */
/* power-on, loading */

static UBYTE *display_cb(ULONG objref)
{
	(void) objref;
	if (display_ready) {
		memcpy(last_frame, draw_buf, sizeof(last_frame));
		frame_pending = 1;
		gLynxhCpuCheck = 1;
	}
	return (UBYTE *) draw_buf;
}

static void reset_counters(void)
{
	frame = 0;
	ticks = 0;
	ticks_seen = gSystemCycleCount;
	frame_ticks = 0;
	last_frame_len = 0;
	hashes.clear();
	hash_first = 0;
	rec.clear();
	rec_on = 0;
	frame_pending = 0;
	stop_pending = 0;
	resume_skip = 0;
	last_chain = 0;
	chains_this_frame = chains_last_frame = 0;
	in_cpu = 0;
}

static void zero_globals(void)
{
	gSystemCycleCount = 0;
	gLastRunCycleCount = 0;
	gNextTimerEvent = 0;
	gCPUWakeupTime = 0;
	gIRQEntryCycle = 0;
	gCPUBootAddress = 0;
	gBreakpointHit = FALSE;
	gSingleStepMode = FALSE;
	gSingleStepModeSprites = FALSE;
	gSystemIRQ = FALSE;
	gSystemNMI = FALSE;
	gSystemCPUSleep = FALSE;
	gSystemCPUSleep_Saved = FALSE;
	gSystemHalt = FALSE;
	gThrottleMaxPercentage = 100;
	gThrottleLastTimerCount = 0;
	gThrottleNextCycleCheckpoint = 0;
	gTimerCount = 0;
	gAudioBufferPointer = 0;
	gAudioLastUpdateCycle = 0;
	gSkipFrame = FALSE;
	memset(gAudioBuffer, 0, sizeof(gAudioBuffer));
}

/* A fresh machine: a new CSystem every power-on, so a load in an emulator
   that ran other programs is identical to one in a fresh process. */
static void power_on(void)
{
	if (lx) {
		delete lx;
		lx = NULL;
	}
	zero_globals();
	memset(draw_buf, 0, sizeof(draw_buf));
	memset(last_frame, 0, sizeof(last_frame));
	if (!cart.empty()) {
		int have_bios = opt_bios[0] != 0;
		lx = new CSystem(NULL, cart.data(), (ULONG) cart.size(), have_bios ? opt_bios : "", !have_bios, "");
		gAudioEnabled = TRUE;
		display_ready = 0;
		lx->DisplaySetAttributes(MIKIE_NO_ROTATE, MIKIE_PIXEL_FORMAT_32BPP, W * 4, display_cb, 0);
		display_ready = 1;
	}
	reset_counters();
	apply_input();
}

static int read_file(const char *path, std::vector<UBYTE> &out)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	out.clear();
	UBYTE buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out.insert(out.end(), buf, buf + n);
	fclose(f);
	return 1;
}

static const char *file_type_name(void)
{
	if (!lx)
		return "none";
	switch (lx->mFileType) {
	case HANDY_FILETYPE_LNX: return "lnx";
	case HANDY_FILETYPE_HOMEBREW: return "o";
	case HANDY_FILETYPE_RAW: return "raw";
	case HANDY_FILETYPE_SNAPSHOT: return "lss";
	}
	return "unknown";
}

/* symbols: ld65 -Ln (VICE labels: "al 00XXXX .name") or a cc65 .dbg file */
static int load_symbols(const char *path, int *count)
{
	FILE *f = fopen(path, "r");
	char line[4096];
	int n = 0;
	if (!f)
		return 0;
	sym_by_name.clear();
	sym_by_addr.clear();
	while (fgets(line, sizeof(line), f)) {
		unsigned v;
		char name[256];
		if (sscanf(line, "al %x .%255s", &v, name) == 2) {
			sym_by_name[name] = (int) (v & 0xffff);
			if (!sym_by_addr.count((int) (v & 0xffff)) || sym_by_addr[(int) (v & 0xffff)][0] == '_'
			    || sym_by_addr[(int) (v & 0xffff)][0] == '@')
				sym_by_addr[(int) (v & 0xffff)] = name;
			n++;
		}
		else if (!strncmp(line, "sym\t", 4)) {
			char *pn = strstr(line, "name=\""), *pv = strstr(line, "val=0x"), *pt = strstr(line, "type=lab");
			if (pn && pv && pt) {
				char *e = strchr(pn + 6, '"');
				if (e && e - (pn + 6) < 255) {
					memcpy(name, pn + 6, e - (pn + 6));
					name[e - (pn + 6)] = 0;
					v = (unsigned) strtoul(pv + 6, NULL, 16);
					sym_by_name[name] = (int) (v & 0xffff);
					if (!sym_by_addr.count((int) (v & 0xffff)))
						sym_by_addr[(int) (v & 0xffff)] = name;
					n++;
				}
			}
		}
	}
	fclose(f);
	*count = n;
	return 1;
}

/* ------------------------------------------------------------------ */
/* sockets */

#define MAX_CLIENTS 8
struct client_t {
	lh_sock fd;
	std::string buf;
};
static lh_sock listen_fd = LH_BADSOCK;
static client_t clients[MAX_CLIENTS];
static int bound_port = 0;

static void client_drop(int i)
{
	if (clients[i].fd != LH_BADSOCK)
		lh_close(clients[i].fd);
	clients[i].fd = LH_BADSOCK;
	clients[i].buf.clear();
}

static void send_reply(int ci, const char *s, size_t n)
{
	size_t off = 0;
	if (ci < 0 || ci >= MAX_CLIENTS || clients[ci].fd == LH_BADSOCK)
		return;
	while (off < n) {
		long w = (long) send(clients[ci].fd, s + off, (int) (n - off), 0);
		if (w <= 0) {
			if (w < 0 && errno == EINTR)
				continue;
			client_drop(ci);
			return;
		}
		off += (size_t) w;
	}
}

static void reply_str(int ci, const char *s)
{
	send_reply(ci, s, strlen(s));
}

static void reply_sb(int ci, sbuf &b)
{
	b.s.push_back('\n');
	send_reply(ci, b.s.data(), b.s.size());
}

static void reply_err(int ci, const char *fmt, ...)
{
	char tmp[700];
	va_list ap;
	int n;
	strcpy(tmp, "ERR ");
	va_start(ap, fmt);
	vsnprintf(tmp + 4, sizeof(tmp) - 6, fmt, ap);
	va_end(ap);
	for (char *p = tmp; *p; p++)
		if (*p == '\n')
			*p = ' ';
	n = (int) strlen(tmp);
	tmp[n++] = '\n';
	send_reply(ci, tmp, n);
}

static int open_listen(void)
{
	struct sockaddr_in sa;
	socklen_t sl = sizeof(sa);
	int one = 1;
#ifdef _WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd == LH_BADSOCK)
		return 0;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short) opt_port);
	sa.sin_addr.s_addr = inet_addr(opt_host);
	if (bind(listen_fd, (struct sockaddr *) &sa, sizeof(sa)) != 0 || listen(listen_fd, 4) != 0)
		return 0;
	getsockname(listen_fd, (struct sockaddr *) &sa, &sl);
	bound_port = ntohs(sa.sin_port);
	return 1;
}

static void accept_client(void)
{
	int i, one = 1;
	lh_sock fd = accept(listen_fd, NULL, NULL);
	if (fd == LH_BADSOCK)
		return;
	for (i = 0; i < MAX_CLIENTS; i++)
		if (clients[i].fd == LH_BADSOCK)
			break;
	if (i == MAX_CLIENTS) {
		lh_close(fd);
		return;
	}
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof(one));
	clients[i].fd = fd;
	clients[i].buf.clear();
}

static void poll_sockets(int timeout_ms)
{
	struct pollfd pf[MAX_CLIENTS + 1];
	int idx[MAX_CLIENTS + 1];
	int n = 0, i, r;
	pf[n].fd = listen_fd;
	pf[n].events = POLLIN;
	idx[n++] = -1;
	for (i = 0; i < MAX_CLIENTS; i++)
		if (clients[i].fd != LH_BADSOCK) {
			pf[n].fd = clients[i].fd;
			pf[n].events = POLLIN;
			idx[n++] = i;
		}
#ifdef _WIN32
	r = WSAPoll(pf, n, timeout_ms);
#else
	r = poll(pf, n, timeout_ms);
#endif
	if (r <= 0)
		return;
	for (i = 0; i < n; i++) {
		if (!(pf[i].revents & (POLLIN | POLLHUP | POLLERR)))
			continue;
		if (idx[i] < 0)
			accept_client();
		else {
			char tmp[65536];
			long got = (long) recv(clients[idx[i]].fd, tmp, sizeof(tmp), 0);
			if (got <= 0)
				client_drop(idx[i]);
			else
				clients[idx[i]].buf.append(tmp, got);
		}
	}
}

static int take_line(int i, std::string &line)
{
	client_t *c = &clients[i];
	if (c->fd == LH_BADSOCK || c->buf.empty())
		return 0;
	size_t nl = c->buf.find('\n');
	if (nl == std::string::npos)
		return 0;
	line = c->buf.substr(0, nl);
	if (!line.empty() && line[line.size() - 1] == '\r')
		line.erase(line.size() - 1);
	c->buf.erase(0, nl + 1);
	return 1;
}

/* ------------------------------------------------------------------ */
/* command arguments: cmd key=value key="quoted value" */

#define MAX_ARGS 24
struct args_t {
	char *cmd;
	int n;
	char *k[MAX_ARGS];
	char *v[MAX_ARGS];
};

static int parse_args(char *line, args_t *a)
{
	char *p = line;
	a->n = 0;
	while (*p == ' ')
		p++;
	a->cmd = p;
	while (*p && *p != ' ')
		p++;
	if (*p)
		*p++ = 0;
	for (;;) {
		char *k, *w;
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		if (a->n >= MAX_ARGS)
			return 0;
		k = p;
		while (*p && *p != '=' && *p != ' ')
			p++;
		if (*p != '=') {
			if (*p)
				*p++ = 0;
			a->k[a->n] = k;
			a->v[a->n++] = (char *) "1";
			continue;
		}
		*p++ = 0;
		a->k[a->n] = k;
		if (*p == '"') {
			w = ++p;
			a->v[a->n] = w;
			while (*p && *p != '"') {
				if (*p == '\\' && p[1]) {
					p++;
					*w++ = *p == 'n' ? '\n' : *p;
					p++;
				}
				else
					*w++ = *p++;
			}
			if (*p == '"')
				p++;
			*w = 0;
		}
		else {
			a->v[a->n] = p;
			while (*p && *p != ' ')
				p++;
			if (*p)
				*p++ = 0;
		}
		a->n++;
	}
	return 1;
}

static const char *arg(const args_t *a, const char *k)
{
	for (int i = 0; i < a->n; i++)
		if (strcmp(a->k[i], k) == 0)
			return a->v[i];
	return NULL;
}

static int arg_num(const args_t *a, const char *k, long long *out)
{
	const char *v = arg(a, k);
	return v != NULL && parse_num(v, out);
}

static long long arg_num_def(const args_t *a, const char *k, long long def)
{
	long long v;
	return arg_num(a, k, &v) ? v : def;
}

static int arg_bool(const args_t *a, const char *k, int def)
{
	const char *v = arg(a, k);
	if (!v)
		return def;
	return !(strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "no") == 0 || strcmp(v, "off") == 0);
}

/* ------------------------------------------------------------------ */
/* status lines */

static void add_regs(sbuf &b)
{
	C6502_REGS g = get_regs();
	b.addf(" pc=%04x a=%02x x=%02x y=%02x s=%02x p=%02x", g.PC & 0xffff, g.A & 0xff, g.X & 0xff, g.Y & 0xff,
	       g.SP & 0xff, g.PS & 0xff);
}

static void add_position(sbuf &b)
{
	b.addf(" frame=%lld cycles=%lld frame_cycles=%lld midframe=%d", frame, cycles_now(), frame_cycles(), in_cpu);
	if (in_cpu)
		b.addf(" line=%d", display_line());
}

static const char *rotate_name(void)
{
	return opt_rotate == 1 ? "left" : opt_rotate == 2 ? "right" : "none";
}

static void finish_run(const char *why, const char *detail)
{
	sbuf b;
	double dt = now_s() - run.t0;
	if (dt > 0 && run.frames_done > 0)
		last_fps = run.frames_done / dt;
	b.addf("why=%s ran=%lld", why, run.frames_done);
	add_position(b);
	add_regs(b);
	if (detail && *detail)
		b.addf(" %s", detail);
	b.addf(" log=%lld", log_end);
	snprintf(last_stop, sizeof(last_stop), "%s", b.s.c_str());
	if (run.client >= 0) {
		sbuf r;
		r.addf("OK %s fps=%.1f speed=%.2f", b.s.c_str(), last_fps, last_fps / cur_hz());
		reply_sb(run.client, r);
	}
	run.active = 0;
	run.until_len = 0;
	pend.active = 0;
	update_cpu_check();
}

static void start_run(int client)
{
	run.active = 1;
	run.client = client;
	run.frames_done = 0;
	run.t0 = now_s();
	pend.active = 0;
	update_cpu_check();
}

static void start_free_run(void)
{
	memset(&run, 0, sizeof(run));
	run.steps = -1;
	run.at = -1;
	run.max_frames = -1;
	start_run(-1);
}

/* ------------------------------------------------------------------ */
/* commands */

typedef void (*cmd_fn)(int ci, args_t *a);
struct cmd_t {
	const char *name;
	cmd_fn fn;
	const char *help;
};

static void cmd_status(int ci, args_t *a)
{
	sbuf b;
	(void) a;
	b.addf("OK mode=%s", run.active && run.client >= 0 ? "running" : mode == MODE_FREE ? "free" : "paused");
	add_position(b);
	add_regs(b);
	b.addf(" log=%lld signals=%lld signal=%d signal_frame=%lld", log_end, signal_count, signal_last, signal_frame);
	b.addf(" fps=%.1f hz=%.2f rotate=%s hashing=%d recording=%d breakpoints=%d watchpoints=%d", last_fps, cur_hz(),
	       rotate_name(), hash_on, rec_on, n_bp, n_wp);
	b.addf(" turbo=%d window=%d bios=%d type=%s symbols=%d pid=%ld version=%s", free_turbo, fe && fe->window ? 1 : 0,
	       lx && lx->mRom->mValid ? 1 : 0, file_type_name(), (int) sym_by_name.size(), (long) getpid(), LYNXH_VERSION);
	b.kv_str("loaded", loaded_path);
	b.kv_str("last_stop", last_stop);
	reply_sb(ci, b);
}

static void cmd_run(int ci, args_t *a)
{
	long long v;
	const char *u;
	memset(&run, 0, sizeof(run));
	run.max_frames = arg_num_def(a, "frames", 1);
	if (run.max_frames <= 0) {
		reply_err(ci, "frames must be > 0");
		return;
	}
	run.steps = -1;
	if (arg_num(a, "steps", &v)) {
		/* stopped mid-frame, the current instruction runs without a hook call */
		run.steps = in_cpu ? v - 1 : v;
		run.has_cond = 1;
		if (run.steps < 0)
			run.steps = 0;
	}
	if (arg_bool(a, "over", 0)) {
		C6502_REGS g = get_regs();
		if (safe_peek((UWORD) g.PC) == 0x20) {
			run.over = 1;
			run.over_pc = (g.PC + 3) & 0xffff;
			run.over_s = g.SP;
		}
		else
			run.steps = in_cpu ? 0 : 1;
		run.has_cond = 1;
	}
	run.at = -1;
	if (arg_num(a, "at", &v)) {
		run.at = v;
		run.has_cond = 1;
	}
	if (arg_num(a, "to", &v)) {
		run.to = 1;
		run.to_pc = (int) (v & 0xffff);
		run.has_cond = 1;
	}
	u = arg(a, "until");
	if (u) {
		long n = unhex(u, run.until, sizeof(run.until));
		if (n <= 0) {
			reply_err(ci, "until: hex bytes expected");
			return;
		}
		run.until_len = (int) n;
		run.until_since = arg_num_def(a, "since", log_end);
		run.has_cond = 1;
	}
	run.until_signal = arg_bool(a, "until_signal", 0);
	if (run.until_signal)
		run.has_cond = 1;
	run.nobreak = arg_bool(a, "nobreak", 0);
	if (n_bp + n_wp > 0 && !run.nobreak)
		run.has_cond = 1;
	start_run(ci);
}

static void cmd_pause(int ci, args_t *a)
{
	(void) a;
	mode = MODE_PAUSED;
	if (run.active && run.client < 0)
		run.active = 0;
	update_cpu_check();
	reply_str(ci, "OK mode=paused\n");
}

static void cmd_resume(int ci, args_t *a)
{
	mode = MODE_FREE;
	free_turbo = arg_bool(a, "turbo", 0);
	reply_str(ci, "OK mode=free\n");
	if (!run.active)
		start_free_run();
}

static void cmd_reset(int ci, args_t *a)
{
	(void) a;
	/* the Lynx has no warm reset: power-on with the same cart */
	power_on();
	reply_str(ci, "OK\n");
}

static void cmd_load(int ci, args_t *a)
{
	const char *path = arg(a, "path");
	sbuf b;
	if (!path) {
		reply_err(ci, "path required");
		return;
	}
	std::vector<UBYTE> data;
	if (!read_file(path, data) || data.size() < 64) {
		reply_err(ci, "cannot load %s", path);
		return;
	}
	cart.swap(data);
	snprintf(loaded_path, sizeof(loaded_path), "%s", path);
	power_on();
	if (lx->mFileType == HANDY_FILETYPE_ILLEGAL || lx->mFileType == HANDY_FILETYPE_SNAPSHOT) {
		cart.clear();
		loaded_path[0] = 0;
		power_on();
		reply_err(ci, "not a Lynx program: %s", path);
		return;
	}
	snprintf(loaded_type, sizeof(loaded_type), "%s", file_type_name());
	/* symbols beside it (.lbl, else .dbg) */
	{
		std::string p(path), base = p;
		size_t dot = p.find_last_of('.');
		size_t sl = p.find_last_of('/');
		if (dot != std::string::npos && (sl == std::string::npos || dot > sl))
			base = p.substr(0, dot);
		int cnt = 0;
		sym_by_name.clear();
		sym_by_addr.clear();
		if (!load_symbols((base + ".lbl").c_str(), &cnt))
			load_symbols((base + ".dbg").c_str(), &cnt);
	}
	b.addf("OK type=%s size=%ld bios=%d symbols=%d", loaded_type, (long) cart.size(), lx->mRom->mValid ? 1 : 0,
	       (int) sym_by_name.size());
	b.kv_str("name", lx->CartGetName());
	reply_sb(ci, b);
}

static void cmd_rotate(int ci, args_t *a)
{
	const char *v = arg(a, "value");
	if (v) {
		if (!strcmp(v, "none") || !strcmp(v, "0")) opt_rotate = 0;
		else if (!strcmp(v, "left")) opt_rotate = 1;
		else if (!strcmp(v, "right")) opt_rotate = 2;
		else {
			reply_err(ci, "value: none, left or right");
			return;
		}
	}
	sbuf b;
	b.addf("OK rotate=%s cart_rotate=%d", rotate_name(), lx ? (int) lx->CartGetRotate() : 0);
	reply_sb(ci, b);
}

static void cmd_regs(int ci, args_t *a)
{
	sbuf b;
	char fl[9];
	(void) a;
	C6502_REGS g = get_regs();
	b.addf("OK");
	add_regs(b);
	for (int i = 0; i < 8; i++)
		fl[i] = (g.PS & (0x80 >> i)) ? "NV-BDIZC"[i] : '-';
	fl[8] = 0;
	b.addf(" flags=%s sleep=%d irq=%d", fl, gSystemCPUSleep ? 1 : 0, gSystemIRQ ? 1 : 0);
	add_position(b);
	std::string s = sym_for(g.PC & 0xffff, 0);
	if (!s.empty())
		b.kv_str("symbol", s.c_str());
	reply_sb(ci, b);
}

static void cmd_setreg(int ci, args_t *a)
{
	const char *n = arg(a, "name");
	long long v;
	if (!n || !arg_num(a, "value", &v)) {
		reply_err(ci, "name and value required");
		return;
	}
	if (!lx) {
		reply_err(ci, "nothing loaded");
		return;
	}
	C6502_REGS g = get_regs();
	std::string nm(n);
	for (size_t i = 0; i < nm.size(); i++)
		nm[i] = (char) tolower((unsigned char) nm[i]);
	if (nm == "a") g.A = (int) (v & 0xff);
	else if (nm == "x") g.X = (int) (v & 0xff);
	else if (nm == "y") g.Y = (int) (v & 0xff);
	else if (nm == "s" || nm == "sp") g.SP = (int) (v & 0xff);
	else if (nm == "p" || nm == "flags") g.PS = (int) ((v & 0xff) | 0x20);
	else if (nm == "pc") g.PC = (int) (v & 0xffff);
	else {
		reply_err(ci, "unknown register %s", n);
		return;
	}
	lx->SetRegs(g);
	reply_str(ci, "OK\n");
}

static int get_space(int ci, args_t *a, int *space)
{
	const char *s = arg(a, "space");
	*space = 0;
	if (!s || !strcmp(s, "cpu"))
		return 1;
	if (!strcmp(s, "ram")) {
		*space = 1;
		return 1;
	}
	reply_err(ci, "space: cpu or ram");
	return 0;
}

static UBYTE space_get(int space, UWORD addr)
{
	return space == 1 ? peek_ram(addr) : safe_peek(addr);
}

static void space_put(int space, UWORD addr, UBYTE v)
{
	if (!lx)
		return;
	if (space == 1)
		lx->mRam->Poke(addr, v);
	else
		lx->Poke_CPU(addr, v);   /* hardware registers take effect */
}

static void cmd_peek(int ci, args_t *a)
{
	long long addr, len = arg_num_def(a, "len", 1);
	int space;
	sbuf b;
	if (!arg_num(a, "addr", &addr) || len < 1 || len > 0x10000) {
		reply_err(ci, "addr and len=1..65536 required");
		return;
	}
	if (!get_space(ci, a, &space))
		return;
	std::vector<UBYTE> buf((size_t) len);
	for (long i = 0; i < len; i++)
		buf[i] = space_get(space, (UWORD) (addr + i));
	b.addf("OK addr=%04llx len=%lld data=", addr & 0xffff, len);
	b.hex(buf.data(), buf.size());
	reply_sb(ci, b);
}

static void cmd_poke(int ci, args_t *a)
{
	long long addr;
	int space;
	UBYTE buf[4096];
	long n;
	const char *d = arg(a, "data");
	if (!arg_num(a, "addr", &addr) || !d || (n = unhex(d, buf, sizeof(buf))) <= 0) {
		reply_err(ci, "addr and data=HEX (up to 4096 bytes) required");
		return;
	}
	if (!get_space(ci, a, &space))
		return;
	for (long i = 0; i < n; i++)
		space_put(space, (UWORD) (addr + i), buf[i]);
	reply_str(ci, "OK\n");
}

static void cmd_find(int ci, args_t *a)
{
	UBYTE pat[256];
	long n, hits = 0;
	long long start = arg_num_def(a, "start", 0), end = arg_num_def(a, "end", 0xffff), maxh = arg_num_def(a, "max", 64), i;
	int space, j;
	const char *d = arg(a, "data");
	sbuf b;
	if (!d || (n = unhex(d, pat, sizeof(pat))) <= 0) {
		reply_err(ci, "data=HEX required");
		return;
	}
	if (!get_space(ci, a, &space))
		return;
	b.addf("OK addrs=");
	for (i = start; i + n - 1 <= end && hits < maxh; i++) {
		for (j = 0; j < n; j++)
			if (space_get(space, (UWORD) (i + j)) != pat[j])
				break;
		if (j == n) {
			b.addf(hits ? ",%04llx" : "%04llx", i);
			hits++;
		}
	}
	b.addf(" count=%ld", hits);
	reply_sb(ci, b);
}

struct regname { const char *name; UWORD addr; };

static void cmd_hw(int ci, args_t *a)
{
	sbuf b;
	(void) a;
	if (!lx) {
		reply_err(ci, "nothing loaded");
		return;
	}
	CSusie *s = lx->mSusie;
	CMikie *m = lx->mMikie;
	b.addf("OK");
	/* Suzy: 16-bit registers from the chip state, 8-bit ones as they read */
	b.addf(" suzy.tmpadr=%04x suzy.tiltacum=%04x suzy.hoff=%04x suzy.voff=%04x suzy.vidbas=%04x suzy.collbas=%04x",
	       s->mTMPADR.Word, s->mTILTACUM.Word, s->mHOFF.Word, s->mVOFF.Word, s->mVIDBAS.Word, s->mCOLLBAS.Word);
	b.addf(" suzy.vidadr=%04x suzy.colladr=%04x suzy.scbnext=%04x suzy.sprdline=%04x suzy.hposstrt=%04x suzy.vposstrt=%04x",
	       s->mVIDADR.Word, s->mCOLLADR.Word, s->mSCBNEXT.Word, s->mSPRDLINE.Word, s->mHPOSSTRT.Word, s->mVPOSSTRT.Word);
	b.addf(" suzy.sprhsiz=%04x suzy.sprvsiz=%04x suzy.stretch=%04x suzy.tilt=%04x suzy.sprdoff=%04x suzy.sprvpos=%04x",
	       s->mSPRHSIZ.Word, s->mSPRVSIZ.Word, s->mSTRETCH.Word, s->mTILT.Word, s->mSPRDOFF.Word, s->mSPRVPOS.Word);
	b.addf(" suzy.colloff=%04x suzy.vsizacum=%04x suzy.hsizoff=%04x suzy.vsizoff=%04x suzy.scbadr=%04x suzy.procadr=%04x",
	       s->mCOLLOFF.Word, s->mVSIZACUM.Word, s->mHSIZOFF.Word, s->mVSIZOFF.Word, s->mSCBADR.Word, s->mPROCADR.Word);
	b.addf(" suzy.sprctl0=%02x suzy.sprctl1=%02x suzy.sprcoll=%02x suzy.sprsys=%02x suzy.suzybusen=%02x suzy.sprgo=%02x",
	       (s->mSPRCTL0_Type & 7) | (s->mSPRCTL0_Vflip ? 0x10 : 0) | (s->mSPRCTL0_Hflip ? 0x20 : 0)
	           | (((s->mSPRCTL0_PixelBits ? s->mSPRCTL0_PixelBits : 1) - 1) << 6),
	       (s->mSPRCTL1_StartLeft ? 1 : 0) | (s->mSPRCTL1_StartUp ? 2 : 0) | (s->mSPRCTL1_SkipSprite ? 4 : 0)
	           | (s->mSPRCTL1_ReloadPalette ? 8 : 0) | ((s->mSPRCTL1_ReloadDepth & 3) << 4) | (s->mSPRCTL1_Sizing ? 0x40 : 0)
	           | (s->mSPRCTL1_Literal ? 0x80 : 0),
	       (s->mSPRCOLL_Number & 0x0f) | (s->mSPRCOLL_Collide ? 0x20 : 0), s->Peek(SPRSYS), (unsigned) (s->mSUZYBUSEN & 0xff),
	       (unsigned) (s->mSPRGO & 0xff));
	b.addf(" suzy.last_chain=%04x suzy.chains_last_frame=%lld", last_chain, chains_last_frame);
	b.addf(" suzy.joystick=%02x suzy.switches=%02x suzy.sprsys_w=%02x", s->Peek(JOYSTICK), s->Peek(SWITCHES),
	       (s->mSPRSYS_SignedMath ? 0x80 : 0) | (s->mSPRSYS_Accumulate ? 0x40 : 0) | (s->mSPRSYS_NoCollide ? 0x20 : 0)
	           | (s->mSPRSYS_VStretch ? 0x10 : 0) | (s->mSPRSYS_LeftHand ? 0x08 : 0) | (s->mSPRSYS_UnsafeAccess ? 0x04 : 0)
	           | (s->mSPRSYS_StopOnCurrent ? 0x02 : 0));
	b.addf(" suzy.math_abcd=%08lx suzy.math_efgh=%08lx suzy.math_jklm=%08lx suzy.math_np=%04x",
	       (unsigned long) s->mMATHABCD.Long, (unsigned long) s->mMATHEFGH.Long, (unsigned long) s->mMATHJKLM.Long,
	       (unsigned) s->mMATHNP.Words.NP);
	b.addf(" suzy.penindex=");
	for (int i = 0; i < 16; i++)
		b.addf(i ? ",%x" : "%x", s->mPenIndex[i] & 0x0f);
	/* Mikey timers */
	static const char *const tn[8] = { "tim0", "tim1", "tim2", "tim3", "tim4", "tim5", "tim6", "tim7" };
	for (int t = 0; t < 8; t++) {
		UWORD base = (UWORD) (TIM0BKUP + 4 * t);
		b.addf(" mikey.%sbkup=%02x mikey.%sctla=%02x mikey.%scnt=%02x mikey.%sctlb=%02x", tn[t], safe_peek(base), tn[t],
		       safe_peek((UWORD) (base + 1)), tn[t], safe_peek((UWORD) (base + 2)), tn[t], safe_peek((UWORD) (base + 3)));
	}
	/* audio */
	for (int c = 0; c < 4; c++) {
		UWORD base = (UWORD) (AUD0VOL + 8 * c);
		b.addf(" mikey.aud%dvol=%02x mikey.aud%dshftfb=%02x mikey.aud%doutval=%02x mikey.aud%dl8shft=%02x"
		       " mikey.aud%dtback=%02x mikey.aud%dctl=%02x mikey.aud%dcount=%02x mikey.aud%dmisc=%02x",
		       c, safe_peek(base), c, safe_peek((UWORD) (base + 1)), c, safe_peek((UWORD) (base + 2)), c,
		       safe_peek((UWORD) (base + 3)), c, safe_peek((UWORD) (base + 4)), c, safe_peek((UWORD) (base + 5)), c,
		       safe_peek((UWORD) (base + 6)), c, safe_peek((UWORD) (base + 7)));
	}
	b.addf(" mikey.atten=%02x,%02x,%02x,%02x mikey.mpan=%02x mikey.mstereo=%02x", safe_peek(ATTEN_A), safe_peek(ATTEN_B),
	       safe_peek(ATTEN_C), safe_peek(ATTEN_D), safe_peek(MPAN), safe_peek(MSTEREO));
	b.addf(" mikey.intset=%02x mikey.intmask=%02x mikey.iodir=%02x mikey.iodat=%02x mikey.serctl=%02x",
	       (unsigned) (m->mTimerStatusFlags & 0xff), (unsigned) (m->mTimerInterruptMask & 0xff), (unsigned) (m->mIODIR & 0xff),
	       (unsigned) (m->mIODAT & 0xff), safe_peek(SERCTL));
	b.addf(" mikey.dispctl=%02x mikey.dispadr=%04x mikey.pbkup=%02x mikey.line=%d mikey.cpusleep=%d",
	       safe_peek(DISPCTL), (unsigned) (m->mDisplayAddress & 0xffff), (unsigned) (m->mTIM_2_BKUP & 0xff), display_line(),
	       gSystemCPUSleep ? 1 : 0);
	b.addf(" mikey.green=");
	for (int i = 0; i < 16; i++)
		b.addf(i ? ",%02x" : "%02x", m->mPalette[i].Colours.Green);
	b.addf(" mikey.bluered=");
	for (int i = 0; i < 16; i++)
		b.addf(i ? ",%02x" : "%02x", (m->mPalette[i].Colours.Blue << 4) | m->mPalette[i].Colours.Red);
	b.addf(" mem.mapctl=%02x mem.suzy=%d mem.mikey=%d mem.rom=%d mem.vectors=%d", lx->mMemMap->Peek(MMAPCTL),
	       lx->mMemMap->mSusieEnabled ? 1 : 0, lx->mMemMap->mMikieEnabled ? 1 : 0, lx->mMemMap->mRomEnabled ? 1 : 0,
	       lx->mMemMap->mVectorsEnabled ? 1 : 0);
	b.addf(" cart.bank0=%lu cart.bank1=%lu cart.rotate=%d", (unsigned long) lx->mCart->mMaskBank0 + 1,
	       (unsigned long) lx->mCart->mMaskBank1 + 1, (int) lx->CartGetRotate());
	reply_sb(ci, b);
}

static bp_t *bp_find(long id)
{
	for (int i = 0; i < MAX_BP; i++)
		if (bps[i].used && bps[i].id == id)
			return &bps[i];
	return NULL;
}

static bp_t *bp_new(int ci, long id)
{
	bp_t *p = bp_find(id);
	if (!p)
		for (int i = 0; i < MAX_BP; i++)
			if (!bps[i].used) {
				p = &bps[i];
				break;
			}
	if (!p) {
		reply_err(ci, "too many breakpoints (%d)", MAX_BP);
		return NULL;
	}
	memset(p, 0, sizeof(*p));
	p->id = id;
	p->pc = -1;
	return p;
}

static void cmd_break(int ci, args_t *a)
{
	long long id, pc;
	const char *c = arg(a, "cond");
	bp_t *p;
	expr_t e;
	if (!arg_num(a, "id", &id)) {
		reply_err(ci, "id required");
		return;
	}
	if (c && !expr_compile(c, &e)) {
		reply_err(ci, "bad condition: %s", c);
		return;
	}
	if (!arg_num(a, "pc", &pc) && !c) {
		reply_err(ci, "pc or cond required");
		return;
	}
	if (!(p = bp_new(ci, (long) id)))
		return;
	p->used = 1;
	if (arg_num(a, "pc", &pc))
		p->pc = (int) (pc & 0xffff);
	if (c) {
		p->has_cond = 1;
		p->cond = e;
		snprintf(p->src, sizeof(p->src), "%s", c);
	}
	p->once = arg_bool(a, "once", 0);
	recount_bp();
	reply_str(ci, "OK\n");
}

static void cmd_watch(int ci, args_t *a)
{
	long long id, addr, len = arg_num_def(a, "len", 1);
	const char *k = arg(a, "kind");
	bp_t *p;
	int kind = 3;
	if (!arg_num(a, "id", &id) || !arg_num(a, "addr", &addr) || len < 1) {
		reply_err(ci, "id, addr (and len) required");
		return;
	}
	if (k) {
		if (!strcmp(k, "r")) kind = 1;
		else if (!strcmp(k, "w")) kind = 2;
		else if (!strcmp(k, "rw")) kind = 3;
		else {
			reply_err(ci, "kind: r, w or rw");
			return;
		}
	}
	if (!(p = bp_new(ci, (long) id)))
		return;
	p->used = 1;
	p->watch = 1;
	p->lo = (UWORD) addr;
	p->hi = (UWORD) (addr + len - 1);
	p->kind = kind;
	recount_bp();
	reply_str(ci, "OK\n");
}

static void cmd_delete(int ci, args_t *a)
{
	long long id;
	bp_t *p;
	if (arg_bool(a, "all", 0)) {
		memset(bps, 0, sizeof(bps));
		recount_bp();
		reply_str(ci, "OK\n");
		return;
	}
	if (!arg_num(a, "id", &id) || !(p = bp_find((long) id))) {
		reply_err(ci, "no such breakpoint");
		return;
	}
	p->used = 0;
	recount_bp();
	reply_str(ci, "OK\n");
}

static void cmd_breaks(int ci, args_t *a)
{
	sbuf b;
	int first = 1;
	(void) a;
	b.addf("OK list=\"");
	for (int i = 0; i < MAX_BP; i++) {
		bp_t *p = &bps[i];
		if (!p->used)
			continue;
		if (!first)
			b.addf(";");
		first = 0;
		if (p->watch)
			b.addf("%ld:watch:%04x-%04x:%s:%lld", p->id, p->lo, p->hi, p->kind == 1 ? "r" : p->kind == 2 ? "w" : "rw",
			       p->hits);
		else
			b.addf("%ld:break:%d:%s:%lld", p->id, p->pc, p->src, p->hits);
	}
	b.addf("\"");
	reply_sb(ci, b);
}

static void cmd_log(int ci, args_t *a)
{
	long long base = log_base();
	long long since = arg_num_def(a, "since", base), mx = arg_num_def(a, "max", 65536), next;
	sbuf b;
	if (since < base)
		since = base;
	if (since > log_end)
		since = log_end;
	next = since + mx < log_end ? since + mx : log_end;
	b.addf("OK base=%lld end=%lld since=%lld next=%lld data=", base, log_end, since, next);
	for (long long i = since; i < next; i++) {
		UBYTE c = log_ring[i % LOG_SIZE];
		b.hex(&c, 1);
	}
	reply_sb(ci, b);
}

static void cmd_log_clear(int ci, args_t *a)
{
	(void) a;
	log_mark = log_end;
	reply_str(ci, "OK\n");
}

static void cmd_screen(int ci, args_t *a)
{
	sbuf b;
	UBYTE rgb[W * H * 3];
	(void) a;
	frame_rgb(rgb);
	b.addf("OK w=%d h=%d format=rgb24 rotate=%s frame=%lld hash=%08lx data=", W, H, rotate_name(), frame,
	       fnv1a(rgb, sizeof(rgb)));
	b.hex(rgb, sizeof(rgb));
	reply_sb(ci, b);
}

static void cmd_palette(int ci, args_t *a)
{
	sbuf b;
	UBYTE rgb[48];
	char raw[16 * 4 + 1];
	(void) a;
	raw[0] = 0;
	for (int i = 0; i < 16; i++) {
		int r = 0, g = 0, bl = 0;
		if (lx) {
			r = lx->mMikie->mPalette[i].Colours.Red;
			g = lx->mMikie->mPalette[i].Colours.Green;
			bl = lx->mMikie->mPalette[i].Colours.Blue;
		}
		rgb[i * 3] = (UBYTE) (r * 17);
		rgb[i * 3 + 1] = (UBYTE) (g * 17);
		rgb[i * 3 + 2] = (UBYTE) (bl * 17);
		snprintf(raw + strlen(raw), sizeof(raw) - strlen(raw), "%s%x%x%x", i ? "," : "", g, bl, r);
	}
	b.addf("OK data=");
	b.hex(rgb, sizeof(rgb));
	b.addf(" raw=%s", raw);
	reply_sb(ci, b);
}

static void cmd_screen_hash(int ci, args_t *a)
{
	sbuf b;
	(void) a;
	b.addf("OK hash=%08lx frame=%lld", screen_hash(), frame);
	reply_sb(ci, b);
}

static void cmd_hash(int ci, args_t *a)
{
	const char *act = arg(a, "action");
	sbuf b;
	if (!act || !strcmp(act, "status"))
		b.addf("OK on=%d first=%lld count=%ld", hash_on, hash_first, (long) hashes.size());
	else if (!strcmp(act, "on")) {
		if (!hash_on) {
			hash_on = 1;
			hashes.clear();
			hash_first = frame + 1;
		}
		b.addf("OK on=1 first=%lld", hash_first);
	}
	else if (!strcmp(act, "off")) {
		hash_on = 0;
		b.addf("OK on=0");
	}
	else if (!strcmp(act, "clear")) {
		hashes.clear();
		hash_first = frame + 1;
		b.addf("OK first=%lld", hash_first);
	}
	else if (!strcmp(act, "get")) {
		long long since = arg_num_def(a, "since", hash_first);
		long mx = (long) arg_num_def(a, "max", 100000), k = 0;
		if (since < hash_first)
			since = hash_first;
		b.addf("OK first=%lld data=", since);
		for (long long i = since - hash_first; i < (long long) hashes.size() && k < mx; i++, k++)
			b.addf("%08lx", hashes[i]);
		b.addf(" count=%ld", k);
	}
	else {
		reply_err(ci, "action: on off clear get status");
		return;
	}
	reply_sb(ci, b);
}

static void cmd_rec(int ci, args_t *a)
{
	const char *act = arg(a, "action");
	sbuf b;
	if (!act || !strcmp(act, "status"))
		b.addf("OK on=%d start=%lld count=%ld", rec_on, rec_start_frame, (long) rec.size());
	else if (!strcmp(act, "start")) {
		rec_on = 1;
		rec.clear();
		rec_start_frame = frame;
		rec_last = 0xffffffffu;
		b.addf("OK start=%lld midframe=%d", frame, in_cpu);
	}
	else if (!strcmp(act, "stop")) {
		rec_on = 0;
		b.addf("OK count=%ld end=%lld", (long) rec.size(), frame);
	}
	else if (!strcmp(act, "get")) {
		long since = (long) arg_num_def(a, "since", 0);
		if (since < 0)
			since = 0;
		b.addf("OK start=%lld end=%lld count=%ld data=", rec_start_frame, frame, (long) rec.size());
		for (long i = since; i < (long) rec.size(); i++)
			b.addf(i > since ? ",%lld:%x" : "%lld:%x", rec[i].frame, rec[i].buttons);
	}
	else {
		reply_err(ci, "action: start stop get status");
		return;
	}
	reply_sb(ci, b);
}

/* buttons=HEX (a bare number is hex here; $ / 0x prefixes accepted) */
static int arg_hex(const args_t *a, const char *k, long long *out)
{
	const char *v = arg(a, k);
	char *end;
	if (!v || !*v)
		return 0;
	if (*v == '$')
		v++;
	else if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X'))
		v += 2;
	*out = strtoll(v, &end, 16);
	return *end == 0 && end != v;
}

static void cmd_input(int ci, args_t *a)
{
	long long v;
	sbuf b;
	if (arg(a, "buttons") && !arg_hex(a, "buttons", &v)) {
		reply_err(ci, "buttons=HEX");
		return;
	}
	if (arg(a, "raw")) {
		f_active = arg_bool(a, "raw", 0);
		if (arg_hex(a, "buttons", &v))
			f_buttons = (unsigned) v & 0x1ff;
	}
	else if (arg_hex(a, "buttons", &v))
		h_buttons = (unsigned) v & 0x1ff;
	b.addf("OK buttons=%x raw=%d forced=%x seen=%x", h_buttons, f_active, f_buttons, cur_buttons);
	reply_sb(ci, b);
}

static void cmd_savestate(int ci, args_t *a)
{
	const char *path = arg(a, "path");
	if (!path) {
		reply_err(ci, "path required");
		return;
	}
	if (!lx) {
		reply_err(ci, "nothing loaded");
		return;
	}
	size_t n = lx->ContextSize();
	std::vector<UBYTE> buf(n);
	LSS_FILE fp;
	fp.memptr = buf.data();
	fp.index = 0;
	fp.index_limit = (ULONG) n;
	fp.nul_stream = 0;
	if (!lx->ContextSave(&fp)) {
		reply_err(ci, "cannot save state");
		return;
	}
	FILE *f = fopen(path, "wb");
	if (!f || fwrite(buf.data(), 1, n, f) != n) {
		if (f)
			fclose(f);
		reply_err(ci, "cannot write %s", path);
		return;
	}
	fclose(f);
	std::string side = std::string(path) + ".lynxh";
	f = fopen(side.c_str(), "w");
	if (f) {
		/* Mikey's display position isn't in Handy's state (CMikie::ContextSave):
		   without it the first frame after a load is not drawn */
		CMikie *m = lx->mMikie;
		long dispoff = m->mpDisplayCurrent && m->mpDisplayBits ? (long) (m->mpDisplayCurrent - m->mpDisplayBits) : -1;
		fprintf(f, "frame=%lld\nticks=%lld\nframe_ticks=%lld\nframe_len=%lld\nmidframe=%d\n"
		        "lynxline=%lu\nlynxdma=%lu\nlynxaddr=%lu\ndispoff=%ld\nloaded=%s\n",
		        frame, ticks_now(), frame_ticks, last_frame_len, in_cpu, (unsigned long) m->mLynxLine,
		        (unsigned long) m->mLynxLineDMACounter, (unsigned long) m->mLynxAddr, dispoff, loaded_path);
		fwrite(last_frame, 1, sizeof(last_frame), f);   /* the last frame, then the one being drawn */
		fwrite(draw_buf, 1, sizeof(draw_buf), f);
		fclose(f);
	}
	sbuf b;
	b.addf("OK midframe=%d size=%ld", in_cpu, (long) n);
	reply_sb(ci, b);
}

static void cmd_loadstate(int ci, args_t *a)
{
	const char *path = arg(a, "path");
	if (!path) {
		reply_err(ci, "path required");
		return;
	}
	std::vector<UBYTE> buf;
	if (!read_file(path, buf)) {
		reply_err(ci, "cannot read %s", path);
		return;
	}
	/* the state needs its cart: the one loaded, or the sidecar's */
	std::string side = std::string(path) + ".lynxh";
	FILE *f = fopen(side.c_str(), "rb");
	long long s_frame = 0, s_ticks = 0, s_fticks = 0, s_flen = 0;
	int s_mid = 0;
	long long s_line = -1, s_dma = 0, s_addr = 0, s_dispoff = 0;
	char s_loaded[1024] = "";
	int have_fb = 0;
	static ULONG fb[W * H], fb2[W * H];
	int have_fb2 = 0;
	if (f) {
		char line[1100];
		while (fgets(line, sizeof(line), f)) {
			size_t n = strlen(line);
			if (n && line[n - 1] == '\n')
				line[--n] = 0;
			if (!strncmp(line, "frame=", 6)) s_frame = atoll(line + 6);
			else if (!strncmp(line, "ticks=", 6)) s_ticks = atoll(line + 6);
			else if (!strncmp(line, "frame_ticks=", 12)) s_fticks = atoll(line + 12);
			else if (!strncmp(line, "frame_len=", 10)) s_flen = atoll(line + 10);
			else if (!strncmp(line, "midframe=", 9)) s_mid = atoi(line + 9);
			else if (!strncmp(line, "lynxline=", 9)) s_line = atoll(line + 9);
			else if (!strncmp(line, "lynxdma=", 8)) s_dma = atoll(line + 8);
			else if (!strncmp(line, "lynxaddr=", 9)) s_addr = atoll(line + 9);
			else if (!strncmp(line, "dispoff=", 8)) s_dispoff = atoll(line + 8);
			else if (!strncmp(line, "loaded=", 7)) {
				snprintf(s_loaded, sizeof(s_loaded), "%s", line + 7);
				break;
			}
		}
		have_fb = fread(fb, 1, sizeof(fb), f) == sizeof(fb);
		have_fb2 = have_fb && fread(fb2, 1, sizeof(fb2), f) == sizeof(fb2);
		fclose(f);
	}
	if (cart.empty() && s_loaded[0]) {
		std::vector<UBYTE> data;
		if (read_file(s_loaded, data)) {
			cart.swap(data);
			snprintf(loaded_path, sizeof(loaded_path), "%s", s_loaded);
		}
	}
	if (cart.empty()) {
		reply_err(ci, "load the program first");
		return;
	}
	power_on();
	LSS_FILE fp;
	fp.memptr = buf.data();
	fp.index = 0;
	fp.index_limit = (ULONG) buf.size();
	fp.nul_stream = 0;
	if (!lx->ContextLoad(&fp)) {
		power_on();
		reply_err(ci, "cannot load state %s (another cart?)", path);
		return;
	}
	/* Handy's state holds the CPU, memory and chips but not Mikey's display
	   position (line counter, line DMA, address, buffer pointer): the sidecar
	   does. Without it the first frame after a load stays blank. */
	{
		CMikie *m = lx->mMikie;
		m->mpDisplayBits = (UBYTE *) draw_buf;
		if (s_dispoff < 0 || s_dispoff > (long long) sizeof(draw_buf) - W * 4)
			s_dispoff = 0;
		m->mpDisplayCurrent = (UBYTE *) draw_buf + s_dispoff;
		if (s_line >= 0) {
			m->mLynxLine = (ULONG) s_line;
			m->mLynxLineDMACounter = (ULONG) s_dma;
			m->mLynxAddr = (ULONG) s_addr;
		}
		else {
			/* a plain Handy .lss: start at the top of a frame */
			m->mLynxLine = m->mTIM_2_BKUP;
			m->mLynxLineDMACounter = 0;
		}
		if (have_fb2)
			memcpy(draw_buf, fb2, sizeof(fb2));
		else if (have_fb)
			memcpy(draw_buf, fb, sizeof(fb));
	}
	reset_counters();
	frame = s_frame;
	ticks = s_ticks;
	ticks_seen = gSystemCycleCount;
	frame_ticks = s_fticks;
	last_frame_len = s_flen;
	if (have_fb)
		memcpy(last_frame, fb, sizeof(fb));
	sbuf b;
	b.addf("OK midframe=%d frame=%lld", s_mid, frame);
	reply_sb(ci, b);
}

static void wav_header(FILE *f, long frames)
{
	unsigned rate = HANDY_AUDIO_SAMPLE_FREQ, bytes = (unsigned) frames * 4;
	UBYTE h[44];
	memcpy(h, "RIFF", 4);
	unsigned v = 36 + bytes;
	h[4] = v; h[5] = v >> 8; h[6] = v >> 16; h[7] = v >> 24;
	memcpy(h + 8, "WAVEfmt ", 8);
	h[16] = 16; h[17] = h[18] = h[19] = 0;
	h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
	h[24] = rate; h[25] = rate >> 8; h[26] = rate >> 16; h[27] = rate >> 24;
	v = rate * 4;
	h[28] = v; h[29] = v >> 8; h[30] = v >> 16; h[31] = v >> 24;
	h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
	memcpy(h + 36, "data", 4);
	h[40] = bytes; h[41] = bytes >> 8; h[42] = bytes >> 16; h[43] = bytes >> 24;
	fseek(f, 0, SEEK_SET);
	fwrite(h, 1, 44, f);
	fseek(f, 0, SEEK_END);
}

static void cmd_audio(int ci, args_t *a)
{
	const char *act = arg(a, "action");
	sbuf b;
	if (act && !strcmp(act, "start")) {
		const char *path = arg(a, "path");
		if (!path) {
			reply_err(ci, "path required");
			return;
		}
		if (wav) {
			wav_header(wav, wav_frames);
			fclose(wav);
		}
		wav = fopen(path, "wb");
		if (!wav) {
			reply_err(ci, "cannot write %s", path);
			return;
		}
		wav_frames = 0;
		snprintf(wav_path, sizeof(wav_path), "%s", path);
		wav_header(wav, 0);
		b.addf("OK recording=1 rate=%d", HANDY_AUDIO_SAMPLE_FREQ);
	}
	else if (act && !strcmp(act, "stop")) {
		if (wav) {
			wav_header(wav, wav_frames);
			fclose(wav);
			wav = NULL;
		}
		b.addf("OK recording=0 frames=%ld seconds=%.2f", wav_frames, (double) wav_frames / HANDY_AUDIO_SAMPLE_FREQ);
	}
	else
		b.addf("OK recording=%d frames=%ld rate=%d", wav ? 1 : 0, wav_frames, HANDY_AUDIO_SAMPLE_FREQ);
	reply_sb(ci, b);
}

static void cmd_disasm(int ci, args_t *a)
{
	long long addr;
	long long count = arg_num_def(a, "count", 12);
	sbuf b;
	if (!arg(a, "addr") || !strcmp(arg(a, "addr"), "pc"))
		addr = get_regs().PC;
	else if (!arg_num(a, "addr", &addr)) {
		reply_err(ci, "addr: number, symbol or pc");
		return;
	}
	if (count < 1 || count > 1000)
		count = 12;
	b.addf("OK lines=\"");
	UWORD pc = (UWORD) addr;
	for (long long i = 0; i < count; i++) {
		char txt[200];
		int len = disasm_one(pc, txt, sizeof(txt));
		std::string lab = sym_for(pc, 1);
		b.addf("%s%04x:", i ? ";" : "", pc);
		for (int k = 0; k < len; k++)
			b.addf("%02x", safe_peek((UWORD) (pc + k)));
		b.addf(":%s:", lab.c_str());
		for (char *p = txt; *p; p++)
			if (*p == '"' || *p == ';' || *p == '\\')
				*p = ' ';
		b.addf("%s", txt);
		pc = (UWORD) (pc + len);
	}
	b.addf("\" next=%04x", pc);
	reply_sb(ci, b);
}

static void cmd_symbols(int ci, args_t *a)
{
	const char *path = arg(a, "path");
	const char *look = arg(a, "name");
	long long addr;
	sbuf b;
	if (path) {
		int n = 0;
		if (!load_symbols(path, &n)) {
			reply_err(ci, "cannot read %s", path);
			return;
		}
		b.addf("OK count=%d", (int) sym_by_name.size());
	}
	else if (look) {
		long long v;
		if (!parse_num(look, &v)) {
			reply_err(ci, "no symbol %s", look);
			return;
		}
		b.addf("OK value=%04llx", v & 0xffff);
	}
	else if (arg_num(a, "addr", &addr)) {
		std::string s = sym_for((int) (addr & 0xffff), 0);
		b.addf("OK addr=%04llx", addr & 0xffff);
		b.kv_str("symbol", s.c_str());
	}
	else
		b.addf("OK count=%d", (int) sym_by_name.size());
	reply_sb(ci, b);
}

/* tiny PNG writer (stored deflate blocks: no zlib needed) */
static unsigned long crc_table[256];
static void crc_init(void)
{
	for (unsigned n = 0; n < 256; n++) {
		unsigned long c = n;
		for (int k = 0; k < 8; k++)
			c = c & 1 ? 0xedb88320UL ^ (c >> 1) : c >> 1;
		crc_table[n] = c;
	}
}
static unsigned long crc_upd(unsigned long c, const UBYTE *p, size_t n)
{
	c ^= 0xffffffffUL;
	for (size_t i = 0; i < n; i++)
		c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffffUL;
}
static void be32(std::vector<UBYTE> &v, unsigned long x)
{
	v.push_back((UBYTE) (x >> 24)); v.push_back((UBYTE) (x >> 16)); v.push_back((UBYTE) (x >> 8)); v.push_back((UBYTE) x);
}
static void png_chunk(std::vector<UBYTE> &out, const char *type, const std::vector<UBYTE> &data)
{
	be32(out, (unsigned long) data.size());
	std::vector<UBYTE> td(type, type + 4);
	td.insert(td.end(), data.begin(), data.end());
	out.insert(out.end(), td.begin(), td.end());
	be32(out, crc_upd(0, td.data(), td.size()));
}
static int write_png(const char *path, const UBYTE *rgb, int w, int h)
{
	if (!crc_table[1])
		crc_init();
	std::vector<UBYTE> raw;
	for (int y = 0; y < h; y++) {
		raw.push_back(0);
		raw.insert(raw.end(), rgb + (size_t) y * w * 3, rgb + (size_t) (y + 1) * w * 3);
	}
	std::vector<UBYTE> z;
	z.push_back(0x78); z.push_back(0x01);
	size_t off = 0;
	unsigned long s1 = 1, s2 = 0;
	for (size_t i = 0; i < raw.size(); i++) {
		s1 = (s1 + raw[i]) % 65521;
		s2 = (s2 + s1) % 65521;
	}
	do {
		size_t n = raw.size() - off > 65535 ? 65535 : raw.size() - off;
		z.push_back(off + n == raw.size() ? 1 : 0);
		z.push_back((UBYTE) n); z.push_back((UBYTE) (n >> 8));
		z.push_back((UBYTE) ~n); z.push_back((UBYTE) (~n >> 8));
		z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
		off += n;
	} while (off < raw.size());
	be32(z, (s2 << 16) | s1);
	std::vector<UBYTE> out;
	static const UBYTE sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	out.insert(out.end(), sig, sig + 8);
	std::vector<UBYTE> ihdr;
	be32(ihdr, w); be32(ihdr, h);
	ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
	png_chunk(out, "IHDR", ihdr);
	png_chunk(out, "IDAT", z);
	png_chunk(out, "IEND", std::vector<UBYTE>());
	FILE *f = fopen(path, "wb");
	if (!f)
		return 0;
	int ok = fwrite(out.data(), 1, out.size(), f) == out.size();
	return fclose(f) == 0 && ok;
}

/* the last frame, rotated as chosen (or rot), scaled */
static void frame_view(int rot, int scale, std::vector<UBYTE> &out, int *w, int *h)
{
	UBYTE rgb[W * H * 3];
	frame_rgb(rgb);
	int ow = rot ? H : W, oh = rot ? W : H;
	*w = ow * scale;
	*h = oh * scale;
	out.assign((size_t) *w * *h * 3, 0);
	for (int y = 0; y < oh; y++)
		for (int x = 0; x < ow; x++) {
			int sx, sy;
			if (rot == 1) { sx = W - 1 - y; sy = x; }        /* left: turned counter-clockwise */
			else if (rot == 2) { sx = y; sy = H - 1 - x; }   /* right: clockwise */
			else { sx = x; sy = y; }
			const UBYTE *p = rgb + (sy * W + sx) * 3;
			for (int dy = 0; dy < scale; dy++)
				for (int dx = 0; dx < scale; dx++) {
					UBYTE *q = &out[(((size_t) y * scale + dy) * *w + (size_t) x * scale + dx) * 3];
					q[0] = p[0]; q[1] = p[1]; q[2] = p[2];
				}
		}
}

static void cmd_screenshot(int ci, args_t *a)
{
	const char *path = arg(a, "path");
	long long scale = arg_num_def(a, "scale", 1);
	int rot = arg_bool(a, "rotated", 1) ? opt_rotate : 0;
	if (!path) {
		reply_err(ci, "path required");
		return;
	}
	if (scale < 1 || scale > 16)
		scale = 1;
	std::vector<UBYTE> img;
	int w, h;
	frame_view(rot, (int) scale, img, &w, &h);
	if (!write_png(path, img.data(), w, h)) {
		reply_err(ci, "cannot write %s", path);
		return;
	}
	sbuf b;
	b.addf("OK w=%d h=%d hash=%08lx frame=%lld", w, h, screen_hash(), frame);
	reply_sb(ci, b);
}

static void cmd_quit(int ci, args_t *a)
{
	(void) a;
	reply_str(ci, "OK bye\n");
	if (wav) {
		wav_header(wav, wav_frames);
		fclose(wav);
	}
	exit(0);
}

static void cmd_help(int ci, args_t *a);

static const cmd_t commands[] = {
	{ "status", cmd_status, "mode, position, registers, log end, signals, hz, last_stop" },
	{ "run", cmd_run, "frames=N [steps=N] [over] [to=ADDR] [at=CYCLE] [until=HEX since=OFF] [until_signal] [nobreak]" },
	{ "pause", cmd_pause, "stop free-running at the next frame boundary" },
	{ "resume", cmd_resume, "[turbo=1] free-run (real time, or as fast as possible)" },
	{ "reset", cmd_reset, "power-on with the same cart (the Lynx has no warm reset)" },
	{ "load", cmd_load, "path=FILE: power-on and boot a .lnx (HLE boot) / BLL .o" },
	{ "rotate", cmd_rotate, "[value=none|left|right] display rotation (screenshots, window)" },
	{ "regs", cmd_regs, "A X Y S P PC, flags" },
	{ "setreg", cmd_setreg, "name=a|x|y|s|p|pc value=N" },
	{ "peek", cmd_peek, "addr=A len=N [space=cpu|ram]" },
	{ "poke", cmd_poke, "addr=A data=HEX [space=cpu|ram]" },
	{ "find", cmd_find, "data=HEX [start= end= max=] [space=]" },
	{ "hw", cmd_hw, "Suzy/Mikey registers, palette, MAPCTL" },
	{ "break", cmd_break, "id=N [pc=ADDR] [cond=EXPR] [once=1]" },
	{ "watch", cmd_watch, "id=N addr=A [len=N] [kind=r|w|rw]" },
	{ "delete", cmd_delete, "id=N | all" },
	{ "breaks", cmd_breaks, "list breakpoints and watchpoints" },
	{ "log", cmd_log, "[since=OFF] [max=N]: guest log bytes" },
	{ "log_clear", cmd_log_clear, "forget the buffered log" },
	{ "screen", cmd_screen, "the 160x102 rgb24 frame (unrotated)" },
	{ "palette", cmd_palette, "the 16 Mikey colours (rgb24 + raw GBR)" },
	{ "screen_hash", cmd_screen_hash, "FNV-1a of the rgb24 frame" },
	{ "screenshot", cmd_screenshot, "path=PNG [scale=N] [rotated=0]" },
	{ "hash", cmd_hash, "action=on|off|clear|get|status [since=F]: per-frame hashes" },
	{ "rec", cmd_rec, "action=start|stop|get|status: input recorder" },
	{ "input", cmd_input, "buttons=HEX | raw=1 buttons=HEX | raw=0" },
	{ "savestate", cmd_savestate, "path=FILE" },
	{ "loadstate", cmd_loadstate, "path=FILE" },
	{ "audio", cmd_audio, "action=start path=WAV | action=stop | status" },
	{ "disasm", cmd_disasm, "[addr=A|pc] [count=N]: 65C02 with symbols" },
	{ "symbols", cmd_symbols, "path=LBL|DBG | name=SYM | addr=A" },
	{ "quit", cmd_quit, "exit the emulator" },
	{ "help", cmd_help, "this list" },
	{ NULL, NULL, NULL }
};

static void cmd_help(int ci, args_t *a)
{
	sbuf b;
	(void) a;
	b.addf("OK commands=\"");
	for (const cmd_t *c = commands; c->name; c++)
		b.addf("%s%s: %s", c == commands ? "" : ";", c->name, c->help);
	b.addf("\"");
	reply_sb(ci, b);
}

static void exec_line(int ci, std::string &line)
{
	args_t a;
	const cmd_t *c;
	if (line.empty())
		return;
	std::vector<char> copy(line.begin(), line.end());
	copy.push_back(0);
	if (!parse_args(copy.data(), &a)) {
		reply_err(ci, "too many arguments");
		return;
	}
	for (c = commands; c->name; c++)
		if (strcmp(c->name, a.cmd) == 0)
			break;
	if (!c->name) {
		reply_err(ci, "unknown command %s (try help)", a.cmd);
		return;
	}
	c->fn(ci, &a);
}

static int client_running(void)
{
	return run.active && run.client >= 0;
}

/* Process commands. block: wait until a run starts. Otherwise handle what
   has arrived and return; a client's run takes over at once. */
static void serve(int block)
{
	int idle = 0;
#define SERVE_DONE() (client_running() || (block && run.active) || quit_req)
	for (;;) {
		int did = 0;
		for (int i = 0; i < MAX_CLIENTS && !SERVE_DONE(); i++) {
			std::string line;
			while (!SERVE_DONE() && take_line(i, line)) {
				did = 1;
				exec_line(i, line);
			}
		}
		if (SERVE_DONE())
			return;
		if (!block) {
			poll_sockets(0);
			int i;
			for (i = 0; i < MAX_CLIENTS; i++)
				if (clients[i].fd != LH_BADSOCK && clients[i].buf.find('\n') != std::string::npos)
					break;
			if (i == MAX_CLIENTS)
				return;
			continue;
		}
		if (!did) {
			check_parent();
			if (fe && fe->idle) {
				poll_sockets(15);
				fe->idle(idle == 0);
				idle++;
			}
			else
				poll_sockets(1000);
		}
	}
}

/* ------------------------------------------------------------------ */
/* the CPU hook and the main loop */

static char stop_why[16];
static char stop_detail[240];

static void request_stop(const char *why, const char *detail)
{
	snprintf(stop_why, sizeof(stop_why), "%s", why);
	snprintf(stop_detail, sizeof(stop_detail), "%s", detail);
	stop_pending = 1;
}

int LYNXH_CpuHook(int pc)
{
	UWORD ea = 0;
	int i, acc;
	char det[240];
	if (frame_pending)
		return 1;            /* the frame ended: stop at this boundary */
	/* resuming from a stop: the instruction it stopped on runs without the
	   stop checks (it would stop again), but a watch still sees its access */
	int skip = resume_skip;
	resume_skip = 0;
	if (!run.active) {
		update_cpu_check();
		return 0;
	}
	lx->GetRegs(hook_regs);
	hook_valid = 1;
	hook_regs.PC = pc;
	if (skip)
		;
	else if (pend.active) {
		if (pend.ea >= 0)
			snprintf(det, sizeof(det), "%s value=%02x", pend.detail, safe_peek((UWORD) pend.ea));
		else
			snprintf(det, sizeof(det), "%s", pend.detail);
		pend.active = 0;
		request_stop(pend.reason, det);
	}
	else if (run.steps >= 0) {
		if (run.steps == 0)
			request_stop("step", "");
		else
			run.steps--;
	}
	else if (run.over && pc == run.over_pc && hook_regs.SP >= run.over_s)
		request_stop("step", "");
	else if (run.to && pc == run.to_pc)
		request_stop("to", "");
	else if (run.at >= 0 && cycles_now() >= run.at) {
		run.at = -1;
		request_stop("at", "");
	}
	else if (!run.nobreak && n_bp > 0) {
		for (i = 0; i < MAX_BP; i++) {
			bp_t *p = &bps[i];
			if (!p->used || p->watch)
				continue;
			if (p->pc >= 0 && p->pc != pc)
				continue;
			if (p->has_cond && !expr_eval(&p->cond))
				continue;
			p->hits++;
			if (p->has_cond)
				snprintf(det, sizeof(det), "id=%ld cond=\"%s\"", p->id, p->src);
			else
				snprintf(det, sizeof(det), "id=%ld", p->id);
			if (p->once) {
				p->used = 0;
				recount_bp();
			}
			request_stop("break", det);
			break;
		}
	}
	if (stop_pending) {
		hook_valid = 0;
		return 1;
	}
	/* the instruction about to run: does it touch a watched address? */
	if (!run.nobreak && n_wp > 0 && (acc = insn_access(hook_regs, &ea)) != 0) {
		for (i = 0; i < MAX_BP; i++) {
			bp_t *p = &bps[i];
			if (!p->used || !p->watch || !(p->kind & acc) || ea < p->lo || ea > p->hi)
				continue;
			p->hits++;
			set_pending("watch", "id=%ld addr=%04x access=%s insn_pc=%04x old=%02x", p->id, ea,
			            (acc & p->kind) == 3 ? "rw" : (acc & p->kind) == 2 ? "w" : "r", pc, safe_peek(ea));
			pend.ea = ea;
			break;
		}
	}
	hook_valid = 0;
	update_cpu_check();
	return 0;
}

/* A stop inside the frame: answer the client; hold (serve) unless a free
   window run should go on (log match / position / signal). */
static void stop_here(void)
{
	int halt = mode != MODE_FREE || !(strcmp(stop_why, "log") == 0 || strcmp(stop_why, "at") == 0
	                                  || strcmp(stop_why, "signal") == 0);
	stop_pending = 0;
	in_cpu = 1;
	finish_run(stop_why, stop_detail);
	if (!halt) {
		start_free_run();
	}
	else {
		mode = MODE_PAUSED;
		while (!run.active && !quit_req)
			serve(1);
	}
	if (lx && in_cpu)
		resume_skip = 1;
	update_cpu_check();
}

static void drain_audio(void)
{
	if (!lx)
		return;
	lx->FetchAudioSamples();
	int n = (int) (gAudioBufferPointer >> 1);   /* stereo frames */
	if (n > 0) {
		const short *s = (const short *) gAudioBuffer;
		if (wav) {
			fwrite(s, 4, n, wav);
			wav_frames += n;
		}
		if (fe && fe->audio && opt_sound && mode == MODE_FREE && !(run.active && run.client >= 0))
			fe->audio(s, n);
	}
	gAudioBufferPointer = 0;
}

static void frame_end(void)
{
	static double next_t = 0.0;
	frame++;
	chains_last_frame = chains_this_frame;
	chains_this_frame = 0;
	last_frame_len = ticks_now() - frame_ticks;
	frame_ticks = ticks_now();
	drain_audio();
	if (hash_on)
		hashes.push_back(screen_hash());
	if (rec_on && (rec.empty() || cur_buttons != rec_last)) {
		recent r;
		r.frame = frame - 1;   /* the input was applied during the frame that just ended */
		r.buttons = cur_buttons;
		rec.push_back(r);
		rec_last = cur_buttons;
	}
	if ((frame & 63) == 0)
		check_parent();
	if (fe && fe->frame) {
		/* free-running: every frame; a client's run (turbo): ~30 times a second */
		static double last_present = 0.0;
		int client = run.active && run.client >= 0;
		double t = client ? now_s() : 0.0;
		if (!client || t - last_present > 1.0 / 30) {
			fe->frame();
			last_present = client ? t : now_s();
		}
	}
	if (!run.active)
		return;
	run.frames_done++;
	if (run.client >= 0) {
		if (run.frames_done >= run.max_frames)
			finish_run(run.has_cond ? "frame_limit" : "frames", "");
	}
	else {
		double dt = now_s() - run.t0;
		if (dt > 0.5)
			last_fps = run.frames_done / dt;
		if (!free_turbo) {
			double now = now_s();
			if (next_t == 0.0 || now - next_t > 0.25)
				next_t = now;
			next_t += 1.0 / cur_hz();
			if (next_t > now)
				sleep_s(next_t - now);
		}
	}
}

/* run instructions to the end of the frame. Returns 1 at the frame end, 0
   when stopped inside it. */
#define FRAME_CAP_TICKS (HANDY_SYSTEM_FREQ / 15)   /* no display: a frame every 1/15 s */
static int emulate_frame(void)
{
	if (!lx) {
		/* nothing loaded: blank frames at 75 Hz */
		ticks += HANDY_SYSTEM_FREQ / 75;
		return 1;
	}
	for (;;) {
		lx->Update();
		sync_ticks();
		if (frame_pending) {
			frame_pending = 0;
			update_cpu_check();
			return 1;
		}
		if (stop_pending)
			return 0;
		if (ticks - frame_ticks >= FRAME_CAP_TICKS)
			return 1;
	}
}

static void main_loop(void)
{
	while (!quit_req) {
		/* at a frame boundary: commands, then input for the next frame */
		in_cpu = 0;
		for (;;) {
			if (!run.active) {
				if (mode == MODE_FREE)
					start_free_run();
				else
					serve(1);
			}
			else if (run.client < 0)
				serve(0);
			if (run.active || quit_req)
				break;
		}
		if (quit_req)
			break;
		apply_input();
		update_cpu_check();
		/* a frame, with stops inside it */
		for (;;) {
			if (emulate_frame()) {
				in_cpu = 0;
				frame_end();
				break;
			}
			stop_here();
			if (quit_req || !lx || !in_cpu)
				break;   /* load/reset/loadstate during the stop: a new frame */
		}
	}
}

/* ------------------------------------------------------------------ */

const unsigned int *LYNXH_Frame(void) { return (const unsigned int *) last_frame; }
int LYNXH_Rotate(void) { return opt_rotate; }
int LYNXH_Zoom(void) { return opt_zoom; }
int LYNXH_Sound(void) { return opt_sound; }
void LYNXH_RequestQuit(void) { quit_req = 1; }

static void usage(const char *p)
{
	fprintf(stderr,
	        "usage: %s --port N [--paused|--free] [--seed N] [--rotate none|left|right] [--zoom N] [--nosound]\n"
	        "          [--bios lynxboot.img] [--host 127.0.0.1] [--load FILE]\n"
	        "Without --bios the HLE boot is used (no ROM file is ever searched for).\n",
	        p);
}

int LYNXH_Main(int argc, char *argv[], const struct lynxh_frontend *frontend)
{
	const char *load_path = NULL;
	int have_port = 0;
	fe = frontend;
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	for (int i = 1; i < argc; i++) {
		const char *o = argv[i];
		if (o[0] == '-' && o[1] == '-')
			o++;
		if (!strcmp(o, "-port") && i + 1 < argc) {
			opt_port = atoi(argv[++i]);
			have_port = 1;
		}
		else if (!strcmp(o, "-host") && i + 1 < argc)
			snprintf(opt_host, sizeof(opt_host), "%s", argv[++i]);
		else if (!strcmp(o, "-free"))
			opt_free = 1;
		else if (!strcmp(o, "-paused"))
			opt_free = 0;
		else if (!strcmp(o, "-seed") && i + 1 < argc)
			opt_seed = strtoul(argv[++i], NULL, 0);
		else if (!strcmp(o, "-rotate") && i + 1 < argc) {
			const char *v = argv[++i];
			opt_rotate = !strcmp(v, "left") ? 1 : !strcmp(v, "right") ? 2 : 0;
		}
		else if (!strcmp(o, "-zoom") && i + 1 < argc)
			opt_zoom = atoi(argv[++i]);
		else if (!strcmp(o, "-nosound"))
			opt_sound = 0;
		else if (!strcmp(o, "-bios") && i + 1 < argc)
			snprintf(opt_bios, sizeof(opt_bios), "%s", argv[++i]);
		else if (!strcmp(o, "-load") && i + 1 < argc)
			load_path = argv[++i];
		else if (!strcmp(o, "-help") || !strcmp(o, "-h")) {
			usage(argv[0]);
			return 0;
		}
		else {
			fprintf(stderr, "lynxh: unknown option %s\n", argv[i]);
			usage(argv[0]);
			return 2;
		}
	}
	if (!have_port) {
		usage(argv[0]);
		return 2;
	}
	if (opt_zoom < 1 || opt_zoom > 16)
		opt_zoom = 3;
	(void) opt_seed;   /* the Lynx has no random source to seed; power-on is fixed */
	for (int i = 0; i < MAX_CLIENTS; i++)
		clients[i].fd = LH_BADSOCK;
	log_ring = (UBYTE *) calloc(1, LOG_SIZE);
	if (!open_listen()) {
		fprintf(stderr, "lynxh: cannot listen on %s:%d\n", opt_host, opt_port);
		return 4;
	}
	memset(&run, 0, sizeof(run));
	run.steps = -1;
	run.at = -1;
	power_on();
	if (load_path) {
		std::vector<UBYTE> data;
		if (read_file(load_path, data)) {
			cart.swap(data);
			snprintf(loaded_path, sizeof(loaded_path), "%s", load_path);
			power_on();
		}
	}
	mode = (opt_free < 0 ? (fe && fe->window) : opt_free) ? MODE_FREE : MODE_PAUSED;
	if (getenv("LYNXH_PARENT_WATCH"))
		parent_pid = (long) getppid();
	printf("LYNXH port=%d pid=%ld\n", bound_port, (long) getpid());
	fflush(stdout);
	main_loop();
	if (wav) {
		wav_header(wav, wav_frames);
		fclose(wav);
	}
	return 0;
}
