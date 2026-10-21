#!/usr/bin/env python3
"""The photo mode foundation (pc/src/photo.cpp, pc/src/lua/lua_api_photo.cpp;
pc/docs/Extensions.md "Photo mode foundation") on a renderer (run it with
--renderer gl and --renderer vulkan):

    python3 pc/tests/photo_foundation_smoke.py [--card ...] [--out DIR] [--engine-reload]

One game from the save_room checkpoint; the example plugin
pc/tests/lua/plugins/test.photo in its own ROOD_PLUGINS folder, its six
capabilities granted (RPC lua grant; settings.cfg is read after that and must
be the same at the end); private photos (ROOD_PHOTOS). Driven from the REPL
in the plugin's sandbox:
  - the grants: the functions exist only when granted (the console sandbox has
    rood.game.paused, not pause, nor rood.camera ...);
  - rood.game.pause: held at once at the boundary, RPC steps dropped (the vsync
    stays), the state digest the same through everything below, refused twice
    is one hold;
  - rood.post: list() (ranges, labels, choices, the format), set and get (dof,
    f-stop, exposure, vignette, grain, aberration, tilt-shift, format), a bad
    key refused, a value clamped, overrides(); the user's layer untouched;
  - rood.camera: begin at the game's camera (a render), orbit (the picture
    changes), set fov / roll / collision, move; get; another plugin refused;
  - rood.ui.hide: the HUD's corner (HP, MP) gone from the picture, back on
    show (the same picture as before, deterministic renders);
  - rood.capture.photo: a PNG in the photos folder with a timestamped name,
    cropped to the format; scale 2 says so; a path without fs.write refused;
  - rood.input.capture (the game running): Circle (battle mode) reaches the
    plugin as a button event, never the game; Start gives the input back (the
    plugin told); Escape too; an input handler that raises gives it back;
  - the example's own session (M.start: pause, orbit, DoF, exposure, hide,
    capture, give back) to its end: the photo written, everything restored;
  - a plugin unloaded (disabled) while holding everything: all given back;
  - the end: unpaused, the camera the game's (the same picture as before the
    session), post values as before, settings.cfg unchanged, the state digest
    (RAM, scratch, I/O, VRAM, overlays, vsync) the held one.
--engine-reload (the dev64 split build, pc/build/dev64/rood_host): the same
holds, then RPC reload: everything given back, the moment's state kept, the
plugin started again with nothing held."""
import argparse
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import time
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT, Deadline  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402

CAPABILITIES = ["game.pause", "camera", "post", "ui.hide", "capture", "input.capture"]
DIGEST = ("ram", "scratch", "io", "vram", "overlays", "vsync")


def png_size(path):
    data = pathlib.Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", data[16:24])


def ppm(path):
    """(w, h, bytes) of a binary PPM."""
    data = pathlib.Path(path).read_bytes()
    parts, at = [], 0
    while len(parts) < 4:
        while data[at:at + 1].isspace():
            at += 1
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[at:end])
        at = end
    return int(parts[1]), int(parts[2]), data[at + 1:]


