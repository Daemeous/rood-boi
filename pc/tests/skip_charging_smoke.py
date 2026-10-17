#!/usr/bin/env python3
"""Battle charging skip (settings qol_skip_charging, pc/src/charge_skip.c and
charge_skip.inc): the hold of an action's wind-up before it takes effect
(BATTLE's action state D_800F196C 2: _cameraMode 4, the camera's move to the
caster and the name window, runs as it is; 5 casting and 6 flash are taken
out), the name window kept up past the release, and nothing else changes.

    python3 pc/tests/skip_charging_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Needs a card whose first save is in the save room (skipped otherwise).
Continues it (fast), warps with enemies to zone 9 room 18 (two enemies that
keep their distance; one comes within Solid Shock's range and waits) and
saves a save state there, then one in zone 48 room 10 (enemies that cast).
Once with the option off and once on (Skip), a game paced at 59.94 Hz like
play (not --fast) resumes the first state, learns Solid Shock, fills MP
(99), maps a defence ability, pins the random numbers (RPC skip_charging
pin: seeded whenever an action ends, so what follows rolls the same however
many vsyncs the wind-up took) and, through the modern menus:

  - casts Solid Shock (Magick > Warlock) on the enemy in range,
  - uses a Cure Root (Items > Misc, the first row) on Ashley,
  - casts a Break Art (the equipped category's first, learned in RAM; the
    enemies healed first, so nothing dies and drops loot),
  - uses another Cure Root with the Information option off,
  - then lets the battle run on.

Each wind-up is stepped in one go from the confirming press, then vsync by
vsync until the action has ended. Checks, per action:
  - off: one wind-up of more than 100 vsyncs, all presented;
  - skip: the camera's move (_cameraMode 4) as off's, as many vsyncs, all
    presented and more than one; the hold after it (5 and 6) at most 32
    vsyncs (8 to 12 seen), none presented; the whole wind-up at most half
    off's vsyncs and wall time (both reported);
  - the view at the release's first vsync (the camera's look-at, position,
    distance and yaw) and the caster's facing are off's (a Break Art's cast
    animation turns the caster 2/4096 while it plays: VIEW_SLACK,
    FACING_SLACK);
  - the name window: off and skip open it once, in the wind-up, with the
    same text, which holds the action's name (vs_main_actions[id].name);
    skipped, it is held past the release and shown there for the setting's
    time (qol_skip_charging_name, 1 s here; NAME_SLACK) unless the action
    ends first; with the Information option off neither shows one;
  - the same outcome at the action's end: the HP, MP and statuses of every
    actor, MP -25 for Solid Shock, the Cure Root's healing and the stack one
    less, the art's HP cost, the event flags 0xB7 / 0xB9, the score data;
  - afterwards clean: BATTLE back in state 1, _cameraMode 0, no effects in
    either list, no name window, D_800F522C 0, the object data slots empty,
    vs_gametime_tickspeed back, the free heap off's.
After the battle ran on: every actor's HP, MP, statuses and position the
same. Then the casters' room (who: all, Ashley standing by; unpaced, --fast:
no wall time is checked there, only what happened): the enemies cast
the same spells with the same names shown, their holds skipped and not
presented, their camera moves presented, and every enemy action on Ashley
opens the defence window as off. (Their order may differ
over longer runs: the enemies' own timing counts the vsyncs their wind-ups
took.) --out keeps the logs."""
import argparse
import os
import pathlib
import shutil
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402

