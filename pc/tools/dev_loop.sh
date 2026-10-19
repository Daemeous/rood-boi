#!/usr/bin/env bash
# The engine's edit loop: rebuild the 64-bit game, then put the running game
# (pc/run-game.sh --64 or --vulkan) back at the same moment on the new build.
#
#   pc/tools/dev_loop.sh            rebuild, then RPC restart the game
#   pc/tools/dev_loop.sh --no-build restart only
#
# RPC restart (pc/src/dev_rpc.c, snapshot.c) saves the auto-snapshot at the
# next vsync, runs the executable again (now the rebuilt one) and resumes it
# there; the window closes and reopens. An engine-only change (pc/ outside the
# game's own units) resumes exactly. A change to the game's code (src/, or a
# pc/src unit built like it) cannot: the game boots normally and its log says
# why. The game's RPC socket: $ROOD_RPC, else pc/run-game.sh's
# pc/build/boot/rpc-live.sock. ROOD_BUILD_LOCK=file serialises the build
# with other builds (flock).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
build=1
for arg in "$@"; do
    case "$arg" in
    --no-build) build=0 ;;
    *) echo "usage: $0 [--no-build]" >&2; exit 2 ;;
    esac
done
export ROOD_RPC="${ROOD_RPC:-pc/build/boot/rpc-live.sock}"
start=$(date +%s.%N)
if ((build)); then
    if [[ -n "${ROOD_BUILD_LOCK:-}" ]]; then
        flock "$ROOD_BUILD_LOCK" python3 pc/tools/build_boot.py --arch 64
    else
        python3 pc/tools/build_boot.py --arch 64
    fi
fi
built=$(date +%s.%N)
python3 - "$start" "$built" <<'EOF'
import os
import sys
import time

sys.path.insert(0, "pc/tools")
import rood_rpc  # noqa: E402

start, built = float(sys.argv[1]), float(sys.argv[2])
path = os.environ["ROOD_RPC"]
if not rood_rpc.listening(path):
    sys.exit(f"no game on {path} (start one with pc/run-game.sh --vulkan)")
old = os.stat(path).st_ino
game = rood_rpc.Game(path)
before = game.status()["vsync"]
reply = game.call("restart")
game.sock.close()
# The old process removes its socket before it runs the new executable, which
# opens a new one once it has resumed (or booted): never connect to the old.
deadline = time.monotonic() + 60
gone = False
while time.monotonic() < deadline:
    try:
        gone = gone or os.stat(path).st_ino != old
    except OSError:
        gone = True
    if gone and rood_rpc.listening(path):
        try:
            game = rood_rpc.Game(path)
            status = game.status()
            break
        except OSError:
            pass
    time.sleep(0.01)
else:
    sys.exit("the game did not come back within 60 s")
now = time.time()
resumed = status["vsync"] >= reply["vsync"]
print(f"build {built - start:.1f} s, restart {now - built:.1f} s, total {now - start:.1f} s: "
      + (f"resumed at vsync {reply['vsync']} (was {before})" if resumed
         else "booted normally (the snapshot was refused: see the game's log)"))
EOF
