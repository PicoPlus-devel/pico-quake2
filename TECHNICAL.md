# pico-quake2: technical notes

Developer notes for the Quake II port to RP2350 (HSTX video, at least 378 MHz, 8 MB PSRAM,
320x240). End-user documentation is in README.md.

## Status

Phase 0 (host harness and memory diet) is complete. Phase 1 (firmware) runs on the
Adafruit Fruit Jam: the attract demo plays from the SD card through the flash asset cache.
Phase 2 (performance) has started. Phase 3 is done apart from testing more input devices:
- USB input works (tested with a DualShock 4).
- Sound plays over HDMI or the headphone jack.
- Saves load after a reset and across firmware builds.

## Firmware

```sh
./fruitjam-build.sh                          # -> build_fruitjam/src/pico/quake2.uf2
./fruitjam-build.sh -DQ2_CPU_KHZ=504000      # 504 MHz (1.70 V) instead of 378 MHz
./adafruitdvisd-build.sh                     # -> build_adafruitdvisd/src/pico/quake2.uf2
./fruitjam-build-forbootloader.sh            # -> build_bl_fruitjam/src/pico/quake2.uf2
./adafruitdvisd-build-forbootloader.sh       # -> build_bl_adafruitdvisd/src/pico/quake2.uf2
```

Options (CMake): `Q2_CPU_KHZ` (a multiple of 126000), `Q2_VREG`, `Q2_ZBUFFER_SRAM`.

Boards. Each has a `<tag>_cflags.h`, force-included into every translation unit, and a
`<tag>-build.sh`:

| Tag | Board | `PICO_BOARD` |
|---|---|---|
| `fruitjam` | Adafruit Fruit Jam (HW_CONFIG 8): TLV320 DAC with headphone detect, PIO-USB on GPIO 1/2 | `adafruit_fruit_jam` |
| `adafruitdvisd` | pico_shared HW_CONFIG 2: Pimoroni Pico Plus 2 with the Adafruit DVI breakout (HSTX on GPIO 12–19, not inverted) and microSD breakout (SPI0 on GPIO 2–5); PIO-USB on GPIO 20/21; NES/SNES ports on GPIO 6–8 and 9–11; optional PCM5100A on GPIO 26–28 | `pico2` |

