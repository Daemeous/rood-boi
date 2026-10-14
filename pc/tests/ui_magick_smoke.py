#!/usr/bin/env python3
"""Modern menus, Magick tab (pc/src/ui/magick_bridge.c, ui_shell.cpp,
ui_magick_draw.cpp): the snapshot, casting a spell through the shell (the
original MENU0's hand-off), a cancelled target, the LV picker, greyed spells
and Teleport.

    python3 pc/tests/ui_magick_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Needs a card whose first save is in the save room (skipped otherwise). 4:3,
960x720 window (layout units 1:1). Both processes get the same RAM set-up
after Continue: the Magick command on (vs_main_settings.menuFlags bit 0), Heal
(Shaman), Herakles (Sorcerer), a Warlock spell, Explosion LV1-LV3 and
Thunderburst LV1-LV2 learned (vs_main_actions[id].unlocked; their higher
levels not), MP full.

Parity, a non-levelled spell (Heal), two game processes from copies of the
same card: (a) ui_menus off: the original MENU0 (Triangle, Magick, Shaman,
the Heal row, Circle); (b) ui_menus on: the shell's Magick > Shaman, Heal,
Circle. Once targeting is up (D_800F196C 2, currentMenu 0x3F) the hand-off is
compared: D_800F4E98 (8 bytes), vs_battle_menuState currentMenu / returnState
/ executeAbilityType, vs_battle_submenuStates[0], D_800F4EE8.cursorMemories
(52 bytes). (The shell's own flag, a cancelled target's way back, lives in
the bridge, not in game RAM.) Circle on the target: the cast completes, MP
drops by vs_main_actions[Heal].cost, in both.

Shell-only checks (process b):
  - snapshot = RAM: schools unlocked (a spell of its vs_battle_<school>SpellIds
    learned), each list = the learned ids of that table in order, MP =
    vs_main_actions[id].cost, names non-empty, levels: Explosion 3 (LV1-LV3
    learned), Thunderburst 2, with ids base + 0..n-1 and their costs, the
    others 1; MP / max
  - cancel: Heal cast with a click on its row (mouse: focus + confirm), Cross
    on the target: the shell on Magick > Shaman, Heal focused, no MENU0
    screen (0x41), no MP spent
  - the original screen (Square on the shell's Magick): MENU0 (0x41), Shaman,
    Heal, Circle: MENU0's hand-off; Cross on the target: MENU0 again, not the
    shell (the shell's cancel flag is only its own hand-off's)
  - Explosion LV2 via Right, then via a click on its LV2 chip: the hand-off
    id base + 1 (MENU0's LV picker memories: level 1 of 3), Cross: the shell
    on the same row and level
  - LV3 not affordable (MP = LV2's cost): greyed "Not enough MP.", Circle
    refused (the shell stays, D_800F4E98 untouched)
  - the LV picker's level is the game's (cursorMemories[12]): Explosion LV3
    cast and cancelled, the menu closed and opened again: LV3 shown; a hover
    on Thunderburst (2 levels): LV2, written back to [12] (MENU0's
    levelledSpellInit clamp), so Explosion then shows LV2
  - MP 0 when the menu opens (D_800F4EA0 0x20): every spell greyed "Not
    enough MP.", Circle refused
  - silence (vs_battle_actor2.statuses 0x1000): every spell greyed with the
    silence reason, Circle refused
  - mouse: hover a spell row focuses it, a sub-tab click switches the school
  - Teleport (vs_main_stateFlags.unkB5 1, savePointStates set in RAM): costs
    = MENU0 _getTeleportCost re-implemented here from the tables in
    src/MENU/MENU0.PRG/84.c and the room id (vs_main_locationZoneMapIds), the
    circle the player stands on marked current (and focused), a state-2
    (spirit paling) circle disabled; Circle on another circle: unk2 0xA,
    executeAbility = MENU0's packing (_teleportMenu's return from the save
    point data + 1, less vs_menu0_exec's 1, re-done here), the room changes
    and MP drops by the cost.
--out keeps screenshots (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import re
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
MENU0 = 0x41       # currentMenu while the original Magick screen runs from the top level
TARGETING = 2      # D_800F196C while a spell's target is chosen
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (vs_battle_menuItems, characterState)

# vs_action_t: 52 bytes, cost (signed) at 3, unlocked = bit 7 of byte 13
ACTION, ACTION_COST, ACTION_UNLOCKED = 52, 3, 13
MENU_FLAGS = 2                 # vs_main_settings.menuFlags (bit 0: the Magick command)
MP, MP_MAX = 0x1C, 0x1E        # vs_battle_actor2 currentMP / maxMP
STATUSES, SILENCE = 0x948, 0x1000
UNK_B5, SAVE_POINTS = 0xB5, 0x156  # vs_main_stateFlags.unkB5 / savePointStates[48]
SCHOOLS = (("warlock", 18), ("shaman", 6), ("sorcerer", 18), ("enchanter", 8))
HEAL, HERAKLES, WARLOCK_SPELL, EXPLOSION, THUNDERBURST = 96, 60, 0x61, 0x6A, 0x6E
LEARN = (HEAL, HERAKLES, WARLOCK_SPELL, EXPLOSION, EXPLOSION + 1, EXPLOSION + 2, THUNDERBURST, THUNDERBURST + 1)
UNLEARN = (EXPLOSION + 3, THUNDERBURST + 2, THUNDERBURST + 3)
LEVELS = {EXPLOSION: 3, THUNDERBURST: 2}
NO_MP = 0x20       # D_800F4EA0: MP 0 when the menu opened (BATTLE func_800CABE0)
SHAMAN_ITEM = 11   # MENU0's school rows: items 10 (Warlock) .. 14 (Teleport); spells from item 20
LEVEL_MEMORY = 12  # MENU0's LV picker: cursorMemories[12] the level, [13] how many
SILENCED = "Silenced"
POOR = "Not enough MP."

# 4:3 pointer targets (layout units; ui_magick_draw.cpp): list rows from y 160, 48 high
ROW_X, SUB_Y, FIRST_SUB_X = 150, 110, 70
CHIP_X = (381, 419, 457)  # LV1..LV3 chips (the list's widest levelled spell has 3)


def row_y(i):
    return 160 + 48 * i + 24


def s8(v):
    return v - 256 if v > 127 else v


def menu0_tables():
    """_savePointData and savePointCosts from the decompiled MENU0 (84.c)."""
    text = (ROOT / "src/MENU/MENU0.PRG/84.c").read_text()
    data = re.search(r"_savePointData\[\]\s*=\s*\{([^}]*)\}", text).group(1)
    costs = re.search(r"savePointCosts\[\]\s*=\s*\{([^}]*)\}", text).group(1)
    return [int(x, 0) for x in data.replace("\n", " ").split(",") if x.strip()], \
           [int(x, 0) for x in costs.replace("\n", " ").split(",") if x.strip()]


def teleport_cost(data, costs, room, target):
    """MENU0 _getTeleportCost."""
    i = next((i for i in range(48) if (data[i] & 0xFFFF) == room), 48)
    here = i
    if target < i:
        i, target = target, here
    return 15 + sum(costs[j] for j in range(i, target))


def teleport_packed(data, point, cost):
    """MENU0 vs_menu0_exec teleportMenu: executeAbility = _teleportMenu's return - 1, the
    return built (15AC.c returnIfReady) from i = _savePointData[point] + 1."""
    i = data[point] + 1
    v0 = i & 0x1FF                 # (rowCount & ~0x1FF) | v1: the stale high bits are masked below
    v0 &= ~0x3E00
    v0 |= (i >> 7) & 0x3E00
    v0 &= 0x3FFF
    returned = v0 | ((i >> 0x18) & 0x1F) << 0xE | cost << 0x13
    return returned - 1


class Session:
    """One game process continued to the save room, the spells set up."""

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

    def prepare(self):
        g, sym = self.g, self.sym
        f = sym["vs_main_settings"] + MENU_FLAGS
        g.write(f, struct.pack("<H", struct.unpack("<H", g.read(f, 2))[0] | 1))
        for i in LEARN:
            self.learn(i)
        for i in UNLEARN:
            a = self.sym["vs_main_actions"] + i * ACTION + ACTION_UNLOCKED
            g.write(a, bytes([g.read(a, 1)[0] & 0x7F]))
        a = self.actor2()
        g.write(a + MP, g.read(a + MP_MAX, 2))
        g.run(2)

    # ---- reads ----
    def ptr(self, address):
        return int.from_bytes(self.g.read(address, PTR), "little")

    def menu(self):
        return self.g.read(self.sym["vs_battle_menuState"], 1)[0]

    def action(self, i):
        return self.g.read(self.sym["vs_main_actions"] + i * ACTION, ACTION)

    def cost(self, i):
        return s8(self.action(i)[ACTION_COST])

    def learned(self, i):
        return bool(self.action(i)[ACTION_UNLOCKED] & 0x80)

    def learn(self, i):
        a = self.sym["vs_main_actions"] + i * ACTION + ACTION_UNLOCKED
        self.g.write(a, bytes([self.g.read(a, 1)[0] | 0x80]))

    def actor2(self):
        actor = self.ptr(self.sym["vs_battle_characterState"])
        return int.from_bytes(self.g.read(actor + 0x3C, 4), "little")

    def mp(self):
        return struct.unpack("<h", self.g.read(self.actor2() + MP, 2))[0]

    def set_mp(self, v):
        self.g.write(self.actor2() + MP, struct.pack("<h", v))
        self.g.run(2)

    def targeting(self):
        return self.g.read(self.sym["D_800F196C"], 1)[0] == TARGETING

    def handoff(self):
        """The game state a spell's hand-off writes (compared between the processes)."""
        g, sym = self.g, self.sym
        ms = g.read(sym["vs_battle_menuState"], 3)
        return {"D_800F4E98": g.read(sym["D_800F4E98"], 8).hex(),
                "currentMenu": ms[0], "returnState": ms[1], "executeAbilityType": ms[2],
                "submenuStates[0]": g.read(sym["vs_battle_submenuStates"], 1)[0],
                "cursorMemories": g.read(sym["D_800F4EE8"], 52).hex()}

    def exec_fields(self):
        e = self.g.read(self.sym["D_800F4E98"], 8)
        ms = self.g.read(self.sym["vs_battle_menuState"], 3)
        unk2, ability = struct.unpack("<hi", e[2:8])
        return unk2, ability, ms[2], ms[1]

    def ui(self, **params):
        return self.g.call("ui_menu", **params)

    def mg(self):
        return self.ui().get("magick") or {}

    # ---- waits (raise on timeout unless quiet) ----
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
        p = self.ptr(self.sym["vs_battle_menuItems"])
        if not p:
            return {}
        raw = self.g.read(p, 64 * 40)
        return {i: (raw[i * 64], raw[i * 64 + 6]) for i in range(40) if raw[i * 64]}

    def wait_ready(self, want, limit=400):
        calm = 0

        def ready():
            nonlocal calm
            items = self.menu_items()
            ok = want in items and all(s <= 1 for s, _ in items.values())
            calm = calm + 1 if ok else 0
            return calm >= 3
        self.wait(ready, limit, f"menu item {want} ready", step=4)
        self.g.run(4)

    def selected(self, ids):
        items = self.menu_items()
        return next((i for i in ids if i in items and items[i][1]), None)

    def nav(self, ids, target):
        for _ in range(len(ids) + 2):
            if self.selected(ids) == target:
                return
            self.g.press("down", vsyncs=3, settle=6)
        if self.selected(ids) != target:
            raise TimeoutError(f"{self.name}: item {target} not reached (selected {self.selected(ids)})")

    # ---- the shell (ui_menus on) ----
    def shell_open(self, sub=None):
        g = self.g
        for _ in range(6):  # Triangle is ignored while an action still runs
            if self.ui().get("visible") and self.menu() == 0:
                break
            g.press("triangle", vsyncs=3, settle=4)
            if self.wait(lambda: self.ui().get("visible") and self.menu() == 0, 120, "", step=2,
                         quiet=True) is not None:
                g.run(6)  # the shell ignores input for its first ticks
        self.wait(lambda: self.ui().get("visible") and self.menu() == 0, 2, "the shell opens")
        for _ in range(8):
            if self.ui().get("tab") == "magick":
                break
            g.press("r1", vsyncs=3, settle=4)
        self.wait(lambda: self.ui().get("tab") == "magick", 20, "the Magick tab")
        if sub:
            for _ in range(6):
                if self.mg().get("sub") == sub:
                    break
                g.press("r2", vsyncs=3, settle=4)
            self.wait(lambda: self.mg().get("sub") == sub, 20, f"the {sub} sub-tab")
        g.run(2)

    def shell_row(self, row):
        for _ in range(24):
            if self.mg()["cursor"] == row:
                return
            self.g.press("down", vsyncs=3, settle=4)
        raise TimeoutError(f"{self.name}: row {row} not reached (cursor {self.mg()['cursor']})")

    def shell_level(self, level):
        for _ in range(6):
            m = self.mg()
            if m["level"] == level:
                return
            self.g.press("right" if m["level"] < level else "left", vsyncs=3, settle=4)
        raise TimeoutError(f"{self.name}: level {level} not reached ({self.mg()['level']})")

    def pointer(self, x, y, click=False):
        self.ui(pointer={"x": x, "y": y, "click": bool(click)})
        self.g.run(4)

    def shell_close(self):
        g = self.g
        for _ in range(6):
            if self.menu() == CLOSED and not self.ui().get("visible"):
                return
            g.press("triangle", vsyncs=3, settle=4)
            self.wait(lambda: self.menu() == CLOSED and not self.ui().get("visible"), 200,
                      "the shell closes", step=4, quiet=True)
        if self.menu() != CLOSED:
            raise TimeoutError(f"{self.name}: the shell did not close ({self.menu():#x})")

    def wait_targeting(self):
        return self.wait(lambda: self.targeting() and self.menu() == CLOSED, 300, "", step=2, quiet=True)

    def press_until(self, button, done, tries=6, limit=40):
        """Press `button` (the target cursor takes presses once it is up) until done()."""
        for _ in range(tries):
            self.g.press(button, vsyncs=3, settle=4)
            n = self.wait(done, limit, "", step=2, quiet=True)
            if n is not None:
                return n
        return None

    def confirm(self, check, cost, what):
        """Circle on the target until targeting ends: MP drops by the cost (the step where it
        goes down: MP regenerates meanwhile)."""
        mp1 = self.mp()
        last, drop = [mp1], []

        def dropped():
            v = self.mp()
            if v < last[0]:
                drop.append(last[0] - v)
            last[0] = v
            return bool(drop)
        self.press_until("circle", lambda: not self.targeting() or dropped())
        self.wait(dropped, 120, "", quiet=True)
        check(drop == [cost], f"{self.name}: {what}: MP dropped by {drop} (cost {cost}; {mp1} -> {self.mp()})")
        self.g.run(200)  # the spell plays


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_magick"
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

    # (a) the original MENU0: Heal
    shutil.copyfile(args.card, card)
    a = Session("original", out, card, env, bool(args.out))
    try:
        a.g.call("ui_menu", modern=False)
        state = a.g.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1
        a.prepare()
        start_a = a.handoff()
        heal_cost = a.cost(HEAL)
        shaman = [i for i in a.g.read(a.sym["vs_battle_shamanSpellIds"], 6) if a.learned(i)]
        g = a.g
        g.press("triangle", vsyncs=3, settle=10)
        a.wait_ready(0)
        a.nav(range(0, 10), 0)
        g.press("circle", vsyncs=3, settle=10)
        a.wait_ready(10)
        a.nav(range(10, 15), SHAMAN_ITEM)
        g.press("circle", vsyncs=3, settle=10)
        a.wait_ready(20)
        g.run(10)
        a.nav(range(20, 20 + len(shaman)), 20 + shaman.index(HEAL))
        g.press("circle", vsyncs=3, settle=1)
        n = a.wait_targeting()
        orig = a.handoff()
        check(n is not None, f"original: Heal's target is up ({orig})")
        a.shot("targeting")
        g.run(20)
        a.confirm(check, heal_cost, "Heal confirmed")
    finally:
        a.close()

    # (b) the shell, a new process from the same card
    shutil.copyfile(args.card, card)
    b = Session("shell", out, card, env, bool(args.out))
    try:
        b.g.call("ui_menu", modern=True)
        b.prepare()
        check(b.handoff() == start_a, "both processes start equal (hand-off state, cursor memories)")
        parity(b, check, orig, heal_cost)
        snapshot_checks(b, check)
        cancel_checks(b, check)
        original_cancel(b, check)
        level_checks(b, check)
        level_memory_checks(b, check)
        silence_checks(b, check)
        no_mp_checks(b, check)
        teleport_checks(b, check)
    finally:
        b.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


