#!/usr/bin/env bash
# Start/Select button paths: leaving the attract demo at three points, Start
# through the New Game opening, Start/Select in the first battle, and the
# field menu's Data screen. Each run
# must reach its battle-frame checkpoint. Audio is recorded, not played.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
export ROOD_AUDIO="${ROOD_AUDIO:-wav:pc/build/boot/start-smoke.wav}"
status=0
run() {
    local name=$1
    shift
    if python3 pc/tests/input_smoke.py "$@" >/dev/null 2>&1; then
        echo "PASS: $name"
    else
        echo "FAIL: $name ($(grep -o 'UNIMPLEMENTED.*' pc/build/boot/new-game-test.log | head -1))"
        status=1
    fi
}
run "demo, Start at 300, New Game" --idle --frames 1800 --after 300 --keys "Return @title"
run "demo, Start at 900, New Game" --idle --frames 2400 --after 900 --keys "Return @title"
run "demo, Start at 2400, New Game" --idle --frames 4200 --after 2400 --keys "Return @title"
run "Start through the opening" --frames 6600 --advance-dialogue --after 300 \
    --keys "Return @600 Return @900 Return @1200 Return @1800 Return @2400 Return @3000 Return @4500 Return @6000 Return"
run "Start/Select in battle" --frames 8700 --advance-dialogue --after 7500 \
    --keys "Return Return @7800 Return z Return @8100 Tab Tab Return Return"
# Field menu Data opened and closed five times: each visit opens and closes the
# eight memory-card events, so closed event slots must be reused.
run "Data menu five times, then walk" --realtime --frames 3000 --advance-dialogue --after 300 \
    --keys "Return @600 Return @900 s @960 Down @1000 Down @1040 Down @1100 z @1250 x @1400 z @1550 x @1700 z @1850 x @2000 z @2150 x @2300 z @2450 x @2600 x @2700 +Right"
exit $status
