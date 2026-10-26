#!/usr/bin/env python3
"""The opening's story scenes seen from the cutscene player's free camera.

Knights search the Duke's manor (room-001-00-s000) and Ashley in the
rain-soaked courtyard (room-002-00-s000), as the viewer plays them:

- rain: the room's weather rains; a drop beneath the manor's ceiling is left
  out of the picture (environment rain_probe), drops the game draws in the
  hall from a free camera are hidden by its walls and ceiling, and rain still
  falls in the open courtyard (render_stats rain_drops / rain_drops_hidden);
- the game's afterimage (a shot dissolve draws the game camera's previous
  frame over the new one): left out of the free camera's view only
  (render_stats afterimages_skipped), so the view is not smeared with the game
  shot and piled-up rain, and two renders of the same pose are the same;
- a group an event script hides (flag 0x100, func_800919D8) stays out of the
  free camera's whole room (render_stats instances);
- the free camera is given back by play, seek and Stop;
- the credits card (Art Direction / Character Design, the hall's take at
  f4200): black behind the text as the native picture (RPC screenshot, the
  PS1's raster) at the game camera with the editor closed and open, from the
  editor's free camera and from the viewer's;
- the baked shadow (room settings baked_shadow_*): the courtyard's far floor
  slab, which the PS1 baked dark, stays dark under the room's moon and sky
  (close to the native picture, darker than without the baked shadow) while
  the moonlit near floor keeps its light.

Pictures are compared with themselves only (game imagery: never committed)."""
import argparse
import os
import pathlib
import struct
import sys
import tempfile
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Deadline, RpcError, wait_for_file  # noqa: E402
from checkpoints import launch_at  # noqa: E402

HALL, COURTYARD = "room-001-00-s000", "room-002-00-s000"
CARD = 4200  # the hall's take: the credits card "Art Direction ... / Character Design ..."
CLOSE_UP = 1150  # the courtyard's take: Ashley close up, the far floor slab behind her
# The courtyard's moon and sky as the lighting packs' generated layer gives them (2/0 is open to the sky).
MOON = {"azimuth": 279, "elevation": 35, "color": [0.582, 0.655, 0.886], "intensity": 0.7, "angle": 2}
SKY = {"zenith": [0.074, 0.091, 0.149], "horizon": [0.18, 0.182, 0.214], "ground": [0.032, 0.03, 0.033],
       "sun_tint": 0.25, "ambient": 0.6}
