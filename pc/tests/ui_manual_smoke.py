#!/usr/bin/env python3
"""Modern menus, the Quick manual (pc/src/ui/ui_manual.cpp, manual_parse.c,
manual_cache.c, manual_bridge.c, ui_manual_draw.cpp): System > Quick manual
is a native reader of MENUE's topics and pages; the original MENUE stays
the only path with ui_menus off (and Square's "original screen").

    python3 pc/tests/ui_manual_smoke.py [--card pc/save/card1.mcd] [--out DIR]
        [--parity-only | --no-parity] [--diff-all]

From copies of the card (skipped without one), in the save room; the
fixture sets menuFlags bit 9 (the QUICK MANUAL command) when it is clear.

Parity (three processes, the same story; waits on state, never on time):
  A  ui_menus off: Triangle, Up to QUICK MANUAL, Circle (MENUE's list);
     Down x3, Circle (topic 3); Down x4, Square + Down, Start, Up (the wrap
     to the last line); Cross (the list); Down x9 (the window scrolls),
     Circle (topic 12), Down x2; Circle (the list), Cross (the top level:
     "top"), Triangle (closed); reopened: topic 12 at its line, Triangle from the
     page; cursor memory off: reopened, the state zeroed, Triangle
  B  ui_menus on: the same through System > Quick manual (Circle in, Cross
     out of reading then out of the topics: the shell on System / manual,
     "top"; then Square on the row: MENUE on the native topic, Cross back)
  B2 ui_menus on, the pointer (RPC ui_menu pointer, the published hits): a
     topic clicked, the wheel (a line a notch), a page chevron, the scroll
     track, a right click back, another topic clicked, the wheel, right
     clicks out
  compared after every step (the steps' names equal, B2's A's prefix):
  vs_battle_manualDisplayState (0x24 bytes),
  D_800F4EE8 (the cursor memories), vs_battle_submenuStates[0], the
  settings and the world flag D_800F190C; after the closes also the menu
  state, the HUD flags (D_800EB9AF, D_800EB9B0), text box 0's state and the
  heap's largest free block (as large as before the session). MASKS: what
  may differ, each with its reason. --diff-all: snapshots at A's and B's
  ends, every differing symbol listed (snapshot_diff.py).

Content (process B): every topic paged through (Right); each page's ten
lines equal this file's port of the tokenizer over the disc's
SMALL/HELPnn.HF0 (fixture_lib.Disc), with the lines, line count, pages,
sprite and rule counts; the labels and descriptions equal MENUE's menuText
decoded; no word over the size floor.
Sounds: the sounds the game played (RPC sfx: vs_main_playSfxDefault's
calls, bank 0x7E's change / select / leave / invalid) from the open to
every step through "top" equal A's in B; in B and B2 they equal what the
manual asked for (its JSON counters) at every step it is open, so the
bridge plays each request (B2's story is not A's: its own requests).
Button art (B): Basic controls' 38 PlayStation button sprites are drawn as
the device's prompts with a keyboard or Xbox pad, as the pixels with a
PlayStation pad.
16:9: open, scroll, close; every hit inside 1280; the canvas centred in the
card; k 2.6. 4:3 reading: k 2.6 and no rail hits.
Robustness: a soft reset while reading (manual null, Continue works, the
manual again), a forced currentMenu write (the session abandoned, the world
flag back), ui_menus switched off mid-session (the session keeps its
owner).
Frames: a blinking illustration (Simple map) toggles "hidden"; the canvas
shows Basic controls' text and pictures (bright pixels in a screenshot,
kept in the out directory); 4:3 reading at k 2.6 without rail hits."""
import argparse
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, BUILD, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

CLOSED = 0x3F
MANUAL_MENU = 10 | 0x40
QUICK_MANUAL = 9  # the top level's command (menuFlags bit 9)
MENUE_SECTION = "rood_ovl_menu_menue_prg"
MENUE_STATICS = ("_showMenuState", "_scrollPosition", "_lineCount")
SHOW_LIST, SHOW_PAGE = 2, 5  # _showMenu's getSelection, showSubMenu
TEXTBOX_BYTES, TEXTBOX_STATE = 0x30, 0x1C
TOPICS = 14
SOUNDS = ("change", "select", "leave", "invalid")
SOUND_STEPS = ("topic3", "steps", "list", "topic12", "top")  # A's and B's sounds compared (from "open")

# Masked: (region, start, end, reason). The regions are compared at every
# step ("core") or after a close ("close"); see compare().
MASKS = []


def menue_symbols():
    """MENUE's statics: each name must resolve to one address in its overlay."""
    image = BUILD / "rood_battle_probe"
    out = subprocess.run([os.environ.get("ROOD_OBJDUMP", "objdump"), "-t", str(image)], capture_output=True,
                         text=True).stdout
    found = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 6 and parts[-1] in MENUE_STATICS and parts[-3].startswith(MENUE_SECTION):
            found.setdefault(parts[-1], []).append(int(parts[0], 16))
    bad = [n for n in MENUE_STATICS if len(found.get(n, [])) != 1]
    if bad:
        raise RuntimeError(f"MENUE statics not resolved to one address: {bad} ({found})")
    return {n: a[0] for n, a in found.items()}


