#!/usr/bin/env python3
"""Modern menus, Abilities tab (pc/src/ui/abilities_bridge.c, ui_shell.cpp,
ui_abilities_draw.cpp): button mapping with the original's effect, the
snapshot, break arts (states, why one is greyed) and casting one.

    python3 pc/tests/ui_abilities_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Needs a card whose first save is in the save room with chain and defense
abilities learned (skipped otherwise). 4:3, 960x720 window (layout units 1:1).

Parity: the same list of assignments runs in two game processes from copies of
the same card. (a) ui_menus off (set explicitly): the original Battle Abilities screen (MENU2
64.c: Triangle, the Battle Abilities command, Chain / Defense, the list row;
in its list Circle / Triangle / Square put the row on that button, Cross
back); (b) ui_menus on: the shell's Abilities tab, Chain / Defense sub-tabs,
each assignment one of three ways: the list's direct press (Circle /
Triangle / Square on a row), the button's pick list (the slot, Circle, the
row, Circle) or the mouse (a click on the slot, a click on the row). After
every assignment vs_main_settings.mappedChainAbilities[4] and
mappedDefenseAbilities[4] (8 bytes) are compared; the list includes setting,
moving to another button, replacing and toggling off (the same button
again). Both closed with Triangle at the end: the bytes again.

Shell-only checks (process b, after the parity list):
  - the lists: chain / defense entries = the actions of
    vs_battle_chainAbilityOffsets / defenseAbilityOffsets with
    vs_main_actions[id].unlocked, in that order, names non-empty, RISK cost =
    vs_main_actions[id].cost; the slots' ids = the mapped bytes;
    next_ability_kills = vs_main_battleAbilitiesPointsRequirements
    [kills.battleAbilitiesUnlocked] - kills.total
  - break arts, set up in RAM (this card has none): the equipped category's
    first art learned (unlocked bit + artsLearned), kills for the second one
    reached (pending), the third locked; a dagger art learned too (another
    category). Listed: the learned art with name and HP cost (the action's
    cost); the pending one with 0 kills to go; the locked one without a name
    (drawn "???") and kills_to_go = requirement - kills; the category's
    learned count, kills, next_kills
  - greyed, with a reason: sheathed (Circle on it is refused: the shell
    stays); the dagger art (not the equipped category); drawn with HP = the
    cost (HP too low); drawn with HP enough: usable, no reason
  - cast: weapon drawn, Circle on the art: the shell closes, D_800F4E98
    executeAbility = the art, unk2 6, executeAbilityType 5, returnState 0x42,
    then targeting (D_800F196C 2) with the menu closed (0x3F); Circle confirms
    (until targeting ends): HP drops by the cost
  - loading: a second art learned, cursor memory on; the shell reopens on it
    while MAINMENU still loads: the art says "Loading…" and a click on it is
    refused (the shell stays, D_800F4E98 untouched); usable once resident
  - cancel: that art cast, Cross on the target: the original Break Arts list
    (currentMenu 0x42) on the equipped category (menu item 10's icon) with
    the cast art's row highlighted (the list's cursor memory); View Arts there,
    the dagger's list, Cross: the shell on Abilities > Break Arts; the first
    art cast and cancelled: the equipped category's list again (not the
    dagger's: MENU1's weapon type, ROOD_PC in 30.c), row 0; Cross: the shell.
    Triangle closes (0x3F), and after all that Triangle opens and closes the
    menu normally (no stuck 0x1F)
  - mouse (RPC ui_menu pointer, layout units, the real hit tests): hover a
    rail row focuses that category, a click on an art row moves the focus to
    the arts on that row (no cast).
--out keeps screenshots (game imagery: keep it out of git)."""
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
BREAK_ARTS = 0x42  # the original Break Arts list from the top level (command 1 + 1 | 0x40)
TARGETING = 2      # D_800F196C while a break art's target is chosen
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (vs_battle_menuItems, characterState)

