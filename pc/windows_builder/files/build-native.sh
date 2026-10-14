#!/bin/bash
# rood-boi builder, native Windows side: runs in the builder's own MSYS2 (MINGW64), without WSL.
# Started by build.ps1 -Native, which has set up MSYS2, a portable Python and the PS1 build tools.
#   build-native.sh <output dir> <disc .bin> <repository> <branch> <merlose 0/1>
#                   <max stat items 0/1> <choosable bonus wheel 0/1>
# Environment: ROOD_NATIVE = the folder with msys64, python and tools (Windows path).
set -euo pipefail

OUT=$(cygpath -u "$1") DISC=$(cygpath -u "$2") REPO=$3 BRANCH=$4 MERLOSE=$5 MAXSTATS=$6 WHEEL=$7
NATIVE=$(cygpath -u "$ROOD_NATIVE")
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$NATIVE/rood-boi
PYTHON=$NATIVE/python/tools/python.exe
TOOLS=$NATIVE/tools
PLAY="$OUT/rood-boi"
export PATH="$TOOLS/bin:$PATH"
exec > >(tee -a "$OUT/build.log") 2>&1
echo "=== $(date) (native) ==="

step() { echo; echo "== $* =="; }

step "2/5 Source code ($REPO, $BRANCH)"
if [ -d "$SRC/.git" ]; then
    cd "$SRC"
    git remote set-url origin "$REPO"
    git fetch -q origin "$BRANCH"
    git checkout -q -B "$BRANCH" FETCH_HEAD
    git reset -q --hard FETCH_HEAD
    git submodule update -q --init --recursive
else
    git clone -q --recursive -b "$BRANCH" "$REPO" "$SRC"
    cd "$SRC"
fi
git log -1 --format='%h %s'

# The decompilation's Makefiles download Linux programs (the PS1 compilers, mkpsxiso, xdelta):
# put Windows ones where they look instead.
for v in 2.7.2-psx 2.7.2-cdk 2.8.1-psx; do
    mkdir -p tools/old-gcc/$v
    cmp -s "$TOOLS/old-gcc/$v/cc1.exe" tools/old-gcc/$v/cc1.exe || cp "$TOOLS/old-gcc/$v/cc1.exe" tools/old-gcc/$v/
done
if [ ! -f tools/mkpsxiso/dumpsxiso.exe ]; then
    mkdir -p tools/mkpsxiso
    curl -fsSL -o /tmp/mkpsxiso.zip https://github.com/Lameguy64/mkpsxiso/releases/download/v2.30/mkpsxiso-2.30-win64.zip
    unzip -q -j -o /tmp/mkpsxiso.zip -d tools/mkpsxiso
fi
if [ ! -f tools/xdelta/xdelta3.exe ]; then
    mkdir -p tools/xdelta
    curl -fsSL -o /tmp/xdelta.zip https://github.com/jmacd/xdelta/releases/download/v3.2.2/xdelta3-3.2.2-windows-x86_64.zip
    unzip -q -j -o /tmp/xdelta.zip -d tools/xdelta
fi
cp "$HERE/native-helpers/xdelta3-inplace" tools/xdelta/
# The decompilation's Python tools, in their own environment (Windows CPython: PyPI has its
# packages ready-built); sitecustomize.py keeps their piped output in Unix line endings.
if [ ! -x tools/python/Scripts/python.exe ] || [ requirements.txt -nt tools/python ]; then
    echo "setting up the Python tools"
    "$PYTHON" -m venv tools/python
    tools/python/Scripts/python.exe -m pip install --quiet --disable-pip-version-check -r requirements.txt
fi
cp "$HERE/native-helpers/sitecustomize.py" tools/python/Lib/site-packages/
touch tools/python tools/.sysdeps
VPY=tools/python/Scripts/python.exe

step "3/5 Your disc, extracted and checked"
mkdir -p disks
if ! cmp -s "$DISC" disks/SLUS-01040.bin; then
    echo "copying $(basename "$DISC")"
    cp "$DISC" disks/SLUS-01040.bin
fi
if ! make -j"$(nproc)" PYTHON="$PYTHON" VPYTHON=$VPY XDELTA=tools/xdelta/xdelta3-inplace > "$NATIVE/make.log" 2>&1; then
    tail -25 "$NATIVE/make.log"
    echo
    echo "The decompilation's build failed. If it says files don't match, your .bin isn't a clean"
    echo "Vagrant Story (USA) image (it must be unmodified: no Zenith, Randomizer or other patches)."
    exit 1
fi
tail -2 "$NATIVE/make.log"

callo=()
[ "$MAXSTATS" = 1 ] || callo+=(--random-stat-items)
[ "$WHEEL" = 1 ] || callo+=(--spin-bonus-wheel)
mkdir -p "$PLAY/disks"
step "4/5 rood.exe (this is the long part)"
$VPY pc/tools/build_boot.py --target windows
dist=()
if [ "$MERLOSE" = 1 ]; then
    step "4/5 rood-callo.exe, with Merlose"
    $VPY pc/tools/build_boot.py --target windows --callo "${callo[@]}"
    dist+=(--callo)
fi
step "5/5 The play folder"
if ! cmp -s disks/SLUS-01040.bin "$PLAY/disks/SLUS-01040.bin"; then
    cp disks/SLUS-01040.bin "$PLAY/disks/SLUS-01040.bin"
fi
$VPY pc/tools/windows_dist.py "$(cygpath -w "$PLAY")" "${dist[@]}"
if [ "$MERLOSE" != 1 ]; then
    rm -f "$PLAY/rood-callo.exe" "$PLAY/play-callo.bat"
fi
echo "built from $(git log -1 --format=%h)"
