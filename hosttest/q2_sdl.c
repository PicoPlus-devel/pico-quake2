/*
 * SDL front end of the host harness (q2_host_sdl): a window, keyboard, mouse and sound
 * around the same engine objects as q2_host. q2_host.c, built with -DQ2_HOST_SDL, keeps
 * the files, the framebuffer, the flash model and the main loop; it runs on real time
 * and shows each frame here. This file has the quakegeneric input hooks, SNDDMA and
 * QG_PlatformConfig of that build.
 *
 * As on the board:
 * - the picture is 320x240 doubled to 640x480, each channel truncated to 5 bits
 *   (rgb555x2 in src/pico/q2_display.c);
 * - keys are mapped by position: SDL scancodes are USB HID usages, so the table is the
 *   firmware's (src/pico/q2_input.c);
 * - the sound ring is played up to what the engine has painted and no further
 *   (src/pico/q2_audio.c).
 * Gamepads are not supported.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <SDL.h>

#include "client/client.h"
#include "client/snd_loc.h"
#include "quakegeneric.h"

void Quake2_SendKey(int key, int down);
extern int soundtime;	/* client/snd_dma.c */

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

/* ---- window ---- */

int q2_sdl_init(void)
{
	if (SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "q2_host_sdl: %s\n", SDL_GetError());
		return 0;
	}
	atexit(SDL_Quit);
	window = SDL_CreateWindow("pico-quake2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		640, 480, SDL_WINDOW_RESIZABLE);
	if (window)
		renderer = SDL_CreateRenderer(window, -1, 0);
	if (renderer)
		texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING, 320, 240);
	if (!texture) {
		fprintf(stderr, "q2_host_sdl: %s\n", SDL_GetError());
		return 0;
	}
	SDL_RenderSetLogicalSize(renderer, 320, 240);	/* 4:3 when the window is resized */
	printf("q2_host_sdl: %s video\n", SDL_GetCurrentVideoDriver());
	return 1;
}

int q2_sdl_ticks(void)
{
	return (int)SDL_GetTicks();
}

void q2_sdl_present(const byte *fb, byte palette[256][3])
{
	uint32_t lut[256];
	void *pixels;
	int pitch;

	for (int i = 0; i < 256; i++)
		lut[i] = 0xff000000u | (palette[i][0] & 0xf8) << 16 | (palette[i][1] & 0xf8) << 8 |
			(palette[i][2] & 0xf8);
	if (SDL_LockTexture(texture, NULL, &pixels, &pitch))
		return;
	for (int y = 0; y < 240; y++) {
		uint32_t *row = (uint32_t *)((char *)pixels + y * pitch);
		for (int x = 0; x < 320; x++)
			row[x] = lut[fb[y * 320 + x]];
	}
	SDL_UnlockTexture(texture);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
	/* a level load draws but does not return to the main loop for a while */
	SDL_PumpEvents();
}

/* ---- keyboard and mouse ---- */

