#!/usr/bin/env python3
"""Material response (pc/docs/Materials.md): the catalog and the roughness
maps at run time, the specular lobe on screen, and that without materials
(or with the switch off) the picture is exactly the one without them.

    python3 pc/tests/material_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Continues from the card's save room (zone 9 room 16), lighting preset Low
with ambient occlusion off, lights still (light_animation 0), no dust, no
antialiasing: a redrawn paused frame is the same every time. A test light is
added just above the floor beside Ashley; the floor texture under it is
picked (RPC material pick, by world point) and given entries in a scratch
shipped catalog and a scratch user layer.

- matching: an entry in the shipped catalog marks the floor's texture
  (RPC material: its use, texels, the picked texel's roughness, specular,
  metal); the user layer's entry of the same id wins; an entry limited to
  another room or another palette marks nothing; an unsaved edit (set) is
  live and Save writes it into the user file;
- roughness maps: a rect painted over RPC changes the texel's roughness
  there and only there, and is written as the user layer's roughness.png
  (8-bit grayscale, the texture's size); a map written straight to disk is
  picked up while the game runs; a map of another size is ignored;
- identity: with no entry (both catalogs empty) the frame is identical,
  byte for byte, to the frame with entries but specular off (and to one
  with entries for other rooms only); the switch off clears RPC active;
- Vulkan: on, only material pixels change (the material view's mask),
  the specular-only view (view_mode 7) shows a highlight on the floor, and
  the highlight moves across the floor's texels when the camera moves (its
  brightest pixel picks a different texel);
- OpenGL: entries match (RPC) but the picture is the same as without them
  (OpenGL draws no material response).
"""
import argparse
import json
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
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402

EMPTY = {"format": 1, "presets": {}, "entries": []}


def frame(game, path):
    game.call("redraw")
    game.wait_presents(6)
    game.present_screenshot(str(path))
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)


def write_json(path, data):
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, indent=1) + "\n")
    os.replace(tmp, path)


def pose_looking_at(eye, target):
    d = [target[i] - eye[i] for i in range(3)]
    yaw = math.degrees(math.atan2(d[0], d[2]))
    pitch = math.degrees(math.asin(-d[1] / math.sqrt(sum(x * x for x in d))))
    return dict(x=eye[0], y=eye[1], z=eye[2], yaw=yaw, pitch=pitch)


PRESETS = {"polished metal": {"roughness": 0.4, "specular": 0.65, "metal": 0.65}}


