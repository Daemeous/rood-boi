#!/usr/bin/env python3
"""The Quick manual's page files (SMALL/HELPnn.HF0 / .HF1, MENUE) read from
the user's disc image and summarised on stdout: the paths, line / sprite /
rule counts, every control code and pictograph with its count, the FB
(colour / font table) operands, the sprites' colour depth and CLUT offsets.
A dev aid for the native reader (pc/src/ui/manual_parse.c); it prints no
text of the game's.

--art lists every distinct illustration (composited as manual_parse.c does)
with its size and pixel hash (pc_ui_manual_art_hash), the source of
manual_parse.c's button_art table; --sheet PATH also writes them, labelled,
to one PNG to look at (a local file: never commit it).

  python3 pc/tools/manual_dump.py [--disc PATH] [--art [--sheet PATH]]
"""
import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fixture_lib import Disc  # noqa: E402

PICTO = {0xAD: "note", 0xAE: "triangle", 0xAF: "square", 0xB0: "circle", 0xB1: "cross", 0xB2: "left",
         0xB3: "right", 0xB4: "up", 0xB5: "down", 0xB6: "lv", 0xB7: "star", 0xB8: "block"}


def strings(hf0):
    n = struct.unpack_from("<H", hf0, 16)[0]
    out = []
    for i in range(n):
        off = struct.unpack_from("<H", hf0, 16 + 2 * (i + 1))[0] * 2 + 16
        end = hf0.index(0xE7, off)
        out.append(hf0[off:end + 1])
    return out


def walk(s, codes, fb):
    i = 0
    while i < len(s):
        c = s[i]
        i += 1
        if c == 0xE7:
            break
        if c >= 0xEC:
            op = s[i] if i < len(s) else -1
            i += 1
            codes["%02X" % c] += 1
            if c == 0xFB:
                fb[op] += 1
        elif c in PICTO:
            codes[PICTO[c]] += 1
        elif c >= 0xE5:
            codes["%02X" % c] += 1


