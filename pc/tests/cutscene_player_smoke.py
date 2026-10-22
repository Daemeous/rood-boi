#!/usr/bin/env python3
"""The cutscene player (pc/src/cutscene.cpp, pc/docs/Cutscene_Player.md) on the opening.

From the title menu (no card: the cursor on New Game), RPC cutscene start
{scene: "opening"} presses Start and starts a take at that frame. Then:
  - played live to frame M and on to N with per-vsync state digests (host
    clock masked, as the oracle does) recorded from M to N; at N the state
    hashes, the native picture (RPC screenshot: VRAM's display area) and the
    presented picture of the vsync into N;
  - back to frame A < M (a rewind to the mark at or before A, then replayed
    to it), then forward to N again with the digests compared: every vsync
    from M to N identical, and the hashes and both pictures at N the same.
    The scanline estimate stays host time (no ROOD_DETERMINISTIC): the take's
    log is what makes the replay exact;
  - one frame back (<1) lands on N - 1;
  - capture: a PNG of the frame; capture next 3: three PNGs, frames N..N+2;
  - repro: written at N (the state at the mark before N, the log, the
    settings), the take moved elsewhere, the repro opened (RPC jump): at N
    again, the same hashes and native picture;
  - slow motion: 1/4 plays 24 frames in no less than about 4x their time;
  - instant replay: a rolling take while playing on (input included), the
    hotkey freezes it, back 200 frames and forward again: the same state;
  - the free camera (the photo mode's) on and off: the game's state as it was.
Then the opening in 16:9 (a session of its own: ROOD_WIDESCREEN=1, a 1024x576
window), Circle every 120 frames from frame 0, seeks in steps of 60: the
credits' jagged tear wipes (the event's screen image, func_800CC204) cross
the whole 16:9 view as they cross the 4:3 screen. Captured at 6530 and 10250
the credits card's black still reaches the view's right edge (it stopped at
the 4:3 edge, x = 896 of 1024, the next shot showing past it); at 6560 and
10280 the tear's bars reach the view's edge where they reach the 4:3 one.
With --card (the tests' pinned card), a warp scene from the catalog too: from
the card's save room, the Room Browser's warp with scripts on reaches it, its
entry event runs in the take, and going back and forward again gives the
same state.

    python3 pc/tests/cutscene_player_smoke.py [--card CARD] [--out DIR]
"""
import argparse
import hashlib
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

