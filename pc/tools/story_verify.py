#!/usr/bin/env python3
"""Play every story scene of pc/assets/cutscenes/story.json in the game and
record what it did (the cutscene viewer: pc/docs/Cutscene_Player.md).

    python3 pc/tools/story_verify.py [--scenes ID,ID] [--cap VSYNCS] [--out DIR] [--apply]
    python3 pc/tools/story_verify.py --from-json DIR/verify.json --apply

One game (64 bits, headless, unpaced) from the title menu with no card:
each scene is started through RPC cutscene {action: "start"} as the
viewer's Play does (from the title: New Game into it; after that from the
scene before: the game starts afresh there), dialogue going on by itself
(cutscene_auto_advance). It is followed until its event has ended for 2
seconds (or CAP vsyncs). A scene is `verified` when its event ran in the
take (the interpreter's threads were at MIN_OFFSETS places or more in its
scripts: RPC cutscene's take_event) and the take is still on; frames is
how many frames an event ran. A
crash restarts the game. --apply writes verified, frames and skip (a
scene without dialogue whose event was over within SHORT frames: nothing
to watch) into story.json (by id; the rest is kept). Run under nice; writes only to
DIR (default pc/build/story_verify) and, with --apply, story.json.
"""
import argparse
import json
import os
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
os.environ.setdefault("ROOD_ARCH", "64")
from rood_rpc import Deadline, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402

STORY = ROOT / "pc/assets/cutscenes/story.json"
CAP = 6000
ENDED = 120  # vsyncs without an event: the scene is over
MIN_OFFSETS = 8
SHORT = 240  # frames: a scene without dialogue that ends sooner is not listed (skip)


class Runner:
    def __init__(self, out):
        self.out = out
        self.game = None
        self.n = 0

    def launch(self):
        self.n += 1
        sock = f"/tmp/rood-story-verify-{os.getpid()}-{self.n}.sock"
        self.game = launch_at("title", fast=True, log=self.out / f"game-{self.n}.log", socket_path=sock, extra_env={
            "ROOD_CARD1": str(self.out / "absent1.mcd"), "ROOD_CARD2": str(self.out / "absent2.mcd"),
            "ROOD_CUTSCENES": str(self.out / "cutscenes"), "ROOD_WINDOW": "640x480"})
        self.game.call("cutscene", action="settings", interval=600, marks_max=8)

    def alive(self):
        try:
            self.game.status()
            return True
        except Exception:
            return False

    def play(self, scene, cap):
        g = self.game
        g.call("cutscene", action="start", scene=scene["id"])
        d = Deadline(180)
        while True:
            s = g.call("cutscene")
            if s["on"] and not s["busy"]:
                break
            if not s["busy"] and not s["on"]:
                return {"started": False, "message": s["message"]}
            if d.passed():
                return {"started": False, "message": "timeout: " + s["recipe"]}
            time.sleep(0.1)
        start = s["frame"]
        d = Deadline(600)
        quiet_since = None
        shots = set()
        while True:
            time.sleep(0.2)
            s = g.call("cutscene")
            te = s["take_event"]
            # pictures to name the scene by: early on, and its first dialogue box
            for what, due in (("start", s["frame"] - start >= 90), ("talk", te["dialogue"] > 20)):
                if due and what not in shots:
                    shots.add(what)
                    g.screenshot(str(self.out / f"{scene['id']}-{what}.png"))
            running = s["event"]["running"]
            if running:
                quiet_since = None
            elif quiet_since is None:
                quiet_since = s["frame"]
            if te["frames"] and quiet_since is not None and s["frame"] - quiet_since > ENDED:
                break
            if s["frame"] - start > cap or d.passed() or not s["on"]:
                break
        g.screenshot(str(self.out / f"{scene['id']}.png"))
        te = s["take_event"]
        return {"started": True, "frames": te["frames"], "places": te["places"], "events": te["ids"],
                "ended": quiet_since is not None, "take": s["on"], "length": s["frame"] - start}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenes")
    ap.add_argument("--cap", type=int, default=CAP)
    ap.add_argument("--out", default=str(ROOT / "pc/build/story_verify"))
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--from-json", help="apply an earlier run's DIR/verify.json instead of playing")
    args = ap.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    story = json.loads(STORY.read_text())
    scenes = story["scenes"]
    if args.scenes:
        want = args.scenes.split(",")
        scenes = [s for s in scenes if s["id"] in want]
    results = {}
    if args.from_json:
        results = json.loads(pathlib.Path(args.from_json).read_text())
        scenes = []
    else:
        r = Runner(out)
        r.launch()
    try:
        for sc in scenes:
            try:
                res = r.play(sc, args.cap)
            except (RpcError, OSError, ConnectionError) as ex:
                res = {"started": False, "message": f"crash: {ex}"}
                try:
                    r.game.close()
                except Exception:
                    pass
                r.launch()
            res["verified"] = bool(res.get("started") and res.get("take") and res.get("places", 0) >= MIN_OFFSETS)
            results[sc["id"]] = res
            print(sc["id"], json.dumps(res), flush=True)
            (out / "verify.json").write_text(json.dumps(results, indent=1) + "\n")
    finally:
        if not args.from_json:
            try:
                r.game.close()
            except Exception:
                pass
    ok = sum(1 for v in results.values() if v["verified"])
    print(f"{ok}/{len(results)} verified")
    if args.apply:
        for e in story["scenes"]:
            v = results.get(e["id"])
            if v:
                e["verified"] = v["verified"]
                if v.get("frames"):
                    e["frames"] = v["frames"]
                # nothing to watch: a script that sets flags or fades a room in, no dialogue, over at once
                e["skip"] = bool(v.get("started") and v.get("ended") and v.get("frames", 0) < SHORT
                                 and not e.get("texts"))
        STORY.write_text(json.dumps(story, indent=1) + "\n")


if __name__ == "__main__":
    main()
