#!/usr/bin/env python3
"""ui_text (pc/src/ui_text.c; README "2D / UI"): the game's text drawn from
open-licensed fonts.

    python3 pc/tests/ui_text_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

Runs on the backend the environment selects (run.py: 32-bit GL, 64-bit GL,
or ROOD_RENDERER=vulkan). Comparisons use the same recorded frame drawn
twice (RPC ui_upscale redraw), once with ui_text original and once with
ui_text font, so nothing moves between the two images. Checks:

- default: ui_text original, and while it is off nothing goes through the
  2D atlas (no lookups, no font regions): the original drawing path;
- the opening's first talk bubble (dialogue), and with a card (a copy of
  it) the save room's HUD (digits, RISK ZERO) and the in-game menu (menu
  rows, information box): the font changes the picture, only inside the
  glyph boxes the game drew (RPC ui_upscale mask), every glyph region is
  drawn from a font (none fell back), and each text block's ink keeps the
  original's extent within two PS1 pixels (the layout is the game's);
- original after font: the frame drawn with ui_text original again equals
  the first original drawing;
- fixed labels (text baked into images): with ui_text font the HUD's HP and
  MP, the menu's header and the information box's INFORMATION are drawn
  from the font (RPC ui_text labels_drawn), never while it is original;
  HP drawn alone (RPC ui_text render) keeps its ink inside the label's box
  at the original's height;
- the L2 shortcut cross (held L2 in the save room): its labels (ITEMS,
  DEFENSE, CHAIN; BREAK ARTS when unlocked) and connector dots are drawn
  from the font, the same frame changing only inside the boxes the game
  drew; the dot drawn alone (RPC ui_text render) is a round disc in the
  original's place: centred on the original dot, symmetric, lettering
  colour inside a dark outline, inside its 6 x 6 box;
- HUD digits (RPC ui_text render of each digit box): every digit of each
  size keeps at least the digits style's aspect (DIGITS_ASPECT, less a
  small tolerance) of the width/height it has at aspect 1 (the font's own
  proportions), and neighbours at the game's advances (5 small, 6 large)
  never touch;
- settings: ui_text saves and loads (ROOD_SETTINGS); RPC ui_text style
  overrides apply;
- no shader errors, Vulkan validation messages (ROOD_VK_VALIDATE where the
  layer exists) or crashes.

--shots keeps side-by-side images (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game  # noqa: E402

failures = []


def check(ok, what):
    print(("PASS: " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)
    return ok


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def redraw(game, work, mode, name):
    game.call("graphics", ui_text=mode)
    path = work / f"{name}-{mode}.ppm"
    info = game.call("ui_upscale", redraw=str(path))
    image = np.asarray(Image.open(path))
    path.unlink()
    return image, info


def ink(image, polarity):
    """Text ink: dark pixels on a light bubble, bright ones on dark UI."""
    luma = image[..., :3].astype(int) @ np.array([299, 587, 114]) // 1000
    return luma < 90 if polarity == "dark" else luma > 170


def compare(game, work, name, shots, label, polarity):
    """Original and font drawings of the same frame."""
    game.call("graphics", ui_text="font")
    game.call("ui_upscale", mask=str(work / "mask.pgm"))  # clear
    game.run(6)  # frames drawn with the font (the mask accumulates)
    game.call("ui_upscale", mask=str(work / "mask.pgm"))
    mask = np.asarray(Image.open(work / "mask.pgm")) > 0
    original, info = redraw(game, work, "original", name)
    font, _ = redraw(game, work, "font", name)
    again, _ = redraw(game, work, "original", name + "-again")
    stats = game.call("ui_text")
    scale = original.shape[1] // 1024
    x, y, w, h = info["area"]
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, 1024), min(y + h, 512)
    a = original[y0 * scale:y1 * scale, x0 * scale:x1 * scale]
    b = font[y0 * scale:y1 * scale, x0 * scale:x1 * scale]
    c = again[y0 * scale:y1 * scale, x0 * scale:x1 * scale]
    m = mask[y0:y1, x0:x1]
    # One VRAM pixel of slack: a primitive's box is rounded outwards.
    grown = ndimage.binary_dilation(m, iterations=1)
    big = np.repeat(np.repeat(grown, scale, axis=0), scale, axis=1)
    diff = np.any(a != b, axis=2)
    outside = int((diff & ~big).sum())
    check(int(diff.sum()) > 0, f"{label} {name}: the font changes the text ({int(diff.sum())} pixels)")
    check(outside == 0, f"{label} {name}: nothing changes outside the glyph boxes ({outside} pixels)")
    check(np.array_equal(a, c), f"{label} {name}: original after font draws the same picture")
    check(stats["regions"] > 0 and stats["failed"] == 0,
          f"{label} {name}: {stats['regions']} glyph regions drawn from fonts, {stats['failed']} fell back")
    # Layout: each text block (glyph boxes joined into words and lines)
    # keeps its ink extent within two PS1 pixels.
    blocks, count = ndimage.label(ndimage.binary_dilation(grown, iterations=3))
    ia, ib = ink(a, polarity), ink(b, polarity)
    worst, measured = 0, 0
    for k, sl in enumerate(ndimage.find_objects(blocks), 1):
        region = (blocks[sl] == k) & grown[sl]
        big_region = np.repeat(np.repeat(region, scale, axis=0), scale, axis=1)
        outer = (slice(sl[0].start * scale, sl[0].stop * scale), slice(sl[1].start * scale, sl[1].stop * scale))
        pa, pb = ia[outer] & big_region, ib[outer] & big_region
        if pa.sum() < 8 * scale * scale or pb.sum() < 8 * scale * scale:
            continue
        ya, xa = np.where(pa)
        yb, xb = np.where(pb)
        edges = [abs(xa.min() - xb.min()), abs(xa.max() - xb.max()), abs(ya.min() - yb.min()),
                 abs(ya.max() - yb.max())]
        worst = max(worst, max(edges) / scale)
        measured += 1
    check(measured > 0 and worst <= 2.0,
          f"{label} {name}: text blocks keep their extent ({measured} blocks, worst edge {worst:.1f} PS1 pixels)")
    if shots:
        side = np.concatenate([a, np.full((a.shape[0], 8, 3), 255, np.uint8), b], axis=1)
        image = Image.fromarray(side)
        if image.width > 1600:
            image = image.resize((1600, image.height * 1600 // image.width), Image.LANCZOS)
        image.save(shots / f"{label}-{name}.jpg", quality=82)


UI_PAGE = 12  # the field UI page (768, 0), 4-bit
CLUT_DIGITS = (223 << 6) | (832 >> 4)
CLUT_LABELS = (223 << 6) | (864 >> 4)


def render(game, work, region, factor=8):
    """RPC ui_text render: the region's font texels as (h, w) PS1 colours."""
    path = work / "render.raw"
    reply = game.call("ui_text", render=list(region), factor=factor, path=str(path))["render"]
    if not reply["ok"]:
        return None
    texels = np.fromfile(path, "<u2").reshape(reply["h"], reply["w"])
    path.unlink()
    return texels


