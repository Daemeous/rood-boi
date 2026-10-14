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
- line layout (ui_text_layout line, set explicitly; the bubble, the next
  bubble with a comma and a period, the menu rows and information box, and
  the screens below): one
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
- the game's printers (pc_text_glyph): on the memory card screens (the
  title's Continue, TITLE.PRG, and the field's DATA > LOAD, MENU7.PRG: their
  own printers and advances, BATTLE.PRG's table not loaded at the title),
  the status screen, the equipment list, the setup menu in a workshop, the
  quick manual and one of its pages, and the magic menu in battle mode, every
  dialogue and menu glyph drawn belongs to a string a printer reported (none
  unexplained), and words stay whole: letters the game set next to each
  other stay close (at most WORD_GAP) and each space it left stays a space
  (at least WORD_GAP), with the line checks above (descenders whole both
  ways: glyph by glyph no longer cuts them at the cell);
- text at the window's resolution (ui_text_overlay auto; the title's memory
  card screen in a 1920 x 1440 window, six times the PS1's): at internal
  resolution 4 text is held back and presented at the window's resolution,
  the same paused frame presented with the overlay off differs only on the
  text (each pixel of the window maps to a VRAM pixel the 2D path drew text
  into, a pixel around), and it is nearer (OVERLAY_NEARER of the mean
  difference on the text) to the picture drawn at internal resolution 6 (the
  window's own) than the 4x picture is; internal resolution 1 too (font text
  at 1x); 16:9 likewise; ui_text original holds nothing back and presents
  the same picture either way (frame-exact); in the save room with frame
  interpolation the HUD's held-back numbers present the same on every image;
