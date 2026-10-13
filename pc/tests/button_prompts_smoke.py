#!/usr/bin/env python3
"""button_prompts (pc/src/button_prompts.c; README "2D / UI"): the PS1 buttons
the game shows drawn as the bound key or gamepad button.

    python3 pc/tests/button_prompts_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

Needs a memory card with a save (a copy is used): the save room's menu,
Battle Abilities, whose information box names the circle, triangle and
square buttons in its text (font pictographs), then the quick manual, whose
page keys are the 16 x 16 button sprites. Pixel checks are frame-exact: a
frame is recorded under an option (64 vsyncs, whole cursor cycles), the game
stays paused, and that one recorded frame is drawn again (RPC ui_upscale
redraw) with the icons and with the PS1 glyphs. What each prompt was drawn
as comes from the game (RPC button_prompts shown), not from comparing
different frames (the scene behind the menu animates). Anti-aliasing is off
so the boxes can be checked to the pixel. Checks:

- default: button_prompts auto, the device the keyboard (no gamepad);
- original: a frame recorded with it uses no icon (no atlas lookup, no box,
  nothing shown) and that same frame is the same picture with the icons
  switched on; again after every other mode;
- xbox, playstation, nintendo (circle = A, the east button) and keyboard
  (key caps: circle = Z): the same frame changes, only inside the icon boxes
  (RPC ui_upscale mask), no region falls back, and each prompt shows the
  family's icon for its binding;
- auto follows the device: xbox (as a gamepad would report it) shows the
  xbox icons, the keyboard key caps;
- rebinding: bind_key_circle c shows C, bind_pad_circle south the Xbox A,
  the defaults the first icons again;
- the quick manual's button sprites likewise; internal resolution 1 draws
  the icons as well;
- native menus' images (pc_prompt_glyph); settings save and load;
- no crash, shader error or validation message.

--shots keeps side-by-side images (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game  # noqa: E402

failures = []


def check(ok, what):
    print(("PASS: " if ok else "FAIL: ") + what)
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


class Shot:
    """A frame recorded with some prompt options, drawn twice from the same
    recording (RPC ui_upscale redraw): with the icons, and with the PS1
    glyphs (original: the atlas is not sampled). The game is paused between
    the two, so they are the same game frame (frame-exact); also where icons
    were drawn (RPC ui_upscale mask) and which icon each prompt on screen
    was drawn as (RPC button_prompts shown)."""

    def __init__(self, game, work, name, **prompts):
        game.call("button_prompts", clear_shown=True, **prompts)
        game.call("ui_upscale", mask=str(work / "mask.pgm"))  # clear
        game.run(RECORD_VSYNCS)  # frames recorded with these options
        self.info = game.call("button_prompts")
        game.call("ui_upscale", mask=str(work / "mask.pgm"))
        mask = np.asarray(Image.open(work / "mask.pgm")) > 0
        self.icons, area, self.scale = redraw(game, work, name)
        mode = self.info["button_prompts"]
        game.call("button_prompts", button_prompts="original")
        self.original, _, _ = redraw(game, work, name + "-original")
        game.call("button_prompts", button_prompts=mode)
        x0, y0, x1, y1 = area
        self.boxes = int(mask[y0:y1, x0:x1].sum())
        grown = ndimage.binary_dilation(mask[y0:y1, x0:x1], iterations=1)  # boxes round outwards
        self.mask = np.repeat(np.repeat(grown, self.scale, axis=0), self.scale, axis=1)
        self.changed = differs(self.original, self.icons)
        self.outside = int((np.any(self.original != self.icons, axis=2) & ~self.mask).sum())


# Frames recorded per option (the last is drawn again): whole 16-frame
# cursor cycles, so the menu settles the same way each time.
RECORD_VSYNCS = 64


def redraw(game, work, label):
    path = work / f"{label}.ppm"
    info = game.call("ui_upscale", redraw=str(path))
    image = np.asarray(Image.open(path))
    path.unlink()
    scale = image.shape[1] // 1024
    x, y, w, h = info["area"]
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, 1024), min(y + h, 512)
    return image[y0 * scale:y1 * scale, x0 * scale:x1 * scale], (x0, y0, x1, y1), scale


def differs(a, b):
    return int(np.any(a != b, axis=2).sum())


def save_pair(shots, name, shot):
    if not shots:
        return
    a, b = shot.original, shot.icons
    side = np.concatenate([a, np.full((a.shape[0], 8, 3), 255, np.uint8), b], axis=1)
    image = Image.fromarray(side)
    if image.width > 1600:
        image = image.resize((1600, image.height * 1600 // image.width), Image.LANCZOS)
    image.save(shots / f"{name}.png")


def check_shot(shot, name, what):
    check(shot.changed > 0 and shot.outside == 0,
          f"{name}: {what} changes {shot.changed} pixels of the same frame, none outside the icon boxes "
          f"({shot.outside})")


def check_original(game, work, name):
    """Original: a frame recorded with the PS1 glyphs uses no icon (no atlas
    lookup, nothing drawn from icons, no boxes), and drawing that same frame
    with the icons switched on changes nothing (its prompts carry no icon)."""
    stats0 = game.call("ui_upscale")
    s = Shot(game, work, name + "-original", button_prompts="original")
    stats1 = game.call("ui_upscale")
    shown = s.info["shown"]
    check(stats1["lookups"] == stats0["lookups"] and s.boxes == 0 and not shown["text"] and not shown["sprite"],
          f"{name}: original sends nothing through the 2D atlas ({stats1['lookups'] - stats0['lookups']} lookups, "
          f"{s.boxes} box texels, shown {shown})")
    game.call("button_prompts", button_prompts="xbox")
    again, _, _ = redraw(game, work, name + "-original-xbox")
    game.call("button_prompts", button_prompts="original")
    check(s.changed == 0 and np.array_equal(again, s.original),
          f"{name}: the original frame is the same picture whatever the option ({s.changed}, "
          f"{differs(again, s.original)} pixels)")


# The icon each family shows for the PS1 buttons on screen (default bindings).
EXPECTED = {
    "xbox": {"circle": "xbox_button_b", "triangle": "xbox_button_y", "square": "xbox_button_x"},
    "playstation": {"circle": "playstation_button_circle", "triangle": "playstation_button_triangle",
                    "square": "playstation_button_square"},
    "nintendo": {"circle": "switch_button_a", "triangle": "switch_button_x", "square": "switch_button_y"},
    "keyboard": {"circle": "keyboard_z", "triangle": "keyboard_s", "square": "keyboard_a"},
}


def compare_modes(game, work, name, shots, kind, buttons):
    """The buttons on screen (`buttons`, drawn as `kind`: text or sprite)
    under every mode."""
    check_original(game, work, name)
    shot = {}
    for mode in EXPECTED:
        before = game.call("button_prompts")
        s = shot[mode] = Shot(game, work, f"{name}-{mode}", button_prompts=mode)
        check_shot(s, name, mode)
        save_pair(shots, f"{name}-{mode}", s)
        drawn = s.info["regions"] - before["regions"]
        check(s.info["failed"] == before["failed"],
              f"{name}: {mode}: {drawn} new {kind} regions drawn from icons, none fell back")
        want = {b: EXPECTED[mode][b] for b in buttons}
        check(s.info["shown"][kind] == want, f"{name}: {mode} shows {s.info['shown'][kind]} (want {want})")
    # auto: the device used last.
    s = Shot(game, work, name + "-auto-xbox", button_prompts="auto", device="xbox")
    check(s.info["family"] == "xbox" and s.info["shown"][kind] == shot["xbox"].info["shown"][kind]
          and s.changed > 0 and s.outside == 0, f"{name}: auto after a gamepad (xbox) draws the xbox icons")
    s = Shot(game, work, name + "-auto-keyboard", device="keyboard")
    check(s.info["family"] == "keyboard" and s.info["shown"][kind] == shot["keyboard"].info["shown"][kind]
          and s.changed > 0 and s.outside == 0, f"{name}: auto after the keyboard draws key caps")
    # Rebinding: the icon shown follows the binding.
    s = Shot(game, work, name + "-rebound", bind={"bind_key_circle": "c"})
    check(s.info["glyphs"]["circle"] == "keyboard_c" and s.info["shown"][kind].get("circle") == "keyboard_c"
          and s.changed > 0 and s.outside == 0,
          f"{name}: bind_key_circle c shows C (shown {s.info['shown'][kind].get('circle')})")
    save_pair(shots, f"{name}-keyboard-rebound", s)
    s = Shot(game, work, name + "-pad-rebound", button_prompts="xbox", bind={"bind_pad_circle": "south"})
    check(s.info["shown"][kind].get("circle") == "xbox_button_a" and s.outside == 0,
          f"{name}: bind_pad_circle south shows the Xbox A (shown {s.info['shown'][kind].get('circle')})")
    s = Shot(game, work, name + "-defaults", bind={"bind_key_circle": "z", "bind_pad_circle": "east"})
    check(s.info["shown"][kind] == shot["xbox"].info["shown"][kind] and s.outside == 0,
          f"{name}: the default bindings show the first xbox icons again")
    check_original(game, work, name + "-again")
    return shot


def launch(work, card, label, extra=None):
    settings = work / "settings.cfg"
    env = {"ROOD_OUT": str(work), "ROOD_SETTINGS": str(settings), "ROOD_BUTTON_PROMPTS": "", **(extra or {})}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env.setdefault("ROOD_VK_VALIDATE", "1")
    return Game.launch(socket_path=f"/tmp/rood-prompts-{os.getpid()}-{label}.sock", log=str(work / f"{label}.log"),
                       extra_env=env, card=str(card), fast=True)


def log_clean(game, label):
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log and "[gl] shader" not in log and "[vk] validation:" not in log
          and "[gl] link" not in log and "cannot load font" not in log and "button_prompts: cannot" not in log,
          f"{label}: no crash, shader error, validation message or missing icon font")
    if "[vk] validation:" in log or "[crash]" in log:
        print("\n".join(line for line in log.splitlines() if "validation" in line or "crash" in line)[:4000])


def run(work, card, shots):
    copy = work / "card.mcd"
    shutil.copyfile(card, copy)
    game = launch(work, copy, "menu")
    settings = work / "settings.cfg"
    try:
        p = game.call("button_prompts")
        check(p["button_prompts"] == "auto" and p["family"] == "keyboard",
              f"default: button_prompts auto, the keyboard without a gamepad ({p['button_prompts']}, {p['family']})")
        game.call("button_prompts", button_prompts="original")
        # No anti-aliasing: SMAA blends along whole edges, so an icon would
        # change pixels beyond its box; the boxes are checked exactly.
        game.call("graphics", interpolate=False, aa="off")
        game.wait_title()
        game.run(90)
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        if not check(bool(game.state().get("player")), "in the save room"):
            return
        game.press("triangle", vsyncs=3, settle=60)
        game.press("circle", vsyncs=3, settle=60)  # Battle Abilities: the information box names ○, △ and □
        compare_modes(game, work, "chain", shots, "text", ["circle", "triangle", "square"])
        # Internal resolution 1: icons too (blocks of the 2x atlas).
        scale = game.call("graphics")["scale"]
        game.call("graphics", scale=1)
        game.run(8)
        s = Shot(game, work, "scale1", button_prompts="xbox")
        check(s.scale == 1 and s.changed > 0 and s.outside == 0,
              f"internal resolution 1: icons drawn inside their boxes ({s.changed} pixels)")
        save_pair(shots, "scale1-xbox", s)
        game.call("button_prompts", button_prompts="original")
        game.call("graphics", scale=scale)
        game.run(8)
        # The quick manual: the button sprites.
        game.press("cross", vsyncs=3, settle=60)
        for _ in range(7):  # Battle Abilities down to Quick Manual
            game.press("down", vsyncs=3, settle=12)
        game.press("circle", vsyncs=3, settle=90)
        game.press("circle", vsyncs=3, settle=90)
        compare_modes(game, work, "manual", shots, "sprite", ["circle", "square"])
        # Native menus' images (pc_prompt_glyph): one per button and size,
        # redrawn when the device changes.
        a = game.call("button_prompts", button_prompts="xbox", icon="circle", size=32)
        b = game.call("button_prompts", button_prompts="keyboard", icon="start", size=32)
        check(a["icon"]["texture"] and a["icon"]["h"] == 32 and a["icon"]["w"] > 0 and a["icon"]["device"] == 2
              and b["icon"]["texture"] and b["icon"]["device"] == 4 and b["icon"]["generation"] != a["icon"]["generation"],
              f"native menus: pc_prompt_glyph images ({a['icon']}, {b['icon']})")
        # Settings: the mode and changed bindings persist.
        game.call("button_prompts", button_prompts="nintendo", bind={"bind_key_cross": "space"})
        game.call("settings", action="save")
        text = settings.read_text()
        check("button_prompts nintendo" in text and "bind_key_cross space" in text
              and "bind_key_circle" not in text, "settings: button_prompts and the changed binding saved")
        game.call("button_prompts", button_prompts="original", bind={"bind_key_cross": "x"})
        game.call("settings", action="load")
        p = game.call("button_prompts")
        check(p["button_prompts"] == "nintendo" and p["bindings"]["cross"]["keys"] == ["space"],
              "settings: they load back")
        game.call("button_prompts", button_prompts="original", bind={"bind_key_cross": "x"})
        game.run(4)
    finally:
        game.close()
    log_clean(game, "menu")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--card", type=pathlib.Path, default=default_card())
    parser.add_argument("--shots", type=pathlib.Path)
    args = parser.parse_args()
    if not args.card or not args.card.exists():
        print("SKIP: no memory card with a save (pc/save/card1.mcd or --card)")
        return 77
    if args.shots:
        args.shots.mkdir(parents=True, exist_ok=True)
    work = OUT / "button-prompts"
    work.mkdir(parents=True, exist_ok=True)
    (work / "settings.cfg").unlink(missing_ok=True)
    run(work, args.card, args.shots)
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("PASS: button_prompts_smoke")
    return 0


if __name__ == "__main__":
    sys.exit(main())
