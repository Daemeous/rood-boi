#!/usr/bin/env python3
"""The shipped photo mode, pc/plugins/rood.photo (pc/docs/Extensions.md "Photo
mode"), on a renderer (run.py runs it with --renderer gl and vulkan):

    python3 pc/tests/photo_mode_smoke.py [--card ...] [--out DIR]

One game from the save_room checkpoint, the shipped plugins as they ship (no
user plugins), a private settings.cfg, plugin data and photos folder. Driven
by the photo mode's own input (RPC system keys and the virtual pad: the real
input path):
  - not in a menu: the button in the field menu shows a notice, nothing held;
  - F12 (bind_key_photo) enters: held at once (the vsync and the state digest
    kept to the end), the HUD, the game's 2D and the overlay hidden, the input
    and the camera the plugin's, the post values unchanged;
  - each row of the card by the keyboard's arrows (the d-pad's keys): Format
    (the format), Filter (a grade's post values), Vignette, Grain,
    Aberration, Exposure (sliders: a step each), Aperture (depth of field on,
    the f-stop), Focus (dof_focus), each read back live through rood.post;
    Square resets a row;
  - the camera: a held key moves the pivot (rood.camera.get), the picture
    changes; Select puts it back;
  - Circle (Z) takes a photo: a PNG in the private photos folder, the
    "Saved" notice;
  - Cross (X) leaves: unpaused, nothing held (camera, hidden layers, format,
    input, post overrides), the post values as before, the state digest the
    held one, and the picture 6 vsyncs on the same as the game draws without
    the session (the camera back);
  - the pad's photo button (misc1, bind_pad_photo) enters, F12 leaves; Start
    (the foundation's safety) leaves too;
  - settings.cfg unchanged; no plugin errors in the log.
Deterministic: every wait is on a condition (the paused loop redraws in real
time); nothing depends on how long a wait took."""
import argparse
import os
import pathlib
import shutil
import struct
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT, Deadline  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402
from photo_foundation_smoke import ppm, difference  # noqa: E402

DIGEST = ("ram", "scratch", "io", "vram", "overlays", "vsync")
PLUGIN = "rood.photo"


def png_size(path):
    data = pathlib.Path(path).read_bytes()
    return struct.unpack(">II", data[16:24]) if data[:8] == b"\x89PNG\r\n\x1a\n" else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="logs, the plugin data, the photos")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if os.environ.get("ROOD_RENDERER") == "null":
        print("SKIP: the photo mode needs a renderer")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "photo_mode"
    for d in ("plugins", "plugin_data", "photos"):
        shutil.rmtree(out / d, ignore_errors=True)
    (out / "plugins").mkdir(parents=True, exist_ok=True)
    with card_guard(out / "cards", source=args.card) as cards:
        return run(out, cards.card1)