def settle(game):
    """Files changed on disk (a catalog, a map): read again at once (RPC material reload), then the paused
    frame redrawn so the planes follow (no game frame passes: the picture stays comparable)."""
    game.call("material", action="reload")
    for _ in range(3):
        game.call("redraw")
        game.wait_presents(2)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep the frames and logs here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    out = pathlib.Path(args.out) if args.out else OUT / "material"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    shipped = out / "catalog.json"
    user = out / "materials.json"
    write_json(shipped, EMPTY)
    user.unlink(missing_ok=True)
    (out / "settings.cfg").write_text("light_animation 0\ndust 0\naa off\n")
    env = {"ROOD_LIGHTS": str(lights), "ROOD_LIGHTING": str(ROOT / "lighting"), "ROOD_SCALE": "2",
           "ROOD_WINDOW": "1280x960", "ROOD_GI": "1" if vulkan else "0", "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_MATERIAL_CATALOG": str(shipped), "ROOD_MATERIAL_CATALOG_USER": str(user),
           "ROOD_MATERIALS": str(out / "maps"), "ROOD_MATERIAL_EXPORTS": str(out / "exports")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-material-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        game.call("graphics", interpolate=False, lighting_preset="low")
        game.call("graphics", ao=0, reflection_bounces=0, specular=1)
        game.run(20)
        state = game.state()
        zone, room = state["zone"], state["room"]
        player = state.get("player") or {}
        if "x" not in player:
            print(f"FAIL: no Ashley in the save room: {state}")
            return 1
        px, py, pz = player["x"], player["y"], player["z"]
        # The floor a step beside Ashley, and a test light just above it.
        floor = [px + 40, py, pz + 40]
        game.call("lights", action="add", position=[px + 40, py - 24, pz + 40])
        game.run(10)
        pick = game.call("material", action="pick", world=floor)
        check(pick.get("valid"), f"a floor texture under {floor}: {pick}")
        if not pick.get("valid"):
            return 1
        key, size, clut = pick["key"], pick["size"], pick["clut"]
        tx, ty = pick["texel"]
        print(f"floor texture {key} {size[0]} x {size[1]}, palette {clut}, texel {tx},{ty}; room {zone}/{room}")
        game.call("graphics", view_mode=0)
        nothing = frame(game, out / "nothing.png")

        # ---- matching: the shipped catalog, the user layer over it, scopes.
        entry = {"id": "T001", "name": "test floor", "where": "test", "match": {"key": key, "size": size},
                 "preset": "polished metal", "roughness": 0.2, "specular": 1, "metal": 0, "review": "new"}
        write_json(shipped, dict(EMPTY, presets=PRESETS, entries=[entry]))
        settle(game)
        listing = game.call("material")
        uses = [u for u in listing["uses"] if u["key"] == key]
        check(listing["active"] and uses and uses[0]["entry"] == "T001" and listing["cells"] > 0,
              f"the shipped entry marks the floor ({len(uses)} uses, {listing['cells']} texels, active "
              f"{listing['active']})")
        at = game.call("material", action="pick", world=floor).get("material")
        check(at == [51, 255, 0], f"the picked texel's roughness, specular, metal x 255: {at} (expected [51, 255, 0])")
        write_json(user, dict(EMPTY, entries=[dict(entry, specular=0.5, roughness=0.6)]))
        settle(game)
        at = game.call("material", action="pick", world=floor).get("material")
        check(at == [153, 128, 0], f"the user layer's entry of the same id wins: {at} (expected [153, 128, 0])")
        user.unlink()
        for name, scoped in (("another room", dict(entry, scope={"room": [zone, (room + 1) % 256]})),
                             ("another palette", dict(entry, match=dict(entry["match"], clut="0123456789")))):
            write_json(shipped, dict(EMPTY, presets=PRESETS, entries=[scoped]))
            settle(game)
            listing = game.call("material")
            check(not listing["active"] and not listing["uses"], f"an entry for {name} marks nothing "
                  f"({len(listing['uses'])} uses)")
        other_rooms = frame(game, out / "other-room.png")
        check(np.array_equal(other_rooms, nothing), "entries for another room only: the frame is identical "
              f"({int((other_rooms != nothing).any(2).sum())} pixels differ)")
        write_json(shipped, dict(EMPTY, presets=PRESETS, entries=[entry]))
        settle(game)
        edited = game.call("material", action="set", id="T001", specular=0.25)
        settle(game)
        at = game.call("material", action="pick", world=floor).get("material")
        check(edited.get("value", {}).get("specular") == 0.25 and at and at[1] == 64,
              f"an unsaved edit is live: {at}")
        saved = game.call("material", action="save")
        on_disk = json.loads(user.read_text()) if user.exists() else {}
        check(not saved["unsaved"] and [e.get("specular") for e in on_disk.get("entries", [])] == [0.25],
              f"Save writes the entry into the user layer: {on_disk.get('entries')}")
        copied = json.loads(game.call("material", action="copy", id="T001")["json"])
        check(copied.get("id") == "T001" and copied.get("match", {}).get("key") == key,
              f"Copy JSON gives the catalog entry: {copied}")
        user.unlink()
        settle(game)

        # ---- roughness maps
        maps = out / "maps" / key
        painted = game.call("material", action="paint", key=key, value=20, rects=[[0, 0, 3, 3]])
        at_corner = game.call("material", action="pick", world=floor)
        png = maps / "roughness.png"
        img = Image.open(png) if png.exists() else None
        check(painted.get("map") and img is not None and img.mode == "L" and list(img.size) == size,
              f"a painted rect writes {png} (8-bit grayscale {size[0]} x {size[1]}): "
              f"{img.mode if img else None} {img.size if img else None}")
        values = np.asarray(img) if img is not None else np.zeros((1, 1))
        check(values.shape == (size[1], size[0]) and (values[:4, :4] == 20).all() and (values[4:, :] == 51).all()
              and (values[:4, 4:] == 51).all(),
              "the map holds 20 in the rect and the entry's roughness (51) elsewhere")
        inside = 0 <= tx <= 3 and 0 <= ty <= 3
        expect = 20 if inside else 51
        check(at_corner.get("material", [None])[0] == expect,
              f"the picked texel ({tx},{ty}) reads its map value: {at_corner.get('material')} (expected {expect})")
        ramp = np.full((size[1], size[0]), 77, np.uint8)
        Image.fromarray(ramp, "L").save(maps / "roughness.tmp.png")
        os.replace(maps / "roughness.tmp.png", png)
        settle(game)
        at = game.call("material", action="pick", world=floor).get("material")
        check(at and at[0] == 77, f"a map written straight to disk is picked up: {at}")
        Image.fromarray(np.full((size[1] + 1, size[0]), 5, np.uint8), "L").save(maps / "roughness.tmp.png")
        os.replace(maps / "roughness.tmp.png", png)
        settle(game)
        at = game.call("material", action="pick", world=floor).get("material")
        check(at and at[0] == 51, f"a map of another size is ignored (the entry's roughness): {at}")
        png.unlink()
        settle(game)

        # ---- identity: no entries vs entries with the switch off
        write_json(shipped, dict(EMPTY, presets=PRESETS, entries=[dict(entry, roughness=0.15, specular=1)]))
        settle(game)
        game.call("graphics", specular=0)
        settle(game)
        off = frame(game, out / "specular-off.png")
        check(not game.call("material")["active"], "specular off: RPC material active is false")
        check(np.array_equal(off, nothing), "specular off with an entry in view: identical to no entries "
              f"({int((off != nothing).any(2).sum())} pixels differ)")
        game.call("graphics", specular=1)
        settle(game)
        on = frame(game, out / "specular-on.png")
        changed = (on != nothing).any(2)
        if vulkan:
            game.call("graphics", view_mode=6)
            mask = frame(game, out / "material-view.png").sum(2) > 0
            game.call("graphics", view_mode=0)
            # (the HUD draws over everything in the material view too: count only 3D pixels that change)
            outside = changed & ~mask
            check(changed.sum() > 50, f"specular on: the floor changes ({int(changed.sum())} pixels)")
            check(outside.sum() == 0, f"only material pixels change ({int(outside.sum())} outside the mask)")

            # ---- the highlight moves with the camera
            def highlight(pose, name):
                game.call("editor_camera", **pose)
                game.run(12)
                # the specular-only view, less the same view with the lobe off (the HUD, 2D)
                game.call("graphics", view_mode=7)
                spec = frame(game, out / f"spec-{name}.png").sum(2)
                game.call("graphics", specular=0)
                settle(game)
                spec = spec - frame(game, out / f"spec-{name}-off.png").sum(2)
                game.call("graphics", view_mode=0, specular=1)
                settle(game)
                if spec.max() <= 0:
                    return None, 0
                y, x = np.unravel_index(int(np.argmax(spec)), spec.shape)
                hit = game.call("material", action="pick", x=int(x), y=int(y))
                return (tuple(hit.get("texel", [])) if hit.get("key") == key else None), int(spec.max())

            # the light 24 units over the floor, the eye 600 over it and 200 to either side: the mirror
            # point lies about 8 units from the light's foot, toward the eye
            pose_a = pose_looking_at((floor[0] - 200, py - 600, floor[2]), floor)
            pose_b = pose_looking_at((floor[0] + 200, py - 600, floor[2]), floor)
            a, peak_a = highlight(pose_a, "a")
            b, peak_b = highlight(pose_b, "b")
            check(peak_a > 30 and peak_b > 30, f"a highlight on the floor from both sides (peaks {peak_a}, {peak_b})")
            check(a is not None and b is not None and a != b,
                  f"the highlight moves across the floor's texels with the camera ({a} -> {b})")
            game.call("editor_camera", off=1)
        else:
            check(not changed.any(), f"OpenGL: no material response (the frame is identical; "
                  f"{int(changed.sum())} pixels differ)")
    finally:
        game.close()
    print(f"{'FAILED' if failures else 'passed'}: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
