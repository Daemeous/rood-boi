#!/usr/bin/env python3
"""Modern menus, the Loot screen (pc/src/ui/loot_bridge.c, ui_loot.cpp,
ui_loot_draw.cpp; the direct-entry hook in menu_bridge.c): the same effect on
game state as the original loot menu (MENUB) for take, take all, keys, leave,
a chest, disassemble, consolidate and Organize inventory's discard.

    python3 pc/tests/ui_loot_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Needs a card whose first save is in the save room with an equipped weapon and
another in the bag (skipped otherwise). 4:3, 960x720 window (layout units
1:1).

Fixtures: the game opens its loot menu itself (after a battle: BATTLE
func_800CB158 on the drops list; at a chest: func_800CB18C on the chest's
loot). The test opens it the same way through RPC loot (loot_bridge.c
pc_ui_loot_debug_open): loot lists built here (BATTLE vs_battle_loot, 0x220
bytes: copies of records of the bag's own weapon, armor and a gem, misc
items, keys), linked as the drops list (or one as a chest's) and opened with
func_800CB158 / func_800CB18C, in the field's menu mode as the field's
Triangle opens the menu (func_8007357C). Both processes get the same lists
and the same RAM set-up before the sessions that need it (the blades bag
filled to 16 with copies of a bag blade; the misc bag filled to 64 records
around a Cure Root stack of 90).

Parity: the same sessions run in two game processes from copies of the same
card: (a) ui_menus off: the original MENUB (Up/Down, Circle, Square, Cross,
its Yes / No; its Organize menu and, for Organize inventory, the Items menu it
shares with MENU3: L1/R1 to the category, Circle twice, Discard, Yes); (b)
ui_menus on: the shell's Loot screen (the same buttons; its Organize menu,
its Yes / No, its Organize panel: Right to the category, Circle, Yes). After
every session (the menu closed: currentMenu 0x3F) vs_main_inventory (0xF00
bytes), vs_main_inventoryIndices (176 bytes), D_800F4E98 bytes 0..3 (unk0,
unk2: the loot's result, 1 when something was taken or nothing was left)
and the keys' state flags (key id + 438) are compared byte for byte. The
sessions:
  1 take one (the weapon; in the shell: a mouse click on its row), leave the
    rest (Cross, Yes: itemsRemaining)
  2 take all with a key (Square: armor, Cure Root, a key; its flag set; the
    list empties: the session ends)
  3 keys: Cross refused with a key in the list (invalidDiscard, any button
    back), the key taken (Circle), the rest left (Yes)
  4 leave untouched after a battle (Cross, Yes: discardAllConfirm; unk2 0)
  5 a chest left untouched (Cross: ends at once, unk2 0)
  6 a chest taken (Circle: the list empties)
  7 disassemble: a weapon with a gem that does not fit (the blades bag
    full): Organize, Disassemble, Yes (blade, grip and gem become rows);
    Square takes grip and gem; the blade does not fit: Organize, Organize
    inventory, a copy blade discarded (Yes), back; the blade taken
  8 consolidate: two Cure Root drops that do not fit (the misc bag full, its
    Cure Root stack at 90): Organize, Consolidate, Yes (a row of 10 that fits
    into the stack, one of 70 that does not); the 10 taken; the rest left
  9 the difficulty filter: drops whose difficultyFlags exclude the current
    difficulty only: the list comes empty (the game's emptyChest line, any
    button ends it; unk2 as the opening left it)
  10 two loot lists linked (two nodes, as after a fight with two enemies
    dropping): take all
  11 the No paths: Organize opened and backed out of; Disassemble and
    Consolidate answered No; Leave answered No; then Leave, Yes
  12 Organize inventory's other actions (MENUB's Items menu): a quantity
    discarded from a misc stack (the picker), a stack moved onto another of
    the same item and a sort; the drop then fits and is taken. The one
    deliberate difference: the original (MENUB's merge) loses the stack
    moved (Cure Bulb 3 + 3 leaves 3), the shell merges as the Items menu
    (MENU3) does and keeps the sum (6). Parity for this session: every
    region but the misc records and their list equal byte for byte; the misc
    records equal as a multiset of (id, count) but the merged stack; then
    the shell's bag is set to the original's so the sessions after it start
    equal again
  13 a shield's disassembly (the shields bag full): its gem becomes a row,
    taken; the rest left
And first, before any RAM set-up, the real chest flow on the save room's own
chest (BATTLE func_80074050, what the field's Cross at a chest calls: field
mode 9, the lid, func_800CB18C on the chest's loot, func_800744B8 ticking
the menu and closing the chest; RPC chest):
  R1 the chest left untouched (Cross: ends at once; the lid closes, its
    opened flag stays clear)
  R2 the chest again: one row taken, the rest left (its flag set)
After every session also: _lootListHead, the game heap's free total (not
below its value before the session in each process: nothing allocated is
kept; the first session also sees a start-up block freed), D_800F190C (the world runs), D_800EB9AF (the HUD flag),
D_800F4EE8.unk3A (the Items menu's current category and list memories) and
the chest's opened flag. In (a) each session must have done what it says
(rows taken, the result), so a step the original menu missed cannot pass as
parity.

The after-battle flow (BATTLE func_800797BC / func_8007983C after a fight
with drops) is not driven for real: no enemy near the save room can be
fought and killed by a script reliably; its menu entry is func_800CB158 on
the drops list as the RPC loot fixture calls it (only the field mode that
ticks the menu differs).

Shell-only checks (process b): the rows' fits / needs / organize / key
fields; the Organize menu's entries; the shell closes with the session; a
soft reset with the Organize panel open (the world frozen): over the title
nothing of the screen is left, Continue gives a running world (D_800F190C
1), the HUD flag as before and a loot menu that opens and closes again; a
16:9 screenshot with --out.
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
LOOT_MENU = 0xC
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (vs_battle_menuItems)

# vs_main_inventory_t / vs_main_inventoryIndices_t (BATTLE func_8006B57C_t.h)
INVENTORY_BYTES, INDICES_BYTES = 0xF00, 176
WEAPONS, SHIELDS, BLADES, GRIPS, ARMOR, GEMS, MISC = 0, 0x100, 0x280, 0x540, 0x640, 0x8C0, 0xE00
WEAPON, BLADE, GRIP, ARMOR_REC, GEM, SHIELD = 32, 44, 16, 40, 28, 48
IDX = {0: (0, 8), 1: (8, 16), 2: (24, 16), 3: (40, 8), 4: (48, 16), 5: (64, 48), 6: (112, 64)}
CATEGORIES = ["weapons", "blades", "grips", "shields", "armor", "gems", "misc"]
SETTINGS_CURSOR_MEMORY = 1  # vs_main_settings.cursorMemory
CATEGORY_MEMORY = 0x3A      # D_800F4EE8.unk3A.currentItemCategory: category + 1 (0: the Manage page)

# vs_battle_loot (func_8006B57C_t.h): weapon 0 (blade 4, grip 48, gems 64, name 148), blade 0xAC, grip 0xDC,
# shield 0xF0, armor 0x170 / 0x19C, accessory 0x1C8, gem 0x1F4 (record at 0x1F8), misc 0x214 (3 x {id, flags, count})
LOOT = 0x220
EVERY_DIFFICULTY = 7  # difficultyFlags: droppable at every difficulty

CURE_ROOT, CURE_BULB, KEYS = 323, 324, (459, 460)
SAVE_ROOM_CHEST_FLAG = 47 + 0x3C0  # the save room's chest (MPD section 13 unk10 47): opened flag 0x3C0 + 47
FIELD_FREE = 1  # D_800F196C: the field's mode with Ashley free to act
KEY_FLAG = 438  # MENUB _setKeyFlag: vs_battle_setStateFlag(id + 438, 1)

# Shell layout (4:3, 960 x 720 layout units; ui_loot_draw.cpp): the list's first row
LIST_X, LIST_Y0, ROW_H = 300, 167, 48


def gem_record(set_in=0):
    """A Feldspar-like gem record (vs_main_inventoryGem, 28 bytes)."""
    return struct.pack("<HBBBbbb8b8bHh", 261, 0, 0, 0, 2, 0, 1, *([5, 0, 0, 0, 0, -3, 0, 0]),
                       *([0, 4, 0, 0, 0, 0, 0, 0]), set_in, 1)


class Loot:
    """A vs_battle_loot being built."""

    def __init__(self, flags=EVERY_DIFFICULTY):
        self.b = bytearray(LOOT)
        self.flags = flags

    def weapon(self, inv, w, gem=False):
        b = self.b
        wr = inv[WEAPONS + w * WEAPON:WEAPONS + (w + 1) * WEAPON]
        blade, grip = wr[1], wr[2]
        b[0] = self.flags
        b[4:48] = inv[BLADES + (blade - 1) * BLADE:BLADES + blade * BLADE]
        b[48:64] = inv[GRIPS + (grip - 1) * GRIP:GRIPS + grip * GRIP]
        if gem:
            b[48 + 4] = max(b[48 + 4], 1)  # the grip gets a gem slot
            b[64:64 + GEM] = gem_record()
        b[148:172] = wr[8:32]
        return self

    def armor(self, inv, a, slot=0):
        o = 0x170 + slot * 44
        self.b[o] = self.flags
        rec = bytearray(inv[ARMOR + a * ARMOR_REC:ARMOR + (a + 1) * ARMOR_REC])
        rec[38] = 0  # not worn
        self.b[o + 4:o + 44] = rec
        return self

    def shield(self, inv, k):
        """Bag shield k's record (vs_battle_lootedShield at 0xF0: flags, the armor-like record, three gems) with
        one gem set in it."""
        o = 0xF0
        self.b[o] = self.flags
        rec = bytearray(inv[SHIELDS + k * SHIELD + 4:SHIELDS + k * SHIELD + 44])
        rec[12] = max(rec[12], 1)  # gemSlots
        rec[38] = 0
        self.b[o + 4:o + 44] = rec
        self.b[o + 44:o + 44 + GEM] = gem_record()
        return self

    def gem(self):
        self.b[0x1F4] = self.flags
        self.b[0x1F8:0x1F8 + GEM] = gem_record()
        return self

    def misc(self, *items):
        for k, (i, n) in enumerate(items):
            struct.pack_into("<HBB", self.b, 0x214 + 4 * k, i, self.flags, n)
        return self

    def hex(self):
        return bytes(self.b).hex()


class Session:
    """One game process continued to the save room."""

    def __init__(self, name, out, card, env, shots):
        self.name, self.out, self.shots = name, out, shots
        self.g = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env)
        self.sym = symbols()
        self.inventory, self.indices = self.sym["vs_main_inventory"], self.sym["vs_main_inventoryIndices"]

    def close(self):
        self.g.close()

    def shot(self, name):
        if self.shots:
            self.g.present_screenshot(self.out / f"{self.name}-{name}.png")

    def ptr(self, address):
        return int.from_bytes(self.g.read(address, PTR), "little")

    def byte(self, name, signed=False):
        v = self.g.read(self.sym[name], 1)[0]
        return v - 256 if signed and v > 127 else v

    def menu(self):
        return self.g.read(self.sym["vs_battle_menuState"], 1)[0]

    def inv(self):
        return self.g.read(self.inventory, INVENTORY_BYTES)

    def idx(self):
        return self.g.read(self.indices, INDICES_BYTES)

    def flags(self):
        return bytes(self.g.call("state_flag", id=k + KEY_FLAG)["value"] for k in KEYS)

    def u32(self, address):
        return int.from_bytes(self.g.read(address, 4), "little")

    def heap_free(self):
        """The game heap's free total (heapA's circular free list: +4 next, +8 the block's size)."""
        head = self.sym["heapA"]
        node, total = self.u32(head + 4), 0
        for _ in range(4096):
            if node in (head, 0):
                break
            total += self.u32(node + 8)
            node = self.u32(node + 4)
        return total

    def field_mode(self):
        return self.u32(self.sym["D_800F196C"])

    def dump(self):
        """What a session may change (compared byte for byte between the processes)."""
        return {"inventory": self.inv(), "indices": self.idx(), "D_800F4E98": self.g.read(self.sym["D_800F4E98"], 4),
                "key flags": self.flags(),
                "chest flag": bytes([self.g.call("state_flag", id=SAVE_ROOM_CHEST_FLAG)["value"]]),
                "_lootListHead": self.g.read(self.sym["_lootListHead"], PTR),
                "D_800F190C": self.g.read(self.sym["D_800F190C"], 4),
                "D_800EB9AF": self.g.read(self.sym["D_800EB9AF"], 1),
                "unk3A": self.g.read(self.sym["D_800F4EE8"] + 0x3A, 18),
                "heap free": struct.pack("<I", self.heap_free())}

    def wait(self, pred, limit, what, step=1, quiet=False):
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

    def open(self, loots, chest=False):
        """The loot menu, as the game opens it (RPC loot; chest "real": the save room's own chest through the
        field's chest flow, RPC chest)."""
        if chest == "real":
            r = self.g.call("chest", open=0)
            if not r.get("opened"):
                raise TimeoutError(f"{self.name}: the chest did not open ({self.g.call('chest')})")
            self.wait(lambda: self.menu() == LOOT_MENU, 600, "the chest's loot menu opens", step=4)
            return
        r = self.g.call("loot", hex="".join(x.hex() for x in loots), chest=chest)
        if not r.get("opened"):
            raise TimeoutError(f"{self.name}: the loot menu did not open")

    def closed(self):
        self.wait(lambda: self.menu() == CLOSED and self.field_mode() == FIELD_FREE, 900,
                  "the loot menu closes (the field free again)", step=4)
        self.g.run(60)

    # ---- set-up ----
    def write_inv(self, offset, data):
        self.g.write(self.inventory + offset, data)

    # ---- the original (MENUB) ----
    def menu_items(self):
        p = self.ptr(self.sym["vs_battle_menuItems"])
        if not p:
            return {}
        raw = self.g.read(p, 64 * 40)
        return {i: (raw[i * 64], raw[i * 64 + 6]) for i in range(40) if raw[i * 64]}

    def wait_ready(self, want=None, limit=400):
        calm = 0

        def ready():
            nonlocal calm
            items = self.menu_items()
            ok = all(s <= 1 for s, _ in items.values()) and (want is None or want in items)
            calm = calm + 1 if ok else 0
            return calm >= 3
        self.wait(ready, limit, f"menu item {want} ready", step=2)
        self.g.run(2)

    def o_input(self, limit=400):
        """MENUB's list takes input: _processLootMenu's processInput (D_8010A6B7 the row + 1), rows in place."""
        calm = 0

        def ready():
            nonlocal calm
            v = self.byte("D_8010A6B7")
            ok = 1 <= v <= 8 and self.byte("_lootLeftEdge") == 0 and self.byte("_controlsEnabled") == 1
            calm = calm + 1 if ok else 0
            return calm >= 3
        self.wait(ready, limit, "MENUB's list takes input", step=2)

    def o_cursor(self):
        return self.byte("_selectedLoot", True) + self.g.read(self.sym["_lootListOffset"], 1)[0]

    def o_count(self):
        return self.byte("_lootListCount")

    def o_row(self, r):
        for _ in range(20):
            if self.o_cursor() == r:
                return
            self.press("up" if self.o_cursor() > r else "down")
        raise TimeoutError(f"{self.name}: loot row {r} not reached ({self.o_cursor()})")

    def o_pick(self, want, ids=(30, 31)):
        """A two-row menu (Yes / No, Organize: menu items 30 and 31): to item `want`, Circle."""
        self.wait_ready(want=ids[1])
        self.g.run(4)
        for _ in range(4):
            items = self.menu_items()
            sel = next((i for i in ids if i in items and items[i][1]), None)
            if sel == want:
                break
            self.press("up")
        items = self.menu_items()
        if not (want in items and items[want][1]):
            raise TimeoutError(f"{self.name}: menu item {want} not selected ({items})")
        self.press("circle")

    def o_category(self):
        return (self.g.read(self.sym["D_800F4EE8"] + CATEGORY_MEMORY, 1)[0] - 1) & 7

    def o_items_category(self, c):
        """Organize inventory (MENUB's Items menu) on category c (L1 / R1)."""
        self.wait_ready(want=20)
        for _ in range(10):
            if self.o_category() == c:
                break
            self.press("r1" if (c - self.o_category()) % 8 <= 4 else "l1", settle=4)
            self.g.run(4)
            self.wait_ready(want=20)
        if self.o_category() != c:
            raise TimeoutError(f"{self.name}: Items category {c} not reached ({self.o_category()})")

    def o_items_discard(self, c, r):
        """Organize inventory (MENUB's Items menu): category c, row r discarded (Circle, Circle, Discard, Yes),
        then Cross back to the loot."""
        self.o_items_category(c)
        sel = lambda: next((i for i in range(20, 40) if i in self.menu_items() and self.menu_items()[i][1]), None)
        for _ in range(20):
            if sel() == 20 + r:
                break
            self.press("up" if (sel() or 20) > 20 + r else "down")
        if sel() != 20 + r:
            raise TimeoutError(f"{self.name}: Items row {r} not reached ({sel()})")
        self.press("circle")
        self.wait_ready(want=20)
        self.press("circle")
        self.wait_ready(want=32)
        self.g.run(4)
        self.press("down", 1 if c in (1, 2, 5, 6) else 0)  # Search (blades, grips, gems) / Use (misc) first
        self.press("circle")
        self.wait_ready(want=33)
        self.g.run(4)
        self.press("up")
        self.press("circle")
        self.wait_ready(want=20)
        self.press("cross", settle=10)

    # ---- the shell ----
    def ui(self, **params):
        return self.g.call("ui_menu", **params)

    def lt(self):
        return self.ui().get("loot") or {}

    def s_input(self):
        self.wait(lambda: self.lt().get("valid"), 600, "the Loot screen shows", step=4)
        self.g.run(40)  # past the opening press guard

    def s_row(self, r):
        for _ in range(20):
            if self.lt().get("cursor") == r:
                return
            self.press("down", settle=4)
        raise TimeoutError(f"{self.name}: loot row {r} not reached")

    def s_mode(self, mode, limit=60):
        self.wait(lambda: self.lt().get("mode") == mode, limit, f"the Loot screen in {mode}", step=2)

    def s_bag_cursor(self, r):
        for _ in range(70):
            cur = self.lt().get("bag_panel", {}).get("cursor")
            if cur == r:
                return
            self.press("up" if cur is not None and cur > r else "down", settle=4)
        raise TimeoutError(f"{self.name}: bag row {r} not reached")

    def s_bag_action(self, action):
        """Circle on the focused bag row (its actions), to `action`, Circle."""
        self.press("circle", settle=4)
        self.s_mode("bag_actions")
        ids = [x["id"] for x in self.lt().get("actions", [])]
        if action not in ids:
            raise TimeoutError(f"{self.name}: no {action} in {ids}")
        for _ in range(len(ids) + 1):
            if self.lt().get("menu") == ids.index(action):
                break
            self.press("down", settle=4)
        self.press("circle", settle=4)

    def s_yes(self):
        for _ in range(3):
            if self.lt().get("yes"):
                break
            self.press("up", settle=4)
        if not self.lt().get("yes"):
            raise TimeoutError(f"{self.name}: Yes not reached")
        self.press("circle", settle=4)


