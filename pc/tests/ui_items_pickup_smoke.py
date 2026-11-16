#!/usr/bin/env python3
"""Modern menus, the Items tab after pickups (pc/src/ui/items_bridge.c,
menu_bridge.c's MAINMENU wait): every row resolves, nothing stays on
"Loading...".

    python3 pc/tests/ui_items_pickup_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

A player reported the modern Items screen stuck after a few pickups (around
Undercity West): misc rows' sub line "Loading..." in gold (MAINMENU.PRG not
resident: items_bridge.c refuses Use, Discard, Sort and Move meanwhile), every
item locked until a save was reloaded. The rows say "Loading..." while the
shell's top level runs without MAINMENU (BATTLE vs_menu_frameworkUpdateMenuOverlay ticks it while
vs_battle_loadMenuPrg(0) waits for its CD queue slot), which normally lasts
the 2 or 3 frames of the load.

Needs a card whose first save is in the save room (skipped otherwise). One
game process, ui_menus on. Each check opens the Items tab, visits every
category and reads RPC ui_menu after 60 vsyncs: no row's use_reason is
"Loading...", MAINMENU resident and the longest wait for it short; the CD
queue is clean at rest (every slot free, nothing counted queued or reading).
  pickups  loot lists taken through the game's loot menu (RPC loot: the
           drops list and vs_menu_frameworkOpenLootListMenu, as after a battle; the modern Loot
           screen, Square takes all): misc items, a copy of a bag armor and a
           gem, several rounds; then the save room's own chest (RPC chest:
           the field's chest flow), taken
  full     the misc bag full (64 records): a drop that does not fit, the Loot
           screen's Organize > Organize inventory, a bag row discarded, back,
           taken; several rounds
  stall    the Items tab open, MAINMENU's load made to wait on a CD slot that
           nothing will load (vs_battle_menuState: loading 1, its slot a free
           queue slot): the shell starts the load over (RPC ui_menu mainmenu
           restarts + 1, the "[ui] MAINMENU's load lost its CD slot" log line)
           and the rows resolve within MAINMENU_CHECK ticks and the load
The port has no bag capacity option: the full bag is the game's own.
--out keeps the logs."""
import argparse
import os
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import card_fixtures as cf  # noqa: E402
from ui_loot_smoke import Loot, CURE_ROOT, CURE_BULB, MISC  # noqa: E402

CLOSED = 0x3F
CATEGORIES = ["weapons", "blades", "grips", "shields", "armor", "gems", "misc"]
MISC_IDX = 112        # vs_main_inventoryIndices: the misc list (64 entries)
SLOT = 24             # sizeof vs_main_CdQueueSlot
LOADING = "Loading"
SETTLE = 60           # vsyncs a check waits after opening a category
LONGEST = 10          # the longest wait for MAINMENU a menu open may take (ticks; 2 or 3 seen)


