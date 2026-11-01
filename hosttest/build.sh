#!/bin/bash
# Build the Linux host harness from the same engine sources as the firmware.
#   hosttest/build.sh            -> hosttest/q2_host   (-m32, ASan + UBSan)
#                                   hosttest/q2_host_sdl (the same in a window, when the
#                                   32-bit SDL2 package libsdl2-dev:i386 is installed)
#   NOSAN=1 hosttest/build.sh    -> without sanitizers (faster, for sweeps)
#   Q2_HOST_CFLAGS=-D...         -> extra compiler flags for every file
# -m32 keeps pointer-heavy structs the same size as on the ILP32 Cortex-M33.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../src/quake2"
OBJ="$HERE/build"
mkdir -p "$OBJ"

CLIENT="cl_cin cl_ents cl_fx cl_newfx cl_input cl_inv cl_main cl_parse cl_pred cl_tent
        cl_scrn cl_view console keys menu qmenu snd_adpcm snd_dma snd_mem snd_mix"
QCOMMON="cmd cmodel common crc cvar fcache files md4 net_chan pmove"
SERVER="sv_ccmds sv_ents sv_game sv_init sv_main sv_send sv_user sv_world"
OTHER="q_hunk q_system vid_lib vid_menu glob"
GAME="g_ai p_client g_cmds g_svcmds g_combat g_func g_items g_main g_misc g_monster
      g_phys g_save g_spawn g_target g_trigger g_turret g_utils g_weapon m_actor
      m_berserk m_boss2 m_boss3 m_boss31 m_boss32 m_brain m_chick m_flipper m_float
      m_flyer m_gladiator m_gunner m_hover m_infantry m_insane m_medic m_move m_mutant
      m_parasite m_soldier m_supertank m_tank p_hud p_trail p_view p_weapon q_shared
      g_chase m_flash"
REF="r_aclip r_alias r_bsp r_draw r_edge r_image r_light r_main r_misc r_model r_part
     r_poly r_polyse r_rast r_scan r_sprite r_surf"

FILES=""
for f in $CLIENT;  do FILES="$FILES client/$f.c"; done
for f in $QCOMMON; do FILES="$FILES qcommon/$f.c"; done
for f in $SERVER;  do FILES="$FILES server/$f.c"; done
for f in $OTHER;   do FILES="$FILES other/$f.c"; done
for f in $GAME;    do FILES="$FILES game/$f.c"; done
for f in $REF;     do FILES="$FILES ref_soft/$f.c"; done
FILES="$FILES net/net_loopback.c null/cd_null.c"

SAN="-fsanitize=address,undefined"
[ -n "$NOSAN" ] && SAN=""
TLSF="$HERE/../3rdparty/tlsf"
# -fwrapv: the renderer relies on wrapping signed arithmetic (fixed-point steps).
# frame pointers: the heap trace (Q2_MEMTRACE/Q2_MEMLIVE) walks the call chain
CFLAGS="-m32 -fno-pie -fno-omit-frame-pointer -g -O1 -fno-strict-aliasing -fwrapv -fno-aggressive-loop-optimizations -fcommon -Dstricmp=strcasecmp -DQ2_HOST -w $SAN -I$SRC -I$SRC/other -I$TLSF $Q2_HOST_CFLAGS"
# Engine sources get q2_force.h (heap redirect, float math) and the firmware's float
# flags; q_mem.c, TLSF and the harness do not.
ENGINE_CFLAGS="$CFLAGS -fsingle-precision-constant -fno-math-errno -include q2_force.h"
export CFLAGS ENGINE_CFLAGS SRC OBJ

compile() {
	o="$OBJ/$(echo "$1" | tr / _ | sed 's/\.c$/.o/')"
	gcc $ENGINE_CFLAGS -c "$SRC/$1" -o "$o" || { echo "FAILED: $1" >&2; exit 1; }
}
export -f compile

rm -f "$OBJ"/*.o "$OBJ"/*/*.o
echo $FILES | tr ' ' '\n' | xargs -P "$(nproc)" -I{} bash -c 'compile {}'
# savegame name tables, generated from the game sources
python3 -I "$HERE/../tools/gen_savetables.py" "$SRC/game" "$OBJ/g_savetables.c"
gcc $ENGINE_CFLAGS -I"$SRC/game" -c "$OBJ/g_savetables.c" -o "$OBJ/game_g_savetables.o"
gcc $CFLAGS -c "$SRC/other/q_mem.c" -o "$OBJ/other_q_mem.o"
gcc $CFLAGS -c "$TLSF/tlsf.c" -o "$OBJ/tlsf.o"

# Static data of the engine objects (.bss/.data/common; q_mem.o is left out because
# its host-only debug tables do not exist on the device): on the device all of this
# is placed in PSRAM, so the harness heap gets what is left of the 8 MB.
STATIC=$(for o in "$OBJ"/*.o; do [ "${o##*/}" = other_q_mem.o ] || nm -S -t d "$o"; done 2>/dev/null |
	awk '$3 ~ /^[bBdDcC]$/ && $4 !~ /asan|ubsan|^__/ {s+=$2} END {print s+0}')
echo "engine static data: $STATIC bytes"
# the harness objects go in subdirectories: "$OBJ"/*.o is the engine for both binaries
mkdir -p "$OBJ/host" "$OBJ/sdl"
gcc $CFLAGS -DQ2_ENGINE_STATIC_BYTES=$STATIC -c "$HERE/q2_host.c" -o "$OBJ/host/q2_host.o"
gcc -m32 -no-pie $SAN -o "$HERE/q2_host" "$OBJ"/*.o "$OBJ"/host/*.o -lm
echo "built $HERE/q2_host"

# q2_host_sdl, against the Debian/Ubuntu multiarch SDL2 for i386
SDL_PC=/usr/lib/i386-linux-gnu/pkgconfig
if ! SDL_CFLAGS=$(PKG_CONFIG_LIBDIR=$SDL_PC pkg-config --cflags sdl2 2>/dev/null); then
	echo "no 32-bit SDL2 (libsdl2-dev:i386): q2_host_sdl not built"
	exit 0
fi
SDL_LIBS=$(PKG_CONFIG_LIBDIR=$SDL_PC pkg-config --libs sdl2)
gcc $CFLAGS $SDL_CFLAGS -DQ2_ENGINE_STATIC_BYTES=$STATIC -DQ2_HOST_SDL -c "$HERE/q2_host.c" -o "$OBJ/sdl/q2_host.o"
gcc $CFLAGS $SDL_CFLAGS -c "$HERE/q2_sdl.c" -o "$OBJ/sdl/q2_sdl.o"
gcc -m32 -no-pie $SAN -o "$HERE/q2_host_sdl" "$OBJ"/*.o "$OBJ"/sdl/*.o $SDL_LIBS -lm
echo "built $HERE/q2_host_sdl"
