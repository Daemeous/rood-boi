#!/usr/bin/env bash
# rood-boi for Linux - builder: builds the game from source against your own disc image, in a
# container (Podman or Docker), into ./rood-boi. Run: ./build-rood-boi.sh [your-disc.cue]
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
image=localhost/rood-boi-builder:4
volume=rood-boi-build

say() { printf '%s\n' "$*"; }
fail() { say ""; say "PROBLEM: $*"; exit 1; }

say "rood-boi for Linux - builder"
say "============================"
say ""

# --- Options -----------------------------------------------------------------------------------
opt() {  # <key> <default>
    local v
    v=$(sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*\([^;]*\)/\1/p" "$here/rood-boi options.ini" 2>/dev/null |
        tail -1 | tr -d '\r' | sed 's/[[:space:]]*$//')
    printf '%s' "${v:-$2}"
}
yes() { [[ "${1,,}" =~ ^(y|yes|on|true|1)$ ]]; }
merlose=$(opt merlose yes) maxstats=$(opt max_stat_items yes) wheel=$(opt choosable_bonus_wheel yes)
repo=$(opt repository https://github.com/Daemeous/rood-boi.git) branch=$(opt branch callo)
say "Options (from rood-boi options.ini):"
say "  Merlose companion mod: $(yes "$merlose" && echo yes || echo no)"
if yes "$merlose"; then
    say "  max stat items: $(yes "$maxstats" && echo yes || echo no)"
    say "  choosable bonus wheel: $(yes "$wheel" && echo yes || echo no)"
fi
say ""

# --- The disc image ----------------------------------------------------------------------------
cue_bin() {  # the single .bin a .cue names, or nothing
    local files
    mapfile -t files < <(sed -n 's/^[[:space:]]*FILE[[:space:]]*"\{0,1\}\([^"]*\)"\{0,1\}[[:space:]]\{1,\}BINARY.*/\1/p' "$1" | tr -d '\r')
    [ ${#files[@]} -eq 1 ] && [ -f "$(dirname "$1")/${files[0]}" ] && printf '%s' "$(dirname "$1")/${files[0]}"
}
disc=""
if [ $# -ge 1 ]; then
    case "$1" in
    *.cue|*.CUE) disc=$(cue_bin "$1") || true; [ -n "$disc" ] || fail "$1 doesn't point to a single .bin next to it." ;;
    *) disc=$1 ;;
    esac
else
    choices=()
    declare -A used=()
    shopt -s nullglob nocaseglob
    for cue in "$here"/*.cue; do
        bin=$(cue_bin "$cue") || true
        [ -n "$bin" ] && { choices+=("$bin"); used["$bin"]=1; }
    done
    for bin in "$here"/*.bin; do
        [ -z "${used[$bin]:-}" ] && choices+=("$bin")
    done
    for f in "$here"/*.{chd,iso,pbp,ecm,zip,7z,rar}; do
        say "Note: $(basename "$f") can't be used: the build needs a raw .bin (with its .cue) of your disc."
    done
    shopt -u nullglob nocaseglob
    [ ${#choices[@]} -gt 0 ] || fail "No disc image found. Put your Vagrant Story (USA) .bin and .cue in this folder:
  $here
then run this again."
    if [ ${#choices[@]} -eq 1 ]; then
        disc=${choices[0]}
    else
        say "Found these disc images:"
        for i in "${!choices[@]}"; do say "  $((i + 1)). $(basename "${choices[$i]}")"; done
        while [ -z "$disc" ]; do
            read -rp "Which one is your clean Vagrant Story (USA)? Type 1-${#choices[@]}: " a
            [[ "$a" =~ ^[0-9]+$ ]] && ((a >= 1 && a <= ${#choices[@]})) && disc=${choices[$((a - 1))]}
        done
    fi
fi
disc=$(realpath "$disc")
[ -f "$disc" ] || fail "$disc doesn't exist."
(( $(stat -c %s "$disc") % 2352 == 0 )) || fail "$(basename "$disc") isn't a raw (2352-byte sector) .bin image."
say "Disc image: $(basename "$disc")"
say "(It has to be the unmodified USA release: no Zenith, Randomizer or other patches.)"
say ""

# --- Podman or Docker --------------------------------------------------------------------------
engine="" owner=""
if command -v podman > /dev/null; then
    engine=podman
elif command -v docker > /dev/null; then
    engine=docker
    docker info > /dev/null 2>&1 || fail "Docker is installed but you can't use it as yourself. Add yourself to the
docker group (sudo usermod -aG docker \$USER, then log out and back in), or install Podman instead."
    owner="$(id -u):$(id -g)"  # Docker's containers write files as root: hand them back
else
    . /etc/os-release 2> /dev/null || true
    case " ${ID:-} ${ID_LIKE:-} " in
    *" arch "*) cmd="sudo pacman -S podman" ;;
    *" fedora "*|*" rhel "*) cmd="sudo dnf install podman" ;;
    *" opensuse"*|*" suse "*) cmd="sudo zypper install podman" ;;
    *" debian "*|*" ubuntu "*) cmd="sudo apt install podman" ;;
    *) cmd="your package manager's podman package" ;;
    esac
    fail "The build runs in a container, and neither Podman nor Docker is installed.
Install Podman ($cmd), then run this again."
fi
avail=$(df -Pk "$here" | awk 'NR==2 {print int($4 / 1048576)}')
((avail >= 15)) || fail "The build needs about 15 GB free here (the build tools and the build); only $avail GB is free."

if ! $engine image exists "$image" 2> /dev/null && [ -z "$($engine images -q "$image" 2> /dev/null)" ]; then
    say "Setting up the build tools (first time only, about 1 GB download)..."
    $engine build -t "$image" -f "$here/files/Containerfile" "$here/files" ||
        fail "Couldn't set up the build tools (see the messages above)."
fi

say "Building. The first time takes 20-40 minutes; later builds are quicker."
say "Everything is also written to build.log."
$engine run --rm -i -v "$here:/work:z" -v "$disc:/disc.bin:ro,z" -v "$volume:/root" "$image" \
    bash /work/files/build.sh /work /disc.bin "$repo" "$branch" \
    "$(yes "$merlose" && echo 1 || echo 0)" "$(yes "$maxstats" && echo 1 || echo 0)" \
    "$(yes "$wheel" && echo 1 || echo 0)" linux "$owner" ||
    fail "The build stopped (see the messages above, and build.log). Running this again continues where it left off."

say ""
say "Done! To play, run:"
say "  $here/rood-boi/play.sh"
yes "$merlose" && say "  $here/rood-boi/play-callo.sh   (with Merlose)"
exit 0
