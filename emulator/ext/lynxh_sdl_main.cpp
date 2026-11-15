/* lynxh window emulator: Handy + the lynxh main loop + an SDL2 window,
 * keyboard pad and sound. Free-running by default; the control socket works
 * exactly as in the headless build (a run fast-forwards the window, a
 * breakpoint holds it).
 *
 * Keys: arrows = pad, Z / Left Alt = A (outer), X / Left Cmd / Left Ctrl = B
 * (inner), 1 = OPTION1, 2 = OPTION2, P / Return = PAUSE, Esc closes the
 * window. --rotate left|right turns the display (and the arrows with it).
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "lynxh.h"

static SDL_Window *win = NULL;
static SDL_Renderer *ren = NULL;
static SDL_Texture *tex = NULL;
static SDL_AudioDeviceID audio_dev = 0;
static int tex_rot = -1;
static unsigned user_buttons = 0;

enum { B_A = 0x01, B_B = 0x02, B_OPT2 = 0x04, B_OPT1 = 0x08, B_RIGHT = 0x10, B_LEFT = 0x20, B_DOWN = 0x40, B_UP = 0x80,
       B_PAUSE = 0x100 };

static void ensure_window(void)
{
	int rot = LYNXH_Rotate(), zoom = LYNXH_Zoom();
	int w = rot ? 102 : 160, h = rot ? 160 : 102;
	if (!win) {
		win = SDL_CreateWindow("Lynx (lynxh)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w * zoom, h * zoom,
		                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
		ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
		if (!ren)
			ren = SDL_CreateRenderer(win, -1, 0);
	}
	if (tex_rot != rot) {
		if (tex)
			SDL_DestroyTexture(tex);
		tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
		SDL_RenderSetLogicalSize(ren, w, h);
		if (tex_rot >= 0)
			SDL_SetWindowSize(win, w * zoom, h * zoom);
		tex_rot = rot;
	}
}

static void present(void)
{
	ensure_window();
	const unsigned int *f = LYNXH_Frame();
	int rot = tex_rot;
	void *pix;
	int pitch;
	if (SDL_LockTexture(tex, NULL, &pix, &pitch) == 0) {
		int ow = rot ? 102 : 160, oh = rot ? 160 : 102;
		for (int y = 0; y < oh; y++) {
			Uint32 *row = (Uint32 *) ((Uint8 *) pix + y * pitch);
			for (int x = 0; x < ow; x++) {
				int sx, sy;
				if (rot == 1) { sx = 159 - y; sy = x; }
				else if (rot == 2) { sx = y; sy = 101 - x; }
				else { sx = x; sy = y; }
				row[x] = 0xff000000u | f[sy * 160 + sx];
			}
		}
		SDL_UnlockTexture(tex);
	}
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	SDL_RenderCopy(ren, tex, NULL, NULL);
	SDL_RenderPresent(ren);
}

/* the pad as the user sees it: in a rotated display the arrows turn too */
static unsigned dir_bits(int up, int down, int left, int right)
{
	int rot = LYNXH_Rotate();
	unsigned b = 0;
	if (rot == 1) {            /* display turned counter-clockwise */
		if (up) b |= B_RIGHT;
		if (down) b |= B_LEFT;
		if (left) b |= B_UP;
		if (right) b |= B_DOWN;
	}
	else if (rot == 2) {
		if (up) b |= B_LEFT;
		if (down) b |= B_RIGHT;
		if (left) b |= B_DOWN;
		if (right) b |= B_UP;
	}
	else {
		if (up) b |= B_UP;
		if (down) b |= B_DOWN;
		if (left) b |= B_LEFT;
		if (right) b |= B_RIGHT;
	}
	return b;
}

static void pump(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT)
			LYNXH_RequestQuit();
		else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
			LYNXH_RequestQuit();
	}
	const Uint8 *k = SDL_GetKeyboardState(NULL);
	unsigned b = dir_bits(k[SDL_SCANCODE_UP], k[SDL_SCANCODE_DOWN], k[SDL_SCANCODE_LEFT], k[SDL_SCANCODE_RIGHT]);
	if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_LALT]) b |= B_A;
	if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_LGUI] || k[SDL_SCANCODE_LCTRL]) b |= B_B;
	if (k[SDL_SCANCODE_1]) b |= B_OPT1;
	if (k[SDL_SCANCODE_2]) b |= B_OPT2;
	if (k[SDL_SCANCODE_P] || k[SDL_SCANCODE_RETURN]) b |= B_PAUSE;
	user_buttons = b;
}

static void fe_idle(int redraw)
{
	pump();
	if (redraw)
		present();
	else
		SDL_RenderPresent(ren);
}

static void fe_frame(void)
{
	pump();
	present();
}

static unsigned fe_buttons(void)
{
	return user_buttons;
}

static void fe_audio(const short *s, int frames)
{
	if (!audio_dev) {
		static int tried = 0;
		SDL_AudioSpec want, have;
		if (tried)
			return;
		tried = 1;
		memset(&want, 0, sizeof(want));
		want.freq = 48000;
		want.format = AUDIO_S16SYS;
		want.channels = 2;
		want.samples = 1024;
		audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
		if (!audio_dev)
			return;
		SDL_PauseAudioDevice(audio_dev, 0);
	}
	/* keep the queue short (~120 ms): drop rather than lag */
	if (SDL_GetQueuedAudioSize(audio_dev) > 48000 * 4 / 8)
		return;
	SDL_QueueAudio(audio_dev, s, (Uint32) frames * 4);
}

int main(int argc, char *argv[])
{
	static const struct lynxh_frontend fe = { 1, fe_idle, fe_frame, fe_buttons, fe_audio };
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
		fprintf(stderr, "lynxh-sdl: SDL_Init: %s\n", SDL_GetError());
		return 3;
	}
	int r = LYNXH_Main(argc, argv, &fe);
	if (audio_dev)
		SDL_CloseAudioDevice(audio_dev);
	SDL_Quit();
	return r;
}
