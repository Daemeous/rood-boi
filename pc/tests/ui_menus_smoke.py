#!/usr/bin/env python3
"""Modern menus foundation (pc/src/ui, pc/docs/Modern_Menus.md).

    python3 pc/tests/ui_menus_smoke.py [--card pc/save/card1.mcd] [--out DIR]

From a copy of the card (skipped without one), in the save room:
  - ui_menus off: Triangle opens the game's own menu (currentMenu 0), the shell
    stays hidden; Triangle closes it
  - on: Triangle opens the shell on the Ashley tab; R1/L1 move between tabs
  - Square on Ashley opens the original Status (MENU4); Cross returns to
    the shell on the same tab; Triangle closes the menu (currentMenu 0x3F) and
    the field runs again
  - System > Options (original) opens the game's Options screen; Cross returns
  - System > Interface > Menus switches ui_menus off from inside the shell, from the next
    open: Status and back keeps the shell for the rest of this menu session;
    the next Triangle opens the game's own menu
  - the shell takes only the top level: an L2 shortcut (L2 + Up, misc items:
    currentMenu 4, vs_battle_shortcutInvoked 5) shows the game's own list and
    the shell stays hidden during and after it
  - Abilities > Break Arts: with break arts unlocked in RAM (this card has
    none) and the weapon sheathed, the equipped weapon's learned art is not
    usable and says why, Circle on it is refused (the menu stays), Square
    opens the game's Break Arts (MENU1) and Cross returns to the shell on the
    same sub-tab
  - 16:9 (graphics widescreen): the shell opens and closes the same way.
The save room's container (zone 9 room 17) is not covered: reaching it needs
walking Ashley to the chest, which blind RPC input does not do reliably."""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

