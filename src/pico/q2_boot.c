// q2_boot.c -- RP2350 entry point for pico-quake2.
//
// Brings up the hardware (378 MHz, 126 MHz clk_hstx, PSRAM, HSTX, SD, the flash
// asset cache), then runs the engine's main loop. The clock sequence and the
// hardfault breadcrumb are taken from pico-duke3D's duke_boot.c.

#include <malloc.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "hardware/structs/qmi.h"

#include "quake2.h"
#include "q_mem.h"
#include "qcommon/fcache.h"

#include "q2_console.h"
#include "q2_display.h"
#include "q2_fatal.h"
#include "q2_fatfs_io.h"
#include "q2_flash.h"
#include "q2_input.h"
#include "q2_psram.h"

/*
==============================================================================

Hardfault breadcrumb: raw UART writes (no stdio, no locks) of the exception frame
and the fault status registers. The naked trampoline passes the frame pointer in r0;
reading the stack pointer from C would get the frame wrong.

==============================================================================
*/

static void hf_putc (char c)
{
	uart_putc_raw (uart0, c);
}

static void hf_puthex (uint32_t v)
{
	for (int i = 28; i >= 0; i -= 4)
	{
		int	d = (v >> i) & 0xf;
		hf_putc (d < 10 ? '0' + d : 'a' + d - 10);
	}
}

static void hf_field (const char *name, uint32_t v)
{
	for (; *name; name++)
		hf_putc (*name);
	hf_puthex (v);
}

void hf_report (uint32_t *sp)
{
	hf_field ("\n!HF pc=", sp[6]);
	hf_field (" lr=", sp[5]);
	hf_field (" psr=", sp[7]);
	hf_field (" sp=", (uint32_t)sp);
	hf_field ("\n!HF cfsr=", *(volatile uint32_t *)0xE000ED28u);
	hf_field (" hfsr=", *(volatile uint32_t *)0xE000ED2Cu);
	hf_field (" mmfar=", *(volatile uint32_t *)0xE000ED34u);
	hf_field (" bfar=", *(volatile uint32_t *)0xE000ED38u);
	hf_putc ('\n');
	while (1)
		__asm volatile ("nop");
}

__attribute__((naked)) void isr_hardfault (void)
{
	__asm volatile (
		"tst lr, #4\n"
		"ite eq\n"
		"mrseq r0, msp\n"
		"mrsne r0, psp\n"
		"b hf_report\n"
	);
}

/*
==============================================================================

Clocks

==============================================================================
*/

// Q2_CPU_KHZ must be a multiple of 126000 (126 MHz is clk_hstx: 25.2 MHz pixel clock):
// 378000 (default) or 504000. Q2_VREG is the core voltage (VREG_VOLTAGE_*).
#ifndef Q2_CPU_KHZ
#define Q2_CPU_KHZ	378000
#endif
#ifndef Q2_VREG
#define Q2_VREG		VREG_VOLTAGE_1_50
#endif
#if Q2_CPU_KHZ % 126000
#error Q2_CPU_KHZ must be a multiple of 126000
#endif

static void setup_clocks (void)
{
	// Flash clock at most 94.5 MHz (what 378 MHz / 4 has proven), with the read
	// sample delay scaled to stay at about the same time in ns; set before clk_sys
	// is raised. Same formula as pico_shared: 0x60007304 at 378 MHz, /6 at 504 MHz.
	const uint32_t	flash_div = (Q2_CPU_KHZ + 94499) / 94500 < 4 ? 4 : (Q2_CPU_KHZ + 94499) / 94500;
	const uint32_t	rxdelay = (3 * Q2_CPU_KHZ + 189000) / 378000;

	vreg_disable_voltage_limit ();
	vreg_set_voltage (Q2_VREG);
	qmi_hw->m[0].timing = 0x60000000u | (14u << 11) | ((rxdelay > 7 ? 7 : rxdelay) << 8) | flash_div;
	sleep_ms (100);
	set_sys_clock_khz (Q2_CPU_KHZ, true);
	sleep_ms (100);

#ifdef HAS_USBPIO
	// clk_hstx from a retasked PLL_USB (756 / 6 = 126 MHz), which keeps PLL_SYS
	// jitter off the TMDS clock. Only possible when the native USB controller is
	// unused (PIO-USB boards).
	pll_deinit (pll_usb);
	pll_init (pll_usb, 1, 756000000, 6, 1);
	clock_configure (clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
		126000000u, 126000000u);
#else
	clock_configure (clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
		Q2_CPU_KHZ * 1000u, 126000000u);
#endif
	// set_sys_clock_khz left clk_peri on PLL_USB; put it on PLL_SYS (halved above
	// 400 MHz; the SD card SPI and the UART derive their clocks from it)
	clock_configure (clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
		Q2_CPU_KHZ * 1000u, Q2_CPU_KHZ > 400000 ? Q2_CPU_KHZ * 500u : Q2_CPU_KHZ * 1000u);
}

/*
==============================================================================

Exit: the engine leaves through _Exit (Sys_Quit with 0, Sys_Error with 1), which
ends here. Quitting resets the board (back to the pico-bootLoader picker on a
bootloader build); an error stays on screen.

==============================================================================
*/

void __attribute__((noreturn)) _exit (int status)
{
	if (status == 0)
	{
		printf ("\nquake2: quit, resetting\n");
		stdio_flush ();
		watchdog_reboot (0, 0, 1);
		while (1)
			tight_loop_contents ();
	}
	q2_fatal_halt ();
}

