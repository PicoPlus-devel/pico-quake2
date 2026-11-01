# Legacy NES/SNES controller input via PIO (two ports, SNES auto-detected at
# runtime). Vendored from the pico_shared root (nespad.h/nespad.cpp/nespad.pio),
# see the parent CMakeLists.txt for the resync procedure. The PIO program is an
# inline instruction array in nespad.cpp — nespad.pio is reference documentation
# only, no pico_generate_pio_header needed. nespad.cpp reads the force-included
# HW_CONFIG macro to select the shared-CLK/LAT program variant (Murmulator M2).
#
# Copied from pico-lutionX, which has pico_shared fb9ad91 (SNES Mouse with
# NESPAD_SNES_MOUSE, which the board header defines; 18-clock read otherwise,
# nespad_padtype[]) and the patch below with nespad_read_start() doing nothing
# while a read is pending.
#
# LOCAL PATCH (keep on resync / upstream to pico_shared): nespad_read_ready()
# in nespad.{h,cpp}, marked pico-quake2: -- non-blocking "finish will not
# block" check, used by src/pico/q2_nespad.cpp, which polls from the USB timer
# interrupt, so a controller port can never hang it.
add_library(pico_shared_nespad INTERFACE)
target_sources(pico_shared_nespad INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/nespad.cpp
)
target_include_directories(pico_shared_nespad INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(pico_shared_nespad INTERFACE hardware_pio)