SIZES = [(WEAPONS, WEAPON, 8), (BLADES, BLADE, 16), (GRIPS, GRIP, 16), (SHIELDS, 48, 8), (ARMOR, ARMOR_REC, 16),
         (GEMS, GEM, 48), (MISC, 4, 64)]


def record_id(inv, c, k):
    """MAINMENU _getItemId: weapons their blade, shields base.id (bytes); the rest a u16 id first."""
    base, size, _ = SIZES[c]
    o = base + k * size
    if c == 0:
        return inv[o + 1]
    if c == 3:
        return inv[o + 4]
    if c == 4:
        return inv[o]
    return struct.unpack_from("<H", inv, o)[0]


def count(d, c):
    return sum(1 for k in range(SIZES[c][2]) if record_id(d["inventory"], c, k))


def misc_counts(d, item):
    inv = d["inventory"]
    return [inv[MISC + k * 4 + 2] for k in range(64) if struct.unpack_from("<H", inv, MISC + k * 4)[0] == item]


def misc_multiset(d, but):
    """The misc records as a sorted list of (id, count), item `but` left out."""
    inv = d["inventory"]
    recs = (struct.unpack_from("<HB", inv, MISC + k * 4) for k in range(64))
    return sorted((i, n) for i, n in recs if i and i != but)


