/* emutest: the guest program for test_lynxh.py (the emulator's own tests).
 * Writes the harness log ($FCF0) and signals ($FCF1) directly, so it doesn't
 * depend on the SDK. Build: cl65 -t lynx -O -Ln emutest.lbl -o emutest.lnx emutest.c
 */
#include <lynx.h>
#include <tgi.h>
#include <joystick.h>
#include <stdio.h>

#define LOG (*(volatile unsigned char *) 0xFCF0)
#define SIG (*(volatile unsigned char *) 0xFCF1)
#define JOYRAW (*(volatile unsigned char *) 0xFCB0)
#define SWRAW (*(volatile unsigned char *) 0xFCB1)

unsigned char counter;      /* watched by the tests */
unsigned int frames;
static char buf[48];

static void logs(const char *s)
{
	while (*s)
		LOG = *s++;
}

void tick(void)             /* the tests break here */
{
	++counter;
}

void main(void)
{
	unsigned char j, sw, lastj = 0xff, lastsw = 0xff;
	tgi_install(tgi_static_stddrv);
	joy_install(joy_static_stddrv);
	tgi_init();
	__asm__("cli");
	sprintf(buf, "emutest: harness=%c%c\n", LOG, SIG);
	logs(buf);
	while (tgi_busy())
		;
	tgi_setcolor(COLOR_BLACK);
	tgi_clear();
	tgi_setcolor(COLOR_WHITE);
	tgi_outtextxy(8, 8, "EMUTEST");
	tgi_updatedisplay();
	logs("emutest: ready\n");
	SIG = 1;
	for (;;) {
		j = JOYRAW;
		sw = SWRAW & 1;
		if (j != lastj || sw != lastsw) {
			sprintf(buf, "input: joy=%02x pause=%u\n", j, sw);
			logs(buf);
			lastj = j;
			lastsw = sw;
			if ((j & 0x0c) == 0x0c) {     /* OPTION1+OPTION2: fail on purpose */
				logs("emutest: fail\n");
				SIG = 2;
			}
		}
		tick();
		while (tgi_busy())
			;
		tgi_setcolor(counter & 15);
		tgi_bar(0, 60, (counter & 127) + 10, 101);
		tgi_updatedisplay();
		++frames;
		if ((frames & 255) == 0) {
			sprintf(buf, "frames: %u\n", frames);
			logs(buf);
		}
	}
}