def cast(b, check, what):
    """Circle on the focused row: the shell closes into targeting; returns the hand-off fields."""
    b.g.press("circle", vsyncs=3, settle=1)
    b.wait(lambda: not b.ui()["visible"], 30, f"{what}: the shell closes for the cast")
    fields = b.exec_fields()
    n = b.wait_targeting()
    check(n is not None, f"{what}: targeting, the menu closed ({b.menu():#x})")
    b.g.run(20)
    return fields


def cancel(b, check, what, sub, row, level):
    """Cross on the target: the shell again on Magick > sub, row and level; no MENU0 screen, no MP spent."""
    mp1 = b.mp()
    seen = set()

    def back():
        seen.add(b.menu())
        return b.ui()["visible"] and b.menu() == 0
    n = None
    for _ in range(6):
        b.g.press("cross", vsyncs=3, settle=0)
        n = b.wait(back, 40, "", quiet=True)
        if n is not None:
            break
    b.g.run(4)
    s, m = b.ui(), b.mg()
    check(n is not None and MENU0 not in seen and b.mp() == mp1,
          f"{what}: cancelled: the shell, no MENU0 screen (seen {sorted(hex(x) for x in seen)}), "
          f"MP {mp1} -> {b.mp()}")
    check(s.get("tab") == "magick" and m.get("sub") == sub and m.get("cursor") == row and m.get("level") == level,
          f"{what}: on {s.get('tab')} > {m.get('sub')} ({sub}), row {m.get('cursor')} ({row}), "
          f"level {m.get('level')} ({level})")
    b.shot("cancel-" + what.replace(" ", "-"))


