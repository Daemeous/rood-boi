#!/usr/bin/env python3
"""16:9: 2D is drawn where the projection made it, or compressed as screen
furniture, never a mix of both.

    python3 pc/tests/narrowed_2d_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only menus|popups|fill]

In widescreen the GTE narrows projected X by 3/4 and the hardware path
compresses 2D about the screen's centre by the same factor; 2D whose
vertices all came from the GTE this frame is taken for projected
("narrowed") and drawn where it is (boot_runtime.c narrowed_2d, gpu_hw.c).
The projection log matches any pixel the GTE projected to, so:

- menus (classic look, ui_menus off): with the menu's Ashley on screen, a
  menu's lines and bars landed on her vertices often enough that pieces of
  them were drawn uncompressed: the stat bars' rows came apart (stair-stepped
  ends, stray blocks: a zero-length fill line of a -1 affinity drawn 14
  pixels left of the bar's marker), and slivers of the header bands showed
  left of the 4:3 screen. Screen furniture links in the ordering table's
  slots in front of the scene's, which now never count as projected.
  The loot screen's prompt bars (GET / ALL, END: twelve lines each) are the
  same: a line taken for projected ran on from its bar's end far to the
  right (the user's 16:9 report); the chest's loot screen in the save room,
  40 vsyncs: none narrowed (the field's minimap, projected, is hidden there).
  Items > Equip > Weapon (the weapon list with the class / affinity page)
  and Status > Weapon, each over 20 vsyncs and at rest:
    - RPC render_stats narrowed_triangles 0 while Ashley's model is drawn
      (object_lists > 0);
    - each stat row's band (11 lines, the game's _renderStatPage): every line
      starts at the same column, and nothing is drawn left of the 4:3 screen
      on those rows;
    - the headers EQUIP/WEAPON and CLASS/AFFINITY/TYPE (ui_text font, the
      line layout: RPC ui_text lines): the ink gaps between neighbouring
      letters are even (the largest at most GAP_SPREAD PS1 pixels over the
      median; the glyph layout's I sat in a 6-pixel slot: "EQU IP").
- battle number popups (BATTLE func_80094B0C: 8 x 12 digit quads bouncing
  as they land, linked in front of the scene): over an enemy, a digit whose
  four corners landed on the model's projected vertices was drawn
  uncompressed for that frame, about 180 pixels to the side at 1080p (the
  numbers "glitched out" as they landed; 4 of 443 digit draws in eight
  poison ticks on the zone 9 room 18 enemy here, more over a body). Each
  frame of POPUPS poison ticks on an enemy (ui_text font): every digit the
  game drew (RPC ui_text popups) has ink inside its quad's compressed box,
  and none of the digits' ink lies elsewhere on their rows. (The field's
  minimap is projected 2D: render_stats does not tell here.)
- the area-targeting floor fill (Magick > Warlock > Explosion's target, zone
  9 room 18 with its enemies): the game fills the reachable floor around the
  target with one semi-transparent line per scanline from the narrowed
  projections (pc/src/map_view.c); only the lines whose ends happened to land
  on a projected point stayed narrowed, the rest were compressed: the fill
  was squeezed and its narrowed rows stuck out as bands to the view's sides.
  Its packets are now marked projected (PC_ANCHOR_PROJECTED), and with
  ui_lines joined, upscaled, each row is drawn joined to the rows above and
  below (gpu_hw.c pc_hw_span_shape) instead of a rectangle a native row
  tall. On the presented picture (green-dominant pixels; the fill is the
  largest blob once the sphere's thin lines are opened away):
    - no bands: no horizontal bar (a run of at least 1/12 of the width with
      the same ends on the next row) outside the fill;
    - smooth edges: where the fill's left or right edge slopes (1.5 to 12
      pixels a row), its end moves on almost every row (at most FLAT_MAX of
      those rows keep the previous row's end; stair steps a native row tall
      keep it on about 2 of 3).

--out keeps the screenshots (game imagery: keep it out of git)."""
import argparse
import json
import os
import pathlib
import statistics
import struct
import sys
import tempfile

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402
import ui_equip_smoke  # noqa: E402
import ui_magick_smoke  # noqa: E402

WINDOW = (1280, 720)
STAT_ROWS = 6       # HUMAN .. EVIL (class page)
BAND_LINES = 11     # _renderStatPage: the label band's lines, y 64 + 16 * row + j
GAP_SPREAD = 1.0    # PS1 pixels the widest letter gap of a header may exceed its median
FLAT_MAX = 0.4      # share of sloped edge rows whose end does not move
POPUPS = 4          # poison ticks on the enemy
HEADERS = ("EQUIP/WEAPON", "CLASS/AFFINITY/TYPE")

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)
    return ok


