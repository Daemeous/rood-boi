#!/usr/bin/env python3
"""Each effect plug-in's tables are its own data (the mover's plgNNN groups, rood_ovl_effect_plgNNN_bin_data sections),
restarted when its file is read, saved with the game's statics.

    ROOD_ARCH=64 python3 pc/tests/plugin_images_smoke.py [--binary rood_battle_probe] [--card card.mcd]

In the casts lane's room (zone 9 room 18) Ashley casts her warlock spells through the menus; each read of an effect plug-in file
into the slot (pc_plugin_slot: the disc read's destination) is looked at, and the sections of all plug-ins are watched:
1. The plug-in loaded by a read of the same file again (A, B, A, A) finds its tables as the first read did
   (restart on disc read), though the cast in between may have changed them.
2. At a save point with a plug-in loaded: a mark, then 120 vsyncs, then rewind: the sections, the game state hash
   and the picture equal the save point's, and 120 more vsyncs equal the first 120 (the marks hold the regions).
3. The same save point as a save state resumed by a second process (given the host settings the first was given by RPC: a
   save state holds the game, not the settings: without them the cast runs on differently): the same hash and sections at
   once, and the same state and picture after the same 120 vsyncs.
PLUGIN_SMOKE_LEG sets the leg's length, PLUGIN_SMOKE_DIFF a folder for the two legs' save states (snapshot_diff.py), PLUGIN_SMOKE_KEEP
one for the games' logs.
Prints the sequence of plug-ins loaded, the size of the plug-in sections and the number of restarts seen."""
import argparse
import contextlib
import hashlib
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import charge_skip_soak as soak  # noqa: E402
import skip_charging_smoke as smoke  # noqa: E402

LEG = int(os.environ.get("PLUGIN_SMOKE_LEG", "120"))
SLOT_LOOK = 0x400


