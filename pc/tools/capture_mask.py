#!/usr/bin/env python3
"""Are the pictures that differ because screen copies are sampled from the capture confined to the capture's draws?

    BUILD_COMPARE_KEEP=K BUILD_COMPARE_LOGS=L python3 pc/tools/build_compare.py NEW NEW --menus ROUTE \\
        --setting 'screen_copy ps1' --new-env ROOD_SCREEN_COPY=1 --new-env ROOD_CAPTURE_LOG=1
    python3 pc/tools/capture_mask.py K L

build_compare (--control: its three games; ROOD_SCALE=4: the target's own resolution, which build_compare's lean default 1x would hide) plays the route in three games (old, old again, new: the same build, screen_copy ps1 and native) and keeps each
sampled frame (<pid>-<n>.ppm) with the game's vsync (<pid>-<n>.vsync); ROOD_CAPTURE_LOG makes the new build log each triangle of
a primitive drawn from the capture (`[capture] v<vsync> x,y`, VRAM pixels). For each sampled frame the old and the new pictures
differ in, this reports the pixels outside the boxes of the capture triangles drawn within a few vsyncs of it (the render thread
may lag the game's count): 0 everywhere means a screen copy at the target's resolution changed what the screen copies draw and
nothing else. 4:3 only (the log's coordinates are the game's). Exit 0 when no frame has a differing pixel outside its mask."""
import collections
import glob
import pathlib
import re
import sys

import numpy as np
from PIL import Image

WINDOW = 8  # vsyncs either side of a sample (the game draws a frame every two or four vsyncs; the picture shown is the last drawn)


def main():
    keep, logs = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    pids = sorted({f.name.split("-")[0] for f in keep.glob("*.ppm")}, key=int)
    if len(pids) != 3:
        sys.exit(f"{len(pids)} games' frames in {keep}, want 3 (old, old again, new)")
    old, control, new = pids
    boxes = collections.defaultdict(list)  # vsync -> triangle corner lists
    corners = collections.defaultdict(list)
    for log in sorted(logs.glob(f"{new}-*.log")):
        for line in log.read_text(errors="replace").splitlines():
            m = re.match(r"\[capture\] v(\d+) (-?[\d.]+),(-?[\d.]+)", line)
            if m:
                corners[int(m.group(1))].append((float(m.group(2)), float(m.group(3))))
    total = differing = outside = 0
    worst = []
    for shot in sorted(keep.glob(f"{new}-*.ppm")):
        n = shot.stem.split("-")[1]
        a = np.asarray(Image.open(keep / f"{old}-{n}.ppm").convert("RGB")).astype(int)
        b = np.asarray(Image.open(shot).convert("RGB")).astype(int)
        c = np.asarray(Image.open(keep / f"{control}-{n}.ppm").convert("RGB")).astype(int)
        total += 1
        if (np.abs(a - c).max(axis=2) > 0).any():
            print(f"frame {n}: the control differs: not comparable")
            continue
        d = np.abs(a - b).max(axis=2) > 0
        if not d.any():
            continue
        differing += 1
        vsync = int((keep / f"{new}-{n}.vsync").read_text())
        mask = np.zeros_like(d)
        points = [p for v in range(vsync - WINDOW, vsync + WINDOW + 1) for p in corners.get(v, [])]
        for i in range(0, len(points) - 2, 3):  # (the triangles' boxes; a quad's two make its box)
            base = 320 if min(p[0] for p in points[i:i + 3]) >= 320 else 0  # (the frame buffer the triangle is in)
            xs = [p[0] - base for p in points[i:i + 3]]
            ys = [p[1] for p in points[i:i + 3]]
            x0, x1 = int(np.floor(min(xs))) - 1, int(np.ceil(max(xs))) + 1
            y0, y1 = int(np.floor(min(ys))) - 1, int(np.ceil(max(ys))) + 1
            mask[max(y0, 0):max(y1, 0), max(x0, 0):max(x1, 0)] = True
        out = int((d & ~mask).sum())
        outside += out > 0
        if out:
            ys, xs = np.nonzero(d & ~mask)
            worst.append((out, n, vsync, (xs.min(), ys.min(), xs.max(), ys.max())))
    print(f"{total} frames, {differing} differ, {outside} with a differing pixel outside the capture draws")
    for out, n, vsync, box in sorted(worst, reverse=True)[:8]:
        print(f"  frame {n} (vsync {vsync}): {out} pixels outside, in {box}")
    return 1 if outside else 0


if __name__ == "__main__":
    sys.exit(main())
