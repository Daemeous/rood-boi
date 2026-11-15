#!/usr/bin/env python3
"""The present side's scramble canary (T1 R4a, present_header.h). A route is played three times with the hardware renderer, the
interpolated frame's target hashed with each picture (as render_jitter_smoke does):
  - without the canary;
  - ROOD_PRESENT_CANARY=1: while the presenter runs, the game-owned variables it reads hold poison (restored after it): every frame
    must equal the first run's, which holds only if the presenter reads them through the present's frame header;
  - ROOD_PRESENT_CANARY=2: the renderer's own scale setting, which the presenter still reads live, is poisoned too: the frames
    must differ, which shows the canary bites (and what the settings request has left). The widescreen flag is no longer one of
    them: the renderer's copy follows the hw_cmd stream (C_SET_WIDESCREEN).

    python3 pc/tests/present_canary_smoke.py [--route data]
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)
    return ok


def play(route, canary, work, settings=()):
    out = pathlib.Path(tempfile.mkdtemp(prefix=f"canary-{route}-", dir=work))
    env = dict(os.environ, BUILD_COMPARE_REPLAY="1")
    env.pop("ROOD_PRESENT_CANARY", None)
    if canary:
        env["ROOD_PRESENT_CANARY"] = str(canary)
    binary = os.environ.get("ROOD_BINARY") or str(ROOT / "pc/build/boot64/rood_battle_probe")
    cmd = [sys.executable, str(ROOT / "pc/tools/build_compare.py"), binary, "--play-menus", "--menus", route, "--menu-sample", "3",
           "--env", "ROOD_SCALE=1", "--setting", "interpolate 1", "--setting", "present_rate 4", *[a for line in settings for a in ("--setting", line)], "--out", str(out)]
    done = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, timeout=1800)
    for line in done.stdout.splitlines():
        if line.startswith("RESULT"):
            return json.loads(line[6:])[route]["hashes"]
    print(done.stdout[-500:], done.stderr[-500:])
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--route", default="platforms")
    args = parser.parse_args()
    work = tempfile.mkdtemp(prefix="present-canary-")
    base, one, two = (play(args.route, level, work) for level in (0, 1, 2))
    if not check(None not in (base, one, two), f"{args.route}: played without the canary and at levels 1 and 2"):
        return 1
    bad1 = sum(a != b for a, b in zip(base, one))
    bad2 = sum(a != b for a, b in zip(base, two))
    check(len(base) == len(one) and bad1 == 0, f"{args.route}: level 1 (the presenter's own variables poisoned): {len(base)} frames equal ({bad1} differ)")
    check(bad2 > 0, f"{args.route}: level 2 (the renderer's scale setting too) differs in {bad2} frames: the canary bites")
    # Level 3: the draw settings' game variables poisoned (non-zero: the opposite of perspective 0, subpixel 0 and texture smoothing 0, which the
    # run is set to): the renderer's copies follow the stream, so the frames must equal the same settings without the poison.
    off = ("perspective_textures 0", "subpixel 0", "smooth_textures 0")
    base3, three = play(args.route, 0, work, off), play(args.route, 3, work, off)
    if check(None not in (base3, three), f"{args.route}: played at level 3"):
        bad3 = sum(a != b for a, b in zip(base3, three))
        check(len(base3) == len(three) and bad3 == 0, f"{args.route}: level 3 (the draw settings' variables poisoned): {len(base3)} frames equal ({bad3} differ)")
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("PASS: present_canary_smoke")
    return 0


if __name__ == "__main__":
    sys.exit(main())
