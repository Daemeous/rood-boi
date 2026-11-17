#!/usr/bin/env python3
"""Text without a box fading out (BATTLE 5BF94.c _renderTextImmediate; ROOD_PC:
pc/src/gpu_hw.c pc_hw_opacity_packets).

A text box of type 0 (text with no bubble: narration) closes by fading its
text out, and one of type 6 draws its text that way all the time: the game
reads the box's rectangle of the frame buffer back, blends the glyphs into it
on the CPU and uploads it again. With the hardware renderer that frame buffer
is the PS1-resolution mirror, so the upload used to put a pixelated
rectangle of an older picture over the scene, the old text faintly inside in
the original font. Now the text is drawn as glyph sprites at the fading
opacity.

The opening's first talk bubble (checkpoint first_bubble), made type 0 by an
RPC write once it has settled, then closed (Circle until it does), with the
settings of the report (ui_upscale mmpx, ui_text font, ui_text_overlay auto,
widescreen, aa msaa2+fxaa) in a 1280 x 720 window. In the internal-resolution
target (RPC gpu_dump) inside the box's rectangle (the displayed frame buffer), the
share of scale x scale blocks of one exact colour (an upload of PS1 pixels:
every block):
  - while it closes stays near the open frame's (BLOCK_SLACK), with the text
    fading: the first closing frame differs from the frame after it closed
    nearly as much as the open frame does (its text is drawn whole), the last
    much less;
  - the same with ui_text original (the PS1's glyphs: opacity without
    distance texels, drawn into the target), and at internal resolution 2
    (font text held for the window's resolution: ui_text_overlay; the dump
    bakes it);
  - a box of type 6 (that path every frame): no rectangle, its text drawn;
  - ROOD_OPACITY_2D=0 (the game's read-back and upload, a session of its
    own): the check sees the rectangle (BLOCK_BUG and more).

    python3 pc/tests/text_fade_smoke.py [--out DIR]
"""
import argparse
import os
import pathlib
import shutil
import struct
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402

BLOCK_SLACK = 0.15    # closing: at most this much more of the rectangle in uniform blocks than when open
BLOCK_BUG = 0.8       # the upload: (nearly) every block uniform
SETTINGS = "ui_upscale mmpx\nui_text font\nui_text_overlay auto\nwidescreen 1\naa msaa2+fxaa\n"

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


class Session:
    def __init__(self, out, name, env):
        self.out = out / name
        self.out.mkdir(parents=True, exist_ok=True)
        settings = self.out / "settings.cfg"
        settings.write_text(SETTINGS)
        self.game = launch_at("first_bubble", log=self.out / "game.log",
                              socket_path=f"/tmp/rood-text-fade-{name}-{os.getpid()}.sock",
                              extra_env={"ROOD_SETTINGS": str(settings), "ROOD_WINDOW": "1280x720", **env})
        self.base = "vs_battle_textBoxes[0]"  # (box 0)

    def box(self):
        g, b = self.game, self.base  # (the members by name: the box holds a host pointer)
        return {"flags": g.value(b + ".flags"), "x": g.value(b + ".x"), "y": g.value(b + ".y"),
                "state": g.value(b + ".state"), "duration": g.value(b + ".unk22"),
                "w": g.value(b + ".charsPerLine") * 12 + 10, "h": g.value(b + ".lineCount") * 13 + 4}

    def settle(self):
        """Until box 0 is open and settled (its text printing)."""
        for _ in range(400):
            b = self.box()
            if b["state"] > 0 and b["state"] == b["duration"] + 1:
                return b
            self.game.run(2)
        raise RpcError("the opening's first bubble did not open")

    def retype(self, kind):
        b = self.box()
        self.game.store(self.base + ".flags", (b["flags"] & ~0xF) | kind)

    def dump(self, name):
        """The target, and the displayed frame buffer's VRAM X (the frame just drawn)."""
        path = self.out / f"{name}.ppm"
        self.game.call("gpu_dump", path=str(path))
        im = np.asarray(Image.open(path)).astype(np.int32)
        path.unlink()
        return im, self.game.call("ui_upscale")["display"][0]

    def redraw(self):
        """Back to the mark; both frame buffers drawn again (a rewind leaves the target VRAM's upload)."""
        self.game.rewind()
        self.game.run(8)

    def close(self):
        self.game.close()


def crop(frame, b, k):
    """The box's rectangle (PS1 pixels) in the frame buffer the dump displays, at scale k."""
    im, display_x = frame
    x, y = b["x"] + display_x, b["y"]
    return im[y * k:(y + b["h"]) * k, x * k:(x + b["w"]) * k]


