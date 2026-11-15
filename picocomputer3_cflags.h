#ifndef Q2_PICOCOMPUTER3_CFLAGS_H
#define Q2_PICOCOMPUTER3_CFLAGS_H 1
//
// Board configuration for pico-quake2 on pico_shared HW_CONFIG 17: the Pico
// Computer 3 (PC3, https://github.com/UKTailwind/Pico-Computer-3), an RP2350B
// with 16 MB flash and 8 MB PSRAM, HSTX video on its HDMI connector, a PCM5102
// I2S DAC on the audio jack, a microSD slot on SPI1, a four-port USB hub on the
// RP2350's own USB controller and an I/O header with two NES/SNES controller
// ports.
//
// Force-included into every translation unit (see CMakeLists.txt). Pin values
// mirror pico_shared BoardConfigs.cmake HW_CONFIG 17. The build uses
// PICO_BOARD=weact_studio_rp2350b_core, as pico_shared does for this config; the
// pico-sdk ships that header, and it declares the RP2350B the SD pins need.
//
// Differences from the Fruit Jam that the code branches on:
//
//   PICO_DEFAULT_UART 1  the console is on UART1 (GPIO 8/9). The board header's
//                        default, UART0 on GPIO 12/13, would take HSTX pins.
//   Q2_AUDIO_I2S_DRIVER  PCM5102: no codec and no headphone detect, so
//                        q2_audio.c plays to HDMI and I2S at the same time.
//   SDCARD_SPI spi1      the SD card is on SPI1 (GPIO 28/30/31/33).
//   NES_PIN_*            the two controller ports on the I/O header, polled by
//                        q2_nespad.cpp. Both share CLK and LAT, which HW_CONFIG 17
//                        selects in nespad.cpp.
//   no HAS_USBPIO        the USB host is the RP2350's own controller, behind the
//                        board's USB hub, as pico_shared builds this config. At
//                        384 MHz its 48 MHz is divided from PLL_SYS, and clk_hstx
//                        runs from PLL_USB (q2_boot.c).
//

#define HW_CONFIG 17		// also selects nespad.cpp's PIO program (shared CLK/LAT)
#define Q2_BOARD_NAME "Pico Computer 3"

// --- UART (debug console): UART1 on GPIO 8/9 ----------------------------------
#define PICO_DEFAULT_UART 1
#define PICO_DEFAULT_UART_TX_PIN 8
#define PICO_DEFAULT_UART_RX_PIN 9

// --- HSTX / HDMI video lanes (read by pico_hdmi/video_output.c) -------------
// Same lanes as the Fruit Jam, also inverted (D- = D+ - 1).
#define HSTX_CKP 13
#define HSTX_D0P 15
#define HSTX_D1P 17
#define HSTX_D2P 19
#define GPIOHSTXCK HSTX_CKP
#define GPIOHSTXD0 HSTX_D0P
#define GPIOHSTXD1 HSTX_D1P
#define GPIOHSTXD2 HSTX_D2P
#define GPIOHSTXINVERTED 1
#define USE_HSTX 1

// --- I2S audio: PCM5102 DAC (the audio jack) ---------------------------------
// BCK on GPIO 10, LRCK on GPIO 11, DIN on GPIO 22.
#define Q2_AUDIO_I2S_DRIVER PICO_AUDIO_I2S_DRIVER_PCM5000A
#define PICO_AUDIO_I2S_DATA_PIN 22
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 10
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
#define I2S_AUDIO_COMPENSATE_DC_OFFSET 1	// PCM510x: DC blocker in audio_i2s.c
#define I2S_AUDIO_RING_SIZE 4096   // samples (16 KB); q2_audio.c keeps half of it queued
// No TLV320, and Quake II does not read a Wii pad. tlv320dac3100.c is still
// compiled and uses WIIPAD_I2C as an i2c_inst_t *, so it must name a valid
// instance: i2c0 is the board's system I2C on GPIO 20/21.
#define WIIPAD_I2C i2c0
#define WII_PIN_SDA -1
#define WII_PIN_SCL -1

// --- SD card (hardware SPI1; PIO-SPI fallback available) --------------------
// GPIO 30 = SPI1 SCK, 31 = SPI1 TX, 28 = SPI1 RX; CS 33 is a plain GPIO. GPIO
// 30/31 are only in the vendored pico_fatfs SPI1 table on RP2350B.
#define SD_TX 31   // MOSI
#define SD_RX 28   // MISO
#define SD_SCK 30
#define SD_CS 33
#define USE_SD 1
#define SDCARD_SPI spi1
#define SDCARD_PIO pio1

// --- PSRAM (8 MB on QMI CS1 = GPIO 47) --------------------------------------
#define PSRAM_CS_PIN 47

// --- LED: blinks while a level's assets are copied into flash (q2_flash.c) ---
// The HEARTBEAT LED, with jumper H5 on 1-2.
#define Q2_LED_PIN 25

// --- NES/SNES controller ports (pico_shared nespad, PIO) ---------------------
// On the I/O header. Both ports share CLK (GPIO 4) and LAT (GPIO 5); only DATA
// differs, so nespad.cpp runs two lockstep state machines released by the same
// PIO IRQ (the HW_CONFIG 12/13/17 program). No SNES Mouse on this board: that
// program does not have the timing the mouse needs.
#define NES_PIN_CLK 4
#define NES_PIN_DATA 6
#define NES_PIN_LAT 5
#define NES_PIO pio1
#define NES_PIN_CLK_1 4
#define NES_PIN_DATA_1 7
#define NES_PIN_LAT_1 5
#define NES_PIO_1 pio1

// --- USB host: the native controller (no HAS_USBPIO, see above) ---------------
// The four USB-A ports are behind the board's hub. tusb_config.h allows one hub
// (CFG_TUH_HUB 1) and three devices on it.

#endif // Q2_PICOCOMPUTER3_CFLAGS_H
