#!/usr/bin/env python3
"""A skipped wind-up never hangs the game, at any of the hold settings (issue #25: with
qol_skip_charging_hold_mine and _hold_enemy both at 0.75 an action occasionally froze the game).

    python3 pc/tests/cast_speed_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--holds 0.75,0.5,...]

The casters' room of skip_charging_smoke.py (two enemies that cast at Ashley), who: all, each hold
setting (seconds, both settings at once) in its own game, Information on and off: the battle runs
CASTERS_RUN unpaced vsyncs in steps. Fails when BATTLE's action state (D_800F196C 2) lasts longer than a
wind-up and its action can (STUCK vsyncs), when no vsync passes in a step, or when fewer than MIN_ACTIONS
wind-ups ran (the room did not cast), and on a crash. Ashley's own action too: a Solid Shock cast at each hold
in the first room of skip_charging_smoke.py, which must end (the action state back to the field within the
action's own wait) with the hold elapsing in phase 5 or 6 as the setting falls."""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import skip_charging_smoke as smoke  # noqa: E402

HOLDS = (0.75, 0.4, 1.3)  # (the settings stop at 1.5 s; --holds sweeps more, the RPC goes past it)
STEP = 60
STUCK = 2400  # vsyncs in the action state: past any wind-up and action
MIN_ACTIONS = 1
INFO = (True, False)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(smoke.card_default()))
    parser.add_argument("--out", help="keep the logs here")
    parser.add_argument("--holds", default=",".join(str(h) for h in HOLDS))
    parser.add_argument("--run", type=int, default=smoke.CASTERS_RUN, help="vsyncs the battle runs")
    parser.add_argument("--info", default="1,0", help="Information on (1) and off (0)")
    args = parser.parse_args()
    global INFO
    INFO = tuple(x == "1" for x in args.info.split(","))
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "cast_speed").resolve()
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_PREFABS": str(out / "prefabs"),
           "ROOD_LIGHTING": str(out / "lighting"), "ROOD_GI": "0"}
    (out / "prefabs").mkdir()
    (out / "lighting").mkdir()
    if not os.environ.get("ROOD_OUT"):
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-castspeed-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    snap = out / "casters.snap"
    room_snap = out / "room.snap"
    g = launch_at("save_room", card=card, fast=True, log=out / "boot.log", extra_env=env, socket_path=socket)
    try:
        reply = g.call("room_goto", zone=smoke.CASTERS[0], room=smoke.CASTERS[1], enemies=True, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(30)
        st = g.state()
        if not check(reply["ok"] and (st["zone"], st["room"]) == smoke.CASTERS, "warped to the casters' room"):
            return 1
        g.call("snapshot", action="save", path=str(snap))
        reply = g.call("room_goto", zone=smoke.ROOM[0], room=smoke.ROOM[1], enemies=True, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(smoke.APPROACH)
        st = g.state()
        if not check(reply["ok"] and (st["zone"], st["room"]) == smoke.ROOM, "warped to the room with an enemy in range"):
            return 1
        g.call("snapshot", action="save", path=str(room_snap))
    finally:
        g.close()

    for hold in (float(h) for h in args.holds.split(",")):
        r = smoke.Run("mine-skip", room_snap, card, env, out, socket, fast=True)
        try:
            r.prepare()
            r.g.call("skip_charging", mode="skip", who="all", name=smoke.NAME_SECONDS, hold_mine=hold, hold_enemy=hold)
            r.shell("magick")
            for _ in range(6):
                if r.ui()["magick"].get("sub") == "warlock":
                    break
                r.g.press("r2", vsyncs=3, settle=4)
            spells = next(x["spells"] for x in r.ui()["magick"]["schools"] if x.get("id") == "warlock")
            row = next(i for i, x in enumerate(spells) if x["id"] == smoke.SOLID_SHOCK)
            for _ in range(24):
                if r.ui()["magick"]["cursor"] == row:
                    break
                r.g.press("down", vsyncs=3, settle=4)
            try:
                result = r.act("Solid Shock")
                stats = result["stats"]
                check(stats["sequences"] >= 1, f"hold {hold} s, Ashley's Solid Shock: ended, wind-up phases "
                      f"{stats['phases']}")
            except TimeoutError as e:
                check(False, f"hold {hold} s, Ashley's Solid Shock: hung ({e})")
        finally:
            r.close()
        for information in INFO:
            label = f"hold {hold} s, Information {'on' if information else 'off'}"
            r = smoke.Run("casters-skip", snap, card, env, out, socket, fast=True)
            try:
                g = r.g
                g.call("skip_charging", mode="skip", who="all", pin=smoke.PIN, reset=True, name=smoke.NAME_SECONDS,
                       hold_mine=hold, hold_enemy=hold)
                r.information(information)
                r.map_defense()
                g.press("circle", vsyncs=3, settle=30)
                in_action, stuck, done = 0, None, 0
                while done < args.run and stuck is None:
                    g.run(STEP)
                    done += STEP
                    in_action = in_action + STEP if r.mode_now()[0] == 2 else 0
                    if in_action > STUCK:
                        stuck = f"the action state for {in_action} vsyncs ({g.call('skip_charging')['phase']} phase)"
                stats = g.call("skip_charging")
                log = pathlib.Path(g.log).read_text(errors="replace")
                check(stuck is None, f"{label}: no hang ({stuck or 'ok'})")
                check(stats["sequences"] >= MIN_ACTIONS, f"{label}: {stats['sequences']} wind-ups ran")
                check("[crash]" not in log and "[UNIMPLEMENTED]" not in log, f"{label}: no crash")
            finally:
                r.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
