#!/usr/bin/env python3
"""The original Magic menu's LV picker (MENU0 84.c _warlockMagicMenu,
levelledSpell) with a levelled Warlock spell, modern menus off.

    python3 pc/tests/menu0_levelled_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Needs a card whose first save is in the save room (skipped otherwise). 4:3,
960x720 window. After Continue: the Magic command on (vs_main_settings
.menuFlags bit 0), Explosion LV1 and LV2 learned (vs_main_actions[106] and
[107] .unlocked), LV3 and LV4 not, MP enough.

The picker draws "--" for the levels not learned. For those the game
overwrites the box sprite's texpage word with 0x64602000, so the GPU reads a
sprite from the middle of the packet whose size word is the box's UV/CLUT
word (0x37FE70C0); the PS1 GPU keeps 10 / 9 bits of it (192x510, off screen).
The port's GPU stopped there ("sprite dimensions") before it masked the size
like the hardware (pc/src/boot_runtime.c).

For LV1 and then LV2: Triangle, Magic, Warlock, the Explosion row, Circle:
the LV picker (cursorMemories[12] the level, [13] = 2 levels), drawn: the
boxes up to the chosen level lit (yellow), the others not, the game still
running; Left / Right to the level, Circle: targeting (D_800F196C 2, menu
closed) with the hand-off D_800F4E98 = (unk2 7, the level's action id),
executeAbilityType 6, returnState 0x41; Circle on the target: MP drops by
that level's cost.
--out keeps screenshots (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import struct
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

CLOSED = 0x3F
MENU0 = 0x41       # currentMenu while the original Magic screen runs from the top level
TARGETING = 2      # D_800F196C while a spell's target is chosen
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (vs_battle_menuItems, characterState)

ACTION, ACTION_COST, ACTION_UNLOCKED = 52, 3, 13  # vs_action_t
MENU_FLAGS = 2                 # vs_main_settings.menuFlags (bit 0: the Magic command)
MP, MP_MAX = 0x1C, 0x1E        # vs_battle_actor2 currentMP / maxMP
EXPLOSION = 106                # LV1; LV2..LV4 are 107..109
MAGIC_ITEM, WARLOCK_ITEM, SPELL_ITEM = 0, 10, 20  # MENU0's menu item ids
LEVEL_MEMORY = 12              # cursorMemories[12] the picker's level, [13] how many


def s8(v):
    return v - 256 if v > 127 else v


class Session:
    def __init__(self, out, card, env, shots):
        self.out, self.shots = out, shots
        self.g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
        self.sym = symbols()

    def close(self):
        self.g.close()

    def ptr(self, address):
        return int.from_bytes(self.g.read(address, PTR), "little")

    def menu(self):
        return self.g.read(self.sym["vs_battle_menuState"], 1)[0]

    def action_byte(self, i, offset):
        return self.sym["vs_main_actions"] + i * ACTION + offset

    def learned(self, i):
        return bool(self.g.read(self.action_byte(i, ACTION_UNLOCKED), 1)[0] & 0x80)

    def set_learned(self, i, on):
        a = self.action_byte(i, ACTION_UNLOCKED)
        v = self.g.read(a, 1)[0]
        self.g.write(a, bytes([(v | 0x80) if on else (v & 0x7F)]))

    def cost(self, i):
        return s8(self.g.read(self.action_byte(i, ACTION_COST), 1)[0])

    def actor2(self):
        actor = self.ptr(self.sym["vs_battle_characterState"])
        return int.from_bytes(self.g.read(actor + 0x3C, 4), "little")

    def mp(self):
        return struct.unpack("<h", self.g.read(self.actor2() + MP, 2))[0]

    def fill_mp(self, need):
        a = self.actor2()
        mp, mp_max = struct.unpack("<hh", self.g.read(a + MP, 4))
        top = max(mp_max, need)
        self.g.write(a + MP, struct.pack("<hh", top, top))
        self.g.run(2)

    def levels(self):
        cm = self.g.read(self.sym["D_800F4EE8"], 52)
        return cm[LEVEL_MEMORY], cm[LEVEL_MEMORY + 1]

    def targeting(self):
        return self.g.read(self.sym["D_800F196C"], 1)[0] == TARGETING

    def exec_fields(self):
        e = self.g.read(self.sym["D_800F4E98"], 8)
        ms = self.g.read(self.sym["vs_battle_menuState"], 3)
        unk2, ability = struct.unpack("<hi", e[2:8])
        return unk2, ability, ms[2], ms[1]

    def wait(self, pred, limit, what, step=1, quiet=False):
        for n in range(0, limit + 1, step):
            if pred():
                return n
            self.g.run(step)
        if quiet:
            return None
        raise TimeoutError(f"{what}: not after {limit} vsyncs (currentMenu {self.menu():#x})")

    def menu_items(self):
        p = self.ptr(self.sym["vs_battle_menuItems"])
        if not p:
            return {}
        raw = self.g.read(p, 64 * 40)
        return {i: (raw[i * 64], raw[i * 64 + 6]) for i in range(40) if raw[i * 64]}

    def wait_ready(self, want, limit=400):
        calm = 0

        def ready():
            nonlocal calm
            items = self.menu_items()
            ok = want in items and all(s <= 1 for s, _ in items.values())
            calm = calm + 1 if ok else 0
            return calm >= 3
        self.wait(ready, limit, f"menu item {want} ready", step=4)
        self.g.run(4)

    def selected(self, ids):
        items = self.menu_items()
        return next((i for i in ids if i in items and items[i][1]), None)

    def nav(self, ids, target):
        for _ in range(len(ids) + 2):
            if self.selected(ids) == target:
                return
            self.g.press("down", vsyncs=3, settle=6)
        if self.selected(ids) != target:
            raise TimeoutError(f"item {target} not reached (selected {self.selected(ids)})")

    def frame(self, name):
        """The next presented image as an RGB array (and a PNG with --out)."""
        ppm = self.out / f"{name}.ppm"
        ppm.unlink(missing_ok=True)
        self.g.call("present_dump", path=str(ppm))
        for _ in range(200):
            if ppm.exists() and ppm.stat().st_size:
                try:
                    image = Image.open(ppm).convert("RGB")
                    break
                except OSError:
                    pass
            self.g.run(1)
        else:
            raise RpcError("no frame presented")
        if self.shots:
            image.save(self.out / f"{name}.png")
        ppm.unlink()
        return np.asarray(image).astype(int)


def lit(image, level):
    """The fraction of yellow pixels inside LV box `level` (PS1 96 + level * 32, 100, 32x16;
    the 320x224 picture fills the 4:3 frame)."""
    h, w = image.shape[:2]
    box = image[int(101 * h / 224):int(115 * h / 224), int((98 + level * 32) * w / 320):int((126 + level * 32) * w / 320)]
    r, g, b = box[..., 0], box[..., 1], box[..., 2]
    return float(((r > 120) & (g > 80) & (b < r - 50)).mean())


def cast(s, check, level, spells):
    g = s.g
    want = EXPLOSION + level
    cost = s.cost(want)
    s.fill_mp(cost + 10)
    for _ in range(4):  # Triangle is ignored while an action still runs
        g.press("triangle", vsyncs=3, settle=10)
        if s.wait(lambda: MAGIC_ITEM in s.menu_items(), 60, "", step=4, quiet=True) is not None:
            break
    s.wait_ready(MAGIC_ITEM)
    s.nav(range(0, 10), MAGIC_ITEM)
    g.press("circle", vsyncs=3, settle=10)
    s.wait_ready(WARLOCK_ITEM)
    s.nav(range(10, 15), WARLOCK_ITEM)
    g.press("circle", vsyncs=3, settle=10)
    s.wait_ready(SPELL_ITEM)
    g.run(10)
    s.nav(range(SPELL_ITEM, SPELL_ITEM + len(spells)), SPELL_ITEM + spells.index(EXPLOSION))
    g.press("circle", vsyncs=3, settle=4)
    s.wait(lambda: s.levels()[1] == 2, 60, "the LV picker")
    for _ in range(4):
        cur = s.levels()[0]
        if cur == level:
            break
        g.press("right" if cur < level else "left", vsyncs=3, settle=6)
    g.run(20)  # the boxes fly in
    image = s.frame(f"picker-lv{level + 1}")
    cur, count = s.levels()
    boxes = [round(lit(image, i), 2) for i in range(4)]
    check(s.menu() == MENU0 and (cur, count) == (level, 2),
          f"LV{level + 1}: the picker is up (currentMenu {s.menu():#x}), level {cur}, {count} levels")
    check(all(boxes[i] > 0.5 for i in range(level + 1)) and all(boxes[i] < 0.1 for i in range(level + 1, 4)),
          f"LV{level + 1}: lit boxes {boxes} (LV1..LV{level + 1} lit, the rest not)")
    g.press("circle", vsyncs=3, settle=1)
    n = s.wait(lambda: s.targeting() and s.menu() == CLOSED, 300, "", step=2, quiet=True)
    unk2, ability, kind, ret = s.exec_fields()
    check(n is not None, f"LV{level + 1}: targeting, the menu closed ({s.menu():#x})")
    check((unk2, ability, kind, ret) == (7, want, 6, MENU0),
          f"LV{level + 1} hand-off: unk2 {unk2}, executeAbility {ability} ({want}), type {kind}, return {ret:#x}")
    g.run(20)
    # Circle on the target until MP drops (MP regenerates meanwhile: the step where it goes down)
    last, drop = [s.mp()], []

    def dropped():
        v = s.mp()
        if v < last[0]:
            drop.append(last[0] - v)
        last[0] = v
        return bool(drop)
    for _ in range(6):
        g.press("circle", vsyncs=3, settle=4)
        if s.wait(lambda: not s.targeting() or dropped(), 40, "", step=2, quiet=True) is not None:
            break
    s.wait(dropped, 120, "", quiet=True)
    check(drop == [cost], f"LV{level + 1} confirmed: MP dropped by {drop} (cost {cost})")
    s.wait(lambda: s.menu() == CLOSED and not s.targeting(), 600, "the spell ends", step=10, quiet=True)
    g.run(60)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "menu0_levelled").resolve()
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    if not os.environ.get("ROOD_OUT"):  # Continue reads the card only with ROOD_OUT set (run.py sets it)
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    s = Session(out, card, env, bool(args.out))
    try:
        g = s.g
        g.call("ui_menu", modern=False)
        state = g.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1
        f = s.sym["vs_main_settings"] + MENU_FLAGS
        g.write(f, struct.pack("<H", struct.unpack("<H", g.read(f, 2))[0] | 1))
        for level in range(4):
            s.set_learned(EXPLOSION + level, level < 2)
        g.run(2)
        spells = [i for i in g.read(s.sym["vs_battle_warlockSpellIds"], 18) if s.learned(i)]
        for level in (0, 1):
            cast(s, check, level, spells)
        log = (out / "game.log").read_text(errors="replace")
        check("[UNIMPLEMENTED]" not in log and g.status()["vsync"] > 0, "the game is still running")
    finally:
        s.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
