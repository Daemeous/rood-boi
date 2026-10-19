#!/usr/bin/env python3
"""Modern menus, Items tab (pc/src/ui/items_bridge.c, ui_shell.cpp,
ui_items_draw.cpp): discard, quantity discard, discard all, sort, move and
merge with the same effect on game state as the original Items screen
(MENU3), Use's hand-off, a cancelled target, keys and sigils, the weapon
quick-swap slot of a discarded weapon, mouse.

    python3 pc/tests/ui_items_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Needs a card whose first save is in the save room with an equipped weapon and
two more in the bag, eight armor records and two shields (skipped otherwise).
4:3, 960x720 window (layout units 1:1).

Both processes get the same RAM set-up after Continue: the menu's cursor
memory off (vs_main_settings.cursorMemory 0: MENU3's action menus open on
their first row), the misc bag replaced (eight rows: three Cure Root stacks
of 12, 95 and 30, a Cure Bulb of 4, a key, a sigil, 5 of one item, 1 of
another), a gem set in the first weapon in the bag (its grip given a gem
slot), that weapon bound to quick-swap slot 2 together with the second
shield.

Parity: the same list of operations runs in two game processes from copies of
the same card. (a) ui_menus off: the original Items screen (Triangle, Items;
L1/R1 the category, Circle picks a row up, Circle on it again its actions,
the action, Up to Yes (the Yes / No opens on No); the quantity picker's
Up/Left; Circle on another row moves or merges); (b) ui_menus on: the
shell's Items tab (L2/R2 the category, Circle the actions, the action, the
quantity, Up to Yes; Move then the target row). After every operation
vs_main_inventory (0xF00 bytes) and vs_main_inventoryIndices (176 bytes) are
compared byte for byte; masked only what MASKS lists, with its reason. So are
the cursor memories, D_800F4EE8 bytes 0..103 (cursorMemories with the Items
lists' memories in unk3A and the action and sort menus' past it; see
MEMORY_NOTE): (b) starts each operation from the bytes (a) had once on the
operation's category, and must end with the bytes (a) ended with. The
operations: discard of the equipped
weapon (refused) and of a key and a sigil (refused); sort armor by every
stat of its list (MENU3's per-category lists, shifted as the original's
are), weapons and shields by two stats each; discard of the weapon with the
gem (its blade, grip and gem cleared); misc: 3 of 12, a whole stack of 4
through the quantity picker (Left to the 100 step: all), discard all, sort by
id and by quantity, a swap, a merge to <= 100 and one past 100. Then, with
the cursor memory on (vs_main_settings.cursorMemory 1: the action and sort
menus open where they were left), weapons sorted by row 10 and then row 3
(the sort menu scrolled down, then up from its memory), a quantity discard,
a swap and a misc sort; the option goes back off.
Use: Cure Root through each path; once its target is up (D_800F196C 2,
currentMenu 0x3F): D_800F4E98 (8 bytes), vs_battle_menuState currentMenu /
returnState / executeAbilityType, vs_battle_miscItemInvoked,
vs_battle_submenuStates[0], D_800F4EE8 bytes 58..103 (the Items lists' and
action and sort menus' cursor memories) compared (both zeroed first); Circle
on the target: the stack loses one, HP goes up, inventory equal again.
In (a) each operation must have done something (refused ones nothing; a
third of the sorts at least reorder, the rest find their list sorted), so a
step the original menu missed cannot pass as parity.

Shell-only checks (process b):
  - keys and sigils: not usable, not discardable (reasons), Use refused (the
    shell stays, no hand-off)
  - the discarded weapon's quick-swap slot 2 is empty (weapon_slots), the
    shield's slot-2 bit cleared too (see MASKS)
  - Use cancelled (Cross on the target): the shell on Items > Misc, the same
    row, nothing spent; then Use again and completed (the parity above)
  - mouse (RPC ui_menu pointer, layout units): a row click opens its action
    popover; a popover click on Discard opens the quantity picker; its
    arrows: up twice, down once (count 1), the step arrows (< 10, > 1);
    Cross backs out with nothing discarded.
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
TARGETING = 2  # D_800F196C while a target is chosen
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (vs_battle_menuItems, characterState)

# vs_main_inventory_t / vs_main_inventoryIndices_t (BATTLE func_8006B57C_t.h)
INVENTORY_BYTES, INDICES_BYTES = 0xF00, 176
WEAPONS, SHIELDS, BLADES, GRIPS, ARMOR, GEMS, MISC = 0, 0x100, 0x280, 0x540, 0x640, 0x8C0, 0xE00
WEAPON, SHIELD, BLADE, GRIP, ARMOR_REC, GEM = 32, 48, 44, 16, 40, 28
IDX = {0: (0, 8), 1: (8, 16), 2: (24, 16), 3: (40, 8), 4: (48, 16), 5: (64, 48), 6: (112, 64)}
CATEGORIES = ["weapons", "blades", "grips", "shields", "armor", "gems", "misc"]
WEAPON_C, SHIELD_C, ARMOR_C, MISC_C = 0, 3, 4, 6
SHIELD_SLOTS = 0x2F  # shields[i].gems[3]: the quick-swap slots' bits (weapon_slots.c)
WEAPON_SLOT = 7      # weapons[i].gems[3]: its quick-swap slot (weapon_slots.c)
SETTINGS_CURSOR_MEMORY = 1  # vs_main_settings.cursorMemory
ITEM_MEMORIES = (58, 104)   # D_800F4EE8: unk3A (category, lists 30..37), action menus 38..44, sort menus 45..51
MEMORY_BYTES = 104          # D_800F4EE8 bytes 0..103: every cursor memory the Items operations can store
CATEGORY_MEMORY = 0x3A      # D_800F4EE8.unk3A.currentItemCategory: category + 1 (0: the Manage page)

# MENU3 _sortItems' lists (items1.h): sortStats[category], the menu's rows
WEAPON_STATS = list(range(24))
BLADE_STATS = [0, 8, 9, 10, 24, 25, 26]
GRIP_STATS = [1, 4, 5, 6, 7, 8, 9, 10, 40, 41, 42, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39]
SHIELD_STATS = [0, 1, 4, 5, 8, 9, 10, 40, 41, 42, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39]
EQUIPMENT_STATS = [8, 9, 10, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39]
SORT_STATS = [WEAPON_STATS, WEAPON_STATS, BLADE_STATS, GRIP_STATS, SHIELD_STATS, EQUIPMENT_STATS, [0, 56]]

# The misc bag both processes get: (id, count)
CURE_ROOT, CURE_BULB, KEY, SIGIL = 323, 324, 459, 466
MISC_BAG = [(CURE_ROOT, 12), (CURE_ROOT, 95), (CURE_ROOT, 30), (CURE_BULB, 4), (KEY, 1), (SIGIL, 1),
            (340, 5), (331, 1)]

# Masked: (region, start, end, reason). Everything else must be equal.
MASKS = [
    ("inventory", SHIELDS + i * SHIELD + SHIELD_SLOTS, SHIELDS + i * SHIELD + SHIELD_SLOTS + 1,
     "shields' quick-swap slot bits (a port mod byte): discarding a weapon bound to a slot clears the "
     "slot in the shell (weapon and shield binding); the original screen knows nothing of the slots and "
     "leaves the shield's bit")
    for i in range(8)
]

# The cursor memories after an operation (D_800F4EE8 bytes 0..103) are compared for every operation but the
# refused ones. The exceptions, as the bridge's header (items_bridge.c) lists them:
MEMORY_NOTE = (
    "(b) starts each operation from (a)'s memories as (a) stood on the operation's category: on the way there "
    "the original stores each category's list cursor it leaves (L1 / R1, only once its list is up: timing) and "
    "passes the Manage page, the shell's categories store nothing. Refused operations are not compared: the "
    "original stores its list's and action menu's cursors as the player backs out (Cross), the shell stores "
    "memories only for what it carries out. In a list or sort menu longer than its screen the original's memory "
    "depends on the path its cursor took: both sides take the straight one here (Down to a row below where the "
    "menu opens, Up to one above), which is the one the bridge models.")

# Shell layout (4:3, 960 x 720 layout units; ui_items_draw.cpp)
LIST_X, LIST_Y0, ROW_H, X1 = 200, 160, 48, 536


def row_top(r):
    return LIST_Y0 + r * ROW_H


class Session:
    """One game process continued to the save room, the bag set up."""

    def __init__(self, name, out, card, env, shots):
        self.name, self.out, self.shots = name, out, shots
        self.g = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env)
        self.sym = symbols()
        s = self.sym
        self.inventory, self.indices = s["vs_main_inventory"], s["vs_main_inventoryIndices"]
        self.selected_action = s.get("_selectedItemAction")
        self.action_offset = s.get("_itemActionListOffset")

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

    def inv(self):
        return self.g.read(self.inventory, INVENTORY_BYTES)

    def idx(self):
        return self.g.read(self.indices, INDICES_BYTES)

    def dump(self):
        return {"inventory": self.inv(), "indices": self.idx()}

    def actor2(self):
        return int.from_bytes(self.g.read(self.ptr(self.sym["vs_battle_characterState"]) + 0x3C, 4), "little")

    def hp(self):
        return struct.unpack("<h", self.g.read(self.actor2() + 0x18, 2))[0]

    def targeting(self):
        return self.g.read(self.sym["D_800F196C"], 1)[0] == TARGETING

    def handoff(self):
        g, sym = self.g, self.sym
        ms = g.read(sym["vs_battle_menuState"], 3)
        return {"D_800F4E98": g.read(sym["D_800F4E98"], 8).hex(),
                "currentMenu": ms[0], "returnState": ms[1], "executeAbilityType": ms[2],
                "miscItemInvoked": g.read(sym["vs_battle_miscItemInvoked"], 1)[0],
                "submenuStates[0]": g.read(sym["vs_battle_submenuStates"], 1)[0],
                "D_800F4EE8[58:104]": g.read(sym["D_800F4EE8"], ITEM_MEMORIES[1])[ITEM_MEMORIES[0]:].hex()}

    def zero_memories(self):
        a, b = ITEM_MEMORIES
        self.g.write(self.sym["D_800F4EE8"] + a, bytes(b - a))

    def memories(self):
        return self.g.read(self.sym["D_800F4EE8"], MEMORY_BYTES)

    def set_memories(self, data):
        self.g.write(self.sym["D_800F4EE8"], data)

    def cursor_memory(self, on):
        self.g.write(self.sym["vs_main_settings"] + SETTINGS_CURSOR_MEMORY, b"\x01" if on else b"\x00")

    def wait(self, pred, limit, what, step=1, quiet=False):
        for n in range(0, limit + 1, step):
            if pred():
                return n
            self.g.run(step)
        if quiet:
            return None
        raise TimeoutError(f"{self.name}: {what}: not after {limit} vsyncs (currentMenu {self.menu():#x})")

    def prepare(self):
        """The RAM set-up both processes share (see the module docstring)."""
        g, inv = self.g, self.inv()
        base = self.inventory
        g.write(self.sym["vs_main_settings"] + SETTINGS_CURSOR_MEMORY, b"\x00")
        misc = b"".join(struct.pack("<HBB", i, n, k + 1) for k, (i, n) in enumerate(MISC_BAG))
        misc += b"".join(struct.pack("<HBB", 0, 0, k + 1) for k in range(len(MISC_BAG), 64))
        g.write(base + MISC, misc)
        lo, size = IDX[MISC_C]
        g.write(self.indices + lo, bytes(range(1, len(MISC_BAG) + 1)) + bytes(size - len(MISC_BAG)))
        # a gem in the first bag weapon (its grip gets a slot), that weapon and the second shield on slot 2
        w = self.bag_weapon(inv)
        grip = inv[WEAPONS + w * WEAPON + 2]
        g.write(base + GRIPS + (grip - 1) * GRIP + 4, b"\x01")
        gem = struct.pack("<HBBBbbb8b8bHh", 261, 0, 0, 0, 2, 0, 1, *([5, 0, 0, 0, 0, -3, 0, 0]),
                          *([0, 4, 0, 0, 0, 0, 0, 0]), w + 1, 1)
        g.write(base + GEMS, gem)
        g.write(self.indices + IDX[5][0], b"\x01")
        g.write(base + WEAPONS + w * WEAPON + 4, bytes([1, 0, 0, 2]))  # gems[0] = gem 1, gems[3] = slot 2
        g.write(base + SHIELDS + 1 * SHIELD + SHIELD_SLOTS, b"\x02")
        g.run(2)
        return w

    @staticmethod
    def bag_weapon(inv):
        return next(i for i in range(8) if inv[WEAPONS + i * WEAPON + 1] and not inv[WEAPONS + i * WEAPON + 3])

    # ---- the original Items screen (ui_menus off) ----
    def menu_items(self):
        p = self.ptr(self.sym["vs_battle_menuItems"])
        if not p:
            return {}
        raw = self.g.read(p, 64 * 40)
        return {i: (raw[i * 64], raw[i * 64 + 6]) for i in range(40) if raw[i * 64]}

    def wait_ready(self, want=None, limit=400):
        """Every menu item idle (vs_mainmenu_ready) a few frames in a row; `want` live."""
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

    def press(self, button, n=1, settle=6):
        for _ in range(n):
            self.g.press(button, vsyncs=3, settle=settle)

    def o_category(self):
        return (self.g.read(self.sym["D_800F4EE8"] + CATEGORY_MEMORY, 1)[0] - 1) & 7

    def o_open(self):
        """Triangle, Items: MENU3's list (the Manage page first: cursor memory off)."""
        self.press("triangle", settle=10)
        self.wait_ready(want=2)
        for _ in range(12):
            if self.selected(range(2, 10)) == 3:
                break
            self.press("down")
        if self.selected(range(2, 10)) != 3:
            raise TimeoutError(f"{self.name}: no Items command")
        self.press("circle", settle=10)
        self.wait(lambda: self.menu() != 0, 200, "MENU3 starts", step=2)
        self.wait_ready(want=20)

    def o_goto(self, c):
        for _ in range(9):
            if self.o_category() == c:
                break
            self.press("r1" if (c - self.o_category()) % 8 <= 4 else "l1", settle=4)
            self.g.run(4)
        if self.o_category() != c:
            raise TimeoutError(f"{self.name}: category {c} not reached ({self.o_category()})")
        self.wait_ready(want=20)

    def o_row(self, r):
        """Straight to row r (Down to a row below, Up to one above; the lists here fit on screen)."""
        for _ in range(20):
            cur = self.selected(range(20, 40))
            if cur == 20 + r:
                return
            self.press("up" if cur is not None and cur > 20 + r else "down")
        raise TimeoutError(f"{self.name}: row {r} not reached (selected {self.selected(range(20, 40))})")

    def o_action_index(self):
        if self.selected_action is None:
            return None
        sel = struct.unpack("<i", self.g.read(self.selected_action, 4))[0]
        return sel + self.g.read(self.action_offset, 1)[0]

    def o_pick(self, c, r):
        """Row r of category c picked up (Circle once)."""
        self.o_goto(c)
        self.o_row(r)
        self.press("circle")
        self.wait_ready(want=20)

    def o_menu_row(self, row):
        """An action menu's cursor straight to `row` (it opens on its first row, or on its memory with the
        cursor memory on): Down to a row below, Up to one above."""
        if self.o_action_index() is None:  # no symbols: it opens on its first row (cursor memory off)
            self.press("down", row)
            return
        for _ in range(40):
            got = self.o_action_index()
            if got == row:
                return
            self.press("down" if got < row else "up")
        raise TimeoutError(f"{self.name}: menu row {row} not reached ({self.o_action_index()})")

    def o_actions(self, c, r, action):
        """Row r's action menu (Circle, Circle) on action number `action` (from the first)."""
        self.o_pick(c, r)
        self.press("circle")
        self.wait_ready(want=32)
        self.g.run(4)
        self.o_menu_row(action)

    def o_yes(self):
        """The Yes / No menu (it opens on No): Up, Circle; then the list again."""
        self.wait_ready(want=33)
        self.g.run(4)
        self.press("up")
        if self.o_action_index() not in (None, 0):
            raise TimeoutError(f"{self.name}: Yes not reached")
        self.press("circle")
        self.wait_ready(want=20)

    def o_back(self):
        """Cross out of an action menu (the list again)."""
        self.press("cross")
        self.wait_ready(want=20)

    # ---- the shell (ui_menus on) ----
    def ui(self, **params):
        return self.g.call("ui_menu", **params)

    def it(self):
        return self.ui().get("items") or {}

    def s_open(self):
        g = self.g
        for _ in range(6):
            if self.ui().get("visible") and self.menu() == 0:
                break
            g.press("triangle", vsyncs=3, settle=4)
            if self.wait(lambda: self.ui().get("visible") and self.menu() == 0, 120, "", step=2,
                         quiet=True) is not None:
                g.run(6)
        self.wait(lambda: self.ui().get("visible") and self.menu() == 0, 2, "the shell opens")
        for _ in range(8):
            if self.ui().get("tab") == "items":
                break
            g.press("r1", vsyncs=3, settle=4)
        self.wait(lambda: self.ui().get("tab") == "items", 20, "the Items tab")
        g.run(2)

    def s_close(self):
        for _ in range(6):
            if self.menu() == CLOSED and not self.ui().get("visible"):
                return
            self.g.press("triangle", vsyncs=3, settle=4)
            self.wait(lambda: self.menu() == CLOSED and not self.ui().get("visible"), 200, "", step=4, quiet=True)
        raise TimeoutError(f"{self.name}: the shell did not close ({self.menu():#x})")

    def s_goto(self, c):
        for _ in range(8):
            if self.it().get("category") == CATEGORIES[c]:
                return
            self.press("r2", settle=4)
        raise TimeoutError(f"{self.name}: category {CATEGORIES[c]} not reached")

    def s_row(self, r):
        for _ in range(70):
            if self.it().get("cursor") == r:
                return
            self.press("down", settle=4)
        raise TimeoutError(f"{self.name}: row {r} not reached")

    def s_actions(self, c, r, action_id):
        self.s_goto(c)
        self.s_row(r)
        self.press("circle", settle=4)
        for _ in range(10):
            m = self.it()
            if m.get("mode") == "actions" and m["actions"][m["action"]]["id"] == action_id:
                return
            self.press("down", settle=4)
        raise TimeoutError(f"{self.name}: action {action_id} not reached")


