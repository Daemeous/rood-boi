#!/usr/bin/env python3
"""The classic look's parity gate (plan pc/docs/plans/2026-10-20-classic-ui-in-lua.md
§3; pc/src/classic/classic_parity.h): rood.classic's Lua draw of each kind it
owns against the game's own drawing of the same element in the same frame,
packet for packet.

    python3 pc/tests/classic_parity.py [--card pc/build/test-card1.mcd] [--out DIR]
        [--renderer null|gl|vulkan] [--kind status,chain]

One game from the save_room checkpoint (a copy of the card), the classic look
(ui_menus 0), the null renderer by default (packets need no GPU), the shipped
plugins only (rood.classic on), dev mode (the RPC):
  - parity (RPC classic {parity}): the game draws; at each hook rood.classic's
    claim draws into a shadow slot and both are normalised and compared. Each
    kind's states are held in turn with rood.dev.show's spawners (the game's
    own records): status, every timing word and success word at the ages of
    its slide in, held and slide out; chain, counts 1 (it slides in), 2, 3, 9,
    10 and 99 at the same ages. Each state is held until the gate has compared
    two of its frames (a condition, not a count of vsyncs);
  - waivers (pc/tests/classic/waivers.toml) equal the overrides rood.classic
    declares (its M.look.overrides), and no mismatch outside their fields;
  - the default and the fallback, the parity mode off: rood.classic draws each
    kind (claimed, suppressed, drawn); the kind's hud_classic_<kind> off, a
    draw that raises, the plugin off: the game draws it again, each time;
  - state parity (§3.4): from one mark, a held word with rood.classic drawing,
    then with the game drawing, leaves the game's memory the same every vsync
    (Oracle page digests; the primitive buffers, ordering tables and the
    packet pointer masked).
What packets cannot show (a host-side hide, widescreen's widening) is the other suites' to see: the classic
gate also runs cutscene_player_smoke and widescreen_smoke (run.py classic_parity hud classic
cutscene_player_smoke widescreen_smoke).
Reports: <out>/<kind>.json (frames compared, mismatches by
field, the first difference with both packets, waived fields, the states
covered). Exit 0 when every kind is green."""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402
from snapshot_diff import Symbols  # noqa: E402

TIMINGS = ["too_fast", "fast", "slow", "too_slow"]
SUCCESS = ["COOL!", "GOOD!", "EXCELLENT!", "RIGHT ON!", "PERFECT!", "WELL TIMED!", "NICE!", "GREAT!"]
WORDS = {"too_fast": "TOO FAST!", "fast": "FAST!", "slow": "SLOW!", "too_slow": "TOO SLOW!"}
CHAIN_COUNTS = [1, 2, 3, 9, 10, 99]
SLOTS = ["WEAPON", "SHIELD", "R.ARM", "L.ARM", "HEAD", "BODY", "LEGS"]
STATS = ["HUMAN", "BEAST", "UNDEAD", "PHANTOM", "DRAGON", "EVIL", "PHYSICS", "AIR", "FIRE", "EARTH", "WATER", "LIGHT",
         "DARKNESS"]
VALUES = [1, 9, 10, 99, 100, 999]  # one to three figures (the spawners cap at 999)
# The ages each kind's whole life must have shown: its slide in and out (func_800958A4: 64, 16, 4, 1 pixels
# at ages 0..3 and 45..42; func_80095A4C: the first chain's 0..3, every count's 90..87); a number's bounce
# (func_80094B0C: frames 0..15, then held at 15) and a stat's drift (half its age, to 30).
SLIDES = {"status": {0, 1, 2, 3, 42, 43, 44, 45}, "chain": {0, 1, 2, 3, 87, 88, 89, 90},
          "damage": set(range(17)) | {30}}
# The gauges' states (Ashley's HUD record written: hp, max_hp, mp, max_mp, risk): digits of one to four
# figures, empty and full bars, RISK ZERO and RISK's bar; then the display's slide out and back.
GAUGES = [(157, 253, 50, 50, 0), (1, 253, 0, 50, 0), (253, 253, 50, 50, 1), (9, 9, 7, 7, 55), (999, 999, 999, 999, 100),
          (1000, 1500, 120, 300, 99), (0, 253, 50, 50, 0)]
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (vs_battle_actors)


def states(kind):
    """(the show's args, the element's text) for every element of kind the gate plays."""
    if kind == "status":
        for timing in TIMINGS:
            yield {"timing": timing}, WORDS[timing]
        for variant, word in enumerate(SUCCESS):
            yield {"timing": "success", "variant": variant}, word
    elif kind == "chain":
        for count in CHAIN_COUNTS:
            yield {"count": count}, f"{count} CHAIN" + ("" if count == 1 else "S")
    elif kind == "buff":
        # every equipment word and every class / affinity word, both signs, digits of one to three figures
        for i, slot in enumerate(SLOTS):
            yield {"slot": slot, "stat": STATS[i], "value": 1 + i, "sign": 1}, f"{slot} {STATS[i]} +{1 + i}"
        for i, stat in enumerate(STATS):
            yield {"slot": SLOTS[i % 7], "stat": stat, "value": 2, "sign": -1}, f"{SLOTS[i % 7]} {stat} -2"
        for value in VALUES:
            for sign in (1, -1):
                yield ({"slot": "WEAPON", "stat": "HUMAN", "value": value, "sign": sign},
                       f"WEAPON HUMAN {'+' if sign > 0 else '-'}{value}")
    elif kind == "damage":
        # HP and MP, damage and healing (four colours, both bounces), one to three figures; each stat's
        # icon, its sign and drift both ways
        for pool in ("hp", "mp"):
            for sign in (-1, 1):
                for value in VALUES:
                    yield {"pool": pool, "sign": sign, "value": value}, str(value)
        for stat in range(6):
            for sign in (-1, 1):
                yield {"pool": "stat", "stat": stat, "sign": sign, "value": 12 + stat}, str(12 + stat)


def stats_address(g):
    """Ashley's HUD record's currentHP (vs_battle_actors[0] -> +0x3c -> +0x18: HP, max, MP, max, risk)."""
    symbols = rood_rpc.symbols()
    actor = int.from_bytes(g.read(symbols["vs_battle_actors"], PTR), "little")
    record = int.from_bytes(g.read(actor + 0x3C, 4), "little")
    return record + 0x18


def tour_gauges(g, check, counters):
    """The gauges through their states and the display's slide; the ages are the states compared."""
    import struct
    at = stats_address(g)
    original = g.read(at, 10)
    covered = []
    try:
        for hp, max_hp, mp, max_mp, risk in GAUGES:
            g.write(at, struct.pack("<hhhhH", hp, max_hp, mp, max_mp, risk))
            before = counters("gauges")["frames"]
            ok = g.until(lambda: counters("gauges")["frames"] >= before + 2, guard=60)
            c = counters("gauges")
            if check(bool(ok), f"gauges HP {hp}/{max_hp} MP {mp}/{max_mp} risk {risk}: compared ({c['frames']} frames, "
                               f"{c['mismatched']} mismatched, '{c['text']}')"):
                covered.append({"hp": hp, "max_hp": max_hp, "mp": mp, "max_mp": max_mp, "risk": risk})
        # the slide: out (D_800EB9AF, as a menu opening sets it) until hidden, then back
        symbols = rood_rpc.symbols()
        before = counters("gauges")["frames"]
        g.write(symbols["D_800EB9AF"], b"\x01")
        out = g.until(lambda: int.from_bytes(g.read(symbols["D_800EB9B0"], 4), "little") == 0x200000, step=1, guard=60)
        g.write(symbols["D_800EB9AF"], b"\x00")
        back = g.until(lambda: int.from_bytes(g.read(symbols["D_800EB9B0"], 4), "little") == 0, step=1, guard=60)
        frames = counters("gauges")["frames"] - before
        if check(bool(out) and bool(back) and frames >= 6, f"gauges: the display slid out and back ({frames} frames "
                                                            f"compared)"):
            covered.append({"slide": "out and back"})
    finally:
        g.write(at, original)
    return covered


def tour_figure(g, check, counters):
    """Both stances, every limb condition, mixed conditions, and the game's visibility gates."""
    import struct
    symbols = rood_rpc.symbols()
    at = stats_address(g) - 0x18 + 0x398
    limbs = [at + i * 0xDC for i in range(5)]
    saved = [g.read(a, 4) for a in limbs]
    stance_at = symbols["D_800EB9CC"]
    stance = g.read(stance_at, 1)
    covered = []
    try:
        for pose in (0, 1):
            for hp in (0, 1, 2, 24, 25, 74, 75, 99, 100):
                g.write(stance_at, bytes([pose]))
                for a in limbs:
                    g.write(a, struct.pack("<hh", hp, 100))
                before = counters("figure")["frames"]
                ok = g.until(lambda: counters("figure")["frames"] >= before + 2, guard=60)
                check(bool(ok), f"figure stance {pose}, all limbs {hp}/100: compared")
                e = next(e for e in g.call("hud", action="list")["elements"] if e["kind"] == "figure" and e["live"])
                condition = 0 if hp < 2 else 1 if hp < 25 else 2 if hp < 75 else 3 if hp < 100 else 4
                check(e["stance"] == pose and all(e[f"limb_{i}_status"] == condition for i in range(5)),
                      f"figure stance {pose}, {hp}/100: read-only condition fields {condition}")
                covered.append({"stance": pose, "hp": [hp] * 5})
            for i, a in enumerate(limbs):
                g.write(a, struct.pack("<hh", [1, 24, 50, 99, 100][i], 100))
            before = counters("figure")["frames"]
            check(bool(g.until(lambda: counters("figure")["frames"] >= before + 2, guard=60)),
                  f"figure stance {pose}, mixed conditions: compared")
            e = next(e for e in g.call("hud", action="list")["elements"] if e["kind"] == "figure" and e["live"])
            check(e["stance"] == pose and [e[f"limb_{i}_status"] for i in range(5)] == list(range(5)),
                  f"figure stance {pose}: mixed read-only conditions")
            covered.append({"stance": pose, "hp": [1, 24, 50, 99, 100]})
        # Menu slide and lower-screen visibility: no hook while hidden; restored drawing afterwards.
        for name, hidden in (("D_800EB9AF", 1), ("vs_battle_lowerScreenUiState", 2)):
            address = symbols[name]
            original = g.read(address, 1)
            try:
                g.write(address, bytes([hidden]))
                before = counters("figure")["frames"]
                g.run(12)
                check(counters("figure")["frames"] == before, f"figure hidden by {name}: no packets")
                covered.append({"hidden": name})
            finally:
                g.write(address, original)
            before = counters("figure")["frames"]
            check(bool(g.until(lambda: counters("figure")["frames"] >= before + 2, guard=60)),
                  f"figure restored after {name}")
    finally:
        for a, value in zip(limbs, saved):
            g.write(a, value)
        g.write(stance_at, stance)
    return covered


def status_icons_fixture(g, statuses=0x1FFE0, phase=0, current=0, previous=255, next_icon=1):
    """Guest status bits, not a new spawner: the game still decides carousel transitions."""
    import struct
    symbols = rood_rpc.symbols()
    at = stats_address(g) - 0x18 + 0x948
    g.write(at, struct.pack("<I", statuses) + b"\xff" * 8)
    g.write(symbols["D_800EB9D0"], bytes([previous, current, next_icon, phase]))


def tour_status_icons(g, check, counters):
    """Every icon on/off and clipped, both CLUTs, stat glyphs, counts, carousel phases and speeds."""
    import struct
    symbols = rood_rpc.symbols()
    at = stats_address(g) - 0x18 + 0x948
    saved = [(at, g.read(at, 12))]
    for name, size in (("D_800EB9D0", 4), ("D_800EB9AE", 1), ("D_800EB9AF", 1),
                       ("D_800EB9B0", 4), ("vs_gametime_tickspeed", 4)):
        saved.append((symbols[name], g.read(symbols[name], size)))
    mapped = struct.unpack("<8H", g.read(symbols["D_800EBC68"], 16))
    covered = []

    def compared(label, **fields):
        before = counters("status_icons")["frames"]
        ok = g.until(lambda: counters("status_icons")["frames"] >= before + 2, step=1, guard=60)
        check(bool(ok), "status_icons " + label + ": compared")
        covered.append({"scenario": label, **fields})

    try:
        for icon in range(32):
            raw = 1 << (icon + 5) if icon < 16 else next(1 << (i + 21) for i, m in enumerate(mapped) if m & (1 << (icon - 16)))
            status_icons_fixture(g, raw, current=icon, previous=icon, next_icon=icon)
            compared(f"icon {icon} on", icon=icon, statuses=raw)
            e = next(e for e in g.call("hud", action="list")["elements"] if e["kind"] == "status_icons" and e["live"])
            check(e["current"] == icon, f"status_icons read-only current is icon {icon}")
            # Each icon exercises its own crop and (for 16..31) supplemental glyph, including zero width.
            for phase in (9, 11, 12, 15):
                status_icons_fixture(g, raw, phase=phase, current=icon, previous=icon, next_icon=icon)
                compared(f"icon {icon} clipped phase {phase}", icon=icon, phase=phase)
            status_icons_fixture(g, 0)
            before = counters("status_icons")["frames"]
            g.run(8)
            check(counters("status_icons")["frames"] == before, f"status_icons icon {icon} off: no packets")
            covered.append({"scenario": "off", "icon": icon})
        for count in (1, 2, 3, 8, 9, 16, 32):
            raw = ((1 << count) - 1) << 5 if count <= 16 else 0x1FFFFFE0
            status_icons_fixture(g, raw)
            compared(f"count {count}", count=count)
            e = next(e for e in g.call("hud", action="list")["elements"] if e["kind"] == "status_icons" and e["live"])
            check(e["count"] == count, f"status_icons read-only count is {count}")
            g.run(64)  # unpinned full carousel lives, not just held phases
        for speed in (2, 4):
            g.write(symbols["vs_gametime_tickspeed"], struct.pack("<I", speed))
            for phase in range(16):
                status_icons_fixture(g, phase=phase, current=7, previous=6, next_icon=8)
                compared(f"speed {speed} phase {phase}", speed=speed, phase=phase)
            check(int.from_bytes(g.read(symbols["vs_gametime_tickspeed"], 4), "little") == speed,
                  f"status_icons tickspeed {speed} held by game")
        # Invalidate all three slots, rotate on phase 12, and select the second indicator row.
        for previous, current, next_icon in ((31, 30, 29), (0, 8, 1), (8, 0, 8)):
            status_icons_fixture(g, phase=12, previous=previous, current=current, next_icon=next_icon)
            compared(f"slot removal/rotation {previous}/{current}/{next_icon}")
        status_icons_fixture(g)
        for hidden in (1, 0):
            g.write(symbols["D_800EB9AE"], bytes([hidden]))
            if hidden:
                before = counters("status_icons")["frames"]
                g.run(8)
                check(counters("status_icons")["frames"] == before, "status_icons disabled: no packets")
                covered.append({"scenario": "disabled"})
            else:
                compared("enabled again")
        g.write(symbols["D_800EB9AF"], b"\x01")
        check(bool(g.until(lambda: int.from_bytes(g.read(symbols["D_800EB9B0"], 4), "little") == 0x200000,
                           step=1, guard=60)), "status_icons slide out")
        g.write(symbols["D_800EB9AF"], b"\x00")
        check(bool(g.until(lambda: int.from_bytes(g.read(symbols["D_800EB9B0"], 4), "little") == 0,
                           step=1, guard=60)), "status_icons slide back")
        compared("slide out and back")
    finally:
        for a, value in saved:
            g.write(a, value)
    return covered


def battle_mode_fixture(g, mode=1, step=4, phase=0):
    symbols = rood_rpc.symbols()
    g.write(symbols["D_800EB9CC"], bytes([mode]))
    g.write(symbols["D_800EB9CD"], bytes([step]))
    g.write(symbols["D_800EBD64"], phase.to_bytes(4, "little"))


def tour_battle_mode(g, check, counters):
    """Entry, exit, packed-colour pulse extrema/wrap, and the pre-update panel displacement."""
    symbols = rood_rpc.symbols()
    saved = [(symbols[n], g.read(symbols[n], size)) for n, size in
             (("D_800EB9CC", 1), ("D_800EB9CD", 1), ("D_800EBD64", 4),
              ("D_800EB9AF", 1), ("D_800EB9B0", 4), ("vs_gametime_tickspeed", 4))]
    covered = []

    def frame(label, expected_step=None, expected_slide=None, expected_pulse=None):
        before = counters("battle_mode")["frames"]
        check(bool(g.until(lambda: counters("battle_mode")["frames"] > before, step=1, guard=60)),
              "battle_mode " + label + ": compared")
        e = next(e for e in g.call("hud", action="list")["elements"] if e["kind"] == "battle_mode" and e["live"])
        check(e["anchor"] == 2, "battle_mode right anchor")
        for key, value in (("step", expected_step), ("slide", expected_slide), ("pulse", expected_pulse)):
            if value is not None:
                check(e[key] == value, f"battle_mode {label}: {key} {e[key]} = {value}")
        covered.append({"scenario": label, "step": e["step"], "slide": e["slide"], "pulse": e["pulse"]})

    try:
        g.write(symbols["D_800EB9AF"], b"\x00")
        g.write(symbols["D_800EB9B0"], bytes(4))
        for step in range(4):
            battle_mode_fixture(g, step=step, phase=1024)
            frame(f"entry {step + 1}", expected_step=step + 1, expected_pulse=0)
        for step in (4, 3, 2):
            battle_mode_fixture(g, mode=0, step=step)
            frame(f"exit {step - 1}", expected_step=step - 1)
        battle_mode_fixture(g, mode=0, step=1)
        before = counters("battle_mode")["frames"]
        g.run(12)
        check(counters("battle_mode")["frames"] == before, "battle_mode fully hidden: no packets")
        covered.append({"scenario": "hidden"})
        for speed in (2, 4):
            g.write(symbols["vs_gametime_tickspeed"], speed.to_bytes(4, "little"))
            for phase in (0, 1, 512, 1023, 1024, 1025, 1536, 2047, 2048, 3072, 4095):
                battle_mode_fixture(g, phase=phase)
                frame(f"speed {speed} phase {phase}", expected_step=4,
                      expected_pulse=0 if phase in (0, 2048) else 128 if phase in (1024, 3072) else None)
            g.run(64)  # an entire unpinned pulse period at each tickspeed
        for hidden in (1, 0):
            g.write(symbols["D_800EB9AF"], bytes([hidden]))
            for slide in ((0, 8, 16, 24, 32) if hidden else (32, 24, 16, 8, 0)):
                g.write(symbols["D_800EB9B0"], (slide << 16).to_bytes(4, "little"))
                battle_mode_fixture(g, phase=1024)
                frame(f"panel {'out' if hidden else 'back'} {slide}", expected_slide=slide << 16)
        battle_mode_fixture(g, step=0)
        g.run(40)
        covered.append({"scenario": "uninterrupted entry and pulse"})
        battle_mode_fixture(g, mode=0)
        g.run(20)
        covered.append({"scenario": "uninterrupted exit"})
    finally:
        for at, value in saved:
            g.write(at, value)
    return covered


def label_pool(g):
    """The label records (D_800F1BAC: 24-byte D_800F1BAC_t) and their count (D_800F227E)."""
    symbols = rood_rpc.symbols()
    pool = int.from_bytes(g.read(symbols["D_800F1BAC"], PTR), "little")
    return pool, g.read(symbols["D_800F227E"], 1)[0]


def tour_buff(g, check, counters):
    """A buff pushed up by the buffs shown after it: the record's rise (unk12, 10 for each) written, as the
    label loop writes it when a later one appears; the spawner shows each at once (none waits behind)."""
    covered = []
    g.call("hud", action="show", kind="buff", hold=True, age=10, slot="BODY", stat="FIRE", value=3, sign=1)
    for rise in (10, 20, 30):
        pool, count = label_pool(g)
        at = next((pool + i * 24 for i in range(count) if g.read(pool + i * 24 + 8, 1)[0] in (6, 7)), None)
        if not check(at is not None, f"buff rise {rise}: its record"):
            break
        g.write(at + 0x12, rise.to_bytes(2, "little"))
        before = counters("buff")["frames"]
        ok = g.until(lambda: counters("buff")["frames"] >= before + 2, guard=60)
        e = next((e for e in g.call("hud", action="list")["elements"] if e["kind"] == "buff" and e["live"]), None)
        check(bool(ok) and e is not None and e["rise"] == rise, f"buff BODY FIRE +3 risen {rise}: compared, read-only "
                                                                f"rise {e and e['rise']}")
        covered.append({"slot": "BODY", "stat": "FIRE", "value": 3, "rise": rise})
    g.call("hud", action="clear")
    return covered


LABELS = 29  # the combat labels with pieces (D_800690B4: 29..31 have none)


def spawn_word(g, kind=2, flags=0, index=0, age=0):
    """A word over Ashley as the game's spawners make one (func_8009406C: kind 2, a combat label, flags bit 0 a
    lowered stat's; func_80093B68 with type 2: kind 1, MISS), appended to the label pool; returns its address.
    (D_800F227A 0: Ashley's words shown, as when no action has hidden them.)"""
    import struct
    symbols = rood_rpc.symbols()
    pool, count = label_pool(g)
    if count >= 30:
        return None
    g.write(symbols["D_800F227A"], bytes(2))
    spread = 0x1000 if kind == 1 else 0  # (kind 1: the number's spread not yet placed)
    at = pool + count * 24
    g.write(at, struct.pack("<hhhhBBBBHBBhh4s", 0, 0, 0, 0, kind, 0, 0xFB if kind == 2 else 255, age, index, flags,
                            0, spread, spread, bytes(4)))
    g.write(symbols["D_800F227E"], (count + 1).to_bytes(2, "little"))
    return at


def keep_word(g):
    """The default and fallback checks' word: a combat label kept at age 10 (written each poll), respawned when
    gone."""
    pool, count = label_pool(g)
    for i in range(count):
        if g.read(pool + i * 24 + 8, 1)[0] == 2 and g.read(pool + i * 24 + 12, 2) == b"\x05\x00":
            g.write(pool + i * 24 + 11, bytes([10]))
            return
    spawn_word(g, index=5, age=10)


def tour_actor_word(g, check, counters):
    """Every combat label's whole life, plain and as a lowered stat's (its arrow), MISS from both of the label
    loop's MISS calls (kinds 1 and 2), and two words at once; every age compared."""
    covered, ages = [], set()

    def life(label, **spawn):
        before = counters("actor_word")["frames"]
        at = spawn_word(g, **spawn)
        if not check(at is not None, f"actor_word {label}: spawned"):
            return
        seen = set()

        def gone():
            live = [e for e in g.call("hud", action="list")["elements"] if e["kind"] == "actor_word" and e["live"]]
            seen.update(e["age"] for e in live)
            return not live and seen
        ok = g.until(gone, step=1, guard=60)
        frames = counters("actor_word")["frames"] - before
        check(bool(ok) and frames >= len(seen), f"actor_word {label}, its life: {frames} frames compared, ages "
                                                f"{min(seen) if seen else '-'}..{max(seen) if seen else '-'}")
        ages.update(seen)
        covered.append({"word": label, **spawn, "ages": sorted(seen)})

    for index in range(LABELS):
        life(f"label {index}", index=index)
        life(f"label {index} lowered", index=index, flags=1)
    life("MISS (a number's)", kind=1, flags=2)
    life("MISS (a word's)", kind=2, flags=2)
    before = counters("actor_word")["frames"]
    spawn_word(g, index=1)
    spawn_word(g, index=21, flags=1)
    g.run(20)
    elements = [e for e in g.call("hud", action="list")["elements"] if e["kind"] == "actor_word" and e["live"]]
    check(len(elements) == 2 and counters("actor_word")["frames"] >= before + 2,
          f"actor_word: two words at once ({len(elements)} live) compared")
    covered.append({"word": "two at once"})
    g.run(40)
    check(set(range(31)) <= ages, f"actor_word: every age 0..30 covered (missing {sorted(set(range(31)) - ages)})")
    return covered


TIMERS = [(0, 0, 0, 0), (0, 0, 1, 0), (1, 2, 3, 0), (4, 5, 6, 0), (7, 8, 9, 0), (9, 9, 9, 0), (10, 10, 10, 0),
          (23, 45, 67, 0), (47, 47, 47, 0), (38, 16, 82, 0), (0, 59, 99, 0), (59, 59, 98, 0), (12, 34, 56, 1)]  # minutes, seconds, hundredths, the overflow byte (59:59:99 whatever the rest)


def timer_fixture(g, minutes=0, seconds=0, hundredths=0, over=0, shown=1):
    """The time attack's timer as its state holds it: D_8005046C's seconds, minutes and overflow bytes,
    vs_main_stateFlags.puzzleTimerHundredths (+0xA0), vs_battle_lowerScreenUiState 1 (the lower screen shows
    it)."""
    symbols = rood_rpc.symbols()
    g.write(symbols["D_8005046C"] + 1, bytes([seconds, minutes, over]))
    g.write(symbols["vs_main_stateFlags"] + 0xA0, bytes([hundredths]))
    g.write(symbols["vs_battle_lowerScreenUiState"], bytes([shown]))


def tour_timer(g, check, counters):
    """Digits of every place 0..9 and their carries' boundaries, the overflow (59:59:99), then the timer
    hidden and shown again."""
    symbols = rood_rpc.symbols()
    saved = [(symbols[n] + o, g.read(symbols[n] + o, size)) for n, o, size in
             (("D_8005046C", 0, 4), ("vs_main_stateFlags", 0xA0, 1), ("vs_battle_lowerScreenUiState", 0, 1))]
    covered = []
    try:
        for minutes, seconds, hundredths, over in TIMERS:
            timer_fixture(g, minutes, seconds, hundredths, over)
            text = "59:59:99" if over else f"{minutes:02}:{seconds:02}:{hundredths:02}"
            before = counters("timer")["frames"]
            ok = g.until(lambda: counters("timer")["frames"] >= before + 2, step=1, guard=60)
            e = next((e for e in g.call("hud", action="list")["elements"] if e["kind"] == "timer" and e["live"]), None)
            check(bool(ok) and e is not None and e["text"] == text
                  and (e["minutes"], e["seconds"], e["hundredths"]) == tuple(int(text[i:i + 2]) for i in (0, 3, 6)),
                  f"timer {text}: compared, read-only fields {e and (e['minutes'], e['seconds'], e['hundredths'])}")
            covered.append({"text": text})
        timer_fixture(g, shown=0)
        before = counters("timer")["frames"]
        g.run(12)
        check(counters("timer")["frames"] == before, "timer hidden: no packets")
        covered.append({"hidden": True})
        timer_fixture(g, 3, 4, 5)
        before = counters("timer")["frames"]
        check(bool(g.until(lambda: counters("timer")["frames"] >= before + 2, step=1, guard=60)), "timer shown again")
        g.run(64)  # (unpinned: as the game keeps it)
        check(counters("timer")["frames"] >= before + 32, "timer shown, unpinned: compared")
    finally:
        for at, value in saved:
            g.write(at, value)
    return covered


def keep_band(g):
    """The name window held open (its band): shown again when gone."""
    if not any(e["kind"] == "text_band" and e["live"] for e in g.call("hud", action="list")["elements"]):
        g.call("hud", action="show", kind="name_window", hold=True)


def tour_text_band(g, check, counters):
    """The name window's band (text box 7): its whole life (opening row by row, held, closing), then held with its
    box's fields written: from the top or the bottom, each grown edge, one to four lines, each icon."""
    import struct
    symbols = rood_rpc.symbols()
    box = symbols["vs_battle_textBoxes"] + 7 * 0x30
    covered, seen = [], set()

    def compared(label, frames=2):
        before = counters("text_band")["frames"]
        ok = g.until(lambda: counters("text_band")["frames"] >= before + frames, step=1, guard=60)
        elements = [e for e in g.call("hud", action="list")["elements"] if e["kind"] == "text_band" and e["live"]]
        seen.update((e["top"], e["edge"], e["lines"], e["settled"], e["icon"]) for e in elements)
        check(bool(ok) and elements, f"text_band {label}: compared ({len(elements)} live)")
        covered.append({"scenario": label, "bands": [{k: e[k] for k in ("box", "top", "edge", "lines", "settled",
                                                                        "icon")} for e in elements]})

    for index in (184, 1):  # a Break Art's name, another action's
        before = counters("text_band")["frames"]
        g.call("hud", action="show", kind="name_window", hold=False, index=index)
        ok = g.until(lambda: not any(e["kind"] == "text_band" and e["live"]
                                     for e in g.call("hud", action="list")["elements"])
                     and counters("text_band")["frames"] > before, step=1, guard=60)
        check(bool(ok) and counters("text_band")["frames"] >= before + 20,
              f"text_band: action {index}'s name window, its life ({counters('text_band')['frames'] - before} frames)")
        covered.append({"scenario": f"life {index}"})
    g.call("hud", action="show", kind="name_window", hold=True)
    compared("held")
    flags = struct.unpack("<I", g.read(box, 4))[0]
    y, lines = g.read(box + 0x16, 2), g.read(box + 0x21, 1)
    try:
        for top in (1, 0):
            for count in (1, 2, 3, 4):
                for icon in (0, 1, 2, 3, 4):
                    g.write(box + 0x16, struct.pack("<h", 0 if top else 1))
                    g.write(box + 0x21, bytes([count]))
                    g.write(box, struct.pack("<I", (flags & ~0xF00) | icon << 8))
                    compared(f"{'top' if top else 'bottom'}, {count} lines, icon {icon}")
    finally:
        g.write(box, struct.pack("<I", flags))
        g.write(box + 0x16, y)
        g.write(box + 0x21, lines)
        g.call("hud", action="clear")
    return covered


def targeting(g, on=True):
    """Targeting up (Circle, Circle in the save room: battle mode, then the target sphere; with no enemy about the
    information box says so, D_800EABAA 16) or left (Cross: out of it, then the weapon sheathed). The rows are the
    gate's to write (target_row)."""
    symbols = rood_rpc.symbols()

    def up():
        return int.from_bytes(g.read(symbols["D_800EB9B8"], 4), "little") != 0
    if up() != on:
        g.call("input", buttons=[], vsyncs=2)  # (a fixture's held L2 + R2 let go first)
        g.run(4)
    for _ in range(6):
        if up() == on and (on or not g.call("weapon_slots")["weapon_drawn"]):
            return True
        g.press("circle" if on else "cross", vsyncs=3, settle=40)
    return up() == on


ROW = 0x990  # D_800EB9B8's rows (unk990: 24 of 20 bytes)


def limb_names(g):
    """The parts' names as the rows show them (vs_battle_limbNames: guest addresses; '#' right-aligns)."""
    symbols = rood_rpc.symbols()
    pointer = 8 if pathlib.Path(rood_rpc.BINARY).read_bytes()[4] == 2 else 4  # (ELF class)
    names = []
    for i in range(16):
        at = int.from_bytes(g.read(symbols["vs_battle_limbNames"] + pointer * i, pointer), "little")
        if at >> 24 != 0x80:
            break
        names.append((at, g.read(at, 16).split(b"\0")[0].decode("latin-1")))
    return names


