#!/bin/sh
# ====================================================================================
# pico-quake2 build-all script
#
# Builds every released file into releases/. This is the whole body of the release
# workflow (.github/workflows/BuildAndRelease.yml), so running it locally rehearses
# a release.
#
#   export PICO_SDK_PATH=/path/to/pico-sdk
#   export PICO_PIO_USB_PATH=/path/to/Pico-PIO-USB
#   ./buildAll.sh
#
# Optional:
#   Q2_RELEASE_VERSION=v0.1-alpha   version stamp in the UF2 binary info (picotool info)
#   Q2_DEMO_EXE=/path/to/q2-314-demo-x86.exe
#                                   use this demo installer instead of downloading it
#
# Released:
#   pico-quake2_<board>.uf2         the firmware, standalone build, one per board
#   pico-quake2_demo_sdcard.zip     the shareware demo data for the SD card, with the
#                                   demo's licence (tools/make_demo_zip.py)
#
# pico-bootLoader builds (<tag>-build-forbootloader.sh) are not released here: the
# pico-bootLoader repo builds them from this repo's latest release tag
# (build_emulators.sh) and ships them in its SD-card archive.
# ====================================================================================
cd "$(dirname "$0")" || exit 1

RELEASE_BOARDS="fruitjam adafruitdvisd"

# --- tools -------------------------------------------------------------------
if [ -z "$PICO_SDK_PATH" ] || [ ! -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
    echo "PICO_SDK_PATH is not set or is not a Pico SDK checkout" >&2
    exit 1
fi
# Every board so far drives USB host over PIO.
if [ -z "$PICO_PIO_USB_PATH" ]; then
    echo "PICO_PIO_USB_PATH is not set" >&2
    exit 1
fi
for TOOL in ninja picotool python3; do
    if ! command -v $TOOL > /dev/null 2>&1; then
        echo "$TOOL could not be found" >&2
        exit 1
    fi
done

[ -d releases ] && rm -rf releases
mkdir releases || exit 1

# --- firmware ------------------------------------------------------------------
# A fresh build directory per release build: CMake options are sticky in
# CMakeCache.txt, so reusing a development tree could carry options such as
# Q2_CPU_KHZ or Q2_ZBUFFER_SRAM into a release.
for TAG in $RELEASE_BOARDS; do
    echo ""
    echo "=================== $TAG ==================="
    BUILD="build_release_${TAG}"
    rm -rf "$BUILD"
    BUILD="$BUILD" CMAKE_ARGS="${CMAKE_ARGS} -DQ2_RELEASE_VERSION=${Q2_RELEASE_VERSION:-dev}" \
        ./${TAG}-build.sh || exit 1
    cp "$BUILD/src/pico/quake2.uf2" "releases/pico-quake2_${TAG}.uf2" || exit 1
done

# --- demo data -----------------------------------------------------------------
echo ""
echo "=================== demo data ==================="
python3 -I tools/make_demo_zip.py releases/pico-quake2_demo_sdcard.zip $Q2_DEMO_EXE || exit 1

# -------------------------------------------------------------------------------
echo ""
echo "=================== releases/ ==================="
for UF2 in releases/*.uf2; do
    ls -l "$UF2"
    picotool info "$UF2"
    echo ""
done
ls -l releases/*.zip
echo "$(ls releases | wc -l) files in releases/"
