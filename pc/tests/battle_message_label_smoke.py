#!/usr/bin/env python3
"""The message box's header label (CAUTION) sits at the box's left corner in widescreen.

    python3 pc/tests/battle_message_label_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The game draws a message ("No target in range!", the attack with no enemy in reach) in a black box whose dark tile
widens to the whole 16:9 view; its header label is a sprite at the box's left corner. Left in the 4:3 screen (compressed
with the rest of the 2D) it floated 160 pixels inside the box (a 1280 x 720 window). It is anchored to the view's left
edge (game/battle/func_800CD0FC.zig), where the HP / MP HUD sits; a menu's information box (the same function) keeps its label with its text. Checks, in a 1280 x 720 view at 16:9 and a 960 x 720
one at 4:3: the label's ink (bright pixels in its rows) starts within 60 pixels of the left edge in both, at the same
fraction of the box's width within 2 percent of the screen at 4:3 as the original draws it."""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402

# The 16:9 case is a session of its own: the game lays its 2D out for the view it booted in (ROOD_WIDESCREEN), which a save state of a 4:3
# boot carries along; resumed in 16:9 the menu spreads over the whole view (as menu_open_scale_smoke and camera_stick_close_smoke: boot).
os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
from checkpoints import launch_at  # noqa: E402

ROW = (570, 612)  # the label's rows (of 720)
LEFT_MAX = 60  # the label's first ink column, pixels from the view's left edge


def label_edge(path):
    image = np.asarray(Image.open(path).convert("RGB")).astype(int)
    rows = image[ROW[0]:ROW[1]]
    bright = rows.min(axis=2) > 180
    columns = np.nonzero(bright.any(axis=0))[0]
    return int(columns.min()) if len(columns) else None, image.shape[1]


def shot(aspect, card, out, menu=False):
    work = pathlib.Path(tempfile.mkdtemp(prefix=f"label-{aspect[0]}-", dir=out))
    (work / "prefabs").mkdir()
    (work / "settings.cfg").write_text(f"aspect {aspect}\nui_menus 0\ninterpolate 0\n")
    env = {"ROOD_WINDOW": "1280x720" if aspect == "16:9" else "960x720", "ROOD_LIGHTS": str(work / "lights"),
           "ROOD_PREFABS": str(work / "prefabs"), "ROOD_GI": "0", "ROOD_OUT": str(work),
           "ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_SCALE": "1"}
    if aspect == "16:9":
        env["ROOD_WIDESCREEN"] = "1"
    g = launch_at("save_room", card=card, fast=True, log=work / "game.log", extra_env=env)
    try:
        g.call("room_goto", zone=9, room=18, enemies=True, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(30)
        if menu:  # the Items screen's information box (Triangle, down to Items, Circle): its label stays above its text
            from menu_nav import top_command
            g.press("triangle", vsyncs=3, settle=40)
            top_command(g, 3)
            g.press("circle", vsyncs=3, settle=60)
            g.run(240)
        else:
            g.press("circle", vsyncs=3, settle=40)  # the weapon drawn
            g.press("circle", vsyncs=3, settle=10)  # attack: no target in reach
            g.run(50)
        return pathlib.Path(g.present_screenshot(str(work / "label.ppm")))
    finally:
        g.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep the work here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "battle_message_label"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    failures = 0
    for aspect in ("4:3", "16:9"):
        edge, width = label_edge(shot(aspect, card, out))
        ok = edge is not None and edge < LEFT_MAX
        print(("ok:   " if ok else "FAIL: ") + f"{aspect}: the header label's first ink column {edge} of {width} (< {LEFT_MAX})")
        failures += not ok
    # A menu's information box (16:9): the label is above its text, in the 4:3 screen's middle, not at the view's edge.
    edge, width = label_edge(shot("16:9", card, out, menu=True))
    ok = edge is not None and edge > 120
    print(("ok:   " if ok else "FAIL: ") + f"16:9 menu: the information label's first ink column {edge} of {width} (> 120: with its text)")
    failures += not ok
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