def list_rows(inv_indices, c):
    lo, size = IDX[c]
    return [i - 1 for i in inv_indices[lo:lo + size] if i]


def row_of(idx, c, inventory):
    return list_rows(idx, c).index(inventory)


# ---- the operations, through each path ----
# An operation: (name, kind, category, args). Rows are found from the
# inventory indices at the time it runs (both processes hold the same).
# kinds: discard (inventory, quantity: None = no picker; "all" = Left to 100), refused (inventory, action),
# discard_all (inventory), sort (index in the category's list), move (inventory from, inventory to).

def discard_action(c):
    """Discard's row in MENU3's action menu: after Use (misc) or Search (blades, grips, gems)."""
    return 1 if c in (1, 2, 5, MISC_C) else 0


def original_op(a, op):
    """Runs op; returns the cursor memories as they stood once the op's category was up."""
    name, kind, c, args = op
    idx = a.idx()
    a.o_goto(c)
    before = a.memories()
    if kind == "sort":
        a.o_actions(c, 0, discard_action(c) + 1 + (c == MISC_C))  # Sort: after Discard (all)
        a.press("circle")
        a.wait_ready(want=32)
        a.g.run(4)
        a.o_menu_row(args)
        got = a.o_action_index()
        if got is not None and got != args:
            raise TimeoutError(f"{a.name}: sort row {args} not reached ({got})")
        a.press("circle")
        a.wait_ready(want=20)
        return before
    if kind == "move":
        src, dst = row_of(idx, c, args[0]), row_of(idx, c, args[1])
        a.o_pick(c, src)
        a.o_row(dst)
        a.press("circle")
        a.wait_ready(want=20)
        return before
    inventory = args[0]
    r = row_of(idx, c, inventory)
    first = discard_action(c)
    if kind == "refused":
        a.o_actions(c, r, {"use": 0, "discard": first, "discard_all": first + 1}[args[1]])
        a.press("circle")  # unselectable: the invalid buzz, the menu stays
        a.g.run(10)
        a.o_back()
        return before
    if kind == "discard_all":
        a.o_actions(c, r, first + 1)
        a.press("circle")
        a.o_yes()
        return before
    # discard
    a.o_actions(c, r, first)
    a.press("circle")
    quantity = args[1]
    if quantity is None:
        a.o_yes()
        return before
    a.wait_ready(want=34)
    a.g.run(10)
    if quantity == "all":
        a.press("left", 3)
    else:
        a.press("up", quantity)
    a.press("circle")
    a.o_yes()
    return before


