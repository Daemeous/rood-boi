#!/usr/bin/env python3
"""Modern menus, the native Score screen (pc/src/ui/ui_score.cpp, score_bridge.c,
score_format.c, score_nav.c, mon_parse.c, score_mon.c, ui_score_draw.cpp):
System > Score is native; MENU9 (src/MENU/MENU9.PRG/238.c) stays the only path
with ui_menus off, and Square's "original screen".

    python3 pc/tests/ui_score_smoke.py [--card CARD] [--out DIR] [--only CASES]

Card-safe: every game runs under card_fixtures.card_guard (ROOD_CARD1 a
temporary copy; the player's cards hashed unchanged) and the card copy's own
hash is checked unchanged too (Score writes RAM only). Skipped (77) without
a card. From the save room checkpoint; the fixtures (card_fixtures.score_fixture:
fresh, midgame, cleared, maxed) are written into vs_main_scoredata /
vs_main_stateFlags before each open (the card's save never loads).

Cases (A: ui_menus off, MENU9 by pad; B: native by pad; C: native by pointer):
  1 open     three opens (cursor memory on with roomFlags[1] bit 0x800000;
             off with the menu9 cursor memory pre-seeded; on, pre-seeded):
             the whole vs_main_scoredata block, roomFlags[16],
             menu9CursorMemory, cursorMemories[2..3] and settings.cursorMemory
             equal A's; B's JSON score / rank equal A's _score /
             _characterRank (and B's own statics)
  2 moves    the same pad script per section (Titles with the wrap, Records,
             Time Attack, the Bestiary), and the Bestiary's Circle on a locked
             foe (sound 7): the cursor memories equal A's after every step;
             the sounds played equal A's; after Triangle: D_800F190C,
             D_800EB9B0, currentMenu equal A's (closed). B alone: Circle on an
             unlocked foe shows the hint only (no ZUD load, nothing delivered)
  3 sweep    boundaries from the fresh fixture (chain tiers and 29 / 30,
             clear-time tiers, the cap, the rank's point thresholds on both
             sides, kills 4999 / 5000, attacks 499 / 500 / 4999 / 5000, chests
             51 / 52, map 360 / 361, each boss, the keys, time attacks 7 / 8,
             the abilities' titles,
             the high-water marks): B's JSON against B's statics and this
             file's ports of _calculateScore / _determineCharacterRank /
             _setTitleFlags (titles_mask = scoredata.titles = the port; each
             boundary's bit or rank checked)
  4 idem     native open then Square: MENU9 runs on the native row,
             vs_main_scoredata unchanged by its second _calculateProgress
  5 text     every string and value of the JSON equals MENU9's statics
             decoded (the tables in the running binary), the Titles rows as
             drawn (locked: miscInfo), and the Records / Time Attack values
             computed from scoredata
  6 mon      A at state 14: _monBinData (78 records: fields, unlocked, prev,
             next, every description) equals B's parse
  7 mouse    960x720 and 1280x720: hover writes nothing, a click on Home's
             row, a list row (its canonical pair), the strip, the wheel,
             right clicks out; one real SDL click (RPC system mouse)
  8 tune     a temporary theme (ROOD_UI_THEME) edited: the JSON follows; the
             RPC override (score_tune, one per key), its reset, and a file
             edit after a tune winning over it
Robustness (B): a forced currentMenu write ends the session (world back)."""
import argparse
import json
import re
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, BUILD, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import (card_guard, score_fixture, sha256, SCOREDATA, SCOREDATA_SIZE,  # noqa: E402
                           TRIAL_NONE)
import ui_manual_smoke as text_lib  # noqa: E402  (charset, decode: pc_ui_text_decode's rules)

CLOSED = 0x3F
SCORE_COMMAND = 8  # the top level's command (menuFlags bit 8)
MENU9_SECTION = "rood_ovl_menu_menu9_prg"
MENU9_STATICS = ("_score", "_characterRank", "_clearCount", "_openedChestCount", "_mapCompletion", "_menuState",
                 "_monBinData", "_menuText", "_rankText", "_titleText", "_titleDescriptions", "_statHeaders",
                 "_statDescriptions", "_timeAttacks", "_timeAttackDescriptions", "_miscInfo",
                 "_timeAttackReferenceTimes")
TOP, SECTION_STATES = 2, (5, 8, 11, 14)  # _menuInput's states: the top menu, each page's input
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4
MODES = ("titles", "records", "trials", "bestiary")
STATE_BOSSES, STATE_GOLD, STATE_CHEST_KEY, STATE_CHESTS = 0xCC, 0x383, 0x387, 0x3C0  # card_fixtures.STATE_FLAGS
ACTION_SIZE = 52  # sizeof(vs_action_t) (main.h: unk10 at 0x10, name[24] at 28); unlocked: byte 13, bit 7
SOUND_IDS = (4, 5, 6, 7)


