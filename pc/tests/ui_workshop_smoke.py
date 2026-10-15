#!/usr/bin/env python3
"""Modern menus, the Workshop (Items > Workshop: pc/src/ui/workshop_bridge.c,
ui_workshop.cpp, ui_workshop_draw.cpp): assemble, gems attach and remove,
disassemble (a weapon, a shield, the equipped weapon), repair, rename and
combine (blades, shields, armor) with the same effect on game state as the
original Setup screen (MENUC, reached from MENU3's Items > Setup, and MENU8's
rename).

    python3 pc/tests/ui_workshop_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Needs a card whose first save is in the save room with an equipped weapon
and shield, two more weapons, a second shield and two unworn armor pieces
(skipped otherwise). 4:3, 960x720 window.

Both processes get the same set-up after Continue: the cursor memory off
(vs_main_settings.cursorMemory 0), three gems in the bag (unset), gem slots
on three grips and both shields, some damage (blades' DP and PP), the
equipped weapon and shield re-equipped from their records through the
game's own path (RPC ui_menu reequip: vs_battle_equipWeapon / Shield), so
Ashley's battle record has the gem slots too (MAINMENU's
vs_mainMenu_initInventory copies it back over the bag's as every menu
opens), then a warp (RPC room_goto, scripts and enemies off) into zone 47
room 0, a workshop (vs_battle_inWorkshop 6: every material).

Parity: the same list of operations runs in two game processes from copies
of the same card. (a) ui_menus off: the original Setup screen (Triangle,
Items, Setup on the OPTIONS page; its lists by their menu items; MENU8's grid
for a name: Square clears the field, the grid types, Start and Yes); (b)
ui_menus on: Items > Workshop. After every operation (the menu closed again,
a model reload settled) vs_main_inventory (0xF00 bytes),
vs_main_inventoryIndices (176 bytes) and Ashley's battle record
(vs_battle_actor2 behind vs_battle_characterState->unk3C: weapon, shield,
limbs, statuses) are compared byte for byte (masked: MASKS, with the
reason), and so are MENUC's cursor memories (D_800F4EE8 cursorMemories ids
53..61, bytes 106..123; workshop_bridge.c's header lists how they are
kept). The operations:
  repair; gems: two into an unequipped weapon, one taken out again, one into
  the unequipped shield, one into the equipped weapon (re-equipped); the
  equipped weapon enchanted (a status bit of func_8006B270's 0x01E00000
  written into the battle record), then Gems on it with no change, Attach
  (the original does not re-equip: the enchantment stays), then Gems on it
  backed out with Cross (the original's _attachGems(targetGems) re-equips:
  it ends);
  disassemble that shield (its gem out) and that weapon; assemble its blade
  and grip with a gem (the name left blank: the blade's); rename the
  equipped weapon "Ab" (re-equipped; b: a grid character, a typed one); combine blades (both
  weapons they are in taken apart), armor (two unworn pieces), shields (the
  equipped one: unequipped, models reloaded); disassemble the equipped
  weapon (unequipped, models reloaded).
In (a) each operation must have changed the bag (the two enchantment steps:
the enchantment must have stayed / ended), so a step the original menu
missed cannot pass as parity.

Shell-only checks (process b): in the save room (no workshop) Assemble,
Repair and Combine are greyed with MENUC's notInWorkshop (its loop greys
rows 4, 5 and 0: Gems, Disassemble and Rename work anywhere); in the workshop with
eight weapons (records faked, then put back) Assemble is greyed
(weaponsFull); the name field's keyboard (RPC ui_menu text / key);
previews: the assembled weapon's and a combine's result card; a click on a
command opens it (found by hovering the command column: its focus follows
the pointer). At the end a warp into zone 45 (workshop 4: silver and
damascus only): Combine's blade list greys the bronze blades
(invalidBladeMaterial).
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
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

CLOSED = 0x3F
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4
WORKSHOP = (47, 0)    # vs_battle_inWorkshop 6: _workshopMaterials 0xFE, every material
WORKSHOP_4 = (45, 0)  # 4: 0xC0, silver and damascus

INVENTORY_BYTES, INDICES_BYTES = 0xF00, 176
WEAPONS, SHIELDS, BLADES, GRIPS, ARMOR, GEMS = 0, 0x100, 0x280, 0x540, 0x640, 0x8C0
WEAPON, SHIELD, BLADE, GRIP, ARMOR_REC, GEM = 32, 48, 44, 16, 40, 28
IDX = {0: (0, 8), 1: (8, 16), 2: (24, 16), 3: (40, 8), 4: (48, 16), 5: (64, 48), 6: (112, 64)}
SETTINGS_CURSOR_MEMORY = 1
MEMORIES = (106, 124)  # D_800F4EE8 cursorMemories ids 53..61 (MENUC's lists)
SHIELD_SLOTS = 0x2F    # shields[i].gems[3]: weapon_slots.c's quick-swap bits (a port mod byte)
WEAPON_SLOT = 7        # weapons[i].gems[3]: its quick-swap slot
ACTOR2_BYTES = 0x964   # vs_battle_actor2 (BATTLE 146C.h)
ACTOR_UNK3C = 60       # vs_battle_actor.unk3C: the vs_battle_actor2 (a PS1 address)
STATUSES = 0x948       # vs_battle_actor2.statuses
ENCHANT = 0x00400000   # a weapon enchantment's status (func_8006B270 clears 0x01E00000 on vs_battle_equipWeapon)

FRAMES = "the two paths take different numbers of frames"
MASKS = [
    ("inventory", SHIELDS + i * SHIELD + SHIELD_SLOTS, SHIELDS + i * SHIELD + SHIELD_SLOTS + 1,
     "shields' quick-swap slot bits (a port mod byte): a weapon taken apart loses its slot in the shell "
     "(weapon_slots.c, as the Items screen's discard); the original knows nothing of the slots")
    for i in range(8)
] + [
    ("actor2", 0x18, 0x1A, "currentHP: regenerates with game time; " + FRAMES),
    ("actor2", 0x1C, 0x1E, "currentMP: as currentHP"),
] + [
    ("actor2", 0x398 + k * 0xDC, 0x398 + k * 0xDC + 2, f"limbs[{k}].hp: regenerates with game time; " + FRAMES)
    for k in range(6)
]

# MENUC's menu items (vs_battle_setMenuItem ids; vs_mainmenu_setMenuRows: row + rowInfo * 10)
TOP = 10           # the command list (0x135: rowInfo 1)
SUB = 20           # Gems' / Disassemble's Weapons, Shields; Combine's categories (rowInfo 2)
LIST = 10          # the weapon / shield lists (rowInfo 1)
PICK = 20          # _populateItemsList's rows
YES = 29           # _confirmationPrompt / _confirmCombine: Yes, No(, Cancel)
MENU8_YES = 20     # MENU8's _confirmationScreen


def u16(b, o):
    return struct.unpack("<H", b[o:o + 2])[0]


class Bag:
    """vs_main_inventory and its indices, as the menus list them."""

    def __init__(self, inv, idx):
        self.inv, self.idx = inv, idx

    def listed(self, c):
        lo, size = IDX[c]
        return [i - 1 for i in self.idx[lo:lo + size] if i]

    def weapon(self, i):
        b = self.inv[WEAPONS + i * WEAPON:WEAPONS + (i + 1) * WEAPON]
        return {"blade": b[1], "grip": b[2], "equipped": b[3], "gems": list(b[4:7])}

    def shield(self, i):
        b = self.inv[SHIELDS + i * SHIELD:SHIELDS + (i + 1) * SHIELD]
        return {"id": b[4], "equipped": b[1], "slots": b[4 + 12], "gems": list(b[44:47])}

    def blade(self, i):
        b = self.inv[BLADES + i * BLADE:BLADES + (i + 1) * BLADE]
        return {"id": b[0], "asm": b[42]}

    def grip(self, i):
        b = self.inv[GRIPS + i * GRIP:GRIPS + (i + 1) * GRIP]
        return {"id": u16(b, 0), "slots": b[4], "asm": u16(b, 12)}

    def armor(self, i):
        b = self.inv[ARMOR + i * ARMOR_REC:ARMOR + (i + 1) * ARMOR_REC]
        return {"id": b[0], "category": b[3], "limb": b[38]}

    def gem(self, i):
        b = self.inv[GEMS + i * GEM:GEMS + (i + 1) * GEM]
        return {"id": u16(b, 0), "set": u16(b, 24)}

    # the original's pick lists (_populateItemsList: the slot's own first, a placeholder when empty)
    def gem_pick(self, current):
        return [current] + [g for g in self.listed(5) if self.gem(g)["set"] == 0 and g != current]

    def blade_pick(self):
        return [None] + [b for b in self.listed(1) if self.blade(b)["asm"] == 0]

    def grip_pick(self):
        return [None] + [g for g in self.listed(2) if self.grip(g)["asm"] == 0]

    def combine_pick(self, c, chosen):
        items = self.listed(c)
        if c == 4:
            items = [a for a in items if self.armor(a)["category"] != 7]
        return [None] + [i for i in items if i not in chosen]


class Session:
    def __init__(self, name, out, card, env, shots):
        self.name, self.out, self.shots = name, out, shots
        self.g = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env)
        self.sym = symbols()

    def close(self):
        self.g.close()

    def shot(self, name):
        if self.shots:
            self.g.run(4)
            self.g.present_screenshot(self.out / f"{self.name}-{name}.png")

    def ptr(self, address):
        return int.from_bytes(self.g.read(address, PTR), "little")

    def menu(self):
        return self.g.read(self.sym["vs_battle_menuState"], 1)[0]

    def bag(self):
        return Bag(self.inv(), self.idx())

    def inv(self):
        return self.g.read(self.sym["vs_main_inventory"], INVENTORY_BYTES)

    def idx(self):
        return self.g.read(self.sym["vs_main_inventoryIndices"], INDICES_BYTES)

    def memories(self):
        a, b = MEMORIES
        return self.g.read(self.sym["D_800F4EE8"] + a, b - a)

    def actor2(self):
        """Ashley's vs_battle_actor2 (psx_get(vs_battle_characterState->unk3C))."""
        state = self.ptr(self.sym["vs_battle_characterState"])
        return int.from_bytes(self.g.read(state + ACTOR_UNK3C, 4), "little")

    def statuses(self):
        return struct.unpack("<I", self.g.read(self.actor2() + STATUSES, 4))[0]

    def enchant(self):
        """The equipped weapon enchanted: the status bit written into the battle record."""
        self.g.write(self.actor2() + STATUSES, struct.pack("<I", self.statuses() | ENCHANT))
        self.g.run(2)

    def dump(self):
        return {"inventory": self.inv(), "indices": self.idx(), "actor2": self.g.read(self.actor2(), ACTOR2_BYTES)}

    def wait(self, pred, limit, what, step=2, quiet=False):
        for n in range(0, limit + 1, step):
            if pred():
                return n
            self.g.run(step)
        if quiet:
            return None
        raise TimeoutError(f"{self.name}: {what}: not after {limit} vsyncs (currentMenu {self.menu():#x})")

    def press(self, button, n=1, settle=6):
        for _ in range(n):
            self.g.press(button, vsyncs=3, settle=settle)

    def warp(self, zone, room):
        reply = self.g.call("room_goto", zone=zone, room=room, scripts=False, enemies=False)
        if not reply.get("ok"):
            raise TimeoutError(f"{self.name}: room_goto {zone}/{room}: {reply}")
        for _ in range(60):
            self.g.run(20)
            if self.g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        self.g.run(60)
        st = self.g.state()
        if (st["zone"], st["room"]) != (zone, room):
            raise TimeoutError(f"{self.name}: warped to {st['zone']}/{st['room']}, not {zone}/{room}")

    def prepare(self):
        """The RAM set-up both processes share (the module docstring)."""
        g, base, bag = self.g, self.sym["vs_main_inventory"], self.bag()
        g.write(self.sym["vs_main_settings"] + SETTINGS_CURSOR_MEMORY, b"\x00")
        for k, gid in enumerate((261, 262, 263)):  # three gems, unset
            rec = struct.pack("<HBBBbbb8b8bHh", gid, gid - 260, 0, 0, k + 1, 0, -1, *([4, 0, 0, 0, 0, -2, 0, 0]),
                              *([0, 3, 0, 0, 0, 0, 0, 0]), 0, k + 1)
            g.write(base + GEMS + k * GEM, rec)
        lo, _ = IDX[5]
        g.write(self.indices_addr() + lo, bytes([1, 2, 3]))
        weapons = bag.listed(0)
        for i in weapons:
            w = bag.weapon(i)
            g.write(base + GRIPS + (w["grip"] - 1) * GRIP + 4, bytes([1 if w["equipped"] else 2]))
        for i in range(8):
            if bag.shield(i)["id"]:
                g.write(base + SHIELDS + i * SHIELD + 4 + 12, b"\x01")
        g.run(2)
        self.ui(reequip=True)  # the battle record as the bag now (the gem slots of the equipped weapon / shield)
        # damage: blades' DP below their maximum, PP that a repair costs
        for k, i in enumerate(bag.listed(1)):
            b = bag.inv[BLADES + i * BLADE:BLADES + (i + 1) * BLADE]
            max_dp = u16(b, 4)
            g.write(base + BLADES + i * BLADE + 8, struct.pack("<HH", max_dp - 1500 - 700 * k, 40 + 30 * k))
        g.run(2)

    def indices_addr(self):
        return self.sym["vs_main_inventoryIndices"]

    # ---- the original Setup screen (ui_menus off) ----
    def menu_items(self):
        p = self.ptr(self.sym["vs_battle_menuItems"])
        if not p:
            return {}
        raw = self.g.read(p, 64 * 40)
        return {i: (raw[i * 64], raw[i * 64 + 6]) for i in range(40) if raw[i * 64]}

    def wait_ready(self, want=None, limit=600):
        calm = 0

        def ready():
            nonlocal calm
            items = self.menu_items()
            ok = all(s <= 1 for s, _ in items.values()) and (want is None or want in items)
            calm = calm + 1 if ok else 0
            return calm >= 3
        self.wait(ready, limit, f"menu item {want} ready", step=2)
        self.g.run(2)

    def selected(self, ids):
        items = self.menu_items()
        return next((i for i in ids if i in items and items[i][1]), None)

    def nav(self, ids, target, what):
        for _ in range(len(ids) + 3):
            if self.selected(ids) == target:
                return
            self.press("down")
        if self.selected(ids) != target:
            raise TimeoutError(f"{self.name}: {what}: item {target} not reached ({self.selected(ids)})")

    def o_setup(self):
        """Triangle, Items, Setup: MENUC's command list."""
        self.press("triangle", settle=10)
        self.wait_ready(want=2)
        self.nav(range(2, 10), 3, "Items")
        self.press("circle", settle=10)
        self.wait(lambda: self.menu() != 0, 200, "MENU3 starts")
        self.wait_ready(want=21)
        self.nav([20, 21], 21, "Setup")
        self.press("circle", settle=10)
        self.wait_ready(want=TOP + 5)

    def o_command(self, row):
        self.nav(range(TOP, TOP + 6), TOP + row, "command")
        self.press("circle", settle=8)

    def o_close(self):
        for _ in range(6):
            if self.menu() == CLOSED:
                break
            self.press("triangle", settle=10)
            self.wait(lambda: self.menu() == CLOSED, 600, "", step=4, quiet=True)
        self.wait(lambda: self.menu() == CLOSED, 2, "the menu closes")
        self.g.run(60)

    def o_pick(self, rows, want):
        """A _populateItemsList list up: Down to `want`'s row, Circle."""
        self.wait_ready(want=PICK)
        self.press("down", rows.index(want), settle=4)
        self.press("circle", settle=8)

    def o_repair(self):
        self.o_setup()
        self.o_command(4)
        self.wait_ready(want=YES + 1)
        self.press("circle", settle=10)  # Yes (it opens on Yes)
        self.g.run(30)
        self.press("circle", settle=10)  # "All equipment repaired"
        self.wait_ready(want=TOP + 5)
        self.o_close()

    def o_gems(self, shield, record, actions):
        """actions: (slot, gem record or None = take the slot's gem out)."""
        self.o_setup()
        self.o_command(1)
        self.wait_ready(want=SUB + 1)
        self.nav([SUB, SUB + 1], SUB + shield, "Weapons / Shields")
        self.press("circle", settle=8)
        self.wait_ready(want=LIST)
        bag = self.bag()
        row = bag.listed(3 if shield else 0).index(record)
        self.nav(range(LIST, LIST + 8), LIST + row, "the weapon / shield")
        self.press("circle", settle=8)
        first = 11 + (0 if shield else 2)
        self.wait_ready(want=first)
        for slot, gem in actions:
            self.nav(range(first, first + 3), first + slot, "gem slot")
            self.press("circle", settle=8)
            bag = self.bag()
            gems = bag.shield(record)["gems"] if shield else bag.weapon(record)["gems"]
            current = gems[slot] - 1 if gems[slot] else None
            self.o_pick(bag.gem_pick(current), current if gem is None else gem)
            self.wait_ready(want=first)
        self.press("start", settle=8)
        self.wait_ready(want=YES + 2)
        self.press("circle", settle=10)  # Yes
        self.wait_ready(want=TOP + 5)
        self.o_close()

    def o_gems_back(self, shield, record):
        """Gems on a target, its slots backed out of with Cross (_attachGems(targetGems))."""
        self.o_setup()
        self.o_command(1)
        self.wait_ready(want=SUB + 1)
        self.nav([SUB, SUB + 1], SUB + shield, "Weapons / Shields")
        self.press("circle", settle=8)
        self.wait_ready(want=LIST)
        row = self.bag().listed(3 if shield else 0).index(record)
        self.nav(range(LIST, LIST + 8), LIST + row, "the weapon / shield")
        self.press("circle", settle=8)
        self.wait_ready(want=11 + (0 if shield else 2))
        self.press("cross", settle=10)
        self.wait_ready(want=LIST)
        self.o_close()

    def o_disassemble(self, shield, record):
        self.o_setup()
        self.o_command(2)
        self.wait_ready(want=SUB + 1)
        self.nav([SUB, SUB + 1], SUB + shield, "Weapons / Shields")
        self.press("circle", settle=8)
        self.wait_ready(want=LIST)
        row = self.bag().listed(3 if shield else 0).index(record)
        self.nav(range(LIST, LIST + 8), LIST + row, "the weapon / shield")
        self.press("circle", settle=8)
        self.wait_ready(want=YES + 1)
        self.press("circle", settle=10)  # Yes
        self.wait_ready(want=SUB + 1, limit=1200)  # back on Weapons / Shields (an equipped one: its models loaded)
        self.o_close()

    def o_menu8_done(self):
        """MENU8: Start, Yes; back on MENUC's command list."""
        self.press("start", settle=10)
        self.wait_ready(want=MENU8_YES + 2)
        self.g.run(10)
        for _ in range(5):  # Yes (the confirm takes a press once its rows have slid in)
            self.press("circle", settle=10)
            if self.wait(lambda: self.menu() & 0x3F == 13, 200, "", quiet=True) is not None:
                break
        self.wait(lambda: self.menu() & 0x3F == 13, 2, "back to MENUC")
        self.wait_ready(want=TOP + 5)

    def o_rename(self, record):
        self.o_setup()
        self.o_command(3)
        self.wait_ready(want=LIST)
        row = self.bag().listed(0).index(record)
        self.nav(range(LIST, LIST + 8), LIST + row, "the weapon")
        self.press("circle", settle=8)
        self.wait_ready(want=YES + 1)
        self.press("circle", settle=10)  # Yes
        self.wait(lambda: self.menu() & 0x3F == 8, 300, "MENU8 opens")
        self.g.run(40)
        self.press("square", 20, settle=3)  # the name cleared
        self.press("circle", settle=4)      # 'A' (row 0, column 0)
        self.press("down", 2, settle=4)
        self.press("right", settle=4)
        self.press("circle", settle=4)      # 'b' (row 2, column 1)
        self.o_menu8_done()
        self.o_close()

    def o_assemble(self, blade, grip, gem):
        self.o_setup()
        self.o_command(0)
        self.wait_ready(want=12)
        bag = self.bag()
        blades, grips = bag.blade_pick(), bag.grip_pick()
        self.nav([11, 12], 11, "Blade")
        self.press("circle", settle=8)
        self.o_pick(blades, blade)
        self.wait_ready(want=12)
        self.press("circle", settle=8)  # the grip row (the cursor moved there)
        self.o_pick(grips, grip)
        self.wait_ready(want=13)
        self.press("circle", settle=8)  # the first gem slot
        self.o_pick(self.bag().gem_pick(None), gem)
        self.wait_ready(want=13)
        self.press("start", settle=8)
        self.wait_ready(want=YES + 2)
        self.press("circle", settle=10)  # Yes
        self.wait(lambda: self.menu() & 0x3F == 8, 300, "MENU8 opens")
        self.g.run(40)
        self.o_menu8_done()  # the name left blank: the blade's
        self.o_close()

    def o_combine(self, c, a, b):
        cat = {1: 0, 3: 1, 4: 2}[c]
        self.o_setup()
        self.o_command(5)
        self.wait_ready(want=SUB + 2)
        self.nav([SUB, SUB + 1, SUB + 2], SUB + cat, "category")
        self.press("circle", settle=8)
        self.wait_ready(want=12, limit=900)
        self.nav([11, 12], 11, "First")
        self.press("circle", settle=8)
        self.o_pick(self.bag().combine_pick(c, []), a)
        self.wait_ready(want=12)
        self.press("circle", settle=8)  # Second (the cursor moved there)
        self.o_pick(self.bag().combine_pick(c, [a]), b)
        self.wait_ready(want=12)
        self.press("circle", settle=8)  # the OK row (the cursor moved there)
        self.wait_ready(want=YES + 2)
        self.press("circle", settle=10)  # Yes
        # back on the command list, or (an equipped one: models loaded) on Combine's categories
        self.wait(lambda: TOP + 5 in self.menu_items() or SUB + 2 in self.menu_items(), 1200, "Combine returns")
        self.wait_ready(want=TOP + 5 if TOP + 5 in self.menu_items() else SUB + 2, limit=1200)
        self.o_close()

    # ---- the shell (ui_menus on) ----
    def ui(self, **params):
        return self.g.call("ui_menu", **params)

    def ws(self):
        it = self.ui().get("items") or {}
        return it.get("workshop") or {}

    def s_open(self):
        g = self.g
        for _ in range(6):
            if self.ui().get("visible") and self.menu() == 0:
                break
            g.press("triangle", vsyncs=3, settle=4)
            self.wait(lambda: self.ui().get("visible") and self.menu() == 0, 120, "", quiet=True)
            g.run(6)
        self.wait(lambda: self.ui().get("visible") and self.menu() == 0, 2, "the shell opens")
        for _ in range(8):
            if self.ui().get("tab") == "items":
                break
            g.press("r1", vsyncs=3, settle=4)
        for _ in range(9):
            if (self.ui().get("items") or {}).get("category") == "workshop":
                break
            g.press("l2", vsyncs=3, settle=4)
        self.wait(lambda: self.ws().get("valid") and not self.ws().get("busy"), 300, "the Workshop")

    def s_close(self):
        for _ in range(6):
            if self.menu() == CLOSED and not self.ui().get("visible"):
                break
            self.g.press("triangle", vsyncs=3, settle=4)
            self.wait(lambda: self.menu() == CLOSED and not self.ui().get("visible"), 600, "", step=4, quiet=True)
        self.wait(lambda: self.menu() == CLOSED and not self.ui().get("visible"), 2, "the shell closes")
        self.g.run(60)

    def s_until(self, pred, button, what, n=60):
        for _ in range(n):
            if pred(self.ws()):
                return
            self.press(button, settle=3)
        raise TimeoutError(f"{self.name}: {what} not reached ({self.ws().get('mode')}, {self.ws().get('cursor')})")

    def s_command(self, row):
        self.s_until(lambda w: w.get("mode") == "rows" and w.get("row") == row, "down", f"command {row}")
        self.press("circle", settle=4)

    def s_entry(self, inventory, list_name=None):
        def on(w):
            lst = w.get("list") or []
            c = w.get("cursor", -1)
            return 0 <= c < len(lst) and lst[c]["inventory"] == inventory and (
                list_name is None or lst[c]["list"] == list_name)
        self.s_until(on, "down", f"entry {list_name} {inventory}")
        self.press("circle", settle=4)

    def s_category(self, cat):
        self.s_until(lambda w: (w.get("list") or [{}] * 3)[w.get("cursor", 0)].get("category") == cat, "down",
                     f"category {cat}")
        self.press("circle", settle=4)

    def s_slot(self, k):
        self.s_until(lambda w: w.get("cursor") == k, "down", f"slot {k}")
        self.press("circle", settle=4)

    def s_finish(self):
        self.s_until(lambda w: w.get("finish") and w.get("cursor") == w.get("slots", 0) - 1, "down", "the finish row")
        self.press("circle", settle=4)

    def s_yes(self):
        self.s_until(lambda w: (w.get("confirm") or {}).get("yes"), "up", "Yes")
        self.press("circle", settle=4)

    def s_settle(self):
        self.g.run(8)
        self.wait(lambda: not self.ws().get("busy"), 900, "the model reload")


