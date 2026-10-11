#!/usr/bin/env python3
"""The game's cutaway walls (facing masks) under the game camera: the native
room draws the same room groups and the same picture as the packet path at
every camera yaw, during L1/R1 turns and with frame interpolation on.

    python3 pc/tests/facing_cutaway_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Vagrant Story leaves out the walls between its camera and the room: each
wall group's flags carry one bit per 45 degrees of camera yaw, the frame
sets the camera's left/front/right bits (BATTLE func_8008AC78, from the yaw
at scratch 0x1F800058) and func_8009723C draws only the groups that have
all three (Native_Rendering.md "Seeing into rooms"). Continues from a copy
of the card (never the card itself; skipped without one) into its save room
(zone 9 room 17), then turns the camera through its eight yaws with R1 and
checks, at each:

- the facing mask the frame tested is the one the game's yaw gives;
- the groups drawn (RPC room_groups "drawn") are exactly those the mask and
  the room's hide flags keep, the same with graphics native_room 0 (the
  packet path, the original's drawing) and 2 (native meshes), and the
  native list has one instance per drawn group;
- the two paths' pictures match (up to the idle animation between frames);
- while the camera turns, every frame's mask belongs to that frame's yaw
  (or the one just before) and its drawn groups follow it;
- the editor's free camera, once turned off, leaves gameplay with the
  game's mask.

Then (unless --no-room13) it goes through the save room's door with the
door redirected (a RAM write to the room's door table) to zone 9 room 13,
the user's report: at yaw 0xE00 a wall right of Ashley is cut away by the
game's own mask, in both paths (a wall the original hides too).

--out keeps the frames for looking (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import struct
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

FACING = 0x1F800010  # scratch: the facing mask (low byte), first-person view (bit 16)
YAW = 0x1F800058  # scratch: camera.angles.vy
GRAPHICS = {**gf.DEFAULTS, "scale": 2, "interpolate": True,
            "editor_hide_masks": True, "editor_game_cutaway": True, "editor_cutaway": True}


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def mask_for(yaw):
    """func_8008AC78: the camera's left, front and right bits."""
    return ((1 << (((0x9C0 - yaw) & 0xFFF) >> 9)) | (1 << (((0x840 - yaw) & 0xFFF) >> 9))
            | (1 << (((0x900 - yaw) & 0xFFF) >> 9)))


def kept(flags, facing):
    """func_8009723C's group test, the alternate set aside (None: it depends on the call's mode)."""
    if flags & 0x1100:
        return False
    if flags & 0x8000:
        return None
    return bool(facing >> 16) or not ((flags ^ 255) & facing & 255)


