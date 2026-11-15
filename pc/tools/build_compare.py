#!/usr/bin/env python3
"""Does a change alter the game? The same fight on two builds, frame by frame.

    python3 pc/tools/build_compare.py OLD_BINARY [NEW_BINARY] [--card CARD] [--seconds 30]
    (the fight's extra settings.cfg lines: BUILD_COMPARE_SETTINGS=$'render_thread 2\n')

Every step the named fields of every actor (RPC battle_state: its record, stats, object and AI state) are
compared too, which shows what an off-screen actor does. Afterwards every enemy is defeated (its HP written to 0) and the loot the game builds
is compared too: how many drop nodes, each one's actor id and item bytes.

Runs are deterministic (no wall clock in game logic, no input), so two builds of
the same behaviour draw the same frames. A room with enemies is entered from the
save room checkpoint and left to play: the enemies walk, attack and cast. Every
quarter second the frame's hash is taken and, at the end, what was cast. NEW_BINARY
defaults to this tree's build. OLD against OLD runs first as the control: if that
differs, the comparison proves nothing and the tool says so. Exit 0 when the frames
match, and the sound: the room's recorded mix (this room, then zone 15 room 0 with its script) must be byte for byte the same
(--audio off skips it; on a mismatch the first differing time is printed). Covers this fight and this sound only, not every screen or effect.

    python3 pc/tools/build_compare.py OLD_BINARY --casts [spells,arts] [--cast-limit N]

The wider lane: instead of the fight, Ashley casts every spell of the four schools (and
the equipped category's Break Arts) through the menus, as tests/charge_skip_soak.py does,
and the display is hashed every few vsyncs through each action. Old, old again (the
control) and new are compared action by action. Where the new build records them
(RPC effect_sites), the effect polygon call sites that ran are mapped to plugin
functions and counted against all of the build's (the coverage the frames cover).
Slow (several minutes); its actions are the spells' effect plugins, not every one.

    python3 pc/tools/build_compare.py OLD_BINARY --menus [ending,menuf] [--vram-stream] [--menu-sample N] [--claims off] [--aspect 16:9] [--new-env K=V]

The menus' lane: the screens the classic UI draws, reached by the routes classic_parity.py tours them by (the
save room's game, the classic look: the shipped plugins on, so rood.classic's claims draw what they claim;
--claims off leaves the game's own drawing), with the display hashed every N vsyncs (default 10) through
everything each route does. Old, old again (the control) and new are compared route by route. The routes are
SCENES below: a new screen's route is one function there, or one of classic_parity.py's tours wrapped as the others are. Routes:
mainmenu, manual, levelled, buff (the HUD buff rows), emote_arrow (the off-screen emote arrow and the balloons), items, magic, arts, abilities, status, score, map, data (MENU7's SAVE and LOAD file menus), setup, loot,
storage, ending, menuf, dialogue (the talk bubble's every shape: first_bubble), title (its menu and its file menu's copy) and gameover
(MENU7's game over LOAD). opening_events (the opening played through, the scanlines forced on) and script_<zone>_<room> (a room's own event script, for the rooms with a door into them) and platforms (the platform shadows of rooms 9-5, 15-7, 24-2, 25-2, 32-0) and story_<scene id> (a story scene of pc/assets/cutscenes/story.json played as the cutscene player plays it, from the title: the rooms and events no warp reaches; the wipes, the screen effect, its border).
"""
import argparse
import contextlib
import hashlib
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
ROOM = tuple(int(n) for n in os.environ.get("BUILD_COMPARE_ROOM", "48,10").split(","))  # (zone, room) of the fight; another room has other enemies and abilities


ROOM_ENEMIES = range(2, 10)  # vs_battle_actors slots of the room's enemies
STATS_HP = 0x18  # vs_battle_actor2.currentHP (the stats pointer is read by the build's exported offset)
LOOT_BYTES = 0x220  # vs_battle_loot; a drop node is: next (the pointer's width), int actorId, the loot


def loot(g, width):
    """Defeat the room's enemies and read the drop list: [(actor id, item bytes hash)]."""
    from rood_rpc import symbols
    sym = symbols()
    pointers = g.read(sym["vs_battle_actors"], 16 * 8)
    for slot in ROOM_ENEMIES:
        actor = int.from_bytes(pointers[slot * 8:slot * 8 + 8], "little")
        stats = g.actor_stats(actor) if actor else 0
        if stats:
            g.write(stats + STATS_HP, (0).to_bytes(2, "little"))
    head = lambda: int.from_bytes(g.read(sym["_lootListHead"], 8), "little") & ((1 << 8 * width) - 1)
    for _ in range(40):
        g.run(15)
        if head():
            break
    g.run(60)
    drops, node = [], head()
    while node and len(drops) < 64:
        raw = g.read(node, width + 4 + LOOT_BYTES)
        drops.append([int.from_bytes(raw[width:width + 4], "little"), hashlib.sha256(raw[width + 4:]).hexdigest()[:16]])
        node = int.from_bytes(raw[:width], "little")
    return drops


def cover(binary):
    """BUILD_COMPARE_COVER=<log>: this tree's own build runs under gdb (pc/tools/line_coverage.py), the lines it reaches
    appended to the log; ROOD_BINARY is the wrapper, ROOD_SYMBOLS stays the binary."""
    wrapper = os.environ.get("BUILD_COMPARE_WRAPPER")  # a script that runs COV_REAL (e.g. under gdb with a probe of its own)
    if wrapper and pathlib.Path(binary).resolve() == pathlib.Path(os.environ.get("BUILD_COMPARE_COVER_BINARY") or ROOT / "pc/build/boot64/rood_battle_probe").resolve():
        os.environ.update(ROOD_BINARY=wrapper, COV_REAL=str(pathlib.Path(binary).resolve()))
        return
    log = os.environ.get("BUILD_COMPARE_COVER")
    work = ROOT / "pc/build/line_coverage"
    if log and pathlib.Path(binary).resolve() == pathlib.Path(os.environ.get("BUILD_COMPARE_COVER_BINARY") or ROOT / "pc/build/boot64/rood_battle_probe").resolve() and (work / "wrap").exists():
        os.environ.update(ROOD_BINARY=str(work / "wrap"), COV_REAL=str(pathlib.Path(binary).resolve()), COV_PROBE=str(work / "probe.py"),
                          COV_TARGETS=str(work / "targets.txt"), COV_LOG=log)


