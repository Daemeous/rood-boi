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
- the free camera is given back by play, seek and Stop.

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

    def rain_share(image):
        """Share of the picture that is the rain's blue (its lines are 96,96,128 -> 32,32,100)."""
        r, g, b = image[..., 0], image[..., 1], image[..., 2]
        return float(((b > r + 12) & (b > g + 12) & (b > 40)).mean())

    try:
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
    finally:
        game.close()
    log = (out / "game.log").read_text(errors="replace")
    check("[crash]" not in log, "no crash")
    print(f"cutscene_freecam_smoke: {'FAIL' if failures else 'PASS'} ({len(failures)} failures); {out}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
