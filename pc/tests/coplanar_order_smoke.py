#!/usr/bin/env python3
"""Coplanar room surfaces paint in the PS1's order (render.c
draw_room_ordered, graphics room_order).

The PS1 had no depth buffer: room polygons lying on other room polygons
(documents on a table, a notice on a wall, a rug on the floor) won by the
ordering table. The native room is depth-tested, so without the order
they z-fight: the table's wood speckles through the papers or hides them.

    python3 pc/tests/coplanar_order_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

1. The title's attract cutscene (idle 1000 vsyncs; VKP headquarters, zone
   20): the wide shot of the council table at a fixed vsync. The papers by
   the left figure's arm and behind the lantern are classified (bright,
   paper-coloured pixels) in the native picture with room_order on, off
   (the same paused frame redrawn), and in the PS1-order reference (the
   room from packets, native_room 0, drawn in ordering-table order):
   - on matches the reference (IoU of the paper pixels), off does not
     (the test sees the bug);
   - on is stable over consecutive game frames (no flicker).
2. With a memory card (a copy; Continue, then the Room Browser's warp):
   rooms with room-on-room decals in view (ROOMS: the forge's coals lying
   on the floor at its base in Workshop "Metal Works", the notices on the
   walls of Rue Lejour), the camera turned in eight steps (right stick).
   Where on and off differ, on must be closer to the PS1-order reference
   than off (mean colour distance), and it must not change over game
   frames.
No crash, shader error or Vulkan validation message.
"""
import argparse
import os
import pathlib
import shutil
import sys
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game  # noqa: E402

WINDOW = "960x720"
# The council table's wide shot, vsyncs after the title menu is ready.
ATTRACT_VSYNC = 1800
# Native rect (960 x 720 picture) of the papers by the left figure and
# behind the lantern.
PAPER_RECTS = [(312, 205, 395, 250), (440, 185, 545, 215)]
# In-game rooms with room-on-room decals: zone, room, what lies on what.
ROOMS = [
    (45, 0, 'Workshop "Metal Works": the forge\'s coals on the floor at its base'),
    (32, 15, "Rue Lejour: notices on the walls"),
]


def grab(game, path, timeout=20):
    ppm = path.with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    game.call("present_dump", path=str(ppm))
    deadline = time.monotonic() + timeout
    while not (ppm.exists() and ppm.stat().st_size) and time.monotonic() < deadline:
        time.sleep(0.03)
    time.sleep(0.15)
    image = np.asarray(Image.open(ppm).convert("RGB")).astype(np.int16)
    ppm.unlink()
    return image


def redrawn(game, path, **graphics):
    game.call("graphics", **graphics)
    game.call("redraw")
    time.sleep(0.15)
    return grab(game, path)


def paper(image):
    """Paper-coloured pixels (light beige; the table's wood is dark red-brown)."""
    r, g, b = image[..., 0], image[..., 1], image[..., 2]
    return (g > 95) & (r > 120) & (r - b < 110)


def in_rects(mask, rects):
    out = np.zeros_like(mask)
    for x0, y0, x1, y1 in rects:
        out[y0:y1, x0:x1] = mask[y0:y1, x0:x1]
    return out


def iou(a, b):
    union = (a | b).sum()
    return float((a & b).sum()) / union if union else 1.0


def log_ok(game, check):
    log = pathlib.Path(game.log).read_text(errors="replace")
    bad = [line for line in log.splitlines()
           if "[crash]" in line or "shader" in line.lower() and "error" in line.lower() or "VUID" in line]
    check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))


