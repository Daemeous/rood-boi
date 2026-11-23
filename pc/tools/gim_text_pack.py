#!/usr/bin/env python3
"""Write the text pack of the game's pictures of text (gitea #14): for each GIM file the lines of ink (a box and a colour per row each), and their words.

    python3 pc/tools/gim_text_pack.py [--data data/GIM] [--out pc/user/gim_text] [--ocr auto|off] [NAME ...]    (JO DEMO_001 STAFF001 ...; STAFF* globs)
    python3 pc/tools/gim_text_pack.py NOWLOADING [--exe data/SLUS_010.40] [--font FILE]    (the loading screen's picture, which is in the executable: gitea #23)

Reads the player's own disc files (pc/tools/gim_decode.py: what the pictures are) and writes pc/user/gim_text/<NAME>.txt, which the game reads (pc/src/gim_text.cpp:
with the modern fonts on, a picture the pack has words for is drawn as real text at its lines' places and in the colours sampled here, row by row).
The pack is the player's: it holds the game's text, so it stays out of the repository (pc/user is ignored). A line's words come from tesseract (if it is installed:
--ocr auto) and are yours to correct: the tool keeps the words of a line it wrote before when its box is the same, and a line it could not read has none
(`line ... |` and nothing after the bar: the game leaves that picture as it is until you write them in).

A pack file:   [1]                        the image of the file (0: the first)
               fill linear|flat [angle=N] [scope=block|line] [strength=X] [measured] | pos:rrggbb ...    how the words are coloured (pc/README.md, Pictures of text):
                                          the tool writes a default (a flat colour when the ink's colour hardly varies across the block, as the quote's, else a
                                          left-to-right gradient fitted to the median ink colour of each column) and marks it `measured`; edit it by hand (drop
                                          `measured`) and the tool keeps your line when it re-runs
                                          a glow (the halo behind the words): `glow=rrggbb glow_radius=PS1px glow_strength=X` among the keys, measured from the picture's
                                          halo (the colour, the reach and the strength that, as the game draws them, best reproduce the pixels around the ink)
                                          the words' own font: `font=FILE font_stretch=X font_embolden=EM` (the file, its sideways stretch and its weight in ems; with none the
                                          pictures style's font; for NOWLOADING the tool measures the stretch and weight that match the picture's capitals)
               line x0 y0 x1 y1 | words   the box in the image's pixels, its words
                                          (words starting with ~: set in ordinary case, not as small capitals, as the staff cards' role titles are)"""
import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import gim_decode  # noqa: E402

INK = 40       # a pixel is ink above this brightness
GAP = 0        # empty rows tolerated inside a line (a role title over a name leaves one)
RULE_MIN = 24  # an unbroken stroke at least this long, at a line's edge, is a rule (not text)
MIN_ROWS = 5   # a band thinner than this (an underline, a dot) belongs to its neighbour


def is_rule(row):
    """A row of ink that is one unbroken stroke at least RULE_MIN pixels long (an underline, a flourish): not letters, which break into several."""
    runs, length, gaps = [], 0, 0
    for v in row:
        if v:
            length += 1
            gaps = 0
        else:
            gaps += 1
            if gaps == 2 and length:
                runs.append(length)
                length = 0
    if length:
        runs.append(length)
    return bool(runs) and max(runs) >= RULE_MIN and sum(runs) - max(runs) <= 6


def trim_rules(ink, band):
    """The band without the rules at its top and bottom edges (they are left in the picture); None if nothing is left."""
    y0, y1 = band
    while y1 - y0 + 1 > MIN_ROWS and is_rule(ink[y1]):
        y1 -= 1
    while y1 - y0 + 1 > MIN_ROWS and is_rule(ink[y0]):
        y0 += 1
    return (y0, y1) if y1 - y0 + 1 >= MIN_ROWS else None