def write_row(g, i, state=1, selected=0, fade=8, age=0, x=248, y=32, bank=0, text=0):
    """Row i as func_800C5798 writes one (state 1 shown, 2 coming in from the right / going out to the left, 3 the
    other way; the 3D point the origin), and the fields the targeting steps (selected, the plate's fade, the line's
    age)."""
    import struct
    base = int.from_bytes(g.read(rood_rpc.symbols()["D_800EB9B8"], 4), "little")
    g.write(base + ROW + 20 * i, struct.pack("<BBBBhbbhhhhI", state, selected, fade, age, x, y, bank, 0, 0, 0, 0,
                                             text))


def row_state(g, i):
    base = int.from_bytes(g.read(rood_rpc.symbols()["D_800EB9B8"], 4), "little")
    return g.read(base + ROW + 20 * i, 1)[0]


def clear_rows(g):
    base = int.from_bytes(g.read(rood_rpc.symbols()["D_800EB9B8"], 4), "little")
    if base:
        g.write(base + ROW, bytes(24 * 20))


def tour_target_row(g, check, counters):
    """Every part's name on a row's whole life from either side (its slide in, the plate's fade from 8 to 0, its
    slide out), selected (the full plate, white) on both sides and banks, the list's rows at once."""
    covered = []
    if not check(targeting(g), "target_row: targeting up"):
        return covered
    names = limb_names(g)
    check(len(names) >= 6, f"target_row: the parts' names ({[n for _, n in names]})")
    fades, sides = set(), set()
    try:
        for k, (at, name) in enumerate(names):
            for side, (state, x, out) in enumerate(((2, 320, 3), (3, -72, 2))):
                clear_rows(g)
                bank = (k + side) & 1
                before = counters("target_row")["frames"]
                write_row(g, 0, state=state, x=x, y=32 + 11 * (k % 8), bank=bank, text=at)
                seen = set()

                def faded():  # (slid in, its plate's fade down to 0)
                    seen.update(e["fade"] for e in g.call("hud", action="list")["elements"]
                                if e["kind"] == "target_row" and e["live"])
                    return row_state(g, 0) == 1 and 0 in seen
                g.until(faded, step=1, guard=40)
                base = int.from_bytes(g.read(rood_rpc.symbols()["D_800EB9B8"], 4), "little")
                g.write(base + ROW, bytes([out]))  # (as func_800C56C0 sends them off)
                gone = g.until(lambda: row_state(g, 0) == 0, step=1, guard=40)
                frames = counters("target_row")["frames"] - before
                check(bool(gone) and frames >= 8, f"target_row {name!r} from the {('right', 'left')[side]}, bank "
                                                   f"{bank}: its life, {frames} frames compared")
                fades |= seen
                sides.add(side)
                covered.append({"name": name, "side": ("right", "left")[side], "bank": bank, "frames": frames})
        for side, x in ((0, 248), (1, -4)):
            for bank in (0, 1):
                clear_rows(g)
                before = counters("target_row")["frames"]
                for i in range(4):
                    write_row(g, i, selected=int(i == 1), x=x, y=32 + 11 * i, bank=bank ^ (i & 1),
                              text=names[1 + i % (len(names) - 1)][0])
                g.run(12)
                elements = [e for e in g.call("hud", action="list")["elements"] if e["kind"] == "target_row"
                            and e["live"]]
                frames = counters("target_row")["frames"] - before
                check(frames >= 16 and any(e["selected"] for e in elements),
                      f"target_row: four rows on the {('right', 'left')[side]}, bank {bank}, one selected: {frames} "
                      f"frames compared")
                covered.append({"rows": 4, "side": ("right", "left")[side], "bank": bank, "selected": 1})
        # deselected: the plate fades from the selected row's 7 to 0
        clear_rows(g)
        write_row(g, 0, selected=1, text=names[1][0])
        g.run(4)
        base = int.from_bytes(g.read(rood_rpc.symbols()["D_800EB9B8"], 4), "little")
        g.write(base + ROW + 1, b"\0")
        seen = set()
        g.until(lambda: seen.update(e["fade"] for e in g.call("hud", action="list")["elements"]
                                    if e["kind"] == "target_row" and e["live"]) or 0 in seen, step=1, guard=30)
        check(set(range(8)) <= seen, f"target_row: deselected, the plate faded 7..0 ({sorted(seen)})")
        covered.append({"deselected": sorted(seen)})
    finally:
        clear_rows(g)
        check(targeting(g, False), "target_row: targeting left")
    check(sides == {0, 1} and set(range(9)) <= fades, f"target_row: both sides, every fade 0..8 ({sorted(fades)})")
    return covered


LINE_AGES = [0, 1, 2, 3, 9, 16, 17, 18, 19, 25, 31, 33]  # (the sweep in; from 18 the second line, its count wrapped)


def tour_target_line(g, check, counters):
    """A selected row's leader lines from points about Ashley (in front, behind, to either side, above: the lines'
    every direction and odd and even lengths) to a row on either side, at the ages of their sweep (every age 0..33
    for the first point: the game's own run, 0x22 wrapping to 0x12 in its state 19)."""
    import struct
    covered, ages, sides = [], set(), set()
    if not check(targeting(g), "target_line: targeting up"):
        return covered
    names = limb_names(g)
    base = int.from_bytes(g.read(rood_rpc.symbols()["D_800EB9B8"], 4), "little")
    cx, cy, cz = struct.unpack("<hhh", g.read(base + 8, 6))  # (the sphere's centre: Ashley)
    points = [(cx + dx, cy + dy, cz + dz) for dx in (-768, 0, 768) for dy in (-512, 0) for dz in (-768, 0, 768)]
    try:
        for side, x in ((0, 248), (1, -4)):
            for k, point in enumerate(points):
                clear_rows(g)
                write_row(g, 0, selected=1, x=x, y=32 + 11 * (k % 8), bank=k & 1, text=names[1 + k % (len(names) - 1)][0])
                g.write(base + ROW + 8, struct.pack("<hhh", *point))
                before = counters("target_line")["frames"]
                for age in (range(34) if k == 0 else LINE_AGES):
                    g.write(base + ROW + 3, bytes([age]))
                    at = counters("target_line")["frames"]
                    if g.until(lambda: counters("target_line")["frames"] >= at + 1, step=1, guard=20):
                        e = next((e for e in g.call("hud", action="list")["elements"]
                                  if e["kind"] == "target_line" and e["live"]), None)
                        if e and e["age"] == age:
                            ages.add(age)
                frames = counters("target_line")["frames"] - before
                states = 34 if k == 0 else len(LINE_AGES)
                check(frames >= states, f"target_line from {point} to a row on the {('right', 'left')[side]}: "
                                        f"{frames} frames compared ({states} ages)")
                sides.add(side)
                covered.append({"point": point, "side": ("right", "left")[side], "frames": frames})
    finally:
        clear_rows(g)
        check(targeting(g, False), "target_line: targeting left")
    check(sides == {0, 1} and set(range(34)) <= ages, f"target_line: both sides, every age 0..33 "
                                                       f"(missing {sorted(set(range(34)) - ages)})")
    return covered


def keep_line(g):
    """The default and fallback checks' lines: targeting up, a selected row at age 20 (both lines) kept."""
    if not targeting(g):
        return
    if not any(e["kind"] == "target_line" and e["live"] for e in g.call("hud", action="list")["elements"]):
        write_row(g, 0, selected=1, age=20, text=limb_names(g)[1][0])


def keep_row(g):
    """The default and fallback checks' row: targeting up, a selected row kept (written when gone)."""
    if not targeting(g):
        return
    if not any(e["kind"] == "target_row" and e["live"] for e in g.call("hud", action="list")["elements"]):
        write_row(g, 0, selected=1, text=limb_names(g)[1][0])


KEEP = {"actor_word": keep_word, "emote": lambda g: keep_emotes(g), "text_band": keep_band,
        "target_row": keep_row, "target_line": keep_line}  # kinds without a show

EMOTES = [1, 2, 3, 4, 5, 6, 7, 0x10]  # the dots, the shout's '!', the heart, the surprise, '?' alone; 0x10 a balloon
# with no case of its own (func_80095C18's switch has none: the balloon alone)
OFFSETS = [(dx, dy, dz) for dx in (-6000, 0, 6000) for dy in (-4000, 0, 4000) for dz in (-6000, 0, 6000)]


def emote_actors(g):
    """The opening's actors with a record (D_800F4538, past Ashley): their positions' addresses."""
    symbols = rood_rpc.symbols()
    found = []
    for i in range(1, 16):
        actor = int.from_bytes(g.read(symbols["D_800F4538"] + PTR * i, PTR), "little")
        if actor:
            found.append((i, actor + 0x1C))  # (unk0.position)
    return found


def tour_emote(g, check, counters):
    """Each emote on every actor of the opening, both sides of each balloon (the side bits written either
    way), then the off-screen arrow: an emoting actor moved (its position written each frame) through a grid
    of places around and beyond the view, every direction it reaches compared; a far actor's sprite."""
    import struct
    symbols = rood_rpc.symbols()
    kinds_at, sides_at = symbols["D_800F2260"], symbols["D_800F2274"]
    saved = [(kinds_at, g.read(kinds_at, 16)), (sides_at, g.read(sides_at, 4)),
             (symbols["D_800F2270"], g.read(symbols["D_800F2270"], 2))]
    g.write(symbols["D_800F2270"], bytes(2))
    covered, seen = [], set()

    def live():
        return [e for e in g.call("hud", action="list")["elements"] if e["kind"] == "emote" and e["live"]]

    def compared(label, frames=2, keep=None):
        before = counters("emote")["frames"]

        def done():
            if keep:
                keep()
            return counters("emote")["frames"] >= before + frames
        ok = g.until(done, step=1, guard=60)
        elements = live()
        seen.update((e["draw"], e["emote"], e["side"], e["direction"]) for e in elements)
        check(bool(ok) and elements, f"emote {label}: compared ({len(elements)} live, "
                                     f"{counters('emote')['frames'] - before} frames)")
        covered.append({"scenario": label, "elements": [{k: e[k] for k in ("draw", "emote", "side", "z",
                                                                            "direction", "frame", "value")}
                                                        for e in elements]})
        return elements

    try:
        actors = emote_actors(g)
        check(len(actors) > 0, f"emote: the opening's actors {[i for i, _ in actors]}")
        for kind in EMOTES:
            for sides in (0, 0xFFFF):
                g.write(kinds_at, bytes([0] + [kind] * 15))
                g.write(sides_at, struct.pack("<I", sides))
                compared(f"kind {kind:#x}, side bits {sides:#x}")
        balloons = {(k, s) for d, k, s, _ in seen if d == "balloon"}
        check({1, 2, 3, 4, 5, 6} <= {k for k, _ in balloons} and any(d == "balloon" and k == 7 for d, k, _, _ in seen),
              f"emote: every kind drawn ({sorted(balloons)})")
        check({s for _, s in balloons} >= {0, 1, 2, 3, 4, 5}, f"emote: all six balloon sides drawn "
                                                             f"({sorted({s for _, s in balloons})})")
        # the arrow: kind 4 on one actor, moved
        i, at = actors[0]
        home = g.read(at, 6)
        g.write(kinds_at, bytes([0] + [4 if a == i else 0 for a in range(1, 16)]))
        x0, y0, z0 = struct.unpack("<hhh", home)
        try:
            for dx, dy, dz in OFFSETS:
                place = struct.pack("<hhh", max(-32768, min(32767, x0 + dx)), max(-32768, min(32767, y0 + dy)),
                                    max(-32768, min(32767, z0 + dz)))
                compared(f"actor {i} moved {dx}, {dy}, {dz}", keep=lambda: g.write(at, place))
        finally:
            g.write(at, home)
        directions = {d for draw, _, _, d in seen if draw == "arrow"}
        check(len(directions) > 0, f"emote: off-screen arrows drawn, directions {sorted(directions)}")
        # a far actor's sprite (kind with bit 8): only where the actor's motion gives it a frame
        g.write(kinds_at, bytes([0] + [8] * 15))
        g.run(8)
        far = [e for e in live() if e["draw"] == "far"]
        covered.append({"scenario": "far", "drawn": len(far)})
        print(f"note: far sprites drawn: {len(far)}", flush=True)
    finally:
        for address, value in saved:
            g.write(address, value)
    return covered, seen


def keep_emotes(g):
    symbols = rood_rpc.symbols()
    g.write(symbols["D_800F2270"], bytes(2))
    g.write(symbols["D_800F2260"], bytes([0] + [1] * 15))


def ability_icons_fixture(g, weapon_mode=False):
    g.call("settings", apply=f"qol_weapon_slots {int(weapon_mode)}")
    g.call("input", buttons=["l2", "r2"], vsyncs=2000)
    g.run(24)


def bind_test_weapon_slots(g):
    """Only the launched game's inventory: two occupied slots, the rest empty; caller restores it."""
    at = rood_rpc.symbols()["vs_main_inventory"]
    saved = g.read(at, 0x280)
    data = bytearray(saved)
    assigned = 0
    for i in range(8):
        data[i * 32 + 7] = 0
        if data[i * 32 + 1] and assigned < 2:
            assigned += 1
            data[i * 32 + 7] = assigned
    g.write(at, data)
    return at, saved, assigned


def tour_ability_icons(g, check, counters):
    """Nine emit groups, real C eligibility, animation, and weapon-slot side effects."""
    symbols = rood_rpc.symbols()
    actions = symbols["vs_main_actions"]
    saved_actions = g.read(actions, 256 * 52)
    mp_at = stats_address(g) + 4
    old_mp = g.read(mp_at, 2)
    status_at = stats_address(g) - 0x18 + 0x948
    old_status = g.read(status_at, 12)
    inventory_at, old_inventory, assigned = bind_test_weapon_slots(g)
    check(assigned > 0, "ability_icons occupied weapon-slot fixture")
    covered, seen_availability = [], set()

    def compared(label):
        before = counters("ability_icons")["frames"]
        check(bool(g.until(lambda: counters("ability_icons")["frames"] >= before + 9, step=1, guard=60)),
              "ability_icons " + label + ": nine groups compared")
        elements = [e for e in g.call("hud", action="list")["elements"] if e["kind"] == "ability_icons" and e["live"]]
        check({e["group"] for e in elements} == set(range(9)), "ability_icons all group keys live")
        check(all(e["anchor"] == 0 for e in elements), "ability_icons centre anchored")
        seen_availability.update(e["availability"] for e in elements if e["group"] != 8)
        covered.append({"scenario": label, "groups": [{k: e[k] for k in ("group", "availability", "animation", "offset")} for e in elements]})
        return elements

    try:
        for unlocked in (False, True):
            buf = bytearray(saved_actions)
            for i in range(256):
                buf[i * 52 + 13] = (buf[i * 52 + 13] & 127) | (128 if unlocked else 0)
            g.write(actions, buf)
            ability_icons_fixture(g)
            for mp in (0, 50):
                g.write(mp_at, mp.to_bytes(2, "little"))
                compared(f"unlocked {unlocked}, MP {mp}")
            if unlocked:
                g.write(status_at, (1 << 13).to_bytes(4, "little") + b"\xff" * 8)
                elements = compared("Ward disables Break Arts")
                check(next(e for e in elements if e["group"] == 7)["availability"] == 0x140,
                      "ability_icons disabled Break Arts from C")
                g.write(status_at, old_status)
            for index in range(1, 11):
                g.write(symbols["D_800EBCE8"], index.to_bytes(1, "little"))
                elements = compared(f"unlocked {unlocked}, entry animation {index - 1}")
                check(all(e["animation"] == index - 1 for e in elements), "ability_icons exact entry index")
            g.call("input", buttons=[], vsyncs=1)
            before = counters("ability_icons")["frames"]
            check(bool(g.until(lambda: g.read(symbols["D_800EBCE8"], 1) == b"\x0a", step=1, guard=90)),
                  "ability_icons animated hide completed")
            check(counters("ability_icons")["frames"] > before, "ability_icons hide frames compared")
            before = counters("ability_icons")["frames"]
            g.run(12)
            check(counters("ability_icons")["frames"] == before, "ability_icons hidden: no hook")
            covered.append({"scenario": f"unlocked {unlocked}, hide and hidden"})
        check({0, 0x140, 0x180} <= seen_availability,
              f"ability_icons locked/disabled/enabled observed: {sorted(seen_availability)}")
        g.write(actions, saved_actions)
        for mode in (False, True):
            ability_icons_fixture(g, mode)
            ws = g.call("weapon_slots")
            check(ws["overlay"] == int(mode) and ws["field_mode"] == 6,
                  f"ability_icons real weapon mode {mode}: overlay {ws['overlay']}, field {ws['field_mode']}")
            elements = compared(f"weapon-slot mode {mode}")
            if mode:
                check(all(e["availability"] == 0 for e in elements), "weapon mode C labels return zero; their packets are retained")
            g.call("input", buttons=[], vsyncs=1)
            g.run(40)
            covered.append({"scenario": f"weapon-slot mode {mode} exit"})
    finally:
        g.call("input", buttons=[], vsyncs=1)
        g.run(40)
        g.call("settings", apply="qol_weapon_slots 0")
        g.write(actions, saved_actions)
        g.write(mp_at, old_mp)
        g.write(status_at, old_status)
        g.write(inventory_at, old_inventory)
    return covered


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="logs and reports (default pc/build/tests/classic_parity)")
    parser.add_argument("--renderer", choices=["null", "gl", "vulkan"], default="null")
    parser.add_argument("--kind", default="status,chain,gauges,figure,status_icons,battle_mode,ability_icons,buff,damage,actor_word,timer,emote,text_frame,text_band,box_text,image,target_row,target_line,target_marker,menu_row,menu_cursor,menu_command,menu_tabs,menu_background,option_value,items_category,items_status_button,equip_stats,dp_pp_bars,gem_slots,stat_page,ok_mark,storage_buttons,storage_counts,loot_capacities,discard_count,rename_grid,rename_box,file_menu,file_element,menuf,menuf_load", help="the kinds to check (comma-separated)")
    parser.add_argument("--candidate", action="store_true", help="explicitly opt in ability_icons before its default is gated")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "classic_parity"
    out.mkdir(parents=True, exist_ok=True)
    card2 = card2_fixture(out, args.card)
    with card_guard(out / "cards", source=args.card) as cards:
        MENUS_CARD2.append(cards.dir / "card2-menus.mcd")  # (the menus game's slot 2 only: the other games
        shutil.copyfile(card2, MENUS_CARD2[0])                    # run with no card 2, the guard's)
        return run(out, cards.card1, args.renderer, [k for k in args.kind.split(",") if k], args.candidate)


OPENING = ("emote", "text_frame", "box_text")  # the kinds the opening shows (first_bubble): actors, the duke's talk bubble


def tour_text_frame(g, check, counters):
    """The talk bubble's frame: every shape the frames' table has, each flip and tail, written into the open box's
    fields (func_800CD3E4 reads them; the box's place and size stay the game's), then the next lines (the
    bubble closing, opening, moving)."""
    import struct
    symbols = rood_rpc.symbols()
    boxes, table = symbols["vs_battle_textBoxes"], symbols["D_800EB588"]
    covered, seen = [], set()

    def open_box():
        for i in range(8):
            flags, state = struct.unpack_from("<I", g.read(boxes + i * 0x30, 4))[0], struct.unpack(
                "<h", g.read(boxes + i * 0x30 + 0x1C, 2))[0]
            if state != 0 and (flags & 15) in (2, 7):
                return i, flags
        return None, None

    def compared(label, frames=2):
        before = counters("text_frame")["frames"]
        ok = g.until(lambda: counters("text_frame")["frames"] >= before + frames, step=1, guard=60)
        elements = [e for e in g.call("hud", action="list")["elements"] if e["kind"] == "text_frame" and e["live"]]
        seen.update((e["shape"], e["flip"], e["tail"]) for e in elements)
        check(bool(ok) and elements, f"text_frame {label}: compared ({len(elements)} live)")
        covered.append({"scenario": label, "boxes": [{k: e[k] for k in ("box", "box_x", "box_y", "box_w", "box_h",
                                                                        "shape", "flip", "tail")} for e in elements]})

    box, flags = open_box()
    if not check(box is not None, f"text_frame: a talk bubble open (box {box})"):
        return covered, seen
    shapes = [k for k in range(16) if int.from_bytes(g.read(table + k * PTR, PTR), "little")]
    try:
        for shape in shapes:
            for flip in range(4):
                for tail in (0, 1, 2, 3):
                    g.write(boxes + box * 0x30, struct.pack("<I", (flags & ~0xFFC0) | flip << 6 | shape << 8
                                                            | tail << 12 | (flags & 0xFFFF0000)))
                    compared(f"box {box} shape {shape} flip {flip} tail {tail}")
    finally:
        g.write(boxes + box * 0x30, struct.pack("<I", flags))
    check(len({sh for sh, _, _ in seen}) == len(shapes), f"text_frame: every shape drawn {sorted({sh for sh, _, _ in seen})}"
                                                         f" of {shapes}")
    for line in range(2):  # the next lines: the bubble closes, the next one opens and zooms in
        before = counters("text_frame")["frames"]
        g.press("circle", vsyncs=3, settle=0)
        g.run(300)
        check(counters("text_frame")["frames"] > before + 20, f"text_frame: line {line + 2}, its bubble's life compared "
                                                              f"({counters('text_frame')['frames'] - before} frames)")
        covered.append({"scenario": f"line {line + 2}"})
    return covered, seen


def tour_box_text(g, check, counters):
    """The talk bubble's text: the duke's line set (its zoom over), held; then the next lines typing out glyph by
    glyph (the typewriter's every step), the continuation arrow's frames; then each colour style and font table
    written into the open box's flags (the printer reads them)."""
    covered, seen = [], set()

    def compared(label, frames=2):
        before = counters("box_text")["frames"]
        ok = g.until(lambda: counters("box_text")["frames"] >= before + frames, step=1, guard=60)
        elements = [e for e in g.call("hud", action="list")["elements"] if e["kind"] == "box_text" and e["live"]]
        seen.update((e["box"], e["value"]) for e in elements)
        check(bool(ok) and elements, f"box_text {label}: compared ({[e['value'] for e in elements]} glyphs)")
        covered.append({"scenario": label, "glyphs": [e["value"] for e in elements]})

    import struct
    compared("the duke's line", frames=20)
    boxes = rood_rpc.symbols()["vs_battle_textBoxes"]
    box = next(e["box"] for e in g.call("hud", action="list")["elements"] if e["kind"] == "box_text" and e["live"])
    flags, brightness = struct.unpack("<I", g.read(boxes + box * 0x30, 4))[0], g.read(boxes + box * 0x30 + 0x1E, 1)
    try:  # (the printer's font table and colour style follow the box's kind; its brightness the box's selection)
        for kind, light in ((2, 0x40), (7, 0x80), (7, 0x40), (5, 0x80)):
            g.write(boxes + box * 0x30, struct.pack("<I", (flags & ~15) | kind))
            g.write(boxes + box * 0x30 + 0x1E, bytes([light]))
            compared(f"box {box} as kind {kind}, brightness {light:#x}")
    finally:
        g.write(boxes + box * 0x30, struct.pack("<I", flags))
        g.write(boxes + box * 0x30 + 0x1E, brightness)
    before = counters("box_text")["frames"]
    g.press("circle", vsyncs=3, settle=0)
    g.run(240)
    check(counters("box_text")["frames"] > before + 20, f"box_text: the next line typed out glyph by glyph and held "
                                                        f"({counters('box_text')['frames'] - before} frames)")
    covered.append({"scenario": "the next line, typed out"})
    return covered, seen


NEW_GAME = ("image",)  # the kinds New Game's opening shows before the first bubble: its cards


def run_new_game(out, env, check, waivers, kinds):
    """The images (func_800CCA90's tiled drawer): New Game's opening cards (the title, no card: Start, Circle). From
    the first card's first frame (a mark): the default and the fallback, state parity, then back to the mark and
    every frame of the cards' lives compared."""
    g = launch_at("title", card=str(out / "no-card.mcd"), fast=True, log=out / "game-new-game.log", extra_env=env)
    try:
        g.call("classic", parity=False)

        def counters(kind):
            return g.call("classic")["kinds"][kind]

        def images():
            return [e for e in g.call("hud", action="list")["elements"] if e["kind"] == "image" and e["live"]]
        g.press("start", vsyncs=4, settle=90)
        g.press("circle", vsyncs=3, settle=0)
        check(bool(g.until(lambda: images(), step=1, guard=120)), "image: the opening's first card drawn")
        g.pause()
        g.mark()
        default_and_fallback(g, "image", check)
        # state parity (§3.4) from the mark
        symbols = rood_rpc.symbols()
        pointer = 8 if pathlib.Path(rood_rpc.BINARY).read_bytes()[4] == 2 else 4
        masks = [[0x1F800000, 4]]
        for name, size in (("D_8005E0C0", 0x20000), ("D_80055C80", 0x822 * 4)):
            for i in range(2):
                address = int.from_bytes(g.read(symbols[name] + pointer * i, pointer), "little")
                masks.append([address, size])
        masks += [[a, n] for a, n, name in Symbols(rood_rpc.BINARY).rows if name == "last_vsync" and n == 2 * pointer]
        masks += packet_masks(symbols, pointer)
        g.rewind()
        drawn = g.call("hud", action="stats")["kinds"]["image"]["drawn"]
        g.digest("record", ticks=120, masks=masks)
        g.until(lambda: g.digest("result")["recorded"] >= 120, guard=120, what="the digest recorded")
        drawn = g.call("hud", action="stats")["kinds"]["image"]["drawn"] - drawn
        g.rewind()
        g.call("settings", apply="hud_classic_image 0")
        fallback = g.call("hud", action="stats")["kinds"]["image"]["fallback"]
        g.digest("compare")
        g.until(lambda: g.digest("result")["ticks"] >= 120, guard=120, what="the digest compared")
        result = g.digest("result")
        fallback = g.call("hud", action="stats")["kinds"]["image"]["fallback"] - fallback
        g.call("settings", apply="hud_classic_image 1")
        check(drawn > 0 and fallback > 0 and result.get("error") == 0 and result.get("first_tick") == -1
              and result.get("ticks") == result.get("recorded"),
              f"state parity image: {result.get('ticks')} of {result.get('recorded')} vsyncs the same (rood.classic "
              f"drew {drawn}, the game {fallback}; first difference {result.get('first_tick')} "
              f"{result.get('first_pages')})")
        # the gate: from the mark, the cards' lives
        g.rewind()
        g.call("classic", parity=True, reset=True)
        covered, seen = [], set()
        g.until(lambda: images(), step=1, guard=30)
        for card in range(3):
            before = counters("image")["frames"]
            ages = set()

            def gone():
                live = images()
                ages.update((e["image"], e["drawer"]) for e in live)
                return not live
            ok = g.until(gone, step=1, guard=120)
            frames = counters("image")["frames"] - before
            check(bool(ok) and frames > 10, f"image: card {card + 1}, its life: {frames} frames compared "
                                            f"({sorted(ages)})")
            seen |= ages
            covered.append({"scenario": f"card {card + 1}", "frames": frames, "images": sorted(map(list, ages))})
            if not g.until(lambda: images(), step=2, guard=30):
                break
        report = opening_report(g, check, waivers, "image", covered, seen)
        g.call("classic", parity=False)
        (out / "image.json").write_text(json.dumps(report, indent=1))
        print(f"report: {out / 'image.json'}: {report['frames']} frames, {report['states']} states, "
              f"{'green' if report['green'] else 'RED'}")
    finally:
        g.close()


def area_target(g):
    """Explosion's area target from the save room (the original menus: Triangle, Magick, Warlock, Explosion; the
    spells learned and MP full as ui_magick_smoke's prepare writes them): the sphere opened and the marker up."""
    import ui_magick_smoke
    symbols = rood_rpc.symbols()

    class Caster(ui_magick_smoke.Session):
        def __init__(self, game):
            self.name, self.out, self.shots, self.g, self.sym = "marker", None, False, game, symbols
    b = Caster(g)
    b.prepare()
    g.press("triangle", vsyncs=3, settle=10)
    b.wait_ready(0)
    b.nav(range(0, 10), 0)
    g.press("circle", vsyncs=3, settle=10)
    b.wait_ready(10)
    b.nav(range(10, 15), 10)
    g.press("circle", vsyncs=3, settle=10)
    b.wait_ready(20)
    g.run(10)
    warlock = [i for i in g.read(symbols["vs_battle_warlockSpellIds"], 18) if b.learned(i)]
    b.nav(range(20, 20 + len(warlock)), 20 + warlock.index(ui_magick_smoke.EXPLOSION))
    for _ in range(4):
        g.press("circle", vsyncs=3, settle=1)
        if b.wait(lambda: b.targeting() and b.menu() == ui_magick_smoke.CLOSED, 120, "", step=2, quiet=True) is not None:
            break
    return g.until(lambda: any(e["kind"] == "target_marker" and e["live"]
                               for e in g.call("hud", action="list")["elements"]), step=2, guard=120)


