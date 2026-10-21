# Changelog

pico-quake2 is a port of id Software's Quake II to boards built around the Raspberry Pi
RP2350 microcontroller. The picture goes out over HDMI, the game data is read from an SD
card, and the game is played with a USB gamepad or a USB keyboard and mouse.

[The files of this release are listed at the end of these notes](#downloads___).

## v0.2-alpha

Second pre-release. It adds a second board, NES and SNES controllers, and a version for
pico-bootLoader. This is still an early version: a lot remains to be fixed, and problems
are to be expected.


### New

- Support for a second board: a Pimoroni Pico Plus 2 with the Adafruit DVI and microSD
  breakouts (the pico_shared HW_CONFIG 2 setup). The wiring is described under
  [Boards](https://github.com/PicoPlus-devel/pico-quake2#boards) in the README.
- NES and SNES controllers in that board's two controller ports.
- On that board, sound plays over HDMI and, if one is fitted, a PCM5100A DAC.
- Quake II can be started from the menu of
  [pico-bootLoader](https://github.com/PicoPlus-devel/pico-bootLoader) on both boards, and
  quitting the game returns to it. The bootloader's SD-card archive includes it.
- The board's LED blinks during the slow first load of a level.

### Fixed

- The start-up text no longer turns nearly black while the game loads.

### Known limitations

- About 20 to 25 frames per second, with the view at 70% of the screen
- The first load of each level is slow
- Boards with 8 MB of flash memory are not supported: most levels do not fit in it. The
  game needs 16 MB of flash memory and 8 MB of PSRAM.
- On the Pico Plus 2 board, USB controllers must be connected to a USB-A socket on
  GPIO 20/21, not to the Pico's own USB port
- Keyboards, mice and USB gamepads other than the DualShock 4 have not been tested yet
- The full game has not been tested
- No music and no multiplayer

See [Status and limitations](https://github.com/PicoPlus-devel/pico-quake2#status-and-limitations)
for details.


## v0.1-alpha

First pre-release. Quake II runs on the Adafruit Fruit Jam, but this is an early
version: a lot remains to be fixed, and problems are to be expected.

### Downloads

- `pico-quake2_fruitjam.uf2`: the firmware for the Adafruit Fruit Jam.
- `pico-quake2_demo_sdcard.zip`: the Quake II demo (three levels) for the SD card.
  Unpack it to the root of the card. It includes id Software's licence agreement for
  the demo, which must stay with the data.

Installation is described in the
[README](https://github.com/PicoPlus-devel/pico-quake2#installing).

### What works

- The three demo levels, the attract demo and the menus
- Video over HDMI, and sound over HDMI or the headphone jack
- USB gamepads, tested with a DualShock 4
- Saving, loading and settings on the SD card

### Known limitations

- About 20 to 25 frames per second, with the view at 70% of the screen
- The first load of each level is slow
- Keyboards, mice and gamepads other than the DualShock 4 have not been tested yet
- The full game has not been tested
- No music, no multiplayer, and no pico-bootLoader version yet

See [Status and limitations](https://github.com/PicoPlus-devel/pico-quake2#status-and-limitations)
for details.

<a name="downloads___"></a>

## Downloads

| Board | Firmware |
|:--|:--|
| Adafruit Fruit Jam | [pico-quake2_fruitjam.uf2](https://github.com/PicoPlus-devel/pico-quake2/releases/download/v0.2-alpha/pico-quake2_fruitjam.uf2) |
| Pimoroni Pico Plus 2 with the Adafruit DVI and microSD breakouts (HW_CONFIG 2) | [pico-quake2_adafruitdvisd.uf2](https://github.com/PicoPlus-devel/pico-quake2/releases/download/v0.2-alpha/pico-quake2_adafruitdvisd.uf2) |

The game data for the SD card is in
[pico-quake2_demo_sdcard.zip](https://github.com/PicoPlus-devel/pico-quake2/releases/download/v0.2-alpha/pico-quake2_demo_sdcard.zip):
the Quake II demo (three levels), the same as in v0.1-alpha. Unpack it to the root of the
card. It includes id Software's licence agreement for the demo, which must stay with the
data.

Installation is described in the
[README](https://github.com/PicoPlus-devel/pico-quake2#installing).