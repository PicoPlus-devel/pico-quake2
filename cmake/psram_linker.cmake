# Generates the linker script for pico-quake2 from the SDK's memmap_default.ld.
# Adapted from pico-duke3D's cmake/psram_linker.cmake; see that file for the
# history behind each patch. One generator owns all patches, because
# pico_set_linker_script can only be applied once per target.
#
# Memory layout:
#
#   FLASH   the application, capped so it ends below the flash asset cache
#           (cmake/BootPartition.cmake), in both the standalone and bootloader builds
#   RAM     SDK, drivers and the platform layer, plus the engine objects explicitly
#           placed in SRAM (Q2_SRAM_BSS -> .sram_bss, Q2_HOT -> .time_critical.*)
#   STACK   core0's stack, carved off the top of SRAM (core1 gets its own array)
#   PSRAM   all of libq2.a's .bss/COMMON (.psram_bss, zeroed by q2_psram_init) and
#           .data (.psram_data, copied from flash by q2_psram_init), then the engine
#           heap (TLSF, other/q_mem.c) up to the end of the fitted chip
#
# .psram_bss/.psram_data are only valid after SetupPsram() maps the PSRAM window, so
# nothing may touch engine globals before q2_psram_init() has run.

set(Q2_STACK_REGION "16k" CACHE STRING "SRAM reserved at the top for core0's stack")
set(Q2_PSRAM_SIZE "8192k" CACHE STRING "PSRAM region size in the linker script")

# Pico SDK >= 2.3.0 no longer ships a flat RP2350 memmap_default.ld to copy. It
# assembles the linker script from small include files (pico_standard_link/
# script_include) and lets a target override any of them with
# pico_add_linker_script_override_path. With such an SDK the patches below are
# applied to the include files they touch instead: every patch text occurs in
# exactly one of them. The files are joined into one text with a marker line in
# front of each, patched like the flat script, split again, and the ones that
# changed go into a directory of their own that the linker searches first.
set(_Q2_LD_FRAGMENTS
    memory_flash.incl memory_psram.incl memory_ram.incl
    default_text_excludes.incl default_rodata_excludes.incl
    section_default_data.incl sections_stack.incl section_end.incl)

function(_q2_ld_fragments_read out)
    set(_dir "${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/script_include")
    set(_text "")
    foreach(_f IN LISTS _Q2_LD_FRAGMENTS)
        if (NOT EXISTS "${_dir}/${_f}")
            message(FATAL_ERROR "psram_linker: ${_dir}/${_f} not found")
        endif()
        file(READ "${_dir}/${_f}" _c)
        string(APPEND _text "/*@@ ${_f} @@*/\n${_c}")
    endforeach()
    set(${out} "${_text}" PARENT_SCOPE)
endfunction()

function(_q2_ld_fragments_write target text)
    set(_sdk "${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/script_include")
    set(_dir "${CMAKE_BINARY_DIR}/${target}_ld")
    file(REMOVE_RECURSE "${_dir}")
    set(_changed "")
    list(LENGTH _Q2_LD_FRAGMENTS _n)
    math(EXPR _last "${_n} - 1")
    foreach(_i RANGE ${_last})
        list(GET _Q2_LD_FRAGMENTS ${_i} _f)
        set(_mark "/*@@ ${_f} @@*/\n")
        string(FIND "${text}" "${_mark}" _a)
        string(LENGTH "${_mark}" _ml)
        math(EXPR _a "${_a} + ${_ml}")
        if (_i EQUAL _last)
            string(LENGTH "${text}" _b)
        else()
            math(EXPR _j "${_i} + 1")
            list(GET _Q2_LD_FRAGMENTS ${_j} _g)
            string(FIND "${text}" "/*@@ ${_g} @@*/\n" _b)
        endif()
        math(EXPR _len "${_b} - ${_a}")
        string(SUBSTRING "${text}" ${_a} ${_len} _c)
        file(READ "${_sdk}/${_f}" _orig)
        if (NOT _c STREQUAL _orig)
            file(WRITE "${_dir}/${_f}" "${_c}")
            list(APPEND _changed ${_f})
        endif()
    endforeach()
    pico_add_linker_script_override_path(${target} "${_dir}" FILES ${_changed})
    message(STATUS "psram_linker: ${target} overrides ${_changed} (in ${_dir})")
endfunction()

