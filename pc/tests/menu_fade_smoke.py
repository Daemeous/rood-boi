#!/usr/bin/env python3
"""The menu's fade (settings menu_fade plain | original; MAINMENU 58EC.c _renderBackground, and the classic Lua's
menu_background, pc/plugins/rood.classic/main.lua).

Opening the Items screen from the field's command menu fades the picture over eight game frames (16 vsyncs), and
closing it fades back. The game's own fade (menu_fade original) adds the menu's picture to a half blend of the
previous screen: the field never goes dark (the picture's blue is added to it: the hue drifts), and the previous
screen is a PS1-resolution copy of the frame (the HUD's text in it is blocky, doubled under the live text). menu_fade
plain (the default) dims the field evenly to black under a black tile at an opacity and then brings the picture in from
black: by brightness alone, no subtraction, no previous screen.

At 1280 x 720, over the vsyncs after Circle opens Items (each present read back, RPC present_screenshot):
  - plain: the field's brightness falls (never rises) until it is black, in the field's own colour (the chromaticity,
    red / green / blue shares of the light, stays within HUE_SLACK of the first frame's through the fade's first half
    while 0.4 of the light is left), down to DIP_DEPTH of what it was; then the menu's picture comes in;
  - original: the same check fails (its hue drifts past HUE_SLACK as the picture's blue is added, and it never gets that
    dark): the test's own sanity.
Over the vsyncs after Cross closes Items (the target, RPC gpu_dump, at the internal resolution):
  - plain: the HUD's digits (the field's, behind the menu) are smooth font text in every fade frame: the share of
    scale x scale blocks of one exact colour (a PS1-resolution picture of them: every block) stays near the rest
    frame's (BLOCK_SLACK).

    python3 pc/tests/menu_fade_smoke.py [--out DIR]
"""
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

FRAMES = 24          # vsyncs read after each press (the fade takes 16)
HUE_SLACK = 0.012    # the chromaticity's drift while the frame still has light
DIP_DEPTH = 0.2      # the darkest frame's brightness, of the first's (plain: about 0.1; the original's about 0.6)
BRIGHT_SLACK = 0.5   # brightness may wobble by this much (of 255) between frames of the fall
BLOCK_SLACK = 0.2    # a fade frame's HUD digits in uniform blocks, over the rest frame's
HUD_BOX = (20, 6, 40, 16)  # the HUD's digits (PS1 pixels: x, y, w, h in the displayed frame buffer)
SETTINGS = "ui_upscale mmpx\nui_text font\nui_text_overlay auto\nwidescreen 1\naa msaa2+fxaa\ninternal_resolution 3\n"

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


FIELD = (0.15, 0.3, 0.6, 0.85)  # the field's part of the frame (x0, y0, x1, y1 of its size: no HUD, no command menu)


def light(path):
    """The field's mean light per channel (0..255)."""
    im = np.asarray(Image.open(path).convert("RGB")).astype(float)
    h, w, _ = im.shape
    return im[int(h * FIELD[1]):int(h * FIELD[3]), int(w * FIELD[0]):int(w * FIELD[2])].reshape(-1, 3).mean(axis=0)


def entry_fade(lights):
    """(dive ok, hue drift, depth): the brightness (mean of the channels) falls until its least, the chromaticity of
    the frames that still have light, the least brightness of the first's."""
    bright = [float(m.mean()) for m in lights]
    least = int(np.argmin(bright))
    falls = all(bright[i + 1] <= bright[i] + BRIGHT_SLACK for i in range(least))
    first = lights[0] / max(lights[0].sum(), 1e-6)
    drift = max((float(np.abs(m / m.sum() - first).max()) for m in lights[:8] if m.mean() > 0.4 * bright[0]),
                default=0.0)
    return falls, drift, bright[least] / max(bright[0], 1e-6), bright


def blocks(path, k, display_x):
    """The share of k x k blocks (PS1 pixels) of the HUD's digits with ink that are one exact colour."""
    im = np.asarray(Image.open(path)).astype(np.int32)
    x, y, w, h = HUD_BOX
    c = im[y * k:(y + h) * k, (display_x + x) * k:(display_x + x + w) * k].reshape(h, k, w, k, 3)
    spread = (c.max(axis=(1, 3)) - c.min(axis=(1, 3))).max(axis=-1)
    inked = c.max(axis=(1, 3, 4)) > 24
    return float(((spread == 0) & inked).sum() / max(int(inked.sum()), 1))


