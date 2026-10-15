#!/usr/bin/env python3
"""Check shipped material maps before they are published (pc/tools/publish.sh).

    python3 pc/tools/check_material_maps.py [--rev REV] PATH...

Each PATH (repository-relative) under pc/assets/materials/ must be
<texture key>/<channel>.png: a 10-hex-digit content key, a channel from the
allowed list (pc/src/materials.cpp known_channel), and a single-channel PNG
(grayscale, 8 bits or fewer per texel, at most 256 x 256: a hand-made map
over one texture, never the texture's own colours). The file is read from
REV (default: the working tree). Exit 1 with a message per bad file.
Design: pc/docs/Materials.md.
"""
import argparse
import pathlib
import re
import struct
import subprocess
import sys

ALLOWED = {"emissive"}
PATTERN = re.compile(r"^pc/assets/materials/([0-9a-f]{10})/([a-z0-9_]+)\.png$")


def read(path, rev):
    if rev:
        return subprocess.run(["git", "show", f"{rev}:{path}"], check=True, capture_output=True).stdout
    return pathlib.Path(path).read_bytes()


def problem(path, data):
    m = PATTERN.match(path)
    if not m:
        return "not pc/assets/materials/<10-hex-digit texture key>/<channel>.png"
    if m.group(2) not in ALLOWED:
        return f"channel {m.group(2)!r} is not one of {sorted(ALLOWED)}"
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return "not a PNG"
    width, height, depth, colour = struct.unpack(">IIBB", data[16:26])
    if colour != 0:
        kinds = {2: "RGB", 3: "palette", 4: "grayscale + alpha", 6: "RGBA"}
        return (f"{kinds.get(colour, colour)} PNG: a map must be single-channel grayscale "
                "(import it in the editor, which writes it so)")
    if depth > 8:
        return f"{depth} bits per texel (8 at most)"
    if not (0 < width <= 256 and 0 < height <= 256):
        return f"{width} x {height} (a texture's box is at most 256 x 256)"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rev", help="read the files from this revision (default: the working tree)")
    parser.add_argument("paths", nargs="*")
    args = parser.parse_args()
    bad = 0
    for path in args.paths:
        try:
            why = problem(path, read(path, args.rev))
        except (OSError, subprocess.CalledProcessError) as e:
            why = f"unreadable ({e})"
        if why:
            print(f"!! {path}: {why}", file=sys.stderr)
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
