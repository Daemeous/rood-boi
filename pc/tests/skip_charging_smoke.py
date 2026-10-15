#!/usr/bin/env python3
"""Battle charging (settings qol_skip_charging, pc/src/boot_window.c): a
spell's casting and an item's wind-up before they take effect run at several
game vsyncs per displayed field, and nothing else changes.

    python3 pc/tests/skip_charging_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Needs a card whose first save is in the save room (skipped otherwise).
Continues it (fast), warps with enemies to zone 9 room 18 (two enemies that
keep their distance; one comes within Solid Shock's range and waits) and
saves a save state there. Then, once per mode (off, fast, skip), a game
paced at 59.94 Hz like play (not --fast) resumes that state, learns Solid
Shock, fills MP (99) and, through the modern menus:

  - casts Solid Shock (Magick > Warlock) on the enemy in range,
  - uses a Cure Root (Items > Misc, the first row) on Ashley.

Each action is stepped in one go from the confirming press until the field
is back, and the RPC skip_charging counters say how many vsyncs charged
(BATTLE D_800F196C 2, _cameraMode 4..6, an action with an effect) and their
wall time. Checks:
  - each action charges once, for more than 100 vsyncs; the same number of
    vsyncs in every mode (the game runs every vsync);
  - off: the charging takes about real time (vsyncs x 16.7 ms, +-25 %);
    fast: at most 40 % of off's wall time (4x: 25 %); skip: at most fast's
    (+15 % for a loaded machine);
  - the outcome is the same in every mode: Solid Shock's MP cost (25) and
    the enemy's damage, the Cure Root's healing and the stack one less, the
    vsync count, and the whole game state after both actions (save states
    compared with snapshot_diff.py: guest RAM, scratchpad, I/O, VRAM,
    statics, SPU; ROOD_DETERMINISTIC=1 so the AI's scanline budget is not
    the wall clock's). UNINITIALISED masks the few bytes that differ between
    two identical paced runs of the 64-bit build anyway (stack garbage).
--out keeps the logs."""
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
import rood_rpc  # noqa: E402
import snapshot_diff  # noqa: E402
from rood_rpc import OUT, Game, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402

ROOM = (9, 18)
APPROACH = 676  # vsyncs after the warp: an enemy waits within Solid Shock's range
SOLID_SHOCK, CURE_ROOT = 97, 323
ACTION, ACTION_COST, ACTION_UNLOCKED = 52, 3, 13  # vs_action_t
HP, MP = 0x18, 0x1C  # vs_battle_actor2 currentHP, currentMP (maxMP after it)
MISC = 0xE00  # vs_main_inventory: the misc bag (id u16, count u8, index u8)
CLOSED = 0x3F
FIELD_MS = 1001 / 60  # one NTSC field
RAM = 0x80000000
# Bytes that differ between two identical paced runs on the 64-bit build,
# whatever this option is set to: the effect request func_8007138C builds on
# the stack with fields it never writes, copied into D_800F53C0
# (func_800CF0E8_t, 0xE8 bytes), and the dead part of the guest stack. The
# host's own stack garbage lands there; the game does not read it.
UNINITIALISED = ((0x800F53C0, 0xE8), (0x801F8000, 0x8000))
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (vs_battle_actors, characterState)
MODES = ("off", "fast", "skip")


def card_default():
    env = os.environ.get("ROOD_TEST_CARD")
    return pathlib.Path(env) if env else ROOT / "pc/build/test-card1.mcd"


class Run:
    """One paced game resumed from the room's save state, in one mode."""

    def __init__(self, mode, snap, card, env, out, socket):
        self.mode = mode
        self.g = Game.launch(card=str(card), log=out / f"{mode}.log", extra_env=env, socket_path=socket,
                             snapshot=snap)
        self.sym = symbols()

    def close(self):
        self.g.close()

    def ptr(self, address):
        return int.from_bytes(self.g.read(address, PTR), "little")

    def u32(self, address):
        return int.from_bytes(self.g.read(address, 4), "little")

    def stats(self, slot):
        """(HP, MP) of the actor in slot `slot`, or None."""
        actor = self.ptr(self.sym["vs_battle_actors"] + slot * PTR)
        if not actor:
            return None
        a2 = self.u32(actor + 0x3C)
        return struct.unpack("<h", self.g.read(a2 + HP, 2))[0], struct.unpack("<h", self.g.read(a2 + MP, 2))[0]

    def actors(self):
        return [self.stats(i) for i in range(16)]

    def mode_now(self):
        return self.g.read(self.sym["D_800F196C"], 1)[0], self.g.read(self.sym["vs_battle_menuState"], 1)[0]

    def camera_mode(self):
        return self.g.read(self.sym["_cameraMode"], 1)[0]

    def misc(self, item):
        raw = self.g.read(self.sym["vs_main_inventory"] + MISC, 64 * 4)
        return sum(raw[k * 4 + 2] for k in range(64) if struct.unpack_from("<H", raw, k * 4)[0] == item)

    def ui(self):
        return self.g.call("ui_menu")

    def wait(self, pred, limit, what, step=2):
        for n in range(0, limit + 1, step):
            if pred():
                return n
            self.g.run(step)
        raise TimeoutError(f"{self.mode}: {what}: not after {limit} vsyncs (mode {self.mode_now()})")

    def prepare(self):
        g, sym = self.g, self.sym
        g.call("ui_menu", modern=True)
        g.call("skip_charging", mode=self.mode)
        a = sym["vs_main_actions"] + SOLID_SHOCK * ACTION + ACTION_UNLOCKED
        g.write(a, bytes([g.read(a, 1)[0] | 0x80]))
        a2 = self.u32(self.ptr(sym["vs_battle_characterState"]) + 0x3C)
        g.write(a2 + MP, struct.pack("<hh", 99, 99))
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

    def act(self, what):
        """Circle on the focused row, then on the target; step the action through in one go. Returns
        (charging counters, actors before, actors after)."""
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
        # The action and the camera's return to the field, in one step (the counters' wall time
        # would include the RPC's pauses between steps).
        g.run(720)
        stats = g.call("skip_charging")
        self.wait(lambda: self.mode_now()[0] == 1, 600, f"{what}: back in the field", step=10)
        return stats, before, self.actors()


