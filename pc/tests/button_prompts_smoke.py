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
- the L2 shortcut cross: icons inside their cells, neighbours apart, the
  family's icons and D-pad; its D-pad (button_prompts_dpad, default dim):
  it is drawn with the arms the game reported usable as it drew the labels
  (RPC button_prompts cross_actions; at least one on the save),
  the same frame with every arm lit is exactly the whole D-pad (cross),
  dim and lit change only pixels inside the D-pad's mask (dim only darker);
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
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from menu_nav import ABILITIES_COMMAND, top_command  # noqa: E402
from checkpoints import launch_at  # noqa: E402

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
    check(stats1["lookups"] == stats0["lookups"] and s.boxes == 0 and not shown["text"] and not shown["sprite"]
          and shown["dpad"] is None,
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
        want = {b: EXPECTED[mode][b] + "_outline" for b in buttons}  # the default style, color_outline
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
    check(s.info["glyphs"]["circle"] == "keyboard_c" and s.info["shown"][kind].get("circle") == "keyboard_c_outline"
          and s.changed > 0 and s.outside == 0,
          f"{name}: bind_key_circle c shows C (shown {s.info['shown'][kind].get('circle')})")
    save_pair(shots, f"{name}-keyboard-rebound", s)
    s = Shot(game, work, name + "-pad-rebound", button_prompts="xbox", bind={"bind_pad_circle": "south"})
    check(s.info["shown"][kind].get("circle") == "xbox_button_a_outline" and s.outside == 0,
          f"{name}: bind_pad_circle south shows the Xbox A (shown {s.info['shown'][kind].get('circle')})")
    s = Shot(game, work, name + "-defaults", bind={"bind_key_circle": "z", "bind_pad_circle": "east"})
    check(s.info["shown"][kind] == shot["xbox"].info["shown"][kind] and s.outside == 0,
          f"{name}: the default bindings show the first xbox icons again")
    check_original(game, work, name + "-again")
    return shot


# The L2 shortcut cross (BATTLE.PRG func_800C9FE8): the face-button diamond
# (16 x 16 sprites, diagonal neighbours 9 PS1 pixels apart: their cells
# overlap) and the D-pad (one 30 x 30 sprite), per family in the default
# style (color_outline) and in solid.
CROSS_FACES = {  # PS1 button -> (cell column on the page, glyph)
    "playstation": {"triangle": "playstation_button_triangle", "square": "playstation_button_square",
                    "circle": "playstation_button_circle", "cross": "playstation_button_cross"},
    "xbox": {"triangle": "xbox_button_y", "square": "xbox_button_x", "circle": "xbox_button_b",
             "cross": "xbox_button_a"},
    "nintendo": {"triangle": "switch_button_x", "square": "switch_button_y", "circle": "switch_button_a",
                 "cross": "switch_button_b"},
    "keyboard": {"triangle": "keyboard_s", "square": "keyboard_a", "circle": "keyboard_z", "cross": "keyboard_x"},
}
CROSS_DPAD = {"playstation": "playstation_dpad", "xbox": "xbox_dpad", "nintendo": "switch_dpad",
              "keyboard": "keyboard_arrows"}
CELLS = ["triangle", "square", "circle", "cross"]  # the page's cells at (16 k, 128)


def cross_geometry(game):
    """Where the cross's sprites are on screen now, from the game's tables:
    {button: (x, y)} for the 16 x 16 face sprites and the D-pad's (x, y)."""
    step = game.read_symbol("D_800EBCE8", 1)[0]
    steps = game.read_symbol("vs_battle_rowAnimationSteps", 16)
    slide = int.from_bytes(steps[step:step + 1], "little", signed=True) if step < 16 else None
    table = game.read_symbol("D_800EBC88", 16)
    faces = {}
    for k, button in enumerate(CELLS):
        v = int.from_bytes(table[4 * k:4 * k + 4], "little")
        faces[button] = (160 + (slide or 0) + int.from_bytes((v & 0xffff).to_bytes(2, "little"), "little", signed=True),
                         v >> 16)
    return step, slide, faces, (-(slide or 0) + 0x48, 0x66)


def icon_mask(game, u0, v0, size, factor, scale):
    m = game.call("button_prompts", mask=[u0, v0, u0 + size - 1, v0 + size - 1, factor, scale])["mask"]
    if not m["w"]:
        return None
    return np.array([[c == "1" for c in row] for row in m["rows"]], bool)


def place(canvas_shape, mask, x, y, scale):
    canvas = np.zeros(canvas_shape, bool)
    h, w = mask.shape
    canvas[y * scale:y * scale + h, x * scale:x * scale + w] = mask
    return canvas


def cross_checks(game, work, shots):
    """The shortcut cross: icons inside their cells with a margin, no two
    neighbours touching (at the internal resolution and at 1x), the drawn
    pixels exactly the icons' (frame-exact against a smaller size), the
    family's icons and D-pad, and original unchanged."""
    def held(name, **prompts):
        game.call("input", buttons=["l2"], vsyncs=RECORD_VSYNCS + 40)
        return Shot(game, work, name, **prompts)

    game.call("input", buttons=["l2"], vsyncs=RECORD_VSYNCS * 3)
    check_original(game, work, "cross")
    step, slide, faces, dpad_xy = cross_geometry(game)
    if not check(slide is not None and step != 10, f"cross: the L2 shortcut cross is open (animation step {step})"):
        return
    default = game.call("button_prompts")
    check(default["style"] == "color_outline" and abs(default["scale"] - 0.7) < 1e-6,
          f"cross: default style color_outline, scale 0.7 ({default['style']}, {default['scale']})")
    for style in ["color_outline", "solid"]:
        suffix = "_outline" if style == "color_outline" else ""
        for family in CROSS_FACES:
            name = f"cross-{style}-{family}"
            s = held(name, button_prompts=family, style=style, scale=0.7)
            check_shot(s, name, f"{family} {style}")
            save_pair(shots, name, s)
            want = {b: g + suffix for b, g in CROSS_FACES[family].items()}
            dpad = CROSS_DPAD[family] + ("_none" if suffix else "")
            check(s.info["shown"]["sprite"] == want and s.info["shown"]["dpad"] == dpad,
                  f"{name}: shows {s.info['shown']['sprite']}, D-pad {s.info['shown']['dpad']} (want {want}, {dpad})")
            sc = s.scale
            masks = {b: icon_mask(game, 16 * k, 128, 16, sc, sc) for k, b in enumerate(CELLS)}
            dmask = icon_mask(game, 184, 160, 30, sc, sc)
            if not check(all(m is not None and m.shape == (16 * sc, 16 * sc) for m in masks.values())
                         and dmask is not None, f"{name}: every cross icon has a mask"):
                continue
            # Inside the cell with at least 2 PS1 pixels of margin.
            margin = 2 * sc
            inside = all(not m[:margin].any() and not m[-margin:].any() and not m[:, :margin].any()
                         and not m[:, -margin:].any() and m.any() for m in masks.values())
            dm = 1 * sc
            check(inside and dmask.any() and not dmask[:dm].any() and not dmask[-dm:].any()
                  and not dmask[:, :dm].any() and not dmask[:, -dm:].any(),
                  f"{name}: icons inside their cells with a margin of {margin} pixels (D-pad {dm})")
            # Neighbours apart: no pixel within half a PS1 pixel of another icon.
            shape = s.icons.shape[:2]
            placed = {b: place(shape, masks[b], *faces[b], sc) for b in CELLS}
            grown = {b: ndimage.binary_dilation(p, iterations=max(sc // 2, 1)) for b, p in placed.items()}
            touching = [(a, b) for i, a in enumerate(CELLS) for b in CELLS[i + 1:] if (grown[a] & placed[b]).any()]
            check(not touching, f"{name}: no two face icons within half a PS1 pixel ({touching})")
            # At internal resolution 1 (one texel per pixel) none overlap either.
            small = {b: icon_mask(game, 16 * k, 128, 16, 2, 1)[::2, ::2] for k, b in enumerate(CELLS)}
            small_placed = {b: place((224, 320), small[b], *faces[b], 1) for b in CELLS}
            overlap = [(a, b) for i, a in enumerate(CELLS) for b in CELLS[i + 1:]
                       if (small_placed[a] & small_placed[b]).any()]
            check(not overlap, f"{name}: at internal resolution 1 no two face icons share a pixel ({overlap})")
            # Frame-exact: the same frame with smaller icons differs only
            # where either size's icons are (nothing else is drawn).
            t = held(name + "-small", scale=0.4)
            union = np.zeros(shape, bool)
            for k, b in enumerate(CELLS):
                union |= placed[b] | place(shape, icon_mask(game, 16 * k, 128, 16, sc, sc), *faces[b], sc)
            union |= place(shape, dmask, *dpad_xy, sc) | place(shape, icon_mask(game, 184, 160, 30, sc, sc), *dpad_xy, sc)
            diff = np.any(s.icons != t.icons, axis=2)
            check(diff.any() and not (diff & ~union).any(),
                  f"{name}: scale 0.4 vs 0.7 changes {int(diff.sum())} pixels, all inside the icons' masks "
                  f"({int((diff & ~union).sum())} outside)")
            game.call("button_prompts", scale=0.7)
    dpad_checks(game, work, shots, held, dpad_xy)
    game.call("button_prompts", button_prompts="original", style="color_outline", dpad="dim")
    game.call("input", buttons=["l2"], vsyncs=RECORD_VSYNCS * 2)
    check_original(game, work, "cross-again")
    game.run(90)  # released: the cross closes


ARMS = ["up", "left", "right", "down"]  # button_prompts.c cross_actions bits


def dpad_checks(game, work, shots, held, dpad_xy):
    """The shortcut cross's D-pad modes on one recorded frame per family and
    style: dim (the default) lights the arms the game reported usable, all
    arms lit is the whole D-pad (cross) to the pixel, and dim and lit change
    nothing outside the D-pad's icon (dim only darkens)."""
    check(game.call("button_prompts")["dpad_mode"] == "dim", "cross: default D-pad mode dim")
    for style in ["color_outline", "solid"]:
        for family in CROSS_FACES:
            name = f"dpad-{style}-{family}"
            s = held(name, button_prompts=family, style=style, dpad="dim")
            actions = s.info["cross_actions"]
            lit = [a for k, a in enumerate(ARMS) if actions >> k & 1]
            check(s.info["shown"]["dpad_arms"] == lit and lit, f"{name}: the arms with an action are lit "
                  f"({s.info['shown']['dpad_arms']}, cross_actions {actions})")
            sc = s.scale
            shape = s.icons.shape[:2]

            def dpad_mask():
                return place(shape, icon_mask(game, 184, 160, 30, sc, sc), *dpad_xy, sc)

            dim, dim_mask = s.icons, dpad_mask()
            t = held(name + "-cross", dpad="cross")
            cross, cross_mask = t.icons, dpad_mask()
            check(differs(s.original, t.original) == 0, f"{name}: the frames recorded for each mode are the same "
                  f"frame ({differs(s.original, t.original)} pixels differ in the original drawing)")
            every = held(name + "-all-lit", dpad="dim", cross_actions=15).icons
            check(np.array_equal(every, cross), f"{name}: every arm lit is the whole D-pad to the pixel "
                  f"({differs(every, cross)} pixels differ)")
            t = held(name + "-lit", dpad="lit", cross_actions=-1)
            lit_image, lit_mask = t.icons, dpad_mask()
            game.call("button_prompts", dpad="dim")
            area = ndimage.binary_dilation(dim_mask | cross_mask | lit_mask, iterations=1)
            luma = lambda im: im.astype(int) @ np.array([299, 587, 114])
            for mode, image in (("dim", dim), ("lit", lit_image)):
                diff = np.any(image != cross, axis=2)
                check((diff.any() or actions == 15) and not (diff & ~area).any(),
                      f"{name}: {mode} changes {int(diff.sum())} pixels of the same frame, all inside the D-pad "
                      f"({int((diff & ~area).sum())} outside)")
            # (to an 8-bit level: an edge pixel's coverage blends both the same, rounded once)
            darker = np.any(dim != cross, axis=2)
            check(not darker.any() or (luma(dim)[darker] <= luma(cross)[darker] + 1000).all(),
                  f"{name}: dim only darkens the arms without an action")
            if shots:
                side = np.concatenate([cross, np.full((cross.shape[0], 8, 3), 255, np.uint8), dim,
                                       np.full((cross.shape[0], 8, 3), 255, np.uint8), lit_image], axis=1)
                Image.fromarray(side).save(shots / f"{name}.png")


def launch(work, card, label, extra=None):
    settings = work / "settings.cfg"
    env = {"ROOD_OUT": str(work), "ROOD_SETTINGS": str(settings), "ROOD_BUTTON_PROMPTS": "", **(extra or {})}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env.setdefault("ROOD_VK_VALIDATE", "1")
    return launch_at("save_room", card=card, socket_path=f"/tmp/rood-prompts-{os.getpid()}-{label}.sock",
                     log=str(work / f"{label}.log"), extra_env=env, fast=True)


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
        if not check(bool(game.state().get("player")), "in the save room"):
            return
        cross_checks(game, work, shots)
        game.press("triangle", vsyncs=3, settle=60)
        top_command(game, symbols(), ABILITIES_COMMAND)  # by the game's cursor (fast travel's Magic on a Circle)
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
        game.call("button_prompts", button_prompts="nintendo", style="solid", scale=0.8,
                  bind={"bind_key_cross": "space"})
        game.call("settings", action="save")
        text = settings.read_text()
        check("button_prompts nintendo" in text and "bind_key_cross space" in text
              and "button_prompts_style solid" in text and "button_prompts_scale 0.8" in text
              and "bind_key_circle" not in text, "settings: button_prompts, style, scale and the changed binding saved")
        game.call("button_prompts", button_prompts="original", style="color_outline", scale=0.7,
                  bind={"bind_key_cross": "x"})
        game.call("settings", action="load")
        p = game.call("button_prompts")
        check(p["button_prompts"] == "nintendo" and p["bindings"]["cross"]["keys"] == ["space"]
              and p["style"] == "solid" and abs(p["scale"] - 0.8) < 1e-6, "settings: they load back")
        game.call("button_prompts", button_prompts="original", style="color_outline", scale=0.7,
                  bind={"bind_key_cross": "x"})
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
