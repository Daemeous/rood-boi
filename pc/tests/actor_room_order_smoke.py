#!/usr/bin/env python3
"""Characters in the room's PS1 order near its surfaces (render.h
pc_native_actor_window, graphics actor_window_units; GitHub issue 18).

The PS1 painted characters into the room's ordering table: a polygon in a
nearer bucket than a room polygon painted over it, wherever their depths
were. The native path decides by depth, so a character standing in a
doorway, its lower leg a few dozen GTE units behind the door's plane, lost
the leg to the door. Now, behind the room surface in front of it by at most
the window, a character's polygon the PS1 painted after that surface wins;
further behind, depth decides.

    python3 pc/tests/actor_room_order_smoke.py [--card pc/build/test-card1.mcd] [--shots DIR]

1. The leg (no card; from the first_bubble checkpoint): the prologue played
   with Circle (the zone 3 fight won, its two enemies weakened over RPC) to
   zone 4 room 0, where Ashley stands in a doorway while the text box says
   "Now, search!". The frame is kept as a save state and drawn once per
   setting from it (so every picture is the same game frame). In the leg's
   rectangle (LEG, 960 x 720 picture), pixels away from the PS1-order
   reference (native_actors 0: the character's packets in ordering-table
   order after the room):
   - window 0 (depth alone): many (the test sees the bug);
   - the default window: few (the leg drawn);
   - window 16 (less than the leg's 30-60 units behind the door): most of
     the leg stays cut (beyond the window depth decides: constructed guard);
   - MSAA 4x: as single-sampled (the multisampled key images).
2. A character genuinely behind a wall (with a card; occlusion_smoke's
   scene: zone 51 room 1, Ashley moved behind the dark beam, camera yaw
   0x400, occlusion off): the beam hides her knees with the default window
   as with window 0 and as in the packets' own order (native_room 0,
   native_actors 0); the hybrid reference (native_actors 0 alone) shows her
   legs there (so something is hidden).
No crash, shader error or validation message. --shots saves pictures (game
imagery: keep them local).
"""
import argparse
import os
import pathlib
import shutil
import struct
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols, wait_for_file  # noqa: E402
from checkpoints import launch_at  # noqa: E402

WINDOW = "960x720"
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4
HP = 0x18  # vs_battle_actor2 currentHP
FIGHTERS = (2, 3)  # the zone 3 fight's two enemies (battle actor slots)
WEAKEN_AT = 11777  # vsync: the fight is on (Battle Mode)
TEXT_AFTER = 1323  # vsyncs after zone 4 loads: "...Well? Have you found it?" waits for Circle
SEARCH_PRESSES = 11  # Circle presses (3 + 90 vsyncs) from there to "Now, search!"
SETTLE = 16  # vsyncs a resumed save state runs before a picture
# Ashley's lower left leg (left of the picture) against the door, 960 x 720.
LEG = (325, 555, 380, 650)
CLOSE = 24  # colour distance (sum over RGB) still "the same"
BASE = {"native_actors": 2, "native_room": 2, "room_slack_units": 2, "aa": "off"}
# The beam (occlusion_smoke): Ashley behind it at zone 51 room 1's save point.
POSITION = 28  # D_800F4538_t unk0.position
SPOT = (704, 0, 960)
YAW = 0x1F800058

failures = []


def check(ok, message):
    print(("ok   " if ok else "FAIL ") + message, flush=True)
    if not ok:
        failures.append(message)


def settings_file(out):
    path = out / "settings.txt"
    path.write_text("ui_menus 0\nwidescreen 0\ninterpolate 0\nqol_skip_charging 0\ncamera_stick 0\n"
                    "gameplay_walls original\naa off\n")
    return path


def env_for(out):
    return {"ROOD_WINDOW": WINDOW, "ROOD_GI": "0", "ROOD_LIGHTS": str(out / "lights"),
            "ROOD_SETTINGS": str(settings_file(out)), "ROOD_DETERMINISTIC": "1"}


def grab(game, path):
    ppm = path.with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    game.call("present_dump", path=str(ppm))
    wait_for_file(ppm, 20)
    image = np.asarray(Image.open(ppm).convert("RGB")).astype(np.int16)
    ppm.unlink()
    return image


