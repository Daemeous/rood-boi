#!/bin/bash
# rood-boi Windows builder, Linux side: runs as root in the builder's private WSL Ubuntu.
# usage: build.sh <output dir> <disc .bin> <repository> <branch> <merlose 0/1>
#                 <max stat items 0/1> <choosable bonus wheel 0/1>
set -euo pipefail
OUT=$1 DISC=$2 REPO=$3 BRANCH=$4 MERLOSE=$5 MAXSTATS=$6 WHEEL=$7
SRC=/root/rood-boi
PLAY="$OUT/rood-boi"
exec > >(tee -a "$OUT/build.log") 2>&1
echo "=== $(date) ==="

step() { echo; echo "== $* =="; }

step "1/5 Build tools (first time only)"
DEPS_STAMP=/root/.rood-boi-deps-2
if [ ! -f $DEPS_STAMP ]; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -q
    apt-get install -y -q --no-install-recommends \
        git ca-certificates build-essential ninja-build cmake pkg-config python3 python3-venv \
        python3-numpy python3-pil gdb mingw-w64 binutils-mipsel-linux-gnu clang-format unzip wget \
        libvulkan-dev libx11-dev libxext-dev libxi-dev libxcursor-dev libxfixes-dev \
        libxrandr-dev libxrender-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev \
        libdecor-0-dev libpipewire-0.3-dev libpulse-dev libasound2-dev libdbus-1-dev \
        libudev-dev libegl-dev libgl-dev
    touch $DEPS_STAMP
else
    echo "already installed"
fi

step "2/5 Source code ($REPO, $BRANCH)"
if [ -d $SRC/.git ]; then
    cd $SRC
    git remote set-url origin "$REPO"
    git fetch -q origin "$BRANCH"
    git checkout -q -B "$BRANCH" FETCH_HEAD
    git reset -q --hard FETCH_HEAD
    git submodule update -q --init --recursive
else
    git clone -q --recursive -b "$BRANCH" "$REPO" $SRC
    cd $SRC
fi
git log -1 --format='%h %s'

step "3/5 Your disc, extracted and checked"
mkdir -p disks
if ! cmp -s "$DISC" disks/SLUS-01040.bin; then
    echo "copying $(basename "$DISC")"
    cp "$DISC" disks/SLUS-01040.bin
fi
if ! make -j"$(nproc)" > /root/make.log 2>&1; then
    tail -25 /root/make.log
    echo
    echo "The decompilation's build failed. If it says files don't match, your .bin isn't a clean"
    echo "Vagrant Story (USA) image (it must be unmodified: no Zenith, Randomizer or other patches)."
    exit 1
fi
tail -2 /root/make.log

step "4/5 rood.exe (this is the long part)"
python3 pc/tools/build_boot.py --target windows
dist=()
if [ "$MERLOSE" = 1 ]; then
    step "4/5 rood-callo.exe, with Merlose"
    extra=()
    [ "$MAXSTATS" = 1 ] || extra+=(--random-stat-items)
    [ "$WHEEL" = 1 ] || extra+=(--spin-bonus-wheel)
    python3 pc/tools/build_boot.py --target windows --callo "${extra[@]}"
    dist+=(--callo)
fi

step "5/5 The play folder"
mkdir -p "$PLAY/disks"
if ! cmp -s disks/SLUS-01040.bin "$PLAY/disks/SLUS-01040.bin"; then
    cp disks/SLUS-01040.bin "$PLAY/disks/SLUS-01040.bin"
fi
python3 pc/tools/windows_dist.py "$PLAY" "${dist[@]}"
if [ "$MERLOSE" != 1 ]; then
    rm -f "$PLAY/rood-callo.exe" "$PLAY/play-callo.bat"
fi
echo "built from $(git log -1 --format=%h)"