def lines_of(image):
    """The lines of ink of an RGBA image: their boxes (x0, y0, x1, y1)."""
    rgb = image[..., :3].astype(int)
    bright = rgb.max(axis=2)
    ink = (image[..., 3] > 0) & (bright > INK)
    rows = np.flatnonzero(ink.any(axis=1))
    bands, start, last = [], None, None
    for y in rows:
        if start is None:
            start = last = y
        elif y - last <= GAP + 1:
            last = y
        else:
            bands.append((start, last))
            start = last = y
    if start is not None:
        bands.append((start, last))
    merged = []
    for band in bands:
        thin = band[1] - band[0] + 1 < MIN_ROWS
        if thin:
            cols = np.flatnonzero(ink[band[0]:band[1] + 1].any(axis=0))
            if cols.size and (cols.max() + 1 - cols.min()) >= 6 * (band[1] - band[0] + 1):
                continue  # a rule or an underline: an ornament, it stays in the picture (not text)
        if merged and (thin or merged[-1][1] - merged[-1][0] + 1 < MIN_ROWS) and band[0] - merged[-1][1] <= 3:
            merged[-1] = (merged[-1][0], band[1])
        else:
            merged.append(band)
    bands = [trim_rules(ink, band) for band in merged]
    bands = [b for b in bands if b is not None]
    out = []
    for y0, y1 in bands:
        columns = np.flatnonzero(ink[y0:y1 + 1].any(axis=0))
        x0, x1 = int(columns.min()), int(columns.max()) + 1
        out.append((x0, int(y0), x1, int(y1) + 1))
    return out


FLAT_BELOW = 0.2  # the ink's brightness varying less than this across the block: one flat colour (the quote), else a gradient (the staff cards)


def gradient_of(image, boxes, angle=0.0, core=0.5):
    """The picture's ink colour across its block of lines, left to right: the median ink colour of each column (over all the lines), fitted by a few stops.
    Returns (variation, flat colour, [(position 0..1, (r, g, b))])."""
    rgb = image[..., :3].astype(int)
    ink = (image[..., 3] > 0) & (rgb.max(axis=2) > INK)
    x0, x1 = min(b[0] for b in boxes), max(b[2] for b in boxes)
    columns = []
    if angle:  # (a gradient at another angle: the ink's colour by its place along the direction, in as many steps as the block has columns)
        import math
        ca, sa = math.cos(math.radians(angle)), math.sin(math.radians(angle))
        y0, y1 = min(b[1] for b in boxes), max(b[3] for b in boxes)
        extent = max(abs((x1 - x0) * ca) + abs((y1 - y0) * sa), 1.0)
        steps = max(x1 - x0, y1 - y0)
        bins = [[] for _ in range(steps)]
        for b in boxes:
            ys, xs = np.nonzero(ink[b[1]:b[3], b[0]:b[2]])
            for y, x in zip(ys + b[1], xs + b[0]):
                t = ((x - (x0 + x1) / 2) * ca + (y - (y0 + y1) / 2) * sa) / extent + 0.5
                bins[min(max(int(t * steps), 0), steps - 1)].append(rgb[y, x])
        for pixels in bins:
            if pixels:
                pixels = np.array(pixels)
                weight = pixels.max(axis=1)
                columns.append(np.median(pixels[weight >= core * weight.max()], axis=0))
            else:
                columns.append(None)
        x1 = x0 + steps
    for x in (range(x0, x1) if not angle else []):
        pixels = np.concatenate([rgb[b[1]:b[3], x][ink[b[1]:b[3], x]] for b in boxes if b[0] <= x < b[2]] or [np.zeros((0, 3), int)])
        if len(pixels):
            weight = pixels.max(axis=1)
            pixels = pixels[weight >= core * weight.max()]  # (the ink's body: not the dim edge pixels)
            columns.append(np.median(pixels, axis=0))
        else:
            columns.append(None)
    known = [i for i, c in enumerate(columns) if c is not None]
    if not known:
        return 0.0, (255, 255, 255), [(0.0, (255, 255, 255)), (1.0, (255, 255, 255))]
    profile = np.array([np.interp(i, known, [columns[k][c] for k in known]) for c in range(3) for i in range(len(columns))]).reshape(3, -1).T
    kernel = np.ones(9) / 9
    smooth = np.stack([np.convolve(np.pad(profile[:, c], 4, mode="edge"), kernel, mode="valid") for c in range(3)], axis=1)
    bright = smooth.max(axis=1)
    lo, hi = np.percentile(bright, [10, 90])  # (the 10th to the 90th percentile: the block's ragged ends do not count)
    variation = float((hi - lo) / max(hi, 1))
    flat = tuple(int(v) for v in np.median(profile[profile.max(axis=1) >= 0.7 * profile.max(axis=1).max()], axis=0).round())
    stops = [0, len(smooth) - 1]
    while len(stops) < 5:  # the point farthest from the stops' curve joins them
        order = sorted(stops)
        fit = np.stack([np.interp(np.arange(len(smooth)), order, smooth[order, c]) for c in range(3)], axis=1)
        error = np.abs(fit - smooth).max(axis=1)
        worst = int(error.argmax())
        if error[worst] < 10:
            break
        stops.append(worst)
    order = sorted(stops)
    n = max(len(smooth) - 1, 1)
    return variation, flat, [(i / n, tuple(int(v) for v in smooth[i].round())) for i in order]


