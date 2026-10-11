#!/usr/bin/env python3
"""The editor viewport's overlays and snapping (editor_overlays.h): room
edges and points over the lit view, depth-aware and following the editor's
view into rooms, and the snap targets light drags use (point, edge, edge
midpoint, surface) with the points lit around a snapped point.

    python3 pc/tests/editor_snap_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Continues from a copy of the card (never the card itself; skipped without
one) into its save room, opens the editor and looks down into the room from
the free camera. Checks:

- settings file keys load (editor_wireframe, editor_snap_point, ...);
- the room's points and edges are found (welded; quads' diagonals are not
  edges), some hidden behind other surfaces and some visible, the occlusion
  test finishes;
- the overlays draw only with the editor open and change only the view;
- hiding a room group (Outliner) or showing the black shells changes what
  the overlay shows;
- point snapping finds the point under the cursor, with neighbours lit by
  a falloff (weight 1 at the point, falling with distance, none beyond the
  highlight radius); points win over edges;
- edge snapping lands on the edge under the cursor, midpoint snapping on
  its middle; surface snapping lands off the surface along its normal by
  the offset;
- applying a snap moves the selected light there (as a drag would).

--out keeps the frames for looking (game imagery: keep it out of git)."""
import argparse
import math
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
from rood_rpc import OUT, Game  # noqa: E402
import golden_frames as gf  # noqa: E402
from editor_cutaway_smoke import default_card, pose_looking_at  # noqa: E402

OVERLAYS_OFF = {"editor_wireframe": False, "editor_points": False, "editor_character_wireframe": False}
OVERLAYS_ON = {"editor_wireframe": True, "editor_points": True, "editor_character_wireframe": False}


def diff(a, b):
    return float(np.abs(a.astype(int) - b.astype(int)).max(axis=2).mean())


