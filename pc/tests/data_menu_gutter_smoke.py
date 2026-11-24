#!/usr/bin/env python3
"""The Data page's picture hides what the game draws behind it, in 16:9 as in 4:3 (gutters of the view).

    python3 pc/tests/data_menu_gutter_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The main menu's DATA / SAVE / LOAD page (the original screen, ui_menus off) keeps the field HUD drawn behind its picture: the
picture is opaque over the whole 4:3 screen, which hides the HUD. In 16:9 (widescreen) the HUD's panels hug the view's edges
and the picture reaches only the 4:3 part, so a clipped strip of the HP gauge stayed drawn in the left side beside it, and a
strip in the right. A black tile behind the picture, spread over the view (58EC.c _coverViewBehindPicture),
covers them.

- widescreen (aspect 2, widescreen 1; the window is 960x720, the view 960x540 from y 90, the 4:3 picture x 160..800):
  the left side (x < 150) is black
- ... and so is the right side past the menu's bars (x >= 912)
- (4:3 has no sides: its picture is the whole screen, and the same frames as before the tile)
- shots (--out): wide.png"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
import card_fixtures as cf  # noqa: E402
import ui_data_smoke as u  # noqa: E402
from PIL import Image  # noqa: E402

LEVEL = 60  # r + g + b above this is drawn (the sides are black)


def lit(path, x0, x1, y0, y1):
    im = Image.open(path).convert("RGB")
    return sum(1 for x in range(x0, x1) for y in range(y0, y1) if sum(im.getpixel((x, y))) > LEVEL)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(cf.default_save()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "data_menu_gutter"
    out.mkdir(parents=True, exist_ok=True)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    with cf.card_guard(out / "cards", source=pathlib.Path(args.card)) as cards:
        for name, settings in (("wide", ["aspect 2", "widescreen 1"]),):
            s = u.Session(name, out, cards.card1, modern=False)
            try:
                s.g.call("settings", apply=settings)
                s.open_data()
                s.g.run(30)
                s.g.present_screenshot(out / f"{name}.png")
            finally:
                s.close()
            left = lit(out / f"{name}.png", 0, 150, 90, 630)
            right = lit(out / f"{name}.png", 912, 960, 90, 630)
            check(left == 0, f"{name}: the left side is black ({left} drawn pixels)")
            check(right == 0, f"{name}: the right side past the bars is black ({right} drawn pixels)")
    if failures:
        print(f"data_menu_gutter_smoke: {len(failures)} failed")
        return 1
    print("data_menu_gutter_smoke: all ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
