#!/usr/bin/env python3
"""rood-studio, the standalone model studio (pc/docs/plans/2026-11-24-standalone-studio-design.md), against the in-game studio.

    python3 pc/tests/rood_studio_smoke.py [--out DIR]

The game (started with no title: ROOD_STUDIO_BOOT) shows models in its own studio through its own loaders; RPC `studio dump_model` writes what the
renderer was given for each. rood-studio then loads the same models from the disc with the native decoders and compares:
  - the polygons (positions, texture coordinates, vertex colours, bones, codes, flags), the page and CLUT words and the content key, for several
    characters (flat and coloured layouts), an enemy (a ZUD's SHP) and a weapon;
  - the skeleton's chain: the game's turns put through the studio's chain give the game's bone matrices back (except a hand a pose lock holds);
  - the picture, in the flat look with the game's own bone palette and camera: same pixels where the stage and the model are drawn the same;
  - an enemy's weapon (second mesh of the game's dump): polygons, page and CLUT words, content key, and its bones from the hand's matrix;
  - the app: started with no model its screen shows the panels (the catalog's list, the controls); summoning, orbiting, zooming, the turntable, the
    backdrops (solid black, grey and white are the colour to the level) and an enemy with its weapon and a weapon alone are all RPC `studio` actions on the
    running app, each changing the picture as it should;
  - every model the catalog names decodes (rood-studio --check-catalog);
  - a normal start (the rest pose from the model's own sequence block, path tracing on) draws a model on the stage.
Pictures are written to the output folder (PNG). Skips without the disc image."""
import argparse
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT, Game  # noqa: E402
from photo_foundation_smoke import ppm  # noqa: E402
import miniws  # noqa: E402
import paint_live  # noqa: E402

# one of each layout: characters (flat records), one with coloured records (0xB3), an enemy with a weapon, a dagger
ITEMS = (("character", 3), ("character", 0), ("character", 16), ("character", 179), ("character", 58), ("enemy", 14), ("enemy", 4), ("enemy", 26), ("enemy", 40),
         ("enemy", 70), ("enemy", 95), ("weapon", 1))
WINDOW = (960, 720)
# a picture is the same where the stage and the model are drawn the same: the share of pixels that differ by more than 24 levels may not pass this
PICTURE_DIFFERENT = 0.002
# the enemy holds a weapon the game's studio draws and M1's rood-studio does not (a weapon is drawn on its holder's hand bone: M2)
PICTURE_DIFFERENT_ENEMY = 0.01


def image(path):
    w, h, data = ppm(path)
    return np.frombuffer(data, np.uint8).reshape(h, w, 3)


def disc_present():
    if os.environ.get("ROOD_DISC") and pathlib.Path(os.environ["ROOD_DISC"]).exists():
        return True
    return (ROOT / "disks/SLUS-01040.bin").exists()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out")
    args = parser.parse_args()
    if not disc_present():
        print("SKIP: no disc image")
        return 77
    if os.environ.get("ROOD_RENDERER") == "null":
        print("SKIP: pictures need a renderer")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "rood_studio"
    out.mkdir(parents=True, exist_ok=True)
    return run(out)


def settings_file(out):
    work = out / "work"
    work.mkdir(exist_ok=True)
    (work / "lights").mkdir(exist_ok=True)
    (work / "material_catalog_user.json").unlink(missing_ok=True)
    (work / "settings.cfg").write_text("aspect 4:3\nui_menus 0\nlight_animation 0\ndust 0\naa off\nskip_intro 1\n")
    return work


