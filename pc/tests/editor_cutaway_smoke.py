#!/usr/bin/env python3
"""The editor's free camera sees into rooms (room_editor.h): the game's
cutaway walls for the free camera's direction, the black shell groups left
out, and the cutaway between the eye and a focus point.

    python3 pc/tests/editor_cutaway_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Continues from a copy of the card (never the card itself; skipped without
one) into its save room (the user's report: zone 9 room 17, where the
free camera's view from above was covered by a black slab and black wall
tops). Checks:

- the room's black shell groups are found (RPC room_groups);
- the game camera draws the same room groups with the editor switches on
  and off, and the same picture (up to the idle animation between frames):
  gameplay rendering is unchanged;
- from the user's pose above the room, the shells are left out (fewer
  groups; the black slab over the left of the view is gone);
- from a low pose behind props, the cutaway alone cuts the surfaces between
  the eye and the focus (Ashley), only around the line of sight, and draws
  them as a ghost (part of their pixels and their edges stay; opacity 0 is
  the old hole);
- from inside the room with the focus beyond a wall, the cut no longer
  opens onto the black void: the ghost is well brighter than the hole;
- the cut follows whether the focus is hidden: none where Ashley is in plain
  view (the same picture as without it), with hysteresis when it turns;
- per-group hiding (room_groups hide/show_all) removes a group and brings
  it back.

--out keeps the frames for looking (game imagery: keep it out of git)."""
import argparse
import math
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

OFF = {"editor_hide_masks": False, "editor_game_cutaway": False, "editor_cutaway": False}
ON = {"editor_hide_masks": True, "editor_game_cutaway": True, "editor_cutaway": True, "editor_cutaway_radius": 320,
      "editor_cutaway_opacity": 0.3, "editor_cutaway_auto": True}


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def pose_looking_at(eye, target):
    d = [target[i] - eye[i] for i in range(3)]
    yaw = math.degrees(math.atan2(d[0], d[2]))
    pitch = math.degrees(math.asin(-d[1] / math.sqrt(sum(x * x for x in d))))
    return dict(x=eye[0], y=eye[1], z=eye[2], yaw=yaw, pitch=pitch)


