# pico-quake2

A port of id Software's Quake II to boards built around the Raspberry Pi RP2350
microcontroller: the Adafruit Fruit Jam, and a Pimoroni Pico Plus 2 with the Adafruit
DVI and microSD breakouts (see [Boards](#boards)). The picture goes out over HDMI, sound
over HDMI or a headphone output, and the game is played with a USB gamepad or a USB
keyboard and mouse. The game data is read from an SD card.

> [!WARNING]
> **This is an early pre-release, and a lot remains to be fixed.** The three levels of
> the Quake II demo load and can be played, and saving works. However, the frame rate
> is low, few boards and gamepads have been tested, and crashes and other errors are
> to be expected. Read [Status and limitations](#status-and-limitations) before
> installing.

## Screenshots

| | |
|---|---|
| ![The main menu, opened over the attract demo](docs/screenshots/main-menu.png) | ![A firefight in the warehouse of the first demo level](docs/screenshots/demo1-firefight.png) |
| Main menu | First demo level |
| ![An enemy between the crates of the first demo level](docs/screenshots/demo1-warehouse.png) | ![A corridor of the second demo level with a health pickup message](docs/screenshots/demo2-corridor.png) |
| First demo level | Second demo level |
| ![Gunfire crossing a room of the second demo level](docs/screenshots/demo2-firefight.png) | ![The help computer with the objectives of the third demo level](docs/screenshots/demo3-help-computer.png) |
| Second demo level | Help computer, third demo level |

The screenshots show the picture as the board produces it, with its default settings:
the game draws 320×240 pixels, which are doubled to 640×480 for HDMI, with the view at
70% of the screen and the brightness raised. They were rendered on a PC by the port's
test program, which runs the same game code and reproduces the board's colour depth.

## Status and limitations

### What works

- The three levels of the shareware demo, the attract demo and the menus
- Video over HDMI at 640×480
- Sound effects over HDMI, the Fruit Jam's headphone jack, or a PCM5100A DAC on the Pico
  Plus 2 board (see [Sound](#sound))
- USB gamepad input, tested with a DualShock 4 (see [Controls](#controls)); keyboards
  and mice are supported but not yet tested
- NES and SNES controllers in the controller ports of the Pico Plus 2 board
- Savegames and settings on the SD card
- Errors are shown on the screen, so no serial cable or computer is needed to find out
  what went wrong

### Known limitations

- **Low frame rate.** The demo levels run at about 20 to 25 frames per second, and
  slower in busy scenes. To get there, the view fills only 70% of the screen by default.
  A larger view lowers the frame rate further.
- **Two boards.** Only the boards listed under [Boards](#boards) are supported. The port
  needs an RP2350 with 8 MB of PSRAM and 16 MB of flash memory; other boards with that
  much memory may follow. Boards with 8 MB of flash memory or less do not work: the
  textures, models and sounds of a level must all fit in the flash memory, and those of
  most levels do not (see [TECHNICAL.md](TECHNICAL.md#8-mb-flash-boards-investigated-not-supported)).
- **Overclocked.** The RP2350 runs at 378 MHz, more than twice its rated 150 MHz, with
  its core voltage raised to 1.50 V. This has been reliable on the test board, but it is
  outside the chip's specification.
- **Slow first load of each level.** The first time a level is loaded, its textures,
  models and sounds are copied from the SD card into the board's flash memory. This
  takes considerably longer than later loads of the same level; the board's LED blinks
  while the copy is in progress. The copy is kept when the board is switched off. If
  other firmware overwrites that part of the flash, or the game data on the card
  changes, the copy is checked and refreshed automatically.
- **Only the demo has been tested.** The data files of the full game are read the same
  way, but none of its levels have been tried, and some may not fit in memory.
- **Untested input devices.** Of the USB devices, only a Sony DualShock 4 has been
  tested. Keyboards, mice, Xbox-compatible (XInput) pads and other USB gamepads are
  supported by the code but have not been tested yet.
- **Not every action is on the gamepad.** Using an inventory item, such as Quad Damage
  or the rebreather, and selecting a weapon directly require a keyboard.
- **No music.** The original game played its music from the CD, which is not supported.
  The demo has no music.
- **Not supported:** multiplayer, mission packs and mods.

Problems can be reported on the
[issues page](https://github.com/PicoPlus-devel/pico-quake2/issues). Please include the
text shown on the screen, if any.

## Boards

| Board | Firmware file |
|---|---|
| [Adafruit Fruit Jam](https://www.adafruit.com/product/6200) | `pico-quake2_fruitjam.uf2` |
| Pimoroni Pico Plus 2 with the Adafruit DVI breakout and microSD breakout (pico_shared hardware configuration 2) | `pico-quake2_adafruitdvisd.uf2` |

The second board is the configuration the pico_shared emulators call HW_CONFIG 2, built
on a breadboard or on a printed circuit board. It must use a
[Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2), or
another RP2350B module with 8 MB of PSRAM on GPIO 47 and 16 MB of flash; a Raspberry Pi
Pico 2 has no PSRAM and does not work. It is wired as follows:

| Function | GPIO |
|---|---|
| [Adafruit DVI breakout](https://www.adafruit.com/product/4984) | D0+ 12, CK+ 14, D2+ 16, D1+ 18 (and the pins next to them) |
| [Adafruit microSD breakout](https://www.adafruit.com/product/254) | SCK 2, MOSI 3, MISO 4, CS 5 |
| USB host socket (USB-A) | D+ 20, D− 21; 5 V from VBUS |
| NES/SNES controller port 1 | CLK 6, DATA 7, LATCH 8 |
| NES/SNES controller port 2 | CLK 9, DATA 10, LATCH 11 |
| PCM5100A I2S DAC (optional) | DATA 26, BCK 27, LRCK 28 |
| Serial console (optional) | TX 0, RX 1 |

## What you need

- One of the [boards](#boards) above
- A microSD card, formatted FAT32 or exFAT, with about 60 MB of free space
- The game data: the Quake II demo (see [Installing](#installing)), or the `baseq2`
  folder of the full game (untested, see above)
- An HDMI screen
- A USB gamepad, or a USB keyboard and preferably a mouse; on the Pico Plus 2 board, a
  NES or SNES controller can be used as well
- Optionally, headphones (Fruit Jam) or a PCM5100A DAC (Pico Plus 2 board)

## Installing

1. Download the firmware file for your board (see [Boards](#boards)) and
   `pico-quake2_demo_sdcard.zip` from the
   [releases page](https://github.com/PicoPlus-devel/pico-quake2/releases).
2. Hold the **BOOT** button on the board while connecting it to a computer over USB.
   It appears as a USB drive named `RP2350`.
3. Copy the firmware file onto that drive. The board restarts and shows an error,
   because the game data is not on the card yet.
4. Unpack `pico-quake2_demo_sdcard.zip` to the root of the SD card. This creates the
   folder `roms/quake2`.
   To use your own copy of Quake II instead, copy its `baseq2` folder into
   `roms/quake2` on the card.
5. Insert the card, connect the HDMI screen and a controller, and switch the board on.

The attract demo starts. Pressing any key or button opens the main menu; a new game is
started from **Game**.

The SD card then contains:

```
roms/quake2/license.txt          the licence agreement of the demo
roms/quake2/baseq2/pak0.pak      the game data
roms/quake2/baseq2/players/      player models and skins
roms/quake2/baseq2/config.cfg    settings and key bindings, written by the game
roms/quake2/baseq2/save/         savegames, written by the game
```

### The demo data and its licence

The firmware contains no game data. `pico-quake2_demo_sdcard.zip` contains the
unmodified data files of id Software's Quake II demo (version 3.14) and id Software's
licence agreement for the demo (`license.txt`). That agreement permits the demo to be
copied and distributed free of charge and by electronic means only, provided the
agreement accompanies it. It does not permit the demo to be sold, modified or used
commercially. The demo data is not covered by this project's GPL licence. Keep
`license.txt` with the data when copying it.

The full game is not included.

### With pico-bootLoader

Quake II can also be started from the menu of
[pico-bootLoader](https://github.com/PicoPlus-devel/pico-bootLoader), which keeps several
applications on one board. It is listed in the bootloader's **Ports** category on both
boards, and the bootloader's SD-card archive contains its firmware. The game data is not
part of that archive: install it in `roms/quake2` as described above.

- Quitting the game, or pressing RESET, returns to the bootloader menu.
- Other applications started from the bootloader may overwrite part of the flash memory
  that holds the copied level data. The next load of an affected level is then slow
  again, as on its first load.
- On the Pico Plus 2 board, USB devices must be connected to the USB-A socket on
  GPIO 20/21 (see [Boards](#boards)). The bootloader and the emulators take USB
  controllers on the Pico's own USB port instead; Quake II does not. NES and SNES
  controllers in the controller ports work in both.
- The standalone firmware files cannot be started from the bootloader. The bootloader
  version is built with `./fruitjam-build-forbootloader.sh` or
  `./adafruitdvisd-build-forbootloader.sh` (see [Building from source](#building-from-source))
  and goes in `/emu/8/` or `/emu/2/` on the SD card.

## Controls

A gamepad, a keyboard and a mouse can be used at the same time.

### Gamepad

Buttons are named by their position. The bottom face button is **A** on an Xbox pad and
**Cross** on a PlayStation pad.

| Button | In game | In menus |
|---|---|---|
| D-pad or left stick | move forward and back, turn | navigate |
| Right stick | look around | |
| Bottom face button (A / Cross) | fire | select |
| Right trigger | fire | |
| Right face button (B / Circle) | jump | back |
| Left face button (X / Square) | crouch | |
| Top face button (Y / Triangle) | next weapon | |
| L / R shoulder buttons | strafe left / right | |
| Select / Back, held | the D-pad looks up and down | |
| Left trigger | help computer (objectives) | |
| Start | open the menu | back |

The right stick works on pads that report one, such as Xbox-compatible pads, the
DualShock 4 and the DualSense.

### NES and SNES controllers

On the Pico Plus 2 board, controllers in the two NES/SNES ports work like a USB gamepad
with the same buttons. A SNES controller's buttons sit in the same positions as on the
table above: **B** fires, **A** jumps, **Y** crouches, **X** selects the next weapon and
**L** / **R** strafe. On a NES controller, **B** fires and selects in menus, and **A**
jumps and goes back. Some SNES controllers are only recognised once **A**, **X**, **L**
or **R** has been pressed; until then, **B** and **Y** act as a NES controller's **A**
and **B**.

### Keyboard and mouse

These are the standard Quake II bindings, with two changes: the player always runs
(hold **Shift** to walk), and the mouse always looks around, including up and down.
Bindings can be changed in **Options → Customize controls**.

| Action | Key |
|---|---|
| Move forward / back | **↑** / **↓** |
| Turn left / right | **←** / **→** |
| Strafe left / right | **,** / **.** |
| Strafe with the arrow keys (held) | **Alt** |
| Jump, swim up | **Space** |
| Crouch, swim down | **C** |
| Walk (held) | **Shift** |
| Look up / down | **A** / **Z** |
| Centre the view | **End** |
| Fire | **Ctrl** or left mouse button |
| Select a weapon | **1** to **0** |
| Next weapon | **/** or mouse wheel down |
| Previous weapon | mouse wheel up |
| Show the inventory | **Tab** |
| Previous / next inventory item | **[** / **]** |
| Use the inventory item | **Enter** |
| Drop the inventory item | **Backspace** |
| Strafe with the mouse (held) | right mouse button |
| Move forward | middle mouse button |

| Key | Function |
|---|---|
| **Escape** | menu |
| **F1** | help computer |
| **F2** / **F3** | save / load menu |
| **F4** | key bindings menu |
| **F6** / **F9** | quick save / quick load |
| **F10** | quit, which restarts the board |
| **=** / **-** | enlarge / reduce the view |
| **`** | console |
| **Pause** | pause |

### Settings

The view size (**screen size**) and **brightness** are set in the **Video** menu. Only
these two entries of that menu apply to the board; leave the others unchanged. The view
starts at 70% of the screen to keep the frame rate up, and the brightness starts above
the original game's default.

Settings and key bindings are saved to the SD card when the menu is closed.

To show the frame rate, open the console with **`** and enter `cg_drawfps 1`.

## Saving and loading

With a gamepad: press **Start**, select **Game → Save game**, choose a slot and press
the bottom face button. Loading works the same way through **Game → Load game**.

With a keyboard, **F2** and **F3** open the same menus, and **F6** and **F9** save and
load the quick save slot.

Savegames are written to `roms/quake2/baseq2/save/` on the SD card.

## Sound

Sound plays over HDMI.

- **Fruit Jam:** while headphones are plugged into the headphone jack, sound plays there
  instead. The board's speaker is not used.
- **Pico Plus 2 board:** if a PCM5100A DAC is fitted, sound plays over HDMI and the DAC
  at the same time.

## When something goes wrong

Errors are shown on the screen:

```
              FATAL ERROR

*** FATAL ERROR ***
baseq2/pak0.pak not found in
/roms/quake2.
Copy the baseq2 folder of Quake II
(or of the shareware demo) there.

Press RESET or power-cycle the board.
```

The board then stops, so the message stays readable.

| Message | Cause |
|---|---|
| No SD card | The card is not inserted, or not formatted FAT32 or exFAT |
| `/roms/quake2` not found on the SD card | The folder is missing or has a different name |
| `baseq2/pak0.pak` not found | The game data is not on the card, or not in `roms/quake2/baseq2` |
| No PSRAM found, or PSRAM too small | The board does not have 8 MB of PSRAM |
| This level's assets do not fit in the flash asset cache | The board has less than 16 MB of flash memory |
| Any other message | An error in the game itself; please report it with the text on the screen |

## Building from source

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
export PICO_PIO_USB_PATH=/path/to/Pico-PIO-USB
./fruitjam-build.sh          # -> build_fruitjam/src/pico/quake2.uf2
./adafruitdvisd-build.sh     # -> build_adafruitdvisd/src/pico/quake2.uf2
./buildAll.sh                # every release file -> releases/

# pico-bootLoader versions
./fruitjam-build-forbootloader.sh       # -> build_bl_fruitjam/src/pico/quake2.uf2
./adafruitdvisd-build-forbootloader.sh  # -> build_bl_adafruitdvisd/src/pico/quake2.uf2
```

Design notes, debugging tools and measurements are in [TECHNICAL.md](TECHNICAL.md).

## Credits

- Ported by Frank Hoedemakers
- Quake II engine and game code by id Software, released under the GNU GPL version 2;
  this port starts from [quake2generic](https://github.com/ozkl/quake2generic) by ozkl
- Board drivers from [pico_shared](https://github.com/PicoPlus-devel/pico_shared),
  including [PicoHDMI](https://github.com/fliperama86/pico_hdmi) by fliperama86 and
  the PSRAM setup from [PicoPlusPsram](https://github.com/AndrewCapon/PicoPlusPsram) by
  Andrew Capon
- USB host: [TinyUSB](https://github.com/hathach/tinyusb),
  [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) by sekigon-gonnoc and
  [tusb_xinput](https://github.com/Ryzee119/tusb_xinput) by Ryan Wendland
- [FatFs](http://elm-chan.org/fsw/ff/) by ChaN, and the
  [TLSF](https://github.com/mattconte/tlsf) allocator by Matthew Conte

### Use of AI

This port was developed with an AI coding assistant,
[Claude Code](https://claude.com/claude-code) by Anthropic. Most of the code written for
the port, the changes to the Quake II engine, the test tools, this documentation and the
screenshots were produced by Claude Code, working under the direction of Frank
Hoedemakers, who made the main design choices and tested the firmware on both boards.

## Licence

The source code is licensed under the GNU General Public License version 2; see
[LICENSE](LICENSE). The components in `3rdparty/` keep their own licences. The Quake II
demo data in the release is covered by id Software's demo licence agreement, as
described [above](#the-demo-data-and-its-licence).

Quake II © 1997 id Software, Inc. This project is not affiliated with or endorsed by
id Software.
