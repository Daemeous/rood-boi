#!/usr/bin/env python3
"""Fast travel (pc/docs/Fast_Travel.md; pc/src/fast_travel.c, fast_travel_rules.c;
src hooks in MENU0 15AC.c, BATTLE 5BF94.c and 146C.c): the game's own
Teleportation from the first Circle of Magic once the prologue is over.

    python3 pc/tests/fast_travel_smoke.py [--card pc/build/test-card1.mcd] [--menus off|on] [--cases a,b] [--out DIR]

Card safety: everything runs inside card_fixtures.card_guard (the player's
cards hashed before and after; ROOD_CARD1 / ROOD_CARD2 temp copies); the save
case writes only its own copy. Skipped (77) without a card, or when the card's
save room is not a Circle of Magic after the prologue.

Cases (each from the save_room checkpoint in a process of its own; status and
audit share the first):
  status  defaults (on, free), the port's unlock, the points = savePointStates,
          each cost mode, the Magic command shown and not stored, off
  audit   R6: each of the 35 destinations' rooms has its Circle object (id
          point + 1) on the arrival cell, read from the disc
  warp    R7 and warp hygiene: a Room Browser warp (scripts on) onto an
          unfound Circle: travel refused while warped (the button with R7's
          reason, standing on that Circle); back where the warps started:
          savePointStates as before (the Circle recorded meanwhile is put back)
  reload  a warp chain open when the game's state is replaced (soft reset,
          Continue): its snapshot and warped state dropped, the loaded save's
          savePointStates kept after Stay
  menu    the original list (off: Triangle > Magic > TELEPORTATION; on:
          Magick > Teleport): rows, free, the cursor on this Circle; travel to
          the nearest other Circle: MENU0's hand-off, arrival on the Circle,
          MP, magicUsed, unkB5, menuFlags, savePointStates unchanged
  save    travel, then Data > Save as file 2: decoded, checksums, saveLocation
          = the destination + 1, stateFlags = RAM, menuFlags bit 0 not stored,
          the room record = the destination; Continue from file 2: on that
          Circle, saving allowed, travel offered again
  refuse  the prologue (introState 2), unkA7, this Circle unrecorded, an
          event, MP short (game cost), off the Circle: refused (the row greyed,
          or the modern Magick > Teleport page refusing with the reason; the
          button ignored, the room kept); MP 0 free: offered in both menus
  game    unkB5 1: the game's own (raw cost charged, magicUsed set) whatever the
          settings; unkB5 2: everything refused
  here    (off) choosing the Circle Ashley stands on: back to the field
  button  the Travel button: the default binding (a virtual gamepad's back)
          and a key bound to it and shared with Select, then the RPC press:
          the list directly, Select's zoom untouched, Cross closes, travel;
          with fast travel off the same press only zooms; refused presses open
          nothing
  prompt  the prompt shows on the Circle when travel is offered, not when off,
          not when the device used last has nothing bound (original mode too:
          the keyboard shows Travel's key, not Select's PlayStation glyph);
          modern UI only (the classic look draws none); in a 16:9 window
          it sits in the game picture's upper right (inside the 4:3
          picture, not over its side bars; the 16:9 picture too) in the HUD
          labels' font, outlined, no plate (qol_fast_travel_prompt_backdrop 0), and moves with
          the anchor and offsets
  settings (also) an old qol_fast_travel_prompt_x / _y (window fractions)
          is dropped for the anchor's default and not saved again
On the pinned test card a case that cannot run its checks fails instead of
skipping.
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
from checkpoints import launch_at  # noqa: E402
import checkpoints  # noqa: E402
import card_fixtures as cf  # noqa: E402

PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (characterState, menuItems)
CLOSED = 0x3F           # vs_battle_menuState.currentMenu with no menu
FLAGS_SIZE = 0x440      # sizeof(vs_main_flags_t)
UNK_A7, INTRO, UNK_B5, MAGIC_USED, SAVE_POINTS = 0xA7, 0xAB, 0xB5, 0xB7, 0x156
MENU_FLAGS = 2          # vs_main_settings.menuFlags (bit 0: the Magic command)
MP, MP_MAX = 0x1C, 0x1E  # vs_battle_actor2 currentMP / maxMP
ITEM = 64               # sizeof(vs_battle_menuItem_t): state +0, selected +6, unselectable +7
MAGIC_ITEM, TELEPORT_ITEM, LIST_ITEM = 0, 14, 20  # the top level's Magic, MENU0's Teleport row, the destinations
TELEPORT_MENU = 14      # vs_menu0_exec's state while _teleportMenu runs (vs_battle_submenuStates[1])
SHORTCUT_TRAVEL = 9     # vs_battle_shortcutInvoked of the Travel button (fast_travel.h)
INITIAL_DISTANCE = 32   # vs_battle_cameraCurrentSpherical.initialDistance (Select: 0x600 near / 0x900 far)
USED = 35
SAVE_FLAGS, SAVE_SETTINGS, SAVE_ROOM = 0x200, 0x6A8, 0x1778  # savedata_t (MENU7 260.c)


def save_point_data():
    """MENU0 84.c _savePointData (fast_travel_probe checks the port's copy against it)."""
    text = (ROOT / "src/MENU/MENU0.PRG/84.c").read_text()
    body = re.search(r"_savePointData\[\]\s*=\s*\{([^}]*)\}", text).group(1)
    return [int(x, 0) for x in body.replace("\n", " ").split(",") if x.strip()]


DATA = save_point_data()


def packed(point, cost):
    """MENU0's hand-off (D_800F4E98.executeAbility, unsigned)."""
    d = DATA[point]
    return (d & 0x1FF) | ((d >> 16) & 0x1F) << 9 | ((d >> 24) & 0x1F) << 14 | (cost & 0xFFF) << 19


class Trip:
    """One game process at the card's save room."""

    def __init__(self, name, out, card, menus, env, shots=False):
        self.name, self.out, self.card, self.menus, self.env, self.shots = name, out, card, menus, env, shots
        self.g = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env)
        self.sym = symbols()
        self.g.call("graphics", interpolate=False)
        self.g.call("ui_menu", modern=menus == "on")
        self.g.run(2)

    def close(self):
        self.g.close()

    def shot(self, what):
        if self.shots:
            self.g.present_screenshot(self.out / f"{self.name}-{what}.png")

    # ---- reads and writes ----
    def ft(self, **params):
        return self.g.call("fast_travel", **params)

    def ui(self, **params):
        return self.g.call("ui_menu", **params)

    def mg(self):
        return self.ui().get("magick") or {}

    def u8(self, name, offset=0):
        return self.g.read(self.sym[name] + offset, 1)[0]

    def u32(self, address):
        return struct.unpack("<I", self.g.read(address, 4))[0]

    def flags(self):
        return bytes(self.g.read(self.sym["vs_main_stateFlags"], FLAGS_SIZE))

    def set_flag(self, offset, value):
        self.g.write(self.sym["vs_main_stateFlags"] + offset, bytes([value]))

    def settings_bytes(self):
        return bytes(self.g.read(self.sym["vs_main_settings"], 4))

    def menu(self):
        return self.u8("vs_battle_menuState")

    def menu0_state(self):
        return self.u8("vs_battle_submenuStates", 1)

    def actor2(self):
        actor = int.from_bytes(self.g.read(self.sym["vs_battle_characterState"], PTR), "little")
        return int.from_bytes(self.g.read(actor + 0x3C, 4), "little")

    def mp(self):
        return struct.unpack("<h", self.g.read(self.actor2() + MP, 2))[0]

    def set_mp(self, mp, top=None):
        a = self.actor2()
        mp_max = struct.unpack("<h", self.g.read(a + MP_MAX, 2))[0]
        top = max(mp_max, top or 0, mp)
        self.g.write(a + MP, struct.pack("<hh", mp, top))
        self.g.run(2)

    def location(self):
        st = self.g.state()
        table = self.g.read(self.sym["vs_main_locationZoneMapIds"], 1024)
        return next((i for i in range(1, 512) if table[2 * i] == st["zone"] and table[2 * i + 1] == st["room"]), 0)

    def room_of(self, location):
        table = self.g.read(self.sym["vs_main_locationZoneMapIds"], 1024)
        return table[2 * location], table[2 * location + 1]

    def handoff(self):
        e = self.g.read(self.sym["D_800F4E98"], 8)
        unk2 = struct.unpack("<h", e[2:4])[0]
        return unk2, struct.unpack("<I", e[4:8])[0]

    # ---- the original menus ----
    def items(self):
        p = int.from_bytes(self.g.read(self.sym["vs_battle_menuItems"], PTR), "little")
        if not p:
            return {}
        raw = self.g.read(p, ITEM * 40)
        return {i: (raw[i * ITEM], raw[i * ITEM + 6], raw[i * ITEM + 7]) for i in range(40) if raw[i * ITEM]}

    def wait(self, pred, limit, what, step=2, quiet=False):
        for n in range(0, limit + 1, step):
            if pred():
                return n
            self.g.run(step)
        if quiet:
            return None
        raise TimeoutError(f"{self.name}: {what}: not after {limit} vsyncs (currentMenu {self.menu():#x})")

    def wait_ready(self, want, limit=400):
        calm = [0]

        def ready():
            items = self.items()
            ok = want in items and all(s <= 1 for s, _, _ in items.values())
            calm[0] = calm[0] + 1 if ok else 0
            return calm[0] >= 3
        self.wait(ready, limit, f"menu item {want} ready", step=4)
        self.g.run(4)

    def selected(self, ids):
        items = self.items()
        return next((i for i in ids if i in items and items[i][1]), None)

    def nav(self, ids, target):
        """Up / Down until item `target` of the list `ids` is the selected one."""
        ids = list(ids)
        for _ in range(2 * len(ids) + 4):
            cur = self.selected(ids)
            if cur == target:
                return
            self.g.press("down" if cur is None or cur < target else "up", vsyncs=3, settle=6)
        raise TimeoutError(f"{self.name}: item {target} not reached (selected {self.selected(ids)})")

    def close_menu(self):
        for _ in range(8):
            if self.menu() == CLOSED:
                break
            self.g.press("triangle", vsyncs=3, settle=20)
        self.wait(lambda: self.menu() == CLOSED, 300, "the menu closed", step=4)
        self.g.run(10)


