#!/usr/bin/env python3
"""The room's sun and sky (pc/src/environment.cpp; pc/docs/Vulkan_Renderer.md
"Sun and sky"), on whichever renderer the run uses (OpenGL, Vulkan).

    python3 pc/tests/sky_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

Continues a copy of the card's save (zone 9 room 17, the wine cellar's save
room: roofed) with private copies of the lighting packs (ROOD_LIGHTING) and
an empty user layer, at the Ultra preset (path-traced GI and traced shadows:
Vulkan ray queries, OpenGL compute), then:

- roofed room: the generated layer gives it no sun or sky (RPC environment
  inactive), and the picture with the environment switched on and off is
  the same;
- outdoor room (Snowfly Forest, zone 40 room 0): the generated layer gives
  it a sun and a sky (env:sun, env:sky, badge G) and the renderers take them;
- sun shadows: with only a strong sun, the traced visibility view has both
  shadowed and lit room surfaces, and in the lighting-only view the lit ones
  are brighter than the shadowed ones; the Medium preset (no traced
  shadows: the sun's shadow map; probe GI) shades the same regions alike;
- sky fill: with only the sky, the indirect light view is lit where it was
  black with the environment off; energy: no surface gets more than the
  brightest sky radiance (a surface cannot reflect more than its sky); on
  Vulkan with NRD, the displayed indirect light keeps the raw samples'
  energy (mean within 15 %); the probes (Medium) bring the sky too, within a
  factor 2 of the path tracer;
- settings: an edit by RPC saves to the user layer as one override line,
  reloads from it, and the generated value returns when the file goes;
- generated tags: `generate_lighting.py --check` for the two rooms (when the
  disc is there): the generated layer is what the generator writes now
  (deterministic: outdoor detection gives the same tags);
- no crash, shader error or (Vulkan, with the Khronos layer on
  VK_LAYER_PATH) validation message.

Frame-based waits only (RPC step counts).
"""
import argparse
import os
import pathlib
import shutil
import subprocess
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

OUTDOOR = (40, 0)  # Snowfly Forest, "The Hunt Begins": open, no fixtures
ROOFED = (9, 17)  # the save room
SUN_ONLY = {"sun": {"enabled": True, "azimuth": 200, "elevation": 50, "color": [1, 1, 1], "intensity": 2.0,
                    "angle": 1, "shadows": True}, "sky": {"enabled": False}}