def sections(binary):
    """{name: (address, size)} of the plug-ins' sections, from the binary's __start_/__stop_ symbols."""
    syms = {}
    for line in subprocess.run(["nm", str(binary)], capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2].startswith(("__start_rood_ovl_effect_plg", "__stop_rood_ovl_effect_plg")):
            syms[parts[2]] = int(parts[0], 16)
    out = {}
    for name, start in syms.items():
        m = re.fullmatch(r"__start_rood_ovl_effect_(plg\d+)_bin_data", name)
        if m:
            out[m.group(1)] = (start, syms[name.replace("__start_", "__stop_")] - start)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default=str(ROOT / "pc/build/boot64/rood_battle_probe"))
    parser.add_argument("--baseline", action="store_true", help="a build without the plug-ins' own sections (main before S9): the same drive, no section checks")
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD", ROOT / "pc/build/test-card1.mcd"))
    args = parser.parse_args()
    binary = pathlib.Path(args.binary)
    os.environ.update(ROOD_BINARY=str(binary), ROOD_SYMBOLS=str(binary), ROOD_ARCH="64", ROOD_TEST_SNAPSHOTS="0")
    rood_rpc.BINARY = binary
    secs = sections(binary)
    assert secs or args.baseline, "no rood_ovl_effect_* sections: not a build with the plug-ins' own data"
    total = sum(size for _, size in secs.values())
    print(f"plug-in sections: {len(secs)}, {total} bytes")
    files = {}
    for path in sorted((ROOT / "data/EFFECT").glob("PLG*.BIN")):
        files[path.stem.lower()] = path.read_bytes()[:SLOT_LOOK]
    lo = min((a for a, _ in secs.values()), default=0)
    hi = max((a + s for a, s in secs.values()), default=0)
    lowest = min(secs, key=lambda n: secs[n][0]) if secs else None
    keep = os.environ.get("PLUGIN_SMOKE_KEEP")  # a folder to keep the games' logs in
    with (contextlib.nullcontext(keep) if keep else tempfile.TemporaryDirectory(prefix="plugin-images-", dir=ROOT / "pc/build")) as directory:
        work = pathlib.Path(directory)
        work.mkdir(parents=True, exist_ok=True)
        card = work / "card.mcd"
        card.write_bytes(pathlib.Path(args.card).read_bytes())
        settings = work / "settings.txt"
        settings.write_text("ui_menus 0\nwidescreen 0\ninterpolate 0\nqol_skip_charging 0\ncamera_stick 0\n")
        env = {"ROOD_SETTINGS": str(settings), "ROOD_GI": "0", "ROOD_AUDIO": "none", "ROOD_OUT": str(work),
               "ROOD_LIGHTS": str(work / "lights"), "ROOD_PREFABS": str(work / "prefabs"), "ROOD_WINDOW": "960x720"}

        def span(g):
            if not secs:
                return b""
            return g.data(f"__start_rood_ovl_effect_{lowest}_bin_data", hi - lo)

        def picture(g):
            shot = work / "shot.ppm"
            g.screenshot(str(shot))
            return hashlib.sha256(shot.read_bytes()).hexdigest()[:16]

        # The room of the casts lane (zone 9 room 18, an enemy waits within Solid Shock's range): a save state, then one menu cast so the
        # action driver has a captured state, then the enemies' abilities driven through the game's own entry.
        snap0 = work / "room.snap"
        g = launch_at("save_room", card=str(card), fast=True, log=work / "boot.log", extra_env=env)
        try:
            g.call("room_goto", hold=True, zone=smoke.ROOM[0], room=smoke.ROOM[1], enemies=True, scripts=False)
            for _ in range(80):
                g.run(20)
                if g.call("room_goto", action="status")["phase"] in (0, 4):
                    break
            g.run(smoke.APPROACH)
            g.call("snapshot", action="save", path=str(snap0))
        finally:
            g.close()
        r = soak.Soak("plugins", snap0, card, env, work, f"/tmp/rood-plugins-{os.getpid()}.sock", fast=True)
        g = r.g
        slot_head = lambda: g.data("pc_plugin_slot", SLOT_LOOK)  # (the disc read's destination)
        loaded_names = lambda: next((n for n, b in files.items() if b == slot_head()), None)
        try:
            g.call("ui_menu", modern=True)
            g.call("skip_charging", mode="skip", who="all", name=smoke.NAME_SECONDS)
            g.store("vs_main_settings.information", 1)
            g.store("vs_main_settings.menuFlags", g.value("vs_main_settings.menuFlags") | 1)
            for name, size in soak.SCHOOLS:
                for i in g.data(f"vs_battle_{name}SpellIds", size):
                    g.store(f"vs_main_actions[{i}].unlocked", 1)
            g.run(4)
            slot_host = next(l.split()[0] for l in subprocess.run(["nm", str(binary)], capture_output=True, text=True).stdout.splitlines()
                             if l.endswith(" pc_plugin_slot"))
            logfile = pathlib.Path(g.log)
            reads = lambda: logfile.read_text(errors="replace").count(f"-> 0x{slot_host.lstrip('0')}\n")
            order, first, last_end, restarts, mutated = [], {}, {}, 0, 0
            where = {}  # spell id -> the plug-in it loaded

            def region_of(name):
                return g.data(f"__start_rood_ovl_effect_{name}_bin_data", secs[name][1])

            def to_row(row):
                r.refill()
                r.shell("magick")
                for _ in range(6):
                    if r.ui()["magick"].get("sub") == "warlock":
                        break
                    g.press("r2", vsyncs=3, settle=4)
                for _ in range(24):
                    c = r.ui()["magick"]["cursor"]
                    if c == row:
                        break
                    g.press("down" if c < row else "up", vsyncs=3, settle=4)

            def cast(row, spell_id, stop_at_load=False, corrupt=False):
                """Ashley casts the warlock spell in `row` (the menu, as the casts lane does). Each read of a plug-in file (a disc line to the
                slot) is looked at the vsync it happens: the tables must be the first read's (restart on read); what the cast left of
                them is noted. Returns the plug-ins read, in order; with stop_at_load, stops at the first (the game running)."""
                nonlocal restarts, mutated
                to_row(row)
                g.call("skip_charging", reset=True)
                g.press("circle", vsyncs=3, settle=1)
                try:
                    r.wait(lambda: r.mode_now() == (2, smoke.CLOSED), 120, "")
                except TimeoutError:
                    return None
                g.run(10)
                for _ in range(6):
                    g.call("input", buttons=["circle"], vsyncs=3)
                    g.run(4)
                    if r.camera_mode() not in (1, 2, 3):
                        break
                    g.run(30)
                if r.camera_mode() in (1, 2, 3):
                    for _ in range(4):
                        g.press("cross", vsyncs=3, settle=10)
                        if r.mode_now()[0] != 2:
                            break
                    return None
                seen, count = [], reads()
                for i in range(2500):
                    n = reads()
                    if n > count:
                        count = n
                        name = loaded_names()
                        if name is not None:
                            seen.append(name)
                            order.append(name)
                            where[spell_id] = name
                            if name in secs:
                                region = region_of(name)
                                if name in first:
                                    restarts += 1
                                    if last_end.get(name, first[name]) != first[name]:
                                        mutated += 1
                                    assert region == first[name], f"{name} read again: its tables are not as the first read left them"
                                else:
                                    first[name] = region
                                if stop_at_load:
                                    return seen
                    if i > 10 and r.mode_now()[0] == 1:
                        break
                    g.run(1)
                g.run(4)
                r.wait(lambda: r.mode_now()[0] == 1, 600, "back in the field", step=4)
                for name in set(seen):
                    if name in secs:
                        if corrupt:  # a table the cast changed (its first word flipped): the next read of the file must put it back
                            g.store_data(f"__start_rood_ovl_effect_{name}_bin_data", bytes(b ^ 0xFF for b in first[name][:4]))
                        last_end[name] = region_of(name)
                return seen

            r.shell("magick")
            for _ in range(6):
                if r.ui()["magick"].get("sub") == "warlock":
                    break
                g.press("r2", vsyncs=3, settle=4)
            spells = next(s["spells"] for s in r.ui()["magick"]["schools"] if s.get("id") == "warlock")
            ran = []
            for row, spell in enumerate(spells):
                if cast(row, spell["id"]):
                    ran.append((row, spell["id"]))
                g.run(30)
            print("plug-ins read, by warlock spell:", where, flush=True)
            # A, B, A, A: two spells with different plug-ins, the first twice more.
            pair = next(((x, y) for x in ran for y in ran if where[x[1]] != where[y[1]]), None)
            assert pair, "the warlock spells load fewer than two different plug-ins"
            for x in (pair[0], pair[1], pair[0], pair[0]):
                cast(*x, corrupt=True)
                g.run(30)
            assert args.baseline or restarts >= 3, f"A, B, A, A: only {restarts} re-reads seen"
            seen = cast(*pair[0], stop_at_load=True)
            assert seen, "the save point's cast loaded no plug-in"
            print("plug-ins loaded in order:", order)
            g.pause()
            picture(g)  # (a screenshot changes a native static of the state hash: it is taken before the mark, not between it and a leg)
            g.mark()
            before = (g.call("snapshot", action="hash"), span(g), None)
            snap = work / "point.snap"
            g.call("snapshot", action="save", path=str(snap))
            g.run(LEG)
            first_leg = (g.call("snapshot", action="hash"), span(g), picture(g))
            if os.environ.get("PLUGIN_SMOKE_DIFF"):
                g.call("snapshot", action="save", path=os.environ["PLUGIN_SMOKE_DIFF"] + "/leg1.snap")
            g.rewind()
            after = (g.call("snapshot", action="hash"), span(g), None)
            assert after[0] == before[0], "rewind: the game state hash differs from the save point's"
            assert after[1] == before[1], "rewind: the plug-in sections differ from the save point's"
            g.run(LEG)
            again = (g.call("snapshot", action="hash"), span(g), picture(g))
            if again != first_leg:
                import json as _j
                print("rewind legs differ in:", [n for n, x, y in zip(("hash", "sections", "picture"), again, first_leg) if x != y], [k for k in _j.loads(again[0]) if _j.loads(again[0])[k] != _j.loads(first_leg[0])[k]] if isinstance(again[0], str) else [k for k in again[0] if again[0][k] != first_leg[0][k]])
            assert again[:2] == first_leg[:2] and (LEG < 60 or again[2] == first_leg[2]), "rewind: the second leg's state hash, plug-in sections or picture differ from the first's"
            print(f"rewind: state hash and plug-in sections exact at the mark and after {LEG} vsyncs, the picture too after them ({hi - lo} bytes spanned)")
        finally:
            g.close()
        second = rood_rpc.Game.launch(card=str(card), fast=True, log=work / "resume.log", extra_env=env, snapshot=snap)
        try:
            second.call("ui_menu", modern=True)  # (host settings the first process was given by RPC: not in a save state)
            second.call("skip_charging", mode="skip", who="all", name=smoke.NAME_SECONDS)
            got = (second.call("snapshot", action="hash"), span(second), None)
            assert got[0] == before[0], "resume: the game state hash differs from the save point's"
            assert got[1] == before[1], "resume: the plug-in sections differ from the save point's"
            second.run(LEG)
            leg = (second.call("snapshot", action="hash"), span(second), picture(second))
            if os.environ.get("PLUGIN_SMOKE_DIFF"):
                second.call("snapshot", action="save", path=os.environ["PLUGIN_SMOKE_DIFF"] + "/leg2.snap")
            assert leg[:2] == first_leg[:2] and (LEG < 60 or leg[2] == first_leg[2]), \
                "resume: after the leg the state hash, plug-in sections or picture differ from the first leg's"
            print("resume: state hash and plug-in sections exact at once and after the leg, the picture too after a long leg")
        finally:
            second.close()
    print(f"PASS: {len(order)} plug-in reads, {restarts} of an already read plug-in (its tables as the first read left them; {mutated} of them after a cast that had changed them)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
