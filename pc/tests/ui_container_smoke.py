#!/usr/bin/env python3
"""Modern menus, the native Container screen (pc/src/ui/container_bridge.c,
container_ops.c, ui_container.cpp, ui_container_draw.cpp; MENU7 hooks in
src/MENU/MENU7.PRG/260.c): the save room's container. MENU7's
vs_menu7_saveContainerMenu runs framed (its prompts and card wait answered
by the player through the native screen, its save list the Data screen's
model in container mode); MENUD's vs_menuD_exec is ported natively.

    python3 pc/tests/ui_container_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--sets 1,2,...]

Card safety: everything runs inside card_fixtures.card_guard (the player's
cards hashed before and after; ROOD_CARD1 / ROOD_CARD2 temp copies set
before any launch); every process gets its own copy of the fixture under
OUT/ui_container. Skipped (77) without a card.

Fixtures (card_fixtures.py): the card's own save with a container built
from its own bag (read from RAM after a load: a weapon with blade, grip and
two gems, a shield with a gem, an armor, two loose gems, two stacks of 95
and 70 of an item the bag holds; container_fixture, layouts from
container_ops_probe --layout), and container_none (its header's generation
one ahead: MENU7 finds no save of this journey).

Processes: A the original (ui_menus off, MENUD by pad), B the native screen
by pad, C the native screen by the mouse (real SDL events through the system
RPC, 960x720: layout units are pixels). The container is opened through RPC
container (the save room has no container object). Each set compares:
  S1  at MENUD's exit (currentMenu 14 -> 7): the bag and its lists, the
      storage (unk0, unkF00, unkFB0, unk87B0, itemsToTransfer, unkC430,
      unk105B0), containerEmpty, returnState / currentMenu,
      vs_menuD_containerData and the six vs_mainMenu_* pointers: A = B = C;
      the cursor memories (D_800F4EE8): A = B; unk4BB0 reported (a scratch
      buffer MENUD always rewrites before reading it: the waiver, checked
      against 234.c's source below)
  S2  after a save (KEY and the play time pinned at the confirm): the card
      images byte-identical, one write, only file 1's blocks changed, the
      decoded container block equal to S1's unkC430.data packed
  S3  at the close: currentMenu 0x3F, D_800EB9B0 0, the background fade 0,
      the heap's free total equal across the processes (A = B = C; printed
      next to its value at the open, not checked against it), the HUD flag
      and vs_mainMenu_hideMenu as before, no session (container null)

Sets (P1: the container fixture, in this order in one process each):
  3 sort weapons (material), gems (and their details, L1 / R1), misc by
    quantity; discard 13 of the 95 stack (the amount picker); Triangle:
    reverted, no write
  6 the second pick: the 95 and 70 stacks merge (100 + 65), rows 1 and 2 (two
    items) swap;
    a staged / unstaged round trip on tab 7; Triangle
  4 the misc tab's first stack removed and committed, Cross at the top: the
    cancel warning, No reverts the bag; again: Yes, then Cross at the save
    list's card rows: the warning again; No reverts (no write)
  Each operation is checked to have happened (every process, the original
  too: unkC430's misc counts and order, the gems' order, itemsToTransfer,
  the bag), so a script that drifts cannot pass as A = B.
  1 Remove the weapon assembly and the 95 stack (onto the bag's stack), End,
    commit, End: save over file 1 (S2)
  2 Store the weapon again (with its gems); the equipped weapon refused;
    End, save (S2)
P2 (container_none): 5 the card wait (emptyCard), containerWarn No (no
    write); again: Yes: an empty container (settings bit 0x10), Triangle
P3: 7 a soft reset mid-transfer (B, C): nothing left over the title,
    Continue; in the save list the reset is off (MENU7's, as the original);
    the container opens and closes again
Presentation (B, C): vs_mainMenu_hideMenu set while latched, MENU7's gated
calls climbing, MENUD's own menu items never set up, the HUD held out, the
backdrop following ui_menu_opacity, 16:9 and 4:3 screenshots (--out only).
C runs sets 4, 1 and 7.

--out keeps screenshots there (game imagery: keep it out of git)."""
import argparse
import os
import subprocess
import pathlib
import re
import shutil
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, BUILD, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import card_fixtures as cf  # noqa: E402
from ui_data_smoke import menu7_symbols, changed_ranges, file_bytes_only, KEY, GAMETIME, ELEMENT  # noqa: E402

PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4
CLOSED = 0x3F
ITEM = 64  # sizeof(vs_battle_menuItem_t)
# vs_menu_inventoryStorage_t's members (their names are their offsets)
STORAGE = {"unk0": (0, 0xF00), "unkF00": (0xF00, 0xB0), "unkFB0": (0xFB0, 0x3C00), "unk87B0": (0x87B0, 0x3C00),
           "itemsToTransfer": (0xC3B0, 0x80), "unkC430": (0xC430, 0x3C00 + 0x580), "unk105B0": (0x105B0, 0x3C00 + 0x580)}
UNK4BB0 = (0x4BB0, 0x3C00)
SETTLE = 30  # vsyncs after a click (the frame's pointer, the original's slides)
HOLD = 6  # vsyncs a press is held: the menus run at 15 to 30 frames a second, a press must span a pad read


LAYOUT = {}  # container_ops_probe --layout (run() fills it)


class Timeout(Exception):
    pass


_overlay = {}


