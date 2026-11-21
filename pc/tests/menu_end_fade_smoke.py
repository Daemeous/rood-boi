#!/usr/bin/env python3
"""The end rows of a menu list fade out smoothly (Gitea #16; setting menu_end_fade).

    python3 pc/tests/menu_end_fade_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The game dims the top and bottom row of a list that goes on (OPTIONS scrolled: its first and last visible row) with twelve
one-pixel lines, each a step darker, and the picture's own background shows between them: at 4x hard horizontal bands. With the
setting on (menu_end_fade, 0..1, default 1) the row is drawn whole and its opacity runs down it, smooth along its height, to
(1 - strength) at the list's outer edge (gpu_hw.c edge_fade_at: one opacity to a vertex). 0 is the game's own lines. 1280 x 960
window, 4:3, internal resolution 4 (exact 4x): the setting at 0, 0.5 and 1:
  - 0 and 1 differ in the two end rows' bands and nowhere else;
  - at 1 the row is clearer at its inner edge than its outer, and at 1 the outer edge is fainter than at 0.5 (the strength);
  - at 1 the fade clears steadily toward the list (a ramp along the row's height, not bands)."""
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
import classic_parity as cp  # noqa: E402
from menu_nav import top_command  # noqa: E402

ROWS = (35, 46)  # the game lines of the top end row (OPTIONS scrolled down), of 240
BOTTOM = (163, 174)  # the bottom end row
DOWNS = 8


def shot(scale, smooth, card, out):
    work = out / f"s{scale}_{smooth}"
    (work / "prefabs").mkdir(parents=True)
    (work / "plugins").mkdir()
    (work / "settings.cfg").write_text(f"aspect 4:3\nui_menus 0\ninterpolate 0\nmenu_end_fade {smooth}\n")
    env = {"ROOD_WINDOW": "1280x960", "ROOD_LIGHTS": str(work / "lights"), "ROOD_PREFABS": str(work / "prefabs"), "ROOD_GI": "0",
           "ROOD_OUT": str(work), "ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_SCALE": str(scale),
           "ROOD_PLUGINS": str(work / "plugins")}
    g = launch_at("save_room", card=card, fast=True, log=work / "game.log", extra_env=env)
    try:
        g.press("triangle", vsyncs=3, settle=30)
        top_command(g, cp.OPTION_COMMAND)
        for _ in range(5):
            g.press("circle", vsyncs=2, settle=2)
            g.run(30)
        g.until(lambda: g.value("vs_battle_menuState.currentMenu") & 0x3F not in (0x3F, 0), guard=120, what="the Option screen")
        g.run(120)  # (its overlay loads, the rows slide in)
        for _ in range(DOWNS):
            g.press("down", vsyncs=3, settle=12)
        g.run(60)
        return np.asarray(Image.open(g.present_screenshot(str(work / "o.ppm"))).convert("RGB")).astype(float)
    finally:
        g.close()


def profile(image, lo, hi, dark):
    """The row's brightness down its lines (mean over the bar's columns), the list's outer end first when `dark`."""
    height, width = image.shape[:2]
    x0, x1 = int(285 / 320 * width), int(310 / 320 * width)
    y0, y1 = int(lo * 1.0714 / 240 * height), int(hi * 1.0714 / 240 * height)  # (the display's 224 lines are stretched to 240)
    return image[y0:y1, x0:x1].mean(axis=(1, 2))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "menu_end_fade"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    failures = 0

    def check(ok, message):
        nonlocal failures
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        failures += not ok

    four = {s: shot(4, s, card, out) for s in (0, 0.5, 1)}
    changed = np.abs(four[0] - four[1]).max(axis=2) > 0
    ys = np.nonzero(changed.any(axis=1))[0]
    height = four[0].shape[0]
    bands = [(ROWS[0], ROWS[1]), (BOTTOM[0], BOTTOM[1])]
    inside = all(any(lo - 4 <= y * 240 / 1.0714 / height <= hi + 4 for lo, hi in bands) for y in ys[::4]) if len(ys) else False
    check(inside, "4x: setting 0 and 1 differ in the two end rows' bands and nowhere else")
    for name, (lo, hi), outer_first in (("top", ROWS, True), ("bottom", BOTTOM, False)):
        p0, p5, p1 = (profile(four[k], lo, hi, outer_first) for k in (0, 0.5, 1))
        n = max(2, len(p1) // 4)
        outer = (lambda p: p[:n].mean()) if outer_first else (lambda p: p[-n:].mean())
        inner = (lambda p: p[-n:].mean()) if outer_first else (lambda p: p[:n].mean())
        check(inner(p1) > outer(p1) + 3 and outer(p1) < outer(p5) - 1,
              f"4x: the {name} row at strength 1 is clearer at its inner edge ({inner(p1):.0f}) than its outer ({outer(p1):.0f}) and "
              f"fainter at its outer edge than at 0.5 ({outer(p5):.0f})")
        toward_inner = p1 if not outer_first else p1  # (outer end first for the top row, last for the bottom)
        ordered = toward_inner if outer_first else toward_inner[::-1]
        rising = float((np.diff(ordered[::4]) >= -1.0).mean())
        check(rising >= 0.85, f"4x: the {name} row at strength 1 clears steadily toward the list ({rising:.2f} of its steps rise)")
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
