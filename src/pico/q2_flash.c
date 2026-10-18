// q2_flash.c -- flash access for the asset cache (qcommon/fcache.c).
//
// The cache region is read through the cached XIP window and written with the SDK's
// flash_range_erase/flash_range_program, which take the QMI out of XIP mode for the
// duration. While they run nothing may execute from or read flash or PSRAM:
//
//   - this code runs from SRAM, and every interrupt on core0 is masked except
//     the USB frame interrupt, whose handler is in SRAM and keeps the attached
//     USB devices from suspending (q2_usb_flash_begin);
//   - core1 runs the HSTX scanout, whose code and data are in SRAM, and the audio
//     pump, which is held off for the duration (q2_audio_hold);
//   - the bootrom resets QMI M0 (flash) timing, which is restored afterwards (the
//     SDK saves and restores the CS1/PSRAM setup itself, and cleans the XIP cache
//     first so no PSRAM write-back is lost);
//   - the data to program is copied into an SRAM buffer first, because the source
//     may be in PSRAM, which is unreachable while the QMI is in direct mode.
//
// Erases are split into 64 KB steps so interrupts are not held off for long.
//
// The build claims 16 MB of flash (cmake/BootPartition.cmake), whatever the board
// header says, so the region is cut here to the chip's real size: past its end
// the writes would wrap round onto the firmware.
//
// The board LED (Q2_LED_PIN) blinks while the cache is written, which is the slow
// first load of a level, so the board is visibly busy while the screen stands still.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/qmi.h"

#include "qcommon/fcache.h"
#include "q2_flash.h"
#include "q2_audio.h"
#include "q2_input.h"

#define XIP_BASE_ADDR	0x10000000u
#define CS0_WINDOW		(16u * 1024 * 1024)	// what XIP maps of the chip
#define ERASE_STEP		(64 * 1024)
#define PROGRAM_STEP	4096
#define MIN_REGION		(1024 * 1024)		// smaller than this: no cache

// Q2_FLASH_DIAG=1: log every erase and program with its duration on the UART
#ifndef Q2_FLASH_DIAG
#define Q2_FLASH_DIAG	0
#endif

static fc_flash_t	cache_flash;
static uint8_t		__scratch_x("q2_flash_bounce") bounce[PROGRAM_STEP] __attribute__((aligned(4)));	// 4 KB SCRATCH_X bank
static uint32_t		jedec_id;		// 0xMMTTCC: manufacturer, type, log2 of the capacity

static uint32_t flash_offset (unsigned offset)
{
	return (Q2_FCACHE_BASE - XIP_BASE_ADDR) + offset;
}

/*
==============================================================================

Activity LED

==============================================================================
*/

#define LED_HALF_PERIOD_US	125000		// 4 Hz

#ifdef Q2_LED_PIN
static bool		led_on;
static uint32_t	led_toggled, led_last_write;
#endif

static void led_init (void)
{
#ifdef Q2_LED_PIN
	gpio_init (Q2_LED_PIN);
	gpio_set_dir (Q2_LED_PIN, GPIO_OUT);
	gpio_put (Q2_LED_PIN, 0);
#endif
}

// Called for every erase and program step
static void led_activity (void)
{
#ifdef Q2_LED_PIN
	uint32_t	now = time_us_32 ();

	led_last_write = now;
	if (now - led_toggled >= LED_HALF_PERIOD_US)
	{
		led_on = !led_on;
		gpio_put (Q2_LED_PIN, led_on);
		led_toggled = now;
	}
#endif
}

void q2_flash_idle (void)
{
#ifdef Q2_LED_PIN
	if (led_on && time_us_32 () - led_last_write > 2 * LED_HALF_PERIOD_US)
	{
		led_on = false;
		gpio_put (Q2_LED_PIN, 0);
	}
#endif
}

/*
==============================================================================

Chip size, from pico_shared FrensHelpers.cpp (storage_get_flash_capacity,
storage_get_flash_jedec_id, storage_get_flash_manufacturer_name).

==============================================================================
*/

// JEDEC 0x9F read. Runs before the overclock and before the PSRAM is set up, as
// pico_shared does. flash_do_cmd re-enters XIP through the bootrom, which resets
// the QMI M0 setup, so that is put back as in erase_safe.
void __no_inline_not_in_flash_func(q2_flash_probe) (void)
{
	uint8_t		tx[4] = { 0x9f };
	uint8_t		rx[4] = { 0 };
	uint32_t	irq = save_and_disable_interrupts ();
	uint32_t	timing = qmi_hw->m[0].timing, rfmt = qmi_hw->m[0].rfmt, rcmd = qmi_hw->m[0].rcmd;

	flash_do_cmd (tx, rx, sizeof(tx));
	qmi_hw->m[0].rfmt = rfmt;
	qmi_hw->m[0].rcmd = rcmd;
	qmi_hw->m[0].timing = timing;
	__compiler_memory_barrier ();
	restore_interrupts (irq);
	// rx[0] is clocked in while the command byte goes out; the ID follows
	jedec_id = ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
}