def luma(texels):
    t = texels.astype(int)
    return (t & 31) * 299 + (t >> 5 & 31) * 587 + (t >> 10 & 31) * 114


def bright(texels):
    """The lettering, its anti-aliased edge included: neither the outline's
    colour (the darkest drawn) nor the colour key."""
    y = luma(texels)
    lit = y[texels != 0]
    return (texels != 0) & (y > lit.min()) if len(lit) else np.zeros(texels.shape, bool)


def check_digits(game, work):
    """Each digit size: drawn at the style's aspect and at aspect 1 (the
    font's own proportions, as wide as the box allows, shorter), the ink's
    width/height compared; neighbours at the game's advances."""
    aspect = {st["name"]: st for st in game.call("ui_text")["styles"]}["digits"]["aspect"]
    factor = 8

    def draw(name, u, w, h, step):
        inks = {}
        for d in range(10):
            texels = render(game, work, (UI_PAGE, CLUT_DIGITS, u + step * d, 0, u + step * d + w - 1, h - 1), factor)
            if texels is None:
                return None
            inks[d] = bright(texels)
        return inks

    def ratio(ink):
        ys, xs = np.where(ink)
        return (xs.max() - xs.min() + 1) / (ys.max() - ys.min() + 1)

    for name, u, w, h, step, advance in (("small", 0, 6, 10, 6, 5), ("large", 64, 7, 12, 8, 6)):
        inks = draw(name, u, w, h, step)
        if not check(inks is not None, f"digits {name}: drawn from the font"):
            return
        game.call("ui_text", style="digits", aspect=1.0)
        natural = draw(name, u, w, h, step)
        game.call("ui_text", style="digits", aspect=aspect)
        kept = {d: ratio(inks[d]) / ratio(natural[d]) for d in range(10)}
        worst = min(kept, key=kept.get)
        check(kept[worst] >= aspect - 0.08,
              f"digits {name}: every digit keeps at least {aspect} - 0.08 of the font's proportions "
              f"(least: {worst}, {kept[worst]:.2f})")
        gap = min(advance * factor + np.where(inks[b].any(0))[0].min() - np.where(inks[a].any(0))[0].max() - 1
                  for a, b in zip(range(9), range(1, 10)))
        check(gap >= 1, f"digits {name}: neighbours at the game's advance {advance} never touch "
                        f"(narrowest gap {gap / factor:.2f} PS1 pixels)")


