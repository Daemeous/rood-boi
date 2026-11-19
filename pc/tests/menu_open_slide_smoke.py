#!/usr/bin/env python3
"""A menu screen slides in from the view's own edges at 16:9 (PC_ANCHOR_SLIDE: game/battle/vs_menu_frameworkUpdateMenuOverlay.zig,
render_menu_item.zig; pc/src/gpu_hw.c; setting menu_slide).

    python3 pc/tests/menu_open_slide_smoke.py [--out DIR]

The user's report: "a weird pop when entering the items screen ... it almost seems like Ashley is sliding in from the original
location on the screen". Opening ITEMS from the COMMAND menu, the game slides the screen's left block (its STATUS label, its icon
row, Ashley's model) in from the screen's left edge and its right column (ITEMS, OPTIONS, EQUIP, SETUP) in from the right. At 16:9
the 2D layer was clipped to the 4:3 screen's drawing area, so the label and the rows appeared from nothing a fifth of the view in
(the label as "US", the rows as "OPTIO", "EQU").

At 1760 x 1216 (the user's window, widescreen on, menu_fade plain), ITEMS opened from COMMAND, each present after Circle kept, and
the same at 960 x 720 (4:3) as the reference, and at 16:9 with menu_slide 0:
  - the 4:3 screen's part of the 16:9 frames is the 4:3 frames, frame for frame, to within the two renderers' sampling (a mean
    difference under 3 levels of 255 once both are brought to 320 x 224): the slide's trajectory is the game's own, anchored, not
    a different one (this includes the model and the icon row);
  - the view's left band (the 40 game pixels left of the 4:3 screen) holds the label sliding in: the strip of the label's rows has
    light in the band in some frame (menu_slide 1), and never (menu_slide 0, the original's clipped slide); the same for the
    right band and the right column;
  - the leftmost light of the label's strip never jumps: between presented frames it moves by no more than the 4:3 slide's largest
    step does plus a few pixels (30 at least), and it starts beyond the 4:3 edge: no pop at the 4:3 edge.
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

FIRST, LAST = 20, 46  # presents after Circle: the open's slide is within these
ITEMS_COMMAND = 3
failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def open_items(out, name, wide, slide, claims=False):
    from menu_nav import top_command
    from weapon_slots_smoke import default_card
    d = out / name
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    (d / "settings.cfg").write_text("ui_upscale mmpx\nui_text font\nui_text_layout line\nui_text_overlay auto\n"
                                    f"widescreen {int(wide)}\naspect {'16:9' if wide else '4:3'}\naa msaa2+fxaa\nmenu_fade plain\n"
                                    f"screen_copy native\nrender_thread 2\npresent_pacing 1\nui_menus 0\ninterpolate 0\nmenu_slide {slide}\n")
    card = d / "card1.mcd"
    shutil.copyfile(default_card(), card)
    env = {"ROOD_SETTINGS": str(d / "settings.cfg"), "ROOD_WINDOW": "1760x1216" if wide else "960x720", "ROOD_AUDIO": "none", "ROOD_GI": "0"}
    if wide:
        env["ROOD_WIDESCREEN"] = "1"
    if claims:  # the shipped classic claims on (rood.classic draws the menu's rows, tabs and background itself)
        (d / "plugins").mkdir(exist_ok=True)
        env.update(ROOD_PLUGINS=str(d / "plugins"), ROOD_PLUGIN_DATA=str(d / "plugin_data"), ROOD_PLUGINS_SHIPPED=str(ROOT / "pc/plugins"))
    g = launch_at("save_room", card=card, fast=True, log=d / "game.log", extra_env=env)
    paths = []
    try:
        g.run(120)
        for _ in range(8):
            if g.value("vs_battle_menuState.currentMenu") & 0x3F != 0x3F:
                break
            g.press("triangle", vsyncs=3, settle=40)
        g.run(40)
        top_command(g, ITEMS_COMMAND)
        g.press("circle", vsyncs=2, settle=0)
        for k in range(LAST + 1):
            g.run(1)
            if k >= FIRST:
                paths.append(d / f"{k:02d}.png")
                g.present_screenshot(paths[-1])
    finally:
        g.close()
    return paths


def grey(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(float)


def box(image, wide):
    """The 4:3 screen's part of a frame, brought to 320 x 224 (a block average)."""
    h, w, _ = image.shape
    x0, x1 = (int(w * 0.125), int(w * 0.875)) if wide else (0, w)
    part = Image.fromarray(image[:, x0:x1].astype("uint8")).resize((320, 224), Image.BOX)
    return np.asarray(part).astype(float)


