#!/usr/bin/env bash
# Remove a worktree made by pc/tools/new_worktree.sh, with its builds and test
# outputs; its branch stays (delete it with git branch -d when merged).
#
#   pc/tools/remove_worktree.sh NAME [--force]
#
# Refuses while the worktree has uncommitted changes to tracked files, unless
# --force. Never touches the main checkout or the shared build cache. Files a
# Docker run left owned by root (commit-check without -u) cannot be removed by
# you; the script then names the command that removes them.
set -euo pipefail
if [ $# -lt 1 ] || [ "${1#-}" != "$1" ]; then
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
name=$1 force=0
[ "${2:-}" = --force ] && force=1
case "$name" in
    */*|.*|*..*) echo "remove_worktree.sh: NAME must be a plain directory name" >&2; exit 2 ;;
esac
common=$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)
main=$(dirname "$common")
dir=$main/.claude/worktrees/$name
if [ ! -d "$dir" ]; then
    echo "remove_worktree.sh: no worktree $dir" >&2
    exit 1
fi
if [ "$(cd "$dir" && pwd -P)" = "$(cd "$main" && pwd -P)" ]; then
    echo "remove_worktree.sh: $dir is the main checkout" >&2
    exit 1
fi
if [ $force = 0 ] && [ -n "$(git -C "$dir" status --porcelain --untracked-files=no)" ]; then
    echo "remove_worktree.sh: $dir has uncommitted changes (commit them, or pass --force)" >&2
    git -C "$dir" status --short --untracked-files=no >&2
    exit 1
fi
# The input symlinks first (rm -r never follows them; this keeps that obvious).
for input in data disks; do
    [ -L "$dir/$input" ] && rm "$dir/$input"
done
if ! rm -rf "$dir" 2>/dev/null; then
    echo "remove_worktree.sh: some files in $dir are not yours (a Docker run as root); remove them with" >&2
    echo "  docker run --rm -v \"$dir:/w\" rood-reverse-dev:local rm -rf /w/build /w/pc/build" >&2
    echo "then run this again." >&2
    exit 1
fi
git -C "$main" worktree prune
echo "removed $dir (its branch is kept)"