def overlay_symbols():
    """MENUD's and MAINMENU's input states (overlay statics: the objdump of the build, a name's
    function-local suffix dropped): the original's (A) presses go in when its menu takes input."""
    if _overlay:
        return _overlay
    want = {"rood_ovl_menu_menud_prg": ("D_80109A70", "D_80109A68", "D_80109A69", "state", "D_80109A5C",
                                        "D_80109A47"),
            "rood_ovl_menu_mainmenu_prg": ("vs_mainMenu_actionMenuState", "vs_mainMenu_itemsListRow", "_itemsText",
                                           "_sortItemsTextBuf")}
    image = BUILD / "rood_battle_probe"
    out = subprocess.run([os.environ.get("ROOD_OBJDUMP", "objdump"), "-t", str(image)], capture_output=True,
                         text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 6:
            continue
        name = parts[-1].split(".")[0]
        for section, names in want.items():
            if parts[-3].startswith(section) and name in names:
                _overlay.setdefault(name, int(parts[0], 16))
    missing = [n for names in want.values() for n in names if n not in _overlay]
    if missing:
        raise RuntimeError(f"overlay statics not found: {missing}")
    return _overlay


class Session:
    def __init__(self, name, out, card, mode, shots=False, extra=None):
        self.name, self.out, self.mode, self.shots = name, out, mode, shots
        self.card1 = out / f"{name}-card1.mcd"
        shutil.copyfile(card, self.card1)
        real = {p.resolve() for p in cf.player_cards()}
        assert self.card1.resolve() not in real
        lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
        self.settings = out / f"{name}-settings.cfg"
        self.settings.write_text("")
        env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_DETERMINISTIC": "1",
               "ROOD_SETTINGS": str(self.settings), **(extra or {})}
        self.g = launch_at("save_room", card=self.card1, fast=True, log=out / f"{name}.log", extra_env=env)
        self.g.call("graphics", interpolate=False)
        self.g.call("ui_menu", modern=mode != "A")
        self.sym = symbols()
        self.m7 = menu7_symbols()
        self.shot_n = 0
        # the game's cursor memory option off (every process alike): menus and the transfer's tabs start
        # where the scripts expect (the cursor memories are still written, and compared)
        self.g.write(self.sym["vs_main_settings"] + 1, b"\0")

    def close(self):
        self.g.close()

    # ---- reads ----
    def ui(self):
        return self.g.call("ui_menu")

    def ctr(self):
        return self.ui().get("container")

    def data(self):
        return self.ui().get("data")

    def u8(self, name, offset=0):
        return self.g.read(self.sym[name] + offset, 1)[0]

    def u32(self, address):
        return int.from_bytes(self.g.read(address, 4), "little")

    def ptr(self, name):
        return int.from_bytes(self.g.read(self.sym[name], PTR), "little")

    def menu(self):
        return self.u8("vs_battle_menuState") & 0x3F

    def m7_state(self):
        return self.u8("vs_battle_submenuStates", 7)

    def md_state(self):
        return self.u8("vs_battle_submenuStates", 14)

    def cursor(self):
        v = struct.unpack("<I", self.g.read(self.m7["_selectCursorXy"], 4))[0]
        return v & 0xFFFF, v >> 16, v

    def file_cursor(self):
        x, y, v = self.cursor()
        if not v or x != 24:
            return -1
        page = (72 - struct.unpack("<h", self.g.read(self.m7["_fileMenuElements"] + 5 * ELEMENT + 14, 2))[0]) // 40
        return (y - 62) // 40 + page

    def writes(self):
        return self.g.call("card")["writes"]

    def heap_free(self):
        head = self.sym["heapA"]
        node, total = self.u32(head + 4), 0
        for _ in range(4096):
            if node in (head, 0):
                break
            total += self.u32(node + 8)
            node = self.u32(node + 4)
        return total

    def item_state(self, i):
        items = self.ptr("vs_battle_menuItems")
        return self.g.read(items + i * ITEM, 1)[0] if items else 0

    def shot(self, what):
        if self.shots:
            self.g.present_screenshot(self.out / f"{self.name}-{self.shot_n:02d}-{what}.png")
            self.shot_n += 1

    # ---- the operations' own effects (the source container unkC430 while MENUD runs) ----
    def source(self, kind, field, fmt="<B"):
        """unkC430's records of `kind` in display order (its index list): field `field` of each."""
        L, st = LAYOUT, self.ptr("vs_menu_inventoryStorage")
        size = L["container"]
        data, idx = self.g.read(st + 0xC430, size), self.g.read(st + 0xC430 + size, 0x580)
        at = sum(cf.CAPS[k] for k in cf.INDEX_ORDER[:cf.INDEX_ORDER.index(kind)]) * 2
        out = []
        for v in struct.unpack_from(f"<{cf.CAPS[kind]}H", idx, at):
            if v:
                r = data[L["arrays"][kind] + (v - 1) * L["sizes"][kind]:L["arrays"][kind] + v * L["sizes"][kind]]
                out.append(struct.unpack_from(fmt, r, L["fields"][field])[0])
        return out

    def misc(self):
        """(id, count) of unkC430's misc stacks in display order, once the menu takes input again."""
        self.settle()
        return list(zip(self.source("misc", "misc_id", "<H"), self.source("misc", "misc_count")))

    def staged(self):
        self.settle()
        return self.g.read(self.ptr("vs_menu_inventoryStorage") + 0xC3B0, 0x80) != bytes(0x80)

    # ---- the snapshots ----
    def s1(self):
        st = self.ptr("vs_menu_inventoryStorage")
        g = self.g
        d = {k: g.read(st + o, n) for k, (o, n) in STORAGE.items()}
        d["inventory"] = g.read(self.sym["vs_main_inventory"], 0xF00)
        d["indices"] = g.read(self.sym["vs_main_inventoryIndices"], 0xB0)
        d["containerEmpty"] = bytes([self.u8("vs_main_settings") & 0x10])
        d["menuState"] = g.read(self.sym["vs_battle_menuState"], 2)
        d["vs_menuD_containerData"] = g.read(self.sym["vs_menuD_containerData"], PTR)
        d["pointers"] = b"".join(g.read(self.sym[n], PTR) for n in (
            "vs_mainMenu_weapons", "vs_mainMenu_blades", "vs_mainMenu_grips", "vs_mainMenu_shields",
            "vs_mainMenu_armor", "vs_mainMenu_gems"))
        cursor = g.read(self.sym["D_800F4EE8"], 0xF2)
        scratch = g.read(st + UNK4BB0[0], UNK4BB0[1])
        return d, cursor, scratch

    def s3(self):
        return {"menu": self.u8("vs_battle_menuState"), "D_800EB9B0": self.u32(self.sym["D_800EB9B0"]),
                "fade": struct.unpack("<i", self.g.read(self.sym["vs_mainMenu_backgroundFadeStep"], 4))[0],
                "heap": self.heap_free(), "hud": self.u8("D_800EB9AF"), "hideMenu": self.u8("vs_mainMenu_hideMenu"),
                "container": self.ctr()}

    # ---- waits and presses ----
    def wait(self, pred, what, limit=1200, step=2):
        for _ in range(limit // step):
            if pred():
                return
            self.g.run(step)
        raise Timeout(f"{self.name}: {what} (menu {self.menu():#x}, MENU7 {self.m7_state()}, MENUD {self.md_state()}, "
                      f"{(self.ctr() or {}).get('phase')})")

    def items_ready(self):
        """MAINMENU's vs_mainmenu_ready: no menu item animating (state > 1)."""
        items = self.ptr("vs_battle_menuItems")
        if not items:
            return True
        raw = self.g.read(items, 40 * ITEM)
        return all(raw[i * ITEM] <= 1 for i in range(40))

    def a_ready(self):
        """The original takes input (A): MENUD's top menu at its state 3, func_80107490's list (2, 6: the
        row list at its input row 17, or the empty line), action / sort / Yes-No menus (9, 12, 15, 11) at
        theirs, the amount picker (11) and MENU7 once nothing slides."""
        o = overlay_symbols()
        rd = lambda name: self.g.read(o[name], 1)[0]  # noqa: E731
        ptr = lambda name: int.from_bytes(self.g.read(o[name], PTR), "little")  # noqa: E731
        # MAINMENU's row list / action list take input: their text up (getSelectedRow / actionInputProcessed
        # -1) and at their input row
        rows_in = lambda: ptr("_itemsText") != 0 and rd("vs_mainMenu_itemsListRow") == 17  # noqa: E731
        acts_in = lambda: ptr("_sortItemsTextBuf") != 0 and rd("vs_mainMenu_actionMenuState") == 17  # noqa: E731
        if self.menu() == 14:
            md = self.md_state()
            if md == 3:
                return rd("D_80109A70") == 3
            if md == 5:
                st = rd("D_80109A68")
                if st == 2:
                    return (rd("D_80109A69") != 0 and self.items_ready()) or rows_in()
                if st == 6:
                    return rows_in()
                if st == 15:  # func_801072B0's Yes / No (its state 1; 2 waits for the menu after the answer)
                    return rd("D_80109A5C") == 1 and acts_in()
                if st in (9, 12):
                    return acts_in()
                if st == 11:  # _discardMenu: its Yes / No (3, 7) at the action list's input row, the amount (5)
                    ds = rd("state")
                    return (ds in (3, 7) and acts_in()) or ds == 5
                if st == 7:  # the details (gems: _gemNavigation's handleInput, 2)
                    return rd("D_80109A47") == 2
                return False
            return False
        return self.items_ready()

    def settle(self, quiet=4, limit=900):
        """The original (A): a press goes in once its menu takes input (a_ready) for `quiet` vsyncs."""
        if self.mode != "A":
            self.g.run(6)
            return
        calm = 0
        for _ in range(limit):
            calm = calm + 1 if self.a_ready() else 0
            if calm >= quiet:
                return
            self.g.run(1)
        raise Timeout(f"{self.name}: the original never took input (MENUD {self.md_state()})")

    def press(self, button, settle=None):
        self.settle()
        self.g.press(button, vsyncs=HOLD, settle=4)
        if os.environ.get("ROOD_CTR_TRACE"):  # debugging a script: the states after each press
            o = overlay_symbols()
            st = self.ptr("vs_menu_inventoryStorage")
            print(f"trace {self.name} {button}: menu {self.menu()} MENUD {self.md_state()} "
                  f"80107490 {self.g.read(o['D_80109A68'], 1)[0]} top {self.u8('D_800F4EE8', 0x9E)} "
                  f"tab {(self.u8('D_800F4EE8', 0xA0) - 1) & 7} staged "
                  f"{self.g.read(st + 0xC3B0, 12).hex() if st else '-'}", flush=True)

    def phase(self):
        return (self.ctr() or {}).get("phase")

    # ---- the pointer: real SDL events (960x720: layout units are window pixels) ----
    def target(self, kind, index, block="container"):
        for h in reversed((self.ui().get(block) or {}).get("hits", [])):
            if h["k"] == kind and h["i"] == index:
                r = h["r"]
                return [(r[0] + r[2]) / 2, (r[1] + r[3]) / 2]
        return None

    def click(self, kind, index, block="container", run=SETTLE, button=1):
        xy = self.target(kind, index, block)
        if xy is None:
            raise Timeout(f"{self.name}: no {kind} {index} drawn ({block})")
        self.g.call("system", mouse=xy, button=button, down=True)
        self.g.run(1)
        self.g.call("system", mouse=xy, button=button, down=False)
        self.g.run(run)

    def right_click(self, run=SETTLE):
        self.g.call("system", mouse=[480, 360], button=3, down=True)
        self.g.run(1)
        self.g.call("system", mouse=[480, 360], button=3, down=False)
        self.g.run(run)

    # ---- the flow ----
    def open(self):
        """The container opened as the field opens it; MENU7's first prompt up."""
        self.heap0, self.hud0, self.hide0 = self.heap_free(), self.u8("D_800EB9AF"), self.u8("vs_mainMenu_hideMenu")
        r = self.g.call("container")
        if not r.get("opened"):
            raise Timeout(f"{self.name}: the container did not open ({r})")
        self.prompt()

    def prompt(self):
        """MENU7 waits at a Yes / No (its handleInput: native, the prompt phase; original: 40 vsyncs)."""
        if self.mode == "A":
            self.wait(lambda: self.m7_state() in (1, 5, 12), "a MENU7 prompt")
            self.g.run(40)
        else:
            self.wait(lambda: (self.phase() or "").startswith("prompt"), "the prompt phase")

    def answer(self, yes):
        if self.mode == "C":
            self.click("yes" if yes else "no", 0)
            return
        c = self.ctr() if self.mode == "B" else None
        lit_yes = c["prompt_yes"] if c else self.item_selected(0x1E)
        if yes:
            if not lit_yes:
                self.press("up", settle=6)
            self.press("circle")
        else:
            self.press("cross")

    def item_selected(self, i):
        items = self.ptr("vs_battle_menuItems")
        return self.g.read(items + i * ITEM + 6, 1)[0] if items else 0  # selected

    def to_top(self):
        """MENUD's top menu takes input."""
        if self.mode == "A":
            self.wait(lambda: self.menu() == 14 and self.md_state() == 3, "MENUD's top menu", limit=2400)
            self.g.run(30)
        else:
            # the session's first ticks drop input (the press that answered MENU7's Yes)
            self.wait(lambda: (self.ctr() or {}).get("session") == 1 and not self.ctr()["settling"],
                      "the native session", limit=2400)
            self.g.run(4)

    def dismiss_wait(self):
        """MENU7's card wait (state 4): any button."""
        if self.mode == "A":
            self.wait(lambda: self.m7_state() == 4, "MENU7's card wait")
            self.g.run(20)
            self.press("circle")
        else:
            self.wait(lambda: self.phase() == "wait", "the wait phase")
            if self.mode == "C":
                self.click("status", 0)
            else:
                self.press("circle")

    def exit_s1(self, button):
        """The press that leaves MENUD, then each vsync until currentMenu is MENU7's again: S1."""
        self.settle()
        self.g.call("input", buttons=[button], vsyncs=HOLD)
        for _ in range(600):
            if self.menu() == 7:
                return self.s1()
            self.g.run(1)
        raise Timeout(f"{self.name}: MENUD's exit")

    def mouse_exit_s1(self, kind, index):
        """The click that leaves MENUD (kind None: a right click, Cross), then each vsync: S1."""
        xy = self.target(kind, index) if kind else [480, 360]
        if xy is None:
            raise Timeout(f"{self.name}: no {kind} {index} drawn")
        button = 1 if kind else 3
        self.g.call("system", mouse=xy, button=button, down=True)
        self.g.call("system", mouse=xy, button=button, down=False)
        for _ in range(600):
            if self.menu() == 7:
                return self.s1()
            self.g.run(1)
        raise Timeout(f"{self.name}: MENUD's exit (mouse)")

    def top_to(self, i):
        """MENUD's top menu cursor (D_800F4EE8.unk9E, the same RAM in every process) to row i by Down."""
        for _ in range(3):
            if self.u8("D_800F4EE8", 0x9E) == i:
                return
            self.press("down")
        raise Timeout(f"{self.name}: the top menu's row {i}")

    def closed(self, limit=1200):
        self.wait(lambda: self.menu() == CLOSED, "the menu closed", limit=limit)
        self.g.run(20)

    # ---- the save list (MENU7 _showSaveMenu(2): A original, B / C the Data screen in container mode) ----
    def card_rows(self, n=1):
        self.wait(lambda: self.cursor()[0] == 180 and self.cursor()[2] != 0, "the card rows")
        if self.mode == "C":
            self.g.run(10)
            self.click("card", n, block="data")
            return
        if (self.cursor()[1] - 26) // 16 != n:
            self.press("down", settle=6)
        self.press("circle", settle=6)

    def file_rows(self, i):
        self.wait(lambda: self.cursor()[0] == 24 and self.cursor()[2] != 0, "the file list")
        if self.mode == "C":
            return
        for _ in range(8):
            f = self.file_cursor()
            if f == i:
                return
            self.press("down" if f < i else "up", settle=6)
        raise Timeout(f"{self.name}: file {i}")

    def save_over(self, i):
        """File i chosen, the overwrite prompt's Yes with KEY and the play time pinned at the confirm."""
        w0 = self.writes()
        if self.mode == "C":
            self.g.run(10)
            self.click("file", i, block="data", run=10)
            self.wait(lambda: (self.data() or {}).get("phase") == "prompt", "the overwrite prompt")
            xy = self.target("yes", 0, block="data")
            trigger = lambda: (self.g.call("system", mouse=xy, button=1, down=True),  # noqa: E731
                               self.g.call("system", mouse=xy, button=1, down=False))
        else:
            self.press("circle", settle=10)
            self.wait(lambda: self.cursor()[0] == 0 and self.cursor()[2] != 0, "the overwrite prompt")
            self.g.run(10)
            if (self.cursor()[1] - 10) // 16 != 0:
                self.press("up", settle=8)
            trigger = lambda: self.g.call("input", buttons=["circle"], vsyncs=4)  # noqa: E731
        trigger()
        for _ in range(120):
            if self.cursor()[2] == 0:
                self.g.write(self.sym["_keystreamState"], struct.pack("<I", KEY))
                self.g.write(self.sym["vs_main_gametime"], GAMETIME)
                break
            self.g.run(1)
        else:
            raise Timeout(f"{self.name}: the overwrite was not taken")
        self.wait(lambda: self.writes() > w0, "the save written")
        self.g.run(40)
        if self.mode == "C":
            self.wait(lambda: (self.data() or {}).get("phase") == "wait", "saved")
            self.click("status", 0, block="data")
        else:
            self.press("circle", settle=10)  # dismiss "saved"
        return self.writes() - w0


def diff(a, b):
    return {k: changed_ranges(a[k], b[k])[:3] for k in a if a[k] != b[k]}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=None)
    parser.add_argument("--out", help="keep screenshots here")
    parser.add_argument("--sets", default="1,2,3,4,5,6,7")
    parser.add_argument("--modes", default="A,B,C")
    args = parser.parse_args()
    card = pathlib.Path(args.card) if args.card else cf.default_save()
    if not card.exists():
        print("SKIP: no memory card", card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_container"
    if out.exists() and not args.out:
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)
    sets = {int(c) for c in args.sets.split(",") if c}
    modes = [m for m in args.modes.split(",") if m]
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    try:
        with cf.card_guard(out / "cards", source=card) as cards:
            status = run(cards, out, sets, modes, check, bool(args.out))
    except Timeout as e:
        check(False, f"timeout: {e}")
        status = 1
    except AssertionError as e:
        check(False, str(e))
        status = 1
    if status == 77:
        return 77
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


