#!/usr/bin/env python3
"""The loading screen's "NOW LOADING" as real text with a glow (gitea #23).

    python3 pc/tests/gim_text_loading_smoke.py [--out DIR] [--card pc/build/test-card1.mcd]

The picture is in the executable (SKIP without data/SLUS_010.40). Loading a save from the title shows it; the pack NOWLOADING is made here from the
executable's own picture (pc/tools/gim_text_pack.py: the words, their colour and the halo measured), and the loading screen is captured at 4:3 and 16:9:
  original text, with the pack          the picture as the game draws it: the same frame as with no pack (byte for byte)
  modern text, no pack                  = the same
  modern text, the pack                 the words as text with a halo: the centre of the screen differs, the halo's pixels (dim, around the words) exist,
                                        the words sit where the picture's did (4:3: the ink's box), and nothing else on the screen changed
  modern text, gim_text_glow 0          the halo gone (fewer dim pixels), the words as they were
  modern text, the pack's font= changed the words are set in that font (pixels differ)
  modern text, a fill with another glow colour (gim_text_fill, live) tints the halo."""
import argparse
import pathlib
import re
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402


def loading_frame(work, text, pack, wide, card, extra="", live=None):
    """The presented frame of the loading screen (Load from the title's Continue: the card's first save) as an array."""
    work.mkdir(parents=True, exist_ok=True)
    (work / "prefabs").mkdir(exist_ok=True)
    (work / "settings.cfg").write_text(f"ui_menus 0\nui_text {text}\ninterpolate 0\nwidescreen {1 if wide else 0}\naspect 0\n{extra}")
    env = {"ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_WINDOW": "1280x720" if wide else "1280x960", "ROOD_AUDIO": "none", "ROOD_OUT": str(work),
           "ROOD_LIGHTS": str(work / "lights"), "ROOD_PREFABS": str(work / "prefabs"), "ROOD_GIM_TEXT": str(pack), "ROOD_SCALE": "1"}
    if wide:
        env["ROOD_WIDESCREEN"] = "1"
    g = launch_at("title", card=card, fast=True, log=work / "game.log", extra_env=env)
    try:
        if live:
            g.call("graphics", **live)
        g.press("start", vsyncs=4, settle=90)
        for _ in range(3):
            g.press("circle", vsyncs=3, settle=90)
        path = work / "loading.ppm"
        for _ in range(400):
            g.run(1)
            g.wait_presents(1)
            path.unlink(missing_ok=True)
            g.call("present_dump", path=str(path))
            for _ in range(50):
                if path.exists() and path.stat().st_size > 0:
                    break
                g.run(1)
                g.wait_presents(1)
            a = np.asarray(Image.open(path).convert("RGB"))
            h, w = a.shape[:2]
            m = a.max(axis=2)
            ys, xs = np.nonzero(m > 120)
            if len(ys) > 50 and (m < 20).mean() > 0.9 and h * 0.4 < ys.min() and ys.max() < h * 0.6 and xs.min() > w * 0.2 and xs.max() < w * 0.8:
                return a.copy()
        raise RuntimeError("the loading screen never showed")
    finally:
        g.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(OUT / "gim_text_loading"))
    ap.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    args = ap.parse_args()
    if not (ROOT / "data/SLUS_010.40").exists():
        print("SKIP: no data/SLUS_010.40")
        return 0
    out = pathlib.Path(args.out)
    failures = 0

    def check(ok, message):
        nonlocal failures
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        failures += not ok

    pack, empty = out / "pack", out / "empty"
    empty.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(ROOT / "pc/tools/gim_text_pack.py"), "--ocr", "off", "--out", str(pack), "NOWLOADING"], check=True, capture_output=True)
    text = (pack / "NOWLOADING.txt").read_text()
    check("glow=" in text and "line " in text, "the pack tool measured the picture's ink and halo from the executable")
    check("font=pc/third_party/fonts/" in text and "font_stretch=" in text and "font_embolden=" in text, "the pack names the font that sets the words, with the stretch and weight measured for it")
    other = out / "pack_font"
    other.mkdir(parents=True, exist_ok=True)
    (other / "NOWLOADING.txt").write_text(re.sub(r"font=\S+", "font=pc/third_party/fonts/Alegreya/Alegreya[wght].ttf", text))
    for wide in (False, True):
        tag = "16:9" if wide else "4:3"
        base = out / tag.replace(":", "x")
        a = loading_frame(base / "a", "original", pack, wide, args.card)
        b = loading_frame(base / "b", "original", empty, wide, args.card)
        c = loading_frame(base / "c", "font", empty, wide, args.card)
        d = loading_frame(base / "d", "font", pack, wide, args.card)
        e = loading_frame(base / "e", "font", pack, wide, args.card, extra="gim_text_glow 0\n")
        g = loading_frame(base / "g", "font", other, wide, args.card)
        check(int((d != g).any(axis=2).sum()) > 1500, f"{tag}: the pack's font= sets the words (another file there changes {int((d != g).any(axis=2).sum())} pixels)")
        check((a == b).all(), f"{tag}: original text with a pack = with none, byte for byte")
        check((a == c).all(), f"{tag}: modern text with no pack = the picture")
        diff = (a != d).any(axis=2)
        h, w = a.shape[:2]
        ys, xs = np.nonzero(diff)
        check(len(ys) > 2000, f"{tag}: the pack replaces the picture ({len(ys)} pixels differ)")
        if len(ys):
            check(h * 0.4 < ys.min() and ys.max() < h * 0.6 and abs((xs.min() + xs.max()) / 2 - w / 2) < 0.03 * w,
                  f"{tag}: the words sit at the picture's place, centred (rows {ys.min()}..{ys.max()}, columns {xs.min()}..{xs.max()} of {w} x {h})")
        dim = lambda f: int(((f.max(axis=2) > 12) & (f.max(axis=2) < 110)).sum())  # noqa: E731 (the halo: dim, not black, not the words)
        check(dim(d) > dim(e) + 1500, f"{tag}: the glow is a halo of dim pixels around the words ({dim(d)} with, {dim(e)} without)")
        blue = d[..., 2].astype(int) - d[..., 0].astype(int)
        halo = (d.max(axis=2) > 12) & (d.max(axis=2) < 110)
        check(float(blue[halo].mean()) > 20, f"{tag}: the halo is the measured blue (blue over red by {float(blue[halo].mean()):.0f})")
        check(int((d.max(axis=2) > 140).sum()) > 500, f"{tag}: the words themselves are bright ({int((d.max(axis=2) > 140).sum())} pixels)")
        f = loading_frame(base / "f", "font", pack, wide, args.card, live={"gim_text_fill": "NOWLOADING=flat glow=ff3000 glow_radius=5 glow_strength=1 | bdded5", "gim_text_fill_force": True})
        redder = f[..., 0].astype(int) - f[..., 2].astype(int)
        check(float(redder[(f.max(axis=2) > 12) & (f.max(axis=2) < 110)].mean()) > 10, f"{tag}: a fill with another glow colour tints the halo (live, no restart)")
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