def check_label_render(game, work):
    """HP alone: ink inside its box, at the original's height."""
    texels = render(game, work, (UI_PAGE, CLUT_LABELS, 144, 0, 159, 8), 8)
    if not check(texels is not None, "label HP: drawn from the font"):
        return
    ink = bright(texels)
    ys, xs = np.where(ink)
    height = (ys.max() - ys.min() + 1) / 8
    check(xs.min() >= 8 and xs.max() < texels.shape[1] - 8 and ys.min() >= 8 and ys.max() < texels.shape[0] - 8,
          "label HP: the lettering stays inside the box's outline")
    check(5.5 <= height <= 8.0, f"label HP: lettering {height:.1f} PS1 pixels tall (the original's 7)")


BATTLE_PAGE = 0x17  # the battle page (448, 256), 4-bit
CLUT_CROSS = (220 << 6) | (976 >> 4)  # the L2 shortcut cross's labels and dots
CROSS_WORDS = {"ITEMS", "DEFENSE", "CHAIN"}  # Break Arts may be locked on the card's save
DOT = "\u2022"  # ui_labels.h's connector dot


def check_cross(game, work, shots):
    """The L2 shortcut cross: labels and dots from the font (frame-exact
    against the original), and the dot drawn alone as a disc."""
    game.call("input", buttons=["l2"], vsyncs=230)  # released after compare's frames
    game.run(200)
    step = game.read_symbol("D_800EBCE8", 1)[0]
    if not check(step != 10, f"cross: the L2 shortcut cross is open (animation step {step})"):
        return
    compare(game, work, "cross", shots, "cross", "bright")
    drawn = set(game.call("ui_text")["labels_drawn"])
    check(CROSS_WORDS <= drawn and DOT in drawn, f"cross: labels and dots drawn from the font ({sorted(drawn)})")
    texels = render(game, work, (BATTLE_PAGE, CLUT_CROSS, 192, 128, 197, 133), 8)
    if not check(texels is not None and texels.shape == (48, 48), "cross dot: drawn alone from the font"):
        game.run(150)
        return
    inside = bright(texels)
    drawn_px = texels != 0
    ys, xs = np.where(inside)
    oy, ox = np.where(drawn_px)
    # The original dot: lettering 2 x 2 at (194..195, 130..131) in a ring of outline, centred 3 PS1 pixels into
    # the box each way (24 texels at 8x); the disc's centre within half a PS1 pixel of it.
    cy, cx = ys.mean() + 0.5, xs.mean() + 0.5
    check(abs(cx - 24) <= 4 and abs(cy - 24) <= 4,
          f"cross dot: the disc is centred on the original dot ({cx / 8:.2f}, {cy / 8:.2f} of 6)")
    wide, tall = xs.max() - xs.min() + 1, ys.max() - ys.min() + 1
    check(abs(wide - tall) <= 1 and 12 <= wide <= 24,
          f"cross dot: round, about the original's 2 PS1 pixels across ({wide / 8:.2f} x {tall / 8:.2f})")
    sym = np.array_equal(inside, inside[::-1, :]) and np.array_equal(inside, inside[:, ::-1])
    check(sym, "cross dot: the disc is symmetric")
    check(oy.min() > 0 and ox.min() > 0 and oy.max() < 47 and ox.max() < 47 and (drawn_px & ~inside).any(),
          "cross dot: an outline around it, inside the 6 x 6 box")
    game.run(150)  # released: the cross closes


def launch(work, card, label, extra=None):
    settings = work / "settings.cfg"
    env = {"ROOD_OUT": str(work), "ROOD_SETTINGS": str(settings), **(extra or {})}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env.setdefault("ROOD_VK_VALIDATE", "1")
    return Game.launch(socket_path=f"/tmp/rood-text-{os.getpid()}-{label}.sock", log=str(work / f"{label}.log"),
                       extra_env=env, card=str(card) if card else None, fast=True)


def log_clean(game, label):
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log and "[gl] shader" not in log and "[vk] validation:" not in log
          and "[gl] link" not in log and "cannot load font" not in log,
          f"{label}: no crash, shader error, validation message or missing font")
    if "[vk] validation:" in log or "[crash]" in log:
        print("\n".join(line for line in log.splitlines() if "validation" in line or "crash" in line)[:4000])


