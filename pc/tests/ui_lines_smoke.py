#!/usr/bin/env python3
"""2D lines and vector talk bubbles at the internal resolution
(pc/src/gpu_hw.c pc_hw_line, pc/src/ui_bubbles.c; README "2D / UI").

Synthetic draws (RPC gpu_draw at the title screen, no game imagery) on
both rasterizers: a filled polygon outlined edge by edge with one-pixel
lines (as the game outlines its talk bubbles), an open polyline with sharp
turns, horizontal, vertical, diagonal and shallow lines, a semi-transparent
line. Checks:

- default ui_lines joined, ui_outline_boost 0, ui_bubbles original;
- 1x: the hardware target's line pixels are the CPU rasterizer's;
- 4x: a horizontal and a vertical line are 4 internal pixels thick; along
  the polyline every sample within 0.3 PS1 pixels of its centre line is
  covered (no gaps at joins); each 4 x 4 block's coverage gives back the 1x
  line (downsampled); no light fill touches the backdrop beside the
  outlined polygon (legacy, for comparison, leaves such dots);
- ui_outline_boost 2: lines 2 output pixels thicker.

The opening's first talk bubble (New Game), at 4x:

- ui_lines joined: no light fill touches the dark background along the
  bubble's outline (legacy leaves light dots there);
- ui_bubbles vector: every bubble frame matches its packets (none falls
  back), the outline drawn follows the game's points at each frame of the
  zoom (bounds equal to the game's whole-pixel points; within 2 pixels with
  ui_subpixel), no light pixel inside the outline band, all of the game's
  shapes, mirrorings and tails (poked into the live bubble) verify;
- settings: ui_lines, ui_outline_boost, ui_bubbles, ui_bubble_outline save
  and load; no crash, shader error or Vulkan validation message.

    python3 pc/tests/ui_lines_smoke.py [--shots DIR]

--shots keeps crops (game imagery: keep it out of git)."""
import argparse
import math
import os
import pathlib
import re
import shutil
import struct
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game, symbols  # noqa: E402

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)
    return ok


# ---- synthetic draws ----

BASE_X, BASE_Y, W, H = 640, 256, 128, 200
OFFSET_X, OFFSET_Y = BASE_X + 400, BASE_Y + 300  # far from any GTE projection: 2D
BLUE, FILL, INK, WHITE = 0xFF0000, 0xC0C0C0, 0x303030, 0xFFFFFF


def xy(x, y):
    x -= OFFSET_X - BASE_X
    y -= OFFSET_Y - BASE_Y
    return (x & 0xFFFF) | ((y & 0xFFFF) << 16)


def word_hex(words):
    return "".join(f"{w & 255:02x}{(w >> 8) & 255:02x}{(w >> 16) & 255:02x}{(w >> 24) & 255:02x}" for w in words)


# A jagged polygon (a talk bubble's outline: edges at many slopes).
CENTRE = (40, 48)
RADII = [30, 26, 33, 28, 35, 24, 31, 27, 34, 25, 32, 29, 30, 26]
POLYGON = [(CENTRE[0] + round(r * math.cos(2 * math.pi * i / len(RADII)) * 1.1),
            CENTRE[1] + round(r * math.sin(2 * math.pi * i / len(RADII)) * 0.8)) for i, r in enumerate(RADII)]
POLYLINE = [(82, 8), (122, 22), (92, 34), (124, 60), (86, 70), (100, 40), (80, 56)]
LINES = {"horizontal": ((6, 100), (60, 100)), "vertical": ((70, 92), (70, 150)),
         "diagonal": ((80, 96), (120, 136)), "shallow": ((6, 110), (60, 126))}
SEMI = ((6, 140), (60, 170))