def diff(a, b):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)
    return float(d.mean()), float((d > 32).mean())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the frames here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "editor_cutaway"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))  # no per-room lights
    env = {"ROOD_WINDOW": gf.WINDOW, "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "2"}
    os.environ.pop("ROOD_SETTINGS", None)
    game = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env)
    failures = []
    stats = {}

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message)
        if not ok:
            failures.append(message)

    def shot(name, graphics, pose=None):
        game.call("graphics", **{**gf.DEFAULTS, "scale": 2, **graphics})
        if pose:
            game.call("editor_camera", **pose)
        else:
            game.call("editor_camera", off=1)
        game.run(gf.SETTLE)
        image = gf.settled(game, out / f"{name}.png")
        stats[name] = game.call("render_stats")
        if args.out:
            Image.fromarray(image).save(out / f"{name}.png")
        return image

    try:
        game.wait_title()
        game.call("graphics", **{**gf.DEFAULTS, "scale": 2})
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        player = state.get("player")
        if not player or not game.call("scene")["triangles"]:
            print(f"FAIL: no room with Ashley after Continue: {state}")
            return 1
        print(f"zone {state['zone']} room {state['room']}, Ashley at {player['x']} {player['y']} {player['z']}")
        focus = (player["x"], player["y"] - 80, player["z"])  # editor_view.cpp's automatic focus

        groups = game.call("room_groups")["groups"]
        shells = [i for i, g in enumerate(groups) if g["black"]]
        print(f"{len(groups)} groups, black shells {shells}")
        check(len(shells) > 0, "the room has black shell groups")

        # Gameplay: the game camera's frame does not depend on the switches.
        off1 = shot("game-off", OFF)
        on = shot("game-on", ON)
        off2 = shot("game-off2", OFF)
        noise, _ = diff(off1, off2)
        mean, share = diff(on, off2)
        check(mean <= 2 * noise + 0.3, f"game camera: the same picture (mean {mean:.2f}; idle animation {noise:.2f})")
        check(stats["game-off"]["instances"] == stats["game-on"]["instances"],
              f"game camera: the same room groups ({stats['game-off']['instances']}, {stats['game-on']['instances']})")
        check(stats["game-off"]["native_triangles"] == stats["game-on"]["native_triangles"],
              f"game camera: the same room triangles ({stats['game-off']['native_triangles']}, "
              f"{stats['game-on']['native_triangles']})")

        # The user's pose above the room (their editor view started there).
        game.call("editor_view", focus=None)
        above = pose_looking_at((player["x"] + 696, player["y"] - 945, player["z"] - 642), focus)
        before = shot("above-off", OFF, above)
        after = shot("above-on", ON, above)
        h, w = before.shape[:2]
        left = (slice(h // 8, h * 7 // 8), slice(w // 16, w * 3 // 8))
        dark_before, dark_after = float(before[left].max(axis=2).mean()), float(after[left].max(axis=2).mean())
        check(stats["above-on"]["instances"] < stats["above-off"]["instances"],
              f"above: fewer room groups ({stats['above-off']['instances']} -> {stats['above-on']['instances']})")
        check(dark_after > dark_before + 10,
              f"above: the black slab over the left of the view is gone (brightness {dark_before:.1f} -> {dark_after:.1f})")

        # Low, behind the props: the cutaway alone.
        low = pose_looking_at((player["x"] + 452, player["y"] - 150, player["z"] + 724), focus)
        plain = shot("low-off", OFF, low)
        cut = shot("low-cut", {**OFF, "editor_cutaway": True, "editor_cutaway_radius": 320}, low)
        centre = (slice(h * 3 // 8, h * 5 // 8), slice(w * 3 // 8, w * 5 // 8))
        corner = (slice(h // 8, h // 3), slice(0, w // 8))  # the wall left of the props
        _, cshare = diff(plain[centre], cut[centre])
        far, _ = diff(plain[corner], cut[corner])
        check(stats["low-cut"]["instances"] == stats["low-off"]["instances"],
              "cutaway: the same groups drawn (it cuts pixels, not groups)")
        check(cshare > 0.3, f"cutaway: what stands before the focus is cut ({cshare * 100:.0f}% of the centre)")
        check(far < 3.0, f"cutaway: away from the line of sight little changes (mean {far:.2f})")
        view = game.call("editor_view")
        check(view["focus"] == [float(v) for v in focus], f"cutaway focus is Ashley ({view['focus']})")
        check(view["hidden"] == 1 and view["amount"] == 1, f"behind the props Ashley is hidden: cut ({view})")
        # The ghost against the old hole: part of the props stays drawn.
        hole = shot("low-hole", {**OFF, "editor_cutaway": True, "editor_cutaway_radius": 320,
                                 "editor_cutaway_opacity": 0}, low)
        kept_ghost = float((np.abs(cut[centre].astype(np.int16) - plain[centre].astype(np.int16)).max(axis=2) < 6).mean())
        kept_hole = float((np.abs(hole[centre].astype(np.int16) - plain[centre].astype(np.int16)).max(axis=2) < 6).mean())
        check(kept_ghost > kept_hole + 0.1,
              f"cutaway ghost: part of the cut surfaces stays ({kept_ghost * 100:.0f}% of the centre as before, "
              f"{kept_hole * 100:.0f}% with a hole)")

        # Inside the room, the focus beyond a wall: the hole showed the void.
        bounds = game.call("scene")["bounds"]
        eye = (player["x"], player["y"] - 150, player["z"])
        beyond = (player["x"], player["y"] - 100, bounds[1][2] + 300)
        game.call("editor_view", focus=list(beyond))
        inside = pose_looking_at(eye, beyond)
        void = shot("void-hole", {**ON, "editor_cutaway_opacity": 0}, inside)
        ghost = shot("void-ghost", ON, inside)
        bright_hole = float(void[centre].max(axis=2).mean())
        bright_ghost = float(ghost[centre].max(axis=2).mean())
        check(game.call("editor_view")["hidden"] == 1, "the focus beyond the wall is hidden")
        check(bright_ghost > bright_hole * 2 + 4,
              f"through a wall: a ghost, not the black void (centre brightness {bright_hole:.1f} -> {bright_ghost:.1f})")
        if args.out:  # with the editor's overlays (the focus marker), for looking
            game.call("editor_ui", visible=True)
            game.run(gf.SETTLE)
            Image.fromarray(gf.settled(game, out / "void-editor.png")).save(out / "void-editor.png")
            game.call("editor_ui", visible=False)

        # Ashley in plain view (from above, the near walls cut by the game's
        # own cutaway): nothing ghosted, the same picture as without it.
        game.call("editor_view", focus=None)
        plain_above = shot("above-nocut", {**ON, "editor_cutaway": False}, above)
        seen = shot("above-seen", ON, above)
        view = game.call("editor_view")
        mean, _ = diff(plain_above, seen)
        check(view["hidden"] == 0 and view["amount"] == 0, f"Ashley in plain view: nothing cut ({view})")
        check(mean <= 2 * noise + 0.3, f"in plain view the picture is as without the cut (mean {mean:.2f})")
        # Hysteresis: hidden at once, seen again only after a while.
        shot("low-again", ON, low)
        check(game.call("editor_view")["amount"] == 1, "back behind the props: cut again")
        game.call("editor_camera", **above)
        game.run(4)
        held = game.call("editor_view")
        game.run(120)
        released = game.call("editor_view")
        check(held["amount"] > 0 and released["hidden"] == 0 and released["amount"] == 0,
              f"seen again: the cut holds a moment, then fades ({held['amount']} -> {released['amount']})")

        # Per-group hiding (the Outliner's checkboxes).
        # The largest group shown from every direction (facing mask 0xff: floors, props).
        biggest = max((i for i, g in enumerate(groups)
                       if not g["black"] and not g["room_hides"] and g["flags"] & 255 == 255),
                      key=lambda i: groups[i]["polygons"])
        shown = shot("hide-none", ON, above)
        game.call("room_groups", hide=[biggest])
        hid = shot("hide-group", ON, above)
        check(game.call("room_groups")["groups"][biggest]["hidden"] == 1, f"group {biggest} hidden")
        check(stats["hide-group"]["instances"] == stats["hide-none"]["instances"] - 1,
              f"hiding group {biggest} drops one instance")
        changed = float((np.abs(shown.astype(np.int16) - hid.astype(np.int16)).max(axis=2) > 8).mean())
        check(changed > 0.005, f"hiding group {biggest} changes the frame ({changed * 100:.1f}% of the pixels)")
        game.call("room_groups", show_all=True)
        shot("hide-restored", ON, above)
        check(stats["hide-restored"]["instances"] == stats["hide-none"]["instances"], "show_all restores it")
        game.call("editor_camera", off=1)
    finally:
        game.close()
        shutil.rmtree(lights, ignore_errors=True)
    log = pathlib.Path(game.log).read_text(errors="replace")
    if "[crash]" in log or "[gl] shader" in log or "[vk] validation:" in log:
        print("FAIL: crash or shader error; see", game.log)
        return 1
    if failures:
        print(f"FAIL: {len(failures)} checks; frames in {out}")
        return 1
    print("PASS: the free camera sees into the room; the game camera's picture is unchanged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
