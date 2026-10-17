#!/usr/bin/env python3
"""Standing in a save point keeps its light, and Ashley still casts shadows
from it (GitHub issue 2; pc/docs/Glows.md "Enclosing glows").

    python3 pc/tests/save_point_glow_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The save point's glow was a small sphere at the centre of its column, which
is inside Ashley when he stands in it: every shadow ray from the room hit
him, and the room went dark around him. Its glow is now the column's
glowing side (glows.c fit_column), and its shadow rays aim at a thinner
core and stop at the column's clear middle (rt_lights.glsl emitter_shadow):
standing in the middle hides none of its light, and beside it Ashley casts
a soft shadow that can be seen (a playtest in save room 29/0 found the
first column's shadow too faint to see).

Continues from a copy of the card (never the card itself; skipped without
one) into its save room (zone 9 room 17, where Continue leaves Ashley
standing in the save point), with a private lights directory. With the
High and Ultra lighting presets (the ones whose glows cast shadows), on
whichever renderer the run uses (GL or Vulkan), checks:

- the save point glow is a column the size of its polygons (radius ~64,
  224 high, RPC glows `shape`);
- Ashley at the centre: the baked light's visibility on the floor around
  the circle (RPC gi_probe at world points: the GPU's own result) stays
  high, where the old sphere (RPC glows {shapes: 0}) loses about half;
  the frame with glow shadows on keeps most of the light of the frame
  with them off (whole picture, HUD left out);
- Ashley just inside the edge and just outside the circle: the floor
  behind him (away from the column) is clearly darker than the floor to
  the sides: his shadow; and in the presented frame, glow shadows on
  against off, the floor 1 and 2 body lengths behind him is at least 30%
  darker (where the GI probe puts those points on screen);
- save room 29/0 (the playtest's; RPC room_goto), a dimmer save point:
  the same visible shadow on both sides of it, and the room's light kept
  with Ashley in the circle.

--out keeps the frames (shadows on / off) and the logs."""
import argparse
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
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_cutaway_smoke import default_card  # noqa: E402

AXIS = (448, 576)  # the save point's axis in the save room (world x, z; the floor is y 0)
SETTLE = 90  # vsyncs for the traced light and its filters after a change
KEEP = 0.85  # the light kept with Ashley in the circle (glow shadows on / off; the old sphere: ~0.56)
VISIBLE = 0.80  # the baked light's mean visibility around the circle with Ashley inside (the old sphere: ~0.5)
SHADOW = 0.10  # how much darker the floor behind him is than the floor to his sides
BODY = 112  # Ashley's height: a body length
VISIBLE_SHADOW = 0.30  # the final frame behind him, 1 and 2 body lengths, glow shadows on against off (measured 0.33-0.44)


def luminance(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    return 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]


