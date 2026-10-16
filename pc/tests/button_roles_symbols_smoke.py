#!/usr/bin/env python3
"""The game's own ○ and × follow the optional button roles (pc/src/button_roles.c:
symbols_swap, pc_button_roles_glyph, pc_button_roles_texels; README "Cross confirms").

    python3 pc/tests/button_roles_symbols_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

From the save room (a copy of the card; skipped without one), in the game's
menus (context "menus": qol_confirm_cross decides), each frame recorded for 64
vsyncs (whole cursor cycles) and drawn again (RPC ui_upscale redraw). The
English data has a ○ / × in two texts, and both name buttons the roles never
move (they stay; RPC button_roles symbol_exceptions false exchanges them too,
to test the text paths):

- text in sprites (the menus' printer): Battle Abilities' information,
  "Assign Abilities to the ○, △, and □ buttons" (the ability buttons): off
  and on the ○ is drawn as itself (RPC button_roles "drawn", nothing
  "swapped"); on without the exceptions as × (exchanged), only that glyph's
  pixels change in the original drawing (ui_text original, button_prompts
  original), and on (kept) and off again are the same pixels as off; with
  ui_text font and the PlayStation prompts the prompt shown (RPC
  button_prompts shown) is the circle on, the cross without the exceptions;
- sprites: the Quick Manual's ○ MENU key (the 16 x 16 button sprite, the
  manual's own control): the cross with the option on, the circle off
  (counters and, with the PlayStation prompts, the icon shown); in the
  original drawing on and off differ only inside one button cell and off
  again is the same picture (identical output);
- text in the fixed font (the manual's pages): the L2 shortcut's "(Using
  △,○,×, and □ buttons)", as the information above.

--shots keeps the recorded frames (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from button_prompts_smoke import default_card  # noqa: E402

CLOSED = 0x3F
BATTLE_ABILITIES, QUICK_MANUAL = 2, 9  # top-level rows
RECORD_VSYNCS = 64
SETTLE_VSYNCS = 32  # a record is 96 vsyncs: every frame recorded at the same animation phase

failures = []


def check(ok, what):
    print(("PASS: " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", type=pathlib.Path, default=default_card())
    parser.add_argument("--shots", type=pathlib.Path)
    args = parser.parse_args()
    if not args.card.exists():
        print("SKIP: no memory card", args.card)
        return 77
    work = OUT / "button_roles_symbols"
    work.mkdir(parents=True, exist_ok=True)
    if args.shots:
        args.shots.mkdir(parents=True, exist_ok=True)
    card = work / "card.mcd"
    shutil.copyfile(args.card, card)
    env = {"ROOD_OUT": str(work), "ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_BUTTON_PROMPTS": "",
           "ROOD_GI": "0"}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env.setdefault("ROOD_VK_VALIDATE", "1")
    sym = symbols()
    menu_state, submenu = sym["vs_battle_menuState"], sym["vs_battle_submenuStates"]
    game = launch_at("save_room", card=card, socket_path=f"/tmp/rood-symbols-{os.getpid()}.sock",
                     log=str(work / "game.log"), extra_env=env, fast=True)
    try:
        game.call("graphics", interpolate=False, aa="off")
        game.call("button_prompts", button_prompts="original")
        game.call("graphics", ui_text="original")
        game.run(200)  # the room settles before the menu takes input

        def roles(**params):
            return game.call("button_roles", **params)

        def current_menu():
            return game.read(menu_state, 1)[0] & 0xFF

        def record(name, confirm_cross, exceptions=True):
            """Record frames with the option, then draw the last one again: its
            image, the ○ / × drawn meanwhile (deltas) and the prompts shown."""
            roles(confirm_cross=confirm_cross, symbol_exceptions=exceptions)
            game.run(SETTLE_VSYNCS)  # (the frames already built; whole cursor cycles)
            game.call("button_prompts", clear_shown=True)
            before = roles()["drawn"]
            game.run(RECORD_VSYNCS)
            r = roles()
            drawn = {k: {s: r["drawn"][k][s] - before[k][s] for s in ("circle", "cross", "swapped")}
                     for k in ("text", "sprite")}
            path = work / f"{name}.ppm"
            game.call("ui_upscale", redraw=str(path))
            image = np.asarray(Image.open(path)).copy()  # (all of VRAM: both display buffers)
            path.unlink()
            if args.shots:
                Image.fromarray(image).save(args.shots / f"{name}.png")
            scale = image.shape[1] // 1024
            roles(symbol_exceptions=True)
            return {"image": image, "scale": scale, "drawn": drawn, "context": r["context"],
                    "shown": game.call("button_prompts")["shown"]}

        def changed_boxes(a, b):
            """The boxes (y0, y1, x0, x1) of the groups of pixels that differ
            (each display buffer of VRAM holds its own copy)."""
            labels, _ = ndimage.label(ndimage.binary_dilation(np.any(a != b, axis=2), iterations=2))
            return [(s[0].start, s[0].stop, s[1].start, s[1].stop) for s in ndimage.find_objects(labels)]

        def fits(boxes, height, width):
            return bool(boxes) and all(y1 - y0 <= height and x1 - x0 <= width for y0, y1, x0, x1 in boxes)

        def same_in(a, b, boxes):
            return all(np.array_equal(a[y0:y1, x0:x1], b[y0:y1, x0:x1]) for y0, y1, x0, x1 in boxes)

        def open_row(row, confirm):
            for _ in range(5):
                if current_menu() != CLOSED:
                    break
                game.press("triangle", vsyncs=3, settle=60)
            for _ in range(12):
                if game.read(submenu, 1)[0] == row:
                    break
                game.press("down", vsyncs=3, settle=12)
            ok = current_menu() != CLOSED and game.read(submenu, 1)[0] == row
            game.press(confirm, vsyncs=3, settle=90)
            return ok

        def close_menu():
            for _ in range(8):
                if current_menu() == CLOSED:
                    break
                game.press("triangle", vsyncs=3, settle=60)

        def counts(r, kind):
            d = r["drawn"][kind]
            return f"{d['circle']} ○, {d['cross']} ×, {d['swapped']} swapped"

        def text_checks(label, off, on, free, again, width):
            """Text drawn off, on (an exception: kept), on with the exceptions
            off (exchanged) and off again: what was drawn and the pixels of the
            same frame's glyphs (one recorded frame each; the menu is still)."""
            t = "text"
            check(off["drawn"][t]["circle"] > 0 and off["drawn"][t]["swapped"] == 0,
                  f"{label}, off: its ○ drawn as itself ({counts(off, t)})")
            check(on["drawn"][t] == off["drawn"][t],
                  f"{label}, on: kept, it names buttons the roles never move ({counts(on, t)})")
            check(free["drawn"][t]["swapped"] > 0 and free["drawn"][t]["cross"] == off["drawn"][t]["circle"]
                  and free["drawn"][t]["circle"] == off["drawn"][t]["cross"],
                  f"{label}, on without the exceptions: ○ and × exchanged ({counts(free, t)})")
            boxes = changed_boxes(on["image"], free["image"])  # (sprites drawn as on)
            k = off["scale"]
            if check(fits(boxes, 16 * k, (width + 4) * k), f"{label}: exchanged, only its ○ / × glyphs change "
                                                             f"(boxes {boxes})"):
                check(same_in(off["image"], on["image"], boxes) and same_in(off["image"], again["image"], boxes),
                      f"{label}: kept and off again, the same pixels as off")

        # ---- text in sprites (the menus' printer): Battle Abilities' information ----
        roles(confirm_cross=False)
        if not check(open_row(BATTLE_ABILITIES, "circle") and current_menu() == (3 | 0x40),
                     f"Battle Abilities opens ({current_menu():#x})"):
            return 1
        a = record("abilities-off", False)
        b = record("abilities-on", True)
        c = record("abilities-on-free", True, exceptions=False)
        d = record("abilities-off-again", False)
        check(b["context"] == "menus", f"Battle Abilities' choice: context menus ({b['context']})")
        text_checks("Battle Abilities' information", a, b, c, d, 14)
        # The same with ui_text font and the PlayStation prompts.
        game.call("graphics", ui_text="font")
        game.call("button_prompts", button_prompts="playstation")
        e = record("abilities-font-on", True)
        f = record("abilities-font-free", True, exceptions=False)
        check(e["shown"]["text"].get("circle", "").startswith("playstation_button_circle")
              and "cross" not in e["shown"]["text"], f"text, font, on: the PlayStation circle ({e['shown']['text']})")
        check(f["shown"]["text"].get("cross", "").startswith("playstation_button_cross")
              and "circle" not in f["shown"]["text"],
              f"text, font, on without the exceptions: the PlayStation cross ({f['shown']['text']})")
        game.call("graphics", ui_text="original")
        game.call("button_prompts", button_prompts="original")
        close_menu()

        # ---- sprites: the Quick Manual's ○ MENU key ----
        roles(confirm_cross=False)
        open_row(QUICK_MANUAL, "circle")
        game.press("circle", vsyncs=3, settle=120)  # Basic Controls: its first page
        h = record("manual-off", False)
        i = record("manual-on", True)
        j = record("manual-off-again", False)
        check(h["drawn"]["sprite"]["circle"] > 0 and h["drawn"]["sprite"]["cross"] == 0,
              f"sprite, off: the ○ key drawn as itself ({counts(h, 'sprite')})")
        check(i["drawn"]["sprite"]["cross"] > 0 and i["drawn"]["sprite"]["circle"] == 0
              and i["drawn"]["sprite"]["swapped"] > 0,
              f"sprite, on: the ○ key drawn as × ({counts(i, 'sprite')})")
        boxes = changed_boxes(h["image"], i["image"])
        cell = 20 * h["scale"]
        check(fits(boxes, cell, cell), f"sprite, original drawing: on and off differ only inside the button's cell "
                                       f"(boxes {boxes})")
        check(np.array_equal(h["image"], j["image"]), "sprite, original drawing: off again, the same picture")
        game.call("button_prompts", button_prompts="playstation")
        k = record("manual-prompts-off", False)
        m = record("manual-prompts-on", True)
        check(k["shown"]["sprite"].get("circle", "").startswith("playstation_button_circle")
              and "cross" not in k["shown"]["sprite"],
              f"sprite, PlayStation prompts, off: the circle ({k['shown']['sprite']})")
        check(m["shown"]["sprite"].get("cross", "").startswith("playstation_button_cross")
              and "circle" not in m["shown"]["sprite"],
              f"sprite, PlayStation prompts, on: the cross ({m['shown']['sprite']})")
        game.call("button_prompts", button_prompts="original")

        # ---- text in the fixed font (the manual's pages): "(Using △,○,×, and □ buttons)" ----
        roles(confirm_cross=False)
        found = False
        for _ in range(24):  # Square held, Down: the next page
            before = roles()["drawn"]["text"]
            game.run(8)
            if roles()["drawn"]["text"]["cross"] > before["cross"]:
                found = True
                break
            game.call("input", buttons=["square"], vsyncs=6)
            game.run(3)
            game.call("input", buttons=["square", "down"], vsyncs=3)
            game.run(120)
        if check(found, "the Quick Manual's page with the L2 shortcut's ○ and ×"):
            n = record("help-off", False)
            o = record("help-on", True)
            p = record("help-on-free", True, exceptions=False)
            q = record("help-off-again", False)
            text_checks("the manual's shortcut line", n, o, p, q, 3 * 12)
        close_menu()
    finally:
        game.close()
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log and "[vk] validation:" not in log, "no crash or validation message")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
