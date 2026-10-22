#!/usr/bin/env python3
"""The right-stick camera in menus that leave the scene in view
(pc/src/camera_stick.c pc_camera_stick_menu, setting camera_stick_menus).

    python3 pc/tests/camera_stick_menus_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Continues from a copy of the card (skipped without one) into its save room
and checks, in the classic look and the modern one (ui_menus 1):

- the Triangle menu's top level (context "command", listed by default; the
  modern shell's Lea Monde tab, its rows over the scene):
  the same 60 vsyncs run twice from one in-memory checkpoint (RPC snapshot
  mark / rewind), once with the right stick held (RPC input right_stick)
  and once without: with it the follow camera's yaw (the game's own
  vs_battle_cameraCurrentSpherical) turns and the picture changes (classic:
  the modern shell animates in the host's time, which no rewind takes back); the
  game's state is the same either way (GAME_STATE: the field and camera
  modes, the menu's state, cursor rows and text boxes, the game time, the
  random numbers, the bag) and the camera module reports the context
  allowed and placements made;
- closing the menu (Triangle) leaves the yaw where the stick put it, back in
  the field;
- a full-screen menu (ITEMS, context "items", not listed; the modern
  shell's Items tab): the stick changes nothing (yaw, picture, state);
- camera_stick_menus none: the top level ignores the stick too;
- a chest's loot (the save room's own chest, RPC chest; context "chest"):
  the stick turns the camera while its list is up, the state as without.

--out keeps evidence frames (game imagery: keep it out of git)."""
import argparse
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402

# The game's state the stick must not touch (symbol, bytes). The camera's own
# values (vs_battle_cameraCurrentSpherical, the scratch camera) are left out.
GAME_STATE = [("D_800F196C", 4), ("_cameraMode", 4), ("vs_battle_menuState", 8), ("D_800F4E98", 0x50),
              ("vs_battle_submenuStates", 16), ("vs_battle_submenuState", 4), ("vs_battle_textBoxes", 0x200),
              ("vs_battle_textBoxStatuses", 0x40), ("textBoxSelector", 4), ("vs_main_gametime", 4),
              ("vs_gametime_tickspeed", 4), ("randArr", 0x184), ("randIndex", 4), ("vs_main_inventory", 0xF00),
              ("vs_main_inventoryIndices", 0xB0), ("vs_main_mapStatus", 0x100)]
SPHERICAL = "vs_battle_cameraCurrentSpherical"
HOLD = 60  # vsyncs the stick is held (or not)
CLOSED = 0x3F

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


class Run:
    def __init__(self, game, out):
        self.g, self.out, self.sym = game, out, symbols()

    def u8(self, name, offset=0):
        return self.g.read(self.sym[name] + offset, 1)[0]

    def u32(self, name):
        return struct.unpack("<I", self.g.read(self.sym[name], 4))[0]

    def yaw(self):
        return struct.unpack("<i", self.g.read(self.sym[SPHERICAL], 4))[0] & 0xFFF

    def menu(self):
        return self.u8("vs_battle_menuState")

    def state(self):
        return {name: self.g.read(self.sym[name], size).hex() for name, size in GAME_STATE}

    def picture(self, name):
        path = self.out / f"{name}.png"
        self.g.present_screenshot(path)
        return np.asarray(Image.open(path).convert("RGB")).astype(int)

    def wait(self, pred, what, limit=900, step=5):
        for _ in range(0, limit, step):
            if pred():
                return True
            self.g.run(step)
        return check(False, f"{what} (within {limit} vsyncs; field mode {self.u32('D_800F196C')}, "
                            f"menu {self.menu():#x})")

    def twice(self, label, stick):
        """The same HOLD vsyncs from one checkpoint without and with the stick: (yaw, state, picture) each."""
        self.g.mark()
        runs = []
        for held in (False, True):
            if held:
                self.g.rewind()
            before = self.g.call("camera_stick")
            if held:
                self.g.call("input", right_stick=stick, vsyncs=HOLD)
            self.g.run(HOLD)
            self.g.run(30)  # (the stick released: the turn eases out)
            after = self.g.call("camera_stick")
            runs.append((self.yaw(), self.state(), self.picture(f"{label}-{'stick' if held else 'still'}"),
                         before, after))
        return runs


def menu_check(r, label, stick, allowed, picture=True):
    """picture: also compare the two runs' pictures (not with the modern menus: their shell animates in the
    host's time, which a rewind does not take back)."""
    (yaw0, state0, pic0, _, _), (yaw1, state1, pic1, before, after) = r.twice(label, stick)
    diff = float(np.abs(pic0 - pic1).mean()) if picture else (99.0 if allowed else 0.0)  # (99: not compared)
    changed = [k for k in state0 if state0[k] != state1[k]]
    check(not changed, f"{label}: the game's state the same with and without the stick (differs: {changed})")
    if allowed:
        check(yaw1 != yaw0 and diff > 1.0,
              f"{label}: the stick turns the camera (yaw {yaw0:#x} -> {yaw1:#x}; picture changes {diff:.2f})")
        check(after["menu_allowed"] == 1 and after["menu"] == label.split()[0]
              and after["menu_places"] > before["menu_places"],
              f"{label}: context {after['menu']} allowed, placed {after['menu_places'] - before['menu_places']} times")
    else:
        check(yaw1 == yaw0 and diff < 0.05,
              f"{label}: the stick does nothing (yaw {yaw0:#x} -> {yaw1:#x}; picture changes {diff:.3f})")
        check(after["menu_places"] == before["menu_places"], f"{label}: nothing placed (context {after['menu']}, "
                                                             f"allowed {after['menu_allowed']})")
    return yaw1