# vs_main_settings (main.h): cursorMemory at 1, mappedChainAbilities[4] at 12, mappedDefenseAbilities[4] at 16
CURSOR_MEMORY, MAPPED, MAPPED_BYTES = 1, 12, 8
LOADING = "Loading\u2026"  # an art's reason while MAINMENU is not resident yet
# vs_action_t: 52 bytes, cost (signed) at 3, unlocked = bit 7 of byte 13
ACTION, ACTION_COST, ACTION_UNLOCKED = 52, 3, 13
# vs_main_artsStatus_t: artsLearned[12], kills.weaponCategories[10] (u16), battleAbilitiesUnlocked, total
LEARNED, KILLS, ABILITIES_UNLOCKED = 0, 12, 32  # kills.total follows battleAbilitiesUnlocked
FIRST_ART = 184
HP = 0x18  # vs_battle_actor2.currentHP
BUTTONS = ("circle", "triangle", "square")
DAGGER = 1

# The parity list: (type, learned list position, button, the shell's way)
STEPS = [
    ("chain", 0, "circle", "list"),       # set
    ("chain", 0, "triangle", "list"),     # move to another button
    ("chain", 0, "triangle", "list"),     # the same button again: off
    ("chain", 1, "square", "pick"),
    ("chain", 2, "triangle", "mouse"),
    ("chain", 1, "circle", "list"),       # moves from Square
    ("chain", 2, "circle", "pick"),       # replaces Circle's, leaves Triangle
    ("defense", 0, "circle", "list"),
    ("defense", 2, "square", "mouse"),
    ("defense", 2, "triangle", "list"),   # move
    ("defense", 1, "circle", "pick"),     # replace
    ("defense", 1, "circle", "list"),     # off
    ("defense", 0, "square", "list"),
]

# 4:3 pointer targets (layout units; ui_abilities_draw.cpp)
SLOT_X, SLOT_Y = (128, 280, 432), 179


def row_y(i):
    return 277 + 38 * i


def rail_y(r):
    return 178 + 48 * r


def art_y(i):
    return 176 + 44 * i


RAIL_X, ROW_X, ART_X = 150, 150, 600


