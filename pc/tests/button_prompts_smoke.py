#!/usr/bin/env python3
"""button_prompts (pc/src/button_prompts.c; README "2D / UI"): the PS1 buttons
the game shows drawn as the bound key or gamepad button.

    python3 pc/tests/button_prompts_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

Needs a memory card with a save (a copy is used): the save room's menu,
Battle Abilities > Chain Abilities, whose information box names the circle,
triangle and square buttons in its text (font pictographs), then the quick
manual, whose page keys are the 16 x 16 button sprites. Each comparison
records the frame under an option (64 vsyncs of the paused menu: whole
cursor cycles) and draws it again (RPC ui_upscale redraw) with the icons and
with the PS1 glyphs, so the two images are the same frame (deterministic);
icon sets are compared inside the boxes both drew:

- default: button_prompts auto, the device the keyboard (no gamepad);
- original: no 2D region goes through the atlas (the original drawing path),
  and original drawn again after the other modes is the same picture;
- forced xbox: the pictographs change, only inside the boxes drawn from icons
  (RPC ui_upscale mask), every prompt region drawn (none fell back), the
  glyphs named are the Xbox ones for the default bindings (circle = B);
- playstation, nintendo (circle = A, the east button) and keyboard (key caps:
  circle = Z) each draw a different picture;
- auto follows the device: the device xbox (as a gamepad would report)
  draws the forced xbox picture, the keyboard the keyboard picture;
- rebinding: bind_key_circle c changes the keyboard picture (circle = C),
  bind_pad_circle south shows the Xbox A; defaults restored draw the first
  pictures again;
- the quick manual's button sprites change with the family too;
- internal resolution 1 draws the icons as well;
- settings: button_prompts and changed bindings save and load;
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
    """The paused game's frame recorded with some prompt options: drawn again
    as recorded (RPC ui_upscale redraw) and with the PS1 glyphs (original:
    the atlas is not sampled), plus where icons were drawn (RPC ui_upscale
    mask). Both images are the same recorded frame."""

    def __init__(self, game, work, name, **prompts):
        self.info = game.call("button_prompts", **prompts)
        game.call("ui_upscale", mask=str(work / "mask.pgm"))  # clear
        game.run(RECORD_VSYNCS)  # frames recorded with these options
        game.call("ui_upscale", mask=str(work / "mask.pgm"))
        mask = np.asarray(Image.open(work / "mask.pgm")) > 0
        self.icons, area, self.scale = redraw(game, work, name)
        mode = self.info["button_prompts"]
        game.call("button_prompts", button_prompts="original")
        self.original, _, _ = redraw(game, work, name + "-original")
        game.call("button_prompts", button_prompts=mode)
        x0, y0, x1, y1 = area
        grown = ndimage.binary_dilation(mask[y0:y1, x0:x1], iterations=1)  # boxes round outwards
        self.mask = np.repeat(np.repeat(grown, self.scale, axis=0), self.scale, axis=1)
        self.changed = differs(self.original, self.icons)
        self.outside = int((np.any(self.original != self.icons, axis=2) & ~self.mask).sum())


# Menus animate their cursor in 16-frame cycles: whole cycles between
# recordings keep the rest of the picture the same.
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


def icons_differ(a, b):
    """Two shots' icon pixels (inside both masks) differ."""
    m = a.mask & b.mask
    return int((np.any(a.icons != b.icons, axis=2) & m).sum())


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
          f"{name}: {what} changes {shot.changed} pixels, none outside the icon boxes ({shot.outside})")