M, N, A = 300, 600, 140  # marks every 30 frames: M is a mark, A is not
STATE_KEYS = ("ram", "scratch", "io", "overlays", "vram")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--card", help="a card saved in a field room: also a warp scene (z009r01)")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "cutscene_player"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    cutscenes = out / "cutscenes"
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-cutscene-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    game = launch_at("title", fast=True, log=out / "game.log", socket_path=socket, extra_env={
        "ROOD_CARD1": str(out / "absent-card1.mcd"), "ROOD_CARD2": str(out / "absent-card2.mcd"),
        "ROOD_CUTSCENES": str(cutscenes), "ROOD_PHOTOS": str(out / "photos"),
        "ROOD_DETERMINISTIC": "0", "ROOD_WINDOW": "640x480"})

    def cs(**params):
        return game.call("cutscene", **params)

    def settle(what, guard=300):
        """Until the player's seek / capture / repro / jump is done (a hang guard only)."""
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

    def native(path):
        game.screenshot(path)
        return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()[:16]

    def presented(frame, path):
        """Go to `frame` (forward); the picture presented by its last vsync (stepped, as present_dump wants)."""
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

    try:
        game.call("graphics", interpolate=False)
        pointer_bytes = 8 if pathlib.Path(BINARY).read_bytes()[4] == 2 else 4
        masks = [[address, size] for address, size, name in Symbols(BINARY).rows
                 if name == "last_vsync" and size == 2 * pointer_bytes]
        check(len(masks) == 1, f"the host clock's timestamp to mask: {masks}")
        scenes = [s["id"] for s in cs(action="list")["scenes"]]
        check("opening" in scenes, f"the catalog lists the opening: {scenes}")

        s = cs(action="start", scene="opening")
        s = settle("reaching the opening")
        check(s["on"] and s["scene"] == "opening" and s["marks"][:1] == [0],
              f"the opening's take starts at the title's Start press, marked: {s['scene']} frame {s['frame']}")
        cs(action="pause")
        go(M)
        game.digest("record", ticks=N - M, masks=masks)
        first_presented = presented(N, out / "first-present.ppm")
        s = cs()
        live = game.digest("stop")
        check(live["recorded"] == N - M, f"{N - M} digests recorded live from {M} to {N}: {live}")
        check(not s["replaying"] and s["head"] == N + 1 and s["divergences"] == 0,
              f"played live to {N} (head {s['head']}), marks {s['marks'][:4]}... ({len(s['marks'])}, "
              f"{s['mark_bytes'] / 1e6:.0f} MB, last {s['mark_ms']:.1f} ms)")
        check(s["event"]["running"] and s["event"]["threads"],
              f"an event script runs, its place shown: {s['event']}")
        first = hashes()
        first_native = native(out / "first.ppm")

        s = go(A)
        check(s["replaying"] and s["frame"] == A, f"back to frame {A} (between marks): replaying the log")
        go(M)
        game.digest("compare")
        second_presented = presented(N, out / "second-present.ppm")
        s = cs()
        same = game.digest("result")
        check(same["error"] == 0 and same["first_tick"] == -1 and same["ticks"] == N - M,
              f"replayed {M}..{N}: every vsync's state digest identical: {same}")
        check(s["divergences"] == 0, f"no replayed read past the log ({s['divergences']})")
        check(hashes() == first, f"frame {N}: the same RAM, scratchpad, I/O, overlays and VRAM")
        check(native(out / "second.ppm") == first_native, f"frame {N}: the same native picture")
        check(second_presented == first_presented, f"frame {N}: the same presented picture")

        cs(action="back", frames=1)
        s = settle("one frame back")
        check(s["frame"] == N - 1, f"<1: one frame back lands on {N - 1} ({s['frame']})")
        go(N)

        cs(action="capture", count=1)
        s = settle("capture")
        png = pathlib.Path(s["last_capture"])
        check(not s["capture_error"] and png.exists() and png.read_bytes()[:4] == b"\x89PNG"
              and png.parent == cutscenes / "opening" and png.name.startswith(f"f{N:06d}"),
              f"capture frame: {png} ({s['capture_error']})")
        cs(action="capture", count=3)
        s = settle("capture next 3")
        shots = sorted(p.name for p in (cutscenes / "opening").glob("f*.png"))
        check(s["frame"] == N + 2 and [f"f{N + i:06d}" in " ".join(shots) for i in range(3)] == [True] * 3,
              f"capture next 3: frames {N}..{N + 2}: {shots}")

        go(N)
        cs(action="repro")
        s = settle("repro")
        repro = pathlib.Path(s["repro"])
        check(repro.exists() and s["frame"] == N, f"repro written at frame {N}: {repro}")
        go(A)
        cs(action="jump", repro=str(repro))
        s = settle("jump")
        check(s["frame"] == N and s["scene"] == "opening" and hashes() == first and native(out / "jump.ppm") == first_native,
              f"the repro opens on frame {N} with the same state and picture ({s['frame']}, {s['message']})")

        cs(action="speed", value=4)
        cs(action="play")
        start, f0 = time.monotonic(), cs()["frame"]
        deadline = Deadline(60)
        while cs()["frame"] < f0 + 24 and not deadline.passed():
            time.sleep(0.02)
        elapsed = time.monotonic() - start
        cs(action="pause")
        cs(action="speed", value=1)
        check(elapsed >= 24 * 4 * 0.0167 * 0.8, f"1/4 slow motion: 24 frames in {elapsed:.2f} s")

        cs(action="stop")
        cs(action="settings", instant_replay=1, replay_seconds=10, replay_interval=30)
        game.pause(False)
        game.call("input", buttons=["circle"], vsyncs=4)
        deadline = Deadline(120)
        while (cs().get("head", 0) < 450 or not cs()["rolling"]) and not deadline.passed():
            time.sleep(0.05)
        cs(action="hotkey")
        s = settle("hotkey")
        frozen = s["frame"]
        check(s["rolling"] and s["paused"] and frozen >= 450 and s["first"] <= frozen - 300,
              f"instant replay: frozen at frame {frozen}, frames {s['first']}..{s['head'] - 1} kept, "
              f"{len(s['marks'])} marks ({s['mark_bytes'] / 1e6:.0f} MB)")
        live_hash = hashes()
        live_native = native(out / "replay-live.ppm")
        go(frozen - 200)
        s = go(frozen)
        check(hashes() == live_hash and native(out / "replay-again.ppm") == live_native and s["divergences"] == 0,
              f"instant replay: 200 frames back and forward again, the same state and picture")
        cs(action="camera", on=True)
        check(cs()["camera"], "the free camera holds the frame")
        cs(action="camera", on=False)
        s = settle("camera off")
        check(not s["camera"] and hashes() == live_hash, "the free camera off: the game's state as it was")
        cs(action="stop")
        cs(action="settings", instant_replay=0)
        log = pathlib.Path(game.log).read_text(errors="replace")
        check("[crash]" not in log and "[UNIMPLEMENTED]" not in log, "no crash")
    finally:
        game.close()
    opening_wipe_widescreen(out, socket, check)
    if args.card and pathlib.Path(args.card).exists():
        warp_scene(args, out, socket, check)
    if failures:
        print(f"FAIL cutscene player: {len(failures)} check(s)")
        return 1
    print("PASS cutscene player: scrub back and replay exact, capture, repro, slow motion, instant replay")
    return 0


WIPE_COVERED = (6530, 10250)  # the card's black still over the screen's right side
WIPE_BARS = (6560, 10280)  # the tear's bars across the screen's right side


