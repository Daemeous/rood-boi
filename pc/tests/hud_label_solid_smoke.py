#!/usr/bin/env python3
"""The HUD's stat labels (HP, MP, RISK) are drawn solid, like their numbers
(src/BATTLE/BATTLE.PRG/5BF94.c _renderStatBar, ROOD_PC).

    python3 pc/tests/hud_label_solid_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The original draws the labels as semi-transparent sprites (GP0 0x66, the
first argument 0x180 to setSpriteDefaultTexPage). Their CLUT (864, 223) has
one entry with the STP bit, the outline (entry 1, black): drawn
semi-transparent it halves the field instead of covering it, so the
lettering's outline is see-through (the ui_text font's outline keeps the
entry's STP bit too); the digits beside them are opaque sprites over the
same kind of CLUT, their outline solid black. On PC the labels are opaque.

From a copy of the card inside card_fixtures.card_guard (skipped without
one; the player's cards fingerprinted before and after), in the save room, 4:3 in a 960x720 window, with ui_text
font and then original: the HUD's label boxes are dumped (present_dump),
the field camera is turned with the right stick (the ground behind the HUD
changes), dumped again. While the ground around them changes: the label
lettering (its bright pixels) and the digits stay the same within a few
levels, and the labels' boxes hold solid black outline pixels, the same in
both images, about as many as the digits' outline per lettering pixel
(before: the outline was the field at half brightness, changing with it;
almost no black pixel).
--out keeps the crops (game imagery: keep it out of git)."""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402

TOLERANCE = 6     # levels a solid pixel may move (filtering at the edges of the lettering)
BG_CHANGE = 12    # mean change of the ground around the labels that makes the test meaningful



def present(g, path):
    import numpy as np
    from PIL import Image
    g.wait_presents(3)
    ppm = pathlib.Path(path).with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    g.call("present_dump", path=str(ppm.resolve()))
    for _ in range(200):
        if ppm.exists():
            break
        g.wait_presents(1)
    image = np.asarray(Image.open(ppm).convert("RGB")).astype(np.int16)
    ppm.unlink()
    return image


def label_boxes(g):
    """The HP and MP labels' boxes in PS1 pixels (x, y, w, h): _renderStatBar's sprite (RISK's box is
    "RISK  ZERO"'s, drawn as opaque text, at zero risk)."""
    xy = g.read_symbol("D_800EBBEC", 16)
    widths = g.read_symbol("D_800EBBFC", 4)
    slide = g.u32("D_800EB9B0")
    boxes = []
    for i in range(2):
        v = int.from_bytes(xy[4 * i:4 * i + 4], "little") - slide
        x, y = v & 0xFFFF, (v >> 16) & 0xFFFF
        if widths[i] and x < 320 and y < 240:
            boxes.append((x, y, widths[i], 9))
    return boxes, slide


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="keep the crops here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "hud_label_solid"
    out.mkdir(parents=True, exist_ok=True)
    # the player's cards fingerprinted before and after (card_guard), the game on a temp copy
    with card_guard(out / "cards", source=args.card) as cards:
        return run(args, out, cards.card1)


def run(args, out, card):
    import numpy as np
    from PIL import Image
    settings = out / "settings.cfg"
    settings.write_text("aspect 4:3\n")
    env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(settings)}
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        g.call("graphics", interpolate=False, aa="off")
        for mode in ("font", "original"):
            g.call("graphics", ui_text=mode)
            g.run(20)
            boxes, slide = label_boxes(g)
            if not check(len(boxes) == 2 and slide == 0, f"{mode}: the HUD shows its labels ({boxes}, slide {slide})"):
                continue
            a = present(g, out / f"{mode}-a")
            g.call("input", right_stick=[1, 0], vsyncs=45)
            g.run(20)
            b = present(g, out / f"{mode}-b")
            h, w = a.shape[:2]
            sx, sy = w / 320, h / 240
            lum_a, lum_b = a @ np.array([299, 587, 114]) / 1000, b @ np.array([299, 587, 114]) / 1000
            black = (a.max(axis=2) <= 2) & (b.max(axis=2) <= 2)
            boxes_mask = np.zeros((h, w), bool)
            around = np.zeros((h, w), bool)
            for x, y, bw, bh in boxes:
                x0, y0, x1, y1 = int(x * sx), int(y * sy), int((x + bw) * sx), int((y + bh) * sy)
                boxes_mask[y0:y1, x0:x1] = True
                around[max(0, y0 - 12):y1 + 12, max(0, x0 - 12):x1 + 12] = True
            around &= ~boxes_mask
            lettering = boxes_mask & (np.minimum(lum_a, lum_b) > 150)
            # the digits: everything bright in the HUD's corner outside the label boxes, their outline the black
            # pixels next to them
            corner = np.zeros((h, w), bool)
            corner[:int(40 * sy), :int(110 * sx)] = True
            digits = corner & ~boxes_mask & (np.minimum(lum_a, lum_b) > 200)
            near_digits = np.zeros((h, w), bool)
            ys, xs = np.nonzero(digits)
            for dy in range(-4, 5):
                for dx in range(-4, 5):
                    near_digits[np.clip(ys + dy, 0, h - 1), np.clip(xs + dx, 0, w - 1)] = True
            digit_outline = corner & ~boxes_mask & near_digits & black
            label_outline = boxes_mask & black
            d = np.abs(a - b).max(axis=2)
            bg = float(d[around].mean())
            if args.out:
                for name, image in (("a", a), ("b", b)):
                    Image.fromarray(image[:int(48 * sy), :int(120 * sx)].astype(np.uint8)).save(
                        out / f"{mode}-{name}.png")
            check(bg > BG_CHANGE, f"{mode}: the ground around the labels changed (mean {bg:.1f} levels)")
            worst_l = int(np.percentile(d[lettering], 99)) if lettering.any() else -1
            worst_d = int(np.percentile(d[digits], 99)) if digits.any() else -1
            check(lettering.sum() > 100 and worst_l <= TOLERANCE,
                  f"{mode}: the labels' lettering is solid: {int(lettering.sum())} pixels, 99th percentile change "
                  f"{worst_l} (<= {TOLERANCE})")
            check(digits.sum() > 100 and worst_d <= TOLERANCE,
                  f"{mode}: the digits are solid: {int(digits.sum())} pixels, 99th percentile change {worst_d}")
            rl = label_outline.sum() / max(1, lettering.sum())
            rd = digit_outline.sum() / max(1, digits.sum())
            check(rd > 0.2 and rl > 0.4 * rd,
                  f"{mode}: the labels' outline is solid black like the digits': {int(label_outline.sum())} black "
                  f"pixels in the label boxes, {rl:.2f} per lettering pixel (the digits' {rd:.2f}, want at least "
                  f"{0.4 * rd:.2f})")
    finally:
        g.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