def compare_modes(game, work, name, shots, kinds):
    """The buttons on screen under every mode; `kinds`: what they are."""
    stats0 = game.call("ui_upscale")
    game.call("button_prompts", button_prompts="original")
    game.run(RECORD_VSYNCS)
    original, _, _ = redraw(game, work, name + "-original")
    stats1 = game.call("ui_upscale")
    check(stats1["lookups"] == stats0["lookups"],
          f"{name}: original sends nothing through the 2D atlas ({stats1['lookups'] - stats0['lookups']} lookups)")
    before = game.call("button_prompts")["regions"]
    shot = {"xbox": Shot(game, work, name + "-xbox", button_prompts="xbox")}
    after = game.call("button_prompts")
    check(after["regions"] > before and after["failed"] == 0,
          f"{name}: {after['regions'] - before} {kinds} regions drawn from icons, {after['failed']} fell back")
    g = after["glyphs"]
    check(g["circle"] == "xbox_button_b" and g["triangle"] == "xbox_button_y" and g["square"] == "xbox_button_x"
          and g["cross"] == "xbox_button_a", f"{name}: xbox names B/Y/X/A for circle/triangle/square/cross")
    check(np.array_equal(shot["xbox"].original, original),
          f"{name}: with the icons off the frame is the original picture")
    for mode in ("playstation", "nintendo", "keyboard"):
        shot[mode] = Shot(game, work, f"{name}-{mode}", button_prompts=mode)
    for mode, s in shot.items():
        check_shot(s, name, mode)
        save_pair(shots, f"{name}-{mode}", s)
    g = shot["nintendo"].info["glyphs"]
    check(g["circle"] == "switch_button_a" and g["cross"] == "switch_button_b",
          f"{name}: nintendo shows the east button's A for circle ({g['circle']}), B for cross")
    g = shot["keyboard"].info["glyphs"]
    check(g["circle"] == "keyboard_z" and g["square"] == "keyboard_a",
          f"{name}: keyboard shows key caps (circle {g['circle']}, square {g['square']})")
    names = list(shot)
    distinct = all(icons_differ(shot[a], shot[b]) > 0 for i, a in enumerate(names) for b in names[i + 1:])
    check(distinct, f"{name}: xbox, playstation, nintendo and keyboard draw four different icon sets")
    # auto: the device used last.
    s = Shot(game, work, name + "-auto-xbox", button_prompts="auto", device="xbox")
    check(s.info["family"] == "xbox" and icons_differ(s, shot["xbox"]) == 0,
          f"{name}: auto after a gamepad (xbox) draws the xbox icons")
    s = Shot(game, work, name + "-auto-keyboard", device="keyboard")
    check(s.info["family"] == "keyboard" and icons_differ(s, shot["keyboard"]) == 0,
          f"{name}: auto after the keyboard draws key caps")
    # Rebinding: the glyph shown follows the binding.
    s = Shot(game, work, name + "-rebound", bind={"bind_key_circle": "c"})
    check(s.info["glyphs"]["circle"] == "keyboard_c" and s.outside == 0
          and icons_differ(s, shot["keyboard"]) > 0,
          f"{name}: bind_key_circle c shows C ({s.info['glyphs']['circle']}, "
          f"{icons_differ(s, shot['keyboard'])} icon pixels changed)")
    save_pair(shots, f"{name}-keyboard-rebound", s)
    s = Shot(game, work, name + "-pad-rebound", button_prompts="xbox", bind={"bind_pad_circle": "south"})
    check(s.info["glyphs"]["circle"] == "xbox_button_a" and icons_differ(s, shot["xbox"]) > 0,
          f"{name}: bind_pad_circle south shows the Xbox A ({s.info['glyphs']['circle']})")
    s = Shot(game, work, name + "-defaults", bind={"bind_key_circle": "z", "bind_pad_circle": "east"})
    check(icons_differ(s, shot["xbox"]) == 0, f"{name}: the default bindings draw the first xbox icons")
    game.call("button_prompts", button_prompts="original")
    game.run(RECORD_VSYNCS)
    again, _, _ = redraw(game, work, name + "-original-again")
    check(np.array_equal(again, original), f"{name}: original after the icons draws the same picture")
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
        game.call("graphics", interpolate=False)
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
        compare_modes(game, work, "chain", shots, "pictograph")
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
        compare_modes(game, work, "manual", shots, "sprite")
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
