#!/usr/bin/env python3
"""Smoke test for the Callo Merlose companion mod (build_boot.py --callo).

Starts a New Game on an empty memory card, sets the prologue's progress flag
(state_flag 0xAB = 3, as the Wine Cellar does) instead of playing it, then
warps through rooms with the Room Browser's warp (room_goto, enemies on) and
checks the mod's state there (the `callo` part of the RPC `state` method):
that she spawns, on the enemy team, with HP, and that the game keeps running.
Screenshots of each room go to the output directory. No saves are needed.

    python3 pc/tests/callo_smoke.py --exe pc/build/boot64_callo/rood_battle_probe
    py pc\\tests\\callo_smoke.py --exe C:\\Games\\rood-boi\\rood-callo.exe --dir C:\\Games\\rood-boi

--dir is the folder the game runs in (data/, disks/); default: the repository.
Exit status 0 when every check passed.
"""
import argparse
import json
import atexit
import os
import pathlib
import socket
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import rood_rpc  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROGRESS_FLAG = 0xAB  # mod/callo.c: she joins once it is >= 3 (the prologue is over)

# (zone, room, what): rooms to warp to, with their enemies
ROOMS = [
    (9, 17, "Wine Cellar room (the playtest save's)"),
    (9, 1, "Wine Cellar, another room"),
    (40, None, "Snowfly Forest (Sydney's fight zone): its first room"),
]


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Run:
    def __init__(self, args):
        self.out = pathlib.Path(args.out).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.log_path = self.out / "game.log"
        self.failures = []
        card = self.out / "card1.mcd"
        if getattr(args, "card", None) and pathlib.Path(args.card).resolve() == card:
            raise ValueError("source card must be outside the test output directory")
        if card.exists():
            card.unlink()  # a New Game on an empty card
        if getattr(args, "card", None):
            import shutil
            shutil.copyfile(args.card, card)  # disposable test copy, never write to the source card
        port = args.port or free_port()
        rpc = rood_rpc.with_token(f"tcp:{port}")
        env = dict(os.environ, ROOD_RPC=rpc, ROOD_AUDIO="none", ROOD_CONTROLLER="none",
                   ROOD_CARD1=str(card), ROOD_CARD2=str(self.out / "card2-absent.mcd"), ROOD_OUT=str(self.out),
                   ROOD_GI=os.environ.get("ROOD_GI", "0"))
        env.pop("ROOD_SETTINGS", None)
        # Headless as rood_rpc runs it ($ROOD_HEADLESS: egl, or a private Xvfb), not on the
        # desktop's display (WSLg's can drop the connection mid-run).
        if os.environ.get("ROOD_HEADLESS"):
            mode, _ = rood_rpc.headless_mode()
            if mode == "egl":
                rood_rpc.headless_env(mode, env)
            else:
                xvfb, env["DISPLAY"] = rood_rpc.start_xvfb()
                atexit.register(xvfb.kill)
        self.process = subprocess.Popen([args.exe, "--window", "--skip-movies"] + (["--fast"] if args.fast else []),
                                        cwd=args.dir, env=env, stdout=subprocess.DEVNULL,
                                        stderr=open(self.log_path, "w"))
        deadline = time.monotonic() + 60
        while not rood_rpc.listening(rpc):
            if self.process.poll() is not None or time.monotonic() > deadline:
                raise SystemExit(f"the game did not open its RPC port; see {self.log_path}")
            time.sleep(0.2)
        self.game = rood_rpc.Game(rpc)
        self.game.log = str(self.log_path)

    def check(self, ok, what):
        print(("  ok    " if ok else "  FAIL  ") + what, flush=True)
        if not ok:
            self.failures.append(what)
        return ok

    def log_problems(self):
        text = self.log_path.read_text(errors="replace")
        return [line for line in text.splitlines() if line.startswith(("[crash]", "[UNIMPLEMENTED]", "[lp64]"))]

    def state(self):
        return self.game.state()

    def run(self, vsyncs):
        self.game.run(vsyncs)

    def shot(self, name):
        path = self.out / f"{name}.ppm"
        self.game.call("screenshot", path=path.as_posix())
        try:
            from PIL import Image
            Image.open(path).save(path.with_suffix(".png"))
            path.unlink()
        except ImportError:
            pass

    def wait_for(self, predicate, vsyncs, step=30):
        for _ in range(0, vsyncs, step):
            self.run(step)
            state = self.state()
            if predicate(state):
                return state
        return None

    def warp_settled(self):
        """The warp has finished (not loading or arrived-but-not-yet-in-the-field): a new warp
        request is refused until then."""
        return self.wait_for(lambda s: self.game.call("room_goto", action="status")["phase"] not in (2, 3), 600)

    def close(self):
        try:
            self.game.call("quit")
        except Exception:
            pass
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="the --callo build (rood.exe or rood_battle_probe)")
    parser.add_argument("--dir", default=str(ROOT), help="the game's working directory (data/, disks/)")
    parser.add_argument("--out", default=str(ROOT / "pc/build/tests/callo_smoke"))
    parser.add_argument("--port", type=int)
    parser.add_argument("--fast", action="store_true", help="drop the 59.94 Hz pacing (RPC stepping is exact anyway)")
    parser.add_argument("--advance", type=int, default=400,
                        help="at most this many Circle presses (a second apart) to get through scenes "
                        "before a warp can be taken")
    args = parser.parse_args()

    t = Run(args)
    try:
        print("title")
        t.game.wait_title(timeout=120)
        t.game.pause()
        t.shot("00-title")
        print("new game")
        # An empty card: the title's cursor is on NEW GAME
        t.game.press("circle", vsyncs=4, settle=60)
        state = t.wait_for(lambda s: s["player"] is not None and s["zone"] not in (0, 20), 3600)
        t.check(state is not None, "a New Game reaches the field")
        if state is None:
            return 1
        print(f"  in zone {state['zone']} room {state['room']}")
        t.check("callo" in state, "the build has the mod (state reports `callo`)")
        if "callo" not in state:
            return 1
        t.check(not state["callo"]["present"], "she is not there during the prologue")

        # The opening's own scripts set the progress flag (back to 1) as they run: get through
        # them first, with a warp out of the opening (taken once the player has control, see
        # below), then mark the prologue over.
        zone0, room0, _ = ROOMS[1]  # not the first test room: that warp must load a room
        print(f"out of the opening: warp to zone {zone0} room {room0}")
        t.game.call("room_goto", zone=zone0, room=room0, enemies=False, scripts=False, block_saves=True)
        for _ in range(args.advance):
            if t.game.call("room_goto", action="status")["phase"] != 1:
                break
            t.game.press("circle", vsyncs=2, settle=58)
        state = t.wait_for(lambda s: s["zone"] == zone0 and s["room"] == room0 and s["player"] is not None, 1800)
        if not t.check(state is not None, "the opening's scenes are over (the player has control)"):
            t.shot("00-stuck")
            return 1
        t.check(t.warp_settled() is not None, "the warp out of the opening finished")
        t.check(not state["callo"]["present"], "she is not there while the prologue flag says so")
        flag = t.game.call("state_flag", id=PROGRESS_FLAG, value=3)
        t.check(flag["value"] == 3, "the progress flag is set (prologue over)")

        rooms = t.game.call("rooms").get("rooms", [])
        for i, (zone, room, what) in enumerate(ROOMS):
            if room is None:
                candidates = sorted(r["room"] for r in rooms if r.get("zone") == zone)
                if not candidates:
                    t.check(False, f"zone {zone} has rooms")
                    continue
                room = candidates[0]
            print(f"warp: zone {zone} room {room} ({what})")
            warp = t.game.call("room_goto", zone=zone, room=room, enemies=True, scripts=False, block_saves=True)
            if not t.check(warp.get("ok"), f"the warp is accepted ({warp.get('message')})"):
                continue
            # The warp is taken in the field once no event script runs
            state = t.wait_for(lambda s: t.game.call("room_goto", action="status")["phase"] != 1, 600)
            if not t.check(state is not None, "the field took the warp"):
                t.shot(f"{i + 1:02d}-stuck")
                t.game.call("room_goto", action="cancel")
                continue
            state = t.wait_for(lambda s: s["zone"] == zone and s["room"] == room and s["player"] is not None, 1800)
            if not t.check(state is not None, f"arrived in zone {zone} room {room}"):
                continue
            t.check(t.warp_settled() is not None, "the warp finished")
            state = t.wait_for(lambda s: s["callo"]["present"], 300) or t.state()
            callo = state["callo"]
            if not t.check(callo["present"], f"she spawned (slot {callo['slot']})"):
                print("        " + json.dumps(callo))
            if callo["present"]:
                t.check(callo["team"] == 4, f"on the enemy team, fighting for Ashley (team {callo['team']})")
                t.check(callo["hp"] > 0 and callo["hp"] <= callo["max_hp"],
                        f"with HP {callo['hp']}/{callo['max_hp']}, MP {callo['mp']}/{callo['max_mp']}")
            before = t.game.call("status")["vsync"]
            t.run(300)  # ten seconds of the room
            t.shot(f"{i + 1:02d}-zone{zone}-room{room}")
            t.check(t.game.call("status")["vsync"] >= before + 300, "the game kept running for 300 vsyncs")
            callo = t.state()["callo"]
            if callo["present"]:
                t.check(callo["hp"] > 0, f"she is still standing after them (HP {callo['hp']})")
            problems = t.log_problems()
            if not t.check(not problems, "no crash or unimplemented stop in the log"):
                print("\n".join("        " + p for p in problems[:5]))
                return 1
            t.game.call("room_goto", action="clear")
    finally:
        t.close()
    print(f"\n{'PASSED' if not t.failures else 'FAILED: ' + str(len(t.failures))} (screenshots and log: {t.out})")
    return 1 if t.failures else 0


if __name__ == "__main__":
    sys.exit(main())