class Session:
    """One game process continued to the save room."""

    def __init__(self, name, out, card, env, shots):
        self.name, self.out, self.shots = name, out, shots
        self.g = Game.launch(fast=True, card=str(card), log=out / f"{name}.log", extra_env=env)
        self.sym = symbols()
        g = self.g
        g.wait_title()
        g.press("start", vsyncs=4, settle=90)
        for _ in range(3):
            g.press("circle", vsyncs=3, settle=90)
        g.run(400)

    def close(self):
        self.g.close()

    def shot(self, name):
        if self.shots:
            self.g.present_screenshot(self.out / f"{self.name}-{name}.png")

    # ---- reads ----
    def ptr(self, address):
        return int.from_bytes(self.g.read(address, PTR), "little")

    def menu(self):
        return self.g.read(self.sym["vs_battle_menuState"], 1)[0]

    def mapped(self):
        return self.g.read(self.sym["vs_main_settings"] + MAPPED, MAPPED_BYTES)

    def action(self, i):
        return self.g.read(self.sym["vs_main_actions"] + i * ACTION, ACTION)

    def actor2(self):
        actor = self.ptr(self.sym["vs_battle_characterState"])
        return int.from_bytes(self.g.read(actor + 0x3C, 4), "little")

    def hp(self):
        return struct.unpack("<h", self.g.read(self.actor2() + HP, 2))[0]

    def ui(self, **params):
        return self.g.call("ui_menu", **params)

    def ab(self):
        return self.ui().get("abilities") or {}

    # ---- waits (vsync by vsync; raise on timeout) ----
    def wait(self, pred, limit, what, step=1, quiet=False):
        for n in range(0, limit + 1, step):
            if pred():
                return n
            self.g.run(step)
        if quiet:
            return None
        raise TimeoutError(f"{self.name}: {what}: not after {limit} vsyncs (currentMenu {self.menu():#x})")

    # ---- the original menu (ui_menus off) ----
    def menu_items(self):
        """vs_battle_menuItems: {id: (state, selected)} of the live items ({} when none)."""
        p = self.ptr(self.sym["vs_battle_menuItems"])
        if not p:
            return {}
        raw = self.g.read(p, 64 * 40)
        return {i: (raw[i * 64], raw[i * 64 + 6]) for i in range(40) if raw[i * 64]}

    def wait_ready(self, want, limit=400):
        """Until item `want` is live and every item idle (vs_mainmenu_ready) for a few frames."""
        calm = 0

        def ready():
            nonlocal calm
            items = self.menu_items()
            ok = want in items and all(s <= 1 for s, _ in items.values())
            calm = calm + 1 if ok else 0
            return calm >= 3
        self.wait(ready, limit, f"menu item {want} ready", step=4)
        self.g.run(4)

    def item_icon(self, i):
        """vs_battle_menuItems[i].rowIcon (the Break Arts list's title: its category)."""
        p = self.ptr(self.sym["vs_battle_menuItems"])
        return self.g.read(p + i * 64 + 9, 1)[0] if p else None

    def ab_rows(self, cat):
        """The learned arts of category `cat` (the original list's rows before View Arts)."""
        return sum(1 for i in range(4) if self.action(FIRST_ART + (cat - 1) * 4 + i)[ACTION_UNLOCKED] & 0x80)

    def selected(self, ids):
        items = self.menu_items()
        return next((i for i in ids if i in items and items[i][1]), None)

    def nav(self, ids, target):
        """Down (lists wrap) until item `target` of `ids` is the selected one."""
        for _ in range(len(ids) + 2):
            if self.selected(ids) == target:
                return
            self.g.press("down", vsyncs=3, settle=6)
        if self.selected(ids) != target:
            raise TimeoutError(f"{self.name}: item {target} not reached (selected {self.selected(ids)})")

    def original_open(self, kind, count):
        """Triangle, Battle Abilities (command 2), Chain (row 10) / Defense (11): the list (rows 20..)."""
        g = self.g
        if self.menu() == CLOSED:
            g.press("triangle", vsyncs=3, settle=10)
            self.wait_ready(2)
            self.nav(range(2, 10), 2)
            g.press("circle", vsyncs=3, settle=10)
        self.wait_ready(11)
        self.nav([10, 11], 10 if kind == "chain" else 11)
        g.press("circle", vsyncs=3, settle=10)
        self.wait_ready(20)
        self.g.run(10)  # MENU2's list: waitReady, then abilityMenu takes presses

    def original_step(self, kind, entry, button, count):
        g = self.g
        self.nav(range(20, 20 + count), 20 + entry)
        g.press(button, vsyncs=3, settle=6)

    def original_back(self):
        """Cross out of the list: the Chain / Defense rows again."""
        self.g.press("cross", vsyncs=3, settle=10)
        self.wait_ready(11)

    def original_close(self):
        g = self.g
        for _ in range(6):
            if self.menu() == CLOSED:
                return
            g.press("triangle", vsyncs=3, settle=10)
            self.wait(lambda: self.menu() == CLOSED, 120, "the original menu closes", step=4, quiet=True)
        if self.menu() != CLOSED:
            raise TimeoutError(f"{self.name}: the menu did not close ({self.menu():#x})")

    # ---- the shell (ui_menus on) ----
    def shell_open(self, sub):
        g = self.g
        for _ in range(6):  # Triangle is ignored while an action (a break art) still runs
            if self.ui().get("visible") and self.menu() == 0:
                break
            g.press("triangle", vsyncs=3, settle=4)
            if self.wait(lambda: self.ui().get("visible") and self.menu() == 0, 120, "", step=2,
                         quiet=True) is not None:
                g.run(6)  # the shell ignores input for its first ticks (the opening press is not its)
        self.wait(lambda: self.ui().get("visible") and self.menu() == 0, 2, "the shell opens")
        for _ in range(8):
            if self.ui().get("tab") == "abilities":
                break
            g.press("r1", vsyncs=3, settle=4)
        self.wait(lambda: self.ui().get("tab") == "abilities", 20, "the Abilities tab")
        for _ in range(4):
            if self.ab().get("sub") == sub:
                break
            g.press("r2", vsyncs=3, settle=4)
        self.wait(lambda: self.ab().get("sub") == sub, 20, f"the {sub} sub-tab")

    def shell_mode(self, mode):
        """Chain / Defense: SLOTS (Cross from a list) or LIST (Down from the slots)."""
        g = self.g
        if self.ab()["mode"] != "slots":
            g.press("cross", vsyncs=3, settle=4)
            self.wait(lambda: self.ab()["mode"] == "slots", 20, "the slots")
        if mode == "list":
            g.press("down", vsyncs=3, settle=4)
            self.wait(lambda: self.ab()["mode"] == "list", 20, "the list")

    def shell_cursor(self, entry):
        for _ in range(16):
            if self.ab()["cursor"] == entry:
                return
            self.g.press("down", vsyncs=3, settle=4)
        raise TimeoutError(f"{self.name}: list row {entry} not reached (cursor {self.ab()['cursor']})")

    def shell_slot(self, button):
        for _ in range(4):
            if self.ab()["slot"] == button:
                return
            self.g.press("right", vsyncs=3, settle=4)
        raise TimeoutError(f"{self.name}: slot {button} not reached ({self.ab()['slot']})")

    def pointer(self, x, y, click=False):
        self.ui(pointer={"x": x, "y": y, "click": bool(click)})
        self.g.run(4)

    def shell_step(self, kind, entry, button, how):
        g = self.g
        self.shell_open(kind)
        before = self.mapped()
        if how == "list":
            self.shell_mode("list")
            self.shell_cursor(entry)
            g.press(button, vsyncs=3, settle=4)
        elif how == "pick":
            self.shell_mode("slots")
            self.shell_slot(button)
            g.press("circle", vsyncs=3, settle=4)
            self.wait(lambda: self.ab()["mode"] == "pick", 20, "the pick list")
            self.shell_cursor(entry)
            g.press("circle", vsyncs=3, settle=4)
            self.wait(lambda: self.ab()["mode"] == "slots", 20, "the slots after the pick")
        else:  # mouse: a click on the slot (its pick list), a click on the row
            self.shell_mode("slots")
            self.pointer(SLOT_X[BUTTONS.index(button)], SLOT_Y, click=True)
            self.wait(lambda: self.ab()["mode"] == "pick", 20, "the pick list (click)")
            self.pointer(ROW_X, row_y(entry), click=True)
            self.wait(lambda: self.ab()["mode"] == "slots", 20, "the slots after the click")
            self.pointer(-1, -1)
        self.wait(lambda: self.mapped() != before, 30, "the mapping changes", quiet=True)
        g.run(2)

    def shell_close(self):
        g = self.g
        for _ in range(6):
            if self.menu() == CLOSED and not self.ui().get("visible"):
                return
            if self.ui().get("visible") and (self.ab().get("mode") == "list"):
                g.press("cross", vsyncs=3, settle=4)  # the list takes Triangle (a mapping)
                continue
            g.press("triangle", vsyncs=3, settle=4)
            self.wait(lambda: self.menu() == CLOSED and not self.ui().get("visible"), 200,
                      "the shell closes", step=4, quiet=True)
        if self.menu() != CLOSED:
            raise TimeoutError(f"{self.name}: the shell did not close ({self.menu():#x})")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_abilities"
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    if not os.environ.get("ROOD_OUT"):  # Continue reads the card only with ROOD_OUT set (run.py sets it)
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    # (a) the original Battle Abilities screen
    shutil.copyfile(args.card, card)
    a = Session("original", out, card, env, bool(args.out))
    try:
        a.g.call("ui_menu", modern=False)
        state = a.g.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1
        counts = learned_counts(a)
        if min(counts.values()) < 3:
            print(f"SKIP: the card needs three chain and three defense abilities learned ({counts})")
            return 77
        dumps_a = [a.mapped()]
        kind = None
        for step in STEPS:
            k, entry, button, _ = step
            if k != kind:
                if kind is not None:
                    a.original_back()
                a.original_open(k, counts[k])
                kind = k
            a.original_step(k, entry, button, counts[k])
            a.wait(lambda: a.mapped() != dumps_a[-1], 30, "", quiet=True)
            dumps_a.append(a.mapped())
        a.shot("original-defense")
        a.original_back()
        a.original_close()
        a.g.run(10)
        dumps_a.append(a.mapped())
        print("original:", [d.hex() for d in dumps_a])
    finally:
        a.close()

    # (b) the shell, a new process from the same card
    shutil.copyfile(args.card, card)
    b = Session("shell", out, card, env, bool(args.out))
    try:
        b.g.call("ui_menu", modern=True)
        check(b.mapped() == dumps_a[0], f"both processes start equal ({b.mapped().hex()})")
        for n, step in enumerate(STEPS, 1):
            b.shell_step(*step)
            got, want = b.mapped(), dumps_a[n]
            check(got == want, f"parity: {step[0]} row {step[1]} -> {step[2]} ({step[3]}): "
                               f"{got.hex()} (original {want.hex()})")
            if step[3] == "mouse":
                b.shot(f"mouse-{step[0]}")
        b.shot("shell-defense")
        b.shell_close()
        b.g.run(10)
        check(b.mapped() == dumps_a[-1], f"parity: closed: {b.mapped().hex()} (original {dumps_a[-1].hex()})")
        changes = len({d for d in dumps_a})
        check(changes >= 10, f"the list changed the mapping at every step ({changes} distinct states)")

        lists_checks(b, check)
        arts_checks(b, check)
    finally:
        b.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


