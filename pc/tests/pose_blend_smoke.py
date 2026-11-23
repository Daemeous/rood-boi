#!/usr/bin/env python3
"""Ashley's slowed stride drawn between its animation ticks (anim_blend; pc/src/pose_blend.c).

    python3 pc/tests/pose_blend_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Under analog movement her stride plays at the pace of her speed, one whole animation tick at a time with the fraction
carried (analog_move.c), so at 30% of the stick the pose changes only every few vsyncs. anim_blend draws her at the
fraction between the pose the game holds and the one the next tick brings (draw only). From a copy of the card (skipped
without one), warped to the Crumbling Market (zone 48 room 21), the stick held at 30% from the same marked moment with
anim_blend 0 and 100:

- the control (0): the drawn pose repeats for several images between changes (plateaus of 3 or more)
- anim_blend 100: the drawn pose (anim_trace: the bones relative to the root) changes on every image, no plateau of 2
- the bones keep their lengths (the pose is built from blended angles, not matrices): the drawn pose's bone offsets
  are as long as the control's, within 1%
- game state is bit-identical on and off (snapshot hash after the hold, and her position and animation frame)
- a 50% blend is between the two (its images' pose step is smaller than the full blend's)
- the full push (no slowed stride) draws exactly as the control: the blend is off at the tick rate
- shots (--out): eight consecutive images at 30% stick, off and on, as two strips (strip_off.png, strip_on.png)"""
import argparse
import pathlib
import subprocess
import sys

import numpy as np

from analog_common import Checks, ROOT, default_card, field_game, prepare, stick_at, warp

sys.path.insert(0, str(ROOT / "pc/tools"))
from anim_trace import EPS, displacement, local_pose, read  # noqa: E402

ROOM = (48, 21)
HEADING = 160
HOLD = 60
STRIP = 8


def ashley_poses(frames):
    """The drawn local poses (bones relative to the root) of the character instance that moves most, one per image."""
    best = None
    by_mesh = {}
    for f in frames:
        seen = {}
        for inst in f["instances"]:
            k = (inst["mesh"], seen.get(inst["mesh"], 0))
            seen[inst["mesh"]] = k[1] + 1
            if inst["bones"] >= 15:
                by_mesh.setdefault(k, []).append((local_pose(inst["drawn"]), inst["drawn"][0, 9:].copy()))
    for k, both in by_mesh.items():
        if len(both) < HOLD // 2:
            continue
        motion = sum(steps([p for p, _ in both]))
        if best is None or motion > best[0]:
            best = (motion, both)
    return ([p for p, _ in best[1]], [r for _, r in best[1]]) if best else ([], [])


def steps(poses):
    """The pose change from each image to the next, as a vertex displacement in world units (anim_trace.displacement)."""
    return [displacement(a, b) for a, b in zip(poses, poses[1:])]


def plateaus(deltas, eps):
    """The longest run of images whose pose did not change."""
    best = run = 0
    for d in deltas:
        run = run + 1 if d <= eps else 0
        best = max(best, run)
    return best