def parity(b, check, orig, heal_cost):
    b.shell_open("shaman")
    m = b.mg()
    row = next(i for i, x in enumerate(m["schools"][1]["spells"]) if x["id"] == HEAL)
    b.shell_row(row)
    b.g.press("circle", vsyncs=3, settle=1)
    b.wait(lambda: not b.ui()["visible"], 30, "the shell closes for Heal")
    n = b.wait_targeting()
    got = b.handoff()
    check(n is not None, "shell: Heal's target is up")
    for k in orig:
        check(got[k] == orig[k], f"parity: {k}: shell {got[k]} (original {orig[k]})")
    unk2, ability, kind, ret = b.exec_fields()
    check((unk2, ability, kind, ret) == (7, HEAL, 8, MENU0),
          f"Heal hand-off: unk2 {unk2}, executeAbility {ability}, executeAbilityType {kind}, returnState {ret:#x}")
    b.shot("targeting")
    b.g.run(20)
    b.confirm(check, heal_cost, "Heal confirmed")


def snapshot_checks(b, check):
    b.shell_open()
    m = b.mg()
    a = b.actor2()
    mp, mp_max = struct.unpack("<hh", b.g.read(a + MP, 4))
    check(m["valid"] and m["mp"] == mp and m["mp_max"] == mp_max, f"MP {m['mp']} / {m['mp_max']} (RAM {mp} / {mp_max})")
    for k, (name, size) in enumerate(SCHOOLS):
        table = list(b.g.read(b.sym[f"vs_battle_{name}SpellIds"], size))
        ids = [i for i in table if b.learned(i)]
        sc = m["schools"][k]
        got = [x["id"] for x in sc["spells"]]
        check(sc["unlocked"] == bool(ids) and got == ids,
              f"{name}: unlocked {sc['unlocked']}, spells {got} (RAM {ids})")
        check(all(x["name"].strip() for x in sc["spells"]) and [x["mp"] for x in sc["spells"]]
              == [b.cost(i) for i in ids],
              f"{name}: names {[x['name'] for x in sc['spells']]}, MP {[x['mp'] for x in sc['spells']]} "
              f"(costs {[b.cost(i) for i in ids]})")
        for x in sc["spells"]:
            want = LEVELS.get(x["id"], 1)
            lv = [(v["id"], v["mp"]) for v in x["level"]]
            ok = x["levels"] == want and lv == [(x["id"] + l, b.cost(x["id"] + l)) for l in range(want)]
            if want > 1 or not ok:
                check(ok, f"{name} '{x['name']}': {x['levels']} levels {lv} (want {want})")
    subs = [n for (n, _), sc in zip(SCHOOLS, m["schools"]) if sc["unlocked"]]
    check(m["subs"][:len(subs)] == subs, f"sub-tabs {m['subs']} (the unlocked schools {subs})")
    b.shot("snapshot")


