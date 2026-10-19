#!/usr/bin/env bash
# Make a worktree of this repository ready to build and test, sharing what it
# can with the main checkout:
#
#   pc/tools/new_worktree.sh NAME [BASE]
#
# creates .claude/worktrees/NAME (under the main checkout) on a new branch NAME
# from BASE (default main), then
#   - symlinks data/ and disks/ to the main checkout's (your game data: read only),
#   - copies build/, the decompilation's PS1 build (~85 MB; a copy because
#     `make commit-check` regenerates it and Docker's mipsel ld cannot follow a
#     symlink), when the main checkout has one,
#   - makes requirements.txt older than the venv and marks tools/.sysdeps, so
#     make (commit-check) neither rebuilds the Python venv nor re-runs the
#     submodule/system-dependency step.
# The third-party builds (SDL, glslang, NRD), the object cache and the Wine
# prefix are shared by every checkout ($ROOD_BUILD_CACHE, default
# ~/.cache/rood-boi; pc/tools/shared_cache.py), so the worktree's own pc/build
# holds only its builds and test outputs. pc/tools/remove_worktree.sh NAME
# removes it again.
set -euo pipefail
if [ $# -lt 1 ] || [ $# -gt 2 ] || [ "${1#-}" != "$1" ]; then
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
name=$1 base=${2:-main}
case "$name" in
    */*|.*|*..*) echo "new_worktree.sh: NAME must be a plain directory name" >&2; exit 2 ;;
esac
common=$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)
main=$(dirname "$common")
dir=$main/.claude/worktrees/$name
if [ -e "$dir" ]; then
    echo "new_worktree.sh: $dir already exists" >&2
    exit 1
fi
git -C "$main" worktree add -b "$name" "$dir" "$base"
for input in data disks; do
    if [ -e "$main/$input" ]; then
        ln -s "$(readlink -f "$main/$input")" "$dir/$input"
    fi
done
if [ -d "$main/build" ]; then
    cp -a "$main/build" "$dir/build"
fi
touch -d '2000-01-01' "$dir/requirements.txt"
touch "$dir/tools/.sysdeps"
python3 "$dir/pc/tools/shared_cache.py" | head -1
echo "worktree $dir on branch $name (from $base)"
