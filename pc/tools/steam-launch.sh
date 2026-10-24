#!/usr/bin/env bash
# Launches the game for a Steam non-Steam shortcut (Remote Play): Vulkan
# renderer, the existing 64-bit build (built once when there is none).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
flags=(--vulkan --no-build)
[[ -x pc/build/boot64/rood_battle_probe ]] || flags=(--vulkan)
exec pc/run-game.sh "${flags[@]}" "$@"