class Session:
    def __init__(self, out, card, env):
        self.g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
        self.sym = symbols()
        self.log = out / "game.log"
        self.g.call("ui_menu", modern=True)
        self.g.run(2)

    def close_game(self):
        self.g.close()

    def u8(self, name):
        return self.g.read(self.sym[name], 1)[0]

    def menu(self):
        return self.u8("vs_battle_menuState")

    def ui(self):
        return self.g.call("ui_menu")

    def lt(self):
        return self.ui().get("loot") or {}

    def wait(self, pred, limit, step=2):
        for _ in range(0, limit, step):
            if pred():
                return True
            self.g.run(step)
        return pred()

    def press(self, button, settle=6):
        self.g.press(button, vsyncs=3, settle=settle)

    def queue(self):
        """The CD queue's slots in use (state, priority, lba) and its counts (processing, queued)."""
        raw = self.g.read(self.sym["_cdQueue"], 32 * SLOT)
        used = [struct.unpack_from("<hhi", raw, i * SLOT) for i in range(32) if struct.unpack_from("<h", raw, i * SLOT)[0]]
        return used, struct.unpack("<hh", self.g.read(self.sym["_cdQueueCount"], 4))

    def shell_items(self):
        for _ in range(6):
            if self.ui().get("visible") and self.menu() == 0:
                break
            self.press("triangle", settle=4)
            self.wait(lambda: self.ui().get("visible") and self.menu() == 0, 120)
            self.g.run(6)
        for _ in range(8):
            if self.ui().get("tab") == "items":
                return
            self.press("r1", settle=4)
        raise TimeoutError(f"the Items tab (tab {self.ui().get('tab')})")

    def close(self):
        for _ in range(8):
            if self.menu() == CLOSED and not self.ui().get("visible"):
                break
            self.press("triangle", settle=4)
            self.wait(lambda: self.menu() == CLOSED and not self.ui().get("visible"), 200, 4)
        self.g.run(30)

    def items_check(self, check, what):
        """Every category of the Items tab: no row on Loading..., MAINMENU resident."""
        self.shell_items()
        stuck, seen = [], 0
        for _ in range(len(CATEGORIES)):
            self.g.run(SETTLE)
            it = self.ui().get("items") or {}
            rows = it.get("list", [])
            seen += len(rows)
            stuck += [f"{it.get('category')}:{r['name']}" for r in rows if LOADING in r.get("use_reason", "")]
            self.press("r2", settle=4)
        mm = self.ui().get("mainmenu", {})
        check(not stuck and seen > 0 and mm.get("resident"),
              f"{what}: {seen} rows over the categories, none on Loading... ({stuck[:4]}), MAINMENU resident ({mm})")
        self.close()
        used, counts = self.queue()
        check(not used and counts == (0, 0), f"{what}: the CD queue clean at rest (slots {used}, counts {counts})")
        return mm

    def take_loot(self, loot, what):
        r = self.g.call("loot", hex=loot.hex(), chest=False)
        if not r.get("opened"):
            raise TimeoutError(f"{what}: the loot menu did not open")
        self.wait(lambda: self.lt().get("valid"), 600, 4)
        self.g.run(40)
        self.press("square", settle=10)
        self.finish_loot()

    def finish_loot(self):
        """Ends a loot session (Cross, Yes on anything left), the field free again."""
        for _ in range(4):
            if self.wait(lambda: self.menu() == CLOSED, 120, 4):
                break
            self.press("cross", settle=10)
            if self.lt().get("mode") == "confirm":
                self.press("up", settle=4)
                self.press("circle", settle=10)
        self.wait(lambda: self.menu() == CLOSED and self.u8("D_800F196C") == 1, 900, 4)
        self.g.run(60)


def case_pickups(s, check):
    inv = s.g.read(s.sym["vs_main_inventory"], 0xF00)
    s.items_check(check, "pickups: before")
    misc = [(CURE_ROOT, 1), (CURE_BULB, 2), (325, 1)]
    for k in range(4):
        loot = Loot().misc(*misc[k % 3:] + misc[:k % 3])
        if k % 2:
            loot.armor(inv, 0).gem()
        s.take_loot(loot, f"pickups {k}")
        s.items_check(check, f"pickups: after loot {k}")
    r = s.g.call("chest", open=0)
    if r.get("opened"):
        s.wait(lambda: s.menu() == 0xC, 600, 4)
        s.wait(lambda: s.lt().get("valid"), 600, 4)
        s.g.run(40)
        s.press("square", settle=10)
        s.finish_loot()
        s.items_check(check, "pickups: after the save room's chest")
    else:
        check(False, f"pickups: the save room's chest did not open ({r})")