def run_marker(out, card, env, check, waivers):
    """The area target's marker (func_800C28AC), its own game from the save room: Explosion's target, the marker
    moved about (the d-pad: pitch and turn; the stick), every brightness step, locked (the first stage, or Square
    held) and with its brackets (Circle: the second stage); default, fallback and state parity there."""
    g = launch_at("save_room", card=card, fast=True, log=out / "game-marker.log", extra_env=env)
    kind = "target_marker"
    try:
        def counters():
            return g.call("classic")["kinds"][kind]

        def marker():
            return next((e for e in g.call("hud", action="list")["elements"] if e["kind"] == kind and e["live"]), None)
        g.call("classic", parity=True, reset=True)
        if not check(bool(area_target(g)), "target_marker: Explosion's area target up"):
            return
        covered, steps, places = [], {}, set()

        def watch(lock, vsyncs, buttons=()):
            before = counters()["frames"]
            for _ in range(vsyncs // 2):
                if buttons:
                    g.hold(list(buttons), 2)
                else:
                    g.run(2)
                e = marker()
                if e and e["lock"] == lock:
                    steps.setdefault(lock, set()).add(e["step"])
                    places.add((e["call_x"], e["call_y"]))
            frames = counters()["frames"] - before
            covered.append({"lock": lock, "buttons": list(buttons), "frames": frames})
            return frames
        for stage, lock in ((0, True), (1, False)):
            if stage == 1:
                g.press("circle", vsyncs=3, settle=4)  # (the second stage: the brackets)
            frames = watch(lock, 40)
            for buttons in (["up"], ["down"], ["left"], ["right"], ["up", "left"], ["down", "right"]):
                frames += watch(lock, 16, buttons)
            check(frames >= 60 and set(range(16)) <= steps.get(lock, set()),
                  f"target_marker {'locked' if lock else 'with its brackets'}: {frames} frames compared, steps "
                  f"{sorted(steps.get(lock, set()))}")
        frames = watch(True, 20, ["square"])  # (Square held: locked in the second stage too)
        check(frames >= 8, f"target_marker, Square held: locked, {frames} frames compared")
        check(len(places) >= 8, f"target_marker: moved about ({len(places)} places)")
        report = opening_report(g, check, waivers, kind, covered, set())
        g.call("classic", parity=False)
        default_and_fallback(g, kind, check)
        # state parity (§3.4): from a mark with the brackets up
        masks = state_masks(g)
        g.run(2)
        g.pause()
        g.mark()
        g.rewind()
        drawn = g.call("hud", action="stats")["kinds"][kind]["drawn"]
        g.digest("record", ticks=120, masks=masks)
        g.until(lambda: g.digest("result")["recorded"] >= 120, guard=120, what="the digest recorded")
        drawn = g.call("hud", action="stats")["kinds"][kind]["drawn"] - drawn
        g.rewind()
        g.call("settings", apply=f"hud_classic_{kind} 0")
        fallback = g.call("hud", action="stats")["kinds"][kind]["fallback"]
        g.digest("compare")
        g.until(lambda: g.digest("result")["ticks"] >= 120, guard=120, what="the digest compared")
        result = g.digest("result")
        fallback = g.call("hud", action="stats")["kinds"][kind]["fallback"] - fallback
        g.call("settings", apply=f"hud_classic_{kind} 1")
        check(drawn > 0 and fallback > 0 and result.get("error") == 0 and result.get("first_tick") == -1
              and result.get("ticks") == result.get("recorded"),
              f"state parity target_marker: {result.get('ticks')} of {result.get('recorded')} vsyncs the same "
              f"(rood.classic drew {drawn}, the game {fallback}; first difference {result.get('first_tick')} "
              f"{result.get('first_pages')})")
        (out / f"{kind}.json").write_text(json.dumps(report, indent=1))
        print(f"report: {out / (kind + '.json')}: {report['frames']} frames, {report['states']} states, "
              f"{'green' if report['green'] else 'RED'}")
    finally:
        g.close()


ITEMS_PANELS = ("items_category", "items_status_button",  # the Items screen's own (MENU3: Items only), and
                "equip_stats", "dp_pp_bars", "gem_slots",   # MAINMENU's equipment panels and a grip's gem slots there,
                "stat_page")                                # the class / affinity / type table
SCREEN_PANELS = ("ok_mark", "storage_buttons",  # the other screens' own: Setup's and Storage's OK mark, Storage's
                 "storage_counts", "loot_capacities",  # button strip and counts, Loot's capacities strip, and a misc
                 "discard_count",  # item's discard amount (Items, Storage: not at the Items list's rest), the rename
                 "rename_grid", "rename_box",  # grid and its highlight boxes (MENU8 from Setup's Rename), MENU7's file
                 "file_menu", "file_element",  # menu (drawn at once: Data's SAVE / LOAD, the save list after Storage),
                 "menuf", "menuf_load")  # MENUF's screens (RPC menuf: the congratulations, the time attack, the cube puzzle)
PICKER_WALK = ("up",) * 3 + ("left",) + ("up",) * 2 + ("left",) + ("up",) + ("right",) * 2 + ("down",) * 2
# (a misc item's discard amount picker: the count up by ones, tens, hundreds (to its maximum), back down)
MENUS = ("menu_row", "menu_cursor", "menu_command", "menu_tabs", "menu_background",  # MAINMENU's shared frame
         "option_value") + ITEMS_PANELS + SCREEN_PANELS  # (and our Option pages' value column)
MENU_TOURED = ("menu_row", "menu_cursor", "menu_command", "menu_tabs", "menu_background",  # (Items: the tabs,
               "option_value") + ITEMS_PANELS + SCREEN_PANELS                             # the background)
STATUS_VIEW_PANELS = ("equip_stats", "dp_pp_bars", "gem_slots", "stat_page")  # (drawn in a status view, not the list)
ITEMS_ONLY = ("menu_tabs", "menu_background") + ITEMS_PANELS + SCREEN_PANELS  # (not on the Option screen: their default and
                                                              # fallback in Items)
OPTION_COMMAND, VANILLA_OPTIONS = 7, 9  # the top level's Option row (MENU8), its nine rows of the game's options


ITEMS_COMMAND, ITEMS_MENU = 3, 4  # the top level's Items row; its currentMenu (MENU3's screen)
INTRO_STATE = 0xAB  # vs_main_stateFlags.introState: under 3 (the prologue) Manage's Equip is unselectable
ITEM_CATEGORY = 0x3A  # D_800F4EE8.unk3A.currentItemCategory: the category + 1 (0: the Manage page, category 7)


def items_open(g, symbols):
    """Triangle, Items: MENU3 (its rows at rest)."""
    from menu_nav import top_command
    g.press("triangle", vsyncs=3, settle=30)
    top_command(g, symbols, ITEMS_COMMAND)
    for _ in range(5):  # (Circle once the top level takes it)
        g.press("circle", vsyncs=2, settle=2)
        if g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] & 0x3F == ITEMS_MENU, guard=10):
            break
    items_ready(g, symbols)


def items_live(g, symbols):
    """The live rows' ids (vs_battle_menuItems: 20.. a list, 32.. an action menu, ui_items_smoke's)."""
    p = int.from_bytes(g.read(symbols["vs_battle_menuItems"], PTR), "little")
    raw = g.read(p, 64 * 40) if p else b""
    return [i for i in range(len(raw) // 64) if raw[i * 64]]


def items_ready(g, symbols, limit=400):
    """Every live row idle (state 0 or 1) three frames in a row (ui_items_smoke's wait), MENU3 up."""
    calm = 0
    for _ in range(limit):
        p = int.from_bytes(g.read(symbols["vs_battle_menuItems"], PTR), "little")
        raw = g.read(p, 64 * 40) if p else b""
        live = [raw[i * 64] for i in range(len(raw) // 64) if raw[i * 64]]
        calm = calm + 1 if live and all(st <= 1 for st in live) else 0
        if calm >= 3:
            g.run(2)
            return
        g.run(1)
    raise RpcError(f"Items: the rows never came to rest (currentMenu {g.read(symbols['vs_battle_menuState'], 1)[0]:#x})")


def tour_items(g, symbols, toured, frames):
    """The Items screen (MENU3) through the original menus, for the shared frame's kinds: opened; the Manage page's
    Equip (its parts walked, the weapon's list in and out); every category in turn with R1 (the tab arrows; the
    category's list walked down past its end and back, the row's status view (Square) in and out, a row picked up and
    its actions walked, the Yes / No of its Discard opened and left on No); back round with L1; closed."""
    def press(button, settle=6):
        g.press(button, vsyncs=3, settle=settle)

    def category():
        return (g.read(symbols["D_800F4EE8"] + ITEM_CATEGORY, 1)[0] - 1) & 7

    def rows_now():  # (the live rows and their places: a status view slides them)
        p = int.from_bytes(g.read(symbols["vs_battle_menuItems"], PTR), "little")
        raw = g.read(p, 64 * 40) if p else b""
        return [(i, raw[i * 64 + 20:i * 64 + 24]) for i in range(len(raw) // 64) if raw[i * 64]]

    def at_rest():
        items_ready(g, symbols)
        if g.read(symbols["vs_battle_menuState"], 1)[0] & 0x3F != ITEMS_MENU:
            raise RpcError(f"Items: left MENU3 ({g.read(symbols['vs_battle_menuState'], 1)[0]:#x})")

    g.write(symbols["vs_main_settings"] + 1, b"\x00")  # (the cursor memory off: lists open on their first row)
    intro = g.read(symbols["vs_main_stateFlags"] + INTRO_STATE, 1)
    g.write(symbols["vs_main_stateFlags"] + INTRO_STATE, bytes([max(intro[0], 3)]))  # (Equip selectable)
    before = frames()
    items_open(g, symbols)
    toured("Items opened", before)
    for _ in range(9):
        if category() == 7:
            break
        press("r1", settle=10)
        at_rest()
    before = frames()
    press("circle", settle=20)  # Equip
    at_rest()
    for button in ("down",) * 6 + ("up",) * 6:
        press(button)
    press("circle", settle=20)  # the weapon's list
    at_rest()
    for button in ("down", "down", "up", "up"):
        press(button)
    press("cross", settle=20)
    at_rest()
    press("cross", settle=20)
    at_rest()
    toured("Items: Equip, its parts, the weapon's list", before)
    for step in range(8):  # weapons .. misc, then the Manage page again
        before = frames()
        press("r1", settle=10)
        at_rest()
        c, names = category(), None
        if c != 7 and any(20 <= i < 30 for i in items_live(g, symbols)):  # (an empty category: Cross would leave)
            for button in ("down",) * 10 + ("up",) * 10:
                press(button, settle=4)
            rows = rows_now()
            press("square", settle=20)  # the status view (refused in some categories: nothing changes)
            g.run(30)
            if rows_now() != rows:
                press("cross", settle=20)
                at_rest()
            press("circle", settle=10)  # picked up
            at_rest()
            press("circle", settle=10)  # its actions
            at_rest()
            for button in ("down",) * 4 + ("up",) * 4:
                press(button)
            actions = str(repl(g, "rood.classic", "local t = {} for _, r in ipairs(rood.game.menu().rows) do "
                               "if r.id >= 32 and r.state ~= 'inactive' then t[#t + 1] = r.id .. '=' .. r.text end end "
                               "return table.concat(t, '|')")).strip('"').split("|")
            names = [a.split("=", 1)[1].strip().lower()
                     for a in sorted((a for a in actions if "=" in a), key=lambda a: int(a.split("=")[0]))]
            if "discard" in names:
                for _ in range(names.index("discard")):  # (the actions open on their first row)
                    press("down")
                press("circle", settle=10)  # Discard: the Yes / No (on No); misc: the amount picker
                g.run(20)
                if c == 6:
                    for button in PICKER_WALK:
                        press(button, settle=4)
                press("cross", settle=10)  # left on No (the picker: left)
                g.run(20)
            at_rest()
            if any(i >= 32 for i in items_live(g, symbols)):  # (the actions again: Cross to the list)
                press("cross", settle=10)
                at_rest()
        print(f"Items: category {c} toured (actions {names if c != 7 else None})", flush=True)  # (progress)
        toured(f"Items: category {c}: its list, status view, actions, Discard's Yes / No", before, category=c)
    before = frames()
    for _ in range(8):
        press("l1", settle=10)
        at_rest()
    toured("Items: the categories back round with L1", before)
    before = frames()
    press("triangle", settle=60)
    g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F, guard=600, what="Items closed")
    toured("Items closed", before)
    g.write(symbols["vs_main_stateFlags"] + INTRO_STATE, intro)


ITEM_CATEGORIES = {0: "weapons", 1: "blades", 2: "grips", 3: "shields", 4: "armor", 5: "gems", 6: "misc"}


def items_category_now(g, symbols):
    return (g.read(symbols["D_800F4EE8"] + ITEM_CATEGORY, 1)[0] - 1) & 7


def items_rows_now(g, symbols):
    """The live rows and their places (a status view slides them)."""
    p = int.from_bytes(g.read(symbols["vs_battle_menuItems"], PTR), "little")
    raw = g.read(p, 64 * 40) if p else b""
    return [(i, raw[i * 64 + 20:i * 64 + 24]) for i in range(len(raw) // 64) if raw[i * 64]]


def items_to_category(g, symbols, want):
    """R1 round the categories to `want` (Items open, its list at rest)."""
    for _ in range(9):
        if items_category_now(g, symbols) == want:
            return True
        g.press("r1", vsyncs=3, settle=10)
        items_ready(g, symbols)
    return items_category_now(g, symbols) == want


def action_names(g):
    """The live action rows' texts (rows 32.., in order), lower case."""
    actions = str(repl(g, "rood.classic", "local t = {} for _, r in ipairs(rood.game.menu().rows) do "
                       "if r.id >= 32 and r.state ~= 'inactive' then t[#t + 1] = r.id .. '=' .. r.text end end "
                       "return table.concat(t, '|')")).strip('"').split("|")
    return [a.split("=", 1)[1].strip().lower()
            for a in sorted((a for a in actions if "=" in a), key=lambda a: int(a.split("=")[0]))]


def discard_picker(g, symbols, opens):
    """A misc row's actions (opens: the presses that open them), Discard: its amount picker at rest."""
    for button in opens:
        g.press(button, vsyncs=3, settle=10)
        items_ready(g, symbols)
    g.run(20)
    names = action_names(g)
    if "discard" not in names:
        raise RpcError(f"no Discard in the misc row's actions ({names})")
    for _ in range(names.index("discard")):
        g.press("down", vsyncs=3, settle=6)
    g.press("circle", vsyncs=3, settle=10)
    g.run(40)


def items_discard_open(g, symbols):
    """Items, the misc category, its first row's Discard: the amount picker at rest (items1.h _discardMenu)."""
    items_open(g, symbols)
    if not items_to_category(g, symbols, 6):
        raise RpcError("Items: the misc category never reached")
    discard_picker(g, symbols, ("circle", "circle"))  # (picked up, its actions)


def status_view_open(g, symbols, category):
    """From the menu closed: Items, the category, Square on its first row: the item's status view (its panels at
    rest). False when the category is empty or has no status view."""
    items_open(g, symbols)
    if not items_to_category(g, symbols, category) or not any(20 <= i < 30 for i in items_live(g, symbols)):
        return False
    rows = items_rows_now(g, symbols)
    g.press("square", vsyncs=3, settle=20)
    g.run(40)
    return items_rows_now(g, symbols) != rows


# vs_main_inventory_t / vs_main_inventoryIndices_t (BATTLE func_8006B57C_t.h; ui_items_smoke's)
INVENTORY_BYTES, INDICES_BYTES = 0xF00, 176
INV_BLADES, INV_GRIPS, INV_GEMS, BLADE_BYTES, GRIP_BYTES, GEM_BYTES = 0x280, 0x540, 0x8C0, 44, 16, 28
WEAPON_BYTES = 32  # weapons[8] at 0: index, blade, grip (1-based), isEquipped, gems[4]
GEM_INDICES = 64  # vs_main_inventoryIndices: the gems' list (48)


def status_view_fixture(g, symbols):
    """What play reaches, for the panels' every state: a broken blade (DP 0, PP full) and a full one (DP full,
    PP 0); a grip with no gem slot and one with three (empty); two loose gems in an empty gem bag (one raising, one
    lowering classes and affinities). Returns the inventory and its indices as they were (restored after)."""
    import struct
    inv, idx = symbols["vs_main_inventory"], symbols["vs_main_inventoryIndices"]
    saved = (g.read(inv, INVENTORY_BYTES), g.read(idx, INDICES_BYTES))
    raw = saved[0]
    blades = [i for i in range(16) if raw[INV_BLADES + i * BLADE_BYTES]]
    for n, i in enumerate(blades[:2]):
        at = INV_BLADES + i * BLADE_BYTES
        max_dp, max_pp = struct.unpack_from("<HH", raw, at + 4)
        g.write(inv + at + 8, struct.pack("<HH", 0, max_pp) if n == 0 else struct.pack("<HH", max_dp, 0))
    # grips: one with three slots (the empty ones its weapon's rows show), one with none where its weapon holds no
    # gem (a loose grip first, else an assembled one: weapons[w].grip, its gems[0..2])
    gemless = [raw[w * WEAPON_BYTES + 2] - 1 for w in range(8) if raw[w * WEAPON_BYTES + 2]
               and not any(raw[w * WEAPON_BYTES + 4:w * WEAPON_BYTES + 7])]
    loose = [i for i in range(16) if raw[INV_GRIPS + i * GRIP_BYTES]
             and not struct.unpack_from("<H", raw, INV_GRIPS + i * GRIP_BYTES + 12)[0]]
    grips = [i for i in range(16) if raw[INV_GRIPS + i * GRIP_BYTES]]
    none = (loose + gemless)[:1]
    three = [i for i in grips if i not in none][:1]
    for i in none:
        g.write(inv + INV_GRIPS + i * GRIP_BYTES + 4, b"\x00")
    for i in three:
        g.write(inv + INV_GRIPS + i * GRIP_BYTES + 4, b"\x03")
    if not any(raw[INV_GEMS + i * GEM_BYTES] or raw[INV_GEMS + i * GEM_BYTES + 1] for i in range(48)):
        gems = [struct.pack("<HBBBbbb8b8bHh", 261, 0, 0, 0, 2, 0, 1, 5, 0, 0, 0, 0, -3, 0, 0,
                            0, 4, 0, 0, 0, 0, 0, 0, 0, 1),
                struct.pack("<HBBBbbb8b8bHh", 262, 0, 0, 0, -1, 3, 0, 0, -6, 2, 0, 0, 0, 0, 0,
                            -4, 0, 0, 7, 0, 0, -2, 0, 0, 2)]
        g.write(inv + INV_GEMS, b"".join(gems))
        g.write(idx + GEM_INDICES, bytes([1, 2]))
    g.run(2)
    print(f"Items: the status views' fixture: blades {blades[:2]} broken / full, grips {none} no gem slot, {three} "
          "three, gems in the bag", flush=True)
    return saved


def tour_status_views(g, symbols, toured, frames):
    """Each category's status view (Square on its first row): the panels sliding in, at rest, then every item of
    the category paged through with R1 (each its own values: DP / PP, stats, a grip's gem slots, a gem's affinities;
    Square on each: the class / affinity / type table's next page) and back with L1, the view left with Cross (the
    panels sliding out); the Items screen closed at the end."""
    g.write(symbols["vs_main_settings"] + 1, b"\x00")  # (the cursor memory off: lists open on their first row)
    saved = status_view_fixture(g, symbols)
    before = frames()
    items_open(g, symbols)
    toured("Items reopened for the status views", before)
    for category, name in ITEM_CATEGORIES.items():
        before = frames()
        if not items_to_category(g, symbols, category) or not any(20 <= i < 30 for i in items_live(g, symbols)):
            print(f"Items: {name}: empty", flush=True)
            continue
        rows = items_rows_now(g, symbols)
        g.press("square", vsyncs=3, settle=20)
        g.run(40)
        if items_rows_now(g, symbols) == rows:
            print(f"Items: {name}: no status view", flush=True)
            continue
        for button in ("r1",) * 12 + ("l1",) * 12:  # (every item of the category, round and back)
            g.press(button, vsyncs=3, settle=12)
            if button == "r1":  # (Square: the class / affinity / type table's next page, each item's)
                g.press("square", vsyncs=3, settle=12)
        for button in ("down",) * 8 + ("right", "left") + ("up",) * 8:  # (the view's own hand, its rows)
            g.press(button, vsyncs=3, settle=6)
        g.run(20)
        g.press("cross", vsyncs=3, settle=20)
        items_ready(g, symbols)
        print(f"Items: {name}: status view toured", flush=True)
        toured(f"Items: {name}' status view: in, every item (R1 / L1), out", before, category=category)
    before = frames()
    g.press("triangle", vsyncs=3, settle=60)
    g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F, guard=600, what="Items closed")
    toured("Items closed after the status views", before)
    g.write(symbols["vs_main_inventory"], saved[0])  # (the bag as it was)
    g.write(symbols["vs_main_inventoryIndices"], saved[1])


# Magic (MENU0), Break Arts (MENU1), Battle Abilities (MENU2): currentMenu & 0x3F (menu_model.c SCREENS) and the top
# level's commands (menu_nav)
MAGIC_MENU, ARTS_MENU, ABILITIES_MENU = 1, 2, 3
ACTION_SIZE, ACTION_UNLOCKED = 52, 13  # vs_action_t: 52 bytes, unlocked = bit 7 of byte 13
SPELL_TABLES = (("warlock", 18), ("shaman", 6), ("sorcerer", 18), ("enchanter", 8))  # vs_battle_<school>SpellIds
FIRST_ART = 184  # vs_main_actions_daggerArt1: category c's four arts from FIRST_ART + (c - 1) * 4
MENU_FLAGS, UNK_B5, SAVE_POINTS = 2, 0xB5, 0x156  # vs_main_settings.menuFlags; vs_main_stateFlags.unkB5 / savePointStates
MAPPED = 12  # vs_main_settings.mappedChainAbilities[4], mappedDefenseAbilities[4]


def live_rows(g):
    """rood.game.menu()'s live rows: [(id, text, selected)] by id."""
    out = str(repl(g, "rood.classic", "local t = {} for _, r in ipairs(rood.game.menu().rows) do "
                   "if r.state ~= 'inactive' then t[#t + 1] = r.id .. '@@' .. (r.selected and 1 or 0) .. '@@' "
                   ".. (r.text or '') end end return table.concat(t, '##')")).strip('"')
    rows = []
    for item in out.split("##") if out else []:
        parts = item.split("@@", 2)
        if len(parts) == 3:
            rows.append((int(parts[0]), parts[2].strip(), parts[1] == "1"))
    return sorted(rows)


def learn(g, symbols, action):
    a = symbols["vs_main_actions"] + action * ACTION_SIZE + ACTION_UNLOCKED
    g.write(a, bytes([g.read(a, 1)[0] | 0x80]))


def screen_open(g, symbols, command, screen):
    """Triangle, the top level's `command`, Circle: the screen (`screen`, currentMenu & 0x3F) with its rows at rest."""
    from menu_nav import top_command
    g.press("triangle", vsyncs=3, settle=30)
    top_command(g, symbols, command)
    for _ in range(5):  # (Circle once the top level takes it)
        g.press("circle", vsyncs=2, settle=2)
        if g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] & 0x3F == screen, guard=10):
            break
    items_ready(g, symbols)


def menu_cursor(g):
    """The shared list's cursor (rood.game.menu().cursor): (its row, how many rows the list has)."""
    row, rows = str(repl(g, "rood.classic", "local c = rood.game.menu().cursor return c.row .. ',' .. c.rows")
                    ).strip('"').split(",")
    return int(row), int(rows)


def skills_tour_helpers(g, symbols, screen, name):
    def press(button, settle=6):
        g.press(button, vsyncs=3, settle=settle)

    def at_rest():
        items_ready(g, symbols)
        menu = g.read(symbols["vs_battle_menuState"], 1)[0]
        if menu & 0x3F != screen:
            raise RpcError(f"{name}: left the screen ({menu:#x})")
        rows = None
        for _ in range(60):  # (a list's rows come in one a frame: until they stop coming)
            now = [i for i, _, _ in live_rows(g)]
            if now == rows:
                return
            rows = now
            g.run(4)

    def select(row_id, ids):
        """Down (the lists wrap) until row `row_id` is the selected one."""
        for _ in range(2 * len(ids) + 20):
            if any(i == row_id and sel for i, _, sel in live_rows(g)):
                return
            press("down")
        raise RpcError(f"{name}: row {row_id} never selected ({live_rows(g)})")

    def seek(want):
        """Down (the shared list wraps and scrolls) until its cursor is on row `want` (an index), or on a row whose
        text starts with `want` (a string)."""
        for _ in range(menu_cursor(g)[1] + 3):
            row, _ = menu_cursor(g)
            if (row == want if isinstance(want, int)
                    else any(i >= 20 and sel and t.lower().startswith(want) for i, t, sel in live_rows(g))):
                return True
            press("down")
        return False

    def walk(count):
        """Down past the list's end and back up past its top (the scroll, the wrap)."""
        for button in ("down",) * (count + 2) + ("up",) * (count + 2):
            press(button, settle=4)

    def close(before, toured):
        for _ in range(6):  # (Triangle closes the menu from the screen's first page)
            press("triangle", settle=60)
            if g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F, guard=60):
                break
        g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F, guard=600, what=f"{name} closed")
        g.run(30)
        toured(f"{name} closed", before)

    return press, at_rest, select, seek, walk, close


def magic_fixture(g, symbols):
    """Every spell learned (as at the game's end: every grimoire read), the Magic command on (menuFlags bit 0, set
    when the first is learned), MP full, Teleport's Circles of Magic: the save room's and a few others visited."""
    for table, size in SPELL_TABLES:
        for spell in g.read(symbols[f"vs_battle_{table}SpellIds"], size):
            if spell:
                learn(g, symbols, spell)
    f = symbols["vs_main_settings"] + MENU_FLAGS
    g.write(f, bytes([g.read(f, 1)[0] | 1]))
    flags = symbols["vs_main_stateFlags"]
    g.write(flags + UNK_B5, bytes([1]))
    points = bytearray(g.read(flags + SAVE_POINTS, 48))
    for p in (0, 1, 3, 10):
        points[p] = points[p] or 1
    g.write(flags + SAVE_POINTS, bytes(points))
    g.run(2)


def tour_magic(g, symbols, toured, frames):
    """Magic (MENU0) through the original menus: opened; each school (Warlock, Shaman, Sorcerer, Enchanter,
    Teleport) in turn: its list walked past both ends; Warlock's levelled spell (found by its text: Explosion) into
    its LV picker, the levels walked right and left, Cross out; Cross back to the schools; closed. Nothing is cast."""
    press, at_rest, select, seek, walk, close = skills_tour_helpers(g, symbols, MAGIC_MENU, "Magic")
    magic_fixture(g, symbols)
    before = frames()
    screen_open(g, symbols, 0, MAGIC_MENU)
    at_rest()
    schools = [(i, t) for i, t, _ in live_rows(g) if 10 <= i < 20]
    toured("Magic opened", before, schools=[t for _, t in schools])
    for school, text in schools:
        before = frames()
        select(school, [i for i, _ in schools])
        press("circle", settle=20)
        at_rest()
        rows = menu_cursor(g)[1]
        walk(rows)
        picker = False
        if seek("explosion"):  # (Warlock's levelled spell: its LV picker)
            press("circle", settle=20)  # the LV picker (the list flies out right)
            g.run(20)
            for button in ("right",) * 4 + ("left",) * 4:
                press(button)
            press("cross", settle=20)  # back to the list
            at_rest()
            picker = True
        press("cross", settle=20)  # back to the schools
        at_rest()
        print(f"Magic: {text} toured ({rows} rows, LV picker {picker})", flush=True)  # (progress)
        toured(f"Magic: {text}: its list walked{', the LV picker' if picker else ''}", before, school=text,
               rows=rows)
    close(frames(), toured)


def equipped_category(g, symbols):
    """vs_battle_characterState->equippedWeaponCategory (vs_battle_actor + 37)."""
    actor = int.from_bytes(g.read(symbols["vs_battle_characterState"], PTR), "little")
    return g.read(actor + 37, 1)[0]


def tour_arts(g, symbols, toured, frames):
    """Break Arts (MENU1) through the original menus: opened on the equipped category's arts; its list walked past
    both ends; View Arts (found by its text) into the ten categories, walked past both ends; each category with an
    art learned opened, walked, and back to the categories (Cross); closed. Nothing is cast."""
    press, at_rest, select, seek, walk, close = skills_tour_helpers(g, symbols, ARTS_MENU, "Break Arts")
    equipped = equipped_category(g, symbols)
    status, others = symbols["vs_main_artsStatus"], [c for c in (1, 3, 5) if c != equipped][:2]
    for cat, count in ((equipped, 3), (others[0], 4), (others[1], 1)):
        for art in range(count):
            learn(g, symbols, FIRST_ART + (cat - 1) * 4 + art)
        g.write(status + (cat % 10), bytes([max(g.read(status + (cat % 10), 1)[0], count)]))
    g.run(2)
    before = frames()
    screen_open(g, symbols, 1, ARTS_MENU)
    at_rest()
    toured("Break Arts opened", before)

    def view_arts():
        """The arts list walked, then View Arts (its last row, found by its text): the categories."""
        rows = menu_cursor(g)[1]
        walk(rows)
        texts = []
        if seek(rows - 1):  # (View Arts: the list's last row, after the learned arts)
            row, _ = menu_cursor(g)
            texts = [t for i, t, sel in live_rows(g) if i == 20 + row and sel]
        if texts != ["View Arts"]:  # (never Circle on an art: that casts it)
            raise RpcError(f"Break Arts: View Arts not reached ({menu_cursor(g)}, {live_rows(g)})")
        press("circle", settle=20)
        at_rest()
        return rows

    before = frames()
    rows = view_arts()
    toured("Break Arts: the equipped category's arts walked, View Arts", before, rows=rows)
    before = frames()
    cats = menu_cursor(g)[1]
    walk(cats)
    toured("Break Arts: the ten categories walked", before, rows=cats)
    for cat in (others[0], others[1], equipped):
        before = frames()
        if not seek(cat - 1):
            raise RpcError(f"Break Arts: category {cat} never reached ({menu_cursor(g)})")
        press("circle", settle=20)  # the category's arts
        at_rest()
        rows = view_arts()  # (and back to the categories through View Arts)
        print(f"Break Arts: category {cat} toured ({rows} rows)", flush=True)  # (progress)
        toured(f"Break Arts: category {cat}: its arts walked, View Arts", before, category=cat, rows=rows)
    before = frames()
    press("cross", settle=20)  # Cross on the categories: the arts list again
    at_rest()
    toured("Break Arts: Cross on the categories", before)
    close(frames(), toured)


def tour_abilities(g, symbols, toured, frames):
    """Battle Abilities (MENU2) through the original menus: every chain and defense ability learned (as by kills in
    play); opened; Chain, then Defense (found by their text): the list walked past both ends (it scrolls: 8 rows
    shown), a row put on Circle, Triangle and Square (the button icons) and taken off again (the same button), Cross
    back; closed. The mapping is restored after."""
    press, at_rest, select, seek, walk, close = skills_tour_helpers(g, symbols, ABILITIES_MENU, "Battle Abilities")
    for kind in ("chain", "defense"):
        for action in g.read(symbols[f"vs_battle_{kind}AbilityOffsets"], 14):
            if action:
                learn(g, symbols, action)
    mapped = g.read(symbols["vs_main_settings"] + MAPPED, 8)
    g.run(2)
    before = frames()
    screen_open(g, symbols, 2, ABILITIES_MENU)
    at_rest()
    types = [(i, t) for i, t, _ in live_rows(g) if 10 <= i < 20]
    toured("Battle Abilities opened", before, types=[t for _, t in types])
    for row, text in types:
        before = frames()
        select(row, [i for i, _ in types])
        press("circle", settle=20)
        at_rest()
        g.run(10)  # (MENU2's list: waitReady, then abilityMenu takes presses)
        count = sum(1 for i, _, _ in live_rows(g) if i >= 20)
        walk(16)  # (the learned list: up to 14, 8 shown)
        press("down")
        for button in ("circle", "triangle", "square"):
            press(button, settle=10)  # on that button
        for button in ("square", "triangle", "circle"):
            press(button, settle=10)  # (already on Circle: Triangle and Square moved it; the same button: off)
        press("cross", settle=20)
        at_rest()
        print(f"Battle Abilities: {text} toured ({count} rows shown)", flush=True)  # (progress)
        toured(f"Battle Abilities: {text}: its list walked, the buttons put on and off", before, kind=text,
               rows=count)
    close(frames(), toured)
    g.write(symbols["vs_main_settings"] + MAPPED, mapped)


SKILL_PROOFS = (  # (the top level's command, the screen, where, its kinds at rest)
    (0, MAGIC_MENU, "the Magic schools at rest", ("menu_row", "menu_cursor", "menu_command")),
    (1, ARTS_MENU, "the Break Arts list at rest", ("menu_row", "menu_cursor", "menu_command")),
    (2, ABILITIES_MENU, "the Battle Abilities types at rest", ("menu_row", "menu_cursor", "menu_command")),
    (2, ABILITIES_MENU, "the Battle Abilities first type's list at rest (its own hand)",
     ("menu_row", "menu_cursor", "menu_command"), True),
)


def skills_proofs(g, symbols, check, state_proof):
    """Magic, Break Arts, Battle Abilities (their fixtures from the tours still learned): each opened to its first
    list at rest, the default and the fallback of each kind there, the state proof (§3.4) from that mark."""
    for command, screen, label, kinds, *into in SKILL_PROOFS:
        g.rewind()
        for _ in range(8):  # (out of whatever the last mark had open)
            if g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F:
                break
            g.press("triangle", vsyncs=3, settle=60)
        g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F, guard=600, what="the menu closed")
        g.run(30)
        screen_open(g, symbols, command, screen)
        if into:  # (Battle Abilities: its first type's list, MENU2's own hand: vs_battle_drawCursor)
            g.run(40)
            g.press("circle", vsyncs=3, settle=20)
            menu_wait(g, f"{label}: the list", lambda: any(i >= 20 and sel for i, _, sel in live_rows(g)))
            g.run(60)
        g.pause()
        g.mark()
        for kind in kinds:
            g.rewind()
            default_and_fallback(g, kind, check)
        state_proof(label, kinds)
        if into:  # (the mark again with the menu closed: Triangle does not leave MENU2's list, Cross does)
            g.rewind()
            g.press("cross", vsyncs=3, settle=30)
            menu_close(g, symbols)
            g.pause()
            g.mark()
MANUAL_COMMAND, MANUAL_MENU, MANUAL_TOPICS = 9, 10, 14  # the top level's QUICK MANUAL row (menuFlags bit 9),
MANUAL_LIST, MANUAL_PAGE = 2, 5                         # its currentMenu (MENUE), topics; MENUE's _showMenuState
MENUE = {}  # MENUE's statics (ui_manual_smoke.menue_symbols)


def manual_state(g, symbols):
    """(currentMenu & 0x3F, MENUE's _showMenuState, the list taking input (vs_mainMenu_itemsListRow 17))."""
    from ui_manual_smoke import menue_symbols
    if not MENUE:
        MENUE.update(menue_symbols())
    return (g.read(symbols["vs_battle_menuState"], 1)[0] & 0x3F,
            int.from_bytes(g.read(MENUE["_showMenuState"], 4), "little", signed=True),
            g.read(symbols["vs_mainMenu_itemsListRow"], 1)[0] == 17)


def menu_close(g, symbols):
    """Triangle until the menu is closed (a press while a screen still glides in is not taken)."""
    for _ in range(6):
        g.press("triangle", vsyncs=3, settle=30)
        if g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F, guard=10):
            break
    g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F, guard=10, what="the menu closed")
    g.run(30)