# ---- the reference: the disc's page files as the reader shows them ----
def charset():
    """ui_text.c's charset (the game's glyph indices 0..188 -> code points; 0 none)."""
    src = (ROOT / "pc/src/ui_text.c").read_text()
    body = re.search(r"static const uint16_t charset\[189\] = \{(.*?)\};", src, re.S).group(1)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    out = []
    for m in re.finditer(r"'(\\.|[^'])'|0x[0-9A-Fa-f]+|\d+", body):
        tok = m.group(0)
        out.append(ord(m.group(1).encode().decode("unicode_escape")) if tok.startswith("'") else int(tok, 0))
    assert len(out) == 189, len(out)
    return out


CHARSET = None


def decode(raw):
    """pc_ui_text_decode: blanks collapsed, codes skipped (0xEC.. but 0xFC take an operand)."""
    out = ""
    i = 0
    while i < len(raw):
        c = raw[i]
        i += 1
        if c == 0xE7:
            break
        cp = 0
        if c in (0x8F, 0xE8):
            cp = 32
        elif c == 0xFA:
            cp = 32
            i += 1
        elif c >= 0xEC and c != 0xFC:
            i += 1
        elif c < 189:
            cp = CHARSET[c]
        if not cp or (cp == 32 and (not out or out[-1] == " ")):
            continue
        out += chr(cp)
    return out.rstrip(" ")


BUTTONS = {0xAE: "[triangle]", 0xAF: "[square]", 0xB0: "[circle]", 0xB1: "[cross]"}
ARROWS = {0xB2: "←", 0xB3: "→", 0xB4: "↑", 0xB5: "↓"}


def page_line(raw):
    """manual_parse.c's runs of one line, as manual_cache.c's page JSON joins them."""
    parts, word = [], b""
    i = 0

    def end():
        nonlocal word
        if word:
            text = decode(word + b"\xe7")
            if text:
                parts.append(text)
        word = b""
    while i < len(raw):
        c = raw[i]
        if c == 0xE7:
            break
        if c >= 0xEC:
            end()
            i += 2
            continue
        if c >= 0xE5 or c == 0x8F:
            end()
            i += 1
            continue
        if 0xAD <= c <= 0xB8:
            end()
            if c in BUTTONS:
                parts.append(BUTTONS[c])
            elif c in ARROWS:
                parts.append(ARROWS[c])
            elif c == 0xB7:
                parts.append("★")
            i += 1
            continue
        word += bytes([c])
        i += 1
    end()
    return " ".join(parts)


