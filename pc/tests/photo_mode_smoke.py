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
  - the tabbed card: R1 / L1 (W / Q) and E change the tab; rows by the
    keyboard's arrows (the d-pad's keys): Lens (depth of field, focus, a
    dimmed row, the f-stops), Effects (vignette, grain, aberration), Colour
    (a filter and its strength, exposure; Square resets a row), Frame
    (format, bars colour, guides), Presets (save, Select resets everything,
    load), Camera (field of view; R resets the tab), each read back live;
  - the camera: a held key moves the pivot (rood.camera.get), the picture
    changes; Select puts it back;
  - Circle (Z) takes a photo: a PNG in the private photos folder, the
    "Saved" notice;
  - Cross (X) leaves: unpaused, nothing held (camera, hidden layers, format,
    input, post overrides), the post values as before, the state digest the
    held one, and the picture 6 vsyncs on the same as the game draws without
    the session (the camera back);
  - D-pad up alone does nothing; the pad's chord (Back held + D-pad up,
    bind_pad_photo back+dpup) enters, F12 leaves; Start (the foundation's
    safety) leaves too;
  - Music keep: the sound driver runs while held (RPC photo audio_vsyncs),
    the vsync stays; the exact digest checks run with Music stop;
  - a host call made slow (RPC lua budget slow_call) never counts against the
    plugin's budget: no faults, no kills;
  - disabled by faults: F12 brings it back at once, open, with a notice; the
    cool-down brings it back by itself;
  - settings.cfg unchanged; no plugin errors in the log (before the faults).
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
        status = run(out, cards.card1)
    return run_bubbles(out) or status


