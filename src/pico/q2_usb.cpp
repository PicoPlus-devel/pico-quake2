// q2_usb.cpp -- USB host for keyboards, mice and gamepads on Pico-PIO-USB.
//
// The pico_shared HID driver (3rdparty/pico_shared_drivers/usb_hid) keeps the
// latest state of every device in io::. This file brings the host stack up and
// runs it from a repeating 4 ms timer on core0 at the lowest interrupt priority,
// so a key tap or mouse click shorter than a frame (60-80 ms in game) is still
// seen: the HID driver only re-arms an endpoint from tuh_task, and a device
// reports nothing but its latest state. Each tick passes a sample to
// q2_input_sample (q2_input.c), with the NES/SNES controller ports
// (q2_nespad.cpp) merged into the gamepad buttons.
//
// The 1 ms USB frame runs from its own SRAM-resident interrupt
// (q2_usb_frame.c), which also keeps SOFs going while flash is written.
//
// tuh_task and the HID callbacks run from flash, which is safe because flash
// writes (q2_flash.c) mask every interrupt but the frame one on core0, and
// core1 never touches USB.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/dma.h"
#include "tusb.h"
#include "pio_usb_configuration.h"
#include "gamepad.h"

#include "q2_input.h"

#ifndef HAS_USBPIO
#error "q2_usb.cpp needs a board with Pico-PIO-USB (HAS_USBPIO)"
#endif

#define USB_TICK_US		4000
#define USB_TICK_ALARM	1		// hardware alarm for the sampling timer

static alarm_pool_t		*tick_pool;
static repeating_timer_t	tick_timer;

// The HID driver's slot for a keyboard has short name "KB" (a keyboard is also
// translated into pad buttons there, which this port does not want).
static bool slot_is_keyboard (const io::GamePadState &gp)
{
	return gp.GamePadShortName && strcmp (gp.GamePadShortName, "KB") == 0;
}

// A slot is in use from mount (which names it) to unmount (which clears the
// name). The driver only marks some pad types connected, so the name decides.
static bool slot_in_use (const io::GamePadState &gp)
{
	return gp.GamePadShortName != nullptr;
}

static void usb_sample (void)
{
	q2_usb_state_t	s;
	bool			keyboard = false;

	memset (&s, 0, sizeof(s));
	for (int i = 0; i < 2; i++)
	{
		const io::GamePadState	&gp = io::getCurrentGamePadState (i);

		if (!slot_in_use (gp))
			continue;
		if (slot_is_keyboard (gp))
		{
			keyboard = true;
			continue;
		}
		s.pad_buttons |= gp.buttons;
		for (int t = 0; t < 2; t++)
			if (gp.trigger[t] > s.pad_trigger[t])
				s.pad_trigger[t] = gp.trigger[t];
	}
	if (keyboard)
	{
		const io::KeyboardState	&kb = io::getCurrentKeyboardState ();

		s.kb_modifier = kb.modifier;
		memcpy (s.kb_keys, kb.keycode, sizeof(s.kb_keys));
	}

	io::MouseState	&m = io::getCurrentMouseState ();
	if (m.connected)
		s.mouse_buttons = m.buttons;
	s.mouse_wheel = m.wheel;	// written by the HID callbacks, i.e. in this interrupt
	m.wheel = 0;

	s.pad_buttons |= q2_nespad_buttons ();

	q2_input_sample (&s);
}

static bool usb_tick (repeating_timer_t *)
{
	tuh_task ();
	usb_sample ();
	return true;
}


void q2_usb_mouse_motion (int *dx, int *dy)
{
	io::MouseState	&m = io::getCurrentMouseState ();
	uint32_t		irq = save_and_disable_interrupts ();

	*dx = m.dx;
	*dy = m.dy;
	m.dx = m.dy = 0;
	restore_interrupts (irq);
}

void q2_usb_look (int *x, int *y)
{
	int		best = 0;

	*x = *y = 0;
	for (int i = 0; i < 2; i++)
	{
		const io::GamePadState	&gp = io::getCurrentGamePadState (i);
		int						lx = gp.look[0], ly = gp.look[1];
		int						mag = (lx < 0 ? -lx : lx) + (ly < 0 ? -ly : ly);

		if (slot_in_use (gp) && !slot_is_keyboard (gp) && mag > best)
		{
			best = mag;
			*x = lx;
			*y = ly;
		}
	}
}

void q2_usb_init (void)
{
#ifdef PIN_USB_HOST_VBUS
	gpio_init (PIN_USB_HOST_VBUS);
	gpio_set_dir (PIN_USB_HOST_VBUS, GPIO_OUT);
	gpio_put (PIN_USB_HOST_VBUS, 1);
#endif

	pio_usb_configuration_t	pio_cfg = PIO_USB_DEFAULT_CONFIG;
	static_assert (PIN_USB_HOST_DP + 1 == PIN_USB_HOST_DM, "D+/D- must be adjacent");
	pio_cfg.pinout = PIO_USB_PINOUT_DPDM;
	pio_cfg.pin_dp = PIN_USB_HOST_DP;
	// As pico_shared does: PIO2 for both directions (PIO0/1 stay free for the
	// audio and SD drivers) and a free DMA channel
	pio_cfg.pio_tx_num = 2;
	pio_cfg.pio_rx_num = 2;
	pio_cfg.tx_ch = dma_claim_unused_channel (true);
	dma_channel_unclaim (pio_cfg.tx_ch);	// pio_usb_bus_init claims it again
	pio_cfg.skip_alarm_pool = true;	// frames come from q2_usb_frame.c
	tuh_configure (CFG_TUH_RPI_PIO_USB, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

	const tusb_rhport_init_t	host_init = {
		.role = TUSB_ROLE_HOST,
		.speed = TUSB_SPEED_AUTO,
	};
	tusb_init (CFG_TUH_RPI_PIO_USB, &host_init);
	q2_usb_frame_start ();

	// Devices enumerate in the background once the timer runs.
	tick_pool = alarm_pool_create (USB_TICK_ALARM, 1);
	irq_set_priority (hardware_alarm_get_irq_num (USB_TICK_ALARM), PICO_LOWEST_IRQ_PRIORITY);
	alarm_pool_add_repeating_timer_us (tick_pool, -USB_TICK_US, usb_tick, NULL, &tick_timer);
	printf ("q2_usb: PIO-USB host on GPIO %d/%d\n", PIN_USB_HOST_DP, PIN_USB_HOST_DM);
}