def topic_reference(disc, t):
    hf0 = disc.read(f"SMALL/HELP{t + 1:02d}.HF0")
    spr, lin, lin_size, _ = struct.unpack_from("<IIII", hf0, 0)
    n = struct.unpack_from("<H", hf0, 16)[0]
    lines = []
    for i in range(n):
        off = 16 + struct.unpack_from("<H", hf0, 18 + 2 * i)[0] * 2
        lines.append(page_line(hf0[off:hf0.index(0xE7, off) + 1]))
    base = 16 + spr
    sprites = struct.unpack_from("<H", hf0, base)[0]
    rules = struct.unpack_from("<H", hf0, base + lin)[0] if lin_size > 0 else 0
    count = max(0, -(-n // 10) * 10 - 10)
    return {"lines": lines, "line_count": count, "pages": count // 10 + 1, "sprites": sprites, "rules": rules}


def menu_text():
    """MENUE's menuText (the build's extracted table): 14 names, 14 lines."""
    data = (ROOT / "build/assets/MENU/MENUE.PRG/menuText.vsString.bin").read_bytes()
    out = []
    for i in range(2 * TOPICS):
        off = struct.unpack_from("<H", data, 2 * i)[0] * 2
        out.append(decode(data[off:]))
    return out


# ---- a game process ----
class Session:
    def __init__(self, name, out, card, env, shots):
        self.name, self.out, self.shots = name, out, shots
        self.game = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env)
        self.game.call("graphics", interpolate=False)
        sym = symbols()
        self.sym = sym
        self.menu_state = sym["vs_battle_menuState"]
        self.settings = sym["vs_main_settings"]
        self.display = sym["vs_battle_manualDisplayState"]
        self.items_row = sym["vs_mainMenu_itemsListRow"]
        self.me = menue_symbols()

    def close(self):
        self.game.close()

    # ---- reads ----
    def shell(self):
        return self.game.call("ui_menu")

    def manual(self):
        return (self.shell().get("system") or {}).get("manual")

    def current_menu(self):
        return self.game.read(self.menu_state, 1)[0]

    def u32(self, addr):
        return struct.unpack("<I", self.game.read(addr, 4))[0]

    def menue(self, name):
        return struct.unpack("<i", self.game.read(self.me[name], 4))[0]

    def scroll(self, t):
        return struct.unpack("<H", self.game.read(self.display + 4 + 2 * t, 2))[0]

    def current(self):
        return struct.unpack("<H", self.game.read(self.display, 2))[0]

    def world(self):
        return self.u32(self.sym["D_800F190C"])

    def played(self):
        """The menu sounds the game played (RPC sfx: vs_main_playSfxDefault, bank 0x7E)."""
        r = self.game.call("sfx")
        return {k: r[k] for k in SOUNDS}

    def asked(self):
        """What the native manual asked the bridge to play (its JSON counters; None off the System tab)."""
        m = (self.shell().get("system") or {}).get("manual_sfx")
        return {k: m.get(k, 0) for k in SOUNDS} if m else None

    def dump(self):
        g, s = self.game, self.sym
        return {
            "display": g.read(self.display, 0x24),
            "cursorMemories": g.read(s["D_800F4EE8"], 52),
            "submenu0": g.read(s["vs_battle_submenuStates"], 1),
            "settings": g.read(self.settings, 32),
            "world": g.read(s["D_800F190C"], 4),
            # close only
            "menuState": g.read(self.menu_state, 4),
            "hud": g.read(s["D_800EB9AF"], 1) + g.read(s["D_800EB9B0"], 4),
            "textBox0": g.read(s["vs_battle_textBoxes"] + TEXTBOX_STATE, 2),
            # the sounds played so far and those the manual asked for (compare_sounds, check_asked)
            "played": self.played(),
            "asked": self.asked(),
        }

    def heap(self):
        return self.shell().get("map_heap_free", 0)

    def shot(self, name):
        if self.shots:
            self.game.present_screenshot(self.out / f"{self.name}-{name}.png")

    # ---- the fixture ----
    def fixture(self, cursor_memory=1):
        g = self.game
        flags = struct.unpack("<H", g.read(self.settings + 2, 2))[0]
        if not flags & (1 << QUICK_MANUAL):
            g.write(self.settings + 2, struct.pack("<H", flags | 1 << QUICK_MANUAL))
        g.write(self.settings + 1, bytes([cursor_memory]))

    # ---- waits (state) ----
    def wait(self, pred, limit=600, step=2):
        for _ in range(limit // step):
            v = pred()
            if v:
                return v
            self.game.run(step)
        return pred()

    def wait_list(self):
        """MENUE's list takes input (its rows in: MAINMENU 58EC.c case 17)."""
        return self.wait(lambda: self.current_menu() == MANUAL_MENU and self.menue("_showMenuState") == SHOW_LIST
                         and self.game.read(self.items_row, 1)[0] == 17)

    def wait_page(self):
        return self.wait(lambda: self.current_menu() == MANUAL_MENU and self.menue("_showMenuState") == SHOW_PAGE)

    def wait_closed(self):
        ok = self.wait(lambda: self.current_menu() == CLOSED and not self.shell()["visible"], 600, 5)
        self.game.run(20)
        return ok

    def wait_top(self):
        """The original top level takes input (rows slid in: D_800F4CC0 >= 0)."""
        return self.wait(lambda: self.current_menu() == 0 and struct.unpack(
            "<i", self.game.read(self.sym["D_800F4CC0"], 4))[0] >= 0 and self.game.read(
            self.sym["vs_battle_menuItems"], 4) != b"\0\0\0\0")

    def wait_shell(self):
        ok = self.wait(lambda: self.shell()["visible"] and self.current_menu() == 0, 300, 3)
        self.game.run(20)  # the shell takes the pad once it has settled
        return ok

    def wait_manual(self, pred, limit=120):
        return self.wait(lambda: (lambda m: m if m and pred(m) else None)(self.manual()), limit, 1)

    # ---- the original's path ----
    def open_original(self):
        g = self.game
        g.press("triangle", vsyncs=3, settle=10)
        self.wait_top()
        for _ in range(12):
            if g.read(self.sym["vs_battle_submenuStates"], 1)[0] == QUICK_MANUAL:
                break
            g.press("up", vsyncs=3, settle=6)
        g.press("circle", vsyncs=3, settle=4)
        return self.wait_list()

    # ---- the shell's path ----
    def open_shell(self):
        g = self.game
        g.press("triangle", vsyncs=3, settle=4)
        self.wait_shell()
        for _ in range(8):
            if self.shell()["tab"] == "system":
                break
            g.press("l1", vsyncs=3, settle=10)
        for _ in range(16):
            if self.shell().get("row") == "manual":
                break
            g.press("down", vsyncs=3, settle=6)
        g.press("circle", vsyncs=3, settle=4)
        return self.wait_manual(lambda m: m["mode"] == "topics" and m["loaded"] == 1)

    # ---- the pointer (layout units) ----
    def ptr(self, run=3, **p):
        self.game.call("ui_menu", pointer=p)
        self.game.run(run)

    def hit(self, kind, index=None):
        m = self.manual()
        for h in reversed(m["hits"] if m else []):
            if h["kind"] == kind and (index is None or h["index"] == index):
                return h
        return None

    def click(self, kind, index, run=3):
        h = self.hit(kind, index)
        if not h:
            return False
        self.ptr(run, x=(h["x0"] + h["x1"]) / 2, y=(h["y0"] + h["y1"]) / 2, click=True)
        return True

    def click_line(self, line, run=3):
        """The scroll track at `line` (ui_manual_draw.cpp's mapping)."""
        m = self.manual()
        h = self.hit("scroll")
        if not h or not m["line_count"]:
            return False
        lc = m["line_count"]
        th = (h["y1"] - h["y0"]) * 10 / (lc + 10)
        y = h["y0"] + th / 2 + line / lc * (h["y1"] - h["y0"] - th)
        self.ptr(run, x=(h["x0"] + h["x1"]) / 2, y=y, click=True)
        return True

    def wheel(self, notches, run=3):
        h = self.hit("page", 1) or self.hit("canvas")
        for _ in range(abs(notches)):
            self.ptr(run, x=10, y=400, wheel=-1 if notches > 0 else 1)  # down: negative (ImGui)
        return h


def diff(a, b, regions):
    found = []
    for region in regions:
        x, y = a[region], b[region]
        if len(x) != len(y):
            found.append((region, 0, max(len(x), len(y))))
            continue
        i = 0
        while i < len(x):
            if x[i] != y[i] and not any(r == region and s <= i < e for r, s, e, _ in MASKS):
                j = i
                while j < len(x) and x[j] != y[j]:
                    j += 1
                found.append((region, i, j))
                i = j
            else:
                i += 1
    return found


def describe(a, b, rest):
    return ", ".join(f"{r} {s:#x}..{e:#x} {a[r][s:e][:16].hex()} vs {b[r][s:e][:16].hex()}" for r, s, e in rest[:8])


CORE = ("display", "cursorMemories", "submenu0", "settings", "world")
CLOSE = CORE + ("menuState", "hud", "textBox0")


def compare(check, label, dumps_a, dumps_b):
    names_a, names_b = [n for n, _ in dumps_a], [n for n, _ in dumps_b]
    if not check(names_a == names_b, f"parity {label}: the same steps ({names_a} vs {names_b})"):
        return
    for (name, da), (_, db) in zip(dumps_a, dumps_b):
        regions = CLOSE if name.startswith("close") else CORE
        rest = diff(da, db, regions)
        check(not rest, f"parity {label} {name}: equal" + (f" (differs: {describe(da, db, rest)})" if rest else ""))


def delta(d, base):
    return {k: d["played"][k] - base["played"][k] for k in SOUNDS}


def compare_sounds(check, label, dumps_a, dumps_b):
    """The sounds played from the open to each manual step, A's against the native path's."""
    a, b = dict(dumps_a), dict(dumps_b)
    if "open" not in a or "open" not in b:
        check(False, f"sounds {label}: no open step")
        return
    for name in SOUND_STEPS:
        if name in a and name in b:
            da, db = delta(a[name], a["open"]), delta(b[name], b["open"])
            check(da == db, f"sounds {label} {name}: played as the original ({db}, original {da})")


def check_asked(check, label, dumps):
    """Through the manual's own steps (the open to "top"): the sounds played since the open equal those it
    asked for (the bridge plays every request, each as the right sound)."""
    steps = dict(dumps)
    base = steps.get("open")
    if not base or not base["asked"]:
        check(False, f"sounds {label}: the open step has the manual's counters")
        return
    for name in SOUND_STEPS:
        d = steps.get(name)
        if not d or not d["asked"]:
            check(False, f"sounds {label} {name}: the step and the manual's counters")
            continue
        played = delta(d, base)
        asked = {k: d["asked"][k] - base["asked"][k] for k in SOUNDS}
        check(played == asked, f"sounds {label} {name}: played what the manual asked ({played}, asked {asked})")


def snapshot(s, path):
    s.game.call("snapshot", action="save", path=str(path.resolve()))


# ---- the stories ----
def story_original(a, check, dumps):
    g = a.game
    g.call("ui_menu", modern=False)
    heap0 = a.heap()
    check(a.open_original(), f"A: Triangle, QUICK MANUAL, Circle: MENUE's list ({a.current_menu():#x})")
    dumps.append(("open", a.dump()))
    for _ in range(3):
        g.press("down", vsyncs=3, settle=4)
    g.press("circle", vsyncs=3, settle=4)
    check(a.wait_page() and a.current() == 3, f"A: topic 3's page ({a.current()})")
    dumps.append(("topic3", a.dump()))
    start = a.scroll(3)
    for _ in range(4):
        g.press("down", vsyncs=3, settle=4)
    g.hold(["square", "down"], 3)
    g.run(4)
    g.press("start", vsyncs=3, settle=4)
    g.press("up", vsyncs=3, settle=4)
    check(a.scroll(3) == a.menue("_lineCount") > 0, f"A: the page steps end on the last line ({start} -> "
                                                     f"{a.scroll(3)}, {a.menue('_lineCount')})")
    dumps.append(("steps", a.dump()))
    g.press("cross", vsyncs=3, settle=4)
    check(a.wait_list(), "A: Cross: the list")
    dumps.append(("list", a.dump()))
    for _ in range(9):
        g.press("down", vsyncs=3, settle=4)
    g.press("circle", vsyncs=3, settle=4)
    check(a.wait_page() and a.current() == 12, f"A: Down x9, Circle: topic 12 ({a.current()})")
    for _ in range(2):
        g.press("down", vsyncs=3, settle=4)
    dumps.append(("topic12", a.dump()))
    g.press("circle", vsyncs=3, settle=4)
    check(a.wait_list(), "A: Circle: the list")
    g.press("cross", vsyncs=3, settle=4)
    check(a.wait(lambda: a.current_menu() == 0, 300), f"A: Cross: the top level ({a.current_menu():#x})")
    a.wait_top()
    dumps.append(("top", a.dump()))
    g.press("triangle", vsyncs=3, settle=4)
    check(a.wait_closed(), f"A: Triangle closes ({a.current_menu():#x})")
    dumps.append(("close", a.dump()))
    check(a.heap() >= heap0, f"A: the heap as before ({a.heap()} >= {heap0})")
    # reopened: topic 12 at its line; Triangle from the page
    line12 = a.scroll(12)
    a.open_original()
    g.press("circle", vsyncs=3, settle=4)
    check(a.wait_page() and a.current() == 12 and a.menue("_scrollPosition") == line12,
          f"A: reopened on topic 12 at line {line12} ({a.current()}, {a.menue('_scrollPosition')})")
    g.press("triangle", vsyncs=3, settle=4)
    check(a.wait_closed(), f"A: Triangle from the page closes ({a.current_menu():#x})")
    dumps.append(("close-reopened", a.dump()))
    # cursor memory off: the state starts over
    a.fixture(cursor_memory=0)
    a.open_original()
    check(a.current() == 0 and a.scroll(12) == 0, f"A: no cursor memory: zeroed ({a.current()}, {a.scroll(12)})")
    dumps.append(("open-zeroed", a.dump()))
    g.press("triangle", vsyncs=3, settle=4)
    check(a.wait_closed(), "A: Triangle closes")
    dumps.append(("close-zeroed", a.dump()))
    a.fixture(cursor_memory=1)


def story_pad(b, check, dumps):
    g = b.game
    g.call("ui_menu", modern=True)
    heap0 = b.heap()
    m = b.open_shell()
    check(m and m["mode"] == "topics", "B: System > Quick manual: the topics")
    dumps.append(("open", b.dump()))
    for _ in range(3):
        g.press("down", vsyncs=3, settle=4)
    g.press("circle", vsyncs=3, settle=4)
    m = b.wait_manual(lambda m: m["mode"] == "reading" and m["topic"] == 3)
    check(m is not None, "B: topic 3 read")
    dumps.append(("topic3", b.dump()))
    for _ in range(4):
        g.press("down", vsyncs=3, settle=4)
    g.hold(["square", "down"], 3)
    g.run(4)
    g.press("start", vsyncs=3, settle=4)
    g.press("up", vsyncs=3, settle=4)
    m = b.manual()
    check(m["line"] == m["line_count"] > 0, f"B: the steps end on the last line ({m['line']}, {m['line_count']})")
    dumps.append(("steps", b.dump()))
    g.press("cross", vsyncs=3, settle=4)
    check(b.wait_manual(lambda m: m["mode"] == "topics"), "B: Cross: the topics")
    dumps.append(("list", b.dump()))
    for _ in range(9):
        g.press("down", vsyncs=3, settle=4)
    g.press("circle", vsyncs=3, settle=4)
    m = b.wait_manual(lambda m: m["mode"] == "reading" and m["topic"] == 12)
    check(m is not None and m["row"] == 7 and m["page_top"] == 5, f"B: Down x9, Circle: topic 12 (window "
                                                                   f"{m and (m['row'], m['page_top'])})")
    for _ in range(2):
        g.press("down", vsyncs=3, settle=4)
    dumps.append(("topic12", b.dump()))
    g.press("circle", vsyncs=3, settle=4)
    check(b.wait_manual(lambda m: m["mode"] == "topics") is not None, "B: Circle: the topics")
    g.press("cross", vsyncs=3, settle=4)
    b.wait(lambda: b.manual() is None, 60, 1)
    b.game.run(4)
    s = b.shell()
    check(s["visible"] and s["tab"] == "system" and s.get("row") == "manual" and s["system"]["manual"] is None,
          f"B: Cross: the shell on System / manual ({s['tab']}, {s.get('row')})")
    dumps.append(("top", b.dump()))  # the native Cross out of the topics, against MENUE's to the top level
    # Square on the row: the original MENUE on the native topic; Cross back to the shell
    g.press("square", vsyncs=3, settle=4)
    ok = b.wait_list()
    cm = b.game.read(b.sym["D_800F4EE8"] + 4, 2)
    check(ok and b.current() == 12 and tuple(cm) == (7, 5),
          f"B: Square: MENUE's list on topic 12 ({b.current_menu():#x}, {b.current()}, {tuple(cm)})")
    g.press("cross", vsyncs=3, settle=4)
    b.wait_shell()
    s = b.shell()
    check(s["visible"] and s["tab"] == "system" and s.get("row") == "manual",
          f"B: Cross from MENUE: the shell on System / manual ({s['tab']}, {s.get('row')})")
    g.press("triangle", vsyncs=3, settle=4)
    check(b.wait_closed(), f"B: Triangle closes ({b.current_menu():#x})")
    dumps.append(("close", b.dump()))
    check(b.heap() >= heap0, f"B: the heap as before ({b.heap()} >= {heap0})")
    # reopened: topic 12 at its line; Triangle from the page
    line12 = b.scroll(12)
    m = b.open_shell()
    g.press("circle", vsyncs=3, settle=4)
    m = b.wait_manual(lambda m: m["mode"] == "reading")
    check(m and m["topic"] == 12 and m["line"] == line12, f"B: reopened on topic 12 at line {line12} "
                                                          f"({m and (m['topic'], m['line'])})")
    g.press("triangle", vsyncs=3, settle=4)
    check(b.wait_closed(), "B: Triangle from the page closes")
    dumps.append(("close-reopened", b.dump()))
    b.fixture(cursor_memory=0)
    m = b.open_shell()
    check(m and m["topic"] == 0 and b.scroll(12) == 0, f"B: no cursor memory: zeroed ({m and m['topic']})")
    dumps.append(("open-zeroed", b.dump()))
    g.press("triangle", vsyncs=3, settle=4)
    check(b.wait_closed(), "B: Triangle closes")
    dumps.append(("close-zeroed", b.dump()))
    b.fixture(cursor_memory=1)


def story_pointer(b2, check, dumps):
    g = b2.game
    g.call("ui_menu", modern=True)
    b2.open_shell()
    dumps.append(("open", b2.dump()))
    ok = b2.click("topic", 3)
    m = b2.wait_manual(lambda m: m["mode"] == "reading" and m["topic"] == 3)
    check(ok and m is not None, "B2: topic 3 clicked: read")
    dumps.append(("topic3", b2.dump()))
    start = m["line"] if m else 0
    b2.wheel(4)
    m = b2.manual()
    check(m["line"] == min(start + 4, m["line_count"]), f"B2: four wheel notches, four lines ({start} -> "
                                                        f"{m['line']})")
    b2.click("page", 1)
    m = b2.manual()
    check(m["line"] == min((start + 4) // 10 * 10 + 10, m["line_count"]), f"B2: the next page ({m['line']})")
    b2.click_line(0)
    b2.click_line(b2.manual()["line_count"])
    m = b2.manual()
    check(m["line"] == m["line_count"], f"B2: the track at the end ({m['line']}, {m['line_count']})")
    dumps.append(("steps", b2.dump()))
    b2.ptr(3, x=10, y=400, right=True)
    check(b2.wait_manual(lambda m: m["mode"] == "topics"), "B2: a right click: the topics")
    dumps.append(("list", b2.dump()))
    ok = b2.click("topic", 12)
    m = b2.wait_manual(lambda m: m["mode"] == "reading" and m["topic"] == 12)
    check(ok and m and m["row"] == 7 and m["page_top"] == 5, f"B2: topic 12 clicked (window "
                                                               f"{m and (m['row'], m['page_top'])})")
    b2.wheel(2)
    dumps.append(("topic12", b2.dump()))
    b2.ptr(3, x=10, y=400, right=True)
    b2.wait_manual(lambda m: m["mode"] == "topics")
    b2.ptr(3, x=10, y=400, right=True)
    s = b2.shell()
    check(s["visible"] and s.get("row") == "manual" and s["system"]["manual"] is None,
          "B2: right clicks out: the System list on the manual")
    dumps.append(("top", b2.dump()))
    g.press("triangle", vsyncs=3, settle=4)
    check(b2.wait_closed(), "B2: Triangle closes")
    dumps.append(("close", b2.dump()))


# ---- process B continues ----
def content_checks(b, check):
    import fixture_lib
    disc = fixture_lib.Disc()
    g = b.game
    texts = menu_text()
    m = b.open_shell()
    for _ in range(TOPICS):  # to topic 0
        if m["topic"] == 0:
            break
        g.press("up", vsyncs=3, settle=2)
        m = b.manual()
    bad = []
    for t in range(TOPICS):
        ref = topic_reference(disc, t)
        m = b.manual()
        if m["topic"] != t:
            bad.append(f"topic {t}: at {m['topic']}")
            break
        if m["label"] != texts[t] or m["desc"] != texts[TOPICS + t]:
            bad.append(f"topic {t}: label / desc ({m['label']!r}, {m['desc']!r})")
        g.press("circle", vsyncs=3, settle=2)
        m = b.wait_manual(lambda m: m["mode"] == "reading" and m["loaded"] == 1)
        g.press("start", vsyncs=3, settle=2)  # the top
        for key in ("line_count", "pages", "sprites", "rules"):
            if m[key] != ref[key]:
                bad.append(f"topic {t}: {key} {m[key]} != {ref[key]}")
        if m["lines"] != len(ref["lines"]) or m["overflow"] or m["unknown"]:
            bad.append(f"topic {t}: lines {m['lines']}, overflow {m['overflow']}, unknown {m['unknown']}")
        for page in range(ref["pages"]):
            m = b.manual()
            want = (ref["lines"] + [""] * 10)[page * 10:page * 10 + 10]
            if m["line"] != page * 10 or m["page_lines"] != want:
                diffs = [(i, a, w) for i, (a, w) in enumerate(zip(m["page_lines"], want)) if a != w][:2]
                bad.append(f"topic {t} page {page}: line {m['line']}, {diffs}")
                break
            g.press("right", vsyncs=3, settle=2)
        g.press("start", vsyncs=3, settle=2)
        g.press("cross", vsyncs=3, settle=2)
        g.press("down", vsyncs=3, settle=2)
    check(not bad, "content: every page of the 14 topics is the disc's text; labels, counts" +
          (f" ({'; '.join(bad[:6])})" if bad else ""))
    g.press("triangle", vsyncs=3, settle=4)
    b.wait_closed()


def walk_to(b, check, topic, what):
    """The topics' cursor onto `topic` (Up / Down presses, bounded); the manual's state or None."""
    g = b.game
    m = b.manual()
    for _ in range(TOPICS + 2):
        if not m or m["mode"] != "topics" or m["topic"] == topic:
            break
        g.press("down" if m["topic"] < topic else "up", vsyncs=3, settle=2)
        m = b.manual()
    ok = m is not None and m["mode"] == "topics" and m["topic"] == topic
    check(ok, f"{what}: the topics' cursor on topic {topic} ({m and (m['mode'], m['topic'])})")
    return m if ok else None


def wait_k(b, k):
    """The 4:3 card slides in real time: the manual once its k is reached (or the last read)."""
    m = b.manual()
    for _ in range(40):
        if not m or abs(m["k"] - k) < 0.001:
            break
        time.sleep(0.05)
        b.game.run(2)
        m = b.manual()
    return m


def frame_checks(b, check, shots):
    g = b.game
    import numpy as np
    from PIL import Image
    m = b.open_shell()
    check(m is not None, "frames: the manual opens")
    # a blinking illustration (Simple map, HELP12): "hidden" changes as the ticks go
    if walk_to(b, check, 11, "frames"):
        g.press("circle", vsyncs=3, settle=2)
        seen = set()
        for _ in range(40):
            g.run(2)
            m = b.manual()
            if m:
                seen.add(m["hidden"])
        check(len(seen) > 1, f"frames: a blinking illustration toggles ({sorted(seen)})")
        g.press("cross", vsyncs=3, settle=2)
    # 4:3 reading: the rail steps aside, the canvas at k 2.6
    if walk_to(b, check, 0, "frames"):
        g.press("circle", vsyncs=3, settle=2)
        m = wait_k(b, 2.6)
        topics = [h for h in m["hits"] if h["kind"] == "topic"] if m else None
        check(m is not None and abs(m["k"] - 2.6) < 0.001 and not topics,
              f"4:3 reading: k {m and m['k']}, rail hits {topics and len(topics)}")
        # the canvas draws: Basic controls' text and pictures (a screenshot in the out directory, always)
        path = b.out / "B-basic-controls.png"
        g.present_screenshot(path)
        image = np.asarray(Image.open(path).convert("RGB")).astype(int)
        page = [h for h in m["hits"] if h["kind"] == "page"] if m else []
        x0, x1 = 100, int(page[0]["x0"]) if page else 600
        sy, sx = image.shape[0] / 720, image.shape[1] / 960
        crop = image[int(220 * sy):int(500 * sy), int(x0 * sx):int(x1 * sx)]
        bright = int((crop.max(axis=2) > 150).sum())
        check(bright > 500, f"frames: Basic controls' text and pictures in the canvas ({bright} bright pixels)")
        art_checks(b, check)
    g.press("triangle", vsyncs=3, settle=4)
    b.wait_closed()


def art_checks(b, check):
    """Basic controls (reading): its PlayStation button sprites follow the device (button_prompts auto)."""
    g = b.game
    g.call("button_prompts", button_prompts="auto")
    seen = {}
    for device in ("playstation", "keyboard", "xbox", "playstation"):
        g.call("button_prompts", device=device)
        g.run(3)
        m = b.manual()
        seen[device] = m and (m["art"], m["art_prompts"])
        if device != "playstation":
            b.shot(f"art-{device}")
    check(seen["playstation"] == (38, False) and seen["keyboard"] == (38, True) and seen["xbox"] == (38, True),
          f"art: Basic controls' 38 button sprites as the device's prompts off a PlayStation pad ({seen})")


def wide_checks(b, check):
    g = b.game
    g.call("graphics", widescreen=True)
    g.run(10)
    m = b.open_shell()
    for _ in range(40):  # the card slides in real time
        time.sleep(0.05)
        g.run(2)
        m = b.manual()
    hits = m["hits"]
    canvas = next((h for h in hits if h["kind"] == "canvas"), None)
    check(hits and all(0 <= h["x0"] and h["x1"] <= 1280 for h in hits), f"16:9: every hit inside 1280 ({len(hits)})")
    # the card: from the rail's right (56 + 300 + 28) to 1280 - 56, its text from 22 in
    centre = (384 + 22 + 1280 - 56 - 22) / 2
    check(canvas and abs((canvas["x0"] + canvas["x1"]) / 2 - centre) < 1 and abs(m["k"] - 2.6) < 0.001,
          f"16:9: the canvas centred in the card at k 2.6 ({canvas and (canvas['x0'], canvas['x1'])}, {m['k']})")
    g.press("circle", vsyncs=3, settle=2)
    g.press("down", vsyncs=3, settle=2)
    m = b.manual()
    check(m["mode"] == "reading" and m["line"] >= 1 and any(h["kind"] == "topic" for h in m["hits"]),
          "16:9: reading keeps the rail")
    b.shot("wide-reading")
    g.press("triangle", vsyncs=3, settle=4)
    check(b.wait_closed(), "16:9: Triangle closes")
    g.call("graphics", widescreen=False)
    g.run(10)


def robustness(b, check):
    g = b.game
    # ui_menus switched off mid-session: the session keeps its owner
    m = b.open_shell()
    g.call("ui_menu", modern=False)
    g.press("down", vsyncs=3, settle=2)
    m2 = b.manual()
    check(m and m2 and m2["topic"] != m["topic"], "robust: ui_menus off mid-session: the manual still runs "
          f"({m and m['topic']} -> {m2 and m2['topic']})")
    g.press("triangle", vsyncs=3, settle=4)
    check(b.wait_closed() and b.world() == 1, "robust: and closes (the world runs)")
    g.call("ui_menu", modern=True)
    # a soft reset while reading: manual null, Continue works, the manual again
    titles = pathlib.Path(g.log).read_text().count("TITLE MENU READY")
    b.open_shell()
    g.press("circle", vsyncs=3, settle=2)
    g.call("input", buttons=["l1", "l2", "r1", "r2", "start", "select"], vsyncs=6)
    for _ in range(100):
        g.run(30)
        if pathlib.Path(g.log).read_text().count("TITLE MENU READY") > titles:
            break
    s = b.shell()
    check(not s["visible"] and (s.get("system") or {}).get("manual") is None,
          "robust: a soft reset while reading: the title, manual null")
    g.press("start", vsyncs=4, settle=90)
    for _ in range(3):
        g.press("circle", vsyncs=3, settle=90)
    g.run(400)
    b.fixture()
    g.call("ui_menu", modern=True)
    m = b.open_shell()
    check(m is not None and b.world() == 0, "robust: Continue after the reset: the manual opens (the world frozen)")
    g.press("triangle", vsyncs=3, settle=4)
    check(b.wait_closed() and b.world() == 1, "robust: and closes (the world runs)")
    # a forced currentMenu write (the last case: the game's menu is left mid-run): the session is
    # abandoned, the world flag back
    b.open_shell()
    g.press("circle", vsyncs=3, settle=2)
    frozen = b.world()
    g.write(b.menu_state, bytes([CLOSED]))
    g.run(6)
    check(frozen == 0 and b.manual() is None and b.world() == 1,
          f"robust: a forced currentMenu write ends the manual, the world runs ({frozen} -> {b.world()})")


def main():
    global CHARSET
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    parser.add_argument("--parity-only", action="store_true", help="only the parity stories")
    parser.add_argument("--no-parity", action="store_true", help="only the content, frame and robustness checks")
    parser.add_argument("--diff-all", action="store_true", help="snapshot A's and B's ends and list every difference")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    CHARSET = charset()
    out = pathlib.Path(args.out) if args.out else OUT / "ui_manual"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_DETERMINISTIC": "1"}
    os.environ.pop("ROOD_SETTINGS", None)
    shots = bool(args.out)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    dumps = {}
    if not args.no_parity:
        shutil.copyfile(args.card, card)
        a = Session("A", out, card, env, shots)
        try:
            a.fixture()
            dumps["A"] = []
            story_original(a, check, dumps["A"])
            if args.diff_all:
                snapshot(a, out / "A.snap")
        finally:
            a.close()

    shutil.copyfile(args.card, card)
    b = Session("B", out, card, env, shots)
    try:
        b.fixture()
        if not args.no_parity:
            dumps["B"] = []
            story_pad(b, check, dumps["B"])
            if args.diff_all:
                snapshot(b, out / "B.snap")
            compare(check, "A/B", dumps["A"], dumps["B"])
            compare_sounds(check, "A/B", dumps["A"], dumps["B"])
            check_asked(check, "B", dumps["B"])
        if not args.parity_only:
            b.game.call("ui_menu", modern=True)
            content_checks(b, check)
            frame_checks(b, check, shots)
            wide_checks(b, check)
            robustness(b, check)
    finally:
        b.close()

    if not args.no_parity and args.diff_all:
        subprocess.run([sys.executable, str(ROOT / "pc/tests/snapshot_diff.py"), str(out / "A.snap"),
                        str(out / "B.snap"), "--binary", str(BUILD / "rood_battle_probe")])

    if not args.no_parity:
        shutil.copyfile(args.card, card)
        b2 = Session("B2", out, card, env, shots)
        try:
            b2.fixture()
            dumps["B2"] = []
            story_pointer(b2, check, dumps["B2"])
            compare(check, "A/B2", dumps["A"][:len(dumps["B2"])], dumps["B2"])
            check_asked(check, "B2", dumps["B2"])
        finally:
            b2.close()

    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("ui_manual_smoke: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
