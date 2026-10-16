#!/usr/bin/env python3
"""The optional button roles (settings qol_confirm_cross and
qol_field_cross_attack, pc/src/button_roles.c: Circle and Cross exchanged in
the pad word by the game's context): Cross confirms in menus, Cross attacks
in the field; each alone, both, and neither (the original). The RPC input
goes through the same pad word as the keys and the gamepad.

    python3 pc/tests/button_roles_smoke.py [--card pc/save/card1.mcd] [--out DIR]

From a copy of the card (skipped without one):
  - the settings file's qol_confirm_cross 1 applies at startup (RPC
    button_roles: confirm on Cross, field roles unchanged)
  - the title (Start, then Cross for Continue, the memory card and the file)
    loads the save: the title and load menus confirm with Cross; the
    context is "menus" at the title, "field" in the field, "menus" with the
    menu open
  - the original Battle Abilities list (a chain ability learned in RAM):
    context "game" (Circle, Triangle and Square put the ability on that
    button, Cross leaves), no exchange with the option on
  - confirm on, in the field: Circle closes the top-level menu; Cross on
    Status opens it (MENU4), Circle returns to the top level; the field is
    unchanged: Circle draws the weapon, Cross sheathes it
  - both off (the original): Circle opens Status, Cross returns, Cross closes
    the top level
  - field on alone: Cross draws, Circle sheathes; the menu keeps Circle
  - both on (the mod's default controls): Cross draws, Circle sheathes, Cross
    opens Status, Circle returns; drawn, Cross opens targeting (context
    "targeting", exchanged), Circle backs out of it
  - the modern menus (ui_menus) follow qol_confirm_cross, on then off: the
    back button closes the shell; Equipment (Gear: the pick list and back),
    Items (the action menu and back), Items > Workshop (Rename's list and
    back), Abilities (a chain ability learned in RAM; Chain: the pick list
    and back; in the learned list Circle stays the ability button and Cross
    goes back either way), System (Controls, which lists both options, and
    back); the Loot screen over a loot list opened as after a battle (RPC
    loot, one Cure Root): back asks to leave, back again returns, confirm
    takes the root and closes it."""
import argparse
import os
import pathlib
import shutil
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