def manual_open(g, symbols):
    """Triangle, QUICK MANUAL (the command shown: menuFlags bit 9), Circle: MENUE's list at rest."""
    from menu_nav import top_command
    flags = int.from_bytes(g.read(symbols["vs_main_settings"] + 2, 2), "little")
    g.write(symbols["vs_main_settings"] + 2, (flags | 1 << MANUAL_COMMAND).to_bytes(2, "little"))
    g.press("triangle", vsyncs=3, settle=30)
    top_command(g, symbols, MANUAL_COMMAND)
    for _ in range(5):
        g.press("circle", vsyncs=2, settle=2)
        if g.until(lambda: manual_state(g, symbols)[0] == MANUAL_MENU, guard=10):
            break
    manual_wait(g, symbols, MANUAL_LIST)


def manual_wait(g, symbols, show, limit=600):
    """MENUE's list taking input (MANUAL_LIST) or a topic's page up (MANUAL_PAGE)."""
    for _ in range(limit):
        menu, state, row = manual_state(g, symbols)
        if menu == MANUAL_MENU and state == show and (show != MANUAL_LIST or row):
            g.run(4)
            return
        g.run(1)
    raise RpcError(f"Quick Manual: never reached state {show} ({manual_state(g, symbols)})")


def tour_manual(g, symbols, toured, frames):
    """The Quick Manual (MENUE, currentMenu 10) through the original menus: opened (the cursor memory off: topic 0);
    the topic list walked down past its end and up past its top (the window scrolls); every topic opened: its lines
    stepped down and up, Up at the top (to the end) and Down at the end (to the top), each page turned with Square +
    Down, Start (the top), Circle back to the list (topic 13 left with Cross); Cross from the list to the top level;
    reopened and closed with Triangle from a page."""
    def press(button, settle=4):
        g.press(button, vsyncs=3, settle=settle)

    def topic():
        return int.from_bytes(g.read(symbols["vs_battle_manualDisplayState"], 2), "little")

    memory = g.read(symbols["vs_main_settings"] + 1, 1)
    g.write(symbols["vs_main_settings"] + 1, b"\x00")  # (the cursor memory off: the list opens on topic 0)
    before = frames()
    manual_open(g, symbols)
    toured("Quick Manual opened", before)
    before = frames()
    for button in ("down",) * (MANUAL_TOPICS + 1) + ("up",) * (MANUAL_TOPICS + 2):
        press(button)
    manual_wait(g, symbols, MANUAL_LIST)
    toured(f"Quick Manual: the list walked, {MANUAL_TOPICS + 1} down and {MANUAL_TOPICS + 2} up", before)
    for t in range(MANUAL_TOPICS):
        before = frames()
        manual_wait(g, symbols, MANUAL_LIST)
        for _ in range(2 * MANUAL_TOPICS):  # (to topic t by the list's cursor, rood.game.menu(): row + scroll)
            at = int(repl(g, "rood.classic", "local c = rood.game.menu().cursor return c.row + c.page"))
            if at == t:
                break
            press("down" if at < t else "up")
        press("circle", settle=10)
        manual_wait(g, symbols, MANUAL_PAGE)
        if topic() != t:
            raise RpcError(f"Quick Manual: topic {topic()} open, not {t}")
        g.run(12)  # (the page's content drawn after eight frames)
        for button in ("down",) * 3 + ("up",) * 3 + ("up", "down"):  # lines; Up at the top to the end, Down back
            press(button)
        pages = 0
        for _ in range(40):  # Square + Down: each page in turn, to the last
            line = int.from_bytes(g.read(symbols["vs_battle_manualDisplayState"] + 4 + 2 * t, 2), "little")
            g.hold(["square", "down"], 3)
            g.run(4)
            if int.from_bytes(g.read(symbols["vs_battle_manualDisplayState"] + 4 + 2 * t, 2), "little") <= line:
                break
            pages += 1
        press("start")  # the top
        press("cross" if t == MANUAL_TOPICS - 1 else "circle", settle=10)
        manual_wait(g, symbols, MANUAL_LIST)
        print(f"Quick Manual: topic {t} toured ({pages} page turns)", flush=True)  # (progress)
        toured(f"Quick Manual: topic {t}: its lines, pages, the top, back", before, topic=t, pages=pages)
    before = frames()
    press("cross", settle=20)
    g.until(lambda: manual_state(g, symbols)[0] == 0, guard=30, what="the top level")
    g.until(lambda: int.from_bytes(g.read(symbols["D_800F4CC0"], 4), "little", signed=True) >= 0, guard=30,
            what="the top level's rows in")
    menu_close(g, symbols)
    manual_open(g, symbols)
    press("circle", settle=10)
    manual_wait(g, symbols, MANUAL_PAGE)
    g.run(12)
    menu_close(g, symbols)
    toured("Quick Manual: Cross to the top level, closed; reopened, a page, Triangle", before)
    g.write(symbols["vs_main_settings"] + 1, memory)


STATUS_COMMAND, STATUS_MENU = 4, 5  # the top level's STATUS row; its currentMenu (MENU4)
STATUS_IDLE, STATUS_EQUIPMENT, STATUS_STATS = 5, 8, 9  # vs_menu4_exec's states (vs_battle_submenuStates[5])


STATUS_MOVES = (0x000A010A, 0x010A0200, 0x020A0301, 0x030A0402, 0x040A0503, 0x050A0604, 0x060D0705, 0x070D0806,
                0x080D0807, 0x0A090B09, 0x0009000A, 0x000B0D09)  # vs_menu4_exec's D_801080FC: an element's up, down,
STATUS_DIRECTIONS = ("up", "down", "left", "right")                # left, right neighbours (13: the modifiers)
MENU4 = {}  # MENU4's statics


