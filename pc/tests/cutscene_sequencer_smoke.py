#!/usr/bin/env python3
"""RPC regression for sampled tracks, scene-only lighting and real camera input.

Uses a private Xvfb (SDL keyboard focus is required), scratch lighting/settings,
no memory card, and no pose setters. Pictures are compared at identical frames.
"""
import argparse
import os
import pathlib
import sys
import tempfile
import time

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Deadline, RpcError, wait_for_file
from checkpoints import launch_at
from cutscene_viewer_smoke import pixels_apart, ppm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out")
    args = parser.parse_args()
    base = pathlib.Path(args.out) if args.out else OUT / "cutscene_sequencer"
    base.mkdir(parents=True, exist_ok=True)
    out = pathlib.Path(tempfile.mkdtemp(prefix="run-", dir=base)).resolve()
    failures = []

    def check(ok, text):
        print(("ok:   " if ok else "FAIL: ") + text, flush=True)
        if not ok:
            failures.append(text)

    game = launch_at("title", fast=True, headless="xvfb", log=out / "game.log",
                     socket_path=f"/tmp/rood-sequencer-{os.getpid()}.sock", extra_env={
        "ROOD_CARD1": str(out / "absent1.mcd"), "ROOD_CARD2": str(out / "absent2.mcd"),
        "ROOD_LIGHTS": str(out / "lights"), "ROOD_CUTSCENES": str(out / "takes"),
        "ROOD_PHOTOS": str(out / "photos"), "ROOD_SETTINGS": str(out / "settings.cfg"),
        "ROOD_PREFABS": str(out / "prefabs"), "ROOD_PLUGINS": str(out / "plugins"),
        "ROOD_PLUGIN_DATA": str(out / "plugin-data"), "ROOD_VK_PIPELINE_CACHE": str(out / "vk-pipelines.bin"),
        "ROOD_DETERMINISTIC": "0", "ROOD_WINDOW": "960x720", "ROOD_GI": "0"})

    def cs(**params):
        return game.call("cutscene", **params)

    def settle():
        deadline = Deadline(180)
        while True:
            status = cs()
            if not status["busy"]:
                return status
            if deadline.passed():
                raise RpcError(f"cutscene busy: {status}")
            time.sleep(0.02)

    def go(frame):
        cs(action="seek", frame=frame)
        status = settle()
        check(status["frame"] == frame, f"seek lands exactly on {frame}")
        return status

    def picture(frame, name):
        # RPC pauses at the vsync boundary BEFORE its render. Release boundary f
        # to capture picture f, then stop at boundary f+1. A burst excludes paused redraws.
        go(frame)
        path = out / f"{name}.ppm"
        game.call("present_dump", path=str(path), count=1)
        cs(action="step", frames=1)
        path = pathlib.Path(str(path) + ".0000.ppm")
        if not wait_for_file(path, 60):
            raise RpcError("no presented picture")
        check(settle()["frame"] == frame + 1, f"picture {frame} rendered before boundary {frame + 1}")
        return path

    def paused_picture(name):
        # Observe the current presented view; never seek, step or reconstruct it.
        path = out / f"{name}.ppm"
        game.call("present_dump", path=str(path))
        if not wait_for_file(path, 60):
            raise RpcError(f"no paused picture: {name}")
        return path

    def lighting_picture(frame, name):
        # Capture the held boundary itself, with HUD/game2D hidden. A stepped
        # window dump includes the simple-map HUD, whose replay submission is
        # independent of the lighting layer under test. Keep the full image
        # and the same pixel tolerance; do not mask/crop that discrepancy.
        status = go(frame)
        previous = status["last_capture"]
        photo_before = game.call("photo")
        state_before = game.call("snapshot", action="hash")
        cs(action="capture", count=1, hide_text=True)
        status = settle()
        if status["capture_error"]:
            raise RpcError(f"clean lighting capture failed or fell back: {status['capture_error']}")
        path = pathlib.Path(status["last_capture"])
        if str(path) == previous or not path.is_file():
            raise RpcError(f"clean lighting capture did not produce a new file: {status}")
        check(status["frame"] == frame, f"clean lighting capture holds exact boundary {frame}")
        # capture_service drops its busy flag after resume; the photo service
        # may still owe a restoration redraw. Poll, never advance the game.
        deadline = Deadline(30)
        while True:
            photo_after = game.call("photo")
            if all(photo_after[k] == photo_before[k] for k in ("phase", "owners", "hidden", "camera")):
                break
            if deadline.passed():
                raise RpcError(f"capture restoration timed out: {photo_before} -> {photo_after}")
            time.sleep(0.02)
        check(cs()["frame"] == frame, "photo restoration leaves the capture boundary unchanged")
        check(True, "clean lighting capture restores photo ownership, visibility and camera")
        state_after = game.call("snapshot", action="hash")
        check(all(state_after[k] == state_before[k] for k in ("ram", "scratch", "io", "overlays", "vram")),
              "clean lighting capture preserves game state hashes")
        # Lossless RGB conversion lets the existing PPM comparison inspect every
        # pixel while retaining the original PNG as a capture artifact.
        converted = out / f"{name}.ppm"
        with Image.open(path) as image:
            image.convert("RGB").save(converted)
        return converted

    def look(**params):
        return cs(action="look", **params)

    def key(name, down):
        game.call("system", key=name, down=down)
        game.wait_presents(4)

    try:
        game.call("graphics", interpolate=False)
        scene = "room-009-04-s001"
        check(any(s["id"] == scene for s in cs(action="list")["scenes"]), "story scene is listed")
        cs(action="start", scene=scene)
        settle()
        go(300)
        tracks = cs(action="tracks", limit=256)
        keys = [k for t in tracks["tracks"] for k in t["keys"]]
        check(tracks["total"] > 0 and bool(keys) and any(t["keys"] for t in tracks["tracks"]),
              "sampled thread tracks have keys")
        if tracks["next_offset"] >= 0:
            page = cs(action="tracks", offset=tracks["next_offset"], limit=16)
            check(page["offset"] == tracks["next_offset"] and page["total"] == tracks["total"],
                  "sampled keys paginate without changing the total")
        if not keys:
            raise RpcError(f"no sampled keys: {tracks}")
        selected = next(k for k in keys if k["frame"] > 0)
        go(selected["frame"])
        check(all(k.get("name") and "opcode" in k and "arguments" in k for k in keys),
              "keys expose opcode names and decoded argument fields")

        before = lighting_picture(250, "before-base")
        at = lighting_picture(300, "at-base")
        check(sum(ppm(before)[2]) / len(ppm(before)[2]) > 2, "pre-key picture is visibly drawn")
        check(sum(ppm(at)[2]) / len(ppm(at)[2]) > 2, "key-frame picture is visibly drawn")
        doc = look()
        target = next(t for t in doc["targets"] if t["name"] == "baked_lighting")
        value = target["minimum"]
        doc = look(op="add", frame=300, target=target["name"], value=value, ease="step")
        authored = doc["keys"]
        check(len(authored) == 1, "scene lighting key added")
        same = lighting_picture(250, "before-key")
        lit = lighting_picture(300, "at-key")
        check(pixels_apart(before, same) <= 64, "lighting key does not affect a frame before its start")
        changed = pixels_apart(at, lit)
        check(changed > 100, f"lighting key changes the presented picture at its frame ({changed} pixels)")
        saved = look(op="save")
        check(pathlib.Path(saved["user_path"]).is_relative_to(out / "lights"), "save stays in scratch lighting directory")
        check(pathlib.Path(saved["user_path"]).is_file(), "scene lighting override saved")
        look(op="delete", id=authored[0]["id"])
        check(not look()["keys"], "key removed in memory")
        check(look(op="reload")["keys"] == authored, "saved key reloads with identical values")
        reloaded = lighting_picture(300, "at-reloaded")
        check(pixels_apart(lit, reloaded) <= 64, "reloaded key restores the same picture")
        look(op="delete", id=authored[0]["id"])

        go(250)
        baseline = next(t["value"] for t in look()["targets"] if t["name"] == "baked_lighting")
        check(baseline == target["value"], "deleting the step key restores the original room lighting baseline")
        look(op="add", frame=300, target="baked_lighting", value=0, ease="linear")
        look(op="add", frame=360, target="baked_lighting", value=1, ease="linear")
        go(250)
        current = lambda: next(t["value"] for t in look()["targets"] if t["name"] == "baked_lighting")
        check(current() == baseline, "linear track leaves the baseline before its first key")
        go(330)
        check(abs(current() - 0.5) < 1e-6, "linear lighting interpolates to 0.5 at frame 330")
        for k in look()["keys"]:
            look(op="delete", id=k["id"])
        check(current() == baseline, "deleting the final lighting key restores the room baseline")

        # Real SDL input; never set a pose to fake navigation.
        cs(action="open", on=False)
        game.call("editor_ui", visible=False)
        picture(300, "camera-established")
        game.wait_presents(8)
        reference = paused_picture("camera-reference")
        reference_pixels = ppm(reference)[2]
        check(sum(reference_pixels) / len(reference_pixels) > 2,
              "camera restoration reference contains a visible scene")
        reference_frame = cs()["frame"]
        photo_reference = game.call("photo")
        editor_before = game.call("editor_camera")
        state_before = game.call("snapshot", action="hash")
        for opened in (False, True):
            if opened:
                key("f1", True)
                key("f1", False)
            check(game.call("editor_ui")["visible"] == opened, f"F1 visibility is {opened}")
            game.wait_presents(8)
            cs(action="camera", on=True)
            game.wait_presents(8)
            initial = game.call("photo")["camera"]
            check(initial is not None, f"camera enabled, F1 {'open' if opened else 'closed'}")
            for axis, field in (("leftx", "position"), ("rightx", "yaw")):
                start_pose = game.call("photo")["camera"]
                game.call("system", virtual_pad=axis, down=True)
                game.wait_presents(30)
                game.call("system", virtual_pad=axis, down=False)
                game.wait_presents(4)
                check(game.call("photo")["camera"][field] != start_pose[field],
                      f"SDL {axis} moves {field}, F1 open={opened}")
            if not opened:
                game.call("system", mouse=[480, 360], button=3, down=True)
                game.wait_presents(4)
                start_pose = game.call("photo")["camera"]
                key("w", True)
                game.wait_presents(30)
                key("w", False)
                check(game.call("photo")["camera"]["position"] != start_pose["position"],
                      "RMB + W moves the free camera with F1 closed")
                game.call("system", mouse=[520, 380], button=0)
                game.wait_presents(8)
                check(game.call("photo")["camera"]["yaw"] != start_pose["yaw"],
                      f"RMB + mouse motion turns the free camera with F1 closed ({start_pose} -> {game.call('photo')['camera']})")
                game.call("system", mouse=[520, 380], button=3, down=False)
            trigger_start = game.call("photo")["camera"]
            game.call("system", virtual_pad="righttrigger", down=True)
            game.wait_presents(30)
            game.call("system", virtual_pad="righttrigger", down=False)
            game.wait_presents(4)
            moved = game.call("photo")["camera"]
            check(moved is not None and moved["position"] != trigger_start["position"],
                  f"synthetic SDL trigger moves free camera, F1 {'open' if opened else 'closed'}")
            game.wait_presents(8)
            check(game.call("photo")["camera"] == moved,
                  "released synthetic controls leave the camera stationary")
            cs(action="camera", on=False)
            game.wait_presents(8)
            check(game.call("photo")["camera"] is None, "disabling camera releases its pose")
            check(game.call("editor_camera") == editor_before, "borrowed editor camera state restored")
            cs(action="camera", on=True)
            game.wait_presents(8)
            restored = game.call("photo")["camera"]
            check(restored["position"] == initial["position"] and restored["yaw"] == initial["yaw"],
                  f"re-enabling starts from the original game camera ({initial} -> {restored})")
            cs(action="camera", on=False)
        state_after = game.call("snapshot", action="hash")
        check(all(state_after[k] == state_before[k] for k in ("ram", "scratch", "io", "overlays", "vram")),
              "free camera navigation leaves game state hashes unchanged")
        key("f1", True)
        key("f1", False)
        deadline = Deadline(30)
        while True:
            restored_photo = game.call("photo")
            if all(restored_photo[k] == photo_reference[k] for k in ("phase", "owners", "hidden", "camera")):
                break
            if deadline.passed():
                raise RpcError(f"camera restoration timed out: {restored_photo}")
            time.sleep(0.02)
        check(not game.call("editor_ui")["visible"], "F1 hidden for restored picture")
        restored_picture = paused_picture("camera-restored")
        check(cs()["frame"] == reference_frame, "camera restoration did not seek or advance the take")
        restored_difference = pixels_apart(reference, restored_picture)
        check(restored_difference == 0,
              f"original presented game view restored without seek ({restored_difference} differing pixels)")
        cs(action="camera", on=True)
        cs(action="stop")
        check(not cs()["camera"] and game.call("photo")["camera"] is None
              and "cutscene.camera" not in game.call("photo")["owners"],
              "stop leaves no borrowed camera or photo pause")
        cs(action="here")
        cs(action="camera", on=True)
        check(cs()["camera"] and game.call("photo")["camera"] is not None,
              "camera borrowed before switching the viewer scene")
        switched = cs(action="start", scene=scene)
        check(not switched["camera"] and game.call("photo")["camera"] is None
              and "cutscene.camera" not in game.call("photo")["owners"],
              "switching scenes releases camera and photo pause even without Stop")
    finally:
        game.close()
    print(f"cutscene_sequencer_smoke: {'FAIL' if failures else 'PASS'} ({len(failures)} failures); {out}")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