def frame_luminance(path):
    lum = luminance(path)
    h, w = lum.shape
    return lum[int(h * 0.15):int(h * 0.68), int(w * 0.15):int(w * 0.85)]  # the room, not the HUD


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the frames and logs here")
    parser.add_argument("--presets", default="ultra,high")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "save_point_glow"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    # The path tracer on every present (ROOD_GI_BUDGET 0): its automatic budget times the GPU, so
    # with other suites sharing it the light was traced less often and not settled after SETTLE.
    env = {"ROOD_LIGHTS": str(lights), "ROOD_GI": "1", "ROOD_SCALE": "2", "ROOD_WINDOW": "1280x720",
           "ROOD_GI_BUDGET": "0"}
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-savepoint-{os.getpid()}.sock"  # sun_path is short
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        state = game.state()
        if (state["zone"], state["room"]) != (9, 17) or not state.get("player"):
            check(False, f"Continue leads to the save room (zone {state['zone']} room {state['room']})")
            return 1
        game.call("graphics", widescreen=True)
        actor = struct.unpack("<I", game.read_symbol("D_800F4538", 4))[0]  # Ashley's record (dev_state.c)

        axis = list(AXIS)

        def place(dx, dz):
            """Ashley at the axis + (dx, dz): his position (SVECTOR at 0x1C)."""
            game.write(actor + 0x1C, struct.pack("<hhh", int(axis[0] + dx), 0, int(axis[1] + dz)))

        def frame_shadow(name, dx, dz, tag):
            """What the player sees: the presented frame with glow shadows on
            against off, on the floor 1 and 2 body lengths behind Ashley
            (along the line from the axis through him), where the GI probe
            puts those points on screen."""
            dist = math.hypot(dx, dz)
            ux, uz = dx / dist, dz / dist
            points, frames = {}, {}
            for on in (True, False):
                game.call("graphics", glow_shadows=on)
                game.run(SETTLE)
                path = out / f"{tag}-shadows-{'on' if on else 'off'}.png"
                game.present_screenshot(path)
                frames[on] = luminance(path)
                if on:
                    for k in (1, 2):
                        d = dist + k * BODY
                        try:
                            points[k] = game.call("gi_probe", x=axis[0] + ux * d, y=0, z=axis[1] + uz * d)
                        except Exception:
                            points[k] = None
            game.call("graphics", glow_shadows=True)
            h, w = frames[True].shape
            aspect = 16 / 9 if game.call("graphics").get("widescreen") else 4 / 3
            dw = min(w, h * aspect)
            dh = dw / aspect
            ox, oy = (w - dw) / 2, (h - dh) / 2
            for k in (1, 2):
                q = points[k]
                x, y = (int(ox + q["u"] * dw), int(oy + q["v"] * dh)) if q else (-99, -99)
                if not (8 <= x < w - 8 and 8 <= y < h - 8):
                    check(False, f"{name}: the floor {k} body length(s) behind him is on screen")
                    continue
                on_, off_ = frames[True][y - 6:y + 7, x - 6:x + 7].mean(), frames[False][y - 6:y + 7, x - 6:x + 7].mean()
                if off_ < 10:
                    check(False, f"{name}: the floor {k} body length(s) behind him is lit floor on screen ({off_:.0f})")
                    continue
                dark = 1 - on_ / off_
                check(dark >= VISIBLE_SHADOW,
                      f"{name}: his shadow is visible {k} body length(s) behind him ({dark:.0%} darker >= {VISIBLE_SHADOW:.0%})")

        def floor(radius, n=16):
            """The baked light's visibility at n floor points around the axis
            (None where the point is off screen)."""
            values = []
            for k in range(n):
                a = 2 * math.pi * k / n
                try:
                    r = game.call("gi_probe", x=AXIS[0] + radius * math.cos(a), y=0, z=AXIS[1] + radius * math.sin(a))
                    values.append(r["baked_visibility"])
                except Exception:
                    values.append(None)
            return values

        def ring_mean(values):
            v = [x for x in values if x is not None]
            return sum(v) / len(v) if v else 0.0

        def shadow(name):
            """Behind him (+x, within 22.5 degrees) against his sides (90 degrees off, within 22.5)."""
            rings = [floor(96), floor(160)]
            behind = [r[k] for r in rings for k in (15, 0, 1) if r[k] is not None]
            sides = sorted(r[k] for r in rings for k in (3, 4, 5, 11, 12, 13) if r[k] is not None)
            if not behind or not sides:
                check(False, f"{name}: the floor around the circle is on screen ({rings})")
                return
            side = sides[len(sides) // 2]
            print(f"{name}: behind him {', '.join(f'{x:.2f}' for x in behind)}; to his sides (median) {side:.2f}")
            check(min(behind) <= side - SHADOW, f"{name}: his shadow, darker behind him ({min(behind):.2f} vs {side:.2f})")

        glows = game.call("glows")["glows"]
        save = [g for g in glows if g["name"].startswith("Save point glow")]
        check(len(save) == 1, f"the save point is one glow ({[g['name'] for g in glows]})")
        if save:
            shape = save[0]["shape"]
            print(f"save point glow: {shape}")
            check(shape["kind"] == "column" and 50 <= shape["radius"] <= 80 and 100 <= shape["half_axis"][1] <= 124,
                  f"its glow is the column's side (radius {shape['radius']:.0f}, half height {shape['half_axis'][1]:.0f})")
            # A per-glow shape override, saved with the room by key and loaded again.
            gid = save[0]["id"]

            def kind():
                game.run(2)
                return [g for g in game.call("glows")["glows"] if g["id"] == gid][0]["shape"]["kind"]

            game.call("glows", action="set", id=gid, shape="sphere")
            sphere = kind()
            game.call("actors", action="save")
            room_file = lights / "zone09_room17.txt"
            line = [x for x in (room_file.read_text() if room_file.exists() else "").splitlines() if x.startswith("glow ")]
            game.reload_lights()
            reloaded = kind()
            check(sphere == "sphere" and reloaded == "sphere" and len(line) == 1 and "shape=sphere" in line[0],
                  f"a shape override (sphere) applies, is saved ({line}) and loads again ({reloaded})")
            game.call("glows", action="reset", id=gid)
            check(kind() == "column", "reset: the column again")
            game.call("actors", action="save")

        for preset in args.presets.split(","):
            game.call("graphics", lighting_preset=preset)
            if not game.call("graphics").get("glow_shadows"):
                print(f"{preset}: glows cast no shadows here (skipped)")
                continue
            # Ashley in the centre (where Continue leaves him).
            place(0, 0)
            game.call("glows", shapes=0)  # the old sphere: the issue
            game.run(SETTLE)
            before = ring_mean(floor(96) + floor(160))
            game.call("glows", shapes=1)
            game.run(SETTLE)
            after = ring_mean(floor(96) + floor(160))
            print(f"{preset}: baked light visible around the circle: {before:.2f} (sphere) -> {after:.2f} (column)")
            check(after >= VISIBLE, f"{preset}: standing in the circle keeps the floor around it lit ({after:.2f} >= {VISIBLE})")
            check(before < after - 0.15, f"{preset}: (the old sphere loses it: {before:.2f})")
            frames = {}
            for on in (True, False):
                game.call("graphics", glow_shadows=on)
                game.run(SETTLE)
                path = out / f"{preset}-centre-shadows-{'on' if on else 'off'}.png"
                game.present_screenshot(path)
                frames[on] = frame_luminance(path)
            keep = float(frames[True].mean() / max(frames[False].mean(), 1e-3))
            check(keep >= KEEP, f"{preset}: the room keeps {keep:.2f} of its light with Ashley in the circle (>= {KEEP})")
            game.call("graphics", glow_shadows=True)
            # Inside near the edge, and just outside the circle: his shadow.
            for name, dx in (("edge", 40), ("outside", 100)):
                place(dx, 0)
                game.run(SETTLE)
                shadow(f"{preset} {name}")
                # (on the other side, -x: floor in view up to 2 body lengths behind him)
                place(-dx, 0)
                game.run(SETTLE)
                frame_shadow(f"{preset} {name} (-x)", -dx, 0, f"{preset}-{name}")
            place(0, 0)
        # The user's report (2026-10-14): save room 29/0, a dimmer save point
        # glow, Ashley beside it: his shadow could not be seen.
        game.call("graphics", lighting_preset=args.presets.split(",")[0])
        game.call("room_goto", zone=29, room=0, enemies=False, scripts=False)
        for _ in range(40):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(60)
        state = game.state()
        save = [g for g in game.call("glows")["glows"] if g["name"].startswith("Save point glow")]
        check((state["zone"], state["room"]) == (29, 0) and len(save) == 1 and save[0]["shape"]["kind"] == "column",
              f"save room 29/0 and its save point column ({state['zone']}/{state['room']}, {len(save)})")
        if save and (state["zone"], state["room"]) == (29, 0):
            centre = save[0]["shape"]["centre"]
            axis[0], axis[1] = centre[0], centre[2]
            for name, dz in (("29/0 beside it", 100), ("29/0 the other side", -100)):
                place(0, dz)
                game.run(SETTLE)
                frame_shadow(name, 0, dz, name.replace("/", "-").replace(" ", "-"))
            place(0, 0)
            frames = {}
            for on in (False, True):
                game.call("graphics", glow_shadows=on)
                game.run(SETTLE)
                path = out / f"29-0-centre-shadows-{'on' if on else 'off'}.png"
                game.present_screenshot(path)
                frames[on] = frame_luminance(path).mean()
            keep = frames[True] / max(frames[False], 1e-3)
            check(keep >= KEEP, f"29/0: the room keeps {keep:.2f} of its light with Ashley in the circle (>= {KEEP})")
    finally:
        game.close()
    if not args.out:
        shutil.rmtree(lights, ignore_errors=True)
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
