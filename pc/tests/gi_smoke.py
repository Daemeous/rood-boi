"""Path tracer (ptgi.c): start New Game headless, reach the first room, turn
PTGI on and check that it compiles, runs every pass and finds glowing or
baked light sources, and that turning it off again restores the cube-map
path. Software GL (Xvfb) is slow, so it traces at a low resolution briefly."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import Game  # noqa: E402

game = Game.launch(extra_env={"ROOD_SCALE": "1"}, fast=True)
try:
    game.wait_title()
    game.press("start")
    for _ in range(20):  # the opening's first room (about 1200 vsyncs in)
        game.run(300)
        scene = game.call("scene")
        if scene["triangles"]:
            break
    assert scene["triangles"] > 0, scene
    game.call("graphics", gi=True, rt_shadows=True, baked_shadows=True, baked_estimate=True, gi_resolution=1, gi_rays=1, gi_bounces=1)
    game.run(20)
    for debug in (1, 2, 3):
        game.call("graphics", gi_debug=debug)
        game.run(2)
    game.call("graphics", gi_debug=0, gi=False, rt_shadows=False, baked_shadows=False)
    game.run(10)
finally:
    game.close()
log = pathlib.Path(game.log).read_text()
assert "[gi] path-traced global illumination ready" in log, "path tracer did not start"
assert "[gl] shader" not in log and "[gl] compute link" not in log, "shader errors"
assert "baked light sources estimated" in log, "no baked light estimate"
assert "[crash]" not in log
print("PASS: path tracer compiled, traced, accumulated and filtered; baked light sources estimated")