def segment_distance(p, a, b):
    """Window pixels from p to the segment a-b."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    ll = dx * dx + dy * dy
    t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / ll)) if ll > 0 else 0.0
    return math.hypot(p[0] - a[0] - dx * t, p[1] - a[1] - dy * t)


def close(a, b, tolerance=0.01):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the frames here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "editor_snap"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))  # no per-room lights
    settings = out / "settings.cfg"  # the keys this feature adds, as File > Save settings writes them
    settings.write_text("editor_snap_point 1\neditor_snap_highlight 96\neditor_occluded_opacity 0.3\n")
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "2",
           "ROOD_SETTINGS": str(settings)}
    game = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message)
        if not ok:
            failures.append(message)

    def frame(name):
        game.run(gf.SETTLE)
        path = out / f"{name}.png"
        game.present_screenshot(path)
        image = np.asarray(Image.open(path).convert("RGB"))
        if not args.out:
            path.unlink()
        return image

    try:
        game.wait_title()
        graphics = game.call("graphics")
        check(graphics.get("editor_snap_point") == 1 and graphics.get("editor_snap_highlight") == 96
              and abs(graphics.get("editor_occluded_opacity", 0) - 0.3) < 1e-6,
              "settings file keys load (editor_snap_point, editor_snap_highlight, editor_occluded_opacity)")
        game.call("graphics", **{**gf.DEFAULTS, "scale": 2, **OVERLAYS_OFF, "editor_snap_highlight": 128,
                                 "editor_hide_masks": True, "editor_game_cutaway": True, "editor_cutaway": False})
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        player = state.get("player")
        if not player or not game.call("scene")["triangles"]:
            print(f"FAIL: no room with Ashley after Continue: {state}")
            return 1
        focus = (player["x"], player["y"] - 80, player["z"])
        above = pose_looking_at((player["x"] + 696, player["y"] - 945, player["z"] - 642), focus)
        game.call("editor_camera", **above)
        game.run(gf.SETTLE)

        # The game picture with the editor closed: overlays never draw there.
        closed_off = frame("closed-off")
        game.call("graphics", **OVERLAYS_ON)
        closed_on = frame("closed-on")
        game.call("graphics", **OVERLAYS_OFF)
        closed_off2 = frame("closed-off2")
        noise, mean = diff(closed_off, closed_off2), diff(closed_on, closed_off2)
        check(mean <= 2 * noise + 0.3,
              f"editor closed: the overlays draw nothing (mean difference {mean:.2f}; idle animation {noise:.2f})")

        game.call("editor_ui", visible=True)
        game.call("graphics", **OVERLAYS_OFF)
        before = frame("overlays-off")
        game.call("graphics", **OVERLAYS_ON)
        after = frame("overlays-on")
        st = game.call("editor_overlay", points=256, edges=256)
        scene = game.call("scene")
        print(f"room: {scene['triangles']} triangles, {st['points']} points, {st['edges']} edges; shown "
              f"{st['shown_points']} points, {st['shown_edges']} edges; samples {st['visible_samples']} visible, "
              f"{st['occluded_samples']} hidden; drawn {st['drawn_lines']} lines, {st['drawn_points']} points in "
              f"{st['draw_ms']:.2f} ms")
        check(0 < st["points"] < scene["triangles"] * 3 and 0 < st["edges"] < scene["triangles"] * 3,
              "points and edges welded from the room's triangles")
        check(st["edges"] < scene["triangles"] * 3 / 2,
              f"quads' diagonals are not edges ({st['edges']} edges for {scene['triangles']} triangles)")
        check(st["pending_samples"] == 0, "the occlusion test finished")
        check(st["visible_samples"] > 0 and st["occluded_samples"] > 0,
              f"some edges/points visible, some behind surfaces ({st['visible_samples']} / {st['occluded_samples']})")
        check(st["drawn_lines"] > 100 and st["drawn_points"] > 50, "the overlay drew lines and points")
        game.run(10)  # a still view: the occlusion test has nothing left to do
        still = game.call("editor_overlay", update=False)
        check(still["pending_samples"] == 0 and still["occlusion_ms"] < 0.5,
              f"a still view costs no rays ({still['occlusion_ms']:.2f} ms; drawing {still['draw_ms']:.2f} ms)")
        x, y, w, h = [int(v) for v in st["view"]]
        changed = np.abs(before.astype(int) - after.astype(int)).max(axis=2) > 24
        inside = changed[y:y + h, x:x + w].mean()
        outside = changed.mean() * changed.size - changed[y:y + h, x:x + w].sum()
        check(inside > 0.01, f"wireframe and points change the view ({inside * 100:.1f}% of its pixels)")
        check(outside < 50, f"nothing drawn outside the view ({outside:.0f} pixels)")

        # The overlay follows what the free camera's view draws.
        groups = game.call("room_groups")["groups"]
        biggest = max((i for i, g in enumerate(groups)
                       if not g["black"] and not g["room_hides"] and g["flags"] & 255 == 255),
                      key=lambda i: groups[i]["polygons"])
        game.call("room_groups", hide=[biggest])
        game.run(4)
        hidden = game.call("editor_overlay")
        game.call("room_groups", show_all=True)
        game.run(4)
        check(hidden["shown_edges"] < st["shown_edges"],
              f"hiding group {biggest} hides its edges ({st['shown_edges']} -> {hidden['shown_edges']})")
        game.call("graphics", editor_hide_masks=False)
        game.run(4)
        shells = game.call("editor_overlay")
        game.call("graphics", editor_hide_masks=True)
        game.run(4)
        check(shells["shown_edges"] > st["shown_edges"],
              f"black shells shown: their edges too ({st['shown_edges']} -> {shells['shown_edges']})")

        # Point snapping: a visible point well apart from the others on screen.
        game.call("graphics", **OVERLAYS_OFF)
        st = game.call("editor_overlay", points=256, edges=256)
        points = [p for p in st["point_samples"] if not p["hidden"]]
        radius = game.call("graphics")["editor_snap_radius"]

        def apart(p):
            return min((math.dist(p["screen"], q["screen"]) for q in st["point_samples"] if q is not p), default=1e9)
        def crowd(p):  # points around it in the world (to light up)
            return sum(1 for q in st["point_samples"] if 0 < math.dist(q["position"], p["position"]) < 128)
        spaced = [p for p in points if apart(p) > radius + 4] or points
        target = max(spaced, key=lambda p: (crowd(p), apart(p)))
        cursor = (target["screen"][0] + 4, target["screen"][1] + 3)
        snap = game.call("editor_snap", x=cursor[0], y=cursor[1], modes=["point"], highlight=True)
        print(f"point snap at {cursor}: {snap['type']} {snap.get('position')}, "
              f"{len(snap.get('neighbours', []))} neighbours")
        check(snap["type"] == "point" and snap["distance"] <= 5.3,
              f"point snap finds the point under the cursor ({snap.get('position')}, {snap.get('distance', 0):.1f} px)")
        if apart(target) > radius + 4:
            check(close(snap["position"], target["position"]), f"... it is {target['position']}")
        near = snap.get("neighbours", [])
        check(near and close(near[0]["position"], target["position"]) and abs(near[0]["weight"] - 1) < 1e-3,
              "the snapped point is lit fully")
        distances = [math.dist(n["position"], snap["position"]) for n in near]
        weights = [n["weight"] for n in near]
        check(len(near) > 1, f"neighbours light up ({len(near) - 1} within {snap['highlight_radius']:.0f} units)")
        check(all(d < snap["highlight_radius"] for d in distances)
              and all(a >= b for a, b in zip(weights, weights[1:]))
              and all(0 < wt <= 1 for wt in weights),
              "neighbour weights fall off with distance inside the highlight radius")
        snapped = frame("snap-point")
        both = game.call("editor_snap", x=cursor[0], y=cursor[1], modes=["point", "edge", "surface"])
        check(both["type"] == "point", f"a point wins over edges and surfaces ({both['type']})")
        none = game.call("editor_snap", x=-500, y=-500, modes=["point", "edge"])
        check(none["type"] == "none", "nothing to snap to away from the view")

        # Edge snapping: a long visible edge, a few pixels to its side.
        edges = [e for e in st["edge_samples"] if not e["hidden"] and math.dist(e["screen"][:2], e["screen"][2:]) > 80]
        check(len(edges) > 0, f"long visible edges to test ({len(edges)})")
        def isolation(e):
            ax, ay, bx, by = e["screen"]
            q = (ax + (bx - ax) * 0.3, ay + (by - ay) * 0.3)
            m = ((ax + bx) / 2, (ay + by) / 2)
            others = [o for o in st["edge_samples"] if o is not e]
            return min(min((segment_distance(q, o["screen"][:2], o["screen"][2:]) for o in others), default=1e9),
                       min((segment_distance(m, o["screen"][:2], o["screen"][2:]) for o in others), default=1e9),
                       min((math.dist(q, p["screen"]) for p in st["point_samples"]), default=1e9))
        if edges:
            edge = max(edges, key=isolation)
            alone = isolation(edge) > radius + 6
            ax, ay, bx, by = edge["screen"]
            length = math.hypot(bx - ax, by - ay)
            nx, ny = -(by - ay) / length, (bx - ax) / length
            mx, my = (ax + bx) / 2 + nx * 4, (ay + by) / 2 + ny * 4
            # A quarter along, away from the midpoint and the end points.
            qx, qy = ax + (bx - ax) * 0.3 + nx * 4, ay + (by - ay) * 0.3 + ny * 4
            on = game.call("editor_snap", x=qx, y=qy, modes=["edge"], highlight=True)
            a, b = edge["a"], edge["b"]
            if on["type"] == "edge":
                p = on["position"]
                ab = [b[i] - a[i] for i in range(3)]
                t = sum((p[i] - a[i]) * ab[i] for i in range(3)) / sum(v * v for v in ab)
                off = math.dist(p, [a[i] + ab[i] * t for i in range(3)])
            else:
                t, off = -1, 1e9
            check(on["type"] == "edge" and on["distance"] <= 4.05,
                  f"edge snap lands on an edge under the cursor ({on.get('distance', 0):.2f} px)")
            if alone:
                check(-1e-3 <= t <= 1 + 1e-3 and off < 0.5,
                      f"... on this edge, perspective-correct (t {t:.2f}, {off:.2f} units off it)")
            frame("snap-edge")
            mid = game.call("editor_snap", x=mx, y=my, modes=["edge", "midpoint"])
            middle = [(a[i] + b[i]) / 2 for i in range(3)]
            check(mid["type"] == "midpoint" and mid["distance"] <= radius,
                  f"midpoint snap near the cursor ({mid['type']}, {mid.get('distance', 0):.2f} px)")
            if alone:
                check(close(mid["position"], middle, 0.05), f"... the middle {middle} ({mid.get('position')})")

        # Surface snapping: off the surface along its normal by the offset.
        surface = game.call("editor_snap", u=0.5, v=0.6, modes=["surface"], highlight=True)
        offset = game.call("graphics")["editor_snap_offset"]
        if surface["type"] == "surface":
            lift = [surface["position"][i] - surface["surface"][i] for i in range(3)]
            along = sum(lift[i] * surface["normal"][i] for i in range(3))
            check(abs(math.hypot(*lift) - offset) < 0.1 and abs(along - offset) < 0.1,
                  f"surface snap: {offset:.0f} units off the surface along its normal ({along:.2f})")
        else:
            check(False, f"surface snap found the room under the view's centre ({surface['type']})")
        frame("snap-surface")
        game.call("editor_snap", clear=True)

        # Applying a snap moves the selected light (a drag's last frame).
        game.call("lights", action="add", position=[player["x"], player["y"] - 160, player["z"]])
        moved = game.call("editor_snap", x=cursor[0], y=cursor[1], modes=["point"], apply=True)
        light = game.call("lights")["lights"][-1]
        check(moved["type"] == "point" and close(light["position"], snap["position"]),
              f"the selected light moved to the snapped point ({light['position']})")
        game.call("editor_ui", visible=False)
        game.call("editor_camera", off=1)
        del snapped
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
    print("PASS: overlays follow the view; point, edge, midpoint and surface snapping land where they should")
    return 0


if __name__ == "__main__":
    sys.exit(main())
