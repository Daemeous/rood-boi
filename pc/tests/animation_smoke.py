"""Character animation in-betweens (frame interpolation of bone palettes).

The game animates characters at its own frame rate (30 Hz: one animation
tick per game frame) and the renderer draws in-betweens by blending each
model's bone palette from the frame before (render.c instance_palette).
RPC anim_trace records what was drawn per presented image;
pc/tools/anim_trace.py classifies it.

1. Held poses (New Game, the opening's second scene, vsync ~4000): the
   characters' idle motion changes only every other game frame (15 Hz).
   Blended frame to frame, every other game frame stands still: about 30
   distinct poses per second (the control, interp_holds 0). With the hold
   spread over both frames (interp_holds 1, default): about 60.
2. Fast rotations (--card: Continue from the save, battle mode and an
   attack): a swing turns bones by up to ~95 degrees per game frame. Linear
   blending of the matrices shortens such bones halfway by up to ~30% (the
   control, interp_skeleton 0); rotations slerped through the skeleton
   (interp_skeleton 1, default) keep their length, and the weapon stays in
   the hand.

    python3 pc/tests/animation_smoke.py [--card pc/save/card1.mcd]
"""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from anim_trace import analyse, read  # noqa: E402
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402

failures = []


def check(ok, message):
    print(("PASS: " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)


def trace(game, name, count, action=None):
    path = OUT / f"animation-{name}.bin"
    game.call("anim_trace", path=str(path), count=count)
    if action:
        action()
    left = game.call("anim_trace")["remaining"]
    if left:
        game.run(left + 2)
    report = analyse(read(path))
    path.unlink()
    return report


def held_poses():
    game = launch_at("opening", fast=True)  # New Game, after the opening pan (checkpoints.py)
    try:
        game.run(4000 - game.status()["vsync"])
        game.call("graphics", interp_holds=False)
        game.run(4)
        stepped = trace(game, "holds-off", 100, lambda: game.run(100))
        game.call("graphics", interp_holds=True)
        game.run(4)
        smooth = trace(game, "holds-on", 100, lambda: game.run(100))
    finally:
        game.close()
    assert "[crash]" not in pathlib.Path(game.log).read_text(errors="replace")
    held = [k for k, r in stepped["instances"].items() if r["held_fraction"] > 0.3 and r["moving_fraction"] > 0.2]
    check(len(held) >= 2, f"control: characters hold their idle pose every other game frame ({len(held)} instances)")
    for k in held:
        before = stepped["instances"][k].get("effective_fps", 0)
        after = smooth["instances"].get(k, {}).get("effective_fps", 0)
        check(before < 40 and after > 50,
              f"{k}: distinct poses per second {before:.0f} stepped -> {after:.0f} with held poses blended")


def fast_rotations(card):
    scratch = OUT / "animation-card.mcd"
    shutil.copy(card, scratch)
    game = launch_at("save_room", card=scratch, fast=True)  # Continue from the card (checkpoints.py)

    def attack():
        game.press("circle", 3, 40)  # battle mode: the weapon is drawn
        game.press("circle", 3, 60)  # attack
        game.press("circle", 3, 40)
        game.press("cross", 3, 40)  # back out of battle mode

    try:
        game.call("graphics", interp_skeleton=False)
        linear = trace(game, "linear", 180, attack)
        game.call("graphics", interp_skeleton=True)
        game.run(30)
        skeleton = trace(game, "skeleton", 180, attack)
    finally:
        game.close()
    assert "[crash]" not in pathlib.Path(game.log).read_text(errors="replace")
    shrink = max((r["max_shrink"] for r in linear["instances"].values()), default=0)
    check(shrink > 0.05, f"control: linear blending shortens fast-turning bones by up to {shrink * 100:.0f}%")
    shrink = max((r["max_shrink"] for r in skeleton["instances"].values()), default=0)
    check(shrink < 0.01, f"skeleton blending keeps bone lengths (worst {shrink * 100:.1f}%)")
    moving = [r for r in skeleton["instances"].values() if r["moving_fraction"] > 0.2 and r["pose_step_median"] > 1]
    check(bool(moving) and all(r.get("effective_fps", 0) > 50 for r in moving),
          "every animating instance shows a new pose every image: "
          + ", ".join(f"{r['effective_fps']:.0f}" for r in moving))
    drift = max((r["attachment_drift"] for r in skeleton["instances"].values()), default=0)
    attached = [r for r in skeleton["instances"].values() if r["attached_to"]]
    check(drift < 4.0, f"carried models stay in place mid-blend ({len(attached)} attached, drift {drift:.1f} units)")
    unmatched = sum(r["unmatched_game_frames"] for r in skeleton["instances"].values())
    check(unmatched == 0, f"every instance drawn the frame before finds its previous palette ({unmatched} misses)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--card", type=pathlib.Path, help="memory card with a save (for the battle part)")
    args = parser.parse_args()
    held_poses()
    if args.card and args.card.exists():
        fast_rotations(args.card)
    else:
        print("SKIP: fast rotations (no --card)")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS: character in-betweens")
    return 0


if __name__ == "__main__":
    sys.exit(main())
