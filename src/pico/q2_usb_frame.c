// q2_usb_frame.c -- the 1 ms USB frame for Pico-PIO-USB, and SOFs during flash writes.
//
// PIO-USB is set up with skip_alarm_pool (q2_usb.cpp), and its frame (SOF and
// every transaction, pio_usb_host_frame) is driven from hardware alarm 2 at the
// highest priority, by a handler that is in SRAM and installed directly in the
// vector table rather than going through the SDK's alarm pool, whose interrupt
// handler runs from flash.
//
// Flash erases and programs (q2_flash.c) take tens to hundreds of milliseconds
// with XIP off. Without SOFs for 3 ms the Fruit Jam's hub and the devices behind
// it suspend, and a DualShock 4 did not come back from that: its light went out
// and every later transfer failed. So around a flash write every interrupt on
// core0 is masked except the frame one, which then sends only a bare SOF (or a
// low-speed keep-alive) from SRAM. PIO-USB's full frame calls into flash and
// waits until the write is done.
//
// Boards with the native USB controller (no HAS_USBPIO) never call
// q2_usb_frame_start: the controller sends SOFs by itself, also during a flash
// write, so q2_usb_flash_begin only switches interrupts off there.
//
// RP2350-E9 (chips up to A2, such as the Fruit Jam's): a pad with its input
// enabled keeps a line that was high at about 2.2 V, and the pull-down cannot
// bring it back. Pico-PIO-USB switches the inputs off for 8 NOPs before it reads
// the line state (21 ns at 384 MHz), too short to drain a line, and reads right
// after switching them back on. On the Fruit Jam a mouse then often stayed
// unseen: D- was latched, so D+ and D- both read high (SE1), never an idle state.
// So while nothing is attached, the inputs stay off between frames (the lines
// drain through the pull-downs) and the line state is read here instead, with the
// inputs settled. While a device is attached, the lines are drained for a few
// microseconds every 16 frames, so that PIO-USB sees an unplug as SE0.
//
// A connect is taken only after the same J state has held for 100 frames (the
// USB attach debounce), and PIO-USB's own one-sample check is kept out of it. A
// cold start with a low-speed mouse behind the Fruit Jam's hub otherwise now and
// then took the full-speed hub for a low-speed device: one sample with D- high
// was enough, and from then on the host's own low-speed signalling kept D- high
// and latched, so no device worked until a reset.

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/structs/nvic.h"
#include "hardware/structs/pads_bank0.h"

#include "pio_usb.h"
#include "pio_usb_ll.h"
#include "usb_crc.h"
#include "usb_definitions.h"

#include "q2_input.h"

#define SOF_ALARM	2		// hardware alarm for the 1 ms USB frame
#define E9_DRAIN_FRAMES	16	// frames between drains while a device is attached
#define E9_DRAIN_US	5
#define E9_ATTACH_FRAMES	100	// frames a J state must hold before a connect counts

static uint32_t			sof_next;
static volatile bool	flash_busy;
static bool				sof_started;
static bool				e9;
static uint32_t			e9_frames;
static uint8_t			e9_line;		// J state last seen: 0 none, 1 low-speed, 2 full-speed
static uint32_t			e9_stable;		// frames it has held
static uint16_t			bare_frame;
static uint8_t			bare_sof[4] = { USB_SYNC, USB_PID_SOF, 0, 0 };
static uint8_t			bare_sof_encoded[4 * 2 * 7 / 6 + 2];
static uint8_t			bare_keepalive[1];

static void __not_in_flash_func(wait_us) (uint32_t us)
{
	uint32_t	start = timer_hw->timerawl;

	while (timer_hw->timerawl - start <= us)
		;
}

static void __not_in_flash_func(set_inputs) (const root_port_t *root, bool on)
{
	io_rw_32	*dp = &pads_bank0_hw->io[root->pin_dp];
	io_rw_32	*dm = &pads_bank0_hw->io[root->pin_dm];

	if (on)
	{
		hw_set_bits (dp, PADS_BANK0_GPIO0_IE_BITS);
		hw_set_bits (dm, PADS_BANK0_GPIO0_IE_BITS);
	}
	else
	{
		hw_clear_bits (dp, PADS_BANK0_GPIO0_IE_BITS);
		hw_clear_bits (dm, PADS_BANK0_GPIO0_IE_BITS);
	}
	(void)*dp;		// read back: the writes have reached the pads
	(void)*dm;
}

// Before pio_usb_host_frame: drain the lines of an attached device now and then.
static void __not_in_flash_func(e9_drain) (void)
{
	root_port_t	*root = PIO_USB_ROOT_PORT (0);

	if (!(root->initialized && root->connected && !root->suspended) || ++e9_frames % E9_DRAIN_FRAMES)
		return;
	set_inputs (root, false);
	wait_us (E9_DRAIN_US);
	set_inputs (root, true);
}

