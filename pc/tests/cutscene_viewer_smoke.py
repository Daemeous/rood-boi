#!/usr/bin/env python3
"""The cutscene viewer's story scenes (pc/docs/Cutscene_Player.md,
pc/src/story_boot.c, pc/assets/cutscenes/story.json).

From a fresh load (the title menu, no card), story scenes from different
chapters are started from the list as the viewer's Play does (RPC cutscene
{action: "start", scene}): the first from the title (New Game into it),
the others from the scene before (the game starts afresh there). Each must
visibly run its event, not be a bare warp: the take's event interpreter
(RPC status take_event) ran for MIN_FRAMES frames or more through
MIN_PLACES script places or more, its dialogue boxes showed and went on by
themselves (auto-advance), and the event id is the scene's (a chained or
handed-over scene: played on to it first). Then, on the last scene:
  - seeking is exact: per-vsync state digests (host clock masked) from M to
    N, back to A < M, forward to N again: every digest, the state hashes,
    the native picture and the presented one the same; dragging past what
    has played plays on (live);
  - Back to the game: the state (hashes) the game had when the first scene
    was asked for from it (the second here) comes back.
The player catalog's recipes (the opening from the title, warps, the
ending) are cutscene_player_smoke's.

    python3 pc/tests/cutscene_viewer_smoke.py [--scenes ID,ID] [--out DIR]
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, BINARY, Deadline, RpcError, wait_for_file  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from snapshot_diff import Symbols  # noqa: E402

# One scene per part of the story (story.json ids): the Wine Cellar, the Town Center (a chained EVT file),
# the Snowfly Forest (a trigger's door script chaining on), the Keep, the Cathedral's end.
SCENES = ["room-009-04-s001", "evt-0027", "evt-0038", "evt-0037", "room-070-00-s081"]
MIN_FRAMES, MIN_PLACES = 240, 8
PLAY = 1500  # frames each scene is watched at most
M, N, A = 150, 300, 70  # (marks every 30 frames: A is not one)
STATE_KEYS = ("ram", "scratch", "io", "overlays", "vram")
# The game's state replays exactly (digests, hashes); the pictures are drawn by the renderer, whose temporal
# history (the native characters' frame-to-frame matching) is host state the take does not rewind: a replayed
# picture can differ in a few pixels of a moving character (seen: 9 of 307,200).
PICTURE_SLACK = 64


def ppm(path):
    """(w, h, bytes) of a binary PPM."""
    data = pathlib.Path(path).read_bytes()
    parts, at = [], 0
    while len(parts) < 4:
        while data[at:at + 1].isspace():
            at += 1
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[at:end])
        at = end
    return int(parts[1]), int(parts[2]), data[at + 1:]


def pixels_apart(a, b):
    """How many pixels differ (a size change: all)."""
    wa, ha, da = ppm(a)
    wb, hb, db = ppm(b)
    if (wa, ha) != (wb, hb):
        return wa * ha
    return sum(1 for i in range(0, len(da), 3) if da[i:i + 3] != db[i:i + 3])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--scenes", help="story scene ids (default: one per part of the story)")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "cutscene_viewer"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-cutscene-viewer-{os.getpid()}.sock"
    scenes = args.scenes.split(",") if args.scenes else SCENES
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    game = launch_at("title", fast=True, log=out / "game.log", socket_path=socket, extra_env={
        "ROOD_CARD1": str(out / "absent-card1.mcd"), "ROOD_CARD2": str(out / "absent-card2.mcd"),
        "ROOD_CUTSCENES": str(out / "cutscenes"), "ROOD_PHOTOS": str(out / "photos"),
        "ROOD_DETERMINISTIC": "0", "ROOD_WINDOW": "640x480"})

    def cs(**params):
        return game.call("cutscene", **params)

    def settle(what, guard=300):
        deadline = Deadline(guard)
        while True:
            s = cs()
            if not s["busy"]:
                return s
            if deadline.passed():
                raise RpcError(f"{what}: still busy after {guard} s: {s}")
            time.sleep(0.05)

    def go(frame):
        cs(action="seek", frame=frame)
        s = settle(f"seek {frame}")
        if s["frame"] != frame:
            raise RpcError(f"seek {frame} landed on {s['frame']}: {s['message']}")
        return s

    def presented(frame, path):
        """Go to `frame` (forward): the picture its last vsync presented (stepped, as present_dump wants)."""
        go(frame - 1)
        ppm = pathlib.Path(path)
        ppm.unlink(missing_ok=True)
        game.call("present_dump", path=str(ppm))
        cs(action="step", frames=1)
        if not wait_for_file(ppm, 60):
            raise RpcError("no frame presented")
        settle("step")
        return hashlib.sha256(ppm.read_bytes()).hexdigest()[:16]

    def hashes():
        h = game.call("snapshot", action="hash")
        return {k: h[k] for k in STATE_KEYS}

    def native(path):
        game.screenshot(path)
        return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()[:16]

    try:
        game.call("graphics", interpolate=False)
        pointer_bytes = 8 if pathlib.Path(BINARY).read_bytes()[4] == 2 else 4
        masks = [[address, size] for address, size, name in Symbols(BINARY).rows
                 if name == "last_vsync" and size == 2 * pointer_bytes]
        listed = {s["id"]: s for s in cs(action="list")["scenes"] if s["kind"] == "story"}
        chapters = {s["chapter"] for s in listed.values()}
        check(len(listed) >= 50 and len(chapters) >= 6,
              f"the viewer lists {len(listed)} story scenes in {len(chapters)} chapters")
        check(all(sid in listed for sid in scenes), f"the test's scenes are listed: {scenes}")
        story = {e["id"]: e for e in json.loads((ROOT / "pc/assets/cutscenes/story.json").read_text())["scenes"]}
        check(len({story[sid]["chapter"] for sid in scenes if sid in story}) == len(scenes),
              "each from another chapter: " + ", ".join(story.get(sid, {}).get("chapter", "?") for sid in scenes))

        before_game = None
        for i, sid in enumerate(scenes):
            if i == 1:  # the first scene asked for from a game: Back to the game returns here
                cs(action="pause")
                before_game = hashes()
            cs(action="start", scene=sid)
            s = settle(f"starting {sid}", 600)
            check(s["on"] and s["scene"] == sid and s["marks"][:1] == [0] and not s["message"].startswith("the "),
                  f"{sid} ({listed[sid]['name']}): {'from the title' if i == 0 else 'from the scene before'}, "
                  f"a take at its first frame ({s['message']})")
            if not s["on"]:
                continue
            cs(action="play")
            deadline = Deadline(300)
            while True:
                s = cs()
                te = s["take_event"]
                enough = te["frames"] >= MIN_FRAMES and te["places"] >= MIN_PLACES and \
                    (te["dialogue"] > 0 or not story[sid]["texts"])
                if enough or s["frame"] >= PLAY or deadline.passed():
                    break
                time.sleep(0.1)
            cs(action="pause")
            game.screenshot(str(out / f"{sid}.ppm"))
            want = story[sid]["event"]  # a room's script: (1 << 12) | the last EVT file's number
            seen = any(x == want or (want >> 12 == 1 and x >> 12 == 1) for x in te["ids"])
            check(enough and seen,
                  f"{sid}: its event ran {te['frames']} frames through {te['places']} script places, dialogue "
                  f"{te['dialogue']} frames, events {[hex(x) for x in te['ids']]} (wants {hex(want)}), "
                  f"at frame {s['frame']}")
            check(s["divergences"] == 0, f"{sid}: no replayed read past the log")

        # seeking on the last scene: live M..N recorded, back to A, forward to N compared
        s = cs()
        if s["on"]:
            if s["head"] - 1 < N:
                go(N)
            go(M)
            game.digest("record", ticks=N - M, masks=masks)
            first_presented = presented(N, out / "first-present.ppm")
            game.digest("stop")
            first, first_native = hashes(), native(out / "first.ppm")
            s = go(A)
            check(s["replaying"] and s["frame"] == A, f"back to frame {A}: replaying")
            go(M)
            game.digest("compare")
            second_presented = presented(N, out / "second-present.ppm")
            same = game.digest("result")
            check(same["error"] == 0 and same["first_tick"] == -1 and same["ticks"] == N - M,
                  f"replayed {M}..{N}: every vsync's state digest identical: {same}")
            second = hashes()
            check(second == first, f"frame {N}: the same state "
                  f"({[k for k in STATE_KEYS if first[k] != second[k]] or 'all equal'})")
            native(out / "second.ppm")
            changed = [pixels_apart(out / f"first{k}.ppm", out / f"second{k}.ppm") for k in ("", "-present")]
            check(all(c <= PICTURE_SLACK for c in changed),
                  f"frame {N}: the same native and presented pictures but for the renderer's own history "
                  f"({changed} pixels apart; at most {PICTURE_SLACK})")
            past = cs()["head"] + 60
            go(past)
            s = cs()
            check(s["frame"] == past and not s["replaying"],
                  f"scrubbed past what had played: on to {past}, live (head {s['head']})")

            cs(action="return")
            deadline = Deadline(120)
            while not deadline.passed():
                s = cs()
                if not s["on"] and not s["busy"]:
                    break
                time.sleep(0.1)
            cs(action="pause")
            check(before_game is not None and hashes() == before_game,
                  "Back to the game: the game as it was when the first scene was asked for from it came back")
        log = pathlib.Path(game.log).read_text(errors="replace")
        check("[crash]" not in log and "[UNIMPLEMENTED]" not in log, "no crash")
    finally:
        game.close()
    if failures:
        print(f"FAIL cutscene viewer: {len(failures)} check(s)")
        return 1
    print("PASS cutscene viewer: story scenes from five chapters play from the list, seeking exact, back to the game")
    return 0


if __name__ == "__main__":
    sys.exit(main())
