# Host harness

The host harness runs the firmware's Quake II engine on Linux. It compiles the same sources
under `src/quake2` as the RP2350 build, with the device-specific parts replaced by host
equivalents, so engine problems can be investigated without flashing a board.

The harness models the board's memory constraints:

- It is built with `-m32`, so structures have the same size as on the 32-bit Cortex-M33.
- The engine heap is limited to what the 8 MB PSRAM leaves after the engine's static data.
  Running out of memory in the harness means running out of memory on the board.
- The flash asset cache gets an emulated NOR flash region with the board's erase and program
  rules.
- By default it is built with AddressSanitizer and UndefinedBehaviorSanitizer.

There are two programs:

| Program | Purpose |
|---|---|
| `q2_host` | Headless. Runs on a virtual clock, so runs are deterministic. Writes frames as PPM files. Used for memory budgets, regression checks and screenshots. |
| `q2_host_sdl` | Interactive. The same engine in a window, with keyboard, mouse and sound, on real time. |

## Requirements

- GCC with 32-bit support (`gcc-multilib`) and Python 3.
- For `q2_host_sdl`: the 32-bit SDL2 development package. On Debian and Ubuntu:

  ```sh
  sudo dpkg --add-architecture i386
  sudo apt update
  sudo apt install gcc-multilib libsdl2-dev:i386
  ```

- A Quake II base directory: a directory containing `baseq2/pak0.pak`. The shareware demo is
  sufficient.

## Building

```sh
hosttest/build.sh            # q2_host and q2_host_sdl, with ASan and UBSan
NOSAN=1 hosttest/build.sh    # without sanitizers (faster, for sweeps)
```

`Q2_HOST_CFLAGS` adds compiler flags for every file, for example
`Q2_HOST_CFLAGS=-DSOME_DEFINE hosttest/build.sh`.

The script rebuilds everything on each run. Object files are placed in `hosttest/build/`.
If the 32-bit SDL2 package is not installed, the script builds `q2_host` only and says so.

## Running q2_host

```sh
./hosttest/q2_host <basedir> <frames> <dump-every-N> <outdir> [+commands...] [@N command...]
```

| Argument | Meaning |
|---|---|
| `<basedir>` | Directory containing `baseq2/pak0.pak`. |
| `<frames>` | Number of frames to run. Each frame advances the virtual clock by 16 ms. |
| `<dump-every-N>` | Write every Nth drawn frame to `<outdir>` as `frame_NNNNN.ppm`; 0 writes none. |
| `<outdir>` | Output directory for the frames; created if it does not exist. |
| `+command` | Quake II command-line commands, for example `+map demo2`. |
| `@N command` | Runs a console command at frame N, for example `@600 "map demo3"`. |

Examples:

```sh
# The attract demo for 1200 frames, one frame every 100
./hosttest/q2_host ~/roms/quake2 1200 100 out

# Load demo2, then change to demo3 at frame 400
./hosttest/q2_host ~/roms/quake2 800 50 out +map demo2 @400 "map demo3"

# Convert a frame to PNG; --hdmi gives the board's 640x480 RGB555 output
python3 hosttest/ppm2png.py out/frame_00600.ppm
python3 hosttest/ppm2png.py --hdmi out/frame_00600.ppm
```

At exit, the harness reports:

- the heap and PSRAM peak;
- the high-water marks of the renderer's work arrays;
- the flash cache contents and hit counts;
- the flash erase and program volume, with an estimate of the time it would take on the
  board.

Because the clock is virtual, two runs with the same arguments produce identical frames.
This makes before-and-after comparisons of engine changes possible.

## Running q2_host_sdl

```sh
./hosttest/q2_host_sdl <basedir> 0 0 <outdir> [+commands...] [@N command...]
```

The arguments are the same as for `q2_host`, with these differences:

- `<frames>` 0 runs until the window is closed.
- `<frames>`, `<dump-every-N>` and `@N` count drawn frames.
- The clock is real time, so runs are not deterministic. The engine limits the frame rate to
  `cl_maxfps` (90).