def packet():
    words = [0x28000000 | BLUE, xy(0, 0), xy(W, 0), xy(0, H), xy(W, H)]
    for i in range(len(POLYGON)):
        a, b = POLYGON[i], POLYGON[(i + 1) % len(POLYGON)]
        words += [0x20000000 | FILL, xy(*CENTRE), xy(*a), xy(*b)]
    for i in range(len(POLYGON)):  # outline edge by edge (GP0 0x40), as func_800CD3E4 does
        a, b = POLYGON[i], POLYGON[(i + 1) % len(POLYGON)]
        words += [0x40000000 | INK, xy(*a), xy(*b)]
    words += [0x48000000 | WHITE] + [xy(*p) for p in POLYLINE] + [0x55555555]
    for a, b in LINES.values():
        words += [0x40000000 | WHITE, xy(*a), xy(*b)]
    words += [0xE1000000, 0x42000000 | WHITE, xy(*SEMI[0]), xy(*SEMI[1])]
    return words


def draw(game):
    return game.call("gpu_draw", area_x=BASE_X, area_y=BASE_Y, area_w=W, area_h=H, offset_x=OFFSET_X,
                     offset_y=OFFSET_Y, packet=word_hex(packet()))["words"]


def luma(rgb):
    return rgb[..., :3].astype(int) @ np.array([299, 587, 114]) // 1000


def target(game, path):
    game.call("gpu_dump", path=str(path))
    im = Image.open(path)
    scale = im.width // 1024
    crop = np.asarray(im.crop((BASE_X * scale, BASE_Y * scale, (BASE_X + W) * scale, (BASE_Y + H) * scale)))
    path.unlink()
    return scale, crop


def cpu_region(game):
    base = symbols()["vram"]
    rows = [np.frombuffer(game.read(base + (y * 1024 + BASE_X) * 2, W * 2), "<u2") for y in range(BASE_Y, BASE_Y + H)]
    px = np.array(rows)
    return np.stack([(px & 31) * 8, ((px >> 5) & 31) * 8, ((px >> 10) & 31) * 8], -1).astype(np.uint8)


def white(rgb):
    return (rgb[..., 0] > 200) & (rgb[..., 1] > 200) & (rgb[..., 2] > 200)


def segment_distance(px, py, a, b):
    """Distance from points to segment a-b (arrays px, py)."""
    ax, ay, bx, by = a[0] + 0.5, a[1] + 0.5, b[0] + 0.5, b[1] + 0.5  # pixel centres
    dx, dy = bx - ax, by - ay
    t = np.clip(((px - ax) * dx + (py - ay) * dy) / max(dx * dx + dy * dy, 1e-9), 0, 1)
    return np.hypot(px - (ax + t * dx), py - (ay + t * dy))


def speckles(rgb, ink_region):
    """Light fill pixels touching the blue backdrop (the outline must part them)."""
    light = (luma(rgb) > 150) & (rgb[..., 2] < 230)
    blue = (rgb[..., 2] > 200) & (rgb[..., 0] < 60) & (rgb[..., 1] < 60)
    touch = np.zeros_like(blue)
    touch[1:, :] |= blue[:-1, :]
    touch[:-1, :] |= blue[1:, :]
    touch[:, 1:] |= blue[:, :-1]
    touch[:, :-1] |= blue[:, 1:]
    return int((light & touch & ink_region).sum())


def polygon_region(scale):
    ys, xs = np.mgrid[0:H * scale, 0:W * scale]
    px, py = (xs + 0.5) / scale, (ys + 0.5) / scale
    d = np.full(px.shape, 1e9)
    for i in range(len(POLYGON)):
        d = np.minimum(d, segment_distance(px, py, POLYGON[i], POLYGON[(i + 1) % len(POLYGON)]))
    return d < 2.5


def thickness(mask, scale, axis, at, lo, hi):
    """Covered run across a line (axis 0: rows of column `at`, 1: columns of row `at`)."""
    if axis == 0:
        run = mask[lo * scale:hi * scale, int((at + 0.5) * scale)]
    else:
        run = mask[int((at + 0.5) * scale), lo * scale:hi * scale]
    return int(run.sum())


