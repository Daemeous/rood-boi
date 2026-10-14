#!/usr/bin/env python3
"""The room's haze, ground fog, fog volumes and sun shafts
(pc/src/atmosphere.cpp, pc/shaders/vulkan/medium.glsl; pc/docs/Vulkan_Renderer.md
"Haze, ground fog and sun shafts"). Frame-based, on the renderer of
ROOD_RENDERER (OpenGL: ptgi.c's traced march; Vulkan: the froxel volume), with
the haze's light alone (RPC graphics {haze_debug}) for most checks:

- the save room (zone 9 room 17, roofed): no generated fog or volumes there;
  a ground fog's light follows its height (a layer 3 units tall leaves
  little, a taller layer gives more); a fog volume lights only its
  bounds (smaller box: less, a sphere inside the box: less, removed: none);
  the settings save to the user layer as `env fog` / `fog <id>` lines and
  reload;
- Snowfly Forest (zone 40 room 6, open to the sky): the generated layer gives
  it a ground fog; the moon's shafts: with its shadows the haze's light varies
  across the view (shadowed air darker than lit air), without them it is even;
  the haze is steady from frame to frame when nothing changes;
- no crash, shader error or validation message.
"""
import argparse
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_cutaway_smoke import default_card  # noqa: E402
import lighting_format as lf  # noqa: E402

