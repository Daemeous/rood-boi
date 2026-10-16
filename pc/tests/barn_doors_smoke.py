#!/usr/bin/env python3
"""Barn doors and a spot's inner cone in the renderers (light_shape.h,
pc/shaders/vulkan/light_shape.glsl), measured in the presented picture.
Run it under OpenGL, and under Vulkan with run.py --arch 64 --renderer vulkan.

    python3 pc/tests/barn_doors_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

In the card's save room (scratch ROOD_LIGHTS, the editor closed, the
Lighting only view, the animation frozen), a rect light hangs 96 units above
the floor beside Ashley, facing down. Frames: the light off, on without
doors, on with 96-unit doors pointing straight down. Under the light the
doors change little; 128 units to the side they leave at most a quarter of
its light. Then the light becomes a spot (a 60 degree cone, 128 above the
floor): 20 degrees off its axis an inner cone of 20 degrees leaves less light
than the automatic edge, and on its axis the same.
"""
import argparse
import math
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import golden_frames as gf  # noqa: E402
from editor_selection_smoke import pinned_card  # noqa: E402

RECT_HEIGHT = 96  # the rect light above the floor
SIDE = 128  # the floor point beside it that its doors hide
SPOT_HEIGHT = 128
OFF_AXIS = SPOT_HEIGHT * math.tan(math.radians(20))  # 20 degrees off the spot's axis


def lum(image, xy, r=2):
    """Mean luminance (0-255) of the (2r+1)^2 pixels around xy."""
    x, y = int(round(xy[0])), int(round(xy[1]))
    box = image[y - r:y + r + 1, x - r:x + r + 1].astype(float)
    return float((box[..., 0] * 0.3 + box[..., 1] * 0.59 + box[..., 2] * 0.11).mean())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "barn_doors"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    settings = out / "settings.cfg"
    settings.write_text("# barn_doors: scratch settings\naa fxaa\n")
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_SETTINGS": str(settings), "ROOD_SCALE": "2"}
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-barn-{os.getpid()}.sock"  # sun_path is short
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    def schema(**params):
        return g.call("editor_schema", **params)

    def floor_pixel(point):
        """The window pixel of a floor point, when the floor (nothing in front of it) is there."""
        xy = g.call("editor_select", action="project", point=point)["pixel"]
        if xy is None:
            return None
        hit = g.call("editor_select", action="pick", x=xy[0], y=xy[1])["world"]
        return xy if hit and math.dist(hit, point) < 10 else None

    try:
        g.call("graphics", **{**gf.DEFAULTS, "scale": 2, "view_mode": 2, "gi": False})
        g.call("actors", time=0)
        g.run(gf.SETTLE)
        p = g.state()["player"]
        found = None
        for dx, dz in ((240, 0), (-240, 0), (0, 240), (0, -240), (320, 160), (-320, -160)):
            c = [p["x"] + dx, p["y"], p["z"] + dz]
            points = [c, [c[0] + SIDE, c[1], c[2]], [c[0] + OFF_AXIS, c[1], c[2]]]
            pixels = [floor_pixel(q) for q in points]
            if all(pixels):
                found = (c, pixels)
                break
        if not check(found is not None, "a floor spot beside Ashley with the points under and beside it in view"):
            return 1
        c, (at_c, at_side, at_q) = found
        light = g.call("lights", action="add", position=[c[0], c[1] - RECT_HEIGHT, c[2]])["lights"][-1]["id"]
        schema(action="set", id=light, values={"type": "rect"})
        schema(action="set", id=light, values={"direction": [0, 1, 0], "width": 64, "height": 64, "radius": 800,
                                               "intensity": 1, "color": [1, 1, 1], "enabled": False})
        off = gf.settled(g, out / "off")
        schema(action="set", id=light, values={"enabled": True})
        lit = gf.settled(g, out / "rect-open")
        schema(action="set", id=light, values={"barn_length": 96, "barn_angle": 0})
        doors = gf.settled(g, out / "rect-doors")
        lit_c, lit_s = lum(lit, at_c) - lum(off, at_c), lum(lit, at_side) - lum(off, at_side)
        door_c, door_s = lum(doors, at_c) - lum(off, at_c), lum(doors, at_side) - lum(off, at_side)
        check(lit_c > 12 and lit_s > 6, f"the rect light lights the floor under it and beside it ({lit_c:.1f}, {lit_s:.1f})")
        check(door_c >= 0.85 * lit_c, f"under it the doors change little ({door_c:.1f} of {lit_c:.1f})")
        check(door_s <= 0.25 * lit_s, f"beside it the doors cut the light ({door_s:.1f} of {lit_s:.1f})")
        schema(action="set", id=light, values={"type": "spot"})
        schema(action="set", id=light, values={"position": [c[0], c[1] - SPOT_HEIGHT, c[2]], "direction": [0, 1, 0],
                                               "cone": 60, "inner": 0, "radius": 900, "intensity": 0.6})
        auto = gf.settled(g, out / "spot-auto")
        schema(action="set", id=light, values={"inner": 20})
        narrow = gf.settled(g, out / "spot-inner20")
        a_q, n_q = lum(auto, at_q) - lum(off, at_q), lum(narrow, at_q) - lum(off, at_q)
        a_c, n_c = lum(auto, at_c) - lum(off, at_c), lum(narrow, at_c) - lum(off, at_c)
        check(a_q > 6 and a_q > 1.25 * n_q,
              f"20 degrees off the axis an inner cone of 20 leaves less light than the automatic edge ({n_q:.1f} vs {a_q:.1f})")
        check(abs(a_c - n_c) <= 0.1 * max(a_c, 1), f"on the axis both are the same ({n_c:.1f} vs {a_c:.1f})")
    finally:
        g.close()
        shutil.rmtree(lights, ignore_errors=True)
    log = pathlib.Path(g.log).read_text(errors="replace")
    if "[crash]" in log or "[gl] shader" in log or "[vk] validation:" in log:
        print("FAIL: crash or shader error; see", g.log)
        return 1
    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: barn doors cut a rect light's spill; a spot's inner cone sets its edge")
    return 0


if __name__ == "__main__":
    sys.exit(main())