def synthetic(game, out, shots):
    g = game.call("graphics")
    check(g.get("ui_lines") == "joined" and g.get("ui_outline_boost") == 0 and g.get("ui_bubbles") == "original",
          f"defaults: ui_lines joined, ui_outline_boost 0, ui_bubbles original ({g.get('ui_lines')}, "
          f"{g.get('ui_outline_boost')}, {g.get('ui_bubbles')})")
    lines_mask = None
    for scale in (1, 4):
        game.call("graphics", scale=scale)
        game.run(4)
        before = game.call("ui_bubbles")["lines_drawn"]
        check(draw(game) == len(packet()), f"scale {scale}: gpu_draw drew the test packet")
        check(game.call("ui_bubbles")["lines_drawn"] - before >= len(POLYGON) + len(POLYLINE) + 4,
              f"scale {scale}: the lines reached the hardware path")
        got, rgb = target(game, out / "target.ppm")
        check(got == scale, f"hardware target at internal scale {got}")
        hw = white(rgb)
        if scale == 1:
            cpu = white(cpu_region(game))
            # The semi-transparent line blends; compare the opaque white lines.
            keep = np.ones_like(cpu)
            keep[135:175, :64] = False
            diff = int((cpu[keep] != hw[keep]).sum())
            if diff:
                print("  differ at (x, y, CPU):",
                      [(int(x), int(y), bool(cpu[y, x])) for y, x in zip(*np.nonzero((cpu != hw) & keep))][:12])
            check(diff == 0, f"1x: the hardware lines are the CPU rasterizer's pixels ({diff} differ of "
                             f"{int(cpu[keep].sum())})")
            lines_mask = cpu & keep
            continue
        # 4x
        h = thickness(hw, scale, 0, 30, 96, 106)
        v = thickness(hw, scale, 1, 120, 66, 76)
        check(h == scale and v == scale, f"4x: horizontal and vertical lines are {scale} pixels thick ({h}, {v})")
        ys, xs = np.mgrid[0:H * scale, 0:W * scale]
        px, py = (xs + 0.5) / scale, (ys + 0.5) / scale
        d = np.full(px.shape, 1e9)
        for a, b in zip(POLYLINE, POLYLINE[1:]):
            d = np.minimum(d, segment_distance(px, py, a, b))
        near = d < 0.3
        gaps = int((near & ~hw).sum())
        check(gaps == 0, f"4x: the polyline covers its centre line, joins included ({gaps} gaps of {int(near.sum())})")
        # Downsampled (each 4 x 4 block's coverage), the 4x lines are the 1x
        # lines: every 1x pixel at least half covered, as much ink in all
        # (within 10 %), none further than the 1x pixels' neighbours.
        keep = np.ones((H, W), bool)
        keep[135:175, :64] = False
        cover = hw.reshape(H, scale, W, scale).mean(axis=(1, 3)) * keep
        missed = int((lines_mask & (cover < 0.5)).sum())
        ink, ones = float(cover.sum()), int(lines_mask.sum())
        grown = lines_mask.copy()
        grown[1:, :] |= lines_mask[:-1, :]
        grown[:-1, :] |= lines_mask[1:, :]
        grown[:, 1:] |= grown[:, :-1].copy()
        grown[:, :-1] |= grown[:, 1:].copy()
        stray = int(((cover > 0) & ~grown).sum())
        check(missed == 0 and abs(ink - ones) <= 0.1 * ones and stray == 0,
              f"4x: downsampled, the lines are the 1x lines ({missed} of {ones} pixels under half covered, "
              f"ink {ink:.0f} for {ones}, {stray} blocks beyond their neighbours)")
        region = polygon_region(scale)
        joined = speckles(rgb, region)
        check(joined == 0, f"4x joined: no light fill beside the outlined polygon ({joined} pixels)")
        if shots:
            Image.fromarray(rgb).save(shots / "synthetic-joined-4x.png")
        game.call("graphics", ui_lines="legacy")
        draw(game)
        _, legacy_rgb = target(game, out / "target.ppm")
        legacy = speckles(legacy_rgb, region)
        print(f"note: legacy lines leave {legacy} light pixels beside the outline (joined: {joined})")
        check(legacy > joined, "4x legacy: the check sees the light dots the joined lines remove")
        if shots:
            Image.fromarray(legacy_rgb).save(shots / "synthetic-legacy-4x.png")
        game.call("graphics", ui_lines="joined", ui_outline_boost=2)
        draw(game)
        _, boost_rgb = target(game, out / "target.ppm")
        npo = game.call("ui_bubbles")["native_per_output"]
        extra = 2 * npo * scale
        hb = thickness(white(boost_rgb), scale, 0, 30, 92, 110)
        check(abs(hb - (scale + extra)) <= 1.01,
              f"4x ui_outline_boost 2: a horizontal line is {hb} pixels thick ({scale} + {extra:.1f})")
        game.call("graphics", ui_outline_boost=0)