def menu4(g, name, size=1):
    """One of MENU4's statics: _selectedElement (0..5 the body's parts, 6.. the stat descriptions), _selectedActor
    (1 Ashley, then the room's other actors)."""
    if not MENU4:
        out = subprocess.run([os.environ.get("ROOD_OBJDUMP", "objdump"), "-t", str(rood_rpc.BINARY)],
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 6 and parts[-3].startswith("rood_ovl_menu_menu4_prg"):
                MENU4[parts[-1]] = int(parts[0], 16)
    return int.from_bytes(g.read(MENU4[name], size), "little")


def status_element(g):
    return menu4(g, "_selectedElement")


def status_to(g, symbols, element):
    """Steps the Status screen's hand to `element` by the game's own neighbours (the shortest way)."""
    far = {element: 0}
    while True:  # (distances to `element`, walking the neighbours backwards)
        more = {e: far[n] + 1 for e, m in enumerate(STATUS_MOVES) for n in (m >> 8 * d & 0xFF for d in range(4))
                if n in far and e not in far}
        if not more:
            break
        far.update(more)
    for _ in range(16):
        at = status_element(g)
        if at == element:
            return
        moves = [(far.get(STATUS_MOVES[at] >> 8 * d & 0xFF, 99), d) for d in range(4)]
        g.press(STATUS_DIRECTIONS[min(moves)[1]], vsyncs=3, settle=6)
    raise RpcError(f"Status: the hand never reached element {element} (at {status_element(g)})")


def status_state(g, symbols):
    return g.read(symbols["vs_battle_menuState"], 1)[0] & 0x3F, g.read(symbols["vs_battle_submenuStates"] + 5, 1)[0]


def menu_rows(g):
    """The live rows through rood.game.menu(): {id: state}."""
    rows = str(repl(g, "rood.classic", "local t = {} for _, r in ipairs(rood.game.menu().rows) do "
                    "t[#t + 1] = r.id .. '=' .. r.state end return table.concat(t, '|')")).strip('"')
    return {int(i): st for i, st in (r.split("=") for r in rows.split("|") if "=" in r)}


def status_wait(g, symbols, state, limit=600):
    """MENU4 in `state`, then settled (its rows, if any, idle)."""
    for _ in range(limit):
        if status_state(g, symbols) == (STATUS_MENU, state):
            calm = 0
            for _ in range(200):
                calm = calm + 1 if all(st in ("inactive", "shown") for st in menu_rows(g).values()) else 0
                if calm >= 3:
                    break
                g.run(1)
            g.run(12)
            return
        g.run(1)
    raise RpcError(f"Status: never reached state {state} ({status_state(g, symbols)})")


def status_open(g, symbols):
    """Triangle, STATUS, Circle: MENU4 at rest on Ashley."""
    from menu_nav import top_command
    g.press("triangle", vsyncs=3, settle=30)
    top_command(g, symbols, STATUS_COMMAND)
    for _ in range(5):
        g.press("circle", vsyncs=2, settle=2)
        if g.until(lambda: status_state(g, symbols)[0] == STATUS_MENU, guard=10):
            break
    status_wait(g, symbols, STATUS_IDLE)
    g.run(20)
    drawn = g.call("hud", action="stats")["kinds"]["menu_cursor"]["drawn"]  # (the hand comes up a while after the
    g.until(lambda: g.call("hud", action="stats")["kinds"]["menu_cursor"]["drawn"] > drawn, guard=30,  # rest)
            what="Status: the hand up at rest")
    g.run(10)


def status_into(g, symbols, button, state, opened=False):
    """`button` until MENU4 is in `state` (a press while the screen still glides is not taken); Status opened first
    unless `opened`."""
    if not opened:
        status_open(g, symbols)
    for _ in range(6):
        was = status_state(g, symbols)
        g.press(button, vsyncs=3, settle=2)
        for _ in range(30):  # (taken: the state moves on)
            if status_state(g, symbols) != was:
                break
            g.run(1)
        for _ in range(120):  # (on its way, through other states: not pressed again)
            if status_state(g, symbols) in ((STATUS_MENU, state), was):
                break
            g.run(1)
        if status_state(g, symbols) == (STATUS_MENU, state):
            status_wait(g, symbols, state)
            return
    raise RpcError(f"Status: {button} never reached state {state} ({status_state(g, symbols)})")


ACTOR2_WEAPON, ACTOR2_SHIELD = 0x3C, 0x1D4  # vs_battle_actor2: weapon (vs_battle_uiWeapon), shield (vs_battle_uiShield)
WEAPON_BLADE_DP, WEAPON_DP, SHIELD_PP, SHIELD_BASE_PP = 0x18 + 8, 0x114, 0xDC, 0x18 + 0xC  # (current DP / PP)


def actor2(g):
    """Ashley's vs_battle_actor2 (vs_battle_actors[0] -> +0x3c): what Status reads."""
    actor = int.from_bytes(g.read(rood_rpc.symbols()["vs_battle_actors"], PTR), "little")
    return int.from_bytes(g.read(actor + 0x3C, 4), "little")


def status_equipment_fixture(g):
    """What play reaches, for the DP / PP bars' ends on Status: Ashley's weapon broken (its DP and its blade's 0, PP
    as it is) and her shield's PP spent (DP as it is). Returns what it overwrote (status_equipment_restore)."""
    a = actor2(g)
    places = (a + ACTOR2_WEAPON + WEAPON_BLADE_DP, a + ACTOR2_WEAPON + WEAPON_DP, a + ACTOR2_SHIELD + SHIELD_PP,
              a + ACTOR2_SHIELD + SHIELD_BASE_PP)
    saved = [(at, g.read(at, 2)) for at in places]
    for at in places:
        g.write(at, b"\x00\x00")
    print(f"Status: the equipment's fixture: the weapon broken, the shield's PP spent (was "
          f"{[int.from_bytes(v, 'little') for _, v in saved]})", flush=True)
    return saved


def status_equipment_restore(g, saved):
    for at, value in saved:
        g.write(at, value)


def status_detail_open(g, symbols):
    """Status, the equipment list, Circle on its first row (the weapon): the piece's detail at rest (its stats
    panel, DP / PP bars and table)."""
    status_into(g, symbols, "circle", STATUS_EQUIPMENT)
    for _ in range(10):  # (the weapon's row: 10, the list's first; the hand opens on the part's own)
        if any(i == 10 and sel for i, _, sel in live_rows(g)):
            break
        g.press("up", vsyncs=3, settle=6)
    status_wait(g, symbols, STATUS_EQUIPMENT)
    for _ in range(6):
        g.press("circle", vsyncs=3, settle=20)
        if g.until(lambda: any(i >= 32 for i in menu_rows(g)), guard=10):
            break
    g.run(40)


def tour_status(g, symbols, toured, frames):
    """Status (MENU4, currentMenu 5) through the original menus: opened; the body's parts and the stat descriptions
    walked every way past their ends; a part's equipment (Circle): its list walked down and up past its ends, each
    piece's detail opened (Circle), its elements walked, out; back (Cross); a part's stats (Square): its rows walked
    past both ends, the parts stepped round with R1 and L1, back; R1 / L1 through the room's actors and back to Ashley;
    closed with Triangle. The equipment walked with Ashley's weapon broken (DP 0) and her shield's PP spent
    (status_equipment_fixture: the DP / PP bars' ends; restored after); each detail's class / affinity / type table
    paged with Square."""
    def press(button, settle=6):
        g.press(button, vsyncs=3, settle=settle)

    def detail():  # (a piece's detail up: its rows from 32)
        return any(i >= 32 for i in menu_rows(g))

    def into(button, state):
        status_into(g, symbols, button, state, opened=True)

    before = frames()
    status_open(g, symbols)
    toured("Status opened", before)
    before = frames()
    for button in ("down",) * 8 + ("up",) * 8 + ("right",) * 3 + ("left",) * 3 + ("down",) * 3 + ("right",) * 2 \
            + ("up",) * 10:
        press(button)
    status_wait(g, symbols, STATUS_IDLE)
    toured("Status: the parts and the stat descriptions walked every way", before)
    before = frames()
    actors = []
    for button in ("r1",) * 2 + ("l1",) * 2:  # the room's other actors (each one's figure and parts), round and back
        press(button, settle=40)
        status_wait(g, symbols, STATUS_IDLE)
        actors.append(menu4(g, "_selectedActor", 4))
    for _ in range(16):
        if menu4(g, "_selectedActor", 4) == 1:
            break
        press("l1", settle=40)
        status_wait(g, symbols, STATUS_IDLE)
    if menu4(g, "_selectedActor", 4) != 1:
        raise RpcError(f"Status: never back on Ashley (actor {menu4(g, '_selectedActor', 4)})")
    g.run(30)
    toured(f"Status: R1 / L1 through the actors {actors}", before, actors=actors)
    saved = status_equipment_fixture(g)
    for part in range(2):
        before = frames()
        status_to(g, symbols, part)
        into("circle", STATUS_EQUIPMENT)
        n = len([i for i in menu_rows(g) if 10 <= i < 20])  # (the part's equipment: rows 10..)
        for button in ("down",) * (n + 1) + ("up",) * (n + 1):
            press(button)
        details = 0
        for row in range(n):
            status_wait(g, symbols, STATUS_EQUIPMENT)
            press("circle", settle=20)  # the piece's detail (refused on an empty slot: nothing changes)
            if g.until(detail, guard=5):
                details += 1
                g.run(20)
                for button in ("square",) * 4 + ("down",) * 4 + ("right",) * 2 + ("up",) * 4 + ("left",) * 2 \
                        + ("r1", "l1"):  # (Square: the table's next page, round)
                    press(button)
                press("cross", settle=20)
                g.until(lambda: not detail(), guard=10, what="Status: the detail closed")
                status_wait(g, symbols, STATUS_EQUIPMENT)
            press("down")
        into("cross", STATUS_IDLE)
        print(f"Status: part {part}'s equipment toured ({n} rows, {details} details)", flush=True)  # (progress)
        toured(f"Status: part {part}'s equipment, {n} rows, {details} details", before, part=part)
    status_equipment_restore(g, saved)
    before = frames()
    status_to(g, symbols, 0)
    into("square", STATUS_STATS)
    for button in ("down",) * 12 + ("up",) * 3 + ("r1",) * 7 + ("l1",) * 2:
        press(button)
    into("cross", STATUS_IDLE)
    toured("Status: a part's stats, its rows, the parts round", before)
    before = frames()
    menu_close(g, symbols)
    toured("Status closed", before)


# The remaining MAINMENU screens: Score (MENU9), Map (MENU5), Data (MENU7), Setup (MENUC), Storage (MENUD), Loot
# (MENUB): currentMenu & 0x3F (menu_model.c SCREENS)
MAP_MENU, DATA_MENU, SCORE_MENU, LOOT_MENU, SETUP_MENU, STORAGE_MENU = 6, 7, 9, 12, 13, 14
SCORE_COMMAND = 8  # the top level's SCORE row (menuFlags bit 8)
SCORE_TOP, SCORE_SECTIONS = 2, (5, 8, 11, 14)  # MENU9 _menuState: the top menu, each section taking input
SCORE_ROWS = 10  # MENU9's top menu: rows 10.. (TITLE, GAZETTE, RECORD TIME, ENCYCLOPAEDIA; 0..9 the top level's)
OVERLAY_STATICS = {}  # {(overlay section, name): address}


def ovl(g, section, name, size=4, signed=True):
    """An overlay's static by name (its section rood_ovl_menu_<section>_prg; other overlays have statics of the
    same name; a function's static by its name without the compiler's .N, the first of that name)."""
    if not OVERLAY_STATICS:
        out = subprocess.run([os.environ.get("ROOD_OBJDUMP", "objdump"), "-t", str(rood_rpc.BINARY)],
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 6 and parts[-3].startswith("rood_ovl_menu_"):  # (a function's own: its name.N)
                OVERLAY_STATICS.setdefault((parts[-3].split("_")[3], parts[-1].split(".")[0]), int(parts[0], 16))
    return int.from_bytes(g.read(OVERLAY_STATICS[(section, name)], size), "little", signed=signed)


def current_menu(g, symbols):
    return g.read(symbols["vs_battle_menuState"], 1)[0]


def menu_wait(g, what, ready, limit=900):
    """Until ready(); then a few frames more."""
    for _ in range(limit):
        if ready():
            g.run(6)
            return
        g.run(1)
    raise RpcError(f"{what}: never reached")


def press_until(g, button, taken, tries=4, frames=30):
    """`button` again until taken() (a press while a screen settles is not taken), a few frames apart."""
    for _ in range(tries):
        g.press(button, vsyncs=3, settle=2)
        for _ in range(frames):
            if taken():
                return
            g.run(1)
    raise RpcError(f"{button}: never taken")


def score_state(g):
    return ovl(g, "menu9", "_menuState")


def score_open(g, symbols):
    """Triangle, SCORE (the command shown: menuFlags bit 8), Circle: MENU9's top menu taking input."""
    from menu_nav import top_command
    flags = int.from_bytes(g.read(symbols["vs_main_settings"] + 2, 2), "little")
    g.write(symbols["vs_main_settings"] + 2, (flags | 1 << SCORE_COMMAND).to_bytes(2, "little"))
    g.press("triangle", vsyncs=3, settle=30)
    top_command(g, symbols, SCORE_COMMAND)
    for _ in range(5):
        g.press("circle", vsyncs=2, settle=2)
        if g.until(lambda: current_menu(g, symbols) & 0x3F == SCORE_MENU, guard=10):
            break
    menu_wait(g, "Score's top menu", lambda: current_menu(g, symbols) & 0x3F == SCORE_MENU
              and score_state(g) == SCORE_TOP)
    g.run(30)  # (the top menu's list flies in)


def tour_score(g, symbols, toured, frames):
    """Score (MENU9, currentMenu 9) through the original menus, with card_fixtures' "maxed" score (every title,
    record, time attack and foe: the lists page): opened; each section (Titles, Records, Time Attack, the
    Bestiary) in turn: its list walked down past its end and up past its top (the wraps), back to the top menu
    (Cross); the top menu walked round; a foe of the Bestiary opened (Circle: its model and description), turned
    to the next foe and back, out; closed with Triangle (from a section) and again from the top menu."""
    from card_fixtures import score_fixture

    def press(button, settle=6):
        g.press(button, vsyncs=3, settle=settle)

    def at(state):
        menu_wait(g, f"Score state {state}", lambda: current_menu(g, symbols) & 0x3F == SCORE_MENU
                  and score_state(g) == state)

    f = score_fixture("maxed")
    for off, data in f["scoredata"].items():
        g.write(symbols["vs_main_scoredata"] + off, data)
    for off, data in f["state"].items():
        g.write(symbols["vs_main_stateFlags"] + off, data)
    memory = g.read(symbols["vs_main_settings"] + 1, 1)
    g.write(symbols["vs_main_settings"] + 1, b"\x00")  # (the cursor memory off)
    before = frames()
    score_open(g, symbols)
    toured("Score opened", before)
    before = frames()
    for button in ("down",) * 5 + ("up",) * 5:
        press(button)
    at(SCORE_TOP)
    toured("Score: the top menu walked round", before)
    for section, (downs, name) in enumerate(((40, "Title"), (30, "Gazette"), (12, "Record Time"),
                                             (80, "Encyclopaedia"))):
        before = frames()
        for _ in range(8):  # (the section's row selected, by rood.game.menu(): rows 10.. TITLE, GAZETTE, ..)
            if any(i == SCORE_ROWS + section and sel for i, _, sel in live_rows(g)):
                break
            press("down")
        press_until(g, "circle", lambda: score_state(g) != SCORE_TOP)
        at(SCORE_SECTIONS[section])
        g.run(12)
        for button in ("down",) * downs + ("up",) * (downs + 2):
            press(button, settle=2)
        g.run(6)
        if section == 3:  # a foe: its model and description, the next foe and back, out
            press("down")
            press("circle", settle=10)
            menu_wait(g, "Score: a foe's page", lambda: score_state(g) == 20)
            g.run(30)
            for button in ("right", "left", "select", "select"):
                press(button, settle=20)
            press("cross", settle=10)
            at(SCORE_SECTIONS[section])
        press("cross", settle=10)
        at(SCORE_TOP)
        g.run(12)
        print(f"Score: {name} toured", flush=True)  # (progress)
        toured(f"Score: {name}: its list past both ends, back", before, section=section)
    before = frames()
    press_until(g, "circle", lambda: score_state(g) != SCORE_TOP)
    menu_wait(g, "Score: a section", lambda: score_state(g) in SCORE_SECTIONS)
    menu_close(g, symbols)
    score_open(g, symbols)
    menu_close(g, symbols)
    toured("Score: closed from a section; reopened, closed from the top", before)
    g.write(symbols["vs_main_settings"] + 1, memory)


MAP_COMMAND, MAP_INPUT, MAP_LIST = 5, 1, 2  # the top level's MAP row; MENU5 _menuState: processInput, the list
MAP_AREAS = range(1, 14)  # ui_map_smoke's fixture: areas 1..13 visited (more than the list's 9 rows: it pages)


def map_state(g):
    return ovl(g, "menu5", "_menuState")


def map_fixture(g, symbols):
    """ui_map_smoke's: areas 1..13 visited (areaFlags), every countable room seen (roomFlags |= the completion
    flags: an area bit without a seen room would hang MENU5's _updateRoomIndex)."""
    status, done = symbols["vs_main_mapStatus"], symbols["vs_battle_mapCompletionFlags"]
    rooms = [a | b for a, b in zip(g.read(status, 64), g.read(done, 64))]
    g.write(status, bytes(rooms))
    area = int.from_bytes(g.read(status + 64, 4), "little")
    for i in MAP_AREAS:
        area |= 1 << i
    g.write(status + 64, area.to_bytes(4, "little"))


def map_open(g, symbols):
    """Triangle, MAP, Circle: MENU5 taking input (its map up)."""
    from menu_nav import top_command
    g.press("triangle", vsyncs=3, settle=30)
    top_command(g, symbols, MAP_COMMAND)
    press_until(g, "circle", lambda: current_menu(g, symbols) & 0x3F == MAP_MENU)
    menu_wait(g, "the map", lambda: current_menu(g, symbols) & 0x3F == MAP_MENU and map_state(g) == MAP_INPUT)
    g.run(30)


def map_list(g, symbols):
    """The map, then its area list (Circle) at rest."""
    map_open(g, symbols)
    press_until(g, "circle", lambda: map_state(g) == MAP_LIST)
    items_ready(g, symbols)


def tour_map(g, symbols, toured, frames):
    """The Map (MENU5, currentMenu 6) through the original menus, with ui_map_smoke's fixture (13 areas visited:
    the area list pages): opened; the area list (Circle) walked down past its end and up past its top, left with
    Cross; again, four rows down and Circle (another area's map loaded); R1 / L1 through its rooms, L2 + Right
    (the turn), Select (the help hidden and shown); Cross to the top level, closed; reopened, the list, Triangle
    (closes the menu)."""
    def press(button, settle=8):
        g.press(button, vsyncs=3, settle=settle)

    def at(state):
        menu_wait(g, f"the map's state {state}", lambda: current_menu(g, symbols) & 0x3F == MAP_MENU
                  and map_state(g) == state)

    map_fixture(g, symbols)
    before = frames()
    map_open(g, symbols)
    toured("Map opened", before)
    before = frames()
    press_until(g, "circle", lambda: map_state(g) == MAP_LIST)
    items_ready(g, symbols)
    for button in ("down",) * (len(MAP_AREAS) + 2) + ("up",) * (len(MAP_AREAS) + 3):
        press(button)
    press("cross")
    at(MAP_INPUT)
    toured("Map: the area list walked past both ends, left", before)
    before = frames()
    scene = ovl(g, "menu5", "_currentScene")
    press_until(g, "circle", lambda: map_state(g) == MAP_LIST)
    items_ready(g, symbols)
    for _ in range(4):
        press("down")
    press("circle")
    menu_wait(g, "another area's map", lambda: map_state(g) == MAP_INPUT
              and ovl(g, "menu5", "_currentScene") != scene, limit=1200)
    g.run(20)
    for button in ("r1", "r1", "l1"):
        press(button, settle=10)
    g.hold(["l2", "right"], 4)
    g.run(10)
    for button in ("select", "select"):
        press(button, settle=20)
    toured(f"Map: area {scene} to {ovl(g, 'menu5', '_currentScene')}, its rooms, the turn, the help hidden",
           before)
    before = frames()
    press_until(g, "cross", lambda: map_state(g) != MAP_INPUT)
    menu_wait(g, "the top level", lambda: current_menu(g, symbols) & 0x3F == 0, limit=1200)
    g.until(lambda: int.from_bytes(g.read(symbols["D_800F4CC0"], 4), "little", signed=True) >= 0, guard=30,
            what="the top level's rows in")
    menu_close(g, symbols)
    map_list(g, symbols)
    press_until(g, "triangle", lambda: map_state(g) != MAP_LIST)
    menu_wait(g, "the menu closed", lambda: current_menu(g, symbols) == 0x3F, limit=1200)
    g.run(30)
    toured("Map: Cross to the top level, closed; reopened, the list, Triangle", before)


DATA_COMMAND, DATA_ROWS, DATA_SAVE, DATA_LOAD = 6, 6, 8, 10  # the top level's DATA row; vs_menu7_dataMenu's
DATA_FIRST_ROW = 10                                            # states: its rows (SAVE 10, LOAD 11), Save, Load


def data_state(g, symbols):
    return g.read(symbols["vs_battle_submenuStates"] + DATA_MENU, 1)[0]


def data_open(g, symbols):
    """Triangle, DATA, Circle: MENU7's SAVE / LOAD rows at rest."""
    from menu_nav import top_command
    g.press("triangle", vsyncs=3, settle=30)
    top_command(g, symbols, DATA_COMMAND)
    press_until(g, "circle", lambda: current_menu(g, symbols) & 0x3F == DATA_MENU)
    menu_wait(g, "Data's rows", lambda: data_state(g, symbols) == DATA_ROWS)
    items_ready(g, symbols)


def data_files_open(g, symbols, state=DATA_SAVE):
    """Data's SAVE (or LOAD), card 1's files at rest (MENU7's file menu: its frame and panels, drawn at once)."""
    data_open(g, symbols)
    if not any(i == DATA_FIRST_ROW + (state == DATA_LOAD) and sel for i, _, sel in live_rows(g)):
        g.press("down", vsyncs=3, settle=8)
    press_until(g, "circle", lambda: data_state(g, symbols) == state)
    g.run(90)  # (the cards' rows come in, the cards are read)
    g.press("circle", vsyncs=3, settle=8)  # card 1's files
    g.run(90)
    g.press("down", vsyncs=3, settle=8)
    g.run(30)


def data_files_leave(g, symbols):
    """From a card's files: Cross back to Data's rows, the menu closed (nothing saved or loaded)."""
    press_until(g, "cross", lambda: data_state(g, symbols) == DATA_ROWS, tries=6, frames=60)
    items_ready(g, symbols)
    menu_close(g, symbols)


def tour_data(g, symbols, toured, frames):
    """Data (MENU7, currentMenu 7) through the original menus, nothing saved or loaded: opened; its rows walked
    past both ends; SAVE: the memory cards walked, card 1's files walked down past the last page and back (the
    pages), out with Cross to the rows; LOAD the same; Cross to the top level, closed; reopened, Triangle."""
    def press(button, settle=8):
        g.press(button, vsyncs=3, settle=settle)

    def row(n):
        for _ in range(4):
            if any(i == DATA_FIRST_ROW + n and sel for i, _, sel in live_rows(g)):
                return
            press("down")
        raise RpcError(f"Data: row {n} never selected ({live_rows(g)})")

    before = frames()
    data_open(g, symbols)
    toured("Data opened", before)
    before = frames()
    for button in ("down",) * 3 + ("up",) * 3:
        press(button)
    items_ready(g, symbols)
    toured("Data: its rows walked", before)
    for n, (state, name) in enumerate(((DATA_SAVE, "SAVE"), (DATA_LOAD, "LOAD"))):
        before = frames()
        row(n)
        press_until(g, "circle", lambda: data_state(g, symbols) == state)
        g.run(90)  # (the cards' rows come in, the cards are read)
        for button in ("down", "up", "down", "up"):  # the two cards (on card 1 again)
            press(button)
        press("circle")  # card 1's files
        g.run(90)
        for button in ("down",) * 17 + ("up",) * 17:
            press(button, settle=4)
        press_until(g, "cross", lambda: data_state(g, symbols) == DATA_ROWS, tries=6, frames=60)
        items_ready(g, symbols)
        print(f"Data: {name} toured", flush=True)  # (progress)
        toured(f"Data: {name}: the cards, card 1's files, out", before)
    before = frames()
    press_until(g, "cross", lambda: current_menu(g, symbols) & 0x3F == 0)
    g.until(lambda: int.from_bytes(g.read(symbols["D_800F4CC0"], 4), "little", signed=True) >= 0, guard=30,
            what="the top level's rows in")
    menu_close(g, symbols)
    data_open(g, symbols)
    menu_close(g, symbols)
    toured("Data: Cross to the top level, closed; reopened, Triangle", before)


def data_file_to(g, index):
    """MENU7's file list: the hand on file `index` (0..4): Up past the first, then Down `index` times (the list pages
    past its third row; slots in use are passed over, so `index` counts the files the hand can reach)."""
    for _ in range(6):
        g.press("up", vsyncs=3, settle=8)
    for _ in range(index):
        g.press("down", vsyncs=3, settle=8)
    g.run(10)
    if not ovl(g, "menu7", "_selectCursorXy", signed=False):
        raise RpcError("Data: the file list's hand is not up")


def data_card2_open(g, symbols, state=DATA_SAVE):
    """Data's SAVE (or LOAD), card 2's files at rest (card_fixtures.file_states_fixture: a file at 100 hours and 100%
    map, the container's save, a temporary file, a slot free, a slot in use by another game's file)."""
    data_open(g, symbols)
    if not any(i == DATA_FIRST_ROW + (state == DATA_LOAD) and sel for i, _, sel in live_rows(g)):
        g.press("down", vsyncs=3, settle=8)
    press_until(g, "circle", lambda: data_state(g, symbols) == state)
    g.run(90)  # (the cards' rows come in, the cards are read)
    g.press("down", vsyncs=3, settle=8)  # card 2
    g.run(20)
    g.press("circle", vsyncs=3, settle=8)  # its files
    g.run(90)


def tour_data_card2(g, symbols, toured, frames):
    """Data's file menu on card 2 (card_fixtures' file_states, a copy: the only card a save here writes; the game's
    state rewound after each): SAVE: its files walked (100 hours and 100% map, a save, a temporary file, the free
    slot's New; the slot in use drawn, not reached); a save to the free slot (the progress, its completion); LOAD: file 1
    (100 hours) loaded (the progress, its completion, the game loaded)."""
    import card_fixtures
    card2 = MENUS_CARD2[0] if MENUS_CARD2 else None  # (a copy in the card guard's directory)
    if not card2 or not pathlib.Path(card2).exists():
        raise RpcError("Data: no card 2 (main() copies the file_states fixture to MENUS_CARD2)")

    def progress(what, done):
        seen = set()
        for _ in range(1800):
            seen.add(ovl(g, "menu7", "_fileProgressCounter") < 0)
            if True in seen and done():
                return
            g.run(1)
        raise RpcError(f"Data: {what}: never done (progress {ovl(g, 'menu7', '_fileProgressCounter')})")

    g.pause()
    g.mark()
    before = frames()
    data_card2_open(g, symbols)
    for button in ("down",) * 4 + ("up",) * 4:
        g.press(button, vsyncs=3, settle=8)
    toured("Data: SAVE, card 2's files walked (100 hours, 100% map, a temporary file, New, a slot in use)", before)
    before = frames()
    data_file_to(g, 3)
    writes = g.call("card")["writes"]
    g.press("circle", vsyncs=3, settle=2)
    progress("the save", lambda: g.call("card")["writes"] > writes and ovl(g, "menu7", "_fileProgressCounter") == 0)
    g.run(30)
    saved = card_fixtures.saves(bytearray(pathlib.Path(card2).read_bytes()))
    if sorted(saved) != [1, 2, 4]:
        raise RpcError(f"Data: the save to card 2's slot 4: the card copy's saves {sorted(saved)}")
    toured("Data: SAVE to card 2's free slot (a copy): the progress, its completion", before)
    print("Data: card 2's SAVE toured", flush=True)  # (progress)
    g.rewind()
    before = frames()
    data_card2_open(g, symbols, DATA_LOAD)
    for button in ("down",) * 4 + ("up",) * 4:
        g.press(button, vsyncs=3, settle=8)
    data_file_to(g, 0)
    press_until(g, "circle", lambda: ovl(g, "menu7", "_fileProgressCounter") != 0, frames=10)
    progress("the load", lambda: current_menu(g, symbols) == 0x3F)
    g.run(60)
    toured("Data: LOAD card 2's file 1 (100 hours): the progress, its completion, the game loaded", before)
    print("Data: card 2's LOAD toured", flush=True)  # (progress)
    g.rewind()
    menu_close(g, symbols)


SETUP_ROW, SETUP_COMMANDS = 21, 3  # Items' Manage page: Setup's row; MENUC's state: its command list (rows 10..15)
WORKSHOP = (47, 0)  # a workshop (vs_battle_inWorkshop 6: every material; ui_workshop_smoke's)


def setup_state(g, symbols):
    return g.read(symbols["vs_battle_submenuStates"] + SETUP_MENU, 1)[0]


def warp(g, zone, room):
    """RPC room_goto (scripts and enemies off; ui_workshop_smoke's warp)."""
    reply = g.call("room_goto", zone=zone, room=room, scripts=False, enemies=False)
    if not reply.get("ok"):
        raise RpcError(f"room_goto {zone}/{room}: {reply}")
    for _ in range(60):
        g.run(20)
        if g.call("room_goto", action="status")["phase"] in (0, 4):
            break
    g.run(60)
    st = g.state()
    if (st["zone"], st["room"]) != (zone, room):
        raise RpcError(f"warped to {st['zone']}/{st['room']}, not {zone}/{room}")


def setup_open(g, symbols):
    """Triangle, Items, its Manage page, Setup: MENUC's command list at rest."""
    items_open(g, symbols)
    for _ in range(9):
        if SETUP_ROW in items_live(g, symbols):
            break
        g.press("r1", vsyncs=3, settle=10)
        items_ready(g, symbols)
    for _ in range(4):
        if any(i == SETUP_ROW and sel for i, _, sel in live_rows(g)):
            break
        g.press("down", vsyncs=3, settle=6)
    press_until(g, "circle", lambda: current_menu(g, symbols) & 0x3F == SETUP_MENU)
    menu_wait(g, "Setup's commands", lambda: setup_state(g, symbols) == SETUP_COMMANDS)
    items_ready(g, symbols)


def workshop_setup_open(g, symbols):
    """Warped to a workshop, ui_workshop_smoke's bag (three gems, gem slots on the grips and shields, some damage),
    Setup's commands at rest."""
    import ui_workshop_smoke
    warp(g, *WORKSHOP)
    bag = ui_workshop_smoke.Session.__new__(ui_workshop_smoke.Session)
    bag.g, bag.sym, bag.name = g, symbols, "classic_parity"
    bag.prepare()
    setup_open(g, symbols)


def workshop_session(g, symbols):
    """ui_workshop_smoke's Session on this game (its original-menu helpers: o_setup, o_command, o_pick, ...)."""
    import ui_workshop_smoke
    bag = ui_workshop_smoke.Session.__new__(ui_workshop_smoke.Session)
    bag.g, bag.sym, bag.name = g, symbols, "classic_parity"
    return bag


def setup_loose_parts(g, symbols):
    """Setup in a workshop (workshop_setup_open), closed; two weapons not equipped taken apart (Disassemble, Yes:
    ui_workshop_smoke's o_disassemble): loose blades and grips for Assemble's and Combine's two picks. Returns the
    Session."""
    workshop_setup_open(g, symbols)
    menu_close(g, symbols)
    bag = workshop_session(g, symbols)
    for _ in range(2):
        b = bag.bag()
        record = next(i for i in b.listed(0) if not b.weapon(i)["equipped"])
        bag.o_disassemble(False, record)
    return bag


def setup_two_picks(bag, command):
    """From the menu closed: Setup, Assemble (command 0: a loose blade and grip) or Combine (5: Blades, its first two
    blades): both picked, the OK beside them (MENUC _drawOk)."""
    bag.o_setup()
    bag.o_command(command)
    if command == 5:
        bag.wait_ready(want=22)
        bag.nav([20, 21, 22], 20, "Blades")
        bag.press("circle", settle=8)
        bag.wait_ready(want=12, limit=900)
        first, second = bag.bag().combine_pick(1, [])[1:3]
        bag.nav([11, 12], 11, "First")
        bag.press("circle", settle=8)
        bag.o_pick(bag.bag().combine_pick(1, []), first)
        bag.wait_ready(want=12)
        bag.press("circle", settle=8)  # Second (the cursor moved there)
        bag.o_pick(bag.bag().combine_pick(1, [first]), second)
    else:
        bag.wait_ready(want=12)
        blades, grips = bag.bag().blade_pick(), bag.bag().grip_pick()
        bag.nav([11, 12], 11, "Blade")
        bag.press("circle", settle=8)
        bag.o_pick(blades, blades[1])
        bag.wait_ready(want=12)
        bag.press("circle", settle=8)  # the grip row (the cursor moved there)
        bag.o_pick(grips, grips[1])
    bag.wait_ready(want=12)
    bag.g.run(30)


def setup_two_picks_open(g, symbols, command):
    """Setup in a workshop, two weapons taken apart, Assemble (0: a blade and a grip) or Combine, Blades (5: two
    blades) picked: the OK at rest. Returns the bag as it was (inventory, indices: loot_restore)."""
    saved = (g.read(symbols["vs_main_inventory"], INVENTORY_BYTES), g.read(symbols["vs_main_inventoryIndices"],
                                                                            INDICES_BYTES))
    setup_two_picks(setup_loose_parts(g, symbols), command)
    return saved


def tour_setup_picks(g, symbols, toured, frames):
    """Setup's two-pick states (in tour_setup's workshop; the bag rewound after): two weapons taken apart; Assemble:
    a blade and a grip picked (the OK), the rows walked, Cross back through the picks; Combine, Blades: two blades
    picked (the OK), walked, Cross back; each left to the commands."""
    def walk():
        for button in ("down",) * 6 + ("up",) * 6:
            g.press(button, vsyncs=3, settle=4)
        items_ready(g, symbols)

    def out():
        for _ in range(8):
            if setup_state(g, symbols) == SETUP_COMMANDS and 15 in items_live(g, symbols):
                items_ready(g, symbols)
                return
            g.press("cross", vsyncs=3, settle=12)
            items_ready(g, symbols)
        raise RpcError(f"Setup: never back on the commands (state {setup_state(g, symbols)})")

    menu_close(g, symbols)
    bag = workshop_session(g, symbols)
    for _ in range(2):
        b = bag.bag()
        bag.o_disassemble(False, next(i for i in b.listed(0) if not b.weapon(i)["equipped"]))
    for command, name in ((0, "Assemble"), (5, "Combine, Blades")):
        before = frames()
        setup_two_picks(bag, command)
        walk()
        out()
        menu_close(g, symbols)
        print(f"Setup: {name}: two picks toured", flush=True)  # (progress)
        toured(f"Setup: {name}: two picked (the OK), walked, out", before)
    setup_open(g, symbols)


def assemble_rename_open(g, symbols, bag=None):
    """Setup in a workshop, two weapons taken apart (setup_loose_parts, unless `bag` is given), Assemble: a blade and a
    grip picked, Start, Yes: MENU8's rename grid for the new weapon at rest (its name the blade's). Returns the bag as
    it was (inventory, indices: loot_restore)."""
    saved = (g.read(symbols["vs_main_inventory"], INVENTORY_BYTES), g.read(symbols["vs_main_inventoryIndices"],
                                                                            INDICES_BYTES))
    bag = bag or setup_loose_parts(g, symbols)
    setup_two_picks(bag, 0)
    bag.press("start", settle=8)
    bag.wait_ready(want=29 + 2)  # (MENUC's Yes / No / Cancel)
    press_until(g, "circle", lambda: current_menu(g, symbols) & 0x3F == RENAME_MENU, frames=60)
    rename_wait(g, symbols, "the new weapon's rename grid at rest")
    g.run(40)
    return saved


def assemble_rename_leave(g, symbols):
    """From the new weapon's rename grid or its Yes / No: Start, Yes (the name as it is): the weapon made, back on
    Setup's commands; the menu closed."""
    if rename_state(g)[0]:
        press_until(g, "start", lambda: rename_state(g)[0] == 0, frames=10)
    menu_wait(g, "the rename's Yes / No", lambda: any(i == 22 for i, _, _ in live_rows(g)) and rename_state(g)[1] == 10)
    g.run(30)
    for _ in range(4):
        if any(i == 20 and sel for i, _, sel in live_rows(g)):
            break
        g.press("up", vsyncs=3, settle=6)
    press_until(g, "circle", lambda: current_menu(g, symbols) & 0x3F == SETUP_MENU, frames=60)
    menu_wait(g, "Setup's commands after the new weapon's name", lambda: setup_state(g, symbols) == SETUP_COMMANDS
              and 15 in items_live(g, symbols), limit=1200)
    items_ready(g, symbols)
    menu_close(g, symbols)


def tour_assemble_rename(g, symbols, toured, frames):
    """MENU8's rename grid reached from Assemble (a new weapon's name; in tour_setup's workshop, two weapons taken
    apart, the bag rewound after): a blade and a grip picked, Start, Yes: the grid in; walked past both edges, a
    character typed and taken back, the pen moved; the commands walked; Start: its Yes / No walked, Cross (the grid
    again); Start, Yes: the weapon made, back on Setup's commands."""
    def press(button, settle=4):
        g.press(button, vsyncs=3, settle=settle)

    menu_close(g, symbols)
    bag = workshop_session(g, symbols)
    for _ in range(2):  # (after tour_setup_picks the parts are loose already: nothing left to take apart)
        b = bag.bag()
        loose = [i for i in b.listed(0) if not b.weapon(i)["equipped"]]
        if not loose or len(b.blade_pick()) > 1 and len(b.grip_pick()) > 1:
            break
        bag.o_disassemble(False, loose[0])
    before = frames()
    assemble_rename_open(g, symbols, bag)
    toured("Setup: Assemble, a blade and a grip, Yes: the new weapon's rename grid in", before)
    before = frames()
    for button in ("right",) * 15 + ("down",) * 10 + ("up", "left", "circle", "cross", "l1", "r1", "r1"):
        press(button)
    press("left")  # (off the grid: the commands)
    for button in ("down",) * 5 + ("up",) * 5:
        press(button)
    press("right", settle=8)
    g.run(20)
    toured("Setup: Assemble's rename grid walked, typed, taken back, the commands walked", before)
    before = frames()
    press_until(g, "start", lambda: rename_state(g)[0] == 0, frames=10)
    menu_wait(g, "the rename's Yes / No", lambda: any(i == 22 for i, _, _ in live_rows(g)) and rename_state(g)[1] == 10)
    g.run(20)
    for button in ("down",) * 3 + ("up",) * 3:
        press(button, settle=6)
    press("cross", settle=10)
    rename_wait(g, symbols, "the new weapon's rename grid again")
    toured("Setup: Assemble's rename, its Yes / No walked, Cross: the grid again", before)
    before = frames()
    assemble_rename_leave(g, symbols)
    toured("Setup: Assemble's rename, Yes: the weapon made, the commands", before)
    setup_open(g, symbols)


def setup_select(g, symbols, row_id):
    """Down until MENUC's row `row_id` is the selected one."""
    for _ in range(12):
        if any(i == row_id and sel for i, _, sel in live_rows(g)):
            return
        g.press("down", vsyncs=3, settle=6)
        items_ready(g, symbols)
    raise RpcError(f"Setup: row {row_id} never selected ({live_rows(g)})")


def setup_assemble_open(g, symbols):
    """Setup in a workshop (workshop_setup_open), Assemble, the blades' pick list at rest: a blade's stats panel,
    DP / PP bars and table."""
    workshop_setup_open(g, symbols)
    setup_select(g, symbols, 10)
    g.press("circle", vsyncs=3, settle=10)
    items_ready(g, symbols)
    setup_select(g, symbols, 11)
    g.press("circle", vsyncs=3, settle=10)
    items_ready(g, symbols)
    g.run(40)


def setup_gems_open(g, symbols):
    """Setup in a workshop, Gems (row 11), Weapons (row 20), the first weapon: its gem slots at rest (the OK mark
    beside them: MENUC's _drawOk)."""
    workshop_setup_open(g, symbols)
    setup_select(g, symbols, 11)
    g.press("circle", vsyncs=3, settle=10)
    items_ready(g, symbols)
    setup_select(g, symbols, 20)
    for _ in range(2):
        g.press("circle", vsyncs=3, settle=10)
        items_ready(g, symbols)
    g.run(40)


def setup_repair_open(g, symbols):
    """Setup in a workshop, Repair (row 14): its Yes / No at rest (MENUC's own hand: vs_battle_drawCursor)."""
    workshop_setup_open(g, symbols)
    setup_select(g, symbols, 14)
    g.press("circle", vsyncs=3, settle=10)
    items_ready(g, symbols)
    g.run(40)


RENAME_MENU = 8  # currentMenu while MENU8's rename grid is up (Setup's Rename, Yes)


def rename_state(g):
    """MENU8's rename grid: (shown, its slide step: 0 at rest, 10 out)."""
    return ovl(g, "menu8", "_showRenameInterface", 1), ovl(g, "menu8", "_animStep", 1, signed=False)


def rename_column(g):
    """The hand on the grid: (column, row) (_renameMenuInput's statics)."""
    return ovl(g, "menu8", "_charTableColumn", 1), ovl(g, "menu8", "_charTableRow", 1)


def rename_wait(g, symbols, what="the rename grid at rest"):
    menu_wait(g, what, lambda: current_menu(g, symbols) & 0x3F == RENAME_MENU and rename_state(g) == (1, 0))


def setup_rename_open(g, symbols):
    """Setup in a workshop, Rename (row 13), the first weapon, Yes: MENU8's rename grid at rest (its buttons, bars,
    words, name, arrows, table and background; the character's and the name's highlight boxes; the hand)."""
    workshop_setup_open(g, symbols)
    setup_select(g, symbols, 13)
    g.press("circle", vsyncs=3, settle=10)
    items_ready(g, symbols)
    g.press("circle", vsyncs=3, settle=10)  # (the first weapon: its Yes / No)
    menu_wait(g, "Rename's Yes / No", lambda: any(i == 29 and sel for i, _, sel in live_rows(g)))
    press_until(g, "circle", lambda: current_menu(g, symbols) & 0x3F == RENAME_MENU, frames=60)
    rename_wait(g, symbols)
    g.run(40)


def setup_rename_confirm_open(g, symbols):
    """The rename grid (setup_rename_open), Start: its Yes / No at rest (rows 20..22, BATTLE's hand as menu_cursor)."""
    setup_rename_open(g, symbols)
    press_until(g, "start", lambda: rename_state(g)[0] == 0, frames=10)
    menu_wait(g, "the rename's Yes / No", lambda: any(i == 22 for i, _, _ in live_rows(g)) and rename_state(g)[1] == 10)
    g.run(60)


def rename_leave(g, symbols):
    """From the rename grid or its Yes / No: Start, the third row (no change), out to Setup's commands; the menu
    closed."""
    if rename_state(g)[0]:
        press_until(g, "start", lambda: rename_state(g)[0] == 0, frames=10)
    menu_wait(g, "the rename's Yes / No", lambda: any(i == 22 for i, _, _ in live_rows(g)) and rename_state(g)[1] == 10)
    g.run(30)
    for _ in range(4):
        if any(i == 22 and sel for i, _, sel in live_rows(g)):
            break
        g.press("down", vsyncs=3, settle=6)
    g.press("circle", vsyncs=3, settle=10)
    menu_wait(g, "Setup's commands after the rename", lambda: current_menu(g, symbols) & 0x3F == SETUP_MENU
              and setup_state(g, symbols) == SETUP_COMMANDS)
    items_ready(g, symbols)
    menu_close(g, symbols)


def tour_rename(g, symbols, toured, frames):
    """MENU8's rename grid from Setup (Rename, the first weapon, Yes; the save room's state rewound to after by
    tour_setup): the grid walked across past both edges and down past both; two characters typed, one taken back,
    the pen moved (L1, R1); left off the grid onto the commands, walked past both ends, Insert, Delete and Back
    each taken (the bars' fades), Right back onto the grid; Start: its Yes / No walked, Cross (back to the grid,
    sliding in again); Start, the third row (no change), out to Setup's commands."""
    def press(button, settle=4):
        g.press(button, vsyncs=3, settle=settle)

    before = frames()
    setup_select(g, symbols, 13)
    press("circle", settle=10)
    items_ready(g, symbols)
    press("circle", settle=10)
    menu_wait(g, "Rename's Yes / No", lambda: any(i == 29 and sel for i, _, sel in live_rows(g)))
    press_until(g, "circle", lambda: current_menu(g, symbols) & 0x3F == RENAME_MENU, frames=60)
    rename_wait(g, symbols)
    toured("Setup: Rename, the grid in", before)
    before = frames()
    for button in ("right",) * 15 + ("down",) * 10 + ("up", "left", "circle", "circle", "cross", "l1", "r1", "r1"):
        press(button)
    if rename_column(g) != (0, 0):
        raise RpcError(f"the rename grid: the hand at {rename_column(g)}, not (0, 0)")
    toured("Setup: Rename, the grid walked, typed, taken back, the pen moved", before)
    before = frames()
    press("left")  # (off the grid: the commands, the hand on Delete)
    for button in ("down",) * 5 + ("up",) * 7:  # (round both ways; up to OK at the top)
        press(button)
    for row in (3, 2, 1):  # Insert, Delete, Back (from OK at the top)
        for _ in range(row):
            press("down")
        press("circle", settle=8)
        for _ in range(row):
            press("up")
    press("right", settle=8)
    g.run(20)
    toured("Setup: Rename, the commands walked, Insert / Delete / Back, back on the grid", before)
    before = frames()
    press_until(g, "start", lambda: rename_state(g)[0] == 0, frames=10)
    menu_wait(g, "the rename's Yes / No", lambda: any(i == 22 for i, _, _ in live_rows(g)) and rename_state(g)[1] == 10)
    g.run(20)
    for button in ("down",) * 3 + ("up",) * 3:
        press(button, settle=6)
    press("cross", settle=10)
    rename_wait(g, symbols, "the rename grid again")
    toured("Setup: Rename, its Yes / No walked, Cross: the grid again", before)
    before = frames()
    press_until(g, "start", lambda: rename_state(g)[0] == 0, frames=10)
    menu_wait(g, "the rename's Yes / No", lambda: any(i == 22 for i, _, _ in live_rows(g)) and rename_state(g)[1] == 10)
    g.run(20)
    press("down", settle=6)
    press("down", settle=6)
    press("circle", settle=10)
    menu_wait(g, "Setup's commands after the rename", lambda: current_menu(g, symbols) & 0x3F == SETUP_MENU
              and setup_state(g, symbols) == SETUP_COMMANDS)
    items_ready(g, symbols)
    toured("Setup: Rename, no change, out to the commands", before)


def loot_leave_open(g, symbols):
    """The loot menu (loot_open), Cross: leave's Yes / No at rest (MENUB's own hand: vs_battle_drawCursor)."""
    loot_open(g, symbols)
    g.press("cross", vsyncs=3, settle=6)
    items_ready(g, symbols)
    g.run(40)


def tour_setup(g, symbols, toured, frames):
    """Setup (MENUC, currentMenu 13; Items > Setup) through the original menus, in a workshop (warped there; the
    save room's state rewound to after; ui_workshop_smoke's bag: gems, gem slots, damage), nothing changed: opened; its commands walked past both ends; Assemble:
    the blade's and the grip's lists walked; Gems: Weapons, a weapon's gem slots, a slot's gem list, Shields'
    list; Disassemble: a weapon's Yes / No (Cross); Rename: a weapon's Yes / No (Cross); Repair's Yes / No
    (Cross); Combine: its categories, the blades' first pick list; each left with Cross; the rename grid
    (tour_rename: MENU8, Yes on Rename's); closed. Every list walked is paged with Square after (the stats' table,
    where one is up)."""
    def press(button, settle=6):
        g.press(button, vsyncs=3, settle=settle)
        items_ready(g, symbols)

    def walk(count=10):
        for button in ("down",) * count + ("up",) * count + ("square",) * 4:  # (Square: the table's pages, round;
            g.press(button, vsyncs=3, settle=4)                               # MENUC takes no Square)
        items_ready(g, symbols)

    def select(row_id):
        setup_select(g, symbols, row_id)

    def commands():
        """Cross until MENUC's command list takes input again."""
        for _ in range(8):
            if setup_state(g, symbols) == SETUP_COMMANDS and 15 in items_live(g, symbols):
                items_ready(g, symbols)
                return
            press("cross", settle=10)
        raise RpcError(f"Setup: never back on the commands (state {setup_state(g, symbols)}, {live_rows(g)})")

    g.pause()
    g.mark()  # (the save room again after: the tours and proofs after this one run there)
    g.write(symbols["vs_main_settings"] + 1, b"\x00")
    intro = g.read(symbols["vs_main_stateFlags"] + INTRO_STATE, 1)
    g.write(symbols["vs_main_stateFlags"] + INTRO_STATE, bytes([max(intro[0], 3)]))
    before = frames()
    workshop_setup_open(g, symbols)
    walk(8)
    toured("Setup opened (a workshop), its commands walked", before)
    # each command's script (MENUC's state while in it): "sel N" a row by id, "walk N" its list down and up
    scripts = (("Assemble", 4, ("sel 11", "circle", "walk 8", "cross", "sel 12", "circle", "walk 8", "cross")),
               ("Gems", 5, ("walk 2", "sel 20", "circle", "walk 8", "circle", "walk 3", "circle", "walk 8",
                            "cross", "cross", "cross", "sel 21", "circle", "walk 8", "cross")),
               ("Disassemble", 6, ("walk 2", "sel 20", "circle", "walk 8", "circle", "walk 1", "cross")),
               ("Rename", 7, ("walk 8", "circle", "walk 1", "cross")),
               ("Repair", 8, ("walk 1",)),
               ("Combine", 9, ("walk 3", "sel 20", "circle", "sel 11", "circle", "walk 8", "cross")))
    for row, (name, state, script) in enumerate(scripts):
        before = frames()
        select(10 + row)
        press("circle", settle=10)
        for step in ("in",) + script:
            if step.startswith("walk"):
                walk(int(step[5:]))
            elif step.startswith("sel"):
                select(int(step[4:]))
            elif step != "in":
                press(step, settle=10)
            g.run(10)
            if current_menu(g, symbols) & 0x3F != SETUP_MENU or setup_state(g, symbols) != state:
                raise RpcError(f"Setup: {name}: left the command at {step} (currentMenu "
                               f"{current_menu(g, symbols):#x}, state {setup_state(g, symbols)})")
        commands()
        print(f"Setup: {name} toured", flush=True)  # (progress)
        toured(f"Setup: {name}", before, command=row)
    tour_rename(g, symbols, toured, frames)
    print("Setup: the rename grid toured", flush=True)  # (progress)
    tour_setup_picks(g, symbols, toured, frames)
    tour_assemble_rename(g, symbols, toured, frames)
    print("Setup: Assemble's rename grid toured", flush=True)  # (progress)
    before = frames()
    menu_close(g, symbols)
    toured("Setup closed", before)
    g.rewind()  # (the save room, the cursor memory, introState)


def loot_input(g, symbols, limit=600):
    """MENUB's list takes input (ui_loot_smoke's o_input: _processLootMenu's row + 1, the rows in place)."""
    calm = 0
    for _ in range(limit):
        ok = (1 <= g.read(symbols["D_8010A6B7"], 1)[0] <= 8 and g.read(symbols["_lootLeftEdge"], 1)[0] == 0
              and g.read(symbols["_controlsEnabled"], 1)[0] == 1)
        calm = calm + 1 if ok else 0
        if calm >= 3:
            g.run(2)
            return
        g.run(1)
    raise RpcError(f"Loot: the list never took input (currentMenu {current_menu(g, symbols):#x})")


def loot_pick(g, symbols, want):
    """A two-row menu of MENUB's (Yes / No; a row's own action / Organize: menu items 30, 31): to `want`, Circle."""
    items_ready(g, symbols)
    g.run(4)
    for _ in range(4):
        if any(i == want and sel for i, _, sel in live_rows(g)):
            break
        g.press("up", vsyncs=3, settle=6)
    if not any(i == want and sel for i, _, sel in live_rows(g)):
        raise RpcError(f"Loot: menu item {want} never selected ({live_rows(g)})")
    g.press("circle", vsyncs=3, settle=6)


def loot_closed(g, symbols):
    menu_wait(g, "the loot menu closed", lambda: current_menu(g, symbols) == 0x3F
              and int.from_bytes(g.read(symbols["D_800F196C"], 4), "little") == 1, limit=1200)
    g.run(60)


def loot_open(g, symbols, lists=None):
    """The loot menu opened as after a battle (RPC loot: ui_loot_smoke's lists; by default a weapon and an armor
    copied from the bag and three misc items), MENUB's list taking input."""
    import ui_loot_smoke as loot
    if lists is None:
        inv = g.read(symbols["vs_main_inventory"], loot.INVENTORY_BYTES)
        idx = g.read(symbols["vs_main_inventoryIndices"], loot.INDICES_BYTES)
        weapons = [i for i in loot.list_rows(idx, 0) if inv[loot.WEAPONS + i * loot.WEAPON + 1]
                   and not inv[loot.WEAPONS + i * loot.WEAPON + 3]]
        armor = [i for i in loot.list_rows(idx, 4) if inv[loot.ARMOR + i * loot.ARMOR_REC]]
        if not weapons or not armor:
            raise RpcError("Loot: the card's bag has no unequipped weapon or no armor")
        lists = [loot.Loot().weapon(inv, weapons[0]).armor(inv, armor[0])
                 .misc((loot.CURE_ROOT, 3), (340, 2), (loot.CURE_BULB, 1))]
    if not g.call("loot", hex="".join(x.hex() for x in lists), chest=False).get("opened"):
        raise RpcError("Loot: the loot menu did not open")
    menu_wait(g, "the loot menu", lambda: current_menu(g, symbols) & 0x3F == LOOT_MENU, limit=600)
    loot_input(g, symbols)


def loot_organize_open(g, symbols, walk=False):
    """The misc bag full (ui_loot_smoke's fill_misc) and the status views' fixture (status_view_fixture: broken /
    full blades, gem slots 0 / 3, loose gems); the loot menu opened with three misc items, the first row (it does
    not fit): Consolidate / Organize, Organize inventory: MENUB's Items at rest (its first category's list).
    Returns the bag as it was (inventory, indices)."""
    import ui_loot_smoke as loot
    saved = (g.read(symbols["vs_main_inventory"], INVENTORY_BYTES), g.read(symbols["vs_main_inventoryIndices"],
                                                                            INDICES_BYTES))
    status_view_fixture(g, symbols)
    recs = [(loot.CURE_ROOT, 90)] + [(loot.CURE_BULB, 1)] * 63
    g.write(symbols["vs_main_inventory"] + loot.MISC,
            b"".join(i.to_bytes(2, "little") + bytes([n, k + 1]) for k, (i, n) in enumerate(recs)))
    lo, _ = loot.IDX[6]
    g.write(symbols["vs_main_inventoryIndices"] + lo, bytes(range(1, 65)))
    g.run(2)
    loot_open(g, symbols, [loot.Loot().misc((loot.CURE_ROOT, 50), (loot.CURE_ROOT, 30), (341, 1))])
    g.press("circle", vsyncs=3, settle=6)  # the first row: Consolidate / Organize
    items_ready(g, symbols)
    for button in ("down", "up", "down") if walk else ():
        g.press(button, vsyncs=3, settle=6)
        items_ready(g, symbols)
    loot_pick(g, symbols, 31)  # Organize inventory: MENUB's Items
    menu_wait(g, "Organize inventory", lambda: 20 in items_live(g, symbols))
    items_ready(g, symbols)
    return saved


def loot_status_open(g, symbols, category):
    """Loot's Organize inventory (loot_organize_open), the category, Square on its first row: the item's status
    view at rest. Returns the bag as it was."""
    saved = loot_organize_open(g, symbols)
    if not items_to_category(g, symbols, category):
        raise RpcError(f"Loot: Organize inventory never reached category {category}")
    rows = items_rows_now(g, symbols)
    g.press("square", vsyncs=3, settle=20)
    g.run(40)
    if items_rows_now(g, symbols) == rows:
        raise RpcError(f"Loot: category {category}'s status view never opened")
    items_ready(g, symbols)
    g.run(20)
    return saved


def loot_discard_picker(g, symbols):
    """In Loot's Organize inventory: the misc category, its first row picked, its actions, Discard: the amount picker
    (items1.h _discardMenu) at rest."""
    if not items_to_category(g, symbols, 6):
        raise RpcError("Loot: Organize inventory never reached the misc category")
    discard_picker(g, symbols, ("circle", "circle"))  # (picked up, its actions)


def loot_discard_open(g, symbols):
    """Loot's Organize inventory (loot_organize_open), a misc row's Discard: the amount picker at rest. Returns the bag
    as it was."""
    saved = loot_organize_open(g, symbols)
    loot_discard_picker(g, symbols)
    return saved


def loot_leave(g, symbols):
    """From anywhere in the loot menu (Organize inventory, a status view): Cross back to MENUB's list, Cross, leave:
    Yes; the menu closed."""
    for _ in range(3):  # (a status view: Cross to the list; the list: Cross to MENUB's)
        g.press("cross", vsyncs=3, settle=20)
        items_ready(g, symbols)
        try:
            loot_input(g, symbols, limit=120)
            break
        except RpcError:
            continue
    else:
        raise RpcError("Loot: never back on MENUB's list")
    g.press("cross", vsyncs=3, settle=6)
    loot_pick(g, symbols, 30)  # leave: Yes
    loot_closed(g, symbols)


def loot_restore(g, saved):
    symbols = rood_rpc.symbols()
    g.write(symbols["vs_main_inventory"], saved[0])
    g.write(symbols["vs_main_inventoryIndices"], saved[1])


def tour_loot(g, symbols, toured, frames):
    """Loot (MENUB, currentMenu 12) through the original menus, opened as the game opens it after a battle (RPC
    loot: ui_loot_smoke's lists, from the bag's own records; the save room's state rewound to after): a weapon,
    an armor and three misc items: the list walked past both ends, Cross's Yes / No left on No, a row taken,
    Square (the rest taken); the misc bag full and a list of three misc items: a row that does not fit, its
    Consolidate / Organize menu, Organize inventory (MENUB's Items: every category round with R1 and L1, a list
    walked), back, Consolidate's Yes / No left on No, Cross, Yes (left)."""
    import ui_loot_smoke as loot

    def press(button, settle=6):
        g.press(button, vsyncs=3, settle=settle)

    g.pause()
    g.mark()  # (the save room's bag again after)
    g.write(symbols["vs_main_settings"] + 1, b"\x00")
    before = frames()
    loot_open(g, symbols)
    toured("Loot opened (a weapon, an armor, three misc items)", before)
    before = frames()
    for button in ("down",) * 7 + ("up",) * 7:
        press(button)
    loot_input(g, symbols)
    press("cross")  # leave? No
    loot_pick(g, symbols, 31)
    loot_input(g, symbols)
    toured("Loot: the list walked, leave's Yes / No left on No", before)
    before = frames()
    count = g.read(symbols["_lootListCount"], 1)[0]
    press("circle")  # the first row taken
    menu_wait(g, "a row taken", lambda: g.read(symbols["_lootListCount"], 1)[0] < count, limit=240)
    loot_input(g, symbols)
    press("square")  # the rest
    loot_closed(g, symbols)
    toured("Loot: a row taken, the rest (Square)", before)
    before = frames()
    loot_organize_open(g, symbols, walk=True)
    for button in ("r1",) * 8 + ("l1",) * 8:
        press(button, settle=10)
        items_ready(g, symbols)
    for button in ("down",) * 6 + ("up",) * 6:
        press(button, settle=4)
    items_ready(g, symbols)
    views = []
    for category, name in ITEM_CATEGORIES.items():  # (each category's status view, as Items' tour_status_views)
        if not items_to_category(g, symbols, category) or not any(20 <= i < 30 for i in items_live(g, symbols)):
            continue
        rows = items_rows_now(g, symbols)
        press("square", settle=20)
        g.run(40)
        if items_rows_now(g, symbols) == rows:
            continue
        for button in ("r1",) * 12 + ("l1",) * 12:  # (every item of the category, round and back)
            press(button, settle=12)
            if button == "r1":  # (Square: the table's next page)
                press("square", settle=12)
        for button in ("down",) * 8 + ("right", "left") + ("up",) * 8:
            press(button)
        g.run(20)
        press("cross", settle=20)
        items_ready(g, symbols)
        views.append(name)
    print(f"Loot: Organize inventory's status views {views}", flush=True)  # (progress)
    toured("Loot: a row that does not fit, Organize inventory (its categories round, their status views)",
           before, views=views)
    before = frames()  # the misc category, its first row's Discard: the amount picker walked and left
    loot_discard_picker(g, symbols)
    for button in PICKER_WALK:
        press(button, settle=4)
    press("cross", settle=10)  # (the picker left)
    g.run(20)
    items_ready(g, symbols)
    if any(i >= 32 for i in items_live(g, symbols)):  # (the actions again: Cross to the list)
        press("cross", settle=10)
        items_ready(g, symbols)
    press("cross", settle=10)
    loot_input(g, symbols)
    print("Loot: Organize inventory's discard picker toured", flush=True)  # (progress)
    toured("Loot: Organize inventory, a misc row's Discard: its amount picker walked; back", before)
    before = frames()
    press("circle")
    loot_pick(g, symbols, 30)  # Consolidate
    loot_pick(g, symbols, 31)  # No
    loot_input(g, symbols)
    press("cross")
    loot_pick(g, symbols, 30)  # leave: Yes
    loot_closed(g, symbols)
    toured("Loot: Consolidate's Yes / No left on No; left", before)
    g.rewind()  # (the bag, the cursor memory)


STORAGE_TOP, STORAGE_ROW = 3, 0x9E  # MENUD's state: its top menu; D_800F4EE8 + 0x9E: its row (ui_container_smoke)


def storage_state(g, symbols):
    return g.read(symbols["vs_battle_submenuStates"] + STORAGE_MENU, 1)[0]


def storage_top(g, symbols, limit=2400):
    """MENUD's top menu taking input (ui_container_smoke's a_ready: its state 3, D_80109A70 3)."""
    menu_wait(g, "Storage's top menu", lambda: current_menu(g, symbols) & 0x3F == STORAGE_MENU
              and storage_state(g, symbols) == STORAGE_TOP and ovl(g, "menud", "D_80109A70", 1) == 3, limit=limit)
    items_ready(g, symbols)
    g.run(20)


def storage_open(g, symbols, walk=False):
    """The container opened as the field opens it (RPC container: MENU7's saveContainerMenu), its first prompt
    answered Yes (rows 0x1E / 0x1F, the shared rows; BATTLE's hand: walked when `walk`): MENUD's top menu."""
    if not g.call("container").get("opened"):
        raise RpcError("Storage: the container did not open")
    menu_wait(g, "MENU7's prompt", lambda: g.read(symbols["vs_battle_submenuStates"] + DATA_MENU, 1)[0] in (1, 5, 12))
    g.run(40)
    for button in ("down", "up") * 2 if walk else ():
        g.press(button, vsyncs=3, settle=8)
    if not any(i == 0x1E and sel for i, _, sel in live_rows(g)):
        g.press("up", vsyncs=3, settle=6)
    g.press("circle", vsyncs=3, settle=6)
    storage_top(g, symbols)


def storage_store(g, symbols):
    """Storage's top menu, Store (row 1): the bag's first tab's list at rest (the tab arrows)."""
    storage_open(g, symbols)
    for _ in range(4):
        if g.read(symbols["D_800F4EE8"] + STORAGE_ROW, 1)[0] == 1:
            break
        g.press("down", vsyncs=3, settle=8)
        items_ready(g, symbols)
    g.press("circle", vsyncs=3, settle=20)
    items_ready(g, symbols)
    g.run(30)


STORAGE_WARNING, STORAGE_SAVE_LIST = 12, 11  # vs_menu7_saveContainerMenu's states: after MENUD, the warning's Yes /
# No (_promptYesNo: rows 0x1E / 0x1F, BATTLE's hand), the save list (_showSaveMenu, _drawFileMenu)


def storage_warning_wait(g, symbols):
    """MENU7 after MENUD (Cross at its top): the warning's Yes / No taking input."""
    menu_wait(g, "MENU7's warning", lambda: current_menu(g, symbols) & 0x3F == DATA_MENU
              and data_state(g, symbols) == STORAGE_WARNING
              and any(i in (0x1E, 0x1F) and sel for i, _, sel in live_rows(g)), limit=1200)
    g.run(30)


def storage_save_list_wait(g, symbols):
    """MENU7's save list after MENUD (the warning's Yes), its cards read."""
    menu_wait(g, "MENU7's save list", lambda: data_state(g, symbols) == STORAGE_SAVE_LIST, limit=1200)
    g.run(120)


def storage_warning_open(g, symbols):
    """Storage's top menu (storage_open), Cross: MENU7's warning, its Yes / No at rest (BATTLE's hand)."""
    storage_open(g, symbols)
    press_until(g, "cross", lambda: current_menu(g, symbols) & 0x3F == DATA_MENU, frames=60)
    storage_warning_wait(g, symbols)
    g.run(30)


def storage_save_list_open(g, symbols):
    """MENU7's warning (storage_warning_open), Yes: the save list at rest (the cards)."""
    storage_warning_open(g, symbols)
    g.press("circle", vsyncs=3, settle=10)
    storage_save_list_wait(g, symbols)


def storage_leave(g, symbols):
    """From MENU7's save list or its warning: Cross to the warning, No: the container left, nothing saved."""
    if data_state(g, symbols) != STORAGE_WARNING:
        press_until(g, "cross", lambda: data_state(g, symbols) == STORAGE_WARNING, tries=8, frames=60)
        storage_warning_wait(g, symbols)
    if not any(i == 0x1F and sel for i, _, sel in live_rows(g)):
        g.press("down", vsyncs=3, settle=8)
    g.press("circle", vsyncs=3, settle=10)
    g.until(lambda: current_menu(g, symbols) == 0x3F, guard=900, what="the container left")
    g.run(30)


STORAGE_CATEGORY = 0xA0  # D_800F4EE8 + 0xA0: MENUD's list's category + 1 (234.c: unkA0[0], unk85[0x1B])


def storage_discard_open(g, symbols):
    """Storage's Store, the bag's misc tab, its first row's Discard: the amount picker at rest (MENUD's copy)."""
    storage_store(g, symbols)
    for _ in range(8):
        if g.read(symbols["D_800F4EE8"] + STORAGE_CATEGORY, 1)[0] == 7:
            break
        g.press("r1", vsyncs=3, settle=10)
        items_ready(g, symbols)
    if g.read(symbols["D_800F4EE8"] + STORAGE_CATEGORY, 1)[0] != 7:
        raise RpcError("Storage: the misc tab never reached")
    g.run(10)
    discard_picker(g, symbols, ("circle", "circle"))  # (picked, its actions)


def storage_status_view(g, symbols, press):
    """In a Storage list at rest: Square on its row, the item's status view (refused on an empty list or misc:
    False); every item paged with R1 and back with L1 (Square after each: the class / affinity / type table's next
    page), the view's hand walked, left with Cross (to the list)."""
    rows = items_rows_now(g, symbols)
    if not any(20 <= i < 30 for i, _ in rows):
        return False
    g.press("square", vsyncs=3, settle=20)
    g.run(40)
    if items_rows_now(g, symbols) == rows:
        return False
    for button in ("r1",) * 4 + ("l1",) * 4:
        g.press(button, vsyncs=3, settle=12)
        if button == "r1":
            g.press("square", vsyncs=3, settle=12)
    for button in ("down",) * 6 + ("up",) * 6:
        g.press(button, vsyncs=3, settle=6)
    g.run(20)
    press("cross", settle=20)
    g.run(30)
    items_ready(g, symbols)
    return True


def storage_status_open(g, symbols, grip=False):
    """Storage's Store, the bag's first tab (`grip`: the first whose status view shows a grip's gem slots), Square:
    its first item's status view at rest (its stats panel, DP / PP bars or the gem slots, and table)."""
    def gems():
        return g.call("hud", action="stats")["kinds"]["gem_slots"]["drawn"]

    storage_store(g, symbols)
    for _ in range(8):  # (the tabs round until one's status view is the one wanted)
        rows = items_rows_now(g, symbols)
        if any(20 <= i < 30 for i, _ in rows):
            g.press("square", vsyncs=3, settle=20)
            g.run(40)
            if items_rows_now(g, symbols) != rows:
                items_ready(g, symbols)
                drawn = gems()
                g.run(20)
                if not grip or gems() > drawn:
                    return
                g.press("cross", vsyncs=3, settle=20)
                g.run(30)
                items_ready(g, symbols)
        g.press("r1", vsyncs=3, settle=10)
        items_ready(g, symbols)
        g.run(10)
    raise RpcError(f"Storage: no Store list's status view opened (grip {grip})")


def storage_card2_current(g, symbols):
    """RAM's settings pointed at card 2's file 2 (file_states_fixture's container, a generation ahead: the journey's
    current save there, not card 1's) and containerEmpty cleared, so the container opens with its items. Returns
    the settings' first 32 bytes as they were (put them back with storage_card2_restore)."""
    at = symbols["vs_main_settings"]
    saved = g.read(at, 32)
    g.write(at, bytes([saved[0] & ~0x10 & 0xFF]))
    g.write(at + 28, (int.from_bytes(saved[28:32], "little") + 1).to_bytes(4, "little"))
    return saved


def storage_card2_restore(g, symbols, saved):
    g.write(symbols["vs_main_settings"], saved)


def storage_remove_open(g, symbols, view=False):
    """The container with items (storage_card2_current), Storage's Remove: its first tab with rows at rest (`view`:
    Square, its first item's status view at rest). Returns the settings as they were."""
    saved = storage_card2_current(g, symbols)
    storage_open(g, symbols)
    g.press("circle", vsyncs=3, settle=20)  # (Remove: the top menu's first row)
    items_ready(g, symbols)
    for _ in range(8):
        if any(20 <= i < 30 for i, _ in items_rows_now(g, symbols)):
            break
        g.press("r1", vsyncs=3, settle=10)
        items_ready(g, symbols)
    else:
        raise RpcError("Storage: Remove: no tab with rows (the container's items not read)")
    if view:
        rows = items_rows_now(g, symbols)
        g.press("square", vsyncs=3, settle=20)
        g.run(40)
        if items_rows_now(g, symbols) == rows:
            raise RpcError("Storage: Remove: the status view did not open")
        items_ready(g, symbols)
    g.run(30)
    return saved


def storage_remove_leave(g, symbols, saved):
    """From Storage's Remove (a list, a status view): Cross to the top, Cross: MENU7's warning, No: left, nothing
    saved; the settings put back."""
    for _ in range(8):
        if current_menu(g, symbols) & 0x3F == DATA_MENU:
            break
        g.press("cross", vsyncs=3, settle=20)
        g.run(20)
    storage_warning_wait(g, symbols)
    storage_leave(g, symbols)
    storage_card2_restore(g, symbols, saved)


def tour_storage_remove(g, symbols, toured, frames):
    """Storage's Remove with items (card 2's container: storage_card2_current; the save room's state rewound to after,
    nothing saved): every tab round with R1 and L1, each list walked and its first row's status view (as Store's);
    a weapon taken out (Circle on its row, the message); Cross at the top: MENU7's warning, No."""
    def press(button, settle=8):
        g.press(button, vsyncs=3, settle=settle)
        items_ready(g, symbols)

    g.pause()
    g.mark()
    g.write(symbols["vs_main_settings"] + 1, b"\x00")
    saved = storage_card2_current(g, symbols)
    before = frames()
    storage_open(g, symbols)
    press("circle", settle=20)  # Remove
    g.run(20)
    views = 0
    for _ in range(8):
        press("r1", settle=10)
        for button in ("down",) * 6 + ("up",) * 6:
            press(button, settle=4)
        if storage_status_view(g, symbols, press):
            views += 1
    for _ in range(8):
        press("l1", settle=10)
    print(f"Storage: Remove with items toured ({views} status views)", flush=True)  # (progress)
    toured(f"Storage: Remove with items: every tab round, the lists walked, {views} status views", before,
           views=views)
    before = frames()
    for _ in range(8):  # the weapons' tab: its row taken out
        if g.read(symbols["D_800F4EE8"] + STORAGE_CATEGORY, 1)[0] == 1:
            break
        press("r1", settle=10)
    count = len([i for i, _ in items_rows_now(g, symbols) if 20 <= i < 30])
    press("circle", settle=20)
    g.run(60)
    for _ in range(4):  # (the message: Circle until the list takes input again)
        if any(20 <= i < 30 for i, _ in items_rows_now(g, symbols)) or count == 1:
            break
        press("circle", settle=20)
    g.run(30)
    toured("Storage: Remove, a weapon taken out", before)
    before = frames()
    storage_remove_leave(g, symbols, saved)
    toured("Storage: Remove: Cross at the top, the warning's No: left, nothing saved", before)
    g.rewind()


def tour_storage(g, symbols, toured, frames):
    """Storage (MENUD, currentMenu 14: the container; MENU7's prompt before it) through the original menus,
    opened as the field opens it (RPC container: the card's own container; the save room's state rewound to
    after: nothing saved): MENU7's Yes / No; MENUD's top menu walked; Remove: every tab round with R1 and L1
    (the container's lists), each walked; Store: the bag's tabs round, each list walked and its first row's status
    view (Square: every item paged with R1 / L1, the table's pages with Square, the view's hand walked, left with
    Cross), a row's actions walked and left; Cross at the top (MENUD left for MENU7's save; rewound there, nothing
    saved). The bag: the status views' fixture (status_view_fixture: broken / full blades, gem slots 0 / 3)."""
    def press(button, settle=8):
        g.press(button, vsyncs=3, settle=settle)
        items_ready(g, symbols)

    def top_to(row):
        for _ in range(4):
            if g.read(symbols["D_800F4EE8"] + STORAGE_ROW, 1)[0] == row:
                return
            press("down")
        raise RpcError(f"Storage: the top menu's row {row} never reached")

    def back():
        for _ in range(6):
            if storage_state(g, symbols) == STORAGE_TOP and ovl(g, "menud", "D_80109A70", 1) == 3:
                break
            press("cross", settle=10)
        storage_top(g, symbols)

    g.pause()
    g.mark()  # (the save room again after; nothing saved)
    g.write(symbols["vs_main_settings"] + 1, b"\x00")
    status_view_fixture(g, symbols)  # (rewound after)
    before = frames()
    storage_open(g, symbols, walk=True)
    toured("Storage opened: MENU7's Yes / No walked, MENUD's top menu", before)
    before = frames()
    for button in ("down",) * 4 + ("up",) * 4:
        press(button)
    toured("Storage: the top menu walked", before)
    for row, name in ((0, "Remove"), (1, "Store")):
        before = frames()
        top_to(row)
        press("circle", settle=20)
        g.run(20)
        views = 0
        for _ in range(8):  # every tab round: each list walked (Store: its first row's status view)
            press("r1", settle=10)
            for button in ("down",) * 4 + ("up",) * 4:
                press(button, settle=4)
            if row == 1 and storage_status_view(g, symbols, press):
                views += 1
        for _ in range(8):
            press("l1", settle=10)
        if row == 1:  # a row's actions, walked and left
            press("circle", settle=10)
            for button in ("down",) * 4 + ("up",) * 4:
                press(button, settle=4)
            press("cross", settle=10)
        back()
        print(f"Storage: {name} toured ({views} status views)", flush=True)  # (progress)
        toured(f"Storage: {name}: every tab round, the lists walked, {views} status views", before, views=views)
    before = frames()  # Store, the misc tab, a row's Discard: its amount picker walked and left
    top_to(1)
    press("circle", settle=20)
    g.run(20)
    for _ in range(8):
        if g.read(symbols["D_800F4EE8"] + STORAGE_CATEGORY, 1)[0] == 7:
            break
        press("r1", settle=10)
    discard_picker(g, symbols, ("circle", "circle"))  # (picked, its actions)
    for button in PICKER_WALK:
        press(button, settle=4)
    press("cross", settle=10)
    back()
    toured("Storage: Store, a misc row's Discard: its amount picker walked", before)
    before = frames()
    press_until(g, "cross", lambda: current_menu(g, symbols) & 0x3F == DATA_MENU, frames=60)  # MENUD left: MENU7
    storage_warning_wait(g, symbols)
    for button in ("down", "up") * 2:  # (BATTLE's hand)
        press(button, settle=8)
    toured("Storage: Cross at the top: MENU7's warning, its Yes / No walked", before)
    before = frames()
    press("circle", settle=10)  # Yes: MENU7's save list
    storage_save_list_wait(g, symbols)
    for button in ("down", "up", "down", "up"):  # the two cards (on card 1 again)
        press(button, settle=8)
    press("circle", settle=8)  # card 1's files
    g.run(90)
    for button in ("down",) * 17 + ("up",) * 17:
        press(button, settle=4)
    press_until(g, "cross", lambda: data_state(g, symbols) == STORAGE_WARNING, tries=8, frames=60)
    storage_warning_wait(g, symbols)
    toured("Storage: MENU7's save list after: the cards, card 1's files, out (the warning again)", before)
    before = frames()
    storage_leave(g, symbols)
    toured("Storage: the warning's No: left, nothing saved", before)
    g.rewind()  # (the container never saved: the menu closed, the save room)


MENUF_MENU = 15  # currentMenu while a MENUF screen is up (RPC menuf: BATTLE's func_800CB23C, after a fight)
MENUF_SCREENS = {0: "the congratulations", 1: "the time attack's start", 2: "the time attack's end",
                 4: "the cube puzzle's start", 5: "the cube puzzle's end", 6: "the cube puzzle's quit"}  # (3: none)


def menuf_state(g):
    return ovl(g, "menuf", "_screenState"), ovl(g, "menuf", "_screenTimer")


TIME_ATTACK = 0xC4  # vs_main_stateFlags: timeAttackBoss, timeAttackMins, timeAttackSecs, timeAttackHundredths
TRIALS = 0x28  # vs_main_scoredata.bossTimeTrialScores[boss][3] (card_fixtures.trial: time:24, round:7, difficulty:1)
NO_RECORD = {  # the time attack's end without a new record: (the course's three records, the time made) and where
    "none": ((0x000100, 0x000200, 0x000300), (0, 5, 0)),       # it lands (_newTimeSlot): past the three (3), or
    "second": ((0x000100, 0x010000, 0x020000), (0, 30, 0))}    # the second (1: NEW RECORD is the first's only)


def menuf_open(g, symbols, screen, boss=0, records=None):
    """MENUF's screen `screen` opened from the field (RPC menuf; boss the time attack's course), its init done (the
    pictures loaded: vs_battle_submenuStates[15] 2). records: a NO_RECORD key, the course's records and the time made
    written first (the end's ranking: _initMenu takes the time's place among them). Returns what to put back."""
    saved = (g.read(symbols["vs_main_scoredata"], 0x114),  # (the time attack's records, the scores, the time made:
             g.read(symbols["vs_main_stateFlags"] + TIME_ATTACK, 4))  # put back on leaving)
    if records:
        import card_fixtures
        times, made = NO_RECORD[records]
        g.write(symbols["vs_main_scoredata"] + TRIALS + boss * 12, b"".join(card_fixtures.trial(t) for t in times))
        g.write(symbols["vs_main_stateFlags"] + TIME_ATTACK + 1, bytes(made))
    if not g.call("menuf", submenu=screen, boss=boss).get("opened"):
        raise RpcError(f"MENUF: screen {screen} did not open")
    menu_wait(g, "MENUF's screen", lambda: current_menu(g, symbols) & 0x3F == MENUF_MENU
              and g.read(symbols["vs_battle_submenuStates"] + MENUF_MENU, 1)[0] == 2, limit=600)
    if records:
        slot = ovl(g, "menuf", "_newTimeSlot")
        if slot != {"none": 3, "second": 1}[records]:
            raise RpcError(f"MENUF: the time attack's end ({records}): the time placed {slot}")
    return saved


def menuf_until(g, what, ready, limit=3000):
    for _ in range(limit):
        if ready():
            return
        g.run(1)
    raise RpcError(f"MENUF: {what} never reached (state, timer {menuf_state(g)})")


def menuf_leave(g, symbols, saved=None):
    """Circle until the screen is left (each screen's last state takes it), the menu closed; the score data put back."""
    for _ in range(12):
        if current_menu(g, symbols) & 0x3F != MENUF_MENU:
            break
        g.press("circle", vsyncs=2, settle=20)
    menu_wait(g, "MENUF left", lambda: current_menu(g, symbols) == 0x3F, limit=900)
    if saved is not None:
        g.write(symbols["vs_main_scoredata"], saved[0])
        g.write(symbols["vs_main_stateFlags"] + TIME_ATTACK, saved[1])
    g.run(30)


def menuf_rest(g, symbols, screen, records=None, wheel=False):
    """A MENUF screen opened and played to its rest: the congratulations' bonus chosen (its state 2; wheel: the
    wheel left to stop on its own), the time attack's end's every record shown (state 1; records: menuf_open's), the
    cube puzzle's prompt in (start, end, quit)."""
    saved = menuf_open(g, symbols, screen, records=records)
    if screen == 0:
        menuf_until(g, "the PRESS BUTTON prompt in", lambda: ovl(g, "menuf", "_elementAnimationState") >= 48)
        if not wheel:
            g.press("circle", vsyncs=2, settle=2)
        menuf_until(g, "the bonus chosen", lambda: menuf_state(g)[0] == 2, limit=6000)
    elif screen == 2:
        menuf_until(g, "the records shown", lambda: menuf_state(g)[0] == 1)
    else:
        rest = {4: 78, 5: 138, 6: 63}[screen]
        menuf_until(g, "the prompt in", lambda: menuf_state(g)[1] >= rest)
    g.run(20)
    return saved


def tour_menuf(g, symbols, toured, frames):
    """MENUF (currentMenu 15) from the field (RPC menuf, as BATTLE opens it after a fight; the room's state rewound to
    after: the records and the bonus taken back): each screen played through, left with Circle, then again skipped
    with an early Circle: the congratulations (the score and the map counted in, the rank, the bonus wheel spun
    and stopped; again left to stop on its own; the fade out), the time attack's start (courses 1 to 7: the boss's
    name in one picture or two; the countdown), its end (counted in, the records, the fade out; the pulsing CLUT; again
    with no new record: the time placed past the three records, on course 1, and second, on course 4), the cube
    puzzle's start, end and
    quit (the prompt; the pulsing CLUT). The pictures' uploads (menuf_load) with each screen's opening."""
    g.pause()
    g.mark()  # (the field again after each)
    for screen, name in MENUF_SCREENS.items():
        runs = {0: ((0, None, False), (0, None, True)), 1: tuple((boss, None, False) for boss in range(7)),
                2: ((0, None, False), (0, "none", False), (3, "second", False))}.get(screen, ((0, None, False),))
        for boss, records, wheel in runs:
            g.rewind()
            before = frames()
            saved = menuf_open(g, symbols, screen, boss, records)
            if screen == 0:
                menuf_until(g, "the PRESS BUTTON prompt in", lambda: ovl(g, "menuf", "_elementAnimationState") >= 48)
                g.run(30)
                if not wheel:  # (else the wheel spins down to its own stop: past timer 2072, 30 a second)
                    g.press("circle", vsyncs=2, settle=2)
                menuf_until(g, "the bonus chosen", lambda: menuf_state(g)[0] == 2, limit=6000)
                g.run(30)
            elif screen == 1:
                menuf_until(g, "the countdown", lambda: current_menu(g, symbols) & 0x3F != MENUF_MENU, limit=900)
            elif screen == 2:
                menuf_until(g, "the records shown", lambda: menuf_state(g)[0] == 1)
                g.run(60)
            else:
                g.run(180)
            menuf_leave(g, symbols, saved)
            how = (f" (course {boss + 1})" if screen == 1 else " (the wheel left to its own stop)" if wheel
                   else f" (course {boss + 1}, no new record: the time placed {records})" if records else "")
            toured(f"MENUF: {name}{how} played through, left", before, screen=screen)
        g.rewind()
        before = frames()
        saved = menuf_open(g, symbols, screen)
        g.run(20)
        g.press("circle", vsyncs=2, settle=2)  # (skipped: to the screen's last state)
        g.run(90)
        menuf_leave(g, symbols, saved)
        toured(f"MENUF: {name} skipped with Circle, left", before, screen=screen)
        print(f"MENUF: {name} toured", flush=True)  # (progress)
    g.rewind()  # (the field, nothing taken)


def tour_remaining(g, symbols, toured, frames):
    """The remaining MAINMENU screens' tours, in the menus game after Status."""
    tour_score(g, symbols, toured, frames)
    tour_map(g, symbols, toured, frames)
    tour_data(g, symbols, toured, frames)
    tour_data_card2(g, symbols, toured, frames)
    tour_setup(g, symbols, toured, frames)
    tour_storage(g, symbols, toured, frames)
    tour_storage_remove(g, symbols, toured, frames)
    tour_loot(g, symbols, toured, frames)
    tour_menuf(g, symbols, toured, frames)


def remaining_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof):
    """Each remaining screen at rest (screen_proofs): the default and the fallback of every kind it draws there, the
    state proof from that mark. (Data's file lists first: in the save room, where SAVE is taken.)"""
    for name, opener in (("Data's SAVE, card 1's files (the file menu)", data_files_open),
                         ("Data's LOAD, card 1's files (the file menu)",
                          lambda g, symbols: data_files_open(g, symbols, DATA_LOAD)),
                         ("Data's SAVE, card 2's files (100 hours, 100% map, a temporary file, slots in use)",
                          data_card2_open),
                         ("Data's LOAD, card 2's files (100 hours, 100% map, a temporary file, slots in use)",
                          lambda g, symbols: data_card2_open(g, symbols, DATA_LOAD))):
        screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, name, opener,
                      after=lambda g: data_files_leave(g, symbols))
    for name, opener in (("Score's top menu", score_open), ("the Map", map_open), ("the Map's area list", map_list),
                         ("Data's rows", data_open), ("Setup's commands (a workshop)", workshop_setup_open),
                         ("Setup's Assemble, the blades' list (a workshop)", setup_assemble_open),
                         ("Setup's Repair, its Yes / No (a workshop)", setup_repair_open),
                         ("Setup's Gems, a weapon's gem slots and the OK (a workshop)", setup_gems_open),
                         ("Storage's top menu", storage_open), ("Storage's Store list (the tabs)", storage_store),
                         ("Storage's Store, a status view", storage_status_open),
                         ("Storage's Store, a misc row's discard amount", storage_discard_open),
                         ("Storage's Store, a grip's status view", lambda g, symbols: storage_status_open(g, symbols, True))):
        screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, name, opener)
    for name, view in (("Storage's Remove, a container with items (card 2)", False),
                       ("Storage's Remove, a container item's status view (card 2)", True)):
        settings = []
        screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, name,
                      lambda g, symbols, v=view: settings.append(storage_remove_open(g, symbols, v)),
                      after=lambda g: storage_remove_leave(g, symbols, settings[-1]))
    parts = []  # (Setup's two picks: two weapons taken apart; the bag put back after, the mark taken again)
    for name, command in (("Setup's Assemble, two picked and the OK (a workshop)", 0),
                          ("Setup's Combine, two blades picked and the OK (a workshop)", 5)):
        screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, name,
                      lambda g, symbols, c=command: parts.append(setup_two_picks_open(g, symbols, c)),
                      after=lambda g: (menu_close(g, symbols), loot_restore(g, parts[-1])))
    screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof,
                  "Setup's Assemble, the new weapon's rename grid (a workshop)",
                  lambda g, symbols: parts.append(assemble_rename_open(g, symbols)),
                  after=lambda g: (assemble_rename_leave(g, symbols), loot_restore(g, parts[-1])))
    for name, opener in (("Setup's Rename, the rename grid (a workshop)", setup_rename_open),
                         ("Setup's Rename, the rename grid's Yes / No (a workshop)", setup_rename_confirm_open)):
        screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, name, opener,
                      after=lambda g: rename_leave(g, symbols))
    for name, opener in (("MENU7's warning after Storage, its Yes / No", storage_warning_open),
                         ("MENU7's save list after Storage", storage_save_list_open)):
        screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, name, opener,
                      after=lambda g: storage_leave(g, symbols))
    bags = []  # (Organize inventory: MENUB's Items, its category icons; a weapon's and a grip's status views; the
    for name, opener in (  # loot menu left after each, its bag put back: the next proof opens from the field)
            ("Loot's Organize inventory", lambda g, symbols: loot_organize_open(g, symbols)),
            ("Loot's Organize inventory, a weapon's status view", lambda g, symbols: loot_status_open(g, symbols, 0)),
            ("Loot's Organize inventory, a grip's status view", lambda g, symbols: loot_status_open(g, symbols, 2)),
            ("Loot's Organize inventory, a misc row's discard amount", loot_discard_open)):
        screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, name,
                      lambda g, symbols, o=opener: bags.append(o(g, symbols)),
                      after=lambda g: (loot_leave(g, symbols), loot_restore(g, bags[-1])))
    for screen, name, records, wheel in (
            (0, "the congratulations' bonus chosen (MENUF)", None, False),
            (0, "the congratulations' bonus, the wheel stopped on its own (MENUF)", None, True),
            (2, "the time attack's records (MENUF)", None, False),
            (2, "the time attack's records, no new record: none placed (MENUF)", "none", False),
            (2, "the time attack's records, no new record: second placed (MENUF)", "second", False),
            (5, "the cube puzzle's end (MENUF)", None, False), (6, "the cube puzzle's quit (MENUF)", None, False)):
        menuf_saved = []  # (left with Circle, the score data put back: the congratulations' bonus stays taken)
        screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, name,
                      lambda g, symbols, s=screen, r=records, w=wheel: menuf_saved.append(
                          menuf_rest(g, symbols, s, r, w)),
                      after=lambda g: menuf_leave(g, symbols, menuf_saved[-1]))
    screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, "the Loot list's leave, its Yes / No",
                  loot_leave_open, after=lambda g: loot_leave(g, symbols))
    screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, "the Loot list", loot_open)


