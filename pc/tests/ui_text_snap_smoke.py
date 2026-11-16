#!/usr/bin/env python3
"""ui_text_snap: glyphs drawn at the window's resolution start on a whole window pixel (gpu_hw.c pc_hw_text_overlay).

    python3 pc/tests/ui_text_snap_smoke.py [--out DIR]

The Equip screen (the Status command's, Ashley's equipment) with the font text at 1920 x 1080 and 2560 x 1440 (a game each), the
same held picture presented with ui_text_snap 0 and 1 (RPC graphics ui_text_snap; the paused game redraws it):
 - the setting reads back and ui_text reports it (snap), 1 by default;
 - the pictures differ (snap draws), and by little: the letters' ink centroids (connected components of the list's text, paired by
   position) move less than one window pixel each, and on average less than half a one: a glyph moves by under half a window
   pixel in x and y, no more;
 - the thin strokes (components at most 7 window pixels wide and as tall as a capital: l, I): the spread of their peak
   brightness is not more than with snap off (a guard. Snap fixes where a letter starts; a stroke narrower than a window pixel
   stays dim whatever the pixel it lands on, because the glyph's CLUT ramp follows its coverage: not the subject here);
 - snap 0 reproduces the picture the build drew before the setting (the same frame twice with the setting off: the same pixels).
--out keeps the frames (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402

SIZES = ((1920, 1080), (2560, 1440))
SETTINGS = "ui_upscale mmpx\nui_text font\nui_text_overlay auto\nui_text_layout line\nwidescreen 1\naa msaa2+fxaa\nmenu_fade original\n"
failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def present(g, path):
    g.wait_presents(4)
    g.present_screenshot(path)
    return np.asarray(Image.open(path).convert("RGB")).astype(float)


def shots(out, size):
    from menu_nav import top_command
    from weapon_slots_smoke import default_card
    d = out / f"{size[0]}x{size[1]}"
    d.mkdir(parents=True, exist_ok=True)
    (d / "settings.cfg").write_text(SETTINGS)
    card = d / "card1.mcd"
    shutil.copyfile(default_card(), card)
    g = launch_at("save_room", card=card, fast=True, log=d / "game.log", socket_path=f"/tmp/rood-snap-{size[0]}-{os.getpid()}.sock",
                  extra_env={"ROOD_SETTINGS": str(d / "settings.cfg"), "ROOD_WINDOW": f"{size[0]}x{size[1]}", "ROOD_AUDIO": "none",
                             "ROOD_WIDESCREEN": "1"})
    sym = symbols()
    try:
        g.run(160)
        for _ in range(8):
            if g.read(sym["vs_battle_menuState"], 1)[0] & 0x3F != 0x3F:
                break
            g.press("triangle", vsyncs=3, settle=40)
        top_command(g, sym, 4)  # STATUS
        for _ in range(5):
            g.press("circle", vsyncs=2, settle=2)
            g.run(30)
        g.run(150)
        report = g.call("ui_text")
        default = report.get("snap")
        got = {}
        g.call("graphics", ui_text_snap=0)
        got["off"] = present(g, d / "snap0.png")
        got["off again"] = present(g, d / "snap0b.png")
        g.call("graphics", ui_text_snap=1)
        got["on"] = present(g, d / "snap1.png")
        return got, default, g.call("ui_text").get("snap")
    finally:
        g.close()


def components(luma, region, threshold):
    """[(centroid x, centroid y, width, height, peak)] of the connected ink in `region` (x0, y0, x1, y1), brightness-weighted."""
    x0, y0, x1, y1 = region
    sub = luma[y0:y1, x0:x1]
    labels, count = ndimage.label(sub > threshold)
    out = []
    for k, sl in enumerate(ndimage.find_objects(labels), 1):
        m = labels[sl] == k
        w = np.where(m, sub[sl] - threshold, 0.0)
        if w.sum() <= 0 or m.sum() < 6:
            continue
        ys, xs = np.mgrid[sl[0], sl[1]]
        out.append((float((xs * w).sum() / w.sum()) + x0, float((ys * w).sum() / w.sum()) + y0, sl[1].stop - sl[1].start,
                    sl[0].stop - sl[0].start, float(sub[sl][m].max())))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "ui_text_snap"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    for size in SIZES:
        got, default, after = shots(out, size)
        tag = f"{size[0]}x{size[1]}"
        check(default == 1 and after == 1, f"{tag}: ui_text_snap is 1 by default and reads back ({default}, {after})")
        off, again, on = got["off"], got["off again"], got["on"]
        check(float(np.abs(off - again).max()) == 0.0, f"{tag}: snap 0 draws the same picture twice")
        diff = float((np.abs(off - on).max(axis=2) > 24).sum())
        check(diff >= 200, f"{tag}: snap 1 draws differently ({diff:.0f} pixels differ by more than 24 of 255)")
        w, h = size
        region = (int(w * 0.5), int(h * 0.04), w, int(h * 0.45))  # the list of the equipment
        a = components(off.mean(axis=2), region, 120)
        b = components(on.mean(axis=2), region, 120)
        pairs = []
        for ca in a:
            near = min(b, key=lambda cb: (cb[0] - ca[0]) ** 2 + (cb[1] - ca[1]) ** 2, default=None)
            if near and (near[0] - ca[0]) ** 2 + (near[1] - ca[1]) ** 2 < 9 and abs(near[2] - ca[2]) <= 2 and abs(near[3] - ca[3]) <= 2:
                pairs.append((ca, near))
        check(len(pairs) >= 20, f"{tag}: {len(pairs)} letters paired between the pictures")
        shift = [max(abs(p[0] - q[0]), abs(p[1] - q[1])) for p, q in pairs]
        mean_shift = float(np.mean(shift)) if shift else 9.0
        print(f"      {tag}: letter centroid shift, window pixels: mean {mean_shift:.3f}, max {max(shift, default=9):.3f} "
              f"(PS1 pixels: max {max(shift, default=9) * 320 / w:.3f})", flush=True)
        check(shift and max(shift) < 1.0 and mean_shift < 0.5, f"{tag}: the letters stay within a window pixel (max {max(shift, default=9):.2f}, "
                                                              f"mean {mean_shift:.2f})")
        line = 26 * w / 1920.0  # a letter's height in this window (the list's text is about 26 pixels tall at 1080p)
        thin = lambda cs: [c[4] for c in cs if c[2] <= 7 * w / 1920.0 and 0.8 * line <= c[3] <= 1.15 * line]
        ta, tb = thin(a), thin(b)
        print(f"      {tag}: thin strokes: snap 0 {len(ta)} (peak std {np.std(ta) if ta else 0:.1f}), snap 1 {len(tb)} "
              f"(peak std {np.std(tb) if tb else 0:.1f})", flush=True)
        check(len(ta) >= 3 and len(tb) >= 3, f"{tag}: thin strokes found ({len(ta)}, {len(tb)})")
        if ta and tb:
            check(np.std(tb) <= np.std(ta) + 0.5, f"{tag}: thin strokes at least as even with snap on (std {np.std(tb):.1f}, off {np.std(ta):.1f})")
    print(f"{len(failures)} failure(s)" if failures else "ui_text_snap_smoke: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
