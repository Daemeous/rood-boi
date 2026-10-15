#!/usr/bin/env python3
"""Emissive texels (pc/docs/Emissive.md): off is the picture without them,
marked texels glow and nothing else changes, the catalog finds its curated
textures in their rooms, an editor-made mask survives save and reload, with
bloom on the glowing texels are what blooms, and with ray tracing (Vulkan)
the forge's coals light the hood above them.

    python3 pc/tests/emissive_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--trace]

Continues from the card's save into Workshop "Metal Works" (zone 45 room 0,
RPC room_goto): its forge's coals (curated E004) in the game's view, the
game paused, the frame redrawn with each setting (RPC redraw).

- off: the frame with emissive 0, with every entry disabled (emissive 1,
  nothing marked) and with emissive 0 again are identical, pixel for pixel
  (Vulkan: on the Original preset; lit, no more apart than two redraws);
- on: the coals (the pixels that change) are at least 15% brighter, and no
  pixel outside the forge's part of the picture changes (path tracing off:
  no light reaches other surfaces); with bloom (threshold 1) a halo
  appears around them;
- matcher: every curated entry marks its textures in its room (32/3 with its
  floating blocks shown, 34/0, 45/0, 53/21);
- editor: a mask made over RPC on a texture picked in the view (a rect and a
  CLUT index set) marks texels, is saved to the user file, and after a
  reload from disk marks the same texels with the same emission;
- --trace (or ROOD_RENDERER=vulkan): with path traced GI (Ultra) the hood
  and wall above the forge get brighter when the coals glow (their light).
"""
import argparse
import json
import math
import os
import pathlib
import shutil
import struct
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402

SETTLE = 45
MATCH_ROOMS = [((32, 3), ["E001"]), ((34, 0), ["E002", "E003"]), ((45, 0), ["E004"]), ((53, 21), ["E005"])]


def frame(game, path):
    game.present_screenshot(path)
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)


def redraw(game):
    """The paused game's newest frame drawn again with the current settings (RPC redraw), then shown."""
    game.call("redraw")
    game.wait_presents(4)


def goto(game, zone, room):
    game.call("editor_camera", off=1)
    game.call("room_goto", zone=zone, room=room, enemies=False, scripts=False)
    for _ in range(60):
        game.run(20)
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
    game.run(40)
    state = game.state()
    return (state["zone"], state["room"]) == (zone, room)


def window_camera(game, yaw=270):
    """The free camera inside Dinas Walk looking at its two lattice windows (yaw 270: the west wall; 90: the
    east wall and its octagonal window)."""
    lo, hi = game.call("scene")["bounds"]
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    r = max(hi[0] - lo[0], hi[2] - lo[2]) * 0.45
    a = math.radians(yaw)
    game.call("editor_camera", x=cx - math.sin(a) * r, y=-1100, z=cz - math.cos(a) * r, yaw=yaw, pitch=-3)