def symbol_table():
    """The probe's symbol table (ROOD_OBJDUMP, else objdump): (address, size, section, name) per symbol (the
    game's globals are absolute, sizeless; the overlays' statics carry their sizes)."""
    out = subprocess.run([os.environ.get("ROOD_OBJDUMP", "objdump"), "-t", str(BUILD / "rood_battle_probe")],
                         capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 6:
            try:
                rows.append((int(parts[0], 16), int(parts[-2], 16), parts[-3], parts[-1]))
            except ValueError:
                pass
    return rows


def menu9_symbols(rows):
    """MENU9's statics: each name must resolve to exactly one (address, size) in MENU9's own sections
    (ENDING's same-named statics, and MENUF's, live in theirs)."""
    found = {}
    for addr, size, section, name in rows:
        if name in MENU9_STATICS and section.startswith(MENU9_SECTION):
            found.setdefault(name, []).append((addr, size))
    bad = [n for n in MENU9_STATICS if len(found.get(n, [])) != 1]
    if bad:
        raise RuntimeError(f"MENU9 statics not resolved to one address: {bad} ({found})")
    return {n: a[0] for n, a in found.items()}


# ---- the game's rules, ported here as the reference (238.c) ----
def u32(v):
    return v & 0xFFFFFFFF


def reference_score(sd, clear_now):
    kills = struct.unpack_from("<6H", sd, SCOREDATA["kills"])
    attacks = struct.unpack_from("<10H", sd, SCOREDATA["attacks"])
    trials = struct.unpack_from("<24I", sd, SCOREDATA["trials"])
    chain = struct.unpack_from("<H", sd, SCOREDATA["max_chain"])[0]
    minutes = struct.unpack_from("<I", sd, SCOREDATA["minutes"])[0]
    streak, misc = struct.unpack_from("<II", sd, SCOREDATA["streak"])
    s = 0
    for k, p in zip(kills, (20, 20, 40, 80, 100, 60)):
        s = u32(s + k * p)
    for a, p in zip(attacks, (20, 40, 20, 100, 60, 100, 60, 100, 80, 60)):
        s = u32(s + a * p)
    for i in range(8):
        if trials[i * 3] & 0xFFFFFF != TRIAL_NONE:
            s = u32(s + 20000)
    for limit, mult in ((6, 10), (11, 20), (16, 40), (21, 80), (26, 160), (31, 320), (51, 640), (1 << 20, 1280)):
        if chain < limit:
            s = u32(s + chain * mult)
            break
    s = u32(s + clear_now * 100000)
    if clear_now and minutes < 600:
        if minutes >= 540:
            s = u32(s + s // 4)
        elif minutes >= 300:
            s = u32(s + s // 2)
        else:
            s = u32(s * 2)
    s = u32(s + streak)
    s = u32(s + misc)
    return min(s, 999999999)


def reference_titles(before, state, actions, stride, clears, map_now, chests_now):
    """238.c _setTitleFlags over scoredata as _calculateProgress leaves it (the high-water marks raised by the
    live counts) and vs_main_stateFlags / vs_main_actions (unlocked: byte 13's bit 7 of each vs_action_t)."""
    titles = struct.unpack_from("<I", before, SCOREDATA["titles"])[0]
    map_best, chest_best = struct.unpack_from("<II", before, SCOREDATA["map"])
    map_best, chest_best = max(map_best, map_now), max(chest_best, chests_now)
    kills = struct.unpack_from("<6H", before, SCOREDATA["kills"])
    attacks = struct.unpack_from("<10H", before, SCOREDATA["attacks"])
    trials = struct.unpack_from("<24I", before, SCOREDATA["trials"])
    chain = struct.unpack_from("<H", before, SCOREDATA["max_chain"])[0]

    def unlocked(i):
        return actions[i * stride + 13] & 0x80

    def bit(n, on):
        return (1 << n) if on else 0
    titles |= bit(0, clears != 0) | bit(2, chest_best >= 52) | bit(3, map_best > 360)
    for k in range(6):
        titles |= bit(4 + k, state[STATE_BOSSES + k] != 0)
    titles |= bit(10, all(trials[i * 3] & 0xFFFFFF != TRIAL_NONE for i in range(8)))
    titles |= bit(11, chain >= 30) | bit(12, state[STATE_GOLD] != 0) | bit(13, state[STATE_CHEST_KEY] != 0)
    titles |= bit(14, all(unlocked(i) for i in range(184, 224)))
    titles |= bit(15, all(unlocked(i) for i in range(22, 54) if i not in (33, 36, 38, 39)))
    titles |= bit(20, all(k >= 5000 for k in kills)) | bit(21, all(a >= 5000 for a in attacks))
    for i in range(9):
        titles |= bit(22 + i, attacks[i + 1] >= 500)
    titles |= bit(31, attacks[0] >= 500)
    return titles


def reference_rank(titles, score):
    count = bin(titles & 0xFFFF).count("1")
    titles_need = [16, 12, 8] + [0] * 13
    points = [100000000, 75000000, 60000000, 40000000, 32000000, 24000000, 16000000, 12000000, 8000000, 5000000,
              4000000, 3000000, 2000000, 1000000, 500000, 0]
    for i in range(16):
        if count >= titles_need[i] and score >= points[i]:
            return i
    return 15


def table_strings(raw, n):
    """A vsString table's first n strings (u16 offsets in u16 units), decoded as the native screen does."""
    return [text_lib.decode(raw[struct.unpack_from("<H", raw, 2 * i)[0] * 2:]) for i in range(n)]


def fastest(rounds, minutes):
    h = int(minutes) // 60
    m = 59 if h >= 100 else int(minutes) % 60
    return f"R{rounds + 1} {min(h, 99):02d}:{m:02d}"


# ---- a game process ----
class Session:
    def __init__(self, name, out, card, env, modern):
        self.name, self.out = name, out
        self.game = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env)
        self.game.call("graphics", interpolate=False)
        self.game.call("ui_menu", modern=modern)
        self.modern = modern
        s = symbols()
        self.sym = s
        self.table = symbol_table()
        m9 = menu9_symbols(self.table)
        self.m9 = {n: a for n, (a, _) in m9.items()}
        self.m9_size = {n: z for n, (_, z) in m9.items()}

    def close(self):
        self.game.close()

    # ---- reads ----
    def shell(self):
        return self.game.call("ui_menu")

    def score(self):
        return (self.shell().get("system") or {}).get("score")

    def rd(self, name, n):
        return self.game.read(self.sym[name], n)

    def u8(self, name, off=0):
        return self.game.read(self.sym[name] + off, 1)[0]

    def i32(self, addr):
        return struct.unpack("<i", self.game.read(addr, 4))[0]

    def current_menu(self):
        return self.u8("vs_battle_menuState")

    def menu_state(self):
        return self.i32(self.m9["_menuState"])

    def statics(self):
        g = self.game
        return {"score": struct.unpack("<I", g.read(self.m9["_score"], 4))[0],
                "rank": struct.unpack("<H", g.read(self.m9["_characterRank"], 2))[0],
                "clears": struct.unpack("<H", g.read(self.m9["_clearCount"], 2))[0],
                "chests": struct.unpack("<H", g.read(self.m9["_openedChestCount"], 2))[0],
                "map": struct.unpack("<H", g.read(self.m9["_mapCompletion"], 2))[0]}

    def played(self):
        r = self.game.call("sfx")
        return [r["menu"][k] for k in SOUND_IDS]

    def dump(self):
        g, s = self.game, self.sym
        return {
            "scoredata": g.read(s["vs_main_scoredata"], SCOREDATA_SIZE),
            "roomFlags": g.read(s["vs_main_mapStatus"], 64),
            "menu9": g.read(s["vs_battle_menu9CursorMemory"], 8),
            "cm23": g.read(s["D_800F4EE8"] + 2, 2),
            "cursorMemory": g.read(s["vs_main_settings"] + 1, 1),
            "played": self.played(),
        }

    def close_dump(self):
        g, s = self.game, self.sym
        d = self.dump()
        d.update({"world": g.read(s["D_800F190C"], 4), "hud": g.read(s["D_800EB9B0"], 4),
                  "menu": g.read(s["vs_battle_menuState"], 1)})
        return d

    # ---- the fixture ----
    def fixture(self, kind, cursor_memory=1, seed=None, room_bit=False, kill_flags=None):
        g, s = self.game, self.sym
        flags = struct.unpack("<H", g.read(s["vs_main_settings"] + 2, 2))[0]
        g.write(s["vs_main_settings"] + 2, struct.pack("<H", flags | 1 << SCORE_COMMAND))
        g.write(s["vs_main_settings"] + 1, bytes([cursor_memory]))
        f = score_fixture(kind, kill_flags)
        for off, data in f["scoredata"].items():
            g.write(s["vs_main_scoredata"] + off, data)
        for off, data in f["state"].items():
            g.write(s["vs_main_stateFlags"] + off, data)
        if seed is not None:
            g.write(s["vs_battle_menu9CursorMemory"], bytes(seed))
        if room_bit:
            r1 = struct.unpack("<I", g.read(s["vs_main_mapStatus"] + 4, 4))[0]
            g.write(s["vs_main_mapStatus"] + 4, struct.pack("<I", (r1 | 0x800000) & ~0x400000))

    # ---- waits ----
    def wait(self, pred, limit=600, step=2):
        for _ in range(limit // step):
            v = pred()
            if v:
                return v
            self.game.run(step)
        return pred()

    def wait_shell(self):
        ok = self.wait(lambda: self.shell()["visible"] and self.current_menu() == 0, 300, 3)
        self.game.run(20)
        return ok

    def wait_closed(self):
        ok = self.wait(lambda: self.current_menu() == CLOSED and not self.shell()["visible"], 600, 5)
        self.game.run(20)
        return ok

    def wait_top_level(self):
        return self.wait(lambda: self.current_menu() == 0 and self.i32(self.sym["D_800F4CC0"]) >= 0
                         and self.game.read(self.sym["vs_battle_menuItems"], 4) != b"\0\0\0\0")

    def wait_page(self, mode):
        """A's MENU9 in `mode` ("top" or a section) taking input; B's screen there."""
        if self.modern:
            want = "home" if mode == "top" else mode
            return self.wait(lambda: (self.score() or {}).get("mode") == want and self.score()["valid"], 120, 1)
        state = TOP if mode == "top" else SECTION_STATES[MODES.index(mode)]
        ok = self.wait(lambda: self.current_menu() & 0x3F == 9 and self.menu_state() == state, 900, 2)
        self.game.run(30 if mode == "top" else 12)  # the top menu's list flies in; a page's rows drop in
        return ok

    # ---- opening ----
    def open(self):
        g = self.game
        if self.modern:
            g.press("triangle", vsyncs=3, settle=4)
            self.wait_shell()
            for _ in range(8):
                if self.shell()["tab"] == "system":
                    break
                g.press("l1", vsyncs=3, settle=10)
            for _ in range(16):
                if self.shell().get("row") == "score":
                    break
                g.press("down", vsyncs=3, settle=6)
            g.press("circle", vsyncs=3, settle=4)
            return self.wait_page("top")
        g.press("triangle", vsyncs=3, settle=10)
        self.wait_top_level()
        for _ in range(12):
            if self.u8("vs_battle_submenuStates") == SCORE_COMMAND:
                break
            g.press("up", vsyncs=3, settle=6)
        g.press("circle", vsyncs=3, settle=4)
        return self.wait_page("top")

    def step(self, button, mode):
        self.game.press(button, vsyncs=3, settle=6)
        if mode:
            return self.wait_page(mode)
        return True

    # ---- the pointer ----
    def ptr(self, run=3, **p):
        self.game.call("ui_menu", pointer=p)
        self.game.run(run)

    def hit(self, kind, index):
        sc = self.score()
        for h in reversed(sc["hits"] if sc else []):
            if h["kind"] == kind and h["index"] == index:
                return h
        return None

    def click(self, kind, index, run=3):
        h = self.hit(kind, index)
        if not h:
            return False
        self.ptr(run, x=(h["x0"] + h["x1"]) / 2, y=(h["y0"] + h["y1"]) / 2, click=True)
        return True


def diff_keys(a, b, keys):
    return [k for k in keys if a[k] != b[k]]


# ---- 1 + 2: open and moves, the same story on both sides ----
SEED = [3, 4, 10, 6, 15, 7, 5, 2]  # titlePage, titleRow, encyclopaedia*, recordTime*, gazettePage, gazetteRow
CONFIGS = (("midgame", 1, None, True), ("cleared", 0, SEED, False), ("maxed", 1, SEED, False))
KEYS = ("scoredata", "roomFlags", "menu9", "cm23", "cursorMemory")


def story(s, check, dumps):
    """Each config: fixture, open (dump), then (configs 0 and 2) the moves; close (dump). Then the Bestiary's
    Circle on a locked foe (fresh: every foe locked): sound 7 on both sides, nothing loaded."""
    for n, (kind, cm, seed, room_bit) in enumerate(CONFIGS):
        s.fixture(kind, cm, seed, room_bit)
        if not check(s.open(), f"{s.name} {n}: Score opens ({kind})"):
            return
        d = s.dump()
        if s.modern:
            sc = s.score()
            d["json"] = (sc["score"], sc["rank"])
        d["statics"] = s.statics()
        dumps.append((f"open{n}", d))
        if n != 1:
            moves(s, check, dumps, n)
        s.step("triangle", None)
        check(s.wait_closed(), f"{s.name} {n}: Triangle closes")
        dumps.append((f"close{n}", s.close_dump()))
    n = len(CONFIGS)
    s.fixture("fresh", 1, [0] * 8)
    if not check(s.open(), f"{s.name} {n}: Score opens (fresh, every foe locked)"):
        return
    d = s.dump()
    if s.modern:
        sc = s.score()
        d["json"] = (sc["score"], sc["rank"])
    d["statics"] = s.statics()
    dumps.append((f"open{n}", d))
    row = s.game.read(s.sym["D_800F4EE8"] + 2, 1)[0]
    for _ in range((3 - row) & 3):
        s.step("down", None)
    s.game.run(6)
    s.step("circle", "bestiary")
    dumps.append((f"b{n}.in", s.dump()))
    menu = s.current_menu()
    s.step("circle", None)  # the focused foe (0) is locked: sound 7, no viewer
    s.game.run(6)
    d = s.dump()
    dumps.append((f"b{n}.circle", d))
    if s.modern:
        sc = s.score()
        check(sc["mode"] == "bestiary" and not sc["hint"] and s.current_menu() == menu,
              f"{s.name} bestiary: Circle on a locked foe: no hint, the screen stays ({sc['mode']})")
    else:
        check(s.menu_state() == SECTION_STATES[3], f"{s.name} bestiary: MENU9 stays in its list (state 14)")
    s.step("triangle", None)
    check(s.wait_closed(), f"{s.name} {n}: Triangle closes")
    dumps.append((f"close{n}", s.close_dump()))


def bestiary_unlocked(b, check):
    """B: Circle on an unlocked foe: the hint only. Nothing delivered to MENU9 (currentMenu, MENU9's state),
    no ZUD load (the CD queue and the heap's head unchanged), the cursor memory unchanged."""
    g, s = b.game, b.sym
    b.fixture("fresh", 1, [0] * 8, kill_flags=[-1] * 26)
    b.open()
    row = g.read(s["D_800F4EE8"] + 2, 1)[0]
    for _ in range((3 - row) & 3):
        b.step("down", None)
    g.run(6)
    b.step("circle", "bestiary")
    sc = b.score()
    unlocked = sc["mon_ok"] and sc["text"]["bestiary"][sc["focus"]]["unlocked"]

    def state():  # a ZUD load allocates from heapA: its head block unchanged
        return (g.read(s["heapA"], 64), b.current_menu(), b.menu_state(), b.rd("vs_battle_menu9CursorMemory", 8),
                b.played())
    before = state()
    b.step("circle", None)
    g.run(10)
    after = state()
    sc = b.score()
    check(unlocked and sc["hint"] and sc["mode"] == "bestiary" and before == after,
          f"bestiary: Circle on an unlocked foe shows the hint only (hint {sc['hint']}, nothing loaded, "
          f"currentMenu / MENU9 state / cursor memory / sounds unchanged: {before == after})")
    b.step("triangle", None)
    b.wait_closed()


def moves(s, check, dumps, n):
    """From the top: Down, Circle (a section), Down x3, Up, Up x8, Up (the wrap on a press at the top), Cross;
    Down, Circle, Down x9, Cross; Down, Circle, Down x2, Cross (each section's own rules)."""
    row = s.game.read(s.sym["D_800F4EE8"] + 2, 1)[0]  # _setRows' at the open (the list keeps its own after)
    for k, downs in enumerate((3, 9, 2)):
        s.step("down", None)
        row = (row + 1) & 3
        s.game.run(6)
        dumps.append((f"m{n}.{k}.top", s.dump()))
        s.step("circle", MODES[row])
        dumps.append((f"m{n}.{k}.in", s.dump()))
        for _ in range(downs):
            s.step("down", None)
        if k == 0:
            s.step("up", None)
            for _ in range(8):
                s.step("up", None)
            s.step("up", None)
        s.game.run(4)
        dumps.append((f"m{n}.{k}.moved", s.dump()))
        s.step("cross", "top")
        dumps.append((f"m{n}.{k}.back", s.dump()))


def compare_story(check, da, db):
    names_a, names_b = [n for n, _ in da], [n for n, _ in db]
    if not check(names_a == names_b, f"parity: the same steps ({len(names_a)} / {len(names_b)})"):
        return
    base_a = base_b = None
    for (name, a), (_, b) in zip(da, db):
        keys = KEYS + (("world", "hud", "menu") if name.startswith("close") else ())
        bad = diff_keys(a, b, keys)
        check(not bad, f"parity {name}: " + ("equal" if not bad else "differs: " + ", ".join(
            f"{k} {a[k].hex()[:48]} vs {b[k].hex()[:48]}" for k in bad)))
        if name.startswith("open"):
            base_a, base_b = a["played"], b["played"]
            if "json" in b:
                check(b["json"] == (a["statics"]["score"], a["statics"]["rank"]) == (b["statics"]["score"],
                                                                                     b["statics"]["rank"]),
                      f"parity {name}: JSON score / rank {b['json']} = A's _score / _characterRank "
                      f"{(a['statics']['score'], a['statics']['rank'])}")
            check(a["statics"] == b["statics"], f"parity {name}: MENU9's statics equal ({b['statics']})")
        elif base_a is not None and not name.startswith("close"):
            pa = [x - y for x, y in zip(a["played"], base_a)]
            pb = [x - y for x, y in zip(b["played"], base_b)]
            check(pa == pb, f"sounds {name}: played as the original (4/5/6/7: {pb}, original {pa})")


# ---- 3: the sweep (B) ----
def sweep(b, check):
    """Each case from the fresh fixture (every modelled stateFlags field zero): the score and rank against this
    file's ports, the titles (JSON titles_mask, scoredata.titles, _setTitleFlags' port) and each boundary's
    title bit or rank, on both sides of it."""
    g, s = b.game, b.sym
    stride = ACTION_SIZE
    mask = struct.unpack("<16I", g.read(s["vs_battle_mapCompletionFlags"], 64))
    cases = []  # (name, scoredata writes, stateFlags writes, [(title bit, on)], rank or None)
    for chain in (5, 6, 10, 11, 15, 16, 20, 21, 25, 26, 29, 30, 31, 50, 51):
        cases.append((f"chain {chain}", {SCOREDATA["max_chain"]: struct.pack("<H", chain)}, {},
                      [(11, chain >= 30)], None))
    for clears in (0, 1):
        for minutes in (299, 300, 539, 540, 599, 600):
            cases.append((f"time {clears}/{minutes}", {SCOREDATA["clear_count"]: struct.pack("<II", clears, minutes)},
                          {0: bytes([clears])}, [(0, clears != 0)], None))
    cases.append(("cap", {SCOREDATA["streak"]: struct.pack("<II", 999999990, 50)}, {}, [], None))
    for kills in (4999, 5000):
        cases.append((f"kills {kills}", {SCOREDATA["kills"]: struct.pack("<6H", *[kills] * 6)}, {},
                      [(20, kills >= 5000)], None))
    for attacks in (499, 500, 4999, 5000):
        cases.append((f"attacks {attacks}", {SCOREDATA["attacks"]: struct.pack("<10H", *[attacks] * 10)}, {},
                      [(21, attacks >= 5000), (31, attacks >= 500)] + [(22 + i, attacks >= 500) for i in range(9)],
                      None))
    for chests in (51, 52):  # the live count raises the stored one (fresh: 0)
        cases.append((f"chests {chests}", {}, {STATE_CHESTS: bytes([1] * chests + [0] * (64 - chests))},
                      [(2, chests >= 52)], None))
    for best in (360, 361):  # the stored map completion (the save room's live count is far below)
        cases.append((f"map {best}", {SCOREDATA["map"]: struct.pack("<I", best)}, {}, [(3, best > 360)], None))
    for k in range(6):
        cases.append((f"boss {k}", {}, {STATE_BOSSES + k: b"\1"}, [(4 + j, j == k) for j in range(6)], None))
    cases.append(("keys", {}, {STATE_GOLD: b"\1", STATE_CHEST_KEY: b"\1"}, [(12, True), (13, True)], None))
    for n in (7, 8):
        tr = b"".join(struct.pack("<I", 0x001000 if i % 3 == 0 and i // 3 < n else TRIAL_NONE) for i in range(24))
        cases.append((f"trials {n}/8", {SCOREDATA["trials"]: tr}, {}, [(10, n == 8)], None))
    # the rank's point thresholds (fresh: no titles, nothing else scores): both sides of 500000, 1000000, 8000000
    for pts, rank in ((499999, 15), (500000, 14), (999999, 14), (1000000, 13), (7999999, 9), (8000000, 8)):
        cases.append((f"points {pts}", {SCOREDATA["streak"]: struct.pack("<II", pts, 0)}, {}, [], rank))
    for name, sd, st, bits, rank in cases:
        b.fixture("fresh", 1)
        for off, data in sd.items():
            g.write(s["vs_main_scoredata"] + off, data)
        for off, data in st.items():
            g.write(s["vs_main_stateFlags"] + off, data)
        before = b.rd("vs_main_scoredata", SCOREDATA_SIZE)
        state = b.rd("vs_main_stateFlags", STATE_CHESTS + 64)
        actions = b.rd("vs_main_actions", 256 * stride)
        rooms = list(struct.unpack("<16I", b.rd("vs_main_mapStatus", 64)))
        if rooms[1] & 0x800000:  # _calculateProgress' own write, before it counts
            rooms[1] |= 0x400000
        if not check(b.open(), f"sweep {name}: opens"):
            return
        sc, st9 = b.score(), b.statics()
        after = b.rd("vs_main_scoredata", SCOREDATA_SIZE)
        titles = struct.unpack_from("<I", after, 0)[0]
        want = reference_score(before, st9["clears"])
        check(sc["score"] == st9["score"] == want and sc["rank"] == st9["rank"] == reference_rank(titles, want),
              f"sweep {name}: JSON {sc['score']}/{sc['rank']}, statics {st9['score']}/{st9['rank']}, "
              f"reference {want}/{reference_rank(titles, want)}")
        # the live counts
        live_map = sum(bin(r & m).count("1") for r, m in zip(rooms, mask))
        live_chests = sum(1 for x in state[STATE_CHESTS:STATE_CHESTS + 64] if x)
        check(st9["clears"] == state[0] and st9["chests"] == live_chests and st9["map"] == live_map,
              f"sweep {name}: the live counts (clears {st9['clears']}, chests {st9['chests']}/{live_chests}, "
              f"map {st9['map']}/{live_map})")
        ref = reference_titles(before, state, actions, stride, st9["clears"], live_map, live_chests)
        check(sc["titles_mask"] == titles == ref,
              f"sweep {name}: titles JSON {sc['titles_mask']:#010x} = scoredata {titles:#010x} = _setTitleFlags' "
              f"port {ref:#010x}")
        if bits:
            got = [(n, bool(titles >> n & 1)) for n, _ in bits]
            check(got == [(n, on) for n, on in bits], f"sweep {name}: title bits {got}")
        if rank is not None:
            check(sc["rank"] == rank, f"sweep {name}: rank {sc['rank']} (want {rank}: the threshold crossed)")
        b.step("triangle", None)
        b.wait_closed()
    # the abilities' titles (bits 14 / 15: vs_main_actions[184..223] and [22..53] but 33, 36, 38, 39 unlocked):
    # one missing, then all; the game's own bytes put back after
    saved = b.rd("vs_main_actions", 256 * stride)
    for missing in (200, None):
        acts = bytearray(saved)
        for i in list(range(184, 224)) + [i for i in range(22, 54) if i not in (33, 36, 38, 39)]:
            acts[i * stride + 13] |= 0x80
        if missing is not None:
            for i in (missing, 40):
                acts[i * stride + 13] &= 0x7F
        b.fixture("fresh", 1)
        g.write(s["vs_main_actions"], bytes(acts))
        before = b.rd("vs_main_scoredata", SCOREDATA_SIZE)
        state = b.rd("vs_main_stateFlags", STATE_CHESTS + 64)
        b.open()
        sc, st9 = b.score(), b.statics()
        titles = struct.unpack_from("<I", b.rd("vs_main_scoredata", SCOREDATA_SIZE), 0)[0]
        ref = reference_titles(before, state, bytes(acts), stride, st9["clears"], st9["map"], st9["chests"])
        on = missing is None
        check(sc["titles_mask"] == titles == ref and bool(titles >> 14 & 1) == on and bool(titles >> 15 & 1) == on,
              f"sweep abilities {'all' if on else 'one missing'}: titles {titles:#010x} = port {ref:#010x}")
        b.step("triangle", None)
        b.wait_closed()
        g.write(s["vs_main_actions"], saved)
    # the high-water marks: a stored value above the live one stays (fresh: no chest opened)
    b.fixture("fresh", 1)
    g.write(s["vs_main_scoredata"] + SCOREDATA["map"], struct.pack("<II", 300, 50))
    b.open()
    after = b.rd("vs_main_scoredata", SCOREDATA_SIZE)
    sc = b.score()
    best = struct.unpack_from("<II", after, SCOREDATA["map"])
    check(sc["map"][0] < 300 and sc["chests"][0] < 50 and best == (300, 50) and (sc["map"][1], sc["chests"][1]) == best,
          f"sweep: a stored high-water mark above the live count stays ({best}, live {sc['map'][0]} / "
          f"{sc['chests'][0]})")
    b.step("triangle", None)
    b.wait_closed()


# ---- 4: idempotence (B) ----
def idempotence(b, check):
    b.fixture("midgame", 1)
    b.open()
    b.step("down", None)
    home = b.score()["home"]
    before = b.rd("vs_main_scoredata", SCOREDATA_SIZE)
    b.game.press("square", vsyncs=3, settle=4)
    b.modern = False
    ok = b.wait_page("top")
    b.modern = True
    after = b.rd("vs_main_scoredata", SCOREDATA_SIZE)
    row = b.rd("D_800F4EE8", 4)[2]
    check(ok and before == after and row == home,
          f"idem: Square opens MENU9 on the native row ({row}, native {home}), scoredata unchanged")
    b.step("triangle", None)
    check(b.wait_closed(), "idem: and MENU9 closes")


# ---- 5: text (B) ----
def text_checks(b, check):
    g, m9 = b.game, b.m9
    b.fixture("cleared", 1)
    b.open()
    sc = b.score()
    t = sc["text"]
    sizes = b.m9_size

    def table(name, n):
        return table_strings(g.read(m9[name], sizes[name]), n)
    menu = table("_menuText", 4)
    check(t["sections"] == menu, f"text: the sections' names = MENU9's menuText ({t['sections']})")
    titles, descs = table("_titleText", 32), table("_titleDescriptions", 32)
    check([x[0] for x in t["titles"]] == titles and [x[1] for x in t["titles"]] == descs,
          "text: 32 titles and descriptions = MENU9's statics")
    heads, sdesc = table("_statHeaders", 23), table("_statDescriptions", 23)
    check([x[0] for x in t["records"]] == heads and [x[1] for x in t["records"]] == sdesc,
          "text: 23 records and descriptions = MENU9's statics")
    misc = table("_miscInfo", 6)
    check(t["misc"] == misc, f"text: miscInfo = MENU9's statics ({misc})")
    # the Titles rows as drawn: an earned title its own text, a locked one miscInfo's 0 / 1
    mask = sc["titles_mask"]
    want_rows = [[titles[i], descs[i]] if mask >> i & 1 else [misc[0], misc[1]] for i in range(32)]
    check(t["titles_shown"] == want_rows and 0 < bin(mask).count("1") < 32,
          f"text: the Titles rows drawn (locked: miscInfo 0 / 1; {bin(mask).count('1')} earned)")
    ranks = table("_rankText", 16)
    check(sc["rank_text"] == ranks[sc["rank"]], f"text: the rank's text ({sc['rank_text']})")
    names, tdesc = table("_timeAttacks", 8), table("_timeAttackDescriptions", 8)
    check([x["name"] for x in t["trials"]] == names and [x["desc"] for x in t["trials"]] == tdesc,
          "text: 8 time attacks and descriptions = MENU9's statics")
    # 238.c's own literals (code, not game data: the strings' rodata is not readable over the RPC)
    src = (ROOT / "src/MENU/MENU9.PRG/238.c").read_text()
    block = src[src.index("_timeAttackReferenceTimes[] = {"):]
    refs = [x.lstrip("$") for x in re.findall(r'"(\$[0-9:]+)"', block[:block.index("};")])]
    check([x["ref"] for x in t["trials"]] == refs, f"text: the reference times ({refs})")
    # the values from scoredata (_renderBossRushDetails, _printFastestClearTime, _renderBossRushRecords)
    sd = b.rd("vs_main_scoredata", SCOREDATA_SIZE)
    st = b.statics()
    kills = struct.unpack_from("<6H", sd, SCOREDATA["kills"])
    attacks = struct.unpack_from("<10H", sd, SCOREDATA["attacks"])
    rounds, minutes, mapb, chestb = struct.unpack_from("<IIII", sd, SCOREDATA["clear_count"])
    chain = struct.unpack_from("<H", sd, SCOREDATA["max_chain"])[0]
    want = [str(st["clears"]), fastest(rounds, minutes) if st["clears"] else "", f"{st['chests'] * 100 // 52}%",
            f"{u32(chestb * 100) // 52}%", f"{st['map'] * 100 // 361}%", f"{u32(mapb * 100) // 361}%", str(chain)]
    want += [str(k) for k in kills] + [str(a) for a in attacks[1:]] + [str(attacks[0])]
    got = [x[2] for x in t["records"]]
    check(got == want, f"text: the Records values ({got}; want {want})")
    trials = struct.unpack_from("<24I", sd, SCOREDATA["trials"])
    ok = True
    for i, x in enumerate(t["trials"]):
        for j in range(3):
            v = trials[i * 3 + j]
            tm = v & 0xFFFFFF
            ok &= x["times"][j] == ("--:--:--" if tm == TRIAL_NONE else f"{tm >> 16:02d}:{tm >> 8 & 0xFF:02d}:"
                                    f"{tm & 0xFF:02d}")
            ok &= x["rounds"][j] == (0 if tm == TRIAL_NONE else (v >> 24 & 0x7F) + 1)
        ok &= x["has"] == (trials[i * 3] & 0xFFFFFF != TRIAL_NONE)
    check(ok, "text: the time attack's times and rounds")
    b.step("triangle", None)
    b.wait_closed()


# ---- 6: MON.BIN (A at state 14 against B's parse) ----
def mon_original(a, check):
    """A: the Encyclopaedia open (state 14): MENU9's _monBinData read whole."""
    a.fixture("midgame", 1)
    a.open()
    row = a.game.read(a.sym["D_800F4EE8"] + 2, 1)[0]
    for _ in range((3 - row) & 3):
        a.step("down", None)
    a.step("circle", "bestiary")
    p = int.from_bytes(a.game.read(a.m9["_monBinData"], PTR), "little")
    data = a.game.read(p, 14336) if p else b""
    a.step("triangle", None)
    a.wait_closed()
    check(len(data) == 14336, f"mon: A's _monBinData read ({p:#x})")
    return data


def mon_compare(data, sc, check):
    best = sc["text"]["bestiary"]
    table = 150 * 44
    bad = []
    for i in range(78):
        zud, cls, off, cnt = struct.unpack_from("<4h", data, i * 44)
        unlocked, prev, nxt = data[i * 44 + 12], data[i * 44 + 13], data[i * 44 + 14]
        name = text_lib.decode(data[i * 44 + 16:i * 44 + 44] + b"\xe7")
        e = best[i]
        desc_at = table + struct.unpack_from("<H", data, table + 2 * i)[0] * 2
        desc = text_lib.decode(data[desc_at:])
        if (e["zud"], e["cls"], e["unlocked"], e["name"], e["desc"]) != (zud, cls if 0 <= cls < 6 else -1, unlocked,
                                                                       name, desc):
            bad.append(i)
        elif unlocked and (e["prev"], e["next"]) != (prev, nxt):
            bad.append(i)
    check(not bad and sum(e["unlocked"] for e in best) == sc["unlocked"],
          f"mon: 78 records = MENU9's _monBinData (fields, unlocked, prev / next, descriptions; differ: {bad[:8]})")


# ---- 7: the pointer (C) ----
def mouse(c, check, wide):
    g, s = c.game, c.sym
    tag = "16:9" if wide else "4:3"
    g.call("graphics", widescreen=wide)
    g.run(10)
    c.fixture("midgame", 1, [0] * 8)
    c.open()
    g.run(4)
    sc = c.score()
    W = 1280 if wide else 960
    check(sc and all(0 <= h["x0"] and h["x1"] <= W for h in sc["hits"]), f"mouse {tag}: every hit inside {W}")
    mem = c.rd("vs_battle_menu9CursorMemory", 8) + c.rd("D_800F4EE8", 4)
    h = c.hit("home", 1)
    c.ptr(3, x=(h["x0"] + h["x1"]) / 2, y=(h["y0"] + h["y1"]) / 2)  # hover only
    check(c.rd("vs_battle_menu9CursorMemory", 8) + c.rd("D_800F4EE8", 4) == mem and c.score()["home"] == 0,
          f"mouse {tag}: hovering writes nothing")
    c.click("home", 1)
    sc = c.score()
    check(sc["mode"] == "records" and c.rd("vs_battle_menu9CursorMemory", 8)[7] == 1,
          f"mouse {tag}: a click on Home's Records row enters it (gazetteRow 1)")
    g.run(4)  # a frame of the section drawn: its hits
    shown = c.click("row", 8)  # a row on screen (the Records' captions push the later ones below)
    m9 = c.rd("vs_battle_menu9CursorMemory", 8)
    check(shown and c.score()["focus"] == 8 and (m9[4], m9[5]) == (2, 6),
          f"mouse {tag}: a row click: the entry, its canonical pair (recordTime {m9[4]}, {m9[5]})")
    c.ptr(3, x=W / 2, y=400, wheel=-1)
    check(c.score()["focus"] == 9, f"mouse {tag}: the wheel steps the list ({c.score()['focus']})")
    c.click("section", 3)
    check(c.score()["mode"] == "bestiary", f"mouse {tag}: the strip's Bestiary")
    c.ptr(3, x=W / 2, y=400, right=True)
    check(c.score()["mode"] == "home", f"mouse {tag}: a right click: Home")
    c.ptr(3, x=W / 2, y=400, right=True)
    sh = c.shell()
    check(sh["visible"] and (sh.get("system") or {}).get("score") is None, f"mouse {tag}: again: the System list")
    c.ptr(3, x=W / 2, y=400, right=True)
    check(c.wait_closed(), f"mouse {tag}: again: closed")
    g.call("graphics", widescreen=False)
    g.run(10)


def sdl_click(c, check):
    """One click through SDL's own events (window pixels: 960 x 720 is layout units)."""
    c.fixture("midgame", 1, [0] * 8)
    c.open()
    h = c.hit("home", 2)
    x, y = int((h["x0"] + h["x1"]) / 2), int((h["y0"] + h["y1"]) / 2)
    try:
        c.game.call("system", mouse=[x, y])
        c.game.run(3)
        c.game.call("system", mouse=[x, y], button=1, down=True)
        c.game.run(2)
        c.game.call("system", mouse=[x, y], button=1, down=False)
        c.game.run(4)
        ok = c.score()["mode"] == "trials"
    except Exception as e:  # noqa: BLE001
        ok = False
        print("sdl click:", e)
    check(ok, "mouse: a real SDL click on Home's Time Attack row enters it")
    c.step("triangle", None)
    c.wait_closed()


# ---- 8: tunables (B, launched with ROOD_UI_THEME) ----
def tunables(b, check, theme):
    g = b.game
    b.fixture("midgame", 1, [0] * 8)
    b.open()
    b.step("circle", "titles")
    t0 = b.score()["tunables"]
    original = theme.read_text()
    data = json.loads(original)
    data["score"]["row_h"] = 50
    data["score"]["layout"]["narrow"]["titles_list"] = [30, 480]
    theme.write_text(json.dumps(data, indent=2))
    ok = False
    for _ in range(40):
        time.sleep(0.1)
        g.run(2)
        t = b.score()["tunables"]
        if t["row_h"] == 50:
            ok = True
            break
    h0, h1 = b.hit("row", 0), b.hit("row", 1)
    check(ok and t0["row_h"] == 44 and h0 and h1 and abs((h1["y0"] - h0["y0"]) - 50) < 0.01 and h0["x0"] == 30,
          f"tune: the theme file reloads: row_h {t0['row_h']} -> {t['row_h']}, rows 50 apart from x 30")
    g.call("ui_menu", score_tune={"key": "row_h", "value": 52})
    g.run(3)
    t = b.score()["tunables"]
    g.call("ui_menu", score_tune={"key": "narrow.titles_list", "index": 1, "value": 470})
    g.run(3)
    t2 = b.score()["tunables"]
    for v in (53, 54, 52):  # a repeated tune replaces its override
        g.call("ui_menu", score_tune={"key": "row_h", "value": v})
    g.run(3)
    listed = b.score()["tune_overrides"]
    g.call("ui_menu", score_tune={"reset": True})
    g.run(3)
    t3 = b.score()["tunables"]
    check(t["row_h"] == 52 and t2["layout"]["narrow"]["titles_list"] == [30, 470] and t3["row_h"] == 50
          and listed == ["row_h[0]", "narrow.titles_list[1]"] and b.score()["tune_overrides"] == [],
          f"tune: the RPC override (one per key: {listed}) and its reset ({t['row_h']}, "
          f"{t2['layout']['narrow']['titles_list']}, {t3['row_h']})")
    # an edit of the file after a tune: the file's value shows, the override dropped
    g.call("ui_menu", score_tune={"key": "row_h", "value": 60})
    g.run(3)
    data["score"]["row_h"] = 48
    theme.write_text(json.dumps(data, indent=2))
    ok = False
    for _ in range(40):
        time.sleep(0.1)
        g.run(2)
        sc = b.score()
        if sc["tunables"]["row_h"] == 48:
            ok = True
            break
    check(ok and sc["tune_overrides"] == [], f"tune: a file edit after a tune shows ({sc['tunables']['row_h']}, "
          f"overrides {sc['tune_overrides']})")
    theme.write_text(original)  # the next process starts from the shipped values
    b.step("triangle", None)
    b.wait_closed()


def robustness(b, check):
    b.fixture("midgame", 1)
    b.open()
    frozen = struct.unpack("<i", b.rd("D_800F190C", 4))[0]
    b.game.write(b.sym["vs_battle_menuState"], bytes([CLOSED]))
    b.game.run(8)
    world = struct.unpack("<i", b.rd("D_800F190C", 4))[0]
    check(frozen == 0 and (b.score() is None) and world == 1,
          f"robust: a forced currentMenu write ends the screen, the world runs ({frozen} -> {world})")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    from card_fixtures import default_save
    parser.add_argument("--card", default=str(default_save()), help="the source card (copied; never a player card)")
    parser.add_argument("--out", help="keep the logs here")
    parser.add_argument("--only", default="1,2,3,4,5,6,7,8", help="cases to run (1 and 2 run together)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    text_lib.CHARSET = text_lib.charset()
    only = set(args.only.split(","))
    out = pathlib.Path(args.out) if args.out else OUT / "ui_score"
    out.mkdir(parents=True, exist_ok=True)
    lights = out / "lights"
    lights.mkdir(exist_ok=True)
    theme = out / "theme.json"
    shutil.copyfile(ROOT / "pc/assets/ui/theme.json", theme)
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_DETERMINISTIC": "1",
           "ROOD_UI_THEME": str(theme)}
    os.environ.pop("ROOD_SETTINGS", None)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    with card_guard(out / "cards", source=args.card) as cards:
        card_hash = sha256(cards.card1)
        dumps, mon = {}, None
        if only & {"1", "2", "6"}:
            a = Session("A", out, cards.card1, env, modern=False)
            try:
                if only & {"1", "2"}:
                    dumps["A"] = []
                    story(a, check, dumps["A"])
                if "6" in only:
                    mon = mon_original(a, check)
            finally:
                a.close()
        b = Session("B", out, cards.card1, env, modern=True)
        try:
            if only & {"1", "2"}:
                dumps["B"] = []
                story(b, check, dumps["B"])
                compare_story(check, dumps["A"], dumps["B"])
                bestiary_unlocked(b, check)
            if "6" in only and mon:
                b.fixture("midgame", 1)
                b.open()
                mon_compare(mon, b.score(), check)
                b.step("triangle", None)
                b.wait_closed()
            if "5" in only:
                text_checks(b, check)
            if "4" in only:
                idempotence(b, check)
            if "3" in only:
                sweep(b, check)
            if "8" in only:
                tunables(b, check, theme)
            robustness(b, check)
        finally:
            b.close()
        if "7" in only:
            c = Session("C", out, cards.card1, env, modern=True)
            try:
                mouse(c, check, False)
                mouse(c, check, True)
                sdl_click(c, check)
            finally:
                c.close()
        check(sha256(cards.card1) == card_hash, "card: the card copy unchanged (Score writes RAM only)")

    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("ui_score_smoke: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
