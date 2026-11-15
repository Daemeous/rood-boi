#!/usr/bin/env python3
"""A classic panel (rood.draw.panel with {classic = true}, the classic look) is drawn at the game's frame.

A panel of a window, a half-transparent rectangle and a gradient is set from a plugin's console in the field. Checks:
  - the panel is on the picture (a positive control: its rectangle differs from the one before it)
  - the frames with it are the same from frame to frame at rest (a held panel does not flicker)
Per-frame hashes of the panel's rectangle are written to $ROOD_PANEL_HASHES (a file), so two builds can be compared:
    ROOD_BINARY=old ... ; ROOD_BINARY=new ... ; diff the files.
"""
import argparse
import hashlib
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402

PANEL = ("local function draw(d) d.window(24, 24, 200, 90, 128) d.rect(40, 40, 80, 30, 0x2040A0, 0.5) "
         "d.gradient(40, 76, 120, 20, 0xFF0000, 0x0000FF) end "
         "rood.draw.panel(draw, { classic = true }) return true")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "classic_panels"
    out.mkdir(parents=True, exist_ok=True)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    settings = out / "settings.cfg"
    settings.write_text("ui_menus 0\nui_text font\ninterpolate 0\n")
    env = {"ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(out / "plugins"), "ROOD_PLUGINS_SHIPPED": str(ROOT / "pc/plugins"),
           "ROOD_PLUGIN_DATA": str(out / "plugin_data")}
    (out / "plugins").mkdir(exist_ok=True)
    with card_guard(out / "cards", source=args.card) as cards:
        g = launch_at("save_room", card=cards.card1, fast=True, log=out / "game.log", extra_env=env)
        try:
            def region():
                shot = out / "shot.ppm"
                g.screenshot(str(shot))
                data = shot.read_bytes()
                parts = data.split(b"\n", 3)
                w, h = (int(v) for v in parts[1].split())
                pixels = parts[3]
                # the window's body, in the picture's pixels (the game's 320 x 224 view fills the picture's height)
                scale = h / 224.0
                shift = (w - int(320 * scale)) // 2 if w > int(320 * scale) else 0

                def crop(box):
                    x0, x1, y0, y1 = (int(v * scale) for v in box)
                    return hashlib.sha256(b"".join(pixels[(y * w + x0 + shift) * 3:(y * w + x1 + shift) * 3]
                                                   for y in range(y0, y1))).hexdigest()[:16]

                # the window's body (steady at rest) and the window with its frame and shadow (the scene shows past it)
                return crop((36, 212, 36, 106)), crop((22, 232, 22, 122))

            g.run(10)
            before, before_wide = region()
            reply = g.repl(op="eval", plugin="rood.classic", code=PANEL)
            check(reply.get("ok", True), f"the panel is set ({str(reply)[:80]})")
            hashes, wide = [], []
            for _ in range(6):
                g.run(4)
                inner, outer = region()
                hashes.append(inner), wide.append(outer)
            check(before not in hashes, "positive control: the picture with the panel differs from the one before it")
            check(len(set(hashes)) == 1, f"at rest the panel's frames are the same ({len(set(hashes))} distinct of 6)")
            path = os.environ.get("ROOD_PANEL_HASHES")
            if path:
                pathlib.Path(path).write_text("\n".join([before_wide] + wide) + "\n")
        finally:
            g.close()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