def cancel_checks(b, check):
    """Heal by a click on its row (focus + confirm), cancelled; mouse hover and sub-tab click."""
    g = b.g
    b.shell_open("warlock")
    # a sub-tab click: Warlock -> the second shown sub-tab, then back with a click on the first
    subs = b.mg()["subs"]
    b.shell_open("shaman")
    b.pointer(FIRST_SUB_X, SUB_Y, click=True)
    b.pointer(-1, -1)
    check(b.mg()["sub"] == subs[0] and b.ui()["visible"], f"mouse: sub-tab click -> {b.mg()['sub']} ({subs[0]})")
    b.shell_open("shaman")
    m = b.mg()
    rows = m["rows"]
    row = next(i for i, x in enumerate(m["schools"][1]["spells"]) if x["id"] == HEAL)
    if rows > 1:  # hover another row first
        other = (row + 1) % rows
        b.pointer(ROW_X, row_y(other))
        check(b.mg()["cursor"] == other, f"mouse: hover row {other} -> cursor {b.mg()['cursor']}")
    b.pointer(ROW_X, row_y(row))
    check(b.mg()["cursor"] == row, f"mouse: hover row {row} -> cursor {b.mg()['cursor']}")
    mp1 = b.mp()
    b.ui(pointer={"x": ROW_X, "y": row_y(row), "click": True})
    b.wait(lambda: not b.ui()["visible"], 30, "a click on Heal's row closes the shell for the cast")
    b.ui(pointer={"x": -1, "y": -1})
    unk2, ability, kind, _ = b.exec_fields()
    n = b.wait_targeting()
    check(n is not None and (unk2, ability, kind) == (7, HEAL, 8),
          f"mouse: a click on Heal's row casts it (unk2 {unk2}, id {ability}, type {kind}, targeting {n is not None})")
    g.run(20)
    cancel(b, check, "Heal", "shaman", row, 0)
    check(b.mp() == mp1, f"Heal cancelled: MP {mp1} -> {b.mp()}")


