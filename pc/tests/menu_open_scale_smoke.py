#!/usr/bin/env python3
"""The menu's emblem keeps its size while a screen opens (16:9): MAINMENU 58EC.c _anchorPicture, rood.classic's menu_background.
Runs the development split build (pc/build/dev64: rood_host and the engine library, the user's `run-game.sh --reload`), where the
stretch below shows; ROOD_MENU_SCALE_BINARY names another host (a pre-fix artifact, the negative control), ROOD_ENGINE_SMOKE_NO_BUILD=1
uses dev64 as it is.

    python3 pc/tests/menu_open_scale_smoke.py [--out DIR]

The user's report: "the fade looks great but the background's scale changes: it goes from stretched to its normal size". Opening
STATUS from the COMMAND menu at 16:9, the menu's picture (the emblem: two 8-bit sprites, 256 + 64 wide, the screen's whole height)
came in 4/3 too wide and shrank when the screen settled. In 16:9 two sprites that wide and that tall looked like a full-screen
picture (pc_hw_set_bounds' backdrop rule, made for the map's background) and were spread over the whole view; the settled screen
draws the emblem compressed with the rest of the 2D, as the original draws it in its 4:3 part.

At 1760 x 1216 (the user's window, widescreen on), each present after Circle opens STATUS (RPC present_screenshot), the emblem's horizontal scale against the
settled frame's is found by image matching (a stretch of the picture about its centre and a shift, the best correlation), for every
frame with light in the emblem's region:
  - the scale stays within 0.97 .. 1.03 in every frame (before the fix: 1.32 from the first frame with light to the settle);
  - each of: menu_fade plain, menu_fade original, plain with the shipped classic claims on (the Lua's menu_background);
  - 4:3 (widescreen off, plain): the same, a control that was always constant.
--out keeps the frames (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT, symbols  # noqa: E402
from engine_reload_smoke import build  # noqa: E402
from checkpoints import launch_at  # noqa: E402

FRAMES = 44
LOW, HIGH = 0.97, 1.03
failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def open_status(out, name, fade, wide, claims):
    from menu_nav import top_command
    from weapon_slots_smoke import default_card
    d = out / name
    d.mkdir(parents=True, exist_ok=True)
    (d / "settings.cfg").write_text(f"ui_upscale mmpx\nui_text font\nui_text_layout line\nui_text_overlay auto\nwidescreen {int(wide)}\n"
                                    f"aspect {'16:9' if wide else '4:3'}\naa msaa2+fxaa\nmenu_fade {fade}\nscreen_copy native\nrender_thread 2\npresent_pacing 1\nui_menus 0\ninterpolate 0\n")
    card = d / "card1.mcd"
    shutil.copyfile(default_card(), card)
    env = {"ROOD_SETTINGS": str(d / "settings.cfg"), "ROOD_WINDOW": "1760x1216" if wide else "960x720", "ROOD_AUDIO": "none", "ROOD_GI": "0"}
    if wide:
        env["ROOD_WIDESCREEN"] = "1"
    if claims:
        (d / "plugins").mkdir(exist_ok=True)
        env.update(ROOD_PLUGINS=str(d / "plugins"), ROOD_PLUGIN_DATA=str(d / "plugin_data"), ROOD_PLUGINS_SHIPPED=str(ROOT / "pc/plugins"))
    g = launch_at("save_room", card=card, fast=True, log=d / "game.log", extra_env=env)
    sym = symbols()
    try:
        g.run(120)
        for _ in range(8):
            if g.read(sym["vs_battle_menuState"], 1)[0] & 0x3F != 0x3F:
                break
            g.press("triangle", vsyncs=3, settle=40)
        g.run(40)
        top_command(g, sym, 4)  # STATUS
        g.press("circle", vsyncs=2, settle=0)
        paths = []
        for k in range(FRAMES):
            g.run(1)
            paths.append(d / f"{k:02d}.png")
            g.present_screenshot(paths[-1])
        return paths
    finally:
        g.close()


def grey(path):
    return np.asarray(Image.open(path).convert("L")).astype(float)


def scale_against(frame, ref):
    """The horizontal stretch (about the emblem's centre, with a shift) that best maps `ref` onto `frame`, and its correlation."""
    h, w = ref.shape
    box = (slice(int(h * .3), int(h * .97), 3), slice(int(w * .40), int(w * .64), 3))
    r, f = ref[box], frame[box]
    norm = lambda a: (a - a.mean()) / (np.linalg.norm(a - a.mean()) + 1e-9)
    f = norm(f)
    best = (-1.0, 1.0)
    width = r.shape[1]
    for s in np.arange(0.8, 1.6, 0.02):
        m = ndimage.zoom(r, (1.0, s), order=1)
        for dx in range(-24, 25, 4):
            out = np.zeros_like(r)
            x0 = int(width / 2 - m.shape[1] / 2 + dx)
            lo, hi = max(0, x0), min(width, x0 + m.shape[1])
            if hi - lo < 40:
                continue
            out[:, lo:hi] = m[:, lo - x0:hi - x0]
            c = float((f * norm(out)).sum())
            if c > best[0]:
                best = (c, float(s))
    return best[1], best[0]


def scales(paths):
    ref = grey(paths[-1])
    h, w = ref.shape
    box = (slice(int(h * .3), int(h * .97)), slice(int(w * .40), int(w * .64)))
    out = []
    for p in paths[:-1]:
        f = grey(p)
        if f[box].mean() < 1.5:  # (no light in the emblem's region yet: the field, black)
            continue
        s, c = scale_against(f, ref)
        if c > 0.9:  # (the picture is the emblem: the field behind a fade-in is not)
            out.append((p.name, s, c))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out")
    args = ap.parse_args()
    build()
    rood_rpc.BINARY = pathlib.Path(os.environ.get("ROOD_MENU_SCALE_BINARY", ROOT / "pc/build/dev64/rood_host"))
    os.environ["ROOD_SYMBOLS"] = str(rood_rpc.BINARY)
    out = pathlib.Path(args.out) if args.out else OUT / "menu_open_scale"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    for name, fade, wide, claims in (("plain", "plain", True, False), ("original", "original", True, False),
                                     ("plain_claims", "plain", True, True), ("plain_4x3", "plain", False, False)):
        measured = scales(open_status(out, name, fade, wide, claims))
        spread = [m[1] for m in measured]
        print(f"      {name}: {len(measured)} frames with the emblem, scale min {min(spread, default=0):.2f} max {max(spread, default=0):.2f}")
        check(len(measured) >= 12, f"{name}: the emblem is found in {len(measured)} frames of the open")
        check(bool(spread) and LOW <= min(spread) and max(spread) <= HIGH,
              f"{name}: the emblem keeps its size through the open (scale {min(spread, default=0):.2f} .. {max(spread, default=0):.2f}, "
              f"within {LOW} .. {HIGH})")
    print(f"{len(failures)} failure(s)" if failures else "menu_open_scale_smoke: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
