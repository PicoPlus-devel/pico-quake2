#!/bin/sh
# Build pico-quake2 for pico_shared HW_CONFIG 17: the Pico Computer 3 (see
# picocomputer3_cflags.h for the wiring).
#
# PICO_BOARD=weact_studio_rp2350b_core, as pico_shared's bld.sh uses for this
# config. The pico-sdk ships that header.
#
# Uses PICO_SDK_PATH and PICO_PIO_USB_PATH from the environment (Pico-PIO-USB is
# linked although the USB host is the native controller here). BUILD=<dir>
# overrides the build directory (buildAll.sh builds releases in a fresh one).
# Artifact: build_picocomputer3/src/pico/quake2.uf2
set -e
TAG=picocomputer3
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
    -DPICO_BOARD=weact_studio_rp2350b_core \
    -DQ2_BOARD=${TAG} \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/quake2.uf2"