def original_cancel(b, check):
    """Square on the shell's Magick: the original MENU0 (0x41); Shaman, Heal, Circle: its hand-off;
    Cross on the target: MENU0 again (its returnState), not the shell."""
    g = b.g
    b.shell_open("shaman")
    g.press("square", vsyncs=3, settle=4)
    b.wait(lambda: b.menu() == MENU0 and not b.ui()["visible"], 120, "the original Magick screen")
    b.wait_ready(10)
    g.run(10)
    b.nav(range(10, 15), SHAMAN_ITEM)
    g.press("circle", vsyncs=3, settle=10)
    b.wait_ready(20)
    g.run(10)
    shaman = [i for i in g.read(b.sym["vs_battle_shamanSpellIds"], 6) if b.learned(i)]
    b.nav(range(20, 20 + len(shaman)), 20 + shaman.index(HEAL))
    g.press("circle", vsyncs=3, settle=1)
    n = b.wait_targeting()
    unk2, ability, kind, ret = b.exec_fields()
    check(n is not None and (unk2, ability, kind, ret) == (7, HEAL, 8, MENU0),
          f"original MENU0: Heal hand-off: unk2 {unk2}, id {ability}, type {kind}, returnState {ret:#x}, "
          f"targeting {n is not None}")
    g.run(20)
    mp1 = b.mp()
    seen = set()

    def back():
        seen.add(b.menu())
        return b.menu() == MENU0
    n = None
    for _ in range(6):
        g.press("cross", vsyncs=3, settle=0)
        n = b.wait(back, 40, "", quiet=True)
        if n is not None:
            break
    g.run(10)
    check(n is not None and not b.ui()["visible"] and 0 not in seen and b.mp() == mp1,
          f"original MENU0: target cancelled: MENU0 again, not the shell (currentMenu {b.menu():#x}, "
          f"seen {sorted(hex(x) for x in seen)}, shell visible {b.ui()['visible']}, MP {mp1} -> {b.mp()})")
    b.shot("cancel-original")
    b.shell_close()