def diff(a, b, regions=None):
    found = []
    for region in regions or a:
        x, y = a[region], b[region]
        i = 0
        while i < len(x):
            if x[i] != y[i] and not masked(region, i):
                j = i
                while j < len(x) and x[j] != y[j]:
                    j += 1
                found.append((region, i, j))
                i = j
            else:
                i += 1
    return found


def masked(region, offset):
    return any(r == region and s <= offset < e for r, s, e, _ in MASKS)


def describe(want, got, rest):
    return ", ".join(f"{r} {s:#x}..{e:#x} {want[r][s:e].hex()} vs {got[r][s:e].hex()}" for r, s, e in rest[:10])


def plan(bag):
    """The operations for this card (None: it lacks what they need). Records are 0-based."""
    weapons = bag.listed(0)
    eq = [w for w in weapons if bag.weapon(w)["equipped"]]
    free = [w for w in weapons if not bag.weapon(w)["equipped"]]
    shields = [s for s in range(8) if bag.shield(s)["id"]]
    eq_shield = [s for s in shields if bag.shield(s)["equipped"]]
    free_shield = [s for s in shields if not bag.shield(s)["equipped"]]
    unworn = [a for a in range(16) if bag.armor(a)["id"] and not bag.armor(a)["limb"] and bag.armor(a)["category"] != 7]
    if len(eq) != 1 or len(free) < 2 or not eq_shield or not free_shield or len(unworn) < 2:
        return None
    w0, w1, w2 = eq[0], free[0], free[1]
    s0, s1 = eq_shield[0], free_shield[0]
    b1, g1 = bag.weapon(w1)["blade"] - 1, bag.weapon(w1)["grip"] - 1
    b2 = bag.weapon(w2)["blade"] - 1
    return [
        ("repair", "repair", ()),
        ("gems: two into an unequipped weapon", "gems", (0, w1, [(0, 0), (1, 1)])),
        ("gems: the first taken out again", "gems", (0, w1, [(0, None)])),
        ("gems: one into the unequipped shield", "gems", (1, s1, [(0, 0)])),
        ("gems: one into the equipped weapon (re-equipped)", "gems", (0, w0, [(0, 2)])),
        ("gems: the enchanted equipped weapon, no change, Attach (the enchantment stays)", "gems_same", (0, w0, [])),
        ("gems: the enchanted equipped weapon, backed out (re-equipped: the enchantment ends)", "gems_back", (0, w0)),
        ("disassemble the shield (its gem out)", "disassemble", (1, s1)),
        ("disassemble the unequipped weapon", "disassemble", (0, w1)),
        ("assemble its blade and grip with a gem (the blade's name)", "assemble", (b1, g1, 1)),
        ("rename the equipped weapon 'Ab' (re-equipped)", "rename", (w0,)),
        ("combine blades (both weapons taken apart)", "combine", (1, b1, b2)),
        ("combine armor (two unworn pieces)", "combine", (4, unworn[0], unworn[1])),
        ("combine shields (the equipped one: unequipped, models reloaded)", "combine", (3, s0, s1)),
        ("disassemble the equipped weapon (unequipped, models reloaded)", "disassemble", (0, w0)),
    ]