ROOM = (9, 18)
CASTERS = (48, 10)  # Beggars of the Mouthharp: two enemies that cast at Ashley from afar
CASTERS_RUN = 3600  # vsyncs they get
APPROACH = 676  # vsyncs after the warp: an enemy waits within Solid Shock's range
SOLID_SHOCK, CURE_ROOT = 97, 323
ACTION, ACTION_COST, ACTION_UNLOCKED = 52, 3, 13  # vs_action_t
HP, MP, STATUSES = 0x18, 0x1C, 0x948  # vs_battle_actor2 currentHP, currentMP, statuses
MISC = 0xE00  # vs_main_inventory: the misc bag (id u16, count u8, index u8)
SCORE = 0x114  # sizeof vs_main_scoredata_t
FLAGS = (0xB7, 0xB9)  # vs_main_stateFlags set by func_8007138C
OBJECT_SLOT = 0x14  # sizeof vs_battle_objectData
ACTOR_POSITION = 0x1C  # D_800F4538_t unk0.position (facing at 0x26)
CLOSED = 0x3F
FIELD_MS = 1001 / 60  # one NTSC field
PIN = 4242  # the random number pin's seed
RUN_ON = 1800  # vsyncs the battle runs on after the actions
WIND_UP = 150  # vsyncs stepped at once after the confirming press
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers
FIRST_ART = 0xB8  # Break Arts: 0xB8..0xDF, four per weapon category
ARTS_LEARNED = 0  # vs_main_artsStatus.artsLearned[12]
MAPPED_DEFENSE = 16  # vs_main_settings.mappedDefenseAbilities[4]
INFORMATION = 8  # vs_main_settings.information
ACTION_NAME = 28  # vs_action_t name[24] (the game's encoding, 0xE7 ends it)
NAME_SECONDS = 1.0  # qol_skip_charging_name (the default)
NAME_SLACK = 4  # vsyncs: the hold's end is checked once a game frame
HOLD_MAX = 32  # vsyncs of _cameraMode 5 and 6 a skipped wind-up may run
# The camera's move to the caster takes as long as off's (the same steps, presented) from the same start. The
# later actions start a little apart (the menus' loading waits take what they take, and the actors move
# meanwhile), so the move may differ by a few game frames (the Break Art's: 12 vsyncs, off 8).
MOVE_SLACK = 8
MODES = ("off", "skip")
ACTIONS = {"spell": "Solid Shock", "item": "the Cure Root", "art": "the Break Art",
           "quiet": "the Cure Root (Information off)"}


# A Break Art's cast animation turns the caster a little while it plays (0.2 degrees, 2/4096, seen):
# skipped, it does not, so the facing (and with it the camera's aim, 1 unit) may differ that much.
VIEW_SLACK, FACING_SLACK = 2 << 12, 8


def same_view(a, b):
    """The release views [lookAt xyz, position xyz, distance, yaw, facing] alike."""
    return (all(abs(x - y) <= VIEW_SLACK for x, y in zip(a[:6], b[:6])) and a[6:8] == b[6:8]
            and abs((a[8] - b[8] + 2048) % 4096 - 2048) <= FACING_SLACK)


def same_places(a, b):
    """Every actor's position the same, its facing within FACING_SLACK."""
    return all((x is None) == (y is None) and (x is None or (x[:3] == y[:3]
               and abs((x[3] - y[3] + 2048) % 4096 - 2048) <= FACING_SLACK)) for x, y in zip(a, b))


def card_default():
    env = os.environ.get("ROOD_TEST_CARD")
    return pathlib.Path(env) if env else ROOT / "pc/build/test-card1.mcd"