def waiver(check):
    """unk4BB0 is MENUD's scratch: every read of it follows a fresh func_801031A0 (which rewrites it) in
    the same function of 234.c, and MENU7 never reads it: checked on the source, so its timing-dependent
    refreshes (the right panel's slide) need not match. Functions are split at every definition (a
    multi-line parameter list too); only func_801031A0 may keep a pointer to it."""
    src = (ROOT / "src/MENU/MENUD.PRG/234.c").read_text()
    starts = list(re.finditer(r"^[A-Za-z_][\w \t*]*?\b(\w+)\(([^;{}]*?)\)\s*\n\{", src, re.M))
    bodies = [(m.group(1), src[m.end():starts[i + 1].start() if i + 1 < len(starts) else len(src)])
              for i, m in enumerate(starts)]
    defs = len(re.findall(r"^\{", src, re.M))
    uses = [(n, b) for n, b in bodies if "unk4BB0" in b]
    bad = []
    for n, b in uses:
        if n == "func_801031A0":
            continue
        for m in re.finditer(r"unk4BB0", b):
            refill = b.rfind("func_801031A0()", 0, m.start())
            alias = re.search(r"=\s*&[^;]*$", b[b.rfind(";", 0, m.start()) + 1:m.start()])
            # the refill earlier in this function's body, no return between it and the read
            if refill < 0 or "return" in b[refill:m.start()] or alias:
                bad.append(n)
    menu7 = "unk4BB0" not in (ROOT / "src/MENU/MENU7.PRG/260.c").read_text()
    check(len(bodies) == defs and not bad and menu7 and len(uses) >= 2,
          f"waiver: unk4BB0 read only after func_801031A0 refills it in the same function "
          f"({[n for n, _ in uses]}, {len(bodies)}/{defs} functions, bad {bad}), never by MENU7")


