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
  the first original drawing (those checks with ui_text_layout glyph);
- line layout (ui_text_layout line, the default; the bubble, the next
  bubble with a comma and a period, the menu rows and information box): one
  recorded frame drawn original, glyph by glyph and as lines (RPC ui_text
  lines reports each laid-out glyph's ink row by row): only the lines
  change, neighbours' ink never comes within a quarter pixel on any row, the
  nearest approach between letters of a word deviates at most
  SPACING_SPREAD, each line starts where the game's does (0.75 px) and never
  runs past its end (1.5 px), descenders reach below the baseline drawn whole
  and deeper than in the cells; bubbles both left (ui_text_bubble_align left)
  and centred on the game's line (centre); a bubble typing out, each way,
  shows each glyph, frame
  by frame, at the place it keeps (reveal order);
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
import json
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
    """Original and font drawings of the same frame (glyph by glyph in the
    original cells: ui_text_layout glyph; compare_lines checks line layout)."""
    game.call("graphics", ui_text="font", ui_text_layout="glyph")
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


def lines_report(game, work):
    """RPC ui_text lines: the lines laid out in the newest frame that drew
    text (per glyph: the original's place, advance and ink; the laid-out pen
    and ink per half row, [y, x0, x1], screen PS1 pixels)."""
    path = work / "lines.json"
    game.call("ui_text", lines=str(path))
    report = json.loads(path.read_text())
    path.unlink()
    return report["lines"]


def drawn_ink(g):
    """A laid-out glyph's ink extent from its rows: x0, x1, deepest y."""
    rows = g["rows"]
    return min(r[1] for r in rows), max(r[2] for r in rows), max(r[0] for r in rows) + 0.25


def segments(lines):
    """Each run of glyphs set together (a line or a column of it), by x."""
    out = {}
    for line in lines:
        for g in line["glyphs"]:
            if g["rows"]:
                out.setdefault(g["seg"], (line, []))[1].append(g)
    return [(line, sorted(gs, key=lambda g: g["x"])) for line, gs in out.values()]


DESCENDERS = set(map(ord, "gjpqy"))
SPACING_SPREAD = 0.75  # PS1 pixels: the standard deviation of the gaps between letters of a word


def compare_lines(game, work, name, shots, label, polarity, align="left"):
    """ui_text_layout line on one recorded frame, drawn original, glyph by
    glyph and as lines (the frame holds both kinds of quads): what the lines
    change stays on the lines, letters never touch, spacing is even,
    descenders reach below the baseline undistorted, every line starts where
    the game's does and never runs past its end."""
    game.call("graphics", ui_text="font", ui_text_layout="line", ui_text_bubble_align=align)
    game.run(6)
    lines = [line for line in lines_report(game, work) if line["style"] in ("dialogue", "menu")]
    if not check(bool(lines), f"{label} {name}: lines laid out ({len(lines)})"):
        return

    def draw(text, layout, suffix):
        game.call("graphics", ui_text=text, ui_text_layout=layout)
        path = work / f"{name}-{suffix}.ppm"
        info = game.call("ui_upscale", redraw=str(path))
        image = np.asarray(Image.open(path))
        path.unlink()
        return image, info

    original, info = draw("original", "line", "original")
    glyph, _ = draw("font", "glyph", "glyph")
    line, _ = draw("font", "line", "line")
    again, _ = draw("original", "line", "again")
    game.call("graphics", ui_text="font", ui_text_layout="line")
    scale = original.shape[1] // 1024
    x, y, w, h = info["area"]
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, 1024), min(y + h, 512)

    def crop(image):
        return image[y0 * scale:y1 * scale, x0 * scale:x1 * scale]

    a, b, c, d = crop(original), crop(glyph), crop(line), crop(again)
    check(np.array_equal(a, d), f"{label} {name}: original after line layout draws the same picture")
    moved = int(np.any(b != c, axis=2).sum())
    check(moved > 0, f"{label} {name}: line layout changes the glyph-by-glyph picture ({moved} pixels)")
    # What changes is on the lines: each glyph's original cell and its laid-out ink (a pixel around).
    allowed = np.zeros((y1 - y0, x1 - x0), bool)
    for ln in lines:
        k = ln["k"]
        for g in ln["glyphs"]:
            boxes = [(g["x"], ln["top"], g["x"] + 12 * k, ln["top"] + 12 * k)]
            if g["rows"]:
                gx0, gx1, deep = drawn_ink(g)
                boxes.append((gx0, min(r[0] for r in g["rows"]), gx1, deep))
            for bx0, by0, bx1, by1 in boxes:
                allowed[max(int(np.floor(by0)) - 1 - y0, 0):max(int(np.ceil(by1)) + 1 - y0, 0),
                        max(int(np.floor(bx0)) - 1 - x0, 0):max(int(np.ceil(bx1)) + 1 - x0, 0)] = True
    big = np.repeat(np.repeat(allowed, scale, axis=0), scale, axis=1)
    outside = int((np.any(b != c, axis=2) & ~big).sum())
    check(outside == 0, f"{label} {name}: nothing but the lines changes from glyph by glyph ({outside} pixels)")
    # Neighbours' ink never meets (row by row: slanted letters may share columns), and the gaps
    # between the letters of a word are even.
    overlaps, gaps, starts, ends = [], [], [], []
    for ln, gs in segments(lines):
        k = ln["k"]
        for p, q in zip(gs, gs[1:]):
            left = {round(r[0] * 4): r[2] for r in p["rows"]}
            shared = [r[1] - left[round(r[0] * 4)] for r in q["rows"] if round(r[0] * 4) in left]
            if shared:
                overlaps.append(min(shared) / k)
            spaced = (q["x"] - p["x"]) / k - p["advance"] >= 2.5
            if not spaced and shared:  # the nearest approach of a letter to the next
                gaps.append(min(shared) / k)
        l0, l1, o0, o1 = drawn_ink(gs[0])[0], drawn_ink(gs[-1])[1], gs[0]["ink"][0], gs[-1]["ink"][2]
        if ln["style"] == "dialogue" and align == "centre":  # centred on the original's extent
            starts.append(abs((l0 + l1) - (o0 + o1)) / 2 / k)
            ends.append(max(o0 - l0, l1 - o1) / k)
        else:
            starts.append(abs(l0 - o0) / k)
            ends.append((l1 - o1) / k)
    check(not overlaps or min(overlaps) >= 0.25,
          f"{label} {name}: letters never touch ({len(overlaps)} neighbours, narrowest gap "
          f"{min(overlaps, default=0):.2f} PS1 pixels)")
    if len(gaps) >= 4:
        spread = float(np.std(gaps))
        check(spread <= SPACING_SPREAD,
              f"{label} {name}: even spacing between letters (nearest approach {np.mean(gaps):.2f} PS1 pixels, "
              f"deviation {spread:.2f}, at most {SPACING_SPREAD})")
    where = "are centred on the game's" if align == "centre" and label == "dialogue" else "start where the game's do"
    check(max(starts) <= 0.75 and max(ends) <= 1.5,
          f"{label} {name}: {len(starts)} lines {where} (within {max(starts):.2f} PS1 pixels) "
          f"and never run past their ends (at most {max(ends):.2f} past; at most {-min(ends):.1f} shorter)")
    # Descenders below the baseline, drawn whole.
    ia, ib, ic = ink(a, polarity), ink(b, polarity), ink(c, polarity)
    found = deeper = whole = 0
    for ln in lines:
        k = ln["k"]
        for g in ln["glyphs"]:
            if g["c"] not in DESCENDERS or not g["rows"] or ln["style"] != "menu":
                continue
            gx0, gx1, deep = drawn_ink(g)
            found += 1
            if deep < ln["base"] + 1.0 * k:
                continue
            cols = slice(int((gx0 - x0) * scale), int(np.ceil((gx1 - x0) * scale)))
            rows = slice(int((ln["base"] - y0) * scale), int(np.ceil((deep + 1 - y0) * scale)))
            below_line, below_glyph = np.where(ic[rows, cols])[0], np.where(ib[rows, cols])[0]
            if len(below_line) and below_line.max() / scale >= deep - ln["base"] - 1.0:
                whole += 1
            if len(below_line) and (not len(below_glyph) or below_line.max() > below_glyph.max()):
                deeper += 1
    if found:
        check(whole == found and deeper == found,
              f"{label} {name}: {found} descenders reach below the baseline, drawn whole ({whole}) and deeper than "
              f"in the cells ({deeper})")
    if shots:
        side = np.concatenate([a, np.full((a.shape[0], 8, 3), 255, np.uint8), b,
                               np.full((a.shape[0], 8, 3), 255, np.uint8), c], axis=1)
        image = Image.fromarray(side)
        if image.width > 2400:
            image = image.resize((2400, image.height * 2400 // image.width), Image.LANCZOS)
        image.save(shots / f"{label}-{name}-lines-{align}.jpg", quality=85)


def check_reveal(game, work, frames=90, align="centre"):
    """A talk bubble typing out (line layout, centred: the whole line comes
    from the game's text box): every frame's glyphs of a line are the first
    ones of the finished line, each at the place it keeps."""
    game.call("graphics", ui_text="font", ui_text_layout="line", ui_text_bubble_align=align)
    game.run(4)  # (frames laid out before the change)
    history = []
    for _ in range(frames):
        game.run(1)
        frame = {}
        for line in lines_report(game, work):
            if line["style"] != "dialogue" or not line["glyphs"]:
                continue
            base = line["glyphs"][0]["x"]  # (the game draws into two buffers: places relative to the line)
            frame[(round(line["top"], 2), round(base % 320, 2))] = [
                (round(g["x"] - base, 3), round(g["pen"] - base, 3), g["c"]) for g in line["glyphs"]]
        history.append(frame)
    final = history[-1]
    counts = sorted({len(glyphs) for frame in history for key, glyphs in frame.items() if key in final})
    def same(glyphs, kept):  # (to float rounding: the two buffers are 320 pixels apart)
        return len(glyphs) <= len(kept) and all(
            c == d and abs(x - u) < 0.01 and abs(p - q) < 0.01 for (x, p, c), (u, q, d) in zip(glyphs, kept))

    moved = [(n, key, glyphs, final[key][:len(glyphs)]) for n, frame in enumerate(history)
             for key, glyphs in frame.items() if key in final and not same(glyphs, final[key])]
    stable = not moved
    if moved:
        print("moved:", moved[0])
    text = " / ".join("".join(chr(c) for _, _, c in glyphs) for glyphs in final.values())
    check(len(counts) >= 4 and stable,
          f"reveal ({align}): a bubble types out ({len(counts)} lengths seen, '{text}'), each glyph appearing where "
          f"it stays")


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
        check(g.get("ui_text_layout") == "line", f"bubble: default ui_text_layout line ({g.get('ui_text_layout')})")
        check(g.get("ui_text_bubble_align") == "centre",
              f"bubble: default ui_text_bubble_align centre ({g.get('ui_text_bubble_align')})")
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
        compare_lines(game, work, "bubble", shots, "dialogue", "dark", "left")
        compare_lines(game, work, "bubble", shots, "dialogue", "dark", "centre")
        game.press("circle", vsyncs=3, settle=0)  # the next line ("Not yet, sir.") types out
        check_reveal(game, work, align="left")
        compare_lines(game, work, "bubble2", shots, "dialogue", "dark", "centre")
        compare_lines(game, work, "bubble2", shots, "dialogue", "dark", "left")
        game.press("circle", vsyncs=3, settle=0)  # and the next ("Put out the fires, ..."), centred
        check_reveal(game, work, frames=450, align="centre")
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
            compare_lines(game, work, "menu", shots, "menu", "bright", "centre")  # (menus stay left)
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
        game.call("graphics", ui_text_layout="glyph")
        game.call("settings", action="save")
        check("ui_text_layout glyph" in settings.read_text(), "save: ui_text_layout saved")
        game.call("graphics", ui_text_layout="line")
        game.call("settings", action="load")
        check(game.call("graphics")["ui_text_layout"] == "glyph", "save: ui_text_layout loads back")
        game.call("graphics", ui_text_layout="line", ui_text_bubble_align="left")
        game.call("settings", action="save")
        check("ui_text_bubble_align left" in settings.read_text(), "save: ui_text_bubble_align saved")
        game.call("graphics", ui_text_bubble_align="centre")
        game.call("settings", action="load")
        check(game.call("graphics")["ui_text_bubble_align"] == "left", "save: ui_text_bubble_align loads back")
        check(game.call("ui_text", bubble_align="centre")["bubble_align"] == "centre",
              "save: RPC ui_text bubble_align sets it")
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