def opening_wipe_widescreen(out, socket, check):
    """The opening's credits tear wipes in 16:9 reach the view's edges (see the docstring)."""
    from PIL import Image
    game = launch_at("title", fast=True, log=out / "wipe.log", socket_path=socket, extra_env={
        "ROOD_CARD1": str(out / "absent-card1.mcd"), "ROOD_CARD2": str(out / "absent-card2.mcd"),
        "ROOD_CUTSCENES": str(out / "cutscenes-wipe"), "ROOD_PHOTOS": str(out / "photos"),
        "ROOD_WIDESCREEN": "1", "ROOD_WINDOW": "1024x576"})

    def cs(**params):
        return game.call("cutscene", **params)

    def settle(what):
        deadline = Deadline(300)
        while cs()["busy"]:
            if deadline.passed():
                raise RpcError(f"{what}: still busy")
            time.sleep(0.05)
        return cs()

    def black_rows(png, x0, x1):
        """Rows of the capture black (the wipe's own: < 2, darker than the shots) across columns [x0, x1) of
        1024 (scaled to its width)."""
        image = Image.open(png).convert("L")
        w, h = image.size
        box = image.crop((x0 * w // 1024, 0, x1 * w // 1024, h)).load()
        width = (x1 - x0) * w // 1024
        return {y for y in range(h) if max(box[x, y] for x in range(width)) < 2}, h

    try:
        game.call("graphics", interpolate=False)
        cs(action="start", scene="opening")
        settle("reaching the opening")
        cs(action="pause")
        last = max(WIPE_COVERED + WIPE_BARS)
        for f in range(0, last, 60):
            if f % 120 == 0:
                game.call("input", buttons=["circle"], vsyncs=4)
            cs(action="seek", frame=min(f + 60, last))
            settle(f"seek {min(f + 60, last)}")
        for frame in WIPE_COVERED + WIPE_BARS:
            cs(action="seek", frame=frame)
            settle(f"seek {frame}")
            cs(action="capture", count=1)
            s = settle("capture")
            png = pathlib.Path(s["last_capture"])
            if not check(not s["capture_error"] and png.exists(), f"16:9 wipe: frame {frame} captured"):
                continue
            inner, h = black_rows(png, 880, 896)  # just inside the 4:3 screen's right edge
            outer, _ = black_rows(png, 1008, 1024)  # at the 16:9 view's right edge
            if frame in WIPE_COVERED:
                check(len(outer) >= h * 9 // 10,
                      f"16:9 wipe, frame {frame}: the credits card's black reaches the view's right edge "
                      f"({len(outer)}/{h} rows black there, {len(inner)} at the 4:3 edge): {png}")
            else:
                check(len(inner) >= 20 and len(inner & outer) >= len(inner) * 9 // 10,
                      f"16:9 wipe, frame {frame}: the tear's bars reach the view's right edge "
                      f"({len(inner & outer)} of the {len(inner)} rows black at the 4:3 edge black there): {png}")
        log = pathlib.Path(game.log).read_text(errors="replace")
        check("[crash]" not in log, "16:9 wipe: no crash")
    finally:
        game.close()


def warp_scene(args, out, socket, check):
    """A catalogued warp scene: 9/1's entry event (dialogue) from the card's save room."""
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    game = launch_at("save_room", card=card, fast=True, log=out / "warp.log", socket_path=socket, extra_env={
        "ROOD_CARD2": str(out / "absent-card2.mcd"), "ROOD_CUTSCENES": str(out / "cutscenes"),
        "ROOD_PHOTOS": str(out / "photos")})

    def cs(**params):
        return game.call("cutscene", **params)

    def settle(what):
        deadline = Deadline(300)
        while cs()["busy"]:
            if deadline.passed():
                raise RpcError(f"{what}: still busy")
            time.sleep(0.05)
        return cs()

    def hashes():
        h = game.call("snapshot", action="hash")
        return {k: h[k] for k in STATE_KEYS}

    try:
        game.call("graphics", interpolate=False)
        cs(action="start", scene="z009r01")
        s = settle("the warp")
        cs(action="pause")
        start = s["frame"]
        for f in range(start + 10, start + 200, 10):  # the entry event loads its script, then runs
            if s["event"]["running"]:
                break
            cs(action="seek", frame=f)
            s = settle("seek")
        check(s["on"] and s["scene"] == "z009r01" and s["event"]["running"] and s["event"]["id"] == 0x1000,
              f"warp scene z009r01: reached at the arrival (take frame {start}), its entry event running by frame "
              f"{s['frame']} ({s['message']})")
        for k in range(1, 5):  # its dialogue, advanced as the scan did
            cs(action="seek", frame=start + 120 * k)
            settle("seek")
            game.call("input", buttons=["circle"], vsyncs=4)
        end = start + 600
        cs(action="seek", frame=end)
        s = settle("seek")
        first = hashes()
        cs(action="seek", frame=start + 50)
        settle("back")
        cs(action="seek", frame=end)
        s = settle("forward")
        check(s["frame"] == end and hashes() == first and s["divergences"] == 0,
              f"warp scene: back to frame {start + 50} and forward to {end}: the same state")
    finally:
        game.close()


if __name__ == "__main__":
    sys.exit(main())