# The label's rows (the strip the STATUS label slides along) and the right column's rows, in game pixels of 224.
LABEL_ROWS = (38, 48)
COLUMN_ROWS = (30, 80)


def strip(image, rows, wide):
    """The strip of the frame (full width) in the game's rows `rows`."""
    h = image.shape[0]
    return image[int(rows[0] / 224 * h):int(rows[1] / 224 * h)]


def lit(strip_, threshold=30):
    return strip_.sum(2) > threshold * 3


def leftmost_light(image, rows, wide):
    """The leftmost lit column of the label's strip, in 4:3 game pixels (negative: in the view's left band)."""
    s = lit(strip(image, rows, wide))
    cols = np.nonzero(s.any(0))[0]
    if not len(cols):
        return None
    w = image.shape[1]
    unit = (w * 0.75 if wide else w) / 320.0
    origin = w * 0.125 if wide else 0
    return (cols.min() - origin) / unit


def column_edge(image, wide, rows=(50, 60), from_x=190):
    """The left end of the right column's EQUIP row (a bar, saturated blue or teal, at least 25 game pixels long) in 4:3 game
    pixels: the row slides in from the right, so it is None until it is on the screen (16:9: until it reaches the view)."""
    h, w, _ = image.shape
    unit = (w * 0.75 if wide else w) / 320.0
    origin = w * 0.125 if wide else 0
    best = None
    for y in range(int(rows[0] / 224 * h), int(rows[1] / 224 * h), 3):
        r = image[y]
        m = (((r[:, 2] - r[:, 0]) > 40) & (r[:, 2] > 60)) | (((r[:, 1] - r[:, 0]) > 50) & (r[:, 1] > 90))
        xs = np.nonzero(m)[0]
        if not len(xs):
            continue
        breaks = np.nonzero(np.diff(xs) > 3)[0]
        for lo, hi in zip(np.r_[xs[0], xs[breaks + 1]], np.r_[xs[breaks], xs[-1]]):
            x = (lo - origin) / unit
            if (hi - lo) / unit >= 25 and x >= from_x:
                best = x if best is None else min(best, x)
    return best


def band_light(image, wide, side):
    """Lit pixels of the label's strip in the view's left band (side 0), or of the column's strip in the right band (side 1)."""
    h, w, _ = image.shape
    if not wide:
        return 0
    rows = LABEL_ROWS if side == 0 else COLUMN_ROWS
    s = lit(strip(image, rows, wide))
    return int(s[:, :int(w * 0.125) - 2].sum()) if side == 0 else int(s[:, int(w * 0.875) + 2:].sum())


