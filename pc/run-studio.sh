#!/usr/bin/env bash
# Build (unless --no-build) and start rood-studio, the model studio with no game (pc/docs/plans/2026-11-24-standalone-studio-design.md): the same
# renderer, lighting and materials as the game, the models read from the disc. Options after it go to the studio: --character N, --enemy N, --weapon N,
# --look neutral|flat. Linux, the 64-bit Vulkan build only.
#
# Its RPC socket is pc/build/boot/rpc-studio.sock, beside the game's (pc/run-game.sh: rpc-live.sock): the live paint bridge (pc/tools/paint_live.py,
# `paint_roundtrip.py live`) pushes strokes to whichever of the two runs, or both, so a model painted in Aseprite shows in the studio and in the
# game that is running beside it.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
build=1
args=()
for arg in "$@"; do
    case "$arg" in
    --no-build) build=0 ;;
    *) args+=("$arg") ;;
    esac
done
if ((build)); then
    python3 pc/tools/build_boot.py --arch 64
fi
binary=pc/build/boot64/rood-studio
export ROOD_RENDERER="${ROOD_RENDERER:-vulkan}"
if [[ "${ROOD_GPU:-}" != integrated && -e /proc/driver/nvidia/version ]]; then
    export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia
    nvidia_egl=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
    if [[ -e $nvidia_egl && -z "${__EGL_VENDOR_LIBRARY_FILENAMES:-}" ]]; then
        export __EGL_VENDOR_LIBRARY_FILENAMES=$nvidia_egl
    fi
fi
export ROOD_GI="${ROOD_GI:-1}"
export ROOD_SETTINGS="${ROOD_SETTINGS:-pc/user/settings.cfg}"
export ROOD_RPC="${ROOD_RPC:-pc/build/boot/rpc-studio.sock}"
rm -f "$ROOD_RPC"
mkdir -p pc/build/boot "$(dirname "$ROOD_RPC")"
exec "$binary" --window "${args[@]}"