SKY_ONLY = {"sun": {"enabled": False}, "sky": {"enabled": True, "zenith": [0.25, 0.3, 0.45],
                                               "horizon": [0.45, 0.45, 0.5], "ground": [0.05, 0.05, 0.05],
                                               "intensity": 1, "sun_tint": 0}}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--card", default=str(default_card()))
    ap.add_argument("--shots", help="keep the screenshots here")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    gen = ROOT / "lighting/default/generated" / lf.room_file_name(*OUTDOOR)
    if not gen.exists() or "env sun" not in gen.read_text():
        print(f"SKIP: no generated sun for zone {OUTDOOR[0]} room {OUTDOOR[1]} (pc/tools/generate_lighting.py)")
        return 77
    out = OUT / "sky_smoke"
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
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-sky-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    game = launch_at("save_room", card=out / "card1.mcd", fast=True, log=out / "game.log", extra_env=env,
                     socket_path=socket)

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
        """A screenshot after `frames` vsyncs (the traced light settles)."""
        game.run(frames)
        path = game.present_screenshot(shots / f"sky-{name}.png")
        return np.asarray(Image.open(path).convert("RGB"), np.float32)

    def view_mask(image):
        """Pixels of the 3D view: below the HUD's top band, above its bottom one."""
        h = image.shape[0]
        mask = np.zeros(image.shape[:2], bool)
        mask[int(h * 0.12):int(h * 0.78), :] = True
        return mask

    def lum(image):
        return image @ np.array([0.299, 0.587, 0.114], np.float32)

    try:
        st = game.state()
        if (st["zone"], st["room"]) != ROOFED and not warp(*ROOFED):
            check(False, f"in the save room (zone {st['zone']} room {st['room']})")
            return 1
        graphics = game.call("graphics")
        traced = graphics.get("lighting_preset") == "ultra"
        print(f"renderer {'vulkan' if vulkan else 'opengl'}, preset {graphics.get('lighting_preset')}, "
              f"gi_method {graphics.get('gi_method')}", flush=True)

        # ---- the roofed save room: nothing ----
        e = game.call("environment")
        check(not e["active"] and not e["sun"]["defined"] and not e["sky"]["defined"],
              "the roofed save room has no sun or sky (generated layer)")
        on = shot("roofed-on")
        game.call("environment", **{"global": False})
        off = shot("roofed-off")
        game.call("environment", **{"global": True})
        diff = float(np.abs(on - off).mean())
        check(diff < 3.0, f"the roofed room looks the same with the environment on and off (mean difference {diff:.2f})")

        # ---- outdoor: the generated sun and sky ----
        check(warp(*OUTDOOR), f"warped to zone {OUTDOOR[0]} room {OUTDOOR[1]}")
        e = game.call("environment")
        check(e["active"] and e["sun"]["defined"] and e["sky"]["defined"] and e["sun"]["enabled"],
              f"the forest has a sun and a sky from the generated layer (sun {e['sun']['azimuth']}/"
              f"{e['sun']['elevation']}, intensity {e['sun']['intensity']})")
        layers = game.call("lighting")
        badges = {i["id"]: i["badge"] for i in layers["items"]}
        check(badges.get("env:sun") == "G" and badges.get("env:sky") == "G",
              f"env:sun and env:sky come from the generated layer (badges {badges.get('env:sun')}, {badges.get('env:sky')})")
        check(e["gpu"]["sun_col"][3] == 1 and e["gpu"]["zenith"][3] == 1 and abs(np.linalg.norm(e["gpu"]["sun_dir"][:3]) - 1) < 1e-3
              and e["gpu"]["sun_dir"][1] < 0, f"the renderers' record: a unit direction up toward the sun, shadows on ({e['gpu']})")

        # ---- sun shadows ----
        game.call("environment", **SUN_ONLY)
        game.call("graphics", gi_debug=2)  # the traced shadow visibility
        visibility = lum(shot("sun-visibility"))
        game.call("graphics", gi_debug=0, view_mode=2)  # the lighting alone
        lighting = lum(shot("sun-lighting"))
        game.call("graphics", view_mode=0)
        view = view_mask(lighting) & (lighting > 2)  # lit 3D surfaces (not the black void)
        shadowed = view & (visibility < 40)
        lit = view & (visibility > 215)
        if traced:
            check(shadowed.mean() > 0.01 and lit.mean() > 0.01,
                  f"the sun casts shadows: {shadowed.mean() * 100:.1f} % of the view shadowed, {lit.mean() * 100:.1f} % lit")
            lit_mean = float(lighting[lit].mean()) if lit.any() else 0.0
            dark_mean = float(lighting[shadowed].mean()) if shadowed.any() else 0.0
            check(lit_mean > dark_mean * 1.25 + 4, f"sunlit surfaces are brighter than shadowed ones "
                                                    f"(lighting view {lit_mean:.1f} vs {dark_mean:.1f})")
        # ---- sky fill ----
        game.call("environment", **SKY_ONLY)
        game.call("graphics", gi_debug=1)  # the indirect light alone
        sky_on = lum(shot("sky-indirect", 150))
        game.call("environment", **{"global": False})
        sky_off = lum(shot("sky-indirect-off", 150))
        game.call("environment", **{"global": True})
        mask = view_mask(sky_on)
        mean_on, mean_off = float(sky_on[mask].mean()), float(sky_off[mask].mean())
        check(mean_on > mean_off + 3, f"the sky fills the outdoor room (indirect view {mean_on:.1f}, "
                                      f"{mean_off:.1f} without the environment)")
        brightest = 255 * max(max(SKY_ONLY["sky"]["zenith"]), max(SKY_ONLY["sky"]["horizon"]))
        top = float(np.percentile(sky_on[mask], 99.5))
        check(top <= brightest * 1.15 + 6, f"energy: no surface brighter than the brightest sky "
                                           f"(99.5th percentile {top:.0f} <= {brightest:.0f})")
        if vulkan and traced:
            path = out / "sky-light.bin"
            try:
                game.call("gi_dump", path=str(path), light=True)
                with open(path, "rb") as f:
                    head = f.readline().split()
                    w, h = int(head[1]), int(head[2])
                    data = np.frombuffer(f.read(), np.float32).reshape(h, w, 9)
                surface = data[..., 0] > 0.5
                raw = float(data[..., 1:4][surface].mean())
                shown = float(data[..., 4:7][surface].mean())
                check(raw > 0.005, f"the path tracer's raw samples carry the sky ({raw:.4f})")
                if int(head[3]) == 2:  # NRD
                    check(abs(shown / max(raw, 1e-6) - 1) < 0.15,
                          f"NRD keeps the sky's energy (displayed {shown:.4f} vs raw {raw:.4f})")
            except Exception as ex:  # noqa: BLE001
                check(False, f"gi_dump light: {ex}")
        # The probes (Medium): the sky through the probe field; the sun through its shadow map.
        game.call("graphics", gi_debug=0)
        game.call("graphics", lighting_preset="medium")
        game.run(30)
        g = game.call("graphics")
        if g.get("gi_method") == "probes":
            game.call("graphics", gi_debug=1)
            probes = lum(shot("sky-indirect-probes", 240))
            game.call("graphics", gi_debug=0)
            mean_p = float(probes[mask].mean())
            check(mean_on * 0.5 - 2 < mean_p < mean_on * 2 + 4,
                  f"the probes bring the sky too (indirect view {mean_p:.1f} vs path traced {mean_on:.1f})")
        else:
            print(f"note: Medium has no probe GI here ({g.get('gi_method')})")
        if traced:
            game.call("environment", **SUN_ONLY)
            game.call("graphics", view_mode=2)
            mapped = lum(shot("sun-lighting-medium"))
            game.call("graphics", view_mode=0)
            lit_mean = float(mapped[lit].mean()) if lit.any() else 0.0
            dark_mean = float(mapped[shadowed].mean()) if shadowed.any() else 0.0
            check(lit_mean > dark_mean * 1.25 + 4, f"Medium (the sun's shadow map) shades the traced shadow's regions "
                                                    f"alike (lit {lit_mean:.1f} vs shadowed {dark_mean:.1f})")
        game.call("graphics", lighting_preset="ultra")
        game.run(30)

        # ---- settings: save, reload, revert ----
        game.call("lighting", action="reload")
        game.run(4)
        game.call("environment", sun={"elevation": 52.5})
        saved = game.call("lighting", action="save")
        text = (user / lf.room_file_name(*OUTDOOR)).read_text() if (user / lf.room_file_name(*OUTDOOR)).exists() else ""
        lines = [line for line in text.splitlines() if line and not line.startswith(("#", "rood-lighting"))]
        check(saved["ok"] and lines == ["override env:sun elevation=52.5"],
              f"saving writes one override line to the user layer ({lines})")
        game.call("environment", sun={"elevation": 10})
        game.call("lighting", action="reload")
        e = game.call("environment")
        check(abs(e["sun"]["elevation"] - 52.5) < 1e-3, f"reloading reads it back (elevation {e['sun']['elevation']})")
        (user / lf.room_file_name(*OUTDOOR)).unlink()
        game.call("lighting", action="reload")
        e2 = game.call("environment")
        gen_items = {en.id: en.item for en in lf.read_file(str(gen)).entries if en.verb == "define"}
        want = float(gen_items["env:sun"].get("elevation") or lf.SCHEMA["env"][2][2][2])
        check(abs(e2["sun"]["elevation"] - want) < 1e-3, f"without the file the generated value is back "
                                                          f"({e2['sun']['elevation']} = {want})")
    finally:
        game.close()
    # ---- generated tags are stable ----
    disc = pathlib.Path(os.environ.get("ROOD_DISC") or ROOT / "disks/SLUS-01040.bin")
    if disc.exists():
        r = subprocess.run([sys.executable, str(ROOT / "pc/tools/generate_lighting.py"), "--zone", str(OUTDOOR[0]),
                            "--room", str(OUTDOOR[1]), "--zone", str(ROOFED[0]), "--room", str(ROOFED[1]), "--check",
                            "--jobs", "1", "--quiet"], capture_output=True, text=True, cwd=ROOT)
        check(r.returncode == 0, f"the generated layer is what the generator writes now (outdoor tags stable): "
                                 f"{r.stdout.strip().splitlines()[-1:]}")
        roofed = ROOT / "lighting/default/generated" / lf.room_file_name(*ROOFED)
        check(not roofed.exists() or "env " not in roofed.read_text(), "the save room's generated file has no env items")
    else:
        print("note: no disc; the generator check is skipped")
    log = (out / "game.log").read_text(errors="replace")
    bad = [line for line in log.splitlines() if "validation:" in line or "[crash]" in line or "[gl] shader" in line]
    check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))
    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
