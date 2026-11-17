#!/usr/bin/env python3
"""The file menu's completion flash ends clean: MENU7 260.c _fileProcessingCompleteAnim, setting qol_file_flash_fix.

    python3 pc/tests/file_flash_smoke.py [--out DIR]

The user's report: "a weird pink flicker above the save file slot after loading it" (sometimes yellow, from another place). After a
file is loaded the slot flashes (a tile at an opacity over the slot, a gradient feather above it) and fades back to blue over 16 steps.
The counter runs -16 .. -1 and the table it indexes has 16 entries: the last step read the byte after the table, so for two frames a
bright band stood in the gap above the slot, its colour following whatever the picture behind it was (the room behind the menu: why it
was pink after one load and yellow after another). The original draws the same band: it is the game's own out-of-range read; the fix
(qol_file_flash_fix, default 1) ends the fade at colour 0 where the table's pattern goes (8 down to 0).

Data > LOAD > card 1 > the first file, Circle (the load runs), the presents after it (RPC present_screenshot), from two places (the save
room and another room), at 16:9 1280 x 720 and 4:3 960 x 720:
 - the gap above the slot (between the MEMORY CARD bar and the slot) never jumps by more than RISE over its last 4 frames once the
   flash runs (the band is a jump of about 100 of 255 for two frames);
 - with qol_file_flash_fix 0 (the control: the game's read) that rise is there (the test sees it);
 - the same fade's last step with the fix leaves the gap as dark as before the flash (within 6 of 255).
--out keeps the frames (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import classic_parity as cp  # noqa: E402

FRAMES = 70
RISE = 30
GAP = (0.283, 0.319, 0.30, 0.85)  # rows y0..y1 and columns x0..x1, of the picture
failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def run(out, name, wide, fix, room):
    d = out / name
    d.mkdir(parents=True, exist_ok=True)
    (d / "settings.cfg").write_text(f"ui_upscale mmpx\nui_text font\nui_text_layout line\nwidescreen {int(wide)}\naa msaa2+fxaa\n"
                                    f"ui_menus 0\ninterpolate 0\nqol_file_flash_fix {fix}\n")
    card = d / "card1.mcd"
    shutil.copyfile(ROOT / "pc/build/test-card1.mcd", card)
    env = {"ROOD_SETTINGS": str(d / "settings.cfg"), "ROOD_WINDOW": "1280x720" if wide else "960x720", "ROOD_AUDIO": "none", "ROOD_GI": "0"}
    if wide:
        env["ROOD_WIDESCREEN"] = "1"
    g = launch_at("save_room", card=card, fast=True, log=d / "game.log", extra_env=env)
    try:
        g.run(120)
        if room:
            g.call("room_goto", hold=True, zone=room[0], room=room[1], enemies=False, scripts=False)
            for _ in range(80):
                g.run(20)
                if g.call("room_goto", action="status")["phase"] in (0, 4):
                    break
            g.run(60)
        cp.data_open(g)
        g.press("down", vsyncs=3, settle=8)
        cp.press_until(g, "circle", lambda: cp.data_state(g) == cp.DATA_LOAD)
        g.run(90)
        g.press("circle", vsyncs=3, settle=8)
        g.run(90)
        cp.data_file_to(g, 0)
        g.press("circle", vsyncs=2, settle=0)
        series = []
        for k in range(FRAMES):
            g.run(1)
            path = d / f"{k:03d}.png"
            g.present_screenshot(path)
            a = np.asarray(Image.open(path).convert("RGB")).astype(float)
            h, w, _ = a.shape
            gap = a[int(h * GAP[0]):int(h * GAP[1]), int(w * GAP[2]):int(w * GAP[3])]
            series.append(float(gap.mean()))
        return series
    finally:
        g.close()


def spike(series):
    """The largest jump of the gap's brightness over the brightest of its 4 frames before (the flash's own onset climbs about 14 a
    step; the band jumps by about 100 for two frames), and where."""
    jumps = [(series[i] - max(series[i - 4:i]), i) for i in range(8, len(series))]
    return max(jumps)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "file_flash"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    for name, wide, room in (("save_room_16x9", True, None), ("room_9_0_16x9", True, (9, 0)), ("save_room_4x3", False, None)):
        fixed = run(out, name + "_fix", wide, 1, room)
        jump, at = spike(fixed)
        check(max(fixed) > fixed[0] + 15, f"{name}: the flash shows in the gap (peak {max(fixed):.0f}, before {fixed[0]:.0f})")
        check(jump <= RISE, f"{name}: with the fix the gap never jumps once the flash is running (largest jump {jump:.1f} at frame {at})")
        check(abs(fixed[-1] - fixed[0]) <= 6, f"{name}: the fade ends where it began ({fixed[0]:.0f} -> {fixed[-1]:.0f})")
        if name != "save_room_4x3":
            control = run(out, name + "_game", wide, 0, room)
            cjump, cat = spike(control)
            check(cjump > 40, f"{name}: the game's own read (qol_file_flash_fix 0) shows the band: a jump of {cjump:.0f} at frame {cat}")
    print(f"{len(failures)} failure(s)" if failures else "file_flash_smoke: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
