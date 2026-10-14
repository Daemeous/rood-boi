#!/usr/bin/env python3
"""The interactive fog prototype (pc/src/fog_sim.cpp; pc/docs/plans/2026-10-13-interactive-fog.md):
a fog volume with sim=1 in Snowfly Forest (zone 40 room 6), placed around
Ashley through RPC environment {volumes} (nothing is saved):

- off by default: a volume without sim has no field (`sim` null);
- with sim=1 the field exists, sees Ashley, clears her footprint (the value
  under her well below 1) while the rest stays near rest;
- walking leaves a wake: after a walk the cleared cells are more and spread
  along her path (the least value stays low, the mean drops further);
- standing still, it settles back toward rest within its recovery time;
- the haze's light changes around her (frames with the field vs without);
- no crash, shader error or validation message.

    python3 pc/tests/fog_sim_smoke.py [--shots DIR]
"""
import argparse
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_cutaway_smoke import default_card  # noqa: E402

ROOM = (40, 6)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--card", default=str(default_card()))
    ap.add_argument("--shots", help="keep the frames here (a walk through the fog)")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = OUT / "fog_sim_smoke"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    shots = pathlib.Path(args.shots) if args.shots else out
    shots.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.card, out / "card1.mcd")
    user = out / "user"
    user.mkdir()
    env = {"ROOD_LIGHTS": str(user), "ROOD_LIGHTING": str(ROOT / "lighting"), "ROOD_GI": "1", "ROOD_SCALE": "2",
           "ROOD_LIGHTING_PRESET": "ultra"}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-fogsim-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    game = launch_at("save_room", card=out / "card1.mcd", fast=True, log=out / "game.log", extra_env=env,
                     socket_path=socket)

    def shot(name, frames=30):
        game.run(frames)
        path = game.present_screenshot(shots / f"fogsim-{name}.png")
        return np.asarray(Image.open(path).convert("RGB"), np.float32)

    try:
        game.call("room_goto", zone=ROOM[0], room=ROOM[1])
        for _ in range(120):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(60)
        st = game.state()
        if not check((st["zone"], st["room"]) == ROOM, f"warped to zone {ROOM[0]} room {ROOM[1]}"):
            return 1
        p = st["player"]
        game.call("environment", fog_time=5, remove_fog=True, haze={"density": 0})
        vol = {"id": "simtest0000001", "pos": [p["x"], p["y"] - 20, p["z"]], "size": [900, 90, 900],
               "density": 0.02, "edge": 0.4, "noise": 0, "color": [1, 1, 1], "fill": [0.3, 0.3, 0.32]}
        e = game.call("environment", volumes=[vol])
        game.run(30)
        e = game.call("environment")
        check(e["sim"] is None and e["volumes"][0]["sim"] is False, "a volume without sim=1 has no field (off by default)")
        plain = shot("plain")
        e = game.call("environment", volumes=[{"id": "simtest0000001", "sim": True}])
        game.run(120)
        e = game.call("environment")
        s = e["sim"]
        check(s is not None and s["id"] == "simtest0000001" and s["actors"] >= 1, f"sim=1: a field that sees Ashley ({s})")
        check(s and 0 <= s["at_player"] < 0.6 and s["mean"] > 0.9,
              f"standing, her footprint is cleared (under her {s and s['at_player']}), the rest at rest "
              f"(mean {s and s['mean']})")
        standing = shot("standing")
        mask = np.abs(standing - plain).max(axis=2) > 6
        check(0.001 < mask.mean() < 0.4, f"the fog's look changes around her only ({mask.mean() * 100:.1f} % of pixels)")
        game.call("graphics", haze_debug=1)
        shot("standing-haze")
        game.call("graphics", haze_debug=0)
        before = s["mean"]
        start = game.state()["player"]
        game.hold("up", 50)
        game.hold("right", 40)
        end = game.state()["player"]
        e = game.call("environment")
        s = e["sim"]
        moved = ((end["x"] - start["x"]) ** 2 + (end["z"] - start["z"]) ** 2) ** 0.5
        shot("walking", 1)
        game.call("graphics", haze_debug=1)
        shot("walking-haze", 1)
        game.call("graphics", haze_debug=0)
        check(moved > 100 and s["least"] < 0.65 and s["mean"] < before - 0.003,
              f"walking {moved:.0f} units leaves a wake (least {s['least']}, mean {s['mean']} after, {before} standing)")
        game.run(int(60 * 3 * 3))  # three recovery times
        e = game.call("environment")
        s = e["sim"]
        check(s["mean"] > 0.97, f"standing still, it settles back (mean {s['mean']})")
        game.call("environment", sim_reset=True, volumes=[{"id": "simtest0000001", "remove": True}])
    finally:
        game.close()
    log = (out / "game.log").read_text(errors="replace")
    bad = [line for line in log.splitlines() if "validation:" in line or "[crash]" in line or "[gl] shader" in line]
    check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))
    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