def screen_proofs(g, symbols, check, frame_kinds, toured_kinds, state_proof, name, opener, after=None):
    """A toured screen at rest (opener: from the menu closed): the default and the fallback of every frame kind it
    draws there, the state proof (from whatever screen was up: the menu closed first, the cursor memory off).
    after(g): what the opener changed put back at the end (a fixture), the mark taken again without it."""
    g.rewind()
    menu_close(g, symbols)
    memory = g.read(symbols["vs_main_settings"] + 1, 1)
    g.write(symbols["vs_main_settings"] + 1, b"\x00")
    opener(g, symbols)
    drawn = {k: g.call("hud", action="stats")["kinds"][k]["drawn"] for k in frame_kinds}
    g.run(10)
    shown = [k for k in frame_kinds if g.call("hud", action="stats")["kinds"][k]["drawn"] > drawn[k]]
    check(bool(shown), f"{name}: drawn by rood.classic at rest ({shown})")
    g.pause()
    g.mark()
    for kind in shown:
        g.rewind()
        default_and_fallback(g, kind, check)
    toured_kinds[:] = shown
    state_proof(f"{name} at rest", shown)
    g.rewind()
    g.write(symbols["vs_main_settings"] + 1, memory)
    if after:
        after(g)
        g.pause()
        g.mark()


