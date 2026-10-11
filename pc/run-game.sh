#!/usr/bin/env bash
# Build (unless --no-build) and play: the intro movie, then the original title
# and input flow (--skip-movies skips the movie).
# On an NVIDIA hybrid laptop/desktop it renders on the NVIDIA GPU (PRIME
# offload); ROOD_GPU=integrated keeps the default GPU. Path-traced lighting is
# on (ROOD_GI=0 turns it off). --64 plays the 64-bit build; --vulkan plays it
# with the Vulkan renderer (hardware ray tracing). Other arguments go to the game.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
build=1
arch=32
args=()
for arg in "$@"; do
    case "$arg" in
    --no-build) build=0 ;;
    --64) arch=64 ;;
    --vulkan) arch=64; export ROOD_RENDERER=vulkan ;;
    *) args+=("$arg") ;;
    esac
done
if ((build)); then
    if ((arch == 64)); then
        python3 pc/tools/build_boot.py --arch 64
    else
        python3 pc/tools/build_boot.py --battle
    fi
fi
binary=pc/build/boot/rood_battle_probe
((arch == 64)) && binary=pc/build/boot64/rood_battle_probe
if [[ "${ROOD_GPU:-}" != integrated && -e /proc/driver/nvidia/version ]]; then
    export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia
    # The same for EGL, which SDL uses on native Wayland.
    nvidia_egl=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
    if [[ -e $nvidia_egl && -z "${__EGL_VENDOR_LIBRARY_FILENAMES:-}" ]]; then
        export __EGL_VENDOR_LIBRARY_FILENAMES=$nvidia_egl
    fi
fi
export ROOD_GI="${ROOD_GI:-1}"
# Settings saved from the editor (git-ignored); tests leave this unset.
export ROOD_SETTINGS="${ROOD_SETTINGS:-pc/user/settings.cfg}"
# Developer RPC (pc/tools/rood_rpc.py) on a fixed socket, so a running game
# can be inspected: Game("pc/build/boot/rpc-live.sock").
export ROOD_RPC="${ROOD_RPC:-pc/build/boot/rpc-live.sock}"
rm -f "$ROOD_RPC"
# The game writes its display capture (loading.ppm) to pc/build/boot, which a
# 64-bit-only checkout has not created yet.
mkdir -p pc/build/boot "$(dirname "$ROOD_RPC")"
exec "$binary" --window "${args[@]}"
