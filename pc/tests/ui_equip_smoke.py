#!/usr/bin/env python3
"""Modern menus, Equipment screen (pc/src/ui/equip_bridge.c): the snapshot,
equipping through the shell with the same effect on game state as the
original equip page (MENU3), quick swap slots, mouse, the hold on close,
bare hands and equipping with the weapon drawn.

    python3 pc/tests/ui_equip_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Needs a card whose first save is in the save room with three weapons (two
one-handed, one equipped, and a two-handed one), a shield equipped, an arm
piece in the bag and an accessory worn (skipped otherwise).

Parity: the same list of equipment changes runs in two game processes from
copies of the same card. (a) ui_menus off: each change through the original
menu (Triangle, Items, Equip, the slot, the list row; MENU3 16C.c), Triangle
closes; (b) ui_menus on: the shell's Equipment tab (Circle on the slot, the
candidate, Circle; Square takes off), Triangle closes. After every change the
menu is closed, loads settle, and these are compared byte for byte:
  vs_main_inventory (weapons, shields, blades, grips, armor, gems, misc) and
  vs_main_inventoryIndices; Ashley's battle record (vs_battle_actor2 behind
  vs_battle_characterState->unk3C); the character state record
  (vs_battle_actor: weaponDrawn, equippedWeaponCategory, ...); actor 0's
  object (D_800F4538[0], unk5B1 the model category among them); the weapon and
  shield model headers (vs_battle_wepModels[0], [1]).
Masked (volatile, not equipment): see MASKS below, each with its reason.
What this comparison found (fixed in the bridge): the Items menu rebuilds
every inventory index list when it starts (vs_mainMenu_rebuildInventory, so
a bag record missing from its list, like this card's second shield, is
listed from then on); and the original finishes actor 0 after a model load
with func_800FA2CC (idle set up in place), not the field swap's animation
restart (func_800A0204: its script state differed).

Shell-only checks (process b, after the parity list):
  - the snapshot: eight slots; the weapon's name is the inventory record's
    name decoded; DP/STR as Ashley's record holds them; Ashley's STR/INT/AGL
    are her record's (equipment is not summed into them: the equip page's
    values are attack/defense values on top of hers)
  - Quick swap: slot 2 = a bag weapon (not equipped), no shield: the mod's
    bytes (weapons[i].gems[3] == 2, the shield's slot bit clear); closed and
    with qol_weapon_slots on, L2 + R2 + Triangle swaps to it in the field;
    Square in the shell clears the slot
  - mouse (RPC ui_menu pointer, layout units, the real hit tests): hovering
    gear row 3 focuses it, a click opens its list, a click on a candidate
    equips it
  - hold: equip a weapon and press Triangle at once: the shell holds
    (holding true) until the model load ends, then the menu closes and the
    model is the new weapon's
  - bare hands: Square on the weapon takes it off (no weapon in the record,
    category 10); equipping again from bare hands
  - drawn: in battle mode (weapon drawn) equipping another weapon through
    the shell: the record, the model and the drawn state follow.
--out keeps screenshots (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
from weapon_slots_smoke import default_card, two_handed  # noqa: E402

CLOSED = 0x3F
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (vs_battle_menuItems, D_800F4538, ...)

# vs_main_inventory_t (main.h) and records
INVENTORY_BYTES = 3840
WEAPONS, SHIELDS, BLADES, ARMOR = 0, 0x100, 0x280, 0x640
WEAPON, SHIELD, BLADE, ARMOR_REC = 32, 48, 44, 40
INDICES_BYTES = 176
IDX_WEAPONS, IDX_SHIELDS, IDX_ARMOR = 0, 40, 48
ACTOR2_BYTES = 0x964  # vs_battle_actor2 (146C.h)
ACTOR_BYTES = 80  # vs_battle_actor
ACTOR_UNK3C = 60  # vs_battle_actor.unk3C (weaponDrawn 32, equippedWeaponCategory 37: compared with the rest)

OBJECT_BYTES = 6356  # D_800F4538_t: actor 0's object (3A1A0.h)
WEP_MODELS = 2  # vs_battle_wepModels[0], [1]: actor 0's weapon and shield
MODEL_HEADER = 20  # vs_battle_wepModels_t up to unk13: counts, flags, material, modelId, texSlot, unk11..13

# Masked: (region, start, end, reason). Everything else must be equal.
FRAMES = "the two paths take different numbers of frames"
MASKS = [
    ("actor2", 0x18, 0x1A, "currentHP: regenerates with game time; " + FRAMES),
    ("actor2", 0x1C, 0x1E, "currentMP: as currentHP"),
] + [
    ("actor2", 0x398 + k * 0xDC, 0x398 + k * 0xDC + 2, f"limbs[{k}].hp: regenerates with game time; " + FRAMES)
    for k in range(6)
] + [
    ("object0", 108, 1452, "bones[42]: the pose matrices, recomputed from the animation every frame"),
    ("object0", 0x6FC, 0x704, "unk6FC: already differs between two processes before any menu (pose sway)"),
    ("object0", 0x704, 0x11A4, "unk704, unkC54: the animation frame buffers (func_800AFA28); "
                               "their phase follows the frame count, " + FRAMES),
] + [
    ("models", i * MODEL_HEADER + 15, i * MODEL_HEADER + 16,
     "texSlot: the first free weapon texture slot; the original loads while the menu's Ashley "
     "(actor 1) holds her own model slots, so it gets another free slot (same texture, other place)")
    for i in range(WEP_MODELS)
]

LIMB_RARM, LIMB_ACCESSORY = 1, 7
PART_CATEGORY = [0, 5, 5, 2, 3, 4, 6, 7]  # MENU3 bodyPartCategories
GEAR_IDS = ["weapon", "shield", "right_arm", "left_arm", "head", "body", "legs", "accessory"]

# Shell layout (4:3, 960 x 720 layout units; ui_equip_draw.cpp)
def gear_row_xy(i):
    return 200, 150 + i * 51 + 25


def pick_row_xy(r):
    return 200, 150 + r * 56 + 28


# The game's charset for names (tools/etc/vsString.py): digits, letters, a few marks.
CHARSET = {0x8F: " ", 0x90: "!", 0x96: "'", 0x9F: ",", 0xA0: ".", 0xA7: "-"}


def decode(raw):
    """As pc_ui_text_decode: 0xE8 (new line) and 0xFA n (x advance) are
    blanks; 0xEC..0xFF but 0xFC take one operand byte; 0xEB pads.
    (Characters the test does not know read as '?'.)"""
    out = ""
    i = 0
    while i < len(raw):
        b = raw[i]
        i += 1
        if b == 0xE7:
            break
        if b in (0xE8, 0xFA):
            out += " "
            i += b == 0xFA
        elif b >= 0xEC and b != 0xFC:
            i += 1
        elif b >= 0xBD:  # 0xEB padding, 0xFC, ...: not a character
            pass
        elif b < 10:
            out += chr(48 + b)
        elif b < 36:
            out += chr(65 + b - 10)
        elif b < 62:
            out += chr(97 + b - 36)
        else:
            out += CHARSET.get(b, "?")
    return " ".join(out.split())


class Session:
    """One game process continued to the save room."""

    def __init__(self, name, out, card, env, shots):
        self.out, self.shots, self.name = out, shots, name
        self.game = Game.launch(fast=True, card=str(card), log=out / f"{name}.log", extra_env=env)
        g = self.game
        g.wait_title()
        g.press("start", vsyncs=4, settle=90)
        for _ in range(3):
            g.press("circle", vsyncs=3, settle=90)
        g.run(400)
        sym = symbols()
        self.menu_state = sym["vs_battle_menuState"]
        self.inventory = sym["vs_main_inventory"]
        self.indices = sym["vs_main_inventoryIndices"]
        self.items_ptr = sym["vs_battle_menuItems"]
        self.char_state = sym["vs_battle_characterState"]
        self.objects = sym["D_800F4538"]
        self.wep_models = sym["vs_battle_wepModels"]
        self.load_state = sym["vs_battle_loadObjectDataState"]

    def close(self):
        self.game.close()

    def shot(self, name):
        if self.shots:
            self.game.present_screenshot(self.out / f"{self.name}-{name}.png")

    def ptr(self, address):
        return int.from_bytes(self.game.read(address, PTR), "little")

    def current_menu(self):
        return self.game.read(self.menu_state, 1)[0]

    def inv(self):
        return self.game.read(self.inventory, INVENTORY_BYTES)

    def idx(self):
        return self.game.read(self.indices, INDICES_BYTES)

    def actor(self):
        return self.ptr(self.char_state)

    def actor2(self):
        return int.from_bytes(self.game.read(self.actor() + ACTOR_UNK3C, 4), "little")

    def ws(self):
        return self.game.call("weapon_slots")

    def settle(self, limit=600):
        """Run until no model load is under way (MENU3's _loadObjectData
        state, the weapon slots' swap and menu reload)."""
        g = self.game
        for _ in range(limit // 10):
            w = self.ws()
            if not w["loading"] and not w.get("menu_reload") and g.read(self.load_state, 4) == bytes(4):
                break
            g.run(10)
        else:
            raise TimeoutError(f"settle: a model load still runs after {limit} vsyncs ({self.ws()})")
        g.run(30)

    def dump(self):
        g = self.game
        actor = self.actor()
        objects = [self.ptr(self.objects + i * PTR) for i in range(1)]
        d = {
            "inventory": self.inv(),
            "indices": self.idx(),
            "actor": g.read(actor, ACTOR_BYTES),
            "actor2": g.read(self.actor2(), ACTOR2_BYTES),
        }
        d["object0"] = g.read(objects[0], OBJECT_BYTES)
        models = b""
        for i in range(WEP_MODELS):
            p = self.ptr(self.wep_models + i * PTR)
            models += g.read(p, MODEL_HEADER) if p else bytes(MODEL_HEADER)
        d["models"] = models
        return d

    # ---- the original menu (ui_menus off) ----
    def menu_items(self):
        """vs_battle_menuItems: {id: (state, selected)} of the live items."""
        raw = self.game.read(self.ptr(self.items_ptr), 64 * 40)
        return {i: (raw[i * 64], raw[i * 64 + 6]) for i in range(40) if raw[i * 64]}

    def wait_ready(self, limit=400, want=None):
        """Until every menu item is idle (vs_mainmenu_ready) for a few frames
        and no model load runs; `want`: an item id that must be live."""
        g = self.game
        calm = 0
        for _ in range(limit // 4):
            items = self.menu_items()
            ok = all(s <= 1 for s, _ in items.values()) and g.read(self.load_state, 4) == bytes(4)
            if want is not None:
                ok = ok and want in items
            calm = calm + 1 if ok else 0
            if calm >= 3:
                g.run(4)
                return True
            g.run(4)
        return False

    def selected(self, ids):
        items = self.menu_items()
        return next((i for i in ids if i in items and items[i][1]), None)

    def nav(self, ids, target):
        """Down (lists wrap) until item `target` of `ids` is the selected one."""
        for _ in range(len(ids) + 2):
            if self.selected(ids) == target:
                return True
            self.game.press("down", vsyncs=3, settle=10)
        return self.selected(ids) == target

    def original_open_equip(self):
        g = self.game
        g.press("triangle", vsyncs=3, settle=10)
        if not self.wait_ready(want=2):
            return "the command menu did not open"
        if not self.nav(range(2, 10), 3):  # ITEMS: the second command
            return "no Items command"
        g.press("circle", vsyncs=3, settle=10)
        if not self.wait_ready(want=20) or not self.nav([20, 21], 20):  # OPTIONS: EQUIP, SETUP
            return "the Items menu did not open"
        g.press("circle", vsyncs=3, settle=10)
        if not self.wait_ready(want=17):  # the equip page: rows 10..17
            return "the equip page did not open"
        return None

    def original_equip(self, part, row):
        """On the equip page: part 0..7 (weapon .. accessory), list row (0 = the
        worn item or NONE: selecting the worn one takes it off)."""
        g = self.game
        if not self.nav(range(10, 18), 10 + part):
            return f"equip page row {part} not reached"
        g.press("circle", vsyncs=3, settle=10)
        if not self.wait_ready(want=20):
            return "the list did not open"
        if not self.nav(range(20, 26), 20 + row):
            return f"list row {row} not reached"
        self.shot(f"list-{part}-{row}")
        g.press("circle", vsyncs=3, settle=10)
        if not self.wait_ready(want=17, limit=800):
            return "the equip page did not come back"
        return None

    def original_close(self):
        g = self.game
        g.press("triangle", vsyncs=3, settle=10)
        for _ in range(60):
            if self.current_menu() == CLOSED:
                break
            g.run(5)
        if self.current_menu() != CLOSED:
            return f"the menu did not close ({self.current_menu():#x})"
        return None

    # ---- the shell (ui_menus on) ----
    def shell(self):
        return self.game.call("ui_menu")

    def eq(self):
        return self.shell().get("equipment") or {}

    def shell_wait(self, limit=600):
        """Run until the shell neither holds nor is busy (a model load)."""
        g = self.game
        for _ in range(limit // 6):
            s = self.shell()
            e = s.get("equipment") or {}
            if not s.get("holding") and not e.get("busy"):
                return s
            g.run(6)
        raise TimeoutError(f"shell_wait: still holding or busy after {limit} vsyncs ({self.shell()})")

    def shell_open_equipment(self, sub="gear"):
        g = self.game
        g.press("triangle", vsyncs=3, settle=40)
        for _ in range(8):
            if self.shell().get("tab") == "equipment":
                break
            g.press("r1", vsyncs=3, settle=10)
        if self.shell().get("tab") != "equipment":
            return "no Equipment tab"
        if self.eq().get("sub") != sub:
            g.press("r2", vsyncs=3, settle=10)
        if self.eq().get("sub") != sub:
            return f"no {sub} sub-tab"
        return None

    def shell_focus(self, part):
        for _ in range(9):
            if self.eq().get("slot") == GEAR_IDS[part]:
                return True
            self.game.press("down", vsyncs=3, settle=6)
        return self.eq().get("slot") == GEAR_IDS[part]

    def shell_equip(self, part, inventory):
        """inventory -1: take off (Square)."""
        g = self.game
        if not self.shell_focus(part):
            return f"gear slot {part} not focused"
        if inventory < 0:
            g.press("square", vsyncs=3, settle=10)
            self.shell_wait()
            return None
        g.press("circle", vsyncs=3, settle=10)
        e = self.eq()
        for _ in range(20):
            if e.get("mode") == "pick" and not e.get("loading") and e.get("candidates") is not None:
                break
            g.run(3)
            e = self.eq()
        cands = [c["inventory"] for c in e.get("candidates") or []]
        if inventory not in cands:
            return f"inventory {inventory} not among the candidates {cands}"
        want = cands.index(inventory)
        for _ in range(len(cands) + 1):
            if self.eq().get("cursor") == want:
                break
            g.press("down", vsyncs=3, settle=6)
        if self.eq().get("cursor") != want:
            return f"candidate {want} not reached"
        self.shot(f"pick-{part}-{inventory}")
        g.press("circle", vsyncs=3, settle=10)
        self.shell_wait()
        return None

    def shell_close(self):
        g = self.game
        g.press("triangle", vsyncs=3, settle=10)
        for _ in range(80):
            if self.current_menu() == CLOSED and not self.shell().get("visible"):
                break
            g.run(5)
        if self.current_menu() != CLOSED:
            return f"the menu did not close ({self.current_menu():#x})"
        return None


def plan(inv, idx):
    """The parity cases for this card: (name, part, inventory index or -1)."""
    weapons = [inv[WEAPONS + i * WEAPON:WEAPONS + (i + 1) * WEAPON] for i in range(8)]
    shields = [inv[SHIELDS + i * SHIELD:SHIELDS + (i + 1) * SHIELD] for i in range(8)]
    armor = [inv[ARMOR + i * ARMOR_REC:ARMOR + (i + 1) * ARMOR_REC] for i in range(16)]
    category = {i: inv[BLADES + (w[1] - 1) * BLADE + 3] for i, w in enumerate(weapons) if w[1]}
    start = next((i for i, w in enumerate(weapons) if w[1] and w[3]), None)
    # every shield in the bag: the Items menu lists each one (vs_mainMenu_rebuildInventory)
    listed_shields = [i for i in range(8) if shields[i][4]]
    shield = next((i for i in listed_shields if shields[i][1]), None)
    one = [i for i in category if not two_handed(category[i]) and i != start]
    two = [i for i in category if two_handed(category[i])]
    arm_alt = next((a - 1 for a in idx[IDX_ARMOR:IDX_ARMOR + 16]
                    if a and armor[a - 1][0] and armor[a - 1][3] == PART_CATEGORY[LIMB_RARM] and armor[a - 1][38] == 0), None)
    accessory = next((i for i, a in enumerate(armor) if a[0] and a[38] == LIMB_ACCESSORY), None)
    if None in (start, shield, arm_alt, accessory) or two_handed(category[start]) or not one or not two:
        return None
    cases = [
        ("weapon 1H -> 1H", 0, one[0]),
        ("weapon -> two-handed (the shield comes off)", 0, two[0]),
        ("shield on with a two-handed weapon (the weapon comes off)", 1, shield),
        ("weapon from bare hands", 0, start),
    ]
    other_shield = next((i for i in listed_shields if i != shield), None)
    if other_shield is not None:
        cases.append(("shield swap", 1, other_shield))
    cases += [
        ("shield off", 1, -1),
        ("shield on", 1, shield),
        ("right arm swap", 2, arm_alt),
        ("accessory off", 7, -1),
        ("accessory on", 7, accessory),
        ("weapon off (bare hands)", 0, -1),
        ("weapon on", 0, start),
    ]
    return cases


def list_row(inv, idx, part, inventory):
    """MENU3's list row of `inventory` for equip page row `part`: row 0 is
    what is worn (or NONE); then the bag in vs_main_inventoryIndices order."""
    if part == 0:
        recs = [(i, inv[WEAPONS + i * WEAPON:WEAPONS + (i + 1) * WEAPON]) for i in range(8)]
        worn = next((i for i, w in recs if w[3]), None)
        bag = [a - 1 for a in idx[IDX_WEAPONS:IDX_WEAPONS + 8] if a and not recs[a - 1][1][3]]
    elif part == 1:
        recs = [(i, inv[SHIELDS + i * SHIELD:SHIELDS + (i + 1) * SHIELD]) for i in range(8)]
        worn = next((i for i, s in recs if s[1]), None)
        bag = [a - 1 for a in idx[IDX_SHIELDS:IDX_SHIELDS + 8] if a and not recs[a - 1][1][1]]
    else:
        limb = LIMB_ACCESSORY if part == 7 else part - 1
        recs = [inv[ARMOR + i * ARMOR_REC:ARMOR + (i + 1) * ARMOR_REC] for i in range(16)]
        worn = next((a - 1 for a in idx[IDX_ARMOR:IDX_ARMOR + 16] if a and recs[a - 1][38] == limb), None)
        bag = [a - 1 for a in idx[IDX_ARMOR:IDX_ARMOR + 16]
               if a and recs[a - 1][3] == PART_CATEGORY[limb] and recs[a - 1][38] == 0]
    if inventory < 0:
        return 0 if worn is not None else None
    return 1 + bag.index(inventory) if inventory in bag else None


def diff(a, b):
    """[(region, start, end)] of differing byte runs, masks applied."""
    found = []
    for region in a:
        x, y = a[region], b[region]
        if len(x) != len(y):
            found.append((region, 0, max(len(x), len(y))))
            continue
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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    parser.add_argument("--parity-only", action="store_true", help="only the parity list")
    parser.add_argument("--no-parity", action="store_true", help="only the shell checks (debugging)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_equip"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    os.environ.pop("ROOD_SETTINGS", None)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    # (a) the original equip page
    dumps_a = []
    if not args.no_parity:
        shutil.copyfile(args.card, card)
        a = Session("original", out, card, env, bool(args.out))
        try:
            skip = run_original(a, check, dumps_a)
        finally:
            a.close()
        if skip is not None:
            return skip
        if failures:
            print(f"FAIL: {len(failures)} check(s)")
            return 1

    # (b) the shell, a new process from the same card
    shutil.copyfile(args.card, card)
    b = Session("shell", out, card, env, bool(args.out))
    try:
        g = b.game
        g.call("ui_menu", modern=True)
        if dumps_a:
            rest = diff(dumps_a[0][2], b.dump())
            check(not rest, f"both processes start equal {rest}")
        for name, (part, item), want in dumps_a[1:]:
            err = b.shell_open_equipment() or b.shell_equip(part, item) or b.shell_close()
            if not check(err is None, f"shell: {name} {err or ''}"):
                b.shot("stuck")
                break
            b.settle()
            got = b.dump()
            rest = diff(want, got)
            detail = ", ".join(f"{r} {s:#x}..{e:#x} {want[r][s:e].hex()} vs {got[r][s:e].hex()}"
                               for r, s, e in rest[:12])
            check(not rest, f"parity: {name}: the shell's result equals the original's" +
                  (f" (differs: {detail})" if rest else ""))
        if args.parity_only:
            return 1 if failures else 0
        shell_checks(b, check)
    finally:
        b.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


def s16(raw, offset):
    return int.from_bytes(raw[offset:offset + 2], "little", signed=True)


def run_original(a, check, dumps):
    """The parity list through the original menu: dumps gets (name, (part,
    item), dump) per change, the start first. Returns an exit code to stop
    with (skip, wrong room) or None."""
    state = a.game.state()
    if (state["zone"], state["room"]) != (9, 17):
        print(f"FAIL: not in the save room after Continue: {state}")
        return 1
    cases = plan(a.inv(), a.idx())
    if cases is None:
        print("SKIP: the card needs two one-handed weapons (one equipped), a two-handed one, "
              "a shield equipped, a right arm piece in the bag and an accessory worn")
        return 77
    print("cases:", [(c[0], c[1], c[2]) for c in cases])
    dumps.append(("start", None, a.dump()))
    for name, part, item in cases:
        err = a.original_open_equip()
        row = None if err else list_row(a.inv(), a.idx(), part, item)  # the Items menu rebuilt the lists
        if not err and row is None:
            err = f"inventory {item} has no list row"
        err = err or a.original_equip(part, row) or a.original_close()
        if not check(err is None, f"original: {name} (row {row}) {err or ''}"):
            a.shot("stuck")
            break
        a.settle()
        dumps.append((name, (part, item), a.dump()))
    return None


def shell_checks(b, check):
    g = b.game
    # The snapshot: eight slots, names and values as the game holds them.
    err = b.shell_open_equipment()
    if not check(err is None, f"the Equipment tab opens {err or ''}"):
        return
    b.shot("gear")
    e = b.eq()
    inv = b.inv()
    actor2 = g.read(b.actor2(), ACTOR2_BYTES)
    slots = e.get("slots") or []
    check([x["id"] for x in slots] == GEAR_IDS, f"eight gear slots {[x.get('id') for x in slots]}")
    k = next(i for i in range(8) if inv[WEAPONS + i * WEAPON + 3])
    name = decode(inv[WEAPONS + k * WEAPON + 8:WEAPONS + k * WEAPON + 32])
    weapon = slots[0]
    check(weapon["inventory"] == k and weapon["name"] == name,
          f"weapon slot: inventory {weapon['inventory']} '{weapon['name']}' (the record: {k} '{name}')")
    w_dp, w_str, w_int = (u16(actor2, 0x3C + 0x114), s16(actor2, 0x3C + 0x118), s16(actor2, 0x3C + 0x11C))
    check(weapon["dp"] == (w_dp + 99) // 100 and weapon["str"] == w_str and weapon["int"] == w_int,
          f"weapon DP/STR/INT {weapon['dp']}/{weapon['str']}/{weapon['int']} as Ashley's record "
          f"({(w_dp + 99) // 100}/{w_str}/{w_int})")
    limb0 = 0x398 + 0x20  # limbs[0].armor
    a_dp, a_int = u16(actor2, limb0 + 0x30), s16(actor2, limb0 + 0x38)
    check(slots[2]["dp"] == (a_dp + 99) // 100 and slots[2]["int"] == a_int,
          f"right arm DP/INT {slots[2]['dp']}/{slots[2]['int']} as the record ({(a_dp + 99) // 100}/{a_int})")
    check((e["str"], e["int"], e["agl"]) == (u16(actor2, 0x22), u16(actor2, 0x26), u16(actor2, 0x2A)),
          f"Ashley's STR/INT/AGL {e['str']}/{e['int']}/{e['agl']} are the record's (no equipment summed in)")
    check(all(x["name"] and x["name"].isprintable() for x in slots if x["inventory"] >= 0),
          "every worn item has a printable name")

    # Bare hands: Square on the weapon takes it off; Circle + the weapon puts it back.
    b.shell_equip(0, -1)
    w = b.ws()
    check(w["equipped"] == -1 and w["actor_weapon"] == 0 and w["actor_category"] == 10
          and b.eq()["slots"][0]["inventory"] == -1,
          f"bare hands: no weapon (record {w['actor_weapon']}, model category {w['actor_category']})")
    err = b.shell_equip(0, k)
    w = b.ws()
    check(err is None and w["equipped"] == k and w["actor_weapon"] == inv[WEAPONS + k * WEAPON],
          f"from bare hands: weapon {k} again {err or ''}")

    # Mouse: hover gear row 3 (left arm) focuses it, a click opens its list,
    # a click on another candidate equips it.
    g.call("ui_menu", pointer=dict(zip("xy", gear_row_xy(3))))
    g.run(4)
    check(b.eq().get("slot") == "left_arm", f"mouse: hovering row 3 focuses the left arm ({b.eq().get('slot')})")
    g.call("ui_menu", pointer={**dict(zip("xy", gear_row_xy(3))), "click": True})
    g.run(6)
    e = b.eq()
    check(e.get("mode") == "pick", f"mouse: a click opens the list ({e.get('mode')})")
    cands = [c["inventory"] for c in e.get("candidates") or []]
    worn = e["slots"][3]["inventory"]
    other = next((c for c in cands if c != worn and c >= 0), None)
    if check(other is not None, f"mouse: another left arm piece in the list {cands}"):
        b.shot("mouse-pick")
        g.call("ui_menu", pointer={**dict(zip("xy", pick_row_xy(cands.index(other)))), "click": True})
        g.run(6)
        b.shell_wait()
        armor = b.inv()[ARMOR + other * ARMOR_REC:ARMOR + (other + 1) * ARMOR_REC]
        e = b.eq()
        check(e["slots"][3]["inventory"] == other and armor[38] == 2 and e.get("mode") == "gear",
              f"mouse: a click on candidate {other} wears it on the left arm (limb {armor[38]})")
    g.call("ui_menu", pointer={"x": -1, "y": -1})
    g.run(2)

    # Quick swap: slot 2 (Triangle) = a bag weapon, no shield.
    bag = next(i for i in range(8) if inv[WEAPONS + i * WEAPON + 1] and i != k
               and not two_handed(inv[BLADES + (inv[WEAPONS + i * WEAPON + 1] - 1) * BLADE + 3]))
    g.press("r2", vsyncs=3, settle=10)
    for _ in range(9):
        if b.eq().get("quick_slot") == "triangle":
            break
        g.press("down", vsyncs=3, settle=6)
    g.press("circle", vsyncs=3, settle=10)
    e = b.eq()
    cands = [c["inventory"] for c in e.get("candidates") or []]
    ok = e.get("mode") == "quick_weapon" and bag in cands
    if check(ok, f"quick swap: Triangle's weapon list has bag weapon {bag} ({e.get('mode')}, {cands})"):
        for _ in range(len(cands) + 1):
            if b.eq().get("cursor") == cands.index(bag):
                break
            g.press("down", vsyncs=3, settle=6)
        g.press("circle", vsyncs=3, settle=10)
        e = b.eq()
        check(e.get("mode") == "quick_shield" and e.get("cursor") == 0 and e["candidates"][0]["inventory"] == -1,
              f"one-handed: the shield step, on No shield ({e.get('mode')}, cursor {e.get('cursor')})")
        b.shot("quick-shield")
        g.press("circle", vsyncs=3, settle=10)
        raw = b.inv()
        slot_bytes = [raw[WEAPONS + i * WEAPON + 7] for i in range(8)]
        bits = [raw[SHIELDS + i * SHIELD + 0x2F] for i in range(8)]
        q = b.eq()["quick"][1]
        check(slot_bytes[bag] == 2 and slot_bytes.count(2) == 1 and not any(x & 0x02 for x in bits)
              and q["weapon"] == bag and q["shield"] == -1,
              f"quick swap: weapons[{bag}].gems[3] = 2, no shield bit for slot 2 ({slot_bytes}, {bits}; {q})")
    g.press("r2", vsyncs=3, settle=10)
    err = b.shell_close()
    check(err is None, f"the shell closes {err or ''}")
    g.call("weapon_slots", enabled=True)
    g.run(30)
    g.call("input", buttons=["l2", "r2"], vsyncs=60)
    g.run(25)
    g.call("input", buttons=["triangle"], vsyncs=3)
    g.run(6)
    w = wait_swap(b)
    check(w["equipped"] == bag and w["actor_weapon"] == inv[WEAPONS + bag * WEAPON] and w["actor_shield"] == 0,
          f"field: L2 + R2 + Triangle swaps to weapon {bag}, no shield "
          f"(equipped {w['equipped']}, record {w['actor_weapon']}, shield {w['actor_shield']})")
    # Square in the shell clears the slot.
    err = b.shell_open_equipment(sub="quick")
    for _ in range(9):
        if b.eq().get("quick_slot") == "triangle":
            break
        g.press("down", vsyncs=3, settle=6)
    g.press("square", vsyncs=3, settle=10)
    raw = b.inv()
    check(err is None and raw[WEAPONS + bag * WEAPON + 7] == 0 and b.eq()["quick"][1]["weapon"] == -1,
          f"Square clears slot 2 (weapons[{bag}].gems[3] = {raw[WEAPONS + bag * WEAPON + 7]}) {err or ''}")
    g.press("r2", vsyncs=3, settle=10)
    g.call("weapon_slots", enabled=False)

    # Hold: equip a weapon and press Triangle at once: the shell stays (holding)
    # until the model is loaded, then the menu closes with the new model.
    if not check(b.shell_focus(0), "the weapon slot"):
        return
    g.press("circle", vsyncs=3, settle=10)
    e = b.eq()
    cands = [c["inventory"] for c in e.get("candidates") or []]
    target = next(c for c in cands if c not in (bag, -1))
    for _ in range(len(cands) + 1):
        if b.eq().get("cursor") == cands.index(target):
            break
        g.press("down", vsyncs=3, settle=6)
    g.hold("circle", 3)
    for _ in range(10):
        if b.eq().get("busy"):
            break
        g.run(1)
    busy = b.eq().get("busy")
    # Triangle while it loads, then every vsync: the shell must hold (drawn,
    # not visible) until the load is over, unless the load beat Triangle.
    g.call("input", buttons=["triangle"], vsyncs=3)
    held = busy_after = False
    for _ in range(300):
        g.run(1)
        s = b.shell()
        held = held or bool(s.get("holding"))
        if s.get("visible") and (s.get("equipment") or {}).get("busy"):
            busy_after = True
        if held and not s.get("holding"):
            break
        if b.current_menu() == CLOSED:
            break
        if held:
            b.shot("hold")
    if busy and not held and not busy_after:
        print("skip: hold: the model load ended before Triangle reached the shell (nothing to hold)")
    else:
        check(busy and held, f"hold: Triangle while the model loads holds the shell "
              f"(busy {busy}, holding seen {held})")
    for _ in range(60):
        if b.current_menu() == CLOSED:
            break
        g.run(5)
    b.settle()
    w = b.ws()
    blade = inv[WEAPONS + target * WEAPON + 1]
    check(b.current_menu() == CLOSED and not b.shell().get("holding") and w["equipped"] == target
          and w["actor_category"] == inv[BLADES + (blade - 1) * BLADE + 3],
          f"hold: then the menu closes; weapon {target} and its model (category {w['actor_category']})")
    models = b.dump()["models"]
    check(models[14] == inv[BLADES + (blade - 1) * BLADE + 2],
          f"hold: the weapon model is the blade's (model {models[14]}, blade wepId {inv[BLADES + (blade - 1) * BLADE + 2]})")

    # Drawn: battle mode (Circle), then equip another one-handed weapon.
    g.press("circle", vsyncs=3, settle=60)
    drawn = b.ws()["weapon_drawn"]
    err = b.shell_open_equipment()
    err = err or b.shell_equip(0, k)
    err = err or b.shell_close()
    b.settle()
    w = b.ws()
    blade = inv[WEAPONS + k * WEAPON + 1]
    b.shot("drawn")
    check(err is None and drawn == 1 and w["weapon_drawn"] == 1 and w["equipped"] == k
          and w["actor_weapon"] == inv[WEAPONS + k * WEAPON]
          and w["actor_category"] == inv[BLADES + (blade - 1) * BLADE + 3],
          f"drawn: weapon {k} equipped through the shell, still drawn (drawn before {drawn}, "
          f"after {w['weapon_drawn']}, category {w['actor_category']}) {err or ''}")
    g.press("cross", vsyncs=3, settle=60)
    early_equip_checks(b, check, k)
    soft_reset_checks(b, check, k)


def early_equip_checks(b, check, k):
    """An equip in the menu's first ticks: MAINMENU.PRG is still loading
    (each open loads it again), so a weapon equip waits (the snapshot is busy,
    a confirm does nothing); once it is resident the same equip works. The
    mouse drives it (the pad is ignored for the shell's first two ticks):
    every vsync a click on the weapon row, then on the wanted candidate."""
    g = b.game
    settings = symbols()["vs_main_settings"]
    memory = g.read(settings + 1, 1)  # cursorMemory: the shell reopens on its last tab
    g.write(settings + 1, b"\x01")
    err = b.shell_open_equipment() or b.shell_close()
    inv = b.inv()
    other = next(i for i in range(8) if inv[WEAPONS + i * WEAPON + 1] and i != k
                 and not two_handed(inv[BLADES + (inv[WEAPONS + i * WEAPON + 1] - 1) * BLADE + 3]))
    g.run(20)
    g.call("input", buttons=["triangle"], vsyncs=3)
    log = []  # per vsync: (busy, mode, equipped) while the shell shows
    clicked_busy = False
    for _ in range(60):
        g.run(1)
        s = b.shell()
        e = s.get("equipment") or {}
        w = b.ws()
        if not s.get("visible"):
            continue
        log.append((1 if e.get("busy") else 0, e.get("mode"), w["equipped"]))
        if w["equipped"] != k:
            break
        if e.get("mode") == "gear":
            g.call("ui_menu", pointer={**dict(zip("xy", gear_row_xy(0))), "click": True})
        elif e.get("mode") == "pick" and e.get("candidates"):
            cands = [c["inventory"] for c in e["candidates"]]
            clicked_busy = clicked_busy or bool(e.get("busy"))
            g.call("ui_menu", pointer={**dict(zip("xy", pick_row_xy(cands.index(other)))), "click": True})
    g.call("ui_menu", pointer={"x": -1, "y": -1})
    print(f"early equip: (busy, mode, equipped) per vsync from the open: {log}")
    # A click applies at the shell's next tick (two vsyncs at most): three
    # vsyncs on in the busy list with clicks and no equip is a refusal. The
    # equip comes on the tick MAINMENU is resident (busy again: the reload).
    changed = next((i for i, x in enumerate(log) if x[2] != k), None)
    refused = sum(1 for x in log[:changed] if x[0] and x[1] == "pick")
    if not clicked_busy:
        print("skip: early equip: MAINMENU was resident before a candidate could be clicked")
    else:
        check(err is None and refused >= 3,
              f"early equip: candidate clicks while MAINMENU loads equip nothing ({refused} busy vsyncs "
              f"with clicks before the equip) {err or ''}")
    b.shell_wait()
    err = b.shell_close()
    b.settle()
    w = b.ws()
    check(err is None and changed is not None and w["equipped"] == other,
          f"early equip: once resident, the click equips weapon {other} (equipped {w['equipped']}) {err or ''}")
    g.write(settings + 1, memory)


def soft_reset_checks(b, check, k):
    """A soft reset (L1 L2 R1 R2 Start Select, main.c vs_main_processPadState)
    while the shell holds a result for a model load: the session ends
    (boot_sdk.c -> menu_bridge.c pc_ui_menu_reset): over the title nothing of
    the shell is drawn or held, the HUD flag is back, the reload's finish is
    dropped; Continue again gives a working field and menu."""
    g = b.game
    hud = symbols()["D_800EB9AF"]
    hud_before = g.read(hud, 1)[0]
    err = b.shell_open_equipment()
    if not check(err is None and b.shell_focus(0), f"reset: the weapon slot {err or ''}"):
        return
    g.press("circle", vsyncs=3, settle=10)
    for _ in range(40):
        e = b.eq()
        if e.get("mode") == "pick" and not e.get("busy") and e.get("candidates") is not None:
            break
        g.run(3)
    cands = [c["inventory"] for c in e.get("candidates") or []]
    target = next(c for c in cands if c not in (k, -1, b.ws()["equipped"]))
    for _ in range(len(cands) + 1):
        if b.eq().get("cursor") == cands.index(target):
            break
        g.press("down", vsyncs=3, settle=6)
    g.hold("circle", 3)
    g.hold("triangle", 3)  # a held result (the load runs)
    s = b.shell()
    print(f"reset: before the reset: holding {s.get('holding')}, visible {s.get('visible')}, "
          f"reload {b.ws().get('menu_reload')}, HUD flag {g.read(hud, 1)[0]}")
    titles = pathlib.Path(g.log).read_text().count("TITLE MENU READY")
    g.call("input", buttons=["l1", "l2", "r1", "r2", "start", "select"], vsyncs=6)
    for _ in range(100):
        g.run(30)
        if pathlib.Path(g.log).read_text().count("TITLE MENU READY") > titles:
            break
    else:
        check(False, "reset: the title came back")
        return
    s = b.shell()
    w = b.ws()
    check(not s.get("visible") and not s.get("holding") and g.read(hud, 1)[0] == hud_before
          and not w.get("menu_reload"),
          f"reset: over the title the shell is gone (visible {s.get('visible')}, holding {s.get('holding')}), "
          f"HUD flag {g.read(hud, 1)[0]} (before {hud_before}), reload dropped ({w.get('menu_reload')})")
    b.shot("reset-title")
    g.press("start", vsyncs=4, settle=90)
    for _ in range(3):
        g.press("circle", vsyncs=3, settle=90)
    g.run(400)
    state = g.state()
    err = b.shell_open_equipment()
    e = b.eq()
    err = err or b.shell_close()
    check(err is None and (state["zone"], state["room"]) == (9, 17) and e.get("valid")
          and g.read(hud, 1)[0] == hud_before,
          f"reset: Continue again: the field and the menu work (room {state['zone']}/{state['room']}, "
          f"HUD flag {g.read(hud, 1)[0]}) {err or ''}")


def u16(raw, offset):
    return int.from_bytes(raw[offset:offset + 2], "little")


def wait_swap(b, limit=400):
    w = b.ws()
    for _ in range(limit // 6):
        if not w["loading"] and w["field_mode"] == 1:
            break
        b.game.run(6)
        w = b.ws()
    b.game.run(20)
    return b.ws()


if __name__ == "__main__":
    sys.exit(main())
