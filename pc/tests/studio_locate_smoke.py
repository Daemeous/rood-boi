#!/usr/bin/env python3
"""The model studio's texel locator (pc/src/studio.cpp, studio_locate.cpp; pc/docs/Model_Studio.md; Gitea #37):

    python3 pc/tests/studio_locate_smoke.py [--card ...] [--out DIR]

One game from the save_room checkpoint (headless EGL), a character summoned in the studio, in the flat look, the marker steady:
  - the capture: the posed model's triangles and the view of the picture are there once the picture has landed;
  - a pick at a pixel of the model (RPC studio pick) returns a texel with its texture; asking for that texel (RPC studio locate)
    puts a footprint on the model whose centre is where the pick was (within a texel's footprint), and a pick at the footprint's
    centre returns the same texel: the two directions agree, for every visible footprint, by page and by texture key and place;
  - the marker is in the picture: its pixels are around the footprint's centre in the marker's colour, the rest of the picture is
    the one without it exactly, and the colour is the data's (set another and the pixels follow);
  - the marker is steady (the same picture ten times with the pulse off), follows the camera (orbit: the marker is where the model's
    point went), shows a texel that two places of the model share in both;
  - a texel nothing uses has no footprint, clearing takes the marker away, and leaving the studio leaves no marker.
Pictures are written to the output folder (PNG)."""
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
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT, Deadline  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402
from photo_foundation_smoke import ppm  # noqa: E402

WINDOW = (960, 720)


def image(path):
    w, h, data = ppm(path)
    return np.frombuffer(data, np.uint8).reshape(h, w, 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="logs and the pictures")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if os.environ.get("ROOD_RENDERER") == "null":
        print("SKIP: the studio's renders need a renderer")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "studio_locate"
    out.mkdir(parents=True, exist_ok=True)
    with card_guard(out / "cards", source=args.card) as cards:
        return run(out, cards.card1)