def skipped(check, args, message):
    """A case that cannot run its checks: a pass on another card, a failure on the
    pinned test card (whose save room is known to allow every case)."""
    check(not args.pinned, message + (" (on the pinned test card)" if args.pinned else ""))


def nearest_other(s):
    here = s["state"]["here"]
    found = [p["i"] for p in s["points"] if p["state"] == 1 and p["i"] != here]
    return min(found, key=lambda i: abs(i - here)) if found else None


# ---- cases ----
def case_status(t, check, args):
    s = t.ft()
    st = s["state"]
    check(s["settings"] == {"enabled": 1, "cost": "free", "flat": 15, "scale": 0.5, "prompt": 1, "prompt_anchor": 2,
                            "prompt_offset": [-0.025, 0.035], "prompt_scale": 1, "prompt_hold": 8},
          f"status: defaults {s['settings']}")
    check(s["whose"] == "port" and s["unlock"] == 1 and st["game"] == 0,
          f"status: the port's unlock ({s['whose']}, unkB5 {st['game']})")
    here = st["here"]
    check(st["circle"] == here + 1, f"status: on Circle {st['circle'] - 1}, this room's point {here}")
    pts = s["points"]
    ram = t.flags()[SAVE_POINTS:SAVE_POINTS + USED]
    check([p["state"] for p in pts] == list(ram), "status: the points' states are savePointStates")
    check(pts[here]["current"] and all(p["cost"] == 0 for p in pts), "status: free, this room's Circle current")
    other = nearest_other(s)
    check((other is not None) == (s["why"] == ""), f"status: the Travel button: '{s['why']}' (another: {other})")
    check(s["menu_bits"] == 1 and t.settings_bytes()[MENU_FLAGS] & 1 == 0,
          "status: the Magic command shown (menu_bits 1), menuFlags bit 0 not written")
    if other is not None:
        raw = pts[other]["raw"]
        for mode, want in (("flat", 15), ("scaled", int(raw * 0.5 + 0.5)), ("game", raw), ("free", 0)):
            got = t.ft(set={"cost": mode})["points"][other]["cost"]
            check(got == want, f"status: cost {mode}: Circle {other} {got} MP ({want})")
    s = t.ft(set={"enabled": False})
    check(s["whose"] == "off" and s["unlock"] == 0 and s["why"] and s["menu_bits"] == 0,
          f"status: qol_fast_travel 0: off ('{s['why']}')")
    t.ft(set={"enabled": True})


def case_audit(t, check, args):
    s = t.ft(audit=True)
    bad = [a for a in s.get("audit", []) if not a["ok"]]
    check(len(s.get("audit", [])) == USED and not bad,
          f"audit: every destination's room has its Circle on the arrival cell (failing: {bad[:4]})")