# ---- the opening's first talk bubble ----

BOX = 0x30


def box_state(game, sym):
    raw = game.read(sym["vs_battle_textBoxes"], BOX)
    return struct.unpack_from("<I", raw, 0)[0], struct.unpack_from("<h", raw, 0x1C)[0]


def dump_screen(game, out):
    p = out / "bubble.ppm"
    game.call("gpu_dump", path=str(p))
    disp = game.call("ui_upscale")["display"]
    im = np.asarray(Image.open(p))
    p.unlink()
    return im, disp


def bubble_speckles(im, disp, path, scale):
    """Light pixels touching dark background within 2 PS1 pixels of the outline."""
    pts = np.array(path, float) + [disp[0], disp[1]]
    x0, y0 = (pts.min(0) - 3).astype(int)
    x1, y1 = (pts.max(0) + 3).astype(int)
    crop = im[y0 * scale:y1 * scale, x0 * scale:x1 * scale]
    L = luma(crop)
    ys, xs = np.mgrid[0:crop.shape[0], 0:crop.shape[1]]
    px, py = x0 + (xs + 0.5) / scale, y0 + (ys + 0.5) / scale
    d = np.full(px.shape, 1e9)
    for a, b in zip(pts, np.roll(pts, -1, 0)):
        d = np.minimum(d, segment_distance(px + 0.5, py + 0.5, a, b))  # to the corner path
    dark = L < 36
    touch = np.zeros_like(dark)
    touch[1:, :] |= dark[:-1, :]
    touch[:-1, :] |= dark[1:, :]
    touch[:, 1:] |= dark[:, :-1]
    touch[:, :-1] |= dark[:, 1:]
    return int(((L > 120) & touch & (d < 2.0)).sum()), L, d


def paper(im, disp, scale):
    """The light, unsaturated pixels (the bubbles' paper) of the screen's upper part."""
    crop = im[disp[1] * scale:(disp[1] + 110) * scale, disp[0] * scale:(disp[0] + disp[2]) * scale, :3].astype(int)
    return (luma(crop) > 140) & (crop.max(axis=2) - crop.min(axis=2) < 24)


def paper_bounds(mask, near, scale, margin=4):
    """The paper's bounds (PS1 pixels) within `margin` of the bounds `near`."""
    x0, y0 = max(0, int(near[0]) - margin), max(0, int(near[1]) - margin)
    x1, y1 = int(near[2]) + margin, int(near[3]) + margin
    ys, xs = np.nonzero(mask[y0 * scale:y1 * scale, x0 * scale:x1 * scale])
    if not len(xs):
        return None
    return [float(x0 + xs.min() / scale), float(y0 + ys.min() / scale), float(x0 + (xs.max() + 1) / scale),
            float(y0 + (ys.max() + 1) / scale)]


def zoom(game, out, mode):
    """New Game to the opening's first talk bubble, through its zoom: per box
    state, the drawn bubble (RPC) and its paper's bounds in the picture."""
    sym = symbols()
    game.call("graphics", scale=4, ui_bubbles=mode)
    game.wait_title()
    game.run(90)
    game.press("start", vsyncs=4, settle=90)
    game.press("circle", vsyncs=3, settle=60)
    game.run(2200)
    frames = {}
    for _ in range(240):
        _, state = box_state(game, sym)
        if state > 0 and state not in frames:
            game.run(1)  # the frame built at this state is the next one shown
            im, disp = dump_screen(game, out)
            info = game.call("ui_bubbles")
            frames[state] = (paper(im, disp, 4), info["bubbles"][0] if info["bubbles"] else None)
        else:
            game.run(1)
        if state >= 11 and len(frames) >= 9:
            break
    return frames