def explosion_row(b):
    return spell_row(b, EXPLOSION)


def spell_row(b, id):
    return next(i for i, x in enumerate(b.mg()["schools"][0]["spells"]) if x["id"] == id)


def level_memory(b):
    return b.g.read(b.sym["D_800F4EE8"], 52)[LEVEL_MEMORY]


def hover(b, row):
    b.pointer(ROW_X, row_y(row))
    b.pointer(-1, -1)


def level_memory_checks(b, check):
    """The LV picker's level is the game's cursorMemories[12]: kept across a close, clamped (and
    written back) by a spell with fewer levels."""
    g, a = b.g, b.actor2()
    saved = g.read(a + MP, 4)
    mp = b.cost(EXPLOSION + 2) + 20  # LV3 affordable (more than an early save's maximum)
    g.write(a + MP, struct.pack("<hh", mp, mp))
    g.run(2)
    b.shell_open("warlock")
    row, thunder = explosion_row(b), spell_row(b, THUNDERBURST)
    hover(b, row)
    b.shell_level(2)
    check(b.mg()["focus"] == EXPLOSION + 2 and level_memory(b) == 2,
          f"Explosion LV3 focused ({b.mg()['focus']}), cursorMemories[12] {level_memory(b)}")
    unk2, ability, kind, _ = cast(b, check, "Explosion LV3")
    cm = b.g.read(b.sym["D_800F4EE8"], 52)
    check((unk2, ability, kind) == (7, EXPLOSION + 2, 6) and (cm[LEVEL_MEMORY], cm[LEVEL_MEMORY + 1]) == (2, 3),
          f"Explosion LV3 hand-off: id {ability} ({EXPLOSION + 2}), LV picker memory "
          f"{cm[LEVEL_MEMORY]} of {cm[LEVEL_MEMORY + 1]}")
    cancel(b, check, "Explosion LV3", "warlock", row, 2)
    b.shell_close()
    b.shell_open("warlock")
    hover(b, row)
    m = b.mg()
    check(m["cursor"] == row and m["level"] == 2 and m["focus"] == EXPLOSION + 2,
          f"opened again: Explosion LV{m['level'] + 1} (LV3), focus {m['focus']}")
    hover(b, thunder)
    m = b.mg()
    check(m["cursor"] == thunder and m["level"] == 1 and m["focus"] == THUNDERBURST + 1 and level_memory(b) == 1,
          f"Thunderburst (2 levels): LV{m['level'] + 1} (LV2), focus {m['focus']}, "
          f"cursorMemories[12] {level_memory(b)} (1)")
    hover(b, row)
    m = b.mg()
    check(m["level"] == 1 and m["focus"] == EXPLOSION + 1,
          f"back on Explosion: LV{m['level'] + 1} (LV2: the clamp was written back), focus {m['focus']}")
    b.shell_level(0)
    b.shell_close()
    g.write(a + MP, saved)
    g.run(2)


def no_mp_checks(b, check):
    """MP 0 when the menu opens: D_800F4EA0 0x20 greys every spell, Circle refused."""
    g = b.g
    mp0 = b.mp()
    mask = 0
    for _ in range(3):  # MP regenerates: 0 at the very frame the menu opens
        g.write(b.actor2() + MP, struct.pack("<h", 0))
        g.press("triangle", vsyncs=3, settle=4)
        b.wait(lambda: b.ui().get("visible") and b.menu() == 0, 120, "the shell opens", step=2)
        mask = struct.unpack("<H", g.read(b.sym["D_800F4EA0"], 2))[0]
        if mask & NO_MP:
            break
        b.shell_close()
    b.shell_open("warlock")
    m = b.mg()
    spells = [(x["name"], lv["usable"], lv["reason"]) for s in m["schools"] for x in s["spells"] for lv in x["level"]]
    check(mask & NO_MP and m["mask"] & NO_MP and spells and all(not u and r == POOR for _, u, r in spells),
          f"MP 0 at the open (D_800F4EA0 {mask:#x}, snapshot mask {m['mask']:#x}): every spell greyed "
          f"'{POOR}' ({spells})")
    before = g.read(b.sym["D_800F4E98"], 8)
    g.press("circle", vsyncs=3, settle=10)
    check(b.ui()["visible"] and b.menu() == 0 and g.read(b.sym["D_800F4E98"], 8) == before,
          f"MP 0: Circle refused (visible {b.ui()['visible']}, currentMenu {b.menu():#x})")
    b.shot("no-mp")
    b.shell_close()
    b.set_mp(mp0)