def case_warp(t, check, args):
    g = t.g
    s = t.ft()
    before = t.flags()[SAVE_POINTS:SAVE_POINTS + 48]
    origin = (g.state()["zone"], g.state()["room"])
    unfound = [i for i in range(USED) if s["points"][i]["state"] == 0]
    target = None
    for k in unfound:
        zone, room = t.room_of(DATA[k] & 0x1FF)
        # onto its Circle (the arrival cell MENU0's hand-off uses: x, level 0, z)
        cell = [(DATA[k] >> 16) & 0x1F, 0, (DATA[k] >> 24) & 0x1F, 0]
        r = g.call("room_goto", zone=zone, room=room, enemies=False, scripts=True, cell=cell)
        if r["ok"]:
            target = k
            break
    if target is None:
        skipped(check, args, "warp: no unfound Circle's room to warp to (skipped)")
        return
    t.wait(lambda: (g.state()["zone"], g.state()["room"]) == (zone, room) and g.call("rooms")["phase"] in (0, 4),
           1800, "the warp arrives", step=4)
    g.run(120)  # its entry script runs (a Circle's records it)
    if t.flags()[SAVE_POINTS + target] == 0:
        t.set_flag(SAVE_POINTS + target, 1)  # as its script would (with the Circle's own event already played)
        print(f"info: warp: Circle {target}'s script did not record it here; recorded by the test")
    s = t.ft()
    check(s["state"]["warped"] == 1 and s["blocked"].startswith("Leave the Room Browser"),
          f"warp: R7 while warped: blocked '{s['blocked']}'")
    check(s["state"]["circle"] == target + 1, f"warp: standing on Circle {target} ({s['state']['circle'] - 1})")
    check(s["why"] == "Leave the Room Browser warp first.",
          f"warp: the Travel button refused with R7's reason on the Circle ('{s['why']}')")
    check(button_refused(t), "warp: a Travel press while warped opens nothing")
    g.call("room_goto", action="return")
    t.wait(lambda: not g.call("room_goto", action="status")["warped"] and g.call("rooms")["phase"] in (0, 4)
           and (g.state()["zone"], g.state()["room"]) == origin, 1800, "back where the warps started", step=4)
    g.run(30)
    after = t.flags()[SAVE_POINTS:SAVE_POINTS + 48]
    check(after == before, f"warp: savePointStates as before the warps (Circle {target}: {before[target]} -> "
                           f"{after[target]})")
    check(t.ft()["state"]["warped"] == 0 and t.ft()["blocked"] == "", "warp: travel offered again")


def soft_reset(t):
    """The game's soft reset (Start + Select + L1 + R1 + L2 + R2, main.c VS_RESET) by
    the default keys, then Continue the card's first save, as the save_room
    checkpoint does; the field again."""
    g = t.g
    log = pathlib.Path(g.log)
    titles = log.read_text(errors="replace").count("TITLE MENU READY")
    chord = ["1", "2", "q", "w", "tab"]  # L2, R2, L1, R1, Select
    for k in chord:
        g.call("system", key=k, down=True)
    g.run(6)
    g.call("system", key="return", down=True)  # Start: pressed while the rest are held
    g.run(6)
    for k in chord + ["return"]:
        g.call("system", key=k, down=False)
    for _ in range(60):
        if log.read_text(errors="replace").count("TITLE MENU READY") > titles:
            break
        g.run(30)
    else:
        raise TimeoutError("the soft reset did not reach the title menu")
    g.run(60)
    g.press("start", vsyncs=4, settle=90)
    for _ in range(3):  # Continue, the card, its first save
        g.press("circle", vsyncs=3, settle=90)
    t.wait(lambda: t.ft()["state"]["circle"] and t.menu() == CLOSED and t.u8("D_800F196C") == 1, 2400,
           "Continue: in the field again", step=8)
    g.run(30)


def case_reload(t, check, args):
    g = t.g
    s = t.ft()
    card_points = t.flags()[SAVE_POINTS:SAVE_POINTS + 48]
    unfound = [i for i in range(USED) if s["points"][i]["state"] == 0]
    if len(unfound) < 2:
        skipped(check, args, "reload: fewer than two unfound Circles")
        return
    # an extra Circle in RAM only (not on the card), then a warp chain: the snapshot holds it
    extra = unfound[-1]
    t.set_flag(SAVE_POINTS + extra, 1)
    target = None
    for k in unfound[:-1]:
        zone, room = t.room_of(DATA[k] & 0x1FF)
        if g.call("room_goto", zone=zone, room=room, enemies=False, scripts=True)["ok"]:
            target = k
            break
    if target is None:
        skipped(check, args, "reload: no unfound Circle's room to warp to")
        return
    t.wait(lambda: (g.state()["zone"], g.state()["room"]) == (zone, room) and g.call("rooms")["phase"] in (0, 4),
           1800, "the warp arrives", step=4)
    g.run(60)
    st = g.call("room_goto", action="status")
    check(st["warped"] and st["points_held"] == 1, f"reload: warped, the snapshot held ({st})")
    soft_reset(t)
    st = g.call("room_goto", action="status")
    check(not st["warped"] and st["points_held"] == 0, f"reload: the loaded game: no warp, no snapshot ({st})")
    loaded = t.flags()[SAVE_POINTS:SAVE_POINTS + 48]
    check(loaded == card_points, f"reload: savePointStates the card's (Circle {extra}: {loaded[extra]})")
    g.call("room_goto", action="clear")  # Stay (allow saving): the old chain's snapshot must not come back
    g.run(30)
    after = t.flags()[SAVE_POINTS:SAVE_POINTS + 48]
    check(after == card_points, f"reload: after Stay, savePointStates still the loaded save's (Circle {extra}: "
                                f"{after[extra]}, Circle {target}: {after[target]})")
    s = t.ft()
    check(s["state"]["warped"] == 0 and s["blocked"] == "", f"reload: travel not refused as warped ('{s['blocked']}')")


def open_list(t):
    """Triangle > Magic > TELEPORTATION (the original menus): MENU0's destinations list up."""
    g = t.g
    for _ in range(4):  # Triangle is ignored while an action still runs
        g.press("triangle", vsyncs=3, settle=10)
        if t.wait(lambda: MAGIC_ITEM in t.items(), 60, "", step=4, quiet=True) is not None:
            break
    t.wait_ready(MAGIC_ITEM)
    t.nav(range(0, 10), MAGIC_ITEM)
    g.press("circle", vsyncs=3, settle=10)
    t.wait_ready(TELEPORT_ITEM)
    t.nav(range(10, 15), TELEPORT_ITEM)
    g.press("circle", vsyncs=3, settle=10)
    wait_list(t)


def wait_list(t):
    t.wait(lambda: t.menu0_state() == TELEPORT_MENU and LIST_ITEM in t.items(), 180, "the destinations list")
    t.wait_ready(LIST_ITEM)


def pick(t, rows, point):
    """MENU0's list: the cursor to `point`'s row, Circle; the hand-off (unk2, executeAbility)."""
    if len(rows) > 8:
        raise TimeoutError(f"{len(rows)} Circles found: the list pages (not driven here)")
    t.nav(range(LIST_ITEM, LIST_ITEM + len(rows)), LIST_ITEM + rows.index(point))
    t.g.press("circle", vsyncs=3, settle=1)
    t.wait(lambda: t.handoff()[0] == 0xA, 120, "the hand-off (unk2 0xA)", step=1)
    return t.handoff()


def mp_drop(t, limit=150):
    """MP regenerates meanwhile: the step where it goes down (case 10's charge), 0 when none."""
    last, drop = [t.mp()], []

    def seen():
        v = t.mp()
        if v < last[0] and not drop:
            drop.append(last[0] - v)
        last[0] = v
        return bool(drop)
    t.wait(seen, limit, "", step=1, quiet=True)
    return drop[0] if drop else 0


def arrive(t, target, limit=1800):
    loc = DATA[target] & 0x1FF

    def there():
        s = t.ft()  # (first: the report is read on every vsync of the load too)
        if t.menu() != CLOSED or t.location() != loc:
            return False
        # the trip checked, and Ashley free again (the arrival's animation over: a menu can open)
        return s["last"]["valid"] and s["last"]["to"] == target and not s["state"]["busy"] \
            and not s["state"]["event"]
    t.wait(there, limit, f"arrival at Circle {target}", step=1)  # (the report every vsync of the trip's load)
    t.g.run(60)
    return t.ft()


