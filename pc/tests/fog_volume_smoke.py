#!/usr/bin/env python3
"""The fog volumes' own renderer (atmosphere.h pc_fogvol_gpu; pc/shaders/vulkan/
fog_volume.glsl; Vulkan vk_fogvol.inc, OpenGL gl_present.c;
pc/docs/plans/2026-10-14-fog-volume-renderer.md) and the haze's stability,
with the volumes' debug views (RPC environment {fog_volumes: {debug}}: 1 their
light alone, 2 their transmittance):

- thickness: a dense volume hides what is behind it (opacity >= 0.9 at its
  core), and its own opacity caps that (the room's haze limits do not);
- depth: the scene in front of a volume is not fogged (Ashley stays visible
  in front of a volume behind her);
- light response: a red light inside a volume brightens and reddens it; the
  light gone, it is back as it was;
- a dark room (Undercity West, zone 48 room 4): the volume's light is above
  a floor from its ambient term (the room's baked light around it), and
  darker without it;
- stability: with the camera still and the fog's time frozen, the picture
  changes by less than a threshold from one presented frame to the next,
  for the room's haze and ground fog (the froxels) and for a volume;
- the renderer off (fog_volume_render 0): the volumes go back into the haze;
- its fields save with the volume; no crash, shader error or validation message.

    python3 pc/tests/fog_volume_smoke.py [--shots DIR]
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
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_cutaway_smoke import default_card  # noqa: E402

VOL = "fvsmoke000001"
# Frame-to-frame change with everything still (mean over the picture, 0..255): the haze's froxels
# and the volumes' march converge on a fixed cycle of samples. (Vulkan before: 0.12 and 0.29, 99.9th
# percentile 2.0 and 6.0; after: 0.03 and 0.04, 0.7 and 1.0; OpenGL: 0.)
STILL_MEAN, STILL_P999 = 0.08, 2.5


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--card", default=str(default_card()))
    ap.add_argument("--shots", help="keep the frames here")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = OUT / "fog_volume_smoke"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    shots = pathlib.Path(args.shots) if args.shots else out
    shots.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.card, out / "card1.mcd")
    user = out / "user"
    user.mkdir()
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    # Lights hold still (their flicker animates on the wall clock, paused or not): what is left
    # changing between still frames is the renderers' own.
    (out / "settings.cfg").write_text("light_animation 0\n")
    env = {"ROOD_LIGHTS": str(user), "ROOD_LIGHTING": str(ROOT / "lighting"), "ROOD_GI": "1" if vulkan else "0",
           "ROOD_SCALE": "2", "ROOD_LIGHTING_PRESET": "ultra", "ROOD_PERF": "1",
           "ROOD_SETTINGS": str(out / "settings.cfg")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-fogvol-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    game = launch_at("save_room", card=out / "card1.mcd", fast=True, log=out / "game.log", extra_env=env,
                     socket_path=socket)
    # the save room is a Circle of Magic: no Travel prompt plate over the debug views it reads
    game.call("fast_travel", set={"prompt": False})

    def shot(name, frames=20):
        if frames:
            game.run(frames)
        path = game.present_screenshot(shots / f"fogvol-{name}.png")
        return np.asarray(Image.open(path).convert("RGB"), np.float32) / 255.0

    def debug(view, name, frames=20):
        game.call("environment", fog_volumes={"debug": view})
        image = shot(name, frames)
        game.call("environment", fog_volumes={"debug": 0})
        return image

    def still(name, count=8):
        """Consecutive presented frames while paused (the game redraws): their change."""
        game.call("pause", paused=True)
        game.wait_presents(60)  # settled: presented frames counted, not a second guessed (a stopped game draws none)
        frames = []
        for i in range(count):
            path = game.present_screenshot(shots / f"fogvol-{name}-{i}.png")
            frames.append(np.asarray(Image.open(path).convert("RGB"), np.float32))
            time.sleep(0.05)
        game.call("pause", paused=False)
        d = np.stack([np.abs(a - b).mean(axis=2) for a, b in zip(frames, frames[1:])])
        return float(d.mean()), float(np.percentile(d, 99.9))

    def lum(image):
        return image @ np.array([0.2126, 0.7152, 0.0722], np.float32)

    def goto(zone, room):
        game.call("room_goto", zone=zone, room=room, enemies=False, scripts=False)
        for _ in range(120):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(60)

    try:
        st = game.state()
        p = st["player"]
        e = game.call("environment", fog_time=5)
        fv = e["fog_volumes"]
        check(fv["render"] is True and fv["drawn"] == [], f"the renderer is on by default, nothing to draw here ({fv})")
        # Thickness: a dense box around Ashley (on the save point) hides the floor at its core.
        vol = {"id": VOL, "pos": [p["x"], p["y"] - 120, p["z"]], "size": [420, 240, 420], "density": 0.08,
               "edge": 0.3, "noise": 0, "detail": 0, "color": [1, 1, 1], "fill": [0.02, 0.02, 0.02], "opacity": 1}
        game.call("environment", volumes=[vol])
        game.run(40)
        fv = game.call("environment")["fog_volumes"]
        check(len(fv["drawn"]) == 1, f"the volume is drawn by its own renderer ({fv['drawn']})")
        t = debug(2, "transmittance")[..., 0]
        region = t < 0.98
        core = np.percentile(t[region], 10) if region.any() else 1.0
        check(0.01 < region.mean() < 0.8 and core <= 0.1,
              f"a dense volume reaches opacity >= 0.9 (its densest tenth lets {core:.3f} through; "
              f"it covers {region.mean() * 100:.1f} % of the view)")
        game.call("environment", volumes=[{"id": VOL, "opacity": 0.5}],
                  haze={"max_opacity": 0.2, "max_light": 0.05})
        t = debug(2, "transmittance-capped")[..., 0]
        least = float(t[region].min()) if region.any() else 0
        check(0.45 <= least <= 0.6, f"its own opacity caps it, the room's haze limits do not (least transmittance "
                                    f"{least:.3f} with opacity 0.5 and haze_max_opacity 0.2)")
        game.call("environment", volumes=[{"id": VOL, "opacity": 1}], haze={"max_opacity": 0.55, "max_light": 0.15})
        # Depth: the same volume under Ashley's floor (y down) is hidden by it (where the room has a
        # floor under the view: a pit or a ledge may still show a little of it).
        above = t < 0.98
        game.call("environment", volumes=[{"id": VOL, "pos": [p["x"], p["y"] + 200, p["z"]]}])
        under = debug(2, "under-floor", 60)[..., 0] < 0.98
        kept = float(under[above].mean()) if above.any() else 1.0
        check(above.mean() > 0.01 and kept < 0.25,
              f"a volume under the floor is hidden by it (of the pixels it covered above the floor, "
              f"{kept * 100:.1f} % still see it; {under.mean() * 100:.1f} % of the view, past ledges)")
        game.call("environment", volumes=[{"id": VOL, "pos": [p["x"], p["y"] - 120, p["z"]]}])
        # Light response: a red light in the volume (a lighter one: its light inside matters), beside
        # Ashley (inside her, the scene's shadow rays find her). Reading the room's files again drops
        # unsaved volumes: it is put back each time.
        light_vol = dict(vol, density=0.02, noise=0.5, detail=0.8, pos=[p["x"], p["y"] - 120, p["z"]])
        game.call("environment", volumes=[light_vol])
        lit0 = debug(1, "light-before", 60)
        region = debug(2, "light-region")[..., 0] < 0.9
        name = f"zone{st['zone']:02d}_room{st['room']:02d}.txt"
        (user / name).write_text("rood-lighting 2\nlight fvsmokered0001 name=Red type=point "
                                 f"pos={p['x'] + 140},{p['y'] - 100},{p['z']} color=1,0.1,0.05 intensity=3 radius=600\n")
        game.call("lights", action="reload")
        game.call("environment", volumes=[light_vol])
        lit1 = debug(1, "light-red", 90)
        (user / name).write_text("rood-lighting 2\n")
        game.call("lights", action="reload")
        game.call("environment", volumes=[light_vol])
        lit2 = debug(1, "light-after", 90)
        check(len(game.call("environment")["fog_volumes"]["drawn"]) == 1, "the volume is back after the reloads")
        if region.any():
            r0, r1, r2 = (float(x[region][:, 0].mean()) for x in (lit0, lit1, lit2))
            g0, g1 = (float(x[region][:, 1].mean()) for x in (lit0, lit1))
            check(r1 > r0 * 1.2 + 0.02 and r1 / max(g1, 1e-3) > r0 / max(g0, 1e-3) * 1.15,
                  f"a red light inside brightens and reddens it (red {r0:.3f} -> {r1:.3f}, red/green "
                  f"{r0 / max(g0, 1e-3):.2f} -> {r1 / max(g1, 1e-3):.2f})")
            check(abs(r2 - r0) < 0.25 * r0 + 0.01, f"the light gone, it is as before (red {r2:.3f}, before {r0:.3f})")
        else:
            check(False, "the volume covers part of the view")
        # The renderer off: the volumes are in the haze again (and drawn by it, under its limits).
        game.call("environment", fog_volumes={"render": 0})
        game.run(20)
        fv = game.call("environment")["fog_volumes"]
        check(fv["drawn"] == [], "fog_volume_render 0: the renderer draws nothing (the haze has the volume)")
        game.call("environment", fog_volumes={"render": 1})
        # Its fields save with the volume.
        game.call("environment", volumes=[{"id": VOL, "brightness": 1.5, "glow": [0.1, 0.2, 0.05], "taper": 0.4,
                                           "phase": 0.5, "ambient": 0.8, "glow_core": 0.7, "detail_size": 220,
                                           "confine": False}])
        game.call("lighting", action="save")
        saved = (user / name).read_text() if (user / name).exists() else ""
        line = next((l for l in saved.splitlines() if l.startswith(f"fog {VOL} ")), "")
        check(all(k in line for k in ("brightness=1.5", "glow=0.1,0.2,0.05", "taper=0.4", "phase=0.5", "ambient=0.8",
                                      "glow_core=0.7", "detail_size=220", "confine=0")),
              f"its look saves with it ({line[:200]})")
        game.call("environment", volumes=[{"id": VOL, "remove": True}])
        (user / name).unlink(missing_ok=True)
        game.call("lighting", action="reload")

        # A dark room: the Undercity, the user's rolling fog around Ashley.
        goto(48, 4)
        p = game.state()["player"]
        check(game.state()["zone"] == 48, "warped to Undercity West (zone 48 room 4)")
        game.call("environment", fog_time=5)
        mean0, p0 = still("still-room")
        check(mean0 < STILL_MEAN and p0 < STILL_P999,
              f"the room's haze and ground fog are steady (frame to frame: mean {mean0:.3f}, 99.9th "
              f"percentile {p0:.2f} of 255)")
        vol = {"id": VOL, "preset": "rolling_fog", "pos": [p["x"], p["y"] - 70, p["z"]], "size": [900, 140, 900],
               "density": 0.016, "sim_swirl": 2.5, "detail": 0.85, "shadow": 0.5, "fill": [0, 0, 0], "sim": True}
        game.call("environment", volumes=[vol])
        game.call("environment", drop_volume=VOL)
        game.run(120)
        region = debug(2, "dark-region")[..., 0] < 0.5
        light = lum(debug(1, "dark-light"))
        game.call("environment", fog_volumes={"ambient": 0})
        unlit = lum(debug(1, "dark-no-ambient"))
        game.call("environment", fog_volumes={"ambient": 1})
        if check(region.mean() > 0.01, f"the volume covers part of the Undercity's view ({region.mean() * 100:.1f} %)"):
            a, b = float(light[region].mean()), float(unlit[region].mean())
            check(a > 0.03 and a > b * 1.2,
                  f"fog in a dark room is dim grey, not black (its light {a:.3f}, {b:.3f} without the ambient term)")
        mean1, p1 = still("still-volume")
        check(mean1 < STILL_MEAN and p1 < STILL_P999,
              f"a volume is steady (frame to frame: mean {mean1:.3f}, 99.9th percentile {p1:.2f} of 255)")
        if vulkan:
            game.call("perf", reset=True)
            game.run(240)
            sections = game.call("perf").get("sections") or {}
            cost = sum(v for k, v in sections.items() if k.startswith("fog "))
            print(f"info: fog volume passes {cost:.3f} ms/frame ("
                  + ", ".join(f"{k} {v:.3f}" for k, v in sections.items() if k.startswith("fog ")) + ")")
        game.call("environment", volumes=[{"id": VOL, "remove": True}])
    finally:
        game.close()
    bad = []
    log = out / "game.log"
    if log.exists():
        bad = [line for line in log.read_text(errors="replace").splitlines()
               if "validation:" in line or "[crash]" in line or "[gl] shader" in line or "[fog] the fog" in line]
    check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))
    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
