#!/usr/bin/env python3
"""The depth of field stays off the loading screen.

    python3 pc/tests/dof_loading_smoke.py [--card pc/build/test-card1.mcd]

Loading a save from the title shows "NOW LOADING", a picture the game uploads
to VRAM: no 3D, so it sits at far depth, and with the save loaded (Ashley
there, the title phase over) the depth of field blurred it whole. A frame
that drew no 3D keeps the lens blur off (post_profile.cpp lens_blur_gate:
`no_3d`). With depth of field on, the load passes through `no_3d` and the
room after it has the lens again ("").
"""
import argparse
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import Game, OUT  # noqa: E402
from checkpoints import title  # noqa: E402

LENS = {"dof": True, "dof_model": "physical", "dof_fstop": 1.2, "dof_focal_length": 300, "dof_max_blur": 24,
        "dof_near_blur": 1}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    failures = 0

    def check(ok, message):
        nonlocal failures
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        failures += not ok

    with tempfile.TemporaryDirectory(prefix="dof-loading-", dir=OUT) as work:
        g = Game.launch(card=args.card, fast=True, log=pathlib.Path(work) / "game.log",
                        extra_env={"ROOD_WINDOW": "960x720"})
        try:
            title(g)
            g.call("post", layer="gameplay", clear="all", set=LENS, snap=True)
            g.press("start", vsyncs=4, settle=90)
            for _ in range(3):  # Continue, the card, its first save
                g.press("circle", vsyncs=3, settle=90)
            seen = []
            for _ in range(400):
                g.run(2)
                gate = g.call("post")["lens_blur_off"]
                if not seen or seen[-1] != gate:
                    seen.append(gate)
                if seen[-1] == "" and "no_3d" in seen:
                    break
            check("no_3d" in seen, f"the loading screen keeps the lens blur off (gates {seen})")
            check(seen[-1] == "", f"the room after it has the lens again (gates {seen})")
        finally:
            g.close()
    print(f"dof_loading_smoke: {'FAIL (%d failure(s))' % failures if failures else 'ok'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
