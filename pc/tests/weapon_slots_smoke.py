#!/usr/bin/env python3
"""The optional weapon slots (pc/src/weapon_slots.c, ported from eremtsov's
QoL mod): off they change nothing; on, they swap weapons and shields from
the L2 + R2 overlay and keep the mod's save format.

    python3 pc/tests/weapon_slots_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Continues from a copy of the card (never the card itself; skipped without
one) in the save room, then:
  - the card is an unmodded save: every weapon's and shield's 4th gem byte is 0
  - option off: L2 + R2 opens the game's own shortcut overlay (field mode 6,
    no weapon labels); R2 + Select changes no slot; the inventory is unchanged
  - option on, before any input: still no slot and an unchanged inventory
    (no corruption of an unmodded save)
  - R2 + Select puts the equipped weapon into slot 1 and the equipped shield's
    slot bits get bit 0 (bytes at the mod's offsets: weapons[i].gems[3],
    shields[i].gems[3])
  - a weapon given slot 2 in RAM (as the mod writes it) is equipped by
    L2 + R2 + Triangle: inventory flags, Ashley's battle record (weapon and
    shield) and the model category follow; slot 2 has no shield, so the
    shield comes off
  - assign mode (Select, then Square) moves the equipped weapon to slot 3
  - Circle (slot 1) swaps back: the weapon and its shield again
  - Start writes the equipped shield into every occupied one-handed slot
  - the real D-pad (Up = slot 5) equips a two-handed weapon: no shield
  - with the weapon drawn (battle mode) a swap sheathes and draws again
  - R2 + Select takes the equipped weapon out of its slot and puts it back
    into the first free one
  - saved to the card copy (DATA > SAVE > slot 1 > file 1) and continued in a
    new game process with the option off: every slot byte is as saved."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402

WEAPON, SHIELD, BLADE = 32, 48, 44  # inventory record sizes (vs_main_inventory_t)
SHIELDS, BLADES = 0x100, 0x280  # offsets in vs_main_inventory
INVENTORY_BYTES = 0x540  # weapons, shields, blades
SLOT_BYTE, SHIELD_SLOT_BYTE = 7, 0x2F  # gems[3] of a weapon / shield record


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def two_handed(category):
    return (0x1D4 >> (category - 1)) & 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "weapon_slots"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    os.environ.pop("ROOD_SETTINGS", None)  # the option's default (off), not the user's settings
    inventory = symbols()["vs_main_inventory"]
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    def launch(name):
        game = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env)
        return game

    game = launch("game")
    try:
        state = game.state()
        if (state["zone"], state["room"]) != (9, 17) or not state.get("player"):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1

        def ws(**params):
            return game.call("weapon_slots", **params)

        def raw():
            return game.read(inventory, INVENTORY_BYTES)

        def slot_bytes():
            b = raw()
            return ([b[i * WEAPON + SLOT_BYTE] for i in range(8)],
                    [b[SHIELDS + i * SHIELD + SHIELD_SLOT_BYTE] for i in range(8)])

        def shot(name):
            if args.out:
                game.present_screenshot(out / f"{name}.png")

        def overlay(button=None, settle=25, hold=40):
            """Hold L2 + R2 (the overlay slides in), then press `button`."""
            game.call("input", buttons=["l2", "r2"], vsyncs=hold)
            game.run(settle)
            if button:
                game.call("input", buttons=[button], vsyncs=3)
                game.run(6)

        def r2_select():
            game.call("input", buttons=["r2"], vsyncs=20)
            game.run(5)
            game.call("input", buttons=["select"], vsyncs=3)
            game.run(30)

        def wait_swap(limit=400):
            w = ws()
            for _ in range(limit // 6):
                if not w["loading"] and w["field_mode"] == 1:
                    break
                game.run(6)
                w = ws()
            game.run(20)
            return ws()

        before = raw()
        weapons = [before[i * WEAPON:(i + 1) * WEAPON] for i in range(8)]
        shields = [before[SHIELDS + i * SHIELD:SHIELDS + (i + 1) * SHIELD] for i in range(8)]
        blades = [before[BLADES + i * BLADE:BLADES + (i + 1) * BLADE] for i in range(16)]
        owned = [i for i in range(8) if weapons[i][1]]
        category = {i: blades[weapons[i][1] - 1][3] for i in owned}
        one_handed = [i for i in owned if not two_handed(category[i])]
        both = [i for i in owned if two_handed(category[i])]
        w = ws()
        start = w["equipped"]
        shield = next((i for i in range(8) if shields[i][1]), None)
        print(f"weapons {owned} (categories {category}), equipped {start}, shield {shield}")
        if len(owned) < 3 or not one_handed or not both or shield is None or start not in one_handed:
            print("SKIP: the card needs 3 weapons (one-handed equipped, one two-handed) and an equipped shield")
            return 77
        check(all(weapons[i][SLOT_BYTE] == 0 for i in range(8)) and all(s[SHIELD_SLOT_BYTE] == 0 for s in shields),
              "unmodded card: no weapon slot or shield slot bits")

        # Option off: the game as it was.
        check(w["enabled"] == 0, "the option is off by default")
        game.call("input", buttons=["l2", "r2"], vsyncs=60)
        game.run(30)
        w = ws()
        shot("off-l2-r2")
        check(w["field_mode"] == 6 and w["overlay"] == 0,
              f"off: L2 + R2 opens the game's own shortcut overlay (mode {w['field_mode']}, weapon labels {w['overlay']})")
        game.run(40)
        r2_select()
        check(raw() == before, "off: R2 + Select and L2 + R2 change no inventory byte")

        # On, before any input: an unmodded save stays as it is.
        ws(enabled=True)
        game.run(60)
        check(raw() == before, "on: the unmodded inventory is unchanged (no slots, no corruption)")

        # R2 + Select: the equipped weapon into the first free slot.
        r2_select()
        slots, bits = slot_bytes()
        check(slots[start] == 1 and slots.count(0) == 7, f"R2 + Select: equipped weapon {start} in slot 1 ({slots})")
        check(bits[shield] == 0x01 and bits.count(0) == 7,
              f"R2 + Select: the equipped shield {shield} belongs to slot 1 (bits {bits})")

        # Slot 2 as the mod writes it, then L2 + R2 + Triangle.
        other = next(i for i in one_handed if i != start) if len(one_handed) > 1 else both[0]
        game.write(inventory + other * WEAPON + SLOT_BYTE, bytes([2]))
        overlay(settle=25, hold=60)
        w = ws()
        shot("overlay")
        check(w["overlay"] == 1 and w["field_mode"] == 6, f"on: L2 + R2 shows the weapon slots (mode {w['field_mode']})")
        game.call("input", buttons=["triangle"], vsyncs=3)
        w = wait_swap()
        check(w["equipped"] == other and w["actor_weapon"] == weapons[other][0]
              and w["actor_category"] == category[other],
              f"Triangle (slot 2): weapon {other} equipped, Ashley's record and model follow "
              f"(equipped {w['equipped']}, record {w['actor_weapon']}, category {w['actor_category']})")
        check(w["actor_shield"] == 0 and not any(s["equipped"] for s in w["shields"]),
              f"slot 2 has no shield: the shield came off (record {w['actor_shield']})")

        # Assign mode: Select, then Square (slot 3): the weapon moves.
        overlay("select", hold=60)
        check(ws()["assign"] == 1, "Select: assign mode")
        shot("assign")
        game.call("input", buttons=["square"], vsyncs=3)
        game.run(50)
        slots, bits = slot_bytes()
        check(slots[other] == 3 and slots[start] == 1 and slots.count(0) == 6 and ws()["assign"] == 0,
              f"assign mode + Square: weapon {other} moved from slot 2 to slot 3 ({slots})")

        # Circle (slot 1): back to the first weapon and its shield.
        overlay("circle")
        w = wait_swap()
        check(w["equipped"] == start and w["actor_weapon"] == weapons[start][0]
              and w["actor_shield"] == shields[shield][0] and w["shields"][shield]["equipped"],
              f"Circle (slot 1): weapon {start} and shield {shield} again "
              f"(record {w['actor_weapon']}, shield {w['actor_shield']})")

        # Start: the equipped shield into every occupied one-handed slot.
        overlay("start")
        game.run(30)
        slots, bits = slot_bytes()
        expect = 0
        for i in owned:
            if slots[i] and not two_handed(category[i]):
                expect |= 1 << (slots[i] - 1)
        check(bits[shield] == expect, f"Start: shield {shield} slot bits {bits[shield]:#x} (expected {expect:#x})")

        # The real D-pad: Up (slot 5) equips the two-handed weapon, no shield.
        heavy = both[0]
        game.write(inventory + heavy * WEAPON + SLOT_BYTE, bytes([5]))
        overlay("up")
        w = wait_swap()
        check(w["equipped"] == heavy and w["actor_weapon"] == weapons[heavy][0] and w["actor_shield"] == 0,
              f"D-pad Up (slot 5): two-handed weapon {heavy}, no shield (shield {w['actor_shield']})")

        # Drawn: Circle enters battle mode; the swap sheathes and draws again.
        game.press("circle", vsyncs=3, settle=60)
        drawn = ws()["weapon_drawn"]
        overlay("square")
        w = wait_swap()
        shot("drawn")
        check(drawn == 1 and w["equipped"] == other and w["weapon_drawn"] == 1,
              f"drawn: weapon {other} equipped and drawn again (drawn before {drawn}, after {w['weapon_drawn']})")
        game.press("cross", vsyncs=3, settle=60)

        # R2 + Select: out of its slot, then into the first free slot (2).
        r2_select()
        slots, bits = slot_bytes()
        check(slots[other] == 0 and not any(b & 0x04 for b in bits),
              f"R2 + Select: weapon {other} out of slot 3, its shield bit cleared ({slots}, {bits})")
        r2_select()
        slots, bits = slot_bytes()
        check(slots[other] == 2, f"R2 + Select again: weapon {other} in the first free slot, 2 ({slots})")
        saved = slot_bytes()

        # Save to the card copy: DATA > SAVE > MEMORY CARD slot 1 > file 1 > YES.
        game.press("triangle", vsyncs=3, settle=60)
        for _ in range(4):
            game.press("down", vsyncs=3, settle=10)
        game.press("circle", vsyncs=3, settle=60)  # DATA
        game.press("circle", vsyncs=3, settle=150)  # SAVE
        game.press("circle", vsyncs=3, settle=150)  # MEMORY CARD slot 1
        game.press("circle", vsyncs=3, settle=90)  # file 1: overwrite?
        game.press("up", vsyncs=3, settle=10)
        game.press("circle", vsyncs=3, settle=300)  # YES
        shot("saved")
    finally:
        game.close()

    # A new process, the option off (the unmodded game): the slots as saved.
    game = launch("reload")
    try:
        state = game.state()
        b = game.read(inventory, INVENTORY_BYTES)
        loaded = ([b[i * WEAPON + SLOT_BYTE] for i in range(8)],
                  [b[SHIELDS + i * SHIELD + SHIELD_SLOT_BYTE] for i in range(8)])
        check(state.get("player") is not None and loaded == saved,
              f"saved and continued: slots {loaded[0]}, shield bits {loaded[1]} (saved {saved[0]}, {saved[1]})")
        w = game.call("weapon_slots")
        check(w["enabled"] == 0 and w["equipped"] == other,
              f"reloaded with the option off: weapon {other} equipped, nothing else changed")
    finally:
        game.close()

    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