def list_rows(idx, c):
    lo, size = IDX[c]
    return [i - 1 for i in idx[lo:lo + size] if i]


# ---- the sessions, through each path ----
# A session: (name, loots, chest, prep, steps); prep(session) writes RAM (both processes the same), steps is a list
# of actions run in order; each path interprets them:
#   ("take", row) ("all",) ("leave_yes",) ("leave_now",) ("keys_back",) ("disassemble", row)
#   ("consolidate", row) ("organize_discard", row, category, bag row)


def original_bag_ops(a, ops):
    """In MENUB's Items menu (Organize inventory, on the category): each op as its list and menus take it."""
    sel = lambda: next((i for i in range(20, 40) if i in a.menu_items() and a.menu_items()[i][1]), None)

    def to_row(r):
        for _ in range(20):
            if sel() == 20 + r:
                return
            a.press("up" if (sel() or 20) > 20 + r else "down")
        raise TimeoutError(f"{a.name}: Items row {r} not reached ({sel()})")

    def actions(down):
        """Circle on the marked row again (its actions), `down` rows down the action menu, Circle."""
        a.press("circle")
        a.wait_ready(want=32)
        a.g.run(4)
        a.press("down", down)
        a.press("circle")

    for op in ops:
        if op[0] == "discard_n":  # misc: Use, Discard: the picker (Up by 1), Circle, Yes
            to_row(op[1])
            a.press("circle")
            a.wait_ready(want=20)
            actions(1)
            a.wait(lambda: 34 in a.menu_items(), 200, "the quantity picker", step=2)
            a.g.run(20)
            a.press("up", op[2])
            a.press("circle")
            a.wait_ready(want=33)
            a.g.run(4)
            a.press("up")
            a.press("circle")
        elif op[0] == "move":  # the row picked, the target picked
            to_row(op[1])
            a.press("circle")
            a.wait_ready(want=20)
            to_row(op[2])
            a.press("circle")
        elif op[0] == "sort":  # misc: Use, Discard, Discard all, Sort; the stat's row
            to_row(0)
            a.press("circle")
            a.wait_ready(want=20)
            actions(3)
            a.wait_ready(want=32)
            a.g.run(4)
            a.press("down", op[1])
            a.press("circle")
        a.wait_ready(want=20)
        a.g.run(10)