def original_op(a, op):
    name, kind, args = op
    if kind in ("gems_same", "gems_back"):
        a.enchant()
    if kind == "repair":
        a.o_repair()
    elif kind in ("gems", "gems_same"):
        a.o_gems(*args)
    elif kind == "gems_back":
        a.o_gems_back(*args)
    elif kind == "disassemble":
        a.o_disassemble(*args)
    elif kind == "assemble":
        a.o_assemble(*args)
    elif kind == "rename":
        a.o_rename(*args)
    elif kind == "combine":
        a.o_combine(*args)


def shell_op(b, op, check, preview):
    name, kind, args = op
    if kind in ("gems_same", "gems_back"):
        b.enchant()
    b.s_open()
    lists = {0: "weapons", 3: "shields"}
    if kind == "repair":
        b.s_command(4)
        w = b.ws()
        check(w.get("mode") == "confirm" and (w.get("confirm") or {}).get("yes") and len(w.get("repairs") or []) > 0,
              f"shell: Repair asks (on Yes) with the DP / PP list ({len(w.get('repairs') or [])} records)")
        b.shot("repair-confirm")
        b.s_yes()
    elif kind == "gems_back":
        shield, record = args
        b.s_command(1)
        b.s_entry(record, lists[3 if shield else 0])
        check(b.ws().get("mode") == "slots", f"shell: {name}: the slots ({b.ws().get('mode')})")
        b.press("cross", settle=4)
        check(b.ws().get("mode") == "target", f"shell: {name}: Cross backs out ({b.ws().get('mode')})")
    elif kind in ("gems", "gems_same"):
        shield, record, actions = args
        b.s_command(1)
        b.s_entry(record, lists[3 if shield else 0])
        for slot, gem in actions:
            b.s_slot(slot)
            w = b.ws()
            current = (w.get("gems") or [-1])[slot]
            b.s_entry(current if gem is None else gem, "gems")
        b.shot(f"gems-slots-{record}")
        b.s_finish()
    elif kind == "disassemble":
        shield, record = args
        b.s_command(2)
        b.s_entry(record, lists[3 if shield else 0])
        w = b.ws()
        check(w.get("mode") == "confirm" and not (w.get("confirm") or {}).get("yes"),
              f"shell: Disassemble asks, on No ({w.get('mode')}, {w.get('confirm')})")
        b.shot(f"disassemble-confirm-{shield}-{record}")
        b.s_yes()
    elif kind == "assemble":
        blade, grip, gem = args
        b.s_command(0)
        b.s_slot(0)
        b.s_entry(blade, "blades")
        b.s_slot(1)
        b.s_entry(grip, "grips")
        b.s_slot(2)
        b.s_entry(gem, "gems")
        w = b.ws()
        p = w.get("preview") or {}
        preview["assemble"] = p
        check(p.get("category") == 0 and p.get("dp_max", 0) > 0 and p.get("name"),
              f"shell: the new weapon's preview ({p})")
        b.shot("assemble-slots")
        b.s_finish()
        w = b.ws()
        check(w.get("mode") == "name" and (w.get("name") or {}).get("assembling"),
              f"shell: Next opens the name ({w.get('mode')})")
        b.shot("assemble-name")
        # Done: Up from the grid's first row wraps to Delete, Right to Done
        b.press("up", settle=3)
        b.press("right", settle=3)
        b.s_until(lambda w: (w.get("name") or {}).get("grid") == [9, 1], "right", "Done")
        b.press("circle", settle=4)
    elif kind == "rename":
        record, = args
        b.s_command(3)
        b.s_entry(record, "weapons")
        w = b.ws()
        check(w.get("mode") == "name" and (w.get("name") or {}).get("text"),
              f"shell: the name field opens on the weapon's name ({(w.get('name') or {}).get('text')!r})")
        b.ui(key="end")
        for _ in range(20):
            b.ui(key="backspace")
        w = b.ws()
        check((w.get("name") or {}).get("text") == "" and (w.get("name") or {}).get("cursor") == 0,
              f"shell: End, Backspace x20 clear the field ({w.get('name')})")
        b.press("circle", settle=4)  # the grid's 'A' (row 0, column 0)
        b.ui(text="b")         # typed
        w = b.ws()
        check((w.get("name") or {}).get("text") == "Ab", f"shell: grid 'A' and typed 'b' give 'Ab' ({w.get('name')})")
        b.shot("rename-name")
        b.ui(key="enter")
    elif kind == "combine":
        c, x, y = args
        cat = {1: 0, 3: 1, 4: 2}[c]
        lst = {1: "blades", 3: "shields", 4: "armor"}[c]
        b.s_command(5)
        b.s_category(cat)
        b.s_slot(0)
        b.s_entry(x, lst)
        b.s_slot(1)
        b.s_entry(y, lst)
        b.s_finish()
        w = b.ws()
        p = w.get("preview") or {}
        preview[f"combine{c}"] = p
        check(w.get("mode") == "confirm" and not (w.get("confirm") or {}).get("yes") and p.get("name"),
              f"shell: Combine asks, on No, over the result ({p.get('name')!r}, {p.get('material')!r})")
        b.shot(f"combine-confirm-{c}")
        b.s_yes()
    b.g.run(6)
    msg = b.ws().get("message")
    check(not msg, f"shell: {name}: accepted ({msg!r})")
    b.s_settle()
    b.s_close()


