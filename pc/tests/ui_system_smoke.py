#!/usr/bin/env python3
"""Modern menus, System screen (pc/src/ui/ui_settings.cpp, ui_shell.cpp,
ui_system_draw.cpp) driven through the shell: pad input (RPC press) and a
few pointer actions (RPC ui_menu pointer, layout units, the real hit tests).

    python3 pc/tests/ui_system_smoke.py [--card pc/save/card1.mcd] [--out DIR]

From a copy of the card (skipped without one), ROOD_SETTINGS on a fresh file,
in the save room with ui_menus on; Triangle, L1 (System). One row per page:
  - Graphics: Left on Lighting quality steps the preset to Low (live,
    saved; Low: nothing ROOD_GI=0 turns off at the relaunch);
    a pointer drag on Internal resolution shows the number and changes the
    scale only on release (Right/Left put it back at once);
    Right on Anti-aliasing filter to FXAA (graphics aa, saved aa);
    Downsample filter while supersampling is off: greyed with a reason,
    Right does nothing (value, dirty)
  - debounce: right after a change the page is dirty; the save (the save
    counter) follows within about 1 s of real time; Cross saves at once
  - Audio: Master volume Left/Right steps of 5 (the system RPC's volume and
    gain, audio recorded to a WAV), a pointer drag on the bar to the middle
    (50); Sound Mono sets vs_main_settings.monoSound and is not written to
    settings.cfg
  - Gameplay: a click on ON of Weapon quick slots (pc_qol_weapon_slots,
    qol_weapon_slots 1 saved); Map Right = Simple (vs_main_settings.simpleMap 1)
  - Interface: Prompt style greyed while Button prompts are the original
    glyphs (the first run's pick here); Right on Button prompts, then Right
    on Prompt style steps it (pc_button_prompts_style, saved); Menu opacity
    (ui_menu_opacity, default 0.94: the menus' look before the setting):
    Left steps it to 0.92 (pc_ui_menu_opacity, saved); a pointer drag to the
    bar's first fifth sets about 0.2: the backdrop left of the rail goes from the
    ground colour to the world showing through (measured against the
    picture at 0.94 and the ground); the Ashley tab with its model is
    see-through too (the model column's empty left strip off the ground
    colour: the presenter's backdrop; before, an opaque cut-out).
    Menu tint (ui_menu_tint, measured at opacity 1, dragged past the bar's
    end): Lea Monde by default (the theme's ground, a swatch, sliders
    8/7/7); Right steps Classic, Slate, Crimson, Verdant (pc_ui_menu_tint,
    the sliders, the RPC theme's ground; Classic and Crimson measured in the
    left margin); Right on Blue makes it Custom (3, 10, 7) and a pointer drag
    sets Red to about 15; Left / Right on the tint row: Verdant, then Custom
    with its colour back; saved ui_menu_tint custom / ui_menu_tint_custom;
    the Ashley tab's model column shows the tinted ground
  - Controls: Turn speed Right adds 15 deg/s (camera_stick RPC, saved);
    Reset camera (after a stick turn): the camera resets once the menu closes
  - Bindings: a capture blocks navigation (Triangle does not close the menu,
    Down does not move); Escape cancels it; a pushed gamepad trigger binds
    Circle; Reset to defaults puts both back; Circle on Triangle + a pushed
    "m" binds it (bind_key_triangle m saved); in the field "m" opens the menu
  - Display: Widescreen On: pc_gl_view is 16:9 (its outermost 32 px column
    the tinted ground), widescreen 1 saved; Window
    Right = Borderless (system RPC), "Keep these settings?" asks and the
    file keeps Windowed until Circle keeps it (saved)
  - Game group: Data opens the game's own Data screen; Cross comes back to
    the System list on Data
Relaunch with the same file: the values are back (live variables and RPCs,
the menu tint Custom with its colour,
brightness still its default 50), and a save writes the same file.
--out keeps screenshots (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import struct
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402
from ui_ashley_smoke import GROUND  # noqa: E402  (the theme's ground colour)

CLOSED = 0x3F
SIMPLE_MAP, MONO_SOUND = 9, 10  # vs_main_settings_t (src/SLUS_010.40/main.h)
DATA_MENU = 7 | 0x40  # System > Data: command 6, the game's own screen

# Shell layout, 4:3 (960 x 720 layout units; ui_system_draw.cpp)
CARD_ROW_Y0, CARD_ROW_STEP = 183, 42  # the centre of the page's row i
SLIDER_X0, SLIDER_X1 = 632, 786  # the slider bar
TOGGLE_ON_X = 868
STYLES = ["Solid", "Outline", "Colour solid", "Colour outline"]  # Prompt style (ui_settings.cpp)
STYLE_NAMES = ["solid", "outline", "color_solid", "color_outline"]  # button_prompts_style (button_prompts.c)
# Menu tint presets after Lea Monde (ui_tint.cpp): name, 0..31 colour, the menus' ground (ui_theme.cpp tint_theme)
TINTS = [("Classic", (4, 8, 25), "#081134"), ("Slate", (7, 8, 10), "#0e1115"), ("Crimson", (14, 3, 4), "#1d0608"),
         ("Verdant", (3, 10, 6), "#06150c")]


def hex_rgb(text):
    return tuple(int(text[i:i + 2], 16) for i in (1, 3, 5))


def settings_lines(path):
    lines = {}
    if pathlib.Path(path).exists():
        for line in pathlib.Path(path).read_text().splitlines():
            if line and not line.startswith("#"):
                key, _, value = line.partition(" ")
                lines[key] = value.strip()
    return lines


class Timeout(Exception):
    pass


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_system"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    settings = out / "settings.cfg"
    settings.unlink(missing_ok=True)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SETTINGS": str(settings)}
    for name in ("ROOD_WIDESCREEN", "ROOD_PERSPECTIVE"):
        os.environ.pop(name, None)
    sym = symbols()
    menu_state = sym["vs_battle_menuState"]
    main_settings = sym["vs_main_settings"]
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    def read_int(game, name, count=1):
        return list(struct.unpack(f"<{count}i", game.read(sym[name], 4 * count)))

    def read_float(game, name):
        return struct.unpack("<f", game.read(sym[name], 4))[0]

    def view_aspect(game):
        x, y, w, h = read_int(game, "pc_gl_view", 4)
        return w / h if h else 0, (x, y, w, h)

    wav = out / "audio.wav"  # recorded: the master gain follows the output
    game = launch_at("save_room", card=card, fast=True, audio=f"wav:{wav}", log=out / "game.log", extra_env=env)

    def shell():
        return game.call("ui_menu")

    def sysv():
        return shell()["system"]

    def rows():
        return {r["id"]: r for r in sysv()["rows"]}

    def row_index(row_id):
        return [r["id"] for r in sysv()["rows"]].index(row_id)

    def press(button, settle=6):
        game.press(button, vsyncs=3, settle=settle)

    def wait(pred, what, limit=60, step=5):
        for _ in range(limit):
            if pred():
                return
            game.run(step)
        raise Timeout(what)

    def current_menu():
        return game.read(menu_state, 1)[0]

    def game_byte(offset):
        return game.read(main_settings + offset, 1)[0]

    def shot(name):
        if args.out:
            game.present_screenshot(out / f"{name}.png")

    def key(name, settle=6):
        game.call("system", key=name, down=True)
        game.run(3)
        game.call("system", key=name, down=False)
        game.run(settle)

    def ptr(x, y, **kw):
        game.call("ui_menu", pointer=dict(x=x, y=y, **kw))
        game.run(4)

    def goto_item(item):
        for _ in range(16):
            if shell()["row"] == item:
                return
            press("down", settle=4)
        raise Timeout(f"System list entry {item} ({shell()['row']})")

    def enter(page):
        goto_item(page)
        press("circle")
        s = sysv()
        if s["mode"] != "page" or s["page"] != page:
            raise Timeout(f"entering {page} ({s['mode']}, {s['page']})")

    def goto_row(row_id):
        for _ in range(40):
            if sysv()["row"] == row_id:
                return rows()[row_id]
            press("down", settle=2)
        raise Timeout(f"row {row_id} ({sysv()['row']})")

    def settle(polls=2000):
        """Let the page's scroll animation finish (RPC ui_menu at_rest): it
        runs on real time, not vsyncs, and a focus move stops it where it was
        (the pointer's row positions assume the page at its top: settle on
        the first row). The paused game redraws after every request, so
        polling alone lets it finish; `polls` only bounds a hang."""
        for _ in range(polls):
            s = game.call("ui_menu")
            if "at_rest" not in s:
                raise Timeout("RPC ui_menu has no at_rest (an older game?)")
            if s["at_rest"]:
                return
            time.sleep(0.01)
        raise Timeout(f"the System page still gliding after {polls} polls")

    def leave():
        press("cross")
        if sysv()["mode"] != "list":
            raise Timeout("Cross back to the System list")

    def wait_clean(limit=4.0):
        """Real seconds until the debounced save went out (the shell ticks
        on presented frames: keep the game running)."""
        t0 = time.time()
        while sysv()["dirty"]:
            if time.time() - t0 > limit:
                raise Timeout(f"the settings file is still dirty after {limit} s")
            game.run(4)
        return time.time() - t0

    try:
        defaults_title = game.call("system")
        state = game.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1
        # turn the camera off its 45-degree grid (Reset camera checks it later)
        game.call("input", right_stick=[1, 0], vsyncs=20)
        game.run(20)
        cam = game.call("camera_stick")
        resets_before = cam["resets"]

        game.call("ui_menu", modern=True)
        press("triangle", settle=40)
        press("l1", settle=10)
        s = shell()
        check(s["visible"] and s["tab"] == "system" and s["system"]["mode"] == "list",
              f"Triangle, L1: the System list ({s['tab']}, {s['system']['mode']})")
        shot("system_list")

        # ---- Graphics ----
        enter("graphics")
        r = goto_row("lighting_preset")
        before = r["value"]
        saves = sysv()["saves"]
        if before == "Low":
            press("right")
        for _ in range(6):
            press("left")
            if rows()["lighting_preset"]["value"] == "Low":
                break
        after = rows()["lighting_preset"]["value"]
        check(after == "Low" and r["enabled"] and read_int(game, "pc_lighting_preset")[0] == 1,
              f"Graphics: Left steps Lighting quality to Low ({before} -> {after})")
        check(sysv()["dirty"], "a change marks the page dirty")
        elapsed = wait_clean()
        # the debounce (1 s quiet) saved, by the save counter; real time only bounds it loosely (busy machines)
        check(sysv()["saves"] > saves and elapsed < 3.0,
              f"debounce: saved {elapsed:.2f} s after the change ({saves} -> {sysv()['saves']})")
        saved = settings_lines(settings)
        check(saved.get("lighting_preset") == after.lower(),
              f"saved lighting_preset {after.lower()} ({saved.get('lighting_preset')})")
        # Internal resolution: a pointer drag previews the number; the scale applies when the drag ends
        scale = read_int(game, "pc_hw_scale_setting")[0]
        y = CARD_ROW_Y0 + CARD_ROW_STEP * row_index("internal_resolution")
        ptr(SLIDER_X0 + 2, y, down=True)
        ptr(SLIDER_X0 + 30, y, down=True)
        shown = rows()["internal_resolution"]["value"]
        check(shown != f"{scale}x" and read_int(game, "pc_hw_scale_setting")[0] == scale,
              f"Internal resolution drag: {shown} shown, the scale stays {scale}x until the release")
        ptr(SLIDER_X0 + 30, y, down=False)
        game.run(4)
        now = read_int(game, "pc_hw_scale_setting")[0]
        check(f"{now}x" == shown and now != scale, f"released: the scale is {shown} ({now}x)")
        goto_row("internal_resolution")
        for _ in range(8):  # back with Right/Left (each applies at once)
            now = read_int(game, "pc_hw_scale_setting")[0]
            if now == scale:
                break
            press("right" if now < scale else "left")
        check(read_int(game, "pc_hw_scale_setting")[0] == scale, f"Right/Left: the scale back to {scale}x")
        aa_before = goto_row("aa")["value"]
        for _ in range(3):
            press("right")
            if rows()["aa"]["value"] == "FXAA":
                break
        r = rows()["aa"]
        g = game.call("graphics")
        check(r["value"] == "FXAA" and "fxaa" in g["aa"],
              f"Anti-aliasing filter: Right steps to FXAA ({aa_before} -> {r['value']}, aa {g['aa']})")
        r = goto_row("aa_downsample")
        check(not r["enabled"] and r.get("reason") == "Needs supersampling",
              f"Downsample filter greyed while supersampling is off ({r})")
        dirty = sysv()["dirty"]
        press("right")
        r2 = rows()["aa_downsample"]
        check(r2["value"] == r["value"] and sysv()["dirty"] == dirty and g["aa_downsample"]
              == game.call("graphics")["aa_downsample"], f"Right on the greyed row changes nothing ({r2['value']})")
        shot("graphics")
        leave()  # Cross saves at once
        saved = settings_lines(settings)
        check(not sysv()["dirty"] and "fxaa" in saved.get("aa", ""), f"Cross saved aa at once ({saved.get('aa')})")

        # ---- Audio ----
        enter("audio")
        r = goto_row("master_volume")
        check(r["enabled"] and r["value"] == "100%", f"Audio: Master volume 100% ({r['value']})")
        press("left")
        press("left")
        press("right")
        game.run(30)
        sy = game.call("system")
        check(rows()["master_volume"]["value"] == "95%" and sy["master_volume"] == 95,
              f"Left, Left, Right: 95 ({rows()['master_volume']['value']}, {sy['master_volume']})")
        check(abs(sy["gain"] - 0.95 ** 2) < 1e-4, f"gain 0.9025 ({sy['gain']})")
        y = CARD_ROW_Y0 + CARD_ROW_STEP * row_index("master_volume")
        ptr(SLIDER_X0 + 10, y, down=True)
        ptr((SLIDER_X0 + SLIDER_X1) / 2, y, down=True)
        ptr((SLIDER_X0 + SLIDER_X1) / 2, y, down=False)
        game.run(30)
        sy = game.call("system")
        check(sy["master_volume"] == 50 and rows()["master_volume"]["value"] == "50%"
              and abs(sy["gain"] - 0.25) < 1e-4,
              f"pointer drag to the bar's middle: 50 ({sy['master_volume']}, gain {sy['gain']})")
        r = goto_row("sound")
        check(r["value"] == "Stereo" and game_byte(MONO_SOUND) == 0, f"Sound: Stereo ({r['value']})")
        press("right")
        check(rows()["sound"]["value"] == "Mono" and game_byte(MONO_SOUND) == 1,
              f"Right: Mono, vs_main_settings.monoSound 1 ({game_byte(MONO_SOUND)})")
        shot("audio")
        leave()
        saved = settings_lines(settings)
        check(saved.get("master_volume") == "50", f"saved master_volume 50 ({saved.get('master_volume')})")
        check(not any("sound" in k or "mono" in k for k in saved), "Sound is not written to settings.cfg")

        # ---- Gameplay ----
        enter("gameplay")
        r = goto_row("qol_weapon_slots")
        check(r["value"] == "Off" and read_int(game, "pc_qol_weapon_slots")[0] == 0,
              f"Gameplay: Weapon quick slots off ({r['value']})")
        ptr(TOGGLE_ON_X, CARD_ROW_Y0 + CARD_ROW_STEP * row_index("qol_weapon_slots"), click=True)
        check(rows()["qol_weapon_slots"]["value"] == "On" and read_int(game, "pc_qol_weapon_slots")[0] == 1,
              "a click on ON: quick slots on (pc_qol_weapon_slots 1)")
        r = goto_row("map")
        check(r["value"] == "Frame" and game_byte(SIMPLE_MAP) == 3, f"Map: Frame ({r['value']}, {game_byte(SIMPLE_MAP)})")
        press("right")
        check(rows()["map"]["value"] == "Simple" and game_byte(SIMPLE_MAP) == 1,
              f"Right: Simple, vs_main_settings.simpleMap 1 ({game_byte(SIMPLE_MAP)})")
        shot("gameplay")
        leave()
        check(settings_lines(settings).get("qol_weapon_slots") == "1", "saved qol_weapon_slots 1")

        # ---- Interface ----
        enter("interface")
        r = goto_row("button_prompts")
        if r["value"] == "Original glyphs":
            r = rows()["button_prompts_style"]
            check(not r["enabled"] and "Original glyphs" in r.get("reason", ""),
                  f"Interface: Prompt style greyed while prompts are the original glyphs ({r.get('reason')})")
            press("right")
        check(rows()["button_prompts"]["value"] != "Original glyphs",
              f"Button prompts: {rows()['button_prompts']['value']}")
        style = goto_row("button_prompts_style")
        press("right")
        new_style = rows()["button_prompts_style"]["value"]
        style_index = STYLES.index(new_style) if new_style in STYLES else -1
        check(style["enabled"] and style_index == (STYLES.index(style["value"]) + 1) % len(STYLES)
              and read_int(game, "pc_button_prompts_style")[0] == style_index,
              f"Right on Prompt style: {style['value']} -> {new_style} (pc_button_prompts_style {style_index})")
        # Menu opacity: the backdrop over the game and the panels (ui_theme.cpp theme())
        import numpy as np

        def picture(name):
            from PIL import Image
            path = game.present_screenshot(out / f"{name}.png")
            image = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
            if not args.out:
                path.unlink()
            return image

        def off_ground(image, x, y):
            """Mean distance from the theme's ground colour of a 32 x 120 patch (960 x 720: layout = pixels)."""
            import numpy as np
            return float(np.abs(image[y:y + 120, x:x + 32] - np.array(GROUND)).mean())

        goto_row("ui_menus")  # the page scrolled to its top (the pointer's row positions below)
        settle()
        r = goto_row("ui_menu_opacity")
        check(r["enabled"] and r["value"] == "0.94" and abs(read_float(game, "pc_ui_menu_opacity") - 0.94) < 1e-4,
              f"Interface: Menu opacity 0.94 by default ({r['value']})")
        solid = picture("opacity_094")
        press("left")
        check(rows()["ui_menu_opacity"]["value"] == "0.92"
              and abs(read_float(game, "pc_ui_menu_opacity") - 0.92) < 1e-4,
              f"Left: 0.92 ({rows()['ui_menu_opacity']['value']}, {read_float(game, 'pc_ui_menu_opacity'):.3f})")
        y = CARD_ROW_Y0 + CARD_ROW_STEP * row_index("ui_menu_opacity")
        ptr(SLIDER_X0 + 10, y, down=True)
        ptr(SLIDER_X0 + (SLIDER_X1 - SLIDER_X0) * 0.2, y, down=True)
        ptr(SLIDER_X0 + (SLIDER_X1 - SLIDER_X0) * 0.2, y, down=False)
        opacity = read_float(game, "pc_ui_menu_opacity")
        check(0.1 < opacity < 0.3, f"a pointer drag to the bar's first fifth: about 0.2 ({opacity:.2f}, "
                                   f"{rows()['ui_menu_opacity']['value']})")
        game.run(4)
        thin = picture("opacity_low")
        before, after = off_ground(solid, 8, 300), off_ground(thin, 8, 300)
        import numpy as np
        moved = float(np.abs(thin[300:420, 8:40] - solid[300:420, 8:40]).mean())
        check(before < 6 and after > before + 8 and moved > 8,
              f"the backdrop shows the world at {opacity:.2f}: off the ground colour {before:.1f} -> {after:.1f}, "
              f"changed {moved:.1f} (left margin, 32 x 120)")
        wait_clean()
        saved_opacity = settings_lines(settings).get("ui_menu_opacity")
        check(saved_opacity is not None and abs(float(saved_opacity) - opacity) < 1e-3,
              f"saved ui_menu_opacity {opacity:g} ({saved_opacity})")
        # the Ashley tab, its model shown: see-through as well
        leave()
        for _ in range(12):
            if shell()["tab"] == "ashley":
                break
            press("r1", settle=8)
        wait(lambda: (shell().get("ashley") or {}).get("model") == 2, "the Ashley model", limit=80)
        game.run(10)
        ashley = picture("opacity_low_ashley")
        mx, my, mw, mh = (int(v) for v in shell()["ashley"]["model_rect"])
        import numpy as np
        strip = float(np.abs(ashley[my + 8:my + int(mh * 0.95), mx + 4:mx + 48] - np.array(GROUND)).mean())
        check(strip > 6, f"Ashley tab see-through at {opacity:.2f}: the model column's empty left strip "
                         f"off the ground colour by {strip:.1f} (before: an opaque cut-out, 0)")
        for _ in range(12):
            if shell()["tab"] == "system":
                break
            press("l1", settle=8)
        check(shell()["tab"] == "system", f"back to System ({shell()['tab']})")
        enter("interface")
        # Menu tint (ui_tint.cpp, ui_theme.cpp tint_theme): measured with the backdrop opaque
        goto_row("ui_menus")
        settle()
        y = CARD_ROW_Y0 + CARD_ROW_STEP * row_index("ui_menu_opacity")
        ptr(SLIDER_X0 + 10, y, down=True)
        ptr(SLIDER_X1 + 6, y, down=True)
        ptr(SLIDER_X1 + 6, y, down=False)
        opacity = read_float(game, "pc_ui_menu_opacity")
        check(opacity == 1.0, f"a pointer drag past the bar's end: opacity 1 ({opacity:g})")

        def margin(image):
            """The left margin's mean colour (32 x 120 at 8, 300; 960 x 720: layout = pixels)."""
            return image[300:420, 8:40].reshape(-1, 3).mean(axis=0)

        def dist(a, b):
            return float(np.abs(np.array(a, dtype=float) - np.array(b, dtype=float)).max())

        def tint_rgb():
            return tuple(int(rows()[f"ui_menu_tint_{c}"]["value"]) for c in "rgb")

        r = goto_row("ui_menu_tint")
        theme = shell()["theme"]
        image = picture("tint_lea_monde")
        check(r["value"] == "Le\u00e1 Monde" and r.get("swatch") and theme["tint"] == "lea_monde"
              and theme["ground"][:7] == "#%02x%02x%02x" % tuple(GROUND) and tint_rgb() == (8, 7, 7)
              and dist(margin(image), GROUND) < 6,
              f"Menu tint: Le\u00e1 Monde by default, the theme's ground {theme['ground']} (margin "
              f"{margin(image).round(1)}), sliders {tint_rgb()}, swatch {r.get('swatch')}")
        for k, (name, rgb, ground) in enumerate(TINTS):
            press("right")
            theme = shell()["theme"]
            ok = (rows()["ui_menu_tint"]["value"] == name and read_int(game, "pc_ui_menu_tint")[0] == k + 1
                  and tint_rgb() == rgb and theme["ground"][:7] == ground)
            note = ""
            if name in ("Classic", "Crimson"):
                game.run(4)
                m = margin(picture(f"tint_{name.lower()}"))
                ok = ok and dist(m, hex_rgb(ground)) < 6 and dist(m, GROUND) > 10
                if name == "Classic":
                    ok = ok and m[2] - m[0] > 30
                note = f", margin {m.round(1)}"
            check(ok, f"Right: {name} {rgb}, ground {theme['ground']} (want {ground}{note})")
        # a slider: Custom, from the shown preset's colour
        goto_row("ui_menu_tint_b")
        press("right")
        custom = read_int(game, "pc_ui_menu_tint_custom", 3)
        check(tint_rgb() == (3, 10, 7) and rows()["ui_menu_tint"]["value"] == "Custom"
              and read_int(game, "pc_ui_menu_tint")[0] == 5 and custom == [3, 10, 7],
              f"Right on Blue: 6 -> 7, the tint Custom ({tint_rgb()}, pc_ui_menu_tint_custom {custom})")
        goto_row("ui_menus")
        settle()
        y = CARD_ROW_Y0 + CARD_ROW_STEP * row_index("ui_menu_tint_r")
        ptr(SLIDER_X0 + 10, y, down=True)
        ptr(SLIDER_X0 + (SLIDER_X1 - SLIDER_X0) * 0.5, y, down=True)
        ptr(SLIDER_X0 + (SLIDER_X1 - SLIDER_X0) * 0.5, y, down=False)
        red = tint_rgb()[0]
        ground = hex_rgb(shell()["theme"]["ground"])
        check(red in (15, 16) and rows()["ui_menu_tint"]["value"] == "Custom" and ground[0] > ground[2],
              f"a pointer drag to Red's middle: {red}, still Custom, the ground red ({shell()['theme']['ground']})")
        custom = (red, 10, 7)
        goto_row("ui_menu_tint")
        press("left")
        check(rows()["ui_menu_tint"]["value"] == "Verdant" and tint_rgb() == (3, 10, 6),
              f"Left on Menu tint: Verdant ({tint_rgb()})")
        press("right")
        check(rows()["ui_menu_tint"]["value"] == "Custom" and tint_rgb() == custom,
              f"Right: Custom again with its colour {custom} ({tint_rgb()})")
        tinted = hex_rgb(shell()["theme"]["ground"])
        shot("tint_custom")
        wait_clean()
        saved = settings_lines(settings)
        check(saved.get("ui_menu_tint") == "custom" and saved.get("ui_menu_tint_custom") == "%d %d %d" % custom,
              f"saved ui_menu_tint custom, ui_menu_tint_custom {custom} ({saved.get('ui_menu_tint')}, "
              f"{saved.get('ui_menu_tint_custom')})")
        # the Ashley tab follows: the presenter's backdrop behind the model
        leave()
        for _ in range(12):
            if shell()["tab"] == "ashley":
                break
            press("r1", settle=8)
        wait(lambda: (shell().get("ashley") or {}).get("model") == 2, "the Ashley model", limit=80)
        game.run(10)
        ashley = picture("tint_ashley")
        mx, my, mw, mh = (int(v) for v in shell()["ashley"]["model_rect"])
        strip = ashley[my + 8:my + int(mh * 0.95), mx + 4:mx + 48].reshape(-1, 3).mean(axis=0)
        check(dist(strip, tinted) < 6 and dist(strip, GROUND) > 10,
              f"Ashley tab: the model column's empty strip in the tint {tinted} ({strip.round(1)})")
        for _ in range(12):
            if shell()["tab"] == "system":
                break
            press("l1", settle=8)
        check(shell()["tab"] == "system", f"back to System ({shell()['tab']})")
        enter("interface")
        leave()
        saved_style = settings_lines(settings).get("button_prompts_style")
        check(saved_style == STYLE_NAMES[style_index],
              f"saved button_prompts_style {STYLE_NAMES[style_index]} ({saved_style})")

        # ---- Controls ----
        enter("controls")
        r = goto_row("camera_stick_speed")
        speed = game.call("camera_stick")["speed"]
        check(r["enabled"], f"Controls: Turn speed ({r['value']})")
        press("right")
        new_speed = game.call("camera_stick")["speed"]
        check(abs(new_speed - min(speed + 15, 360)) < 1e-3, f"Right: +15 deg/s ({speed} -> {new_speed})")
        goto_row("reset_camera")
        wait_clean()
        press("circle")
        check(not sysv()["dirty"], "Reset camera does not touch the settings file")
        leave()
        saved = settings_lines(settings)
        check(abs(float(saved.get("camera_stick_speed", "0")) - new_speed) < 1e-3,
              f"saved camera_stick_speed {new_speed:g} ({saved.get('camera_stick_speed')})")

        # ---- Bindings ----
        enter("bindings")
        circle_default = goto_row("bind_circle")["value"]
        triangle_default = rows()["bind_triangle"]["value"]
        press("circle")
        s = sysv()
        check(s["capture"] == "bind_circle" and game.call("system")["capture"]["listening"],
              f"Bindings: Circle on Circle starts a capture ({s['capture']})")
        press("triangle", settle=20)
        press("down")
        s = shell()
        check(s["visible"] and s["system"]["capture"] == "bind_circle" and s["system"]["row"] == "bind_circle",
              f"the capture blocks navigation: Triangle, Down do nothing ({s['visible']}, {s['system']['row']})")
        key("escape")
        s = shell()
        check(s["visible"] and s["system"]["capture"] == "" and rows()["bind_circle"]["value"] == circle_default,
              f"Escape cancels the capture, the binding unchanged ({rows()['bind_circle']['value']})")
        press("circle")
        game.call("system", pad="lefttrigger", down=True)
        game.run(4)
        game.call("system", pad="lefttrigger", down=False)
        game.run(4)
        r = rows()["bind_circle"]
        check(sysv()["capture"] == "" and r["value"].endswith("/ Left trigger"),
              f"a pushed left trigger binds Circle ({r['value']})")
        goto_row("bind_reset")
        press("circle")
        check(rows()["bind_circle"]["value"] == circle_default, f"Reset to defaults: Circle back ({circle_default})")
        goto_row("bind_triangle")
        press("circle")
        check(sysv()["capture"] == "bind_triangle", "Circle on Triangle starts a capture")
        shot("capture")
        key("m")
        r = rows()["bind_triangle"]
        check(sysv()["capture"] == "" and r["value"].startswith("M") and r["value"] != triangle_default,
              f"a pushed m binds Triangle ({triangle_default} -> {r['value']})")
        shot("bindings")
        leave()
        saved = settings_lines(settings)
        check(saved.get("bind_key_triangle", "").split(",")[0] == "m",
              f"saved bind_key_triangle m,... ({saved.get('bind_key_triangle')})")
        check(saved.get("bind_pad_circle") != "lefttrigger", f"saved bind_pad_circle default ({saved.get('bind_pad_circle')})")

        # ---- Display (last: widescreen changes the shell's layout) ----
        enter("display")
        r = goto_row("widescreen")
        check(r["value"] == "Off" and abs(view_aspect(game)[0] - 4 / 3) < 0.01,
              f"Display: Widescreen off, the view 4:3 ({view_aspect(game)[1]})")
        press("right")
        game.run(10)
        aspect, view = view_aspect(game)
        check(rows()["widescreen"]["value"] == "On" and game.call("graphics")["widescreen"]
              and abs(aspect - 16 / 9) < 0.01, f"Right: widescreen on, pc_gl_view 16:9 ({view})")
        shot("display_169")
        # the tint at 16:9: the view's outermost column is the tinted backdrop (opacity 1)
        leave()
        game.run(10)
        wide = picture("tint_169")
        vx, vy, vw, vh = view_aspect(game)[1]
        top = wide.shape[0] - vy - vh  # pc_gl_view is GL's (bottom-left origin)
        column = wide[top + vh * 2 // 5:top + vh * 3 // 5, vx:vx + 32].reshape(-1, 3).mean(axis=0)
        check(dist(column, tinted) < 6, f"16:9: the outermost 32 px column in the tint {tinted} ({column.round(1)}, "
                                        f"view {vx},{vy} {vw}x{vh}, image {wide.shape[1]}x{wide.shape[0]})")
        enter("display")
        # (after the screenshot: the headless offscreen driver keeps the old drawable size in borderless)
        r = goto_row("window_mode")
        check(r["enabled"] and r["value"] == "Windowed", f"Window: Windowed ({r})")
        press("right")
        game.run(4)
        check(rows()["window_mode"]["value"] == "Borderless" and game.call("system")["window_mode"] == "borderless",
              "Right: Borderless (system window_mode)")
        # a risky display change: "Keep these settings?" over the menu; until kept the file keeps Windowed
        confirm = sysv()["confirm"]
        check(confirm["pending"] and confirm["focus"] == "keep", f"Keep these settings? asks, on Keep ({confirm})")
        wait_clean()
        saved = settings_lines(settings)
        check(saved.get("window_mode") == "windowed",
              f"unconfirmed: window_mode windowed saved ({saved.get('window_mode')})")
        press("circle")  # Keep
        check(not sysv()["confirm"]["pending"] and game.call("system")["window_mode"] == "borderless",
              "Circle on Keep: kept borderless")
        wait_clean()
        saved = settings_lines(settings)
        check(saved.get("widescreen") == "1" and saved.get("window_mode") == "borderless",
              f"saved widescreen 1, window_mode borderless ({saved.get('widescreen')}, {saved.get('window_mode')})")
        leave()

        # ---- Game group: the original Data screen and back ----
        goto_item("data")
        press("circle", settle=20)
        wait(lambda: current_menu() == DATA_MENU, f"the Data screen ({current_menu():#x})")
        game.run(60)
        check(not shell()["visible"], "System > Data opens the game's own Data screen; the shell hides")
        shot("data")
        for _ in range(6):
            if current_menu() == 0:
                break
            press("cross", settle=60)
        wait(lambda: current_menu() == 0 and shell()["visible"], f"back from Data ({current_menu():#x})")
        s = shell()
        check(s["tab"] == "system" and s["row"] == "data", f"Cross returns to the System list on Data ({s['row']})")

        # close: the camera resets (Reset camera), the new key opens the menu
        press("triangle", settle=90)
        check(current_menu() == CLOSED and not shell()["visible"], f"Triangle closes the menu ({current_menu():#x})")
        cam = game.call("camera_stick")
        check(cam["resets"] > resets_before, f"Reset camera: the camera reset in the field ({resets_before} -> "
                                             f"{cam['resets']})")
        key("m", settle=60)
        check(current_menu() == 0 and shell()["visible"], f"m opens the menu in the field ({current_menu():#x})")
        press("triangle", settle=90)
        check(current_menu() == CLOSED, f"Triangle closes it ({current_menu():#x})")
        game.call("settings", action="save")
        first = settings_lines(settings)
    except Timeout as error:
        check(False, f"timed out waiting for {error}")
        first = None
    finally:
        game.close()

    # ---- relaunch with the same file ----
    if first is not None:
        game = Game.launch(fast=True, card=str(card), log=out / "game2.log", extra_env=env)  # the relaunch itself: boots
        try:
            game.wait_title()
            game.run(10)
            sy = game.call("system")
            g = game.call("graphics")
            aspect, view = view_aspect(game)
            check(g["widescreen"] and abs(aspect - 16 / 9) < 0.01, f"relaunch: widescreen, the view 16:9 ({view})")
            check(sy["master_volume"] == 50 and sy["window_mode"] == "borderless"
                  and sy["brightness"] == defaults_title["brightness"] == 50,
                  f"relaunch: volume 50, borderless, brightness 50 ({sy['master_volume']}, {sy['window_mode']}, "
                  f"{sy['brightness']})")
            check("fxaa" in g["aa"], f"relaunch: aa fxaa ({g['aa']})")
            check(read_int(game, "pc_qol_weapon_slots")[0] == 1, "relaunch: quick slots on")
            check(read_int(game, "pc_button_prompts_style")[0] == style_index,
                  f"relaunch: prompt style {STYLES[style_index]}")
            check(read_int(game, "pc_lighting_preset")[0] == 1, "relaunch: lighting quality Low")
            relaunched = read_float(game, "pc_ui_menu_opacity")
            check(abs(relaunched - float(first["ui_menu_opacity"])) < 1e-3,
                  f"relaunch: menu opacity {first['ui_menu_opacity']} ({relaunched:.3f})")
            tint, custom_now = read_int(game, "pc_ui_menu_tint")[0], read_int(game, "pc_ui_menu_tint_custom", 3)
            check(tint == 5 and " ".join(map(str, custom_now)) == first.get("ui_menu_tint_custom"),
                  f"relaunch: menu tint Custom {custom_now} ({tint}, {first.get('ui_menu_tint_custom')})")
            check(abs(game.call("camera_stick")["speed"] - float(first["camera_stick_speed"])) < 1e-3,
                  "relaunch: turn speed")
            game.call("settings", action="save")
            again = settings_lines(settings)
            diff = {k: (first.get(k), again.get(k)) for k in set(first) | set(again) if first.get(k) != again.get(k)}
            check(not diff, f"relaunch: a save writes the same values (lighting_preset, bindings, ...) {diff}")
        finally:
            game.close()

    wav.unlink(missing_ok=True)
    print("ui_system_smoke:", "FAIL" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