def blocks(frame, b, k):
    """The share of k x k blocks (the PS1 pixels) inside the box of one exact colour."""
    c = crop(frame, b, k).reshape(b["h"], k, b["w"], k, 3)
    return float(((c.max(axis=(1, 3)) - c.min(axis=(1, 3))).max(axis=-1) == 0).mean())


def diff(a, c, b, k):
    return float(np.abs(crop(a, b, k) - crop(c, b, k)).mean())


def bright(frame, b, k):
    """Pixels of near-white ink (the text) inside the box."""
    return int((crop(frame, b, k).min(axis=-1) > 170).sum())


def close_box(s):
    """Circle until box 0 closes; the target at each closing vsync, and once it has closed."""
    frames = []
    for i in range(900):
        if i % 30 == 0 and not frames:
            s.game.call("input", buttons=["circle"], vsyncs=3)
        b = s.box()
        if b["state"] < 0:
            frames.append(s.dump(f"closing{len(frames):02d}"))
        elif frames:
            return frames, s.dump("closed")
        s.game.run(1)
    raise RpcError(f"box 0 did not close ({len(frames)} closing frames seen)")


def fade_case(s, b, k):
    """Box 0 (type 0) closing from the mark: the rectangle and the fade."""
    s.redraw()
    opened = s.dump("open")
    frames, closed = close_box(s)
    base = blocks(opened, b, k)
    worst = max(blocks(f, b, k) for f in frames)
    d_open = diff(opened, closed, b, k)
    d_first = diff(frames[0], closed, b, k)
    d_last = diff(frames[-1], closed, b, k)
    return base, worst, d_open, d_first, d_last, len(frames)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "text_fade"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)

    s = Session(out, "fix", {})
    try:
        if not s.game.call("graphics").get("hardware"):
            print("skip: the software renderer draws this as the game does (no hardware renderer)")
            return 0
        s.settle()
        s.retype(0)
        s.game.run(40)
        s.game.mark()
        b = s.box()
        print(f"box 0: type 0 at {b['x']},{b['y']} {b['w']} x {b['h']}, closing over {b['duration']} frames")
        cases = [("font", {}), ("original", {"ui_text": "original"}), ("overlay", {"ui_text": "font", "scale": 2})]
        for name, graphics in cases:
            if graphics:
                s.game.call("graphics", **graphics)
            k = s.game.call("graphics")["scale"]
            before = s.game.call("ui_text")["overlay"]["presented"]
            base, worst, d_open, d_first, d_last, n = fade_case(s, b, k)
            check(worst <= base + BLOCK_SLACK,
                  f"{name} ({k}x): no PS1-resolution rectangle while the text fades ({n} frames): at most "
                  f"{worst:.2f} of the box in uniform blocks (open {base:.2f})")
            check(d_first >= 0.6 * d_open and 0 < d_last <= 0.6 * d_first,
                  f"{name}: the text fades out (difference from the closed frame: open {d_open:.2f}, "
                  f"first closing {d_first:.2f}, last {d_last:.2f})")
            if name == "overlay":
                after = s.game.call("ui_text")["overlay"]["presented"]
                check(after > before, f"overlay: text was held for the window's resolution ({after - before} "
                                      f"triangles presented)")
        s.game.call("graphics", ui_text="font", scale=4)
        k = s.game.call("graphics")["scale"]
        # Type 6: the text drawn through that path every frame.
        s.redraw()
        opened = s.dump("open6")
        s.retype(6)
        s.game.run(6)
        six = s.dump("type6")
        base, worst = blocks(opened, b, k), blocks(six, b, k)
        ink, ink6 = bright(opened, b, k), bright(six, b, k)
        check(worst <= base + BLOCK_SLACK and ink6 >= 0.5 * ink,
              f"type 6: its text drawn without a rectangle ({worst:.2f} uniform, open {base:.2f}; ink {ink6} "
              f"pixels, open {ink})")
    finally:
        s.close()

    s = Session(out, "off", {"ROOD_OPACITY_2D": "0"})
    try:
        s.settle()
        s.retype(0)
        s.game.run(40)
        s.game.mark()
        k = s.game.call("graphics")["scale"]
        base, worst, *_ = fade_case(s, s.box(), k)
        check(worst >= BLOCK_BUG and worst > base + 2 * BLOCK_SLACK,
              f"ROOD_OPACITY_2D=0 (the game's upload): the check sees the rectangle ({worst:.2f} of the box in "
              f"uniform blocks, open {base:.2f})")
    finally:
        s.close()

    print(f"{len(failures)} failure(s)" if failures else "text_fade_smoke: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