ROOFED = (9, 17)
OUTDOOR = (40, 6)  # Snowfly Forest: trees between the moon and the camera
EMISSIVE_FOG = {"density": 0.01, "height": 32, "offset": 0, "noise": 0, "color": [0, 0, 0], "fill": [0.6, 0.6, 0.6],
                "sky": 0}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--card", default=str(default_card()))
    ap.add_argument("--shots", help="keep the screenshots here")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    gen = ROOT / "lighting/default/generated" / lf.room_file_name(*OUTDOOR)
    if not gen.exists() or "env fog" not in gen.read_text():
        print(f"SKIP: no generated fog for zone {OUTDOOR[0]} room {OUTDOOR[1]} (pc/tools/generate_lighting.py)")
        return 77
    out = OUT / "atmosphere_smoke"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    shots = pathlib.Path(args.shots) if args.shots else out
    shots.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.card, out / "card1.mcd")
    packs, user = out / "packs", out / "user"
    shutil.copytree(ROOT / "lighting", packs)
    user.mkdir()
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    env = {"ROOD_LIGHTS": str(user), "ROOD_LIGHTING": str(packs), "ROOD_GI": "1", "ROOD_SCALE": "2",
           "ROOD_LIGHTING_PRESET": "ultra"}
    if vulkan and os.environ.get("VK_LAYER_PATH"):
        env.setdefault("ROOD_VK_VALIDATE", "1")
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-atmo-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    game = launch_at("save_room", card=out / "card1.mcd", fast=True, log=out / "game.log", extra_env=env,
                     socket_path=socket)
    check(game.call("environment")["fog_global"] is True, "the ground fog and volumes are on by default (settings key fog)")

    def warp(zone, room):
        game.call("room_goto", zone=zone, room=room)
        for _ in range(90):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(60)
        st = game.state()
        return (st["zone"], st["room"]) == (zone, room)

    def shot(name, frames=90):
        game.run(frames)
        path = game.present_screenshot(shots / f"atmo-{name}.png")
        return np.asarray(Image.open(path).convert("RGB"), np.float32) @ np.array([0.299, 0.587, 0.114], np.float32)

    def view(image):
        """The 3D view: below the HUD's top band, above its bottom one."""
        h = image.shape[0]
        return image[int(h * 0.12):int(h * 0.78)]

    try:
        st = game.state()
        if (st["zone"], st["room"]) != ROOFED and not warp(*ROOFED):
            check(False, f"in the save room (zone {st['zone']} room {st['room']})")
            return 1
        graphics = game.call("graphics")
        print(f"renderer {'vulkan' if vulkan else 'opengl'}, preset {graphics.get('lighting_preset')}", flush=True)
        traced = graphics.get("lighting_preset") == "ultra"

        # ---- the roofed save room ----
        e = game.call("environment", fog_time=5)
        check(not e["fog"]["defined"] and not e["volumes"], "the save room has no generated ground fog or volumes")
        floor, (lo, hi) = e["fog"]["floor"], e["bounds"]
        check(floor is not None and lo[1] - 1 <= floor <= hi[1] + 1, f"its floor is found ({floor}, bounds y {lo[1]}..{hi[1]})")
        game.call("environment", haze={"density": 0})
        game.call("graphics", haze_debug=1)
        nothing = view(shot("room-nothing", 30))
        check(nothing.mean() < 1.0, f"no haze, no fog: the haze's light is black ({nothing.mean():.2f})")
        game.call("environment", fog=EMISSIVE_FOG)
        base = view(shot("fog-floor"))
        game.call("environment", fog={"height": 3})
        thin = view(shot("fog-thin"))
        game.call("environment", fog={"height": 96})
        tall = view(shot("fog-tall"))
        check(base.mean() > 3, f"a ground fog on the floor scatters light ({base.mean():.1f})")
        check(thin.mean() < base.mean() * 0.4, f"its density falls off with height: a layer 3 units tall on the floor "
                                               f"(under each point) leaves {thin.mean():.1f} (32 tall: {base.mean():.1f})")
        check(tall.mean() > base.mean() * 1.3, f"a taller layer holds more ({tall.mean():.1f} vs {base.mean():.1f})")
        game.call("environment", remove_fog=True)
        centre = [(lo[0] + hi[0]) / 2, (floor if floor is not None else hi[1]) - 100, (lo[2] + hi[2]) / 2]
        vol = {"id": "test0000000001", "pos": centre, "size": [240, 200, 240], "density": 0.01, "edge": 0.2,
               "noise": 0, "color": [0, 0, 0], "fill": [0.6, 0.6, 0.6]}
        e = game.call("environment", volumes=[vol])
        check(len(e["volumes"]) == 1 and e["volumes"][0]["id"] == "test0000000001", "a fog volume is added")
        box = view(shot("volume-box")) > 6
        game.call("environment", volumes=[{"id": "test0000000001", "size": [120, 100, 120]}])
        small = view(shot("volume-small")) > 6
        game.call("environment", volumes=[{"id": "test0000000001", "size": [240, 200, 240], "shape": "sphere"}])
        sphere = view(shot("volume-sphere")) > 6
        check(0.005 < box.mean() < 0.7, f"the volume lights a part of the view ({box.mean() * 100:.1f} %)")
        check(0 < small.mean() < box.mean() * 0.7 and (small & ~box).mean() < 0.002,
              f"a smaller box lights less, inside the larger one's ({small.mean() * 100:.1f} % vs {box.mean() * 100:.1f} %)")
        check(sphere.mean() < box.mean() and (sphere & ~box).mean() < 0.002,
              f"the sphere inside the box lights less ({sphere.mean() * 100:.1f} %)")
        game.call("environment", volumes=[{"id": "test0000000001", "remove": True}])
        gone = view(shot("volume-gone"))
        check((gone > 6).mean() < 0.001, f"removed: nothing left ({(gone > 6).mean() * 100:.2f} %)")

        # ---- settings: save, reload ----
        game.call("graphics", haze_debug=0)
        game.call("environment", fog={"density": 0.0042, "height": 55}, volumes=[dict(vol, id="test0000000002")])
        saved = game.call("lighting", action="save")
        path = user / lf.room_file_name(*ROOFED)
        text = path.read_text() if path.exists() else ""
        lines = [line for line in text.splitlines() if line.startswith(("env fog", "fog "))]
        check(saved.get("ok") and any(l.startswith("env fog") and "density=0.0042" in l and "height=55" in l for l in lines)
              and any(l.startswith("fog test0000000002 ") for l in lines), f"saving writes env fog and the volume ({lines})")
        game.call("environment", fog={"density": 0.001}, volumes=[{"id": "test0000000002", "remove": True}])
        game.call("lighting", action="reload")
        e = game.call("environment")
        check(abs(e["fog"]["density"] - 0.0042) < 1e-6 and [v["id"] for v in e["volumes"]] == ["test0000000002"],
              f"reloading reads them back (density {e['fog']['density']}, volumes {[v['id'] for v in e['volumes']]})")
        # The editor's Atmosphere tab with the volume selected (its outline and gizmo).
        game.call("editor_ui", visible=True, details="atmosphere")
        game.call("environment", select_volume="test0000000002")
        game.run(30)
        game.screenshot(shots / "atmo-editor.png")
        game.call("editor_ui", visible=False)
        game.run(10)
        check(game.state() is not None, "the editor's Atmosphere tab draws the fog and the selected volume's gizmo")
        path.unlink()
        game.call("lighting", action="reload")
        e = game.call("environment")
        check(not e["fog"]["defined"] and not e["volumes"], "without the file the room is as generated again")

        # ---- outdoor: the generated fog, the moon's shafts ----
        check(warp(*OUTDOOR), f"warped to zone {OUTDOOR[0]} room {OUTDOOR[1]}")
        e = game.call("environment", fog_time=5)
        badges = {i["id"]: i["badge"] for i in game.call("lighting")["items"]}
        check(e["fog"]["defined"] and badges.get("env:fog") == "G" and e["sun"]["defined"],
              f"the forest has a generated ground fog and moon (badge {badges.get('env:fog')})")
        if traced:
            # The shafts at full contrast (haze_shafts 1: shadowed air loses all of the moon's light).
            game.call("environment", haze={"density": 0.002, "anisotropy": 0.6, "shafts": 1}, remove_fog=True)
            game.call("graphics", haze_debug=1)
            on = view(shot("shafts-on", 150))
            game.call("environment", haze={"shafts": 0.5})
            limited = view(shot("shafts-limited", 150))
            game.call("environment", sun={"shadows": False})
            off = view(shot("shafts-off", 150))
            game.call("environment", sun={"shadows": True})
            mask = off > 6
            ratio = on[mask] / off[mask]
            p10, p90 = np.percentile(ratio, [10, 90])
            check(mask.mean() > 0.2 and on[mask].mean() < off[mask].mean() * 0.97,
                  f"the moon's shadow darkens the haze in shadowed air (mean {on[mask].mean():.1f} vs "
                  f"{off[mask].mean():.1f} unshadowed)")
            check(p10 < 0.8 and p90 > 0.9 and p90 - p10 > 0.15, f"shafts: lit air keeps its light, shadowed air loses it "
                                             f"(ratio to unshadowed: 10th percentile {p10:.2f}, 90th {p90:.2f})")
            q10, q90 = np.percentile(limited[mask] / off[mask], [10, 90])
            check(q10 > p10 + 0.05 and q90 - q10 < p90 - p10, f"haze_shafts 0.5 limits their contrast (10th percentile "
                                                              f"{q10:.2f}, 90th {q90:.2f})")
            # Steady when nothing changes (the generated fog, its drift stopped).
            game.call("lighting", action="reload")
            game.call("environment", fog_time=5)
            a = view(shot("steady-a", 150))
            b = view(shot("steady-b", 1))
            c = view(shot("steady-c", 1))
            change = (float(np.abs(a - b).mean()) + float(np.abs(b - c).mean())) / 2
            check(a.mean() > 3 and change < 1.0, f"the haze is steady frame to frame (mean change {change:.2f} of 255, "
                                                 f"level {a.mean():.1f})")
            game.call("graphics", haze_debug=0)
        else:
            print("note: no traced haze with this preset; the shafts are not checked")
    finally:
        game.close()
    log = (out / "game.log").read_text(errors="replace")
    bad = [line for line in log.splitlines() if "validation:" in line or "[crash]" in line or "[gl] shader" in line]
    check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))
    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