# Pictures that are not GIM files: the name, the picture's header address in the executable, the brightness above which a pixel is ink (the rest of the picture is
# its glow), the words (they cannot be read from a glow: the player's own, here the game's) and the share of the brightest ink that counts as the ink's own
# colour (a stroke blurred into its glow is brightest in its middle: the words' colour is that, not the edge's blend).
EXE_PICTURES = {"NOWLOADING": (0x80049150, 150, "NOW LOADING...", 0.9)}


def exe_picture(exe, address):
    """The picture the executable holds at `address` (a header of width and height as two bytes each, then 15-bit pixels), as an RGBA array."""
    data = pathlib.Path(exe).read_bytes()
    o = address - 0x80010000 + 0x800
    w, h = data[o] + (data[o + 1] << 8), data[o + 2] + (data[o + 3] << 8)
    v = np.frombuffer(data[o + 4:o + 4 + w * h * 2], dtype="<u2").reshape(h, w).astype(np.uint32)
    rgb = np.stack([v & 31, (v >> 5) & 31, (v >> 10) & 31], axis=-1) * 255 // 31
    return np.concatenate([rgb, np.full((h, w, 1), 255)], axis=-1).astype(np.uint8)


def blur(a, sigma):
    """A Gaussian blur of a 2D array (separable, edges as zeros)."""
    taps = max(int(np.ceil(sigma * 3)), 1)
    k = np.exp(-0.5 * (np.arange(-taps, taps + 1) / sigma) ** 2)
    k /= k.sum()
    out = np.apply_along_axis(lambda r: np.convolve(r, k, mode="same"), 1, np.pad(a, taps))
    out = np.apply_along_axis(lambda r: np.convolve(r, k, mode="same"), 0, out)
    return out[taps:-taps, taps:-taps]


def glow_of(image, boxes):
    """The halo around the picture's ink as the game's renderer draws one (pc/src/ui_text.c picture_glow_render: the ink's coverage blurred by a Gaussian of reach
    `radius` (3 sigma), alpha = 1 - exp(-4 strength x blur) of the colour): the (colour, radius, strength) that best reproduce the pixels around the ink (those not
    ink, within the reach of it), by a search over radius and strength with the colour's brightness solved for each. None if the picture has no halo."""
    rgb = image[..., :3].astype(float)
    bright = rgb.max(axis=2)
    ink = bright > INK
    near = np.zeros_like(ink)
    for x0, y0, x1, y1 in boxes:
        near[max(y0 - 12, 0):y1 + 12, max(x0 - 12, 0):x1 + 12] = True
    halo = near & ~ink & (bright > 12)
    if halo.sum() < 20:
        return None
    direction = rgb[halo].sum(axis=0)
    direction /= direction.max()
    mask = ink.astype(float)
    best = None
    for radius in np.arange(1.5, 12.01, 0.5):
        soft = blur(mask, radius / 3)
        for strength in np.arange(0.1, 6.01, 0.1):
            alpha = (1 - np.exp(-4 * strength * soft))[halo]
            target = (rgb[halo] * direction).sum(axis=1)  # (the pixels' brightness along the colour's direction)
            level = min(float((alpha * target).sum() / max((alpha * alpha).sum(), 1e-9)), 255.0)  # (the colour's brightness, least squares)
            error = float(((alpha * level - target) ** 2).sum())
            if best is None or error < best[0]:
                best = (error, radius, strength, level)
    _, radius, strength, level = best
    colour = tuple(int(v) for v in np.clip(direction * level, 0, 255).round())
    return colour, float(radius), float(strength)


