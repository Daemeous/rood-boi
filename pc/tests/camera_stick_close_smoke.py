#!/usr/bin/env python3
"""Closing the Triangle menu does not move the camera (pc/src/camera_stick.c pc_camera_stick_place).

    python3 pc/tests/camera_stick_close_smoke.py [--out DIR]

The user's report: "the camera zooms out fast and then zooms in fast when entering and exiting the menu". With the right stick's zoom
away from 1 (the stick zoomed the follow camera in), closing COMMAND pulled the camera out by a step and eased it back over a dozen
frames: the menu's closing frames tick neither the field's stick nor the menus', and the first placement after them found the stick
"not active" (last tick more than 8 vsyncs back), eased the zoom a step toward 1 (dt up to 4 frames at once) and, active again the
next frame, back to the stick's zoom. At the zoom's limit of 0.6 that is a 1.4x pull-out in a frame (the video's 1.39).

In the save room, the stick held down for 70 vsyncs (RPC input right_stick: the zoom goes to its limit, 0.6), COMMAND opened (Triangle),
and closed by Cross through the SDL virtual gamepad (the real pad path: the bindings, the game's pad word, then camera_stick), each
vsync's placed distance, zoom and pitch logged across the close:
  - the zoom was moved (below 0.97): the test is live;
  - the placed distance never leaves 1% of what it was before the menu, through the open and the close;
  - the applied zoom never leaves 0.005 of the stick's zoom.
The hand-over to a scripted camera (the stick does not have it: the zoom eases back to the game's distance) is camera_stick_smoke's New Game opening check: it must still pass.
--out keeps the log (a JSON of the per-vsync values)."""
import argparse
import json
import os
import pathlib
import shutil
import sys

os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "camera_stick_close"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    (out / "settings.cfg").write_text("ui_text font\nwidescreen 1\naspect 16:9\nui_menus 0\nrender_thread 2\npresent_pacing 1\ninterpolate 0\n" + f"camera_stick {os.environ.get('CAMERA_STICK', '1')}\n")
    card = out / "card1.mcd"
    shutil.copyfile(default_card(), card)
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env={
        "ROOD_SETTINGS": str(out / "settings.cfg"), "ROOD_AUDIO": "none", "ROOD_WINDOW": "880x608", "ROOD_WIDESCREEN": "1", "ROOD_GI": "0"})
    rows = []

    def sample(tag, n):
        for _ in range(n):
            g.run(1)
            c = g.call("camera_stick")
            rows.append({"tag": tag, "distance": c["distance"], "zoom": c["zoom_applied"], "target": c["zoom_target"], "pitch": c["pitch"],
                         "active": c["active"], "menu": c["menu"]})

    try:
        g.run(120)
        for _ in range(8):  # the first Triangle once the field takes input
            if g.value("vs_battle_menuState.currentMenu") & 0x3F != 0x3F:
                break
            g.press("triangle", vsyncs=3, settle=40)
        g.run(40)
        g.press("triangle", vsyncs=3, settle=40)  # and out again: the field, from here on
        g.run(60)
        g.call("input", right_stick=[0, -1], vsyncs=70)
        g.run(150)  # the zoom ends at its limit and the placement has eased there
        c0 = g.call("camera_stick")
        d0, z0 = c0["distance"], c0["zoom_applied"]
        check(z0 < 0.97 and abs(z0 - c0["zoom_target"]) < 0.005, f"the stick zoomed the follow camera in (zoom {z0:.3f}, distance {d0})")
        g.press("triangle", vsyncs=3, settle=0)
        sample("open", 80)
        check(any(r["menu"] == "command" for r in rows), "COMMAND is open (the menus' camera context)")
        pad = "south"  # Cross on the virtual gamepad
        g.call("system", virtual_pad=pad, down=True)
        sample("pad", 3)
        g.call("system", virtual_pad=pad, down=False)
        sample("close", 140)
        check(g.value("vs_battle_menuState.currentMenu") & 0x3F == 0x3F, "Cross closed the menu")
    finally:
        g.close()
    (out / "rows.json").write_text(json.dumps(rows))
    if os.environ.get("ROWS"):
        prev = None
        for i, r in enumerate(rows):
            key = (r["distance"], round(r["zoom"], 3), r["active"], r["menu"])
            if key != prev:
                print(f"      v{i:3d} {r['tag']:5s} distance {r['distance']} zoom {r['zoom']:.3f} target {r['target']:.3f} pitch {r['pitch']} active {r['active']} menu {r['menu']}")
            prev = key
    worst = max(abs(r["distance"] - d0) / d0 for r in rows)
    worst_zoom = max(abs(r["zoom"] - z0) for r in rows)
    check(worst <= 0.01, f"the placed distance stays within 1% of {d0} through the open and the close (worst {worst * 100:.2f}%)")
    check(worst_zoom <= 0.005, f"the applied zoom stays at the stick's {z0:.3f} (worst {worst_zoom:.3f} away)")
    print(f"{len(failures)} failure(s)" if failures else "camera_stick_close_smoke: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
