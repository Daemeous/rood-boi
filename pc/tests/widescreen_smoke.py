#!/usr/bin/env python3
"""16:9 widescreen: fades, wipes and full-screen pictures cover the whole view.

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

Two sessions of the same inputs, ROOD_WIDESCREEN_COVER=0 (the old
behaviour, the control) and 1 (the fix), each:

- the title's idle cutscene from the title (the title fading out, the
  cutscene's own fades between shots) and a Start press skipping it, every
  8 vsyncs; the "seam" of each frame is how much stronger the vertical edge
  is on the compressed area's borders (x = 40 and 280 of 320) than in the
  columns around them (1: none);
- Continue into the save room (zone 9 room 17): HUD crop, VRAM read-backs
  over 240 vsyncs in the field (screen copies read the target back; the
  field must not);
- a door transition (RPC room_goto zone 9 room 16, the game's door fade):
  frames every 3 vsyncs; side columns vs the centre;
- the map screen (Triangle, Map): the sides are not the room any more.

Checks: the control shows the bug (seams during the fades; side pixels left
unchanged while the centre fades) so the measures are live; the fix has no
seam at the door, the sides darken with the centre and are black when it is
black, the idle cutscene's fade seams are gone (cutscene frames whose seam
the control had and the fix has not) and no frame gets a seam the control
lacked; the HUD crop is the same in both; the map's sides changed.

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
from rood_rpc import OUT, Game  # noqa: E402
import golden_frames as gf  # noqa: E402

WINDOW = (854, 480)  # 16:9: the view fills the window
GRAPHICS = {**gf.DEFAULTS, "scale": 1, "filter": 0, "aa": False, "widescreen": True, "interpolate": True}
IDLE = range(800, 1600, 8)  # vsyncs after the title is ready: title fade-out, cutscene fades
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


class Session:
    def __init__(self, cover, args, out):
        self.cover, self.out = cover, out / f"cover{cover}"
        self.out.mkdir(parents=True, exist_ok=True)
        card = self.out / "card1.mcd"
        shutil.copyfile(args.card, card)
        self.lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=self.out))
        env = {"ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_LIGHTS": str(self.lights), "ROOD_GI": "0",
               "ROOD_SCALE": "1", "ROOD_WIDESCREEN": "1", "ROOD_WIDESCREEN_COVER": str(cover)}
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
        # ---- the title's idle cutscene, then Start skips it ----
        r["idle"] = []
        for target in IDLE:
            game.run(start + target - game.status()["vsync"])
            r["idle"].append(fade_seam(self.capture(f"idle-{target:04d}")))
        r["idle_zone"] = game.state().get("zone")
        game.call("input", buttons=["start"], vsyncs=4)
        r["skip"] = []
        for i in range(SKIP_FRAMES):
            game.run(2)
            r["skip"].append(fade_seam(self.capture(f"skip-{i:02d}")))
        game.run(300)
        # ---- Continue into the save room ----
        game.run(8 - game.status()["vsync"] % 8)
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
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

    print("PASS: widescreen_smoke" if not failures else f"FAIL: widescreen_smoke ({len(failures)} checks)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
