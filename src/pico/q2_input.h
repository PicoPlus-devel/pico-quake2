// q2_input.h -- USB input: the host stack (q2_usb.cpp) and its mapping to
// Quake II keys (q2_input.c).

#ifndef Q2_INPUT_H
#define Q2_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One sample of every connected device, taken by the USB timer interrupt.
typedef struct
{
	uint8_t		kb_modifier;	// HID modifier bits; 0 without a keyboard
	uint8_t		kb_keys[6];		// HID usages currently down
	uint8_t		mouse_buttons;	// bit 0 left, 1 right, 2 middle
	int			mouse_wheel;	// notches since the last sample, + = up
	uint32_t	pad_buttons;	// io::GamePadState::Button bits of all pads
	uint8_t		pad_trigger[2];	// strongest left and right trigger, 0..255
} q2_usb_state_t;

// Brings up the USB host and starts the sampling timer.
void q2_usb_init (void);

// Around a flash erase or program (q2_flash.c): masks every interrupt except
// the USB frame one, which keeps sending SOFs from SRAM so attached devices do
// not suspend. Before q2_usb_init it disables all interrupts instead.
typedef struct
{
	bool		masked;
	uint32_t	primask;
	uint32_t	iser[2];
} q2_irq_state_t;

// q2_usb_frame.c: drives the 1 ms USB frame (called by q2_usb_init).
void q2_usb_frame_start (void);

q2_irq_state_t q2_usb_flash_begin (void);
void q2_usb_flash_end (q2_irq_state_t st);

// Mouse motion accumulated since the last call (thread context).
void q2_usb_mouse_motion (int *dx, int *dy);

// The right stick with the largest deflection, -32768..32767, +y up.
void q2_usb_look (int *x, int *y);

// q2_input.c: turns one sample into key events. Runs in the timer interrupt.
void q2_input_sample (const q2_usb_state_t *s);

// q2_nespad.cpp: the NES/SNES controller ports, on boards that have them
// (NES_PIN_CLK in the board header); no-ops elsewhere. q2_nespad_buttons runs in
// the USB timer interrupt and returns io::GamePadState::Button bits. A SNES
// Mouse (NESPAD_SNES_MOUSE): q2_nespad_mouse_buttons, in the same interrupt
// after q2_nespad_buttons, returns its buttons (bit 0 left, 1 right), and
// q2_nespad_mouse_motion adds its motion since the last call to *dx and *dy
// (with the interrupts off).
void q2_nespad_init (void);
uint32_t q2_nespad_buttons (void);
uint8_t q2_nespad_mouse_buttons (void);
void q2_nespad_mouse_motion (int *dx, int *dy);

#ifdef __cplusplus
}
#endif

#endif