def run(cards, out, sets, modes, check, shots):
    base = cards.card1
    fixtures = out / "fixtures"
    fixtures.mkdir(exist_ok=True)
    layout = cf.container_layout(out=fixtures)
    LAYOUT.update(layout)
    waiver(check)
    # the bag as the save's load leaves it (RAM): the container's records come from it
    env = {"ROOD_LIGHTS": str(out / "lights-prep"), "ROOD_GI": "0", "ROOD_DETERMINISTIC": "1"}
    g = launch_at("save_room", card=base, fast=True, log=out / "prep.log", extra_env=env)
    try:
        bag = g.read(symbols()["vs_main_inventory"], 0xF00)
    finally:
        g.close()
    fx = fixtures / "container.mcd"
    made = cf.container_fixture(base, fx, bag, layout)
    none = fixtures / "container_none.mcd"
    cf.container_fixture(base, none, bag, layout, kind="container_none")
    box = cf.decode(cf.read_file(bytearray(fx.read_bytes()), cf.save_name(1)))[cf.CONTAINER_AT:cf.CONTAINER_AT
                                                                                + layout["container"]]
    check(made.get("weapon") == 0 and made.get("shield") == 0 and made.get("gems", 0) >= 5,
          f"fixture: a weapon with gems, a shield with a gem, loose gems from the bag ({made})")
    results = {}
    if sets & {1, 2, 3, 4, 6}:
        for mode in modes:
            p1(mode, out, fx, box, made, sets, check, shots, results)
        compare(results, check)
    if 5 in sets:
        for mode in [m for m in modes if m != "C"]:
            p2(mode, out, none, check, shots, results)
        compare({k: v for k, v in results.items() if k[1] == 5}, check)
    if 7 in sets:
        for mode in [m for m in modes if m != "A"]:
            p3(mode, out, fx, check, shots)
    return 0


