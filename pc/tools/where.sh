#!/usr/bin/env bash
# Print where the running game is, as function names. Each signal makes the
# game write a [stack] line to its own terminal; this script also resolves them
# from that terminal's output when it is passed on stdin:
#   pc/tools/where.sh            # ask the running game (5 samples)
#   pc/tools/where.sh resolve    # resolve [stack] lines read from stdin
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
bin=pc/build/boot/rood_battle_probe
if [ "${1:-}" = resolve ]; then
    grep -o '\[stack\].*' | while read -r _ addrs; do
        addr2line -f -s -e "$bin" $addrs | paste - - | awk '{printf "%s (%s) <- ", $1, $2} END {print ""}'
    done
    exit
fi
pid=$(pgrep -f "$bin" | head -1)
for _ in $(seq 5); do
    kill -USR1 "$pid"
    sleep 0.3
done
echo "Sent 5 stack requests to process $pid."