def level_checks(b, check):
    g = b.g
    sym = b.sym
    # LV2 by Right
    b.shell_open("warlock")
    row = explosion_row(b)
    b.shell_row(row)
    b.shell_level(0)
    g.press("right", vsyncs=3, settle=4)
    check(b.mg()["level"] == 1 and b.mg()["focus"] == EXPLOSION + 1,
          f"Right: Explosion LV{b.mg()['level'] + 1}, focus {b.mg()['focus']}")
    unk2, ability, kind, ret = cast(b, check, "Explosion LV2 (Right)")
    cm = g.read(sym["D_800F4EE8"], 52)
    check((unk2, ability, kind, ret) == (7, EXPLOSION + 1, 6, MENU0) and (cm[LEVEL_MEMORY], cm[LEVEL_MEMORY + 1]) == (1, 3),
          f"Explosion LV2 hand-off: id {ability} ({EXPLOSION + 1}), unk2 {unk2}, type {kind}, return {ret:#x}, "
          f"LV picker memory {cm[LEVEL_MEMORY]} of {cm[LEVEL_MEMORY + 1]}")
    b.shot("explosion-target")
    cancel(b, check, "Explosion LV2 Right", "warlock", row, 1)

    # LV2 by a click on its chip (from LV1)
    b.shell_level(0)
    b.pointer(ROW_X, row_y(row))
    b.pointer(CHIP_X[1], row_y(row), click=True)
    b.pointer(-1, -1)
    m = b.mg()
    check(m["level"] == 1 and m["cursor"] == row and b.ui()["visible"],
          f"mouse: LV2 chip click -> level {m['level']}, row {m['cursor']}, no cast ({b.ui()['visible']})")
    b.pointer(ROW_X, row_y(row))
    b.pointer(CHIP_X[0], row_y(row), click=True)
    check(b.mg()["level"] == 0, f"mouse: LV1 chip click -> level {b.mg()['level']}")
    b.pointer(CHIP_X[1], row_y(row), click=True)
    b.pointer(-1, -1)
    check(b.mg()["level"] == 1, f"mouse: LV2 chip click again -> level {b.mg()['level']}")
    unk2, ability, kind, _ = cast(b, check, "Explosion LV2 (chip)")
    check((unk2, ability, kind) == (7, EXPLOSION + 1, 6),
          f"Explosion LV2 (chip) hand-off: id {ability} ({EXPLOSION + 1}), unk2 {unk2}, type {kind}")
    cancel(b, check, "Explosion LV2 chip", "warlock", row, 1)

    # LV3 not affordable: MP = LV2's cost
    lv2, lv3 = b.cost(EXPLOSION + 1), b.cost(EXPLOSION + 2)
    b.shell_close()
    mp0 = b.mp()
    b.set_mp(lv2)
    b.shell_open("warlock")
    b.shell_row(row)
    b.shell_level(2)
    m = b.mg()
    x = m["schools"][0]["spells"][row]
    check(lv3 > lv2 and x["level"][1]["usable"] and not x["level"][2]["usable"] and x["level"][2]["reason"] == POOR,
          f"MP {lv2}: LV2 usable {x['level'][1]['usable']}, LV3 (cost {lv3}) usable {x['level'][2]['usable']}, "
          f"'{x['level'][2]['reason']}'")
    before = g.read(sym["D_800F4E98"], 8)
    g.press("circle", vsyncs=3, settle=10)
    check(b.ui()["visible"] and b.menu() == 0 and g.read(sym["D_800F4E98"], 8) == before,
          f"LV3 refused: the shell stays ({b.ui()['visible']}, {b.menu():#x}), D_800F4E98 untouched")
    b.shot("explosion-lv3-poor")
    b.shell_level(0)
    b.shell_close()
    b.set_mp(mp0)


def silence_checks(b, check):
    g = b.g
    a = b.actor2()
    st = struct.unpack("<I", g.read(a + STATUSES, 4))[0]
    g.write(a + STATUSES, struct.pack("<I", st | SILENCE))
    b.shell_open("warlock")
    m = b.mg()
    spells = [(x["name"], lv["usable"], lv["reason"]) for s in m["schools"] for x in s["spells"] for lv in x["level"]]
    check(spells and all(not u and SILENCED in r for _, u, r in spells),
          f"silenced: every spell greyed with the reason ({spells})")
    before = g.read(b.sym["D_800F4E98"], 8)
    g.press("circle", vsyncs=3, settle=10)
    check(b.ui()["visible"] and b.menu() == 0 and g.read(b.sym["D_800F4E98"], 8) == before,
          f"silenced: Circle refused (visible {b.ui()['visible']}, currentMenu {b.menu():#x})")
    b.shot("silence")
    b.shell_close()
    g.write(a + STATUSES, struct.pack("<I", st))
    g.run(2)