/* SDL scancode (= HID usage) -> Quake II key, as hid_to_key in src/pico/q2_input.c */
static const unsigned char scancode_to_key[0x68] = {
	[0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd', [0x08] = 'e',
	[0x09] = 'f', [0x0a] = 'g', [0x0b] = 'h', [0x0c] = 'i', [0x0d] = 'j',
	[0x0e] = 'k', [0x0f] = 'l', [0x10] = 'm', [0x11] = 'n', [0x12] = 'o',
	[0x13] = 'p', [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
	[0x18] = 'u', [0x19] = 'v', [0x1a] = 'w', [0x1b] = 'x', [0x1c] = 'y',
	[0x1d] = 'z',
	[0x1e] = '1', [0x1f] = '2', [0x20] = '3', [0x21] = '4', [0x22] = '5',
	[0x23] = '6', [0x24] = '7', [0x25] = '8', [0x26] = '9', [0x27] = '0',
	[0x28] = K_ENTER, [0x29] = K_ESCAPE, [0x2a] = K_BACKSPACE, [0x2b] = K_TAB,
	[0x2c] = K_SPACE, [0x2d] = '-', [0x2e] = '=', [0x2f] = '[', [0x30] = ']',
	[0x31] = '\\', [0x32] = '\\', [0x33] = ';', [0x34] = '\'', [0x35] = '`',
	[0x36] = ',', [0x37] = '.', [0x38] = '/',
	[0x3a] = K_F1, [0x3b] = K_F2, [0x3c] = K_F3, [0x3d] = K_F4,
	[0x3e] = K_F5, [0x3f] = K_F6, [0x40] = K_F7, [0x41] = K_F8,
	[0x42] = K_F9, [0x43] = K_F10, [0x44] = K_F11, [0x45] = K_F12,
	[0x48] = K_PAUSE, [0x49] = K_INS, [0x4a] = K_HOME, [0x4b] = K_PGUP,
	[0x4c] = K_DEL, [0x4d] = K_END, [0x4e] = K_PGDN,
	[0x4f] = K_RIGHTARROW, [0x50] = K_LEFTARROW, [0x51] = K_DOWNARROW, [0x52] = K_UPARROW,
	[0x54] = K_KP_SLASH, [0x55] = '*', [0x56] = K_KP_MINUS, [0x57] = K_KP_PLUS,
	[0x58] = K_KP_ENTER, [0x59] = K_KP_END, [0x5a] = K_KP_DOWNARROW, [0x5b] = K_KP_PGDN,
	[0x5c] = K_KP_LEFTARROW, [0x5d] = K_KP_5, [0x5e] = K_KP_RIGHTARROW, [0x5f] = K_KP_HOME,
	[0x60] = K_KP_UPARROW, [0x61] = K_KP_PGUP, [0x62] = K_KP_INS, [0x63] = K_KP_DEL,
	[0x64] = '\\',
};

static int scancode_key(SDL_Scancode sc)
{
	if ((unsigned)sc < sizeof(scancode_to_key))
		return scancode_to_key[sc];
	switch (sc) {
	case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return K_CTRL;
	case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return K_SHIFT;
	case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return K_ALT;
	default: return 0;
	}
}

static int button_key(int button)
{
	switch (button) {
	case SDL_BUTTON_LEFT: return K_MOUSE1;
	case SDL_BUTTON_RIGHT: return K_MOUSE2;
	case SDL_BUTTON_MIDDLE: return K_MOUSE3;
	default: return 0;
	}
}

/* The engine captures the mouse in game and releases it in menus and the console
 * (IN_Frame); it stays released while the window does not have the focus. */
static int capture_wanted;
static int mouse_dx, mouse_dy;

static void update_capture(void)
{
	int on = capture_wanted && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);

	SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

void QG_CaptureMouse(void)
{
	capture_wanted = 1;
	update_capture();
}

void QG_ReleaseMouse(void)
{
	capture_wanted = 0;
	update_capture();
}

void QG_GetMouseDiff(int *dx, int *dy)
{
	*dx = mouse_dx;
	*dy = mouse_dy;
	mouse_dx = mouse_dy = 0;
}

/* Turns the pending SDL events into key events; 0 when the window was closed. */
int q2_sdl_poll(void)
{
	SDL_Event ev;
	int key, notches;

	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_QUIT:
			return 0;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			/* auto-repeats are passed on: the engine repeats only backspace and page up/down */
			if ((key = scancode_key(ev.key.keysym.scancode)))
				Quake2_SendKey(key, ev.type == SDL_KEYDOWN);
			break;
		case SDL_MOUSEMOTION:
			if (SDL_GetRelativeMouseMode()) {
				mouse_dx += ev.motion.xrel;
				mouse_dy += ev.motion.yrel;
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			if ((key = button_key(ev.button.button)))
				Quake2_SendKey(key, ev.type == SDL_MOUSEBUTTONDOWN);
			break;
		case SDL_MOUSEWHEEL:
			/* the wheel has no state: one press and release per notch */
			notches = ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
			key = notches > 0 ? K_MWHEELUP : K_MWHEELDOWN;
			for (int i = 0; i < 4 && i < abs(notches); i++) {
				Quake2_SendKey(key, 1);
				Quake2_SendKey(key, 0);
			}
			break;
		case SDL_WINDOWEVENT:
			if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
			    ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
				update_capture();
			break;
		}
	}
	return 1;
}

/* The board's defaults for the mouse and the picture (QG_PlatformConfig in
 * src/pico/q2_input.c, without the gamepad bindings). */
const char *QG_PlatformConfig(void)
{
	return
		"bind MWHEELUP weapprev\n"
		"bind MWHEELDOWN weapnext\n"
		"set freelook 1\n"
		"set cl_run 1\n"
		"set viewsize 70\n"
		"set vid_gamma 0.7\n";
}

/* ---- SNDDMA: the engine's ring, played by the SDL audio thread ---- */

#define MIX_RATE	22050
#define RING_FRAMES	8192	/* as on the board: s_mixahead plus a long frame */

static SDL_AudioDeviceID audio;
static short ring[RING_FRAMES * 2];	/* stereo frames */
static uint32_t play_pos;	/* frames played since SNDDMA_Init (audio thread) */
static uint32_t painted_end;	/* play_pos value up to which the ring is painted */
static uint32_t seen_pos;	/* play_pos as of the last SNDDMA_GetDMAPos */

/* Play stops at the last painted frame: a stalled main loop (a level load) gets
 * silence instead of the stale ring, and the play position never runs a whole ring
 * past what the engine has seen (GetSoundtime counts wraps). */
static void audio_callback(void *userdata, Uint8 *stream, int len)
{
	short *out = (short *)stream;

	for (int n = len / 4; n > 0; n--, out += 2) {
		if ((int)(painted_end - play_pos) > 0) {
			const short *f = &ring[(play_pos % RING_FRAMES) * 2];
			out[0] = f[0];
			out[1] = f[1];
			play_pos++;
		} else {
			out[0] = out[1] = 0;
		}
	}
}

qboolean SNDDMA_Init(void)
{
	SDL_AudioSpec want;

	if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		printf("q2_host_sdl: no sound: %s\n", SDL_GetError());
		return false;
	}
	memset(&want, 0, sizeof(want));
	want.freq = MIX_RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 512;	/* 23 ms */
	want.callback = audio_callback;
	audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);	/* SDL converts to the device's rate */
	if (!audio) {
		printf("q2_host_sdl: no sound: %s\n", SDL_GetError());
		return false;
	}
	memset(ring, 0, sizeof(ring));
	play_pos = painted_end = seen_pos = 0;

	memset(&dma, 0, sizeof(dma));
	dma.channels = 2;
	dma.samplebits = 16;
	dma.speed = MIX_RATE;
	dma.samples = RING_FRAMES * 2;
	dma.submission_chunk = 1;
	dma.buffer = (byte *)ring;
	SDL_PauseAudioDevice(audio, 0);
	printf("q2_host_sdl: %s sound\n", SDL_GetCurrentAudioDriver());
	return true;
}

int SNDDMA_GetDMAPos(void)
{
	SDL_LockAudioDevice(audio);
	seen_pos = play_pos;
	SDL_UnlockAudioDevice(audio);
	dma.samplepos = (int)((seen_pos * 2) & (uint32_t)(dma.samples - 1));
	return dma.samplepos;
}

void SNDDMA_Shutdown(void)
{
	if (audio)
		SDL_CloseAudioDevice(audio);
	audio = 0;
}

void SNDDMA_BeginPainting(void) {}

/* After each S_Update: publish how far the ring is painted. The engine's soundtime is
 * seen_pos in its own count, as in q2_audio.c. */
void SNDDMA_Submit(void)
{
	int ahead = paintedtime - soundtime;

	SDL_LockAudioDevice(audio);
	painted_end = seen_pos + (uint32_t)(ahead > 0 ? ahead : 0);
	SDL_UnlockAudioDevice(audio);
}