# vs_battle_roomData (_mpdRoomSection, 146C.h): geometrySection, a 4-byte guest pointer.
ROOM_GEOMETRY = 0x64


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out")
    args = parser.parse_args()
    base = pathlib.Path(args.out) if args.out else OUT / "cutscene_freecam"
    base.mkdir(parents=True, exist_ok=True)
    out = pathlib.Path(tempfile.mkdtemp(prefix="run-", dir=base)).resolve()
    failures = []

    def check(ok, text):
        print(("ok:   " if ok else "FAIL: ") + text, flush=True)
        if not ok:
            failures.append(text)
        return ok

    game = launch_at("title", fast=True, log=out / "game.log",
                     socket_path=f"/tmp/rood-freecam-{os.getpid()}.sock", extra_env={
        "ROOD_CARD1": str(out / "absent1.mcd"), "ROOD_CARD2": str(out / "absent2.mcd"),
        "ROOD_LIGHTS": str(out / "lights"), "ROOD_CUTSCENES": str(out / "takes"),
        "ROOD_PHOTOS": str(out / "photos"), "ROOD_SETTINGS": str(out / "settings.cfg"),
        "ROOD_PREFABS": str(out / "prefabs"), "ROOD_PLUGIN_DATA": str(out / "plugin-data"),
        "ROOD_VK_PIPELINE_CACHE": str(out / "vk-pipelines.bin"), "ROOD_DETERMINISTIC": "0",
        "ROOD_WINDOW": "960x720", "ROOD_GI": "0"})

    def cs(**params):
        return game.call("cutscene", **params)

    def settle():
        deadline = Deadline(300)
        while True:
            status = cs()
            if not status["busy"]:
                return status
            if deadline.passed():
                raise RpcError(f"cutscene busy: {status}")
            time.sleep(0.02)

    def scene(name, frame):
        if cs().get("scene") != name:
            cs(action="start", scene=name)
            settle()
            cs(action="pause")
            settle()
        cs(action="seek", frame=frame)
        status = settle()
        check(status["frame"] == frame, f"{name}: at frame {frame}")

    def settled_photo():
        deadline = Deadline(60)
        game.wait_presents(2)
        while game.call("photo")["phase"] != "held":  # (rendering: the redraw's frames still run)
            if deadline.passed():
                raise RpcError(f"photo not held: {game.call('photo')}")
            time.sleep(0.02)
        game.wait_presents(4)

    def picture(name):
        path = out / f"{name}.ppm"
        game.call("present_dump", path=str(path))
        if not wait_for_file(path, 60):
            raise RpcError(f"no picture: {name}")
        time.sleep(0.1)
        return np.asarray(Image.open(path).convert("RGB")).astype(int)

    def camera(on, **pose):
        reply = cs(action="camera", on=on, **pose)
        if on:
            settled_photo()
        return reply

    def stats():
        return game.call("render_stats")

    def native():
        path = out / "native.ppm"
        path.unlink(missing_ok=True)
        game.call("screenshot", path=str(path))
        if not wait_for_file(path, 60):
            raise RpcError("no native picture")
        time.sleep(0.1)
        return np.asarray(Image.open(path).convert("RGB")).astype(int)[:224]

    def view(name):
        """The game's picture alone (the editor's panels cropped off), on the native 320 x 224 grid."""
        image = picture(name)
        x, y, w, h = game.call("ui_text")["overlay"]["view"]
        if w <= 0 or h <= 0:
            x, y, h, w = 0, 0, image.shape[0], image.shape[1]
        crop = image[y:y + h, x:x + w].astype(np.uint8)
        return np.asarray(Image.fromarray(crop).resize((320, 224), Image.BOX)).astype(int)

    def card_black(name, reference):
        """Mean of the picture where the native card is black (away from its letters), the letters' mean
        brightness against the native's."""
        image = view(name)
        lum, ref = image.max(axis=2), reference.max(axis=2)
        letters = ref > 40
        near = np.zeros_like(letters)
        for dy in range(-3, 4):
            for dx in range(-3, 4):
                near |= np.roll(np.roll(letters, dy, 0), dx, 1)
        ground = ~near & (ref < 10)
        return float(lum[ground].mean()), float(lum[letters].mean()) / max(float(ref[letters].mean()), 1.0)

    def rain_share(image):
        """Share of the picture that is the rain's blue (its lines are 96,96,128 -> 32,32,100)."""
        r, g, b = image[..., 0], image[..., 1], image[..., 2]
        return float(((b > r + 12) & (b > g + 12) & (b > 40)).mean())

    try:
        # ---- the credits card: black behind the text in every camera mode ----
        game.call("graphics", widescreen=False)
        scene(HALL, CARD)
        game.wait_presents(4)
        reference = native()
        ok = (reference.max(axis=2) > 40).mean() > 0.01 and (reference.max(axis=2) < 10).mean() > 0.8
        check(ok, f"f{CARD}: the native picture is the credits card (text on black)")
        ground, letters = card_black("card-game", reference)
        check(ground < 4 and letters > 0.6, f"card at the game camera: black behind the text ({ground:.1f}), "
              f"the letters there ({letters:.2f} of native)")
        game.call("editor_ui", visible=True)
        game.wait_presents(6)
        ground, letters = card_black("card-editor", reference)
        check(ground < 4 and letters > 0.6, f"card with the editor open: black ({ground:.1f}), letters {letters:.2f}")
        lo, hi = game.call("environment")["bounds"]
        centre = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2]
        for yaw in (0, 90, 200):
            game.call("editor_camera", x=centre[0], y=centre[1], z=centre[2], yaw=yaw, pitch=-10)
            cs(action="step", frames=2)
            settle()
            game.wait_presents(6)
            ground, letters = card_black(f"card-editor-free-{yaw}", reference)
            check(ground < 4 and letters > 0.6, f"card from the editor's free camera (yaw {yaw}): black ({ground:.1f}), "
                  f"letters {letters:.2f}")
        game.call("editor_camera", off=1)
        game.call("editor_ui", visible=False)
        cs(action="seek", frame=CARD)
        settle()
        reference = native()
        camera(True)
        pose = game.call("photo")["camera"]
        for k, (dx, dy) in enumerate(((0, 0), (300, -200), (-400, 100))):
            camera(True, position=[pose["position"][0] + dx, pose["position"][1] + dy, pose["position"][2]],
                   target=pose["target"])
            ground, letters = card_black(f"card-free-{k}", reference)
            check(ground < 4 and letters > 0.6, f"card from the viewer's free camera (pose {k}): black ({ground:.1f}), "
                  f"letters {letters:.2f}")
        camera(False)
        ground, letters = card_black("card-back", reference)
        check(ground < 4 and letters > 0.6, f"card at the game camera again: black ({ground:.1f}), letters {letters:.2f}")
        game.call("graphics", widescreen=True)

        # ---- rain in the hall: beneath its ceiling, nothing ----
        scene(HALL, 1620)
        env = game.call("environment")
        check(env["raining"], "the hall's weather rains (the original draws its drops indoors too)")
        lo, hi = env["bounds"]
        inside = [[lo[0] + (hi[0] - lo[0]) * fx, hi[1] - 120, lo[2] + (hi[2] - lo[2]) * fz]
                  for fx in (0.3, 0.5, 0.7) for fz in (0.3, 0.5, 0.7)]
        hidden = [game.call("environment", rain_probe=p)["rain_hidden"] for p in inside]
        check(sum(hidden) >= len(hidden) - 1, f"drops beneath the hall's ceiling are left out ({sum(hidden)}/{len(hidden)})")
        before = stats()
        mid = [(lo[0] + hi[0]) / 2, hi[1] - 300, (lo[2] + hi[2]) / 2]
        camera(True, position=mid, target=[mid[0], hi[1] - 200, hi[2]])
        after = stats()
        drops = after["rain_drops"] - before["rain_drops"]
        gone = after["rain_drops_hidden"] - before["rain_drops_hidden"]
        check(drops > 0 and gone > 0, f"free camera in the hall: the drops it would see are hidden ({gone} of {drops})")
        camera(False)

        # ---- the free camera given back ----
        camera(True)
        check(cs()["camera"] and game.call("photo")["camera"] is not None, "free camera borrowed")
        cs(action="seek", frame=1625)
        settle()
        check(not cs()["camera"] and game.call("photo")["camera"] is None, "a seek gives the camera back")
        camera(True)
        cs(action="play")
        game.run(4)
        cs(action="pause")
        settle()
        check(not cs()["camera"] and game.call("photo")["camera"] is None
              and "cutscene.camera" not in game.call("photo")["owners"], "play gives the camera and pause back")

        # ---- rain in the open courtyard still falls ----
        scene(COURTYARD, 1400)
        before = stats()
        cs(action="step", frames=30)
        settle()
        after = stats()
        drawn = (after["rain_drops"] - before["rain_drops"]) - (after["rain_drops_hidden"] - before["rain_drops_hidden"])
        check(drawn > 0, f"rain falls in the open courtyard ({drawn} drops drawn in 30 frames)")

        # ---- the shot dissolve's afterimage: the game camera's only ----
        scene(COURTYARD, 1880)
        before = stats()
        cs(action="step", frames=2)
        settle()
        check(stats()["afterimages_skipped"] == before["afterimages_skipped"],
              "the game camera keeps the game's afterimage (the dissolve as on the PS1)")
        shot = picture("dissolve-game")
        camera(True)
        check(stats()["afterimages_skipped"] > before["afterimages_skipped"],
              "the free camera leaves the game camera's previous frame out")
        first = picture("dissolve-free")
        check(rain_share(first) <= rain_share(shot) + 0.02,
              f"no piled-up rain in the free view ({rain_share(first):.1%}, game {rain_share(shot):.1%})")
        pose = game.call("photo")["camera"]
        eye, look = pose["position"], pose["target"]
        camera(True, position=eye, target=look)
        first = picture("dissolve-free-set")
        camera(True, position=[eye[0] + 200, eye[1], eye[2]], target=look)
        camera(True, position=eye, target=look)
        again = picture("dissolve-free-again")
        diff = float(np.abs(again - first).mean())
        check(diff < 1.0, f"the same pose renders the same picture after another render ({diff:.2f})")
        camera(False)

        # ---- a group an event script hides stays hidden from the free camera ----
        scene(COURTYARD, 300)
        raw = game.read_symbol("vs_battle_roomData", ROOM_GEOMETRY + 4)
        geometry = struct.unpack_from("<I", raw, ROOM_GEOMETRY)[0]
        count = struct.unpack("<I", game.read(geometry, 4))[0]
        groups = game.call("room_groups")["groups"]
        shown = [i for i, x in enumerate(groups) if not x["black"] and not x["flags"] & 0x100
                 and not x["flags"] & 0x8000]
        check(count == len(groups) and len(shown) > 2, f"the courtyard's groups ({len(shown)} of {count} shown)")
        if shown:
            target = max(shown, key=lambda i: groups[i]["polygons"])
            address = geometry + 4 + target * 0x40
            flags = struct.unpack("<I", game.read(address, 4))[0]
            camera(True, position=[-2500, -5000, -5000], target=[1300, -1000, 0])
            whole = stats()["instances"]
            camera(False)
            game.write(address, struct.pack("<I", flags | 0x100))  # as func_800919D8 (an event script) does
            camera(True, position=[-2500, -5000, -5000], target=[1300, -1000, 0])
            hid = stats()["instances"]
            camera(False)
            game.write(address, struct.pack("<I", flags))
            check(hid == whole - 1, f"the free camera leaves out the group the event hid ({whole} -> {hid} instances)")

        # ---- the baked shadow: the far floor slab the PS1 baked dark stays dark ----
        game.call("graphics", widescreen=False)
        scene(COURTYARD, CLOSE_UP)
        game.call("environment", sun=MOON, sky=SKY)
        # The surfaces' light alone: the haze in front of the slab (lit by the same moon) is the air's, not the slab's.
        game.call("room_settings", haze_density=0)
        game.wait_presents(20)
        reference = native()
        game.call("graphics", view_mode=4)  # the vertex light, halved
        game.wait_presents(6)
        baked = view("slab-vertex-light").max(axis=2) / 127.5
        game.call("graphics", view_mode=0)
        rows = np.zeros(baked.shape, bool)
        rows[34:56] = True  # the slab's band above the near floor, behind Ashley
        slab = rows & (baked < 0.12) & (reference.max(axis=2) < 12)
        floor = np.zeros(baked.shape, bool)
        floor[150:200, 0:60] = True  # the moonlit near floor, left of Ashley
        floor &= baked > 0.3
        check(slab.sum() > 1500 and floor.sum() > 1000, f"the slab's dark-baked pixels ({slab.sum()}) and the "
              f"near floor's ({floor.sum()})")
        game.wait_presents(20)
        after = view("slab-after").max(axis=2)
        game.call("room_settings", baked_shadow_level=0)
        game.wait_presents(20)
        before = view("slab-before").max(axis=2)
        game.call("room_settings", baked_shadow_level="default")
        game.call("environment", sun={"enabled": False}, sky={"enabled": False})
        game.wait_presents(20)
        unlit = view("slab-unlit").max(axis=2)  # without the moon and sky (the haze and the raster stay)
        a, b, u = float(after[slab].mean()), float(before[slab].mean()), float(unlit[slab].mean())
        n = float(reference.max(axis=2)[slab].mean())
        check(a - u <= 1.5 and b - u >= 2.5, f"the slab: the moon and sky add {a - u:.1f} in the baked shadow, "
              f"{b - u:.1f} without it (picture {a:.1f}, unlit {u:.1f}, native {n:.1f})")
        fa, fb, fu = float(after[floor].mean()), float(before[floor].mean()), float(unlit[floor].mean())
        check(fa - fu >= 0.5 * (fb - fu), f"the moonlit near floor keeps the moon and sky's light "
              f"(+{fa - fu:.1f}, without the baked shadow +{fb - fu:.1f})")
        game.call("room_settings", haze_density="default")
        game.call("graphics", widescreen=True)
    finally:
        game.close()
    log = (out / "game.log").read_text(errors="replace")
    check("[crash]" not in log, "no crash")
    print(f"cutscene_freecam_smoke: {'FAIL' if failures else 'PASS'} ({len(failures)} failures); {out}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
