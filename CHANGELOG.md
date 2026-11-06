# Changelog

pico-quake2 is a port of id Software's Quake II to boards built around the Raspberry Pi
RP2350 microcontroller. The picture goes out over HDMI, the game data is read from an SD
card, and the game is played with a USB gamepad or a USB keyboard and mouse.

[The files of this release are listed at the end of these notes](#downloads___).

## v0.3.1-alpha

### Fixed

- The game can now be quit with a controller
  ([#4](https://github.com/PicoPlus-devel/pico-quake2/issues/4)). On the "Quit? Y/N"
  screen, the bottom face button (A / Cross, or B on a NES or SNES controller) answers
  yes, and the right face button or Start answers no. On a keyboard, Enter now also
  answers yes.

## v0.3-alpha

### Added

- On the Pico Plus 2 board, an original SNES Mouse can be used in either controller
  port, with a controller in the other port. Its left button fires and its right button
  strafes, as on a USB mouse. See
  [NES and SNES controllers](https://github.com/PicoPlus-devel/pico-quake2#nes-and-snes-controllers)
  in the README.

### Fixed

- The first load of a level, which takes a few minutes, no longer ends with "Loading
  plaque timed out.". The attract demo or the level now starts as soon as the load is
  finished. Before, the screen could stay on the start-up text until the attract demo
  had played through once.
- NES and SNES controllers could stop responding after a level was loaded for the first
  time.
- On the Adafruit Fruit Jam, USB devices were often not found at start-up. Also, after
  switching on with a low-speed mouse attached, sometimes no USB device worked until the
  board was reset.
- On the Pico Plus 2 board, some TVs and monitors showed small dots or short dotted lines
  in the picture. The picture is now clean. Thanks to @szuping for the hint that led to
  this fix.

### Changed

- On the Pico Plus 2 board, USB devices now go on the Pico's own USB-C port, through a
  USB OTG adapter or Y-cable. The USB-A socket on GPIO 20/21 is no longer used.
- A notice on the screen explains that the first load of a level is slow.
- If the board is reset during the first load of a level, the next load continues from
  what was already copied instead of starting again.
- USB keyboards and mice have now been tested and work.
- The processor runs at a lower core voltage (1.35 V instead of 1.50 V), so the board
  uses less power and runs cooler.
- The processor runs at 384 MHz instead of 378 MHz.

## v0.2.1-alpha

The firmware is the same as in v0.2-alpha.

- The release now includes **updateAll.exe**, which installs the Quake II demo in
  `/roms/quake2` on the SD card from a Windows computer. The README explains how to use
  it.
- **PicoNES PCB Gerber files.** The release now includes the Gerber files of the
  PicoNES PCB: `pico_nesPCB_v2.6.zip` with NES controller ports, and
  `pico_nesPCB_v2.8_SNES_PORTS.zip` with SNES controller ports, designed by John Edgar
  Park ([@johnedgarpark](https://twitter.com/johnedgarpark)). See
  [PicoNES PCB](https://github.com/PicoPlus-devel/pico-quake2#picones-pcb) in the
  README.

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