function(q2_use_psram_linker_script target)
    if (COMMAND pico_add_linker_script_override_path)
        # SDK >= 2.3.0: patch the include files (see above).
        set(_sdk23 TRUE)
        set(_src "the SDK's script_include files")
        _q2_ld_fragments_read(_ld)
    else()
        set(_sdk23 FALSE)
        set(_src "${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld")
        set(_dst "${CMAKE_BINARY_DIR}/memmap_quake2.ld")
        file(READ "${_src}" _ld)
    endif()

    # 0) FLASH region: start of the image and a cap below the asset cache.
    if (BUILD_FOR_BOOTLOADER)
        set(_origin "${Q2_APP_BASE}")
        q2_bootloader_target_props(${target})
    else()
        set(_origin "${Q2_XIP_BASE}")
    endif()
    string(REPLACE
        "INCLUDE \"pico_flash_region.ld\""
        "FLASH(rx) : ORIGIN = ${_origin}, LENGTH = ${Q2_APP_SIZE}"
        _ld "${_ld}")
    string(REGEX REPLACE
        "FLASH\\(rx\\)[ \t]*:[^\n]*"
        "FLASH(rx) : ORIGIN = ${_origin}, LENGTH = ${Q2_APP_SIZE}"
        _ld "${_ld}")
    if (NOT _ld MATCHES "ORIGIN = ${_origin}, LENGTH = ${Q2_APP_SIZE}")
        message(FATAL_ERROR "psram_linker: could not set the FLASH region in ${_src}")
    endif()
    message(STATUS "psram_linker: ${target} FLASH ORIGIN=${_origin} LENGTH=${Q2_APP_SIZE}")

    # 1) core0's stack -> a dedicated STACK region at the top of SRAM (out of the
    #    4 KB SCRATCH_Y bank, and outside both the .bss range crt0 zeroes and the
    #    heap's end..__StackLimit range).
    string(REPLACE
        "    .stack1_dummy (NOLOAD):\n    {\n        *(.stack1*)\n    } > SCRATCH_X"
        "    .stack1_dummy (NOLOAD):\n    {\n        *(.stack1*)\n    } > STACK"
        _ld "${_ld}")
    string(REPLACE
        "    .stack_dummy (NOLOAD):\n    {\n        KEEP(*(.stack*))\n    } > SCRATCH_Y"
        "    .stack_dummy (NOLOAD):\n    {\n        KEEP(*(.stack*))\n    } > STACK"
        _ld "${_ld}")
    string(REPLACE
        "__StackOneTop = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);"
        "__StackOneTop = ADDR(.stack1_dummy) + SIZEOF(.stack1_dummy);"
        _ld "${_ld}")
    string(REPLACE
        "__StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);"
        "__StackTop = ADDR(.stack_dummy) + SIZEOF(.stack_dummy);"
        _ld "${_ld}")
    if (NOT _ld MATCHES "ADDR\\(\\.stack_dummy\\)")
        message(FATAL_ERROR "psram_linker: could not move the stack out of SCRATCH_Y in ${_src}")
    endif()

    # 2) MEMORY: shorten RAM by the stack region, add STACK and PSRAM (one
    #    replacement: all three match the same RAM line).
    if (_sdk23)
        # SDK >= 2.3.0 declares RAM from RAM_ORIGIN/RAM_LENGTH (memory_ram.incl)
        # and declares PSRAM itself (memory_psram.incl, from a generated
        # pico_psram_region.ld that is 0 bytes long by default), so only the
        # PSRAM length is filled in here.
        string(REPLACE
            "RAM(rwx) : ORIGIN =  RAM_ORIGIN, LENGTH = RAM_LENGTH"
            "RAM(rwx) : ORIGIN =  RAM_ORIGIN, LENGTH = RAM_LENGTH - ${Q2_STACK_REGION}
    STACK(rw) : ORIGIN = RAM_ORIGIN + RAM_LENGTH - ${Q2_STACK_REGION}, LENGTH = ${Q2_STACK_REGION}"
            _ld "${_ld}")
        string(REPLACE
            "INCLUDE \"pico_psram_region.ld\""
            "PSRAM(rwx) : ORIGIN = 0x11000000, LENGTH = ${Q2_PSRAM_SIZE}"
            _ld "${_ld}")
    else()
        string(REPLACE
            "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k"
            "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k - ${Q2_STACK_REGION}
    STACK(rw) : ORIGIN = 0x20080000 - ${Q2_STACK_REGION}, LENGTH = ${Q2_STACK_REGION}
    PSRAM(rwx) : ORIGIN = 0x11000000, LENGTH = ${Q2_PSRAM_SIZE}"
            _ld "${_ld}")
    endif()
    if (NOT _ld MATCHES "STACK\\(rw\\)" OR NOT _ld MATCHES "PSRAM\\(rwx\\) : ORIGIN = 0x11000000")
        message(FATAL_ERROR "psram_linker: MEMORY rewrite failed in ${_src}")
    endif()

    # 3) libq2.a's initialised data -> PSRAM, loaded from flash. Placed before the
    #    SDK's .data so its *(.data*) does not claim these input sections first (ld
    #    places each input section at the first output section that matches).
    string(REPLACE "    .data : {"
"    .psram_data : {
        . = ALIGN(4);
        __psram_data_start = .;
        *libq2.a:*(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        __psram_data_end = .;
    } > PSRAM AT> FLASH
    __psram_data_load = LOADADDR(.psram_data);

    .data : {" _ld "${_ld}")
    if (NOT _ld MATCHES "__psram_data_load")
        message(FATAL_ERROR "psram_linker: could not add .psram_data to ${_src}")
    endif()

    # 4) Engine objects placed in SRAM on purpose (Q2_SRAM_BSS), then libq2.a's
    #    .bss/COMMON -> PSRAM. .sram_bss lies between .tbss (__bss_start__) and
    #    .bss (__bss_end__), so crt0 zeroes it; .psram_bss is outside that range and
    #    q2_psram_init zeroes it once PSRAM is mapped.
    string(REPLACE "    .bss (NOLOAD) : {"
"    .sram_bss (NOLOAD) : {
        . = ALIGN(8);
        __sram_bss_start = .;
        *(.sram_bss .sram_bss.*)
        /* the renderer objects that hold only small state (edge, span, polygon and
           particle variables, about 11 KB): in PSRAM they were evicted from the XIP
           cache by the pixel traffic and reloaded per span or pixel */
        *libq2.a:r_poly.c.o(.bss .bss.* COMMON)
        *libq2.a:r_polyse.c.o(.bss .bss.* COMMON)
        *libq2.a:r_edge.c.o(.bss .bss.* COMMON)
        *libq2.a:r_misc.c.o(.bss .bss.* COMMON)
        *libq2.a:r_aclip.c.o(.bss .bss.* COMMON)
        *libq2.a:r_bsp.c.o(.bss .bss.* COMMON)
        *libq2.a:r_part.c.o(.bss .bss.* COMMON)
        *libq2.a:r_light.c.o(.bss .bss.* COMMON)
        *libq2.a:r_draw.c.o(.bss .bss.* COMMON)
        /* and the rest of these, whose large arrays are marked Q2_PSRAM_BSS */
        *libq2.a:r_main.c.o(.bss .bss.* COMMON)
        *libq2.a:r_alias.c.o(.bss .bss.* COMMON)
        *libq2.a:r_rast.c.o(.bss .bss.* COMMON)
        *libq2.a:r_scan.c.o(.bss .bss.* COMMON)
        . = ALIGN(8);
        __sram_bss_end = .;
    } > RAM

    .psram_bss (NOLOAD) : {
        . = ALIGN(8);
        __psram_bss_start = .;
        *(.psram_bss.*)
        *libq2.a:*(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(32);
        __psram_bss_end = .;
        __psram_heap_start = .;
    } > PSRAM
    __psram_heap_end = ORIGIN(PSRAM) + LENGTH(PSRAM);

    .bss (NOLOAD) : {" _ld "${_ld}")
    if (NOT _ld MATCHES "__psram_bss_start")
        message(FATAL_ERROR "psram_linker: could not add .psram_bss to ${_src}")
    endif()

    # 5) newlib's memcpy/memset/strcmp in SRAM. The SDK script already keeps
    #    *libc.a:*lib_a-mem*.o out of flash so that .data picks them up, but this
    #    toolchain names the objects libc_a-*.o inside libg.a, which the pattern
    #    misses. memcpy alone was 1.4% of a frame from flash.
    if (_sdk23)
        # SDK >= 2.3.0 no longer lists any libc object in the two EXCLUDE_FILE
        # lists (default_text_excludes.incl, default_rodata_excludes.incl):
        # append all of them, the SDK's old pattern included.
        string(REGEX REPLACE "EXCLUDE_FILE\\(([^)]*)\\)"
            "EXCLUDE_FILE(\\1 *libc.a:*lib_a-mem*.o *libc.a:*libc_a-mem*.o *libg.a:*libc_a-mem*.o *libc.a:*libc_a-strcmp.o *libg.a:*libc_a-strcmp.o)"
            _ld "${_ld}")
        string(REGEX MATCHALL "EXCLUDE_FILE\\([^)]*libg\\.a:\\*libc_a-mem[^)]*\\)" _hits "${_ld}")
        list(LENGTH _hits _n)
        if (NOT _n EQUAL 2)
            message(FATAL_ERROR "psram_linker: could not move memcpy/memset to SRAM in ${_src}")
        endif()
    else()
        string(REPLACE "*libc.a:*lib_a-mem*.o"
            "*libc.a:*lib_a-mem*.o *libc.a:*libc_a-mem*.o *libg.a:*libc_a-mem*.o *libc.a:*libc_a-strcmp.o *libg.a:*libc_a-strcmp.o"
            _ld "${_ld}")
        if (NOT _ld MATCHES "libg\\.a:\\*libc_a-mem")
            message(FATAL_ERROR "psram_linker: could not move memcpy/memset to SRAM in ${_src}")
        endif()
    endif()

    if (_sdk23)
        _q2_ld_fragments_write(${target} "${_ld}")
    else()
        file(WRITE "${_dst}" "${_ld}")
        pico_set_linker_script(${target} "${_dst}")
    endif()
endfunction()