def at_rest(game, path, limit=40):
    """A presented image the same as the one before it."""
    previous = None
    for _ in range(limit):
        image = np.asarray(Image.open(game.present_screenshot(path)).convert("RGB")).astype(int)
        if previous is not None and np.array_equal(image, previous):
            return image
        previous = image
        game.run(4)
    return previous


def look(game):
    game.call("graphics", interpolate=False, aa="off", ui_upscale="mmpx", ui_text="font", ui_text_layout="line",
              ui_lines="joined", widescreen=True)
    game.run(4)


# ---- menus ----

def narrowed_frames(game, what, frames=20, models=True):
    counts, lists = [], []
    for _ in range(frames):
        game.run(1)
        st = game.call("render_stats")
        counts.append(st["narrowed_triangles"])
        lists.append(st["object_lists"])
    check((min(lists) > 0 or not models) and max(counts) == 0,
          f"{what}: no 2D drawn as projected while models are drawn (narrowed triangles per frame "
          f"{sorted(set(counts))}, model lists {sorted(set(lists))})")


def band_rows(image, display, what):
    """Each stat row's label band starts at one column; nothing left of the 4:3 screen on its rows.
    display: the display area (RPC ui_upscale display: x, y, w, h), which the view shows."""
    h, w = image.shape[:2]
    s = h / display[3]
    edge = (w - h * 4 / 3) / 2  # the 4:3 screen's left edge in the view
    for row in range(STAT_ROWS):
        starts, beside = [], 0
        for j in range(BAND_LINES):
            y = int((64 + 16 * row + j + 0.5 - display[1]) * s)
            line = image[y]
            r, g, b = line[:, 0], line[:, 1], line[:, 2]
            blue = np.where((b >= 40) & (b > r + 20) & (b > g))[0]
            starts.append(int(blue[0]) if len(blue) else -1)
            beside += int((line[: int(edge) - 1].sum(axis=1) > 40).sum())
        ok = min(starts) >= 0 and max(starts) - min(starts) <= 1 and min(starts) >= edge - 1 and beside == 0
        check(ok, f"{what}: stat row {row}: its band's {BAND_LINES} lines start at one column "
                  f"(at {sorted(set(starts))}, the 4:3 edge {edge:.0f}), nothing beside the screen ({beside} pixels)")


def header_gaps(game, path):
    """Neighbouring letters' ink gaps (PS1 pixels) in the headers, from the laid-out lines."""
    game.call("ui_text", lines=str(path))
    found = {}
    for line in json.loads(pathlib.Path(path).read_text())["lines"]:
        glyphs, k = line["glyphs"], line["k"]
        text = "".join(chr(g["c"]) for g in glyphs)
        if text not in HEADERS:
            continue
        gaps = []
        for a, b in zip(glyphs, glyphs[1:]):
            if not (chr(a["c"]).isalnum() and chr(b["c"]).isalnum()) or a["seg"] != b["seg"]:
                continue
            right = max(r[2] for r in a["rows"])
            left = min(r[1] for r in b["rows"])
            gaps.append((chr(a["c"]) + chr(b["c"]), (left - right) / k))
        found[text] = gaps
    for text in HEADERS:
        gaps = found.get(text)
        if not check(bool(gaps), f"header {text} laid out (RPC ui_text lines)"):
            continue
        median = statistics.median(g for _, g in gaps)
        pair, widest = max(gaps, key=lambda t: t[1])
        check(widest - median <= GAP_SPREAD,
              f"header {text}: letter gaps even (median {median:.2f}, widest {pair} {widest:.2f}: "
              f"at most {GAP_SPREAD} over)")


def menus(card, out):
    env = {"ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_WIDESCREEN": "1", "ROOD_GI": "0",
           "ROOD_LIGHTS": tempfile.mkdtemp(prefix="lights-", dir=out)}
    s = ui_equip_smoke.Session("menus", out, card, env, False)
    g = s.game
    try:
        look(g)
        g.call("ui_menu", modern=False)
        error = s.original_open_equip()
        if not check(error is None, f"Items > Equip ({error})"):
            return
        g.press("circle", vsyncs=3, settle=10)  # the weapon slot: its list with the class page
        check(s.wait_ready(want=20), "Items > Equip > Weapon: the list")
        g.run(30)
        narrowed_frames(g, "Equip > Weapon")
        band_rows(at_rest(g, out / "equip-weapon.png"), g.call("ui_upscale")["display"], "Equip > Weapon")
        header_gaps(g, out / "equip-weapon-lines.json")
        s.original_close()
        g.run(30)
        # Status > Weapon: the status page, its equipment list, the weapon's page.
        g.press("triangle", vsyncs=3, settle=10)
        s.wait_ready(want=2)
        s.nav(range(2, 10), 4)
        g.press("circle", vsyncs=3, settle=10)
        s.wait_ready()
        g.run(60)
        g.press("circle", vsyncs=3, settle=10)
        if not check(s.wait_ready(want=17), "Status: the equipment list"):
            return
        for _ in range(10):
            if s.selected(range(10, 18)) == 10:
                break
            g.press("up", vsyncs=3, settle=10)
            s.wait_ready()
        g.press("circle", vsyncs=3, settle=10)
        s.wait_ready()
        g.run(30)
        narrowed_frames(g, "Status > Weapon")
        band_rows(at_rest(g, out / "status-weapon.png"), g.call("ui_upscale")["display"], "Status > Weapon")
        s.original_close()
        g.run(30)
        # The loot screen (the save room's chest): its prompt bars (GET / ALL, END) are 12 lines
        # each; one landing on a projected vertex ran on past its bar towards the view's right side.
        opened = g.call("chest", open=0).get("opened")
        g.run(400)
        if check(bool(opened) and s.current_menu() != ui_equip_smoke.CLOSED, "the chest's loot screen"):
            narrowed_frames(g, "Loot", frames=40, models=False)
            at_rest(g, out / "loot.png")
    finally:
        s.close()