def compare(results, check):
    """A = B = C for every set's S1 (cursor memories A = B), S2 (the card images)."""
    keys = sorted({(k[1], k[2]) for k in results})
    for set_, what in keys:
        a = results.get(("A", set_, what))
        if a is None:
            continue
        for other in ("B", "C"):
            o = results.get((other, set_, what))
            if o is None:
                continue
            if what == "S1":
                (da, ca, sa), (do, co, so) = a, o
                d = diff(da, do)
                check(not d, f"set {set_}: S1 {other} = A (the bag, the storage, containerEmpty, the menu state, "
                             f"the pointers) {d}")
                if other == "B":
                    check(ca == co, f"set {set_}: S1 the cursor memories B = A {changed_ranges(ca, co)[:4]}")
                print(f"info: set {set_}: S1 unk4BB0 {other} {'=' if sa == so else '!='} A (waived)")
            elif what == "S2":
                check(a == o, f"set {set_}: S2 the card images {other} = A byte for byte "
                              f"{changed_ranges(a, o)[:4]}")
            else:
                check(a == o, f"set {set_}: {what} {other} = A ({a!r:.60} / {o!r:.60})")


def presentation(s, check, where):
    if s.mode == "A":
        return
    c = s.ctr()
    check(s.u8("vs_mainMenu_hideMenu") == 1, f"{s.name}: {where}: vs_mainMenu_hideMenu set while latched")
    check(s.u8("D_800EB9AF") == 1, f"{s.name}: {where}: the HUD held out")
    check(all(s.item_state(i) == 0 for i in (0xA, 0xB, 20, 21, 32, 33)),
          f"{s.name}: {where}: MENUD's own menu items never set up")
    g0 = c["gated"]
    s.g.run(8)
    if s.phase() and s.phase().startswith("prompt"):
        check(s.ctr()["gated"] > g0, f"{s.name}: {where}: MENU7 ran gated ({g0} -> {s.ctr()['gated']})")