def learned(s, kind):
    """The learned actions of a type, in the game's list order (MENU2)."""
    table = s.g.read(s.sym[f"vs_battle_{kind}AbilityOffsets"], 14)
    return [i for i in table if s.action(i)[ACTION_UNLOCKED] & 0x80]


def learned_counts(s):
    return {k: len(learned(s, k)) for k in ("chain", "defense")}


def s8(v):
    return v - 256 if v > 127 else v


def lists_checks(b, check):
    b.shell_open("chain")
    ab = b.ab()
    mapped = b.mapped()
    for t, kind in enumerate(("chain", "defense")):
        ids = learned(b, kind)
        entries = ab[kind]["entries"]
        got = [e["id"] for e in entries]
        costs = [s8(b.action(i)[ACTION_COST]) for i in ids]
        check(got == ids and all(e["name"].strip() for e in entries) and [e["cost"] for e in entries] == costs,
              f"{kind}: the learned actions {[e['name'] for e in entries]} ids {got} (RAM {ids}), "
              f"RISK costs {[e['cost'] for e in entries]} (actions {costs})")
        slots = [x["id"] for x in ab[kind]["slots"]]
        check(slots == list(mapped[t * 4:t * 4 + 3]), f"{kind}: the slots {slots} = the mapped bytes")
    status = b.sym["vs_main_artsStatus"]
    unlocked, total = struct.unpack("<HH", b.g.read(status + ABILITIES_UNLOCKED, 4))
    if unlocked < 22:
        req = struct.unpack("<H", b.g.read(b.sym["vs_main_battleAbilitiesPointsRequirements"] + 2 * unlocked, 2))[0]
        want = max(0, req - total)
    else:
        want = -1
    check(ab["next_ability_kills"] == want,
          f"next ability in {ab['next_ability_kills']} kills (requirement - kills: {want})")