CLOSED = 0x3F


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_menus"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    os.environ.pop("ROOD_SETTINGS", None)
    sym = symbols()
    menu_state = sym["vs_battle_menuState"]
    shortcut_invoked = sym["vs_battle_shortcutInvoked"]
    hud_hide = sym["D_800EB9AF"]  # the field HUD slides out while set
    actions, action_ranges = sym["vs_main_actions"], sym["D_800EBD68"]
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        state = game.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1

        def current_menu():
            return game.read(menu_state, 1)[0]

        def shell():
            return game.call("ui_menu")

        def shot(name):
            if args.out:
                game.present_screenshot(out / f"{name}.png")

        # off: the game's own menu
        check(shell()["menus"] == "original", "ui_menus is off by default")
        game.press("triangle", vsyncs=3, settle=40)
        check(current_menu() == 0, "off: Triangle opens the game's top-level menu")
        check(not shell()["visible"], "off: the shell stays hidden")
        game.press("triangle", vsyncs=3, settle=60)
        check(current_menu() == CLOSED, "off: Triangle closes it")

        # on: the shell
        game.call("ui_menu", modern=True)
        game.press("triangle", vsyncs=3, settle=40)
        s = shell()
        check(s["visible"] and s["tab"] == "ashley", f"on: the shell opens on Ashley ({s})")
        check(game.read(hud_hide, 1)[0] == 1, "on: the field HUD slides out behind the shell")
        shot("ashley")
        game.press("r1", vsyncs=3, settle=10)
        check(shell()["tab"] == "equipment", "R1: Equipment")
        game.press("l1", vsyncs=3, settle=10)
        check(shell()["tab"] == "ashley", "L1: Ashley")

        # Square on Ashley = the original Status (command 4 -> currentMenu 5 | 0x40)
        game.press("square", vsyncs=3, settle=120)
        check(current_menu() == (5 | 0x40), f"Square opens the original Status ({current_menu():#x})")
        check(not shell()["visible"], "the shell hides while Status shows")
        shot("status")
        game.press("cross", vsyncs=3, settle=120)
        s = shell()
        check(current_menu() == 0 and s["visible"] and s["tab"] == "ashley",
              f"Cross returns to the shell on Ashley ({current_menu():#x}, {s})")

        # System > Interface > Menus switches ui_menus off from inside the shell
        game.press("l1", vsyncs=3, settle=10)
        check(shell()["tab"] == "system", "L1 wraps to System")
        # the game's own screens stay in the list: Options (original) opens MENU8 (command 7)
        for _ in range(4):
            if shell()["row"] == "options":
                break
            game.press("down", vsyncs=3, settle=6)
        game.press("circle", vsyncs=3, settle=120)
        check(current_menu() == (8 | 0x40) and not shell()["visible"],
              f"System > Options (original) opens the game's Options ({current_menu():#x})")
        shot("options_original")
        game.press("cross", vsyncs=3, settle=120)
        s = shell()
        check(current_menu() == 0 and s["visible"] and s["tab"] == "system" and s["row"] == "options",
              f"Cross returns to the System list on Options ({current_menu():#x}, {s['tab']}, {s['row']})")
        for _ in range(12):
            if shell()["row"] == "interface":
                break
            game.press("down", vsyncs=3, settle=6)
        check(shell()["row"] == "interface", "the Interface page in the System list")
        game.press("circle", vsyncs=3, settle=10)
        s = shell()
        check(s["system"]["mode"] == "page" and s["system"]["row"] == "ui_menus",
              f"Circle enters Interface on its Menus row ({s['system']['mode']}, {s['system']['row']})")
        game.press("right", vsyncs=3, settle=10)
        check(shell()["menus"] == "original", "Right on Menus: original from the next open")
        game.press("cross", vsyncs=3, settle=10)
        s = shell()
        check(s["visible"] and s["system"]["mode"] == "list", "Cross: back to the System list, the shell stays")
        # the rest of this session keeps the shell: R1 wraps to Ashley, Status, back
        game.press("r1", vsyncs=3, settle=10)
        game.press("square", vsyncs=3, settle=120)
        check(current_menu() == (5 | 0x40), f"after Menus: Status opens ({current_menu():#x})")
        game.press("cross", vsyncs=3, settle=120)
        s = shell()
        check(current_menu() == 0 and s["visible"] and s["menus"] == "original",
              f"back from Status: still the shell this session ({current_menu():#x}, {s})")

        # Triangle closes the menu; the field runs
        game.press("triangle", vsyncs=3, settle=90)
        check(current_menu() == CLOSED, "Triangle closes the menu")
        check(not shell()["visible"], "the shell is hidden after closing")
        check(game.read(hud_hide, 1)[0] == 0, "the field HUD is back after closing")
        shot("closed")
        # the next open is the game's own menu
        game.press("triangle", vsyncs=3, settle=40)
        check(current_menu() == 0 and not shell()["visible"], "Menus = original: the next Triangle opens the game's menu")
        game.press("triangle", vsyncs=3, settle=90)
        check(current_menu() == CLOSED, "and closes it")
        game.call("ui_menu", modern=True)

        # an L2 shortcut is not the top level: the game's own list, no shell
        game.call("input", buttons=["l2"], vsyncs=80)
        game.run(30)
        game.call("input", buttons=["up"], vsyncs=3)
        game.run(40)
        invoked = game.read(shortcut_invoked, 1)[0]
        check(current_menu() == 4 and invoked == 5,
              f"L2 + Up opens the misc items shortcut (currentMenu {current_menu():#x}, shortcut {invoked})")
        check(not shell()["visible"], "the shell stays hidden over the shortcut list")
        shot("shortcut")
        game.run(60)
        check(not shell()["visible"], "and stays hidden while it shows")
        game.press("cross", vsyncs=3, settle=60)
        check(current_menu() == CLOSED and not shell()["visible"],
              f"Cross closes the shortcut; the shell stays hidden ({current_menu():#x})")
        game.press("triangle", vsyncs=3, settle=40)
        check(current_menu() == 0 and shell()["visible"], "Triangle after a shortcut: the shell again")
        game.press("triangle", vsyncs=3, settle=90)
        check(current_menu() == CLOSED, "and Triangle closes it")

        # Break Arts: unlock the break arts in RAM (vs_main_actions records of 52 bytes, unlocked =
        # byte 13 bit 7; D_800EBD68[2..3] = the break arts range func_800CACD0 checks for the
        # Break Arts command)
        lo, hi = game.read(action_ranges + 2, 2)
        for a in range(lo, hi):
            b = game.read(actions + a * 52 + 13, 1)[0]
            game.write(actions + a * 52 + 13, bytes([b | 0x80]))
        game.press("triangle", vsyncs=3, settle=40)
        for _ in range(7):
            if shell()["tab"] == "abilities":
                break
            game.press("r1", vsyncs=3, settle=10)
        for _ in range(3):
            if shell().get("abilities", {}).get("sub") == "arts":
                break
            game.press("r2", vsyncs=3, settle=10)
        ab = shell()["abilities"]
        check(ab["sub"] == "arts" and ab["mode"] == "categories" and ab["category"] == ab["equipped_category"],
              f"Abilities > Break Arts opens on the equipped weapon's arts ({ab['sub']}, {ab['mode']}, "
              f"{ab['category']}, {ab['equipped_category']})")
        game.press("circle", vsyncs=3, settle=10)
        ab = shell()["abilities"]
        art = ab["categories"][ab["category"] - 1]["arts"][0]
        check(ab["mode"] == "arts" and art["state"] == "learned" and not art["usable"] and art["reason"],
              f"weapon sheathed: the learned art is not usable and says why ({art['name']}, {art['reason']})")
        shot("break_arts")
        game.press("circle", vsyncs=3, settle=30)
        check(current_menu() == 0 and shell()["visible"], f"Circle on it is refused: the menu stays ({current_menu():#x})")
        game.press("square", vsyncs=3, settle=120)
        check(current_menu() == (2 | 0x40) and not shell()["visible"],
              f"Square opens the game's Break Arts ({current_menu():#x})")
        shot("break_arts_open")
        game.press("cross", vsyncs=3, settle=120)
        s = shell()
        check(current_menu() == 0 and s["visible"] and s["abilities"]["sub"] == "arts",
              f"Cross returns to the shell on Break Arts ({current_menu():#x}, {s['abilities']['sub']})")
        game.press("triangle", vsyncs=3, settle=90)
        check(current_menu() == CLOSED and not shell()["visible"], "Triangle closes the menu")

        # 16:9: the shell opens and closes the same way
        game.call("graphics", widescreen=True)
        game.run(30)
        game.press("triangle", vsyncs=3, settle=40)
        s = shell()
        check(s["visible"], f"16:9: the shell opens (on {s['tab']}: the card's cursor memory)")
        shot("ashley_169")
        game.press("triangle", vsyncs=3, settle=90)
        check(current_menu() == CLOSED and not shell()["visible"], "16:9: Triangle closes the menu")
        game.call("graphics", widescreen=False)
    finally:
        game.close()
    print("ui_menus_smoke:", "FAIL" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