class Session:
    def __init__(self, out, name, menu_fade):
        import classic_parity as cp
        from menu_nav import top_command
        from weapon_slots_smoke import default_card
        self.cp, self.top_command = cp, top_command
        self.out = out / name
        self.out.mkdir(parents=True, exist_ok=True)
        settings = self.out / "settings.cfg"
        settings.write_text(SETTINGS + f"menu_fade {menu_fade}\n")
        card = self.out / "card1.mcd"
        shutil.copyfile(default_card(), card)
        self.game = launch_at("save_room", card=card, fast=True, log=self.out / "game.log",
                              socket_path=f"/tmp/rood-menu-fade-{name}-{os.getpid()}.sock",
                              extra_env={"ROOD_SETTINGS": str(settings), "ROOD_WINDOW": "1280x720", "ROOD_AUDIO": "none"})
        self.sym = symbols()

    def to_items_command(self):
        g, sym = self.game, self.sym
        g.run(120)
        for _ in range(8):
            if g.read(sym["vs_battle_menuState"], 1)[0] & 0x3F != 0x3F:
                break
            g.press("triangle", vsyncs=3, settle=40)
        g.run(40)
        self.top_command(g, sym, self.cp.ITEMS_COMMAND)

    def opening(self):
        """Circle on Items, the presents after it."""
        g = self.game
        g.press("circle", vsyncs=2, settle=0)
        paths = []
        for k in range(FRAMES):
            g.run(1)
            paths.append(self.out / f"open_{k:02d}.png")
            g.present_screenshot(paths[-1])
        return paths

    def closing(self):
        """Cross on Items; the target (PPM) at each vsync after it, and the VRAM X of the frame shown."""
        g = self.game
        self.cp.items_ready(g, self.sym)
        g.run(40)
        g.press("cross", vsyncs=2, settle=0)
        frames = []
        for k in range(FRAMES):
            g.run(1)
            path = self.out / f"close_{k:02d}.ppm"
            g.call("gpu_dump", path=str(path))
            frames.append((path, g.call("ui_upscale")["display"][0]))
        g.run(40)
        path = self.out / "rest.ppm"
        g.call("gpu_dump", path=str(path))
        return frames, (path, g.call("ui_upscale")["display"][0])

    def close(self):
        self.game.close()


def lane(out, name, menu_fade):
    s = Session(out, name, menu_fade)
    try:
        if not s.game.call("graphics").get("hardware"):
            print("skip: the software renderer draws no fade at an opacity")
            return None
        s.to_items_command()
        lights = [light(p) for p in s.opening()]
        falls, drift, depth, bright = entry_fade(lights)
        s.cp.items_ready(s.game, s.sym)
        closing = s.closing()
        return falls, drift, depth, bright, closing, s.game.call("graphics")["scale"]
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "menu_fade"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)

    plain = lane(out, "plain", "plain")
    if plain is None:
        return 0
    falls, drift, depth, bright, (frames, rest), k = plain
    check(falls, f"plain: the field's brightness falls to its least without rising (frames {[round(b, 1) for b in bright[:14]]})")
    check(drift <= HUE_SLACK, f"plain: the field keeps its colour while it dims (chromaticity drift {drift:.4f}, at most {HUE_SLACK})")
    check(depth <= DIP_DEPTH, f"plain: the field dims to black ({depth:.2f} of the first frame's light, at most {DIP_DEPTH})")
    base = blocks(rest[0], k, rest[1])
    worst = max(blocks(p, k, x) for p, x in frames)
    check(worst <= base + BLOCK_SLACK,
          f"plain: the HUD's digits are smooth text in every closing fade frame ({worst:.2f} of their blocks uniform, "
          f"at rest {base:.2f})")

    original = lane(out, "original", "original")
    falls, drift, depth, bright, *_ = original
    check(drift > HUE_SLACK and depth > DIP_DEPTH,
          f"original: the same checks fail (hue drift {drift:.4f}, at most {HUE_SLACK}; depth {depth:.2f}, at most "
          f"{DIP_DEPTH}; brightness {[round(b, 1) for b in bright[:14]]})")

    print(f"{len(failures)} failure(s)" if failures else "menu_fade_smoke: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