def run_menus(out, card, env, check, waivers, kinds):
    """MAINMENU's shared frame through the game's Option screen (MENU8 and our pages; Rename excluded), its own game
    from the save room (the original menus): the menu opened; the list walked down through every entry (the
    game's nine rows, our pages, the scroll) and back; each of the nine rows' flyouts opened, moved through, left;
    each of our pages opened and walked through to its end (the scroll arrows), Display's Scaling flyout, the
    Fullscreen mode's "Keep these settings?" (KEEP / REVERT: the test modes); the menu closed. Then the default and
    the fallback per kind, state parity from a mark as the list opens."""
    from menu_nav import top_command
    env = dict(env, ROOD_TEST_FULLSCREEN_MODES="1920x1080@60,1280x720@60",
               **({"ROOD_CARD2": str(MENUS_CARD2[0])} if MENUS_CARD2 else {}))
    g = launch_at("save_room", card=card, fast=True, log=out / "game-menus.log", extra_env=env)
    symbols = rood_rpc.symbols()
    covered = []
    try:
        def opt():
            return g.call("options")["classic"]

        def wait(screen, limit=600):
            for _ in range(limit):
                o = opt()
                if o["screen"] == screen and o["ready"]:
                    return o
                g.run(1)
            raise RpcError(f"options screen {screen}: {json.dumps(opt())[:300]}")

        def press(button, settle=2):
            g.press(button, vsyncs=2, settle=settle)

        def frames():
            k = g.call("classic")["kinds"]
            return {kind: k[kind]["frames"] for kind in MENUS}

        def toured(label, before, **extra):
            after = frames()
            covered.append({"scenario": label, **{k: after[k] - before[k] for k in MENUS if after[k] > before[k]},
                            **extra})

        def list_to(index):
            for _ in range(60):
                o = wait("list")
                if o["list_focus"] == index:
                    return o
                press("down" if o["list_focus"] < index else "up")
            raise RpcError(f"the Option list's entry {index}")

        def page_to(row_id):
            for _ in range(80):
                o = wait("page")
                if o["focus_id"] == row_id:
                    return o
                press("down")
            raise RpcError(f"row {row_id}")

        g.call("classic", parity=True, reset=True)
        before = frames()
        g.press("triangle", vsyncs=3, settle=30)
        top_command(g, symbols, OPTION_COMMAND)
        press("circle")
        o = wait("list")
        toured("the menu opened, the Option list in", before)
        entries = o["entries"]
        total = VANILLA_OPTIONS + len(entries)
        before = frames()
        for _ in range(total):  # down through every entry (the list scrolls), wrapping to the top
            press("down")
            wait("list")
        for _ in range(3):
            press("up")
            wait("list")
        toured(f"the list walked: {total} entries down and back", before, entries=total)
        for row in range(VANILLA_OPTIONS):  # each of the game's own options: its flyout
            list_to(row)
            before = frames()
            press("circle", settle=30)
            for button in ("down", "down", "up"):
                press(button, settle=6)
            press("cross", settle=4)
            wait("list")
            toured(f"option {row}'s flyout: in, moved through, out", before, row=row)
        for entry in entries:
            if not entry.startswith("rood.options/"):
                continue
            list_to(VANILLA_OPTIONS + entries.index(entry))
            before = frames()
            press("circle")
            o = wait("page")
            for _ in range(len(o["rows"]) + 1):  # to its end (the scroll arrows) and around
                press("down")
                wait("page")
            if entry == "rood.options/display":
                page_to("aspect")
                press("circle")
                wait("flyout")
                for button in ("down", "up"):
                    press(button, settle=4)
                    wait("flyout")
                press("cross")
                wait("page")
                def pick(row_id, index):
                    """Circle on the row, its flyout's choice `index`, Circle (options_confirm_smoke's way)."""
                    page_to(row_id)
                    r = next(r for r in wait("page")["rows"] if r["id"] == row_id)
                    press("circle")
                    wait("flyout")
                    for _ in range((index - r["index"]) % r["count"]):
                        press("down")
                    press("circle")
                    return r["index"]
                window = pick("window_mode", 2)  # Fullscreen (the exclusive modes' row then asks)
                wait("page")
                mode = pick("fullscreen_mode", 1)
                wait("confirm")
                press("down", settle=6)  # REVERT
                press("up", settle=6)    # KEEP
                press("cross")           # (Cross: reverted)
                wait("page")
                check(next(r for r in wait("page")["rows"] if r["id"] == "fullscreen_mode")["index"] == mode,
                      "menus: Keep these settings? toured, reverted")
                pick("window_mode", window)
                wait("page")
            press("cross")
            wait("list")
            toured(f"page {entry}: in, every row, out", before, page=entry)
        before = frames()
        press("triangle", settle=60)
        toured("the menu closed", before)
        g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F, guard=600, what="the menu closed")
        g.run(30)
        tour_items(g, symbols, toured, frames)
        g.run(30)
        tour_status_views(g, symbols, toured, frames)
        g.run(30)
        tour_magic(g, symbols, toured, frames)
        tour_arts(g, symbols, toured, frames)
        tour_abilities(g, symbols, toured, frames)
        tour_manual(g, symbols, toured, frames)
        tour_status(g, symbols, toured, frames)
        tour_remaining(g, symbols, toured, frames)
        reports = {kind: opening_report(g, check, waivers, kind, covered, set()) for kind in kinds
                   if kind in MENU_TOURED}
        for kind in kinds:
            if kind not in MENU_TOURED:
                c = g.call("classic")["kinds"][kind]
                check(c["mismatched"] == 0 and c["skipped"] == 0,
                      f"{kind} (not toured here): {c['frames']} frames compared, {c['mismatched']} mismatched")
        g.call("classic", parity=False)
        toured_kinds = [k for k in kinds if k in MENU_TOURED]

        def state_proof(label, expect):
            """§3.4 from the mark: 120 vsyncs with rood.classic drawing, then with the game drawing. (Masked too: the
            Option pages' help clock, classic_options.c's help.since: the host's seconds when a help box was set.)"""
            masks = state_masks(g) + [[a + 400, 8] for a, n, name in Symbols(rood_rpc.BINARY).rows
                                      if name == "help" and n == 408]
            g.rewind()
            drawn = {k: g.call("hud", action="stats")["kinds"][k]["drawn"] for k in toured_kinds}
            g.digest("record", ticks=120, masks=masks)
            g.until(lambda: g.digest("result")["recorded"] >= 120, guard=120, what="the digest recorded")
            drawn = {k: g.call("hud", action="stats")["kinds"][k]["drawn"] - drawn[k] for k in toured_kinds}
            g.rewind()
            g.call("settings", apply=[f"hud_classic_{k} 0" for k in toured_kinds])
            fallback = {k: g.call("hud", action="stats")["kinds"][k]["fallback"] for k in toured_kinds}
            g.digest("compare")
            g.until(lambda: g.digest("result")["ticks"] >= 120, guard=120, what="the digest compared")
            result = g.digest("result")
            fallback = {k: g.call("hud", action="stats")["kinds"][k]["fallback"] - fallback[k] for k in toured_kinds}
            g.call("settings", apply=[f"hud_classic_{k} 1" for k in toured_kinds])
            check(all(drawn[k] > 0 and fallback[k] > 0 for k in toured_kinds if k in expect) and result.get("error") == 0
                  and result.get("first_tick") == -1 and result.get("ticks") == result.get("recorded"),
                  f"state parity menus, {label}: {result.get('ticks')} of {result.get('recorded')} vsyncs the same "
                  f"(rood.classic drew {drawn}, the game {fallback}; first difference {result.get('first_tick')} "
                  f"{result.get('first_pages')})")

        # the default and the fallback, then the state proofs: the Option list at rest (the hand pulsing), and
        # Display's rows sliding in one a frame with its help (a mark just after Circle on it)
        g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F, guard=600, what="the menu closed")
        g.run(30)
        g.press("triangle", vsyncs=3, settle=30)
        top_command(g, symbols, OPTION_COMMAND)
        for _ in range(5):  # (Circle once the top level takes it)
            press("circle")
            if g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] & 0x3F == 8, guard=5):
                break
        wait("list")
        g.pause()
        g.mark()
        for kind in toured_kinds:
            if kind != "option_value" and kind not in ITEMS_ONLY:  # (no values in the list: on a page, below)
                g.rewind()
                default_and_fallback(g, kind, check)
        state_proof("the Option list at rest", ("menu_row", "menu_cursor", "menu_command"))
        g.rewind()
        list_to(VANILLA_OPTIONS + entries.index("rood.options/display"))
        press("circle", settle=0)
        g.mark()
        if "option_value" in toured_kinds:
            default_and_fallback(g, "option_value", check)
        state_proof("Display's rows coming in", [k for k in toured_kinds if k not in ITEMS_ONLY])
        # Items: the default and the fallback of every frame kind, the state proof at its list at rest (the tabs'
        # arrows bobbing, the background up)
        g.rewind()
        for _ in range(8):  # (out of Display's page, the list, the menu)
            if g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F:
                break
            g.press("cross" if g.read(symbols["vs_battle_menuState"], 1)[0] & 0x3F == 8 else "triangle", vsyncs=3,
                    settle=60)
        g.until(lambda: g.read(symbols["vs_battle_menuState"], 1)[0] == 0x3F, guard=600, what="the menu closed")
        g.run(30)
        items_open(g, symbols)
        g.press("r1", vsyncs=3, settle=10)  # (a category's list: the tab arrows)
        items_ready(g, symbols)
        g.pause()
        g.mark()
        items_kinds = [k for k in toured_kinds if k != "option_value"]
        list_kinds = [k for k in items_kinds if k not in STATUS_VIEW_PANELS + SCREEN_PANELS]
        for kind in list_kinds:
            g.rewind()
            default_and_fallback(g, kind, check)
        toured_kinds[:] = items_kinds
        state_proof("the Items list at rest", list_kinds)
        for category in (0, 2):  # (a weapon's status view: the stats, the DP / PP bars; a grip's: its gem slots)
            screen_proofs(g, symbols, check, items_kinds, toured_kinds, state_proof,
                          f"Items, the {ITEM_CATEGORIES[category]}' status view",
                          lambda g, symbols, c=category: status_view_open(g, symbols, c))
        skills_proofs(g, symbols, check, state_proof)
        screen_proofs(g, symbols, check, items_kinds, toured_kinds, state_proof,
                      "Items, a misc row's discard amount", items_discard_open)
        screen_proofs(g, symbols, check, items_kinds, toured_kinds, state_proof, "the Quick Manual list", manual_open)
        screen_proofs(g, symbols, check, items_kinds, toured_kinds, state_proof, "Status", status_open)
        screen_proofs(g, symbols, check, items_kinds, toured_kinds, state_proof, "Status, the equipment list",
                      lambda g, symbols: status_into(g, symbols, "circle", STATUS_EQUIPMENT))
        screen_proofs(g, symbols, check, items_kinds, toured_kinds, state_proof, "Status, a part's stats",
                      lambda g, symbols: status_into(g, symbols, "square", STATUS_STATS))
        screen_proofs(g, symbols, check, items_kinds, toured_kinds, state_proof, "Status, the weapon's detail",
                      status_detail_open)
        broken = []  # (Ashley's weapon broken: both bars empty, the DP one a 3-word tile in its 14 words)
        screen_proofs(g, symbols, check, items_kinds, toured_kinds, state_proof,
                      "Status, the weapon's detail (broken, the shield's PP spent)",
                      lambda g, symbols: (broken.append(status_equipment_fixture(g)), status_detail_open(g, symbols)),
                      after=lambda g: status_equipment_restore(g, broken[-1]))
        remaining_proofs(g, symbols, check, items_kinds, toured_kinds, state_proof)
        for kind, report in reports.items():
            (out / f"{kind}.json").write_text(json.dumps(report, indent=1))
            print(f"report: {out / (kind + '.json')}: {report['frames']} frames, {report['states']} states, "
                  f"{'green' if report['green'] else 'RED'}")
    finally:
        g.close()


FILE_KINDS = ("file_menu", "file_element")  # (the file menu's: the title's LOAD and the game over's, run_file_screens)


def file_state_proof(g, check, label, kinds, ticks=120):
    """§3.4 from the mark (a file menu at rest): rood.classic drawing the kinds, then the game; every vsync's state the
    same but for the packets."""
    masks = state_masks(g)
    g.rewind()
    stats = lambda: g.call("hud", action="stats")["kinds"]  # noqa: E731
    drawn = {k: stats()[k]["drawn"] for k in kinds}
    g.digest("record", ticks=ticks, masks=masks)
    g.until(lambda: g.digest("result")["recorded"] >= ticks, guard=ticks, what="the digest recorded")
    drawn = {k: stats()[k]["drawn"] - n for k, n in drawn.items()}
    g.rewind()
    g.call("settings", apply=[f"hud_classic_{k} 0" for k in kinds])
    fallback = {k: stats()[k]["fallback"] for k in kinds}
    g.digest("compare")
    g.until(lambda: g.digest("result")["ticks"] >= ticks, guard=ticks, what="the digest compared")
    result = g.digest("result")
    fallback = {k: stats()[k]["fallback"] - n for k, n in fallback.items()}
    g.call("settings", apply=[f"hud_classic_{k} 1" for k in kinds])
    check(all(drawn[k] > 0 and fallback[k] > 0 for k in kinds) and result.get("error") == 0
          and result.get("first_tick") == -1 and result.get("ticks") == result.get("recorded"),
          f"state parity, {label}: {result.get('ticks')} of {result.get('recorded')} vsyncs the same (rood.classic "
          f"drew {drawn}, the game {fallback}; first difference {result.get('first_tick')} {result.get('first_pages')})")


def file_rest_proofs(g, check, label):
    """A file menu at rest (the parity mode off): the mark; the default and the fallback of both kinds, state
    parity; back at the mark."""
    g.pause()
    g.mark()
    for kind in FILE_KINDS:
        g.rewind()
        default_and_fallback(g, kind, check)
    file_state_proof(g, check, label, FILE_KINDS)
    g.rewind()


def run_file_screens(out, card, card2, env, check, waivers):
    """The file menu outside MENU7's Data screen, its own game (card 1 and a copy of card 2's file_states fixture: read
    only, nothing saved): the title's LOAD (TITLE 22C.c's copy of the drawing: Start, Continue): the memory cards
    walked; card 1's files walked down past the last page and back; card 2's (100 hours and 100% map, a save a
    generation ahead, a temporary file, a free slot, another game's file) walked; back to the cards. Proofs at rest
    (card 1's file list): default, fallback, state parity. Then card 1's first file loaded (the progress, its
    completion: the save room). The game over's LOAD (MENU7 vs_menu7_gameOver: Ashley's HP to 0, Continue, its
    cards and files walked; its frame buffers cleared first, the 2026-10-21 fix); proofs at rest of its file list.
    The background (_drawFileMenuBg: file_menu part 2) is drawn on both."""
    work = out / "file-screens"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    second = work / "card2-files.mcd"  # (a copy of the fixture: this game's alone)
    if card2:
        shutil.copyfile(card2, second)
    g = launch_at("title", card=str(card), fast=True, log=out / "game-file-screens.log",
                  extra_env=dict(env, **({"ROOD_CARD2": str(second)} if card2 else {})))
    covered = []
    try:
        def frames():
            k = g.call("classic")["kinds"]
            return {kind: k[kind]["frames"] for kind in FILE_KINDS}

        def toured(label, before, need=FILE_KINDS):
            after = frames()
            delta = {k: after[k] - before[k] for k in FILE_KINDS}
            covered.append({"scenario": label, **delta})
            check(all(delta[k] > 0 for k in need), f"{label}: compared {delta}")

        def press(button, settle=8):
            g.press(button, vsyncs=3, settle=settle)

        def walk(buttons, settle=8):
            for b in buttons:
                press(b, settle)

        g.call("classic", parity=True, reset=True)
        # the title's LOAD
        before = frames()
        g.press("start", vsyncs=4, settle=90)
        press("circle", 90)  # Continue: the LOAD screen, its cards
        toured("the title's LOAD: in, the cards", before)
        before = frames()
        walk(("down", "up", "down", "up"))
        toured("the title's LOAD: the two cards walked", before)
        if card2:
            before = frames()
            walk(("down",))
            press("circle", 90)  # card 2's files
            walk(("down",) * 6 + ("up",) * 6, 6)
            press("cross", 60)
            walk(("up",))
            toured("the title's LOAD: card 2's files (file_states) walked, back to the cards", before)
        before = frames()
        press("circle", 90)  # card 1's files
        walk(("down",) * 17 + ("up",) * 17, 4)
        toured("the title's LOAD: card 1's files walked past the last page and back", before)
        before = frames()
        press("cross", 60)
        press("circle", 90)
        g.run(30)
        toured("the title's LOAD: Cross to the cards, card 1's files again", before)
        # proofs at rest: card 1's file list
        g.call("classic", parity=False)
        file_rest_proofs(g, check, "the title's LOAD, card 1's files at rest")
        g.call("classic", parity=True)
        before = frames()
        press("circle", 0)  # its first file: loaded
        g.until(lambda: g.state().get("zone") is not None and frames()["file_element"] > before["file_element"] + 4,
                step=2, guard=120)
        g.run(400)
        st = g.state()
        toured(f"the title's LOAD: card 1's first file loaded (the progress, its completion; zone {st.get('zone')} "
               f"room {st.get('room')})", before)
        # the game over's LOAD
        symbols = rood_rpc.symbols()
        before = frames()

        def ashley():  # (vs_battle_actors[0] -> +0x3c: her record, once the room has her)
            actor = int.from_bytes(g.read(symbols["vs_battle_actors"], PTR), "little")
            try:
                return actor and int.from_bytes(g.read(actor + 0x3C, 4), "little")
            except RpcError:
                return 0
        g.until(ashley, step=10, guard=600, what="Ashley in the room")
        g.run(60)
        g.write(ashley() + 0x18, (0).to_bytes(2, "little"))  # (her HP: the game's own Game Over follows)
        g.until(lambda: g.state()["game_over"], step=10, guard=200, what="Game Over")
        g.run(240)  # (the Game Over screen fades in)
        for _ in range(6):  # Continue (Circle once the screen takes it)
            press("circle", 30)
            if g.until(lambda: frames()["file_menu"] > before["file_menu"], step=2, guard=30):
                break
        g.run(90)
        toured("the game over's LOAD: Continue, the cards", before)
        before = frames()
        walk(("down", "up"))
        if card2:
            walk(("down",))
            press("circle", 90)
            walk(("down",) * 6 + ("up",) * 6, 6)
            press("cross", 60)
            walk(("up",))
        press("circle", 90)  # card 1's files
        walk(("down",) * 17 + ("up",) * 17, 4)
        g.run(30)
        toured("the game over's LOAD: the cards, card 2's files and card 1's walked", before)
        reports = {kind: opening_report(g, check, waivers, kind, covered, set()) for kind in FILE_KINDS}
        g.call("classic", parity=False)
        file_rest_proofs(g, check, "the game over's LOAD, card 1's files at rest")
        for kind, report in reports.items():
            path = out / f"{kind}-file-screens.json"
            path.write_text(json.dumps(report, indent=1))
            print(f"report: {path}: {report['frames']} frames, {report['states']} states, "
                  f"{'green' if report['green'] else 'RED'}")
    finally:
        g.close()


def run_ending(out, card, env, check, waivers):
    """ENDING's copy of MENUF's leaves (src/renderTextures.h: the MISSION COMPLETE screen's words, score, map completion
    and rank; D4.c's range around them, menuf's screen 16), its own game from the save room (a card copy; nothing
    saved): RPC ending, the credits skipped with Start; the screen coming in (every frame compared), at rest (proofs:
    default, fallback, state parity); left with Cross (its fade-out); then the title's clear-data save screen (TITLE
    22C.c's file menu: the cards walked, card 1's save list walked, Cross: "Save now?", NO) to the title menu."""
    from ending_smoke import CREDITS_DONE, RANK_SCREEN_STEP
    g = launch_at("save_room", card=str(card), fast=True, log=out / "game-ending.log", extra_env=env)
    covered = []
    kinds = ("menuf",) + FILE_KINDS
    try:
        def frames():
            k = g.call("classic")["kinds"]
            return {kind: k[kind]["frames"] for kind in kinds}

        def toured(label, before, need):
            after = frames()
            delta = {k: after[k] - before[k] for k in kinds}
            covered.append({"scenario": label, **delta})
            check(all(delta[k] > 0 for k in need), f"{label}: compared {delta}")

        def press(button, settle=8):
            g.press(button, vsyncs=3, settle=settle)

        def step():
            return int.from_bytes(g.read(RANK_SCREEN_STEP, 4), "little")

        g.call("classic", parity=True, reset=True)
        check(g.call("ending")["requested"], "the ending: requested from the field")
        g.run(1200)
        g.press("start", vsyncs=4, settle=200)
        check(g.read(CREDITS_DONE, 1) == b"\x01", "the ending: Start skips the credits")
        before = frames()
        g.until(lambda: step() > 0x120, step=30, guard=1800, what="the MISSION COMPLETE screen")
        g.run(60)
        toured(f"the ending's MISSION COMPLETE: in (step {step():#x})", before, ("menuf",))
        g.call("classic", parity=False)
        g.pause()
        g.mark()
        default_and_fallback(g, "menuf", check)
        file_state_proof(g, check, "the ending's MISSION COMPLETE at rest", ("menuf",))
        g.rewind()
        g.call("classic", parity=True)
        before = frames()
        press("cross", 240)  # leave it (its fade-out): the clear-data save screen, its cards
        toured("the ending's MISSION COMPLETE: left (the fade-out)", before, ("menuf",))
        before = frames()
        for b in ("down", "up"):
            press(b)
        press("circle", 90)  # card 1's save list
        for b in ("down",) * 6 + ("up",) * 6:
            press(b, 4)
        press("cross", 60)
        toured("the title's clear-data save: the cards, card 1's save list walked, back", before, FILE_KINDS)
        before = frames()
        press("cross", 60)  # "Game is not saved. Save now?"
        press("down", 30)   # NO
        toured("the title's clear-data save: Cross, Save now?, NO", before, FILE_KINDS)
        reports = {kind: opening_report(g, check, waivers, kind, covered, set()) for kind in kinds}
        press("circle", 60)
        g.call("classic", parity=False)
        for kind, report in reports.items():
            path = out / f"{kind}-ending.json"
            path.write_text(json.dumps(report, indent=1))
            print(f"report: {path}: {report['frames']} frames, {report['states']} states, "
                  f"{'green' if report['green'] else 'RED'}")
    finally:
        g.close()