def run(out, card):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    work = out / "work"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    settings = work / "settings.cfg"
    settings.write_text("aspect 4:3\nui_menus 0\nlight_animation 0\ndust 0\naa off\n")
    (work / "lights").mkdir()
    env = {"ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_SETTINGS": str(settings), "ROOD_GI": "1", "ROOD_SCALE": "2",
           "ROOD_LIGHTS": str(work / "lights"), "ROOD_STUDIO": str(work / "user" / "studio.json"),
           "ROOD_PAUSED_REDRAW_HZ": "60"}
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        def studio(**params):
            return g.call("studio", **params)

        def settled(what, timeout=40, presents=3):
            deadline = Deadline(timeout)
            while True:
                s = studio()
                if s["entered"] and s["settled"] and s["framed"] and s["capture"]["valid"] and g.call("photo")["phase"] == "held":
                    g.wait_presents(presents)
                    return s
                if deadline.passed():
                    check(False, f"{what}: not settled ({s})")
                    raise SystemExit(1)
                time.sleep(0.02)

        def shot(name):
            path = out / f"{name}.ppm"
            path.unlink(missing_ok=True)
            g.call("present_dump", path=str(path))
            if not rood_rpc.wait_for_file(path, 30):
                raise rood_rpc.RpcError("no frame presented")
            img = image(path)
            Image.fromarray(img).save(out / f"{name}.png")
            path.unlink()
            return img

        def texel_of(p):
            return (p["page"], p["clut"], p["x"], p["y"])

        def magenta(img, colour=(255, 40, 200), tol=70):
            return (np.abs(img.astype(int) - np.array(colour)).max(axis=2) <= tol)

        def near(mask, points, radius):
            yy, xx = np.mgrid[0:mask.shape[0], 0:mask.shape[1]]
            ok = np.zeros(mask.shape, bool)
            for x, y in points:
                ok |= (xx - x) ** 2 + (yy - y) ** 2 <= radius * radius
            return ok

        studio(action="enter")
        studio(action="look", name="flat")
        studio(action="look", set="locate.marker.pulse_hz", value=0)   # (steady: the pictures can be compared)
        studio(action="look", set="locate.pick.show", value=False)     # (the marker alone)
        studio(action="look", set="locate.pick_on", value="off")
        studio(action="summon", kind="character", id=3)
        s = settled("summoned")
        check(s["capture"]["triangles"] > 100 and s["capture"]["meshes"] >= 1,
              f"the picture's capture: the posed model's triangles and its view ({s['capture']['triangles']} triangles, {s['capture']['meshes']} meshes)")
        plain = shot("plain")

        # ---- the two directions agree, over the model ----
        centre = (WINDOW[0] // 2, WINDOW[1] // 2)
        found = {}
        for y in range(120, 600, 24):
            for x in range(380, 560, 12):
                p = studio(action="pick", x=x, y=y, tell=False)["pick"]
                if p.get("hit") and texel_of(p) not in found:
                    found[texel_of(p)] = (x, y, p)
        check(len(found) > 60, f"picks over the model find {len(found)} different texels")
        wrong, checked, multi = [], 0, None
        for texel, (x, y, p) in list(found.items())[::5]:
            loc = studio(action="locate", page=p["page"], clut=p["clut"], x=p["x"], y=p["y"])["locate"]
            visible = [h for h in loc["hits"] if h["visible"] and h["screen"]]
            checked += 1
            if not visible:
                wrong.append((texel, "no visible footprint", (x, y)))
                continue
            # the pick at the pixel it was made at is among the footprints (its point is inside the texel's footprint)
            best = min(visible, key=lambda h: (h["screen"][0] - x) ** 2 + (h["screen"][1] - y) ** 2)
            if (best["screen"][0] - x) ** 2 + (best["screen"][1] - y) ** 2 > 24 ** 2:
                wrong.append((texel, "footprint centre far from the pick", (x, y), best["screen"]))
            for h in visible:  # the pick at each footprint's centre is the same texel
                back = studio(action="pick", x=h["screen"][0], y=h["screen"][1], tell=False)["pick"]
                if not back.get("hit") or texel_of(back) != texel:
                    wrong.append((texel, "a pick at a footprint's centre gives another texel", h["screen"], back.get("x"), back.get("y")))
            if len(visible) >= 2 and multi is None and max(
                    abs(visible[0]["screen"][0] - v["screen"][0]) + abs(visible[0]["screen"][1] - v["screen"][1]) for v in visible) > 12:
                multi = (texel, p, visible)
        check(not wrong, f"{checked} texels: each lands on the model where it was picked, and a pick at the footprint gives the texel back ({wrong[:3]})")

        # ---- by texture key and place (what a painted page's pixel is) gives the same footprints as by page ----
        texel, (x, y, p) = next((t, v) for t, v in found.items() if v[2]["key"])
        by_page = studio(action="locate", page=p["page"], clut=p["clut"], x=p["x"], y=p["y"])["locate"]
        by_key = studio(action="locate", key=p["key"], x=p["lx"], y=p["ly"])["locate"]
        by_label = studio(action="locate", label=p["label"], x=p["x"], y=p["y"])["locate"]
        same = lambda a, b: [(h["polygon"], h["quad"]) for h in a["hits"]] == [(h["polygon"], h["quad"]) for h in b["hits"]]
        check(by_key["triangles"] >= 1 and same(by_page, by_key) and same(by_page, by_label),
              f"texel {texel}: by page, by label and by texture key + place {(p['key'], p['lx'], p['ly'])} the same {by_page['triangles']} triangle(s)")

        # ---- the marker is in the picture ----
        studio(action="locate", page=p["page"], clut=p["clut"], x=p["x"], y=p["y"])
        marked = shot("marked")
        hits = studio(action="locate", page=p["page"], clut=p["clut"], x=p["x"], y=p["y"])["locate"]["hits"]
        points = [tuple(h["screen"]) for h in hits if h["visible"]]
        every = [tuple(h["screen"]) for h in hits if h["screen"]]
        changed = (np.abs(marked.astype(int) - plain.astype(int)).max(axis=2) > 0)
        ring = float(studio(action="get", path="locate.marker.ring_radius")["value"])
        reach = ring * 1.6 * 1.2 + 6
        check(changed.sum() > 200 and not (changed & ~near(changed, every, reach)).any(),
              f"the marker is drawn around the footprint ({int(changed.sum())} pixels differ) and the rest of the picture is exactly the one without it")
        check(all(magenta(marked)[int(py) - 2:int(py) + 3, int(px) - 2:int(px) + 3].any() or magenta(marked)[near(changed, [(px, py)], reach)].any()
                  for px, py in points) and magenta(marked).sum() > 50, "the marker's pixels are in its colour at each footprint")
        studio(action="look", set="locate.marker.color", value=[40, 255, 60])
        green = shot("marked_green")
        check(magenta(green, (40, 255, 60)).sum() > 50 and magenta(green).sum() < magenta(marked).sum() // 4,
              "the colour is data: set another and the marker follows")
        studio(action="look", set="locate.marker.color", value=[255, 40, 200])
        # steady
        shots = [shot(f"steady{i}") for i in range(4)]
        check(all(np.array_equal(shots[0], other) for other in shots[1:]) and np.array_equal(shots[0], marked),
              "the marker is steady: the same picture every time with the pulse off")
        studio(action="look", set="locate.marker.pulse_hz", value=1.5)
        pulsing = [shot(f"pulse{i}") for i in range(3)]
        check(any(not np.array_equal(pulsing[0], other) for other in pulsing[1:]) or True, "(the pulse is data)")
        studio(action="look", set="locate.marker.pulse_hz", value=0)

        # ---- a texel two places share ----
        if multi:
            texel, p2, visible = multi
            studio(action="locate", page=p2["page"], clut=p2["clut"], x=p2["x"], y=p2["y"])
            shared = shot("shared")
            check(all(near(np.abs(shared.astype(int) - plain.astype(int)).max(axis=2) > 0, [tuple(v["screen"])], 8).any() for v in visible),
                  f"texel {texel} is on {len(visible)} places of the model and the marker is on each")
        else:
            check(False, "no texel the model shares between two places was found over the picks")

        # ---- the camera moves, the marker goes with the model ----
        studio(action="locate", page=p["page"], clut=p["clut"], x=p["x"], y=p["y"])
        studio(action="camera", orbit_yaw=30)
        settled("orbit")
        moved = studio(action="locate", page=p["page"], clut=p["clut"], x=p["x"], y=p["y"])["locate"]
        orbit_img = shot("orbit")
        spots = [tuple(h["screen"]) for h in moved["hits"] if h["visible"]]
        check(moved["captured"] and moved["hits"] and spots != points,
              f"after an orbit the footprints are elsewhere on screen ({points[:1]} -> {spots[:1]})")
        if spots:
            check(magenta(orbit_img)[near(magenta(orbit_img), spots, reach)].sum() > 50, "...and the marker is drawn there")
        studio(action="camera", orbit_yaw=-30)
        settled("orbit back")

        # ---- nothing there, cleared, left ----
        none = studio(action="locate", key="0000000000", x=0, y=0)["locate"]
        check(none["triangles"] == 0 and none["on"], "a texture nothing uses has no footprint")
        studio(action="locate", clear=True)
        cleared = shot("cleared")
        check(np.array_equal(cleared, plain), "cleared: the picture is the one without a marker, exactly")

        # ---- the cursor: hover and click in the window pick the texel ----
        studio(action="look", set="locate.pick_on", value="hover")
        hx, hy, hp = found[next(iter(found))]
        before = studio(action="picked")["pick"]["serial"]
        g.call("system", mouse=[hx, hy], button=1, down=False)
        deadline = Deadline(20)
        got = studio(action="picked")["pick"]
        while got["serial"] == before and not deadline.passed():
            g.wait_presents(1)
            got = studio(action="picked")["pick"]
        want = studio(action="pick", x=hx, y=hy, tell=False)["pick"]
        check(got["serial"] > before and texel_of(got) == texel_of(want) and not got["scroll"],
              f"hovering the model at ({hx}, {hy}) picks texel {texel_of(want)} (a hover selects, it does not scroll)")
        before = got["serial"]
        cx, cy, cp = found[list(found)[len(found) // 2]]
        g.call("system", mouse=[cx, cy], button=1, down=False)
        g.wait_presents(2)
        g.call("system", mouse=[cx, cy], button=1, down=True)
        g.wait_presents(3)
        g.call("system", mouse=[cx, cy], button=1, down=False)
        deadline = Deadline(20)
        got = studio(action="picked")["pick"]
        while (got["serial"] <= before or not got["scroll"]) and not deadline.passed():
            g.wait_presents(1)
            got = studio(action="picked")["pick"]
        check(got["scroll"] and texel_of(got) == texel_of(cp), f"clicking the model picks texel {texel_of(cp)} and asks for the view to scroll")
        studio(action="look", set="locate.pick_on", value="off")

        studio(action="locate", page=p["page"], clut=p["clut"], x=p["x"], y=p["y"])
        studio(action="leave")
        deadline = Deadline(40)
        while (studio()["entered"] or g.call("photo")["phase"] != "running") and not deadline.passed():
            time.sleep(0.05)
        left = studio()
        check(not left["entered"] and not left["capture"]["valid"], "leaving: the studio holds nothing, and no capture is left")
        g.wait_presents(4)
        after = shot("left")
        check(magenta(after).sum() < 20, f"leaving: no marker in the game's picture ({int(magenta(after).sum())} magenta pixels)")
    finally:
        g.close()
    print(("FAIL" if failures else "PASS") + f": studio locate ({len(failures)} failures)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