def original_session(a, s):
    """Runs the session; returns its steps with the rows found as they ran (for the shell)."""
    name, loots, chest, prep, steps, _ = s
    a.open(loots, chest)
    if steps[0][0] == "empty":  # the emptyChest line (state 4): any button once it is shown
        a.g.run(60)
        for _ in range(10):
            if a.menu() != LOOT_MENU:
                break
            a.press("circle", settle=20)
        a.closed()
        return list(steps)
    a.o_input()
    done = []
    for st in steps:
        st = tuple(x(a) if callable(x) else x for x in st)  # a row found from the RAM now
        done.append(st)
        kind = st[0]
        if kind == "take":
            a.o_row(st[1])
            n = a.o_count()
            a.press("circle")
            a.wait(lambda: a.o_count() < n or a.menu() != LOOT_MENU, 120, "the row is taken", step=2)
            if a.menu() == LOOT_MENU and a.o_count() > 0:
                a.o_input()
        elif kind == "all":
            n = a.o_count()
            a.press("square")
            a.wait(lambda: a.o_count() < n or a.menu() != LOOT_MENU, 120, "rows are taken", step=2)
            if a.menu() == LOOT_MENU and a.o_count() > 0:
                a.o_input()
        elif kind == "leave_yes":
            a.press("cross")
            a.o_pick(30)
        elif kind == "leave_now":
            a.press("cross")
        elif kind == "leave_no":
            a.press("cross")
            a.o_pick(31)
            a.o_input()
        elif kind in ("disassemble_no", "consolidate_no"):
            a.o_row(st[1])
            a.press("circle")
            a.o_pick(30)  # the row's own action
            a.o_pick(31)  # No
            a.o_input()
        elif kind == "organize_cancel":
            a.o_row(st[1])
            a.press("circle")
            a.wait_ready(want=31)
            a.g.run(4)
            a.press("cross")
            a.o_input()
        elif kind == "keys_back":
            a.press("cross")
            a.g.run(40)
            a.press("circle")
            a.o_input()
        elif kind in ("disassemble", "consolidate"):
            a.o_row(st[1])
            a.press("circle")
            a.o_pick(30)  # the row's own action
            a.o_pick(30)  # Yes (these open on Yes)
            a.o_input()
        elif kind == "organize_discard":
            a.o_row(st[1])
            a.press("circle")
            a.o_pick(31)  # Organize inventory
            a.o_items_discard(st[2], st[3])
            a.o_input()
        elif kind == "bag_ops":
            a.o_row(st[1])
            a.press("circle")
            a.o_pick(31)  # Organize inventory
            a.o_items_category(st[2])
            original_bag_ops(a, st[3])
            a.press("cross", settle=10)
            a.o_input()
    a.closed()
    return done