def travel_to(t, check, args, target, what):
    """Through the menus to `target`: (the report after arrival, the MP charged)."""
    s = t.ft()
    rows = [p["i"] for p in s["points"] if p["state"]]
    if args.menus == "off":
        open_list(t)
        unk2, ability = pick(t, rows, target)
    else:
        unk2, ability = shell_travel(t, target)
    want = packed(target, s["points"][target]["cost"])
    check(unk2 == 0xA and ability == want,
          f"{what}: MENU0's hand-off: unk2 {unk2:#x}, executeAbility {ability:#x} ({want:#x})")
    drop = mp_drop(t)
    s = arrive(t, target)
    if not drop:  # charged before the watch began (the shell's hand-off): the trip's own MP record
        drop = max(0, s["last"]["mp_before"] - s["last"]["mp_after"])
    return s, drop


def open_shell(t):
    """The modern menus' shell open on the top level (Triangle until it is: Triangle is
    ignored while an action still runs)."""
    g = t.g
    for _ in range(12):
        if t.ui().get("visible") and t.menu() == 0:
            break
        g.press("triangle", vsyncs=3, settle=4)
        t.wait(lambda: t.ui().get("visible") and t.menu() == 0, 120, "", step=2, quiet=True)
    t.wait(lambda: t.ui().get("visible") and t.menu() == 0, 2, "the shell opens")
    g.run(6)  # the shell ignores input for its first ticks


def shell_teleport(t):
    """The modern menus: the shell's Magick tab and its Teleport snapshot (None when
    the Magick tab is not there); the shell closed again."""
    g = t.g
    open_shell(t)
    for _ in range(8):
        if t.ui().get("tab") == "magick":
            break
        g.press("r1", vsyncs=3, settle=4)
    tp = None
    if t.ui().get("tab") == "magick":
        for _ in range(6):
            if t.mg().get("sub") == "teleport" or "teleport" not in t.mg().get("subs", []):
                break
            g.press("r2", vsyncs=3, settle=4)
        g.run(4)
        tp = t.mg().get("teleport")
    t.close_menu()
    return tp


def shell_travel(t, target):
    """The modern menus: Magick > Teleport, the cursor to `target`, Circle; the hand-off."""
    g = t.g
    for _ in range(6):  # Triangle is ignored while an action still runs
        if t.ui().get("visible") and t.menu() == 0:
            break
        g.press("triangle", vsyncs=3, settle=4)
        t.wait(lambda: t.ui().get("visible") and t.menu() == 0, 120, "", step=2, quiet=True)
    t.wait(lambda: t.ui().get("visible") and t.menu() == 0, 2, "the shell opens")
    g.run(6)  # the shell ignores input for its first ticks
    for _ in range(8):
        if t.ui().get("tab") == "magick":
            break
        g.press("r1", vsyncs=3, settle=4)
    t.wait(lambda: t.ui().get("tab") == "magick", 20, "the Magick tab")
    for _ in range(6):
        if t.mg().get("sub") == "teleport":
            break
        g.press("r2", vsyncs=3, settle=4)
    t.wait(lambda: t.mg().get("sub") == "teleport", 20, "the Teleport sub-tab")
    tp = t.mg()["teleport"]
    d = next((x for x in tp["destinations"] if x["point"] == target), None)
    if not (tp["exists"] and tp["enabled"] and d and d["usable"]):
        raise TimeoutError(f"Teleport not usable: '{tp['reason']}' / {d}")
    for _ in range(50):
        if t.mg()["focus"] == target:
            break
        g.press("down", vsyncs=3, settle=4)
    g.press("circle", vsyncs=3, settle=1)
    t.wait(lambda: not t.ui()["visible"], 60, "the shell closes for the teleport")
    t.wait(lambda: t.handoff()[0] == 0xA, 120, "the hand-off", step=1)
    return t.handoff()