`pico2` is what pico_shared's `bld.sh` uses for HW_CONFIG 2. Its header declares an
RP2350A with 4 MB of flash, but GPIO 47 (PSRAM chip select) is still driven, and the
flash size is overridden (see [Flash layout](#flash-layout-16-mb-boards)).

**pico-bootLoader builds.** `<tag>-build-forbootloader.sh` runs `<tag>-build.sh` with
`-DBUILD_FOR_BOOTLOADER=ON` in `build_bl_<tag>`.
- FLASH starts at `0x10080000` and ends at the asset cache, 2.5 MB
  (`cmake/BootPartition.cmake`, applied by `cmake/psram_linker.cmake`). The image is
  about 0.8 MB.
- The loader identifies an application by its binary-info program name, `pico-quake2`,
  the first column of its `emu/emulators.txt`. Its `build_emulators.sh` clones this
  repository at the latest release tag, runs these scripts for HW_CONFIG 8 and 2, and
  installs `quake2.uf2` as `emu/<hw>/pico-quake2.uf2`. A tag reaches the bootloader's
  SD-card archive only if it contains the scripts. `buildAll.sh` does not release
  bootloader builds.
- Quitting: `_exit(0)` calls `watchdog_reboot(0, 0, 1)`, which clears watchdog scratch 4.
  The loader's resume check (`watchdog_enable_caused_reboot()`) is then false, so it
  shows its menu instead of starting the game again.
- Shared flash: other applications are flashed into the same partition and may write past
  their image (pico_shared's settings sector right after the binary, ROMs on boards
  without PSRAM, Doom's WAD). The cache's per-entry CRC catches overwritten entries. In
  the other direction, the loader CRCs an application and its aux image in flash before
  trusting its `.flashed` record, and pico_shared CRCs a ROM it left in flash before
  reusing it. An overlap therefore costs refill time only.
- On HW_CONFIG 2, input is PIO-USB on GPIO 20/21 and the NES/SNES ports. The loader and the
  emulators are built without PIO-USB for that configuration and use the native USB port,
  which this build leaves unused (PLL_USB drives `clk_hstx`).

Bring-up and debugging:
- **Flashing:** `tools/flash.sh <elf> [uart-log]` programs over a CMSIS-DAP probe with
  OpenOCD. With a UART log, it also waits for the boot banner and resets once more if none
  appears.
  - A probe reset (SYSRESETREQ) resets neither the clocks nor the QMI. After `reset halt`,
    `clk_sys` still runs from PLL_SYS at 378 or 504 MHz, and OpenOCD's flash algorithm
    would run the boot ROM's flash routines at that speed. The script therefore switches
    `clk_sys` to `clk_ref` (12 MHz) before programming.
  - Without that switch, programming an image that runs at 378 MHz or more could leave the
    board unable to boot or to be flashed again until it was powered up in BOOTSEL mode.
  - After `reset halt` or BOOTSEL, XIP reads of flash return zeros: the QSPI pads are not
    set up yet, and BOOTSEL forces SD1's input low. A zero readback in that state does not
    mean the flash is erased.
- **UART:** the console runs at 115200 baud on GPIO 44/45. Lines typed on it run as console
  commands, and a status line is printed every 5 seconds (fps, PSRAM heap, SRAM heap,
  flash cache).
  - `tools/uart_cmd.sh <uart-log> <timeout> <regex> <command>...` sends commands and waits
    for a reply line.
  - `tools/timedemo.sh <uart-log>` runs `timedemo 1` on `q2demo1.dm2` and prints the
    result. It sends `killserver` first: a `demomap` issued while the attract demo plays
    changes the map in place, and in timedemo mode the server then streams the whole demo
    while the client is still reconnecting. The run then reports 0 frames.
  - In timedemo mode the demo plays one server frame (100 ms) per rendered frame, so it runs
    faster than real time. The on-screen counter (`cg_drawfps`) shows the rate over the
    last second. The timedemo result is the average over the run.
- **Fatal errors:** boot failures (no SD card, no game data, no PSRAM) and engine errors are
  shown on the HDMI output.
- **Profiling:** `tools/pcsample.sh build_fruitjam/src/pico/quake2.elf 5000` reads
  core 0's PC sample register over the probe while it runs and prints the share per
  function, across all of flash, SRAM and ROM. Start a `tools/timedemo.sh` run first.
  OpenOCD's `profile` command is not used: its gmon histogram has 16-bit buckets, which
  overflow on the hottest loops and lost about a quarter of the samples.
- **Halting cores** (gdb, `halt`) stops the USB frame interrupt: a connected pad
  suspends and stays dead until the next reset. It also trips core 1's video watchdog.

## Releases

```sh
./buildAll.sh                       # -> releases/pico-quake2_fruitjam.uf2
                                    #    releases/pico-quake2_adafruitdvisd.uf2
                                    #    releases/pico-quake2_demo_sdcard.zip
tools/release_notes.sh v0.1-alpha   # the CHANGELOG.md section of that tag
```

- **Workflow.** `.github/workflows/BuildAndRelease.yml` runs these two on the
  self-hosted runner when a tag (`v0.N`, `v0.N.M`, `v*.*-alpha`) is pushed, or through
  `gh workflow run BuildAndRelease.yml -f tag=<tag>`. Without a tag it only builds.
  Every release, `-alpha` tags included, is published as a regular release: GitHub
  never marks a pre-release as the latest release, so the repository page would not
  show it. The release text is the tag's section of `CHANGELOG.md`; the workflow fails
  if there is none.
- **Firmware.** `buildAll.sh` builds each board in a fresh `build_release_<board>`,
  because CMake options are sticky: a development tree could carry `Q2_CPU_KHZ` or
  `Q2_ZBUFFER_SRAM` into a release. `Q2_RELEASE_VERSION` stamps the UF2's binary info
  (`picotool info`).
- **Demo data.** `tools/make_demo_zip.py` builds the SD-card zip from id's
  `q2-314-demo-x86.exe`, a self-extracting zip.
  - The installer is downloaded from the GWDG mirror of ftp.idsoftware.com, or from
    Yamagi's, and cached in `~/.cache/pico-quake2`. `Q2_DEMO_EXE=<path>` uses a local
    copy instead.
  - It must match SHA-256 `7ace5a43983f10d6bdc9d9b6e17a1032ba6223118d389bd170df89b945a04a1e`.
  - The zip holds `baseq2/pak0.pak` and `baseq2/players/` unmodified, and the demo's
    `license.txt`. The Windows executables and DLLs are left out.
- **Why the demo may be shipped.** The demo's licence agreement (`DOCS/license.txt`,
  1998) permits distributing the demo free of charge and by electronic means only,
  provided the agreement accompanies it at all times (section 3). It forbids selling,
  modifying and commercial use (section 2). Hence:
  - the files go out unmodified, as a free download on the release page;
  - the licence is in the zip twice, at the top and in `roms/quake2/`, where it travels
    onto the card with the data;
  - no game data goes into the repository or into a firmware image.

## USB input

| File | Role |
|---|---|
| `src/pico/q2_usb.cpp` | TinyUSB host on Pico-PIO-USB (PIO2; Fruit Jam GPIO 1/2 with port power on GPIO 11, HW_CONFIG 2 GPIO 20/21); a 4 ms timer at the lowest priority runs `tuh_task` and samples every device |
| `src/pico/q2_nespad.cpp` | NES/SNES controller ports (pico_shared nespad on PIO1), polled from the same timer without blocking and merged into the pad buttons: SNES pads by position, NES pads by name (A → right, B → bottom) |
| `src/pico/q2_usb_frame.c` | The 1 ms USB frame, and SOFs during flash writes |
| `src/pico/q2_input.c` | HID keyboard usages, mouse and pad buttons to Quake II keys; mouse and right stick to `QG_GetMouseDiff`; the default pad bindings (`QG_PlatformConfig`) |
| `3rdparty/pico_shared_drivers/usb_hid/` | pico_shared's HID driver, with the right stick and triggers added and the report re-arm fix (below) |

The input is sampled in an interrupt rather than once per frame, so a key tap or mouse
click shorter than a frame (60–80 ms in game) is not lost. Key events go into the engine's
key queue (`Quake2_SendKey`), which `Sys_SendKeyEvents` drains.

Default pad bindings, by button position (bottom = Xbox A / PlayStation cross):

| Pad | Game | Menus |
|---|---|---|
| D-pad, left stick | move forward/back, turn | navigate |
| Right stick (XInput, DualShock 4, DualSense) | look | |
| Bottom button, right trigger | fire | select |
| Right button | jump | back |
| Left button | crouch | |
| Top button | next weapon | |
| L / R | strafe | |
| Select / Back | hold: D-pad up/down looks (`+klook`) | |
| Left trigger | help computer | |
| Start | menu | back |

The same platform config (`QG_PlatformConfig` in `src/pico/q2_input.c`) also sets:
- `freelook 1` and `cl_run 1`;
- `viewsize 70`: the screen size, about 14% faster than 100 in the timedemo (22.6 fps
  against 19.9);
- `vid_gamma 0.7`: the brightness.

A saved `config.cfg` overrides all of these. The FPS counter (`cg_drawfps`) is off by
default and archived, so `cg_drawfps 1` typed in the console is kept.

Problems found on the way:
- **Flash writes suspended the USB devices.**
  - Flash erases and programs run with XIP off for tens to hundreds of milliseconds, and
    used to run with every interrupt on core 0 disabled.
  - After 3 ms without SOF the hub and the devices behind it suspend. A DualShock 4 did
    not recover: its light went out and every transfer after the first report failed.
  - Now every interrupt except the USB frame one is masked during a flash write. The frame
    handler is in SRAM and sends a bare SOF while flash is busy, because PIO-USB's full
    frame calls into flash.
- **One failed transfer silenced a pad.**
  - TinyUSB passes a failed transfer to the HID driver as a zero-length report.
  - Most of the driver's error paths ("Invalid DS4 report size 0") returned without
    re-arming the endpoint, so the pad's reports stopped for good.
  - The report callback now re-arms on every path.
- The status line counts failed transfers (`usb N failed`). It should stay at 0.

## Sound

| File | Role |
|---|---|
| `src/pico/q2_audio.c` | `SNDDMA_*` over a ring in SRAM; core1 pump to HDMI or the headphone jack |
| `client/snd_mix.c`, `client/snd_adpcm.c` | The engine's mixer, with its hot code and data in SRAM |

- **Mixing:** core0 runs the engine's `S_Update` once per frame. It writes 16-bit stereo
  at 22050 Hz into a ring of 8192 frames (371 ms, 32 KB in SRAM), up to `s_mixahead`
  (0.2 s) ahead of the play position. Sounds are stored as IMA ADPCM in the flash cache
  and decoded in the mixer.
- **Output:** pico_hdmi's core1 loop calls the pump between scanline interrupts. It
  interpolates the ring to 44.1 kHz and keeps one of two outputs filled:
  - HDMI data-island audio (17 ms queued), the default;
  - I2S to the TLV320 DAC (46 ms queued), while headphones are plugged in.
- **Output switching:** the codec's headset detection switches between the two. As in
  pico-duke3D, the speaker never plays. The jack gets silence while HDMI plays, so its
  PLL stays locked.
- **Both outputs:** a DAC without headset detection (`Q2_AUDIO_I2S_DRIVER` other than
  TLV320; the optional PCM5100A of HW_CONFIG 2) gets every sample that HDMI gets. The
  pump takes as many frames as the fuller of the two queues allows, so the slower of the
  two clocks (I2S from `clk_sys`, HDMI from `clk_hstx`) paces it, as in pico-duke3D.
- **Starvation:** the play position stops at the last painted frame. When core0 stalls,
  the output gets silence instead of a replay of old audio, and the engine never misses a
  ring wrap.
- **Flash writes:** the pump is held off during each one (`q2_audio_hold`), because the
  I2S driver's level query runs from flash.
- **HDMI mode:** the display runs in HDMI mode, with the audio rate set to 44.1 kHz after
  `video_output_init`.
  - pico_hdmi's `hstx.c` is not built, so `q2_display.c` calls `hstx_di_queue_init()`
    itself. Without it the audio queue pointer was NULL: the pump wrote to the boot ROM,
    and the HDMI output broke.
  - The queue is 256 islands (36 KB), upstream's size. Duke's 768 (108 KB) is sized for a
    pump that only runs once per game frame.

Cost on core0, timedemo q2demo1 at 378 MHz (HDMI mode, DualShock 4 attached):

| Sound | Time | fps | Mixing per frame |
|---|---|---|---|
| off (`s_initsound 0`, `snd_restart`) | 46.2 s | 13.7 | |
| on, mixer in flash and PSRAM | 49.2 s | 12.9 | 4.9 ms |
| on, mixer in SRAM (current) | 47.8 s | 13.2 | 2.5 ms |

Moving the mixer to core1 would save the remaining 2.5 ms (3.5%). It would mean core1
reading the flash cache and PSRAM, and the engine starting sounds from inside the mix
loop, so it is not done.

## Layout

| Path | Contents |
|---|---|
| `src/quake2/` | Engine, game and software renderer, vendored from ozkl/quake2generic (see `src/quake2/UPSTREAM.md`) and patched in place. Changes are marked `pico-quake2:`. |
| `src/quake2/qcommon/fcache.c` | Flash asset cache |
| `src/quake2/other/q_mem.c` | Engine heap (TLSF) |
| `src/quake2/other/q_hunk.c` | Model hunks on the engine heap |
| `src/quake2/client/snd_adpcm.c` | IMA ADPCM sound storage |
| `src/quake2/other/q2_force.h` | Force-included into every engine file: heap redirect, `<tgmath.h>` |
| `tools/arm_check.sh` | Compiles the engine for the Cortex-M33 with the firmware flags; reports stack frames, double-precision use and static data |
| `tools/gen_savetables.py` | Generates the savegame name tables from `game/*.c` (run by the build) |
| `src/pico/q2_usb.cpp`, `q2_usb_frame.c`, `q2_input.c` | USB input (see USB input) |
| `src/pico/q2_audio.c` | Sound output (see Sound) |
| `tools/flash.sh`, `tools/uart_cmd.sh`, `tools/timedemo.sh`, `tools/pcsample.sh` | Flashing over the probe, UART console commands, the timedemo benchmark, profiling |
| `3rdparty/tlsf/` | TLSF allocator (BSD-3-Clause) |
| `hosttest/` | Linux host harness |
| `hosttest/screenshots.sh`, `docs/screenshots/` | The README screenshots (see Host harness) |
| `<tag>_cflags.h`, `<tag>-build.sh`, `<tag>-build-forbootloader.sh` | Board configuration, standalone and pico-bootLoader builds (see Firmware) |
| `buildAll.sh`, `tools/make_demo_zip.py`, `tools/release_notes.sh`, `.github/workflows/BuildAndRelease.yml` | Releases (see Releases) |

## Host harness

```sh
hosttest/build.sh                  # -m32, ASan + UBSan  -> hosttest/q2_host
NOSAN=1 hosttest/build.sh          # without sanitizers
./hosttest/q2_host <basedir> <frames> <dump-every-N> <outdir> [+commands] [@N command]
python3 hosttest/ppm2png.py <outdir>/frame_00600.ppm
```

`<basedir>` contains `baseq2/pak0.pak`.

Running the harness:
- **Clock:** virtual, 16 ms per frame, so runs are deterministic. Dumped frames from two runs
  can be compared byte for byte.
- **Scripted commands:** `@N command` runs a console command at frame N, for example
  `@400 "map demo2"`.
- **Build size:** it is built with `-m32` so struct sizes match the 32-bit target.

Environment variables:

| Variable | Effect |
|---|---|
| (default) | Heap = 8 MB minus the engine's static data, which on the device is also in PSRAM. |
| `Q2_SYSTEM_MALLOC=1` | System allocator: no budget, but ASan sees heap overflows. |
| `Q2_HEAP_KB=n` | Overrides the heap size (to measure demand). |
| `Q2_FLASH_KB=n` | Flash cache region size (default 13312; 0 disables the cache). |
| `Q2_FLASH_FILE=path` | Keeps the emulated flash in a file, so a second run starts warm. |
| `Q2_MEMTRACE=bytes` | Logs allocations of at least that size with their callers. |
| `Q2_MEMLIVE=1` | Live-block table, dumped by caller on failure; `hosttest/memlive.sh` resolves it. |
| `Q2_STACK=1` | Reports the stack high-water mark (builds without sanitizers only). |

`hosttest/sweep.sh <basedir>` prints the per-map budget table below.
- `Q2_FLASH_KB` sets the region size for the sweep.
- The `fits` column shows whether the level loaded, and whether only after a rebuild.
- The per-category columns come from `hosttest/fcdump.py <flash-file>`, which lists what an
  emulated cache region (`Q2_FLASH_FILE`) holds: totals per category, or every entry with
  `--list`.

`hosttest/screenshots.sh <basedir>` renders the README screenshots into
`docs/screenshots/`:
- The runs set the two defaults from `QG_PlatformConfig` that change the picture
  (`viewsize 70`, `vid_gamma 0.7`); the harness itself leaves `QG_PlatformConfig` empty.
- `ppm2png.py --hdmi` truncates the palette to RGB555 as `rgb555x2` in `q2_display.c`
  does, and doubles every pixel to 640x480, so the PNGs match the HDMI output.
- The frame numbers are fixed in the script. The virtual clock makes them pick the same
  scenes on every run, until a change alters the engine's behaviour.

The harness also reports high-water marks of the renderer's work arrays at exit (edges,
surfaces, spans, alias vertices) and how many frames ran short of edges or surfaces.

The emulated flash has NOR semantics (programming only clears bits). The harness reports
erase and program volume, with an estimate of the device time using W25Q128-class typical
timings: 4 KB sector 45 ms, 64 KB block 150 ms, 256-byte page 0.4 ms.

## Memory architecture

The engine needs three kinds of memory:
- **PSRAM (8 MB):** static data and mutable state.
- **Flash:** read-only assets, used in place through the XIP window.
- **SRAM:** the hot data, still to be placed (Phase 1).

### PSRAM

**Static data (3.9 MB).** On the device it goes to `.psram_bss`/`.psram_data`.
- Large items: edicts (892 KB), surface cache (808 KB), client and server entity state,
  configstrings, the flash cache directory (128 KB) and the z-buffer (150 KB).
- The edicts, surface cache and z-buffer are static arrays rather than heap blocks, so they
  cannot be defeated by fragmentation.

**Engine heap (TLSF).** Everything the engine allocates goes through
`other/q2_alloc_redirect.h`, which is force-included into the engine sources.

Changes from the stock engine:
- **Collision model:** sized from the map's lump counts. It was 5.7 MB of fixed arrays.
- **BSP loading:** BSPs are read one lump at a time (`FS_LoadFileRange`), by both the
  collision model and the renderer. Neither holds the whole file.
- **World models:** loaded into a block hunk, one heap block per lump. They never need one
  large contiguous block.
- **Alias models and sprites:** use exact-size hunks.
- **Level change:** the local server frees the client's models, images and sounds of the old
  level before loading the new map (`CL_FlushLevelAssets`), so two levels are never in
  memory together.
- **Static cuts:** particles 4096 → 2048, console text 32 → 16 KB, menu player-model list
  1024 → 32. The write-only `r_leaftovis`/`r_vistoleaf` tables were removed.

### Flash asset cache (`qcommon/fcache.c`)

**Contents.** Wall textures, alias models, sprites, decoded pics/skins/skies and sounds are
used in place from a flash region. A level's missing assets are copied from the SD paks when
the level loads.

**Format.**
- Two alternating 128 KB directory slots, then the data area.
- Allocation is in 256-byte pages. Small assets share 4 KB erase sectors.
- A blank-page map lets new data go into erased flash without an erase.

**Lifetime rule.** An entry may be referenced by an in-memory object only while it is used
by the current level, or pinned (HUD and menu pics). The client drops all non-pic objects
before a level registers, so evicting older entries can never leave a dangling pointer.

**Eviction.** When no free run is long enough, the cache evicts the sector-aligned window
with the least evictable data and no in-use entries. Evicting the oldest entries one at a
time leaves scattered holes.

**Rebuild on overflow.** If an asset of the level still cannot be placed:
1. The level registers with placeholders.
2. The client empties the cache.
3. The level registers again, which writes its assets contiguously.

**Integrity.**
- Each entry has a CRC-32, checked on its first use after boot. This catches regions that
  another application overwrote.
- The directory stores an identity of the paks, so changing the game data invalidates
  the cache.

**Sounds** are stored as 4-bit IMA ADPCM at the mixer rate. The format is 256-sample blocks,
each starting with the decoder state, so channels can start or loop at any sample. They are
converted while streaming from SD, so the WAV is never held in PSRAM. Measured SNR is
24–41 dB on sample sounds.

### Stack and render work area

Every engine stack frame larger than 8 KB has been removed. They came to about 900 KB
on ARM (`tools/arm_check.sh`).

| Kind | Moved to |
|---|---|
| Large renderer arrays | the render work area |
| Rare paths (cinematics, demo recording, console resize) | the heap while in use |
| Non-re-entrant collision and trace lists | static |
| Trigger touch lists (nesting callbacks) | 256 entries |
| Print buffers | 1 KB, all `vsnprintf` |

The measured stack high-water mark is 6.8 KB in gameplay and 5.5 KB in demo playback.
The core 0 stack is planned at 16 KB.

**Render work area.** `r_work` in `r_local.h`, 112.5 KB on ARM, SRAM on the device.
- The edge renderer's arrays and the arrays for alias models, sprites and translucent
  polygons are never in use at the same time within a frame, so they share one buffer.
- Limits, with demo high-water marks:

  | Array | Limit | High-water |
  |---|---|---|
  | edges | 1600 | 927 |
  | surfaces | 800 | 456 |
  | spans | 1200 | flushed mid-scan when full, which only draws earlier |
  | alias vertices | 1024 | 405, the largest model; checked when a model loads |

- The display is fixed at 320x240 (`MAXWIDTH`/`MAXHEIGHT`).
- Reducing the limits left every demo frame byte-identical.

### Float math

The Cortex-M33 FPU is single precision.
- The engine is built with `-fsingle-precision-constant -fno-math-errno`.
- `<tgmath.h>` is force-included, so `sin`, `sqrt`, `fabs`, `atan2` and friends use the
  float versions for float arguments.
- With this, `-Wdouble-promotion` reports only `printf("%f")` arguments, 21 of them, down
  from 852.

### Savegames

**Encoding by name.** Function pointers (think, touch, pain and so on) and monster moves are
stored by name instead of as offsets from `InitGame` and `mmove_reloc`.
- The tables are generated at build time by `tools/gen_savetables.py`, from every function
  whose first parameter is `edict_t *` (889) and every `mmove_t` (289).
- The generator refuses `static` callback-shaped functions, so new callbacks cannot be
  missed.

**Version check.** The build-date check is replaced by `SAVEGAME_VERSION` in `g_local.h`.
Change it when the layout of the saved structs changes.

**Verified.**
- Host: a save made by the ASan build loads in the build without sanitizers, whose code
  addresses all differ.
- Fruit Jam: `save q2test` on demo1 writes to `/roms/quake2/baseq2/save/` on the SD card.
  The save loads after a reset. It also loads in a firmware build whose game code is
  0x80 bytes further along ("Function offsets -128"), and the level keeps running.

**Supporting changes.**
- `Sys_FindFirst`/`Sys_FindNext` are implemented on the platform hooks
  `QG_FindFirst`/`QG_FindNext`/`QG_FindClose`. They are needed for the savegame slot copy
  and wipe at unit transitions.
- On the device they use FatFs `f_findfirst` (`src/pico/q2_fatfs_io.c`).

### Measurements (shareware demo, gameplay, cold cache, 13 MB region)

| Map | PSRAM peak | Flash cache | Estimated first-visit flash time |
|---|---|---|---|
| demo1 | 7260 KB | 6199 KB | 10.6 s |
| demo2 | 7246 KB | 8044 KB | 13.6 s |
| demo3 | 7561 KB | 9143 KB | 15.4 s |

Transition chain `demo1 → 2 → 3 → 1 → 2`:
- **13 MB region:** the PSRAM peak is 7561 KB and no evictions are needed.
- **10.5 MB region:** eviction and two rebuilds occur. The dumped frames are byte-identical
  to the 13 MB run.

Both configurations are clean under ASan and UBSan.

### Timedemo on the Fruit Jam (q2demo1, 633 frames, warm flash cache)

| Clock | Z-buffer | Time | fps | SRAM heap left |
|---|---|---|---|---|
| 378 MHz | PSRAM | 43.6 s | 14.5 | 162 KB |
| 378 MHz | SRAM (`Q2_ZBUFFER_SRAM`) | 36.5 s | 17.4 | 12 KB |
| 504 MHz | PSRAM | 40.7 s | 15.5 | 162 KB |
| 504 MHz | SRAM | 33.3 s | 19.0 | 12 KB |

- The z-buffer in SRAM gains 20% at 378 MHz. Most of the remaining SRAM goes to it.
- 504 MHz gains only 7–9% for 33% more clock:
  - PSRAM runs at 126 MHz at both clocks (÷3 or ÷4).
  - Flash drops from 94.5 MHz (÷4) to 84 MHz (÷6).
  - So only the CPU-bound part of a frame speeds up.
- Repeated runs gave the same time to 0.1 s.
- These runs predate HDMI mode, USB and sound. The current build with sound off takes
  46.2 s (13.7 fps), and 47.8 s (13.2 fps) with sound on (see Sound).

### Performance pass (timedemo q2demo1, sound on, 378 MHz)

| Step | Time | fps |
|---|---|---|
| Start (HDMI mode, sound, USB pad) | 47.7 s | 13.3 |
| Hot flash code into SRAM (particles, dynamic lights, `R_MarkLeaves`, alias setup); newlib `memcpy`/`memset`/`strcmp` into SRAM; `q2_input_sample` returns early when nothing changed | 45.9 s | 13.8 |
| Z-buffer through the uncached PSRAM alias (`Q2_UNCACHED`) | 42.3 s | 15.0 |
| Surface blocks built four texels per load and store (no gain; kept, output identical) | 42.2 s | 15.0 |
| Dynamically lit surfaces built one mip level coarser (`sw_dlightmip 1`) | 33.5 s | 18.9 |
| Per-pixel globals hoisted into locals (alias spans, texture spans) | 33.1 s | 19.1 |
| Small renderer state into SRAM (whole objects; large arrays kept in PSRAM with `Q2_PSRAM_BSS`) | 31.9 s | 19.9 |

Notes:
- **Z-buffer.** It is rewritten completely every frame, and every access goes through
  `d_pzbuffer`. Routed through the uncached alias, its 150 KB of writes per frame no
  longer evict textures and the surface cache from the 16 KB XIP cache. Models pay for it:
  their z test now reads PSRAM uncached per pixel.
- **Dynamic lights.**
  - The status line counts surface-cache rebuilds by reason (`surf KB/s`).
  - In the timedemo, dynamic lights caused 92% of them (1.6 MB/s): every lit surface is
    rebuilt each frame, and once more after the light has gone.
  - `cl_lights 0` gives 20.0 fps against 14.9.
  - `sw_dlightmip 1` keeps the lights and gets most of the gain. A lit surface is built a
    mip level coarser (a quarter of the texels) in its own cache slot, so the unlit copy
    stays cached. `sw_dlightmip 0` restores stock behaviour.
- **Sound.** With sounds stored as 8-bit PCM (`s_adpcm 0`) instead of IMA ADPCM, the
  timedemo is about 2% faster (31.2 s). It is not the default: it roughly doubles the
  flash cache taken by sounds and sounds worse than ADPCM made from 16-bit sources.
- **Where the time goes now:**

  | Area | Share |
  |---|---|
  | Texture spans (reading the surface cache from PSRAM) | 16% |
  | Alias models | 22% |
  | Edges and BSP (walking PSRAM model data) | 12% |
  | Z writes | 8% |
  | Sound | 4% |
  | Waiting for the frame flip | 2% |

  85% of the samples are in SRAM code. What remains is mostly PSRAM and XIP latency.
- **504 MHz needs 1.70 V.**
  - At 1.60 V the first level load that writes the flash cache failed within a few
    erases. The failures varied:
    - core1 took an undefined-instruction fault on a valid instruction in SRAM;
    - the boot ROM's flash erase left the QMI stuck in direct mode;
    - DMA reported a read error.
  - A wedged QMI stops OpenOCD from programming the board until a watchdog reset
    (`mww 0x400d8000 0x80000000`, written while the cores are halted) or a power cycle.
  - At 1.70 V, which the pico_shared emulators use at 504 MHz, the board ran without a fault
    through about 11,700 erases and programs (6.7 MB) and several minutes of the attract
    loop. `Q2_VREG` now defaults to `VREG_VOLTAGE_1_70` above 378 MHz.
  - At the end of this pass, 504 MHz gives 21.8 fps at viewsize 100 (19.9 at 378 MHz) and
    24.6 fps at viewsize 70 (22.6). The gain is small for the same reason as above.
  - 378 MHz stays the default: at 504 MHz and 1.70 V the board runs too hot.

### Flash layout (16 MB boards)

| Range | Contents |
|---|---|
| `0x10000000–0x10080000` | pico-bootLoader (bootloader builds) |
| `0x10080000–0x10300000` | application slot, 2.5 MB (standalone builds start at `0x10000000`) |
| `0x10300000–0x11000000` | flash asset cache, 13 MB |

A demo level needs 6–9 MB of cache, so the port requires a board with 16 MB of flash.

**Flash size.** Every build defines `PICO_FLASH_SIZE_BYTES` as 16 MB
(`cmake/BootPartition.cmake`). The SDK's `flash_range_erase`/`_program` hard-assert
against it, and a board header may understate the chip: `pico2.h` declares 4 MB, so the
first cache write would assert on the Pico Plus 2. The real size is read at run time:
- `q2_flash_probe()` reads the JEDEC ID (`0x9F`) first in `main()`, before the overclock
  and the PSRAM setup, as pico_shared's `storage_get_flash_capacity()` does. It restores
  the QMI M0 read setup afterwards.
- `q2_flash_init()` cuts the cache region to the chip, so writes never wrap round onto the
  firmware. Below 1 MB there is no cache, and assets stay in PSRAM.
- The boot log shows the result, e.g. `q2_flash: Winbond flash, JEDEC id ef4018, 16384 KB`.

### 8 MB flash boards (investigated, not supported)

Question: can a board with 8 MB of flash and 8 MB of PSRAM run the game, standalone and
under pico-bootLoader, without lower sound or texture quality and without changing the
16 MB boards? Answer: no. Of the demo's levels only demo1 fits. The attract demo
(`q2demo1.dm2`) plays demo2.

**PSRAM** is not the limit: the port already targets 8 MB. **Flash** is the limit: a level's
assets must all be in the cache at once, and slower level changes do not help when one
level alone exceeds the region.

Regions on an 8 MB chip. Each figure includes the two 128 KB directory slots.

| Layout | Cache | Region |
|---|---|---|
| Current firmware (cut to the chip by `q2_flash_init`) | `0x10300000–0x10800000` | 5120 KB |
| Bootloader build, application slot cut to 1 MB | `0x10180000–0x10800000` | 6656 KB |
| Standalone build, application in the first 1 MB | `0x10100000–0x10800000` | 7168 KB |

Starting the cache directly after the 805 KB image would add 192 KB to either layout. That
changes nothing below.

Smallest region in which each map loads from a cold cache (`Q2_FLASH_KB` bisected in
64 KB steps). The following columns apply the levers in turn:

| Map | Cache contents | Smallest region | Lossless levers | + 11 kHz sound | + half-size skins | Both |
|---|---|---|---|---|---|---|
| demo1 | 6247 KB | 6528 KB | 5915 KB | | | |
| demo2 | 8100 KB | 8384 KB | 7750 KB | 6674 KB | 6429 KB | 5353 KB |
| demo3 | 9210 KB | 9472 KB | 8773 KB | 7596 KB | 7064 KB | 5887 KB |

The contents are KB in 256-byte pages. demo3 breaks down as:
- sounds 2355 KB;
- models 2595 KB;
- skins 2342 KB;
- textures 1348 KB;
- sky 385 KB;
- pics 165 KB.

Levers:
- **Lossless (estimated, not implemented): 613–699 KB per level.**
  - Drop the MD2 GL command lists from cached models (112–198 KB). The software renderer
    never reads them in place.
  - Skip the player model and its weapon model in single player (373 KB). They are drawn
    only for other players.
  - Directory slots of 64 KB instead of 128 KB (128 KB). A level holds at most 557 entries.
- **Assets kept in PSRAM:** the room up to the 7.5 MB target is 420, 434 and 119 KB. That
  is not enough for demo2 or demo3, and the heap fragments: see the failure below.
- **Lower quality (ruled out):**
  - Sounds at 11 kHz halve the sound share.
  - Skins at half size remove three quarters of the skin share.
  - The bootloader layout needs both of them for demo3, which then fits with 769 KB to
    spare. Full-game levels with more monster types are not measured and may need more.

The current firmware on an 8 MB chip:
- The attract demo writes flash for an estimated 65 s, empties the cache, and stops with
  "This level's assets do not fit in the flash asset cache".
- A cold `map demo3` instead stops with `Hunk_Alloc: out of memory`. The second
  registration reloads the world model into a fragmented heap: 790 KB is free, the largest
  block is 217 KB, and `Mod_LoadNodes` needs 225 KB.
- On 16 MB boards the demo levels never overflow twice. The chain at 10.5 MB rebuilds
  demo3 without error.

If 8 MB support is taken up later, the 16 MB layout and cache contents must stay as they
are:
- Choose the layout at run time from the JEDEC size, with an image size check at build
  time.
- Apply any quality trade-off only when the region is small. Sound entries are already
  keyed by the mixer rate (`S_LoadSound`), so a 16 MB board would never use 11 kHz entries.

## Engine fixes found by the harness

| Area | Fix |
|---|---|
| Mixer | The 8-bit path indexed its volume table with `leftvol >> 11`, which is always 0, so every 8-bit sound was silent. It now multiplies. |
| Sound loading | Allocated with the source sample width instead of the stored width. |
| `Mod_LoadTexinfo` | Indexed `vecs[0][4..7]` (undefined behaviour that GCC may exploit at `-O2`). |
| `SZ_Write` | `memcpy` from NULL with length 0. |
| `Com_PageInMemory` | Read past the block-hunk head; it is now a no-op. |
| Compiler flags | The renderer relies on wrapping signed arithmetic, so the engine is built with `-fwrapv -fno-aggressive-loop-optimizations`. |
| FPS counter (`cg_drawfps`) | Did not mark its screen area dirty, so with a smaller view (`viewsize`) the old digits were never erased from the border. |
| `config.cfg` | Stock Quake II writes it only on quit, and the board is switched off instead. It is now written when the menu closes, if an archived cvar or a key binding changed (`com_configmodified`). |

## Next steps

- Test a keyboard, a mouse and an XInput pad. (The headphone switch works.)
- SRAM is nearly full: 13 KB of the 68 KB SRAM heap is free.
  - USB input took 23 KB.
  - Sound took 32 KB (ring) + 16 KB (I2S) + 36 KB (HDMI queue) + 9 KB (mixer).
  - The performance pass moved about 16 KB of code and 17 KB of renderer state in.
  - The z-buffer (150 KB) stays in PSRAM.
- Further performance: model z tests (skip the read for the view weapon), BSP and edge
  data in PSRAM, particles.
