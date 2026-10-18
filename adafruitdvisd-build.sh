#!/bin/sh
# Build pico-quake2 for pico_shared HW_CONFIG 2: a Pimoroni Pico Plus 2 with the
# Adafruit DVI Breakout on HSTX and the Adafruit MicroSD breakout (see
# adafruitdvisd_cflags.h for the wiring).
#
# PICO_BOARD=pico2, as pico_shared's bld.sh uses for this config. The module must
# still be an RP2350B with 8 MB of PSRAM on GPIO 47, such as the Pico Plus 2.
#
# Uses PICO_SDK_PATH from the environment. BUILD=<dir> overrides the build
# directory (buildAll.sh builds releases in a fresh one).
# Artifact: build_adafruitdvisd/src/pico/quake2.uf2
set -e
TAG=adafruitdvisd
BUILD=${BUILD:-build_${TAG}}

if [ -z "${PICO_SDK_PATH}" ]; then
    echo "PICO_SDK_PATH is not set" >&2
    exit 1
fi

cmake -S . -B "$BUILD" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
    -DPICO_PLATFORM=rp2350-arm-s \
    -DPICO_BOARD=pico2 \
    -DQ2_BOARD=${TAG} \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/quake2.uf2"
