#!/usr/bin/env python3
"""Room transitions fade the lit picture (gpu_hw.c BLEND_FADE; pc/docs/Post_Processing.md
"The game's screen fade").

    python3 pc/tests/transition_fade_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

The door fade (BATTLE _drawScreenTransition, effect 0) is a full-screen flat
quad subtracted from the screen. Drawn as translucent 2D it used to mark the
whole picture as UI, so from its first frame the lighting, haze and post
effects dropped out (the unlit PS1 picture, even brighter at first) and
popped back after the fade-in. Now it subtracts from the 2D under it only;
the 3D keeps its depth and surface and the grade pass fades the finished
picture. From the card's save (lighting packs on, so lit and unlit differ),
a warp to another room of the zone (the game's door call) and one to
another zone, every other vsync:

- the shown fade (RPC post `fade`) rises to 1 and falls back to 0;
- while it rises the picture's mean luminance never rises (fading from the
  lit look straight to black), and while it falls it never falls;
- until the picture is black, it differs from the same frame unlit
  (view_mode 1): the lighting stays on through the fade;
- the depth view keeps 3D depth (not UI) under the fade.
"""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, wait_for_file  # noqa: E402
from checkpoints import launch_at  # noqa: E402

failures = []


def check(ok, message):
    print(("ok   " if ok else "FAIL ") + message, flush=True)
    if not ok:
        failures.append(message)


def capture(game, path, timeout=20):
    ppm = path.with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    game.call("present_dump", path=str(ppm))
    wait_for_file(ppm, timeout)
    image = np.asarray(Image.open(ppm).convert("RGB")).astype(np.float32)
    ppm.unlink()
    return image


def redrawn(game, path):
    game.wait_presents(2)
    game.call("redraw")
    game.wait_presents(2)
    return capture(game, path)


def transition(game, out, name, zone, room, save):
    """Warp and sample every other vsync until the fade is over: [(fade, mean, lit-unlit, ui share)]."""
    reply = game.call("room_goto", zone=zone, room=room)
    if not reply.get("ok"):
        print(f"skip {name}: room_goto refused ({reply})")
        return None
    samples, seen, k = [], False, 0
    for step in range(400):
        game.run(2)
        fade = game.call("post")["fade"]
        if fade <= 0 and not seen and step < 200:
            continue
        if fade <= 0 and seen:
            break
        seen = True
        lit = redrawn(game, out / "lit")
        game.call("graphics", view_mode=1)
        unlit = redrawn(game, out / "unlit")
        game.call("graphics", view_mode=0, show_depth=True)
        depth = redrawn(game, out / "depth")
        game.call("graphics", show_depth=False)
        ui = float(((depth[..., 0] > 140) & (depth[..., 1] < 20) & (depth[..., 2] > 140)).mean())
        samples.append((fade, float(lit.mean()), float(np.abs(lit - unlit).mean()), ui))
        if k % 4 == 0:
            save(f"{name}-{k:03d}-fade{fade:.2f}", lit)
        k += 1
    for _ in range(60):
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
        game.run(20)
    return samples


def judge(name, samples, ui_before):
    if not samples:
        check(False, f"{name}: no fade seen")
        return
    fades = [s[0] for s in samples]
    peak = fades.index(max(fades))
    check(max(fades) >= 0.99, f"{name}: the fade reaches black ({max(fades):.2f}, {len(samples)} samples)")
    rising, falling = samples[: peak + 1], samples[peak:]
    up = [b[1] - a[1] for a, b in zip(rising, rising[1:]) if b[0] > a[0]]
    down = [b[1] - a[1] for a, b in zip(falling, falling[1:]) if b[0] < a[0]]
    check(all(d <= 0.3 for d in up), f"{name}: fading out never brightens (largest step {max(up, default=0):+.2f})")
    check(all(d >= -0.3 for d in down), f"{name}: fading in never darkens (largest step {min(down, default=0):+.2f})")
    visible = [s for s in samples if s[1] > 3.0]
    lit = [s[2] for s in visible]
    check(visible and min(lit) > 0.2,
          f"{name}: lit until black ({len(visible)} visible fade frames; lit - unlit at least "
          f"{min(lit, default=0):.2f})")
    ui = max(s[3] for s in samples)
    check(ui <= ui_before + 0.02, f"{name}: the 3D keeps its depth under the fade (UI share {ui:.3f}, "
          f"{ui_before:.3f} before)")
    print("     fade, mean, lit-unlit: " + "  ".join(f"{f:.2f}/{m:.1f}/{d:.1f}" for f, m, d, _ in samples[::3]))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    parser.add_argument("--shots", help="save frames of each fade here (game imagery: keep local)")
    args = parser.parse_args()
    card = pathlib.Path(args.card)
    if not card.exists():
        print(f"SKIP: no card {card}")
        return 77
    out = OUT / "transition_fade"
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)

    def save(name, image):
        if shots:
            Image.fromarray(np.clip(image, 0, 255).astype(np.uint8)).save(shots / f"{name}.png")

    work = pathlib.Path(tempfile.mkdtemp(prefix="fade-", dir=out))
    shutil.copy(card, work / "card1.mcd")
    (work / "lights").mkdir()
    env = {"ROOD_WINDOW": "640x480", "ROOD_LIGHTS": str(work / "lights"), "ROOD_LIGHTING": "lighting",
           "ROOD_GI": "0", "ROOD_SCALE": "2"}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env["ROOD_VK_VALIDATE"] = "1"
    os.environ.pop("ROOD_SETTINGS", None)
    game = launch_at("save_room", card=str(work / "card1.mcd"), fast=True, log=out / "game.log", extra_env=env)
    try:
        state = game.state()
        zone, room = state["zone"], state["room"]
        rooms = game.call("rooms")["rooms"]
        # Rooms a door leads into (the warp takes the game's door call; others need a cell).
        same = [r for r in rooms if r["zone"] == zone and r["room"] != room and r.get("entries")]
        other = [r for r in rooms if r["zone"] != zone and r["zone"] in (9, 10, 11, 12, 13) and r.get("entries")]
        before = redrawn(game, out / "before")
        game.call("graphics", view_mode=1)
        unlit = redrawn(game, out / "before")
        game.call("graphics", view_mode=0, show_depth=True)
        depth = redrawn(game, out / "depth")
        game.call("graphics", show_depth=False)
        ui_before = float(((depth[..., 0] > 140) & (depth[..., 1] < 20) & (depth[..., 2] > 140)).mean())
        lit_gap = float(np.abs(before - unlit).mean())
        check(lit_gap > 0.5, f"zone {zone} room {room}: lit and unlit differ ({lit_gap:.2f})")
        save("before", before)
        if same:
            judge(f"door to room {same[0]['room']}", transition(game, out, "same-zone", zone, same[0]["room"], save),
                  ui_before)
        if other:
            judge(f"zone {other[0]['zone']} room {other[0]['room']}",
                  transition(game, out, "cross-zone", other[0]["zone"], other[0]["room"], save), ui_before)
    finally:
        game.close()
    text = pathlib.Path(game.log).read_text(errors="replace")
    bad = [line for line in text.splitlines() if "validation:" in line or "[crash]" in line]
    check(not bad, "no crash or validation message" + (f": {bad[:3]}" if bad else ""))
    shutil.rmtree(work, ignore_errors=True)
    if failures:
        print(f"FAIL: {len(failures)} checks failed")
        return 1
    print("PASS: room transitions fade the lit picture")
    return 0


if __name__ == "__main__":
    sys.exit(main())
