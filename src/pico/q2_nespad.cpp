// q2_nespad.cpp -- NES/SNES controller ports on PIO (pico_shared nespad).
//
// The USB timer (q2_usb.cpp) asks for the buttons every 4 ms. Each call collects
// the read started on the previous call and starts the next one, so a read
// (about 390 us of 32 bits at the driver's 1 MHz PIO clock) never stalls the
// interrupt.
// The buttons are translated to io::GamePadState::Button bits per port and
// merged with the USB pads, so a pad in a port acts exactly like the USB pad
// with the same buttons (q2_input.c names them by position).
//
// A SNES Mouse in either port (NESPAD_SNES_MOUSE in the board header) is read
// by the same reads; q2_usb.cpp merges its buttons and motion with a USB mouse.
//
// From pico-duke3D's duke_usb_input.cpp (nesButtons, nesToButtons), which has the
// two rules learned on hardware:
//   - nespad_begin starts each state machine at 1 MHz, so its first "irq wait"
//     lands about 1 us after enable. A read started before that is lost and the
//     state machine waits forever; hence the wait before the first start.
//   - nespad_read_finish blocks, so it is only called once nespad_read_ready
//     says the words are there. A port that never delivers is given up after 1 s.
//
// And one learned in pico-lutionX: a state machine hands its word over about
// 5 us before it is back on its "irq wait", and a start in those 5 us is lost
// in the same way. After the interrupts were off for a while (a flash erase, a
// USB device enumerating) the timer runs the missed ticks back to back,
// microseconds apart, and one of them could land there; the ports were then
// disabled after a level load. So a read is collected and the next one started
// at most once per READ_GAP_US, long after the state machines are back on
// their wait.
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
#define READ_GAP_US		1000
// Turning speed of a SNES Mouse. The driver keeps the mouse at its lowest
// sensitivity, about 50 counts per inch. Its own higher settings only boost a
// movement of 4 counts or more between two reads, which at a read every 4 ms
// takes a swipe of 15 inches a second, so the acceleration is done here: a slow
// movement gets MOUSE_GAIN_SLOW mouse units per count, for aiming, and from
// MOUSE_SPEED_SLOW to MOUSE_SPEED_FAST the gain rises to MOUSE_GAIN_FAST. Quake
// II turns a unit into sensitivity x m_yaw degrees, 0.066 by default, so that
// is about a USB mouse of 800 counts per inch. Speeds are in 1/256 counts per
// read, averaged over about 8 reads (32 ms).
#define MOUSE_GAIN_SLOW		4		// 13 degrees per inch by default
#define MOUSE_GAIN_FAST		16		// 53 degrees per inch by default
#define MOUSE_SPEED_SLOW	51		// 0.2 counts per read: 1 inch a second
#define MOUSE_SPEED_FAST	256		// 1 count per read: 5 inches a second

static bool		up, dead;
static uint32_t	last;				// io bits of the last completed read
static uint8_t	mouse_buttons;		// SNES Mouse buttons of the last completed read
static int32_t	mouse_speed;		// SNES Mouse counts per read, x256, averaged
static int32_t	mouse_units[2];		// SNES Mouse motion not yet handed out, in 1/16 units
static uint32_t	started;			// time_us_32 of the last nespad_read_start
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

// Moves the SNES Mouse counts of the last read into mouse_units[], at the gain
// for the current speed
static void mouse_accelerate (void)
{
	int32_t	cx = 0, cy = 0, c, gain;	// gain in 1/16 units per count

	for (int i = 0; i < 2; i++)
	{
		cx += nespad_mouse[i].dx;
		cy += nespad_mouse[i].dy;
		nespad_mouse[i].dx = nespad_mouse[i].dy = 0;
	}
	c = (cx < 0 ? -cx : cx) + (cy < 0 ? -cy : cy);
	mouse_speed += (c * 256 - mouse_speed) / 8;
	if (mouse_speed <= MOUSE_SPEED_SLOW)
		gain = MOUSE_GAIN_SLOW * 16;
	else if (mouse_speed >= MOUSE_SPEED_FAST)
		gain = MOUSE_GAIN_FAST * 16;
	else
		gain = MOUSE_GAIN_SLOW * 16 + (mouse_speed - MOUSE_SPEED_SLOW) *
			(MOUSE_GAIN_FAST - MOUSE_GAIN_SLOW) * 16 / (MOUSE_SPEED_FAST - MOUSE_SPEED_SLOW);
	mouse_units[0] += cx * gain;
	mouse_units[1] += cy * gain;
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
	started = time_us_32 ();
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
	if (time_us_32 () - started < READ_GAP_US)
		return last;
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
			mouse_buttons = 0;
		}
		return last;
	}
	not_ready_since = 0;
	nespad_read_finish ();
	// a mouse port reads as no buttons here
	last = to_buttons (nespad_states_ext[0], nespad_padtype[0] == NESPAD_TYPE_SNES)
		| to_buttons (nespad_states_ext[1], nespad_padtype[1] == NESPAD_TYPE_SNES);
	// 0 on a port without a mouse
	mouse_buttons = nespad_mouse[0].buttons | nespad_mouse[1].buttons;
	mouse_accelerate ();
	nespad_read_start ();
	started = time_us_32 ();
	return last;
}

uint8_t q2_nespad_mouse_buttons (void)
{
	return mouse_buttons;
}

// Whole units; the rest stays for the next call
void q2_nespad_mouse_motion (int *dx, int *dy)
{
	int	ux = mouse_units[0] / 16, uy = mouse_units[1] / 16;

	*dx += ux;
	*dy += uy;
	mouse_units[0] -= ux * 16;
	mouse_units[1] -= uy * 16;
}

#else

void q2_nespad_init (void)
{
}

uint32_t q2_nespad_buttons (void)
{
	return 0;
}

uint8_t q2_nespad_mouse_buttons (void)
{
	return 0;
}

void q2_nespad_mouse_motion (int *, int *)
{
}

#endif