def room_id(b):
    st = b.g.state()
    table = b.g.read(b.sym["vs_main_locationZoneMapIds"], 1024)
    return next((i for i in range(512) if table[2 * i] == st["zone"] and table[2 * i + 1] == st["room"]), 0)


def teleport_checks(b, check):
    g, sym = b.g, b.sym
    flags = sym["vs_main_stateFlags"]
    data, costs = menu0_tables()
    room = room_id(b)
    here = next((i for i in range(48) if (data[i] & 0xFFFF) == room), None)
    if not check(here is not None, f"the save room (location {room:#x}) has a Circle of Magic in MENU0's table"):
        return
    g.write(flags + UNK_B5, bytes([1]))
    pts = bytearray(g.read(flags + SAVE_POINTS, 48))
    others = [p for p in (0, 1, 3, 10, 30) if p != here]
    for p in others + [here]:
        pts[p] = pts[p] or 1
    paling = next(p for p in (5, 6, 7) if p != here)
    pts[paling] = 2
    g.write(flags + SAVE_POINTS, bytes(pts))
    a = b.actor2()
    g.write(a + MP, g.read(a + MP_MAX, 2))
    g.run(2)
    b.shell_open("teleport")
    m = b.mg()
    t = m["teleport"]
    dests = t["destinations"]
    check(t["exists"] and t["enabled"] and "teleport" in m["subs"],
          f"Teleport: exists {t['exists']}, enabled {t['enabled']} ('{t['reason']}'), sub-tabs {m['subs']}")
    want_points = [p for p in range(48) if pts[p]]
    check([d["point"] for d in dests] == want_points,
          f"destinations {[d['point'] for d in dests]} (savePointStates {want_points})")
    py = [teleport_cost(data, costs, room, d["point"]) for d in dests]
    check([d["cost"] for d in dests] == py and all(d["name"].strip() for d in dests),
          f"costs {[d['cost'] for d in dests]} (MENU0 _getTeleportCost {py}), names non-empty")
    cur = [d["point"] for d in dests if d["current"]]
    check(cur == [here] and m["focus"] == here, f"current circle {cur} ({here}), focused {m['focus']}")
    pal = next(d for d in dests if d["point"] == paling)
    check(not pal["usable"] and pal["reason"], f"spirit paling circle {paling}: usable {pal['usable']}, "
                                               f"'{pal['reason']}'")
    b.shot("teleport")
    # Circle on the paling circle: refused
    idx = [d["point"] for d in dests]
    before = g.read(sym["D_800F4E98"], 8)
    for _ in range(50):
        if b.mg()["focus"] == paling:
            break
        g.press("down", vsyncs=3, settle=4)
    g.press("circle", vsyncs=3, settle=10)
    check(b.ui()["visible"] and g.read(sym["D_800F4E98"], 8) == before,
          f"Circle on the paling circle: refused (visible {b.ui()['visible']})")
    # teleport to the nearest other circle
    target = min((p for p in others), key=lambda p: abs(p - here))
    cost = py[idx.index(target)]
    for _ in range(50):
        if b.mg()["focus"] == target:
            break
        g.press("down", vsyncs=3, settle=4)
    mp0, st0 = b.mp(), g.state()
    g.press("circle", vsyncs=3, settle=1)
    b.wait(lambda: not b.ui()["visible"], 30, "the shell closes for the teleport")
    e = g.read(sym["D_800F4E98"], 8)
    unk2, packed = struct.unpack("<hi", e[2:8])
    want = teleport_packed(data, target, cost)
    check(unk2 == 0xA and packed == want, f"teleport hand-off: unk2 {unk2:#x}, executeAbility {packed:#x} "
                                           f"(MENU0's packing {want:#x})")
    # MP regenerates meanwhile: the drop is the step where it goes down
    last, drop = [mp0], []

    def gone():
        v = b.mp()
        if v < last[0] and not drop:
            drop.append(last[0] - v)
        last[0] = v
        return drop and (g.state()["zone"], g.state()["room"]) != (st0["zone"], st0["room"])
    n = b.wait(gone, 1500, "", step=2, quiet=True)
    g.run(200)
    st = g.state()
    check(n is not None and room_id(b) == data[target] & 0xFFFF,
          f"teleported: zone/room {st0['zone']}/{st0['room']} -> {st['zone']}/{st['room']} "
          f"(location {room_id(b):#x}, circle {target}: {data[target] & 0xFFFF:#x})")
    check(drop == [cost], f"teleport: MP dropped by {drop} (cost {cost}; {mp0} -> {b.mp()} with regeneration)")
    b.shot("teleported")


if __name__ == "__main__":
    sys.exit(main())