def greying_checks(b, check):
    """Away from a workshop: Assemble, Repair, Combine greyed (the save room, before the warp)."""
    b.s_open()
    w = b.ws()
    rows = {r["id"]: r for r in w.get("rows") or []}
    check(w.get("workshop") == 0 and all(rows[k]["enabled"] for k in ("gems", "disassemble", "rename"))
          and all(not rows[k]["enabled"] and "workshop" in rows[k].get("reason", "")
                  for k in ("assemble", "repair", "combine")),
          f"save room: Assemble, Repair, Combine greyed with MENUC's line ({[(k, r['enabled'], r.get('reason')) for k, r in rows.items()]})")
    b.shot("rows-no-workshop")
    b.s_close()


def full_weapons_check(b, check):
    """Eight weapons (records faked: a blade id in each empty one; put back after): Assemble greyed."""
    base = b.sym["vs_main_inventory"]
    saved = b.g.read(base, 0x100)
    for i in range(8):
        if not saved[i * WEAPON + 1]:
            b.g.write(base + i * WEAPON + 1, b"\x01\x01")
    b.s_open()
    rows = {r["id"]: r for r in b.ws().get("rows") or []}
    a = rows.get("assemble", {})
    check(not a.get("enabled") and "carry" in a.get("reason", ""),
          f"eight weapons: Assemble greyed with MENUC's weaponsFull ({a})")
    b.press("triangle", settle=10)
    b.g.write(base, saved)
    b.s_close()