def opening_report(g, check, waivers, kind, covered, extra):
    c = g.call("classic")["kinds"][kind]
    declared = sorted(n for n in str(repl(g, "rood.classic", f"return table.concat(M.look.overrides.{kind} or {{}}, ',')"))
                      .strip('"').split(",") if n)
    waived = waivers.get(kind, {})
    allowed = {f for w in waived.values() for f in w.get("fields", [])}
    unwaived = {f: n for f, n in c["fields"].items() if f not in allowed}
    check(c["frames"] > 0 and c["skipped"] == 0,
          f"{kind}: {c['frames']} frames compared, {c['skipped']} skipped ({c['skip'] or 'none'})")
    check(not unwaived, f"{kind}: {c['matched']} matched, {c['mismatched']} mismatched; unwaived fields "
                        f"{unwaived or 'none'}" + (f"; first {json.dumps(c.get('first'))}" if unwaived else ""))
    check(sorted(waived) == declared, f"{kind}: overrides declared {declared} = waived {sorted(waived)}")
    return {"kind": kind, "frames": c["frames"], "matched": c["matched"], "mismatched": c["mismatched"],
            "skipped": c["skipped"], "fields": c["fields"], "first": c.get("first"), "waived": dict(waived),
            "declared": declared, "coverage": covered, "states": len(covered), "drawn": sorted(map(list, extra)),
            "green": c["frames"] > 0 and c["skipped"] == 0 and not unwaived}


def run_opening(out, env, check, waivers, kinds):
    """The kinds the save room does not show: the emotes (actors on screen) and the talk bubbles' frames, at the
    opening (the first_bubble checkpoint: the duke's bubble zooms in, his line waits for a button), its own game;
    the gate, the default and the fallback, state parity there."""
    # (no card: the title's cursor then rests on New Game, the checkpoint's way in)
    g = launch_at("first_bubble", card=str(out / "no-card.mcd"), fast=True, log=out / "game-opening.log",
                  extra_env=env)
    try:
        g.call("classic", parity=True, reset=True)

        def counters(kind):
            return g.call("classic")["kinds"][kind]
        g.run(200)  # (the bubble's zoom: every frame compared)
        tours = {}
        if "emote" in kinds:
            tours["emote"] = tour_emote(g, check, counters)
        if "text_frame" in kinds:
            check(counters("text_frame")["frames"] >= 20, f"text_frame: the bubble's zoom compared "
                                                          f"({counters('text_frame')['frames']} frames)")
            tours["text_frame"] = tour_text_frame(g, check, counters)
        if "box_text" in kinds:
            tours["box_text"] = tour_box_text(g, check, counters)
        reports = {kind: opening_report(g, check, waivers, kind, *tours[kind]) for kind in tours}
        g.call("classic", parity=False)
        symbols = rood_rpc.symbols()
        for kind in kinds:
            if kind == "emote":
                keep_emotes(g)
            default_and_fallback(g, kind, check)
        if "text_frame" in kinds:  # the vector talk bubbles (an option) redraw rood.classic's copies as the game's
            g.call("graphics", ui_bubbles="vector")
            b0 = g.call("ui_bubbles")
            g.run(20)
            b1 = g.call("ui_bubbles")
            g.call("graphics", ui_bubbles="original")
            drawn = g.call("hud", action="stats")["kinds"]["text_frame"]
            check(b1["verified"] >= b0["verified"] + 8 and b1["mismatched"] == b0["mismatched"] and drawn["honoured"],
                  f"text_frame: ui_bubbles vector over rood.classic's frames: {b1['verified'] - b0['verified']} "
                  f"verified, {b1['mismatched'] - b0['mismatched']} mismatched")
        # state parity (§3.4) here: from one mark, rood.classic drawing, then the game
        pointer = 8 if pathlib.Path(rood_rpc.BINARY).read_bytes()[4] == 2 else 4
        masks = [[0x1F800000, 4]]
        for name, size in (("D_8005E0C0", 0x20000), ("D_80055C80", 0x822 * 4)):
            for i in range(2):
                address = int.from_bytes(g.read(symbols[name] + pointer * i, pointer), "little")
                masks.append([address, size])
        masks += [[a, n] for a, n, name in Symbols(rood_rpc.BINARY).rows if name == "last_vsync" and n == 2 * pointer]
        masks += packet_masks(symbols, pointer)
        runs = ([("emote", k) for k in (1, 4, 5) if "emote" in kinds] + [("text_frame", 0)] * ("text_frame" in kinds)
                + [("box_text", 0)] * ("box_text" in kinds))
        for kind, emote in runs:
            g.write(symbols["D_800F2270"], bytes(2))
            g.write(symbols["D_800F2260"], bytes([0] + [emote] * 15))
            g.run(4)
            g.pause()
            g.mark()
            g.rewind()
            drawn = g.call("hud", action="stats")["kinds"][kind]["drawn"]
            g.digest("record", ticks=120, masks=masks)
            g.until(lambda: g.digest("result")["recorded"] >= 120, guard=120, what="the digest recorded")
            drawn = g.call("hud", action="stats")["kinds"][kind]["drawn"] - drawn
            g.rewind()
            g.call("settings", apply=f"hud_classic_{kind} 0")
            fallback = g.call("hud", action="stats")["kinds"][kind]["fallback"]
            g.digest("compare")
            g.until(lambda: g.digest("result")["ticks"] >= 120, guard=120, what="the digest compared")
            result = g.digest("result")
            fallback = g.call("hud", action="stats")["kinds"][kind]["fallback"] - fallback
            g.call("settings", apply=f"hud_classic_{kind} 1")
            check(drawn > 0 and fallback > 0 and result.get("error") == 0 and result.get("first_tick") == -1
                  and result.get("ticks") == result.get("recorded"),
                  f"state parity {kind} {emote}: {result.get('ticks')} of {result.get('recorded')} vsyncs the same "
                  f"(rood.classic drew {drawn}, the game {fallback}; first difference {result.get('first_tick')} "
                  f"{result.get('first_pages')})")
        for kind, report in reports.items():
            (out / f"{kind}.json").write_text(json.dumps(report, indent=1))
            print(f"report: {out / (kind + '.json')}: {report['frames']} frames, {report['states']} states, "
                  f"{'green' if report['green'] else 'RED'}")
    finally:
        g.close()


MENUS_CARD2 = []  # the menus game's card 2 (a copy in the card guard's directory: the Data tour saves to it)


def card2_fixture(out, card):
    """Card 2 for the menus game (card_fixtures.file_states_fixture, a copy the Data tour saves to): MENU7's file
    panels' other states, and a save holding a container built from the save room's bag (read from a game of its
    own: Storage's Remove with items)."""
    import card_fixtures
    fixtures = out / "fixtures"
    fixtures.mkdir(parents=True, exist_ok=True)
    plugins = fixtures / "plugins"
    shutil.rmtree(plugins, ignore_errors=True)
    plugins.mkdir()
    settings = fixtures / "settings.cfg"
    settings.write_text("ui_menus 0\n")
    with card_guard(fixtures / "cards", source=card) as cards:
        g = launch_at("save_room", card=cards.card1, fast=True, log=out / "game-card2.log",
                      extra_env={"ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(plugins),
                                 "ROOD_PLUGIN_DATA": str(fixtures / "plugin_data"), "ROOD_DETERMINISTIC": "1"})
        try:
            bag = g.read(rood_rpc.symbols()["vs_main_inventory"], INVENTORY_BYTES)
        finally:
            g.close()
    path = fixtures / "file_states.mcd"
    made = card_fixtures.file_states_fixture(card, path, bag, card_fixtures.container_layout(out=fixtures))
    print(f"card 2: {path} (the container: {made})", flush=True)
    return path


def run(out, card, renderer, kinds, candidate=False):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    plugins = out / "plugins"  # (no player plugins: only the shipped ones)
    shutil.rmtree(plugins, ignore_errors=True)
    plugins.mkdir(parents=True)
    shutil.rmtree(out / "plugin_data", ignore_errors=True)
    settings = out / "settings.cfg"
    settings.write_text("ui_menus 0\nui_text font\naspect 16:9\ninterpolate 0\n")
    env = {"ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(plugins), "ROOD_PLUGIN_DATA": str(out / "plugin_data"),
           "ROOD_PLUGINS_SHIPPED": str(ROOT / "pc/plugins"), "ROOD_CLASSIC_PARITY": "1", "ROOD_DETERMINISTIC": "1"}
    if renderer == "null":
        env.update({"ROOD_RENDERER": "null", "SDL_VIDEO_DRIVER": "invalid-null-classic-parity"})
        # (the run's save states are made for its own renderer: boot to the checkpoint instead)
        if os.environ.get("ROOD_RENDERER") != "null":
            os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
    elif renderer == "vulkan":
        env["ROOD_RENDERER"] = "vulkan"
    waivers = tomllib.loads((ROOT / "pc/tests/classic/waivers.toml").read_text())
    if "image" in kinds:
        run_new_game(out, env, check, waivers, ["image"])
        kinds = [k for k in kinds if k != "image"]
        if not kinds:
            print(f"{'PASS' if not failures else 'FAIL'}: classic parity ({len(failures)} failures)")
            return 1 if failures else 0
    if "target_marker" in kinds:
        run_marker(out, card, env, check, waivers)
        kinds = [k for k in kinds if k != "target_marker"]
        if not kinds:
            print(f"{'PASS' if not failures else 'FAIL'}: classic parity ({len(failures)} failures)")
            return 1 if failures else 0
    if any(k in FILE_KINDS for k in kinds):
        run_file_screens(out, card, MENUS_CARD2[0] if MENUS_CARD2 else None, env, check, waivers)
    if "menuf" in kinds:
        run_ending(out, card, env, check, waivers)
    if any(k in MENUS for k in kinds):
        run_menus(out, card, env, check, waivers, [k for k in kinds if k in MENUS])
        kinds = [k for k in kinds if k not in MENUS]
        if not kinds:
            print(f"{'PASS' if not failures else 'FAIL'}: classic parity ({len(failures)} failures)")
            return 1 if failures else 0
    if any(k in OPENING for k in kinds):
        run_opening(out, env, check, waivers, [k for k in kinds if k in OPENING])
        kinds = [k for k in kinds if k not in OPENING]
        if not kinds:
            print(f"{'PASS' if not failures else 'FAIL'}: classic parity ({len(failures)} failures)")
            return 1 if failures else 0
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        plugin = g.lua_plugins().get("rood.classic")
        if not check(plugin and plugin.get("state") == "loaded", f"rood.classic loaded ({plugin and plugin.get('state')})"):
            return 1
        if candidate:
            g.call("settings", apply="hud_classic_ability_icons 1")
        g.call("classic", parity=True, reset=True)
        check(g.call("classic")["parity"], "the parity mode is on")

        def counters(kind):
            return g.call("classic")["kinds"][kind]

        waivers = tomllib.loads((ROOT / "pc/tests/classic/waivers.toml").read_text())
        declared = {}  # M.look.overrides as "kind=a,b;kind=..."
        for item in str(repl(g, "rood.classic", "local t = {} for k, v in pairs(M.look.overrides) do "
                                                  "t[#t + 1] = k .. '=' .. table.concat(v, ',') end "
                                                  "return table.concat(t, ';')")).strip('"').split(";"):
            if "=" in item:
                key, names = item.split("=", 1)
                declared[key] = [n for n in names.split(",") if n]
        reports = {}
        for kind in kinds:
            covered = []
            ages = set()
            if kind == "gauges":
                covered = tour_gauges(g, check, counters)
            elif kind == "figure":
                covered = tour_figure(g, check, counters)
            elif kind == "status_icons":
                covered = tour_status_icons(g, check, counters)
            elif kind == "battle_mode":
                covered = tour_battle_mode(g, check, counters)
            elif kind == "ability_icons":
                covered = tour_ability_icons(g, check, counters)
            elif kind == "buff":
                covered = tour_buff(g, check, counters)
            elif kind == "actor_word":
                covered = tour_actor_word(g, check, counters)
            elif kind == "timer":
                covered = tour_timer(g, check, counters)
            elif kind == "text_band":
                covered = tour_text_band(g, check, counters)
            elif kind == "target_row":
                covered = tour_target_row(g, check, counters)
            elif kind == "target_line":
                covered = tour_target_line(g, check, counters)
            for show, text in states(kind):
                # held (rood.dev.show: the game's own spawner, the age pinned) until the gate compared two of
                # its frames showing this element's text
                g.call("hud", action="show", kind=kind, hold=True, age=10, **show)
                before = counters(kind)["frames"]
                ok = g.until(lambda: (lambda c: c["frames"] >= before + 2 and c["text"] == text)(counters(kind)),
                             guard=60)
                if not check(bool(ok), f"{kind} {text}, held: compared ({counters(kind)})"):
                    continue
                # then its whole life played out (slide in, held, slide out): every frame compared
                g.call("hud", action="clear")
                g.call("hud", action="show", kind=kind, hold=False, **show)
                seen, before = set(), counters(kind)["frames"]

                def gone():
                    live = [e for e in g.call("hud", action="list")["elements"]
                            if e["kind"] == kind and e["text"] == text and e["live"] and not e["synthetic"]]
                    seen.update(e["age"] for e in live)
                    return not live and seen
                ok = g.until(gone, step=1, guard=60)
                frames = counters(kind)["frames"] - before
                check(bool(ok) and frames >= len(seen), f"{kind} {text}, its life: {frames} frames compared, ages "
                                                      f"{min(seen) if seen else '-'}..{max(seen) if seen else '-'}")
                ages |= seen
                covered.append({**show, "text": text, "ages": sorted(seen)})
            g.call("hud", action="clear")
            slides = SLIDES.get(kind, set())
            check(slides <= ages, f"{kind}: its slides covered" + (f" (ages {sorted(slides - ages)} missing)"
                                                                     if not slides <= ages else ""))
            c = counters(kind)
            waived = waivers.get(kind, {})
            allowed = {f for w in waived.values() for f in w.get("fields", [])}
            unwaived = {f: n for f, n in c["fields"].items() if f not in allowed}
            check(c["frames"] > 0 and c["skipped"] == 0,
                  f"{kind}: {c['frames']} frames compared, {c['skipped']} skipped ({c['skip'] or 'none'})")
            check(not unwaived, f"{kind}: {c['matched']} matched, {c['mismatched']} mismatched; unwaived fields "
                                f"{unwaived or 'none'}" + (f"; first {json.dumps(c.get('first'))}" if unwaived else ""))
            names = sorted((declared or {}).get(kind) or [])
            check(names == sorted(waived), f"{kind}: overrides declared {names} = waived {sorted(waived)}")
            reports[kind] = {"kind": kind, "frames": c["frames"], "matched": c["matched"],
                             "mismatched": c["mismatched"], "skipped": c["skipped"], "fields": c["fields"],
                             "first": c.get("first"), "waived": {k: v for k, v in waived.items()},
                             "declared": names, "coverage": covered, "states": len(covered),
                             "green": c["frames"] > 0 and c["skipped"] == 0 and not unwaived}
            if kind == "ability_icons":
                reports[kind]["capture_unit"] = "group (nine per display frame)"
                reports[kind]["display_frames"] = c["frames"] // 9
        for kind in OPENING:  # (the save room shows some too: the name window's text; each compared here as well)
            c = counters(kind)
            check(c["mismatched"] == 0 and c["skipped"] == 0,
                  f"{kind} in the save room: {c['frames']} frames compared, {c['mismatched']} mismatched, "
                  f"{c['skipped']} skipped")
        g.call("classic", parity=False)
        check(not g.call("classic")["parity"], "the parity mode is off")

        ability_icons_fixture(g)
        battle_mode_fixture(g)  # active phase for fallback and both same-mark state proofs
        status_icons_fixture(g)  # an active carousel for fallback and the same-mark state proof
        timer_at = rood_rpc.symbols()["vs_battle_lowerScreenUiState"]
        timer_shown = g.read(timer_at, 1)
        timer_fixture(g, 12, 34, 56)  # the timer up for fallback and the state proofs
        for kind in kinds:
            default_and_fallback(g, kind, check)
        if any(k in TARGETING for k in kinds):
            target_state_parity(g, check)
        check(targeting(g, False), "targeting left")
        ability_icons_fixture(g)  # (again: leaving battle mode ended them)
        battle_mode_fixture(g)
        status_icons_fixture(g)
        state_parity(g, check)
        # The doubled tick path and supplemental-glyph carousel get the same state proof too.
        speed_at = rood_rpc.symbols()["vs_gametime_tickspeed"]
        old_speed = g.read(speed_at, 4)
        inventory_at, old_inventory, assigned = bind_test_weapon_slots(g)
        check(assigned > 0, "state parity weapon-slot names fixture occupied")
        try:
            g.write(speed_at, (4).to_bytes(4, "little"))
            status_icons_fixture(g, 0x1FE00000, phase=12, previous=16, current=17, next_icon=18)
            ability_icons_fixture(g, weapon_mode=True)
            state_parity(g, check)
            ability_icons_fixture(g, weapon_mode=True)
            state_parity(g, check, ability_hold=False)  # actual animation out, including C weapon-slot side effects
        finally:
            g.write(speed_at, old_speed)
            g.write(inventory_at, old_inventory)
            g.write(timer_at, timer_shown)
        report_dir = out
        for kind, report in reports.items():
            (report_dir / f"{kind}.json").write_text(json.dumps(report, indent=1))
            measured = (f"{report['frames']} hook comparisons / {report['display_frames']} display frames"
                        if kind == "ability_icons" else f"{report['frames']} frames")
            print(f"report: {report_dir / (kind + '.json')}: {measured}, {report['states']} states, "
                  f"{'green' if report['green'] else 'RED'}")
    finally:
        g.close()
    print(f"{'PASS' if not failures else 'FAIL'}: classic parity ({len(failures)} failures)")
    return 1 if failures else 0


def repl(g, plugin, code):
    """A value from the plugin's sandbox (its module is M), as JSON."""
    reply = g.repl(op="eval", plugin=plugin, code=code)
    values = reply.get("values") or reply.get("results") or []
    if not reply.get("ok", True) or not values:
        raise RpcError(f"repl {code}: {reply}")
    value = values[0]
    return value.get("value", value) if isinstance(value, dict) and "value" in value else value


SHOW = {"status": {"timing": "slow", "age": 10}, "chain": {"count": 3, "age": 40},
        "buff": {"slot": "WEAPON", "stat": "HUMAN", "value": 1, "sign": 1, "age": 10},
        "damage": {"pool": "hp", "sign": -1, "value": 57, "age": 10}}


def default_and_fallback(g, kind, check):
    """The parity mode off: rood.classic draws the kind by default; with its setting off, a draw that
    raises, or the plugin off, the game's own drawing each time."""
    def stats():
        if kind in KEEP:
            KEEP[kind](g)
        return g.call("hud", action="stats")["kinds"][kind]

    if kind in SHOW:  # (the gauges are up whenever the field is)
        g.call("hud", action="show", kind=kind, hold=True, **SHOW[kind])
    s0 = stats()
    drawn = g.until(lambda: (lambda s: s["drawn"] >= s0["drawn"] + 2 and s["claimed_by"] == "rood.classic"
                             and s["honoured"])(stats()), guard=60)
    s = stats()
    check(bool(drawn) and s["suppressed"] > s0["suppressed"],
          f"{kind}: drawn by rood.classic by default (honoured, suppressed {s['suppressed'] - s0['suppressed']}, "
          f"drawn {s['drawn'] - s0['drawn']})")

    def game_draws(why):
        s1 = stats()
        ok = g.until(lambda: (lambda s: s["fallback"] >= s1["fallback"] + 2 and not s["honoured"])(stats()), guard=60)
        s2 = stats()
        check(bool(ok) and s2["drawn"] == s1["drawn"],
              f"{kind}, {why}: the game draws it (fallback {s2['fallback'] - s1['fallback']}, Lua draws "
              f"{s2['drawn'] - s1['drawn']})")

    def lua_draws(why):
        s1 = stats()
        ok = g.until(lambda: (lambda s: s["drawn"] >= s1["drawn"] + 2 and s["honoured"])(stats()), guard=60)
        check(bool(ok), f"{kind}, {why}: rood.classic draws it again")

    g.call("settings", apply=f"hud_classic_{kind} 0")
    game_draws(f"hud_classic_{kind} 0")
    g.call("settings", apply=f"hud_classic_{kind} 1")
    lua_draws(f"hud_classic_{kind} 1")
    repl(g, "rood.classic", f"M.draw.{kind} = function() error('a broken classic draw') end return true")
    faults = stats()["faults"]
    game_draws("a draw that raises")
    check(stats()["faults"] > faults and stats()["claim_failed"], f"{kind}: the fault counted, the claim failed")
    g.call("lua", action="reload", plugin="rood.classic")
    lua_draws("the plugin reloaded")
    g.call("lua", action="enable", plugin="rood.classic", enabled=False)
    game_draws("rood.classic off")
    g.call("lua", action="enable", plugin="rood.classic", enabled=True)
    lua_draws("rood.classic on")
    g.call("hud", action="clear")


POPUPS = ("buff", "damage", "actor_word", "timer", "text_band")


def packet_masks(symbols, pointer):
    """More of what the proof leaves out as packets, beside the primitive buffers and the ordering table: the
    text boxes' private ordering tables (D_800F4CD0: three of 34 links, spliced into the frame's) and the room
    drawing's copies of the packet pointer (D_800F1CC0, D_800F1BB8, 146C.c). A classic draw allocates its
    packets after the game's no-ops, so later packets, and the links to them, sit further on."""
    masks = [[symbols["D_800F4CD0"], 3 * 34 * 4]]
    rows = {name: a for a, n, name in Symbols(rood_rpc.BINARY).rows if name in ("D_800F1CC0", "D_800F1BB8")}
    return masks + [[a, pointer] for a in rows.values()]


def state_masks(g):
    """What a state proof leaves out: the packet pointer, the frame's primitive buffers and ordering tables, the host
    clock, packet_masks'."""
    symbols = rood_rpc.symbols()
    pointer = 8 if pathlib.Path(rood_rpc.BINARY).read_bytes()[4] == 2 else 4  # (ELF class: the arrays' pointers)
    masks = [[0x1F800000, 4]]  # vs_scratch.unk0, the packet pointer
    for name, size in (("D_8005E0C0", 0x20000), ("D_80055C80", 0x822 * 4)):  # the frame's two of each
        for i in range(2):
            address = int.from_bytes(g.read(symbols[name] + pointer * i, pointer), "little")
            masks.append([address, size])
    masks += [[a, n] for a, n, name in Symbols(rood_rpc.BINARY).rows if name == "last_vsync" and n == 2 * pointer]
    return masks + packet_masks(symbols, pointer)


TARGETING = ("target_row", "target_line")  # (the kinds whose state proof runs with targeting up)


def target_state_parity(g, check, ticks=120):
    """§3.4 for targeting: from one mark in targeting, rows coming in from both sides (their plates fading) and a
    selected one with its leader lines, drawn by rood.classic, then by the game: every vsync's state the same but for the packets."""
    masks = state_masks(g)
    if not check(targeting(g), "targeting state parity: targeting up"):
        return
    names = limb_names(g)
    clear_rows(g)
    write_row(g, 0, state=2, x=320, y=32, text=names[1][0])
    write_row(g, 1, state=3, x=-72, y=43, bank=1, text=names[2][0])
    write_row(g, 2, selected=1, age=20, y=54, text=names[3][0])  # (its leader lines, both)
    g.run(2)
    g.pause()
    g.mark()
    g.rewind()
    stats = lambda: g.call("hud", action="stats")["kinds"]  # noqa: E731
    drawn = {k: stats()[k]["drawn"] for k in TARGETING}
    g.call("input", buttons=[], vsyncs=ticks + 20)
    g.digest("record", ticks=ticks, masks=masks)
    g.until(lambda: g.digest("result")["recorded"] >= ticks, guard=120, what="the digest recorded")
    drawn = {k: stats()[k]["drawn"] - n for k, n in drawn.items()}
    g.rewind()
    g.call("settings", apply=[f"hud_classic_{k} 0" for k in TARGETING])
    fallback = {k: stats()[k]["fallback"] for k in TARGETING}
    g.call("input", buttons=[], vsyncs=ticks + 20)
    g.digest("compare")
    g.until(lambda: g.digest("result")["ticks"] >= ticks, guard=120, what="the digest compared")
    result = g.digest("result")
    fallback = {k: stats()[k]["fallback"] - n for k, n in fallback.items()}
    g.call("settings", apply=[f"hud_classic_{k} 1" for k in TARGETING])
    for k in TARGETING:
        check(drawn[k] > 0 and fallback[k] > 0, f"targeting state parity {k}: Lua drew {drawn[k]}, game drew "
                                                f"{fallback[k]}")
    check(result.get("error") == 0 and result.get("first_tick") == -1 and result.get("ticks") == result.get("recorded"),
          f"targeting state parity: {result.get('ticks')} of {result.get('recorded')} vsyncs the same (first difference "
          f"{result.get('first_tick')} {result.get('first_pages')})")
    clear_rows(g)


def state_parity(g, check, ticks=120, ability_hold=True):
    """§3.4: SLOW! held (and the gauges up), from one mark: rood.classic drawing them, then the game; every
    vsync's state
    (guest memory, the native game state, VRAM) the same but for the packets (the primitive buffers,
    the ordering tables, the packet pointer) and the host clock (last_vsync, as snapshot_diff and the
    oracle's own proof mask it). Both runs start from a rewind (the mark's)."""
    masks = state_masks(g)
    g.call("hud", action="show", kind="status", hold=True, timing="slow", age=10)
    for kind in ("buff", "damage"):  # (held beside it: both drawn by rood.classic, then by the game)
        g.call("hud", action="show", kind=kind, hold=True, **SHOW[kind])
    spawn_word(g, index=6, flags=1)  # (a lowered stat's word playing out from the mark: in both runs)
    g.call("hud", action="show", kind="name_window", hold=True)  # (its band, held)
    start = g.call("hud", action="stats")["kinds"]["status"]["drawn"]
    g.until(lambda: g.call("hud", action="stats")["kinds"]["status"]["drawn"] >= start + 2, guard=60,
            what="SLOW! drawn by rood.classic")
    g.pause()
    g.mark()
    g.rewind()
    g.call("input", buttons=["l2", "r2"] if ability_hold else [], vsyncs=ticks + 20)
    g.digest("record", ticks=ticks, masks=masks)
    lua_drawn = g.call("hud", action="stats")["kinds"]["status"]["drawn"]
    figure_drawn = g.call("hud", action="stats")["kinds"]["figure"]["drawn"]
    icons_drawn = g.call("hud", action="stats")["kinds"]["status_icons"]["drawn"]
    battle_drawn = g.call("hud", action="stats")["kinds"]["battle_mode"]["drawn"]
    ability_drawn = g.call("hud", action="stats")["kinds"]["ability_icons"]["drawn"]
    popups_drawn = {k: g.call("hud", action="stats")["kinds"][k]["drawn"] for k in POPUPS}
    g.until(lambda: g.digest("result")["recorded"] >= ticks, guard=120, what="the digest recorded")
    lua_drawn = g.call("hud", action="stats")["kinds"]["status"]["drawn"] - lua_drawn
    figure_drawn = g.call("hud", action="stats")["kinds"]["figure"]["drawn"] - figure_drawn
    icons_drawn = g.call("hud", action="stats")["kinds"]["status_icons"]["drawn"] - icons_drawn
    battle_drawn = g.call("hud", action="stats")["kinds"]["battle_mode"]["drawn"] - battle_drawn
    ability_drawn = g.call("hud", action="stats")["kinds"]["ability_icons"]["drawn"] - ability_drawn
    popups_drawn = {k: g.call("hud", action="stats")["kinds"][k]["drawn"] - n for k, n in popups_drawn.items()}
    g.rewind()
    g.call("settings", apply=["hud_classic_status 0", "hud_classic_chain 0", "hud_classic_gauges 0", "hud_classic_figure 0", "hud_classic_status_icons 0", "hud_classic_battle_mode 0", "hud_classic_ability_icons 0", "hud_classic_buff 0", "hud_classic_damage 0", "hud_classic_actor_word 0", "hud_classic_timer 0", "hud_classic_text_band 0"])
    fallback = g.call("hud", action="stats")["kinds"]["status"]["fallback"]
    figure_fallback = g.call("hud", action="stats")["kinds"]["figure"]["fallback"]
    icons_fallback = g.call("hud", action="stats")["kinds"]["status_icons"]["fallback"]
    battle_fallback = g.call("hud", action="stats")["kinds"]["battle_mode"]["fallback"]
    ability_fallback = g.call("hud", action="stats")["kinds"]["ability_icons"]["fallback"]
    popups_fallback = {k: g.call("hud", action="stats")["kinds"][k]["fallback"] for k in POPUPS}
    g.call("input", buttons=["l2", "r2"] if ability_hold else [], vsyncs=ticks + 20)
    g.digest("compare")
    g.until(lambda: g.digest("result")["ticks"] >= ticks, guard=120, what="the digest compared")
    result = g.digest("result")
    fallback = g.call("hud", action="stats")["kinds"]["status"]["fallback"] - fallback
    figure_fallback = g.call("hud", action="stats")["kinds"]["figure"]["fallback"] - figure_fallback
    icons_fallback = g.call("hud", action="stats")["kinds"]["status_icons"]["fallback"] - icons_fallback
    battle_fallback = g.call("hud", action="stats")["kinds"]["battle_mode"]["fallback"] - battle_fallback
    ability_fallback = g.call("hud", action="stats")["kinds"]["ability_icons"]["fallback"] - ability_fallback
    popups_fallback = {k: g.call("hud", action="stats")["kinds"][k]["fallback"] - n for k, n in popups_fallback.items()}
    for kind in POPUPS:
        check(popups_drawn[kind] > 0 and popups_fallback[kind] > 0,
              f"state parity {kind}: Lua drew {popups_drawn[kind]} frames, game drew {popups_fallback[kind]}")
    check(ability_drawn == ability_fallback and ability_drawn >= (ticks * 9 // 4 if ability_hold else 9),
          f"state parity ability_icons: Lua drew {ability_drawn} groups, game drew {ability_fallback}")
    check(battle_drawn > 0 and battle_fallback > 0,
          f"state parity battle_mode: Lua drew {battle_drawn} frames, game drew {battle_fallback}")
    check(icons_drawn > 0 and icons_fallback > 0,
          f"state parity status_icons: Lua drew {icons_drawn} frames, game drew {icons_fallback}")
    check(figure_drawn > 0 and figure_fallback > 0,
          f"state parity figure: Lua drew {figure_drawn} frames, game drew {figure_fallback}")
    g.call("settings", apply=["hud_classic_status 1", "hud_classic_chain 1", "hud_classic_gauges 1", "hud_classic_figure 1", "hud_classic_status_icons 1", "hud_classic_battle_mode 1", "hud_classic_ability_icons 1", "hud_classic_buff 1", "hud_classic_damage 1", "hud_classic_actor_word 1", "hud_classic_timer 1", "hud_classic_text_band 1"])
    g.call("hud", action="clear")
    check(lua_drawn > 0 and fallback > 0 and result.get("error") == 0 and result.get("first_tick") == -1
          and result.get("ticks") == result.get("recorded"),
          f"state parity: {result.get('ticks')} of {result.get('recorded')} vsyncs the same (rood.classic drew "
          f"{lua_drawn} frames, the game {fallback}; first difference {result.get('first_tick')} "
          f"{result.get('first_pages')})")


if __name__ == "__main__":
    sys.exit(main())