// After pio_usb_host_frame: while nothing is attached, look for a J state (the
// device's pull-up on D+ or D-) and leave the inputs off until the next frame.
// The device counts as attached once the same J state has held for
// E9_ATTACH_FRAMES frames.
static void __not_in_flash_func(e9_connect) (void)
{
	root_port_t	*root = PIO_USB_ROOT_PORT (0);
	bool		dp, dm;
	uint8_t		line;

	if (!root->initialized || root->connected)
	{
		e9_line = 0;
		e9_stable = 0;
		return;
	}
	set_inputs (root, true);
	wait_us (1);
	dp = !gpio_get (root->pin_dp);	// pio_usb_host_add_port inverts the inputs
	dm = !gpio_get (root->pin_dm);
	line = dp == dm ? 0 : dp ? 2 : 1;
	if (line != e9_line)
	{
		e9_line = line;
		e9_stable = 0;
	}
	if (line && ++e9_stable >= E9_ATTACH_FRAMES)
	{
		root->is_fullspeed = dp;
		root->connected = true;
		root->suspended = true;		// a bus reset comes first
		root->ints |= PIO_USB_INTS_CONNECT_BITS;
		return;
	}
	set_inputs (root, false);
}

// The frame on an E9 chip. While nothing is attached, pio_usb_host_frame only
// looks for a connect, from a single line-state sample taken right after it
// switches the inputs on; the port is hidden from that check, so that
// e9_connect alone decides. Its interrupt dispatch does not look at
// initialized, and the frame interrupt cannot be preempted by anything that
// reads the port.
static void __not_in_flash_func(e9_frame) (void)
{
	root_port_t	*root = PIO_USB_ROOT_PORT (0);
	bool		hide = root->initialized && !root->connected;

	e9_drain ();
	if (hide)
		root->initialized = false;
	pio_usb_host_frame ();
	if (hide)
		root->initialized = true;
	e9_connect ();
}

static void __not_in_flash_func(send_bare_sof) (void)
{
	root_port_t	*root = PIO_USB_ROOT_PORT (0);
	pio_port_t	*pp = PIO_USB_PIO_PORT (0);
	uint8_t		len;

	if (!(root->initialized && root->connected && !root->suspended))
		return;
	if (!root->is_fullspeed)
	{
		pio_usb_bus_usb_transfer (pp, bare_keepalive, 1);
		return;
	}
	bare_frame = (bare_frame + 1) & 0x7ff;
	bare_sof[2] = bare_frame & 0xff;
	bare_sof[3] = (uint8_t)((calc_usb_crc5 (bare_frame) << 3) | (bare_frame >> 8));
	len = pio_usb_ll_encode_tx_data (bare_sof, sizeof(bare_sof), bare_sof_encoded);
	pio_usb_bus_usb_transfer (pp, bare_sof_encoded, len);
}

static void __not_in_flash_func(sof_isr) (void)
{
	uint32_t	now;

	timer_hw->intr = 1u << SOF_ALARM;
	sof_next += 1000;
	now = timer_hw->timerawl;
	if ((int32_t)(sof_next - now) < 20)		// fell behind: resynchronise
		sof_next = now + 1000;
	timer_hw->alarm[SOF_ALARM] = sof_next;
	if (flash_busy)
		send_bare_sof ();
	else if (e9)
		e9_frame ();
	else
		pio_usb_host_frame ();
}

void q2_usb_frame_start (void)
{
	unsigned	irq = hardware_alarm_get_irq_num (SOF_ALARM);

	pio_usb_ll_encode_tx_data (NULL, 0, bare_keepalive);
	e9 = rp2350_chip_version () <= 2;
	hardware_alarm_claim (SOF_ALARM);
	irq_set_exclusive_handler (irq, sof_isr);
	irq_set_priority (irq, PICO_HIGHEST_IRQ_PRIORITY);
	hw_set_bits (&timer_hw->inte, 1u << SOF_ALARM);
	irq_set_enabled (irq, true);
	sof_next = timer_hw->timerawl + 1000;
	timer_hw->alarm[SOF_ALARM] = sof_next;
	sof_started = true;
}

q2_irq_state_t q2_usb_flash_begin (void)
{
	q2_irq_state_t	st;

	if (!sof_started)
	{
		st.masked = false;
		st.primask = save_and_disable_interrupts ();
		return st;
	}
	// every interrupt but the frame one: their handlers may run from flash
	st.masked = true;
	st.iser[0] = nvic_hw->iser[0];
	st.iser[1] = nvic_hw->iser[1];
	bare_frame = (uint16_t)pio_usb_host_get_frame_number ();
	nvic_hw->icer[0] = st.iser[0] & ~(1u << hardware_alarm_get_irq_num (SOF_ALARM));
	nvic_hw->icer[1] = st.iser[1];
	__dsb ();
	__isb ();
	flash_busy = true;
	return st;
}

void q2_usb_flash_end (q2_irq_state_t st)
{
	if (!st.masked)
	{
		restore_interrupts (st.primask);
		return;
	}
	flash_busy = false;
	__dsb ();
	nvic_hw->iser[0] = st.iser[0];
	nvic_hw->iser[1] = st.iser[1];
}
