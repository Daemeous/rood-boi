#!/usr/bin/env python3
"""Room texture animations are animated materials on the native room (the
user's report: the save point's sigil on the floor stood still).

    python3 pc/tests/texture_animation_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Continues from a copy of the card (never the card itself; skipped without
one) into its save room (zone 9 room 17). The room's texture effects
(BATTLE 2842C.c, room section 0xC) scroll the UVs of the save point's
polygons (func_80091E10: the sigil and its light column). The native room
draws them as animated materials (room_effects.c): its meshes keep the base
UVs and each frame's draw list carries the offsets the game wrote, so no
mesh is rewritten.

Three runs of the same session (the same inputs on the same vsync phase;
frames are matched by the effects' offsets and the camera):

- A: animated materials (the default);
- B: ROOD_UV_EFFECTS=0, the meshes re-decoded whenever the game rewrites the
  polygons (the behaviour before animated materials, following the bytes the
  packet path draws);
- C: the packet path (graphics native_room 0).

Checks:

- A models the effects (RPC uv_effects), they move, and no mesh is rewritten
  (render_stats mesh_updates stays; uv_effects counts the table);
- B rewrites meshes (the fallback still works);
- a box around the save point's ring below Ashley's feet (the sigil
  animating, nothing else moving) is pixel-identical in A, B and C on
  matched frames (when no frames match, as when a run's camera came to rest
  elsewhere: the 32-bit build does not replay sessions exactly, the
  comparison is skipped with a note);
- overrides: freeze holds the drawn offset while the game's moves; speed 2
  moves it twice as far;
- frame interpolation (present_rate 2; RPC uv_effects "drawn": the first
  effect's offset per draw): interp_uv 0 draws whole texels only; 1 draws
  between them on in-between images and whole texels on game frames; 2
  (smooth) moves on nearly every draw; never backwards across the scroll's
  wrap;
- the ray-traced scene follows (uv_effects scene_uv_serial grows; with gi
  on, rt_uv_updates: triangle records rewritten for the path tracer).

--out keeps frames for looking (game imagery: keep it out of git)."""
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

FRAMES = 16  # captured frames per run (every game frame: 2 vsyncs apart)
WINDOW = "640x480"  # 320x240 at scale 1, nearest
# Around the save point's ring below Ashley's feet (window pixels).
RING = (slice(314, 336), slice(230, 360))
GRAPHICS = {**gf.DEFAULTS, "scale": 1, "filter": 0, "aa": False, "interpolate": False}


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def changed(a, b):
    return np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=-1) > 0


class Run:
    """One session into the save room; frames keyed by the effects' offsets
    (the 18 effects' positions cycle over hundreds of frames) and the camera
    the picture was drawn with."""

    def __init__(self, name, args, out, env, native_room=2):
        self.name, self.out = name, out / name
        self.out.mkdir(parents=True, exist_ok=True)
        card = self.out / "card1.mcd"
        shutil.copyfile(args.card, card)
        self.lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=self.out))  # no per-room lights
        full_env = {"ROOD_WINDOW": WINDOW, "ROOD_LIGHTS": str(self.lights), "ROOD_GI": "0", "ROOD_SCALE": "1", **env}
        os.environ.pop("ROOD_SETTINGS", None)
        self.game = Game.launch(fast=True, card=str(card), log=self.out / "game.log", extra_env=full_env)
        self.native_room = native_room
        self.keep = args.out is not None

    def enter(self):
        game = self.game
        game.wait_title()
        game.call("graphics", **GRAPHICS, native_room=self.native_room)
        # Inputs on a fixed phase of the vsync count (the game draws every
        # 2-4 vsyncs; golden_frames.py).
        game.run(8 - game.status()["vsync"] % 8)
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        return state.get("player") and (state.get("zone"), state.get("room")) == (9, 17), state

    def capture(self):
        """FRAMES game frames as [(offsets, image)] and render_stats before/after."""
        game = self.game
        frames = []
        before = game.call("render_stats")
        for i in range(FRAMES):
            image = gf.capture(game, self.out / "frame")
            effects = game.call("uv_effects")["effects"]
            camera = game.call("scene").get("camera") or {}
            pose = tuple(round(v, 3) for row in camera.get("rotation", []) for v in row) + tuple(
                round(v) for v in camera.get("translation", []))
            key = (tuple(tuple(e["game_offset"]) for e in effects), pose)
            # (the effects step every few frames: the frames of one step in order)
            frames.append(((*key, sum(k[:2] == key for k, _ in frames)), image))
            if self.keep:
                Image.fromarray(image).save(self.out / f"frame-{i:02d}.png")
            game.run(2)
        after = game.call("render_stats")
        return frames, before, after

    def close(self):
        self.game.close()
        shutil.rmtree(self.lights, ignore_errors=True)
        log = pathlib.Path(self.game.log).read_text(errors="replace")
        return not ("[crash]" in log or "[gl] shader" in log or "[vk] validation:" in log)