def start(card, out, name):
    """A game in the fight's room, scripts off, the settings the comparison needs."""
    from checkpoints import launch_at
    g = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env={
        "ROOD_GI": os.environ.get("BUILD_COMPARE_GI", "0"), "ROOD_INTERPOLATE": "0", "ROOD_SETTINGS": str(out / "settings.cfg"), "ROOD_AUDIO": "none",
        "ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(out / "lights"), "ROOD_PREFABS": str(out / "prefabs")})
    g.call("graphics", interpolate=False)
    g.call("room_goto", zone=ROOM[0], room=ROOM[1], enemies=True, scripts=False)
    for _ in range(80):
        g.run(20)
        if g.call("room_goto", action="status")["phase"] in (0, 4):
            break
    return g


STREAM = os.environ.get("BUILD_COMPARE_STREAM") == "1"  # --stream: every vsync's drawing digest (RPC stream) is kept and compared


def stream_take(g, taken):
    """The digests of the frames closed since the last call (`taken`: [closed so far, digests]); a build without the RPC
    leaves them None."""
    if not STREAM or taken[1] is None:
        return
    try:
        r = g.call("stream", count=256)
    except Exception:
        taken[1] = None
        return
    missed = r["closed"] - taken[0] - len(r["frames"])
    new = r["closed"] - taken[0] - max(missed, 0)
    if missed > 0:
        taken[1].append("gap")  # more frames closed than the ring holds: a hole the comparison must not hide
    taken[1] += [[f[0], f[3]] for f in r["frames"][len(r["frames"]) - new:]] if new else []
    taken[0] = r["closed"]


def stream_start(g):
    taken = [0, []]
    if STREAM:
        try:
            g.call("stream", reset=True, on=True)
        except Exception:
            taken[1] = None
    return taken


def census_start(g):
    """BUILD_COMPARE_CENSUS=DIR: the ordering-table census (pc/tools/ot_census.py) counts from here."""
    if os.environ.get("BUILD_COMPARE_CENSUS"):
        try:
            g.call("ot_census", on=True, reset=True)
        except Exception:
            pass


def census_write(g, name):
    out = os.environ.get("BUILD_COMPARE_CENSUS")
    if out:
        try:
            pathlib.Path(out).mkdir(parents=True, exist_ok=True)
            (pathlib.Path(out) / f"{os.getpid()}-{name}.json").write_text(json.dumps(g.call("ot_census")))
        except Exception:
            pass


def play(binary, seconds, card, out, width):
    """In a process of its own (ROOD_BINARY is read at import): the hashes, the casts and the drops, as JSON on stdout."""
    os.environ.update(ROOD_BINARY=binary, ROOD_SYMBOLS=binary, ROOD_HEADLESS="egl", ROOD_RENDERER="vulkan", ROOD_ARCH="64", ROOD_TEST_SNAPSHOTS="0")
    sys.path[:0] = [str(ROOT / "pc/tools"), str(ROOT / "pc/tests")]
    cover(binary)
    out = pathlib.Path(out)
    (out / "settings.cfg").write_text("qol_skip_charging 0\nui_menus 0\n" + os.environ.get("BUILD_COMPARE_SETTINGS", ""))
    g = start(card, out, "fight")
    census_start(g)
    sites = []
    try:
        try:
            g.call("effect_sites", on=True, reset=True)
        except Exception:
            pass
        g.call("skip_charging", reset=True)
        frames, states = [], []
        taken = stream_start(g)
        for _ in range(seconds * 4):
            g.run(15)
            stream_take(g, taken)
            shot = out / "frame.ppm"
            g.screenshot(str(shot))
            frames.append(hashlib.sha256(shot.read_bytes()).hexdigest()[:16])
            try:  # a build older than the RPC has none: the comparison says so
                states.append(g.call("battle_state"))
            except Exception:
                states.append(None)
        census_write(g, "fight")
        ended = g.call("skip_charging").get("ended", "")
        try:
            sites = g.call("effect_sites")["sites"]
        except Exception:
            pass
    finally:
        g.close()
    # A second game: the enemies defeated while idle (one mid-cast does not die at once).
    g = start(card, out, "loot")
    try:
        drops = loot(g, width)
    finally:
        g.close()
    casts = [e for e in (entry.split(":") for entry in ended.split()) if len(e) >= 4 and int(e[0]) != 0 and e[2] == "1"]
    print("RESULT" + json.dumps({"frames": frames, "casts": casts, "drops": drops, "states": states, "sites": sites, "stream": taken[1]}))


DRIVE_CRASHES = {}  # action id -> why the driver skips it (none: a valid id does not crash)
# The rooms whose enemies have, together, every ability the 182 rooms with enemies give them (a scan of stats + 0x8C0 of the first 59 zones'
# rooms, greedily): the lane `--casts enemies` drives each ability once.
ENEMY_ROOMS = ("56,22;24,1;48,25;48,18;52,5;29,0;40,24;50,14;55,1;22,0;24,0;24,4;24,15;49,1;51,16;56,6;13,9;25,1;40,11;48,20;50,6;51,1;51,9;"
               "51,21;55,21;56,10;12,1;13,2;16,0;24,11;40,22;48,26;49,0;50,0;51,6;51,12;52,6;53,23;53,29;54,4;55,14;56,1;56,5")
ENEMY_SKIPS = {147: "an enemy of zone 13 room 9 casting it reads a sector far past the disc's end (the effect's file is named by data that room lacks): the disc read fails"}  # action id -> why the enemies lane leaves it out
SCHOOLS = (("warlock", 18), ("shaman", 6), ("sorcerer", 18), ("enchanter", 8))
SAMPLE = int(os.environ.get("ROOD_CAST_SAMPLE", 6))  # vsyncs between display hashes in an action (to narrow a difference: 1)
CAST_ONLY = [int(i) for i in os.environ.get("ROOD_CAST_ONLY", "").split(",") if i]  # --casts: only these spell ids


def play_casts(binary, card, out, kinds, limit):
    """Every spell (and art) cast with its holds skipped, the display hashed through each: {action: [hashes]}, and
    the effect polygon call sites the build recorded (empty for a build without the RPC)."""
    os.environ.update(ROOD_BINARY=binary, ROOD_SYMBOLS=os.environ.get("ROOD_CAST_SYMBOLS", binary), ROOD_HEADLESS="egl",
                      ROOD_RENDERER="vulkan", ROOD_ARCH="64", ROOD_TEST_SNAPSHOTS="0")
    sys.path[:0] = [str(ROOT / "pc/tools"), str(ROOT / "pc/tests")]
    cover(binary)
    from checkpoints import launch_at
    import skip_charging_smoke as smoke
    import charge_skip_soak as soak
    out = pathlib.Path(out)
    (out / "prefabs").mkdir()
    (out / "lighting").mkdir()
    env = {"ROOD_PERF": "1"} if os.environ.get("BUILD_COMPARE_PERF") else {}
    env |= {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(out / "lights"), "ROOD_PREFABS": str(out / "prefabs"),
           "ROOD_LIGHTING": str(out / "lighting"), "ROOD_GI": os.environ.get("BUILD_COMPARE_GI", "0"), "ROOD_INTERPOLATE": "0", "ROOD_AUDIO": "none", "ROOD_OUT": str(out)}
    os.environ.pop("ROOD_SETTINGS", None)
    card_copy = out / "card1.mcd"
    card_copy.write_bytes(pathlib.Path(card).read_bytes())
    socket = f"/tmp/rood-bcompare-{os.getpid()}.sock"
    snap = out / "room.snap"
    g = launch_at("save_room", card=card_copy, fast=True, log=out / "boot.log", extra_env=env, socket_path=socket)
    try:
        g.call("room_goto", zone=smoke.ROOM[0], room=smoke.ROOM[1], enemies=True, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(smoke.APPROACH)
        g.call("snapshot", action="save", path=str(snap))
    finally:
        g.close()

    class Cast(soak.Soak):
        def cast(self, what):
            """soak.Soak.run_action, hashing the display every SAMPLE vsyncs from the target's confirmation."""
            g = self.g
            g.call("skip_charging", reset=True)
            g.press("circle", vsyncs=3, settle=1)
            try:
                self.wait(lambda: self.mode_now() == (2, smoke.CLOSED), 120, "")
            except TimeoutError:
                return None
            g.run(10)
            for _ in range(6):
                g.call("input", buttons=["circle"], vsyncs=3)
                g.run(4)
                if self.camera_mode() not in (1, 2, 3):
                    break
                g.run(30)
            if self.camera_mode() in (1, 2, 3):
                for _ in range(4):
                    g.press("cross", vsyncs=3, settle=10)
                    if self.mode_now()[0] != 2:
                        break
                return None
            return self.watch(what)

        def watch(self, what):
            """The display hashed every SAMPLE vsyncs through the action, until the player is free again."""
            g = self.g
            hashes = []
            self.last_states = []
            perf = os.environ.get("BUILD_COMPARE_PERF")  # ROOD_PERF's game-thread time per vsync through the action
            if perf:
                g.call("perf", reset=True)
            for i in range(soak.WATCHDOG // SAMPLE):
                g.run(SAMPLE)
                shot = out / "frame.ppm"
                g.screenshot(str(shot))
                hashes.append(hashlib.sha256(shot.read_bytes()).hexdigest()[:12])
                keep = os.environ.get("BUILD_COMPARE_KEEP")  # a directory: the action's frames, by process, action and sample
                if keep:
                    pathlib.Path(keep).mkdir(parents=True, exist_ok=True)
                    (pathlib.Path(keep) / f"cast-{os.getpid()}-{what.replace(' ', '_')}-{len(hashes) - 1:04d}.ppm").write_bytes(shot.read_bytes())
                try:
                    state = g.call("battle_state")
                    if os.environ.get("ROOD_CAST_VIEW"):  # the scene's view record (vs_view; the scratchpad at 0x1F800010 before it)
                        symbols = sys.modules["rood_rpc"].symbols()
                        state["view"] = g.read(symbols.get("vs_view", 0x1F800010), 0x58).hex()
                    self.last_states.append(state)
                except Exception:
                    self.last_states.append(None)
                if i > 2 and self.mode_now()[0] == 1:
                    break
            if perf:
                sections = g.call("perf").get("cpu_sections", {})
                print(f"build_compare: perf {what}: {len(hashes) * SAMPLE} vsyncs, game {sections.get('game')} ms/vsync, "
                      f"draw {sections.get('draw')}", file=sys.stderr, flush=True)
            g.run(4)
            return hashes

    def drive(self, action_id, what, actor=None):
        """The action driver (pc/src/action_driver.c): action `action_id` started through the game's own entry with the state a
        menu-driven cast left, the display hashed through it. None when it could not start (and why, printed)."""
        g = self.g
        g.call("skip_charging", reset=True)
        before = g.call("action")["started"]
        reply = g.call("action", id=action_id) if actor is None else g.call("action", id=action_id, actor=actor)
        if not reply["accepted"]:
            print(f"build_compare: action {action_id} not started: no captured state", file=sys.stderr)
            return None
        for _ in range(60):
            g.run(2)
            if g.call("action")["started"] > before:
                return self.watch(what)
        print(f"build_compare: action {action_id} not started: the game was not free to act", file=sys.stderr)
        return None

    Cast.drive = drive
    r = Cast("casts", snap, card_copy, env, out, socket, fast=True)
    g, sym = r.g, r.sym
    actions, sites, peak, states = {}, [], 0, {}
    try:
        def prepare():
            g.call("ui_menu", modern=True)
            g.call("skip_charging", mode="skip", who="all", name=smoke.NAME_SECONDS)
            g.write(sym["vs_main_settings"] + smoke.INFORMATION, b"\x01")
            flags = g.read(sym["vs_main_settings"] + soak.MENU_FLAGS, 1)[0]
            g.write(sym["vs_main_settings"] + soak.MENU_FLAGS, bytes([flags | 1]))
            for name, size in SCHOOLS:
                for i in g.read(sym[f"vs_battle_{name}SpellIds"], size):
                    a = sym["vs_main_actions"] + i * smoke.ACTION + smoke.ACTION_UNLOCKED
                    g.write(a, bytes([g.read(a, 1)[0] | 0x80]))
            g.run(4)
            try:
                g.call("effect_sites", on=True, reset=True)
            except Exception:
                pass
        prepare()
        count = 0
        by_actor = set()
        found = {}
        record = lambda i: g.read(sym["vs_main_actions"] + i * smoke.ACTION, smoke.ACTION)
        def drive_actors(tag, attack):
            """The game's own entry for an actor's action (func_80072EC4: the base attack's, and what the actor commands run for the
            enemies, which choose from their own list of abilities, stats + 0x8C0: armor[6][4] of 4 bytes, the id first): Ashley's
            attack with her weapon drawn, then each ability of each enemy of the room (its effect data is the room's)."""
            nonlocal count
            pointers = g.read(sym["vs_battle_actors"], 16 * 8)
            todo = [(0, 1)] if attack else []
            for slot in ROOM_ENEMIES:
                actor = int.from_bytes(pointers[slot * 8:slot * 8 + 8], "little")
                stats = g.actor_stats(actor) if actor else 0
                if stats:
                    todo += [(slot, i) for i in sorted({struct.unpack_from("<h", g.read(stats + 0x8C0 + 4 * k, 4))[0] for k in range(24)}) if 0 < i < 256]
            for slot, action_id in todo:
                raw = record(action_id)
                if raw[smoke.ACTION_NAME] == 0xE7 or raw[smoke.ACTION_NAME] == 0 or action_id in by_actor or action_id in ENEMY_SKIPS or (limit and count >= limit):
                    continue
                by_actor.add(action_id)  # (one actor's run of an action: the effect is the same for any other)
                g.rewind()
                g.run(20)
                r.refill()
                if slot == 0:
                    g.press("circle", vsyncs=3, settle=60)  # (the weapon drawn)
                print(f"build_compare: driving action {action_id} by {'Ashley' if slot == 0 else f'enemy {slot} of {tag}'}", file=sys.stderr, flush=True)
                hashes = r.drive(action_id, f"actor {action_id}", actor=slot)
                actions[f"actor {action_id}"] = hashes
                states[f"actor {action_id}"] = r.last_states if hashes else None
                count += hashes is not None
        if "drive" in kinds:
            g.pause()
            g.mark()  # (the room as it is, for after the menus' casts)
            g.call("action", capture=True)  # (every menu-driven cast below leaves its state for the driver, one per kind of target)
            saved = os.environ.get("BUILD_COMPARE_STATES")  # (a file: the captured states of an earlier run, loaded instead of the menus' casts)
            if saved and pathlib.Path(saved).exists():
                g.call("action", load=saved)
                kinds = kinds.replace("drive", "driven")
        for name, size in (SCHOOLS if "spells" in kinds or ("drive" in kinds and "driven" not in kinds) else ()):
            r.refill()
            r.shell("magick")
            for _ in range(6):
                if r.ui()["magick"].get("sub") == name:
                    break
                g.press("r2", vsyncs=3, settle=4)
            spells = next(s["spells"] for s in r.ui()["magick"]["schools"] if s.get("id") == name)
            for row, spell in enumerate(spells):
                if limit and count >= limit:
                    break
                if CAST_ONLY and spell["id"] not in CAST_ONLY:
                    continue
                r.refill()
                r.shell("magick")
                for _ in range(6):
                    if r.ui()["magick"].get("sub") == name:
                        break
                    g.press("r2", vsyncs=3, settle=4)
                for _ in range(24):
                    c = r.ui()["magick"]["cursor"]
                    if c == row:
                        break
                    g.press("down" if c < row else "up", vsyncs=3, settle=4)
                hashes = r.cast(f"spell {spell['id']}")
                actions[f"spell {name} {spell['id']}"] = hashes
                states[f"spell {name} {spell['id']}"] = r.last_states if hashes else None
                count += hashes is not None
        if "drive" in kinds and "driven" not in kinds:
            # The items: a misc item of each kind of target (the action each names: its type and shape of target), used from the Items screen.
            misc = b"".join(struct.pack("<HBB", soak.FIRST_MISC + k, 2, k + 1) for k in range(64))
            g.write(sym["vs_main_inventory"] + smoke.MISC, misc)
            g.run(4)
            seen = set()
            for row in range(64):
                r.refill()
                r.shell("items")
                for _ in range(8):
                    if r.ui().get("items", {}).get("category") == "misc":
                        break
                    g.press("r2", vsyncs=3, settle=4)
                lst = r.ui()["items"]["list"]
                if row < len(lst) and lst[row]["use_reason"].startswith("Loading"):
                    try:  # (MAINMENU is loaded again after an action)
                        r.wait(lambda: not r.ui()["items"]["list"][row]["use_reason"].startswith("Loading"), 240, "", step=4)
                    except TimeoutError:
                        pass
                    lst = r.ui()["items"]["list"]
                if row >= len(lst):
                    break
                if not lst[row]["usable"] or lst[row]["action"] == 0:
                    continue
                key = (g.read(sym["vs_main_actions"] + lst[row]["action"] * smoke.ACTION + 2, 1)[0] >> 1 & 7, g.read(sym["vs_main_actions"] + lst[row]["action"] * smoke.ACTION + 2, 1)[0] >> 4)
                if key in seen:
                    continue
                seen.add(key)
                for _ in range(70):
                    c = r.ui()["items"]["cursor"]
                    if c == row:
                        break
                    g.press("down" if c < row else "up", vsyncs=3, settle=2)
                g.press("circle", vsyncs=3, settle=4)
                for _ in range(10):
                    m = r.ui()["items"]
                    if m.get("mode") == "actions" and m["actions"][m["action"]]["id"] == "use":
                        break
                    g.press("down", vsyncs=3, settle=4)
                actions[f"item {lst[row]['id']}"] = r.cast(f"item {lst[row]['id']}")
                states[f"item {lst[row]['id']}"] = r.last_states if actions[f"item {lst[row]['id']}"] else None
        if "arts" in kinds or ("drive" in kinds and "driven" not in kinds):
            art0, _ = r.learn_art()
            for k in range(4):
                a = sym["vs_main_actions"] + (art0 + k) * smoke.ACTION + smoke.ACTION_UNLOCKED
                g.write(a, bytes([g.read(a, 1)[0] | 0x80]))
            r.shell("abilities")
            cat = r.ab()["equipped_category"]
            g.write(sym["vs_main_artsStatus"] + smoke.ARTS_LEARNED + cat % 10, bytes([4]))
            r.shell_close()
            for k in range(4):
                r.refill()
                r.art_menu(art0)
                for _ in range(6):
                    if r.ab()["art"] == k:
                        break
                    g.press("down", vsyncs=3, settle=4)
                actions[f"art {art0 + k}"] = r.cast(f"art {art0 + k}")
                states[f"art {art0 + k}"] = r.last_states
        if "drive" in kinds or "driven" in kinds:
            g.call("action", capture=False)
            if "driven" not in kinds and os.environ.get("BUILD_COMPARE_STATES"):
                g.call("action", save=os.environ["BUILD_COMPARE_STATES"])
            kept = g.call("action")
            print(f"build_compare: the driver captured {kept['kinds_captured']} kinds of action in the menus' casts: {kept['keys']}", file=sys.stderr)
            count = 0
            # The menus' casts left the game wherever they left it: back to the room as it was before them (the driver's captured
            # states are host data and stay).
            g.rewind()
            g.run(30)
            print(f"build_compare: after the rewind {g.call('action')['kinds_captured']} kinds are kept", file=sys.stderr)
            # Every action the game's table defines, but those that cannot take the captured state: a different kind of target (the
            # action's type and shape) or no name. Each skip is said, with its reason.
            record = lambda i: g.read(sym["vs_main_actions"] + i * smoke.ACTION, smoke.ACTION)
            skipped = {}
            crashers = DRIVE_CRASHES
            for action_id in range(int(os.environ.get("BUILD_COMPARE_FROM", "0")), 256):
                if limit and count >= limit:
                    break
                if action_id in crashers:
                    skipped[action_id] = crashers[action_id]
                    continue
                raw = record(action_id)
                if raw[smoke.ACTION_NAME] == 0xE7 or raw[smoke.ACTION_NAME] == 0:
                    skipped[action_id] = "no name"
                    continue
                g.rewind()  # (each action from the room as it was: an action that leaves the game in a state of its own cannot spoil the next)
                g.run(20)
                r.refill()
                print(f"build_compare: driving action {action_id}", file=sys.stderr, flush=True)
                hashes = r.drive(action_id, f"drive {action_id}")
                actions[f"drive {action_id}"] = hashes
                states[f"drive {action_id}"] = r.last_states if hashes else None
                count += hashes is not None
                if hashes is None:
                    skipped[action_id] = f"no menu-driven cast left a state for its kind of target (type {raw[2] >> 1 & 7}), or it could not start"
            drive_actors("this room", True)
            reasons = {}
            for action_id, why in skipped.items():
                reasons.setdefault(why, []).append(action_id)
            for why, ids in reasons.items():
                print(f"build_compare: actions not driven ({why}): {len(ids)}: {ids[:12]}{'...' if len(ids) > 12 else ''}", file=sys.stderr)
        if "enemies" in kinds:
            # Every ability the enemies of the game have, each by an enemy of a room that has it (the room's effect data loaded): the rooms
            # ENEMY_ROOMS (or BUILD_COMPARE_ENEMY_ROOMS, zone,room;zone,room) one after the other.
            by_actor.clear()
            count = 0
            for zone, room in (tuple(int(n) for n in w.split(",")) for w in os.environ.get("BUILD_COMPARE_ENEMY_ROOMS", ENEMY_ROOMS).split(";")):
                # Each room in a game of its own, from the same room snapshot: what an earlier room's last action left (the player's
                # hurt or dead, the enemies' acting, a battle mode that never returns to free) is not the next room's start. A room run
                # after others ended the game in an effect script (zone 29 room 0 after five others) that it never reaches alone, and
                # rooms' actions "could not start" because the battle was not free.
                try:
                    reply = g.call("effect_sites")
                    found.update({json.dumps(site): site for site in reply["sites"]})
                    peak = max(peak, reply["peak_items"])
                except Exception:
                    pass
                r.close()
                r = Cast("casts", snap, card_copy, env, out, socket, fast=True)
                g, sym = r.g, r.sym
                prepare()
                g.call("room_goto", zone=zone, room=room, enemies=True, scripts=False)
                for _ in range(80):
                    g.run(20)
                    if g.call("room_goto", action="status")["phase"] in (0, 4):
                        break
                g.run(smoke.APPROACH)
                g.pause()
                g.mark()
                print(f"build_compare: enemies of zone {zone} room {room}", file=sys.stderr, flush=True)
                drive_actors(f"{zone},{room}", False)
                g.pause(False)
        try:
            reply = g.call("effect_sites")
            found.update({json.dumps(site): site for site in reply["sites"]})  # (the games of the enemies' rooms keep theirs)
            sites, peak = list(found.values()), max(peak, reply["peak_items"])
        except Exception:
            sites, peak = [], 0
    finally:
        r.close()
    keep = os.environ.get("BUILD_COMPARE_LOGS")  # (a directory: the games' logs kept, for what a build says with its own debug switches)
    if keep:
        pathlib.Path(keep).mkdir(parents=True, exist_ok=True)
        for log in out.glob("*.log"):
            (pathlib.Path(keep) / f"{os.getpid()}-{log.name}").write_bytes(log.read_bytes())
    print("RESULT" + json.dumps({"actions": actions, "states": states, "sites": sites, "peak": peak}))


MENU_SAMPLE = 10  # vsyncs between display hashes in a menu route


def hashed(g, out, sample, hashes, taken=None):
    """g.run in steps that end on multiples of `sample` vsyncs, the display hashed at each: the hashes are the
    route's, whatever the route waits on (classic_parity.py's tours run a vsync at a time and press buttons)."""
    run, shot, state = g.run, out / "frame.ppm", {"t": 0}

    def stepping(vsyncs):
        left = vsyncs
        while left > 0:
            step = min(left, sample - state["t"] % sample)
            run(step)
            state["t"] += step
            left -= step
            if taken is not None:
                stream_take(g, taken)
            if state["t"] % sample == 0:
                g.screenshot(str(shot))
                hashes.append(hashlib.sha256(shot.read_bytes()).hexdigest()[:12])
                if os.environ.get("BUILD_COMPARE_VRAM_STREAM"):  # --vram-stream: the VRAM stream's running digest (RPC vram_stream) with the picture
                    hashes[-1] += ":" + g.call("vram_stream")["total"]["hash"][:10]
                if os.environ.get("BUILD_COMPARE_REPLAY"):  # the interpolated frame's target (RPC gpu_dump replay) with the picture: the screenshot shows the game's own frame
                    g.call("gpu_dump", path=str(out / "replay.ppm"), replay=True)
                    hashes[-1] += ":" + hashlib.sha256((out / "replay.ppm").read_bytes()).hexdigest()[:10]
                    keep_replay = os.environ.get("BUILD_COMPARE_REPLAY_KEEP")  # (a directory: the replay target and the live target of the samples named in BUILD_COMPARE_REPLAY_AT, for looking at a difference)
                    if keep_replay and len(hashes) - 1 in {int(i) for i in os.environ.get("BUILD_COMPARE_REPLAY_AT", "").split(",") if i}:
                        pathlib.Path(keep_replay).mkdir(parents=True, exist_ok=True)
                        g.call("gpu_dump", path=str(pathlib.Path(keep_replay) / f"live-{len(hashes) - 1}.ppm"), replay=False)
                        (pathlib.Path(keep_replay) / f"replay-{len(hashes) - 1}.ppm").write_bytes((out / "replay.ppm").read_bytes())
                keep = os.environ.get("BUILD_COMPARE_KEEP")  # (a directory: every sampled frame kept, for looking at a difference)
                if keep:
                    pathlib.Path(keep).mkdir(parents=True, exist_ok=True)
                    (pathlib.Path(keep) / f"{os.getpid()}-{len(hashes) - 1:04d}.ppm").write_bytes(shot.read_bytes())
    g.run = stepping


def scene_ending(g, cp, symbols):
    """The credits skipped, MISSION COMPLETE coming in, at rest and left with Cross (its fade-out)."""
    from ending_smoke import RANK_SCREEN_STEP
    at = symbols[RANK_SCREEN_STEP] if isinstance(RANK_SCREEN_STEP, str) else RANK_SCREEN_STEP  # (a name once ENDING's data moved)
    step = lambda: int.from_bytes(g.read(at, 4), "little")
    g.call("ending")
    g.run(1200)
    g.press("start", vsyncs=4, settle=200)
    g.until(lambda: step() > 0x120, step=30, guard=1800, what="the MISSION COMPLETE screen")
    g.run(120)
    g.press("cross", vsyncs=3, settle=240)


def scene_mainmenu(g, cp, symbols):
    """The menu opened from the field, each top command entered, moved through and left."""
    g.press("triangle", vsyncs=3, settle=60)
    for _ in range(9):
        g.press("circle", vsyncs=3, settle=60)
        for button in ("down", "down", "up", "right", "left"):
            g.press(button, vsyncs=3, settle=12)
        g.press("cross", vsyncs=3, settle=40)
        g.press("down", vsyncs=3, settle=12)
    g.press("cross", vsyncs=3, settle=60)


def scene_manual(g, cp, symbols):
    """The Quick Manual (MENUE): the list, a topic's pages, back (classic_parity.py's own opening)."""
    from menu_nav import top_command
    flags = int.from_bytes(g.read(symbols["vs_main_settings"] + 2, 2), "little")
    g.write(symbols["vs_main_settings"] + 2, (flags | 1 << cp.MANUAL_COMMAND).to_bytes(2, "little"))
    g.press("triangle", vsyncs=3, settle=30)
    top_command(g, symbols, cp.MANUAL_COMMAND)
    g.press("circle", vsyncs=2, settle=2)
    g.run(400)  # (a fixed wait: the manual's own state is read by symbol, and not by every build's)
    for button in ("down", "down", "up", "circle"):
        g.press(button, vsyncs=3, settle=40)
    for button in ("down", "down", "up", "up"):
        g.press(button, vsyncs=3, settle=20)
    for _ in range(2):
        g.press("cross", vsyncs=3, settle=40)


def scene_levelled(g, cp, symbols):
    """The original Magic menu's LV picker (MENU0 84.c), a levelled Warlock spell with LV1 and LV2 learned: to LV1, to LV2,
    then LV1 again (menu0_levelled_smoke.py's route: its setup and its walk to the picker, the pad sequence the same)."""
    import struct
    import menu0_levelled_smoke as m
    s = m.Session.__new__(m.Session)
    s.g, s.sym, s.out, s.shots = g, symbols, pathlib.Path("."), False
    g.call("ui_menu", modern=False)
    f = symbols["vs_main_settings"] + m.MENU_FLAGS
    g.write(f, struct.pack("<H", struct.unpack("<H", g.read(f, 2))[0] | 1))
    for level in range(4):
        s.set_learned(m.EXPLOSION + level, level < 2)
    g.run(2)
    spells = [i for i in g.read(symbols["vs_battle_warlockSpellIds"], 18) if s.learned(i)]
    s.fill_mp(s.cost(m.EXPLOSION) + 10)
    for _ in range(4):
        g.press("triangle", vsyncs=3, settle=10)
        if s.wait(lambda: m.MAGIC_ITEM in s.menu_items(), 60, "", step=4, quiet=True) is not None:
            break
    s.wait_ready(m.MAGIC_ITEM)
    s.nav(range(0, 10), m.MAGIC_ITEM)
    g.press("circle", vsyncs=3, settle=10)
    s.wait_ready(m.WARLOCK_ITEM)
    s.nav(range(10, 15), m.WARLOCK_ITEM)
    g.press("circle", vsyncs=3, settle=10)
    s.wait_ready(m.SPELL_ITEM)
    g.run(10)
    s.nav(range(m.SPELL_ITEM, m.SPELL_ITEM + len(spells)), m.SPELL_ITEM + spells.index(m.EXPLOSION))
    g.press("circle", vsyncs=3, settle=4)
    s.wait(lambda: s.levels()[1] == 2, 60, "the LV picker")
    for button in ("right", "left", "right", "right", "left"):
        g.press(button, vsyncs=3, settle=20)
    g.run(40)


def scene_menuf(g, cp, symbols):
    """MENUF's screens, each played through and again skipped (classic_parity.py tour_menuf)."""
    cp.tour_menuf(g, symbols, lambda *args, **kwargs: None, lambda: None)


def tour(name):
    """classic_parity.py's tour `name` from a fresh game (each opens its screen itself): its pad work and waits, hashed."""
    def scene(g, cp, symbols):
        import collections
        getattr(cp, f"tour_{name}")(g, symbols, lambda *args, **kwargs: None, lambda: collections.defaultdict(int))
    return scene


def hud_tour(name):
    """classic_parity.py's tour_<name>(g, check, counters) (the HUD buff rows): its RPC shows and waits, hashed; every check passes."""
    def scene(g, cp, symbols):
        import collections
        getattr(cp, f"tour_{name}")(g, lambda ok, *args, **kwargs: True, lambda kind: collections.defaultdict(int))
    return scene


def scene_emote_arrow(g, cp, symbols):
    """The off-screen emote arrow (func_80095C18's kind 4, whose edge arrow is func_8009651C's shadow quad): at the opening
    (first_bubble) an actor shows the arrow emote and is moved through a grid of places around and beyond the view
    (classic_parity.py's OFFSETS), its position written each vsync; also every balloon emote once (the text-box icons)."""
    import struct
    kinds_at, sides_at = symbols["D_800F2260"], symbols["D_800F2274"]
    g.write(symbols["D_800F2270"], bytes(2))
    g.run(200)  # (the bubble's zoom)
    actors = cp.emote_actors(g)
    if not actors:
        return
    for kind in (1, 2, 3, 5, 6, 7):
        g.write(kinds_at, bytes([0] + [kind] * 15))
        g.write(sides_at, struct.pack("<I", 0xFFFF if kind & 1 else 0))
        g.run(6)
    i, at = actors[0]
    x0, y0, z0 = struct.unpack("<hhh", g.read(at, 6))
    g.write(kinds_at, bytes([0] + [4 if a == i else 0 for a in range(1, 16)]))
    clamp = lambda v: max(-32768, min(32767, v))
    for dx, dy, dz in cp.OFFSETS:
        for _ in range(4):
            g.write(at, struct.pack("<hhh", clamp(x0 + dx), clamp(y0 + dy), clamp(z0 + dz)))
            g.run(1)
    g.write(at, struct.pack("<hhh", x0, y0, z0))


STORY_ROUTES = ("room-001-00-s000", "room-003-00-s000", "evt-0001", "evt-0002", "room-010-00-s000", "room-034-00-s034", "room-032-17-s038",
                "evt-0069", "evt-0036", "evt-0038")  # the story scenes whose drawers no room_goto route reaches
STORY_SETTLE = {"evt-0069": 30}  # vsyncs run before a scene is sampled
STORY_WARPS = ("room-034-00-s034", "room-032-17-s038")  # the scenes with a border, played again with the screen warp set
STORY_SCALED = ("room-001-00-s000",)  # the scenes with GIM images, played again with them drawn at another scale
CHECKPOINT = {"dialogue": "first_bubble", "emote_arrow": "first_bubble", "title": "title", "opening": "title", "opening_events": "title", **{f"story_{i}": "title" for i in STORY_ROUTES}, **{f"story_{i}_warp": "title" for i in STORY_WARPS}, **{f"story_{i}_scaled": "title" for i in STORY_SCALED}}  # (the routes that do not start in the save room)


def scene_dialogue(g, cp, symbols):
    """The duke's talk bubble (the first_bubble checkpoint, no card): its zoom; every bubble shape the frames' table has,
    each flip and tail, written into the open box's fields (classic_parity.py tour_text_frame); the next lines."""
    import struct
    boxes, table = symbols["vs_battle_textBoxes"], symbols["D_800EB588"]
    ptr = cp.PTR
    g.run(200)

    def open_box():
        for i in range(8):
            flags = struct.unpack_from("<I", g.read(boxes + i * 0x30, 4))[0]
            state = struct.unpack("<h", g.read(boxes + i * 0x30 + 0x1C, 2))[0]
            if state != 0 and (flags & 15) in (2, 7):
                return i, flags
        return None, None
    box, flags = open_box()
    if box is None:
        raise SystemExit("build_compare dialogue: no talk bubble open")
    shapes = [k for k in range(16) if int.from_bytes(g.read(table + k * ptr, ptr), "little")]
    try:
        for shape in shapes:
            for flip in range(4):
                for tail in range(4):
                    g.write(boxes + box * 0x30, struct.pack("<I", (flags & ~0xFFC0) | flip << 6 | shape << 8 | tail << 12
                                                            | (flags & 0xFFFF0000)))
                    g.run(3)
    finally:
        g.write(boxes + box * 0x30, struct.pack("<I", flags))
    for _ in range(3):  # the next lines: the bubble closes, the next one opens and zooms in
        g.press("circle", vsyncs=3, settle=0)
        g.run(300)


def scene_title(g, cp, symbols):
    """The title (no card needed to rest): its menu idle, then Start and Continue: the LOAD screen (TITLE 22C.c's copy of
    the file menu): the cards, card 1's files walked past the last page and back, the first file loaded."""
    def press(button, settle=8):
        g.press(button, vsyncs=3, settle=settle)
    g.run(300)
    g.press("start", vsyncs=4, settle=90)
    for button in ("down", "up", "down", "up"):
        press(button, 30)
    press("circle", 90)  # Continue: the LOAD screen, its cards
    for button in ("down", "up", "down", "up"):
        press(button)
    press("circle", 90)  # card 1's files
    for button in ("down",) * 17 + ("up",) * 17:
        press(button, 4)
    press("cross", 60)
    press("circle", 90)
    g.run(30)
    press("circle", 0)  # its first file: loaded
    g.run(500)


def parity_tour(name, scale=1):
    """One of classic_parity.py's battle HUD tours (tour_<name>(g, check, counters)) from the save room, the gate's parity mode on
    (the game draws its own packets, which is what is compared; the claims only compare). `scale`: the UI text scale (the
    popups are drawn that much larger by ui_text: ui_text font is on in these games). Known: against a build older than 739f4c808
    actor_word_x2's frames 667-668 (two labels at once) differ: that build scaled a label 2x for two frames on a stale member
    address of the popup placement (ui_text.c); later builds keep it at normal size, as in the frames before."""
    def scene(g, cp, symbols):
        g.call("classic", parity=True, reset=True)
        g.call("ui_text", scale=scale)
        getattr(cp, f"tour_{name}")(g, lambda ok, message="": ok, lambda kind: g.call("classic")["kinds"][kind])
    return scene


def platforms(rooms=((9, 5), (15, 7), (24, 2), (25, 2), (32, 0))):
    """The rooms whose moving platforms are above the floor, each left to run: the flat subtractive squares under them
    (146C.c func_8008CEB0, room_shadows.zig; the native renderer gets each too). Found by a scan of all 419 rooms with a
    breakpoint on the draw: 9-5, 9-6, 15-7..10, 24-2, 24-3, 24-7, 24-8, 24-13, 24-14, 25-2, 25-3, 31-1, 32-0."""
    def scene(g, cp, symbols):
        for zone, room in rooms:
            g.call("room_goto", zone=zone, room=room, enemies=False, scripts=False)
            for _ in range(80):
                g.run(20)
                if g.call("room_goto", action="status")["phase"] in (0, 4):
                    break
            g.run(200)
    return scene


def weather(zone, room):
    """A room with weather, left to run: zone 40 (Snowfly Forest) has the motes (146C.c func_80090434), zone 48 the rain
    (func_8008F440); no room has the snow or the falling particles (a scan of all 419 rooms), whose code is gone."""
    def scene(g, cp, symbols):
        g.call("room_goto", zone=zone, room=room, enemies=False, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(400)
    return scene


def scene_opening(g, cp, symbols):
    """New Game's opening (the title, no card: Start, Circle): its picture cards (func_800CCA90's tiled drawer, the scaled and
    fading images), the manor's exterior in the rain and the first dialogue, left to play with Circle now and then."""
    g.run(60)
    g.press("start", vsyncs=4, settle=90)
    g.press("circle", vsyncs=3, settle=0)
    for step in range(60):
        g.run(60)
        if step % 4 == 3:
            g.press("circle", vsyncs=2, settle=4)


def popups(scale):
    """The damage and heal numbers (HP and MP, both signs, one to three digits) shown over the party, at UI text scale `scale`."""
    def scene(g, cp, symbols):
        g.call("classic", parity=True, reset=True)
        g.call("ui_text", scale=scale)
        for pool in ("hp", "mp"):
            for sign in (-1, 1):
                for value in (1, 9, 57, 100, 999):
                    g.call("hud", action="show", kind="damage", hold=True, pool=pool, sign=sign, value=value, age=10)
                    g.run(12)
        g.call("hud", action="clear")
        g.run(10)
    return scene


def weather_kind(kind):
    """The room's weather of `kind` (dev RPC weather: 2 snow, 4 falling particles, which no room's own data starts but a room
    script's command can) started in Snowfly Forest (a room with the weather section), left to run."""
    def scene(g, cp, symbols):
        g.call("room_goto", zone=40, room=0, enemies=False, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(20)
        g.call("weather", kind=kind)
        g.run(400)
    return scene


def scene_opening_events(g, cp, symbols):
    """New Game's opening played through (its event script: the GIM images, the wipe among them, the screen effect's tiles, the
    five-panel screen freeze), Circle every 90 vsyncs to move its talk boxes along; for part of it the film's scanlines
    (D_800F4BE4, an event opcode's flag) are forced on, as EVT 6 and 15 turn them on (no room starts those)."""
    g.run(60)
    g.press("start", vsyncs=4, settle=90)
    g.press("circle", vsyncs=3, settle=0)
    for step in range(90):
        if 30 <= step < 40:
            g.write(symbols["D_800F4BE4"], b"\x01")
        g.run(30)
        g.press("circle", vsyncs=2, settle=4)


def room_script(zone, room, steps=90):
    """A room's own event script (the cutscene's GIM images, screen wipes, screen effects): warped to with the room's scripts on
    (as cutscene_scan.py does), Circle pressed now and then to move its talk boxes along, left to play."""
    def scene(g, cp, symbols):
        warp = g.call("room_goto", zone=zone, room=room, enemies=False, scripts=True)
        if not warp.get("ok", True):  # (a room with no door into it: the story_ routes play those)
            raise RuntimeError(f"script_{zone}_{room}: {warp.get('message')}")
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        for step in range(steps):
            g.run(60)
            if step % 3 == 2:
                g.press("circle", vsyncs=2, settle=4)
    return scene


def story_scene(scene_id, warp=False, scaled=0):
    """A story scene (pc/assets/cutscenes/story.json) played as the cutscene player plays it (RPC cutscene start: the game started in the scene's
    room with the story's flags, its event run by the game's own entry, the dialogue going on by itself), from the title with no card. For the
    rooms whose scripts room_goto cannot warp to (no door into them), and the chained events (EVT 1, 2): the wipes, the screen effect, the
    border. Back at the scene's first frame, paused (the scene went on by itself, unpaced, while it was reached: the replay is exact), then run for the scene's frames (the player's count) and a little more.
    With `warp`: the screen effect's radial warp (SCREFF2, set by the event script's op 237, which no scene plays here) set in its state while the
    border is on, so the scene's frames are drawn through it (the warp deforms what the ordering table holds: packets and polygon items).
    With `scaled`: the scene's GIM images drawn at that scale (D_800EB9BC's records, unkC's low 14 bits; the scripts here leave them at 4096) from
    the first frame they show to the end: the textured quads of func_800CBBCC's scaled branch."""
    def scene(g, cp, symbols):
        story = json.loads((ROOT / "pc/assets/cutscenes/story.json").read_text())
        frames = next(s["frames"] for s in story["scenes"] if s["id"] == scene_id)
        g.call("cutscene", action="start", scene=scene_id)
        # (the game reaching the scene runs by itself; vsyncs run now would wait on it.) A hang is the game's clock standing still for
        # 30 s, not a wall-clock budget: a long chain (Hardin's past) on a loaded machine outran one.
        last, still = g.status()["vsync"], time.monotonic()
        while True:
            time.sleep(0.1)
            status = g.call("cutscene")
            if status["on"] and not status["busy"]:
                break
            now = g.status()["vsync"]
            if now != last:
                last, still = now, time.monotonic()
            elif time.monotonic() - still > 30:
                raise RuntimeError(f"{scene_id}: {status['message']} (the game's clock stood still for 30 s at vsync {now})")
        # (the scene went on by itself, unpaced, while it was reached: back to its first frame, paused, replayed exactly from there)
        g.call("cutscene", action="pause")
        g.call("cutscene", action="seek", frame=g.call("cutscene")["first"])
        for _ in range(300):
            time.sleep(0.1)
            if not g.call("cutscene")["busy"]:
                break
        if scaled:
            images = symbols["D_800EB9BC"]
            for step in range(frames // 10 + 12):
                g.run(10)
                base = int.from_bytes(g.read(images, 8), "little")
                shown = [base + i * 0x3F8 for i in range(4) if base and g.read(base + i * 0x3F8 + 3, 1)[0]]  # (a record's unk3 is its brightness)
                for gim in shown:
                    unk_c = int.from_bytes(g.read(gim + 0xC, 2), "little")
                    g.write(gim + 0xC, ((unk_c & 0xC000) | scaled).to_bytes(2, "little"))
                if step >= frames // 10 + 11:
                    break
            return
        if warp:
            state = symbols["D_800F4ED8"]  # (the screen effect's state: unk8 the border's, then unk4 and unk6 the warp's base scale and growth)
            for _ in range(frames // 10 + 12):
                g.run(10)
                if int.from_bytes(g.read(state + 8, 4), "little"):
                    break
            else:
                raise RuntimeError(f"{scene_id}: the border never came on")
            g.write(state + 4, struct.pack("<hh", 3400, -48))
            g.run(frames + 120)
            return
        if scene_id in STORY_SETTLE:  # (the frame the seek lands on is not the same to the pixel in every game: evt-0069's first)
            g.call("step", vsyncs=STORY_SETTLE[scene_id])  # (not sampled)
            g.wait_paused()
        g.run(frames + 120)
    return scene


def scene_areamap(g, cp, symbols):
    """The area map (146C.c func_8008AC78: the room's map in the corner and its frame): Map Right set to Frame (vs_main_settings.simpleMap
    3), the map shown (D_800F1CD6), in a room with a map section, the frame drawn with the weapon sheathed (green) and drawn (red, pulsing)."""
    g.call("room_goto", zone=9, room=17, enemies=False, scripts=False)
    for _ in range(80):
        g.run(20)
        if g.call("room_goto", action="status")["phase"] in (0, 4):
            break
    g.write(symbols["vs_main_settings"] + 9, b"\x03")
    g.write(symbols["D_800F1CD6"], b"\x01")
    g.run(120)
    g.press("circle", vsyncs=3, settle=90)  # (the weapon drawn)
    g.run(120)


def scene_transition(g, cp, symbols):
    """The screen transition's drawing (146C.c _drawScreenTransition): each effect (fade to black, to white, the wipe at both
    wipe angles) held at a few alphas, written each step (the transition's own step stays idle, so the room goes on)."""
    effect, alpha, angle = (symbols[n] for n in ("vs_battle_screenTransitionEffect", "vs_battle_screenTransitionAlpha",
                                                   "vs_battle_screenTransitionWipeAngle"))
    size = cp.PTR if False else 4
    g.run(120)
    for kind in (0, 1, 2, 4, 5):
        for wipe_angle in ((0, 1024, 2048) if kind == 2 else (0,)):
            for level in (16, 64, 128, 200, 254, 255):
                g.write(effect, kind.to_bytes(4, "little"))
                g.write(angle, wipe_angle.to_bytes(4, "little"))
                g.write(alpha, level.to_bytes(4, "little"))
                g.run(2)
    g.write(alpha, (0).to_bytes(4, "little"))
    g.run(20)


def scene_gameover(g, cp, symbols):
    """The game over's LOAD (MENU7 vs_menu7_gameOver): Ashley's HP to 0, Continue, the cards, card 1's files walked, the
    first file loaded."""
    import struct
    PTR = cp.PTR

    def press(button, settle=8):
        g.press(button, vsyncs=3, settle=settle)

    def ashley():
        actor = int.from_bytes(g.read(symbols["vs_battle_actors"], PTR), "little")
        try:
            return actor and g.actor_stats(actor)
        except Exception:
            return 0
    g.until(ashley, step=10, guard=600, what="Ashley in the room")
    g.run(60)
    g.write(ashley() + 0x18, (0).to_bytes(2, "little"))
    g.until(lambda: g.state()["game_over"], step=10, guard=200, what="Game Over")
    g.run(240)
    for _ in range(6):
        press("circle", 30)
        g.run(30)
    g.run(90)
    for button in ("down", "up"):
        press(button)
    press("circle", 90)
    for button in ("down",) * 17 + ("up",) * 17:
        press(button, 4)
    g.run(30)
    press("circle", 0)
    g.run(400)


SCENES = {"levelled": scene_levelled, "ending": scene_ending, "menuf": scene_menuf, "mainmenu": scene_mainmenu, "manual": scene_manual,
          "score": tour("score"), "emote_arrow": scene_emote_arrow, "buff": hud_tour("buff"), "map": tour("map"), "status": tour("status"), "setup": tour("setup"), "items": tour("items"),
          "magic": tour("magic"), "arts": tour("arts"), "abilities": tour("abilities"), "data": tour("data"),
          "loot": tour("loot"), "storage": tour("storage"), "dialogue": scene_dialogue, "title": scene_title,
          "gameover": scene_gameover, "damage": popups(1), "damage_x2": popups(2), "actor_word_x2": parity_tour("actor_word", 2),
          "text_band_x2": parity_tour("text_band", 2), "buff_x2": parity_tour("buff", 2), "transition": scene_transition, "areamap": scene_areamap, "opening": scene_opening, "opening_events": scene_opening_events,
          **{f"script_{z}_{r}": room_script(z, r) for z, r in ((9, 0), (48, 3), (9, 2), (22, 0), (9, 1), (22, 4), (22, 6), (24, 0), (24, 4), (24, 11), (24, 15), (25, 1), (39, 0), (40, 22), (41, 2), (49, 1), (49, 5), (50, 6), (50, 22), (51, 2), (51, 6), (53, 1), (53, 23), (53, 29), (54, 1), (55, 3), (55, 12), (56, 14), (56, 22))}, **{f"story_{i}": story_scene(i) for i in STORY_ROUTES}, **{f"story_{i}_warp": story_scene(i, warp=True) for i in STORY_WARPS}, **{f"story_{i}_scaled": story_scene(i, scaled=3000) for i in STORY_SCALED}, "platforms": platforms(), "forest": weather(40, 0), "rain": weather(48, 1), "snow": weather_kind(2), "fall": weather_kind(4),
          **{n: parity_tour(n) for n in ("gauges", "figure", "status_icons", "battle_mode", "buff", "actor_word", "timer", "text_band",
                                        "target_row", "target_line", "ability_icons")}}
def play_menus(binary, card, out, names, sample, claims, aspect="4:3", extra_env=(), renderer="vulkan", settings_lines=()):
    """The routes of `names`, each in a game of its own: {route: [display hashes]}."""
    os.environ.update(ROOD_BINARY=binary, ROOD_SYMBOLS=binary, ROOD_HEADLESS="egl", ROOD_RENDERER=renderer, ROOD_ARCH="64",
                      ROOD_TEST_SNAPSHOTS="0")
    sys.path[:0] = [str(ROOT / "pc/tools"), str(ROOT / "pc/tests")]
    cover(binary)
    import classic_parity as cp
    import rood_rpc
    from checkpoints import launch_at
    out = pathlib.Path(out)
    (out / "plugins").mkdir()
    settings = out / "settings.cfg"
    lines = {"ui_menus": "0", "ui_text": "font", "interpolate": "0", "aspect": aspect}
    lines.update(kv.split(" ", 1) for kv in settings_lines)
    settings.write_text("".join(f"{k} {v}\n" for k, v in lines.items()))
    env = {"ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(out / "plugins"), "ROOD_PLUGIN_DATA": str(out / "plugin_data"),
           "ROOD_GI": os.environ.get("BUILD_COMPARE_GI", "0"), "ROOD_AUDIO": "none", "ROOD_WINDOW": "1280x720" if aspect == "16:9" else "960x720", "ROOD_LIGHTS": str(out / "lights"),
           "ROOD_PREFABS": str(out / "prefabs"), "ROOD_OUT": str(out)}
    if aspect == "16:9":  # (the widescreen picture itself, not only a wide window: the setting is off by default)
        env["ROOD_WIDESCREEN"] = "1"
    env.update(kv.split("=", 1) for kv in extra_env)
    if renderer == "null":  # (the CPU reference frame: no GPU, the original drawing; quicker and the manual opens)
        env["SDL_VIDEO_DRIVER"] = "invalid-null-classic-parity"
    if claims == "on":
        env["ROOD_PLUGINS_SHIPPED"] = str(ROOT / "pc/plugins")
    results = {}
    for name in names:
        card_copy = out / f"card-{name}.mcd"
        if name in ("dialogue", "emote_arrow", "opening", "opening_events") or name.startswith("story_"):  # (no card: the title's cursor then rests on New Game, the checkpoint's way in)
            card_copy = out / "no-card.mcd"
        else:
            card_copy.write_bytes(pathlib.Path(card).read_bytes())
        g = launch_at(CHECKPOINT.get(name, "save_room"), card=str(card_copy), fast=True, log=out / f"{name}.log", extra_env=env)
        hashes = []
        taken = stream_start(g)
        census_start(g)
        try:
            hashed(g, out, sample, hashes, taken)
            SCENES[name](g, cp, rood_rpc.symbols())
            census_write(g, name)
            text = g.call("ui_text") if renderer != "null" else {}
            try:
                if renderer != "null":
                    for call in (("emissive", {"action": "uses", "keys": True}), ("emissive", {"action": "texel", "u": 1, "v": 1, "tpage": 12, "clut": 0}), ("emissive", {"action": "lights"}), ("material", {})):  # the editors' readers once, for the counters
                        try:
                            g.call(call[0], **call[1])
                        except Exception:
                            pass
                published = g.call("emissive", action="published") if renderer != "null" else None
                try:
                    published["check"] = g.call("emissive", action="published_check")  # each published reader against the live state, same inputs
                except Exception:
                    pass  # the emissive reads served from the published copy
            except Exception:
                published = None  # ui_text's word-level drawing: small-capital words and the status words drawn
            try:
                stream_info = g.call("vram_stream")  # (the renderer's replay counters and the fences the game waited at; a build without them has none)
                stats = {**(stream_info.get("renderer") or {}), "fences": stream_info.get("fences")}
                if os.environ.get("BUILD_COMPARE_SNAPSHOT"):  # (a directory: the end state saved, for snapshot_diff.py)
                    pathlib.Path(os.environ["BUILD_COMPARE_SNAPSHOT"]).mkdir(parents=True, exist_ok=True)
                    g.call("snapshot", action="save", path=str(pathlib.Path(os.environ["BUILD_COMPARE_SNAPSHOT"]) / f"{name}-{os.getpid()}.snap"))
                final = {"snapshot": g.call("snapshot", action="hash"), "stream": stream_info["total"]["hash"],
                         "jitter": stream_info.get("jitter")}  # (the end state: the game's, the VRAM stream's; the harness's counters)
            except Exception:
                stats, final = None, None
        finally:
            g.close()
        results[name] = {"hashes": hashes, "stream": taken[1], "renderer": stats, "final": final, "word_glyphs": text.get("word_glyphs"), "regions": (text.get("regions"), text.get("failed")), "gi": gi_lines(out / f"{name}.log"), "published": published, "item_words": text.get("item_words"),
                         "status_glyphs": (text.get("status") or {}).get("glyphs") if isinstance(text.get("status"), dict) else text.get("status_glyphs")}
    print("RESULT" + json.dumps(results))


def gi_lines(path):
    """The path tracer's own log lines in a route's game log: [ready, glows lines]; both 0 when ROOD_GI=0 (the positive control that
    BUILD_COMPARE_GI=1 ran with it on)."""
    try:
        text = pathlib.Path(path).read_text(errors="ignore")
    except OSError:
        return [-1, -1]
    return [text.count("[gi] path-traced global illumination ready"), text.count("[gi] ") - text.count("[gi] path-traced global illumination ready")]


def env_of(binary, args):
    """--new-env's settings, for NEW's runs only."""
    return args.new_env if binary == args.new else []


def run_menus(binary, args, names):
    with tempfile.TemporaryDirectory(prefix="build-compare-", dir=ROOT / "pc/build") as out:
        done = subprocess.run([sys.executable, __file__, "--play-menus", binary, "--menus", ",".join(names), "--menu-sample",
                               str(args.menu_sample), "--claims", args.claims, "--aspect", args.aspect, "--renderer", args.renderer] + [a for kv in args.setting for a in ("--setting", kv)] + [ "--card", args.card, "--out", out]
                              + [a for kv in env_of(binary, args) for a in ("--env", kv)],
                              capture_output=True, text=True)
    lines = [line for line in done.stdout.splitlines() if line.startswith("RESULT")]
    if not lines:
        sys.exit(f"build_compare: {binary} did not finish:\n{done.stderr[-1500:]}\n{done.stdout[-1500:]}")
    return json.loads(lines[-1][6:])


def stream_verdict(label, a, b, c):
    """--stream: old, old again (control) and new per-vsync digests. Returns 0 same, 1 new differs, 2 a
    build has no stream (it proves nothing); prints the first differing vsync."""
    if a is None or b is None or c is None:
        print(f"{label}: stream: a build has no `stream` RPC (OLD must be built from a tree that has it); nothing compared")
        return 2
    holes = [x.count("gap") for x in (a, b, c)]  # (a step longer than the ring held: those vsyncs are unread, not compared)
    a, b, c = ([e for e in x if e != "gap"] for x in (a, b, c))
    # (by the game's vsync number: where the runs' first reading falls can differ by a vsync, and the game draws every second one.
    # A vsync where the control (old against old again) differs is noise of the build itself, not compared: it is counted and listed.)
    da, db, dc = dict(a), dict(b), dict(c)
    both = sorted(set(da) & set(db) & set(dc))
    noisy = [v for v in both if da[v] != db[v]]
    changed = next((v for v in both if v not in noisy and da[v] != dc[v]), None)
    common = len(both)
    print(f"{label}: stream {len(a)} vsyncs{f' ({holes} holes)' if any(holes) else ''} ({len({d for _, d in a})} distinct digests, {common} in all three runs); "
          f"control differs at {len(noisy)} vsyncs{f' (first {noisy[0]})' if noisy else ''}; old against new "
          f"{'same' if changed is None else f'differs at vsync {changed}'}")
    if common < len(a) // 2:
        print(f"{label}: stream: the runs overlap in too few vsyncs to prove anything")
        return 2
    return 0 if changed is None else 1


def compare_menus(args):
    names = [n for n in args.menus.split(",") if n]
    unknown = [n for n in names if n not in SCENES]
    if unknown:
        sys.exit(f"build_compare: no route {unknown} (have {', '.join(SCENES)})")
    first, control, new = run_menus(args.old, args, names), run_menus(args.old, args, names), run_menus(args.new, args, names)
    code = 0
    for name in names:
        a, b, c = first[name]["hashes"], control[name]["hashes"], new[name]["hashes"]
        words = [(r[name]["word_glyphs"], r[name]["status_glyphs"]) for r in (first, control, new)]
        regions = [r[name]["regions"] for r in (first, control, new)]  # ui_text regions drawn from a font / fell back: the layout path ran
        noise, changed = differing({"frames": a}, {"frames": b}), differing({"frames": a}, {"frames": c})
        print(f"{name}: {len(a)} frames ({len(set(a))} distinct, every {args.menu_sample} vsyncs); control {len(noise)} differ; "
              f"old against new {len(changed)} differ{f' (first at frame {changed[0]})' if changed else ''}")
        print(f"{name}: ui_text word / status glyphs drawn: old {words[0]}, control {words[1]}, new {words[2]}"
              f", of the new's, found among draw items: {new[name]['item_words']}{'' if words[0] == words[2] else '  DIFFERENT'}")
        if new[name].get("published"):
            print(f"{name}: emissive reads from the published copy (new): {new[name]['published']}")
        print(f"{name}: [gi] log lines (ready, other): old {first[name]['gi']}, control {control[name]['gi']}, new {new[name]['gi']}")
        print(f"{name}: ui_text regions drawn (regions, failed): old {regions[0]}, control {regions[1]}, new {regions[2]}"
              f"{'' if regions[0] == regions[2] else '  DIFFERENT'}")
        if noise:
            code = 2
        elif (changed or regions[0] != regions[2] or words[0] != words[2]) and code == 0:
            code = 1
        if STREAM:
            v = stream_verdict(name, first[name]["stream"], control[name]["stream"], new[name]["stream"])
            code = 2 if v == 2 or code == 2 else (code or v)
    if code == 2:
        print("build_compare: the control differs, so this proves nothing")
    return code


def coverage(binary, sites):
    """(sites run, all sites, functions run, all functions) of the effect polygon calls in `binary`: the recorded return
    addresses against every call to pc_frame_poly_at, by the function holding each."""
    symbols = []
    for line in subprocess.run(["nm", "-n", "--defined-only", binary], capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "Tt":
            symbols.append((int(parts[0], 16), parts[2]))
    starts = [a for a, _ in symbols]

    def function(address):
        import bisect
        return symbols[bisect.bisect_right(starts, address) - 1][1]

    calls = []
    for line in subprocess.run(["objdump", "-d", "--no-show-raw-insn", binary], capture_output=True, text=True).stdout.splitlines():
        if "call" in line and "pc_frame_poly_at" in line:
            calls.append(int(line.split(":")[0], 16))
    ran = {function(a) for a in sites}
    every = {function(a) for a in calls}
    plugins = lambda names: {n.split("_")[0] for n in names if n.startswith("plg")}
    return len(set(sites)), len(calls), len(ran & every), len(every), len(plugins(ran)), len(plugins(every))


def run_casts(binary, args):
    keep = os.environ.get("ROOD_CAST_OUT")  # keep the game's logs here (a debug print in the game), one run per call
    with (contextlib.nullcontext(keep) if keep else tempfile.TemporaryDirectory(prefix="build-compare-", dir=ROOT / "pc/build")) as out:
        if keep:
            shutil.rmtree(keep, ignore_errors=True)
            os.makedirs(keep)
        done = subprocess.run([sys.executable, __file__, "--play-casts", binary, "--casts", args.casts, "--cast-limit",
                               str(args.cast_limit), "--card", args.card, "--out", out], capture_output=True, text=True)
    lines = [line for line in done.stdout.splitlines() if line.startswith("RESULT")]
    if not lines:
        sys.exit(f"build_compare: {binary} did not finish:\n{done.stderr[-1500:]}\n{done.stdout[-1500:]}")
    return json.loads(lines[-1][6:])


def compare_casts(args):
    first, control, new = run_casts(args.old, args), run_casts(args.old, args), run_casts(args.new, args)

    def differ(a, b):
        return [k for k in a["actions"] if a["actions"][k] != b["actions"].get(k)]
    noise, changed = differ(first, control), differ(first, new)
    done = [k for k, v in first["actions"].items() if v]
    print(f"{len(done)} actions cast ({len(first['actions']) - len(done)} refused here)")
    print(f"control (old against old): {len(noise)} actions differ")
    print(f"old against new: {len(changed)} actions differ{(': ' + ', '.join(changed[:8])) if changed else ''}")
    for name in changed[:3]:  # what differs first, by name (the samples' battle_state: the actors' and the effects' fields)
        a, b = first.get("states", {}).get(name), new.get("states", {}).get(name)
        if a and b and a[0] and b[0] and "effects" in a[0] and "effects" in b[0]:
            for step, (x, y) in enumerate(zip(a, b)):
                if x is None or y is None:
                    continue
                p, q = fields(x), fields(y)
                keys = [k for k in dict.fromkeys(list(p) + list(q)) if p.get(k) != q.get(k)]
                if keys:
                    print(f"  {name}: state differs from sample {step} ({len(keys)} fields): "
                          + "; ".join(f"{k}: {p.get(k)} -> {q.get(k)}" for k in keys[:4]))
                    if os.environ.get("ROOD_CAST_TRACE"):  # the effects of the samples around the first difference, both builds
                        for t in range(max(0, step - 3), min(len(a), len(b), step + 2)):
                            for label, run_ in (("old", a), ("new", b)):
                                for e in (run_[t] or {}).get("effects", [])[:2]:
                                    print(f"    {label} sample {t}: " + " ".join(f"{k}={e[k]}" for k in e if k not in ("particles", "end")
                                                                                 and not k.startswith("entry") or k in ("entry0", "entry1", "entry2")))
                    break
    if new["sites"]:
        ran, calls, fran, fall, pran, pall = coverage(args.new, new["sites"])
        print(f"effect polygon call sites run: {ran} of {calls}; functions {fran} of {fall}; plugins {pran} of {pall}; most items in a frame: {new['peak']}")
    else:
        print("effect polygon call sites: the new build does not record them (RPC effect_sites)")
    if noise:
        print("build_compare: the control differs, so this proves nothing")
        return 2
    return 0 if not changed else 1


def compare_rooms(args):
    """--rooms 40,3;10,1: the fight in each room (its enemies, their abilities), old, old again and new, frame by frame, and the
    effect polygon call sites the new build ran in them all."""
    sites, code = set(), 0
    for room in args.rooms.split(";"):
        env = dict(os.environ, BUILD_COMPARE_ROOM=room)
        first, control, new = (run(binary, args, args.old_width, env) for binary in (args.old, args.old, args.new))
        noise, changed = differing(first, control), differing(first, new)
        sites |= set(new.get("sites") or [])
        print(f"room {room}: casts old {len(first['casts'])} new {len(new['casts'])}; control {len(noise)} of {len(first['frames'])} frames differ; "
              f"old against new {len(changed)} differ{f' (first at {changed[0] / 4:.2f} s)' if changed else ''}", flush=True)
        if noise:
            code = 2
        elif (changed or first["casts"] != new["casts"]) and code == 0:
            code = 1
        if STREAM:
            v = stream_verdict(f"room {room}", first.get("stream"), control.get("stream"), new.get("stream"))
            code = 2 if v == 2 or code == 2 else (code or v)
    if sites:
        ran, calls, fran, fall, pran, pall = coverage(args.new, sorted(sites))
        print(f"effect polygon call sites run: {ran} of {calls}; functions {fran} of {fall}; plugins {pran} of {pall}")
    return code


def run(binary, args, width, env=None):
    with tempfile.TemporaryDirectory(prefix="build-compare-", dir=ROOT / "pc/build") as out:
        done = subprocess.run([sys.executable, __file__, "--play", binary, "--seconds", str(args.seconds), "--card", args.card,
                               "--out", out, "--width", str(width)], capture_output=True, text=True, env=env)
    lines = [line for line in done.stdout.splitlines() if line.startswith("RESULT")]
    if not lines:
        sys.exit(f"build_compare: {binary} did not finish:\n{done.stderr[-1500:]}")
    return json.loads(lines[-1][6:])


def differing(a, b):
    return [i for i, (x, y) in enumerate(zip(a["frames"], b["frames"])) if x != y] + list(range(min(len(a["frames"]), len(b["frames"])), max(len(a["frames"]), len(b["frames"]))))


def fields(state):
    """{slot.section.field: value} of one battle_state reply."""
    flat = {"rand": state["rand"]}
    for entry in state["actors"]:
        for section in ("actor", "object", "model", "ai"):
            for name, value in entry.get(section, {}).items():
                if name != "end":
                    flat[f"slot {entry['slot']} {section}.{name}"] = value
        if "slot" in entry:
            for section in ("actor", "object", "model", "ai"):
                flat.setdefault(f"slot {entry['slot']} {section}", entry.get(section) is not None)
    for index, effect in enumerate(state.get("effects", [])):
        for name, value in effect.items():
            if name == "particles":
                for k, text in enumerate(value[:-1]):
                    flat[f"effect {index} particle {k}"] = text
                flat[f"effect {index} particles"] = len(value) - 1
            elif name != "end":
                flat[f"effect {index} {name}"] = value
    flat["effects"] = len(state.get("effects", [])) - 1
    if "view" in state:
        flat["view"] = state["view"]
    return flat


def compare_states(old, new):
    """The first step whose actors', objects' or AI states differ, field by field: what the frames cannot show
    (an actor off screen). None when the builds agree or one lacks the RPC (printed)."""
    if not old["states"] or old["states"][0] is None or not new["states"] or new["states"][0] is None:
        print("states: not compared (a build without RPC battle_state)")
        return None
    for step, (a, b) in enumerate(zip(old["states"], new["states"])):
        x, y = fields(a), fields(b)
        keys = [k for k in dict.fromkeys(list(x) + list(y)) if x.get(k) != y.get(k)
                and not (k.startswith("effect") and ("effects" in a) != ("effects" in b))]  # (a build older than the effects dump)
        if keys:
            first = ", ".join(f"{k}: {x.get(k)} -> {y.get(k)}" for k in keys[:3])
            print(f"states: differ from step {step} ({step / 4:.2f} s), {len(keys)} fields: {first}")
            return step
    print(f"states: {len(old['states'])} steps, every named field of every actor equal")
    return None


def play_audio(binary, card, out):
    """The sound: the fight's room, then room 0 of zone 15 with its script (its entry sound effects), the mix recorded
    to out/audio.wav. Everything the sound drivers do reaches the wav, which no frame or actor field shows."""
    os.environ.update(ROOD_BINARY=binary, ROOD_SYMBOLS=binary, ROOD_HEADLESS="egl", ROOD_RENDERER="vulkan", ROOD_ARCH="64", ROOD_TEST_SNAPSHOTS="0")
    sys.path[:0] = [str(ROOT / "pc/tools"), str(ROOT / "pc/tests")]
    cover(binary)
    from checkpoints import launch_at
    out = pathlib.Path(out)
    g = launch_at("save_room", card=card, fast=True, log=out / "audio.log", audio=f"wav:{out / 'audio.wav'}", extra_env={
        "ROOD_GI": os.environ.get("BUILD_COMPARE_GI", "0"), "ROOD_INTERPOLATE": "0", "ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(out / "lights"), "ROOD_PREFABS": str(out / "prefabs")})
    try:
        g.call("room_goto", zone=ROOM[0], room=ROOM[1], enemies=True, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(1200)
        g.call("room_goto", zone=15, room=0, enemies=True, scripts=True)
        g.run(900)
    finally:
        g.close()
    print("RESULT")


def run_audio(binary, args):
    """The recording's samples (the 44-byte header's length is only updated once a second: it is skipped)."""
    with tempfile.TemporaryDirectory(prefix="build-compare-", dir=ROOT / "pc/build") as out:
        done = subprocess.run([sys.executable, __file__, "--play-audio", binary, "--card", args.card, "--out", out], capture_output=True, text=True)
        wav = pathlib.Path(out) / "audio.wav"
        if "RESULT" not in done.stdout or not wav.exists():
            sys.exit(f"build_compare: {binary} did not finish the sound run:\n{done.stderr[-1500:]}")
        return wav.read_bytes()[44:]


def first_difference(a, b):
    """The first differing byte of two recordings (None when equal), found in blocks."""
    n, step = min(len(a), len(b)), 1 << 16
    for i in range(0, n, step):
        if a[i:min(i + step, n)] != b[i:min(i + step, n)]:
            j = next(k for k in range(i, min(i + step, n)) if a[k] != b[k])
            return j
    return None if len(a) == len(b) else n


def compare_audio(args):
    """Old, old again (the control) and new: 0 when the new recording is the old one, byte for byte."""
    first, control, new = run_audio(args.old, args), run_audio(args.old, args), run_audio(args.new, args)
    noise, changed = first_difference(first, control), first_difference(first, new)
    seconds = lambda i: i // 4 / 44100
    print(f"audio: control {'identical' if noise is None else f'differs from {seconds(noise):.2f} s'}; "
          f"old {len(first)} bytes, new {len(new)}; " + ("identical" if changed is None else f"first differs at {seconds(changed):.2f} s"))
    if noise is not None:
        print("build_compare: the control's audio differs, so the audio proves nothing")
        return 2
    return 0 if changed is None else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("old")
    parser.add_argument("new", nargs="?", default=str(ROOT / "pc/build/boot64/rood_battle_probe"))
    parser.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--vram-stream", action="store_true", help="--menus: each sampled frame also carries the VRAM stream's running digest (RPC vram_stream; both builds need it)")
    parser.add_argument("--casts", nargs="?", const="spells", help="the wider lane: cast every spell (spells[,arts])")
    parser.add_argument("--cast-limit", type=int, default=0, help="--casts: stop after this many actions")
    parser.add_argument("--old-width", type=int, default=4, help="bytes of a pointer in OLD's drop nodes")
    parser.add_argument("--new-width", type=int, default=8, help="the same in NEW")
    parser.add_argument("--width", type=int, default=8, help=argparse.SUPPRESS)
    parser.add_argument("--play", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--play-casts", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--play-audio", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--audio", choices=["on", "off"], default="on", help="the sound lane after the fight: the room's recorded mix, old, old again and new, byte for byte")
    parser.add_argument("--menus", nargs="?", const="ending,menuf", help="the menus' lane: the routes to play (see the top of this file)")
    parser.add_argument("--menu-sample", type=int, default=MENU_SAMPLE, help="--menus: vsyncs between display hashes")
    parser.add_argument("--claims", choices=["on", "off"], default="on", help="--menus: rood.classic's claims on or off")
    parser.add_argument("--new-env", action="append", default=[], metavar="KEY=VALUE", help="--menus: set in NEW's game (a dev flag such as ROOD_CLASSIC_ITEMS=1)")
    parser.add_argument("--env", action="append", default=[], help=argparse.SUPPRESS)
    parser.add_argument("--renderer", choices=["vulkan", "null"], default="vulkan", help="--menus: null hashes the CPU reference frame (the original drawing, no GPU; no ui_text)")
    parser.add_argument("--setting", action="append", default=[], metavar="'KEY VALUE'", help="--menus: a settings.cfg line (ui_text original)")
    parser.add_argument("--aspect", choices=["4:3", "16:9"], default="4:3", help="--menus: the display's aspect")
    parser.add_argument("--stream", action="store_true", help="also compare every vsync's drawing digest (RPC stream: the words the walk draws, native marker draws, environments); fight, --rooms and --menus lanes. OLD must have the RPC")
    parser.add_argument("--rooms", help="the fight in each of these rooms, zone,room;zone,room")
    parser.add_argument("--play-menus", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--out", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.stream:
        os.environ["BUILD_COMPARE_STREAM"] = "1"
        global STREAM
        STREAM = True
    if args.vram_stream:
        os.environ["BUILD_COMPARE_VRAM_STREAM"] = "1"
    if args.play:
        return play(args.old, args.seconds, args.card, args.out, args.width)
    if args.play_audio:
        return play_audio(args.old, args.card, args.out)
    if args.play_casts:
        return play_casts(args.old, args.card, args.out, args.casts, args.cast_limit)
    if args.play_menus:
        return play_menus(args.old, args.card, args.out, args.menus.split(","), args.menu_sample, args.claims, args.aspect, args.env, args.renderer, args.setting)
    if args.rooms:
        return compare_rooms(args)
    if args.menus:
        return compare_menus(args)
    if args.casts:
        return compare_casts(args)
    first, control, new = run(args.old, args, args.old_width), run(args.old, args, args.old_width), run(args.new, args, args.new_width)
    noise, changed = differing(first, control), differing(first, new)
    stream = stream_verdict("fight", first.get("stream"), control.get("stream"), new.get("stream")) if STREAM else 0
    if stream == 2:
        return 2
    print(f"control (old against old): {len(noise)} of {len(first['frames'])} frames differ")
    print(f"old against new: {len(changed)} of {len(first['frames'])} frames differ{f' (first at {changed[0] / 4:.2f} s)' if changed else ''}")
    print(f"casts: old {len(first['casts'])}, new {len(new['casts'])}; the same: {first['casts'] == new['casts']}")
    print(f"drops: old {len(first['drops'])}, new {len(new['drops'])}; the same: {first['drops'] == new['drops']}")
    state_noise = compare_states(first, control)
    state_changed = compare_states(first, new)
    if state_noise is not None:
        print("build_compare: the control's states differ, so the states prove nothing")
        return 2
    if noise:
        print("build_compare: the control differs, so this proves nothing")
        return 2
    if not first["casts"]:
        print("build_compare: no enemy cast in this run: extend --seconds")
        return 2
    if not first["drops"]:
        print("build_compare: no enemy dropped anything")
        return 2
    verdict = 0 if not stream and not changed and first["casts"] == new["casts"] and first["drops"] == new["drops"] and state_changed is None else 1
    if args.audio == "on":
        sound = compare_audio(args)
        verdict = verdict or sound
    return verdict


if __name__ == "__main__":
    sys.exit(main())