def p1(mode, out, fx, box, made, sets, check, shots, results):
    name = f"P1{mode}"
    s = Session(name, out, fx, mode, shots=shots)
    try:
        hold = 4 in sets
        # ---- set 3: sorts, discard 13 of the 95 stack, Triangle ----
        if 3 in sets and mode != "C":
            s.open()
            presentation(s, check, "set 3 check prompt")
            s.shot("check")
            s.answer(True)
            s.to_top()
            st = s.ptr("vs_menu_inventoryStorage")
            check(s.g.read(st + 0xFB0, len(box)) == box, f"{name}: set 3: the card's container read (unkFB0)")
            presentation(s, check, "set 3 top")
            s.shot("top")
            s.press("circle")  # Remove
            s.press("r1")  # tab 0: weapons
            s.shot("list")
            for btn in ("circle", "circle", "down", "down", "circle", "down", "circle"):  # pick, actions, Sort, stat 1
                s.press(btn)
            for _ in range(2):  # the transfer list, then misc
                s.press("l1")
            s.press("l1")  # gems (5)
            for btn in ("circle", "circle", "down", "down", "down", "circle", "circle"):  # Search, Discard, Sort: STR
                s.press(btn)
            s.press("square")  # the details: L1 / R1 cycle (cursor memory: row 0, scroll = the index)
            s.press("r1")
            s.press("r1")
            s.shot("detail")
            s.press("cross")
            gems = s.source("gems", "gem_str", "<b")
            check(gems in (sorted(gems), sorted(gems, reverse=True)) and len(gems) >= 5,
                  f"{name}: set 3: the gems sorted by STR ({gems})")
            s.press("r1")  # misc (6)
            for btn in ("circle", "circle", "down", "down", "down", "circle", "down", "circle"):  # Sort: quantity
                s.press(btn)
            counts = [n for _, n in s.misc()]
            check(counts == sorted(counts, reverse=True) and counts[:1] == [95],
                  f"{name}: set 3: misc sorted by quantity, the 95 stack first ({counts})")
            s.shot("sorted")
            # the 95 stack is now first: discard 13 (1 x3, step 10, 1 x1)
            for btn in ("circle", "circle", "down", "circle"):  # pick, actions, Discard
                s.press(btn)
            s.shot("amount")
            for btn in ("up", "up", "up", "left", "up", "circle", "up"):
                s.press(btn)
            s.shot("discard-confirm")
            s.press("circle")  # Yes
            mine = sorted(n for i, n in s.misc() if i == made["misc_id"])
            check(mine == [70, 82], f"{name}: set 3: 13 discarded from the 95 stack ({mine})")
            w0 = s.writes()
            results[(mode, 3, "S1")] = s.exit_s1("triangle")
            s.closed()
            check(s.writes() == w0, f"{name}: set 3: Triangle: no write")
            results[(mode, 3, "S3")] = s3_check(s, check, "set 3")
        # ---- set 6: merge, swap, staged / unstaged ----
        if 6 in sets and mode != "C":
            s.open()
            s.answer(True)
            s.to_top()
            s.press("circle")  # Remove: tab 7 (cursor memory off) or the last
            for _ in range(8):
                if s.mode != "A" and s.ctr()["tab"] == 6:
                    break
                if s.mode == "A" and ((s.u8("D_800F4EE8", 0xA0) - 1) & 7) == 6:
                    break
                s.press("l1")
            check(((s.u8("D_800F4EE8", 0xA0) - 1) & 7) == 6, f"{name}: set 6: on the misc tab")
            for btn in ("circle", "down", "circle"):  # the 95 and 70 stacks merge
                s.press(btn)
            s.shot("merged")
            merged = s.misc()
            mine = sorted(n for i, n in merged if i == made["misc_id"])
            check(mine == [65, 100], f"{name}: set 6: the 95 and 70 stacks merged to 100 + 65 ({mine})")
            # the cursor stays on the merge's target (row 1): it and row 2 (another item) swap
            for btn in ("circle", "down", "circle"):
                s.press(btn)
            swapped = s.misc()
            check(len(merged) >= 3 and merged[1][0] != merged[2][0]
                  and swapped == [merged[0], merged[2], merged[1]] + merged[3:],
                  f"{name}: set 6: rows 1 and 2 swapped ({merged} -> {swapped})")
            for btn in ("circle", "circle", "circle"):  # stage row 0 (Remove)
                s.press(btn)
            check(s.staged(), f"{name}: set 6: row 0 staged (itemsToTransfer)")
            s.press("r1")  # tab 7
            s.press("circle")  # unstaged
            check(not s.staged(), f"{name}: set 6: unstaged again (itemsToTransfer empty)")
            s.shot("unstaged")
            results[(mode, 6, "S1")] = s.exit_s1("triangle")
            s.closed()
            results[(mode, 6, "S3")] = s3_check(s, check, "set 6")
        # ---- set 4: the cancel warning ----
        if hold:
            if mode == "C":  # the processes' heaps alike at set 4 (A and B opened twice before it: the
                s.open()     # first open frees a block, and the storage's address shows in S1's pointers)
                s.answer(False)
                s.closed()
            w0 = s.writes()
            bag0 = s.g.read(s.sym["vs_main_inventory"], 0xF00) + s.g.read(s.sym["vs_main_inventoryIndices"], 0xB0)
            s.open()
            s.answer(True)
            s.to_top()
            # something to revert: the misc tab's first stack removed into the bag and committed
            if mode == "C":
                for kind, i in (("top", 0), ("tab", 6), ("row", 0), ("row", 0), ("menu", 0)):
                    s.click(kind, i)
                s.right_click()  # End: the commit asked
                s.click("yes", 0)
                s.g.run(10)
                results[(mode, 4, "S1")] = s.mouse_exit_s1(None, 0)
            else:
                s.press("circle")  # Remove
                for _ in range(8):
                    if ((s.u8("D_800F4EE8", 0xA0) - 1) & 7) == 6:
                        break
                    s.press("l1")
                for btn in ("circle", "circle", "circle", "cross", "circle"):  # stage, End, Yes: the top
                    s.press(btn)
                results[(mode, 4, "S1")] = s.exit_s1("cross")
            moved = results[(mode, 4, "S1")][0]
            check(moved["inventory"] + moved["indices"] != bag0, f"{name}: set 4: the stack moved into the bag")
            s.prompt()
            presentation(s, check, "set 4 cancel warning")
            s.shot("cancel-warning")
            s.answer(False)
            s.closed()
            bag1 = s.g.read(s.sym["vs_main_inventory"], 0xF00) + s.g.read(s.sym["vs_main_inventoryIndices"], 0xB0)
            check(bag1 == bag0, f"{name}: set 4: No reverts the bag ({changed_ranges(bag0, bag1)[:3]})")
            results[(mode, 4, "S3a")] = s3_check(s, check, "set 4 (No)")
            s.open()
            s.answer(True)
            s.to_top()
            if mode == "C":
                s.right_click(run=4)
                s.wait(lambda: s.menu() == 7, "MENUD's exit")
            else:
                s.exit_s1("cross")
            s.prompt()
            s.answer(True)  # Yes: the save list
            s.wait(lambda: s.cursor()[0] == 180 and s.cursor()[2] != 0, "the card rows")
            s.g.run(10)
            if mode == "B":
                d = s.data()
                check(d is not None and d.get("container") == 1 and d["visible"],
                      f"{name}: set 4: the save list is the Data screen in container mode")
                s.shot("save-list")
            if mode == "C":
                s.right_click()
            else:
                s.press("cross")  # cancel at the card rows: the warning again
            s.prompt()
            check(s.m7_state() == 12, f"{name}: set 4: cancelling the save asks again (MENU7 {s.m7_state()})")
            s.answer(False)
            s.closed()
            check(s.writes() == w0, f"{name}: set 4: no write ({s.writes() - w0})")
            results[(mode, 4, "S3b")] = s3_check(s, check, "set 4 (cancel)")
        # ---- set 1: Remove the weapon assembly and the 95 stack, End, save ----
        if 1 in sets:
            before = s.card1.read_bytes()
            s.open()
            s.answer(True)
            s.to_top()
            if mode == "C":
                mouse_set1(s)
                results[(mode, 1, "S1")] = s.mouse_exit_s1("top", 2)
            else:
                s.press("circle")  # Remove
                for _ in range(8):  # weapons
                    if ((s.u8("D_800F4EE8", 0xA0) - 1) & 7) == 0:
                        break
                    s.press("r1")
                for btn in ("circle", "circle", "circle"):  # pick, actions, Remove
                    s.press(btn)
                for _ in range(8):  # misc
                    if ((s.u8("D_800F4EE8", 0xA0) - 1) & 7) == 6:
                        break
                    s.press("l1")
                for btn in ("circle", "circle", "circle"):  # the 95 stack (row 0: the tab's first visit)
                    s.press(btn)
                s.shot("staged")
                s.press("cross")  # End: the commit asked (tab 7)
                s.shot("commit")
                s.press("circle")  # Yes
                s.top_to(2)
                s.shot("end")
                results[(mode, 1, "S1")] = s.exit_s1("circle")
            s1 = results[(mode, 1, "S1")][0]
            check(s1["menuState"][1] == 5, f"{name}: set 1: OK: returnState 5 (save) ({s1['menuState'][1]})")
            s.card_rows(1)
            s.file_rows(0)
            n = s.save_over(0)
            s.closed()
            check(n == 1, f"{name}: set 1: one write ({n})")
            after = s.card1.read_bytes()
            bad = file_bytes_only(before, after, cf.save_name(1))
            check(not bad, f"{name}: set 1: only file 1's frames and blocks changed ({bad[:3]})")
            results[(mode, 1, "S2")] = after
            saved = cf.decode(cf.read_file(bytearray(after), cf.save_name(1)))
            packed = saved[cf.CONTAINER_AT:cf.CONTAINER_AT + 0x3C00]
            check(packed == pack(s1["unkC430"]), f"{name}: set 1: the saved container is S1's unkC430 packed "
                                                 f"({changed_ranges(packed, pack(s1['unkC430']))[:3]})")
            results[(mode, 1, "S3")] = s3_check(s, check, "set 1")
        # ---- set 2: Store the weapon back; the equipped one refused ----
        if 2 in sets and mode != "C":
            before = s.card1.read_bytes()
            s.open()
            s.answer(True)
            s.to_top()
            s.top_to(1)  # Store
            s.press("circle")
            for _ in range(8):
                if ((s.u8("D_800F4EE8", 0xA0) - 1) & 7) == 0:
                    break
                s.press("r1")
            for btn in ("circle", "circle", "circle"):  # the equipped first weapon: Store refused (invalid)
                s.press(btn)
            s.press("cross")  # back out of the actions
            refused = s.g.read(s.ptr("vs_menu_inventoryStorage") + 0xC3B0, 2) == b"\0\0"
            check(refused, f"{name}: set 2: the equipped weapon is not staged")
            s.press("up")  # the last weapon (the one set 1 took out, gems and all): Up wraps
            for btn in ("circle", "circle", "circle"):
                s.press(btn)
            check(s.staged(), f"{name}: set 2: the weapon staged (itemsToTransfer)")
            s.shot("store-staged")
            s.press("cross")  # End
            s.shot("store-commit")
            s.press("circle")
            s.top_to(2)
            s.shot("store-end")
            results[(mode, 2, "S1")] = s.exit_s1("circle")
            s.card_rows(1)
            s.file_rows(0)
            n = s.save_over(0)
            s.closed()
            check(n == 1, f"{name}: set 2: one write ({n})")
            results[(mode, 2, "S2")] = s.card1.read_bytes()
            bad = file_bytes_only(before, results[(mode, 2, "S2")], cf.save_name(1))
            check(not bad, f"{name}: set 2: only file 1's frames and blocks changed ({bad[:3]})")
            results[(mode, 2, "S3")] = s3_check(s, check, "set 2")
        if mode == "B" and shots:  # 16:9 (by pad: the layout's 1280 units no longer match the window's pixels)
            s.g.call("graphics", widescreen=True)
            s.open()
            s.answer(True)
            s.to_top()
            s.top_to(1)
            s.press("circle")
            s.press("r1")
            s.shot("wide-list")
            s.press("circle")
            s.press("circle")
            s.shot("wide-actions")
            s.exit_s1("triangle")
            s.closed()
            s.g.call("graphics", widescreen=False)
        if mode == "B":
            opacity(s, check)
    finally:
        s.close()