def bubble(game, out, shots, original):
    sym = symbols()
    frames = zoom(game, out, "vector")
    states = sorted(frames)
    check(len(states) >= 9, f"bubble: the zoom drew {len(states)} frames ({states})")
    worst = max((max(abs(p - q) for p, q in zip(b["bounds"], b["game_bounds"])) for _, b in frames.values() if b),
                default=99)
    check(worst <= 1.0, f"bubble vector: the outline is the game's points at each frame (worst {worst:.2f} px)")
    # The pictures: the paper's bounds about the bubble drawn (both runs show
    # the same frames, New Game pressed at the same vsync).
    bounds = {}
    for st in states:
        b = frames[st][1]
        if b and st in original:
            v, o = paper_bounds(frames[st][0], b["game_bounds"], 4), paper_bounds(original[st][0], b["game_bounds"], 4)
            if v and o:
                bounds[st] = (max(abs(p - q) for p, q in zip(v, o)), v, o)
    where = max(bounds, key=lambda st: bounds[st][0]) if bounds else None
    off = bounds[where][0] if where else 99
    check(len(bounds) >= 8 and off <= 1.0,
          f"bubble vector: its paper's bounds are the original's within 1 px at each frame of the zoom "
          f"({len(bounds)} frames, worst {off:.2f} px"
          + (f" at state {where}: {bounds[where][1]} vs {bounds[where][2]})" if where else ")"))
    game.run(30)
    info = game.call("ui_bubbles", path=0)
    check(info["drawn"] > 0 and info["verified"] > 0 and info["mismatched"] == 0,
          f"bubble: vector frames drawn {info['drawn']}, verified {info['verified']}, fell back {info['mismatched']}")
    scale = 4
    im, disp = dump_screen(game, out)
    path = info.get("path", [])
    b = info["bubbles"][0]
    count, L, d = bubble_speckles(im, disp, path, scale)
    core = b["thickness"] / 2 - info["native_per_output"] / 2 - 0.05
    band = d < core
    light = int((band & (L > 100)).sum())
    check(len(path) >= 6 and band.sum() > 100 and light == 0,
          f"bubble vector: no light pixel inside the outline band ({light} of {int(band.sum())}, core "
          f"{core:.2f} px)")
    check(count == 0, f"bubble vector: no light fill touches the background along the outline ({count})")
    if shots:
        Image.fromarray(im[disp[1] * 4:(disp[1] + 120) * 4, (disp[0] + 40) * 4:(disp[0] + 260) * 4]).save(
            shots / "bubble-vector.png")
    # With MSAA (2D, the coverage blend too, at the pixel centres): the same.
    aa = game.call("graphics").get("aa")
    if aa is not None:
        game.call("graphics", aa="msaa4")
        game.run(6)
        msaa = game.call("graphics").get("msaa_active", 0)
        im, disp = dump_screen(game, out)
        count, L, d = bubble_speckles(im, disp, path, scale)
        light = int(((d < core) & (L > 100)).sum())
        check(count == 0 and light == 0 and game.call("ui_bubbles")["mismatched"] == 0,
              f"bubble vector with MSAA ({msaa}x): no light pixel in the outline band ({light}) or beside it ({count})")
        game.call("graphics", aa=aa)
        game.run(4)
    # Original bubbles: joined lines leave no light dots; legacy ones do.
    results = {}
    for lines in ("joined", "legacy"):
        game.call("graphics", ui_bubbles="original", ui_lines=lines)
        game.run(6)
        im, disp = dump_screen(game, out)
        results[lines] = bubble_speckles(im, disp, path, scale)[0]
        if shots:
            Image.fromarray(im[disp[1] * 4:(disp[1] + 120) * 4, (disp[0] + 40) * 4:(disp[0] + 260) * 4]).save(
                shots / f"bubble-original-{lines}.png")
    check(results["joined"] == 0, f"bubble original, joined lines: no light dots along the outline "
                                  f"({results['joined']}; legacy {results['legacy']})")
    check(results["legacy"] > results["joined"], "bubble original, legacy lines: the check sees their light dots")
    game.call("graphics", ui_lines="joined", ui_bubbles="vector", ui_subpixel=1)
    game.run(6)
    b = game.call("ui_bubbles")["bubbles"][0]
    off = max(abs(p - q) for p, q in zip(b["bounds"], b["game_bounds"]))
    check(off <= 2.0, f"bubble vector, ui_subpixel: outline within 2 px of the game's points ({off:.2f})")
    game.call("graphics", ui_subpixel=0)
    # The game's shapes (0-8), mirrorings and tails, poked into the live bubble.
    flags0, _ = box_state(game, sym)
    combos = [(s, 0, 4) for s in range(9)] + [(8, f, 4) for f in (1, 2, 3)] + [(8, 0, t) for t in (0, 1, 2, 3, 5, 6)]
    before = game.call("ui_bubbles")
    bad = []
    for shape, flip, tail in combos:
        flags = (flags0 & ~0xFFC0) | (flip << 6) | (shape << 8) | (tail << 12)
        game.write(sym["vs_battle_textBoxes"], struct.pack("<I", flags))
        game.run(8)
        b = game.call("ui_bubbles")["bubbles"][0]
        if (b["shape"], b["flip"], b["tail"]) != (shape, flip, tail) or b["bounds"] != [float(v) for v in
                                                                                        b["game_bounds"]]:
            bad.append((shape, flip, tail))
    after = game.call("ui_bubbles")
    game.write(sym["vs_battle_textBoxes"], struct.pack("<I", flags0))
    check(not bad and after["mismatched"] == before["mismatched"],
          f"bubble vector: {len(combos)} shapes/mirrorings/tails drawn from their own points "
          f"({len(bad)} not: {bad}; fell back {after['mismatched'] - before['mismatched']})")