def composite(hf0, hf1, o):
    """One sprite's RGBA (manual_parse.c composite), or None."""
    w, h, nb, colors, clut = struct.unpack_from("<5h", hf0, o + 20)
    nblocks, ncluts = struct.unpack_from("<II", hf1, 0)
    blocks, cluts = 8, 8 + nblocks * 128
    if w <= 0 or h <= 0 or colors not in (16, 256):
        return None
    stride, clut = (16 if colors == 16 else 8), clut & 0xFFFF
    out, b = bytearray(w * h * 4), 0
    for y0 in range(0, h, 16):
        for x0 in range(0, w, stride):
            if b >= nb:
                b += 1
                continue
            bid = struct.unpack_from("<h", hf0, o + 30 + 2 * b)[0]
            b += 1
            if bid < 0 or bid >= nblocks:
                continue
            blk = blocks + bid * 128
            for py in range(min(16, h - y0)):
                for px in range(min(stride, w - x0)):
                    if stride == 8:
                        i = hf1[blk + py * 8 + px]
                        line, entry = clut + i // 16, i % 16
                    else:
                        i = hf1[blk + py * 8 + px // 2]
                        line, entry = clut, (i >> 4 if px & 1 else i & 15)
                    if line >= ncluts:
                        continue
                    c = struct.unpack_from("<H", hf1, cluts + line * 32 + entry * 2)[0]
                    if not c:
                        continue
                    p = ((y0 + py) * w + x0 + px) * 4
                    out[p:p + 4] = bytes([(c & 31) << 3 | (c & 31) >> 2, (c >> 5 & 31) << 3 | (c >> 5 & 31) >> 2,
                                          (c >> 10 & 31) << 3 | (c >> 10 & 31) >> 2, 255])
    return w, h, bytes(out)


def art_hash(w, h, rgba):
    """manual_parse.c pc_ui_manual_art_hash: FNV-1a 64 of w, h (u16 LE) and the RGBA bytes."""
    v = 14695981039346656037
    for c in struct.pack("<HH", w, h) + rgba:
        v = ((v ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return v


def art(disc, sheet):
    seen, images = {}, []
    for t in range(1, 15):
        hf0, hf1 = disc.read("SMALL/HELP%02d.HF0" % t), disc.read("SMALL/HELP%02d.HF1" % t)
        spr = struct.unpack_from("<I", hf0, 0)[0]
        base = 16 + spr
        for i in range(struct.unpack_from("<H", hf0, base)[0]):
            o = base + struct.unpack_from("<h", hf0, base + 2 + 2 * i)[0] * 2
            img = composite(hf0, hf1, o)
            if not img:
                continue
            v = art_hash(*img)
            if v in seen:
                continue
            seen[v] = "HELP%02d sprite %d" % (t, i)
            images.append((seen[v], img))
            print("%s: %d x %d, hash 0x%016xull" % (seen[v], img[0], img[1], v))
    if sheet:
        from PIL import Image, ImageDraw
        cell, cols = 200, 6
        out = Image.new("RGB", (cols * cell, (len(images) + cols - 1) // cols * cell), (60, 60, 70))
        d = ImageDraw.Draw(out)
        for n, (label, (w, h, rgba)) in enumerate(images):
            x, y = n % cols * cell, n // cols * cell
            k = max(1, min(4, (cell - 10) // w, (cell - 24) // h))
            im = Image.frombytes("RGBA", (w, h), rgba).resize((w * k, h * k), Image.NEAREST)
            out.paste(im, (x + 5, y + 20), im)
            d.text((x + 5, y + 4), label, fill=(255, 255, 0))
        out.save(sheet)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--disc")
    ap.add_argument("--art", action="store_true")
    ap.add_argument("--sheet")
    a = ap.parse_args()
    disc = Disc(a.disc)
    if a.art:
        art(disc, a.sheet)
        return
    small = disc.directory("SMALL")
    total = collections.Counter()
    fb_all = collections.Counter()
    for t in range(1, 15):
        n0, n1 = "HELP%02d.HF0" % t, "HELP%02d.HF1" % t
        hf0, hf1 = disc.read("SMALL/" + n0), disc.read("SMALL/" + n1)
        spr_off, lin_off, lin_size, _ = struct.unpack_from("<IIII", hf0, 0)
        lines = strings(hf0)
        codes, fb = collections.Counter(), collections.Counter()
        for s in lines:
            walk(s, codes, fb)
        base = 16 + spr_off
        ns = struct.unpack_from("<H", hf0, base)[0]
        depth, clutx = collections.Counter(), collections.Counter()
        anim = 0
        for i in range(ns):
            o = base + struct.unpack_from("<h", hf0, base + 2 + 2 * i)[0] * 2
            en = struct.unpack_from("<h", hf0, o)[0]
            x, y, w, h, nb, colors, packed = struct.unpack_from("<7h", hf0, o + 16)
            depth[colors] += 1
            clutx[packed & 15] += 1
            anim += en != 0
        nl = struct.unpack_from("<H", hf0, base + lin_off)[0] if lin_size > 0 else 0
        nblocks, ncluts = struct.unpack_from("<II", hf1, 0)
        print("SMALL/%s %d B, SMALL/%s %d B: %d lines, %d sprites (%d animated; colours %s; clut_x %s), "
              "%d rules, HF1 %d blocks %d cluts" % (n0, len(hf0), n1, len(hf1), len(lines), ns, anim,
                                                    dict(depth), dict(clutx), nl, nblocks, ncluts))
        print("   codes %s fb %s" % (dict(sorted(codes.items())), dict(sorted(fb.items()))))
        total.update(codes)
        fb_all.update(fb)
    print("all codes %s" % dict(sorted(total.items())))
    print("all fb operands %s" % dict(sorted(fb_all.items())))


if __name__ == "__main__":
    main()