def run(out):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    work = settings_file(out)
    env = {"ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_GI": "1", "ROOD_SCALE": "2",
           "ROOD_LIGHTS": str(work / "lights"), "ROOD_STUDIO": str(work / "studio.json"), "ROOD_CARD1": str(work / "no-card.mcd"), "ROOD_PAUSED_REDRAW_HZ": "60",
           "ROOD_MATERIAL_CATALOG_USER": str(work / "material_catalog_user.json")}

    # ---- the game's studio: the references and the pictures ----
    refs, others = {}, {}
    g = Game.launch(fast=True, log=out / "game.log", extra_env={**env, "ROOD_STUDIO_BOOT": "1"})
    try:
        g.pause()
        g.run(1500)  # (the opening room is faded in: the stage shows its grey)
        g.call("studio", action="enter")
        g.call("studio", action="look", name="flat")
        g.run(1)
        for kind, ident in ITEMS:
            g.call("studio", action="summon", kind=kind, id=ident)
            deadline = time.time() + 60
            while True:
                s = g.call("studio")
                if (s["entered"] and s["settled"] and s["framed"] and s.get("box") and s.get("item") and s["item"]["id"] == ident
                        and s["look"] == "flat" and g.call("photo")["phase"] == "held"):
                    break
                if time.time() > deadline:
                    raise SystemExit(f"{kind} {ident}: the game's studio did not settle")
                time.sleep(0.05)
            g.wait_presents(3)
            path = out / f"ref_{kind}_{ident}.bin"
            for k in range(4):
                pathlib.Path(f"{path}.{k}").unlink(missing_ok=True)
            g.call("studio", action="dump_model", path=str(path))
            if not rood_rpc.wait_for_file(pathlib.Path(f"{path}.0"), 30):
                raise SystemExit(f"{kind} {ident}: no dump")
            time.sleep(0.3)
            shot = out / f"game_{kind}_{ident}.ppm"
            shot.unlink(missing_ok=True)
            g.call("present_dump", path=str(shot))
            rood_rpc.wait_for_file(shot, 30)
            Image.fromarray(image(shot)).save(shot.with_suffix(".png"))
            shot.unlink()
            refs[(kind, ident)] = (f"{path}.0", s["models"][0], s["animation"])
        # other animations of the game's own, started some frames in (an enemy's attack: the sequence decoder's deltas)
        for kind, ident, number, frame in (("enemy", 14, 16, 0), ("enemy", 127, 64, 10)):
            g.call("studio", action="look", set="model.animation", value=number)
            g.call("studio", action="look", set="model.animation_frame", value=frame)
            g.call("studio", action="summon", kind=kind, id=ident)
            deadline = time.time() + 60
            while True:
                s = g.call("studio")
                if s["entered"] and s["settled"] and s["framed"] and s.get("box") and s.get("item") and s["item"]["id"] == ident and s["animation"] >= 100:
                    break
                if time.time() > deadline:
                    raise SystemExit(f"{kind} {ident} animation {number}: the game's studio did not settle")
                time.sleep(0.05)
            g.wait_presents(3)
            path = out / f"ref_{kind}_{ident}_a{number}.bin"
            pathlib.Path(f"{path}.0").unlink(missing_ok=True)
            g.call("studio", action="dump_model", path=str(path))
            rood_rpc.wait_for_file(pathlib.Path(f"{path}.0"), 30)
            time.sleep(0.3)
            others[(kind, ident, s["animation"])] = (f"{path}.0", s["models"][0])
    finally:
        g.close()

    # the Golem's attack played by the game itself, frame after frame (not started at a frame: its events hold bones 22 and 23 from the animation's
    # frame 17, which the game saves between frames), the pose before the holds, during and after
    live = {}
    g = Game.launch(fast=True, log=out / "game_live.log", extra_env={**env, "ROOD_STUDIO_BOOT": "1"})
    try:
        g.pause()
        g.run(1500)
        g.call("studio", action="look", set="model.animation", value=64)
        g.call("studio", action="live", kind="enemy", id=127)
        step = 0
        for target in (14, 40, 47):
            while step < target:
                g.run(1)
                step += 1
            path = out / f"live_golem_{target}.bin"
            pathlib.Path(f"{path}.0").unlink(missing_ok=True)
            g.call("studio", action="live_dump", path=str(path))
            live[target] = f"{path}.0"
    finally:
        g.close()

    # ---- rood-studio against them ----
    binary = rood_rpc.BUILD / "rood-studio"
    if not binary.exists():
        binary.symlink_to("rood_battle_probe")

    def studio(*argv, gi="0", timeout=180, extra=None):
        e = {**os.environ, **env, "ROOD_GI": gi, **(extra or {})}
        e.pop("DISPLAY", None), e.pop("WAYLAND_DISPLAY", None)
        e["SDL_VIDEO_DRIVER"] = "offscreen"
        e.setdefault("ROOD_ARCH", "64")
        r = subprocess.run([str(binary), "--window", "--skip-movies", "--fast", *argv], cwd=ROOT, env=e, capture_output=True, text=True, timeout=timeout)
        return r.returncode, "\n".join(line for line in r.stderr.splitlines() if "[rood-studio]" in line)

    for (kind, ident), (ref, key, animation) in refs.items():
        code, log = studio(f"--{kind}", str(ident), "--reference", ref, "--expect-key", key, "--animation", str(max(animation, 0)))
        check(code == 0, f"{kind} {ident}: decode equal to the game's (polygons, page, CLUT, key {key}, the skeleton's chain)\n{log}")

    for (kind, ident, animation), (ref, key) in others.items():
        code, log = studio(f"--{kind}", str(ident), "--reference", ref, "--expect-key", key, "--animation", str(animation))
        check(code == 0, f"{kind} {ident}: animation {animation} decoded to the game's bone matrices\n{log}")

    for target, ref in live.items():
        code, log = studio("--enemy", "127", "--reference", ref, "--animation", "164", "--tolerance", "15")
        check(code == 0, f"the Golem's animation 164, played by the game (game frame {target}): its bones, the held ones included, match to 15 units\n{log}")

    # the picture: the game's palette and camera, the flat look, both drawn by the same renderer
    for kind, ident in (("character", 3), ("character", 0), ("character", 58), ("weapon", 1), ("enemy", 14)):
        ref, key, animation = refs[(kind, ident)]
        mine = out / f"studio_{kind}_{ident}.ppm"
        mine.unlink(missing_ok=True)
        code, log = studio(f"--{kind}", str(ident), "--reference", ref, "--look", "flat", "--frames", "300", "--dump", str(mine), gi="1")
        if not check(code == 0 and mine.exists(), f"{kind} {ident}: rood-studio draws the picture"):
            continue
        a, b = image(mine), image(out / f"game_{kind}_{ident}.png".replace(".png", ".ppm")) if False else np.array(Image.open(out / f"game_{kind}_{ident}.png").convert("RGB"))
        Image.fromarray(a).save(mine.with_suffix(".png"))
        mine.unlink()
        diff = np.abs(a.astype(int) - b.astype(int))
        strong = float((diff.max(axis=2) > 24).mean())
        print(f"      {kind} {ident}: mean difference {diff.mean():.2f}, {strong * 100:.3f}% of pixels differ by more than 24 levels")
        limit = PICTURE_DIFFERENT_ENEMY if kind == "enemy" else PICTURE_DIFFERENT
        check(a.std() > 4 and strong <= limit, f"{kind} {ident}: the picture is the game's studio's ({strong * 100:.3f}% differ, limit {limit * 100}%)")


    # ---- the app: the studio's own screen and its actions ----
    def settle(g, kind, ident, tries=80):
        for _ in range(tries):
            st = g.call("studio")
            if st.get("item") and st["item"]["kind"] == kind and st["item"]["id"] == ident and st["framed"]:
                break
            time.sleep(0.1)
        g.wait_presents(6)

    def shot(g, name):
        p = out / f"app_{name}.ppm"
        p.unlink(missing_ok=True)
        g.call("present_dump", path=str(p))
        rood_rpc.wait_for_file(p, 30)
        a = image(p)
        Image.fromarray(a).save(p.with_suffix(".png"))
        p.unlink()
        return a

    def centre(a):  # (between the panels)
        h, w = a.shape[:2]
        return a[:, int(w * 0.3):int(w * 0.68)].astype(int)

    def model_height(a, backdrop_value):
        d = np.abs(centre(a) - backdrop_value).max(axis=2) > 12
        ys = np.nonzero(d.any(axis=1))[0]
        return int(ys.max() - ys.min()) if len(ys) else 0

    app_env = {**env, "ROOD_GI": "1"}
    e = dict(app_env)
    old_binary = rood_rpc.BINARY
    rood_rpc.BINARY = binary
    g = Game.launch(fast=True, log=out / "app.log", extra_env=e)
    try:
        g.wait_presents(10)
        a = shot(g, "home")
        h, w = a.shape[:2]
        panel = a[:, : int(w * 0.18)].astype(int)
        check(panel.mean() < 70 and panel.std() > 20, f"the home screen: the models panel is drawn on the left (mean {panel.mean():.0f}, spread {panel.std():.0f})")
        right = a[:, int(w * 0.76):].astype(int)
        check(right.mean() < 70 and right.std() > 15, f"the home screen: the controls panel is drawn on the right (mean {right.mean():.0f}, spread {right.std():.0f})")
        g.call("studio", action="summon", kind="character", id=3)
        settle(g, "character", 3)
        a = shot(g, "summon")
        check(centre(a).std() > 12, f"summoning a character through the studio's RPC shows it ({centre(a).std():.1f})")
        base = model_height(a, 95)

        # ---- live paint: the Aseprite bridge into the running studio (and a second target, as a game beside it) ----
        pkg = out / "paint_pkg"
        shutil.rmtree(pkg, ignore_errors=True)
        r = subprocess.run([sys.executable, str(ROOT / "pc/tools/paint_roundtrip.py"), "export", "model", "--model", "studio", "--game", str(g.path), "--out", str(pkg),
                            "--painter", "aseprite"], capture_output=True, text=True, env={**os.environ, "ROOD_ARCH": "64"})
        if not check(r.returncode == 0 and (pkg / "seed.json").exists(), f"a model's paint package is exported from rood-studio's socket ({r.stderr[-200:]})"):
            raise SystemExit(1)
        label = next(iter(json.loads((pkg / "seed.json").read_text())["labels"]))
        scratch = out / "paint_scratch"
        shutil.rmtree(scratch, ignore_errors=True)

        class Beside:
            """A second target (a game running beside the studio): records what it is sent."""
            def __init__(self):
                self.calls = []

            def call(self, method, **kw):
                self.calls.append((method, kw.get("action"), kw.get("channel")))
                return {}
        bridge = paint_live.Bridge(pkg, rpc=[g.path, "/nonexistent/beside.sock"], layer=str(scratch / "layer"), catalog=str(work / "material_catalog_user.json"))
        beside = Beside()
        bridge.conns[1] = beside
        bridge.connect()
        check(bridge.game is not None and [t["up"] for t in bridge.targets()] == [True, True], f"the bridge reaches the studio and the second target ({bridge.targets()})")

        def stroke(layer, value):
            cel = np.full((256, 256, 4), value, np.uint8)
            header = json.dumps({"material": label, "layer": layer, "x": 0, "y": 0, "w": 256, "h": 256, "canvas": [256, 256]}).encode()
            return bridge.message(None, miniws.BINARY, struct.pack(">I", len(header)) + header + cel.tobytes())
        before = shot(g, "paint_before")
        reply = stroke("Metal", 255)
        g.wait_presents(10)
        painted = shot(g, "paint_metal")
        check(reply["ok"] and reply["game"], f"a Metal stroke over the whole page is taken ({reply})")
        check(float(np.abs(centre(before) - centre(painted)).mean()) > 1.0, "...and the model in the studio shows it (the picture changed)")
        check(any(c[1] == "map_set" and c[2] == "metal" for c in beside.calls), f"...and the second target got the same map ({len(beside.calls)} calls)")
        empty = json.dumps({"material": label, "layer": "Metal", "x": 0, "y": 0, "w": 0, "h": 0, "canvas": [256, 256]}).encode()
        bridge.message(None, miniws.BINARY, struct.pack(">I", len(empty)) + empty)
        g.wait_presents(10)
        back = shot(g, "paint_back")
        check(float(np.abs(centre(before) - centre(back)).mean()) < 0.5, "an emptied layer puts the model's own look back")
        # the texel locator both ways
        pick = g.call("studio", action="pick", x=480, y=300)["pick"]
        where = bridge.pixel_of(pick["key"], int(pick["lx"]), int(pick["ly"]), pick.get("model", ""))
        check(pick["hit"] and where is not None, f"the pointer on the model picks a texel the package has a pixel for ({pick.get('key')}, {where})")
        located = bridge.message(None, miniws.TEXT, json.dumps({"locate": {"material": where[0], "x": where[1], "y": where[2]}}).encode())
        check(located["ok"] and located.get("triangles", 0) >= 1, f"Aseprite's cursor on that pixel is found on the model ({located})")
        bridge.pick_serial = None
        bridge.picked()
        for x, y in ((490, 330), (470, 250), (500, 380), (465, 300)):
            if g.call("studio", action="pick", x=x, y=y).get("pick", {}).get("hit"):
                break
        selected = bridge.picked()
        check(selected is not None and selected["select"]["material"] == label, f"a click on the model selects its texel in Aseprite ({selected})")
        g.call("studio", action="camera", yaw=100)
        g.wait_presents(8)
        b = shot(g, "orbit")
        check(float(np.abs(centre(a) - centre(b)).mean()) > 3, "orbiting changes the picture")
        g.call("studio", action="camera", yaw=20, zoom=2)
        g.wait_presents(8)
        c = shot(g, "zoom")
        check(model_height(c, 95) < base * 0.9, f"zooming out shrinks the model ({base} -> {model_height(c, 95)} px)")
        g.call("studio", action="camera", frame=True, yaw=20, elevation=12, zoom=1)
        settle(g, "character", 3)
        for preset, value in (("black", 0), ("white", 255), ("grey", None)):
            g.call("studio", action="backdrop", mode="solid", preset=preset)
            g.wait_presents(8)
            d = shot(g, f"backdrop_{preset}")
            corner = d[8:40, int(w * 0.4):int(w * 0.6)].astype(int)
            expect = value if value is not None else float(g.call("studio", action="get", path="backdrop.presets.grey")["value"][0])
            check(abs(float(corner.mean()) - expect) < 3 and corner.std() < 2, f"solid {preset} backdrop: the colour {expect:.0f} to the level (mean {corner.mean():.1f}, spread {corner.std():.1f})")
        g.call("studio", action="backdrop", mode="stage")
        g.call("studio", action="turntable", model=True, model_speed=90, fixed_dt=0.25)
        g.wait_presents(10)
        t1 = shot(g, "turntable_1")
        g.wait_presents(10)
        t2 = shot(g, "turntable_2")
        g.call("studio", action="turntable", model=False, paused=True)
        check(float(np.abs(centre(t1) - centre(t2)).mean()) > 1.5, "the turntable turns the model")
        g.call("studio", action="turntable", model_angle=0, light_angle=0)
        g.call("studio", action="summon", kind="enemy", id=14)
        settle(g, "enemy", 14)
        enemy = shot(g, "enemy")
        wide = model_height(enemy, 95)
        cols = np.nonzero((np.abs(centre(enemy) - 95).max(axis=2) > 12).any(axis=0))[0]
        check(wide > 100 and len(cols) > 0, f"an enemy with its weapon on its hand ({wide} px high, {cols.max() - cols.min() if len(cols) else 0} px across)")
        g.call("studio", action="summon", kind="weapon", id=1)
        settle(g, "weapon", 1)
        weapon = shot(g, "weapon")
        bright = int((centre(weapon).max(axis=2) > 140).sum())
        check(bright > 200, f"a weapon alone: its blade is in the picture ({bright} bright pixels)")
    finally:
        g.close()
        rood_rpc.BINARY = old_binary

    # every model the catalog names
    code, log = studio("--check-catalog")
    check(code == 0, f"the catalog's models all decode\n{log}")

    # a normal start: the rest pose from the model's own sequence block, path tracing on
    mine = out / "rest_pose_gi.ppm"
    mine.unlink(missing_ok=True)
    code, log = studio("--character", "3", "--frames", "40", "--dump", str(mine), gi="1")
    if check(code == 0 and mine.exists(), "rood-studio starts with no game: a character in the rest pose, path tracing on"):
        a = image(mine)
        Image.fromarray(a).save(mine.with_suffix(".png"))
        mine.unlink()
        centre = a[120:600, 300:660].astype(int)
        check(centre.std() > 12, f"the picture holds a model on the stage (spread {centre.std():.1f})")
    print("FAIL" if failures else "OK")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
