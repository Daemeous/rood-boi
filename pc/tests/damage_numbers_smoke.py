#!/usr/bin/env python3
"""Battle number popups (damage, heal) drawn by ui_text (pc/src/ui_text.c;
README "2D / UI").

    python3 pc/tests/damage_numbers_smoke.py [--card pc/build/test-card1.mcd] [--shots DIR]

The numbers that pop up over Ashley and enemies (BATTLE.PRG
func_80094B0C) draw the HUD's large digits as 8 x 12 quads, one per digit.
From the card's save room (a copy), the game's own status ticks
(func_80087EF4) make them: poison takes HP (a white damage number, then the
poison is cleared), regen gives it back (a green heal number). Each is held
once it has stopped bouncing (the label's frame counter at 16 or more) and:

- ui_text original: no popup digit asks the font (RPC ui_text popup_glyphs
  0, popups_drawn empty), the original path;
- ui_text font: the popup's digits are drawn from the font (popups_drawn has
  each digit of the number, popup_glyphs counts them); the digits the game
  drew this frame (RPC ui_text popups: digit and packet position) spell the
  value in the game's label (its amount and its text) and sit where they did
  with ui_text original (only the glyphs change); the same recorded frame
  drawn original and font (RPC ui_upscale redraw) differs only inside the
  glyph boxes the 2D path drew (RPC ui_upscale mask), and around each popup
  digit; each digit on the picture reads as itself: the font's digit (RPC
  ui_text render, in the popup's colour) matches the picture there better
  than any other digit does (legible);
- a stat change (STR -12: func_80094E18; the damage label's copy as kind 3)
  draws its '-' from the font too;
- UI text scale (RPC ui_text scale; no setting yet): at 2 the digits are
  drawn twice as large about the number's middle, at 1 where the game puts
  them.

--shots keeps a before/after pair of each popup (game imagery: keep it out of
git)."""
import argparse
import os
import pathlib
import shutil
import struct
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from checkpoints import launch_at  # noqa: E402
from rood_rpc import OUT, symbols  # noqa: E402

PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (characterState, the label pool)
HP = 0x18                     # vs_battle_actor2 currentHP
STATUSES, TIMERS = 0x948, 0x94C  # vs_battle_actor2 statuses, statusTimers[8]
POISON, REGEN = 0x4000, 0x20000  # statuses ticking with statusTimers[5]
LABEL = 24                    # D_800F1BAC_t
COLOURS = ((128, 128, 128), (0, 128, 0), (128, 0, 128), (0, 128, 128))  # func_80094B0C
DIGIT_PAGE, DIGIT_CLUT = 12, 223 << 6 | 832 >> 4

failures = []


