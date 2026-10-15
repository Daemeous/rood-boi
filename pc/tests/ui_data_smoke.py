#!/usr/bin/env python3
"""Modern menus, the native Data screen (pc/src/ui/data_bridge.c, ui_data.cpp,
ui_data_draw.cpp, data_plan.c; MENU7 hooks in src/MENU/MENU7.PRG/260.c):
the game's own save / load screen runs unchanged under a native frame; its
drawing is gated, clicks become pad presses MENU7 reads as the player's, its
statics are published for the frame.

    python3 pc/tests/ui_data_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--cases 1,2,...]

Card safety: everything runs inside card_fixtures.card_guard (the player's
cards hashed before and after; ROOD_CARD1 / ROOD_CARD2 temp copies set
before any launch); every process gets its own copies under OUT/ui_data.
Skipped (77) without a card, or when the card's save room is not a save
point (func_8008A4FC() 0: the frame's at_point).

Parity: process A runs the original screen (ui_menus off), B the native
one by pad, C the native one by the mouse (real SDL events through the
system RPC, 960x720: layout units are pixels). A save's bytes depend on the
keystream (_keystreamState: the key and the encoding) and the play time:
each process pins both at the vsync MENU7 takes the file's confirm (its
cursor gone), so the same frames follow in every process.

Cases:
  1 open through the shell: the chooser (ops), the DATA title off screen
    (item 6 at x 320), MENU7's drawing gated, the HUD held out
  2 save parity (pad): A and B save a new file 2 on card 1: the 128 KiB
    images byte-identical (the decoded difference reported as info when
    not), one data write, no .tmp left, only file 2's blocks changed
  3 the mouse (C): hover changes nothing, Save clicked (the card rows
    advance by themselves), file 2 double-clicked and clicked while saving
    (one write, busy drops), Continue, a right click goes back; still one
    write after the close, only file 2's blocks changed, the image
    byte-identical to A's
  4 overwrite: No leaves the card as it was; Yes changes only that file,
    exactly one write (also after Continue and the close)
  5 load parity (card 2: a fixture with three saves; file 2, an older
    generation with its own play time hours): A and B load it by pad, C by
    the mouse (Load, back from card 1's list, card 2, file 2 clicked); the
    play time hours in RAM are file 2's; after the jump every block
    _applyLoadedSaveFile writes is equal (the RAM span; the snapshot hashes
    are reported: the menu paths' own state differs); no frame nor session
    left; nothing native over the loading screen
  6 errors: card 2 absent (insertError, dismissed, no write); a corrupt save
    (refused, RAM and card unchanged); a full card (cardFull); a blank card
    (noData on load; a save makes file 1); writes refused (card fail_writes:
    saveFailed, the card as before); an unformatted card (card unformatted:
    the format prompt, No: the card as before)
  7 exits: Cross back to the shell's System list on Data (currentMenu 0,
    cursorMemory and the cursor memories as the original's), Triangle closes
    (0x3F), the background fade and the HUD flag back, the card row
    remembered (unk85[3])

--out keeps screenshots there (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, BUILD, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import card_fixtures as cf  # noqa: E402

CLOSED = 0x3F
DATA_MENU = 7 | 0x40
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4
MENU7_SECTION = "rood_ovl_menu_menu7_prg"
MENU7_STATICS = ("_selectCursorXy", "_fileMenuElements")
ELEMENT = 52  # sizeof(fileMenuElements_t); y at +14
ITEM = 64     # sizeof(vs_battle_menuItem_t); x at +20
KEY = 0x2468ACE1
GAMETIME = bytes([0, 45, 23, 1])  # vs_main_gametime: 1:23:45
# _applyLoadedSaveFile's blocks: vs_main_scoredata .. vs_main_inventoryIndices (one span of main's data)
SPAN = (0x8005FEA0, 0x80061AE0)


_menu7 = None


def menu7_symbols():
    global _menu7
    if _menu7:
        return _menu7
    image = BUILD / "rood_battle_probe"
    out = subprocess.run([os.environ.get("ROOD_OBJDUMP", "objdump"), "-t", str(image)], capture_output=True,
                         text=True).stdout
    found = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 6 and parts[-1] in MENU7_STATICS and parts[-3].startswith(MENU7_SECTION):
            found.setdefault(parts[-1], []).append(int(parts[0], 16))
    bad = [n for n in MENU7_STATICS if len(found.get(n, [])) != 1]
    if bad:
        raise RuntimeError(f"MENU7 statics not resolved to one address: {bad}")
    _menu7 = {n: a[0] for n, a in found.items()}
    return _menu7


class Timeout(Exception):
    pass


class Session:
    def __init__(self, name, out, card1, card2=None, modern=True, shots=False):
        self.name, self.out, self.shots, self.modern = name, out, shots, modern
        self.card1 = out / f"{name}-card1.mcd"
        shutil.copyfile(card1, self.card1)
        self.card2 = out / f"{name}-card2.mcd"
        self.card2.unlink(missing_ok=True)
        if card2:
            shutil.copyfile(card2, self.card2)
        real = {p.resolve() for p in cf.player_cards()}
        assert self.card1.resolve() not in real and self.card2.resolve() not in real
        lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
        env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_DETERMINISTIC": "1",
               "ROOD_CARD2": str(self.card2)}
        self.g = launch_at("save_room", card=self.card1, fast=True, log=out / f"{name}.log", extra_env=env)
        self.g.call("graphics", interpolate=False)
        self.g.call("ui_menu", modern=bool(modern))
        self.sym = symbols()
        self.m7 = menu7_symbols()

    def close(self):
        self.g.close()

    # ---- reads ----
    def ui(self):
        return self.g.call("ui_menu")

    def data(self):
        return self.ui().get("data")

    def u8(self, name, offset=0):
        return self.g.read(self.sym[name] + offset, 1)[0]

    def current_menu(self):
        return self.u8("vs_battle_menuState")

    def menu7_state(self):
        return self.u8("vs_battle_submenuStates", 7)

    def cursor(self):
        v = struct.unpack("<I", self.g.read(self.m7["_selectCursorXy"], 4))[0]
        return v & 0xFFFF, v >> 16, v

    def page(self):
        y = struct.unpack("<h", self.g.read(self.m7["_fileMenuElements"] + 5 * ELEMENT + 14, 2))[0]
        return (72 - y) // 40

    def file_cursor(self):
        x, y, v = self.cursor()
        return (y - 62) // 40 + self.page() if v and x == 24 else -1

    def writes(self):
        return self.g.call("card")["writes"]

    def hud(self):
        return self.u8("D_800EB9AF")

    def title_x(self):
        items = int.from_bytes(self.g.read(self.sym["vs_battle_menuItems"], PTR), "little")
        return struct.unpack("<h", self.g.read(items + 6 * ITEM + 20, 2))[0] if items else None

    def span(self):
        return self.g.read(SPAN[0], SPAN[1] - SPAN[0])

    def exit_state(self):
        g = self.g
        return {"currentMenu": self.current_menu(), "settings": g.read(self.sym["vs_main_settings"], 32),
                "cursorMemories": g.read(self.sym["D_800F4EE8"], 0x90), "hud": self.hud(),
                "fade": struct.unpack("<i", g.read(self.sym["vs_mainMenu_backgroundFadeStep"], 4))[0]}

    def shot(self, name):
        if self.shots:
            self.g.present_screenshot(self.out / f"{self.name}-{name}.png")

    # ---- waits ----
    def wait(self, pred, what, limit=900, step=2):
        for _ in range(limit // step):
            if pred():
                return
            self.g.run(step)
        raise Timeout(f"{self.name}: {what}")

    def wait_phase(self, phase, limit=900):
        self.wait(lambda: (self.data() or {}).get("phase") == phase, f"phase {phase} "
                  f"({(self.data() or {}).get('phase')})", limit)

    def press(self, button, settle=6):
        self.g.press(button, vsyncs=3, settle=settle)

    # ---- the pointer: real SDL events (960x720: layout units are window pixels) ----
    def target(self, kind, index):
        for h in reversed((self.data() or {}).get("hits", [])):
            if h["k"] == kind and h["i"] == index:
                r = h["r"]
                return [(r[0] + r[2]) / 2, (r[1] + r[3]) / 2]
        return None

    def move(self, xy, run=2):
        self.g.call("system", mouse=xy, button=0, wheel=0)
        self.g.run(run)

    def click(self, kind, index, run=2, button=1):
        xy = self.target(kind, index)
        if xy is None:
            raise Timeout(f"{self.name}: no {kind} {index} drawn")
        self.g.call("system", mouse=xy, button=button, down=True)
        self.g.run(1)
        self.g.call("system", mouse=xy, button=button, down=False)
        self.g.run(run)

    # ---- opening ----
    def open_data(self):
        """Triangle and the game's DATA (off), or System > Data (modern); the chooser up."""
        g = self.g
        self.press("triangle", settle=40)
        if self.modern:
            # L1: the shell's last tab (the Equipment tab on the way would rebuild the
            # bag's lists as the original Equipment screen does: not the original Data's path)
            for _ in range(10):
                if self.ui()["tab"] == "system":
                    break
                self.press("l1", settle=8)
            for _ in range(16):
                if self.ui()["row"] == "data":
                    break
                self.press("down", settle=4)
            self.press("circle", settle=2)
        else:
            for _ in range(4):
                self.press("down", settle=10)
            self.press("circle", settle=2)
        self.wait(lambda: self.current_menu() == DATA_MENU and self.menu7_state() == 6, "the Data chooser")
        g.run(20 if self.modern else 90)  # the chooser takes input (the original's rows slide in first)

    # ---- MENU7 by pad (A and B alike) ----
    def chooser(self, row):
        if row:
            self.press("down", settle=4)
        self.press("circle", settle=4)

    def card_rows(self, n):
        self.wait(lambda: self.cursor()[0] == 180 and self.cursor()[2] != 0, "the card rows")
        if (self.cursor()[1] - 26) // 16 != n:
            self.press("down", settle=4)
        self.wait(lambda: (self.cursor()[1] - 26) // 16 == n, f"card row {n}")
        self.press("circle", settle=4)

    def file_rows(self, i):
        self.wait(lambda: self.cursor()[0] == 24 and self.cursor()[2] != 0, "the file list")
        for _ in range(8):
            f = self.file_cursor()
            if f == i:
                return
            self.press("down" if f < i else "up", settle=4)
        raise Timeout(f"{self.name}: file {i} ({self.file_cursor()})")

    def pin_at_confirm(self, trigger):
        """trigger() starts the confirm (input that runs no vsync); at the
        vsync MENU7's cursor goes the keystream and the play time are pinned
        (the same frames follow)."""
        trigger()
        for _ in range(60):
            if self.cursor()[2] == 0:
                self.g.write(self.sym["_keystreamState"], struct.pack("<I", KEY))
                self.g.write(self.sym["vs_main_gametime"], GAMETIME)
                return
            self.g.run(1)
        raise Timeout(f"{self.name}: the confirm was not taken")

    def confirm_pad(self):
        self.pin_at_confirm(lambda: self.g.call("input", buttons=["circle"], vsyncs=4))
        self.g.run(4)

    def to_chooser(self, limit=900):
        """Cross out of the card rows (or wait) until the chooser is back."""
        for _ in range(limit // 4):
            if self.menu7_state() == 6:
                return
            x, _y, v = self.cursor()
            if v and x == 180:
                self.press("cross", settle=6)
            else:
                self.g.run(4)
        raise Timeout(f"{self.name}: back at the chooser")

    def back_out(self):
        """From the chooser (or the card rows): Cross (back to the menu's top level)."""
        self.to_chooser()
        self.g.run(10 if self.modern else 60)
        self.press("cross", settle=10)
        self.wait(lambda: self.current_menu() == 0, "back at the top level")
        self.g.run(20)

    def close_menu(self):
        for _ in range(4):  # the original top level takes Triangle once its rows are back
            self.press("triangle", settle=30)
            if self.current_menu() != 0:
                break
        self.wait(lambda: self.current_menu() == CLOSED, f"the menu closed ({self.current_menu():#x})")
        self.g.run(10)


def changed_ranges(a, b):
    out, i = [], 0
    while i < len(a):
        if a[i] != b[i]:
            j = i
            while j < len(a) and a[j] != b[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def file_bytes_only(before, after, name):
    """Every changed byte is in `name`'s directory frames or blocks (after)."""
    img = bytearray(after)
    blocks = cf.files(img).get(name, [])
    allowed = [((b + 1) * cf.FRAME, (b + 2) * cf.FRAME) for b in blocks]
    allowed += [((b + 1) * cf.BLOCK, (b + 2) * cf.BLOCK) for b in blocks]
    ok = bytearray(len(after))
    for s, e in allowed:
        ok[s:e] = b"\1" * (e - s)
    return [r for r in changed_ranges(before, after) if not all(ok[r[0]:r[1]])]


def decoded_equal(a, b, name):
    """The save `name` in two images, decoded: equal but for the key and the keystream-only bytes."""
    pa = cf.decode(cf.read_file(bytearray(a), name))
    pb = cf.decode(cf.read_file(bytearray(b), name))
    return [r for r in changed_ranges(pa[:cf.SAVE_SIZE], pb[:cf.SAVE_SIZE]) if not (r[0] >= 0x180 and r[1] <= 0x184)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=None)
    parser.add_argument("--out", help="keep screenshots here")
    parser.add_argument("--cases", default="1,2,3,4,5,6,7")
    args = parser.parse_args()
    card = pathlib.Path(args.card) if args.card else cf.default_save()
    if not card.exists():
        print("SKIP: no memory card", card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_data"
    if out.exists() and not args.out:
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)
    cases = {int(c) for c in args.cases.split(",") if c}
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    try:
        with cf.card_guard(out / "cards", source=card) as cards:
            status = run(cards, out, cases, check, bool(args.out))
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


def run(cards, out, cases, check, shots):
    base = cards.card1
    fixtures = out / "fixtures"
    fixtures.mkdir(exist_ok=True)
    multi = cf.fixture("multi", base, fixtures / "multi.mcd")
    sessions = []

    def session(name, **kw):
        s = Session(name, out, base, shots=shots, **kw)
        sessions.append(s)
        return s

    try:
        # ---- B: the native screen by pad (cases 1, 2, 4, 7); A: the original (parity) ----
        b = session("B")
        b.open_data()
        d = b.data()
        if not d or not d.get("at_point"):
            print(f"SKIP: the save room is not a save point here ({d and d.get('at_point')})")
            return 77
        if 1 in cases:
            check(d["phase"] == "ops" and d["save_ok"] == 1 and d["visible"],
                  f"1: System > Data opens the native chooser ({d['phase']}, save_ok {d['save_ok']})")
            check(b.title_x() == 320, f"1: the DATA title stays off screen (item 6 x {b.title_x()})")
            check(b.hud() == 1, f"1: the HUD held out (D_800EB9AF {b.hud()})")
            g0 = d["menu7_gated"]
            b.g.run(60)
            d = b.data()
            check(d["menu7_gated"] > g0 and d["phase"] == "ops",
                  f"1: MENU7 ran 60 vsyncs gated ({g0} -> {d['menu7_gated']} calls skipped), nothing of its own drawn")
            check(d["location_name"] != "" and d["play"] != "", f"1: this journey ({d['location_name']}, {d['play']})")
            b.shot("open")
        a = session("A", modern=False)
        a.open_data()
        check(a.data() is None, "A: the original screen, no frame (data null)")
        a_bytes = None
        exits = {}
        before = a.card1.read_bytes()
        for s in (a, b):
            w0 = s.writes()
            s.chooser(0)
            s.card_rows(1)
            s.file_rows(1)
            s.confirm_pad()
            s.wait(lambda: s.writes() > w0, "the save written")
            s.g.run(30)
            s.press("circle", settle=10)  # dismiss "saved"
            s.back_out()
            back = s.exit_state()
            if s is b:
                u = b.ui()
                check(u["visible"] and u["tab"] == "system" and u["row"] == "data" and b.data() is None,
                      f"7: Cross: the shell's System list on Data, the frame gone ({u['tab']}, {u['row']})")
            s.close_menu()
            exits[s.name] = (back, s.exit_state())
            check(s.writes() - w0 == 1, f"2: {s.name}: one data write ({s.writes() - w0})")
            check(not pathlib.Path(str(s.card1) + ".tmp").exists(), f"2: {s.name}: no .tmp left")
        if 7 in cases:
            for i, when in enumerate(("back from Data", "closed")):
                # back at the top level the shell holds the HUD out (as for every tab), the original's
                # top level does not: compared once the menu closed
                for k in ("currentMenu", "settings", "cursorMemories", "fade") + (("hud",) if i else ()):
                    check(exits["A"][i][k] == exits["B"][i][k], f"7: {when}: {k} as the original's "
                          f"({exits['A'][i][k]!r:.40} / {exits['B'][i][k]!r:.40})")
            closed = exits["B"][1]
            check(closed["currentMenu"] == CLOSED and closed["fade"] == 0,
                  f"7: closed: 0x3F, the background fade done ({closed['currentMenu']:#x}, {closed['fade']})")
        a_bytes, b_bytes = a.card1.read_bytes(), b.card1.read_bytes()
        if 2 in cases:
            check(sorted(cf.saves(bytearray(a_bytes))) == [1, 2], f"2: A made file 2 ({sorted(cf.saves(bytearray(a_bytes)))})")
            # the keystream and the play time are pinned in every process: the images are byte-identical
            if a_bytes != b_bytes:
                print(f"info: 2: decoded, file 2 differs at {decoded_equal(a_bytes, b_bytes, cf.save_name(2))[:4]}")
            check(a_bytes == b_bytes, f"2: the card images byte-identical (A original, B native by pad; differ at "
                                      f"{changed_ranges(a_bytes, b_bytes)[:4]})")
            bad = file_bytes_only(before, b_bytes, cf.save_name(2))
            check(not bad, f"2: only file 2's frames and blocks changed ({bad[:4]})")
        a.close()

        if 4 in cases:
            b.open_data()
            b.chooser(0)
            b.card_rows(1)
            b.file_rows(1)
            h0 = cf.sha256(b.card1)
            b.press("circle", settle=4)
            b.wait_phase("prompt")
            d = b.data()
            check(d["prompt"] == "overwrite" and d["cancelled"] == 1, f"4: the overwrite prompt on No ({d['prompt']})")
            b.shot("overwrite")
            b.press("cross", settle=4)
            b.wait_phase("files")
            check(cf.sha256(b.card1) == h0, "4: No: the card as before")
            before = b.card1.read_bytes()
            b.press("circle", settle=4)
            b.wait_phase("prompt")
            w0 = b.writes()
            b.press("up", settle=4)
            b.press("circle", settle=4)
            b.wait(lambda: b.writes() > w0, "the overwrite written")
            b.wait_phase("wait")
            check(b.data()["msg"] == "saved", f"4: Yes: saved ({b.data()['msg']})")
            check(b.writes() - w0 == 1, f"4: Yes: one data write ({b.writes() - w0})")
            after = b.card1.read_bytes()
            bad = file_bytes_only(before, after, cf.save_name(2))
            check(after != before and not bad, f"4: Yes changes only file 2 ({bad[:4]})")
            b.press("circle", settle=10)
            b.back_out()
            b.close_menu()
            check(b.writes() - w0 == 1, f"4: still one data write after Continue and the close ({b.writes() - w0})")

        if 3 in cases:
            c = session("C")
            c.open_data()
            h0 = cf.sha256(c.card1)
            before_c = c.card1.read_bytes()
            for kind, i in (("op", 1), ("op", 2), ("card", 1), ("card", 2)):
                xy = c.target(kind, i)
                if xy:
                    c.move(xy)
            d = c.data()
            check(d["phase"] == "ops" and d["goal"]["kind"] == "" and cf.sha256(c.card1) == h0,
                  f"3: hover alone: nothing posted, the card unchanged ({d['goal']})")
            c.click("op", 1)
            c.wait_phase("files")
            check(c.data()["op"] == 1, "3: Save clicked: the card rows advanced by themselves to the list")
            for i in range(5):
                xy = c.target("file", i)
                if xy:
                    c.move(xy, run=1)
            check(cf.sha256(c.card1) == h0 and c.data()["phase"] == "files", "3: hover over the saves: nothing written")
            w0, drops0 = c.writes(), c.data()["busy_drops"]

            def double():
                xy = c.target("file", 1)
                for _ in range(2):  # a double click
                    c.g.call("system", mouse=xy, button=1, down=True)
                    c.g.call("system", mouse=xy, button=1, down=False)
            c.pin_at_confirm(double)  # the events queue; ImGui takes one change a frame
            xy = c.target("file", 1)
            c.g.call("system", mouse=xy, button=1, down=True)  # a click while saving
            c.g.run(1)
            c.g.call("system", mouse=xy, button=1, down=False)
            c.wait(lambda: c.writes() > w0, "the mouse's save written")
            c.wait_phase("wait")
            d = c.data()
            check(c.writes() - w0 == 1 and d["busy_drops"] > drops0,
                  f"3: double click and a click while saving: one write ({c.writes() - w0}), busy drops "
                  f"{drops0} -> {d['busy_drops']}")
            c.shot("saved")
            c.click("status", 0)
            c.wait_phase("ops")
            c.g.call("system", mouse=c.target("op", 1), button=3, down=True)  # right click: back
            c.g.run(1)
            c.g.call("system", mouse=c.target("op", 1) or [480, 360], button=3, down=False)
            c.wait(lambda: c.current_menu() == 0, "the right click's back")
            check(c.ui()["visible"] and c.ui()["row"] == "data", "3: a right click goes back to the System list")
            c.close_menu()
            cb = c.card1.read_bytes()
            check(c.writes() - w0 == 1, f"3: still one data write after Continue, the right click and the close "
                                        f"({c.writes() - w0})")
            bad = file_bytes_only(before_c, cb, cf.save_name(2))
            check(not bad, f"3: only file 2's frames and blocks changed ({bad[:4]})")
            if a_bytes is not None:
                if cb != a_bytes:
                    print(f"info: 3: decoded, file 2 differs at {decoded_equal(a_bytes, cb, cf.save_name(2))[:4]}")
                check(cb == a_bytes, f"3: the mouse's card equals the original's (A) byte for byte (differ at "
                                     f"{changed_ranges(a_bytes, cb)[:4]})")
            c.close()

        if 5 in cases:
            spans, hashes = {}, {}
            # each fixture file has its own play time hours: the RAM after the load names the file read
            hours = cf.multi_hours(cf.stats(cf.decode(cf.read_file(bytearray(multi.read_bytes()), cf.save_name(1))))
                                   ["time"][0], 2)
            for name, modern, mouse in (("LA", False, False), ("LB", True, False), ("LC", True, True)):
                s = session(name, card2=multi, modern=modern)
                s.open_data()
                if mouse:  # Load clicked (the rows advance to card 1's list), back, card 2, file 2 clicked
                    s.click("op", 2)
                    s.wait(lambda: (s.data() or {}).get("phase") == "files" and s.data()["files_valid"],
                           "card 1's load list")
                    s.click("panel", 0, button=3)  # a right click: back to the card rows
                    s.wait_phase("card")
                    s.click("card", 2)
                    s.wait(lambda: (s.data() or {}).get("phase") == "files" and s.data()["card_sel"] == 2
                           and s.data()["files_valid"], "card 2's load list")
                    check(s.file_cursor() == 0, f"5: {name}: the load list starts on the newest ({s.file_cursor()})")
                    s.click("file", 1)
                else:
                    s.chooser(1)
                    s.card_rows(2)
                    s.wait(lambda: s.cursor()[0] == 24 and s.cursor()[2] != 0, "the load list")
                    check(s.file_cursor() == 0, f"5: {name}: the load list starts on the newest ({s.file_cursor()})")
                    s.file_rows(1)
                    s.confirm_pad()
                if modern:
                    s.wait(lambda: (s.data() or {}).get("msg") == "loaded", "loaded")
                    s.shot("loaded")
                s.wait(lambda: s.current_menu() != DATA_MENU, "the jump", limit=1200, step=2)
                u = s.ui()
                check(u.get("data") is None and not u["visible"], f"5: {name}: nothing native over the loading "
                                                                     f"screen (data {u.get('data')}, shell {u['visible']})")
                s.g.run(400)
                spans[name] = s.span()
                got = spans[name][s.sym["vs_main_gametime"] - SPAN[0] + 3]
                check(got == hours, f"5: {name}: file 2 loaded (play time hours {got}, file 2's {hours})")
                hashes[name] = s.g.call("snapshot", action="hash")
                s.g.call("snapshot", action="save", path=str((out / f"{name}.snap").resolve()))
                u = s.ui()
                check(u.get("data") is None and not u["visible"], f"5: {name}: no session nor frame after the load")
                s.close()
            for other, how in (("LB", "B native by pad"), ("LC", "C native by the mouse")):
                diff = changed_ranges(spans["LA"], spans[other])
                check(not diff, f"5: the loaded blocks equal (A original, {how}) "
                                f"{[(hex(SPAN[0] + s), e - s) for s, e in diff[:6]]}")
            # The whole state is not expected equal: the two paths differ in the menu's own state
            # (MAINMENU's row list statics and information box, the chooser's sounds and the sound
            # engine's channels and random state, MENU7's drawing in VRAM). Reported, not checked; the
            # blocks the load writes are (above). LA.snap / LB.snap: pc/tests/snapshot_diff.py.
            for k in ("ram", "scratch", "vram"):
                print(f"info: 5: snapshot hash {k} after the jump: "
                      f"{'equal' if hashes['LA'][k] == hashes['LB'][k] else 'differs (the menu paths)'}")

        if 6 in cases:
            errors(session, cf, fixtures, base, check)
    finally:
        for s in sessions:
            try:
                s.close()
            except Exception:
                pass
    return 0


def errors(session, cf, fixtures, base, check):
    # slot 2 absent, writes refused, an unformatted card (card 1)
    e = session("E1")
    h1 = cf.sha256(e.card1)
    e.open_data()
    e.chooser(0)
    e.card_rows(2)
    e.wait_phase("wait")
    d = e.data()
    check(d["msg"] == "insert_error" and d["cards"][1] == -2, f"6: card 2 absent: insertError, No card ({d['msg']})")
    w0 = e.writes()
    e.click("status", 0)
    e.wait_phase("card")
    check(e.writes() == w0, "6: dismissed (Continue), nothing written")
    e.back_out()
    e.g.call("card", fail_writes=1)
    e.close_menu()
    before = e.card1.read_bytes()
    e.open_data()
    e.chooser(0)
    e.card_rows(1)
    e.file_rows(1)
    e.press("circle", settle=4)
    e.wait_phase("wait")
    d = e.data()
    check(d["msg"] == "save_failed" and e.card1.read_bytes() == before,
          f"6: writes refused: saveFailed, the card as before ({d['msg']})")
    e.g.call("card", fail_writes=0, unformatted=1)
    e.press("circle", settle=10)
    e.back_out()
    e.close_menu()
    e.open_data()
    e.chooser(0)
    e.card_rows(1)
    e.file_rows(0)
    e.press("circle", settle=4)
    e.wait_phase("prompt")
    d = e.data()
    check(d["prompt"] == "format", f"6: an unformatted card: the format prompt ({d['prompt']}, {d['msg_text']})")
    e.press("cross", settle=4)
    e.wait_phase("wait")
    d = e.data()
    check(d["msg"] == "format_cancelled" and e.card1.read_bytes() == before,
          f"6: No: formatCancelled, the card as before ({d['msg']})")
    e.g.call("card", unformatted=0)
    e.press("circle", settle=10)
    e.back_out()
    e.close_menu()
    check(cf.sha256(e.card1) == h1, "6: card 1 as it was through the three cases")
    e.close()

    # a corrupt save (card 2: file 2 has a flipped byte)
    corrupt = cf.fixture("corrupt", base, fixtures / "corrupt.mcd")
    e = session("E2", card2=corrupt)
    e.open_data()
    ram0 = e.span()
    c0 = e.card2.read_bytes()
    e.chooser(1)
    e.card_rows(2)
    e.file_rows(1)
    e.press("circle", settle=4)
    e.wait_phase("wait")
    d = e.data()
    ram1 = e.span()
    gt = e.sym["vs_main_gametime"] - SPAN[0]
    diff = [r for r in changed_ranges(ram0, ram1) if not (r[0] >= gt and r[1] <= gt + 4)]
    check(d["msg"] == "file_corrupt" and not diff and e.card2.read_bytes() == c0,
          f"6: a corrupt save: refused ({d['msg']}), RAM (play time aside) and the card unchanged {diff[:4]}")
    e.press("circle", settle=10)
    e.wait_phase("card")
    check(True, "6: back to the card rows after the refusal (MENU7's own way back)")
    e.close()

    # a full card (card 2: another game's files, two blocks free)
    full = cf.fixture("full", base, fixtures / "full.mcd")
    e = session("E3", card2=full)
    c0 = e.card2.read_bytes()
    e.open_data()
    e.chooser(0)
    e.card_rows(2)
    e.wait_phase("wait")
    d = e.data()
    check(d["msg"] == "card_full" and e.card2.read_bytes() == c0, f"6: a full card: cardFull, unchanged ({d['msg']})")
    e.close()

    # a blank card (card 2): nothing to load; a save makes file 1
    blank = cf.fixture("blank", base, fixtures / "blank.mcd")
    e = session("E4", card2=blank)
    e.open_data()
    e.chooser(1)
    e.card_rows(2)
    e.wait_phase("wait")
    d = e.data()
    check(d["msg"] == "no_data", f"6: a blank card: noData on load ({d['msg']})")
    e.press("circle", settle=10)
    e.back_out()
    e.close_menu()
    e.open_data()
    e.chooser(0)
    e.card_rows(2)
    e.file_rows(0)
    w0 = e.writes()
    e.press("circle", settle=4)
    e.wait(lambda: e.writes() > w0, "the blank card's save")
    e.wait_phase("wait")
    img = bytearray(e.card2.read_bytes())
    check(e.data()["msg"] == "saved" and sorted(cf.saves(img)) == [1],
          f"6: a blank card: a save makes file 1 ({sorted(cf.saves(img))})")
    e.close()


if __name__ == "__main__":
    sys.exit(main())
