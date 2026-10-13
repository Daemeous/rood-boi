#!/usr/bin/env python3
"""Match the game's text styles against open-licensed fonts (README "2D / UI":
ui_text).

    python3 pc/tools/font_match.py capture        # VRAM from the running game (needs pc/save/card1.mcd or --card)
    python3 pc/tools/font_match.py fetch          # candidate fonts into pc/build/font_match/fonts
    python3 pc/tools/font_match.py rank [--top N] # scores, report.txt and contact sheets
    python3 pc/tools/font_match.py all
    python3 pc/tools/font_match.py digits         # HUD digit proportions per size (ui_text DIGITS_ASPECT)
    python3 pc/tools/font_match.py labels [--write]  # fixed label boxes and hashes (pc/src/ui_labels.h)

Everything this writes stays under pc/build/font_match (git-ignored): the
glyphs come from YOUR copy of the game (its VRAM while running, plus the
glyph advance tables in data/BATTLE/BATTLE.PRG), the candidates are
downloaded from the Google Fonts repository (SIL OFL / Apache 2.0 only).
Nothing here is committed except this script; only the fonts picked for
ui_text are vendored (pc/third_party/fonts, THIRD_PARTY.md).

Styles (where the game draws them):
  dialogue  talk-bubble lettering: italic hand-lettered capitals with small
            capitals for lower case; font page (832, 0), glyphs 189-377,
            12 x 12 cells, proportional advances (_printFixedWidthFont,
            vs_battle_printVariableWidthFontChar)
  menu      upright face of the same page, glyphs 0-188: menu rows, item
            names, the information box (vs_mainMenu_renderRowLabel, the
            text boxes with font table 0); 6-pixel fixed advance
  info      wide outlined capitals of the field UI page (768, 0), rows
            76-95, proportional widths and kerning pairs
            (vs_battle_renderTextRawColor: RISK ZERO, COMMAND, NEXT)
  digits    HUD numbers (HP/MP/RISK), two sizes on the UI page row 0
            (_renderDigit)

Placement is the one pc/src/ui_text.c uses at run time: the font's cap
height scaled to the original capitals (measured on H), its baseline on the
original's, each glyph centred on the original glyph's ink, a per-style
horizontal scale fitted to the original widths (small capitals: the
upper-case glyph at the original small-capital height). Scores compare the
1x coverage the replacement would have with the original's (soft IoU,
chamfer distance of the ink, stroke weight); lower is better."""
import argparse
import json
import math
import os
import pathlib
import shutil
import sys
import urllib.request

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parents[1].parent
WORK = ROOT / "pc/build/font_match"
FONTS = WORK / "fonts"
BATTLE_BASE = 0x80068800  # BATTLE.PRG load address (config/BATTLE/BATTLE.PRG/splat.yaml)
GF = "https://raw.githubusercontent.com/google/fonts/main"

