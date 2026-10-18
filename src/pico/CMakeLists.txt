# The pico-quake2 firmware image: libq2.a (cmake/q2_engine.cmake) plus the RP2350
# platform layer in this directory. cmake/psram_linker.cmake generates the linker
# script that places the engine's data in PSRAM.

include(${CMAKE_SOURCE_DIR}/cmake/psram_linker.cmake)

add_executable(quake2
    q2_audio.c
    q2_boot.c
    q2_console.c
    q2_display.c
    q2_fatal.c
    q2_fatfs_io.c
    q2_flash.c
    q2_input.c
    q2_nespad.cpp
    q2_psram.c
    q2_sys.c
    q2_usb.cpp
    q2_usb_frame.c
    # the engine heap (not force-included with the heap redirect, see q2_force.h)
    ${CMAKE_SOURCE_DIR}/src/quake2/other/q_mem.c
    ${CMAKE_SOURCE_DIR}/3rdparty/tlsf/tlsf.c
)

target_include_directories(quake2 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/src/quake2
    ${CMAKE_SOURCE_DIR}/src/quake2/other
    ${CMAKE_SOURCE_DIR}/3rdparty/tlsf
    ${CMAKE_SOURCE_DIR}/3rdparty/pico_shared_drivers/fonts
)

# PICO_STACK_SIZE: core0's stack (cmake/psram_linker.cmake puts it in its own 16 KB
# region; the engine's measured high-water mark is about 7 KB). Core1 is launched
# with its own stack array, so the SDK reserves none for it.
# Q2_CPU_KHZ: 378000 (default) or 504000; Q2_VREG: core voltage for it.
set(Q2_CPU_KHZ "378000" CACHE STRING "System clock in kHz (a multiple of 126000)")
if (Q2_CPU_KHZ GREATER 378000)
    set(_q2_vreg_default VREG_VOLTAGE_1_70)		# as the emulators (pico_shared) at 504 MHz
else()
    set(_q2_vreg_default VREG_VOLTAGE_1_50)
endif()
set(Q2_VREG "${_q2_vreg_default}" CACHE STRING "Core voltage (VREG_VOLTAGE_*)")
message(STATUS "pico-quake2 clock: ${Q2_CPU_KHZ} kHz at ${Q2_VREG}")

target_compile_definitions(quake2 PRIVATE
    Q2_DEVICE=1
    Q2_CPU_KHZ=${Q2_CPU_KHZ}
    Q2_VREG=${Q2_VREG}
    PICO_STACK_SIZE=16384
    PICO_CORE1_STACK_SIZE=0
    Q2_FCACHE_BASE=${Q2_FCACHE_BASE}
    Q2_FCACHE_SIZE=${Q2_FCACHE_SIZE}
)

# the engine headers carry tentative definitions that must merge with libq2's
target_compile_options(quake2 PRIVATE -fcommon)

target_link_libraries(quake2 PRIVATE
    q2
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_pll
    hardware_vreg
    hardware_dma
    hardware_pio
    hardware_spi
    hardware_flash
    hardware_sync
    pico_hdmi
    pico_audio_i2s_shared
    tlv320dac3100
    pico_fatfs
    pico_psram
    # USB host input: TinyUSB reads tusb_config.h from this directory
    pico_shared_usb_hid
    tinyusb_host
    tinyusb_pico_pio_usb
    # NES/SNES controller ports (q2_nespad.cpp; unused without NES_PIN_CLK)
    pico_shared_nespad
)

pico_enable_stdio_uart(quake2 1)
pico_enable_stdio_usb(quake2 0)

q2_use_psram_linker_script(quake2)

pico_set_program_name(quake2 "pico-quake2")
pico_set_program_version(quake2 "${Q2_RELEASE_VERSION}")

pico_add_extra_outputs(quake2)
