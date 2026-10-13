#!/bin/bash
# Package the Linux builder for players (keeps the scripts executable):
#   pc/linux_builder/make_tarball.sh <version>   -> pc/build/rood-boi-linux-builder-<version>.tar.gz
# files/build.sh is shared with the Windows builder (pc/windows_builder/files/build.sh).
set -euo pipefail
cd "$(dirname "$0")"
out=$(realpath -m "../build/rood-boi-linux-builder-$1.tar.gz")
stage=$(mktemp -d)
dir="$stage/rood-boi-linux-builder"
mkdir -p "$dir/files"
cp build-rood-boi.sh remove-build-tools.sh "rood-boi options.ini" README.txt "$dir/"
cp files/Containerfile "$dir/files/"
cp ../windows_builder/files/build.sh "$dir/files/"
sed -i 's/\r$//' "$dir"/*.sh "$dir/rood-boi options.ini" "$dir/README.txt" "$dir/files/"*
chmod 755 "$dir"/*.sh "$dir/files/build.sh"
tar -czf "$out" -C "$stage" --owner=0 --group=0 rood-boi-linux-builder
rm -rf "$stage"
echo "$out"