def compare(name, frames_a, frames_x, check):
    """The box around the ring on matched frames (same offsets and camera)."""
    other = dict(frames_x)
    matched = [(image, other[key]) for key, image in frames_a if key in other]
    differing = [int(changed(x[RING], y[RING]).sum()) for x, y in matched]
    size = (RING[0].stop - RING[0].start) * (RING[1].stop - RING[1].start)
    print(f"{name}: {len(matched)} matched frames; pixels differing around the ring {differing} of {size}")
    if not matched:
        print(f"note: {name}: no frame matched (this build does not replay sessions exactly); not compared")
    else:
        check(max(differing) == 0, f"{name}: the save point's ring is pixel-identical on {len(matched)} frames")


def interpolation(game, check):
    """RPC uv_effects "drawn" (the first effect's offset per draw) under
    frame interpolation for each interp_uv mode."""
    game.call("graphics", interpolate=True, present_rate=2)
    period = game.call("uv_effects")["effects"][0]["period"][1]
    drawn = {}
    for mode in (0, 1, 2):
        game.call("graphics", interp_uv=mode)
        game.run(4)
        game.call("uv_effects")  # (clears the trace)
        game.run(80)  # past a wrap of the first effect (every 18 steps of 4 vsyncs)
        drawn[mode] = game.call("uv_effects")["drawn"]
        # The in-between images in order (a game frame is drawn live, t 1,
        # before the in-betweens leading to it are presented).
        between = [row for row in drawn[mode] if row[0] < 1]
        steps, wraps = [], 0
        for (_, _, a), (_, _, b) in zip(between, between[1:]):
            d = b - a
            if period and d < -period / 2:
                d += period  # the scroll wrapped
                wraps += 1
            steps.append(round(d, 3))
        drawn[mode] = (drawn[mode], steps, wraps)
        print(f"interp_uv {mode}: v drawn {[round(v, 2) for _, _, v in drawn[mode][0][:40]]}... ({wraps} wraps)")
    game.call("graphics", interpolate=False, present_rate=1, interp_uv=1)

    def fractional(rows):
        return [abs(v - round(v)) > 1e-3 for _, _, v in rows]

    rows0, _, _ = drawn[0]
    rows1, steps1, wraps1 = drawn[1]
    rows2, steps2, wraps2 = drawn[2]
    check(len(rows0) >= 8 and not any(fractional(rows0)), "interp_uv 0: whole texels only (steps with the game)")
    live = [f for (t, _, _), f in zip(rows1, fractional(rows1)) if t >= 1]
    check(any(fractional(rows1)) and live and not any(live),
          "interp_uv 1: in-between draws between texels, game frames on whole texels")
    moving = sum(abs(d) > 1e-3 for d in steps2) / max(len(steps2), 1)
    check(moving >= 0.75, f"interp_uv 2 (smooth): the offset moves on nearly every in-between image ({moving:.0%})")
    backwards = [d for d in steps1 + steps2 if d < -1e-3 or d > 1.01]
    check(not backwards and steps2 and wraps1 and wraps2,
          f"in-between offsets never run backwards or jump across the wrap ({wraps1 + wraps2} wraps; {backwards})")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the frames here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "texture_animation"
    out.mkdir(parents=True, exist_ok=True)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    # ---- A: animated materials, with the overrides, interpolation and RT checks ----
    a = Run("animated", args, out, {})
    try:
        entered, state = a.enter()
        if not entered:
            print(f"FAIL: not in the save room (zone 9 room 17) after Continue: {state}")
            a.close()
            return 1
        game = a.game
        effects = game.call("uv_effects")["effects"]
        modelled = [e for e in effects if e["modelled"]]
        print(f"zone 9 room 17: {len(effects)} UV effects, {len(modelled)} modelled "
              f"(types {sorted(set(e['type'] for e in effects))})")
        check(len(modelled) >= 1 and len(modelled) == len(effects), "the save room's UV effects are animated materials")
        frames_a, before, after = a.capture()
        moving = len(set(o for o, _ in frames_a))
        check(moving >= FRAMES // 4, f"the effects move ({moving} distinct offsets in {FRAMES} frames)")
        updates = after["mesh_updates"] - before["mesh_updates"]
        check(updates == 0, f"no native mesh is rewritten ({updates} mesh updates)")
        check(after["uv_effects"] == len(effects), f"the room list carries the effects' table ({after['uv_effects']})")
        # Overrides (effect 1): freeze holds the drawn offset; speed 2 doubles the motion.
        game.call("uv_effects", effect=1, freeze=True)
        game.run(2)
        held = game.call("uv_effects")["effects"][0]
        game.run(12)
        later = game.call("uv_effects")["effects"][0]
        check(later["offset"] == held["offset"] and later["game_offset"] != held["game_offset"],
              f"freeze holds the drawn offset ({held['offset']} -> {later['offset']}) while the game's moves "
              f"({held['game_offset']} -> {later['game_offset']})")
        game.call("uv_effects", effect=1, speed=2, freeze=False)
        game.run(2)
        start = game.call("uv_effects")["effects"][0]
        drawn = game_moved = 0
        for _ in range(8):
            game.run(2)
            e = game.call("uv_effects")["effects"][0]
            drawn += abs(e["shift"][1])
            game_moved += 1 if e["game_offset"] != start["game_offset"] else 0
            start = e
        check(drawn > 0 and game_moved > 0 and abs(drawn - 2 * game_moved) <= 2,
              f"speed 2 moves the drawn offset twice as far ({drawn:g} texels over {game_moved} game steps)")
        game.call("uv_effects", effect=1, speed=1, freeze=False)
        game.run(2)
        e = game.call("uv_effects")["effects"][0]
        check(e["offset"] == e["game_offset"], "without overrides the drawn offset is the game's")

        # Frame interpolation.
        interpolation(game, check)

        # The ray-traced scene follows the drawn UVs.
        game.call("graphics", gi=True)
        game.run(4)
        rt0 = game.call("uv_effects")
        game.run(12)
        rt1 = game.call("uv_effects")
        check(rt1["scene_uv_serial"] > rt0["scene_uv_serial"],
              f"the scene capture's UVs follow ({rt0['scene_uv_serial']} -> {rt1['scene_uv_serial']})")
        if rt1["rt_uv_updates"] == 0:
            print("note: the path tracer did not run (no GI here); its triangle records are not checked")
        else:
            check(rt1["rt_uv_updates"] > rt0["rt_uv_updates"],
                  f"the path tracer's triangle UVs follow ({rt0['rt_uv_updates']} -> {rt1['rt_uv_updates']} records)")
        game.call("graphics", gi=False)
    finally:
        clean = a.close()
    check(clean, "A: no crash or shader error")

    # ---- B: the legacy path (meshes re-decoded) ----
    b = Run("redecode", args, out, {"ROOD_UV_EFFECTS": "0"})
    try:
        entered, _ = b.enter()
        frames_b, before, after = b.capture() if entered else ([], {}, {})
        updates = after.get("mesh_updates", 0) - before.get("mesh_updates", 0)
        check(entered and updates > 0, f"with ROOD_UV_EFFECTS=0 the meshes are re-decoded ({updates} updates)")
    finally:
        check(b.close(), "B: no crash or shader error")

    # ---- C: the packet path ----
    c = Run("packets", args, out, {}, native_room=0)
    try:
        entered, _ = c.enter()
        frames_c, _, _ = c.capture() if entered else ([], {}, {})
        check(entered, "C: in the save room")
    finally:
        check(c.close(), "C: no crash or shader error")

    compare("animated materials vs re-decoded meshes", frames_a, frames_b, check)
    compare("animated materials vs the packet path", frames_a, frames_c, check)
    if failures:
        print(f"FAIL: {len(failures)} checks; frames in {out}" if args.out else f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: room texture animations are exact animated materials on the native room")
    return 0


if __name__ == "__main__":
    sys.exit(main())
