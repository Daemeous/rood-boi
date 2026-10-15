#!/usr/bin/env bash
# Publish main to the remotes:
#   gitea   full history (git push gitea main)
#   github  the public branch: one snapshot commit per publish with main's
#           tree, authored with the public noreply identity, after a leak scan
#           of everything that changed since the last public snapshot.
#
#   pc/tools/publish.sh "Summary of what changed"      scan, commit, push both
#   pc/tools/publish.sh --dry-run "..."                 scan only
#
# Identity comes from PUBLISH_NAME / PUBLISH_EMAIL (or git config
# publish.name / publish.email). The github remote's push refspec should be
# refs/heads/public:refs/heads/main so main itself can never reach it.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

dry=0
if [ "${1:-}" = "--dry-run" ]; then dry=1; shift; fi
message="${1:-}"
[ -n "$message" ] || { echo "usage: $0 [--dry-run] \"summary\"" >&2; exit 2; }

name="${PUBLISH_NAME:-$(git config publish.name || true)}"
email="${PUBLISH_EMAIL:-$(git config publish.email || true)}"
[ -n "$name" ] && [ -n "$email" ] || { echo "set PUBLISH_NAME/PUBLISH_EMAIL or git config publish.name/publish.email" >&2; exit 2; }

[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "commit or stash changes first" >&2; exit 1; }
git rev-parse -q --verify refs/heads/public >/dev/null || { echo "no public branch" >&2; exit 1; }
[ "$(git config --get remote.github.push || true)" = "refs/heads/public:refs/heads/main" ] ||
    { echo "remote.github.push must be refs/heads/public:refs/heads/main" >&2; exit 1; }

if git diff --quiet public main; then
    echo "public already matches main"
else
    echo "== changes since the last public snapshot"
    git diff --stat public main | tail -1
    fail=0
    # Personal data, private hosts, secrets. Extend PUBLISH_DENY (a regex) for
    # anything else that must never be published (or git config publish.deny,
    # which stays in .git/config and is never published itself).
    deny='/home/|/tmp/claude|BEGIN [A-Z ]*PRIVATE KEY|gh[pousr]_[A-Za-z0-9]{20,}'
    extra="${PUBLISH_DENY:-$(git config publish.deny || true)}"
    [ -n "$extra" ] && deny="$deny|$extra"
    # (this script is skipped: its own pattern list would match; -a: a stray
    # non-UTF-8 byte must not turn grep into "binary file matches" and hide lines)
    if git diff public main -- . ':!pc/tools/publish.sh' | grep -a '^+' | grep -ainE "$deny"; then
        echo "!! denied pattern in added lines" >&2; fail=1
    fi
    # No binaries or media: the repo ships code and data files only.
    # Vendored, licensed fonts are the only binaries allowed, and hand-made
    # material maps (pc/docs/Materials.md), which must pass their own check.
    maps='^pc/assets/materials/[^/]+/[^/]+\.png$'
    allow="^pc/third_party/fonts/[^/]+/[^/]+\.(ttf|otf)$|$maps"
    bins=$(git diff --numstat public main | awk '$1=="-"{print $3}' | grep -vE "$allow" || true)
    media=$(git diff --name-only --diff-filter=AM public main |
        grep -iE '\.(png|jpe?g|webp|gif|bmp|ppm|tim|bin|iso|cue|chd|mcd|wav|xa|str|mpd|znd|shp|wep|seq|vab|vag|obj)$' |
        grep -vE "$maps" || true)
    if [ -n "$bins$media" ]; then
        echo "!! binary or media files:" >&2; printf '%s\n' $bins $media >&2; fail=1
    fi
    # Material maps: <texture key>/<allowed channel>.png, single-channel
    # grayscale only (a texture's own colours, the game's art, never ship).
    added_maps=$(git diff --name-only --diff-filter=AM public main | grep -E "$maps" || true)
    if [ -n "$added_maps" ] && ! python3 pc/tools/check_material_maps.py --rev main $added_maps; then
        echo "!! material maps must be pc/assets/materials/<key>/<channel>.png, single-channel grayscale" >&2; fail=1
    fi
    [ "$fail" = 0 ] || { echo "leak scan failed; nothing published" >&2; exit 1; }
    echo "leak scan clean"
fi

[ "$dry" = 0 ] || { echo "dry run: nothing committed or pushed"; exit 0; }

if ! git diff --quiet public main; then
    commit=$(GIT_AUTHOR_NAME="$name" GIT_AUTHOR_EMAIL="$email" \
             GIT_COMMITTER_NAME="$name" GIT_COMMITTER_EMAIL="$email" \
             git commit-tree "main^{tree}" -p public -m "$message")
    git branch -f public "$commit"
fi
git push gitea main
git push github