// Bytes XIP can reach on the chip; 0 when the ID makes no sense.
static uint32_t flash_capacity (void)
{
	uint32_t	exp = jedec_id & 0xff;

	if (exp < 16 || exp >= 32)
		return 0;
	return (1u << exp) < CS0_WINDOW ? (1u << exp) : CS0_WINDOW;
}

// JEDEC manufacturer IDs seen on Picos and Pico-compatible boards
static const char *flash_maker (uint8_t id)
{
	switch (id)
	{
	case 0xef:	return "Winbond";
	case 0xc8:	return "GigaDevice";
	case 0x5e:	return "Zbit";
	case 0x0b:	return "XTX";
	case 0x68:	return "Boya";
	case 0x85:	return "Puya";
	case 0xc2:	return "Macronix";
	case 0x1f:	return "Adesto/Atmel";
	case 0x20:	return "Micron";
	default:	return "unknown";
	}
}

static void __no_inline_not_in_flash_func(erase_safe) (uint32_t ofs, uint32_t count)
{
	q2_audio_hold ();
	q2_irq_state_t	irq = q2_usb_flash_begin ();
	uint32_t		timing = qmi_hw->m[0].timing, rfmt = qmi_hw->m[0].rfmt, rcmd = qmi_hw->m[0].rcmd;

	flash_range_erase (ofs, count);
	qmi_hw->m[0].rfmt = rfmt;
	qmi_hw->m[0].rcmd = rcmd;
	qmi_hw->m[0].timing = timing;
	__compiler_memory_barrier ();
	q2_usb_flash_end (irq);
	q2_audio_release ();
}

static void __no_inline_not_in_flash_func(program_safe) (uint32_t ofs, const uint8_t *data, uint32_t count)
{
	q2_audio_hold ();
	q2_irq_state_t	irq = q2_usb_flash_begin ();
	uint32_t		timing = qmi_hw->m[0].timing, rfmt = qmi_hw->m[0].rfmt, rcmd = qmi_hw->m[0].rcmd;

	flash_range_program (ofs, data, count);
	qmi_hw->m[0].rfmt = rfmt;
	qmi_hw->m[0].rcmd = rcmd;
	qmi_hw->m[0].timing = timing;
	__compiler_memory_barrier ();
	q2_usb_flash_end (irq);
	q2_audio_release ();
}

static void cache_erase (unsigned offset, unsigned len)
{
	while (len)
	{
		// 64 KB at a time where aligned (the bootrom then uses block erase)
		unsigned	step = (offset % ERASE_STEP == 0 && len >= ERASE_STEP) ? ERASE_STEP : FC_SECTOR;

		led_activity ();
#if Q2_FLASH_DIAG
		printf ("fl: erase %06x %u\n", offset, step);
		uint32_t	t0 = time_us_32 ();
#endif
		erase_safe (flash_offset (offset), step);
#if Q2_FLASH_DIAG
		printf ("fl: ok %lu us\n", (unsigned long)(time_us_32 () - t0));
#endif
		offset += step;
		len -= step;
	}
}

static void cache_program (unsigned offset, const void *data, unsigned len)
{
	const uint8_t	*src = data;

	while (len)
	{
		unsigned	step = len < PROGRAM_STEP ? len : PROGRAM_STEP;

		memcpy (bounce, src, step);
		led_activity ();
#if Q2_FLASH_DIAG
		printf ("fl: program %06x %u\n", offset, step);
		uint32_t	t0 = time_us_32 ();
#endif
		program_safe (flash_offset (offset), bounce, step);
#if Q2_FLASH_DIAG
		printf ("fl: ok %lu us\n", (unsigned long)(time_us_32 () - t0));
#endif
		offset += step;
		src += step;
		len -= step;
	}
}

void q2_flash_init (void)
{
	uint32_t	capacity = flash_capacity ();
	uint32_t	start = Q2_FCACHE_BASE - XIP_BASE_ADDR;
	uint32_t	size = capacity > start ? capacity - start : 0;

	led_init ();
	if (size > Q2_FCACHE_SIZE)
		size = Q2_FCACHE_SIZE;
	size &= ~(uint32_t)(ERASE_STEP - 1);
	printf ("q2_flash: %s flash, JEDEC id %06lx, %lu KB\n", flash_maker (jedec_id >> 16),
		(unsigned long)jedec_id, (unsigned long)(capacity / 1024));
	if (size < MIN_REGION)
	{
		printf ("q2_flash: no room for the asset cache above %08lx; assets stay in PSRAM\n",
			(unsigned long)Q2_FCACHE_BASE);
		FC_SetFlash (NULL);
		return;
	}
	if (size < Q2_FCACHE_SIZE)
		printf ("q2_flash: asset cache cut to %lu KB by the chip size\n", (unsigned long)(size / 1024));

	cache_flash.base = (unsigned char *)Q2_FCACHE_BASE;
	cache_flash.size = size;
	cache_flash.erase = cache_erase;
	cache_flash.program = cache_program;
	FC_SetFlash (&cache_flash);
}
