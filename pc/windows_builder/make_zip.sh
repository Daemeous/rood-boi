#!/bin/bash
# Package the Windows builder for players:
#   pc/windows_builder/make_zip.sh <version>   -> pc/build/rood-boi-builder-<version>.zip
set -euo pipefail
cd "$(dirname "$0")"
out=$(realpath -m "../build/rood-boi-builder-$1.zip")
stage=$(mktemp -d)
mkdir -p "$stage/rood-boi-builder/files"
# Windows files with CRLF (Notepad, cmd, PowerShell), build.sh with LF (it runs in Linux)
for f in "Build rood-boi.bat" "Remove build tools.bat" "rood-boi options.ini" README.txt files/build.ps1; do
    sed 's/\r$//; s/$/\r/' "$f" > "$stage/rood-boi-builder/$f"
done
sed 's/\r$//' files/build.sh > "$stage/rood-boi-builder/files/build.sh"
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
