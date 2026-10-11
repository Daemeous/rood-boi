"""Frame interpolation must move the camera between game frames.

The game draws 30 frames a second and double-buffers: it draws frame B into
the buffer that is not on screen while showing A, and flips to B a frame
later. Interpolated frames used to redraw B into B's own (off-screen) area
while presenting the displayed one, a copy of A: every other presented image
repeated the one before it, so camera motion stepped at 30 fps.

New Game, the opening's first room (zone 1 room 0) during its camera pan
(vsyncs ~2420-2750), with a shadow-casting test light and lighting on:
present_dump bursts of what the window shows. Without interpolation every
other image repeats (the control: the scene really runs at 30 fps); with it
every image differs from the one before by a similar amount (the lighting
follows the interpolated picture, so it does not flicker either)."""
import os
import pathlib
import sys
import time

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import OUT, Game  # noqa: E402

lights = pathlib.Path(os.environ.get("ROOD_LIGHTS") or OUT / "interpolation-lights")
lights.mkdir(parents=True, exist_ok=True)
# light <type> x y z  r g b  intensity radius  dx dy dz cone  shadows enabled
(lights / "zone01_room00.txt").write_text("light point 768 -600 836 1 0.75 0.45 2 1500 0 1 0 45 1 1\n")
COUNT = 20


def load(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        return np.frombuffer(f.read(), np.uint8).reshape(h, w, 3).astype(np.float32)


def burst(game, name):
    """Mean absolute difference of each presented image from the one before."""
    base = OUT / f"interpolation-{name.replace(' ', '-')}"
    for old in OUT.glob(base.name + ".*.ppm"):
        old.unlink()
    game.call("present_dump", path=str(base), count=COUNT)
    game.run(COUNT + 6)
    paths = [pathlib.Path(f"{base}.{i:04d}.ppm") for i in range(COUNT)]
    deadline = time.monotonic() + 20
    while not all(p.exists() for p in paths) and time.monotonic() < deadline:
        game.run(1)  # bursts are written a few presents later
    frames = [load(p) for p in paths]
    for p in paths:
        p.unlink()
    # The first images of a burst can repeat while it starts: skip them.
    diffs = np.array([np.mean(np.abs(b - a)) for a, b in zip(frames, frames[1:])])[2:]
    print(f"{name:>12}: " + " ".join(f"{d:.2f}" for d in diffs), flush=True)
    return diffs


game = Game.launch(extra_env={"ROOD_GI": "1", "ROOD_LIGHTS": str(lights)}, fast=True)
try:
    game.wait_title()
    game.press("start")
    game.run(2490 - game.status()["vsync"])
    state = game.state()
    assert (state["zone"], state["room"]) == (1, 0), state
    game.call("graphics", interpolate=False)
    game.run(4)
    stepped = burst(game, "interp off")
    game.call("graphics", interpolate=True)
    game.run(4)
    smooth = burst(game, "interp on")
finally:
    game.close()
log = pathlib.Path(game.log).read_text(errors="replace")
assert "[crash]" not in log

repeats = float(np.mean(stepped < 0.1 * stepped.max()))  # lighting noise keeps repeats from being exact
assert repeats > 0.3 and stepped.max() > 0.5, \
    f"control: the camera pan should step at 30 fps without interpolation ({repeats * 100:.0f}% repeated images)"
median = float(np.median(smooth))
assert smooth.min() > 0.25 * median, \
    f"interpolated images repeat the image before them (min difference {smooth.min():.2f}, median {median:.2f})"
assert smooth.max() < 4 * smooth.min(), \
    f"uneven motion with interpolation (differences {smooth.min():.2f}..{smooth.max():.2f})"
print(f"PASS: camera pan interpolated: frame-to-frame differences {smooth.min():.2f}..{smooth.max():.2f} "
      f"(without interpolation {repeats * 100:.0f}% of images repeat)")