def run(out, card):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    photos = out / "photos"
    settings = out / "settings.cfg"
    settings.write_text("aspect 4:3\nui_menus 0\n")
    settings_before = settings.read_text()
    env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(out / "plugins"),
           "ROOD_PLUGIN_DATA": str(out / "plugin_data"), "ROOD_PHOTOS": str(photos), "ROOD_GI": "0",
           "ROOD_PAUSED_REDRAW_HZ": "60", "ROOD_DETERMINISTIC": "1"}
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        def value(code):
            r = g.repl("eval", session="photo-mode", plugin=PLUGIN, code=code)
            if not r.get("ok"):
                raise rood_rpc.RpcError(f"{code}: {r.get('error')}")
            return r["results"][0] if r.get("results") else None

        def photo():
            return g.call("photo")

        def until(what, test, seconds=30):
            deadline = Deadline(seconds)
            while True:
                result = test()
                if result:
                    return check(True, what)
                if deadline.passed():
                    return check(False, f"{what} (timed out)")
                time.sleep(0.01)

        def press(key, hold_presents=2):
            """A key down and up through SDL (the real path), each seen by a presented frame."""
            g.call("system", key=key, down=True)
            g.wait_presents(hold_presents)
            g.call("system", key=key, down=False)
            g.wait_presents(2)

        def pad(element):
            g.call("system", virtual_pad=element, down=True)
            g.wait_presents(3)
            g.call("system", virtual_pad=element, down=False)
            g.wait_presents(3)

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

        def nothing_held(p):
            return (p["phase"] == "running" and not p["owners"] and p["camera"] is None and p["hidden"] == 0
                    and p["format"] == 0 and p["input"] == "")

        g.run(4)
        info = g.lua_plugins().get(PLUGIN, {})
        check(info.get("state") == "loaded", f"rood.photo loaded ({info.get('state')}, {info.get('error')})")
        check(value("rood.camera ~= nil and rood.post ~= nil and rood.input ~= nil and rood.capture ~= nil") is True,
              "a shipped plugin has the capabilities it declares")
        pages = [p["id"] for p in g.call("lua", action="eval", code="return rood.menu.pages()")["results"][0]]
        check("rood.photo/photo" in pages, "the Photo mode page in the Option screens")
        rows = value("local out = {} for _, r in ipairs(rood.menu.rows('rood.photo/photo')) do "
                     "out[#out + 1] = r.id end return out")
        check("bind_photo" in rows and "plugin:rood.photo/scale" in rows, f"its rows: the binding, the settings ({rows})")
        keys = ["format", "preset", "saturation", "vignette", "grain", "chromatic_aberration", "exposure", "dof",
                "dof_fstop", "dof_focus"]
        post_before = {k: value(f"rood.post.get('{k}')") for k in keys}
        print("post before:", post_before)

        # ---- refused outside the field and battles ----
        check(value("rood.game.mode()") == "field", "in the field (the save room)")
        check(value("select(2, M.allowed('menu'))") == "Photo mode: not in a menu"
              and value("select(2, M.allowed('cutscene'))") == "Photo mode: not during a scene"
              and value("select(2, M.allowed('none'))") == "Photo mode: not while loading"
              and value("M.allowed('battle')") is True, "M.allowed: the field and battles only, a reason otherwise")
        g.call("system", virtual_pad="north", down=True)  # Triangle: the field menu
        g.run(4)
        g.call("system", virtual_pad="north", down=False)
        menu = False
        for _ in range(60):
            g.run(4)
            if value("rood.game.mode()") == "menu":
                menu = True
                break
        check(menu, "Triangle opens the menu (rood.game.mode() menu)")
        if menu:
            press("f12")
            until("in the menu: a notice, nothing held",
                  lambda: value("M.state.notice and M.state.notice.text") == "Photo mode: not in a menu"
                  and nothing_held(photo()) and value("M.state.active") is False)
            for _ in range(8):  # Cross closes the menu (once it takes input)
                g.call("system", virtual_pad="south", down=True)
                g.run(4)
                g.call("system", virtual_pad="south", down=False)
                for _ in range(10):
                    g.run(4)
                if value("rood.game.mode()") == "field":
                    break
            check(value("rood.game.mode()") == "field", "back in the field")
            g.run(30)

        # ---- the reference: the same vsyncs without the session ----
        before_session = digest()
        g.mark()
        g.run(6)
        image_without = shot("without-session")
        g.rewind()
        check(digest() == before_session, "(rewound for the session)")
        v0 = g.status()["vsync"]

        # ---- F12 enters ----
        press("f12")
        until("F12: the photo mode open (held, the camera, hidden layers, the input)",
              lambda: (lambda p: p["phase"] in ("held", "rendering") and p["owners"] == [PLUGIN]
                       and p["camera"] and p["camera"]["owner"] == PLUGIN and p["hidden"] == 7
                       and p["input"] == PLUGIN)(photo()))
        held = digest()
        check(held["vsync"] == v0, f"held at the vsync it was asked at ({held['vsync']} == {v0})")
        check(value("M.state.active and M.state.row == 1") is True, "the card on its first row")
        check({k: value(f"rood.post.get('{k}')") for k in keys} == post_before, "entering changes no post value")

        # ---- each row ----
        def row_is(n):
            until(f"row {n}", lambda: value("M.state.row") == n)

        def right():
            press("right")

        right()
        until("Format: 4:3 (the format, a crop)", lambda: abs(value("rood.post.get('format')") - 4 / 3) < 1e-3)
        press("down"), row_is(2)
        right()
        until("Filter: Noir (its grade's post values)",
              lambda: value("M.choices(M.rows[2])[M.state.value.filter].label") == "Noir"
              and value("rood.post.get('saturation')")[3] == 0)
        press("down"), row_is(3)
        v_before = value("M.state.value.vignette")
        right()
        until("Vignette: a step up, live", lambda: abs(value("rood.post.get('vignette')") - (v_before + 0.05)) < 1e-3)
        press("down"), row_is(4)
        right()
        until("Grain: a step up, live", lambda: abs(value("rood.post.get('grain')") - (post_before["grain"] + 0.05)) < 1e-3)
        press("down"), row_is(5)
        right()
        until("Aberration: a step up, live",
              lambda: abs(value("rood.post.get('chromatic_aberration')") - (post_before["chromatic_aberration"] + 0.1)) < 1e-3)
        press("down"), row_is(6)
        press("left")
        until("Exposure: a step down, live", lambda: abs(value("rood.post.get('exposure')") - (post_before["exposure"] - 0.25)) < 1e-3)
        press("a")  # Square resets the row
        until("Square resets Exposure", lambda: abs(value("rood.post.get('exposure')") - post_before["exposure"]) < 1e-3
              and not value("rood.post.overrides().exposure"))
        press("left")
        press("down"), row_is(7)
        start_aperture = value("M.state.value.aperture")
        right()
        until("Aperture: depth of field on at the next f-stop",
              lambda: value("rood.post.get('dof')") is True
              and abs(value("rood.post.get('dof_fstop')") - value(f"M.apertures[{start_aperture % 10 + 1}]")) < 1e-3)
        press("down"), row_is(8)
        right()
        until("Focus: the screen centre", lambda: value("rood.post.get('dof_focus')") == "centre")
        press("down"), row_is(1)  # wraps

        # ---- the camera ----
        pose0 = value("rood.camera.get()")
        image_a = shot("before-move")
        g.call("system", key="i", down=True)  # forward (M.camera.keys)
        until("a held key moves the camera", lambda: abs(value("rood.camera.get().target.z") - pose0["target"]["z"]) > 50
              or abs(value("rood.camera.get().target.x") - pose0["target"]["x"]) > 50)
        g.call("system", key="i", down=False)
        g.call("system", key="h", down=True)  # yaw
        until("a held key turns it", lambda: abs((value("rood.camera.get().yaw") - pose0["yaw"] + 180) % 360 - 180) > 5)
        g.call("system", key="h", down=False)
        g.wait_presents(6)
        until("the picture follows", lambda: difference(image_a, shot("after-move")) > 4)
        press("tab")  # Select: the camera back where it began
        until("Select: the camera back", lambda: abs(value("rood.camera.get().target.x") - pose0["target"]["x"]) < 1
              and abs(value("rood.camera.get().yaw") - pose0["yaw"]) < 0.5)

        # ---- a photo ----
        press("z")
        until("Circle: a photo written (the Saved notice)",
              lambda: (value("M.state.notice and M.state.notice.text") or "").startswith("Saved rood-boi-"))
        files = sorted(photos.glob("rood-boi-*.png"))
        size = png_size(files[0]) if files else None
        check(len(files) == 1 and size and size[0] == 960 and abs(size[1] - 720) <= 1,
              f"one PNG in the photos folder, cropped to 4:3 in a 4:3 window ({[f.name for f in files]}, {size})")
        check(digest() == held, "the renders and the photo left the state as held")

        # ---- Cross leaves ----
        press("x")
        until("Cross: everything given back", lambda: nothing_held(photo()) and value("M.state.active") is False)
        check(value("next(rood.post.overrides()) == nil") is True, "no post overrides left")
        check({k: value(f"rood.post.get('{k}')") for k in keys} == post_before, "the post values as before")
        check(digest() == held, "the game's state the held one (the same vsync and digest)")
        until("the Saved notice gone (real time: it is over the picture, never in a photo)",
              lambda: value("M.state.notice == nil") is True)
        g.run(6)
        d_after = difference(image_without, shot("after-session"))
        check(d_after < 1.0, f"6 vsyncs on: the picture the game draws without the session (the camera back: {d_after:.2f})")

        # ---- the pad's button, F12 again, Start ----
        pad("misc1")
        until("the pad's photo button (misc1) enters", lambda: value("M.state.active") is True and photo()["input"] == PLUGIN)
        press("f12")
        until("F12 leaves", lambda: nothing_held(photo()) and value("M.state.active") is False)
        press("f12")
        until("F12 enters again", lambda: value("M.state.active") is True)
        pad("start")
        until("Start (the safety) leaves", lambda: nothing_held(photo()) and value("M.state.active") is False)
        g.run(30)
        check(value("next(rood.post.overrides()) == nil") is True and
              {k: value(f"rood.post.get('{k}')") for k in keys} == post_before, "and the post values as before")

        check(settings.read_text() == settings_before, "settings.cfg unchanged")
        log = (out / "game.log").read_text(errors="replace")
        errors = [line for line in log.splitlines() if "[lua]" in line and PLUGIN in line
                  and ("error" in line or "warn" in line.lower())]
        check(not errors, f"no plugin errors or warnings ({errors[:3]})")
    finally:
        g.close()
    check(settings.read_text() == settings_before, "settings.cfg unchanged after the exit")
    print(f"photo_mode_smoke: {'FAIL' if failures else 'PASS'} ({len(failures)} failure(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