def case_menu(t, check, args):
    g = t.g
    s = t.ft()
    here, target = s["state"]["here"], nearest_other(s)
    if target is None:
        skipped(check, args, "menu: no other Circle found (skipped)")
        return
    rows = [p["i"] for p in s["points"] if p["state"]]
    t.set_mp(t.mp() // 2 + 1)  # MP short of the game's cost would grey nothing: free
    flags0, settings0 = t.flags(), t.settings_bytes()
    if args.menus == "off":
        open_list(t)
        items = t.items()
        greyed = [items.get(LIST_ITEM + r, (0, 0, 9))[2] for r in range(len(rows))]
        check(not any(greyed), f"menu: no row greyed (free, MP {t.mp()}): {greyed}")
        check(t.selected(range(LIST_ITEM, LIST_ITEM + len(rows))) == LIST_ITEM + rows.index(here),
              "menu: the cursor starts on this Circle (MENU0's cost-15 row)")
        t.shot("list")
        unk2, ability = pick(t, rows, target)
        check(unk2 == 0xA and ability == packed(target, 0),
              f"menu: MENU0's hand-off: unk2 {unk2:#x}, executeAbility {ability:#x} ({packed(target, 0):#x})")
        drop = mp_drop(t)
        s = arrive(t, target)
    else:
        s, drop = travel_to(t, check, args, target, "menu")
    last = s["last"]
    check(last["arrived"] and last["circle"] == target + 1 and last["room_ok"],
          f"menu: arrived on Circle {target} ({last})")
    check(last["whose"] == "port" and last["counted"] == 0 and drop == 0,
          f"menu: the port's unlock, free (MP charged {drop}), not counted as magic")
    flags = t.flags()
    check(flags[MAGIC_USED] == flags0[MAGIC_USED], f"menu: magicUsed {flags0[MAGIC_USED]} -> {flags[MAGIC_USED]}")
    check(flags[UNK_B5] == 0 and flags[SAVE_POINTS:SAVE_POINTS + 48] == flags0[SAVE_POINTS:SAVE_POINTS + 48],
          "menu: unkB5 and savePointStates unchanged")
    check(t.settings_bytes()[MENU_FLAGS:MENU_FLAGS + 2] == settings0[MENU_FLAGS:MENU_FLAGS + 2],
          "menu: menuFlags unchanged")
    check(not g.call("room_goto", action="status")["warped"], "menu: not a Room Browser warp")
    t.shot("arrived")


def case_save(t, check, args):
    from ui_data_smoke import DATA_MENU, menu7_driver
    s = t.ft()
    target = nearest_other(s)
    if target is None:
        skipped(check, args, "save: no other Circle found (skipped)")
        return
    if args.menus == "off":
        rows = [p["i"] for p in s["points"] if p["state"]]
        open_list(t)
        pick(t, rows, target)
        s = arrive(t, target)
    else:
        s, _ = travel_to(t, check, args, target, "save")
    check(s["last"]["arrived"], f"save: on Circle {target} ({s['last']})")
    d = menu7_driver(t.g, "save", t.out, modern=args.menus == "on", card1=t.card)
    w0 = d.writes()
    try:
        if args.menus == "on":
            # Session.open_data's System > Data, the shell opened by Triangle until it is (an
            # enemy where the trip arrived holds the field while it attacks)
            open_shell(t)
            for _ in range(10):
                if t.ui()["tab"] == "system":
                    break
                d.press("l1", settle=8)
            for _ in range(16):
                if t.ui()["row"] == "data":
                    break
                d.press("down", settle=4)
            d.press("circle", settle=2)
            d.wait(lambda: d.current_menu() == DATA_MENU and d.menu7_state() == 6, "the Data chooser")
            t.g.run(20)
        else:
            d.open_data()
    except Exception:
        ui = t.ui() if args.menus == "on" else {}
        print(f"info: save: Data not reached: currentMenu {t.menu():#x}, MENU7 state {d.menu7_state()}, "
              f"shell {ui.get('visible')} {ui.get('tab')} {ui.get('row')}, state {t.ft()['state']}")
        t.shot("save-data-failed")
        raise
    d.chooser(0)       # Save
    d.card_rows(1)     # memory card 1
    d.file_rows(1)     # file 2 (new)
    d.confirm_pad()
    d.wait(lambda: d.writes() > w0, "the save written")
    t.g.run(30)
    d.press("circle", settle=10)  # "saved"
    d.back_out()
    d.close_menu()
    ram, settings = t.flags(), t.settings_bytes()
    zone, room = t.g.state()["zone"], t.g.state()["room"]
    image = bytearray(t.card.read_bytes())
    files = cf.saves(image)
    check(2 in files, f"save: file 2 written ({sorted(files)})")
    if 2 not in files:
        return
    plain = cf.decode(cf.read_file(image, files[2]))
    check(cf.checksums_ok(plain), "save: the checksums are valid")
    loc = cf.stats(plain)["location"]
    check(loc == target + 1, f"save: saveLocation {loc} (Circle {target} + 1)")
    check(bytes(plain[SAVE_FLAGS:SAVE_FLAGS + FLAGS_SIZE]) == ram,
          "save: stateFlags as RAM (unkB5, savePointStates, magicUsed as the game had them)")
    check(plain[SAVE_SETTINGS + MENU_FLAGS] & 1 == 0 and settings[MENU_FLAGS] & 1 == 0,
          "save: menuFlags bit 0 (Magic) not stored")
    check((plain[SAVE_ROOM], plain[SAVE_ROOM + 1]) == (zone, room),
          f"save: the room record (unk1778) {plain[SAVE_ROOM]}/{plain[SAVE_ROOM + 1]} is the destination {zone}/{room}")
    t.close()
    # Continue from file 2
    g = t.g = Game.launch(fast=True, card=str(t.card), log=t.out / "save-continue.log", extra_env=t.env)
    checkpoints.title(g)  # the title menu taking input (as the save_room checkpoint starts)
    g.press("start", vsyncs=4, settle=90)
    g.press("circle", vsyncs=3, settle=90)  # Continue
    g.press("circle", vsyncs=3, settle=90)  # memory card 1
    # (the load list starts on the save written last: file 2)
    g.press("circle", vsyncs=3, settle=1)
    loc = DATA[target] & 0x1FF
    # the report every vsync through the load (it must never read a room while one loads)
    t.wait(lambda: t.ft()["state"]["circle"] and t.location() == loc and t.menu() == CLOSED, 1800,
           "Continue: in the field on the destination's Circle", step=1, quiet=True)
    g.run(60)
    s = t.ft()
    check(t.location() == DATA[target] & 0x1FF and s["state"]["circle"] == target + 1,
          f"continue: on Circle {target} (location {t.location():#x}, standing on {s['state']['circle'] - 1})")
    check(t.flags()[UNK_A7] == 0, "continue: saving allowed (on a Circle, unkA7 0: MENU7's two conditions)")
    check(s["why"] == "" or nearest_other(s) is None, f"continue: travel offered again ('{s['why']}')")


def magic_rows(t):
    """Triangle: the top level's Magic row (None absent, "greyed", "on") and, through
    it, MENU0's Teleportation row (the same); the menu closed again."""
    g = t.g
    for _ in range(4):
        g.press("triangle", vsyncs=3, settle=10)
        if t.wait(lambda: t.menu() != CLOSED and t.items(), 60, "", step=4, quiet=True) is not None:
            break
    g.run(40)
    items = t.items()
    magic = None if MAGIC_ITEM not in items else ("greyed" if items[MAGIC_ITEM][2] else "on")
    teleport = t.magic_title = None
    if magic == "on":
        t.nav(range(0, 10), MAGIC_ITEM)
        g.press("circle", vsyncs=3, settle=10)
        t.wait_ready(10)
        items = t.items()
        teleport = None if TELEPORT_ITEM not in items else ("greyed" if items[TELEPORT_ITEM][2] else "on")
        # the Magic title MAINMENU sets over the school list (C48.c, item 0)
        t.magic_title = None if MAGIC_ITEM not in items else ("greyed" if items[MAGIC_ITEM][2] else "on")
    t.close_menu()
    return magic, teleport


def button_refused(t):
    """The Travel button pressed: nothing opened, the room kept, the press counted as ignored."""
    s0, loc = t.ft(), t.location()
    t.ft(press=True)
    # the field poll takes it (not while an action plays: an enemy's attack holds the field loop)
    t.wait(lambda: t.ft()["presses"] > s0["presses"], 300, "", step=4, quiet=True)
    t.g.run(8)
    s = t.ft()
    ok = t.menu() == CLOSED and t.location() == loc and s["ignored"] == s0["ignored"] + 1
    if not ok:
        print(f"info: button_refused: menu {t.menu():#x}, location {loc:#x} -> {t.location():#x}, presses "
              f"{s0['presses']} -> {s['presses']}, ignored {s0['ignored']} -> {s['ignored']}, why '{s['why']}', "
              f"state {s['state']}")
    return ok


def case_refuse(t, check, args):
    g, off = t.g, args.menus == "off"
    here = t.ft()["state"]["here"]
    t.set_mp(99, 99)
    # the prologue (introState < 3): the port's unlock off, nothing offered
    intro = t.flags()[INTRO]
    t.set_flag(INTRO, 2)
    g.run(6)
    s = t.ft()
    check(s["whose"] == "off" and s["unlock"] == 0 and s["menu_bits"] == 0 and s["why"] == "Not yet: after the prologue."
          and not s["prompt"], f"refuse: introState 2: off ({s['whose']}, menu_bits {s['menu_bits']}, '{s['why']}', "
                               f"prompt {s['prompt']})")
    check(button_refused(t), "refuse: introState 2: the Travel button opens nothing")
    if off:
        rows = magic_rows(t)
        check(rows[1] is None, f"refuse: introState 2: no Teleportation row {rows}")
    else:
        tp = shell_teleport(t)
        check(tp is None or not tp["exists"], f"refuse: introState 2: no Teleport page ({tp and tp['exists']})")
    t.set_flag(INTRO, intro)
    g.run(4)
    # R3: the game refuses to save here now
    t.set_flag(UNK_A7, 1)
    g.run(2)
    s = t.ft()
    check(s["blocked"] == "Cannot travel from here now." and s["why"], f"refuse: R3 unkA7: '{s['blocked']}'")
    check(button_refused(t), "refuse: R3: the Travel button opens nothing")
    if off:
        rows = magic_rows(t)
        check(rows == ("on", "greyed"), f"refuse: R3: MENU0's Teleportation row greyed {rows}")
    else:
        tp = shell_teleport(t)
        check(tp is not None and tp["exists"] and not tp["enabled"] and tp["reason"] == "Cannot travel from here now.",
              f"refuse: R3: Magick > Teleport refused ({tp and (tp['exists'], tp['enabled'], tp['reason'])})")
    t.set_flag(UNK_A7, 0)
    # R1: this room's Circle not recorded (MENU0's cursor would have no current row)
    t.set_flag(SAVE_POINTS + here, 0)
    g.run(2)
    s = t.ft()
    check("not recorded" in s["blocked"], f"refuse: R1 this Circle unrecorded: '{s['blocked']}'")
    check(button_refused(t), "refuse: R1: the Travel button opens nothing")
    if off:
        rows = magic_rows(t)
        check(rows[1] == "greyed", f"refuse: R1: MENU0's Teleportation row greyed {rows}")
    else:
        tp = shell_teleport(t)
        check(tp is not None and tp["exists"] and not tp["enabled"] and "not recorded" in tp["reason"],
              f"refuse: R1: Magick > Teleport refused ({tp and (tp['exists'], tp['enabled'], tp['reason'])})")
    t.set_flag(SAVE_POINTS + here, 1)
    # an event script running
    n = t.ft()["ignored"]
    g.write(t.sym["D_800F4C34"], b"\x01")
    t.ft(press=True)
    g.run(2)
    s = t.ft()
    g.write(t.sym["D_800F4C34"], b"\x00")
    g.run(6)
    check(s["state"]["event"] == 1 and s["why"] == "Not during an event." and t.menu() == CLOSED
          and t.ft()["ignored"] == n + 1, f"refuse: an event script: '{s['why']}'")
    # MP 0 with the game's cost: every other Circle refused, the Magic command greyed as the game's
    t.ft(set={"cost": "game"})
    t.set_mp(0)
    s = t.ft()
    check(s["why"] != "" and all(p["refused"] for p in s["points"] if p["i"] != here and p["state"] == 1),
          f"refuse: MP 0, the game's cost: refused ('{s['why']}')")
    check(button_refused(t), "refuse: MP 0, the game's cost: the Travel button opens nothing")
    if off:
        t.set_mp(0)  # (MP comes back over time: still 0 when the menu opens)
        rows = magic_rows(t)
        check(rows[0] == "greyed", f"refuse: MP 0, the game's cost: the Magic command greyed {rows}")
    else:
        t.set_mp(0)
        tp = shell_teleport(t)
        others = [d for d in (tp or {}).get("destinations", []) if d["point"] != here]
        check(tp is None or (others and all(not d["usable"] and d["reason"] == "Not enough MP." for d in others)),
              f"refuse: MP 0, the game's cost: Magick > Teleport's destinations refused "
              f"({[(d['point'], d['reason']) for d in others]})")
    # ... and free: MP 0 greys nothing
    t.ft(set={"cost": "free"})
    s = t.ft()
    check(s["why"] == "" or nearest_other(s) is None, f"refuse: MP 0, free: offered ('{s['why']}')")
    if off:
        t.set_mp(0)
        rows = magic_rows(t)
        check(rows == ("on", "on"), f"refuse: MP 0, free: Magic and Teleportation selectable {rows}")
        check(t.magic_title == "on", f"refuse: MP 0, free: MAINMENU's Magic title not greyed ({t.magic_title})")
    else:
        t.set_mp(0)
        tp = shell_teleport(t)
        others = [d for d in (tp or {}).get("destinations", []) if d["point"] != here]
        check(tp is not None and tp["enabled"] and others and all(d["usable"] for d in others),
              f"refuse: MP 0, free: Magick > Teleport offers every destination "
              f"({tp and tp['reason']}, {[(d['point'], d['reason']) for d in others]})")
    t.set_mp(99)
    # off the Circle
    for d in ("down", "up", "left", "right"):
        g.hold([d], 24)
        g.run(30)
        if t.ft()["state"]["circle"] == 0:
            break
    s = t.ft()
    if s["state"]["circle"]:
        skipped(check, args, "refuse: could not walk off the Circle (skipped)")
        return
    check(s["why"] == "Stand on a Circle of Magic." and s["menu_bits"] == 0,
          f"refuse: off the Circle: '{s['why']}', menu_bits {s['menu_bits']}")
    check(button_refused(t), "refuse: off the Circle: the Travel button opens nothing")
    if off:
        rows = magic_rows(t)
        check(rows[0] is None or rows[1] == "greyed", f"refuse: off the Circle: no travel in the menu {rows}")
    else:
        tp = shell_teleport(t)
        check(tp is None or not tp["exists"] or not tp["enabled"],
              f"refuse: off the Circle: Magick > Teleport refused ({tp and (tp['exists'], tp['enabled'], tp['reason'])})")


def case_game(t, check, args):
    g, off = t.g, args.menus == "off"
    t.set_flag(UNK_B5, 1)
    t.set_flag(MAGIC_USED, 0)
    t.set_mp(99, 99)
    s = t.ft()
    target = nearest_other(s)
    check(s["whose"] == "game" and s["unlock"] == 1, f"game: unkB5 1: the game's unlock ({s['whose']})")
    if target is None:
        skipped(check, args, "game: no other Circle found (skipped)")
        return
    raw = s["points"][target]["raw"]
    check(s["points"][target]["cost"] == raw, f"game: the game's cost {raw} whatever qol_fast_travel_cost (free)")
    s, drop = travel_to(t, check, args, target, "game")
    check(s["last"]["arrived"] and s["last"]["whose"] == "game" and s["last"]["counted"] == 1,
          f"game: arrived, the game's trip ({s['last']})")
    check(drop == raw, f"game: MP charged {drop} ({raw}, _getTeleportCost)")
    check(t.flags()[MAGIC_USED] == 1, "game: magicUsed set, as the game's Teleportation does")
    t.set_flag(UNK_B5, 2)
    g.run(2)
    s = t.ft()
    check(s["unlock"] == 2 and "paling" in s["why"], f"game: unkB5 2: the spirit paling ('{s['why']}')")
    check(button_refused(t), "game: unkB5 2: the Travel button opens nothing")
    if off:
        rows = magic_rows(t)
        check(rows[1] == "greyed", f"game: unkB5 2: MENU0's Teleportation row greyed {rows}")
    else:
        tp = shell_teleport(t)
        check(tp is not None and tp["exists"] and not tp["enabled"] and "paling" in tp["reason"],
              f"game: unkB5 2: Magick > Teleport refused ({tp and (tp['exists'], tp['enabled'], tp['reason'])})")


def case_here(t, check, args):
    if args.menus == "on":
        check(True, "here: the original list only (skipped)")
        return
    s = t.ft()
    here = s["state"]["here"]
    rows = [p["i"] for p in s["points"] if p["state"]]
    open_list(t)
    unk2, ability = pick(t, rows, here)
    check(unk2 == 0xA and ability == packed(here, 0), f"here: MENU0's hand-off for this Circle ({ability:#x})")
    n = t.wait(lambda: t.menu() == CLOSED and t.u8("D_800F196C") == 1 and t.ft()["last"]["valid"], 900, "",
               step=4, quiet=True)
    check(n is not None, "here: back to the field (no hang)")
    s = t.ft()
    check(s["last"]["room_ok"] and s["state"]["circle"] == here + 1, f"here: the same room, on its Circle ({s['last']})")


def key(t, name, hold=3):
    """A real key press (SDL events through the system RPC)."""
    t.g.call("system", key=name, down=True)
    t.g.run(hold)
    t.g.call("system", key=name, down=False)
    t.g.run(10)


def case_button(t, check, args):
    g = t.g
    cam = t.sym["vs_battle_cameraCurrentSpherical"] + INITIAL_DISTANCE
    s = t.ft()
    here, target = s["state"]["here"], nearest_other(s)
    if target is None:
        skipped(check, args, "button: no other Circle found (skipped)")
        return
    rows = [p["i"] for p in s["points"] if p["state"]]
    loc = t.location()
    # the default binding: the gamepad's back (Select's element too), pressed on a virtual
    # gamepad the game polls as a real one (pc_controller_poll, pc_window_travel_held)

    def pad_back():
        g.call("system", virtual_pad="back", down=True)
        g.run(4)
        g.call("system", virtual_pad="back", down=False)
        g.run(10)
    t.ft(set={"enabled": False})  # the control: back is only Select, the camera's zoom toggles
    d0 = t.u32(cam)
    pad_back()
    zoomed = t.u32(cam) != d0
    check(zoomed and t.menu() == CLOSED, f"button: fast travel off: the pad's back is Select (zoom {d0:#x} -> "
                                         f"{t.u32(cam):#x}, menu {t.menu():#x})")
    if zoomed:
        pad_back()  # back
    t.ft(set={"enabled": True})
    d0 = t.u32(cam)
    pad_back()
    t.wait(lambda: t.menu() == 1 and t.u8("vs_battle_shortcutInvoked") == SHORTCUT_TRAVEL, 30,
           "the default pad binding opens MENU0 (currentMenu 1, shortcut 9)")
    wait_list(t)
    check(t.u32(cam) == d0, f"button: the pad's back: the list, Select's zoom not toggled ({d0:#x} -> {t.u32(cam):#x})")
    g.press("cross", vsyncs=3, settle=10)
    t.wait(lambda: t.menu() == CLOSED, 180, "Cross closes the menu", step=2)
    g.run(20)
    # a key for Travel, shared with Select (Tab), as the default pad element (back) is
    g.call("button_prompts", bind={"bind_key_travel": "tab"})
    t.ft(set={"enabled": False})  # the control: Tab is only Select, the camera's zoom toggles
    d0 = t.u32(cam)
    key(t, "tab")
    if t.u32(cam) != d0:
        key(t, "tab")  # back
    else:
        print("info: button: Select does not toggle the zoom here (no control)")
    t.ft(set={"enabled": True})
    d0 = t.u32(cam)
    key(t, "tab")
    t.wait(lambda: t.menu() == 1 and t.u8("vs_battle_shortcutInvoked") == SHORTCUT_TRAVEL, 30,
           "the Travel button opens MENU0 (currentMenu 1, shortcut 9)")
    wait_list(t)
    items = t.items()
    check(TELEPORT_ITEM in items and not any(i in items for i in range(10, 14)),
          "button: straight to the destinations (the TELEPORTATION title, no school rows)")
    check(t.selected(range(LIST_ITEM, LIST_ITEM + len(rows))) == LIST_ITEM + rows.index(here),
          "button: the cursor on this Circle")
    check(t.u32(cam) == d0, f"button: Select's zoom not toggled by the shared key ({d0:#x} -> {t.u32(cam):#x})")
    t.shot("button-list")
    g.press("cross", vsyncs=3, settle=10)
    t.wait(lambda: t.menu() == CLOSED, 180, "Cross closes the menu", step=2)
    check(t.handoff()[0] != 0xA and t.location() == loc, "button: Cross: closed, nothing handed to the game")
    g.run(20)
    t.ft(press=True)  # the pad button's path (RPC press)
    g.run(4)
    wait_list(t)
    unk2, ability = pick(t, rows, target)
    check(unk2 == 0xA and ability == packed(target, 0), f"button: MENU0's hand-off ({ability:#x})")
    s = arrive(t, target)
    check(s["last"]["arrived"] and s["opened"] == 3, f"button: arrived on Circle {target} (opened {s['opened']})")
    t.set_flag(UNK_A7, 1)
    check(button_refused(t), "button: a refused press (unkA7) opens nothing")
    t.set_flag(UNK_A7, 0)


def drawn(t, vsyncs=20):
    n = t.ft()["prompt_drawn"]
    t.g.run(vsyncs)
    return t.ft()["prompt_drawn"] - n


def placed(t, check):
    """The prompt in the game picture's upper right (not the window's: a 4:3 picture in a 16:9 window has side
    bars), in the HUD labels' font, outlined, no plate; a bottom-left anchor moves it there; the settings'
    defaults."""
    g = t.g
    s = t.ft()
    st = s["settings"]
    check(st["prompt_anchor"] == 2 and abs(st["prompt_offset"][0] + 0.025) < 1e-6
          and abs(st["prompt_offset"][1] - 0.035) < 1e-6 and st["prompt_scale"] == 1,
          f"prompt: the defaults, the top right anchor and its offsets ({st})")
    check(s["prompt_font"] == "labels", f"prompt: drawn in the HUD labels' font ({s['prompt_font']})")
    check(s["prompt_backdrop"] is False and s["prompt_stroke"] > 0,
          f"prompt: no plate, outlined (backdrop {s['prompt_backdrop']}, stroke {s['prompt_stroke']})")

    def where():
        g.run(4)
        s = t.ft()
        x0, y0, x1, y1 = s["prompt_box"]
        px, py, pw, ph = s["picture"]
        return (x0, y0, x1, y1), (px, py, pw, ph)

    for wide in (False, True):
        g.call("graphics", widescreen=wide)
        (x0, y0, x1, y1), (px, py, pw, ph) = where()
        aspect = "16:9" if wide else "4:3"
        inside = px <= x0 < x1 <= px + pw and py <= y0 < y1 <= py + ph
        check(inside and x0 > px + pw * 0.5 and y1 < py + ph * 0.25,
              f"prompt ({aspect} picture): in its upper right ({[x0, y0, x1, y1]} in {[px, py, pw, ph]})")
        check(abs((px + pw * (1 - 0.025)) - x1) <= 1.5 and abs((py + ph * 0.035) - y0) <= 1.5,
              f"prompt ({aspect} picture): its top right corner at the anchor plus the offsets")
        check(y1 - y0 > ph * 0.03 and y1 - y0 < ph * 0.1, f"prompt ({aspect} picture): a hint's height ({y1 - y0:.0f} px)")
    g.call("graphics", widescreen=False)
    t.ft(set={"prompt_anchor": 6, "prompt_offset_x": 0.02, "prompt_offset_y": -0.02})
    (x0, y0, x1, y1), (px, py, pw, ph) = where()
    check(abs(x0 - (px + pw * 0.02)) <= 1.5 and abs(y1 - (py + ph * 0.98)) <= 1.5,
          f"prompt: anchored bottom left ({[x0, y0, x1, y1]} in {[px, py, pw, ph]})")
    t.ft(set={"prompt_anchor": 2, "prompt_offset_x": -0.025, "prompt_offset_y": 0.035})


def case_prompt(t, check, args):
    g = t.g
    s = t.ft()
    if s["why"]:
        skipped(check, args, f"prompt: travel not offered here ('{s['why']}', skipped)")
        return
    # the classic look (ui_menus off) keeps the game's own sprites and layout: no plate
    g.call("ui_menu", modern=False)
    g.call("button_prompts", button_prompts="auto")
    g.call("button_prompts", device="playstation", variant=4)
    check(t.ft()["prompt"] and drawn(t) == 0, "prompt: not drawn with the modern UI off (classic look)")
    g.call("ui_menu", modern=True)
    # the device used last decides the icon (the tests launch with the PS1 glyphs: original mode)
    check(t.ft()["prompt"] and drawn(t) > 0, "prompt: shown on the Circle with the modern UI (the pad's back)")
    t.shot("prompt")
    placed(t, check)
    t.ft(set={"enabled": False})
    g.run(4)  # the field poll decides the prompt (a frame or two)
    check(not t.ft()["prompt"] and drawn(t) == 0, "prompt: not with fast travel off")
    t.ft(set={"enabled": True, "prompt": False})
    check(drawn(t) == 0, "prompt: not with qol_fast_travel_prompt 0")
    t.ft(set={"prompt": True})
    g.call("button_prompts", device="keyboard")
    check(drawn(t) == 0, "prompt: not on the keyboard while Travel has no key")
    g.call("button_prompts", bind={"bind_key_travel": "t"})
    check(drawn(t) > 0, "prompt: on the keyboard once Travel has a key")
    t.set_flag(UNK_A7, 1)
    g.run(4)
    check(drawn(t) == 0, "prompt: not while travel is refused (unkA7)")
    t.set_flag(UNK_A7, 0)
    # original mode (the PS1's glyphs): Travel is no PS1 button, so the keyboard shows
    # its key (none bound: no prompt), not Select's PlayStation glyph
    g.call("button_prompts", button_prompts="original")
    g.call("button_prompts", bind={"bind_key_travel": "none"})
    g.call("button_prompts", device="keyboard")
    check(drawn(t) == 0, "prompt: original mode, the keyboard used last, no key for Travel: not shown")
    g.call("button_prompts", bind={"bind_key_travel": "t"})
    check(drawn(t) > 0, "prompt: original mode, the keyboard used last, Travel on T: shown")
    g.call("button_prompts", device="playstation", variant=4)
    check(drawn(t) > 0, "prompt: original mode, the pad used last: shown")
    g.call("ui_menu", modern=t.menus == "on")


def case_settings(t, check, args):
    s = t.ft()
    check(s["settings"]["cost"] == "flat" and s["settings"]["flat"] == 7,
          f"settings: read from ROOD_SETTINGS ({s['settings']})")
    other = nearest_other(s)
    if other is not None:
        check(s["points"][other]["cost"] == 7, "settings: flat 7 MP per trip")
    t.ft(set={"cost": "scaled", "scale": 0.25})
    r = t.g.call("settings", action="save")
    text = pathlib.Path(r["path"]).read_text()
    check("qol_fast_travel_cost 2\n" in text and "qol_fast_travel_scale 0.25\n" in text,
          "settings: saved back (qol_fast_travel_cost 2, qol_fast_travel_scale 0.25)")
    # a settings file from before the anchor (the user's own has the old centre, 0.5 / 0.84): its window
    # fractions are dropped, the prompt gets the anchor's default, and a save writes only the new keys
    st = s["settings"]
    check(st["prompt_anchor"] == 2 and abs(st["prompt_offset"][0] + 0.025) < 1e-6
          and abs(st["prompt_offset"][1] - 0.035) < 1e-6,
          f"settings: the old qol_fast_travel_prompt_x / _y migrate to the top right default ({st})")
    check("qol_fast_travel_prompt_x" not in text and "qol_fast_travel_prompt_y" not in text
          and "qol_fast_travel_prompt_anchor 2\n" in text and "qol_fast_travel_prompt_offset_x -0.025\n" in text,
          "settings: saved with qol_fast_travel_prompt_anchor / _offset_x / _y, without the old keys")


def settings_env(out):
    path = out / "settings-fast-travel.cfg"
    path.write_text("qol_fast_travel_cost 1\nqol_fast_travel_flat 7\n"
                    "qol_fast_travel_prompt_x 0.5\nqol_fast_travel_prompt_y 0.84\n")
    return {"ROOD_SETTINGS": str(path)}


def wide_window_env(out):
    """A 16:9 window: the 4:3 picture has side bars (the prompt goes by the picture, not the window)."""
    return {"ROOD_WINDOW": "1280x720"}


CASE_ENV = {"settings": settings_env, "prompt": wide_window_env}


SHARED = ("status", "audit")
CASES = {"status": case_status, "audit": case_audit, "warp": case_warp, "reload": case_reload, "menu": case_menu, "save": case_save, "refuse": case_refuse, "game": case_game, "here": case_here, "button": case_button, "prompt": case_prompt, "settings": case_settings}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(cf.default_save()))
    parser.add_argument("--menus", choices=("off", "on"), default="off")
    parser.add_argument("--cases", default=",".join(CASES))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    args.pinned = pathlib.Path(args.card).resolve() == pathlib.Path(cf.default_save()).resolve()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / f"fast_travel_{args.menus}").resolve()
    out.mkdir(parents=True, exist_ok=True)
    os.environ.pop("ROOD_SETTINGS", None)  # the defaults
    wanted = [c for c in args.cases.split(",") if c in CASES]
    failures = []

    def check(ok, message):
        print(("ok   " if ok else "FAIL ") + message)
        if not ok:
            failures.append(message)

    with cf.card_guard(out / "cards", source=args.card) as cards:
        lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
        env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
        first = Trip("first", out, cards.card1, args.menus, env, bool(args.out))
        try:
            st = first.ft()["state"]
            if not st["circle"] or st["intro"] < 3:
                print(f"SKIP: the card's save room is not a Circle of Magic after the prologue "
                      f"(circle {st['circle']}, introState {st['intro']})")
                return 77
            for name in wanted:
                if name in SHARED:
                    CASES[name](first, check, args)
        finally:
            first.close()
        for name in wanted:
            if name in SHARED:
                continue
            card = out / f"{name}-card1.mcd"
            shutil.copyfile(cards.card1, card)
            t = Trip(name, out, card, args.menus, dict(env, **CASE_ENV.get(name, lambda o: {})(out)), bool(args.out))
            try:
                CASES[name](t, check, args)
            except Exception as e:  # (a helper's own timeout too: the other cases still run)
                check(False, f"{name}: {type(e).__name__}: {e}")
            finally:
                t.close()
    print(f"{len(failures)} failure(s)" if failures else "PASS: fast travel")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