def verify(images, tag):
    # The 4:3 part of each 16:9 frame is the 4:3 frame (the slide is the game's own, the same path).
    for name in ("w169", "off"):
        diffs = [float(np.abs(box(a, True) - box(b, False)).mean()) for a, b in zip(images[name], images["w43"])]
        check(max(diffs) < 3.0, f"{tag}{name}: the 4:3 screen's part matches the 4:3 frames through the open (mean difference "
              f"{max(diffs):.2f} of 255 at most, over {len(diffs)} frames)")

    # The bands: the slide's pieces reach them with menu_slide 1, never with 0 (the right column's bars reach the right band
    # either way: the game's bars extend to the view's edge; its text and its slanted ends are what the clip cut).
    lights = [band_light(a, True, 0) for a in images["w169"]]
    original = [band_light(a, True, 0) for a in images["off"]]
    check(max(lights) >= 40, f"{tag}menu_slide 1: the label slides through the left band (at most {max(lights)} lit pixels in a frame)")
    check(max(original) < 10, f"{tag}menu_slide 0: nothing of the label in the left band (the original's clip; at most {max(original)})")
    more = [band_light(a, True, 1) - band_light(b, True, 1) for a, b in zip(images["w169"], images["off"])]
    check(max(more) >= 40, f"{tag}menu_slide 1: the rows' text reaches the right band as they slide ({max(more)} more lit pixels at most than with 0)")

    # No pop: the label's leftmost light moves no more per frame than the 4:3 slide's own step (or 30 game pixels).
    def steps(frames, wide_):
        xs = [leftmost_light(f, LABEL_ROWS, wide_) for f in frames]
        xs = [x for x in xs if x is not None and x < 80]  # (the label's: other light sits in the strip before it comes)
        return [abs(b - a) for a, b in zip(xs, xs[1:])], xs

    ref_steps, ref_xs = steps(images["w43"], False)
    new_steps, new_xs = steps(images["w169"], True)
    print(f"      4:3 leftmost light {[round(x) for x in ref_xs]}\n      16:9 leftmost light {[round(x) for x in new_xs]}")
    limit = max(max(ref_steps, default=0) + 4, 30)
    check(bool(new_xs) and max(new_steps, default=0) <= limit,
          f"{tag}16:9 slide: the label's leftmost light moves {max(new_steps, default=0):.1f} game pixels a frame at most (the 4:3 slide's own "
          f"largest step {max(ref_steps, default=0):.1f}; no more than {limit:.0f})")
    # The right column (EQUIP, the third row): the left end of its bar, per frame. The 4:3 slide's positions are the 16:9 slide's
    # (the same end within 10 game pixels: the bar's slanted end is drawn a few pixels differently at the two resolutions; moving left only, steps within 12 of the 4:3 slide's), and
    # the 16:9 row is on the view at the edge of the 4:3 screen or beyond it while the 4:3 one is not drawn yet.
    ref_c = [column_edge(f, False) for f in images["w43"]]
    new_c = [column_edge(f, True) for f in images["w169"]]
    ref_p = [x for x in ref_c if x is not None]
    new_p = [x for x in new_c if x is not None]
    print(f"      4:3 right column {[round(x) for x in ref_p]}\n      16:9 right column {[round(x) for x in new_p]}")
    ref_steps_c = [a - b for a, b in zip(ref_p, ref_p[1:])]
    new_steps_c = [a - b for a, b in zip(new_p, new_p[1:])]
    check(bool(ref_p) and bool(new_p) and abs(ref_p[-1] - new_p[-1]) <= 10 and all(s_ >= -2 for s_ in ref_steps_c + new_steps_c)
          and max(new_steps_c, default=0) <= max(ref_steps_c, default=0) + 12,
          f"{tag}the right column slides as the 4:3 one does: both end at {ref_p[-1] if ref_p else 0:.0f} / {new_p[-1] if new_p else 0:.0f}, moving left only, "
          f"the 16:9 step at most {max(new_steps_c, default=0):.0f} game pixels a frame against 4:3's {max(ref_steps_c, default=0):.0f}")
    check(bool(new_p) and new_p[0] >= 316, f"{tag}the right column comes in from the view's edge (first at {new_p[0] if new_p else 0:.0f}, the 4:3 slide's first {ref_p[0] if ref_p else 0:.0f})")
    check(bool(new_xs) and min(new_xs) < -8, f"{tag}16:9 slide: the label comes in from beyond the 4:3 edge (leftmost light {min(new_xs, default=0):.0f})")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "menu_open_slide"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    for claims in (False, True):
        tag = "claims " if claims else ""
        narrow = open_items(out, f"w43{'_claims' if claims else ''}", False, 1, claims)
        wide = open_items(out, f"w169{'_claims' if claims else ''}", True, 1, claims)
        off = open_items(out, f"w169_off{'_claims' if claims else ''}", True, 0, claims)
        images = {n: [grey(p) for p in ps] for n, ps in (("w43", narrow), ("w169", wide), ("off", off))}
        verify(images, tag)
    print(f"{len(failures)} failure(s)" if failures else "menu_open_slide_smoke: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