def case_full(s, check):
    inv, idx = s.sym["vs_main_inventory"], s.sym["vs_main_inventoryIndices"]
    for k in range(3):
        records = b"".join(struct.pack("<HBB", 330 + (i % 40), 1, i + 1) for i in range(64))
        s.g.write(inv + MISC, records)
        s.g.write(idx + MISC_IDX, bytes(range(1, 65)))
        s.g.run(4)
        r = s.g.call("loot", hex=Loot().misc((CURE_ROOT, 1)).hex(), chest=False)
        if not r.get("opened"):
            raise TimeoutError(f"full {k}: the loot menu did not open")
        s.wait(lambda: s.lt().get("valid"), 600, 4)
        s.g.run(40)
        s.press("circle", settle=6)  # the row does not fit: Organize
        s.wait(lambda: s.lt().get("mode") == "organize", 120)
        entries = s.lt().get("entries") or []
        if entries and entries[s.lt().get("menu", 0)] != "organize":
            s.press("down", settle=4)
        s.press("circle", settle=4)
        s.wait(lambda: s.lt().get("mode") == "bag", 120)
        for _ in range(8):
            if s.lt().get("bag_panel", {}).get("category") == "misc":
                break
            s.press("right", settle=4)
        s.press("circle", settle=4)
        s.wait(lambda: s.lt().get("mode") == "bag_actions", 120)
        ids = [a["id"] for a in s.lt().get("actions", [])]
        for _ in range(len(ids) + 1):
            if "discard" not in ids or s.lt().get("menu") == ids.index("discard"):
                break
            s.press("down", settle=4)
        s.press("circle", settle=4)
        s.wait(lambda: s.lt().get("mode") == "bag_confirm", 120)
        s.press("up", settle=4)
        s.press("circle", settle=4)
        s.wait(lambda: s.lt().get("mode") == "bag", 120)
        s.press("cross", settle=4)
        s.wait(lambda: s.lt().get("mode") == "list", 120)
        s.press("square", settle=10)
        s.finish_loot()
        s.items_check(check, f"full: after an organized pickup {k}")


def case_stall(s, check):
    before = s.items_check(check, "stall: before")
    s.shell_items()
    s.g.run(SETTLE)
    raw = s.g.read(s.sym["_cdQueue"], 32 * SLOT)
    free = next(i for i in range(32) if struct.unpack_from("<h", raw, i * SLOT)[0] == 0)
    state = s.sym["vs_battle_menuState"]
    at = s.g.layout("menuState_cdQueue", 4)  # the slot of the last menu load (a host pointer on 64 bits)
    word = "<Q" if at == 8 else "<I"
    last = struct.unpack(word, s.g.read(state + at, struct.calcsize(word)))[0]
    check(0 <= last - s.sym["_cdQueue"] < 32 * SLOT, f"stall: the menu's slot pointer is a queue slot ({last:#x})")
    # loading 1, its slot a free one: nothing will ever load it
    s.g.write(state + 3, b"\x01")
    s.g.write(state + at, struct.pack(word, s.sym["_cdQueue"] + free * SLOT))
    s.g.run(2)
    stalled = s.ui().get("mainmenu", {})
    check(not stalled.get("resident"), f"stall: MAINMENU waits for the slot ({stalled})")
    ok = s.wait(lambda: s.ui().get("mainmenu", {}).get("resident"), 120)
    after = s.ui().get("mainmenu", {})
    check(ok and after.get("restarts", 0) == before.get("restarts", 0) + 1,
          f"stall: the load started over once and MAINMENU is resident again ({after})")
    s.g.run(SETTLE)
    rows = (s.ui().get("items") or {}).get("list", [])
    check(rows and not any(LOADING in r.get("use_reason", "") for r in rows),
          f"stall: the rows resolve ({[r.get('use_reason') for r in rows][:4]})")
    s.close()
    text = s.log.read_text(errors="replace")
    check("MAINMENU's load lost its CD slot" in text, "stall: logged")
    s.items_check(check, "stall: after")


CASES = {"pickups": case_pickups, "full": case_full, "stall": case_stall}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(cf.default_save()))
    parser.add_argument("--cases", default=",".join(CASES))
    parser.add_argument("--out", help="keep the logs here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "ui_items_pickup").resolve()
    out.mkdir(parents=True, exist_ok=True)
    os.environ.pop("ROOD_SETTINGS", None)
    failures = []

    def check(ok, message):
        print(("ok   " if ok else "FAIL ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    with cf.card_guard(out / "cards", source=args.card) as cards:
        env = {"ROOD_WINDOW": "960x720", "ROOD_GI": "0", "ROOD_LIGHTS": str(out / "lights")}
        s = Session(out, cards.card1, env)
        try:
            st = s.g.state()
            if (st["zone"], st["room"]) != (9, 17):
                print(f"SKIP: the card's first save is not in the save room ({st['zone']}, {st['room']})")
                return 77
            for name in args.cases.split(","):
                try:
                    CASES[name](s, check)
                except Exception as e:  # (a helper's timeout: the other cases still run)
                    check(False, f"{name}: {type(e).__name__}: {e}")
                    s.close()
        finally:
            s.close_game()
    print(f"{len(failures)} failure(s)" if failures else "PASS: Items after pickups")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