def shell_session(b, s, check):
    name, loots, chest, prep, steps, _ = s
    b.open(loots, chest)
    b.s_input()
    first = b.lt()
    if steps[0][0] == "empty":
        b.s_mode("message")
        check(b.lt().get("ask") == "empty" and b.lt().get("text"), f"{name}: the empty list's line "
              f"'{b.lt().get('text')}'")
        b.press("circle", settle=4)
        b.closed()
        check(not b.ui().get("loot"), f"{name}: the Loot screen closed with the session")
        return first
    for st in steps:
        kind = st[0]
        if kind == "take":
            b.s_row(st[1])
            n = b.lt().get("rows")
            if st[1] == 0 and name.startswith("1"):  # the mouse: a click on the row takes it
                b.ui(pointer={"x": LIST_X, "y": LIST_Y0 + ROW_H // 2, "click": True})
                b.g.run(6)
            else:
                b.press("circle", settle=4)
            b.wait(lambda: b.lt().get("rows", 0) < n or b.menu() != LOOT_MENU, 60, "the row is taken", step=2)
        elif kind == "all":
            n = b.lt().get("rows")
            b.press("square", settle=4)
            b.wait(lambda: b.lt().get("rows", 0) < n or b.menu() != LOOT_MENU, 60, "rows are taken", step=2)
        elif kind == "leave_yes":
            b.press("cross", settle=4)
            b.s_mode("confirm")
            check(b.lt().get("ask") == "leave" and not b.lt().get("yes"), f"{name}: Leave asks, on No")
            b.s_yes()
        elif kind == "leave_now":
            b.press("cross", settle=4)
        elif kind == "keys_back":
            b.press("cross", settle=4)
            b.s_mode("message")
            m = b.lt()
            check(m.get("ask") == "keys" and m.get("text"), f"{name}: Leave refused with a key: '{m.get('text')}'")
            b.shot("keys")
            b.press("circle", settle=4)
            b.s_mode("list")
        elif kind in ("disassemble", "consolidate"):
            b.s_row(st[1])
            row = b.lt()["list"][st[1]]
            check(not row["fits"] and row["organize"] == kind, f"{name}: row {st[1]} does not fit, offers {kind} "
                  f"({row})")
            b.press("circle", settle=4)
            b.s_mode("organize")
            m = b.lt()
            check(m.get("entries") == [kind, "organize"] and m.get("menu") == 0,
                  f"{name}: Organize: {m.get('entries')} on {m.get('menu')}")
            b.shot(f"organize-{kind}")
            b.press("circle", settle=4)
            b.s_mode("confirm")
            check(b.lt().get("ask") == kind and b.lt().get("yes"), f"{name}: {kind} asks, on Yes")
            b.press("circle", settle=4)
            b.s_mode("list")
        elif kind == "organize_discard":
            b.s_row(st[1])
            b.press("circle", settle=4)
            b.s_mode("organize")
            entries = b.lt().get("entries")
            if entries and entries[b.lt().get("menu", 0)] != "organize":
                b.press("down", settle=4)
            b.press("circle", settle=4)
            b.s_mode("bag")
            for _ in range(8):
                if b.lt().get("bag_panel", {}).get("category") == CATEGORIES[st[2]]:
                    break
                b.press("right", settle=4)
            for _ in range(70):
                if b.lt().get("bag_panel", {}).get("cursor") == st[3]:
                    break
                b.press("down", settle=4)
            b.shot("bag")
            b.s_bag_action("discard")
            b.s_mode("bag_confirm")
            b.s_yes()
            b.s_mode("bag")
            b.press("cross", settle=4)
            b.s_mode("list")
        elif kind == "leave_no":
            b.press("cross", settle=4)
            b.s_mode("confirm")
            b.press("circle", settle=4)  # on No
            b.s_mode("list")
        elif kind in ("disassemble_no", "consolidate_no"):
            b.s_row(st[1])
            b.press("circle", settle=4)
            b.s_mode("organize")
            b.press("circle", settle=4)
            b.s_mode("confirm")
            b.press("down", settle=4)
            check(not b.lt().get("yes"), f"{name}: {kind}: on No")
            b.press("circle", settle=4)
            b.s_mode("list")
        elif kind == "organize_cancel":
            b.s_row(st[1])
            b.press("circle", settle=4)
            b.s_mode("organize")
            b.press("cross", settle=4)
            b.s_mode("list")
        elif kind == "bag_ops":
            b.s_row(st[1])
            b.press("circle", settle=4)
            b.s_mode("organize")
            entries = b.lt().get("entries")
            if entries and entries[b.lt().get("menu", 0)] != "organize":
                b.press("down", settle=4)
            b.press("circle", settle=4)
            b.s_mode("bag")
            for _ in range(8):
                if b.lt().get("bag_panel", {}).get("category") == CATEGORIES[st[2]]:
                    break
                b.press("right", settle=4)
            for op in st[3]:
                if op[0] == "discard_n":
                    b.s_bag_cursor(op[1])
                    b.s_bag_action("discard")
                    b.s_mode("bag_quantity")
                    b.press("up", op[2], settle=4)
                    check(b.lt().get("count") == op[2], f"{name}: the picker at {b.lt().get('count')}")
                    b.shot("quantity")
                    b.press("circle", settle=4)
                    b.s_mode("bag_confirm")
                    b.s_yes()
                elif op[0] == "move":
                    b.s_bag_cursor(op[1])
                    b.s_bag_action("move")
                    b.s_mode("bag_move")
                    b.s_bag_cursor(op[2])
                    b.shot("move")
                    b.press("circle", settle=4)
                elif op[0] == "sort":
                    b.s_bag_cursor(0)
                    b.s_bag_action("sort")
                    b.s_mode("bag_sort")
                    b.press("down", op[1], settle=4)
                    b.shot("sort")
                    b.press("circle", settle=4)
                b.s_mode("bag")
                b.g.run(4)
            b.press("cross", settle=4)
            b.s_mode("list")
    b.closed()
    check(not b.ui().get("loot"), f"{name}: the Loot screen closed with the session")
    return first


def soft_reset(b, check):
    """A soft reset (L1 L2 R1 R2 Start Select) with the Organize panel open (the world frozen): the session is
    dropped (menu_bridge.c pc_ui_menu_reset), Continue gives a running world and a loot menu that works."""
    g = b.g
    hud = b.g.read(b.sym["D_800EB9AF"], 1)[0]
    b.open([Loot().misc((345, 1))])  # the misc bag full (session 12): no room
    b.s_input()
    b.press("circle", settle=4)
    b.s_mode("organize")
    b.press("down", settle=4)  # Organize inventory (after Consolidate)
    b.press("circle", settle=4)
    b.s_mode("bag")
    b.shot("reset-bag")
    frozen = b.u32(b.sym["D_800F190C"])
    titles = pathlib.Path(g.log).read_text().count("TITLE MENU READY")
    g.call("input", buttons=["l1", "l2", "r1", "r2", "start", "select"], vsyncs=6)
    b.wait(lambda: pathlib.Path(g.log).read_text().count("TITLE MENU READY") > titles, 3000,
           "the title after a soft reset", step=30)
    check(frozen == 0 and not b.ui().get("loot"),
          f"reset: the panel froze the world ({frozen}); over the title the Loot screen is gone")
    g.press("start", vsyncs=4, settle=90)
    for _ in range(3):
        g.press("circle", vsyncs=3, settle=90)
    g.run(400)
    state = g.state()
    check((state["zone"], state["room"]) == (9, 17) and b.u32(b.sym["D_800F190C"]) == 1
          and g.read(b.sym["D_800EB9AF"], 1)[0] == hud,
          f"reset: Continue: zone {state['zone']} room {state['room']}, the world runs "
          f"({b.u32(b.sym['D_800F190C'])}), the HUD flag {g.read(b.sym['D_800EB9AF'], 1)[0]} ({hud})")
    b.open([Loot().misc((CURE_BULB, 1))])
    b.s_input()
    b.press("cross", settle=4)
    b.s_mode("confirm")
    b.s_yes()
    b.closed()
    check(not b.ui().get("loot") and b.menu() == CLOSED, "reset: a loot menu opens and closes again")


def diff(a, b, masked=()):
    """Differing (region, start, end) runs; masked: (region, start, end) left out."""
    found = []
    for region in a:
        x, y = a[region], b[region]
        i = 0
        while i < len(x):
            if x[i] != y[i] and not any(r == region and lo <= i < hi for r, lo, hi in masked):
                j = i
                while j < len(x) and x[j] != y[j]:
                    j += 1
                found.append((region, i, j))
                i = j
            else:
                i += 1
    return found


def describe(want, got, rest):
    return ", ".join(f"{r} {s:#x}..{e:#x} {want[r][s:e].hex()} vs {got[r][s:e].hex()}" for r, s, e in rest[:10])


def plan(inv, idx, difficulty):
    """The sessions (see the module docstring), from the bag at the start; None when the card lacks what they
    need."""
    weapons = list_rows(idx, 0)
    bag_w = [i for i in weapons if inv[WEAPONS + i * WEAPON + 1] and not inv[WEAPONS + i * WEAPON + 3]]
    armor = [i for i in list_rows(idx, 4) if inv[ARMOR + i * ARMOR_REC]]
    if not bag_w or not armor:
        return None
    w, a = bag_w[0], armor[0]
    blade = inv[WEAPONS + w * WEAPON + 1]
    blade_rec = bytearray(inv[BLADES + (blade - 1) * BLADE:BLADES + blade * BLADE])
    blade_rec[42] = 0  # not part of a weapon

    def fill_blades(s):
        """The blades bag full: copies of the bag weapon's blade in every empty record."""
        cur = s.inv()
        for k in range(16):
            if not struct.unpack_from("<H", cur, BLADES + k * BLADE)[0]:
                rec = bytearray(blade_rec)
                rec[43] = k + 1
                s.write_inv(BLADES + k * BLADE, rec)

    def fill_misc(s):
        """The misc bag full: a Cure Root stack of 90 and 63 records of one Cure Bulb each."""
        recs = [(CURE_ROOT, 90)] + [(CURE_BULB, 1)] * 63
        s.write_inv(MISC, b"".join(struct.pack("<HBB", i, n, k + 1) for k, (i, n) in enumerate(recs)))
        lo, size = IDX[6]
        s.g.write(s.indices + lo, bytes(range(1, 65)))

    shields = [k for k in list_rows(idx, 3) if inv[SHIELDS + k * SHIELD + 4]]

    def fill_shields(s):
        """The shields bag full: copies of the first bag shield's record (not equipped, no gems)."""
        cur = s.inv()
        src = bytearray(cur[SHIELDS + shields[0] * SHIELD:SHIELDS + (shields[0] + 1) * SHIELD])
        for k in range(8):
            if not cur[SHIELDS + k * SHIELD + 4]:
                rec = bytearray(src)
                rec[0], rec[1], rec[42] = k + 1, 0, k + 1
                rec[44:48] = bytes(4)
                s.write_inv(SHIELDS + k * SHIELD, rec)

    def fill_misc_stacks(s):
        """The misc bag full: two Cure Bulb stacks (5, 3), a Cure Root stack of 40, the rest stacks of 100."""
        recs = [(CURE_BULB, 5), (CURE_BULB, 3), (CURE_ROOT, 40)] + [(340, 100)] * 61
        s.write_inv(MISC, b"".join(struct.pack("<HBB", i, n, k + 1) for k, (i, n) in enumerate(recs)))
        lo, size = IDX[6]
        s.g.write(s.indices + lo, bytes(range(1, 65)))

    other = EVERY_DIFFICULTY & ~(difficulty + 1)  # droppable at no difficulty but the current one's

    def copy_blade_row(s):
        """The first copy blade's row in the blades list (Organize inventory discards it)."""
        cur, ix = s.inv(), s.idx()
        rows = list_rows(ix, 1)
        return next(r for r, k in enumerate(rows) if not cur[BLADES + k * BLADE + 42])

    sessions = [
        ("R1 the real chest left untouched", [], "real", None, [("leave_now",)], dict(taken=0, unk2=0, chest=0)),
        ("R2 the real chest: one taken, the rest left", [], "real", None, [("take", 0), ("leave_yes",)],
         dict(taken=1, unk2=1, chest=1)),
        ("1 take one, leave the rest", [Loot().weapon(inv, w).misc((CURE_ROOT, 3), (340, 2))], False, None,
         [("take", 0), ("leave_yes",)], dict(taken=1, unk2=1, more={0: 1, 1: 1, 2: 1, 6: 0})),
        ("2 take all with a key", [Loot().armor(inv, a).misc((CURE_ROOT, 5), (KEYS[0], 1))], False, None,
         [("all",)], dict(taken=3, unk2=1, flag=0)),
        ("3 a key cannot be left", [Loot().gem().misc((KEYS[1], 1))], False, None,
         [("keys_back",), ("take", 1), ("leave_yes",)], dict(taken=1, unk2=1, flag=1)),
        ("4 leave untouched", [Loot().misc((340, 1))], False, None, [("leave_yes",)], dict(taken=0, unk2=0)),
        ("5 a chest left untouched", [Loot().misc((CURE_BULB, 2))], True, None, [("leave_now",)],
         dict(taken=0, unk2=0)),
        ("6 a chest taken", [Loot().misc((CURE_BULB, 2))], True, None, [("take", 0)], dict(taken=1, unk2=1)),
        ("7 disassemble, organize", [Loot().weapon(inv, w, gem=True)], False, fill_blades,
         [("disassemble", 0), ("all",), ("organize_discard", 0, 1, copy_blade_row), ("take", 0)],
         dict(taken=3, unk2=1, more={0: 0, 1: 0, 2: 1, 5: 1})),
        ("8 consolidate", [Loot().misc((CURE_ROOT, 50), (CURE_ROOT, 30), (341, 1))], False, fill_misc,
         [("consolidate", 0), ("take", 1), ("leave_yes",)], dict(taken=1, unk2=1)),
        ("9 the difficulty filter", [Loot(flags=other).misc((CURE_ROOT, 2)).armor(inv, a)], False, None,
         [("empty",)], dict(taken=0, unk2=0)),
        ("10 two loot lists", [Loot().misc((CURE_BULB, 1)), Loot().armor(inv, a)], False, None, [("all",)],
         dict(taken=2, unk2=1, more={4: 1})),
        ("11 the No paths", [Loot().weapon(inv, w, gem=True).misc((CURE_ROOT, 5))], False, fill_blades,
         [("organize_cancel", 0), ("disassemble_no", 0), ("consolidate_no", 1), ("leave_no",), ("leave_yes",)],
         dict(taken=0, unk2=0)),
        ("12 organize: quantity, merge, sort", [Loot().misc((341, 1))], False, fill_misc_stacks,
         [("bag_ops", 0, 6, [("discard_n", 0, 2), ("move", 1, 0), ("sort", 1)]), ("take", 0)],
         dict(taken=1, unk2=1, bulbs=[3], shell_bulbs=[6])),
        # 5 - 2; 3 moved onto 3: MENUB keeps the target's 3 only, the shell (MENU3's merge) the sum
    ]
    if shields:
        sessions.append(("13 a shield disassembled", [Loot().shield(inv, shields[0])], False, fill_shields,
                         [("disassemble", 0), ("take", 1), ("leave_yes",)], dict(taken=1, unk2=1,
                                                                                    more={3: 0, 5: 1})))
    return sessions


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "ui_loot").resolve()
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    if not os.environ.get("ROOD_OUT"):  # Continue reads the card only with ROOD_OUT set (run.py sets it)
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    failures = []
    t0 = time.monotonic()

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    # (a) the original loot menu (MENUB)
    shutil.copyfile(args.card, card)
    a = Session("original", out, card, env, bool(args.out))
    try:
        a.g.call("ui_menu", modern=False)
        state = a.g.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1
        a.g.write(a.sym["vs_main_settings"] + SETTINGS_CURSOR_MEMORY, b"\x00")
        a.g.run(2)
        difficulty = a.g.read(a.sym["vs_main_stateFlags"] + 1, 1)[0]
        chest = a.g.call("chest")
        if not chest.get("chests") or chest["chests"][0]["opened"] or chest["chests"][0]["flag"] + 0x3C0 != \
                SAVE_ROOM_CHEST_FLAG:
            print(f"SKIP: the save room's chest is not there unopened ({chest})")
            return 77
        sessions = plan(a.inv(), a.idx(), difficulty)
        if sessions is None:
            print("SKIP: the card needs a weapon in the bag (not equipped) and an armor record")
            return 77
        if a.flags() != bytes(len(KEYS)):
            print(f"SKIP: the test keys' flags are already set ({a.flags().hex()})")
            return 77
        start = a.dump()
        dumps_a, steps_a = [], []
        for s in sessions:
            name, loots, chest, prep, steps, want = s
            if prep:
                prep(a)
                a.g.run(2)
            before = a.dump()
            try:
                steps_a.append(original_session(a, (name, loots, chest, prep, steps, want)))
            except TimeoutError as e:
                check(False, f"original: {name}: {e}")
                a.shot("stuck")
                return 1
            d = a.dump()
            dumps_a.append(d)
            unk2 = struct.unpack_from("<h", d["D_800F4E98"], 2)[0]
            taken = d["inventory"] != before["inventory"]
            # records per category: as many more as the session takes (`more`; blades in 7: one discarded, one
            # taken), the stack of 90 at 100 in 8
            counts = [count(d, c) - count(before, c) for c in range(7)]
            check(all(counts[c] == k for c, k in want.get("more", {}).items())
                  and (not name.startswith("8") or 100 in misc_counts(d, CURE_ROOT))
                  and ("bulbs" not in want or misc_counts(d, CURE_BULB) == want["bulbs"]),
                  f"original: {name}: records per category {counts}, Cure Root stacks {misc_counts(d, CURE_ROOT)}, "
                  f"Cure Bulb stacks {misc_counts(d, CURE_BULB)}")
            check(unk2 == want["unk2"] and taken == (want["taken"] > 0)
                  and ("flag" not in want or d["key flags"][want["flag"]] == 1)
                  and ("chest" not in want or d["chest flag"][0] == want["chest"]),
                  f"original: {name}: unk2 {unk2} ({want['unk2']}), the bag changed {taken}, key flags "
                  f"{d['key flags'].hex()}, the chest's flag {d['chest flag'][0]}")
            # nothing allocated is kept (the first session may also see a block of the start-up freed)
            check(struct.unpack("<I", d["heap free"])[0] >= struct.unpack("<I", before["heap free"])[0]
                  and d["_lootListHead"] == bytes(PTR)
                  and d["D_800F190C"][0] == 1,
                  f"original: {name}: heap free {struct.unpack('<I', d['heap free'])[0]} (before "
                  f"{struct.unpack('<I', before['heap free'])[0]}), _lootListHead {d['_lootListHead'].hex()}, "
                  f"D_800F190C {d['D_800F190C'][0]}")
    finally:
        a.close()
    print(f"original: {len(sessions)} sessions in {time.monotonic() - t0:.0f} s", flush=True)

    # (b) the shell, a new process from the same card
    shutil.copyfile(args.card, card)
    b = Session("shell", out, card, env, bool(args.out))
    try:
        b.g.call("ui_menu", modern=True)
        b.g.write(b.sym["vs_main_settings"] + SETTINGS_CURSOR_MEMORY, b"\x00")
        b.g.run(2)
        rest = diff(start, b.dump())
        check(not rest, f"both processes start equal {rest}")
        for n, (s, want, steps) in enumerate(zip(sessions, dumps_a, steps_a)):
            name, loots, chest, prep, _, _ = s
            if prep:
                prep(b)
                b.g.run(2)
            heap = b.heap_free()
            try:
                first = shell_session(b, (name, loots, chest, prep, steps, None), check)
            except TimeoutError as e:
                check(False, f"shell: {name}: {e}")
                b.shot("stuck")
                break
            if name.startswith("1 "):
                rows = first.get("list", [])
                check(len(rows) == 3 and rows[0]["category"] == "weapons" and rows[0]["fits"]
                      and rows[1]["organize"] == "consolidate" and rows[0]["organize"] == "disassemble"
                      and not first.get("chest") and first.get("bag", [{}])[0].get("capacity") == 8,
                      f"{name}: the rows as the game has them ({[(r['category'], r['name'], r['fits']) for r in rows]})")
            got = b.dump()
            if "shell_bulbs" in s[5]:
                # the deliberate difference (the module docstring): the misc records and their list
                misc = [("inventory", MISC, MISC + 64 * 4), ("indices", IDX[6][0], IDX[6][0] + IDX[6][1])]
                rest = diff(want, got, misc)
                check(not rest, f"parity: {name}, but the misc records" + (
                    f" (differs: {describe(want, got, rest)})" if rest else ""))
                check(misc_counts(want, CURE_BULB) == s[5]["bulbs"] and misc_counts(got, CURE_BULB) == s[5]["shell_bulbs"],
                      f"{name}: the merge: the original lost the moved stack ({misc_counts(want, CURE_BULB)}), the "
                      f"shell kept the sum ({misc_counts(got, CURE_BULB)})")
                check(misc_multiset(want, CURE_BULB) == misc_multiset(got, CURE_BULB)
                      and len(list_rows(want["indices"], 6)) == len(list_rows(got["indices"], 6)),
                      f"{name}: every other misc stack as the original's")
                b.g.write(b.inventory, want["inventory"])  # both processes equal again for the sessions after
                b.g.write(b.indices, want["indices"])
                b.g.run(2)
            else:
                rest = diff(want, got)
                check(not rest, f"parity: {name}" + (f" (differs: {describe(want, got, rest)})" if rest else ""))
            check(b.heap_free() >= heap, f"shell: {name}: nothing kept on the heap (free {b.heap_free()}, before {heap})")
        soft_reset(b, check)
        # a 16:9 look (screenshots only, --out)
        if args.out:
            b.g.call("graphics", widescreen=True)
            b.open([Loot().armor(b.inv(), list_rows(b.idx(), 4)[0]).misc((340, 1))])
            b.s_input()
            b.shot("wide")
            b.press("cross", settle=4)
            b.s_mode("confirm")
            b.s_yes()
            b.closed()
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