def shell_op(b, op):
    name, kind, c, args = op
    idx = b.idx()
    if kind == "sort":
        b.s_actions(c, 0, "sort")
        b.press("circle", settle=4)
        for _ in range(30):
            s = b.it().get("sort") or {}
            if s.get("cursor") == args:
                break
            b.press("down", settle=4)
        stats = [x["stat"] for x in b.it()["sort"]["stats"]]
        b.press("circle", settle=4)
        return stats
    if kind == "move":
        src, dst = row_of(idx, c, args[0]), row_of(idx, c, args[1])
        b.s_actions(c, src, "move")
        b.press("circle", settle=4)
        b.s_row(dst)
        b.press("circle", settle=4)
        return None
    r = row_of(idx, c, args[0])
    if kind == "refused":
        b.s_actions(c, r, args[1])
        b.press("circle", settle=4)
        b.g.run(6)
        if b.it().get("mode") != "list":
            b.press("cross", settle=4)
        return None
    if kind == "discard_all":
        b.s_actions(c, r, "discard_all")
        b.press("circle", settle=4)
        b.press("up", settle=4)  # the confirm opens on No, as the original's
        b.press("circle", settle=4)
        return None
    b.s_actions(c, r, "discard")
    b.press("circle", settle=4)
    quantity = args[1]
    if quantity is not None:
        if quantity == "all":
            b.press("left", 3, settle=4)
        else:
            b.press("up", quantity, settle=4)
        b.press("circle", settle=4)
    b.press("up", settle=4)  # Yes (the confirm opens on No)
    b.press("circle", settle=4)
    return None