def check(ok, what):
    print(("PASS: " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


def default_card():
    return pathlib.Path(os.environ.get("ROOD_TEST_CARD") or ROOT / "pc/build/test-card1.mcd")


class Session:
    def __init__(self, out, card, env):
        self.out = out
        self.g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
        self.sym = symbols()
        actor = self.ptr(self.sym["vs_battle_characterState"])
        self.actor2 = int.from_bytes(self.g.read(actor + 0x3C, 4), "little")
        self.pool = self.ptr(self.sym["D_800F1BAC"])

    def ptr(self, address):
        return int.from_bytes(self.g.read(address, PTR), "little")

    def count(self):
        return struct.unpack("<h", self.g.read(self.sym["D_800F227E"], 2))[0]

    def label(self, i=0):
        raw = self.g.read(self.pool + LABEL * i, LABEL)
        kind, frame, amount, style, length = raw[8], raw[11], struct.unpack("<H", raw[12:14])[0], raw[14], raw[15]
        return {"raw": raw, "kind": kind, "frame": frame, "amount": amount, "style": style,
                "wait": struct.unpack("<h", raw[6:8])[0], "text": raw[20:20 + min(length, 4)].decode(errors="replace")}

    def hp(self):
        return struct.unpack("<h", self.g.read(self.actor2 + HP, 2))[0]

    def status_tick(self, bit, what):
        """A status ticking at once (statusTimers[5] 2): its popup, the status
        cleared again. Returns the label (index 0) or None."""
        g = self.g
        statuses = struct.unpack("<I", g.read(self.actor2 + STATUSES, 4))[0]
        g.write(self.actor2 + STATUSES, struct.pack("<I", statuses | bit))
        g.write(self.actor2 + TIMERS + 5, bytes([2]))
        hp = self.hp()
        found = None
        for _ in range(400):
            g.run(1)
            if self.count() and self.label()["kind"] == 1:
                found = self.label()
                break
        g.write(self.actor2 + STATUSES, struct.pack("<I", statuses & ~bit))
        check(found is not None, f"{what}: the game's status tick made a number popup (HP {hp} -> {self.hp()})")
        return found

    def settle(self, what):
        """Until the popup has stopped bouncing (frame >= 16; it goes at 31)."""
        for _ in range(60):
            lab = self.label()
            if lab["kind"] != 1:
                break
            if lab["wait"] < 0 and lab["frame"] >= 16:
                return lab
            self.g.run(1)
        check(False, f"{what}: the popup settles ({self.label()})")
        return None


def redraw(g, work, mode, name):
    g.call("graphics", ui_text=mode)
    path = work / f"{name}-{mode}.ppm"
    info = g.call("ui_upscale", redraw=str(path))
    image = np.asarray(Image.open(path).convert("RGB")).astype(int)
    path.unlink()
    return image, info


def templates(g, work, scale, colour):
    """Each digit's font glyph as the popup draws it: PS1 colours (RPC ui_text
    render of its 8 x 12 region) modulated by the popup's colour; alpha."""
    out = {}
    for d in range(10):
        path = work / f"digit{d}.bin"
        u0 = 64 + 8 * d
        info = g.call("ui_text", render=[DIGIT_PAGE, DIGIT_CLUT, u0, 0, u0 + 7, 11], factor=scale, path=str(path))
        r = info.get("render", {})
        if not r.get("ok"):
            return None
        raw = np.frombuffer(path.read_bytes(), "<u2").reshape(r["h"], r["w"]).astype(int)
        path.unlink()
        rgb = np.stack([(raw >> s & 31) * 255 // 31 for s in (0, 5, 10)], axis=-1)
        rgb = np.minimum(rgb * np.array(colour) // 128, 255)
        out[str(d)] = (rgb, (raw & 0x7FFF) != 0)
    return out


def read_digits(image, info, popups, tmpl, scale):
    """Each popup digit's picture matched to every digit's template (mean
    difference over the template's opaque texels): the best match and its
    margin over the next best."""
    ax, ay = info["area"][0], info["area"][1]
    read, margins = "", []
    for _, x, y in popups:
        x0, y0 = (ax + x) * scale, (ay + y) * scale
        crop = image[y0:y0 + 12 * scale, x0:x0 + 8 * scale]
        scores = {}
        for d, (rgb, alpha) in tmpl.items():
            if crop.shape[:2] != alpha.shape:
                return None, []
            scores[d] = float(np.abs(crop - rgb)[alpha].mean())
        ranked = sorted(scores, key=scores.get)
        read += ranked[0]
        margins.append(scores[ranked[1]] / max(scores[ranked[0]], 1e-6))
    return read, margins


def examine(s, work, what, lab, shots):
    """The settled popup (paused): original and font, the same frame."""
    g = s.g
    value = lab["amount"] & 0x7FFF
    colour = COLOURS[((lab["style"] & 1) << 1) + (0 if lab["amount"] & 0x8000 else 1)]
    check(lab["text"] == str(value), f"{what}: the label's text {lab['text']!r} is its amount {value}")
    # ui_text original: the game's sprites, nothing asks the font.
    g.call("graphics", ui_text="original")
    g.call("ui_text", forget_signs=True)
    g.run(4)  # (presented a frame later than drawn)
    t = g.call("ui_text")
    original_popups = sorted(t["popups"], key=lambda p: p[1])
    check(t["popup_glyphs"] == 0 and t["popups_drawn"] == "",
          f"{what}: ui_text original: no popup digit from the font (popup_glyphs {t['popup_glyphs']}, "
          f"popups_drawn {t['popups_drawn']!r})")
    if shots:
        g.present_screenshot(shots / f"{what}-before.png")
    # ui_text font.
    g.call("graphics", ui_text="font")
    g.call("ui_text", forget_signs=True)
    g.call("ui_upscale", mask=str(work / "mask.pgm"))  # clear
    g.run(4)
    g.call("ui_upscale", mask=str(work / "mask.pgm"))
    mask = np.asarray(Image.open(work / "mask.pgm")) > 0
    t = g.call("ui_text")
    popups = sorted(t["popups"], key=lambda p: p[1])
    digits = "".join(p[0] for p in popups)
    check(digits == str(value), f"{what}: the digits drawn spell the game's value ({digits!r}, {value})")
    check(all(c in t["popups_drawn"] for c in str(value)) and t["popup_glyphs"] >= len(str(value)),
          f"{what}: ui_text font: the popup's digits are drawn from the font (popups_drawn "
          f"{t['popups_drawn']!r}, popup_glyphs {t['popup_glyphs']})")
    check([p[1:] for p in popups] == [p[1:] for p in original_popups],
          f"{what}: the digits sit where the game put them with ui_text original ({[p[1:] for p in popups]})")
    if shots:
        g.present_screenshot(shots / f"{what}-after.png")
    # The same frame both ways: changes only inside the glyph boxes.
    original, info = redraw(g, work, "original", what)
    font, _ = redraw(g, work, "font", what)
    scale = original.shape[1] // 1024
    from scipy import ndimage
    grown = ndimage.binary_dilation(mask, iterations=1)
    big = np.repeat(np.repeat(grown, scale, axis=0), scale, axis=1)[:original.shape[0], :original.shape[1]]
    diff = np.any(original != font, axis=2)
    outside = int((diff & ~big).sum())
    ax, ay = info["area"][0], info["area"][1]
    around = np.zeros_like(diff)
    for _, x, y in popups:
        around[(ay + y - 1) * scale:(ay + y + 13) * scale, (ax + x - 1) * scale:(ax + x + 9) * scale] = True
    check(outside == 0, f"{what}: the font changes nothing outside the glyph boxes ({outside} pixels)")
    check(int((diff & around).sum()) > 0, f"{what}: the font changes the popup's digits "
                                          f"({int((diff & around).sum())} pixels)")
    # Legible: each digit on the picture is best matched by its own font glyph.
    tmpl = templates(g, work, scale, colour)
    check(tmpl is not None, f"{what}: the font draws each digit alone (RPC ui_text render)")
    if tmpl:
        read, margins = read_digits(font, info, popups, tmpl, scale)
        check(read == str(value) and margins and min(margins) > 1.3,
              f"{what}: the picture reads {read!r} (game {value}); best match vs next {[round(m, 2) for m in margins]}")
    g.call("graphics", ui_text="original")


def scaled(s, work, what):
    """UI text scale 2: twice as large about the number's middle. Each scale's
    frame drawn font and original (the scale places the quads as the game
    draws them: the recorded frame keeps their places): the popup's glyphs
    are where the two differ."""
    g = s.g
    g.call("graphics", ui_text="font")
    g.run(2)
    base = sorted(g.call("ui_text")["popups"], key=lambda p: p[1])
    if not check(bool(base), f"{what}: scale: popup digits drawn"):
        return
    x0, x1 = min(p[1] for p in base), max(p[1] for p in base) + 8
    y0, y1 = min(p[2] for p in base), max(p[2] for p in base) + 12
    cx = (x0 + x1) / 2
    extents = {}
    for k in (1, 2):
        g.call("ui_text", scale=k)
        g.run(2)
        font, info = redraw(g, work, "font", f"scale{k}")
        original, _ = redraw(g, work, "original", f"scale{k}")
        g.call("graphics", ui_text="font")
        scale = font.shape[1] // 1024
        ax, ay = info["area"][0], info["area"][1]
        pad = 2 + (k - 1) * 10  # PS1 pixels around the number's quads at scale 1
        r0, r1 = int((ay + y0 - pad) * scale), int((ay + y1 + pad) * scale)
        c0, c1 = int((ax + x0 - pad) * scale), int((ax + x1 + pad) * scale)
        d = np.any(font[r0:r1, c0:c1] != original[r0:r1, c0:c1], axis=2)
        ys, xs = np.nonzero(d)
        extents[k] = ((xs.min() + c0) / scale - ax, (xs.max() + 1 + c0) / scale - ax) if len(xs) else None
    g.call("ui_text", scale=1)
    g.call("graphics", ui_text="original")
    e1, e2 = extents[1], extents[2]
    if check(e1 is not None and e2 is not None, f"{what}: scale: the glyphs found (1: {e1}, 2: {e2})"):
        w1, w2 = e1[1] - e1[0], e2[1] - e2[0]
        m1, m2 = (e1[0] + e1[1]) / 2, (e2[0] + e2[1]) / 2
        check(1.6 * w1 < w2 < 2.4 * w1 and abs(m1 - cx) <= 1.5 and abs(m2 - cx) <= 1.5,
              f"{what}: scale 2 draws the number about twice as wide ({w1:.1f} -> {w2:.1f} PS1 pixels) about its "
              f"middle ({m1:.1f}, {m2:.1f}; the quads' {cx:.1f})")


def stat_sign(s, work, lab):
    """A stat change's sign: the damage label copied as kind 3 (STR, minus)."""
    g = s.g
    raw = bytearray(lab["raw"])
    raw[11] = 0              # frame
    raw[14] = 3              # kind 3: the first stat word (D_800E861C)
    raw[12:14] = struct.pack("<H", 12 | 0x8000)
    raw[15], raw[20:24] = 2, b"12\0\0"
    g.call("graphics", ui_text="font")
    g.call("ui_text", forget_signs=True)
    g.write(s.pool, bytes(raw))
    g.write(s.sym["D_800F227E"], struct.pack("<h", 1))
    g.run(6)  # (drawn in the next frame's ordering table)
    t = g.call("ui_text")
    check("-" in t["popups_drawn"] and "1" in t["popups_drawn"] and "2" in t["popups_drawn"],
          f"stat change: its '-' and digits are drawn from the font (popups_drawn {t['popups_drawn']!r}, "
          f"popups {t['popups']})")
    g.call("graphics", ui_text="original")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--shots", help="keep before/after images here (game imagery: keep it out of git)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (OUT / "damage_numbers").resolve()
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots).resolve() if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    env = {"ROOD_WINDOW": "960x720", "ROOD_GI": "0"}
    if not os.environ.get("ROOD_OUT"):  # Continue reads the card only with ROOD_OUT set (run.py sets it)
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    s = Session(out, card, env)
    try:
        g = s.g
        state = g.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1
        check(g.call("ui_text")["ui_text"] == "original", "default: ui_text original")
        lab = s.status_tick(POISON, "damage")
        if lab:
            check(lab["amount"] & 0x8000 and (lab["style"] & 1) == 0, f"damage: a white damage number ({lab})")
            lab = s.settle("damage")
        if lab:
            examine(s, out, "damage", lab, shots)
            scaled(s, out, "damage")
            stat_sign(s, out, lab)
        for _ in range(80):  # the stat change goes
            if not s.count():
                break
            g.run(2)
        g.write(s.actor2 + HP, struct.pack("<h", 40))
        lab = s.status_tick(REGEN, "heal")
        if lab:
            check(not lab["amount"] & 0x8000, f"heal: a green heal number ({lab})")
            lab = s.settle("heal")
        if lab:
            examine(s, out, "heal", lab, shots)
        log = (out / "game.log").read_text(errors="replace")
        check("[UNIMPLEMENTED]" not in log and "[crash]" not in log and g.status()["vsync"] > 0,
              "the game is still running")
    finally:
        s.g.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