def open_command(r):
    r.g.press("triangle", vsyncs=3, settle=10)
    r.wait(lambda: r.u32("D_800F196C") == 5 and r.menu() == 0, "the Triangle menu open")
    r.g.run(60)  # (slid in)


def close_to_field(r):
    for _ in range(6):
        if r.menu() == CLOSED and r.u32("D_800F196C") == 1:
            break
        r.g.press("triangle", vsyncs=3, settle=40)
    r.wait(lambda: r.menu() == CLOSED and r.u32("D_800F196C") == 1, "back in the field")
    r.g.run(20)


def shell_tab(r, tab):
    """The modern menus' shell on tab `tab` (R1 steps through the tabs)."""
    for _ in range(10):
        if r.g.call("ui_menu").get("tab") == tab:
            break
        r.g.press("r1", vsyncs=3, settle=6)
    r.g.run(20)
    return check(r.g.call("ui_menu").get("tab") == tab, f"the shell's {tab} tab")


def pick_items(r, modern):
    """The top level's ITEMS row: the cursor moved down from MAGIC (rows: MAGIC, BATTLE ABILITIES, ITEMS); in
    the modern menus the shell's Items tab (R1)."""
    if modern:
        return shell_tab(r, "items")
    for _ in range(2):
        r.g.press("down", vsyncs=3, settle=12)
    r.g.press("circle", vsyncs=3, settle=10)
    return r.wait(lambda: r.menu() == 0x44, "ITEMS open (currentMenu 0x44)")


def session(args, out, modern):
    look = "modern" if modern else "classic"
    card = out / f"card-{look}.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    # (no path-traced light: its history is the host's, not rewound with the game, so the two runs' pictures
    # would differ by its noise; the camera alone changes them here)
    env = {"ROOD_OUT": str(out), "ROOD_SETTINGS": str(out / f"settings-{look}.cfg"), "ROOD_WINDOW": "640x480",
           "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    game = launch_at("save_room", card=card, fast=True, log=out / f"{look}.log", extra_env=env)
    r = Run(game, out)
    try:
        game.call("graphics", interpolate=False)
        game.call("ui_menu", modern=modern)
        cs = game.call("camera_stick")
        check(cs["enabled"] == 1 and "command" in cs["menus"].split() and "items" not in cs["menus"].split(),
              f"{look}: defaults: on, the command menu listed, items not ({cs['menus']!r})")
        game.run(30)

        # The top level: the stick turns the camera, the game's state as without.
        open_command(r)
        if modern:  # (the shell opens on Ashley, full screen: its Lea Monde rows are over the scene)
            shell_tab(r, "lea_monde")
        menu_check(r, f"command ({look})", [1, 0], True, not modern)
        yaw = r.yaw()
        close_to_field(r)
        check(r.yaw() == yaw, f"{look}: closing the menu leaves the camera where the stick put it "
                              f"({r.yaw():#x}, {yaw:#x})")

        # A full-screen menu: nothing.
        open_command(r)
        if pick_items(r, modern):
            game.run(60)
            menu_check(r, f"items ({look})", [-1, 0], False, not modern)
        close_to_field(r)

        # Listed nowhere: the top level ignores it too.
        game.call("camera_stick", menus="none")
        open_command(r)
        if modern:
            shell_tab(r, "lea_monde")
        menu_check(r, f"command ({look}, camera_stick_menus none)", [1, 0], False, not modern)
        close_to_field(r)
        game.call("camera_stick", menus="default")

        if not modern:
            # A chest's loot (the camera framed on the chest; field mode 9).
            opened = game.call("chest", open=0).get("opened")
            if check(bool(opened), f"{look}: the save room's chest opens"):
                r.wait(lambda: r.menu() == 0x0C, "the chest's loot list open", limit=900)
                game.run(60)
                menu_check(r, "chest", [-1, 0], True)
                yaw = r.yaw()
                for _ in range(6):
                    if r.menu() != 0x0C:
                        break
                    game.press("cross", vsyncs=3, settle=60)
                r.wait(lambda: r.u32("D_800F196C") == 1, "the chest's loot closed, back in the field")
                check(r.yaw() == yaw, f"chest: back in the field the yaw is the stick's ({r.yaw():#x}, {yaw:#x})")
        log = pathlib.Path(game.log).read_text(errors="replace")
        check("[crash]" not in log, f"{look}: no crash")
    finally:
        game.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", type=pathlib.Path, default=default_card())
    parser.add_argument("--out", type=pathlib.Path)
    args = parser.parse_args()
    if not args.card.exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = args.out or OUT / "camera_stick_menus"
    out.mkdir(parents=True, exist_ok=True)
    for modern in (False, True):
        session(args, out, modern)
    if not args.out:
        for p in out.glob("*.png"):
            p.unlink()
    print(f"{len(failures)} check(s) failed" if failures else "PASS: camera_stick_menus_smoke")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