def settings(game, out):
    game.call("graphics", ui_lines="legacy", ui_outline_boost=3, ui_bubbles="vector", ui_bubble_outline=1.5)
    game.call("settings", action="save")
    text = (out / "settings.cfg").read_text()
    check(all(k in text for k in ("ui_lines legacy", "ui_outline_boost 3", "ui_bubbles vector", "ui_bubble_outline 1.5")),
          "settings: ui_lines, ui_outline_boost, ui_bubbles, ui_bubble_outline saved")
    game.call("graphics", ui_lines="joined", ui_outline_boost=0, ui_bubbles="original", ui_bubble_outline=1)
    game.call("settings", action="load")
    g = game.call("graphics")
    check(g["ui_lines"] == "legacy" and g["ui_outline_boost"] == 3 and g["ui_bubbles"] == "vector"
          and abs(g["ui_bubble_outline"] - 1.5) < 1e-6, "settings: they load back")
    game.call("graphics", ui_lines="joined", ui_outline_boost=0, ui_bubbles="original", ui_bubble_outline=1)
    (out / "settings.cfg").unlink()  # the next launches start from the defaults


def log_clean(game, label):
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log and "[gl] shader" not in log and "[vk] validation:" not in log
          and "[gl] link" not in log and not re.search(r"shader.*(error|fail)", log, re.I),
          f"{label}: no crash, shader error or validation message")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--shots", type=pathlib.Path)
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "ui_lines"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    if args.shots:
        args.shots.mkdir(parents=True, exist_ok=True)
    env = {"ROOD_GI": "0", "ROOD_WINDOW": "1280x960", "ROOD_OUT": str(out), "ROOD_SETTINGS": str(out / "settings.cfg")}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env["ROOD_VK_VALIDATE"] = "1"
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-lines-{os.getpid()}.sock"
    game = Game.launch(fast=True, log=out / "synthetic.log", socket_path=socket, extra_env=env)
    try:
        game.call("graphics", interpolate=False)
        game.wait_title()
        game.run(30)
        synthetic(game, out, args.shots)
        settings(game, out)
    finally:
        game.close()
    log_clean(game, "synthetic")
    game = Game.launch(fast=True, log=out / "original.log", socket_path=socket, extra_env=env)
    try:
        game.call("graphics", interpolate=False)
        original = zoom(game, out, "original")
    finally:
        game.close()
    log_clean(game, "original bubble")
    game = Game.launch(fast=True, log=out / "bubble.log", socket_path=socket, extra_env=env)
    try:
        game.call("graphics", interpolate=False)
        bubble(game, out, args.shots, original)
    finally:
        game.close()
    log_clean(game, "vector bubble")
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
