# Changelog

Add a section per release, headed with the tag. The release workflow puts the section
matching the tag in the GitHub release text (`tools/release_notes.sh`), so keep the
heading exactly the tag name.

## Unreleased

### New

- Support for a second board: a Pimoroni Pico Plus 2 with the Adafruit DVI and microSD
  breakouts (the pico_shared HW_CONFIG 2 setup). Firmware file:
  `pico-quake2_adafruitdvisd.uf2`. The wiring is described in the
  [README](https://github.com/PicoPlus-devel/pico-quake2#boards).
- NES and SNES controllers in that board's two controller ports.
- The board's LED blinks during the slow first load of a level.
- A version for [pico-bootLoader](https://github.com/PicoPlus-devel/pico-bootLoader), on
  both boards: Quake II can be started from the bootloader's menu, and quitting the game
  returns to it.

### Notes

- Boards with 8 MB of flash memory are not supported: most levels do not fit in it. The
  game needs 16 MB of flash memory and 8 MB of PSRAM.

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
