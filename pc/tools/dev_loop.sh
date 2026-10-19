#!/usr/bin/env bash
# The engine's edit loop: put the running game back at the same moment on
# the new build.
#
#   pc/tools/dev_loop.sh            rebuild, then RPC restart the game
#   pc/tools/dev_loop.sh --no-build restart only
#   pc/tools/dev_loop.sh --reload   rebuild only the engine library, then RPC
#                                   reload: the running game swaps it in place
#                                   at its next vsync (pc/run-game.sh --reload)
#   pc/tools/dev_loop.sh --watch    --reload on every change saved under pc/src
#                                   or pc/shaders, until interrupted
#
# Restart (pc/src/dev_rpc.c, snapshot.c): RPC restart saves the
# auto-snapshot at the next vsync, runs the executable again (now the
# rebuilt one) and resumes it there; the window closes and reopens. An
# engine-only change (pc/ outside the game's own units) resumes exactly. A
# change to the game's code (src/, or a pc/src unit built like it) cannot:
# the game boots normally and its log says why.
#
# Reload (pc/docs/plans/2026-10-19-engine-reload.md): the game runs as the
# development split (pc/run-game.sh --reload: pc/build/dev64/rood_host and
# librood_engine.so). pc/tools/engine_lib.py recompiles the engine units that
# changed and relinks the library; RPC reload swaps it at the vsync boundary
# (no restart: the window, audio and game state stay). A change the host is
# built from (the game, the runtime, the save-state code, a header they
# include) falls back to build_boot.py --engine-library and RPC restart.
#
# The game's RPC socket: $ROOD_RPC, else pc/run-game.sh's
# pc/build/boot/rpc-live.sock. ROOD_BUILD_LOCK=file serialises the build
# with other builds (flock).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
build=1 mode=restart
for arg in "$@"; do
    case "$arg" in
    --no-build) build=0 ;;
    --reload) mode=reload ;;
    --watch) mode=watch ;;
    *) echo "usage: $0 [--no-build | --reload | --watch]" >&2; exit 2 ;;
    esac
done
export ROOD_RPC="${ROOD_RPC:-pc/build/boot/rpc-live.sock}"
locked() {
    if [[ -n "${ROOD_BUILD_LOCK:-}" ]]; then
        flock "$ROOD_BUILD_LOCK" "$@"
    else
        "$@"
    fi
}
# The newest saved source (seconds since the epoch): the edit a reload shows.
newest_edit() {
    find pc/src pc/shaders -type f -printf '%T@\n' 2>/dev/null | sort -n | tail -1
}

# RPC restart or reload of the running game, timed from $1 (the build's
# start) and $2 (its end); $3: restart or reload; $4: the edit's time (reload).
rpc() {
    python3 - "$@" <<'EOF'
import os
import sys
import time

sys.path.insert(0, "pc/tools")
import rood_rpc  # noqa: E402

start, built, mode = float(sys.argv[1]), float(sys.argv[2]), sys.argv[3]
edit = float(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4] else None
path = os.environ["ROOD_RPC"]
if not rood_rpc.listening(path):
    sys.exit(f"no game on {path} (start one with pc/run-game.sh --vulkan, or --reload)")
if mode == "reload":
    game = rood_rpc.Game(path)
    waiting = None
    while True:
        try:
            reply = game.call("reload")
            break
        except rood_rpc.RpcError as error:
            # Refused for now (a modern menu open, ...): asked again until it goes through.
            if "retry" not in str(error):
                sys.exit(f"reload failed: {error}")
            if str(error) != waiting:
                print(f"waiting: {error}", flush=True)
                waiting = str(error)
            time.sleep(0.5)
    now = time.time()
    shown = f", edit to the new engine on screen {now - edit:.2f} s" if edit else ""
    print(f"build {built - start:.1f} s, reload {reply['ms']:.0f} ms at vsync {reply['vsync']} "
          f"(generation {reply['generation']}){shown}", flush=True)
    sys.exit(0)
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
         else "booted normally (the snapshot was refused: see the game's log)"), flush=True)
EOF
}

# One engine edit: the library alone and RPC reload, or (a host change) the
# whole split and RPC restart. $1: the edit's time.
reload_once() {
    local start built status=0
    start=$(date +%s.%N)
    python3 pc/tools/engine_lib.py --quiet || status=$? # it takes ROOD_BUILD_LOCK itself
    built=$(date +%s.%N)
    case $status in
    0) rpc "$start" "$built" reload "${1:-}" ;;
    3) echo "nothing to reload (the library is up to date)" ;;
    2)
        echo "rebuilding the host and restarting"
        locked python3 pc/tools/build_boot.py --arch 64 --engine-library
        rpc "$start" "$(date +%s.%N)" restart
        ;;
    *) echo "the engine library did not build" >&2; return 1 ;;
    esac
}

case $mode in
restart)
    start=$(date +%s.%N)
    if ((build)); then
        locked python3 pc/tools/build_boot.py --arch 64
    fi
    rpc "$start" "$(date +%s.%N)" restart
    ;;
reload)
    reload_once "$(newest_edit)"
    ;;
watch)
    echo "watching pc/src and pc/shaders (Ctrl+C stops)"
    last=$(newest_edit)
    while true; do
        if command -v inotifywait >/dev/null; then
            inotifywait -qq -r -e close_write,moved_to,create pc/src pc/shaders || true
        else
            sleep 0.2
        fi
        now=$(newest_edit)
        [[ "$now" == "$last" ]] && continue
        sleep 0.05 # an editor's save is often more than one write
        now=$(newest_edit)
        last=$now
        reload_once "$now" || true
    done
    ;;
esac
