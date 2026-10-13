#!/usr/bin/env python3
"""16:9 widescreen: full-screen 2D covers the view, menus and pictures are laid out for it.

    python3 pc/tests/widescreen_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Widescreen (Hor+) compresses 2D about the screen's centre so it keeps its
shape. The game's full-screen effects are 2D too: the door fade is a
320x240 subtractive quad (BATTLE 146C.c _drawScreenTransition), cutscenes
fade the same way, the battle-mode message bar is a full-width tile, the map
screen draws its background picture in two sprites (256 + 64) and redraws
the last frame for its rippling transition. Compressed, those covered only
the central 3/4 of the view: the sides kept the previous image (the old room
during a door fade, the hall unfaded while the centre went black). gpu_hw.c
(pc_hw_set_bounds) now draws such 2D over the full view.

The later layout fixes (gpu_hw.c pc_widescreen_layout): compressed 2D is
clipped to the compressed drawing area (what the game parks beyond the
screen stays hidden), untextured bars reaching one edge of the screen (menu
rows, title bars) stretch to that edge of the view, 2D whose vertices the GTE
projected is not compressed twice (the map screen's rooms), stills that
cover the screen fill the view (the idle cutscene's VKP Headquarters), a
picture loaded into the frame buffer is compressed like other 2D (the title),
screen copies of the frame being drawn read the target back (the memory-card
screen's text box); the map screen's controls help is anchored to the left
edge. SetDispMask(0) shows black (skipping the idle cutscene showed VRAM).

Two sessions of the same inputs, ROOD_WIDESCREEN_COVER=0,
ROOD_WIDESCREEN_LAYOUT=0 and ROOD_DISPLAY_MASK=0 (the old behaviour, the
control) and the defaults (the fixes), each:

- the title: a frame in 16:9, then 4:3 (RPC graphics widescreen off and on):
  pillarboxed, the 16:9 frame's middle is the 4:3 frame;

- the title's idle cutscene from the title (the title fading out, the
  cutscene's own fades between shots) and a Start press skipping it, every
  8 vsyncs; the "seam" of each frame is how much stronger the vertical edge
  is on the compressed area's borders (x = 40 and 280 of 320) than in the
  columns around them (1: none); the VKP Headquarters still (vsyncs
  1240-1440): its sides; the skip: RPC status display_masked and the frame;
- Continue: the memory-card screen (its rows reach the right edge, no text
  box fragment left of CAUTION), then into the save room (zone 9 room 17):
  HUD crop, VRAM read-backs over 240 vsyncs in the field (screen copies read
  the target back; the field must not);
- a door transition (RPC room_goto zone 9 room 16, the game's door fade):
  frames every 3 vsyncs; side columns vs the centre;
- the map screen (Triangle, Map): the sides are not the room any more; the
  controls help at the left edge;
- back to the main menu: its command rows reach the right edge;
- battle mode with a chain ability's timing verdict and chain count queued
  in RAM ("GOOD!", "3 CHAINS"): beside the status display at the left.

Checks: the control shows the bug (seams during the fades; side pixels left
unchanged while the centre fades) so the measures are live; the fix has no
seam at the door, the sides darken with the centre and are black when it is
black, the idle cutscene's fade seams are gone (cutscene frames whose seam
the control had and the fix has not) and no frame gets a seam the control
lacked; the HUD crop is the same in both; the map's sides changed; and the
layout and SetDispMask checks above (the control without them).

4:3 is not touched (the classification only matters with widescreen): the
golden frames suite covers it. --out keeps frames (game imagery: keep it out
of git)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
import golden_frames as gf  # noqa: E402

WINDOW = (854, 480)  # 16:9: the view fills the window
GRAPHICS = {**gf.DEFAULTS, "scale": 1, "filter": 0, "aa": False, "widescreen": True, "interpolate": True}
IDLE = range(800, 1600, 8)  # vsyncs after the title is ready: title fade-out, cutscene fades
VKP = range(1240, 1448)  # the VKP Headquarters still (a 320x240 picture in 64x15 tiles)
SKIP_FRAMES = 30
HUD = (slice(0, 60), slice(0, 240))  # HP/MP/RISK, top left


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def seam(image):
    """How much stronger the vertical edge is at the compressed 4:3 area's
    borders than in the columns around them (about 1: no seam)."""
    a = image.astype(np.float32).mean(axis=2)
    w = a.shape[1]
    worst = 0.0
    for border in (w * 40 / 320, w * 280 / 320):
        b = int(round(border))

        def step(c):
            return float(np.abs(a[:, c + 2] - a[:, c - 2]).mean())
        base = float(np.median([step(b + k) for k in range(-24, 25) if abs(k) > 5]))
        worst = max(worst, step(b) / (base + 1.0))
    return worst


def fade_seam(image):
    """seam(), counted only where the view's side of a border is the brighter
    one: the sides showing a picture the centre has faded from (or not yet
    into). Pictures that themselves end at the 4:3 borders with black beyond
    (the idle cutscene's VKP shot, a widescreen issue of its own) do not
    count."""
    a = image.astype(np.float32).mean(axis=2)
    w = a.shape[1]
    left, right = int(round(w * 40 / 320)), int(round(w * 280 / 320))
    outside_left, inside_left = a[:, left - 8: left - 2].mean(), a[:, left + 3: left + 9].mean()
    outside_right, inside_right = a[:, right + 3: right + 9].mean(), a[:, right - 8: right - 2].mean()
    if outside_left > inside_left + 4 or outside_right > inside_right + 4:
        return seam(image)
    return 0.0


def regions(image):
    """(sides, centre) luminance: the outer 1/8 of the view on each side
    (outside the compressed 4:3 area) and its middle fifth."""
    a = image.astype(np.float32).mean(axis=2)
    w = a.shape[1]
    sides = np.concatenate([a[:, : w // 8], a[:, w - w // 8:]], axis=1)
    return sides, a[:, w * 2 // 5: w * 3 // 5]


def add_labels(game):
    """Queue the battle labels the timing of a chain ability shows, as
    vs_battle_renderBattleAbilityTimingResult (BATTLE 2842C.c) and the chain
    counter do: "GOOD!" (type 4, success, text 1) and "3 CHAINS" (type 5),
    beside the status display at the left (24-byte records, D_800F1BAC)."""
    table = int.from_bytes(game.read_symbol("D_800F1BAC", 4), "little")
    count = int.from_bytes(game.read_symbol("D_800F227E", 2), "little")
    good, chains = bytearray(24), bytearray(24)
    good[8], good[12], good[15] = 4, 2, 1
    chains[8], chains[12] = 5, 3
    game.write(table + count * 24, bytes(good) + bytes(chains))
    game.write(symbols()["D_800F227E"], (count + 2).to_bytes(2, "little"))


def green_left(image, rows):
    """The leftmost column of the green "GOOD!" (or the width)."""
    a = image[rows].astype(np.int16)
    green = (a[..., 1] > a[..., 0] + 30) & (a[..., 1] > a[..., 2] + 30) & (a[..., 1] > 90)
    columns = np.where(green.any(axis=0))[0]
    return int(columns[0]) if len(columns) else image.shape[1]


def blue_share(image, rows, columns):
    """Share of pixels where blue clearly leads (menu rows' gradient)."""
    a = image[rows, columns].astype(np.int16)
    return float(((a[..., 2] > a[..., 0] + 25) & (a[..., 2] > 40)).mean())


def first_bright_column(image, rows, level=150):
    """The leftmost column with a bright pixel in these rows (or the width)."""
    columns = np.where((image[rows].astype(np.int16).max(axis=2) > level).any(axis=0))[0]
    return int(columns[0]) if len(columns) else image.shape[1]


def bright_count(image, rows, columns, level=150):
    return int((image[rows, columns].astype(np.int16).max(axis=2) > level).sum())


class Session:
    def __init__(self, cover, args, out):
        self.cover, self.out = cover, out / f"cover{cover}"
        self.out.mkdir(parents=True, exist_ok=True)
        card = self.out / "card1.mcd"
        shutil.copyfile(args.card, card)
        self.lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=self.out))
        env = {"ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_LIGHTS": str(self.lights), "ROOD_GI": "0",
               "ROOD_SCALE": "1", "ROOD_WIDESCREEN": "1", "ROOD_WIDESCREEN_COVER": str(cover),
               "ROOD_WIDESCREEN_LAYOUT": str(cover), "ROOD_DISPLAY_MASK": str(cover)}
        os.environ.pop("ROOD_SETTINGS", None)
        socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-widescreen-{os.getpid()}.sock"
        self.game = Game.launch(fast=True, card=str(card), log=self.out / "game.log", extra_env=env,
                                socket_path=socket)
        self.keep = args.out is not None
        self.result = {}

    def capture(self, name):
        image = gf.capture(self.game, self.out / "frame")
        if self.keep:
            Image.fromarray(image).save(self.out / f"{name}.png")
        return image

    def run(self):
        game, r = self.game, self.result
        game.wait_title()
        game.call("graphics", **GRAPHICS)
        game.run(8 - game.status()["vsync"] % 8)  # a fixed phase: both sessions see the same frames
        start = game.status()["vsync"]
        # ---- the title: 16:9, then 4:3 ----
        game.run(30)
        r["title"] = [self.capture("title-169")]
        game.call("graphics", widescreen=False)
        game.run(8)
        r["title"].append(self.capture("title-43"))
        game.call("graphics", widescreen=True)
        game.run(8)
        # ---- the title's idle cutscene, then Start skips it ----
        r["idle"], r["vkp"] = [], []
        for target in IDLE:
            game.run(start + target - game.status()["vsync"])
            image = self.capture(f"idle-{target:04d}")
            r["idle"].append(fade_seam(image))
            if target in VKP:
                sides, centre = regions(image)
                r["vkp"].append((float(sides.mean()), float(centre.mean())))
        r["idle_zone"] = game.state().get("zone")
        game.call("input", buttons=["start"], vsyncs=4)
        r["skip"], r["masked"] = [], []
        for i in range(SKIP_FRAMES):
            game.run(2)
            masked = game.status().get("display_masked")
            image = self.capture(f"skip-{i:02d}")
            r["skip"].append(fade_seam(image))
            r["masked"].append((masked, float(image.mean())))
        game.run(300)
        # ---- Continue: the memory-card screen, into the save room ----
        game.run(8 - game.status()["vsync"] % 8)
        game.press("start", vsyncs=4, settle=90)
        game.press("circle", vsyncs=3, settle=90)  # Continue
        r["card"] = self.capture("card")
        for _ in range(2):  # the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        r["room"] = (state.get("zone"), state.get("room"))
        if r["room"] != (9, 17):
            return
        before = game.call("render_stats")["vram_syncs"]
        game.run(240)
        r["field_syncs"] = game.call("render_stats")["vram_syncs"] - before
        room = self.capture("room")
        r["hud"] = room[HUD]
        # ---- a door: the game's fade out and in ----
        game.call("room_goto", zone=9, room=16)
        r["door"] = [room] + [None] * 40
        for i in range(1, 41):
            game.run(3)
            r["door"][i] = self.capture(f"door-{i:02d}")
        for _ in range(30):
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
            game.run(20)
        game.run(60)
        r["arrived"] = (game.state().get("zone"), game.state().get("room"))
        # ---- the map screen ----
        before_map = self.capture("before-map")
        game.press("triangle", vsyncs=3, settle=40)
        for _ in range(3):  # Battle Abilities, Items, Status, Map
            game.press("down", vsyncs=3, settle=12)
        game.press("circle", vsyncs=3, settle=150)
        r["map"] = (before_map, self.capture("map"))
        # ---- back to the main menu ----
        game.press("cross", vsyncs=3, settle=90)
        r["menu"] = self.capture("menu")
        # ---- battle labels: a timing verdict and a chain count ----
        for _ in range(2):
            game.press("cross", vsyncs=3, settle=60)
        game.press("circle", vsyncs=3, settle=40)  # battle mode
        add_labels(game)
        game.run(20)
        r["labels"] = self.capture("labels")

    def close(self):
        self.game.close()
        shutil.rmtree(self.lights, ignore_errors=True)
        log = pathlib.Path(self.game.log).read_text(errors="replace")
        return not ("[crash]" in log or "[gl] shader" in log or "[vk] validation:" in log)