def diff(a, b):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)
    return float(d.mean()), float((d > 48).mean())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the frames here")
    parser.add_argument("--no-room13", action="store_true", help="the save room only")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "facing_cutaway"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))  # no per-room lights
    env = {"ROOD_WINDOW": gf.WINDOW, "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "2"}
    os.environ.pop("ROOD_SETTINGS", None)
    game = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    def u32(address):
        return struct.unpack("<I", game.read(address, 4))[0]

    def yaw():
        return u32(YAW) & 0xFFF

    def where():
        s = game.state()
        return s["zone"], s["room"], s.get("player") or {}

    def groups():
        r = game.call("room_groups")
        return r["facing"], r["groups"]

    def mismatched(facing, gs):
        """Groups whose drawn state differs from the mask's verdict."""
        return [i for i, g in enumerate(gs) if kept(g["flags"], facing) is not None
                and g["drawn"] != int(kept(g["flags"], facing))]

    def frame(name, mode):
        game.call("graphics", native_room=mode)
        game.run(gf.SETTLE)
        image = gf.capture(game, out / f"{name}.png")
        if args.out:
            Image.fromarray(image).save(out / f"{name}.png")
        return image, groups(), game.call("render_stats")

    def compare_paths(name):
        """Packet path, native, packet again: the same groups and picture."""
        y = yaw()
        a, (fa, ga), _ = frame(f"{name}-packet", 0)
        b, (fb, gb), sb = frame(f"{name}-native", 2)
        c, _, _ = frame(f"{name}-packet2", 0)
        game.call("graphics", native_room=2)
        noise, _ = diff(a, c)
        mean, share = diff(a, b)
        da = [g["drawn"] for g in ga]
        db = [g["drawn"] for g in gb]
        check(fa & 0x100FF == mask_for(y) and fb == fa,
              f"{name}: yaw {y:#05x} facing {fa:#x}/{fb:#x} (the game's bits {mask_for(y):#x})")
        check(not mismatched(fa, ga) and not mismatched(fb, gb),
              f"{name}: drawn groups follow the mask ({sum(db)} drawn; off: {mismatched(fb, gb)})")
        check(da == db, f"{name}: packet path and native draw the same groups")
        check(sb["instances"] == sum(1 for g in gb if g["drawn"] == 1 and g["polygons"] > 0),
              f"{name}: one native instance per drawn group ({sb['instances']})")
        check(mean <= 2 * noise + 1.0 and share <= 0.02 + 2 * diff(a, c)[1],
              f"{name}: the same picture (mean {mean:.2f}, {share * 100:.2f}% strong; between packet frames "
              f"{noise:.2f})")
        return fb, gb, y

    def turn(name, button="r1"):
        """Turn 45 degrees, checking every frame's mask on the way."""
        start = yaw()
        game.call("input", buttons=[button], vsyncs=4)
        previous, steady, bad, recent = start, 0, [], [start, start]
        for step in range(90):
            game.run(2)
            y = yaw()
            recent = recent[-2:] + [y]  # a frame lasts 2-4 vsyncs: its yaw was read up to two steps ago
            facing, gs = groups()
            if facing & 0xFF not in [mask_for(v) for v in recent] or facing >> 16:
                bad.append((step, y, facing))
            elif mismatched(facing, gs):
                bad.append((step, y, facing, "drawn"))
            steady = steady + 1 if y == previous else 0
            previous = y
            if y != start and steady >= 10:
                break
        check(not bad, f"{name}: turn {start:#05x} -> {yaw():#05x}, every frame's mask follows its yaw {bad[:4]}")
        game.run(30)
        return yaw() != start

    try:
        game.wait_title()
        game.call("graphics", **GRAPHICS)
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        zone, room, player = where()
        if (zone, room) != (9, 17) or not player:
            print(f"FAIL: not in the save room after Continue: zone {zone} room {room}")
            return 1
        _, gs = groups()
        masked = [i for i, g in enumerate(gs) if g["flags"] & 255 not in (0, 255)]
        print(f"save room: {len(gs)} groups, facing-masked {masked}")
        check(len(masked) >= 8, "the save room has facing-masked wall groups")

        yaws = set()
        for k in range(8):
            compare_paths(f"save-{k}")
            yaws.add(round(yaw() / 512) % 8)
            if k < 7 and not turn(f"save-turn-{k}"):
                turn(f"save-turn-{k}-again")
        check(len(yaws) == 8, f"the camera visited eight yaws ({sorted(yaws)})")

        # The editor's free camera off again: gameplay takes the game's mask.
        p = player
        game.call("editor_camera", x=p["x"], y=p["y"] - 1600, z=p["z"] - 900, yaw=0, pitch=-55)
        game.run(gf.SETTLE)
        free, _ = groups()
        game.call("editor_camera", off=1)
        game.run(gf.SETTLE)
        facing, gs = groups()
        check(facing & 0x100FF == mask_for(yaw()) and not mismatched(facing, gs),
              f"after the free camera ({free:#x}): the game's mask {facing:#x} decides again")

        if not args.no_room13:
            # Back to the yaw Continue starts with (the walk below is camera-relative).
            for _ in range(8):
                if abs(yaw() - 0x200) < 4:
                    break
                turn("save-turn-back")
            # The save room's door to room 12 (index 2) leads to room 13 instead.
            data = symbols()["vs_battle_roomData"]
            doors = u32(data + 0x70)
            entry = bytearray(game.read(doors + 2 * 12, 12))
            if entry[1] != 12:
                check(False, f"the save room's third door leads to room 12 ({entry[1]})")
            else:
                entry[1] = 13
                u4, u6 = struct.unpack_from("<HH", entry, 4)
                struct.pack_into("<HH", entry, 4, (u4 & 0x80FF) | (5 << 8), (u6 & 0x83FF) | (8 << 10))  # cell 5, 8
                game.write(doors + 2 * 12, bytes(entry))
                game.call("graphics", native_room=2)
                game.hold("left", 12)
                game.run(5)
                game.hold("up", 33)
                game.run(5)
                for _ in range(12):
                    game.press("cross", vsyncs=3, settle=6)  # (leave battle mode)
                    game.hold(["up", "left"], 30)
                    game.run(5)
                    if where()[1] != 17:
                        break
                game.run(250)
                zone, room, player = where()
                if (zone, room) != (9, 13):
                    check(False, f"reached zone 9 room 13 through the redirected door (zone {zone} room {room})")
                else:
                    print(f"room 13: Ashley at {player['x']} {player['y']} {player['z']}")
                    for _ in range(8):
                        if yaw() == 0xE00 or abs(yaw() - 0xE00) < 4:
                            break
                        turn("room13-turn")
                    facing, gs, y = compare_paths("room13-e00")  # (hits on Ashley can nudge the camera after)
                    cut = [i for i, g in enumerate(gs) if kept(g["flags"], facing) is False and not g["flags"] & 0x1100]
                    check(mask_for(y) == 0x20 and facing == 0x20 and cut,
                          f"room 13 at yaw {y:#05x}: the game's own mask {facing:#x} cuts groups {cut}")
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
    print("PASS: the native room keeps the game's cutaway walls at every yaw, as the packet path does")
    return 0


if __name__ == "__main__":
    sys.exit(main())
