#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
python3 pc/tools/build_boot.py
exec pc/build/boot/rood_boot --window --skip-movies "$@"