def read_words(image, box):
    """The words of a line by tesseract (black on white, four times as large), or ''."""
    exe = shutil.which("tesseract")
    if not exe:
        return ""
    x0, y0, x1, y1 = box
    crop = image[max(0, y0 - 3):y1 + 3, max(0, x0 - 6):x1 + 6]
    grey = np.where(crop[..., 3] > 0, crop[..., :3].max(axis=2), 0).astype(np.uint8)
    picture = Image.fromarray(255 - np.minimum(grey.astype(int) * 2, 255).astype(np.uint8)).resize((grey.shape[1] * 4, grey.shape[0] * 4), Image.LANCZOS)
    with tempfile.TemporaryDirectory() as d:
        path = pathlib.Path(d) / "line.png"
        picture.save(path)
        done = subprocess.run([exe, str(path), "stdout", "--psm", "7"], capture_output=True, text=True)
    return " ".join(done.stdout.split())


fills = {}  # the earlier pack's fill line per image (kept when the player has edited it: it no longer says `measured`)


def read_pack(path):
    """An earlier pack's words by (image, box)."""
    words, image = {}, 0
    global fills
    fills = {}
    if path.exists():
        for line in path.read_text().splitlines():
            if line.startswith("[") and line.endswith("]"):
                image = int(line[1:-1])
            elif line.startswith("line ") and "|" in line:
                head, _, rest = line.partition("|")
                box = tuple(int(v) for v in head.split()[1:5])
                words[(image, box)] = rest.rpartition("|")[2].strip()  # (the words follow the last bar; an older pack held colours per row between two)
            elif line.startswith("fill "):
                fills[image] = line.strip()
    return words


def font_hints(image, box, words, font):
    """The pack's `font=` keys for a picture of text set in `font` (a file): the horizontal stretch and weight (ems) that make the font's capitals as tall and wide as the
    picture's and its upright stem as thick, as the game lays a line out (pc/src/ui_text.c picture_layout: the words as wide as the box, the baseline's cap height following
    from the font's own proportions, stretched sideways by `font_stretch`; the weight is a distance added to every edge). Measured from the picture: the capitals' height
    (the rows where at least a third of the widest row's ink is), the box's width, and the stem of the first letter of the second word (a vertical stroke: its width where the
    brightness is 70% of the way from the glow beside it to its peak). Returns (stretch, embolden), or None if the font cannot be read."""
    from PIL import ImageFont
    x0, y0, x1, y1 = box
    ink = image[..., :3].max(axis=2) > INK
    rows = ink[y0:y1 + 1, x0:x1 + 1].sum(axis=1)
    cap_rows = np.nonzero(rows >= rows.max() / 3)[0]
    cap = float(cap_rows.max() - cap_rows.min() + 1)                           # (PS1 pixels)
    mid = y0 + int(cap_rows.mean())
    cols = np.nonzero(ink[y0:y1 + 1, x0:x1 + 1].any(axis=0))[0] + x0
    gaps = np.nonzero(np.diff(cols) > 3)[0]                                    # (the first space: the second word starts after it)
    stem_x = int(cols[gaps[0] + 1]) if len(gaps) else int(cols[0])
    bright = image[..., :3].max(axis=2).astype(float)
    window = bright[mid - 2:mid + 3, stem_x - 2:stem_x + 4].mean(axis=0)       # (six columns: the stem, a column's glow either side)
    level = float(window[0]) + 0.7 * (float(window.max()) - float(window[0]))  # (the stem's core: where it is 70% of the way to its peak; the soft edge is the picture's own blur)
    k = int(np.argmax(window))
    lo = k
    while lo > 0 and window[lo - 1] >= level:
        lo -= 1
    hi = k
    while hi < len(window) - 1 and window[hi + 1] >= level:
        hi += 1
    left = lo - (window[lo] - level) / max(window[lo] - window[lo - 1], 1e-6) if lo > 0 else float(lo)  # (the crossings, interpolated)
    right = hi + (window[hi] - level) / max(window[hi] - window[hi + 1], 1e-6) if hi < len(window) - 1 else float(hi)
    stem = right - left                                                        # (PS1 pixels, between the crossings)
    try:
        f = ImageFont.truetype(str(font), 400)
    except OSError:
        return None
    ink_box = lambda t: f.getmask(t).getbbox()  # noqa: E731 (the ink's box: no side bearings)
    h = ink_box("H")
    cap_px = float(h[3] - h[1])
    bb = ink_box(words.replace("~", ""))
    span_px = float(bb[2] - bb[0])
    letter = np.asarray(f.getmask("H")).reshape(f.getmask("H").size[::-1]) > 127  # (the H's left stem, above its bar)
    row = letter[h[1] + int((h[3] - h[1]) * 0.2)]
    left = int(np.argmax(row))
    stem_px = float(np.argmax(~row[left:]))
    want = float(x1 - x0)
    stretch = (cap_px / span_px) * want / cap                                  # (cap height = want * cap_px / (span_px * stretch))
    scale = cap / cap_px                                                       # (PS1 pixels per font pixel, vertically)
    em = 400 * scale                                                           # (the em in PS1 pixels, vertically)
    own = stem_px * scale * stretch                                            # (the font's stem as the line is stretched)
    embolden = (stem - own) / 2 / (em * min(stretch, 1.0))
    return stretch, embolden