def mouse_set1(s):
    """Set 1 by the mouse: Remove, the weapon tab, the weapon picked twice (its actions), Remove; the misc
    tab, the 95 stack likewise; a right click (Cross: the commit asked): Yes; End."""
    s.click("top", 0)
    s.click("tab", 0)
    s.click("row", 0)
    s.click("row", 0)
    s.click("menu", 0)
    s.click("tab", 6)
    s.click("row", 0)
    s.click("row", 0)
    s.click("menu", 0)
    s.right_click()  # a right click is Cross: End (the commit asked)
    s.click("yes", 0)
    s.g.run(10)


def pack(container):
    """MENU7's _copyContainer: unkC430 packed in display order, parents remapped (card_fixtures)."""
    return cf.pack_container(container)


def s3_check(s, check, where):
    t = s.s3()
    check(t["menu"] == CLOSED and t["D_800EB9B0"] == 0 and t["fade"] == 0,
          f"{s.name}: {where}: closed ({t['menu']:#x}, D_800EB9B0 {t['D_800EB9B0']}, fade {t['fade']})")
    print(f"info: {s.name}: {where}: the heap's free total {t['heap']} (before the open {s.heap0}; compared "
          f"between the processes)")
    check(t["hud"] == s.hud0 and t["hideMenu"] == s.hide0,
          f"{s.name}: {where}: the HUD flag and hideMenu as before ({t['hud']}/{s.hud0}, {t['hideMenu']}/{s.hide0})")
    check(t["container"] is None and s.data() is None, f"{s.name}: {where}: no session left")
    return (t["menu"], t["D_800EB9B0"], t["fade"], t["heap"], t["hud"], t["hideMenu"])