def bone_lengths(poses):
    return np.mean([np.linalg.norm(p[:, 9:], axis=1) for p in poses], axis=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    ready = prepare(args.card, args.out, "pose_blend")
    if not ready:
        return 77
    out, card = ready
    check = Checks()
    game = field_game(out, card)
    try:
        check(warp(game, *ROOM), f"warped to zone {ROOM[0]} room {ROOM[1]}")
        game.call("settings", apply=["analog_move 1", "analog_dead_zone 20", "analog_pace 1", "analog_curve 100",
                                     "analog_min_speed 10", "analog_anim_rate 100"])
        game.run(30)
        game.mark()

        def walk(blend, travel, shots=None):
            """The hold from the marked moment: (drawn poses, state hash, player, frame); `shots`: STRIP consecutive
            images instead of the trace (the same hold, one vsync at a time)."""
            game.rewind()
            game.call("settings", apply=[f"anim_blend {blend}"])
            game.call("input", vsyncs=16, left_stick=stick_at(HEADING, travel))
            game.run(16)
            path = out / f"trace_{blend}_{round(travel * 100)}.bin"
            if shots:
                for i in range(20 + STRIP):
                    game.call("input", vsyncs=1, left_stick=stick_at(HEADING, travel))
                    game.run(1)
                    if i >= 20:
                        game.present_screenshot(out / f"{shots}_{i - 20}.png")
                return ([], []), "", (0, 0), 0
            game.call("anim_trace", path=str(path), count=HOLD)
            game.call("input", vsyncs=HOLD + 4, left_stick=stick_at(HEADING, travel))
            game.run(HOLD + 4)
            left = game.call("anim_trace")["remaining"]
            if left:
                game.run(left + 2)
            poses, roots = ashley_poses(read(path))
            digest = game.call("snapshot", action="hash")
            p = game.state()["player"]
            return (poses, roots), digest, (p["x"], p["z"]), game.value("vs_actors_actorRecords[0]->unkC54.unk540")

        walk(0, 0.3, shots="off")
        walk(100, 0.3, shots="on")
        (off, roots_off), hash_off, at_off, frame_off = walk(0, 0.3)
        (on, roots_on), hash_on, at_on, frame_on = walk(100, 0.3)
        (half, _), _, _, _ = walk(50, 0.3)
        check(len(off) >= HOLD // 2 and len(on) >= HOLD // 2, f"her drawn pose traced ({len(off)} and {len(on)} images)")

        d_off, d_on = steps(off), steps(on)
        eps = EPS
        print("steps off:", " ".join(f"{d:.2f}" for d in d_off[:40]), flush=True)
        print("steps on: ", " ".join(f"{d:.2f}" for d in d_on[:40]), flush=True)
        check(plateaus(d_off, eps) >= 3, f"control (anim_blend 0): the drawn pose repeats for up to {plateaus(d_off, eps)} images")
        check(plateaus(d_on, eps) <= 1, f"anim_blend 100: the drawn pose changes on every image (longest plateau {plateaus(d_on, eps)})")
        changed = sum(1 for d in d_on if d > eps) / max(len(d_on), 1)
        check(changed >= 0.95, f"anim_blend 100: {changed * 100:.0f}% of the image-to-image steps move the pose")
        n = min(len(off), len(on), len(half))
        apart = lambda a, b: float(np.mean([displacement(x, y) for x, y in zip(a[:n], b[:n])]))
        check(apart(half, off) > 1.0 and apart(half, on) > 1.0,
              f"anim_blend 50 draws a pose of its own ({apart(half, off):.1f} from the control's, {apart(half, on):.1f} from the full blend's)")
        l_off, l_on = bone_lengths(off), bone_lengths(on)
        worst = float(np.max(np.abs(l_on - l_off) / np.maximum(l_off, 1e-3)))
        check(worst < 0.01, f"bones keep their lengths with the blend (worst {worst * 100:.2f}% off the control's)")
        for name, roots in (("off", roots_off), ("on", roots_on)):
            moves = [float(np.abs(a - b).max()) for a, b in zip(roots, roots[1:])]
            check(max(moves) <= 3 * max(float(np.median(moves)), 1.0), f"anim_blend {name}: she is drawn where she is, moving steadily "
                  f"(largest image step {max(moves):.1f}, median {np.median(moves):.1f} units)")
        check(hash_on == hash_off, "game state is bit-identical with anim_blend 0 and 100 (snapshot hash)")
        check(at_on == at_off and frame_on == frame_off, f"her position {at_on} and animation frame {frame_on} are the same")

        (full_off, _), _, _, _ = walk(0, 1.0)
        (full_on, _), _, _, _ = walk(100, 1.0)
        same = len(full_off) == len(full_on) and all(np.allclose(a, b, atol=1e-6) for a, b in zip(full_off, full_on))
        check(same, "a full push (the stride at the tick rate) draws the same with the blend on and off")

        for name in ("off", "on"):
            files = [out / f"{name}_{i}.png" for i in range(STRIP)]
            if all(f.exists() for f in files):
                subprocess.run(["montage", *map(str, files), "-tile", f"{STRIP}x1", "-geometry", "+2+2",
                                str(out / f"strip_{name}.png")], check=False, stderr=subprocess.DEVNULL)
    finally:
        game.close()
    return check.finish("pose_blend_smoke")


if __name__ == "__main__":
    sys.exit(main())