FONT_PICTURES = {"JO"}  # GIM pictures whose pack also gets the font keys (the opening quote); NOWLOADING always does
CAPS = 0.8              # a small capital's height against a capital's (the renderer's default, gim_text_caps)


def font_hints_lines(image, boxes, words, font, caps=CAPS):
    """`font_hints` for a picture of several lines set in small capitals (the opening quote): the stretch and weight (ems) of the font such that, with every line made as wide as its
    box, the capitals come out as tall as the picture's and the strokes as thick (the medians over the lines). A line's capital height is its ink's top to its baseline (the last
    row holding a third of the line's densest row's ink); the weight is the distance every edge of the font's line (set as the renderer does: lower case as capitals of `caps` the height) must move for its ink area to equal the
    picture's (area over edge length). Returns (stretch, embolden)."""
    from PIL import Image, ImageDraw, ImageFont
    try:
        f = ImageFont.truetype(str(font), 200)
    except OSError:
        return None
    ink = image[..., :3].max(axis=2) > INK
    stretches, strokes, owns, ems, edges = [], [], [], [], []
    for (x0, y0, x1, y1), text in zip(boxes, words):
        text = text.replace("~", "")
        band = ink[y0:y1 + 1, x0:x1 + 1]
        rows = band.sum(axis=1)
        dense = np.nonzero(rows >= rows.max() / 3)[0]
        cap = float(dense.max() + 1 - np.nonzero(rows)[0].min())            # (PS1 pixels: top of the capitals to the baseline)
        target = float(band.sum())                                          # (the picture's ink in this line, PS1 pixels squared)
        # the line as the renderer sets it: lower case as capitals of `caps` the height, at one advance each
        image_big = Image.new("L", (int(f.getlength(text.upper()) * 1.3) + 400, 700), 0)
        draw = ImageDraw.Draw(image_big)
        pen, small = 100.0, ImageFont.truetype(str(font), int(200 * caps))
        base = 400
        for ch in text:
            low = ch.islower()
            g = small if low else f
            draw.text((pen, base), ch.upper(), font=g, fill=255, anchor="ls")
            pen += g.getlength(ch.upper())
        a = np.asarray(image_big) > 127
        ys, xs = np.nonzero(a)
        span, cap_px = float(xs.max() - xs.min() + 1), float(base - ys.min())
        want = float(x1 - x0 + 1)
        stretches.append(cap_px / span * want / cap)
        scale_y = cap / cap_px                                              # (PS1 pixels per font pixel, vertically)
        ems.append(200 * scale_y)
        sx = scale_y * stretches[-1]
        edge = a & ~(np.roll(a, 1, 0) & np.roll(a, -1, 0) & np.roll(a, 1, 1) & np.roll(a, -1, 1))  # (the mask's boundary pixels)
        owns.append(float(a.sum()) * scale_y * sx)                         # (its ink as the line sets it, and the length of its edge)
        strokes.append(target)
        edges.append(float(edge.sum()) * (scale_y + sx) / 2)
    stretch = float(np.median(stretches))
    grow = float(np.median([(t - o) / e for t, o, e in zip(strokes, owns, edges)]))  # (the distance every edge must move for the ink to match: area / edge length)
    return stretch, grow / (float(np.median(ems)) * min(stretch, 1.0))


