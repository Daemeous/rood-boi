#!/bin/bash
# rood-boi builder, Linux side: runs as root, in the Windows builder's private WSL Ubuntu or in
# the Linux builder's container.
#   build.sh deps <windows|linux>          install the packages (the Linux builder's image)
#   build.sh <output dir> <disc .bin> <repository> <branch> <merlose 0/1>
#            <max stat items 0/1> <choosable bonus wheel 0/1> <bigger inventory 0/1>
#            [windows|linux] [chown uid:gid]
set -euo pipefail

install_deps() {
    local target=$1 stamp=/usr/local/share/rood-boi-deps-4
    [ -f $stamp ] && { echo "already installed"; return; }
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -q
    apt-get install -y -q --no-install-recommends \
        git ca-certificates build-essential ninja-build cmake pkg-config python3 python3-venv \
        python3-numpy python3-pil python3-yaml gdb binutils-mipsel-linux-gnu clang-format unzip wget \
        libvulkan-dev libx11-dev libxext-dev libxi-dev libxcursor-dev libxfixes-dev \
        libxrandr-dev libxrender-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev \
        libdecor-0-dev libpipewire-0.3-dev libpulse-dev libasound2-dev libdbus-1-dev \
        libudev-dev libegl-dev libgl-dev \
        $([ "$target" = windows ] && echo mingw-w64)
    # NRD (the Vulkan denoiser) needs CMake 3.30 or newer; older Ubuntus get it from pip
    if ! cmake --version | awk 'NR==1 {split($3, v, "."); exit !(v[1] > 3 || (v[1] == 3 && v[2] >= 30))}'; then
        python3 -m venv /opt/cmake
        /opt/cmake/bin/pip install -q cmake
        ln -sf /opt/cmake/bin/cmake /usr/local/bin/cmake
    fi
    touch $stamp
}

# The downloads need the internet. WSL sometimes can't look names up (a VPN, antivirus filtering,
# or WSL's own DNS setup): then point this Ubuntu (only) at public DNS servers, which it keeps
# until WSL restarts, and stop with a plain explanation if even that doesn't connect.
ensure_network() {
    local host=archive.ubuntu.com
    if getent hosts $host > /dev/null 2>&1; then
        return
    fi
    echo "This Ubuntu can't look up $host. Trying public DNS servers (Cloudflare, Google) for it."
    rm -f /etc/resolv.conf
    printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' > /etc/resolv.conf
    if getent hosts $host > /dev/null 2>&1; then
        echo "That worked."
        return
    fi
    cat <<'EOF'

PROBLEM: the build can't reach the internet from WSL (Windows' Linux), which it needs to
download its tools. Common causes and fixes:
  * A VPN: turn it off while building.
  * Antivirus or firewall software blocking WSL: allow it, or pause it for the build.
  * WSL's networking stuck: open PowerShell, run   wsl --shutdown   and try again.
If none of these help, ask on the project's Issues page with your build.log.
EOF
    exit 1
}

if [ "$1" = deps ]; then
    install_deps "$2"
    exit 0
fi

OUT=$1 DISC=$2 REPO=$3 BRANCH=$4 MERLOSE=$5 MAXSTATS=$6 WHEEL=$7 BIGINV=${8:-0} TARGET=${9:-windows}
OWNER=${10:-}
SRC=/root/rood-boi
PLAY="$OUT/rood-boi"
exec > >(tee -a "$OUT/build.log") 2>&1
echo "=== $(date) ==="

step() { echo; echo "== $* =="; }

step "1/5 Build tools (first time only)"
ensure_network
install_deps "$TARGET"

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

callo=()
[ "$MAXSTATS" = 1 ] || callo+=(--random-stat-items)
[ "$WHEEL" = 1 ] || callo+=(--spin-bonus-wheel)
callo_dir=_callo
if [ "$BIGINV" = 1 ]; then  # the bigger bag (experimental)
    callo+=(--big-inventory container)
    callo_dir=_callo_biginv