CLOSED = 0x3F
STATUS = 4  # the top-level row of Status (MENU4: currentMenu 5 | 0x40)
BATTLE_ABILITIES = 2  # the top-level row of Battle Abilities (MENU2: currentMenu 3 | 0x40)
LOOT_BYTES, CURE_ROOT = 0x220, 323  # a vs_battle_loot list (misc items at 0x214); ui_loot_smoke.py


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "button_roles"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    settings = out / "settings.cfg"
    settings.write_text("qol_confirm_cross 1\n")
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SETTINGS": str(settings)}
    sym = symbols()
    menu_state, submenu = sym["vs_battle_menuState"], sym["vs_battle_submenuStates"]
    field_mode, camera_mode = sym["D_800F196C"], sym["_cameraMode"]
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env)
    try:
        def roles(**params):
            return game.call("button_roles", **params)

        def current_menu():
            return game.read(menu_state, 1)[0]

        def drawn():
            return game.call("weapon_slots")["weapon_drawn"]

        r = roles()
        check(r["confirm_cross"] == 1 and r["field_cross_attack"] == 0 and r["confirm"] == "cross"
              and r["cancel"] == "circle" and r["attack"] == "circle" and r["interact"] == "cross"
              and r["target"] == "cross", f"settings file: qol_confirm_cross 1, field roles unchanged ({r})")

        game.wait_title()
        r = roles()
        check(r["context"] == "menus" and r["swap"] == 1, f"the title: context menus, Circle and Cross exchanged ({r})")
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("cross", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        check(bool(state.get("player")),
              f"title and loading confirm with Cross: in the field (zone {state['zone']} room {state['room']})")
        if failures:
            return 1
        r = roles()
        check(r["context"] == "field" and r["swap"] == 0, f"the field: context field, not exchanged ({r})")

        def open_menu():
            game.press("triangle", vsyncs=3, settle=40)
            return current_menu() == 0

        def to_status():
            for _ in range(10):
                if game.read(submenu, 1)[0] == STATUS:
                    return True
                game.press("down", vsyncs=3, settle=8)
            return game.read(submenu, 1)[0] == STATUS

        def status_round_trip(confirm, cancel, label):
            ok = open_menu() and to_status()
            game.press(confirm, vsyncs=3, settle=120)
            opened = current_menu()
            game.press(cancel, vsyncs=3, settle=120)
            back = current_menu()
            check(ok and opened == (5 | 0x40) and back == 0,
                  f"{label}: {confirm.title()} opens Status, {cancel.title()} returns ({opened:#x}, {back:#x})")
            game.press("triangle", vsyncs=3, settle=90)
            check(current_menu() == CLOSED, f"{label}: Triangle closes the menu")

        def field(attack, interact, label):
            game.press(attack, vsyncs=3, settle=60)
            d = drawn()
            game.press(interact, vsyncs=3, settle=60)
            check(d == 1 and drawn() == 0, f"{label}: {attack.title()} draws the weapon, "
                                           f"{interact.title()} sheathes it ({d}, {drawn()})")

        def ui(**params):
            return game.call("ui_menu", **params)

        def go_tab(tab):
            for _ in range(8):
                if ui()["tab"] == tab:
                    return True
                game.press("r1", vsyncs=3, settle=6)
            return ui()["tab"] == tab

        def step(button, view, want, label):
            """Press `button`; the tab's mode (ui_menu[view]["mode"]) becomes `want`."""
            game.press(button, vsyncs=3, settle=8)
            mode = ui()[view]["mode"]
            check(mode == want, f"{label}: {button.title()} -> {want} ({mode})")

        def modern_tabs(confirm, cancel, label):
            """Each native tab's confirm and back (ui_menus 1)."""
            game.press("triangle", vsyncs=3, settle=40)
            s = ui()
            check(s["visible"], f"{label}: the shell opens ({s['tab']})")
            game.press(cancel, vsyncs=3, settle=60)
            check(not ui()["visible"] and current_menu() == CLOSED,
                  f"{label}: {cancel.title()} closes the shell from {s['tab']}")
            game.press("triangle", vsyncs=3, settle=40)
            if go_tab("equipment") and ui()["equipment"]["mode"] == "gear":
                step(confirm, "equipment", "pick", f"{label}, Equipment")
                step(cancel, "equipment", "gear", f"{label}, Equipment")
            else:
                check(False, f"{label}: the Equipment tab ({ui()['tab']})")
            if go_tab("items"):
                it = ui()["items"]
                if it["rows"] and it["mode"] == "list" and not it["locked"]:
                    step(confirm, "items", "actions", f"{label}, Items")
                    step(cancel, "items", "list", f"{label}, Items")
                else:
                    print(f"note: {label}: Items has nothing to act on ({it})")
                # Items > Workshop: Rename (usable away from a workshop) opens its list, back to the rows
                category = ui()["items"]["category"]
                for _ in range(9):
                    if ui()["items"]["category"] == "workshop":
                        break
                    game.press("l2", vsyncs=3, settle=6)
                for _ in range(300):
                    w = ui()["items"].get("workshop") or {}
                    if w.get("valid") and not w.get("busy"):
                        break
                    game.run(2)
                w = ui()["items"].get("workshop") or {}
                if w.get("mode") == "rows":
                    rename = [r["id"] for r in w["rows"]].index("rename")
                    for _ in range(8):
                        if (ui()["items"].get("workshop") or {}).get("row") == rename:
                            break
                        game.press("down", vsyncs=3, settle=4)
                    game.press(confirm, vsyncs=3, settle=8)
                    mode = (ui()["items"].get("workshop") or {}).get("mode")
                    check(mode == "target", f"{label}, Workshop: {confirm.title()} on Rename -> target ({mode})")
                    game.press(cancel, vsyncs=3, settle=8)
                    mode = (ui()["items"].get("workshop") or {}).get("mode")
                    check(mode == "rows", f"{label}, Workshop: {cancel.title()} -> rows ({mode})")
                else:
                    check(False, f"{label}: Items > Workshop ({w})")
                for _ in range(9):
                    if ui()["items"]["category"] == category:
                        break
                    game.press("l2", vsyncs=3, settle=6)
            if go_tab("abilities"):
                for _ in range(3):
                    if ui()["abilities"]["sub"] == "chain":
                        break
                    game.press("r2", vsyncs=3, settle=6)
                ab = ui()["abilities"]
                if ab["sub"] == "chain" and ab["mode"] == "slots" and ab["chain"]["entries"]:
                    step(confirm, "abilities", "pick", f"{label}, Abilities")
                    step(cancel, "abilities", "slots", f"{label}, Abilities")
                    # the learned list: Circle puts the ability on Circle, Cross goes back, whatever the option
                    step("down", "abilities", "list", f"{label}, Abilities")
                    step("circle", "abilities", "list", f"{label}, Abilities list (Circle maps, stays)")
                    step("cross", "abilities", "slots", f"{label}, Abilities list (Cross goes back)")
                else:
                    check(False, f"{label}: Abilities > Chain with a learned ability ({ab['sub']}, {ab['mode']})")
            else:
                check(False, f"{label}: the Abilities tab ({ui()['tab']})")
            if go_tab("system"):
                for _ in range(16):
                    if ui()["row"] == "controls":
                        break
                    game.press("down", vsyncs=3, settle=4)
                step(confirm, "system", "page", f"{label}, System > Controls")
                sv = ui()["system"]
                ids = [r["id"] for r in sv["rows"]]
                check(sv["page"] == "controls" and "qol_confirm_cross" in ids and "qol_field_cross_attack" in ids,
                      f"{label}: System > Controls lists both options ({sv['page']})")
                step(cancel, "system", "list", f"{label}, System")
                # System > Data: the native Data screen (data_bridge.c) over the game's MENU7
                for key in ["down"] * 16 + ["up"] * 16:
                    if ui()["row"] == "data":
                        break
                    game.press(key, vsyncs=3, settle=4)
                if ui()["row"] == "data":
                    game.press(confirm, vsyncs=3, settle=4)
                    for _ in range(120):
                        if ui().get("data"):
                            break
                        game.run(2)
                    opened = bool(ui().get("data"))
                    game.run(20)
                    for _ in range(4):
                        if not ui().get("data"):
                            break
                        game.press(cancel, vsyncs=3, settle=60)
                    u = ui()
                    check(opened and not u.get("data") and u["visible"] and u["row"] == "data",
                          f"{label}, System > Data: {confirm.title()} opens the Data screen, {cancel.title()} "
                          f"returns to the System list ({opened}, {u['row']})")
                else:
                    check(False, f"{label}: System > Data ({ui()['row']})")
            else:
                check(False, f"{label}: the System tab ({ui()['tab']})")
            game.press("triangle", vsyncs=3, settle=90)
            check(current_menu() == CLOSED and not ui()["visible"], f"{label}: Triangle closes")

        def loot_screen(confirm, cancel, label):
            """The modern Loot screen (ui_menus 1) over a loot list the game opens as after a battle
            (RPC loot): one Cure Root. Back on the list asks to leave (Yes / No); back again returns to
            the list; confirm takes the root and the screen closes."""
            loot = bytearray(LOOT_BYTES)
            loot[0x214:0x218] = struct.pack("<HBB", CURE_ROOT, 7, 1)  # misc 0: id, every difficulty, 1
            r = game.call("loot", hex=loot.hex())
            if not r.get("opened"):
                check(False, f"{label}: the loot menu opens ({r})")
                return
            for _ in range(300):
                if (ui().get("loot") or {}).get("valid"):
                    break
                game.run(2)
            game.run(40)  # past the opening press guard
            game.press(cancel, vsyncs=3, settle=8)
            mode = (ui().get("loot") or {}).get("mode")
            check(mode == "confirm", f"{label}, Loot: {cancel.title()} asks to leave ({mode})")
            game.press(cancel, vsyncs=3, settle=8)
            mode = (ui().get("loot") or {}).get("mode")
            check(mode == "list", f"{label}, Loot: {cancel.title()} again, back to the list ({mode})")
            game.press(confirm, vsyncs=3, settle=8)
            for _ in range(450):
                if current_menu() == CLOSED and not ui().get("loot"):
                    break
                game.run(2)
            check(current_menu() == CLOSED and not ui().get("loot"),
                  f"{label}, Loot: {confirm.title()} takes the root, the screen closes ({current_menu():#x})")
            game.run(60)

        def shot(name):
            if args.out:
                game.present_screenshot(out / f"{name}.png")

        # Confirm on (from the settings file).
        check(open_menu(), "confirm on: Triangle opens the top-level menu")
        r = roles()
        check(r["context"] == "menus" and r["swap"] == 1, f"the menu: context menus, exchanged ({r})")
        shot("menu")
        game.press("circle", vsyncs=3, settle=60)
        check(current_menu() == CLOSED, f"confirm on: Circle closes the top level ({current_menu():#x})")
        status_round_trip("cross", "circle", "confirm on")
        field("circle", "cross", "confirm on, the field unchanged")

        # Both off: the original.
        r = roles(confirm_cross=False)
        check(r["confirm"] == "circle" and r["target"] == "circle" and r["attack"] == "circle",
              f"both off: the original roles ({r})")
        status_round_trip("circle", "cross", "both off")
        check(open_menu(), "both off: the menu opens")
        game.press("cross", vsyncs=3, settle=60)
        check(current_menu() == CLOSED, f"both off: Cross closes the top level ({current_menu():#x})")
        field("circle", "cross", "both off")

        # Field on alone.
        r = roles(field_cross_attack=True)
        check(r["confirm"] == "circle" and r["attack"] == "cross" and r["interact"] == "circle"
              and r["target"] == "cross", f"field on: Cross attacks, targets confirm with Cross ({r})")
        field("cross", "circle", "field on")
        status_round_trip("circle", "cross", "field on, the menus unchanged")

        # Both on: the mod's default controls.
        r = roles(confirm_cross=True)
        check(r["confirm"] == "cross" and r["attack"] == "cross" and r["target"] == "cross", f"both on ({r})")
        field("cross", "circle", "both on")
        status_round_trip("cross", "circle", "both on")
        # Targeting: drawn, Cross (the attack) opens the target list (field mode 2, _cameraMode 1;
        # with no enemy about it says so), where Circle backs out.
        game.press("cross", vsyncs=3, settle=60)
        game.press("cross", vsyncs=3, settle=40)
        r = roles()
        mode, phase = game.read(field_mode, 1)[0], game.read(camera_mode, 1)[0]
        check(r["context"] == "targeting" and r["swap"] == 1,
              f"both on, drawn: Cross opens targeting (mode {mode}, phase {phase}, {r['context']}, swap {r['swap']})")
        for _ in range(4):
            if roles()["context"] == "field":
                break
            game.press("circle", vsyncs=3, settle=60)
        r = roles()
        check(r["context"] == "field", f"Circle backs out of targeting ({r['context']})")
        game.press("circle", vsyncs=3, settle=60)
        check(drawn() == 0, f"Circle sheathes ({drawn()})")

        # The modern menus' tabs follow qol_confirm_cross (still on), then the original buttons. The
        # first chain ability learned in RAM (vs_main_actions[id] unlocked: byte 13 bit 7), so that the
        # Abilities tab shows whatever the card has learned.
        acts = sym["vs_main_actions"]
        first = game.read(sym["vs_battle_chainAbilityOffsets"], 1)[0]
        b = game.read(acts + first * 52 + 13, 1)[0]
        game.write(acts + first * 52 + 13, bytes([b | 0x80]))

        # The original Battle Abilities list (MENU2, its state in vs_battle_submenuStates[3]: 5 the
        # Chain / Defense choice, 6 the chain list): the buttons are the game's own there.
        check(open_menu(), "both on: the menu opens")
        for _ in range(10):
            if game.read(submenu, 1)[0] == BATTLE_ABILITIES:
                break
            game.press("down", vsyncs=3, settle=8)
        game.press("cross", vsyncs=3, settle=120)
        opened = current_menu()
        game.press("cross", vsyncs=3, settle=60)
        state = game.read(submenu + 3, 1)[0]
        r = roles()
        check(opened == (3 | 0x40) and state == 6 and r["context"] == "game" and r["swap"] == 0,
              f"Battle Abilities > Chain (Cross, Cross): the list, the game's own buttons "
              f"({opened:#x}, state {state}, {r['context']}, swap {r['swap']})")
        game.press("circle", vsyncs=3, settle=20)
        state = game.read(submenu + 3, 1)[0]
        check(state == 6, f"the list: Circle puts the ability on Circle and stays (state {state})")
        game.press("cross", vsyncs=3, settle=60)
        state = game.read(submenu + 3, 1)[0]
        check(state != 6, f"the list: Cross leaves it (state {state})")
        for _ in range(4):
            if current_menu() == CLOSED:
                break
            game.press("triangle", vsyncs=3, settle=90)
        check(current_menu() == CLOSED, f"Triangle closes the menu ({current_menu():#x})")

        game.call("ui_menu", modern=True)
        shot("modern")
        modern_tabs("cross", "circle", "modern, confirm on")
        loot_screen("cross", "circle", "modern, confirm on")
        roles(confirm_cross=False)
        modern_tabs("circle", "cross", "modern, confirm off")
        loot_screen("circle", "cross", "modern, confirm off")
    finally:
        game.close()

    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