The window shows the 320x240 picture at 640x480, in the board's RGB555 colours. It can be
resized and keeps the 4:3 aspect ratio.

Input:

- **Keyboard:** keys are mapped by position, with the same table as the firmware. Escape opens
  the menu and the backquote key opens the console.
- **Mouse:** the mouse is captured during play. It is released in the menus, in the console and
  when the window loses focus.
- **Gamepads:** not supported.

Sound is played at 22050 Hz stereo, as on the board.

The program starts with the board's default settings: mouse wheel weapon selection, freelook,
always run, screen size 70 and brightness 0.7. `q2_host` does not apply these settings.

Settings changed in the menus are saved to `<basedir>/baseq2/config.cfg`. Later `q2_host`
runs on the same base directory also load that file, which changes their output. To keep
interactive sessions separate, use a scratch base directory that links to the game data:

```sh
mkdir -p ~/q2play/baseq2
ln -s ~/roms/quake2/baseq2/pak0.pak ~/roms/quake2/baseq2/players ~/q2play/baseq2/
./hosttest/q2_host_sdl ~/q2play 0 0 ~/q2play/out
```

When no window or sound appears, the first lines of output name the SDL video and sound
drivers in use. `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` runs the program without a
display or sound device.

## Environment variables

These apply to both programs unless noted.

| Variable | Effect |
|---|---|
| `Q2_SYSTEM_MALLOC=1` | Uses the system allocator instead of the PSRAM model. There is no memory budget, but AddressSanitizer detects heap overflows. |
| `Q2_HEAP_KB=n` | Sets the heap size, to measure how much a run needs. |
| `Q2_FLASH_KB=n` | Size of the emulated flash cache region in KB (default 13312; 0 disables the cache). |
| `Q2_FLASH_FILE=path` | Keeps the emulated flash in a file, so the next run starts with a filled cache. |
| `Q2_FLASH_CLOCK=f` | Advances the clock by the flash write time multiplied by f. With f = 10, a first level load takes about as long as on the board. `q2_host` then prints a status line every 5 virtual seconds. |
| `Q2_FLASH_STOP_KB=n` | Ends the run after n KB have been programmed, like a reset during a cache fill. |
| `Q2_MEMTRACE=bytes` | Logs every allocation of at least this size, with its callers. |
| `Q2_MEMLIVE=1` | Keeps a table of live heap blocks and prints it, grouped by caller, when an allocation fails. `Q2_MEMLIVE_MIN=bytes` sets the smallest group printed (default 16384). |
| `Q2_STACK=1` | Reports the engine's stack high-water mark. Requires a build without sanitizers, and is meaningful with `q2_host` only. |

## Scripts

| Script | Purpose |
|---|---|
| `sweep.sh <basedir> [frames] [maps...]` | Loads each map with an empty flash cache and prints its PSRAM and flash requirements as CSV. Defaults to demo1, demo2 and demo3. |
| `screenshots.sh <basedir>` | Renders the README screenshots into `docs/screenshots/`. |
| `memlive.sh <q2_host arguments...>` | Runs `q2_host` with `Q2_MEMLIVE=1` and resolves the callers in the dump to function names. |
| `fcdump.py [--csv \| --list] <flash-file>` | Lists the contents of an emulated flash region saved with `Q2_FLASH_FILE`: totals per category, or every entry. |
| `ppm2png.py [--hdmi] <ppm...>` | Converts PPM frames to PNG. `--hdmi` gives the board's output: RGB555, doubled to 640x480. |

## Files

| File | Contents |
|---|---|
| `build.sh` | Build script for both programs. |
| `q2_host.c` | Platform layer: files, framebuffer, sound buffer, emulated flash, PSRAM model and the main loop. |
| `q2_sdl.c` | The window, keyboard, mouse and sound of `q2_host_sdl`. |

`TECHNICAL.md` in the repository root describes the memory architecture and the flash cache,
and contains the measured per-map figures.