def run_bubble(work, shots):
    """New Game: the opening's first talk bubble."""
    game = launch(work, None, "bubble")
    try:
        g = game.call("graphics")
        check(g.get("ui_text") == "original", f"bubble: default ui_text original ({g.get('ui_text')})")
        game.call("graphics", interpolate=False, aa="off")  # SMAA would blend beyond the glyph boxes
        game.wait_title()
        game.run(90)
        game.press("start", vsyncs=4, settle=90)
        game.press("circle", vsyncs=3, settle=60)
        game.run(2400)  # the duke's first line waits for a button
        s = game.call("ui_upscale")
        t = game.call("ui_text")
        check(s["lookups"] == 0 and t["regions"] == 0,
              f"bubble: while ui_text is original nothing uses the 2D atlas ({s['lookups']} lookups, "
              f"{t['regions']} font regions)")
        compare(game, work, "bubble", shots, "dialogue", "dark")
        styles = {st["name"]: st for st in game.call("ui_text")["styles"]}
        check(styles["dialogue"]["loaded"] is True, "bubble: the dialogue font is loaded")
    finally:
        game.close()
    log_clean(game, "bubble")


def run_save(work, card, shots):
    """The save room: HUD, then the in-game menu."""
    copy = work / "card.mcd"
    shutil.copyfile(card, copy)
    game = launch(work, copy, "save")
    settings = work / "settings.cfg"
    try:
        game.call("graphics", interpolate=False, aa="off")  # SMAA would blend beyond the glyph boxes
        game.wait_title()
        game.run(90)
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        if check(bool(game.state().get("player")), "save: in the save room"):
            s = game.call("ui_upscale")
            check(s["lookups"] == 0, f"save: ui_text original leaves 2D alone ({s['lookups']} lookups)")
            check(game.call("ui_text")["labels_drawn"] == [], "save: no label drawn from a font while original")
            compare(game, work, "hud", shots, "hud", "bright")
            drawn = game.call("ui_text")["labels_drawn"]
            check("HP" in drawn and "MP" in drawn, f"save: HUD labels drawn from the font ({drawn})")
            check_digits(game, work)
            check_label_render(game, work)
            check_cross(game, work, shots)
            game.press("triangle", vsyncs=3, settle=60)
            game.press("circle", vsyncs=3, settle=60)
            game.press("circle", vsyncs=3, settle=60)  # Battle Abilities > Chain Abilities (information box)
            compare(game, work, "menu", shots, "menu", "bright")
            drawn = game.call("ui_text")["labels_drawn"]
            check("MENU" in drawn and "INFORMATION" in drawn, f"save: menu labels drawn from the font ({drawn})")
            styles = {st["name"]: st for st in game.call("ui_text")["styles"]}
            check(all(styles[n]["loaded"] is True for n in ("menu", "info", "digits")),
                  "save: menu, info and digits fonts are loaded")
        # A style override (RPC ui_text, as the settings keys) applies.
        before = game.call("ui_text")["styles"][1]["stretch"]
        after = game.call("ui_text", style="menu", stretch=before + 0.1)["styles"][1]["stretch"]
        check(abs(after - before - 0.1) < 1e-6, f"save: ui_text style override ({before} -> {after})")
        game.call("ui_text", style="menu", stretch=before)
        # Settings persist (only the option; the style is back to its default).
        game.call("graphics", ui_text="font")
        game.call("settings", action="save")
        text = settings.read_text()
        check("ui_text font" in text and "ui_text_menu_stretch" not in text, "save: ui_text saved")
        game.call("graphics", ui_text="original")
        game.call("settings", action="load")
        check(game.call("graphics")["ui_text"] == "font", "save: ui_text loads back")
        game.call("graphics", ui_text="original")
        game.run(4)
    finally:
        game.close()
    log_clean(game, "save")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--card", type=pathlib.Path, default=default_card())
    parser.add_argument("--shots", type=pathlib.Path)
    args = parser.parse_args()
    card = args.card if args.card and args.card.exists() else None
    if args.shots:
        args.shots.mkdir(parents=True, exist_ok=True)
    work = OUT / "ui-text"
    work.mkdir(parents=True, exist_ok=True)
    (work / "settings.cfg").unlink(missing_ok=True)
    run_bubble(work, args.shots)
    if card:
        run_save(work, card, args.shots)
    else:
        print("note: no memory card: the talk bubble only")
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("PASS: ui_text_smoke")
    return 0


if __name__ == "__main__":
    sys.exit(main())