# The command column (ui_workshop_draw.cpp commands(): x from the theme's margin_x 56, 190 wide under a 1100
# wide layout; rows CMD_H 40 apart under a 25-unit header, below the Items chips): where Repair should be
CMD_X = 56 + 190 // 2
CMD_FIRST_Y, CMD_H = 162, 40


def find_command(b, row):
    """The y of a command's row: hover the column from where the layout puts it, outwards, until the
    command's focus follows the pointer (the shell focuses what the pointer is over); its band's middle."""
    guess = CMD_FIRST_Y + row * CMD_H + CMD_H // 2
    hits = []
    for dy in sorted(range(-120, 121, 4), key=abs):
        y = guess + dy
        b.ui(pointer={"x": CMD_X, "y": y})
        b.g.run(2)
        if b.ws().get("row") == row:
            hits.append(y)
        elif hits and abs(dy) > abs(hits[0] - guess) + CMD_H:
            break
    if not hits:
        raise TimeoutError(f"{b.name}: command {row} not found under the pointer")
    return (min(hits) + max(hits)) // 2


def mouse_check(b, check):
    """A click on a command opens it (Repair: its Yes / No); Cross backs out."""
    b.s_open()
    try:
        y = find_command(b, 4)  # Repair
    except TimeoutError as e:
        check(False, f"mouse: {e}")
        b.s_close()
        return
    b.ui(pointer={"x": CMD_X, "y": y, "click": True})
    b.g.run(4)
    w = b.ws()
    check(w.get("mode") == "confirm" and w.get("flow") == "repair", f"mouse: a click on Repair opens it ({w.get('mode')})")
    b.ui(pointer={"x": -1, "y": -1})
    b.press("cross", settle=4)
    check(b.ws().get("mode") == "rows", "mouse: Cross backs out to the commands")
    b.s_close()


