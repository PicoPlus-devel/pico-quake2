// q2_nespad.cpp -- NES/SNES controller ports on PIO (pico_shared nespad).
//
// The USB timer (q2_usb.cpp) asks for the buttons every 4 ms. Each call collects
// the read started on the previous call and starts the next one, so a read
// (about 200 us at the driver's 1 MHz PIO clock) never stalls the interrupt.
// The buttons are translated to io::GamePadState::Button bits per port and
// merged with the USB pads, so a pad in a port acts exactly like the USB pad
// with the same buttons (q2_input.c names them by position).
//
// From pico-duke3D's duke_usb_input.cpp (nesButtons, nesToButtons), which has the
// two rules learned on hardware:
//   - nespad_begin starts each state machine at 1 MHz, so its first "irq wait"
//     lands about 1 us after enable. A read started before that is lost and the
//     state machine waits forever; hence the wait before the first start.
//   - nespad_read_finish blocks, so it is only called once nespad_read_ready
//     says the words are there. A port that never delivers is given up after 1 s.
//
// Boards without NES_PIN_CLK in their header get empty functions, and the linker
// drops the driver.

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "q2_input.h"

#if defined(NES_PIN_CLK) && NES_PIN_CLK >= 0

#include "nespad.h"
#include "gamepad.h"

#if defined(NES_PIN_CLK_1) && NES_PIN_CLK_1 >= 0
#define HAVE_PORT_1		1
#else
#define HAVE_PORT_1		0
#endif
#define DEAD_AFTER_US	1000000

static bool		up, dead;
static uint32_t	last;				// io bits of the last completed read
static uint64_t	not_ready_since;

// nespad_states_ext is in SNES serial order: bit 0 B, 1 Y, 2 Select, 3 Start,
// 4-7 up, down, left, right, 8 A, 9 X, 10 L, 11 R. A NES pad shifts out only 8
// bits, so its A and B arrive where a SNES pad has B and Y; they go on io A and
// io B, as a two-button USB pad has them. A port the driver has not identified
// (no SNES ID yet, which includes clone NES pads) is read as a NES pad: a SNES
// pad that does not identify itself does so the first time A, X, L or R is down.
static uint32_t to_buttons (uint16_t w, bool snes)
{
	using Button = io::GamePadState::Button;
	uint32_t	b = 0;

	if (w & 0x0004) b |= Button::SELECT;
	if (w & 0x0008) b |= Button::START;
	if (w & 0x0010) b |= Button::UP;
	if (w & 0x0020) b |= Button::DOWN;
	if (w & 0x0040) b |= Button::LEFT;
	if (w & 0x0080) b |= Button::RIGHT;
	if (!snes)
	{
		if (w & 0x0001) b |= Button::A;
		if (w & 0x0002) b |= Button::B;
		return b;
	}
	if (w & 0x0001) b |= Button::B;
	if (w & 0x0002) b |= Button::Y;
	if (w & 0x0100) b |= Button::A;
	if (w & 0x0200) b |= Button::X;
	if (w & 0x0400) b |= Button::L;
	if (w & 0x0800) b |= Button::R;
	return b;
}

void q2_nespad_init (void)
{
	uint32_t	cpu_khz = clock_get_hz (clk_sys) / 1000;
	bool		ok = nespad_begin (0, cpu_khz, NES_PIN_CLK, NES_PIN_DATA, NES_PIN_LAT, NES_PIO);

#if HAVE_PORT_1
	ok = nespad_begin (1, cpu_khz, NES_PIN_CLK_1, NES_PIN_DATA_1, NES_PIN_LAT_1, NES_PIO_1) && ok;
#endif
	if (!ok)
	{
		printf ("q2_nespad: no PIO state machine free, ports disabled\n");
		return;
	}
	busy_wait_us (100);		// let the state machines reach their irq wait
	nespad_read_start ();
	up = true;
#if HAVE_PORT_1
	printf ("q2_nespad: NES/SNES ports on GPIO %d-%d and %d-%d\n",
		NES_PIN_CLK, NES_PIN_LAT, NES_PIN_CLK_1, NES_PIN_LAT_1);
#else
	printf ("q2_nespad: NES/SNES port on GPIO %d-%d\n", NES_PIN_CLK, NES_PIN_LAT);
#endif
}

uint32_t q2_nespad_buttons (void)
{
	if (!up || dead)
		return 0;
	if (!nespad_read_ready ())
	{
		uint64_t	now = time_us_64 ();

		if (!not_ready_since)
			not_ready_since = now;
		else if (now - not_ready_since > DEAD_AFTER_US)
		{
			printf ("q2_nespad: a read never completed, ports disabled\n");
			dead = true;
			last = 0;
		}
		return last;
	}
	not_ready_since = 0;
	nespad_read_finish ();
	last = to_buttons (nespad_states_ext[0], nespad_padtype[0] == NESPAD_TYPE_SNES)
		| to_buttons (nespad_states_ext[1], nespad_padtype[1] == NESPAD_TYPE_SNES);
	nespad_read_start ();
	return last;
}

#else

void q2_nespad_init (void)
{
}

uint32_t q2_nespad_buttons (void)
{
	return 0;
}

#endif