fi
mkdir -p "$PLAY/disks"
if [ "$TARGET" = windows ]; then
    step "4/5 rood.exe (this is the long part)"
    python3 pc/tools/build_boot.py --target windows
    dist=()
    if [ "$MERLOSE" = 1 ]; then
        step "4/5 rood-callo.exe, with Merlose"
        python3 pc/tools/build_boot.py --target windows --callo "${callo[@]}"
        dist+=(--callo --callo-build "win64$callo_dir")
    fi
    step "5/5 The play folder"
    if ! cmp -s disks/SLUS-01040.bin "$PLAY/disks/SLUS-01040.bin"; then
        cp disks/SLUS-01040.bin "$PLAY/disks/SLUS-01040.bin"
    fi
    python3 pc/tools/windows_dist.py "$PLAY" "${dist[@]}"
    if [ "$MERLOSE" != 1 ]; then
        rm -f "$PLAY/rood-callo.exe" "$PLAY/play-callo.bat"
    fi
else
    step "4/5 rood (this is the long part)"
    python3 pc/tools/build_boot.py --arch 64
    if [ "$MERLOSE" = 1 ]; then
        step "4/5 rood-callo, with Merlose"
        python3 pc/tools/build_boot.py --arch 64 --callo "${callo[@]}"
    fi
    step "5/5 The play folder"
    if ! cmp -s disks/SLUS-01040.bin "$PLAY/disks/SLUS-01040.bin"; then
        cp disks/SLUS-01040.bin "$PLAY/disks/SLUS-01040.bin"
    fi
    for tree in data lighting pc/assets pc/third_party/fonts; do
        mkdir -p "$PLAY/$tree"
        cp -r "$tree/." "$PLAY/$tree/"
    done
    mkdir -p "$PLAY/pc/user" "$PLAY/pc/save" "$PLAY/pc/lights" "$PLAY/pc/build/boot" "$PLAY/licenses"
    for f in LICENSE THIRD_PARTY.md; do [ -f $f ] && cp $f "$PLAY/licenses/"; done
    strip --strip-debug -o "$PLAY/rood" pc/build/boot64/rood_battle_probe
    write_play() {  # <script> <binary> <log>
        cat > "$PLAY/$1" <<EOF
#!/usr/bin/env bash
# rood-boi (Linux builder): the Vulkan renderer, settings in pc/user/settings.cfg. Arguments go
# to the game (e.g. --skip-movies); the log goes to $3. Escape or F1 opens the editor.
cd "\$(dirname "\${BASH_SOURCE[0]}")"
if [[ "\${ROOD_GPU:-}" != integrated && -e /proc/driver/nvidia/version ]]; then
    export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia
    nvidia_egl=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
    if [[ -e \$nvidia_egl && -z "\${__EGL_VENDOR_LIBRARY_FILENAMES:-}" ]]; then
        export __EGL_VENDOR_LIBRARY_FILENAMES=\$nvidia_egl
    fi
fi
export ROOD_RENDERER="\${ROOD_RENDERER:-vulkan}" ROOD_GI="\${ROOD_GI:-1}"
export ROOD_SETTINGS="\${ROOD_SETTINGS:-pc/user/settings.cfg}"
exec ./$2 --window "\$@" 2> $3
EOF
        chmod +x "$PLAY/$1"
    }
    write_play play.sh rood rood.log
    if [ "$MERLOSE" = 1 ]; then
        strip --strip-debug -o "$PLAY/rood-callo" "pc/build/boot64$callo_dir/rood_battle_probe"
        write_play play-callo.sh rood-callo rood-callo.log
    else
        rm -f "$PLAY/rood-callo" "$PLAY/play-callo.sh"
    fi
    if [ -n "$OWNER" ]; then
        chown -R "$OWNER" "$PLAY" "$OUT/build.log"
    fi
fi
echo "built from $(git log -1 --format=%h)"
