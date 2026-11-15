/* lynxh headless emulator: Handy + the lynxh main loop, no SDL. */
#include "lynxh.h"
#include <stddef.h>

int main(int argc, char *argv[])
{
	static const struct lynxh_frontend fe = { 0, NULL, NULL, NULL, NULL };
	return LYNXH_Main(argc, argv, &fe);
}
