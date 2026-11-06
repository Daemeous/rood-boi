#!/usr/bin/env python3
"""Photo mode's camera moves every field, as play does.

    python3 pc/tests/photo_pace_smoke.py [--card pc/build/test-card1.mcd]

In real time (paced, not --fast): the presents per second of play, then of the
shipped photo mode (rood.photo, F12) while its yaw key turns the camera. Each
camera step is a render (the game run from the held moment, rewound); its time
counts toward the field like a frame's own work, so the photo mode keeps the
play's rate. It did not: the pacing restarted after every render (a whole field
on top of it) and the paused loop waited its redraw period before starting one,
about 37 pictures a second against play's 60. A loaded machine (run.py --play)
slows the renders, so the bound is the bug's own rate for the renders' measured
time, one field plus a render per picture, with a margin: the fix reaches the
play rate when a render fits in a field and one render per picture otherwise.
"""
import argparse
import pathlib
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402

SECONDS = 3
MARGIN = 1.15  # over the bug's rate (unloaded: 37 against the fix's 60)


def rate(g, seconds=SECONDS):
    """Presents per second, and the photo renders' mean time (ms) meanwhile."""
    s0, t0, ms = g.status()["presents"], time.monotonic(), []
    while time.monotonic() - t0 < seconds:
        ms.append(g.call("photo")["last_render_ms"])
        time.sleep(0.05)
    return (g.status()["presents"] - s0) / (time.monotonic() - t0), sum(ms) / len(ms)


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

    with tempfile.TemporaryDirectory(prefix="photo-pace-", dir=OUT) as work:
        work = pathlib.Path(work)
        (work / "settings.cfg").write_text("aspect 4:3\nui_menus 0\n")
        env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(work / "settings.cfg"),
               "ROOD_PLUGINS": str(work / "plugins"), "ROOD_PLUGIN_DATA": str(work / "plugin_data"),
               "ROOD_PHOTOS": str(work / "photos"), "ROOD_PAUSED_REDRAW_HZ": "60"}
        g = launch_at("save_room", card=args.card, fast=False, log=work / "game.log", extra_env=env)
        try:
            g.pause(False)
            time.sleep(1)
            play, _ = rate(g)
            g.call("system", key="f12", down=True)
            g.wait_presents(2)
            g.call("system", key="f12", down=False)
            deadline = time.monotonic() + 30
            while g.call("photo")["owners"] != ["rood.photo"] and time.monotonic() < deadline:
                time.sleep(0.05)
            check(g.call("photo")["owners"] == ["rood.photo"], "F12: the photo mode holds the game")
            renders0 = g.call("photo")["renders"]
            g.call("system", key="f", down=True)  # (rood.photo data.lua: yaw_left)
            time.sleep(0.5)
            photo, render_ms = rate(g)
            g.call("system", key="f", down=False)
            renders = g.call("photo")["renders"] - renders0
            check(renders > SECONDS * 20, f"the camera turned: a render per step ({renders} renders)")
            bug = 1000 / (1000 / play + render_ms)  # a field on top of every render
            check(photo >= min(MARGIN * bug, 0.95 * play),
                  f"turning, {photo:.1f} pictures a second (play {play:.1f}; renders {render_ms:.1f} ms, so a "
                  f"field on top of each would give {bug:.1f})")
        finally:
            g.close()
    print(f"photo_pace_smoke: {'FAIL (%d failure(s))' % failures if failures else 'ok'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