def difference(a, b, box=None):
    """Mean absolute difference (0..255) of two same-size PPM images, over box (x0, y0, x1, y1 in 0..1)."""
    w, h, pa = a
    _, _, pb = b
    x0, y0, x1, y1 = box or (0, 0, 1, 1)
    total = count = 0
    for y in range(int(y0 * h), int(y1 * h), 2):
        row = y * w * 3
        for x in range(int(x0 * w), int(x1 * w), 2):
            i = row + x * 3
            total += abs(pa[i] - pb[i]) + abs(pa[i + 1] - pb[i + 1]) + abs(pa[i + 2] - pb[i + 2])
            count += 3
    return total / max(count, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="logs, the plugins' copy, the photos")
    parser.add_argument("--engine-reload", action="store_true",
                        help="the dev64 split build: holds given back across RPC reload")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if os.environ.get("ROOD_RENDERER") == "null":
        print("SKIP: the photo mode's renders need a renderer")
        return 77
    if args.engine_reload:
        host = ROOT / "pc/build/dev64/rood_host"
        if not sys.platform.startswith("linux"):
            print("SKIP: the engine library reloads on Linux")
            return 77
        if os.environ.get("ROOD_ENGINE_SMOKE_NO_BUILD") != "1":  # (as engine_reload_smoke: dev64 kept current)
            command = [sys.executable, str(ROOT / "pc/tools/build_boot.py"), "--arch", "64", "--engine-library"]
            if os.environ.get("ROOD_BUILD_LOCK"):
                command = ["flock", os.environ["ROOD_BUILD_LOCK"], *command]
            built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            if built.returncode:
                print(built.stdout[-3000:] + built.stderr[-3000:])
                return 1
        if not host.exists():
            print("SKIP: no dev64 build (build_boot.py --arch 64 --engine-library)")
            return 77
        rood_rpc.BINARY = host
        # (as engine_reload_smoke: the runner's checkpoint save states come from the static binary, whose
        # code layout the split host does not share; boot instead of resuming one)
        os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
        os.environ["ROOD_SYMBOLS"] = str(host)
    out = pathlib.Path(args.out) if args.out else OUT / "photo_foundation"
    shutil.rmtree(out / "plugins", ignore_errors=True)
    shutil.rmtree(out / "photos", ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    with card_guard(out / "cards", source=args.card) as cards:
        return run(out, cards.card1, args.engine_reload)


def run(out, card, engine_reload):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    plugins = out / "plugins"
    shutil.copytree(ROOT / "pc/tests/lua/plugins/test.photo", plugins / "test.photo")
    photos = out / "photos"
    settings = out / "settings.cfg"
    settings.write_text("aspect 4:3\nui_menus 0\n")
    env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(plugins),
           "ROOD_PLUGIN_DATA": str(out / "plugin_data"), "ROOD_PHOTOS": str(photos), "ROOD_GI": "0",
           "ROOD_PAUSED_REDRAW_HZ": "60", "ROOD_DETERMINISTIC": "1"}
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        def ev(code, plugin="test.photo"):
            """Evaluate in a sandbox (a session per sandbox): (ok, results, error)."""
            r = g.repl("eval", session="photo-" + plugin.strip("@").replace(".", "-"), plugin=plugin, code=code)
            return r.get("ok"), r.get("results"), r.get("error")

        def value(code, plugin="test.photo"):
            ok, results, error = ev(code, plugin)
            if not ok:
                raise rood_rpc.RpcError(f"{code}: {error}")
            return results[0] if results else None

        def photo():
            return g.call("photo")

        def rendered(before, what):
            """Wait for a render after `before` renders (real time: the paused loop runs them)."""
            deadline = Deadline(30)
            while photo()["renders"] <= before:
                if deadline.passed():
                    return check(False, f"{what}: a render ({photo()})")
                time.sleep(0.01)
            g.wait_presents(2)
            return True

        def shot(name):
            path = out / f"{name}.ppm"
            path.unlink(missing_ok=True)
            g.call("present_dump", path=str(path))
            if not rood_rpc.wait_for_file(path, 30):
                raise rood_rpc.RpcError("no frame presented")
            return ppm(path)

        def digest():
            h = g.call("snapshot", action="hash")
            return {k: h[k] for k in DIGEST}

        for capability in CAPABILITIES:
            g.call("lua", action="grant", plugin="test.photo", capability=capability)
        g.run(4)
        info = g.lua_plugins()["test.photo"]
        check(info["state"] == "loaded" and sorted(info["granted"]) == sorted(CAPABILITIES),
              f"test.photo loaded with its six grants ({info['state']}, {info['granted']})")
        settings_before = settings.read_text()
        check(value("rood.camera ~= nil and rood.post ~= nil and rood.ui ~= nil and rood.capture ~= nil and "
                    "rood.input ~= nil and rood.game.pause ~= nil") is True, "granted: the six APIs exist")
        check(value("rood.camera == nil and rood.post == nil and rood.ui == nil and rood.capture == nil and "
                    "rood.input == nil and rood.game.pause == nil and type(rood.game.paused) == 'function'",
                    plugin="@console") is True, "not granted (the console): only rood.game.paused")
        check(value("rood.plugin.elevated") is False, "the photo capabilities are not elevated ones")

        # ---- post: list, the live values before ----
        keys = ["dof", "dof_fstop", "exposure", "vignette", "grain", "chromatic_aberration", "tilt_shift",
                "bloom_intensity", "dof_focus", "format"]
        listed = value("local out = {} for _, f in ipairs(rood.post.list()) do out[f.key] = f end return out")
        check(all(k in listed for k in keys + ["preset"]) and abs(listed["dof_fstop"]["min"] - 1.2) < 1e-4
              and listed["dof_fstop"]["max"] == 22
              and listed["dof_focus"]["choices"][0] == "ashley" and listed["exposure"]["group"] == "Lens|Exposure"
              and listed["format"]["group"] == "Photo|Format",
              "rood.post.list: every photo key with its range, group, choices (the format too)")
        post_before = {k: value(f"rood.post.get('{k}')") for k in keys}
        print("post before:", post_before)
        user_post = g.call("post")
        image_game = shot("game")

        # ---- pause ----
        check(value("rood.game.paused()") is False, "not paused at first")
        v0 = g.status()["vsync"]
        check(value("rood.game.pause()") is True and value("rood.game.pause()") is True, "rood.game.pause (twice: one hold)")
        p = photo()
        check(p["phase"] == "held" and p["owners"] == ["test.photo"] and p["held_vsync"] == v0,
              f"held at once, at the boundary it was asked at ({p['phase']}, {p['held_vsync']} == {v0})")
        held = digest()
        g.run(30)
        check(g.status()["vsync"] == v0 and digest() == held, "RPC steps dropped while held: no vsync, the same state")
        check(value("rood.game.paused()") is True, "rood.game.paused()")
        # the frozen moment drawn with the game's camera (a render: the overlay hidden, nothing else)
        r = photo()["renders"]
        value("rood.ui.hide { overlay = true }")
        rendered(r, "a render of the game's camera")
        image_reference = shot("reference")
        r = photo()["renders"]
        value("rood.ui.show()")
        rendered(r, "shown")

        # ---- post overrides ----
        ok, results, error = ev("rood.post.set('dof', true), rood.post.set('dof_fstop', 1.4), "
                                "rood.post.set('exposure', 0.7), rood.post.set('vignette', 0.4), "
                                "rood.post.set('grain', 0.3), rood.post.set('chromatic_aberration', 1.5), "
                                "rood.post.set('tilt_shift', true), rood.post.set('format', 2.39)")
        check(ok and results == [True] * 8, f"rood.post.set: eight photo keys ({results}, {error})")
        got = value("{ rood.post.get('dof'), rood.post.get('dof_fstop'), rood.post.get('exposure'), "
                    "rood.post.get('vignette'), rood.post.get('grain'), rood.post.get('chromatic_aberration'), "
                    "rood.post.get('tilt_shift'), rood.post.get('format') }")
        check(got[0] is True and abs(got[1] - 1.4) < 1e-3 and abs(got[2] - 0.7) < 1e-3 and abs(got[3] - 0.4) < 1e-3
              and abs(got[4] - 0.3) < 1e-3 and abs(got[5] - 1.5) < 1e-3 and got[6] is True and abs(got[7] - 2.39) < 1e-3,
              f"rood.post.get: the live values are the overrides ({got})")
        check(value("rood.post.set('preset', 'film') and rood.post.get('preset')") == "film"
              and "film" in listed["preset"]["choices"], "rood.post.set('preset', 'film'): a filter")
        ok, results, _ = ev("rood.post.set('preset', 'no-such-preset')")
        check(results and results[0] is None and "no post preset" in results[1], f"an unknown preset refused ({results})")
        ok, results, _ = ev("rood.post.set('no_such_key', 1)")
        check(results and results[0] is None and "no post field" in results[1], f"an unknown key refused ({results})")
        check(value("rood.post.set('exposure', 99) and rood.post.get('exposure')") == 4,
              "a value past the range clamped (exposure 99: 4)")
        value("rood.post.set('exposure', 0.7)")
        check(set(value("rood.post.overrides()")) >= {"dof", "exposure", "format"}, "rood.post.overrides()")
        check(g.call("post")["layers"] == user_post["layers"] if "layers" in user_post else True,
              "the user's post layers untouched")

        # ---- camera ----
        image_held = shot("held")
        check(value("rood.camera.get()") is None, "rood.camera.get(): nil before begin")
        r = photo()["renders"]
        pose0 = value("assert(rood.camera.begin()) return rood.camera.get()")
        rendered(r, "camera.begin")
        image_begin = shot("begin")
        check(pose0 and pose0["fov"] > 5 and "target" in pose0, f"rood.camera.begin: the game's pose ({pose0})")
        r = photo()["renders"]
        check(value("rood.camera.orbit(40, 15)") is True, "rood.camera.orbit(40, 15)")
        rendered(r, "orbit")
        image_orbit = shot("orbit")
        moved = difference(image_begin, image_orbit)
        check(moved > 8, f"the orbit moves the picture (mean difference {moved:.1f})")
        pose1 = value("rood.camera.get()")
        check(abs((pose1["yaw"] - pose0["yaw"] - 40 + 180) % 360 - 180) < 0.5
              and abs(pose1["target"]["x"] - pose0["target"]["x"]) < 1e-3,
              f"orbited about the target: yaw +40, the target kept ({pose0['yaw']:.1f} -> {pose1['yaw']:.1f})")
        r = photo()["renders"]
        check(value("rood.camera.set { fov = 40, roll = 8, collision = true }") is True, "set fov, roll, collision")
        rendered(r, "fov/roll")
        pose2 = value("rood.camera.get()")
        check(abs(pose2["fov"] - 40) < 1e-3 and abs(pose2["roll"] - 8) < 1e-3 and pose2["collision"] is True,
              f"the pose has them ({pose2['fov']}, {pose2['roll']}, {pose2['collision']})")
        image_fov = shot("fov")
        check(difference(image_orbit, image_fov) > 4, "fov and roll change the picture")
        r = photo()["renders"]
        check(value("rood.camera.move(0, 0, 200)") is True, "rood.camera.move (view space)")
        rendered(r, "move")
        ok, results, _ = ev("rood.camera.begin()", plugin="@console")
        check(not ok or (results and results[0] is None), "the console (no grant) cannot begin the camera")
        check(digest() == held, "renders leave the state digest as held")

        # ---- ui ----
        value("rood.camera.set { collision = false } rood.camera.move(0, 0, -200)")
        value("rood.post.set('format', 0) rood.post.reset('grain') rood.post.reset('preset')")  # (grain: new noise)
        value("rood.camera.set { fov = %f, roll = 0 }" % pose0["fov"])
        r = photo()["renders"]
        value("rood.camera.orbit(-40, -15)")
        rendered(r, "back")
        image_shown = shot("shown")
        r = photo()["renders"]
        check(value("rood.ui.hide { hud = true, game2d = true }") is True, "rood.ui.hide{hud, game2d}")
        rendered(r, "hide")
        hidden = value("rood.ui.hidden()")
        check(hidden == {"hud": True, "game2d": True, "overlay": False}, f"rood.ui.hidden() ({hidden})")
        image_hidden = shot("hidden")
        corner = (0.0, 0.0, 0.3, 0.1)  # the HP / MP gauges' row
        d_corner = difference(image_shown, image_hidden, corner)
        d_middle = difference(image_shown, image_hidden, (0.3, 0.3, 0.7, 0.7))
        check(d_corner > 6 and d_corner > 3 * d_middle,
              f"the HUD's corner gone, the scene kept (corner {d_corner:.1f}, middle {d_middle:.1f})")
        r = photo()["renders"]
        value("rood.ui.show()")
        rendered(r, "show")
        image_again = shot("again")
        check(difference(image_shown, image_again) < 1.0,
              f"shown again: the same picture (renders are deterministic: {difference(image_shown, image_again):.2f})")
        check(digest() == held, "hiding leaves the state digest as held")

        # ---- capture ----
        r = photo()["renders"]
        value("rood.post.set('format', 2.39) rood.ui.hide()")
        rendered(r, "hide all")
        ok, results, error = ev("return rood.capture.photo { scale = 2 }")
        path = pathlib.Path(results[0]) if ok and results else None
        check(path and path.parent == photos and path.name.startswith("rood-boi-") and path.suffix == ".png"
              and len(results) == 3 and "not supported" in results[2],
              f"rood.capture.photo: a timestamped name in the photos folder; scale 2 says so ({results}, {error})")
        deadline = Deadline(30)
        while not (path and path.exists()) and not deadline.passed():
            time.sleep(0.02)
        size = png_size(path) if path and path.exists() else None
        check(size and size[0] == 960 and abs(size[1] - round(960 / 2.39)) <= 1,
              f"the PNG written, cropped to the 2.39 format ({size})")
        status = value(f"rood.capture.status({results[1]})") if ok else None
        check(status and status["done"] and status["ok"] and status["path"] == str(path),
              f"rood.capture.status ({status})")
        ok, results, _ = ev("rood.capture.photo { path = '/tmp/elsewhere.png' }")
        check(results and results[0] is None and "fs.write" in results[1], f"a path without fs.write refused ({results})")
        value("rood.ui.show()")

        # ---- the session ends: everything given back ----
        r = photo()["renders"]
        ev("rood.camera.finish(); rood.post.reset(); rood.ui.show()")
        rendered(r, "finish")
        check(value("rood.camera.get()") is None and photo()["camera"] is None, "rood.camera.finish")
        image_finished = shot("finished")
        check(difference(image_reference, image_finished) < 1.0,
              f"the game's camera again: the same render as before begin ({difference(image_reference, image_finished):.2f})")
        check({k: value(f"rood.post.get('{k}')") for k in keys} == post_before, "rood.post.reset: the values as before")
        ev("rood.game.resume()")
        g.wait_presents(2)
        p = photo()
        check(p["phase"] == "running" and not p["owners"] and not p["mark"], f"resumed ({p['phase']})")
        check(digest() == held, "resumed at the held state: the same digest, the same vsync")
        g.run(8)
        check(g.status()["vsync"] == v0 + 8, "the game runs again")

        # ---- input capture (the game running) ----
        def ashley_battle():
            return value("rood.game.ashley().battle")

        battle0 = ashley_battle()
        value("M.got = {} rood.input.capture(function(e) M.got[#M.got + 1] = e end)")
        check(photo()["input"] == "test.photo", "rood.input.capture")
        g.call("system", virtual_pad="east", down=True)
        g.run(6)
        g.call("system", virtual_pad="east", down=False)
        g.run(6)
        got = value("M.got")
        buttons = [(e["name"], e["down"], e["device"]) for e in got if e["type"] == "button"]
        check(("circle", True, "pad") in buttons and ("circle", False, "pad") in buttons and "held" in got[0]
              and "left" in got[0], f"Circle reaches the plugin: down, up, the pad, held state and sticks ({buttons})")
        check(ashley_battle() == battle0, "the game does not see it (battle mode unchanged)")
        g.call("system", key="w", down=True)
        g.run(2)
        g.call("system", key="w", down=False)
        g.run(2)
        keys_seen = [(e["name"], e["down"]) for e in value("M.got") if e["type"] == "key"]
        check(("w", True) in keys_seen and ("w", False) in keys_seen, f"keys by name ({keys_seen})")
        g.call("system", virtual_pad="start", down=True)
        g.run(4)
        g.call("system", virtual_pad="start", down=False)
        g.run(4)
        got = value("M.got")
        check(photo()["input"] == "" and any(e["type"] == "released" and e.get("reason") == "start" for e in got)
              and value("rood.input.captured()") is False, "Start gives the input back (the plugin told)")
        value("M.got = {} rood.input.capture(function(e) M.got[#M.got + 1] = e end)")
        g.call("system", key="escape", down=True)
        g.run(2)
        g.call("system", key="escape", down=False)
        g.run(2)
        check(photo()["input"] == "" and any(e["type"] == "released" and e.get("reason") == "escape"
                                             for e in value("M.got")), "Escape gives the input back")
        value("rood.input.capture(function(e) error('boom') end)")
        g.call("system", key="w", down=True)
        g.run(2)
        g.call("system", key="w", down=False)
        g.run(2)
        check(photo()["input"] == "", "an input handler that raises gives the input back")
        g.call("system", virtual_pad="east", down=True)
        g.run(6)
        g.call("system", virtual_pad="east", down=False)
        g.run(30)
        check(ashley_battle() != battle0, "released: the game sees Circle again (battle mode toggled)")
        g.call("system", virtual_pad="south", down=True)
        g.run(6)
        g.call("system", virtual_pad="south", down=False)
        g.run(30)

        # ---- the example's own session ----
        g.run(4)
        before_session = digest()
        g.mark()  # the same 6 vsyncs without the session, for the picture after it
        g.run(6)
        image_without = shot("without-session")
        g.rewind()
        check(digest() == before_session, "(rewound for the session)")
        ok, results, error = ev("return M.start()")
        check(ok and results and str(results[0]).endswith(".png"), f"M.start(): the example's session ({results}, {error})")
        deadline = Deadline(60)
        while value("M.state") != "done" and not deadline.passed():
            time.sleep(0.05)
        result = value("M.photo")
        check(value("M.state") == "done" and result and result["ok"] and pathlib.Path(result["path"]).exists(),
              f"the session took its photo and gave everything back ({value('M.state')}, {result})")
        g.wait_presents(2)
        p = photo()
        check(p["phase"] == "running" and p["camera"] is None and p["hidden"] == 0 and p["format"] == 0
              and p["input"] == "", f"nothing held after it ({p})")
        check(digest() == before_session, "its renders and capture left the state as it was")
        check({k: value(f"rood.post.get('{k}')") for k in keys} == post_before, "post values as before the session")
        g.run(6)
        image_after_session = shot("after-session")
        check(difference(image_without, image_after_session) < 1.0,
              f"6 vsyncs on: the picture the game draws without the session "
              f"({difference(image_without, image_after_session):.2f})")

        # ---- unloaded while holding everything ----
        value("rood.game.pause() rood.camera.begin() rood.camera.orbit(30, 0) rood.post.set('vignette', 1) "
              "rood.post.set('format', 1) rood.ui.hide() rood.input.capture(function() end)")
        held2 = digest()
        g.call("lua", action="reload", plugin="test.photo")
        g.wait_presents(4)
        p = photo()
        check(p["phase"] == "running" and not p["owners"] and p["camera"] is None and p["hidden"] == 0
              and p["format"] == 0 and p["input"] == "", f"reloaded (an unload): everything given back ({p})")
        check(value("rood.post.get('vignette')") == post_before["vignette"] and value("M.state") == "idle",
              "its post overrides gone, the plugin fresh")
        check(digest() == held2, "and the state as it was held")
        g.run(4)

        if engine_reload:
            r = photo()["renders"]
            value("rood.game.pause() rood.camera.begin() rood.camera.orbit(30, 0) rood.post.set('vignette', 1) "
                  "rood.ui.hide() rood.input.capture(function() end)")
            rendered(r, "before the reload")
            held3 = digest()
            reply = g.reload()
            print("reload:", reply)
            g.wait_presents(4)
            p = photo()
            check(p["phase"] == "running" and not p["owners"] and p["camera"] is None and p["hidden"] == 0
                  and p["input"] == "", f"an engine reload gives everything back ({p})")
            check(digest() == held3, "the moment's state kept across the reload")
            check(value("rood.game.paused()") is False and value("M.state") == "idle",
                  "the plugin started again with nothing held")

        # ---- settings.cfg ----
        check(settings.read_text() == settings_before, "settings.cfg unchanged")
        log = (out / "game.log").read_text(errors="replace")
        check("[lua] test.photo error" not in log or "boom" in log, "no unexpected plugin errors")
    finally:
        g.close()
    check(settings.read_text() == settings_before, "settings.cfg unchanged after the exit")
    print(f"photo_foundation_smoke: {'FAIL' if failures else 'PASS'} ({len(failures)} failure(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