def log_ok(path):
    log = pathlib.Path(path).read_text(errors="replace")
    bad = [line for line in log.splitlines()
           if "[crash]" in line or "shader" in line.lower() and "error" in line.lower() or "VUID" in line]
    check(not bad, f"no crash, shader error or validation message ({pathlib.Path(path).name})"
          + (f": {bad[:3]}" if bad else ""))


def off_reference(image, ref, rect):
    x0, y0, x1, y1 = rect
    return int((np.abs(image - ref).sum(2) > CLOSE)[y0:y1, x0:x1].sum())


def reach_search(out, env, socket):
    """Play the prologue from first_bubble to "Now, search!"; returns its save state."""
    snap = out / "search.snap"
    game = launch_at("first_bubble", fast=True, log=out / "game-drive.log", extra_env=env, socket_path=socket)
    sym = symbols()

    def ptr(address):
        return int.from_bytes(game.read(address, PTR), "little")

    def actor2(slot):
        actor = ptr(sym["vs_battle_actors"] + slot * PTR)
        return int.from_bytes(game.read(actor + 0x3C, 4), "little") if actor else 0
    try:
        weakened = False
        for _ in range(600):
            state = game.state()
            if state["zone"] == 4:
                break
            vsync = game.status()["vsync"]
            if state["zone"] == 3 and not weakened and vsync >= WEAKEN_AT:
                for slot in FIGHTERS:
                    if actor2(slot):
                        game.write(actor2(slot) + HP, struct.pack("<h", 1))
                weakened = True
            game.press("circle", vsyncs=3, settle=57)
        state = game.state()
        check((state["zone"], state["room"]) == (4, 0), f"the prologue reaches zone 4 room 0 (at "
              f"{state['zone']}/{state['room']}, vsync {game.status()['vsync']})")
        if state["zone"] != 4:
            return None
        game.run(TEXT_AFTER)
        for _ in range(SEARCH_PRESSES):
            game.press("circle", vsyncs=3, settle=90)
        print(f"\"Now, search!\" at vsync {game.status()['vsync']}", flush=True)
        game.call("snapshot", action="save", path=str(snap))
        wait_for_file(snap, 60)
    finally:
        game.close()
    log_ok(out / "game-drive.log")
    return snap


def picture(snap, out, env, socket, name, save, **graphics):
    """The "Now, search!" frame drawn with `graphics` (resumed from its save state)."""
    game = Game.launch(snapshot=snap, fast=True, log=out / f"game-{name}.log", extra_env=env, socket_path=socket)
    try:
        game.call("graphics", **dict(BASE, **graphics))
        game.run(SETTLE)
        image = grab(game, out / name)
        save(f"leg-{name}", image)
        if graphics.get("aa") == "msaa4":
            check(game.call("graphics").get("msaa_active", 0) >= 2, "MSAA active for the multisampled picture")
    finally:
        game.close()
    log_ok(out / f"game-{name}.log")
    return image


def leg(out, env, socket, save):
    snap = reach_search(out, env, socket)
    if not snap:
        return
    ref = picture(snap, out, env, socket, "reference", save, native_actors=0)
    depth = picture(snap, out, env, socket, "window0", save, actor_window_units=0)
    on = picture(snap, out, env, socket, "default", save)
    narrow = picture(snap, out, env, socket, "window16", save, actor_window_units=16)
    msaa = picture(snap, out, env, socket, "msaa4", save, aa="msaa4")
    area = (LEG[2] - LEG[0]) * (LEG[3] - LEG[1])
    n_depth, n_on = off_reference(depth, ref, LEG), off_reference(on, ref, LEG)
    n_narrow, n_msaa = off_reference(narrow, ref, LEG), off_reference(msaa, ref, LEG)
    check(n_depth > 0.12 * area, f"depth alone (window 0) cuts the leg: {n_depth} of {area} leg pixels away from "
          f"the PS1-order reference")
    check(n_on < 0.15 * n_depth, f"the default window draws it: {n_on} pixels away (window 0: {n_depth})")
    check(n_narrow > 0.6 * n_depth, f"beyond a 16-unit window depth decides: {n_narrow} pixels away "
          f"(most of the leg is further behind the door)")
    check(n_msaa < 0.25 * n_depth, f"MSAA 4x draws it too: {n_msaa} pixels away")