# The game's character set for glyph indices 0..0x7C (tools/etc/vsString.py).
CHARSET = ("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

# Candidates: {style: [(directory in google/fonts, licence directory, [files])]}.
# Static files where a family has them; variable fonts are used at their
# default instance (the runtime rasterizer, stb_truetype, reads that one);
# weight is then explored through the distance field (EMBOLDEN).
CANDIDATES = {
    "dialogue": [
        ("patrickhandsc", "ofl", ["PatrickHandSC-Regular.ttf"]),
        ("patrickhand", "ofl", ["PatrickHand-Regular.ttf"]),
        ("comicneue", "ofl", ["ComicNeue-Italic.ttf", "ComicNeue-BoldItalic.ttf", "ComicNeue-Regular.ttf",
                              "ComicNeue-Bold.ttf"]),
        ("comicrelief", "ofl", ["ComicRelief-Regular.ttf", "ComicRelief-Bold.ttf"]),
        ("kalam", "ofl", ["Kalam-Light.ttf", "Kalam-Regular.ttf", "Kalam-Bold.ttf"]),
        ("gochihand", "ofl", ["GochiHand-Regular.ttf"]),
        ("architectsdaughter", "ofl", ["ArchitectsDaughter-Regular.ttf"]),
        ("shadowsintolight", "ofl", ["ShadowsIntoLight.ttf"]),
        ("caveat", "ofl", ["Caveat[wght].ttf"]),
        ("indieflower", "ofl", ["IndieFlower-Regular.ttf"]),
        ("handlee", "ofl", ["Handlee-Regular.ttf"]),
        ("schoolbell", "apache", ["Schoolbell-Regular.ttf"]),
        ("comingsoon", "apache", ["ComingSoon-Regular.ttf"]),
        ("shortstack", "ofl", ["ShortStack-Regular.ttf"]),
        ("neucha", "ofl", ["Neucha.ttf"]),
        ("walterturncoat", "apache", ["WalterTurncoat-Regular.ttf"]),
        ("justanotherhand", "apache", ["JustAnotherHand-Regular.ttf"]),
        ("kranky", "apache", ["Kranky-Regular.ttf"]),
        ("chewy", "apache", ["Chewy-Regular.ttf"]),
        ("bangers", "ofl", ["Bangers-Regular.ttf"]),
        ("permanentmarker", "apache", ["PermanentMarker-Regular.ttf"]),
        ("mansalva", "ofl", ["Mansalva-Regular.ttf"]),
        ("sriracha", "ofl", ["Sriracha-Regular.ttf"]),
        ("itim", "ofl", ["Itim-Regular.ttf"]),
        ("gaegu", "ofl", ["Gaegu-Regular.ttf", "Gaegu-Bold.ttf"]),
        ("amaticsc", "ofl", ["AmaticSC-Regular.ttf", "AmaticSC-Bold.ttf"]),
        ("gloriahallelujah", "ofl", ["GloriaHallelujah.ttf"]),
        ("coveredbyyourgrace", "ofl", ["CoveredByYourGrace.ttf"]),
        ("annieuseyourtelescope", "ofl", ["AnnieUseYourTelescope-Regular.ttf"]),
        ("fuzzybubbles", "ofl", ["FuzzyBubbles-Regular.ttf", "FuzzyBubbles-Bold.ttf"]),
        ("shantellsans", "ofl", ["ShantellSans-Italic[BNCE,INFM,SPAC,wght].ttf", "ShantellSans[BNCE,INFM,SPAC,wght].ttf"]),
        ("playpensans", "ofl", ["PlaypenSans[wght].ttf"]),
        ("grandstander", "ofl", ["Grandstander-Italic[wght].ttf"]),
        ("mynerve", "ofl", ["Mynerve-Regular.ttf"]),
        ("delius", "ofl", ["Delius-Regular.ttf"]),
    ],
    "menu": [
        ("robotocondensed", "ofl", ["RobotoCondensed[wght].ttf"]),
        ("roboto", "ofl", ["Roboto[wdth,wght].ttf"]),
        ("barlowcondensed", "ofl", ["BarlowCondensed-Regular.ttf", "BarlowCondensed-Medium.ttf",
                                    "BarlowCondensed-SemiBold.ttf"]),
        ("barlowsemicondensed", "ofl", ["BarlowSemiCondensed-Medium.ttf", "BarlowSemiCondensed-SemiBold.ttf"]),
        ("barlow", "ofl", ["Barlow-Medium.ttf", "Barlow-SemiBold.ttf"]),
        ("oswald", "ofl", ["Oswald[wght].ttf"]),
        ("ptsansnarrow", "ofl", ["PT_Sans-Narrow-Web-Regular.ttf", "PT_Sans-Narrow-Web-Bold.ttf"]),
        ("ptsans", "ofl", ["PT_Sans-Web-Regular.ttf", "PT_Sans-Web-Bold.ttf"]),
        ("archivonarrow", "ofl", ["ArchivoNarrow[wght].ttf"]),
        ("firasanscondensed", "ofl", ["FiraSansCondensed-Regular.ttf", "FiraSansCondensed-Medium.ttf",
                                      "FiraSansCondensed-SemiBold.ttf"]),
        ("firasans", "ofl", ["FiraSans-Medium.ttf", "FiraSans-SemiBold.ttf"]),
        ("ibmplexsanscondensed", "ofl", ["IBMPlexSansCondensed-Medium.ttf", "IBMPlexSansCondensed-SemiBold.ttf",
                                         "IBMPlexSansCondensed-Bold.ttf"]),
        ("sairacondensed", "ofl", ["SairaCondensed-Medium.ttf", "SairaCondensed-SemiBold.ttf",
                                   "SairaCondensed-Bold.ttf"]),
        ("encodesanscondensed", "ofl", ["EncodeSansCondensed-Medium.ttf", "EncodeSansCondensed-SemiBold.ttf",
                                        "EncodeSansCondensed-Bold.ttf"]),
        ("asapcondensed", "ofl", ["AsapCondensed-Medium.ttf", "AsapCondensed-SemiBold.ttf",
                                  "AsapCondensed-Bold.ttf"]),
        ("pathwaygothicone", "ofl", ["PathwayGothicOne-Regular.ttf"]),
        ("antonio", "ofl", ["Antonio[wght].ttf"]),
        ("fjallaone", "ofl", ["FjallaOne-Regular.ttf"]),
        ("sofiasanscondensed", "ofl", ["SofiaSansCondensed[wght].ttf"]),
        ("sharetech", "ofl", ["ShareTech-Regular.ttf"]),
        ("abel", "ofl", ["Abel-Regular.ttf"]),
        ("mohave", "ofl", ["Mohave[wght].ttf"]),
        ("teko", "ofl", ["Teko[wght].ttf"]),
        ("khand", "ofl", ["Khand-Medium.ttf", "Khand-SemiBold.ttf"]),
        ("rajdhani", "ofl", ["Rajdhani-Medium.ttf", "Rajdhani-SemiBold.ttf", "Rajdhani-Bold.ttf"]),
        ("pragatinarrow", "ofl", ["PragatiNarrow-Regular.ttf", "PragatiNarrow-Bold.ttf"]),
        ("yanonekaffeesatz", "ofl", ["YanoneKaffeesatz[wght].ttf"]),
        ("lato", "ofl", ["Lato-Medium.ttf", "Lato-SemiBold.ttf", "Lato-Bold.ttf"]),
        ("inter", "ofl", ["Inter[opsz,wght].ttf"]),
        ("sourcesans3", "ofl", ["SourceSans3[wght].ttf"]),
        ("notosans", "ofl", ["NotoSans[wdth,wght].ttf"]),
        ("worksans", "ofl", ["WorkSans[wght].ttf"]),
        ("publicsans", "ofl", ["PublicSans[wght].ttf"]),
    ],
    "info": [
        ("michroma", "ofl", ["Michroma-Regular.ttf"]),
        ("orbitron", "ofl", ["Orbitron[wght].ttf"]),
        ("jura", "ofl", ["Jura[wght].ttf"]),
        ("encodesansexpanded", "ofl", ["EncodeSansExpanded-Medium.ttf", "EncodeSansExpanded-SemiBold.ttf",
                                       "EncodeSansExpanded-Bold.ttf"]),
        ("lexendexa", "ofl", ["LexendExa[wght].ttf"]),
        ("exo2", "ofl", ["Exo2[wght].ttf"]),
        ("chakrapetch", "ofl", ["ChakraPetch-Medium.ttf", "ChakraPetch-SemiBold.ttf", "ChakraPetch-Bold.ttf"]),
        ("kronaone", "ofl", ["KronaOne-Regular.ttf"]),
        ("syncopate", "apache", ["Syncopate-Regular.ttf", "Syncopate-Bold.ttf"]),
        ("oxanium", "ofl", ["Oxanium[wght].ttf"]),
        ("aldrich", "ofl", ["Aldrich-Regular.ttf"]),
        ("quantico", "ofl", ["Quantico-Regular.ttf", "Quantico-Bold.ttf"]),
        ("electrolize", "ofl", ["Electrolize-Regular.ttf"]),
        ("audiowide", "ofl", ["Audiowide-Regular.ttf"]),
        ("russoone", "ofl", ["RussoOne-Regular.ttf"]),
        ("zendots", "ofl", ["ZenDots-Regular.ttf"]),
        ("sarpanch", "ofl", ["Sarpanch-Medium.ttf", "Sarpanch-SemiBold.ttf", "Sarpanch-Bold.ttf"]),
        ("tomorrow", "ofl", ["Tomorrow-Medium.ttf", "Tomorrow-SemiBold.ttf", "Tomorrow-Bold.ttf"]),
    ],
    "digits": [
        # Old-style figures by default (the HUD's 3, 4, 5, 7, 9 descend).
        ("alegreya", "ofl", ["Alegreya[wght].ttf"]),
        ("ebgaramond", "ofl", ["EBGaramond[wght].ttf"]),
        ("sortsmillgoudy", "ofl", ["SortsMillGoudy-Regular.ttf"]),
        ("goudybookletter1911", "ofl", ["GoudyBookletter1911.ttf"]),
        ("fanwoodtext", "ofl", ["FanwoodText-Regular.ttf"]),
        ("lindenhill", "ofl", ["LindenHill-Regular.ttf"]),
        ("crimsontext", "ofl", ["CrimsonText-SemiBold.ttf", "CrimsonText-Bold.ttf"]),
        ("cardo", "ofl", ["Cardo-Bold.ttf"]),
        ("imfelldwpica", "ofl", ["IMFePIrm28P.ttf"]),
        ("cinzel", "ofl", ["Cinzel[wght].ttf"]),
        ("spectral", "ofl", ["Spectral-SemiBold.ttf"]),
        ("cormorant", "ofl", ["Cormorant[wght].ttf"]),
        ("alegreyasc", "ofl", ["AlegreyaSC-Bold.ttf"]),
    ],
}
# Digits also try the other styles' faces (the HUD numbers are lettered).
CANDIDATES["digits"] += CANDIDATES["dialogue"] + CANDIDATES["info"]
EMBOLDEN = (-0.02, 0.0, 0.02, 0.04)  # distance-field offsets tried, in em

SAMPLES = {
    "dialogue": ["Have you found Sydney?", "THE BLOOD-SIN IS UPON US"],
    "menu": ["BATTLE ABILITIES", "Heavy Shot  Gain Life", "Inflicts damage 70%"],
    "info": ["RISK ZERO", "COMMAND MENU", "NEXT 20PT"],
    "digits": ["157/253", "0123456789"],
}


# ---- the game's glyphs (from YOUR running game; cached in pc/build) ----

def capture(card):
    """VRAM of the field (the UI page is the field's, not the title's)."""
    sys.path.insert(0, str(ROOT / "pc/tools"))
    from rood_rpc import Game, symbols
    run = WORK / "run"
    run.mkdir(parents=True, exist_ok=True)
    copy = run / "card.mcd"
    shutil.copyfile(card, copy)
    game = Game.launch(socket_path=f"/tmp/rood-fm-{os.getpid()}.sock", log=str(run / "game.log"), card=str(copy),
                       fast=True, extra_env={"ROOD_OUT": str(run), "ROOD_SETTINGS": str(run / "settings.cfg")})
    try:
        game.wait_title()
        game.run(90)
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        if not game.state().get("player"):
            raise SystemExit("capture: the save did not reach the field")
        base = symbols()["vram"]
        raw = b"".join(game.read(base + y * 2048, 32 * 2048) for y in range(0, 512, 32))
    finally:
        game.close()
    np.save(WORK / "vram.npy", np.frombuffer(raw, "<u2").reshape(512, 1024))
    print("captured VRAM ->", WORK / "vram.npy")


def page4(vram, x):
    """A 4-bit texture page as indices (256 x 512)."""
    w = vram[:, x:x + 64]
    p = np.zeros((512, 256), np.uint8)
    for k in range(4):
        p[:, k::4] = (w >> (4 * k)) & 15
    return p


class Glyph:
    def __init__(self, char, cov, box_w, box_h, advance):
        self.char, self.cov, self.w, self.h, self.advance = char, cov, box_w, box_h, advance


def battle_tables():
    data = (ROOT / "data/BATTLE/BATTLE.PRG").read_bytes()

    def at(address, n):
        return data[address - BATTLE_BASE:address - BATTLE_BASE + n]

    return {"advance": at(0x800EB810, 378), "small_widths": at(0x800EB7B4, 32), "small_index": at(0x800EB7D4, 0x3C),
            "kern": at(0x800EB708, 169)}


def original_glyphs():
    """{style: {char: Glyph}} with coverage 0..1 of the glyph body."""
    vram = np.load(WORK / "vram.npy")
    tables = battle_tables()
    font = page4(vram, 832)
    ui = page4(vram, 768)
    styles = {"dialogue": {}, "menu": {}, "info": {}, "digits": {}, "digits_large": {}}
    for style, table in (("menu", 0), ("dialogue", 1)):
        for i, ch in enumerate(CHARSET + "!?.,'-"):
            index = i if i < len(CHARSET) else "!?.,'-".index(ch) + 0
            if ch in "!?.,'-":
                index = {"!": 0x90, "?": 0xA5, ".": 0xA0, ",": 0x9F, "'": 0x96, "-": 0xA7}[ch]
            g = index + table * 189
            r, c = divmod(g, 21)
            cell = font[r * 12:r * 12 + 12, c * 12:c * 12 + 12].astype(float) / 15.0
            styles[style][ch] = Glyph(ch, cell, 12, 12, tables["advance"][g])
    widths, index = tables["small_widths"], tables["small_index"]
    for code in range(0x21, 0x5B):
        c = code - 0x20
        w = (widths[c >> 1] >> ((c << 2) & 4)) & 15
        if not w or c == 10:
            continue
        u, v = index[c] & 0xFE, (index[c] & 1) * 10 + 0x4C
        box = ui[v:v + 10, u:u + w].astype(int)
        body = np.clip((box - 1) / 14.0, 0, 1)  # index 1: the black outline
        styles["info"][chr(code)] = Glyph(chr(code), body, w, 10, w)
    for d in range(10):
        box = ui[0:10, 6 * d:6 * d + 6].astype(int)
        styles["digits"][str(d)] = Glyph(str(d), np.clip((box - 1) / 9.0, 0, 1), 6, 10, 6)
        box = ui[0:12, 64 + 8 * d:64 + 8 * d + 7].astype(int)
        styles["digits_large"][str(d)] = Glyph(str(d), np.clip((box - 1) / 9.0, 0, 1), 7, 12, 7)
    return styles


def ink_box(cov, threshold=0.2):
    ys, xs = np.where(cov > threshold)
    if not len(xs):
        return None
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def style_metrics(glyphs, style):
    """Cap top, baseline and small-capital/x-height top of the original
    (pixel rows, from the ink of H and x)."""
    ref = "0" if style.startswith("digits") else "H"
    box = ink_box(glyphs[ref].cov)
    m = {"cap_top": box[1], "baseline": box[3]}
    if "x" in glyphs:
        xb = ink_box(glyphs["x"].cov)
        m["x_top"], m["x_base"] = xb[1], xb[3]
    return m


# ---- candidates ----

def fetch(styles):
    FONTS.mkdir(parents=True, exist_ok=True)
    seen = set()
    for style in styles:
        for directory, licence, files in CANDIDATES[style]:
            for name in files:
                if name in seen:
                    continue
                seen.add(name)
                path = FONTS / name
                if path.exists():
                    continue
                url = f"{GF}/{licence}/{directory}/{urllib.request.quote(name)}"
                try:
                    with urllib.request.urlopen(url, timeout=60) as r:
                        path.write_bytes(r.read())
                    print("fetched", name)
                except Exception as e:  # noqa: BLE001
                    print("cannot fetch", url, e)
    total = sum(p.stat().st_size for p in FONTS.iterdir())
    print(f"{len(list(FONTS.iterdir()))} fonts, {total / 1e6:.1f} MB in {FONTS}")


EM = 256  # reference outline size (pixels per em) for the distance fields
MARGIN = 24


class Face:
    """A font's glyphs as signed distance fields (EM pixels, positive
    inside), the way ui_text.c keeps them (there from stb_truetype)."""

    def __init__(self, path):
        self.path = path
        self.font = ImageFont.truetype(str(path), EM)
        self.name = path.stem
        self.cache = {}
        h = self.glyph("H")
        self.cap = (EM - h["box"][1]) if h else EM * 0.7
        desc = [g["box"][3] - EM for g in map(self.glyph, "gjpqy") if g]
        self.desc = max(desc) if desc else EM * 0.2

    def height(self, ref):
        """Ink height above the baseline of `ref` (H, or 0 for the digits)."""
        g = self.glyph(ref)
        return (EM - g["box"][1]) if g else self.cap

    def glyph(self, ch):
        if ch in self.cache:
            return self.cache[ch]
        from scipy.ndimage import distance_transform_edt
        image = Image.new("L", (EM * 2, EM * 2), 0)
        ImageDraw.Draw(image).text((EM // 2, EM), ch, fill=255, font=self.font, anchor="ls")  # baseline y = EM
        a = np.asarray(image, dtype=np.float32) / 255.0
        box = ink_box(a, 0.5)
        result = None
        if box is not None:
            x0, y0 = max(box[0] - MARGIN, 0), max(box[1] - MARGIN, 0)
            x1, y1 = min(box[2] + MARGIN, a.shape[1]), min(box[3] + MARGIN, a.shape[0])
            crop = a[y0:y1, x0:x1] >= 0.5
            sdf = distance_transform_edt(crop) - distance_transform_edt(~crop)
            sdf = np.where(crop, sdf - 0.5, sdf + 0.5)
            result = {"sdf": sdf.astype(np.float32), "origin": (x0, y0), "box": box}
        self.cache[ch] = result
        return result


def placement(face, glyph, metrics, params):
    """The replacement's geometry over an original glyph box (what
    ui_text.c computes): a function (xo, yo) -> distance in original
    pixels (positive inside), or None when the face lacks the glyph."""
    ch = glyph.char
    small = params.get("small_caps") and ch.islower()
    source = ch.upper() if small else ch
    g = face.glyph(source)
    orig = ink_box(glyph.cov)
    if g is None or orig is None:
        return None
    base = metrics["x_base"] if small else metrics["baseline"]
    height = (metrics["x_base"] - metrics["x_top"]) if small else (metrics["baseline"] - metrics["cap_top"])
    s = height / face.height(params.get("ref", "H"))  # original pixels per EM pixel
    x0, y0, x1, y1 = g["box"]
    room = glyph.w - 2.0 if params.get("outline") else max(orig[2] - orig[0], glyph.advance) + 0.5
    if params.get("aspect") and params.get("ref") == "0":
        s = digit_scale(face, s, params["aspect"], room)
    sx = s * params["stretch"]
    if (x1 - x0) * sx > room:
        sx = room / (x1 - x0)
    # Descenders squeezed into the box (the PS1 faces have none).
    below = glyph.h - base - (1.0 if params.get("outline") else 0.0) - 0.1
    sd = s
    if (y1 - EM) * s > below > 0:
        sd = below / (y1 - EM)
    cx_t, cx_s = (orig[0] + orig[2]) / 2.0, (x0 + x1) / 2.0
    if params.get("outline"):  # the ink keeps a pixel from the box's edges (ui_text.c)
        half = (x1 - x0) * sx / 2.0
        cx_t = min(max(cx_t, 1.0 + half), glyph.w - 1.0 - half)
    pivot = base - height / 2.0
    slant = params.get("slant", 0.0)
    bold = params.get("embolden", 0.0) * EM
    sdf, (ox, oy) = g["sdf"], g["origin"]
    from scipy.ndimage import map_coordinates

    def distance(xo, yo):
        up = base - yo
        ys = EM - np.where(up >= 0, up / s, up / sd)
        xs = cx_s + (xo - cx_t - slant * (pivot - yo)) / sx
        d = map_coordinates(sdf, [ys - oy, xs - ox], order=1, mode="constant", cval=-MARGIN)
        return (d + bold) * s

    return distance


def digit_scale(face, s, aspect, room):
    """ui_text.c digit_scale: one scale for a digit size, so that its widest
    digit is condensed to no less than `aspect` of the font's proportions."""
    widths = [g["box"][2] - g["box"][0] for g in map(face.glyph, "0123456789") if g]
    widest = max(widths) if widths else 0
    if widest and widest * s * aspect > room:
        s = room / (widest * aspect)
    return s


def coverage(face, glyph, metrics, params, factor):
    """Body coverage of the replacement at `factor` pixels per original
    pixel (pixel centres sampled; the edge is one output pixel wide)."""
    f = placement(face, glyph, metrics, params)
    if f is None:
        return np.zeros((glyph.h * factor, glyph.w * factor), np.float32)
    ys, xs = np.mgrid[0:glyph.h * factor, 0:glyph.w * factor]
    d = f((xs + 0.5) / factor, (ys + 0.5) / factor)
    return np.clip(0.5 + d * factor, 0, 1).astype(np.float32)


def downsample(cov, factor):
    h, w = cov.shape
    return cov.reshape(h // factor, factor, w // factor, factor).mean(axis=(1, 3))


def edt(mask):
    from scipy.ndimage import distance_transform_edt
    if not mask.any():
        return np.full(mask.shape, 6.0)
    return distance_transform_edt(~mask)


def glyph_score(a, b):
    """a: original coverage, b: replacement (same box)."""
    iou = np.minimum(a, b).sum() / max(np.maximum(a, b).sum(), 1e-6)
    ma, mb = a > 0.35, b > 0.35
    da, db = edt(ma), edt(mb)
    chamfer = 0.5 * ((db[ma].mean() if ma.any() else 3) + (da[mb].mean() if mb.any() else 3))
    weight = math.log(max(b.sum(), 1e-3) / max(a.sum(), 1e-3))
    return 1 - iou, min(chamfer, 3.0), weight


def evaluate(face, glyphs, metrics, params):
    """Lower is better: shape (soft IoU, chamfer), weight, distortion."""
    rows = []
    for g in glyphs.values():
        b = downsample(coverage(face, g, metrics, params, 4), 4)
        rows.append(glyph_score(g.cov, b) if b.any() else (1.0, 3.0, 0.0))
    r = np.array(rows)
    iou_l, chamfer, weight = r[:, 0].mean(), r[:, 1].mean(), r[:, 2].mean()
    score = (0.5 * iou_l + 0.3 * chamfer / 3.0 + 0.2 * min(1.0, abs(weight) * 2)
             + 0.15 * abs(math.log(params["stretch"])))
    return score, {"iou": 1 - iou_l, "chamfer": chamfer, "weight": math.exp(weight)}


def fit_stretch(face, glyphs, metrics, small_caps, ref):
    """Horizontal scale that gives the face the original's ink widths."""
    ratios = []
    for ch, g in glyphs.items():
        small = small_caps and ch.islower()
        r = face.glyph(ch.upper() if small else ch)
        orig = ink_box(g.cov)
        if r is None or orig is None or not ch.isalnum():
            continue
        height = (metrics["x_base"] - metrics["x_top"]) if small else (metrics["baseline"] - metrics["cap_top"])
        w = (r["box"][2] - r["box"][0]) * height / face.height(ref)
        if w > 0.5:
            ratios.append((orig[2] - orig[0]) / w)
    return float(np.clip(np.median(ratios), 0.7, 1.4)) if ratios else 1.0


def rank(top, styles):
    originals = original_glyphs()
    report = []
    results = {}
    for style in styles:
        glyphs = {k: v for k, v in originals[style].items() if k.isalnum() or k in "!?"}
        metrics = {k: int(v) for k, v in style_metrics(originals[style], style).items()}
        small_caps = style == "dialogue"
        outline = style in ("info", "digits")
        ref = "0" if style == "digits" else "H"  # the glyph whose height the original's maps to
        entries = []
        for directory, licence, files in CANDIDATES[style]:
            for name in files:
                path = FONTS / name
                if not path.exists():
                    continue
                try:
                    face = Face(path)
                except Exception as e:  # noqa: BLE001
                    print("skip", name, e)
                    continue
                stretch = fit_stretch(face, glyphs, metrics, small_caps, ref)
                for slant in ((0.0, 0.12, 0.24) if style == "dialogue" else (0.0,)):
                    for bold in EMBOLDEN:
                        params = {"stretch": round(stretch, 3), "slant": slant, "embolden": bold,
                                  "outline": outline, "small_caps": small_caps, "ref": ref}
                        score, detail = evaluate(face, glyphs, metrics, params)
                        entries.append((score, face.name, directory, licence, params, detail, path))
        entries.sort(key=lambda e: e[0])
        best, families = [], set()
        for e in entries:  # the best variant of each family
            if e[2] in families:
                continue
            families.add(e[2])
            best.append(e)
        results[style] = best
        report.append(f"== {style} (original metrics {metrics}) ==")
        for i, (score, name, directory, licence, params, detail, _) in enumerate(best):
            report.append(f"{i + 1:2d}. {score:.4f}  {name:44s} {licence:6s} stretch {params['stretch']:.2f} "
                          f"slant {params['slant']:.2f} embolden {params['embolden']:+.2f}  iou {detail['iou']:.3f} "
                          f"chamfer {detail['chamfer']:.2f} weight x{detail['weight']:.2f}")
        report.append("")
        sheet(style, originals[style], metrics, best[:top])
    text = "\n".join(report)
    (WORK / "report.txt").write_text(text)
    serial = {s: [{"rank": i + 1, "score": e[0], "face": e[1], "family": e[2], "licence": e[3], "params": e[4],
                   "detail": e[5], "file": e[6].name} for i, e in enumerate(r)] for s, r in results.items()}
    (WORK / "ranking.json").write_text(json.dumps(serial, indent=1))
    print(text)


def sample_line(style, glyphs, text, render_glyph, factor):
    """A line laid out with the game's advances; render_glyph(glyph) gives
    its box's coverage at `factor`."""
    hbox = max(g.h for g in glyphs.values())
    x, pieces = 0, []
    for ch in text:
        g = glyphs.get(ch) or glyphs.get(ch.upper())
        if ch == " " or g is None:
            x += 6 if style in ("menu", "dialogue") else 5
            continue
        pieces.append((x, render_glyph(g)))
        x += g.advance
    canvas = np.zeros((hbox * factor, (x + 12) * factor), np.float32)
    for px, cov in pieces:
        h, w = cov.shape
        region = canvas[:h, px * factor:px * factor + w]
        np.maximum(region, cov[:, :region.shape[1]], out=region)
    return canvas


def sheet(style, glyphs, metrics, best, factor=4):
    rows = []
    for text in SAMPLES[style]:
        rows.append(("original", sample_line(style, glyphs, text,
                                             lambda g: np.kron(g.cov, np.ones((factor, factor))), factor)))
    for e in best:
        face = Face(e[6])
        for text in SAMPLES[style]:
            rows.append((f"{e[1]} {e[4]['embolden']:+.2f}",
                         sample_line(style, glyphs, text, lambda g: coverage(face, g, metrics, e[4], factor), factor)))
    width = max(r[1].shape[1] for r in rows) + 10
    height = sum(r[1].shape[0] + 6 for r in rows)
    canvas = Image.new("L", (width + 240, height), 24)
    draw = ImageDraw.Draw(canvas)
    y = 0
    for label, cov in rows:
        canvas.paste(Image.fromarray((np.clip(cov, 0, 1) * 255).astype(np.uint8)), (240, y))
        draw.text((4, y + 2), label[:38], fill=160)
        y += cov.shape[0] + 6
    canvas.save(WORK / f"sheet_{style}.png")


# ---- HUD digit proportions (ui_text.c DIGITS_ASPECT) ----

DIGITS_FONT = ROOT / "pc/third_party/fonts/Alegreya/Alegreya[wght].ttf"
DIGITS_PARAMS = {"stretch": 1.07, "slant": 0.0, "embolden": 0.02, "outline": True, "small_caps": False, "ref": "0"}
# The game's advances (vs_battle_renderValue: 5 + font): small 5, large 6.
DIGIT_SIZES = (("digits", "small 6x10", 5), ("digits_large", "large 7x12", 6))


def digits_report(aspects, factor=8):
    """Per digit size and `aspect`: how much of the font's own width/height
    the digits keep, their height against the original's, the shape score,
    and the narrowest gap between neighbours' ink at the game's advance."""
    originals = original_glyphs()
    face = Face(DIGITS_FONT)
    lines, rows = [], []
    for size, label, advance in DIGIT_SIZES:
        glyphs = originals[size]
        metrics = style_metrics(glyphs, size)
        orig_h = np.mean([ink_box(g.cov)[3] - ink_box(g.cov)[1] for g in glyphs.values()])
        lines.append(f"== {label}: advance {advance}, original ink height {orig_h:.1f} px")
        rows.append((f"{label} original", None, glyphs, advance))
        for aspect in aspects:
            params = dict(DIGITS_PARAMS, aspect=aspect)
            kept, heights = [], []
            covs = {}
            for ch, g in glyphs.items():
                cov = coverage(face, g, metrics, params, factor)
                covs[ch] = cov
                box = ink_box(cov, 0.5)
                f = face.glyph(ch)
                if box is None or f is None:
                    continue
                w, h = (box[2] - box[0]) / factor, (box[3] - box[1]) / factor
                natural = (f["box"][2] - f["box"][0]) / (f["box"][3] - f["box"][1])
                kept.append((w / h) / natural)
                heights.append(h)
            # neighbours' ink at the game's advance (the outline is 1 px)
            gap = 99.0
            for a, b in zip("0123456789", "1234567890"):
                ia, ib = covs[a] > 0.5, covs[b] > 0.5
                ca, cb = np.where(ia.any(0))[0], np.where(ib.any(0))[0]
                if len(ca) and len(cb):
                    gap = min(gap, (advance * factor + cb.min() - ca.max() - 1) / factor)
            score, detail = evaluate(face, glyphs, metrics, params)
            lines.append(f"aspect {aspect:4.2f}: proportions kept mean {np.mean(kept):.2f} min {min(kept):.2f}, "
                         f"ink height {np.mean(heights):.1f} px, score {score:.3f} (iou {detail['iou']:.2f}), "
                         f"narrowest gap between digits {gap:.2f} px")
            rows.append((f"{label} aspect {aspect:.2f}", covs, glyphs, advance))
    text = "\n".join(lines)
    print(text)
    (WORK / "digits.txt").write_text(text + "\n")
    # sheet: "1572530" in each variant at the game's advances
    z = 6
    tiles = []
    for name, covs, glyphs, advance in rows:
        h = max(g.h for g in glyphs.values())
        canvas = np.zeros((h * z, (advance * 7 + 3) * z), np.float32)
        for i, ch in enumerate("1572530"):
            g = glyphs[ch]
            if covs is None:
                cov = np.kron(g.cov, np.ones((z, z)))
            else:
                cov = downsample(covs[ch], factor // z) if factor % z == 0 else \
                    np.asarray(Image.fromarray((covs[ch] * 255).astype(np.uint8)).resize(
                        (g.w * z, g.h * z), Image.BILINEAR)) / 255.0
            region = canvas[:cov.shape[0], i * advance * z:i * advance * z + cov.shape[1]]
            np.maximum(region, cov[:, :region.shape[1]], out=region)
        tiles.append((name, canvas))
    width = max(c.shape[1] for _, c in tiles) + 230
    sheet_im = Image.new("L", (width, sum(c.shape[0] + 6 for _, c in tiles)), 24)
    draw = ImageDraw.Draw(sheet_im)
    y = 0
    for name, c in tiles:
        sheet_im.paste(Image.fromarray((np.clip(c, 0, 1) * 255).astype(np.uint8)), (230, y))
        draw.text((4, y + 2), name, fill=170)
        y += c.shape[0] + 6
    sheet_im.save(WORK / "digits.png")
    print("->", WORK / "digits.txt", WORK / "digits.png")


# ---- fixed labels (ui_text.c / ui_labels.h) ----

# Words the game draws as images, per 4-bit texture page (VRAM origin),
# with approximate boxes (inclusive texels); `labels` finds each word's
# exact ink box (touching words split on their shared outline column) and
# writes pc/src/ui_labels.h with a hash of the box, so the runtime only
# replaces a word whose texels are still the same.
LABEL_SPEC = {
    (768, 0): [  # the field UI page (HUD, menus, information box, equipment)
        ("HP", 144, 0, 159, 8), ("MP", 160, 0, 177, 8), ("RISK", 180, 0, 205, 8), ("WT", 208, 0, 226, 8),
        ("NO.", 242, 0, 254, 8),
        ("WAITING for COMMAND!", 144, 9, 226, 16),
        ("TIME", 104, 56, 123, 63), ("SAVE", 127, 56, 150, 63),
        ("COMMAND", 0, 64, 62, 73), ("MENU", 63, 64, 99, 73), ("NUM.", 100, 64, 130, 73),
        ("WARNING", 131, 64, 187, 73), ("HELP", 187, 64, 218, 73),
        ("INFO.", 0, 76, 31, 85),
        ("CLEAR", 223, 112, 250, 119), ("MAP", 223, 120, 241, 127),
        ("CAUTION", 64, 145, 97, 151), ("EXIT", 103, 145, 122, 151), ("FACTORY", 123, 145, 158, 151),
        ("Locked", 190, 144, 216, 151), ("Container", 217, 144, 254, 151),
        ("CHECK", 64, 152, 90, 159), ("INFORMATION", 95, 152, 146, 159), ("Open", 190, 152, 212, 159),
        ("is", 238, 152, 247, 159), ("Unlockable", 214, 160, 255, 167),
        ("WEAPON", 70, 161, 104, 167), ("HUMAN", 104, 161, 135, 167),
        ("SHIELD", 74, 169, 102, 175), ("BEAST", 102, 169, 135, 175),
        ("R.ARM", 77, 177, 102, 183), ("UNDEAD", 102, 177, 137, 183),
        ("L.ARM", 77, 185, 102, 191), ("PHANTOM", 102, 185, 141, 191),
        ("HEAD", 81, 193, 102, 199), ("DRAGON", 102, 193, 137, 199),
        ("BODY", 81, 201, 102, 207), ("EVIL", 102, 201, 124, 207),
        ("LEGS", 81, 209, 104, 215),
        ("PHYSICS", 142, 161, 177, 167), ("AIR", 142, 169, 159, 175), ("FIRE", 142, 177, 163, 183),
        ("EARTH", 142, 185, 171, 191), ("WATER", 142, 193, 171, 199), ("LIGHT", 142, 201, 168, 207),
        ("DARKNESS", 142, 209, 182, 215), ("WORKSHOP", 136, 217, 196, 223),
    ],
    (448, 256): [  # the battle page (battle mode, chain and defense, status changes, spell schools)
        ("BATTLE", 64, 0, 103, 10), ("MODE", 103, 0, 140, 10),
        ("MOVE", 212, 28, 246, 40),
        ("200%", 182, 43, 213, 51), ("50%", 214, 43, 240, 51),
        ("RISK", 150, 53, 178, 63), ("ATTACK", 179, 53, 218, 63), ("Parry", 219, 53, 252, 63),
        ("STR", 0, 64, 22, 76), ("INT", 23, 64, 42, 76), ("AGL", 43, 64, 66, 76),
        ("Quicken", 67, 64, 107, 76), ("Silent", 107, 64, 137, 76), ("Paralysis", 138, 64, 187, 76),
        ("Poison", 188, 64, 222, 76), ("-up", 223, 64, 242, 76),
        ("Numbness", 0, 77, 51, 88), ("Curse", 52, 77, 82, 88), ("Full", 82, 77, 101, 88),
        ("HP", 102, 77, 119, 88), ("Magic", 119, 77, 150, 88), ("Ward", 151, 77, 179, 88),
        ("Equip", 180, 77, 208, 88), ("-down", 209, 77, 246, 88),
        ("Air", 0, 89, 17, 100), ("Fire", 18, 89, 39, 100), ("Earth", 40, 89, 70, 100),
        ("Water", 71, 89, 103, 100), ("Resist", 103, 89, 136, 100), ("Regenerate", 143, 89, 205, 100),
        ("WARLOCK", 0, 101, 51, 110), ("SHAMAN", 52, 101, 99, 110), ("SORCERER", 99, 101, 158, 110),
        ("ENCHANTER", 159, 101, 218, 110), ("MP", 218, 101, 240, 110),
        ("ITEMS", 0, 112, 34, 122), ("BREAK", 35, 112, 72, 122), ("ARTS", 73, 112, 102, 122),
        ("CHAIN", 102, 112, 137, 122), ("DEFENSE", 150, 112, 198, 122),
        ("\u2022", 192, 128, 197, 133),  # the L2 shortcut cross's connector dot (ui_text.c LABEL_DOT: a disc)
    ],
}
OUTLINE_MAX = 2  # CLUT indices 1-2: the outline (a column of only these separates touching words)


def page4_at(vram, x, y):
    return page4(vram, x)[y:y + 256]


def label_boxes(vram):
    def snap(p, x, rows, inward):
        col = lambda c: p[rows, c] if 0 <= c < 256 else np.zeros(1, np.uint8)
        if not col(x).any():  # in a gap: the ink inward
            while not col(x).any():
                x += inward
            return x
        for d in range(4):  # touching: the nearest outline-only column
            for c in (x - d, x + d):
                if 0 <= c < 256 and col(c).any() and col(c).max() <= OUTLINE_MAX:
                    return c
        return x

    out = []
    for (px, py), spec in LABEL_SPEC.items():
        p = page4_at(vram, px, py)
        for text, u0, v0, u1, v1 in spec:
            rows = slice(v0, v1 + 1)
            a, b = snap(p, u0, rows, +1), snap(p, u1, rows, -1)
            ys = np.where(p[v0:v1 + 1, a:b + 1].any(1))[0]
            t, bt = v0 + int(ys.min()), v0 + int(ys.max())
            h = 2166136261
            for v in range(t, bt + 1):
                for u in range(a, b + 1):
                    h = ((h ^ int(p[v, u])) * 16777619) & 0xFFFFFFFF
            out.append((text, px, py, a, t, b, bt, h))
    return out


LABELS_H = ROOT / "pc/src/ui_labels.h"


def labels(write):
    vram = np.load(WORK / "vram.npy")
    entries = label_boxes(vram)
    def c_string(t):  # non-ASCII as UTF-8 escapes
        return "".join(ch if ord(ch) < 128 else "".join(f"\\x{b:02x}" for b in ch.encode()) for ch in t)

    rows = "\n".join(f'    {{ "{c_string(t)}", {px}, {py}, {a}, {v0}, {b}, {v1}, 0x{h:08x}u }},'
                     for t, px, py, a, v0, b, v1, h in entries)
    current = LABELS_H.read_text() if LABELS_H.exists() else ""
    if write:
        start, end = current.index("static const pc_ui_label labels[] = {\n"), current.index("\n};")
        head = current[:start + len("static const pc_ui_label labels[] = {\n")]
        LABELS_H.write_text(head + rows + current[end:])
        print(f"{len(entries)} labels -> {LABELS_H}")
    else:
        stale = [line for line in rows.splitlines() if line not in current]
        print(f"{len(entries)} labels; {len(stale)} differ from {LABELS_H.name}")
        for line in stale:
            print("  ", line.strip())
    # contact sheet of the boxes (game imagery: stays in pc/build)
    z, tiles = 5, []
    for t, px, py, a, v0, b, v1, h in entries:
        p = page4_at(vram, px, py)
        sub = p[max(v0 - 2, 0):v1 + 3, max(a - 3, 0):b + 4] * 17
        im = Image.fromarray(sub.astype(np.uint8)).convert("RGB").resize((sub.shape[1] * z, sub.shape[0] * z),
                                                                          Image.NEAREST)
        ox, oy = (a - max(a - 3, 0)) * z, (v0 - max(v0 - 2, 0)) * z
        ImageDraw.Draw(im).rectangle([ox, oy, ox + (b - a + 1) * z - 1, oy + (v1 - v0 + 1) * z - 1],
                                     outline=(0, 255, 0))
        tiles.append((t, im))
    sheet_im = Image.new("RGB", (720, sum(im.height + 16 for _, im in tiles)), (40, 0, 40))
    y = 0
    for t, im in tiles:
        ImageDraw.Draw(sheet_im).text((0, y), t, fill=(255, 255, 0))
        sheet_im.paste(im, (0, y + 12))
        y += im.height + 16
    sheet_im.save(WORK / "labels.png")
    print("->", WORK / "labels.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["capture", "fetch", "rank", "all", "digits", "labels"])
    parser.add_argument("--card", type=pathlib.Path, default=ROOT / "pc/save/card1.mcd")
    parser.add_argument("--top", type=int, default=6)
    parser.add_argument("--styles", default="dialogue,menu,info,digits")
    parser.add_argument("--aspects", default="0,0.7,0.78,0.85,1", help="digits: the aspect values compared")
    parser.add_argument("--write", action="store_true", help="labels: rewrite pc/src/ui_labels.h")
    args = parser.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    styles = args.styles.split(",")
    if args.command in ("capture", "all"):
        capture(args.card)
    if args.command in ("fetch", "all"):
        fetch(styles)
    if args.command in ("rank", "all"):
        rank(args.top, styles)
    if args.command == "digits":
        digits_report([float(a) for a in args.aspects.split(",")])
    if args.command == "labels":
        labels(args.write)


if __name__ == "__main__":
    main()
