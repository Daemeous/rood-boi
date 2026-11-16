#!/usr/bin/env python3
"""ui_text's glyph fields: multi-channel (ui_text_field msdf, the default) against one channel (sdf).

    python3 pc/tests/ui_text_msdf_smoke.py [--out DIR]

The Equip screen (the Status command's, Ashley's equipment) at 2560 x 1440 with the font text, once per field (a game each):
 - the setting is the one asked for (RPC settings get) and ui_text reports it (field), with no region failing;
 - msdf is drawn: the WEAPON label differs from sdf's (its corners are the outline's, not the field's resolution), but by
   corners only: at most MAX_CHANGED of its pixels differ by more than 64 of 255 in a channel, and at least MIN_CHANGED differ at all;
 - the glyphs stay where they are: each of WEAPON's letters keeps its ink's centroid within MAX_SHIFT PS1 pixels (the field
   changes the distance a texel holds, never where the glyph is), and the label's ink within 2 % of sdf's.
(The last letter is left out: the picture behind it shows through the thresholds.)"""
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
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402

WINDOW = (2560, 1440)
LABEL = (478, 112, 640, 150)  # WEAPON, in a 1280 wide picture's pixels
MAX_SHIFT = 0.1       # PS1 pixels
MIN_CHANGED, MAX_CHANGED = 50, 4000
SETTINGS = "ui_upscale mmpx\nui_text font\nui_text_overlay auto\nwidescreen 1\naa msaa2+fxaa\nmenu_fade original\n"

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def shot(out, field):
    from menu_nav import top_command
    from weapon_slots_smoke import default_card
    d = out / field
    d.mkdir(parents=True, exist_ok=True)
    (d / "settings.cfg").write_text(SETTINGS + f"ui_text_field {field}\n")
    card = d / "card1.mcd"
    shutil.copyfile(default_card(), card)
    g = launch_at("save_room", card=card, fast=True, log=d / "game.log",
                  socket_path=f"/tmp/rood-msdf-{field}-{os.getpid()}.sock",
                  extra_env={"ROOD_SETTINGS": str(d / "settings.cfg"), "ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_AUDIO": "none"})
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
        path = d / "equip.png"
        g.present_screenshot(path)
        line = g.call("settings", get="ui_text_field").get("line")
        report = g.call("ui_text")
        return path, line, report
    finally:
        g.close()


def label(path):
    f = WINDOW[0] / 1280.0
    im = Image.open(path).convert("RGB").crop(tuple(int(v * f) for v in LABEL))
    return np.asarray(im).astype(float)


def letters(luma):
    cols = (luma > 140).any(axis=0)
    out, i = [], 0
    while i < len(cols):
        if cols[i]:
            j = i
            while j < len(cols) and cols[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def centroid(luma, a, b):
    m = luma[:, a:b] > 140
    ys, xs = np.nonzero(m)
    w = luma[:, a:b][m]
    k = WINDOW[0] / 320.0
    return float(((xs + a) * w).sum() / w.sum() / k), float((ys * w).sum() / w.sum() / k)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "ui_text_msdf"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    got = {}
    for field in ("sdf", "msdf"):
        path, line, report = shot(out, field)
        check(line is not None and line.split()[-1] == field, f"{field}: the setting reads back ({line!r})")
        check(report.get("field") == field and report.get("failed") == 0 and report.get("glyphs", 0) > 0,
              f"{field}: ui_text reports it ({report.get('field')}), {report.get('glyphs')} glyphs, {report.get('failed')} regions failed")
        got[field] = label(path)
    a, b = got["sdf"], got["msdf"]
    big = int((np.abs(a - b).max(axis=2) > 64).sum())
    some = int((np.abs(a - b).max(axis=2) > 0).sum())
    check(some >= MIN_CHANGED and big <= MAX_CHANGED,
          f"msdf draws the label from its own field: {some} pixels differ, {big} by more than 64 (of {MIN_CHANGED}.. and at most {MAX_CHANGED})")
    la, lb = a.mean(axis=2), b.mean(axis=2)
    sa, sb = letters(la), letters(lb)
    check(len(sa) == len(sb) and len(sa) >= 5, f"the same letters ({len(sa)} and {len(sb)} runs of ink columns)")
    shifts = [max(abs(x - y) for x, y in zip(centroid(la, p, q), centroid(lb, r, s))) for (p, q), (r, s) in zip(sa[:-1], sb[:-1])]
    check(shifts and max(shifts) <= MAX_SHIFT,
          f"the glyphs stay where they are: the letters' centroids move {['%.3f' % s for s in shifts]} PS1 pixels (at most {MAX_SHIFT})")
    ink_a, ink_b = int((la > 140).sum()), int((lb > 140).sum())
    check(abs(ink_b - ink_a) <= 0.02 * ink_a, f"the label's ink stays the same size ({ink_a} pixels, msdf {ink_b})")
    print(f"{len(failures)} failure(s)" if failures else "ui_text_msdf_smoke: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
