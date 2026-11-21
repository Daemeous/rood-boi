#!/usr/bin/env python3
"""The battle targeting sphere's wire drawn at the output's own resolution (pc/src/gpu_hw.c pc_hw_line, ui_wire_width; gitea #19).

The sphere (Circle twice with the weapon out and no target in range) is a polyline of game line packets between whole-pixel ends. Its draw
item carries the GTE's unrounded ends (draw_items.h PC_ITEM_WIRE), and pc_hw_line draws a thin, feathered line from them: ui_wire_width
output pixels across (0: the game's band, a native pixel across).

    python3 pc/tests/wire_sphere_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Checks, at 2560x1440 (4:3, the hardware target at several times native):
- the sphere's wire is on screen (green pixels) at ui_wire_width 0, 2 and 4;
- the lines get thinner as the width falls (the median run of green across a line, in output pixels), and at width 2 are thinner than a native pixel (6 output pixels here);
- the setting is an RPC graphics key and reads back."""
import argparse
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import editor_selection_smoke as es  # noqa: E402

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)
    return ok


def green(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(int)
    return (a[:, :, 1] > 50) & (a[:, :, 1] - a[:, :, 0] > 22) & (a[:, :, 1] - a[:, :, 2] > 22)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(es.pinned_card()))
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "wire_sphere"
    out.mkdir(parents=True, exist_ok=True)
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    (out / "settings.cfg").write_text("ui_menus 0\nui_text font\ninterpolate 0\nwidescreen 0\naspect 0\n")
    shutil.copyfile(args.card, out / "card1.mcd")
    env = {"ROOD_SETTINGS": str(out / "settings.cfg"), "ROOD_WINDOW": "2560x1440", "ROOD_AUDIO": "none", "ROOD_OUT": str(out),
           "ROOD_LIGHTS": str(out / "lights"), "ROOD_PREFABS": str(out / "prefabs"), "ROOD_SCALE": "4"}
    g = launch_at("save_room", card=str(out / "card1.mcd"), fast=True, log=out / "game.log", extra_env=env)
    try:
        es.goto(g, 9, 0)
        g.run(60)
        g.press("circle", vsyncs=3, settle=60)
        g.press("circle", vsyncs=3, settle=2)
        shots = {}
        for width in (0, 1, 2, 4):
            g.call("graphics", ui_wire_width=width)
            g.run(24)  # (the sphere swells for a few frames)
            g.wait_presents(2)
            path = out / f"width{width}.ppm"
            g.call("present_dump", path=str(path))
            for _ in range(50):  # (the dump is written by the presenting thread)
                if path.exists() and path.stat().st_size > 0:
                    break
                g.run(2)
                g.wait_presents(1)
            shots[width] = green(path)
        got = g.call("graphics")
        check(abs(float(got.get("ui_wire_width", -1)) - 4) < 1e-3, f"ui_wire_width is an RPC graphics key and reads back ({got.get('ui_wire_width')})")
    finally:
        g.close()
    count = {w: int(m.sum()) for w, m in shots.items()}
    check(all(c > 500 for c in count.values()), f"the sphere's wire is on screen at every width ({count})")
    def median_run(m):
        runs = []
        for row in m[::3]:
            edges = np.flatnonzero(np.diff(np.concatenate(([0], row.astype(int), [0]))))
            runs += [edges[i + 1] - edges[i] for i in range(0, len(edges) - 1, 2)]
        return float(np.median(runs)) if len(runs) > 20 else 0.0

    thick = {w: median_run(m) for w, m in shots.items()}
    print("median horizontal run across a line, output pixels:", thick)
    check(thick[1] <= thick[2] < thick[4], f"thicker as the width grows ({thick})")
    check(0 < thick[2] < 6, f"width 2 is thinner than a native pixel (6 output pixels here: {thick[2]})")
    check(thick[2] <= 8, f"width 2: a line is a few output pixels across ({thick[2]})")
    print(f"{len(failures)} failures" if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