def arts_checks(b, check):
    g = b.g
    ab = b.ab()
    b.shell_close()  # the menu's commands (menuFlags: Break Arts) are set when it opens
    cat = ab["equipped_category"]
    c = cat % 10
    status, acts = b.sym["vs_main_artsStatus"], b.sym["vs_main_actions"]
    req = struct.unpack("<4H", g.read(b.sym["vs_main_artsPointsRequirements"] + c * 8, 8))
    art0 = FIRST_ART + (cat - 1) * 4
    dagger0 = FIRST_ART + (DAGGER - 1) * 4
    kills = req[1] + 3 if req[1] + 3 < req[2] else req[1]
    # the equipped category's first art learned, the second pending, the dagger's first learned
    for art in (art0, dagger0):
        v = g.read(acts + art * ACTION + ACTION_UNLOCKED, 1)[0]
        g.write(acts + art * ACTION + ACTION_UNLOCKED, bytes([v | 0x80]))
    g.write(status + LEARNED + c, bytes([1]))
    g.write(status + LEARNED + DAGGER, bytes([1]))
    g.write(status + KILLS + 2 * c, struct.pack("<H", kills))
    cost = s8(b.action(art0)[ACTION_COST])

    b.shell_open("arts")
    g.run(2)
    ab = b.ab()
    k = ab["categories"][cat - 1]
    arts = k["arts"]
    check(ab["order"][0] == cat, f"Break Arts: the rail starts with the equipped category {cat} ({ab['order']})")
    check(k["learned"] == 1 and k["kills"] == kills and k["next_kills"] == max(0, req[1] - kills),
          f"category '{k['name']}': learned {k['learned']}, kills {k['kills']} ({kills}), "
          f"next in {k['next_kills']}")
    check(arts[0]["id"] == art0 and arts[0]["state"] == "learned" and arts[0]["name"].strip()
          and arts[0]["cost"] == cost and arts[0]["help"],
          f"learned art '{arts[0]['name']}' id {arts[0]['id']} ({art0}), HP cost {arts[0]['cost']} (action {cost})")
    check(arts[1]["state"] == "pending" and arts[1]["kills_to_go"] == 0 and not arts[1]["name"],
          f"second art pending (kills {kills} >= {req[1]}): {arts[1]['state']}, to go {arts[1]['kills_to_go']}")
    check(arts[2]["state"] == "locked" and arts[2]["kills_to_go"] == req[2] - kills and not arts[2]["name"],
          f"third art locked ('???'): kills_to_go {arts[2]['kills_to_go']} = {req[2]} - {kills}")
    sheathed = arts[0]
    check(not ab["weapon_drawn"] and not sheathed["usable"] and "draw" in sheathed["reason"].lower(),
          f"sheathed: not usable, '{sheathed['reason']}'")
    # mouse: hover the third rail row, a click on art row 1 (focus only), then the equipped category's art 0
    b.pointer(RAIL_X, rail_y(2))
    ab = b.ab()
    check(ab["mode"] == "categories" and ab["category"] == ab["order"][2],
          f"mouse: hover rail row 2 -> category {ab['category']} ({ab['order'][2]})")
    b.pointer(RAIL_X, rail_y(0))
    b.pointer(ART_X, art_y(1), click=True)
    ab = b.ab()
    check(ab["mode"] == "arts" and ab["art"] == 1 and ab["category"] == cat and b.ui()["visible"],
          f"mouse: click art row 1 -> arts, art {ab['art']}, category {ab['category']} (no cast)")
    b.shot("arts-pending")
    b.pointer(ART_X, art_y(0))
    b.pointer(-1, -1)
    ab = b.ab()
    check(ab["art"] == 0, f"mouse: hover art row 0 -> art {ab['art']}")
    # Circle on the sheathed art: refused, the shell stays
    g.press("circle", vsyncs=3, settle=30)
    check(b.ui()["visible"] and b.menu() == 0, f"sheathed: Circle refused (visible, currentMenu {b.menu():#x})")
    b.shot("arts-sheathed")
    b.shell_close()

    # draw the weapon (Circle in the field)
    g.press("circle", vsyncs=3, settle=60)
    b.shell_open("arts")
    ab = b.ab()
    if not check(ab["weapon_drawn"], "the weapon is drawn"):
        return
    dagger = b.ab()["categories"][DAGGER - 1]["arts"][0]
    check(cat != DAGGER and dagger["state"] == "learned" and not dagger["usable"]
          and dagger["reason"] == "Needs a dagger in hand.",
          f"drawn, another category's art '{dagger['name']}': not usable, '{dagger['reason']}'")
    # HP too low: HP = the cost
    hp0 = b.hp()
    g.write(b.actor2() + HP, struct.pack("<h", cost))
    g.run(2)
    art = b.ab()["categories"][cat - 1]["arts"][0]
    check(not art["usable"] and "hp" in art["reason"].lower(), f"HP {cost} = the cost: not usable, '{art['reason']}'")
    g.write(b.actor2() + HP, struct.pack("<h", hp0))
    g.run(2)
    art = b.ab()["categories"][cat - 1]["arts"][0]
    check(art["usable"] and not art["reason"], f"drawn, HP {hp0}: usable ('{art['reason']}')")
    b.shot("arts-drawn")

    # cast and confirm
    cast(b, check, art0)
    hp1 = b.hp()
    F = b.sym["D_800F196C"]
    for _ in range(6):  # the target cursor takes Circle once it is up: until targeting ends
        g.press("circle", vsyncs=3, settle=4)
        if b.wait(lambda: g.read(F, 1)[0] != TARGETING, 40, "", step=2, quiet=True) is not None:
            break
    b.wait(lambda: b.hp() != hp1, 60, "", step=2, quiet=True)
    g.run(4)
    check(b.hp() == hp1 - cost, f"confirmed: HP {hp1} -> {b.hp()} (cost {cost})")
    g.run(200)  # the art plays
    b.shot("confirmed")

    # a second art of the equipped category learned (the cancel's list has two rows and View Arts)
    art1 = art0 + 1
    v = g.read(acts + art1 * ACTION + ACTION_UNLOCKED, 1)[0]
    g.write(acts + art1 * ACTION + ACTION_UNLOCKED, bytes([v | 0x80]))
    g.write(status + LEARNED + c, bytes([2]))
    g.write(b.sym["vs_main_settings"] + CURSOR_MEMORY, bytes([1]))  # the shell reopens where it was

    # the shell reopens on the art: refused while MAINMENU loads ("Loading…"), cast once it is resident
    b.shell_open("arts")
    if b.ab()["mode"] != "arts":
        g.press("circle", vsyncs=3, settle=4)
    for _ in range(4):
        if b.ab()["art"] == 1:
            break
        g.press("down", vsyncs=3, settle=4)
    check(b.ab()["mode"] == "arts" and b.ab()["art"] == 1, f"focus on art 1 ({b.ab()['mode']}, {b.ab()['art']})")
    b.shell_close()
    g.run(30)
    g.call("input", buttons=["triangle"], vsyncs=2)
    loading = None
    for n in range(60):
        g.run(1)
        s = b.ui()
        x = ((s.get("abilities") or {}).get("categories") or [{}] * cat)[cat - 1].get("arts", [{}, {}])[1]
        if s.get("visible") and b.menu() == 0 and s.get("abilities", {}).get("art") == 1:
            if x.get("reason") == LOADING:
                loading = n
                break
            if x.get("usable"):
                break
    if check(loading is not None and not x["usable"], f"reopened, MAINMENU loading: art 1 not usable, "
                                                      f"'{x.get('reason')}' (vsync {loading})"):
        before = g.read(b.sym["D_800F4E98"], 8)
        b.ui(pointer={"x": ART_X, "y": art_y(1), "click": True})  # the next drawn frame's click: a confirm
        g.run(1)
        still = b.ab()["categories"][cat - 1]["arts"][1]
        g.run(3)
        check(still["reason"] == LOADING and b.ui()["visible"] and b.menu() == 0
              and g.read(b.sym["D_800F4E98"], 8) == before,
              f"cast while loading: refused (visible {b.ui()['visible']}, currentMenu {b.menu():#x}, "
              f"reason then '{still['reason']}')")
        b.pointer(-1, -1)
    b.wait(lambda: b.ab()["categories"][cat - 1]["arts"][1]["usable"], 120, "art 1 usable once MAINMENU is resident")

    # cast art 1 and cancel: the original Break Arts list on the equipped category, art 1 highlighted
    cancel(b, check, art1, 1, cat, "cast after loading")
    # View Arts there, another category (the dagger's), Cross: the shell
    rows = range(20, 23)  # art 0, art 1, View Arts
    b.nav(rows, 22)
    g.press("circle", vsyncs=3, settle=10)
    b.wait_ready(20)
    b.nav(range(20, 30), 20)  # the dagger row
    g.press("circle", vsyncs=3, settle=10)
    b.wait_ready(20)
    icon = b.item_icon(10)
    check(icon == DAGGER, f"View Arts: the dagger's list (item 10 icon {icon})")
    back_to_shell(b, check)
    # cast art 0 and cancel: the list is the equipped category's again, art 0 highlighted
    cancel(b, check, art0, 0, cat, "after View Arts")
    back_to_shell(b, check)
    b.shell_close()
    check(b.menu() == CLOSED, f"Triangle closes ({b.menu():#x})")

    # the menu opens and closes normally afterwards
    g.run(30)
    g.press("triangle", vsyncs=3, settle=4)
    n = b.wait(lambda: b.ui()["visible"] and b.menu() == 0, 200, "", step=2, quiet=True)
    g.run(10)
    g.press("cross" if b.ab().get("mode") == "list" else "triangle", vsyncs=3, settle=4)
    if b.ui().get("visible"):
        g.press("triangle", vsyncs=3, settle=4)
    m = b.wait(lambda: b.menu() == CLOSED and not b.ui()["visible"], 300, "", step=4, quiet=True)
    check(n is not None and m is not None, f"afterwards Triangle opens ({n} vsyncs) and closes ({m} vsyncs; "
                                           f"currentMenu {b.menu():#x})")