def door_measures(frames):
    """Per frame after the first: (centre ratio, side ratio, share of lit side
    pixels unchanged since the frame before the door, side mean)."""
    sides0, centre0 = regions(frames[0])
    lit = sides0 > 12
    rows = []
    for image in frames[1:]:
        sides, centre = regions(image)
        unchanged = float((np.abs(sides - sides0) < 1.0)[lit].mean()) if lit.any() else 0.0
        rows.append((float(centre.mean() / max(centre0.mean(), 1.0)), float(sides.mean() / max(sides0.mean(), 1.0)),
                     unchanged, float(sides.mean())))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the frames here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "widescreen"
    out.mkdir(parents=True, exist_ok=True)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    results = {}
    for cover in (0, 1):
        session = Session(cover, args, out)
        try:
            session.run()
        finally:
            check(session.close(), f"cover {cover}: no crash, shader or validation error in the log")
        results[cover] = session.result
    old, new = results[0], results[1]

    # ---- the title's idle cutscene ----
    for name, r in (("control", old), ("fix", new)):
        print(f"{name}: idle cutscene seams " + " ".join(f"{s:.1f}" for s in r["idle"]))
        print(f"{name}: skip seams " + " ".join(f"{s:.1f}" for s in r["skip"]))
    check(old.get("idle_zone") == 20 and new.get("idle_zone") == 20, "the idle cutscene plays (zone 20)")
    # (The sessions' cutscenes can run a few frames apart: each is judged on its own.)
    seams_old = [IDLE[i] for i, v in enumerate(old["idle"]) if v >= 3.0]
    check(len(seams_old) >= 3, f"control: the title's and the cutscene's fades leave the sides bright at the "
                               f"4:3 borders ({len(seams_old)} frames, vsyncs {seams_old[:12]})")
    seams_new = [(IDLE[i], round(v, 1)) for i, v in enumerate(new["idle"]) if v >= 2.0]
    seams_new += [("skip", round(v, 1)) for v in new["skip"] if v >= 2.0]
    check(not seams_new, f"fix: the sides fade with the centre in the idle cutscene and its skip {seams_new}")

    # ---- the field: HUD, read-backs ----
    check(old.get("room") == (9, 17) and new.get("room") == (9, 17), f"in the save room ({old.get('room')}, "
                                                                       f"{new.get('room')})")
    if new.get("room") != (9, 17) or old.get("room") != (9, 17):
        print("FAIL: widescreen_smoke")
        return 1
    # The HUD's bright pixels (the room behind it may sit a frame apart).
    bright = [r["hud"].astype(np.int16).max(axis=2) > 140 for r in (old, new)]
    overlap = float((bright[0] & bright[1]).sum() / max((bright[0] | bright[1]).sum(), 1))
    check(overlap > 0.9, f"the HUD is unchanged (its bright pixels overlap {overlap:.2f})")
    check(new["field_syncs"] <= 4, f"no frame-buffer read-backs in the field ({new['field_syncs']} in 240 vsyncs)")

    # ---- the door ----
    rows = {k: door_measures(r["door"]) for k, r in results.items()}
    for k, name in ((0, "control"), (1, "fix")):
        print(f"{name}: door centre/side ratio, unchanged side share: "
              + " ".join(f"{c:.2f}/{s:.2f}/{u:.2f}" for c, s, u, _ in rows[k]))
    # Fading out: the centre below 60% of its brightness, before the next room.
    def fading(k):
        out_rows = []
        for row in rows[k]:
            if row[0] < 0.6:
                out_rows.append(row)
            elif out_rows:
                break
        return out_rows
    old_fade, new_fade = fading(0), fading(1)
    check(old_fade and max(u for _, _, u, _ in old_fade) > 0.8,
          f"control: the sides keep the old room while the centre fades "
          f"(unchanged side share up to {max([u for _, _, u, _ in old_fade] or [0]):.2f})")
    check(new_fade and max(u for _, _, u, _ in new_fade) < 0.2,
          f"fix: the sides fade with the centre (unchanged side share at most "
          f"{max([u for _, _, u, _ in new_fade] or [1]):.2f})")
    together = [(round(c, 2), round(s, 2)) for c, s, _, _ in new_fade if s > c + 0.45]
    check(not together, f"fix: sides no brighter than the centre allows during the fade {together}")
    darkest = min(rows[1], key=lambda row: row[0])
    check(darkest[0] < 0.05 and darkest[3] < 3.0,
          f"fix: black sides when the centre is black (centre ratio {darkest[0]:.2f}, side mean {darkest[3]:.1f})")
    door_seams = [seam(image) for image in results[1]["door"]]
    old_seams = [seam(image) for image in results[0]["door"]]
    check(max(old_seams) >= 3.0, f"control: the door fade leaves a seam at the 4:3 borders ({max(old_seams):.1f})")
    check(max(door_seams) < 2.0, f"fix: no seam at the 4:3 borders during the door ({max(door_seams):.1f})")
    check(new.get("arrived") == (9, 16), f"arrived in zone 9 room 16 ({new.get('arrived')})")

    # ---- the map screen ----
    for k, name in ((0, "control"), (1, "fix")):
        before, after = results[k]["map"]
        sb, _ = regions(before)
        sa, _ = regions(after)
        lit = sb > 12
        unchanged = float((np.abs(sa - sb) < 1.0)[lit].mean()) if lit.any() else 0.0
        results[k]["map_unchanged"] = unchanged
        results[k]["map_seam"] = fade_seam(after)
        print(f"{name}: map screen: side pixels unchanged from the room {unchanged:.2f}, "
              f"bright sides at the 4:3 borders {results[k]['map_seam']:.1f}")
    # (The control's sides show the old room, or the frames before rippling.)
    check(results[0]["map_seam"] >= 2.0, "control: the map screen's sides are not its picture")
    check(results[1]["map_unchanged"] < 0.2 and results[1]["map_seam"] < 2.0,
          f"fix: the map screen's picture covers the sides ({results[1]['map_unchanged']:.2f} unchanged, "
          f"{results[1]['map_seam']:.1f})")

    # ---- the layout fixes (the control has none of them) ----
    w, h = WINDOW
    for k, name in ((0, "control"), (1, "fix")):
        r = results[k]
        title16, title43 = r["title"]
        middle = slice(w // 8, w - w // 8)
        r["title_diff"] = float(np.abs(title16[:, middle].astype(np.int16) - title43[:, middle]).mean())
        card, menu, map_image = r["card"], r["menu"], r["map"][1]
        # rows: the memory-card screen's slot rows (y 52..78 of 240), the
        # command rows (y 18..140), the controls help (y 18..86)
        r["card_edge"] = float(card[104:121, w - 50: w - 4, 2].astype(np.float32).mean())  # blue
        r["card_peek"] = bright_count(card, slice(100, 160), slice(w * 7 // 8, w), level=170)
        r["card_ghost"] = bright_count(card, slice(370, 400), slice(4, 100))
        r["menu_edge"] = blue_share(menu, slice(40, 280), slice(w - 30, w - 4))
        r["map_help"] = first_bright_column(map_image, slice(40, 60))
        r["good"] = green_left(r["labels"], slice(90, 120))  # y 48 of 240
        vkp = r["vkp"]
        r["vkp_boxed"] = sum(1 for sides, centre in vkp if sides < 1.5 and centre > 10)
        print(f"{name}: title 16:9 vs 4:3 middle {r['title_diff']:.1f}; memory card row at the edge (blue) "
              f"{r['card_edge']:.0f}, parked text in the right side {r['card_peek']}, bright pixels left of "
              f"CAUTION {r['card_ghost']}; menu rows at the edge "
              f"{r['menu_edge']:.2f}; map help from x {r['map_help']}; GOOD! from x {r['good']}; VKP frames boxed "
              f"{r['vkp_boxed']}/{len(vkp)}; skip (masked, mean) "
              + " ".join(f"{'M' if m else '-'}{v:.0f}" for m, v in r["masked"]))
    old, new = results[0], results[1]
    check(new["title_diff"] < 6 and old["title_diff"] > new["title_diff"] + 4,
          f"title: the 16:9 frame's middle is the 4:3 frame (fix {new['title_diff']:.1f}, "
          f"control {old['title_diff']:.1f})")
    check(new["card_edge"] > 25, f"memory-card screen: its rows reach the right edge (blue {new['card_edge']:.0f})")
    check(new["card_peek"] == 0 and old["card_peek"] > 10,
          f"memory-card screen: the rows parked beyond the screen stay hidden (text in the right side: fix "
          f"{new['card_peek']}, control {old['card_peek']})")
    check(new["card_ghost"] == 0, f"memory-card screen: nothing left of CAUTION (fix {new['card_ghost']}, "
                                  f"control {old['card_ghost']})")
    check(new["menu_edge"] > 0.5 and old["menu_edge"] < 0.1,
          f"main menu: command rows reach the right edge (fix {new['menu_edge']:.2f}, control {old['menu_edge']:.2f})")
    # (The control's map sides still show the room: only the fix is judged.)
    check(new["map_help"] < w * 6 // 100,
          f"map screen: the controls help starts at the left edge (x {new['map_help']} of {w})")
    check(new["good"] < w * 12 // 100 and w * 16 // 100 < old["good"] < w,
          f"battle: the timing verdict beside the status display at the left (GOOD! from x {new['good']}, "
          f"control {old['good']})")
    check(old["vkp_boxed"] >= len(VKP) // 16 and new["vkp_boxed"] == 0,
          f"VKP Headquarters still fills the view (boxed frames: fix {new['vkp_boxed']}, control {old['vkp_boxed']})")
    masked = [i for i, (m, _) in enumerate(new["masked"]) if m]
    check(len(masked) >= 3 and all(new["masked"][i][1] < 0.5 for i in masked),
          f"skipping the idle cutscene: black while the display is masked ({len(masked)} frames: "
          f"{[round(new['masked'][i][1], 1) for i in masked]})")
    check(any(old["masked"][i][1] > 2 for i in masked if i < len(old["masked"])),
          "control: those frames show VRAM (the mask ignored)")

    print("PASS: widescreen_smoke" if not failures else f"FAIL: widescreen_smoke ({len(failures)} checks)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