def near(before, image, box):
    """The words of an earlier line of the image that this box (nearly) covers: a line whose box changed (an ornament no longer in it)."""
    best, area = "", 0
    for (j, b), words in before.items():
        if j != image or not words:
            continue
        w = min(b[2], box[2]) - max(b[0], box[0])
        h = min(b[3], box[3]) - max(b[1], box[1])
        if w > 0 and h > 0 and w * h > area and w * h * 2 > (b[2] - b[0]) * (b[3] - b[1]):
            best, area = words, w * h
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", default=str(ROOT / "data/GIM"))
    ap.add_argument("--exe", default=str(ROOT / "data/SLUS_010.40"))
    ap.add_argument("--out", default=str(ROOT / "pc/user/gim_text"))
    ap.add_argument("--ocr", choices=["auto", "off"], default="auto")
    ap.add_argument("--font", default="pc/third_party/fonts/Marcellus/Marcellus-Regular.ttf", help="NOWLOADING and the quote (JO): the font file (repo-relative) the words are set in; the pack gets its `font=` keys (\"\": the pictures style's font). The original's capitals are thin and wide, a flared humanist sans (Optima-like); Marcellus (OFL) is the nearest free face")
    ap.add_argument("--angle", type=float, default=0.0, help="measure the fill at this angle (0: left to right, 90: top to bottom)")
    ap.add_argument("names", nargs="+")
    args = ap.parse_args()
    src, dst = pathlib.Path(args.data), pathlib.Path(args.out)
    dst.mkdir(parents=True, exist_ok=True)
    pictures = [n for n in args.names if n in EXE_PICTURES]
    files = sorted({f for n in args.names if n not in EXE_PICTURES for f in src.glob(n if n.upper().endswith(".GIM") else n + ".GIM")})
    if not files and not pictures:
        print("no GIM files for", args.names, "in", src, file=sys.stderr)
        return 1
    global INK
    jobs = [(f.stem, f.name, None) for f in files] + [(n, n, n) for n in pictures]
    for stem, label, special in jobs:
        f = pathlib.Path(src / (stem + ".GIM"))
        INK = EXE_PICTURES[special][1] if special else 40
        path = dst / f"{stem}.txt"
        before = read_pack(path)
        text = [f"# gim_text pack of {label}: the lines of the pictures' ink; the words are yours (pc/tools/gim_text_pack.py keeps them)"]
        total = missing = 0
        for j, image in enumerate([exe_picture(args.exe, EXE_PICTURES[special][0])] if special else gim_decode.decode(f)):
            found = lines_of(image)
            if not found:
                continue
            text.append(f"[{j}]")
            old = fills.get(j)
            if old and " measured" not in old.split("|")[0] + " ":
                text.append(old)  # (the player's own: it wins and survives)
            else:
                variation, flat, stops = gradient_of(image, found, args.angle, EXE_PICTURES[special][3] if special else 0.5)
                halo = glow_of(image, found) if special else None
                glow = " glow=%02x%02x%02x glow_radius=%g glow_strength=%.2f" % (*halo[0], halo[1], halo[2]) if halo else ""
                if variation < FLAT_BELOW:
                    text.append("fill flat%s measured | %02x%02x%02x" % (glow, *flat))
                else:
                    text.append("fill linear angle=%g scope=block strength=1%s measured | " % (args.angle, glow) + " ".join("%.3f:%02x%02x%02x" % (pos, *c) for pos, c in stops))
            fill_at = len(text) - 1
            lines_words = []
            for box in found:
                words = before.get((j, box)) or near(before, j, box) or (EXE_PICTURES[special][2] if special else read_words(image, box) if args.ocr == "auto" else "")
                total += 1
                missing += not words
                text.append("line %d %d %d %d | %s" % (*box, words))
                lines_words.append(words)
            if args.font and text[fill_at].startswith("fill ") and " measured" in text[fill_at] and all(lines_words) and (special or stem in FONT_PICTURES):
                hints = font_hints(image, found[0], lines_words[0], ROOT / args.font) if special else font_hints_lines(image, found, lines_words, ROOT / args.font)
                if hints:
                    text[fill_at] = text[fill_at].replace(" measured", " font=%s font_stretch=%.3f font_embolden=%.3f measured" % (args.font, *hints), 1)
        path.write_text("\n".join(text) + "\n")
        print(f"{label}: {total} line(s), {missing} without words -> {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
