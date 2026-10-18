#ifndef Q2_ADAFRUITDVISD_CFLAGS_H
#define Q2_ADAFRUITDVISD_CFLAGS_H 1
//
// Board configuration for pico-quake2 on pico_shared HW_CONFIG 2: a Pimoroni Pico
// Plus 2 on a breadboard or Frank's PCB, with the Adafruit DVI Breakout (product
// 4984) on HSTX, the Adafruit MicroSD breakout (product 254) on SPI0, an optional
// PCM5100A I2S DAC and two NES/SNES controller ports.
//
// Force-included into every translation unit (see CMakeLists.txt). Pin values
// mirror pico_shared BoardConfigs.cmake HW_CONFIG 2 and pico-duke3D's
// adafruitdvisd_cflags.h.
//
// The module must carry 8 MB of PSRAM on QMI CS1 = GPIO 47, i.e. be an RP2350B
// such as the Pico Plus 2. The build uses PICO_BOARD=pico2, as pico_shared's bld.sh
// does for this config; that header declares 4 MB of flash, which the build
// overrides (cmake/BootPartition.cmake) and q2_flash.c checks against the chip.
//
// Differences from the Fruit Jam that the code branches on:
//
//   GPIOHSTXINVERTED 0   the DVI breakout wires D- = D+ + 1.
//   Q2_AUDIO_I2S_DRIVER  PCM5100A: no codec and no headphone detect, so
//                        q2_audio.c plays to HDMI and I2S at the same time.
//                        Harmless with no DAC fitted.
//   NES_PIN_*            the two controller ports, polled by q2_nespad.cpp.
//   PIN_USB_HOST_*       PIO-USB on GPIO 20/21 (pico_shared's PIO_DP_PLUS_PIN for
//                        this config). There is no VBUS switch: the USB-A
//                        socket takes 5 V from VBUS directly.
//

#define HW_CONFIG 2		// also selects nespad.cpp's PIO program (separate CLK/LAT per port)
#define Q2_BOARD_NAME "Pico Plus 2 + Adafruit DVI and SD breakouts"

// --- UART (debug console), the pico2 default ----------------------------------
#define PICO_DEFAULT_UART 0
#define PICO_DEFAULT_UART_TX_PIN 0
#define PICO_DEFAULT_UART_RX_PIN 1

// --- HSTX / HDMI video lanes (read by pico_hdmi/video_output.c) -------------
// Adafruit DVI Breakout 4984, not inverted.
#define GPIOHSTXCK 14
#define GPIOHSTXD0 12
#define GPIOHSTXD1 18
#define GPIOHSTXD2 16
#define GPIOHSTXINVERTED 0
#define USE_HSTX 1

// --- I2S audio: optional PCM5100A DAC ---------------------------------------
#define Q2_AUDIO_I2S_DRIVER PICO_AUDIO_I2S_DRIVER_PCM5000A
#define PICO_AUDIO_I2S_DATA_PIN 26
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 27
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
#define I2S_AUDIO_COMPENSATE_DC_OFFSET 1	// PCM510x: DC blocker in audio_i2s.c
#define I2S_AUDIO_RING_SIZE 4096   // samples (16 KB); q2_audio.c keeps half of it queued
// No TLV320 and no Wii port. tlv320dac3100.c is still compiled and uses
// WIIPAD_I2C as an i2c_inst_t *, so it must name a valid instance.
#define WIIPAD_I2C i2c1
#define WII_PIN_SDA -1
#define WII_PIN_SCL -1

// --- SD card (hardware SPI0; PIO-SPI fallback available) --------------------
// Adafruit MicroSD breakout 254
#define SD_TX 3    // MOSI
#define SD_RX 4    // MISO
#define SD_SCK 2
#define SD_CS 5
#define USE_SD 1
#define SDCARD_SPI spi0
#define SDCARD_PIO pio1

// --- PSRAM (8 MB on QMI CS1 = GPIO 47) --------------------------------------
#define PSRAM_CS_PIN 47

// --- LED: blinks while a level's assets are copied into flash (q2_flash.c) ---
#define Q2_LED_PIN 25

// --- NES/SNES controller ports (pico_shared nespad, PIO) ---------------------
// Each port has its own CLK and LAT.
#define NES_PIN_CLK 6
#define NES_PIN_DATA 7
#define NES_PIN_LAT 8
#define NES_PIO pio1
#define NES_PIN_CLK_1 9
#define NES_PIN_DATA_1 10
#define NES_PIN_LAT_1 11
#define NES_PIO_1 pio1

// --- USB host on Pico-PIO-USB -----------------------------------------------
// Also selects the clock path that retasks PLL_USB as the 126 MHz clk_hstx
// source (q2_boot.c): the native USB controller is unused.
#define HAS_USBPIO
#define PIN_USB_HOST_DP (20u)
#define PIN_USB_HOST_DM (21u)

#endif // Q2_ADAFRUITDVISD_CFLAGS_H