def material_check(b, check):
    """Workshop 4 (silver, damascus): Combine's blade list greys the bronze blades."""
    b.warp(*WORKSHOP_4)
    b.s_open()
    w = b.ws()
    check(w.get("workshop") == 4 and w.get("materials") == 0xC0, f"zone 45: workshop 4 ({w.get('workshop')}, "
          f"materials {w.get('materials')})")
    b.s_command(5)
    w = b.ws()
    cats = w.get("list") or []
    if cats and cats[0].get("enabled"):
        b.s_category(0)
        b.s_slot(0)
        lst = b.ws().get("list") or []
        greyed = [e for e in lst[1:] if not e["enabled"] and "material" in e.get("reason", "")]
        check(len(lst) > 1 and len(greyed) == len(lst) - 1,
              f"workshop 4: every blade greyed for its material ({[(e['label'], e['enabled']) for e in lst]})")
        b.shot("combine-material")
    else:
        check(True, "workshop 4: fewer than two blades (combine blades greyed)")
    b.s_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "ui_workshop").resolve()
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    if not os.environ.get("ROOD_OUT"):
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    failures = []
    t0 = time.monotonic()

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    # (a) the original Setup screen
    shutil.copyfile(args.card, card)
    a = Session("original", out, card, env, bool(args.out))
    try:
        a.g.call("ui_menu", modern=False)
        state = a.g.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1
        a.prepare()
        start = a.dump()
        ops = plan(a.bag())
        if ops is None:
            print("SKIP: the card needs an equipped weapon and shield, two more weapons, a second shield and "
                  "two unworn armor pieces")
            return 77
        a.warp(*WORKSHOP)
        dumps_a, mems_a = [start], []
        for op in ops:
            try:
                original_op(a, op)
            except TimeoutError as e:
                check(False, f"original: {op[0]}: {e}")
                a.shot("stuck")
                return 1
            dumps_a.append(a.dump())
            mems_a.append(a.memories())
            if op[1] == "gems_same":
                check(a.statuses() & ENCHANT and not diff(dumps_a[-2], dumps_a[-1], ("inventory", "indices")),
                      f"original: {op[0]}: the bag as it was, the enchantment stays ({a.statuses():#x})")
            elif op[1] == "gems_back":
                check(not a.statuses() & ENCHANT, f"original: {op[0]}: the enchantment ended ({a.statuses():#x})")
            else:
                check(diff(dumps_a[-2], dumps_a[-1], ("inventory", "indices")) != [],
                      f"original: {op[0]}: the bag changed")
    finally:
        a.close()
    print(f"original: {len(ops)} operations in {time.monotonic() - t0:.0f} s", flush=True)

    # (b) the shell
    shutil.copyfile(args.card, card)
    b = Session("shell", out, card, env, bool(args.out))
    preview = {}
    try:
        b.g.call("ui_menu", modern=True)
        b.prepare()
        rest = diff(dumps_a[0], b.dump())
        check(not rest, f"both processes start equal {describe(dumps_a[0], b.dump(), rest)}")
        greying_checks(b, check)
        b.warp(*WORKSHOP)
        b.s_open()
        b.shot("rows")
        b.s_close()
        for op, want, mem_want in zip(ops, dumps_a[1:], mems_a):
            try:
                shell_op(b, op, check, preview)
            except TimeoutError as e:
                check(False, f"shell: {op[0]}: {e}")
                b.shot("stuck")
                break
            got = b.dump()
            rest = diff(want, got)
            check(not rest, f"parity: {op[0]}" + (f" (differs: {describe(want, got, rest)})" if rest else ""))
            mem = b.memories()
            check(mem == mem_want, f"parity: {op[0]}: MENUC's cursor memories" + (
                "" if mem == mem_want else f" ({mem_want.hex()} vs {mem.hex()})"))
        full_weapons_check(b, check)
        mouse_check(b, check)
        material_check(b, check)
    finally:
        b.close()
    print(f"total {time.monotonic() - t0:.0f} s")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