def cancel(b, check, art, row, cat, what):
    """Cast `art` (row `row` of the list), Cross on the target: the original Break Arts list
    (currentMenu 0x42) on category `cat` with that row highlighted."""
    g = b.g
    b.shell_open("arts")
    cast(b, check, art, row)
    hp1 = b.hp()
    n = None
    for _ in range(6):  # the target cursor takes Cross once it is up
        g.press("cross", vsyncs=3, settle=4)
        n = b.wait(lambda: b.menu() == BREAK_ARTS, 40, "", step=2, quiet=True)
        if n is not None:
            break
    check(b.hp() == hp1, f"{what}: cancelled, no HP spent ({hp1} -> {b.hp()})")
    check(n is not None and not b.ui()["visible"], f"{what}: target cancelled: the original Break Arts list "
                                                   f"(currentMenu {b.menu():#x})")
    b.wait_ready(20 + row)
    rows = list(range(20, 20 + b.ab_rows(cat) + 1))
    icon, sel = b.item_icon(10), b.selected(rows)
    check(icon == cat and sel == 20 + row, f"{what}: the list is category {icon} ({cat}), row {sel} highlighted "
                                           f"({20 + row}: the cast art)")
    b.shot(f"cancel-original-{row}")


def back_to_shell(b, check):
    """Cross in the original Break Arts list: the shell on Abilities > Break Arts."""
    g = b.g
    for _ in range(6):  # Cross before MENU1 has drawn in does nothing: again until it leaves
        g.press("cross", vsyncs=3, settle=4)
        if b.wait(lambda: b.menu() != BREAK_ARTS, 90, "", step=2, quiet=True) is not None:
            break
    n = b.wait(lambda: b.ui()["visible"] and b.menu() == 0, 300, "the shell again", step=2, quiet=True)
    s = b.ui()
    check(n is not None and s["tab"] == "abilities" and (s.get("abilities") or {}).get("sub") == "arts",
          f"Cross: the shell on {s['tab']} > {(s.get('abilities') or {}).get('sub')} (currentMenu {b.menu():#x})")