def state_diff(a_path, b_path):
    """snapshot_diff's differences between two save states with UNINITIALISED masked, and how many masked
    bytes differed."""
    a, b = snapshot_diff.read(a_path), snapshot_diff.read(b_path)
    ram_a, ram_b = a["memory"].get(RAM), b["memory"].get(RAM)
    masked = 0
    if ram_a is not None and ram_b is not None and len(ram_a) == len(ram_b):
        ram = bytearray(ram_b)
        for start, size in UNINITIALISED:
            lo, hi = start - RAM, start - RAM + size
            masked += sum(1 for p, q in zip(ram_a[lo:hi], ram[lo:hi]) if p != q)
            ram[lo:hi] = ram_a[lo:hi]
        b["memory"][RAM] = bytes(ram)
    return snapshot_diff.diff(a, b, snapshot_diff.Symbols(rood_rpc.BINARY)), masked


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
            end = out / f"end-{mode}.snap"
            r.g.call("snapshot", action="save", path=str(end))
            results[mode] = {"cost": cost, "spell": spell, "item": item, "roots": (roots, r.misc(CURE_ROOT)),
                             "vsync": r.g.status()["vsync"],
                             "state": end}
            log = pathlib.Path(r.g.log).read_text(errors="replace")
            check("[UNIMPLEMENTED]" not in log and "[crash]" not in log, f"{mode}: no crash")
        finally:
            r.close()

    for mode in MODES:
        x = results[mode]
        for name, (stats, before, after) in (("Solid Shock", x["spell"]), ("Cure Root", x["item"])):
            print(f"  {mode:4} {name:11}: {stats['vsyncs']} charging vsyncs in {stats['ms']:.0f} ms "
                  f"({stats['ms'] / FIELD_MS:.0f} fields); Ashley {before[0]} -> {after[0]}, "
                  f"enemies {[before[i] for i in (2, 3)]} -> {[after[i] for i in (2, 3)]}")
    off = results["off"]
    for name in ("spell", "item"):
        stats = off[name][0]
        check(stats["sequences"] == 1 and stats["vsyncs"] > 100,
              f"off: the {name} charges once ({stats['sequences']}), {stats['vsyncs']} vsyncs (> 100)")
        real = stats["vsyncs"] * FIELD_MS
        check(abs(stats["ms"] - real) < 0.25 * real,
              f"off: the {name}'s charging takes real time: {stats['ms']:.0f} ms (~{real:.0f})")
        for mode in ("fast", "skip"):
            s = results[mode][name][0]
            check(s["sequences"] == 1 and s["vsyncs"] == stats["vsyncs"],
                  f"{mode}: the {name} charges once ({s['sequences']}) for the same {s['vsyncs']} vsyncs "
                  f"({stats['vsyncs']})")
        fast, skip = results["fast"][name][0]["ms"], results["skip"][name][0]["ms"]
        check(fast <= 0.4 * stats["ms"],
              f"fast: the {name}'s charging in {fast:.0f} ms, {fast / stats['ms']:.0%} of off's (<= 40 %)")
        check(skip <= 1.15 * fast,
              f"skip: the {name}'s charging in {skip:.0f} ms, {skip / stats['ms']:.0%} of off's (<= fast's)")
    # The outcome.
    _, before, after = off["spell"]
    # (MP regenerates over the action and the walk back: up to one point a second or so.)
    check(before[0][1] - off["cost"] <= after[0][1] <= before[0][1] - off["cost"] + 15,
          f"off: Solid Shock spent its MP: {before[0][1]} -> {after[0][1]} (cost {off['cost']}, then regenerating)")
    check(any(b and a and a[0] < b[0] for b, a in zip(before[1:], after[1:])),
          f"off: Solid Shock damaged an enemy: {before[1:5]} -> {after[1:5]}")
    _, before, after = off["item"]
    check(after[0][0] > before[0][0] and off["roots"][1] == off["roots"][0] - 1,
          f"off: the Cure Root healed Ashley ({before[0][0]} -> {after[0][0]}) and was used "
          f"({off['roots'][0]} -> {off['roots'][1]})")
    for mode in ("fast", "skip"):
        x = results[mode]
        for name in ("spell", "item"):
            check(x[name][1:] == off[name][1:],
                  f"{mode}: the {name}'s outcome is off's (HP/MP of every actor before and after)")
        check(x["roots"] == off["roots"], f"{mode}: Cure Roots {x['roots']} (off {off['roots']})")
        check(x["vsync"] == off["vsync"], f"{mode}: done at vsync {x['vsync']} (off {off['vsync']})")
        lines, masked = state_diff(off["state"], x["state"])
        check(not lines, f"{mode}: the game state after both actions is off's (save states: guest RAM, "
              f"scratchpad, I/O, VRAM, statics, SPU; {masked} uninitialised bytes masked)"
              + "".join("\n      " + line for line in lines[:12]))
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
