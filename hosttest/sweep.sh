#!/bin/bash
# Per-map budget sweep: loads each map with a cold flash asset cache, runs some frames
# of gameplay, and reports what it needs.
#
#   hosttest/sweep.sh <basedir> [frames] [maps...]      (default: demo1 demo2 demo3)
#   Q2_FLASH_KB=6656 hosttest/sweep.sh <basedir>        (a smaller cache region)
#
# Output: CSV on stdout. PSRAM figures include the engine's static data; the target is
# a peak of at most 7680 KB (7.5 MB of the 8 MB chip). "fits" is no when the level's
# assets did not fit even after the cache was emptied, rebuilt when they fit only after
# that. The per-category columns (KB in 256-byte pages) come from hosttest/fcdump.py.
HERE="$(cd "$(dirname "$0")" && pwd)"
BASEDIR="${1:?usage: sweep.sh <basedir> [frames] [maps...]}"
FRAMES="${2:-300}"
shift $(($# < 2 ? $# : 2))
MAPS="${*:-demo1 demo2 demo3}"
FLASH="$(mktemp)"
trap 'rm -f "$FLASH"' EXIT

echo "map,region_kb,fits,static_kb,heap_peak_kb,psram_peak_kb,flash_cache_kb,flash_assets,first_visit_flash_s,entries,pages_kb,textures_kb,models_kb,skins_kb,sprites_kb,pics_kb,env_kb,sounds_kb,other_kb,md2_glcmds_kb"
for map in $MAPS; do
	rm -f "$FLASH"
	out=$(Q2_FLASH_FILE="$FLASH" "$HERE/q2_host" "$BASEDIR" "$FRAMES" 0 /tmp/q2sweep +map "$map" 2>&1)
	static=$(echo "$out" | sed -n 's/.*PSRAM model: \([0-9]*\) KB static.*/\1/p')
	heap=$(echo "$out" | sed -n 's/.*heap peak \([0-9]*\) KB.*/\1/p')
	peak=$(echo "$out" | sed -n 's/.*PSRAM peak \([0-9]*\) KB.*/\1/p')
	assets=$(echo "$out" | sed -n 's/.*flash cache: \([0-9]*\) assets.*/\1/p')
	cache=$(echo "$out" | sed -n 's/.*flash cache: [0-9]* assets, \([0-9]*\) KB used.*/\1/p')
	region=$(echo "$out" | sed -n 's/.*flash cache: [0-9]* assets, [0-9]* KB used of \([0-9]*\) KB.*/\1/p')
	secs=$(echo "$out" | sed -n 's/.*programmed, ~\([0-9.]*\) s on the device.*/\1/p')
	fits=yes
	echo "$out" | grep -q "Rebuilding the flash asset cache" && fits=rebuilt
	echo "$out" | grep -q "do not fit in the flash asset cache" && fits=no
	dump=$(python3 -I "$HERE/fcdump.py" --csv "$FLASH" 2>/dev/null)
	echo "$map,$((${region:-0} + 256)),$fits,${static:-?},${heap:-FAILED},${peak:-?},${cache:-?},${assets:-?},${secs:-?},${dump:-?}"
done