def show_platforms(game):
    data = game.read_symbol("vs_battle_roomData", 0xB0)
    table = struct.unpack("<I", data[0xA8:0xAC])[0]
    count = struct.unpack("<I", data[0x48:0x4C])[0] // 184
    for i in range(count):
        word = struct.unpack("<I", game.read(table + i * 184, 4))[0]
        game.write(table + i * 184, struct.pack("<I", word & ~(1 << 9)))
    game.run(2)
    game.write(symbols()["D_800F1D6E"], b"\x01")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep the frames and logs here")
    parser.add_argument("--trace", action="store_true", help="also the ray-traced light (Vulkan)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    trace = args.trace or os.environ.get("ROOD_RENDERER") == "vulkan"
    out = pathlib.Path(args.out) if args.out else OUT / "emissive"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    user = out / "emissive-user.json"
    user.unlink(missing_ok=True)
    env = {"ROOD_LIGHTS": str(lights), "ROOD_EMISSIVE_USER": str(user), "ROOD_SCALE": "2",
           "ROOD_WINDOW": "1280x960", "ROOD_GI": "1" if trace else "0"}
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-emissive-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        listing = game.call("emissive")
        curated = [e["id"] for e in listing["entries"] if e["origin"] == "curated"]
        check(len(curated) >= 5, f"the curated catalog loads ({len(curated)} entries)")
        game.call("graphics", interpolate=False)

        # ---- off, on: the forge of Workshop "Metal Works" (45/0, the game's camera; path tracing off unless
        # --trace). The game stays paused on one frame; RPC redraw draws it again with each setting.
        check(goto(game, 45, 0), "warped to zone 45 room 0")
        if trace:
            game.call("graphics", gi=False)  # pixel-exact comparisons first: no path-traced noise
        game.run(SETTLE)
        def identity(tag, exact):
            """emissive 0, emissive 1 with every entry disabled, emissive 0 again: the same picture."""
            game.call("graphics", emissive=0)
            redraw(game)
            off = frame(game, out / f"off{tag}.png")
            game.call("graphics", emissive=1)
            for e in curated:
                game.call("emissive", action="set", id=e, enabled=False)
            redraw(game)
            listing = game.call("emissive")
            check(listing["marked_texels"] == 0, f"every entry disabled: nothing marked ({listing['marked_texels']})")
            nothing = frame(game, out / f"on-nothing-marked{tag}.png")
            game.call("emissive", action="reload")  # the user layer's disabled copies dropped (unsaved)
            game.call("graphics", emissive=0)
            redraw(game)
            off2 = frame(game, out / f"off-again{tag}.png")
            if exact:
                check(np.array_equal(off, nothing), f"{tag or 'lit'}: emissive 1 with nothing marked is "
                      f"pixel-identical to emissive 0 ({int((off != nothing).any(2).sum())} pixels differ)")
                check(np.array_equal(off, off2), f"{tag or 'lit'}: emissive 0 again is pixel-identical "
                      f"({int((off != off2).any(2).sum())} pixels differ)")
            else:
                # Vulkan's lit picture redraws with temporal noise (haze, AO, probes): the change must be no
                # larger than a redraw's own
                noise = np.abs(off - off2).mean()
                change = np.abs(off - nothing).mean()
                check(change <= noise * 2 + 0.02, f"{tag or 'lit'}: emissive 1 with nothing marked differs from "
                      f"emissive 0 no more than a redraw does ({change:.3f} vs {noise:.3f})")
            return off

        if trace:
            # Vulkan: exact on the original picture (no temporal effects), then the lit one within its noise
            preset0 = game.call("graphics").get("lighting_preset")
            game.call("graphics", lighting_preset="original")
            identity("-original", True)
            if preset0:
                game.call("graphics", lighting_preset=preset0)
            game.call("graphics", gi=False)
            off = identity("", False)
        else:
            off = identity("", True)
        game.call("graphics", emissive=1)
        redraw(game)
        listing = game.call("emissive")
        marked = {e["id"]: e["marked"] for e in listing["entries"]}
        check(marked.get("E004", 0) >= 1, f"the forge coals' textures are marked ({marked.get('E004')})")
        on = frame(game, out / "on.png")
        h, w, _ = on.shape
        region = np.zeros(on.shape[:2], bool)  # the forge's bed: right of the frame, middle height
        region[h * 25 // 100: h * 60 // 100, w * 65 // 100:] = True
        if True:  # path tracing off here (OpenGL; Vulkan with gi off)
            changed = (on != off).any(2)
            lum_on, lum_off = on.mean(2), off.mean(2)
            coals = changed & region
            outside = changed & ~region
            gain = lum_on[coals].mean() / max(lum_off[coals].mean(), 1e-3) if coals.any() else 0
            check(coals.sum() >= 1000 and gain >= 1.15,
                  f"the coals glow: {int(coals.sum())} pixels, {gain:.2f} x their lit light (>= 1.15)")
            check(outside.sum() == 0, f"nothing else changes ({int(outside.sum())} pixels outside the forge)")
            # (anti-aliasing blends a few edge pixels with their brighter neighbours' old value)
            darker = int((lum_on[coals] < lum_off[coals] - 2).sum())
            check(darker <= max(20, coals.sum() // 25), f"no marked pixel gets darker ({darker} edge pixels)")
            # Bloom (post stack, threshold 1: only light above white blooms): the glowing coals are what
            # blooms, a halo around them that emissive 0 does not have.
            game.call("post", layer="gameplay", clear="all", set={"bloom_method": "gaussian", "bloom_intensity": 1,
                                                                  "bloom_threshold": 1.0, "bloom_knee": 0}, snap=True)
            halos = {}
            for value in (0, 1):
                game.call("graphics", emissive=value)
                redraw(game)
                halos[value] = frame(game, out / f"bloom-{value}.png").mean(2)
            ring = changed.copy()  # the pixels within 12 of a coal, the coals left out
            for _ in range(3):
                grown = ring.copy()
                for dy, dx in ((-4, 0), (4, 0), (0, -4), (0, 4)):
                    grown |= np.roll(np.roll(ring, dy, 0), dx, 1)
                ring = grown
            ring &= ~changed
            halo = float(halos[1][ring].mean() - halos[0][ring].mean())
            check(halo >= 0.3, f"with bloom on, the coals bloom: +{halo:.2f} levels around them")
            game.call("post", layer="gameplay", clear="all", snap=True)
            game.call("graphics", emissive=1)
        if trace:
            # ---- the ray-traced light: the forge's surroundings (the coals left out), path traced (Ultra)
            preset = game.call("graphics").get("lighting_preset")
            game.call("graphics", lighting_preset="ultra", gi=True)
            means = {}
            for value in (0, 1):
                game.call("graphics", emissive=value)
                game.run(SETTLE * 3)
                means[value] = np.mean([frame(game, out / f"trace-{value}-{k}.png") for k in range(4)], axis=0)
            lum0, lum1 = means[0].mean(2), means[1].mean(2)
            noise = float(np.abs(frame(game, out / "trace-1-again.png").mean(2) - lum1).mean())
            # the hood and the wall above the forge (the coals themselves are lower in the frame)
            near = (slice(0, h * 30 // 100), slice(w * 40 // 100, w * 85 // 100))
            lift = float(lum1[near].mean() - lum0[near].mean())
            check(lift >= max(0.3, 10 * noise), f"the forge lights the hood and wall above it: "
                  f"{lum0[near].mean():.2f} -> {lum1[near].mean():.2f} (+{lift:.2f} levels; frame noise {noise:.3f})")
            if preset:
                game.call("graphics", lighting_preset=preset)

        # ---- the matcher: curated entries in their rooms
        for (zone, room), ids in MATCH_ROOMS:
            if not goto(game, zone, room):
                check(False, f"warped to zone {zone} room {room}")
                continue
            if (zone, room) == (32, 3):
                show_platforms(game)
                game.run(SETTLE)
            if (zone, room) == (34, 0):  # the high windows: in the free camera's views (the game's culls them)
                for yaw in (270, 90):
                    window_camera(game, yaw)
                    game.run(10)
            game.run(10)
            marked = {e["id"]: e["marked"] for e in game.call("emissive")["entries"]}
            for i in ids:
                check(marked.get(i, 0) >= 1, f"{i} found in zone {zone} room {room} ({marked.get(i, 0)} textures)")

        # ---- editor round trip: a mask on the floor texture under the screen centre
        game.call("editor_camera", off=1)
        game.run(10)
        picked = None
        for y in range(560, 900, 40):
            p = game.call("emissive", action="pick", x=640, y=y)
            if p.get("valid") and p.get("room") and not p.get("entry"):
                picked = p
                break
        check(picked is not None, "a room texture picked in the view")
        if picked:
            w, h = picked["size"]
            entry = {"id": "U900", "name": "test mask", "match": [{"key": picked["key"], "size": [w, h]}],
                     "mask": {"rects": [[0, 0, max(1, w // 2), max(1, h // 2)]], "indices": list(range(0, 16))},
                     "color": [1, 0.5, 0.25], "strength": 2, "light": 0}
            game.call("emissive", action="set", entry=entry)
            game.run(4)
            before = game.call("emissive")
            texel = game.call("emissive", action="texel", u=picked["box"][0], v=picked["box"][1],
                              tpage=picked["tpage"], clut=picked["clut"])
            check(before["marked_texels"] > 0 and texel["slot"] > 0,
                  f"the new mask marks texels ({before['marked_texels']}, slot {texel['slot']})")
            game.call("emissive", action="save")
            saved = json.loads(user.read_text()) if user.exists() else {}
            ids = [e.get("id") for e in saved.get("entries", [])]
            check("U900" in ids, f"saved to the user file ({ids})")
            game.call("emissive", action="reload")
            game.run(4)
            after = game.call("emissive")
            texel2 = game.call("emissive", action="texel", u=picked["box"][0], v=picked["box"][1],
                               tpage=picked["tpage"], clut=picked["clut"])
            mine = [e for e in after["entries"] if e["id"] == "U900"]
            check(bool(mine) and mine[0]["mask"] == entry["mask"] and mine[0]["match"][0]["key"] == picked["key"],
                  "reloaded: the same entry")
            check(after["marked_texels"] == before["marked_texels"] and texel2 == texel,
                  f"reloaded: the same texels and emission ({before['marked_texels']} -> {after['marked_texels']})")
            exported = game.call("emissive", action="export", id="U900")
            check('"key"' in exported.get("json", ""), "export gives the entry's JSON for the curated catalog")
    finally:
        game.close()
    if not args.out and not failures:
        shutil.rmtree(out, ignore_errors=True)
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
