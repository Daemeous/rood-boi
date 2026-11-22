"""Check a later VKP scene reached by the game's own frames (the idle cutscene after the title).

The game runs unpaced from the title with no input; the idle cutscene plays: a room (native lists, drawn by the renderer), its
characters, and the dialogue bubbles, which are vector bubbles drawn over the picture, not pixels of the PS1 frame (so the PS1
frame the old version of this test read back, a 2D picture of the display area, only held the loading screen's text). What this
checks is the picture the game presents: at eight points in the cutscene (a cut fades through black now and then) at least five pictures are a lit 3D scene (many colours, a good part of it not
black), and a dialogue bubble (a light grey box with dark text) is on screen at some of them (the bubbles come and go)."""
import os
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import rood_rpc  # noqa: E402
from rood_rpc import Game  # noqa: E402

# (the title's idle play starts the cutscene; fast, so the vsyncs come as fast as the machine can make them)
POINTS = tuple(range(13000, 21000, 1000))
out = pathlib.Path(os.environ.get("ROOD_OUT") or "pc/build/boot") / "scene_smoke"
out.mkdir(parents=True, exist_ok=True)
game = Game.launch(fast=True, log=out / "game.log", extra_env={"ROOD_SCALE": "1"}, args=("--battle-frame-checkpoint", "900000"))
bubbles = lit = 0
try:
    for n, point in enumerate(POINTS):
        waited = 0
        while game.status()["vsync"] < point:
            subprocess.run(["sleep", "0.2"])
            waited += 1
            if waited > 600:
                raise SystemExit(f"Scene did not reach vsync {point} in two minutes")
        path = game.present_screenshot(str(out / f"scene-{n}.png"))
        frame = np.asarray(Image.open(path).convert("RGB")).astype(np.int32)
        flat = frame.reshape(-1, 3)
        colours = len({tuple(p) for p in flat[::7]})
        nonblack = float((flat.max(1) > 24).mean())
        light = float(((flat.min(1) >= 170) & (flat.max(1) - flat.min(1) <= 24)).mean())
        print(f"vsync {point}: {colours} colours, {nonblack * 100:.0f}% not black, {light * 100:.1f}% bubble-light", flush=True)
        lit += colours > 200 and nonblack > 0.15 # (a cut between scenes fades through black: not every picture is a scene)
        bubbles += light > 0.015
finally:
    game.close()
log = pathlib.Path(game.log).read_text(errors="replace")
assert "[crash]" not in log and "[vk] validation:" not in log, "crash or validation message; see " + game.log
assert lit >= 5, f"Later scene missing: a lit scene in {lit} of {len(POINTS)} pictures"
assert bubbles >= 2, f"Dialogue missing: a bubble in {bubbles} of {len(POINTS)} pictures"
print(f"PASS: the idle cutscene's 3D scene ({lit} of {len(POINTS)} pictures) and dialogue bubbles ({bubbles}) are presented")