class Run:
    """One paced game resumed from the room's save state, the option off or on."""

    def __init__(self, mode, snap, card, env, out, socket, fast=False):
        self.mode = mode
        self.g = Game.launch(card=str(card), log=out / f"{mode}.log", extra_env=env, socket_path=socket,
                             snapshot=snap, fast=fast)
        self.sym = symbols()

    def close(self):
        self.g.close()

    def ptr(self, address):
        return int.from_bytes(self.g.read(address, PTR), "little")

    def u32(self, address):
        return int.from_bytes(self.g.read(address, 4), "little")

    def u8(self, name):
        return self.g.read(self.sym[name], 1)[0]

    def actor2(self, slot):
        actor = self.ptr(self.sym["vs_battle_actors"] + slot * PTR)
        return self.u32(actor + 0x3C) if actor else 0

    def stats(self, slot):
        """(HP, MP, statuses) of the actor in slot `slot`, or None."""
        a2 = self.actor2(slot)
        if not a2:
            return None
        hp, = struct.unpack("<h", self.g.read(a2 + HP, 2))
        mp, = struct.unpack("<h", self.g.read(a2 + MP, 2))
        return hp, mp, self.u32(a2 + STATUSES)

    def actors(self):
        return [self.stats(i) for i in range(16)]

    def positions(self):
        """Every actor's position (x, y, z) and facing (D_800F4538[i]->unk0), or None."""
        out = []
        for i in range(17):
            actor = self.ptr(self.sym["D_800F4538"] + i * PTR)
            out.append(struct.unpack("<3h4xh", self.g.read(actor + ACTOR_POSITION, 12)) if actor else None)
        return out

    def mode_now(self):
        return self.u8("D_800F196C"), self.u8("vs_battle_menuState")

    def camera_mode(self):
        return self.u8("_cameraMode")

    def misc(self, item):
        raw = self.g.read(self.sym["vs_main_inventory"] + MISC, 64 * 4)
        return sum(raw[k * 4 + 2] for k in range(64) if struct.unpack_from("<H", raw, k * 4)[0] == item)

    def flags(self):
        return tuple(self.g.read(self.sym["vs_main_stateFlags"] + f, 1)[0] for f in FLAGS)

    def heap_free(self):
        head = self.sym["heapA"]
        node, total = self.u32(head + 4), 0
        for _ in range(4096):
            if node in (head, 0):
                break
            total += self.u32(node + 8)
            node = self.u32(node + 4)
        return total

    def clean(self):
        """What a finished action leaves (each should be its idle value)."""
        slots = self.g.read(self.sym["vs_battle_objectDataSlots"], 16 * OBJECT_SLOT)
        return {"state": self.u8("D_800F196C"), "camera": self.camera_mode(),
                "effects": self.ptr(self.sym["D_800F53B8"]), "background": self.ptr(self.sym["D_800F54B0"]),
                "name window": self.ptr(self.sym["D_800EB9D8"]), "D_800F522C": self.u32(self.sym["D_800F522C"]),
                "object slots": [slots[i * OBJECT_SLOT] for i in range(16) if slots[i * OBJECT_SLOT]],
                "tickspeed": self.u32(self.sym["vs_gametime_tickspeed"])}

    def ui(self):
        return self.g.call("ui_menu")

    def information(self, on):
        self.g.write(self.sym["vs_main_settings"] + INFORMATION, bytes([1 if on else 0]))

    def action_name(self, action):
        """vs_main_actions[action].name to its terminator, hex."""
        raw = self.g.read(self.sym["vs_main_actions"] + action * ACTION + ACTION_NAME, 24)
        return raw.split(b"\xe7")[0].hex()

    def wait(self, pred, limit, what, step=2):
        for n in range(0, limit + 1, step):
            if pred():
                return n
            self.g.run(step)
        raise TimeoutError(f"{self.mode}: {what}: not after {limit} vsyncs (mode {self.mode_now()})")

    def prepare(self):
        g, sym = self.g, self.sym
        g.call("ui_menu", modern=True)
        g.call("skip_charging", mode=self.mode, who="mine", pin=PIN, name=NAME_SECONDS)
        self.information(True)
        a = sym["vs_main_actions"] + SOLID_SHOCK * ACTION + ACTION_UNLOCKED
        g.write(a, bytes([g.read(a, 1)[0] | 0x80]))
        a2 = self.actor2(0)
        g.write(a2 + MP, struct.pack("<hh", 99, 99))
        for f in FLAGS:
            g.write(sym["vs_main_stateFlags"] + f, b"\0")
        self.map_defense()
        g.run(2)

    def shell(self, tab):
        g = self.g
        for _ in range(6):
            if self.ui().get("visible") and self.mode_now()[1] == 0:
                break
            g.press("triangle", vsyncs=3, settle=4)
            try:
                self.wait(lambda: self.ui().get("visible") and self.mode_now()[1] == 0, 120, "")
            except TimeoutError:
                pass
            g.run(6)
        for _ in range(8):
            if self.ui().get("tab") == tab:
                return
            g.press("r1", vsyncs=3, settle=4)
        raise TimeoutError(f"{self.mode}: the {tab} tab")

    def map_defense(self):
        """A defence ability on Circle (learned), so enemy actions on Ashley open its window."""
        g, sym = self.g, self.sym
        defense = g.read(sym["vs_battle_defenseAbilityOffsets"], 1)[0]
        a = sym["vs_main_actions"] + defense * ACTION + ACTION_UNLOCKED
        g.write(a, bytes([g.read(a, 1)[0] | 0x80]))
        g.write(sym["vs_main_settings"] + MAPPED_DEFENSE, bytes([defense]))

    def ab(self):
        return self.ui().get("abilities") or {}

    def shell_close(self):
        g = self.g
        for _ in range(6):
            if self.mode_now()[1] == CLOSED and not self.ui().get("visible"):
                return
            g.press("triangle", vsyncs=3, settle=4)
            try:
                self.wait(lambda: self.mode_now()[1] == CLOSED and not self.ui().get("visible"), 200, "", step=4)
            except TimeoutError:
                pass
        raise TimeoutError(f"{self.mode}: the shell did not close")

    def learn_art(self):
        """The equipped category's first Break Art learned (the unlocked bit, artsLearned); returns
        (its id, its HP cost). The weapon drawn."""
        g, sym = self.g, self.sym
        self.shell("abilities")
        cat = self.ab()["equipped_category"]
        drawn = self.ab().get("weapon_drawn")
        self.shell_close()  # (the menu's commands are set when it opens)
        art = FIRST_ART + (cat - 1) * 4
        a = sym["vs_main_actions"] + art * ACTION
        g.write(a + ACTION_UNLOCKED, bytes([g.read(a + ACTION_UNLOCKED, 1)[0] | 0x80]))
        g.write(sym["vs_main_artsStatus"] + ARTS_LEARNED + cat % 10, bytes([1]))
        if not drawn:
            g.press("circle", vsyncs=3, settle=60)  # draw the weapon (in the field)
        return art, struct.unpack("b", g.read(a + ACTION_COST, 1))[0]

    def art_menu(self, art):
        """Abilities > Break Arts, the art focused (row 0)."""
        g = self.g
        self.shell("abilities")
        for _ in range(4):
            if self.ab().get("sub") == "arts":
                break
            g.press("r2", vsyncs=3, settle=4)
        if self.ab()["mode"] != "arts":
            g.press("circle", vsyncs=3, settle=4)
            self.wait(lambda: self.ab()["mode"] == "arts", 20, "the arts")
        def first():
            ab = self.ab()
            return ab["categories"][ab["category"] - 1]["arts"][0]
        try:  # ("Loading..." while MAINMENU is not resident yet)
            self.wait(lambda: first()["usable"], 240, "", step=4)
        except TimeoutError:
            pass
        x = first()
        if x["id"] != art or not x["usable"]:
            raise RuntimeError(f"{self.mode}: the art {x['id']} ({art}) is not usable: '{x['reason']}'")

    def act(self, what):
        """Circle on the focused row, then on the target; step the action through in one go. Returns
        the wind-up's counters, the actors before and after, and the score data after."""
        g = self.g
        g.press("circle", vsyncs=3, settle=1)
        self.wait(lambda: self.mode_now() == (2, CLOSED), 300, f"{what}: the target")
        g.run(20)
        before = self.actors()
        g.call("skip_charging", reset=True)
        for _ in range(6):  # the target cursor takes presses once it is up
            g.call("input", buttons=["circle"], vsyncs=3)
            g.run(4)
            if self.camera_mode() not in (1, 3):
                break
            g.run(36)
        # The wind-up in one step (its wall time would include the RPC's pauses between steps): a
        # skipped one is over by then. Then vsync by vsync until the action has ended (BATTLE's state
        # back to 1): the outcome as it is then (HP and MP regenerate in the field after).
        g.run(WIND_UP)
        n = self.wait(lambda: self.mode_now()[0] == 1, 1500, f"{what}: back in the field", step=1)
        g.run(2)  # (a name window held to the action's end closes in the next game frame)
        stats = g.call("skip_charging")
        return {"stats": stats, "end": WIND_UP + n, "before": before, "after": self.actors(), "flags": self.flags(),
                "score": self.g.read(self.sym["vs_main_scoredata"], SCORE), "clean": self.clean(),
                "heap": self.heap_free()}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(card_default()))
    parser.add_argument("--out", help="keep the logs here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "skip_charging").resolve()
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_PREFABS": str(out / "prefabs"),
           "ROOD_LIGHTING": str(out / "lighting"), "ROOD_GI": "0", "ROOD_DETERMINISTIC": "1"}
    (out / "prefabs").mkdir()
    (out / "lighting").mkdir()
    if not os.environ.get("ROOD_OUT"):  # Continue reads the card only with ROOD_OUT set (run.py sets it)
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-charge-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    # The room's save state, made fast.
    snap = out / "room.snap"
    casters_snap = out / "casters.snap"
    g = launch_at("save_room", card=card, fast=True, log=out / "boot.log", extra_env=env, socket_path=socket)
    try:
        if (g.state()["zone"], g.state()["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {g.state()}")
            return 1
        reply = g.call("room_goto", zone=ROOM[0], room=ROOM[1], enemies=True, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(APPROACH)
        st = g.state()
        if not check(reply["ok"] and (st["zone"], st["room"]) == ROOM, f"warped to zone {ROOM[0]} room {ROOM[1]}"):
            return 1
        g.call("snapshot", action="save", path=str(snap))
        # The casters' room's save state.
        reply = g.call("room_goto", zone=CASTERS[0], room=CASTERS[1], enemies=True, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(30)
        st = g.state()
        if not check(reply["ok"] and (st["zone"], st["room"]) == CASTERS,
                     f"warped to zone {CASTERS[0]} room {CASTERS[1]} (casters)"):
            return 1
        g.call("snapshot", action="save", path=str(casters_snap))
    finally:
        g.close()

    results = {}
    for mode in MODES:
        r = Run(mode, snap, card, env, out, socket)
        try:
            r.prepare()
            cost = struct.unpack("b", r.g.read(r.sym["vs_main_actions"] + SOLID_SHOCK * ACTION + ACTION_COST, 1))[0]
            r.shell("magick")
            for _ in range(6):
                if r.ui()["magick"].get("sub") == "warlock":
                    break
                r.g.press("r2", vsyncs=3, settle=4)
            spells = next(s["spells"] for s in r.ui()["magick"]["schools"] if s.get("id") == "warlock")
            row = next(i for i, x in enumerate(spells) if x["id"] == SOLID_SHOCK)
            for _ in range(24):
                if r.ui()["magick"]["cursor"] == row:
                    break
                r.g.press("down", vsyncs=3, settle=4)
            spell = r.act("Solid Shock")
            spell["name"] = r.action_name(SOLID_SHOCK)
            roots = r.misc(CURE_ROOT)
            r.shell("items")
            for _ in range(8):
                if r.ui().get("items", {}).get("category") == "misc":
                    break
                r.g.press("r2", vsyncs=3, settle=4)
            r.g.press("circle", vsyncs=3, settle=4)  # the first row: a Cure Root stack
            for _ in range(10):
                m = r.ui()["items"]
                if m.get("mode") == "actions" and m["actions"][m["action"]]["id"] == "use":
                    break
                r.g.press("down", vsyncs=3, settle=4)
            item = r.act("Cure Root")
            item["roots"] = (roots, r.misc(CURE_ROOT))
            item["name"] = r.action_name(item["stats"]["action"])  # (an item's action: 0xE0 on, its name the use's)
            # A Break Art (the equipped category's first, learned in RAM) on the enemy in range.
            # (The enemies healed first: a kill would drop loot, whose window waits for input.)
            for slot in range(1, 16):
                a2 = r.actor2(slot)
                if a2:
                    r.g.write(a2 + HP, r.g.read(a2 + HP + 2, 2))  # currentHP = maxHP
            art_id, art_cost = r.learn_art()
            r.art_menu(art_id)
            art = r.act("the Break Art")
            art["id"], art["cost"] = art_id, art_cost
            art["name"] = r.action_name(art_id)
            # Another Cure Root, the Information option off: no name window.
            r.information(False)
            r.shell("items")
            for _ in range(8):
                if r.ui().get("items", {}).get("category") == "misc":
                    break
                r.g.press("r2", vsyncs=3, settle=4)
            r.g.press("circle", vsyncs=3, settle=4)
            for _ in range(10):
                m = r.ui()["items"]
                if m.get("mode") == "actions" and m["actions"][m["action"]]["id"] == "use":
                    break
                r.g.press("down", vsyncs=3, settle=4)
            quiet = r.act("Cure Root, Information off")
            quiet["name"] = None
            r.information(True)
            # The enemies' actions skipped too from here (who: all).
            r.g.call("skip_charging", who="all")
            # The battle runs on: the enemies act.
            r.g.call("skip_charging", reset=True)
            r.g.run(RUN_ON)
            later = {"actors": r.actors(), "stats": r.g.call("skip_charging"), "positions": r.positions()}
            results[mode] = {"cost": cost, "spell": spell, "item": item, "art": art, "quiet": quiet, "later": later}
            log = pathlib.Path(r.g.log).read_text(errors="replace")
            check("[UNIMPLEMENTED]" not in log and "[crash]" not in log, f"{mode}: no crash")
        finally:
            r.close()

    off, skip = results["off"], results["skip"]
    for mode in MODES:
        x = results[mode]
        for name in ACTIONS:
            s, before, after = x[name]["stats"], x[name]["before"], x[name]["after"]
            print(f"  {mode:4} {name:5}: {s['vsyncs']} wind-up vsyncs (phases {s['phases']}, presented "
                  f"{s['phases_presented']}) in {s['ms']:.1f} ms, {s['presented']} presented, {s['hidden_vsyncs']} "
                  f"hidden; release view {s['release']}; name window {s['name']}; "
                  f"Ashley {before[0]} -> {after[0]}, enemies {[before[i] for i in (2, 3)]} -> "
                  f"{[after[i] for i in (2, 3)]}; clean {x[name]['clean']}, heap {x[name]['heap']}; "
                  f"action over {x[name]['end']} vsyncs after the confirm" + chr(10) + f"      trace {s['trace']}")
    for name, what in ACTIONS.items():
        o, s = off[name], skip[name]
        so, ss = o["stats"], s["stats"]
        check(so["sequences"] == 1 and so["vsyncs"] > 100 and so["presented"] == so["vsyncs"],
              f"off: {what} winds up once ({so['sequences']}) for {so['vsyncs']} vsyncs (> 100), "
              f"{so['presented']} presented")
        move, hold = ss["phases"][0], ss["phases"][1] + ss["phases"][2]
        check(ss["sequences"] == 1 and abs(move - so["phases"][0]) <= MOVE_SLACK and ss["phases_presented"][0] == move > 1,
              f"skip: {what}'s camera move runs as off's: {move} vsyncs (off {so['phases'][0]}, within {MOVE_SLACK}), "
              f"{ss['phases_presented'][0]} presented")
        check(ss["skipped"] == hold <= HOLD_MAX and sum(ss["phases_presented"][1:]) == 0,
              f"skip: {what}'s hold skipped: {hold} vsyncs (<= {HOLD_MAX}; off {sum(so['phases'][1:])}), "
              f"{sum(ss['phases_presented'][1:])} presented")
        print(f"  {what}: wind-up off {so['vsyncs']} vsyncs / {so['ms']:.0f} ms, skip {ss['vsyncs']} vsyncs / "
              f"{ss['ms']:.0f} ms ({100 * (1 - ss['ms'] / so['ms']):.0f}% of the time saved)")
        check(2 * ss["vsyncs"] <= so["vsyncs"] and 2 * ss["ms"] <= so["ms"],
              f"skip: {what}'s wind-up takes at most half off's: {ss['vsyncs']} vsyncs (off {so['vsyncs']}), "
              f"{ss['ms']:.0f} ms (off {so['ms']:.0f})")
        # The name window: the game's own, in the wind-up; skipped, held past the release.
        for mode, x in (("off", o), ("skip", s)):
            names = [e.split(":") for e in x["stats"]["name"]["names"].split()]
            if x["name"] is None:
                check(not names and x["stats"]["name"]["vsyncs"] == 0,
                      f"{mode}: no name window for {what} ({x['stats']['name']})")
            else:
                action = int(names[0][0]) if names else -1
                check(len(names) == 1 and action == x["stats"]["action"] and x["name"] in names[0][1],
                      f"{mode}: {what}'s name window opened once in its wind-up (action {action}, "
                      f"{x['stats']['action']}) holding its name {x['name']}: {x['stats']['name']['names']}")
        if o["name"] is not None:
            n, want = ss["name"], round(NAME_SECONDS * 60)
            check(ss["name"]["names"] == so["name"]["names"],
                  f"skip: {what}'s name window text is off's ({so['name']['names']})")
            # Shown the setting's time past the release, unless the action ended before (then it closes).
            end = ss["release_vsyncs"]
            check(n["held"] == 1 and n["up"] == 0
                  and (want - NAME_SLACK <= n["after"] <= want + NAME_SLACK or abs(n["after"] - end) <= 2 * NAME_SLACK),
                  f"skip: {what}'s name window held past the release and shown there {n['after']} vsyncs "
                  f"({NAME_SECONDS} s = {want}; the action over {end} vsyncs after the release), closed after")
        check(ss["releases"] == 1 and same_view(ss["release"], so["release"]),
              f"skip: the view and the caster's facing at {what}'s release are off's: {ss['release']} "
              f"(off {so['release']}; look-at and position within {VIEW_SLACK >> 12}, facing within {FACING_SLACK})")
        check(s["before"] == o["before"] and s["after"] == o["after"],
              f"skip: {what}'s outcome is off's (HP, MP and statuses of every actor before and after)")
        check(s["flags"] == o["flags"] and s["score"] == o["score"],
              f"skip: the event flags 0xB7/0xB9 {s['flags']} (off {o['flags']}) and the score data are off's")
        idle = {"state": 1, "camera": 0, "effects": 0, "background": 0, "name window": 0, "D_800F522C": 0,
                "object slots": []}
        for mode, x in (("off", o), ("skip", s)):
            c = x["clean"]
            check(all(c[k] == v for k, v in idle.items()) and c["tickspeed"] == o["clean"]["tickspeed"],
                  f"{mode}: clean after {what}: {c}")
        check(s["heap"] == o["heap"], f"skip: free heap after {what} {s['heap']} (off {o['heap']})")
    _, before, after = None, off["spell"]["before"], off["spell"]["after"]
    # (MP regenerates over the action and the walk back: up to one point a second or so.)
    check(before[0][1] - off["cost"] <= after[0][1] <= before[0][1] - off["cost"] + 15,
          f"off: Solid Shock spent its MP: {before[0][1]} -> {after[0][1]} (cost {off['cost']}, then regenerating)")
    check(any(b and a and a[0] < b[0] for b, a in zip(before[1:], after[1:])),
          f"off: Solid Shock damaged an enemy: {before[1:5]} -> {after[1:5]}")
    before, after, roots = off["item"]["before"], off["item"]["after"], off["item"]["roots"]
    check(after[0][0] > before[0][0] and roots[1] == roots[0] - 1,
          f"off: the Cure Root healed Ashley ({before[0][0]} -> {after[0][0]}) and was used ({roots[0]} -> {roots[1]})")
    check(skip["item"]["roots"] == roots, f"skip: Cure Roots {skip['item']['roots']} (off {roots})")
    check(off["spell"]["flags"][0] == 1, f"off: Solid Shock set the event flag 0xB7 ({off['spell']['flags']})")
    before, after = off["art"]["before"], off["art"]["after"]
    check(after[0][0] == before[0][0] - off["art"]["cost"] and skip["art"]["id"] == off["art"]["id"],
          f"off: the Break Art {off['art']['id']} cost its HP: {before[0][0]} -> {after[0][0]} "
          f"(cost {off['art']['cost']})")
    lo, ls = off["later"], skip["later"]
    for mode, x in (("off", lo), ("skip", ls)):
        print(f"  {mode:4} later: {x['stats']['sequences']} wind-ups (the last by actor {x['stats']['actor']}, "
              f"action {x['stats']['action']}), {x['stats']['skipped']} vsyncs skipped, "
              f"{x['stats']['presented']} presented; actions (actor:action:wound up:window) {x['stats']['ended']}")
    acts = [e.split(":") for e in ls["stats"]["ended"].split()]
    enemy = [e for e in acts if e[0] != "0" and e[2] == "1"]
    check(ls["stats"]["ended"] == lo["stats"]["ended"],
          f"skip (who: all): over {RUN_ON} vsyncs the same actions by the same actors, with the same chain / "
          f"defence windows opened, as off ({len(acts)} actions, {len(enemy)} enemy wind-ups)")
    windows = [e for e in enemy if e[3] == "1"]
    print(f"  enemy wind-ups with a defence window: {len(windows)} of {len(enemy)}")
    if enemy:
        check(ls["stats"]["presented"] == 0 and ls["stats"]["skipped"] == ls["stats"]["vsyncs"],
              f"skip (who: all): the enemies' wind-ups skipped ({ls['stats']['skipped']} of "
              f"{ls['stats']['vsyncs']} vsyncs), none presented ({ls['stats']['presented']})")
    check(ls["actors"] == lo["actors"] and same_places(ls["positions"], lo["positions"]),
          f"skip: {RUN_ON} vsyncs on, every actor's HP, MP, statuses and position are off's"
          + ("" if ls["actors"] == lo["actors"] else f"\n      off  {lo['actors'][:5]}\n      skip {ls['actors'][:5]}")
          + ("" if same_places(ls["positions"], lo["positions"]) else f"\n      off  {lo['positions']}\n      skip {ls['positions']}"))
    # The enemies' wind-ups (who: all), in the casters' room with Ashley standing by.
    casters = {}
    for mode in MODES:
        r = Run(f"casters-{mode}", casters_snap, card, env, out, socket, fast=True)
        try:
            g, sym = r.g, r.sym
            g.call("skip_charging", mode=mode, who="all", pin=PIN, reset=True, name=NAME_SECONDS)
            r.information(True)
            r.map_defense()
            g.press("circle", vsyncs=3, settle=30)  # the weapon drawn: they come
            g.run(CASTERS_RUN)
            casters[mode] = g.call("skip_charging")
            casters[mode]["action_names"] = {
                e.split(":")[0]: r.action_name(int(e.split(":")[0]))
                for e in casters[mode]["name"]["names"].split() if int(e.split(":")[0]) >= 0}
            log = pathlib.Path(g.log).read_text(errors="replace")
            check("[UNIMPLEMENTED]" not in log and "[crash]" not in log, f"casters, {mode}: no crash")
        finally:
            r.close()
    co, cs = casters["off"], casters["skip"]
    for mode, x in (("off", co), ("skip", cs)):
        print(f"  casters {mode:4}: {x['sequences']} wind-ups, {x['vsyncs']} vsyncs ({x['skipped']} skipped, "
              f"{x['presented']} presented, {x['ms']:.0f} ms); actions (actor:action:wound up:window) {x['ended']}"
              + chr(10) + f"      phases {x['phases']} presented {x['phases_presented']}; name windows {x['name']}")
    acts = {m: [e.split(":") for e in x["ended"].split()] for m, x in casters.items()}
    spells = {m: sorted(e[1] for e in a if e[0] != "0" and e[2] == "1") for m, a in acts.items()}
    check(spells["off"] and spells["skip"] == spells["off"],
          f"casters: the enemies cast the same {len(spells['off'])} spells / arts in {CASTERS_RUN} vsyncs "
          f"(skip {spells['skip']}, off {spells['off']}; their order may differ: see the docstring)")
    hold = cs["phases"][1] + cs["phases"][2]
    check(cs["sequences"] > 0 and cs["skipped"] == hold and sum(cs["phases_presented"][1:]) == 0
          and hold <= 16 * cs["sequences"],
          f"skip: the enemies' {cs['sequences']} holds skipped ({cs['skipped']} of {hold} vsyncs, <= 16 each), "
          f"none presented ({sum(cs['phases_presented'][1:])})")
    check(cs["phases_presented"][0] == cs["phases"][0] >= 4 * cs["sequences"]
          and abs(cs["phases"][0] / cs["sequences"] - co["phases"][0] / co["sequences"]) <= 2,
          f"skip: the enemies' camera moves presented ({cs['phases_presented'][0]} of {cs['phases'][0]} vsyncs, "
          f"{cs['phases'][0] / cs['sequences']:.1f} each; off {co['phases'][0] / co['sequences']:.1f})")
    names = {m: sorted(e.split(":")[1] for e in x["name"]["names"].split() if not e.startswith("-"))
             for m, x in casters.items()}
    check(names["off"] and names["skip"] == names["off"] and len(names["skip"]) == cs["sequences"]
          and cs["name"]["held"]
          and all(x["action_names"][e.split(":")[0]] in e.split(":")[1]
                  for x in casters.values() for e in x["name"]["names"].split() if not e.startswith("-")),
          f"casters: the enemies' spells and arts show their names as off ({len(names['skip'])} name windows, "
          f"{cs['name']['opens']} opened for {cs['sequences']} wind-ups, held past the release: {cs['name']['held']})")
    check(all(e[3] == "1" for m in MODES for e in acts[m] if e[0] != "0"),
          "casters: every enemy action on Ashley opened the defence window, off and skip")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