def ppm_mean(path, box):
    """The mean grey of a binary PPM (present_dump) inside box (x0, y0, x1, y1)."""
    data = path.read_bytes()
    parts, at = [], 0
    while len(parts) < 4:
        while data[at:at + 1].isspace():
            at += 1
        start = at
        while not data[at:at + 1].isspace():
            at += 1
        parts.append(data[start:at])
    w, h = int(parts[1]), int(parts[2])
    pixels = data[at + 1:]
    x0, y0, x1, y1 = box
    total = n = 0
    for y in range(y0, min(y1, h), 4):
        row = pixels[(y * w) * 3:(y * w + w) * 3]
        for x in range(x0, min(x1, w), 4):
            total += row[x * 3] + row[x * 3 + 1] + row[x * 3 + 2]
            n += 3
    return total / max(n, 1)


def opacity(s, check):
    """The backdrop follows ui_menu_opacity and the menu tint (no forced-opaque fill): the check prompt
    at the default and at 0.5 with another tint; the backdrop region (left of the card) differs, the
    theme reports the ground's alpha and the tint."""
    s.open()
    s.g.run(10)

    def capture(name):
        ppm = s.out / f"{s.name}-{name}.ppm"
        ppm.unlink(missing_ok=True)
        s.g.call("present_dump", path=str(ppm.resolve()))
        for _ in range(100):
            if ppm.exists() and ppm.stat().st_size > 0:
                break
            s.g.run(1)
        s.g.run(2)
        mean = ppm_mean(ppm, (380, 96, 580, 240))  # above the card: the menu background's emblem
        ppm.unlink(missing_ok=True)
        return mean
    m0, t0 = capture("opacity-default"), s.ui().get("theme", {})
    s.settings.write_text("ui_menu_opacity 0.5\nui_menu_tint classic\n")
    s.g.call("settings", action="load")
    s.g.run(6)
    m1, t1 = capture("opacity-half"), s.ui().get("theme", {})
    s.shot("opacity-half")
    check(t1.get("ground", "")[-2:] in ("7f", "80") and t1.get("tint") != t0.get("tint"),
          f"{s.name}: the theme follows ui_menu_opacity 0.5 and the tint (ground {t0.get('ground')} -> "
          f"{t1.get('ground')}, tint {t0.get('tint')} -> {t1.get('tint')})")
    check(abs(m1 - m0) > 1.0, f"{s.name}: the backdrop shows the game through it as the opacity says "
                              f"(mean grey {m0:.1f} -> {m1:.1f})")
    s.settings.write_text("")
    s.answer(False)
    s.closed()


def p2(mode, out, none, check, shots, results):
    """Set 5: no save of this journey on the card (container_none)."""
    s = Session(f"P2{mode}", out, none, mode, shots=shots)
    try:
        w0 = s.writes()
        s.open()
        s.answer(True)
        s.dismiss_wait()
        s.prompt()
        check(s.m7_state() == 5, f"{s.name}: set 5: emptyCard, then containerWarn (MENU7 {s.m7_state()})")
        s.shot("warn")
        s.answer(False)
        s.closed()
        check(s.writes() == w0, f"{s.name}: set 5: warn No: no write")
        s.open()
        s.answer(True)
        s.dismiss_wait()
        s.prompt()
        s.answer(True)
        s.to_top()
        check(s.u8("vs_main_settings") & 0x10, f"{s.name}: set 5: warn Yes: settings bit 0x10 (an empty container)")
        if s.mode == "B":
            c = s.ctr()
            check(sum(c["box_count"]) == 0, f"{s.name}: set 5: the container is empty ({c['box_count']})")
        results[(mode, 5, "S1")] = s.exit_s1("triangle")
        s.closed()
        check(s.writes() == w0, f"{s.name}: set 5: no write")
        results[(mode, 5, "S3")] = s3_check(s, check, "set 5")
    finally:
        s.close()


def p3(mode, out, fx, check, shots):
    """Set 7: a soft reset mid-transfer: nothing left over the title (the frame forgotten, hideMenu 0);
    in the save list MENU7 has the reset off (vs_main_enableReset(0) while the card is in use, as on
    the PlayStation): the combination does nothing there, the list still shows; then a container opens
    and closes again."""
    s = Session(f"P3{mode}", out, fx, mode, shots=shots)
    g = s.g
    try:
        s.open()
        s.answer(True)
        s.to_top()
        if mode == "C":
            s.click("top", 0)
        else:
            s.press("circle")
        titles = pathlib.Path(g.log).read_text().count("TITLE MENU READY")
        g.call("input", buttons=["l1", "l2", "r1", "r2", "start", "select"], vsyncs=6)
        s.wait(lambda: pathlib.Path(g.log).read_text().count("TITLE MENU READY") > titles,
               "the title after a soft reset", limit=3000, step=30)
        u = s.ui()
        check(u.get("container") is None and u.get("data") is None and s.u8("vs_mainMenu_hideMenu") == 0,
              f"{s.name}: set 7: reset mid-transfer: nothing native over the title, hideMenu 0")
        g.press("start", vsyncs=4, settle=90)
        for _ in range(3):
            g.press("circle", vsyncs=3, settle=90)
        g.run(400)
        # the save list: the reset is off while MENU7 has the card
        w0 = s.writes()
        s.open()
        s.answer(True)
        s.to_top()
        if mode == "C":
            s.click("top", 2)
        else:
            s.top_to(2)
            s.press("circle")
        s.wait(lambda: s.cursor()[0] == 180 and s.cursor()[2] != 0, "the card rows")
        titles = pathlib.Path(g.log).read_text().count("TITLE MENU READY")
        g.call("input", buttons=["l1", "l2", "r1", "r2", "start", "select"], vsyncs=6)
        g.run(200)
        d = s.data() or {}
        check(pathlib.Path(g.log).read_text().count("TITLE MENU READY") == titles and s.m7_state() == 11
              and d.get("container") == 1 and d.get("visible"),
              f"{s.name}: set 7: in the save list the reset is off (MENU7 {s.m7_state()}), the list still shows")
        if mode == "C":
            s.right_click()
        else:
            s.press("cross")  # cancel: the warning
        s.prompt()
        s.answer(False)
        s.closed()
        check(s.ctr() is None and s.u8("vs_mainMenu_hideMenu") == 0 and s.writes() == w0,
              f"{s.name}: set 7: after the reset a container opens, saves nothing ({s.writes() - w0} writes) "
              f"and closes")
    finally:
        s.close()


if __name__ == "__main__":
    sys.exit(main())
