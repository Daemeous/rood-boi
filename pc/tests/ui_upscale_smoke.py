#!/usr/bin/env python3
"""2D / UI magnification (pc/src/ui_upscale.c; README "2D / UI"): MMPX on the
textures of 2D primitives, from the game's own VRAM.

    python3 pc/tests/ui_upscale_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

Runs on the backend the environment selects (run.py: 32-bit GL, 64-bit GL,
or ROOD_RENDERER=vulkan): GPU compute (GL 4.3 / Vulkan), then in a second
launch the CPU fallback (ROOD_UI_UPSCALE_CPU=1). Comparisons use the same
recorded frame drawn twice (RPC ui_upscale redraw), so nothing moves between
the two images. Checks:

- defaults: ui_upscale nearest, ui_subpixel off (the original look);
- title menu, then (with a card; a copy of it) the save room's HUD and the
  in-game menu with MMPX: the picture changes, and only where magnified 2D
  primitives were drawn (RPC ui_upscale mask): 3D is unchanged;
- palette: the atlas read back equals the CPU reference of MMPX texel for
  texel and holds no value outside each region's source texels (RPC
  ui_upscale verify); in the picture, magnified 2D shows no colour the
  nearest picture lacks (blends of the same texels with what lies behind);
- cache: after warm-up a still menu costs lookups only (no new regions);
- invalidation: a texture page changing under a cached region (RPC
  ui_upscale poke: its texels rewritten and uploaded like LoadImage) is
  magnified again from the new texels (a miss, verify still exact);
- settings: ui_upscale / ui_subpixel save and load (ROOD_SETTINGS);
- no shader errors, Vulkan validation messages (ROOD_VK_VALIDATE where the
  layer exists) or crashes.

--shots keeps side-by-side images (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

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


def redraw(game, work, mode, name):
    game.call("graphics", ui_upscale=mode)
    path = work / f"{name}-{mode}.ppm"
    info = game.call("ui_upscale", redraw=str(path))
    image = np.asarray(Image.open(path))
    path.unlink()
    return image, info


def compare(game, work, name, shots, label):
    """Nearest and MMPX of the same frame: (changed pixels, outside the 2D
    mask, new colours inside it)."""
    game.call("ui_upscale", mask=str(work / "mask.pgm"))  # clear
    game.run(6)  # a few frames drawn with MMPX (the mask accumulates)
    game.call("ui_upscale", mask=str(work / "mask.pgm"))
    mask = np.asarray(Image.open(work / "mask.pgm"))
    nearest, info = redraw(game, work, "nearest", name)
    mmpx, _ = redraw(game, work, "mmpx", name)
    scale = nearest.shape[1] // 1024
    x, y, w, h = info["area"]
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, 1024), min(y + h, 512)
    a = nearest[y0 * scale:y1 * scale, x0 * scale:x1 * scale]
    b = mmpx[y0 * scale:y1 * scale, x0 * scale:x1 * scale]
    m = mask[y0:y1, x0:x1] > 0
    # One VRAM pixel of slack: a primitive's box is rounded outwards.
    grown = m.copy()
    grown[1:] |= m[:-1]
    grown[:-1] |= m[1:]
    grown[:, 1:] |= grown[:, :-1].copy()
    grown[:, :-1] |= grown[:, 1:].copy()
    grown = np.repeat(np.repeat(grown, scale, axis=0), scale, axis=1)
    diff = np.any(a != b, axis=2)
    outside = int((diff & ~grown).sum())
    colours = set(map(tuple, a[grown].reshape(-1, 3)))
    inside = b[diff]
    new = sum(1 for c in map(tuple, inside) if c not in colours)
    if shots:
        side = np.concatenate([a, np.full((a.shape[0], 8, 3), 255, np.uint8), b], axis=1)
        Image.fromarray(side).save(shots / f"{label}-{name}.png")
    return int(diff.sum()), outside, new, int(len(inside))


def run(label, card, shots, extra_env, gpu):
    work = OUT / f"ui-upscale-{label}"
    work.mkdir(parents=True, exist_ok=True)
    settings = work / "settings.cfg"
    settings.unlink(missing_ok=True)
    env = {"ROOD_OUT": str(work), "ROOD_SETTINGS": str(settings), **extra_env}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env.setdefault("ROOD_VK_VALIDATE", "1")
    card_copy = None
    if card:
        card_copy = work / "card.mcd"
        shutil.copyfile(card, card_copy)
    game = Game.launch(socket_path=f"/tmp/rood-ui-{os.getpid()}-{label}.sock", log=str(work / "game.log"),
                       extra_env=env, card=str(card_copy) if card_copy else None, fast=True)
    try:
        g = game.call("graphics")
        check(g["ui_upscale"] == "nearest" and g["ui_subpixel"] == 0,
              f"{label}: defaults are the original look (ui_upscale {g['ui_upscale']}, ui_subpixel {g['ui_subpixel']})")
        game.call("graphics", ui_upscale="mmpx", interpolate=False)
        game.wait_title()
        game.run(90)
        stats = game.call("ui_upscale")
        want = ("vulkan" if os.environ.get("ROOD_RENDERER") == "vulkan" else "gl") if gpu else "cpu"
        check(stats["backend"] == want and stats["factor"] >= 2,
              f"{label}: MMPX {stats['factor']}x on {stats['backend']} (expected {want})")
        changed, outside, new, inside = compare(game, work, "title", shots, label)
        check(changed > 0, f"{label} title: MMPX changes the menu ({changed} pixels)")
        check(outside == 0, f"{label} title: nothing changes outside magnified 2D ({outside} pixels)")

        if card_copy:
            game.press("start", vsyncs=4, settle=90)
            for _ in range(3):  # Continue, the card, its first save
                game.press("circle", vsyncs=3, settle=90)
            game.run(400)
            if check(bool(game.state().get("player")), f"{label}: in the save room"):
                changed, outside, new, inside = compare(game, work, "hud", shots, label)
                check(changed > 0, f"{label} HUD: MMPX changes it ({changed} pixels)")
                check(outside == 0, f"{label} HUD: 3D unchanged (changes outside magnified 2D: {outside})")
                game.press("triangle", vsyncs=3, settle=60)
                changed, outside, new, inside = compare(game, work, "menu", shots, label)
                check(changed > 0, f"{label} menu: MMPX changes it ({changed} pixels)")
                check(outside == 0, f"{label} menu: 3D unchanged (changes outside magnified 2D: {outside})")
                # Blends (translucent menu bars) mix texels with what is behind:
                # a new colour must stay rare.
                check(new <= max(16, inside // 100),
                      f"{label} menu: magnified pixels keep the nearest picture's colours ({new} of {inside} new)")

                # Warm cache: a still menu adds no regions.
                game.call("graphics", ui_upscale="mmpx")
                game.run(30)
                before = game.call("ui_upscale")
                game.run(60)
                after = game.call("ui_upscale")
                check(after["misses"] == before["misses"] and after["hits"] > before["hits"],
                      f"{label}: warm cache, {after['hits'] - before['hits']} hits and "
                      f"{after['misses'] - before['misses']} misses in 60 vsyncs; "
                      f"{after['frame_lookups']} lookups, {after['frame_ms']:.3f} ms a frame")

                # A cached region's texture page changes (as LoadImage would).
                regions = game.call("ui_upscale", regions=64).get("regions", [])
                pick = next((r for r in regions if r[4] < 2 and (r[2] - r[0] + 1) * (r[3] - r[1] + 1) <= 512), None)
                if check(pick is not None, f"{label}: a cached CLUT region to rewrite ({len(regions)} listed)"):
                    x0, y0, x1, y1, _ = pick
                    before = game.call("ui_upscale")
                    game.call("ui_upscale", poke=[x0, y0, x1 - x0 + 1, y1 - y0 + 1, 0x1234])
                    game.run(6)
                    after = game.call("ui_upscale", verify=True)
                    check(after["misses"] > before["misses"],
                          f"{label}: rewritten texels are magnified again ({after['misses'] - before['misses']} new)")
                    check(after["verified"] > 0 and after["mismatches"] == 0 and after["foreign"] == 0,
                          f"{label}: after the rewrite the atlas still equals the CPU reference")

        v = game.call("ui_upscale", verify=True)
        check(v["verified"] > 0 and v["mismatches"] == 0,
              f"{label}: atlas equals the CPU reference of MMPX ({v['verified']} regions, "
              f"{v['mismatches']} texels differ)")
        check(v["foreign"] == 0, f"{label}: palette preserved ({v['foreign']} texels not in their source)")

        # Settings persist.
        game.call("graphics", ui_upscale="mmpx", ui_subpixel=1)
        game.call("settings", action="save")
        text = settings.read_text()
        check("ui_upscale mmpx" in text and "ui_subpixel 1" in text, f"{label}: settings saved")
        game.call("graphics", ui_upscale="nearest", ui_subpixel=0)
        game.call("settings", action="load")
        g = game.call("graphics")
        check(g["ui_upscale"] == "mmpx" and g["ui_subpixel"] == 1, f"{label}: settings load back")
        game.call("graphics", ui_upscale="nearest", ui_subpixel=0)
        game.run(4)
    finally:
        game.close()
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log and "[gl] shader" not in log and "[vk] validation:" not in log
          and "[gl] link" not in log and "[gl] compute link" not in log, f"{label}: no crash, shader error or validation message")
    if "[vk] validation:" in log or "[crash]" in log:
        print("\n".join(line for line in log.splitlines() if "validation" in line or "crash" in line)[:4000])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--card", type=pathlib.Path, default=default_card())
    parser.add_argument("--shots", type=pathlib.Path)
    args = parser.parse_args()
    card = args.card if args.card and args.card.exists() else None
    if not card:
        print("note: no memory card: title menu only")
    if args.shots:
        args.shots.mkdir(parents=True, exist_ok=True)
    run("gpu", card, args.shots, {}, True)
    run("cpu", None, args.shots, {"ROOD_UI_UPSCALE_CPU": "1"}, False)
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("PASS: ui_upscale_smoke")
    return 0


if __name__ == "__main__":
    sys.exit(main())
