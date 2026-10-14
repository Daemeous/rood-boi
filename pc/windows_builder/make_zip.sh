#!/bin/bash
# Package the Windows builder for players:
#   pc/windows_builder/make_zip.sh <version>          -> pc/build/rood-boi-builder-<version>.zip (WSL)
#   pc/windows_builder/make_zip.sh <version> native   -> pc/build/rood-boi-native-builder-<version>.zip
#                                                        (no WSL: MSYS2, build.ps1 -Native)
set -euo pipefail
cd "$(dirname "$0")"
native=${2:-}
out=$(realpath -m "../build/rood-boi-${native:+native-}builder-$1.zip")
stage=$(mktemp -d)
dir="$stage/rood-boi-builder"
mkdir -p "$dir/files"
crlf() { sed 's/\r$//; s/$/\r/' "$1" > "$2"; }   # Windows files: Notepad, cmd, PowerShell
lf() { sed 's/\r$//' "$1" > "$2"; }               # bash scripts
crlf "rood-boi options.ini" "$dir/rood-boi options.ini"
crlf files/build.ps1 "$dir/files/build.ps1"
if [ -n "$native" ]; then
    crlf "Build rood-boi (native).bat" "$dir/Build rood-boi.bat"
    crlf "Remove build tools (native).bat" "$dir/Remove build tools.bat"
    crlf README-native.txt "$dir/README.txt"
    lf files/build-native.sh "$dir/files/build-native.sh"
    mkdir -p "$dir/files/native-helpers"
    for f in files/native-helpers/*; do lf "$f" "$dir/$f"; done
else
    for f in "Build rood-boi.bat" "Remove build tools.bat" README.txt; do crlf "$f" "$dir/$f"; done
    lf files/build.sh "$dir/files/build.sh"
fi
rm -f "$out"
python3 - "$stage" "$out" <<'EOF'
import os, sys, zipfile
stage, out = sys.argv[1:]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _, names in os.walk(stage):
        for name in sorted(names):
            path = os.path.join(root, name)
            z.write(path, os.path.relpath(path, stage))
EOF
rm -rf "$stage"
echo "$out"
