#!/usr/bin/env python3
"""Write the text pack of the game's pictures of text (gitea #14): for each GIM file the lines of ink (a box and a colour per row each), and their words.

    python3 pc/tools/gim_text_pack.py [--data data/GIM] [--out pc/user/gim_text] [--ocr auto|off] [NAME ...]    (JO DEMO_001 STAFF001 ...; STAFF* globs)

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


def gradient_of(image, boxes, angle=0.0):
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
                columns.append(np.median(pixels[weight >= 0.5 * weight.max()], axis=0))
            else:
                columns.append(None)
        x1 = x0 + steps
    for x in (range(x0, x1) if not angle else []):
        pixels = np.concatenate([rgb[b[1]:b[3], x][ink[b[1]:b[3], x]] for b in boxes if b[0] <= x < b[2]] or [np.zeros((0, 3), int)])
        if len(pixels):
            weight = pixels.max(axis=1)
            pixels = pixels[weight >= 0.5 * weight.max()]  # (the ink's body: not the dim edge pixels)
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
    ap.add_argument("--out", default=str(ROOT / "pc/user/gim_text"))
    ap.add_argument("--ocr", choices=["auto", "off"], default="auto")
    ap.add_argument("--angle", type=float, default=0.0, help="measure the fill at this angle (0: left to right, 90: top to bottom)")
    ap.add_argument("names", nargs="+")
    args = ap.parse_args()
    src, dst = pathlib.Path(args.data), pathlib.Path(args.out)
    dst.mkdir(parents=True, exist_ok=True)
    files = sorted({f for n in args.names for f in src.glob(n if n.upper().endswith(".GIM") else n + ".GIM")})
    if not files:
        print("no GIM files for", args.names, "in", src, file=sys.stderr)
        return 1
    for f in files:
        path = dst / f"{f.stem}.txt"
        before = read_pack(path)
        text = [f"# gim_text pack of {f.name}: the lines of the pictures' ink; the words are yours (pc/tools/gim_text_pack.py keeps them)"]
        total = missing = 0
        for j, image in enumerate(gim_decode.decode(f)):
            found = lines_of(image)
            if not found:
                continue
            text.append(f"[{j}]")
            old = fills.get(j)
            if old and " measured" not in old.split("|")[0] + " ":
                text.append(old)  # (the player's own: it wins and survives)
            else:
                variation, flat, stops = gradient_of(image, found, args.angle)
                if variation < FLAT_BELOW:
                    text.append("fill flat measured | %02x%02x%02x" % flat)
                else:
                    text.append("fill linear angle=%g scope=block strength=1 measured | " % args.angle + " ".join("%.3f:%02x%02x%02x" % (pos, *c) for pos, c in stops))
            for box in found:
                words = before.get((j, box)) or near(before, j, box) or (read_words(image, box) if args.ocr == "auto" else "")
                total += 1
                missing += not words
                text.append("line %d %d %d %d | %s" % (*box, words))
        path.write_text("\n".join(text) + "\n")
        print(f"{f.name}: {total} line(s), {missing} without words -> {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
