#!/bin/bash
# Programs an ELF into the board over a CMSIS-DAP probe and checks that it booted.
#
#   tools/flash.sh <elf> [uart-log]
#
# With a uart-log (a file that a capture of the board's UART is appending to, e.g.
# `cat /dev/ttyACM0 >> uart.log`), waits up to 30 s for a new "clk_sys" boot line and
# fails if none appears: the reset after programming does not always take over a
# flaky USB link, and the old image keeps running.
#
# A probe reset (SYSRESETREQ) resets neither the clocks nor the QMI, so after
# `reset halt` clk_sys still runs from PLL_SYS at the image's 384/504 MHz and
# OpenOCD's flash algorithm would drive the bootrom's flash routines that fast.
# clk_sys is switched to clk_ref (12 MHz XOSC) first; the glitchless mux makes
# this safe on a halted core.
set -e
ELF="${1:?usage: flash.sh <elf> [uart-log]}"
LOG="$2"
OCD="openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c \"adapter speed 2000\""
CLK_SYS_CTRL=0x4001003c

count_boots() {
	[ -n "$LOG" ] || { echo 0; return; }
	tr -d '\000' < "$LOG" | grep -ac "^clk_sys"	# prints 0 (and fails) when there is none
	true
}

before=$(count_boots)
out=$(eval $OCD -c "\"init; reset halt; mww $CLK_SYS_CTRL 0; sleep 10; program $ELF verify; reset run; exit\"" 2>&1) || true
if ! grep -q "Verified OK" <<<"$out"; then
	grep -vE "busy command|late transfer" <<<"$out" | tail -5 >&2
	echo "flash.sh: programming failed" >&2
	exit 1
fi
echo "flash.sh: programmed and verified"
[ -z "$LOG" ] && exit 0

for i in $(seq 30); do
	if [ "$(count_boots)" -gt "$before" ]; then
		echo "flash.sh: booted: $(tr -d '\000' < "$LOG" | grep -a "^clk_sys" | tail -1)"
		exit 0
	fi
	sleep 1
done
echo "flash.sh: no boot line within 30 s; resetting again" >&2
eval $OCD -c "\"init; reset run; exit\"" > /dev/null 2>&1 || true
for i in $(seq 20); do
	[ "$(count_boots)" -gt "$before" ] && { echo "flash.sh: booted after second reset"; exit 0; }
	sleep 1
done
echo "flash.sh: the board did not boot the new image" >&2
exit 1
