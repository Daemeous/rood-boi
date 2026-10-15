#!/usr/bin/env python3
"""The interactive fog (pc/src/fog_sim.cpp, atmosphere.cpp;
pc/docs/plans/2026-10-13-interactive-fog.md) in Snowfly Forest (zone 40
room 6, warped with its enemies), volumes placed around Ashley through RPC
environment {volumes}:

- off by default: a volume without sim has no field (`sim` null);
- with sim=1 the field sees Ashley, clears her footprint (the value under
  her well below 1) while the rest stays near rest, and the picture changes
  around her only;
- walking leaves a wake; standing still it settles back (recovery);
- weights: each source's weight 0 disables it (global fog_disturb_* through
  RPC environment {disturb}, and a volume's disturb_* overriding it both
  ways); the other characters (the room's enemies) part it;
- an attack stamp parts it only with the attacks weight above 0, and the
  game's own battle actions are seen (swings, chain steps, spells);
- several simulated volumes at once, each its own layer, within the room's
  budget (fog_sim_budget);
- presets: a volume made from one saves only its preset and what differs;
- unsaved volumes survive walking out of the room and back (they were
  dropped by the room's reload before);
- save states: a snapshot taken mid-wake resumes with the same field;
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
from rood_rpc import OUT, Game  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_cutaway_smoke import default_card  # noqa: E402

ROOM = (40, 6)
NEAR = "simtest0000001"
BIG = "simtest0000002"


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
    logs = [out / "game.log"]

    def shot(name, frames=30):
        game.run(frames)
        path = game.present_screenshot(shots / f"fogsim-{name}.png")
        return np.asarray(Image.open(path).convert("RGB"), np.float32)

    def sims():
        return {s["id"]: s for s in game.call("environment")["sims"]}

    def weights(player=0, actors=0, attacks=0, spells=0):
        game.call("environment", disturb={"player": player, "actors": actors, "attacks": attacks, "spells": spells})

    def goto(zone, room, enemies=False):
        game.call("room_goto", zone=zone, room=room, enemies=enemies, scripts=False)
        for _ in range(120):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(60)

    try:
        goto(*ROOM)
        st = game.state()
        if not check((st["zone"], st["room"]) == ROOM, f"warped to zone {ROOM[0]} room {ROOM[1]}"):
            return 1
        p = st["player"]
        e = game.call("environment", fog_time=5, remove_fog=True, haze={"density": 0})
        check(e["disturb"] == {"player": 1, "actors": 1, "attacks": 0, "spells": 0},
              f"default weights: Ashley and the others on, attacks and spells off ({e['disturb']})")
        check("forest_floor" in e["presets"] and "ankle_mist" in e["presets"], f"presets loaded ({e['presets']})")
        # The editor's Drop to floor (End): the bottom face rests on the floor under it (Ashley's feet).
        drop = {"id": "simtest0000009", "pos": [p["x"], p["y"] - 300, p["z"]], "size": [24, 40, 24]}
        game.call("environment", volumes=[drop])
        game.call("environment", drop_volume="simtest0000009")
        v = [x for x in game.call("environment")["volumes"] if x["id"] == "simtest0000009"][0]
        bottom = v["pos"][1] + v["size"][1] / 2
        check(abs(bottom - p["y"]) <= 1, f"Drop to floor: its bottom at {bottom}, the floor (her feet) at {p['y']}")
        game.call("environment", fit_volume="simtest0000009")
        v = [x for x in game.call("environment")["volumes"] if x["id"] == "simtest0000009"][0]
        check(v["size"][0] > 24 and v["size"][2] > 24 and v["yaw"] == 0,
              f"Fit to floor area: the footprint becomes the floor around it ({v['size']}, at {v['pos']})")
        game.call("environment", volumes=[{"id": "simtest0000009", "remove": True}])

        # The enemies stay out of the first checks (their own check below).
        weights(player=1)
        vol = {"id": NEAR, "pos": [p["x"], p["y"] - 20, p["z"]], "size": [900, 90, 900],
               "density": 0.02, "edge": 0.4, "noise": 0, "color": [1, 1, 1], "fill": [0.3, 0.3, 0.32]}
        game.call("environment", volumes=[vol])
        game.run(30)
        e = game.call("environment")
        check(e["sim"] is None and e["volumes"][0]["sim"] is False, "a volume without sim=1 has no field (off by default)")
        plain = shot("plain")
        game.call("environment", volumes=[{"id": NEAR, "sim": True}])
        game.run(120)
        s = game.call("environment")["sim"]
        check(s is not None and s["id"] == NEAR and s["actors"] >= 1 and s["layer"] == 0,
              f"sim=1: a field that sees Ashley ({s})")
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
        s = game.call("environment")["sim"]
        moved = ((end["x"] - start["x"]) ** 2 + (end["z"] - start["z"]) ** 2) ** 0.5
        shot("walking", 1)
        game.call("graphics", haze_debug=1)
        shot("walking-haze", 1)
        game.call("graphics", haze_debug=0)
        check(moved > 100 and s["least"] < 0.65 and s["mean"] < before - 0.003,
              f"walking {moved:.0f} units leaves a wake (least {s['least']}, mean {s['mean']} after, {before} standing)")
        game.run(int(60 * 3 * 3))  # three recovery times
        s = game.call("environment")["sim"]
        check(s["mean"] > 0.97, f"standing still, it settles back (mean {s['mean']})")
        here = game.state()["player"]  # the volume follows her to where the walk ended
        game.call("environment", volumes=[{"id": NEAR, "pos": [here["x"], here["y"] - 20, here["z"]]}])

        # Weights: 0 disables a source; a volume's own weight overrides the global one both ways.
        weights()
        game.call("environment", sim_reset=True)
        game.run(90)
        s = sims()[NEAR]
        check(s["least"] >= 0.999 and not s["awake"],
              f"every weight 0: Ashley standing in it leaves it at rest, idle ({s})")
        game.call("environment", volumes=[{"id": NEAR, "disturb_player": 1}])
        game.run(90)
        s = sims()[NEAR]
        check(s["at_player"] < 0.6, f"the volume's own Ashley weight 1 wins over the global 0 (under her {s['at_player']})")
        weights(player=1)
        game.call("environment", sim_reset=True, volumes=[{"id": NEAR, "disturb_player": 0}])
        game.run(90)
        s = sims()[NEAR]
        check(s["least"] >= 0.999, f"the volume's own weight 0 wins over the global 1 (least {s['least']})")
        game.call("environment", volumes=[{"id": NEAR, "disturb_player": -1}])

        # Several volumes: a second, room-sized one from a preset (its own layer); the budget.
        weights()
        game.call("environment", sim_reset=True, volumes=[
            {"id": BIG, "preset": "forest_floor", "pos": [p["x"], p["y"] - 20, p["z"]], "size": [2600, 400, 2600]}])
        game.run(10)
        e = game.call("environment")
        big = [v for v in e["volumes"] if v["id"] == BIG][0]
        layers = {s["id"]: s["layer"] for s in e["sims"]}
        check(big["preset"] == "forest_floor" and big["sim"] and abs(big["sim_trail"] - 1.2) < 1e-6,
              f"a volume from a preset takes its fields ({big['preset']}, sim {big['sim']}, trail {big['sim_trail']})")
        check(sorted(layers.values()) == [0, 1], f"two simulated volumes, a layer each ({layers})")
        game.call("environment", sim_budget=1)
        game.run(5)
        layers = {v["id"]: v["layer"] for v in game.call("environment")["volumes"]}
        check(layers[NEAR] == 0 and layers[BIG] == -1, f"fog_sim_budget 1: the first one only ({layers})")
        game.call("environment", sim_budget=4)

        # Attack and spell stamps: only with their weight above 0.
        here = game.state()["player"]
        at = [here["x"], here["y"], here["z"]]
        game.call("environment", stamp={"shape": "swing", "source": "attack", "pos": at, "dir": [0, 1]})
        game.run(20)
        s = sims()[NEAR]
        check(s["least"] >= 0.999, f"a swing with the attacks weight 0 does nothing (least {s['least']})")
        weights(attacks=1)
        game.call("environment", stamp={"shape": "swing", "source": "attack", "pos": at, "dir": [0, 1]})
        game.run(12)
        s = sims()[NEAR]
        check(s["least"] < 0.8, f"a swing with the attacks weight 1 parts it (least {s['least']})")
        shot("swing", 1)
        game.call("environment", stamp={"shape": "burst", "source": "spell", "pos": at})
        game.run(12)
        s2 = sims()[NEAR]
        check(abs(s2["mean"] - s["mean"]) < 0.02 or s2["mean"] >= s["mean"],
              f"a spell burst with the spells weight 0 adds nothing ({s['mean']} -> {s2['mean']})")
        weights(spells=1)
        game.call("environment", stamp={"shape": "burst", "source": "spell", "pos": at})
        game.run(12)
        s3 = sims()[NEAR]
        check(s3["mean"] < s2["mean"] - 0.005, f"a spell burst with the spells weight 1 parts it ({s2['mean']} -> {s3['mean']})")
        shot("burst", 1)

        # Presets in the files: the big volume saves its preset and only what differs from it.
        game.call("lighting", action="save")
        text = "\n".join(f.read_text() for f in user.rglob("*") if f.is_file())
        line = [x for x in text.splitlines() if x.startswith("fog " + BIG)]
        check(line and "preset=forest_floor" in line[0] and "density=" not in line[0] and "size=" in line[0],
              f"saved with its preset and what differs ({line[:1]})")

        # Unsaved edits survive walking out and back (the prototype lost live volumes then).
        game.call("environment", volumes=[{"id": "simtest0000003", "pos": at, "size": [300, 60, 300], "sim": True}])
        goto(40, 7)
        check(game.call("environment")["zone"] == 40 and game.call("environment")["room"] == 7, "went to room 7")
        goto(*ROOM, enemies=True)  # back, with the room's enemies this time
        e = game.call("environment")
        ids = [v["id"] for v in e["volumes"]]
        check((e["zone"], e["room"]) == ROOM and "simtest0000003" in ids,
              f"an unsaved volume is back after leaving the room and coming back ({ids})")
        game.call("environment", volumes=[{"id": "simtest0000003", "remove": True}])
        game.call("lighting", action="save")
        game.call("environment", fog_time=5, haze={"density": 0})
        p = game.state()["player"]

        # The other characters (the room's enemies) part the big one, Ashley's weight 0.
        weights(actors=1)
        game.call("environment", sim_reset=True)
        touched = 0
        for _ in range(12):
            game.run(40)
            s = sims()[BIG]
            touched = max(touched, s["actors"])
            if s["least"] < 0.9:
                break
        check(touched >= 1 and s["least"] < 0.9,
              f"the enemies part it with only their weight on ({touched} in it, least {s['least']})")
        shot("enemies")
        weights()
        game.call("environment", sim_reset=True)
        game.run(120)
        s = sims()[BIG]
        check(s["least"] >= 0.999, f"their weight 0: they leave it at rest (least {s['least']})")

        # The game's own battle actions (any actor's: Ashley's swings, the enemies').
        weights(player=1, actors=1, attacks=1, spells=1)
        seen = game.call("environment")["events"]
        for _ in range(8):
            game.press("circle", 3, 30)  # battle mode / the target / attack
            game.press("circle", 3, 40)
            game.press("circle", 3, 60)
            if game.call("environment")["events"]["attacks"] > seen["attacks"]:
                break
            game.run(60)
        now = game.call("environment")["events"]
        check(now["attacks"] > seen["attacks"], f"the game's attacks are seen ({seen} -> {now})")
        shot("battle", 1)
        game.press("cross", 3, 30)

        # Recovery: every source off, it settles to rest and sleeps.
        weights()
        game.run(int(60 * 3 * 5))  # five recovery times: within 1 % of rest, it sleeps
        s = sims()
        check(all(x["mean"] > 0.995 and not x["awake"] for x in s.values()),
              f"nothing parting it, both settle and sleep ({[(k, x['mean'], x['awake']) for k, x in s.items()]})")


        # The editor's preview actor (RPC environment {ghost}): parts it like a character, whatever the weights.
        here = game.state()["player"]
        game.call("environment", ghost=[here["x"] + 120, here["y"], here["z"]])
        game.run(30)
        game.call("environment", ghost=[here["x"] + 200, here["y"], here["z"] + 60])
        game.run(30)
        s = sims()[BIG]
        check(s["least"] < 0.7, f"the ghost parts it with every weight 0 (least {s['least']})")
        # The editor's Atmosphere tab: the volume selected, its interaction controls and its field in the view.
        game.call("editor_ui", visible=True, details="atmosphere")
        game.call("environment", select_volume=BIG)
        game.run(10)
        shot("editor", 10)
        game.call("environment", ghost=False)
        game.call("editor_ui", visible=False)
        game.run(10)

        # Save states: a snapshot taken mid-wake resumes with the same field.
        if game.call("snapshot", action="available").get("available"):
            here = game.state()["player"]
            at = [here["x"], here["y"], here["z"]]
            weights(player=1, attacks=1)
            game.call("environment", stamp={"shape": "vortex", "source": "attack", "pos": at, "radius": 150})
            game.run(30)
            a = [x for x in game.call("environment", sim_field=True)["sims"] if x["id"] == NEAR][0]
            snap = out / "fog.snap"
            game.call("snapshot", action="save", path=str(snap))
            game.close()
            env2 = dict(env)
            game = Game.launch(card=str(out / "card1.mcd"), fast=True, log=out / "game-resumed.log", extra_env=env2,
                               socket_path=socket, snapshot=snap)
            logs.append(out / "game-resumed.log")
            game.call("environment", fog_time=5, remove_fog=True, haze={"density": 0})
            b = [x for x in game.call("environment", sim_field=True)["sims"] if x["id"] == NEAR]
            diff = max(abs(x - y) for x, y in zip(a["field"], b[0]["field"])) if b else 9
            check(b and diff < 1e-4 and a["least"] < 0.95,
                  f"a save state resumes the field as it was (least {a['least']}, largest difference {diff})")
            game.run(30)
            c = [x for x in game.call("environment")["sims"] if x["id"] == NEAR]
            check(c and c[0]["steps"] > b[0]["steps"], "and it steps on from there")
        else:
            print("skip: save states unavailable here")
        game.call("environment", sim_reset=True)
    finally:
        game.close()
    bad = []
    for path in logs:
        if path.exists():
            bad += [line for line in path.read_text(errors="replace").splitlines()
                    if "validation:" in line or "[crash]" in line or "[gl] shader" in line]
    check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))
    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