def cast(b, check, art, row=0):
    """Abilities > Break Arts, the equipped category's art `row`, Circle: the menu closes into targeting."""
    g = b.g
    if b.ab()["mode"] != "arts":
        g.press("circle", vsyncs=3, settle=4)
        b.wait(lambda: b.ab()["mode"] == "arts", 20, "the arts")
    for _ in range(4):
        if b.ab()["art"] == row:
            break
        g.press("down", vsyncs=3, settle=4)
    ab = b.ab()
    x = ab["categories"][ab["category"] - 1]["arts"][row]
    if not check(x["id"] == art and x["usable"], f"the art {x['id']} is usable before the cast "
                                                 f"('{x['reason']}', drawn {ab['weapon_drawn']}, HP {ab['hp']})"):
        b.shot("not-usable")
        raise RuntimeError("the cast cannot start")
    F = b.sym["D_800F196C"]
    before = g.read(F, 1)[0]
    g.press("circle", vsyncs=3, settle=1)
    b.wait(lambda: not b.ui()["visible"], 30, "the shell closes for the cast")
    e = b.g.read(b.sym["D_800F4E98"], 8)
    ms = b.g.read(b.sym["vs_battle_menuState"], 3)
    unk2, ability = struct.unpack("<hi", e[2:8])
    check(ability == art and unk2 == 6 and ms[2] == 5 and ms[1] == BREAK_ARTS,
          f"cast: D_800F4E98 executeAbility {ability} ({art}), unk2 {unk2}, executeAbilityType {ms[2]}, "
          f"returnState {ms[1]:#x}")
    n = b.wait(lambda: g.read(F, 1)[0] == TARGETING and b.menu() == CLOSED, 300, "", step=2, quiet=True)
    check(n is not None, f"targeting (D_800F196C {before} -> {g.read(F, 1)[0]}), the menu closed ({b.menu():#x})")
    g.run(20)
    b.shot("targeting")


if __name__ == "__main__":
    sys.exit(main())