def yaw(game):
    return struct.unpack("<I", game.read(YAW, 4))[0] & 0xFFF


def screen(game, point, shape):
    cam = game.call("scene")["camera"]
    v = np.array(cam["rotation"]) @ np.array(point, dtype=np.float64) + np.array(cam["translation"])
    x = cam["offset"][0] + cam["h"] * v[0] / v[2]
    y = cam["offset"][1] + cam["h"] * v[1] / v[2]
    return x * shape[1] / 320.0, y * shape[0] / 240.0, v[2], cam["h"] * shape[0] / 240.0


def beam(card, out, env, socket, save):
    shutil.copyfile(card, out / "card.mcd")
    game = launch_at("save_room", card=str(out / "card.mcd"), fast=True, log=out / "game-beam.log", extra_env=env,
                     socket_path=socket)
    try:
        game.call("room_goto", zone=51, room=1, enemies=False, scripts=False)
        for _ in range(80):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        state = game.state()
        check((state["zone"], state["room"]) == (51, 1), f"zone 51 room 1 (at {state['zone']}/{state['room']})")
        actor = int.from_bytes(game.read(symbols()["D_800F4538"], PTR), "little")
        game.write(actor + POSITION, struct.pack("<3h", *SPOT))
        game.run(60)
        for _ in range(8):
            if abs(yaw(game) - 0x400) < 8:
                break
            game.call("input", buttons=["r1"], vsyncs=4)
            game.run(60)
        check(abs(yaw(game) - 0x400) < 8, f"the camera at yaw 0x400 ({yaw(game):#05x})")
        game.call("occlusion", occlusion="off")
        game.run(30)
        images = {}
        for name, graphics in (("default", {}), ("window0", {"actor_window_units": 0}),
                               ("hybrid", {"native_actors": 0}), ("packets", {"native_actors": 0, "native_room": 0})):
            game.call("graphics", **dict(BASE, **graphics))
            game.run(4)  # the same still frame (she stands; the game keeps drawing it)
            images[name] = grab(game, out / f"beam-{name}")
            save(f"beam-{name}", images[name])
        game.call("graphics", **BASE)
        cx, cy, depth, k = screen(game, (SPOT[0], SPOT[1] - 30, SPOT[2]), images["default"].shape)
        half = 30 * k / depth  # her knees and shins, behind the beam
        knees = (int(cx - half), int(cy - half), int(cx + half), int(cy + half))
        area = (knees[2] - knees[0]) * (knees[3] - knees[1])
        hybrid = off_reference(images["hybrid"], images["window0"], knees)
        on = off_reference(images["default"], images["window0"], knees)
        packets = off_reference(images["packets"], images["window0"], knees)
        check(hybrid > 0.3 * area, f"her legs are behind the beam ({hybrid} of {area} pixels show them with "
              f"the characters' packets drawn after the native room)")
        check(on < 0.05 * area, f"the default window keeps them hidden: {on} pixels differ from depth alone")
        check(packets < 0.5 * hybrid, f"as the packets' own order does ({packets} pixels differ)")
    finally:
        game.close()
    log_ok(out / "game-beam.log")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card")
    ap.add_argument("--shots", help="save the pictures (PNG) here")
    args = ap.parse_args()
    out = OUT / "actor_room_order"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    (out / "lights").mkdir()
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)

    def save(name, image):
        if shots:
            Image.fromarray(image.astype(np.uint8)).save(shots / f"{name}.png")

    env = env_for(out)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-actor-order-{os.getpid()}.sock"
    leg(out, env, socket, save)
    if args.card and pathlib.Path(args.card).exists():
        beam(args.card, out, env, socket, save)
    else:
        print("note: no memory card: the wall case is skipped")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS: characters follow the room's PS1 order near its surfaces, depth beyond")
    return 0


if __name__ == "__main__":
    sys.exit(main())