def run_bubbles(out):
    """rood.ui.hide{game2d}: the 2D anchored in the world (the emote bubbles over actors, the battle page's
    status words) goes too, not only the HUD's slots: the opening's actors with every emote forced on
    (as ui_text_smoke), drawn again by ui_bubbles vector / ui_text font (their counters count each)."""
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    settings = out / "settings-bubbles.cfg"
    settings.write_text("aspect 4:3\nui_menus 0\n")
    env = {"ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(out / "plugins"),
           "ROOD_PLUGIN_DATA": str(out / "plugin_data"), "ROOD_GI": "0"}
    g = launch_at("first_bubble", fast=True, log=out / "game-bubbles.log", extra_env=env)
    try:
        g.call("graphics", interpolate=False, ui_text="font", ui_bubbles="vector")
        g.run(200)
        sym = rood_rpc.symbols()
        g.write(sym["D_800F2270"], bytes(4))  # (the scene keeps them off)
        g.write(sym["D_800F2260"], bytes([0] + [1] * 15))  # a balloon with dots over each actor

        def emotes():
            e = g.call("ui_bubbles")["emotes"]
            return e["balloons"] + e["dots"] + e["marks"]

        def drawn(vsyncs=8):
            before = emotes()
            g.run(vsyncs)
            g.wait_presents(2)
            return emotes() - before

        def hide(code):
            r = g.repl("eval", session="photo-bubbles", plugin=PLUGIN, code=code)
            return r.get("ok") and (r.get("results") or [None])[0]

        shown = drawn()
        check(shown > 0, f"the emote bubbles drawn over the actors ({shown})")
        check(hide("return rood.ui.hide { game2d = true }") is True, "rood.ui.hide{game2d}")
        # (each image is drawn again once and kept: kinds not drawn yet show whether they are drawn)
        def every_kind():
            total = 0
            for kind in range(2, 8):
                g.write(sym["D_800F2260"], bytes([0] + [kind] * 15))
                total += drawn()
            return total

        g.run(2)
        hidden = every_kind()
        check(hidden == 0, f"hidden: no emote bubble drawn, of any kind ({hidden})")
        hide("rood.ui.show() return true")
        g.run(2)
        again = every_kind()
        check(again > 0, f"shown again: drawn ({again})")
        g.write(sym["D_800F2260"], bytes(16))
    finally:
        g.close()
    log = (out / "game-bubbles.log").read_text(errors="replace")
    check("[crash]" not in log, "no crash")
    print(f"photo_mode_smoke (bubbles): {'FAIL' if failures else 'PASS'} ({len(failures)} failure(s))")
    return 1 if failures else 0


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

        # Music "stop" for the exact checks below (the state digest held to the byte); "keep" is checked after.
        value("rood.settings.set('plugin:rood.photo/music', 'stop')")

        # ---- the reference: the same vsyncs without the session ----
        before_session = digest()
        g.mark()
        g.run(6)
        image_without = shot("without-session")
        g.rewind()
        check(digest() == before_session, "(rewound for the session)")
        v0 = g.status()["vsync"]

        check(g.call("occlusion")["subjects"] != [], "occlusion has subjects in play (Ashley)")
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

        # ---- the tabs and their rows ----
        def at(tab, row):
            until(f"tab {tab}, row {row}", lambda: value("M.state.tab") == tab and value("M.state.row") == row)

        def goto_row(n):
            while value("M.state.row") != n:
                press("down")

        def close(a, b, eps=1e-3):
            return a is not None and b is not None and abs(a - b) < eps

        press("w")  # R1: the next tab
        at(2, 1)
        press("q")  # L1: back
        at(1, 1)
        press("e")  # E: the next tab (the keyboard's own)
        at(2, 1)
        # Lens: depth of field on, the focus, a dimmed row, the aperture's stops
        press("right")
        until("Lens > Depth of field: on", lambda: value("rood.post.get('dof')") is True)
        press("down"), at(2, 2)
        focus0 = value("rood.post.get('dof_focus')")
        press("right")
        until("Lens > Focus: the next mode", lambda: value("rood.post.get('dof_focus')") not in (None, focus0))
        if value("rood.post.get('dof_focus')") != "manual":
            press("down"), at(2, 3)
            depth0 = value("rood.post.get('dof_focus_depth')")
            press("right")
            g.wait_presents(3)
            check(value("rood.post.get('dof_focus_depth')") == depth0 and value("M.dimmed(M.find('focus_distance'))"),
                  "Lens > Focus distance: dimmed unless the focus is manual (left / right do nothing)")
        goto_row(4)
        if value("rood.post.get('dof_model')") != "physical":
            press("right")
            until("Lens > Model: physical", lambda: value("rood.post.get('dof_model')") == "physical")
        check(value("M.dimmed(M.find('aperture'))") is True, "Blur amount (the artistic model's) dimmed")
        goto_row(5)
        stop0 = value("rood.post.get('dof_fstop')")
        press("right")
        until("Lens > Aperture: the next f-stop",
              lambda: (lambda f: f is not None and f > stop0 and f in [1.2, 1.4, 2, 2.8, 4, 5.6, 8, 11, 16, 22])(
                  value("rood.post.get('dof_fstop')")))
        check(value("M.value_text(M.find('fstop'))").startswith("f/"), "the aperture shown as an f-number")
        # Effects: vignette, grain
        press("w"), at(3, 1)
        press("right")
        until("Effects > Vignette: a step up, live", lambda: close(value("rood.post.get('vignette')"), post_before["vignette"] + 0.05))
        check(value("M.value_text(M.find('vignette'))") == "5%", "shown in per cent")
        goto_row(5)
        press("right")
        until("Effects > Grain: a step up, live", lambda: close(value("rood.post.get('grain')"), post_before["grain"] + 0.05))
        goto_row(4)
        press("right")
        until("Effects > Lens distortion: barrel, live", lambda: close(value("rood.post.get('lens_distortion')"), 0.05))
        goto_row(8)
        press("right")
        until("Effects > Aberration: a step up, live",
              lambda: close(value("rood.post.get('chromatic_aberration')"), post_before["chromatic_aberration"] + 0.1))
        # Colour: a filter blended by its strength, exposure, reset of a row
        press("w"), at(4, 1)
        press("right")
        until("Colour > Filter: Noir (its grade's post values)",
              lambda: value("M.choices(M.find('filter'))[M.state.value.filter].label") == "Noir"
              and value("rood.post.get('saturation')")[3] == 0)
        press("down"), at(4, 2)
        press("left")
        until("Colour > Filter strength 95%: the grade blended in",
              lambda: close(value("rood.post.get('saturation')")[3], post_before["saturation"][3] * 0.05, 1e-2))
        press("down"), at(4, 3)
        press("left")
        until("Colour > Exposure: a step down, live",
              lambda: close(value("rood.post.get('exposure')"), post_before["exposure"] - 0.25))
        check(value("M.value_text(M.find('exposure'))") == "-0.25 EV", "shown in EV")
        press("a")  # Square resets the row
        until("Square resets Exposure", lambda: close(value("rood.post.get('exposure')"), post_before["exposure"])
              and not value("rood.post.overrides().exposure"))
        # Frame: the format, its bars, the guides
        press("w"), at(5, 1)
        press("right")
        until("Frame > Format: 4:3 (the format, a crop)", lambda: close(value("rood.post.get('format')"), 4 / 3))
        press("down")
        press("right")
        until("Frame > Bars: white", lambda: value("rood.post.get('format_color')") == 0xFFFFFF)
        press("left")
        until("Frame > Bars: black again", lambda: value("rood.post.get('format_color')") == 0)
        press("down")
        press("right")
        until("Frame > Guides: thirds", lambda: value("M.state.own.guides") == 2)
        # Presets: save, everything reset (Select), load
        press("w"), at(6, 1)
        press("left")
        until("Presets: slot 1 saved (plugin storage)",
              lambda: value("rood.storage.get('presets') and rood.storage.get('presets')[1] ~= nil") is True)
        press("tab")  # Select: everything reset
        until("Select: everything reset", lambda: value("next(rood.post.overrides()) == nil") is True
              and close(value("rood.post.get('vignette')"), post_before["vignette"]))
        press("right")
        until("Presets: slot 1 loaded (format, vignette, the filter, the aperture)",
              lambda: close(value("rood.post.get('format')"), 4 / 3)
              and close(value("rood.post.get('vignette')"), post_before["vignette"] + 0.05)
              and value("rood.post.get('saturation')")[3] < 0.2 and value("rood.post.get('dof')") is True)
        # Camera: the field of view, R (the tab's reset)
        press("w"), at(1, 1)
        goto_row(2)
        fov0 = value("rood.camera.get().fov")
        press("right")
        until("Camera > Field of view: a step wider (on the degree grid)",
              lambda: 0.4 < value("rood.camera.get().fov") - fov0 < 1.6)
        check("mm" in value("M.value_text(M.find('fov'))"), "shown with its focal length")
        press("r")  # R: the tab's reset
        until("R resets the Camera tab", lambda: close(value("rood.camera.get().fov"), fov0, 0.05))

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

        # ---- the left stick is the camera's, never the card's (only the D-pad moves rows and values) ----
        press("w"), press("w"), press("w"), at(4, 1)  # Colour > Filter: a row left / right would change
        filter0, sat0 = value("M.state.value.filter"), value("rood.post.get('saturation')")
        pose1 = value("rood.camera.get()")
        for element in ("lefty", "leftx"):
            g.call("system", virtual_pad=element, down=True)
            g.wait_presents(20)
            g.call("system", virtual_pad=element, down=False)
            g.wait_presents(3)
        check(value("M.state.tab") == 4 and value("M.state.row") == 1, "the left stick moves no row")
        check(value("M.state.value.filter") == filter0 and value("rood.post.get('saturation')") == sat0,
              "the left stick changes no value")
        moved = value("rood.camera.get()")
        check(abs(moved["target"]["x"] - pose1["target"]["x"]) + abs(moved["target"]["z"] - pose1["target"]["z"]) > 1
              or abs(moved["fov"] - pose1["fov"]) > 1e-3
              or abs(moved["position"]["x"] - pose1["position"]["x"]) > 1, "the left stick drives the camera")

        # ---- the card hidden (Triangle): only the camera, Triangle, a photo and leaving act ----
        press("s")  # Triangle
        until("Triangle hides the card", lambda: value("M.state.card") is False)
        for key in ("right", "down", "left", "w", "q", "a", "e", "r"):
            press(key)
        check(value("M.state.tab") == 4 and value("M.state.row") == 1 and value("M.state.value.filter") == filter0
              and value("rood.post.get('saturation')") == sat0,
              "hidden: the D-pad, L1 / R1, Square and the tab keys change nothing")
        press("s")
        until("Triangle shows it again", lambda: value("M.state.card") is True)
        press("right")
        until("shown: the D-pad changes the row's value again", lambda: value("M.state.value.filter") != filter0)
        press("tab")  # Select: everything reset
        until("(reset)", lambda: value("next(rood.post.overrides()) == nil") is True)
        press("w"), press("w"), at(6, 1)
        press("w"), at(1, 1)

        # ---- occlusion (see-through walls) never applies to the frozen moment's renders ----
        occ = g.call("occlusion")
        check(occ["occlusion"] == "fade" and occ["subjects"] == [],
              f"occlusion suspended while the photo camera flies (the setting kept: {occ['occlusion']}, "
              f"subjects {len(occ['subjects'])})")

        # ---- Show Ashley / Show others (rood.ui.hide{actors}): the characters left out of the renders ----
        goto_row(6)
        g.wait_presents(4)
        with_her = shot("with-ashley")
        press("right")
        until("Camera > Show Ashley: off (rood.ui.hidden().actors ashley)",
              lambda: value("rood.ui.hidden().actors") == "ashley" and photo()["hidden"] == 7 | 8)
        g.wait_presents(4)
        d_hidden = difference(with_her, shot("without-ashley"))
        check(d_hidden > 0.3, f"the picture without her ({d_hidden:.2f})")
        press("right")
        until("on again: nothing left out", lambda: value("rood.ui.hidden().actors") is None)
        g.wait_presents(12)  # (Vulkan: the lighting's history settles)
        d_back = difference(with_her, shot("with-ashley-again"))
        check(d_back < d_hidden / 2, f"and the picture with her again ({d_back:.2f} against {d_hidden:.2f})")
        press("down"), press("right")
        until("Camera > Show others: off (actors others)", lambda: value("rood.ui.hidden().actors") == "others")
        press("a")  # Square: the row back to its default (shown)
        until("Square: Show others on again", lambda: value("rood.ui.hidden().actors") is None)
        press("w"), press("q"), at(1, 1)

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
        occ = g.call("occlusion")
        check(occ["occlusion"] == "fade" and occ["subjects"] != [], "occlusion back after leaving (its subjects)")
        d_after = difference(image_without, shot("after-session"))
        check(d_after < 1.0, f"6 vsyncs on: the picture the game draws without the session (the camera back: {d_after:.2f})")

        # ---- the pad's button, F12 again, Start ----
        g.call("system", virtual_pad="dpup", down=True)  # D-pad up alone: the game's, no photo mode
        g.wait_presents(3)
        g.call("system", virtual_pad="dpup", down=False)
        g.wait_presents(6)
        check(value("M.state.active") is False, "D-pad up alone does not open it")
        g.call("system", virtual_pad="back", down=True)  # the chord: Select (Back) held, then D-pad up
        g.wait_presents(2)
        g.call("system", virtual_pad="dpup", down=True)
        g.wait_presents(3)
        g.call("system", virtual_pad="dpup", down=False)
        g.call("system", virtual_pad="back", down=False)
        g.wait_presents(3)
        until("the pad's chord (bind_pad_photo back+dpup) enters",
              lambda: value("M.state.active") is True and photo()["input"] == PLUGIN)
        press("f12")
        until("F12 leaves", lambda: nothing_held(photo()) and value("M.state.active") is False)
        press("f12")
        until("F12 enters again", lambda: value("M.state.active") is True)
        pad("start")
        until("Start (the safety) leaves", lambda: nothing_held(photo()) and value("M.state.active") is False)
        g.run(30)
        check(value("next(rood.post.overrides()) == nil") is True and
              {k: value(f"rood.post.get('{k}')") for k in keys} == post_before, "and the post values as before")

        # ---- Music keep (the default): the sound goes on while the game is held ----
        value("rood.settings.set('plugin:rood.photo/music', 'keep')")
        held_keep = digest()
        press("f12")
        until("Music keep: the sound driver runs in real time while held",
              lambda: photo()["audio"] == 2 and photo()["audio_vsyncs"] > 30)
        g.call("system", key="h", down=True)  # renders meanwhile: each returns to the moment with the sound on
        g.wait_presents(20)
        g.call("system", key="h", down=False)
        kept = digest()
        check(kept["vsync"] == held_keep["vsync"] and all(kept[k] == held_keep[k] for k in ("scratch", "overlays")),
              "the game itself still held (the vsync, the overlays; only the sound's RAM moves)")
        press("f12")
        until("and it leaves", lambda: nothing_held(photo()) and value("M.state.active") is False)
        g.run(30)
        check(g.status()["vsync"] == held_keep["vsync"] + 30, "the game goes on from the moment")

        # ---- a slow host call is the host's: never the plugin's budget ----
        g.call("lua", action="budget", reset_stats=True, slow_call={"name": "state", "ms": 6, "plugin": PLUGIN})
        press("f12")
        until("entered with rood.input.state 6 ms slow on the host's side (every frame)", lambda: value("M.state.active") is True)
        g.call("system", key="h", down=True)
        g.wait_presents(60)
        g.call("system", key="h", down=False)
        g.wait_presents(4)
        info = g.lua_plugins()[PLUGIN]
        check(info.get("state") == "loaded" and info.get("faults") == 0 and info.get("kills") == 0,
              f"slow host calls: no faults, no kills ({info.get('state')}, {info.get('faults')}, {info.get('kills')})")
        g.call("lua", action="budget", slow_call={"name": "", "ms": 0})
        press("f12")
        until("and it leaves", lambda: nothing_held(photo()) and value("M.state.active") is False)

        check(settings.read_text() == settings_before, "settings.cfg unchanged")
        log = (out / "game.log").read_text(errors="replace")
        errors = [line for line in log.splitlines() if "[lua]" in line and PLUGIN in line
                  and ("error" in line or "warn" in line.lower())]
        check(not errors, f"no plugin errors or warnings ({errors[:3]})")

        # ---- disabled by faults: the next press brings it back; so does the cool-down ----
        def state():
            return g.lua_plugins().get(PLUGIN, {}).get("state")

        def overrun():
            # its own Lua made slow: host work counted (host_budget_ms 0), every draw's fit 2 ms
            g.call("lua", action="budget", host_budget_ms=0, slow_call={"name": "fit", "ms": 2, "plugin": PLUGIN})
            press("f12")
            until("over its budget every frame: disabled by the fault policy", lambda: state() == "disabled", 60)
            g.call("lua", action="budget", host_budget_ms=100, slow_call={"name": "", "ms": 0})
            until("everything given back by the disable", lambda: nothing_held(photo()))

        overrun()
        g.wait_presents(10)
        check(state() == "disabled", "it stays disabled until asked for (cool-down 20 s)")
        press("f12")
        until("F12: re-enabled and open at once, with a notice",
              lambda: state() == "loaded" and value("M.state.active") is True
              and value("M.state.notice and M.state.notice.text") == value("M.look.recovered_notice"))
        press("f12")
        until("F12 leaves", lambda: nothing_held(photo()) and value("M.state.active") is False)
        g.call("lua", action="budget", fault_cooldown_s=1)
        overrun()
        until("the cool-down (1 s) re-enables it by itself", lambda: state() == "loaded", 30)
        check(value("M.state.active") is False and nothing_held(photo()), "closed after a cool-down revival")
        g.call("lua", action="budget", fault_cooldown_s=20)
    finally:
        g.close()
    check(settings.read_text() == settings_before, "settings.cfg unchanged after the exit")
    print(f"photo_mode_smoke: {'FAIL' if failures else 'PASS'} ({len(failures)} failure(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
