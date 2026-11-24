#!/usr/bin/env python3
"""The menu tint on the game's own (classic) menus (pc/src/ui/ui_tint.cpp pc_ui_classic_tint, draw_items.c).

With ui_menus 0 the Command menu (Triangle in the field) keeps the game's blue panels under the Original tint (the default) and takes the
chosen tint's hue under Slate and Crimson: the panels' pixels change, the HUD (the HP / MP gauges and text) and the 3D picture beside them
do not. The Original tint is the picture as it was: run twice it is identical, and build_compare --menus against the build before the change
reports 0 differences (run by hand).

    python3 pc/tests/menu_tint_classic_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]"""
import argparse
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


def shot(card, out, name, extra, live=()):
    d = out / name
    d.mkdir(parents=True, exist_ok=True)
    (d / "settings.cfg").write_text("ui_menus 0\nui_text font\ninterpolate 0\nwidescreen 0\naspect 0\n" + extra)
    shutil.copyfile(card, d / "card1.mcd")
    env = {"ROOD_SETTINGS": str(d / "settings.cfg"), "ROOD_WINDOW": "1280x960", "ROOD_AUDIO": "none", "ROOD_OUT": str(d),
           "ROOD_LIGHTS": str(d / "lights"), "ROOD_PREFABS": str(d / "prefabs")}
    g = launch_at("save_room", card=str(d / "card1.mcd"), fast=True, log=d / "game.log", extra_env=env)
    try:
        g.run(60)
        g.press("triangle", vsyncs=3, settle=60)
        g.run(12)
        g.wait_presents(2)
        path = d / "menu.ppm"
        for line in live:  # (settings changed while the menu is open: the tint sliders' drag, ui_system's rows apply the same lines)
            g.call("settings", apply=line)
            g.run(12)
            g.wait_presents(2)
        g.call("present_dump", path=str(path))
        for _ in range(50):  # (written by the presenting thread)
            if path.exists() and path.stat().st_size > 0:
                break
            g.run(2)
            g.wait_presents(1)
    finally:
        g.close()
    return np.asarray(Image.open(path).convert("RGB")).astype(int)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(es.pinned_card()))
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "menu_tint_classic"
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    card = pathlib.Path(args.card)
    a = shot(card, out, "original", "")
    a2 = shot(card, out, "original2", "ui_menu_tint lea_monde\n")
    s = shot(card, out, "slate", "ui_menu_tint slate\n")
    c = shot(card, out, "crimson", "ui_menu_tint crimson\n")
    live = shot(card, out, "live", "", live=["ui_menu_tint custom", "ui_menu_tint_custom 4 8 25", "ui_menu_tint_custom 20 3 3"])
    d = np.abs(live - a).max(axis=2)
    check(int((d[slice(80, 690), slice(780, 1280)] > 8).sum()) > 20000, "a tint changed while the menu is open shows at once (Custom, dragged to red)")
    check(live[slice(80, 690), slice(780, 1280)].reshape(-1, 3).mean(axis=0)[0] > live[slice(80, 690), slice(780, 1280)].reshape(-1, 3).mean(axis=0)[2],
          "and follows the last value (red, not the first)")
    panel = (slice(80, 690), slice(780, 1280))   # the Command rows
    hud = (slice(40, 140), slice(30, 600))       # HP / MP gauges and text
    scene = (slice(330, 700), slice(0, 760))     # Ashley and the room beside the menu
    check(np.array_equal(a, a2), "the Original tint twice: the picture is identical")
    for name, img in (("Slate", s), ("Crimson", c)):
        d = np.abs(img - a).max(axis=2)
        check(int((d[panel] > 8).sum()) > 20000, f"{name}: the Command rows' panels take the tint ({int((d[panel] > 8).sum())} pixels differ)")
        check(int((d[hud] > 0).sum()) == 0, f"{name}: the HUD is untouched ({int((d[hud] > 0).sum())} pixels differ)")
        check(int((d[scene] > 0).sum()) == 0, f"{name}: the 3D picture is untouched ({int((d[scene] > 0).sum())} pixels differ)")
    r = c[panel].reshape(-1, 3).mean(axis=0)
    check(r[0] > r[2], f"Crimson panels lean red (mean rgb {r.round(1).tolist()})")
    print(f"{len(failures)} failures" if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