/*
==============================================================================

Status line on the UART every 5 seconds: frame rate, engine heap, flash cache.

==============================================================================
*/

// core0's stack (cmake/psram_linker.cmake) is painted at boot, so the status
// line can report how deep it has been used, interrupts included (the USB
// timer runs on the same stack).
#define STACK_PAINT	0x5a5a5a5au

static void __attribute__((noinline)) paint_stack (void)
{
	extern uint32_t	__StackLimit[];
	uint32_t		*p = __StackLimit;
	uint32_t		*sp = (uint32_t *)__builtin_frame_address (0) - 64;

	while (p < sp)
		*p++ = STACK_PAINT;
}

static unsigned stack_unused (void)
{
	extern uint32_t	__StackLimit[], __StackTop[];
	const uint32_t	*p = __StackLimit;

	while (p < __StackTop && *p == STACK_PAINT)
		p++;
	return (unsigned)((p - __StackLimit) * 4);
}

static void report_status (int now, int *frames)
{
	static int	last;
	qmem_stats_t	m;
	fc_stats_t		fc;

	(*frames)++;
	if (now - last < 5000)
		return;
	extern char __HeapLimit[], end[];
	struct mallinfo	mi = mallinfo ();

	Qmem_GetStats (&m);
	FC_GetStats (&fc);
	extern unsigned	hid_failed_reports;
	extern int		r_surfbuild_texels[4];	// ref_soft/r_surf.c: miss, dlight, style, anim

	printf ("status: %d.%d fps, heap %u/%u KB (peak %u), sram heap %u/%u KB, cache %u assets %u KB, "
		"%u hits %u stored, %u KB written, stack %u B unused, usb %u failed\n",
		*frames * 1000 / (now - last), (*frames * 10000 / (now - last)) % 10,
		(unsigned)(m.used / 1024), (unsigned)(m.size / 1024), (unsigned)(m.peak / 1024),
		(unsigned)(mi.uordblks / 1024), (unsigned)((__HeapLimit - end) / 1024),
		fc.entries, fc.used_bytes / 1024, fc.hits, fc.stores, fc.bytes_programmed / 1024,
		stack_unused (), hid_failed_reports);
	printf ("surf KB/s: miss %d, dlight %d, style %d, anim %d\n",
		(int)((long long)r_surfbuild_texels[0] * 1000 / (now - last) / 1024),
		(int)((long long)r_surfbuild_texels[1] * 1000 / (now - last) / 1024),
		(int)((long long)r_surfbuild_texels[2] * 1000 / (now - last) / 1024),
		(int)((long long)r_surfbuild_texels[3] * 1000 / (now - last) / 1024));
	for (int i = 0; i < 4; i++)
		r_surfbuild_texels[i] = 0;
	*frames = 0;
	last = now;
}

/*
==============================================================================

main

==============================================================================
*/

// Holds the board's peripheral reset line (if it has one) released, as
// pico-duke3D does. (A low pulse here, as CircuitPython does it, left the SD
// card not ready for the mount that follows.)
static void release_peripheral_reset (void)
{
#ifdef Q2_PERIPH_RESET_PIN
	gpio_put (Q2_PERIPH_RESET_PIN, 1);
	gpio_set_dir (Q2_PERIPH_RESET_PIN, GPIO_OUT);
	gpio_set_function (Q2_PERIPH_RESET_PIN, GPIO_FUNC_SIO);
#endif
}

int main (void)
{
	static char	*argv[] = { "quake2", "+set", "basedir", Q2_ASSET_DIR, NULL };
	size_t		heap_size;
	void		*heap;
	int			oldtime, newtime, frames = 0;

	paint_stack ();
	release_peripheral_reset ();
	q2_flash_probe ();		// before the overclock and the PSRAM
	setup_clocks ();
	stdio_init_all ();
	q2_console_attach_stdio ();
	printf ("pico-quake2 on %s\n", Q2_BOARD_NAME);
	printf ("clk_sys %lu MHz, clk_hstx %lu MHz, clk_peri %lu MHz, flash /%lu\n",
		(unsigned long)(clock_get_hz (clk_sys) / 1000000),
		(unsigned long)(clock_get_hz (clk_hstx) / 1000000),
		(unsigned long)(clock_get_hz (clk_peri) / 1000000),
		(unsigned long)(qmi_hw->m[0].timing & 0xff));

	// PSRAM before the display: SetupPsram stalls XIP while it runs
	q2_psram_init ();
	q2_display_init ();
	if (q2_psram_error ())
		q2_fatal ("%s", q2_psram_error ());

	heap = q2_psram_heap (&heap_size);
	Qmem_Init (heap, heap_size);

	q2_fatfs_init ();
	q2_flash_init ();

	Quake2_Init (4, argv);
	q2_nespad_init ();	// before the USB timer, which polls the controller ports
	q2_usb_init ();		// after Quake2_Init, which resets the key queue the USB timer fills

	oldtime = Quake2_Milliseconds ();
	for (;;)
	{
		int	msec;

		do
		{
			newtime = Quake2_Milliseconds ();
			msec = newtime - oldtime;
		} while (msec < 1);
		Quake2_Frame (msec);
		q2_flash_idle ();
		oldtime = newtime;
		report_status (newtime, &frames);
	}
}