# ---- the area-targeting floor fill ----

def fill_metrics(image):
    h, w = image.shape[:2]
    r, g, b = image[..., 0], image[..., 1], image[..., 2]
    green = (g > r + 30) & (g > b + 30)
    k = int(round(h / 240.0 * 2)) + 1
    body = ndimage.binary_opening(green, structure=np.ones((k, k)))
    labels, n = ndimage.label(body)
    if n == 0:
        return None
    sizes = ndimage.sum(body, labels, range(1, n + 1))
    body = labels == 1 + int(np.argmax(sizes))
    outside = green & ~ndimage.binary_dilation(body, structure=np.ones((k, k)))

    def runs(row):
        d = np.diff(np.concatenate([[0], row.astype(int), [0]]))
        return list(zip(np.where(d == 1)[0], np.where(d == -1)[0]))

    rows = np.where(body.any(axis=1))[0]
    bands = []
    for y in range(max(rows[0], 1), min(rows[-1], h - 2) + 1):
        for x0, x1 in runs(outside[y]):
            if x1 - x0 >= w // 12 and any(abs(a - x0) <= 1 and abs(b - x1) <= 1
                                          for yy in (y - 1, y + 1) for a, b in runs(outside[yy])):
                bands.append((int(y), int(x0), int(x1)))
    flat = sloped = 0
    for side in (0, -1):
        ends = np.array([np.where(body[y])[0][side] for y in rows], float)
        for i in range(6, len(ends) - 7):
            slope = abs(ends[i + 6] - ends[i - 6]) / 12.0
            if 1.5 <= slope <= 12:
                sloped += 1
                flat += abs(ends[i + 1] - ends[i]) < 0.5
    return {"area": int(body.sum()), "bands": bands, "flat": flat / max(sloped, 1), "sloped": sloped}


# ---- battle number popups ----

def popup_ink(image, digits):
    """None, or what is wrong: a digit without ink in its quad's compressed box, or
    the digits' ink on their rows outside the boxes. Packet X lies in the view's
    4:3 middle (compressed 2D), packet Y on the 224 displayed rows."""
    h, w = image.shape[:2]
    sx, sy, left = h * 4 / 3 / 320, h / 224, (w - h * 4 / 3) / 2
    white = image.min(axis=2) > 200
    boxes = []
    for d, x, y in digits:
        x0, x1 = int(left + (x - 1) * sx), int(left + (x + 9) * sx) + 1
        y0, y1 = int((y - 2) * sy), int((y + 15) * sy) + 1
        boxes.append((x0, x1, y0, y1))
        if white[y0:y1, x0:x1].sum() < 4:
            return f"digit {d} at {x},{y}: no ink in its box ({x0}-{x1}, {y0}-{y1})"
    top, bottom = min(b[2] for b in boxes), max(b[3] for b in boxes)
    band = white[top:bottom].copy()
    for x0, x1, y0, y1 in boxes:
        band[y0 - top:y1 - top, x0:x1] = False
    if band.sum() > 40:
        cols = np.where(band.any(axis=0))[0]
        return f"ink beside the digits on their rows ({int(band.sum())} pixels, columns {cols.min()}-{cols.max()})"
    return None


