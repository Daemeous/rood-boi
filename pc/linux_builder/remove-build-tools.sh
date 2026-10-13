#!/usr/bin/env bash
# Deletes the builder's container image and its build volume (the source and build, several
# GB). Your rood-boi folder, saves and settings are kept; building again sets them up again.
set -euo pipefail
engine=$(command -v podman || command -v docker) || { echo "Neither Podman nor Docker is installed: nothing to remove."; exit 0; }
read -rp "Delete the rood-boi build tools and build files? [y/N] " a
[[ "${a,,}" == y* ]] || exit 0
"$engine" volume rm -f rood-boi-build || true
"$engine" rmi -f localhost/rood-boi-builder:4 || true
echo "Done."
