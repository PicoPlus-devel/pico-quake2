#!/bin/bash
# Runs `timedemo 1` on q2demo1 over the board's UART console and prints the result.
#
#   tools/timedemo.sh <uart-log>
#
# Waits for the board to be up (it answers a cvar query on its console), stops
# the attract loop, then starts the timedemo and waits for the frames/seconds/fps
# line. The server is killed first because a demomap issued while a demo plays
# changes the map in place: in timedemo mode the server then streams the whole
# demo while the client is still reconnecting, and the run reports 0 frames.
LOG="${1:?usage: timedemo.sh <uart-log>}"
DIR="$(dirname "$0")"

up=
for i in $(seq 9); do
	"$DIR/uart_cmd.sh" "$LOG" 10 '"timedemo" is' "timedemo" > /dev/null 2>&1 && { up=1; break; }
done
[ -n "$up" ] || { echo "timedemo.sh: the board does not answer on its console" >&2; exit 1; }
"$DIR/uart_cmd.sh" "$LOG" 10 '"timedemo" is' "timedemo 1" "killserver" "timedemo" > /dev/null || exit 1
sleep 3
"$DIR/uart_cmd.sh" "$LOG" 300 "^[0-9]+ frames, .* fps" "demomap q2demo1.dm2"