def popups(card, out):
    import damage_numbers_smoke as D
    env = {"ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_WIDESCREEN": "1", "ROOD_GI": "0",
           "ROOD_LIGHTS": tempfile.mkdtemp(prefix="lights-", dir=out)}
    s = D.Session(out, card, env)
    g = s.g
    try:
        look(g)
        g.call("room_goto", zone=9, room=18, enemies=True, scripts=False)
        for _ in range(100):
            g.run(5)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(300)
        enemy = 0
        for slot in range(1, 16):
            actor = s.ptr(s.sym["vs_battle_actors"] + slot * D.PTR)
            if actor:
                enemy = int.from_bytes(g.read(actor + 0x3C, 4), "little")
                break
        if not check(enemy != 0, "an enemy in zone 9 room 18"):
            return
        frames = bad = 0
        for tick in range(POPUPS):
            g.write(enemy + D.HP, struct.pack("<h", 200))
            statuses = struct.unpack("<I", g.read(enemy + D.STATUSES, 4))[0]
            g.write(enemy + D.STATUSES, struct.pack("<I", statuses | D.POISON))
            g.write(enemy + D.TIMERS + 5, bytes([2]))
            for _ in range(200):
                g.run(1)
                if s.count() and s.label()["kind"] == 1:
                    break
            g.write(enemy + D.STATUSES, struct.pack("<I", statuses & ~D.POISON))
            def shown():
                """The digits drawn lately while the popup shows (the list keeps the last ones drawn)."""
                live = s.count() and s.label()["kind"] == 1 and s.label()["wait"] < 0
                return (g.call("ui_text").get("popups") or []) if live else []
            before = shown()
            for i in range(36):  # the bounce and the landing (the label goes at 31 game frames)
                path = out / f"popup-{tick}-{i:02d}.png"
                image = np.asarray(Image.open(g.present_screenshot(path)).convert("RGB")).astype(int)
                after = shown()
                # The picture is the frame drawn before or after the screenshot's vsync.
                problems = [popup_ink(image, d) for d in (before, after) if d]
                problem = problems[0] if problems and all(problems) else None
                frames += bool(problems)
                before = after
                if problem:
                    bad += 1
                    if bad <= 3:
                        print(f"  tick {tick} frame {i}: {problem}")
                else:
                    path.unlink()
                g.run(1)
        check(frames >= POPUPS * 10, f"popups: {frames} frames with digits drawn")
        check(bad == 0, f"popups: every digit inside its compressed quad on every frame ({bad} of {frames} not)")
    finally:
        g.close()


class Caster(ui_magick_smoke.Session):
    """ui_magick_smoke's session, continued from the save room checkpoint."""

    def __init__(self, name, out, card, env):
        self.name, self.out, self.shots = name, out, False
        self.g = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env)
        self.sym = symbols()


def fill(card, out):
    env = {"ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_WIDESCREEN": "1", "ROOD_GI": "0",
           "ROOD_LIGHTS": tempfile.mkdtemp(prefix="lights-", dir=out)}
    b = Caster("fill", out, card, env)
    g = b.g
    try:
        look(g)
        b.prepare()  # Explosion learned, MP full
        g.call("room_goto", zone=9, room=18, enemies=True, scripts=False)
        for _ in range(100):
            g.run(5)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(60)
        g.call("ui_menu", modern=True)  # (cast through the shell; the targeting is the game's)
        b.shell_open("warlock")
        b.shell_row(ui_magick_smoke.explosion_row(b))
        b.shell_level(0)
        g.press("circle", vsyncs=3, settle=1)
        if not check(b.wait_targeting() is not None, "Explosion: targeting"):
            return
        g.call("ui_menu", modern=False)

        def shot(path):
            image = np.asarray(Image.open(g.present_screenshot(path)).convert("RGB")).astype(int)
            return fill_metrics(image)
        for _ in range(30):  # the fill shows once the sphere has opened
            m = shot(out / "fill-wait.png")
            if m is not None and m["area"] > WINDOW[0] * WINDOW[1] // 40:
                break
            g.run(4)
        for i in range(3):
            m = shot(out / f"fill-{i}.png")
            if not check(m is not None and m["area"] > WINDOW[0] * WINDOW[1] // 40,
                         f"frame {i}: the floor fill drawn ({m and m['area']} pixels)"):
                continue
            check(not m["bands"], f"frame {i}: no bands beside the fill ({len(m['bands'])}: {m['bands'][:4]})")
            check(m["sloped"] > 50 and m["flat"] <= FLAT_MAX,
                  f"frame {i}: the fill's sloped edges move on every row ({m['flat']:.2f} of {m['sloped']} rows "
                  f"keep the previous row's end; at most {FLAT_MAX})")
            g.run(9)
        g.press("cross", vsyncs=3, settle=30)
    finally:
        b.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="keep the screenshots here")
    parser.add_argument("--only", choices=("menus", "popups", "fill"))
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "narrowed_2d"
    out.mkdir(parents=True, exist_ok=True)
    os.environ.pop("ROOD_SETTINGS", None)
    with card_guard(out / "cards", source=args.card) as cards:
        if args.only in (None, "menus"):
            menus(cards.card1, out)
        if args.only in (None, "popups"):
            popups(cards.card1, out)
        if args.only in (None, "fill"):
            fill(cards.card1, out)
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