- emote bubbles over actors (BATTLE.PRG func_80095C18, pc/src/ui_emotes.c;
  every kind forced on the opening's actors: D_800F2260, D_800F2270): with
  ui_text font and ui_bubbles vector the same recorded frame changes only
  inside the regions the 2D path drew from shapes and fonts, the balloons,
  dots and marks are drawn again (RPC ui_bubbles emotes), and with both
  options off none is;
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
from checkpoints import launch_at  # noqa: E402
from rood_rpc import OUT, Game, symbols  # noqa: E402

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


def local_ink(image, rows, cols, contrast=40):
    """Text ink against the background around it (the median), bright or
    dark: a region mostly of background (below a baseline)."""
    luma = image[rows, cols, :3].astype(int) @ np.array([299, 587, 114]) // 1000
    return np.abs(luma - np.median(luma)) > contrast


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
DESCENT_ROWS = 6  # ui_text.c: rows below the 12-pixel cell a glyph may reach
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
    # What changes is on the lines: each glyph's original cell with the rows below it its descender reaches
    # glyph by glyph (DESCENT_ROWS), and its laid-out ink (a pixel around).
    allowed = np.zeros((y1 - y0, x1 - x0), bool)
    for ln in lines:
        k = ln["k"]
        for g in ln["glyphs"]:
            boxes = [(g["x"], ln["top"], g["x"] + 12 * k, ln["top"] + (12 + DESCENT_ROWS) * k)]
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
    # Descenders below the baseline, drawn whole, as lines and glyph by glyph (no longer cut at the cell).
    ia, ib, ic = ink(a, polarity), ink(b, polarity), ink(c, polarity)
    found = deeper = whole = 0
    shallow = []
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
            # Glyph by glyph the descender is in the glyph's own cell's columns (a few pixels from the line's).
            near = slice(max(int((g["x"] - 2 * k - x0) * scale), 0), int(np.ceil((g["x"] + 14 * k - x0) * scale)))
            below_line, below_glyph = np.where(local_ink(c, rows, cols))[0], np.where(local_ink(b, rows, near))[0]
            if len(below_line) and below_line.max() / scale >= deep - ln["base"] - 1.0:
                whole += 1
            # (within a PS1 pixel: a glyph condensed into its cell has a thinner tail, a j's fades sooner)
            if len(below_line) and len(below_glyph) and below_glyph.max() >= below_line.max() - scale:
                deeper += 1
            else:
                shallow.append((chr(g["c"]), round(ln["top"]), round(g["x"]),
                                int(below_line.max()) if len(below_line) else None,
                                int(below_glyph.max()) if len(below_glyph) else None))
    if found:
        check(whole == found and deeper == found,
              f"{label} {name}: {found} descenders reach below the baseline drawn whole, as lines ({whole}) and "
              f"glyph by glyph ({deeper}{'; not: ' + str(shallow) if shallow else ''})")
    if shots:
        side = np.concatenate([a, np.full((a.shape[0], 8, 3), 255, np.uint8), b,
                               np.full((a.shape[0], 8, 3), 255, np.uint8), c], axis=1)
        image = Image.fromarray(side)
        if image.width > 2400:
            image = image.resize((2400, image.height * 2400 // image.width), Image.LANCZOS)
        image.save(shots / f"{label}-{name}-lines-{align}.jpg", quality=85)


LETTER_GAP = 2.25  # PS1 pixels: letters and digits of a word at most this far apart
WORD_GAP = 2.5  # and words at least this far


def check_words(lines, label):
    """Words stay whole: on each line (by x), letters and digits the game set
    without a space between them (their gap after the printer's advance
    under half a space) keep their ink within LETTER_GAP of each other (on
    the rows they share, or between their ink boxes: an F's arm over an a;
    punctuation, ' / : ., may stand further off); neighbours with a space at
    least WORD_GAP apart on every row they share."""
    worst_word, worst_space, pairs = 0.0, 1e9, 0
    for ln in lines:
        k = ln["k"]
        gs = sorted((g for g in ln["glyphs"] if g["rows"]), key=lambda g: g["x"])
        for p, q in zip(gs, gs[1:]):
            if p["str"] != q["str"]:
                continue
            left = {round(r[0] * 4): r[2] for r in p["rows"]}
            shared = [r[1] - left[round(r[0] * 4)] for r in q["rows"] if round(r[0] * 4) in left]
            if not shared:
                continue
            gap = min(shared) / k
            pairs += 1
            if (q["x"] - p["x"]) / k - p["advance"] < 3:
                if chr(p["c"]).isalnum() and chr(q["c"]).isalnum():
                    boxes = (drawn_ink(q)[0] - drawn_ink(p)[1]) / k
                    worst_word = max(worst_word, min(gap, boxes))
            else:
                worst_space = min(worst_space, gap)
    check(pairs > 0 and worst_word <= LETTER_GAP and (worst_space == 1e9 or worst_space >= WORD_GAP),
          f"{label}: words stay whole ({pairs} neighbours; widest gap in a word {worst_word:.2f}, narrowest space "
          f"{worst_space if worst_space < 1e9 else 0:.2f} PS1 pixels)")


# Screens of the game's other printers (from the save room; "warp": the workshop "Metal Works", zone 45 room 0;
# ("pick", ROW): the cursor moved to the menu row reading ROW, then Circle).
SCREENS = [
    ("status", [("triangle", 90), ("pick", "STATUS", 120)]),
    ("equip", [("triangle", 90), ("pick", "ITEMS", 90), ("pick", "EQUIP", 120)]),
    ("load", [("triangle", 90), ("pick", "DATA", 90), ("pick", "LOAD", 150), ("pick", "MEMORYCARDslot1", 150)]),
    ("manual", [("triangle", 90), ("pick", "QUICKMANUAL", 150)]),
    ("manual-page", [("pick", "BASICCONTROLS", 150)]),  # (continues the quick manual)
    ("workshop", ["warp", ("triangle", 90), ("pick", "ITEMS", 90), ("pick", "SETUP", 120)]),
    ("battle", [("circle", 90), ("triangle", 90), ("pick", "BATTLEABILITIES", 90),
                ("pick", "CHAINABILITIES", 120)]),  # battle mode
]


def pick(game, work, row, settle):
    """Move the menu cursor (the yellow hand left of the selected row) to the
    row reading `row` (RPC ui_text lines: each line's text and place), then
    Circle."""
    for _ in range(16):
        game.call("graphics", ui_text="font", ui_text_layout="line")
        game.run(2)
        lines = [ln for ln in lines_report(game, work) if ln["style"] == "menu" and ln["glyphs"]]
        rows = {"".join(chr(g["c"]) for g in sorted(ln["glyphs"], key=lambda g: g["x"])): ln for ln in lines}
        if row not in rows:
            break
        image, info = redraw(game, work, "font", "pick")
        scale = image.shape[1] // 1024
        r, g, b = (image[..., k].astype(int) for k in range(3))
        target = rows[row]
        # The hand is a triangle above-left of the selected row, its tip on the row's middle; the levels above
        # keep theirs further left (the game draws into two buffers 320 pixels apart: the report's and the
        # redrawn frame may be either).
        x0 = min(gl["x"] for gl in target["glyphs"]) % 320
        hand = (r > 180) & (g > 110) & (b < 100) & (r - b > 110)
        near = np.zeros(hand.shape, bool)
        for buffer in (0, 320):
            near[:, max(int((buffer + x0 - 16) * scale), 0):int((buffer + x0) * scale)] = True
        ys = np.where(hand & near)[0]
        if not len(ys):
            game.run(10)  # (no hand yet: the menu still sliding in)
            continue
        y = float(ys.max()) / scale  # its tip
        selected = min(lines, key=lambda ln: abs(ln["top"] + 8 - y))
        if selected is target:
            game.press("circle", vsyncs=3, settle=settle)
            return True
        game.press("down" if selected["top"] < target["top"] else "up", vsyncs=3, settle=20)
    check(False, f"menu row {row} picked")
    return False


def check_printers(game, work, name, shots):
    """One screen: every glyph a printer's (none unexplained), words whole,
    and the line checks (compare_lines)."""
    game.call("graphics", ui_text="font", ui_text_layout="line")
    game.run(6)
    path = work / "lines.json"
    game.call("ui_text", lines=str(path))
    report = json.loads(path.read_text())
    path.unlink()
    lines = [line for line in report["lines"] if line["style"] in ("dialogue", "menu")]
    text = " / ".join("".join(chr(g["c"]) for g in sorted(line["glyphs"], key=lambda g: g["x"])) for line in lines)
    check(bool(lines) and report["unexplained"] == 0,
          f"{name}: every glyph belongs to a printer's string ({report['unexplained']} unexplained; '{text[:120]}')")
    check_words(lines, name)
    compare_lines(game, work, name, shots, "menu", "bright", "left")


OVERLAY_WINDOW = (1920, 1440)
OVERLAY_NEARER = 0.6  # the overlay's mean difference on the text from the 6x picture, at most this share of the 4x's


def present(game, work, name):
    game.wait_presents(3)
    path = work / f"{name}.png"
    game.present_screenshot(path)
    image = np.asarray(Image.open(path).convert("RGB")).astype(int)
    path.unlink()
    return image


def text_mask(game, work, image_shape, mask):
    """Window pixels over VRAM pixels the 2D path drew text into (a pixel
    around): the presented view (RPC ui_text overlay view) maps the display
    area (RPC ui_upscale display)."""
    vx, vy, vw, vh = game.call("ui_text")["overlay"]["view"]
    dx, dy, dw, dh = game.call("ui_upscale")["display"]
    grown = ndimage.binary_dilation(mask, iterations=1)
    ys, xs = np.mgrid[0:image_shape[0], 0:image_shape[1]]
    inside = (xs >= vx) & (xs < vx + vw) & (ys >= vy) & (ys < vy + vh)
    u = np.clip(dx + ((xs - vx) * dw) // max(vw, 1), 0, 1023)
    v = np.clip(dy + ((ys - vy) * dh) // max(vh, 1), 0, 511)
    return inside & grown[v, u]


def run_overlay(work, card, shots):
    """ui_text_overlay at a window six times the PS1's."""
    copy = work / "card.mcd"
    shutil.copyfile(card, copy)
    extra = {"ROOD_WINDOW": f"{OVERLAY_WINDOW[0]}x{OVERLAY_WINDOW[1]}", "ROOD_SCALE": "4"}
    game = launch(work, copy, "overlay", extra, checkpoint="title")
    try:
        game.call("graphics", interpolate=False, aa="off")
        game.press("start", vsyncs=4, settle=60)
        game.press("circle", vsyncs=4, settle=120)
        game.press("circle", vsyncs=4, settle=150)  # Continue > MEMORY CARD slot 1: its files
        check(game.call("graphics")["ui_text_overlay"] == "auto", "overlay: default ui_text_overlay auto")
        # Original: nothing held back, the same picture either way.
        game.call("graphics", ui_text="original", ui_text_overlay="auto")
        game.run(8)
        held = game.call("ui_text")["overlay"]["held"]
        first = present(game, work, "overlay-original-auto")
        game.call("graphics", ui_text_overlay="off")
        again = present(game, work, "overlay-original-off")
        check(held == -1 and np.array_equal(first, again),
              f"overlay: ui_text original holds nothing back (held {held}) and presents the same picture "
              f"({int(np.any(first != again, axis=2).sum())} pixels differ)")

        def drawn(scale, widescreen=False):
            """A frame at internal resolution `scale` with the overlay on, then off (the same paused frame)."""
            game.call("graphics", ui_text="font", ui_text_layout="line", ui_text_overlay="auto", scale=scale,
                      widescreen=widescreen)
            game.call("ui_upscale", mask=str(work / "mask.pgm"))  # clear
            game.run(8)
            game.call("ui_upscale", mask=str(work / "mask.pgm"))
            mask = np.asarray(Image.open(work / "mask.pgm")) > 0
            on = present(game, work, f"overlay-{scale}x-on")
            stats = game.call("ui_text")["overlay"]
            region = text_mask(game, work, on.shape, mask)
            game.call("graphics", ui_text_overlay="off")
            off = present(game, work, f"overlay-{scale}x-off")
            return on, off, stats, region

        def only_text(label, on, off, region):
            diff = np.any(on != off, axis=2)
            outside = int((diff & ~region).sum())
            check(diff.any() and outside == 0, f"overlay {label}: presented at the window's resolution, the same "
                  f"frame changes only on the text ({int(diff.sum())} pixels, {outside} outside)")

        a4, o4, stats, region = drawn(4)
        check(stats["held"] > 0, f"overlay 4x: text held back for the window ({stats['held']} triangles)")
        only_text("4x", a4, o4, region)
        game.call("graphics", ui_text_overlay="off", scale=6)
        game.run(8)
        o6 = present(game, work, "overlay-6x-off")
        held6 = game.call("ui_text")["overlay"]["held"]
        check(held6 == -1, f"overlay 6x: the target as fine as the window: nothing held back ({held6})")

        def error(image):
            return float(np.abs(image - o6).sum(axis=2)[region].mean())

        check(error(a4) <= OVERLAY_NEARER * error(o4),
              f"overlay 4x: nearer the 6x picture on the text ({error(a4):.2f} vs {error(o4):.2f} at 4x)")
        a1, o1, stats, region1 = drawn(1)
        check(stats["held"] > 0, f"overlay 1x: font text held back for the window ({stats['held']} triangles)")
        only_text("1x", a1, o1, region1)
        # (text something drew over is drawn into the 1x picture: nearer than the 4x picture, not by as much)
        check(error(a1) < error(o4),
              f"overlay 1x: nearer the 6x picture on the text ({error(a1):.2f}) than the 4x picture ({error(o4):.2f})")
        w4, wo4, stats, region_w = drawn(4, widescreen=True)
        check(stats["held"] > 0 and stats["view"][2] * 9 == stats["view"][3] * 16,
              f"overlay 16:9: text held back ({stats['held']}) in a 16:9 view ({stats['view']})")
        only_text("16:9", w4, wo4, region_w)
        game.call("graphics", widescreen=False)
        if shots:
            for name, image in (("4x-overlay", a4), ("4x-off", o4), ("6x-off", o6), ("1x-overlay", a1)):
                Image.fromarray(image.astype(np.uint8)).save(shots / f"overlay-{name}.png")
    finally:
        game.close()
    log_clean(game, "overlay")
    # Frame interpolation: images between the game's frames keep the held-back HUD numbers.
    shutil.copyfile(card, copy)
    game = launch(work, copy, "overlay-field", extra, checkpoint="save_room")
    try:
        game.call("graphics", interpolate=True, present_rate=1, aa="off", ui_text="font", ui_text_overlay="auto")
        game.call("ui_upscale", mask=str(work / "mask.pgm"))  # clear
        game.run(10)
        game.call("ui_upscale", mask=str(work / "mask.pgm"))
        mask = np.asarray(Image.open(work / "mask.pgm")) > 0
        images = []
        for _ in range(4):
            game.run(1)
            images.append(present(game, work, "overlay-field"))
        stats = game.call("ui_text")["overlay"]
        vx, vy, vw, vh = stats["view"]
        # The HUD's lettering (HP / MP and their numbers, top left): its bright pixels on the first image
        # (the scene behind its edges moves between images).
        hud = text_mask(game, work, images[0].shape, mask)
        hud[vy + vh // 6:] = False
        hud[:, vx + vw // 2:] = False
        lettering = hud & (images[0] @ np.array([299, 587, 114]) > 180000)
        worst = max(int(np.abs(image - images[0])[lettering].max()) for image in images[1:])
        check(stats["held"] > 0 and lettering.sum() > 1000 and worst <= 8,
              f"overlay: interpolated images keep the held-back HUD text ({stats['held']} triangles held, "
              f"{int(lettering.sum())} lettering pixels, worst change {worst})")
    finally:
        game.close()
    log_clean(game, "overlay-field")


def run_screens(work, card, shots):
    """The title's memory card screen, then each screen of SCREENS."""
    copy = work / "card.mcd"
    shutil.copyfile(card, copy)
    game = launch(work, copy, "title", checkpoint="title")
    try:
        game.call("graphics", interpolate=False, aa="off")
        game.press("start", vsyncs=4, settle=60)
        game.press("circle", vsyncs=4, settle=120)
        game.press("circle", vsyncs=4, settle=150)  # Continue > MEMORY CARD slot 1: its files
        check_printers(game, work, "title-load", shots)
    finally:
        game.close()
    log_clean(game, "title")
    game = None
    for name, steps in SCREENS:
        if not name.startswith("manual-"):  # (a page continues the screen before)
            if game:
                game.close()
                log_clean(game, "screens")
            shutil.copyfile(card, copy)
            game = launch(work, copy, "screens", checkpoint="save_room")
            game.call("graphics", interpolate=False, aa="off")
        for step in steps:
            if step == "warp":
                game.call("room_goto", zone=45, room=0)
                for _ in range(60):
                    game.run(20)
                    if game.call("room_goto", action="status")["phase"] in (0, 4):
                        break
                game.run(60)
                check((game.state()["zone"], game.state()["room"]) == (45, 0), f"{name}: in the workshop")
                continue
            if step[0] == "pick":
                pick(game, work, step[1], step[2])
            else:
                game.press(step[0], vsyncs=3, settle=step[1])
        check_printers(game, work, name, shots)
    if game:
        game.close()
        log_clean(game, "screens")


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


def launch(work, card, label, extra=None, checkpoint=None):
    settings = work / "settings.cfg"
    env = {"ROOD_OUT": str(work), "ROOD_SETTINGS": str(settings), **(extra or {})}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env.setdefault("ROOD_VK_VALIDATE", "1")
    kwargs = dict(socket_path=f"/tmp/rood-text-{os.getpid()}-{label}.sock", log=str(work / f"{label}.log"),
                  extra_env=env, card=str(card) if card else None, fast=True)
    # A test checkpoint (checkpoints.py): booted, or its validated save state.
    return launch_at(checkpoint, **kwargs) if checkpoint else Game.launch(**kwargs)


def log_clean(game, label):
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log and "[gl] shader" not in log and "[vk] validation:" not in log
          and "[gl] link" not in log and "cannot load font" not in log,
          f"{label}: no crash, shader error, validation message or missing font")
    if "[vk] validation:" in log or "[crash]" in log:
        print("\n".join(line for line in log.splitlines() if "validation" in line or "crash" in line)[:4000])


def run_emotes(work, shots):
    """The emote bubbles, each kind, on the opening's actors."""
    game = launch(work, None, "emotes", checkpoint="first_bubble")
    try:
        game.call("graphics", interpolate=False, aa="off", ui_text="original", ui_bubbles="original")
        game.run(200)
        kinds = symbols()["D_800F2260"]
        game.write(symbols()["D_800F2270"], bytes(4))  # (the scene keeps them off)
        before = game.call("ui_bubbles")["emotes"]
        for kind in range(1, 8):
            game.write(kinds, bytes([0] + [kind] * 15))
            game.call("graphics", ui_text="original", ui_bubbles="original")
            game.run(4)
            plain, info = redraw(game, work, "original", f"emote-{kind}-plain")
            if kind == 1:
                check(game.call("ui_bubbles")["emotes"] == before,
                      f"emotes: nothing drawn again while ui_text and ui_bubbles are original ({before})")
            game.call("graphics", ui_text="font", ui_bubbles="vector")
            game.call("ui_upscale", mask=str(work / "mask.pgm"))  # clear
            game.run(4)
            game.call("ui_upscale", mask=str(work / "mask.pgm"))
            mask = np.asarray(Image.open(work / "mask.pgm")) > 0
            game.call("graphics", ui_bubbles="vector")
            drawn, _ = redraw(game, work, "font", f"emote-{kind}")
            game.call("graphics", ui_bubbles="original")
            again, _ = redraw(game, work, "original", f"emote-{kind}-again")
            scale = drawn.shape[1] // 1024
            big = np.repeat(np.repeat(ndimage.binary_dilation(mask, iterations=1), scale, 0), scale, 1)
            diff = np.any(drawn != again, axis=2)
            check(diff.any() and not (diff & ~big).any(),
                  f"emotes {kind}: shapes and fonts change only the regions drawn from them ({int(diff.sum())} pixels, "
                  f"{int((diff & ~big).sum())} outside)")
            x, y, w, h = info["area"]
            crop = (slice(y * scale, (y + h) * scale), slice(x * scale, (x + w) * scale))
            if shots:
                Image.fromarray(np.concatenate([again[crop], drawn[crop]], axis=1)).save(shots / f"emote-{kind}.png")
            del plain
        game.write(kinds, bytes(16))
        after = game.call("ui_bubbles")["emotes"]
        check(after["balloons"] > before["balloons"] and after["dots"] > before["dots"]
              and after["marks"] > before["marks"], f"emotes: balloons, dots and marks drawn again ({after})")
    finally:
        game.close()
    log_clean(game, "emotes")


def run_bubble(work, shots):
    """New Game: the opening's first talk bubble."""
    game = launch(work, None, "bubble", checkpoint="first_bubble")  # New Game, Circle, 2200 vsyncs
    try:
        g = game.call("graphics")
        check(g.get("ui_text") == "original", f"bubble: default ui_text original ({g.get('ui_text')})")
        check(g.get("ui_text_layout") == "glyph", f"bubble: default ui_text_layout glyph ({g.get('ui_text_layout')})")
        check(g.get("ui_text_bubble_align") == "centre",
              f"bubble: default ui_text_bubble_align centre ({g.get('ui_text_bubble_align')})")
        game.call("graphics", interpolate=False, aa="off")  # SMAA would blend beyond the glyph boxes
        game.run(200)  # the duke's first line waits for a button
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
    game = launch(work, copy, "save", checkpoint="save_room")
    settings = work / "settings.cfg"
    try:
        game.call("graphics", interpolate=False, aa="off")  # SMAA would blend beyond the glyph boxes
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
    run_emotes(work, args.shots)
    if card:
        run_save(work, card, args.shots)
        run_screens(work, card, args.shots)
        run_overlay(work, card, args.shots)
    else:
        print("note: no memory card: the talk bubble only")
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("PASS: ui_text_smoke")
    return 0


if __name__ == "__main__":
    sys.exit(main())