def rooms(args, out, env, socket, check, save):
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    (out / "lights").mkdir(exist_ok=True)
    env = dict(env, ROOD_LIGHTS=str(out / "lights"), ROOD_LIGHTING=str(out / "packs"))
    game = Game.launch(fast=True, card=str(card), log=out / "game-rooms.log", extra_env=env, socket_path=socket)
    try:
        game.wait_title()
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        game.call("graphics", aa="off", gi=False)
        for zone, room, note in ROOMS:
            game.call("room_goto", zone=zone, room=room)
            for _ in range(60):
                game.run(20)
                if game.call("room_goto", action="status")["phase"] in (0, 4):
                    break
            game.run(40)
            state = game.state()
            if (state["zone"], state["room"]) != (zone, room):
                check(False, f"warp to zone {zone} room {room}: at {state['zone']}/{state['room']}")
                continue
            n_all = d_on_all = d_off_all = flicker_all = 0
            views = []
            for step in range(8):
                tag = f"z{zone:02d}r{room:02d}-{step}"
                off = redrawn(game, out / "off", room_order=0)
                on = redrawn(game, out / "on", room_order=1)
                game.run(8)
                on_later = grab(game, out / "later")
                game.call("graphics", native_room=0)
                game.run(4)
                ref = grab(game, out / "ref")
                game.call("graphics", native_room=2, room_order=1)
                game.run(4)
                changed = np.abs(on - off).sum(2) > 24
                n = int(changed.sum())
                if n > 300:  # a view with decals in it
                    for name, image in (("off", off), ("on", on), ("reference", ref)):
                        save(f"{tag}-{name}", image)
                    n_all += n
                    d_on_all += float(np.abs(on - ref).sum(2)[changed].sum())
                    d_off_all += float(np.abs(off - ref).sum(2)[changed].sum())
                    flicker_all += int((np.abs(on_later - on).sum(2)[changed] > 48).sum())
                    views.append(step)
                game.call("input", right_stick=[1, 0], vsyncs=40)
                game.run(60)
            d_on, d_off = d_on_all / max(n_all, 1), d_off_all / max(n_all, 1)
            check(n_all > 1000 and d_on < 0.6 * d_off,
                  f"{note} (zone {zone} room {room}, views {views}): {n_all} pixels decided by the order; "
                  f"distance to the PS1 order {d_on:.0f} on, {d_off:.0f} off")
            check(flicker_all < 0.05 * max(n_all, 1), f"  stable over game frames ({flicker_all} of them change)")
        log_ok(game, check)
    finally:
        game.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card")
    ap.add_argument("--shots", help="save the pictures (PNG) here")
    args = ap.parse_args()
    out = OUT / "coplanar_order"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    def save(name, image):
        if shots:
            Image.fromarray(image.astype(np.uint8)).save(shots / f"{name}.png")

    env = {"ROOD_WINDOW": WINDOW, "ROOD_GI": "0"}
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-coplanar-{os.getpid()}.sock"
    game = Game.launch(fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        game.wait_title()
        game.run(ATTRACT_VSYNC)
        state = game.state()
        check(state["zone"] == 20, f"attract cutscene: zone {state['zone']} (VKP headquarters is 20)")
        game.call("graphics", aa="off")
        stable = []
        for k in range(4):
            on = redrawn(game, out / "on", room_order=1)
            off = redrawn(game, out / "off", room_order=0)
            if k == 0:
                first_on, first_off = on, off
                save("attract-on", on)
                save("attract-off", off)
            stable.append(int(in_rects(paper(on), PAPER_RECTS).sum()))
            game.call("graphics", room_order=1)
            game.run(2)
        # The PS1-order reference: the room from packets (ordering table).
        game.call("graphics", native_room=0)
        game.run(4)
        ref = grab(game, out / "ref")
        save("attract-reference", ref)
        game.call("graphics", native_room=2, room_order=1)
        game.run(2)
        p_ref = in_rects(paper(ref), PAPER_RECTS)
        p_on = in_rects(paper(first_on), PAPER_RECTS)
        p_off = in_rects(paper(first_off), PAPER_RECTS)
        check(p_ref.sum() > 400, f"the reference shows the papers ({int(p_ref.sum())} paper pixels)")
        check(iou(p_on, p_ref) > 0.8,
              f"room_order on matches the PS1 order: IoU {iou(p_on, p_ref):.2f} ({int(p_on.sum())} pixels)")
        check(iou(p_off, p_ref) < 0.6,
              f"room_order off differs (the z-fight this guards): IoU {iou(p_off, p_ref):.2f} ({int(p_off.sum())} pixels)")
        spread = (max(stable) - min(stable)) / max(1, max(stable))
        check(spread < 0.05, f"stable over 4 game frames: paper pixels {stable}")
        # MSAA: the ordered passes run on the multisampled twins.
        game.call("graphics", aa="msaa4")
        game.run(2)
        ms_on = redrawn(game, out / "ms", room_order=1)
        save("attract-msaa4-on", ms_on)
        p_ms = in_rects(paper(ms_on), PAPER_RECTS)
        active = game.call("graphics").get("msaa_active", 0)
        check(iou(p_ms, p_on) > 0.85, f"MSAA 4x ({active} samples active): on matches the single-sampled picture, "
              f"IoU {iou(p_ms, p_on):.2f}")
        game.call("graphics", aa="off")

        log_ok(game, check)
    finally:
        game.close()
    if args.card and pathlib.Path(args.card).exists():
        rooms(args, out, env, socket, check, save)
    else:
        print("note: no memory card: the in-game rooms are skipped")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS: coplanar room surfaces follow the PS1 order")
    return 0


if __name__ == "__main__":
    sys.exit(main())
