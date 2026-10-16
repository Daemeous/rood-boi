#!/usr/bin/env python3
"""The editor's transform tools (lights.cpp's gizmo, editor_transform.h): W,
E and R, and the toolbar's World / Local switch. R (scale) changes a light's
reach or size, a light actor's size (its lights' offsets and radii with it)
and a fog volume's size, and never brightness.

    python3 pc/tests/editor_transform_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

The same save room and objects as editor_selection_smoke.py (a point light,
a candle, a fog volume). Sections:

- keys: W, E and R pick move, rotate and scale;
- space: World / Local (settings key editor_gizmo_space) sets the move and
  rotate manipulators' mode; scale is always local;
- lights: R on a point light scales its radius, on a spot its range (the cone
  kept), on a rect its width, height and range per axis; the intensity and
  colour never change; each is one undo step named Scale ...;
- mouse: a real drag of the scale gizmo's centre 50 pixels right makes a
  point light's radius 1.5 times larger (ImGuizmo: 1% per pixel), one undo step;
- actor: R on the candle puts its light 1.5 times as far from the pivot with
  1.5 times the radius and the same intensity;
- fog: R on the fog volume scales its size per axis.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from editor_selection_smoke import finish, pinned_card, start  # noqa: E402
from editor_schema_smoke import close, schema, value  # noqa: E402


def transform(s, **params):
    return s.game.call("editor_transform", **params)


def fields(s, ident):
    return {k: v["value"] for k, v in schema(s, action="get", id=ident)["fields"].items()}


def changed(a, b):
    return sorted(k for k in b if k in a and not close(a[k], b[k]))


def check_keys(s):
    s.select(action="set", ids=[s.light])
    for key, op in (("r", 2), ("e", 1), ("w", 0), ("r", 2)):
        s.check(s.keys([key])["gizmo"] == op, f"{key.upper()} picks tool {op}")
    s.check(transform(s)["operation"] == 2, "editor_transform reports the scale tool")
    s.keys(["w"])


def check_space(s):
    g = s.game
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"type": "spot"})
    schema(s, action="set", id=s.light, values={"direction": [1, 0, 0]})
    for space in ("world", "local"):
        g.call("graphics", editor_gizmo_space=1 if space == "local" else 0)
        s.check(transform(s)["space"] == space, f"the space is {space}")
        for key, op, mode in (("w", "move", space), ("e", "rotate", space), ("r", "scale", "local")):
            s.keys([key])
            gz = transform(s)["gizmo"]
            s.check(gz["shown"] and gz["subject"] == s.light and gz["operation"] == op and gz["mode"] == mode,
                    f"{space}: {key.upper()} is {op} in {mode} space ({gz})")
    g.call("graphics", editor_gizmo_space=0)
    schema(s, action="set", id=s.light, values={"type": "point"})
    s.keys(["w"])


def check_lights(s):
    s.select(action="set", ids=[s.light])
    cases = [
        ("point", {}, [1, 1, 2], {"radius": 1200}),
        ("spot", {"cone": 50}, [1, 1, 1.5], {"radius": 900}),
        ("rect", {"width": 128, "height": 64}, [2, 0.5, 1.25], {"width": 256, "height": 32, "radius": 750}),
    ]
    for kind, extra, ratio, expect in cases:
        schema(s, action="set", id=s.light, values={"type": kind})
        schema(s, action="set", id=s.light, values={"radius": 600, "intensity": 1.5, **extra})
        before, done = fields(s, s.light), s.history()["done"]
        transform(s, action="scale", id=s.light, ratio=ratio)
        after, h = fields(s, s.light), s.history()
        s.check(changed(before, after) == sorted(expect) and all(close(after[k], v) for k, v in expect.items()),
                f"R {ratio} on a {kind} light: {changed(before, after)} -> {[after[k] for k in sorted(expect)]}")
        s.check(close(after["intensity"], 1.5) and close(after["color"], before["color"])
                and h["done"] == done + 1 and h["undo"].startswith("Scale"),
                f"{kind}: intensity and colour kept; one undo step ({h['undo']})")
    schema(s, action="set", id=s.light, values={"type": "point"})
    schema(s, action="set", id=s.light, values={"radius": 600, "intensity": 1})


def check_mouse(s):
    """A real drag of the scale gizmo's centre: ImGuizmo's uniform scale, 1% per pixel."""
    g = s.game
    s.select(action="set", ids=[s.light])
    s.keys(["r"])
    xy = s.objects()[s.light]["screen"]
    done = s.history()["done"]
    s.mouse(xy, down=True)
    s.mouse([xy[0] + 25, xy[1]], down=True)
    s.mouse([xy[0] + 50, xy[1]], down=True)
    s.mouse([xy[0] + 50, xy[1]], down=False)
    r, h = value(s, s.light, "radius"), s.history()
    s.check(close(r, 900, 0.01) and h["done"] == done + 1 and h["undo"].startswith("Scale"),
            f"the gizmo's centre dragged 50 px right: radius 600 -> {r} (900), one undo step ({h['undo']})")
    g.call("editor_undo", action="undo")
    s.check(close(value(s, s.light, "radius"), 600), "undo puts the radius back")
    s.keys(["w"])


def check_actor(s):
    g = s.game
    g.call("actors", time=0)  # the animation frozen: the light's position is exact
    g.run(2)

    def light_of():
        a = next(x for x in g.call("actors")["actors"] if x["id"] == s.actor)
        return a["position"], a["lights"][0]

    pivot, before = light_of()
    s.select(action="set", ids=[s.actor])
    transform(s, action="scale", id=s.actor, ratio=[1.5, 1.5, 1.5])
    pivot2, after = light_of()
    off0 = [b - p for b, p in zip(before["position"], pivot)]
    off1 = [b - p for b, p in zip(after["position"], pivot2)]
    s.check(close(off1, [1.5 * x for x in off0], 0.01) and close(after["radius"], 1.5 * before["radius"], 0.01),
            f"R on the candle: its light 1.5x as far from the pivot ({off0} -> {off1}) with 1.5x the radius "
            f"({before['radius']} -> {after['radius']})")
    s.check(close(after["intensity"], before["intensity"]) and close(value(s, s.actor, "scale"), 1.5),
            f"its intensity kept ({before['intensity']} -> {after['intensity']}); Scale reads 1.5")
    g.call("editor_undo", action="undo")
    s.check(close(value(s, s.actor, "scale"), 1), "undo: scale 1 again")


def check_fog(s):
    s.select(action="set", ids=[s.volume])
    before = value(s, s.volume, "size")
    transform(s, action="scale", ratio=[2, 1, 0.5])
    after = value(s, s.volume, "size")
    s.check(close(after, [before[0] * 2, before[1], before[2] * 0.5]) and s.history()["undo"] == "Scale fog volume",
            f"R on the fog volume: its size {before} -> {after} ({s.history()['undo']})")
    s.game.call("editor_undo", action="undo")


SECTIONS = [check_keys, check_space, check_lights, check_mouse, check_actor, check_fog]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (keys, space, ...)")
    args = parser.parse_args()
    s = start(args, "editor_transform")
    if s is None:
        return 77
    try:
        only = set(args.only.split(",")) if args.only else None
        for section in SECTIONS if not s.failures else []:
            name = section.__name__[len("check_"):]
            if only is None or name in only:
                print(f"-- {name}", flush=True)
                section(s)
    finally:
        status = finish(s, "W/E/R and World/Local; R changes shape (lights, actors, fog volumes), never brightness")
    return status


if __name__ == "__main__":
    sys.exit(main())