def plan(inv, idx, gem_weapon):
    """The operation list for this card (None: the card lacks what it needs)."""
    weapons = list_rows(idx, WEAPON_C)
    equipped = next((i for i in weapons if inv[WEAPONS + i * WEAPON + 3]), None)
    shields = sum(1 for i in range(8) if inv[SHIELDS + i * SHIELD + 4])  # records: a list may miss one
    if equipped is None or len(weapons) < 3 or len(list_rows(idx, ARMOR_C)) < 4 or shields < 2:
        return None
    misc = {k: k for k in range(len(MISC_BAG))}  # misc inventory index = its MISC_BAG row
    ops = [
        ("discard the equipped weapon (refused)", "refused", WEAPON_C, (equipped, "discard")),
        ("discard a key (refused)", "refused", MISC_C, (4, "discard")),
        ("discard all of a sigil (refused)", "refused", MISC_C, (5, "discard_all")),
    ]
    ops += [(f"sort armor by row {i} (stat {SORT_STATS[ARMOR_C][i]})", "sort", ARMOR_C, i)
            for i in range(len(SORT_STATS[ARMOR_C]))]
    ops += [(f"sort weapons by row {i}", "sort", WEAPON_C, i) for i in (8, 0)]
    ops += [(f"sort shields by row {i} (gripSortStats[{i}]: the original's table)", "sort", SHIELD_C, i)
            for i in (1, 0)]
    ops += [
        ("discard the weapon with a gem (blade, grip, gem cleared)", "discard", WEAPON_C, (gem_weapon, None)),
        ("misc: discard 3 of 12", "discard", MISC_C, (misc[0], 3)),
        ("misc: discard a whole stack of 4 (picker: Left x3 = all)", "discard", MISC_C, (misc[3], "all")),
        ("misc: discard all (5)", "discard_all", MISC_C, (misc[6],)),
        ("misc: discard a single one (no picker)", "discard", MISC_C, (misc[7], None)),
        ("misc: sort by quantity", "sort", MISC_C, 1),
        ("misc: sort by id", "sort", MISC_C, 0),
        ("misc: swap a Cure Root stack and the key", "move", MISC_C, (misc[2], misc[4])),
        ("misc: merge, the 95 stack moved onto the 30 one (> 100: 100 and 25)", "move", MISC_C,
         (misc[1], misc[2])),
        ("misc: merge, the 25 stack moved onto the 9 one (<= 100: 34, the target's record cleared)", "move",
         MISC_C, (misc[2], misc[0])),
    ]
    return ops


