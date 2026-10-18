#ifndef Q2_FRUITJAM_CFLAGS_H
#define Q2_FRUITJAM_CFLAGS_H 1
//
// Board configuration for pico-quake2 on the Adafruit Fruit Jam (HW_CONFIG 8).
//
// Force-included into every translation unit (see CMakeLists.txt). The vendored
// pico_shared drivers read these macros at compile time. The pico-sdk board header
// (adafruit_fruit_jam.h) guards its defaults with #ifndef, so these win.
// Pin values mirror pico-duke3D's fruitjam_cflags.h and pico_shared BoardConfigs.cmake.
//

#define HW_CONFIG 8
#define Q2_BOARD_NAME "Adafruit Fruit Jam"

// --- UART (debug console) ---------------------------------------------------
// The board default (UART1 on GPIO 8/9) collides with the on-board ESP32.
#define PICO_DEFAULT_UART 0
#define PICO_DEFAULT_UART_TX_PIN 44
#define PICO_DEFAULT_UART_RX_PIN 45

// --- HSTX / HDMI video lanes (read by pico_hdmi/video_output.c) -------------
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

// --- I2S audio + TLV320DAC3100 codec (headphone jack) -----------------------
#define Q2_AUDIO_I2S_DRIVER PICO_AUDIO_I2S_DRIVER_TLV320
#define PICO_AUDIO_I2S_DATA_PIN 24
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 26
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN 22
// PERIPH_RESET (active low): resets the DAC and other on-board peripherals.
// q2_boot.c holds it high (released), as pico-duke3D does.
#define Q2_PERIPH_RESET_PIN 22
#define PICO_AUDIO_I2S_INTERRUPT_PIN 23
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
#define I2S_AUDIO_RING_SIZE 4096   // samples (16 KB); q2_audio.c keeps half of it queued
#define WIIPAD_I2C i2c0
#define WII_PIN_SDA 20
#define WII_PIN_SCL 21

// --- SD card (hardware SPI0; PIO-SPI fallback available) --------------------
#define SD_TX 35   // MOSI
#define SD_RX 36   // MISO
#define SD_SCK 34
#define SD_CS 39
#define USE_SD 1
#define SDCARD_SPI spi0
#define SDCARD_PIO pio1

// --- PSRAM (8 MB APS6404 on QMI CS1 = GPIO 47) ------------------------------
#define PSRAM_CS_PIN 47

// --- LED: blinks while a level's assets are copied into flash (q2_flash.c) ---
// The red LED; the NeoPixels are not used.
#define Q2_LED_PIN 29

// --- USB host on Pico-PIO-USB -----------------------------------------------
// Also selects the clock path that retasks PLL_USB as the 126 MHz clk_hstx
// source (q2_boot.c): the native USB controller is unused on this board.
#define HAS_USBPIO
#define PIN_USB_HOST_DP (1u)
#define PIN_USB_HOST_DM (2u)
#define PIN_USB_HOST_VBUS (11u)

#endif // Q2_FRUITJAM_CFLAGS_H