def plan_memory_on():
    """The operations run with the cursor memory on, after plan()'s (the misc rows then: inventory 1 a Cure
    Root stack of 100, 4 the key, 2 a Cure Root stack of 34, 5 the sigil)."""
    return [
        ("memory on: sort weapons by row 10 (the sort menu scrolls down)", "sort", WEAPON_C, 10),
        ("memory on: sort weapons by row 3 (Up from the remembered row 10)", "sort", WEAPON_C, 3),
        ("memory on: misc: discard 2 of 34", "discard", MISC_C, (2, 2)),
        ("memory on: misc: swap the stack of 100 and the key", "move", MISC_C, (1, 4)),
        ("memory on: misc: sort by quantity", "sort", MISC_C, 1),
    ]


def diff(a, b):
    found = []
    for region in a:
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


def describe_memories(want, got):
    """The cursor memories that differ: id (offset / 2), {row, page} original vs shell."""
    ids = sorted({i // 2 for i in range(len(want)) if want[i] != got[i]})
    return ", ".join(f"memory {i} ({'category ' if i == 29 else ''}{want[2 * i]},{want[2 * i + 1]} vs "
                     f"{got[2 * i]},{got[2 * i + 1]})" for i in ids)


def misc_count(d, k):
    return struct.unpack("<HBB", d["inventory"][MISC + k * 4:MISC + k * 4 + 4])[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "ui_items").resolve()
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

    # (a) the original Items screen
    shutil.copyfile(args.card, card)
    a = Session("original", out, card, env, bool(args.out))
    try:
        a.g.call("ui_menu", modern=False)
        state = a.g.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1
        gem_weapon = a.prepare()
        ops = plan(a.inv(), a.idx(), gem_weapon)
        if ops is None:
            print("SKIP: the card needs an equipped weapon and two more, two shields and four armor records")
            return 77
        dumps_a = [a.dump()]
        mems_a = []  # (before, after) each operation
        a.o_open()
        prev = a.dump()  # the Items menu rebuilt every list as it opened (a bag shield missing from one)
        memory_on = len(ops)
        ops += plan_memory_on()
        for n, op in enumerate(ops):
            if n == memory_on:
                a.cursor_memory(True)
            try:
                before = original_op(a, op)
            except TimeoutError as e:
                check(False, f"original: {op[0]}: {e}")
                a.shot("stuck")
                return 1
            mems_a.append((before, a.memories()))
            dumps_a.append(a.dump())
            changed, prev = dumps_a[-1] != prev, dumps_a[-1]
            if op[1] == "refused":
                check(not changed, f"original: {op[0]}: nothing changed")
            elif op[1] != "sort":
                check(changed, f"original: {op[0]}: the inventory changed")
        stored = sum(1 for op, (x, y) in zip(ops, mems_a) if op[1] != "refused" and x != y)
        done = sum(1 for op in ops if op[1] != "refused")
        check(stored >= done // 2, f"original: {stored} of {done} operations stored cursor memories")
        # the sort menu's memory (45 + weapons) scrolled: row 10 from the top {5, 5}; Up to row 3 {1, 2}
        sorts_on = [y[2 * 45:2 * 45 + 2] for op, (x, y) in zip(ops, mems_a)
                    if op[0].startswith("memory on: sort weapons")]
        check(sorts_on == [bytes([5, 5]), bytes([1, 2])],
              f"original: the weapons' sort menu memory after rows 10 and 3: {[m.hex() for m in sorts_on]} "
              "(0505, 0102)")
        reordered = [op[0] for op, x, y in zip(ops, dumps_a, dumps_a[1:]) if op[1] == "sort" and x != y]
        sorts = sum(1 for op in ops if op[1] == "sort")
        check(len(reordered) >= sorts // 3, f"original: {len(reordered)} of {sorts} sorts changed the order "
                                            f"(the others found it sorted): {reordered}")
        a.cursor_memory(False)
        a.press("triangle", settle=10)
        a.wait(lambda: a.menu() == CLOSED, 300, "the menu closes", step=4)
        a.g.run(30)
        use_a = original_use(a, check)
    finally:
        a.close()
    print(f"original: {len(ops)} operations and Use in {time.monotonic() - t0:.0f} s", flush=True)

    # (b) the shell, a new process from the same card
    shutil.copyfile(args.card, card)
    b = Session("shell", out, card, env, bool(args.out))
    try:
        b.g.call("ui_menu", modern=True)
        b.prepare()
        rest = diff(dumps_a[0], b.dump())
        check(not rest, f"both processes start equal {rest}")
        slots = b.g.call("weapon_slots")
        check(slots["weapons"][gem_weapon]["slot"] == 2 and slots["shields"][1]["slots"] == 2,
              f"set-up: weapon {gem_weapon} and shield 1 on quick-swap slot 2 "
              f"({slots['weapons'][gem_weapon]}, {slots['shields'][1]})")
        b.s_open()
        key_checks(b, check)
        for n, (op, want, (mem_before, mem_want)) in enumerate(zip(ops, dumps_a[1:], mems_a)):
            if n == memory_on:
                b.cursor_memory(True)
            b.set_memories(mem_before)
            try:
                stats = shell_op(b, op)
            except TimeoutError as e:
                check(False, f"shell: {op[0]}: {e}")
                b.shot("stuck")
                break
            if op[1] != "refused":
                mem = b.memories()
                check(mem == mem_want, f"parity: {op[0]}: cursor memories" + (
                    "" if mem == mem_want else f" (differ: {describe_memories(mem_want, mem)})"))
            if op[1] == "sort":
                check(stats == SORT_STATS[op[2]], f"{CATEGORIES[op[2]]}: the sort list {stats} is MENU3's "
                                                  f"sortStats[{op[2]}] {SORT_STATS[op[2]]}")
            got = b.dump()
            rest = diff(want, got)
            check(not rest, f"parity: {op[0]}" + (f" (differs: {describe(want, got, rest)})" if rest else ""))
            if "gem" in op[0] and op[1] == "discard":
                quick_checks(b, check, gem_weapon, got)
        b.cursor_memory(False)
        mouse_checks(b, check)
        b.s_close()
        b.g.run(30)
        shell_use(b, check, use_a)
    finally:
        b.close()
    print(f"total {time.monotonic() - t0:.0f} s")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


def use_row(s):
    """The Cure Root stack Use takes: the misc list's last Cure Root row (not the first: its cursor
    memory is not 0)."""
    idx = s.idx()
    inv = s.inv()
    rows = list_rows(idx, MISC_C)
    k = next(i for i in reversed(rows) if struct.unpack("<H", inv[MISC + i * 4:MISC + i * 4 + 2])[0] == CURE_ROOT)
    return rows.index(k), k


def complete(s, check, k, count, what):
    """Circle on the target until it is used: the stack loses one, HP goes up."""
    hp1 = s.hp()
    for _ in range(8):
        s.press("circle", settle=4)
        if s.wait(lambda: s.hp() > hp1 or not s.targeting(), 60, "", step=2, quiet=True) is not None:
            break
    s.wait(lambda: s.hp() > hp1, 300, "", step=4, quiet=True)
    s.wait(lambda: s.menu() == CLOSED and not s.targeting(), 400, "the item is used", step=4, quiet=True)
    s.g.run(120)
    d = s.dump()
    check(misc_count(d, k) == count - 1 and s.hp() > hp1,
          f"{s.name}: {what}: Cure Root used: {count} -> {misc_count(d, k)}, HP {hp1} -> {s.hp()}")
    return d


def original_use(a, check):
    """Cure Root through MENU3: the hand-off, then completed. Returns (hand-off, dump after)."""
    a.zero_memories()
    r, k = use_row(a)
    count = misc_count(a.dump(), k)
    a.o_open()
    a.o_actions(MISC_C, r, 0)
    a.press("circle", settle=1)
    n = a.wait(lambda: a.targeting() and a.menu() == CLOSED, 300, "", step=2, quiet=True)
    h = a.handoff()
    check(n is not None, f"original: Cure Root's target is up ({h})")
    a.shot("use-target")
    a.g.run(20)
    return h, complete(a, check, k, count, "Use completed")


def shell_use(b, check, use_a):
    want, after = use_a
    b.zero_memories()
    r, k = use_row(b)
    count = misc_count(b.dump(), k)
    # Cancelled first: the shell on the same row, nothing spent.
    b.s_open()
    b.s_actions(MISC_C, r, "use")
    b.press("circle", settle=1)
    b.wait(lambda: not b.ui()["visible"], 30, "the shell closes for Use")
    n = b.wait(lambda: b.targeting() and b.menu() == CLOSED, 300, "", step=2, quiet=True)
    h = b.handoff()
    check(n is not None, "shell: Cure Root's target is up")
    for key in want:
        check(h[key] == want[key], f"parity: Use: {key}: shell {h[key]} (original {want[key]})")
    b.shot("use-target")
    b.g.run(20)
    seen = set()

    def back():
        seen.add(b.menu())
        return b.ui()["visible"] and b.menu() == 0
    n = None
    for _ in range(6):
        b.press("cross", settle=0)
        n = b.wait(back, 40, "", quiet=True)
        if n is not None:
            break
    b.g.run(6)
    s, m = b.ui(), b.it()
    check(n is not None and s.get("tab") == "items" and m.get("category") == "misc" and m.get("cursor") == r
          and misc_count(b.dump(), k) == count,
          f"Use cancelled: the shell on {s.get('tab')} > {m.get('category')}, row {m.get('cursor')} ({r}), "
          f"{misc_count(b.dump(), k)} Cure Root ({count}); menus seen {sorted(hex(x) for x in seen)}")
    b.shot("use-cancelled")
    # Again, completed.
    b.s_actions(MISC_C, r, "use")
    b.press("circle", settle=1)
    b.wait(lambda: not b.ui()["visible"], 30, "the shell closes for Use")
    n = b.wait(lambda: b.targeting() and b.menu() == CLOSED, 300, "", step=2, quiet=True)
    check(n is not None, "shell: Use again: the target is up")
    b.g.run(20)
    got = complete(b, check, k, count, "Use completed")
    rest = diff(after, got)
    check(not rest, "parity: after Use" + (f" (differs: {describe(after, got, rest)})" if rest else ""))


def key_checks(b, check):
    """Keys and sigils: not usable, not discardable; Use refused (the shell stays, no hand-off)."""
    b.s_goto(MISC_C)
    m = b.it()
    for x in m["list"]:
        if x["id"] in (KEY, SIGIL):
            check(not x["usable"] and not x["discard"] and not x["discard_all"] and x["use_reason"]
                  and x["discard_reason"],
                  f"{x['name']} ({x['id']}): usable {x['usable']} '{x['use_reason']}', discard {x['discard']} "
                  f"'{x['discard_reason']}', discard all {x['discard_all']}")
    r = next(x["row"] for x in m["list"] if x["id"] == KEY)
    before = b.handoff()
    b.s_actions(MISC_C, r, "use")
    acts = {x["id"]: x["enabled"] for x in b.it()["actions"]}
    b.press("circle", settle=4)
    b.g.run(10)
    check(not acts.get("use") and b.ui().get("visible") and b.menu() == 0 and b.handoff() == before,
          f"key: Use greyed {not acts.get('use')}, refused: the shell stays, no hand-off")
    if b.it().get("mode") != "list":
        b.press("cross", settle=4)
    b.s_goto(WEAPON_C)
    eq = next(x for x in b.it()["list"] if x["mark"] == 1)
    check(not eq["discard"] and eq["discard_reason"],
          f"the equipped weapon: not discardable '{eq['discard_reason']}'")


def quick_checks(b, check, w, d):
    slots = b.g.call("weapon_slots")
    bits = d["inventory"][SHIELDS + 1 * SHIELD + SHIELD_SLOTS]
    check(slots["weapons"][w]["slot"] == 0 and d["inventory"][WEAPONS + w * WEAPON + WEAPON_SLOT] == 0
          and not bits & 2,
          f"the discarded weapon's quick-swap slot 2 is empty ({slots['weapons'][w]}; shield 1 bits {bits:#x})")


def mouse_checks(b, check):
    """A row click opens its action popover; a popover click on Discard the quantity picker; its arrows."""
    g = b.g
    before = b.dump()

    def pointer(x, y, click=False):
        b.ui(pointer={"x": x, "y": y, "click": click})
        g.run(4)

    def hover_until(x, y, ready, seconds=3):
        """The menus glide in real time (ui_theme focus_grow), not in vsyncs, and a resumed save state runs
        vsyncs far faster than that: hover (x, y) until what is under it is ready, then it can be clicked."""
        deadline = time.monotonic() + seconds
        while True:
            pointer(x, y)
            if ready() or time.monotonic() > deadline:
                return
            time.sleep(0.02)
    b.s_goto(MISC_C)
    for _ in range(80):
        if b.it().get("cursor") == 0:
            break
        b.press("up", settle=4)
    g.run(40)  # the focus glides to the row (the popover opens under it)
    m = b.it()
    r = next(x["row"] for x in m["list"] if x["count"] >= 3 and x["row"] < 3)
    pointer(LIST_X, row_top(r) + ROW_H // 2, True)
    m = b.it()
    check(m.get("mode") == "actions" and m.get("cursor") == r, f"mouse: a click on row {r} opens its actions "
          f"({m.get('mode')}, cursor {m.get('cursor')})")
    acts = [x["id"] for x in m.get("actions") or []]
    top = row_top(r) + 52
    i = acts.index("discard") if "discard" in acts else 0
    hover_until(X1 - 248 + 60, top + 10 + i * 30 + 15, lambda: b.it().get("action") == i)  # the popover in place
    pointer(X1 - 248 + 60, top + 10 + i * 30 + 15, True)
    m = b.it()
    check(m.get("mode") == "quantity", f"mouse: a click on the popover's Discard: the quantity picker "
          f"({m.get('mode')}; actions {acts})")
    b.shot("mouse-quantity")
    q0 = (m.get("quantity") or {}).get("count")
    pointer(X1 - 40, top + 49, True)
    pointer(X1 - 40, top + 49, True)
    q1 = (b.it().get("quantity") or {}).get("count")
    pointer(X1 - 40, top + 73, True)
    q2 = (b.it().get("quantity") or {}).get("count")
    check((q0, q1, q2) == (0, 2, 1), f"mouse: quantity arrows: up twice, down once: {q0} -> {q1} -> {q2}")
    pointer(X1 - 248, top + 126, True)
    s1 = (b.it().get("quantity") or {}).get("step")
    pointer(X1 - 92, top + 126, True)
    s2 = (b.it().get("quantity") or {}).get("step")
    check((s1, s2) == (10, 1), f"mouse: step arrows: < {s1} (10), > {s2} (1)")
    pointer(-1, -1)
    for _ in range(3):
        if b.it().get("mode") == "list":
            break
        b.press("cross", settle=4)
    rest = diff(before, b.dump())
    check(b.it().get("mode") == "list" and not rest, f"mouse: Cross backs out, nothing discarded {rest}")


if __name__ == "__main__":
    sys.exit(main())
