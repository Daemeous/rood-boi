#!/usr/bin/env python3
"""Window mode switches (System > Display: windowed / borderless /
fullscreen; pc/src/boot_window.c) never stop the presenter: every transition
both ways, several times, with frames still reaching the window after each.

    python3 pc/tests/window_mode_smoke.py [--card pc/save/card1.mcd] [--cycles N] [--out DIR]

The Vulkan presenter used to lose the device (VK_ERROR_DEVICE_LOST, then an
abort at the frame fence) when the window left fullscreen: the swapchain was
recreated at the surface's new, smaller extent in the middle of a frame
whose window image still had the fullscreen size, and the blit wrote outside
the swapchain image. Now switches apply at a frame boundary and the
swapchain (and the window image with it) is recreated only at a frame's
start (vk_present.c sync_swapchain).

Only a window that a window manager resizes reaches the bug (headless EGL
renders offscreen; Xvfb has no window manager), and it shows fullscreen on
the desktop for a few seconds, so the desktop is used
only on request: ROOD_TEST_DESKTOP=1 with DISPLAY set (run.py passes its
DISPLAY on as ROOD_DESKTOP_DISPLAY then; X11 video). Otherwise the switches
run headless (rood_rpc.headless_mode): SDL's offscreen driver (EGL) resizes
the window, so the presenters' resizes and size-dependent targets are
covered, but Vulkan renders offscreen there (no swapchain); Xvfb has no
window manager, so its window never changes size (noted). Skipped (exit 77)
when no headless mode works either.

With the card: the save room (path tracing on with Vulkan on the desktop, as
in play), else the title. Each switch: the mode reported (a failed switch,
e.g. no fullscreen under Xvfb without a window manager, is reported, not
failed), the device not lost, presents going on (window_presents on a real
window); at the end, windowed again, the presented image has the window's
size."""
import argparse
import os
import pathlib
import shutil
import sys

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, RpcError, headless_mode  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

# Every ordered pair of modes once: windowed -> borderless -> fullscreen ->
# windowed -> fullscreen -> borderless -> windowed.
CYCLE = ["borderless", "fullscreen", "windowed", "fullscreen", "borderless", "windowed"]
SETTLE = 20  # vsyncs after a switch (the window manager's reconfigure, the swapchain's recreation)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--cycles", type=int, default=2)
    parser.add_argument("--out")
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "window_mode"
    out.mkdir(parents=True, exist_ok=True)
    settings = out / "settings.cfg"
    settings.unlink(missing_ok=True)
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"

    desktop = None
    if os.environ.get("ROOD_TEST_DESKTOP") == "1":
        desktop = os.environ.get("ROOD_DESKTOP_DISPLAY") or os.environ.get("DISPLAY")
        if not desktop:
            print("note: ROOD_TEST_DESKTOP=1 but no DISPLAY: using Xvfb")
    if desktop:
        print(f"real window on {desktop} (X11)" + (": the Vulkan swapchain path is covered" if vulkan else ""))
    else:
        try:
            mode = headless_mode()[0]
        except RpcError as e:
            print(f"SKIP: no real window (set ROOD_TEST_DESKTOP=1 with a DISPLAY) and no headless mode ({e})")
            return 77
        print(f"note: headless ({mode}): a resizing swapchain is not covered; "
              "run with ROOD_TEST_DESKTOP=1 on a desktop for it")

    lights = out / "lights"
    lights.mkdir(exist_ok=True)
    env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(settings), "ROOD_LIGHTS": str(lights),
           # Path tracing as in play where it is fast (a GPU's Vulkan); software GL on Xvfb is too slow.
           "ROOD_GI": "1" if desktop and vulkan else "0"}
    if desktop:
        os.environ.pop("WAYLAND_DISPLAY", None)
        env.update(DISPLAY=desktop, SDL_VIDEO_DRIVER="x11")
        launch = dict(window=True)
    else:
        launch = {}
    card = out / "card1.mcd"
    have_card = pathlib.Path(args.card).exists()
    if have_card:
        shutil.copyfile(args.card, card)

    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    log = out / "game.log"
    if have_card:
        game = launch_at("save_room", card=card, log=log, extra_env=env, **launch)
    else:
        game = Game.launch(log=log, extra_env=env, **launch)
        game.wait_title()
    try:
        game.run(30)
        start = game.status()
        real_window = start["window_presents"] > 0
        check(not desktop or real_window,
              f"a real window presents before the switches ({start['window_presents']} images)")
        print(f"note: video {game.call('system')['window']['video']}, "
              f"{'window presents counted (GL swaps, Vulkan swapchain)' if real_window else 'offscreen presents only'}")
        for cycle in range(args.cycles):
            for mode in CYCLE:
                before = game.status()
                try:
                    game.call("system", window_mode=mode)
                    game.run(SETTLE)
                    after = game.status()
                    w = game.call("system")["window"]
                except RpcError as e:
                    check(False, f"cycle {cycle + 1}, {mode}: the game answers ({e}; see {log})")
                    raise SystemExit(1)
                label = f"cycle {cycle + 1}, -> {mode}"
                check(not after["device_lost"], f"{label}: the device is not lost")
                if w["failed"]:
                    print(f"note: {label}: {w['video']} cannot switch to {w['failed_mode']} here ({w})")
                elif desktop or w["video"] == "offscreen":
                    check(w["mode"] == mode and w["fullscreen"] == (mode != "windowed"),
                          f"{label}: the window is {mode} (fullscreen {w['fullscreen']}, size {w['size']})")
                else:  # Xvfb: no window manager to make the window fullscreen
                    check(w["mode"] == mode, f"{label}: the mode is {mode}")
                    if w["fullscreen"] != (mode != "windowed"):
                        print(f"note: {label}: Xvfb has no window manager: fullscreen {w['fullscreen']}")
                presented = after["presents"] - before["presents"]
                check(presented >= SETTLE // 2, f"{label}: presents go on ({presented} in {SETTLE} vsyncs)")
                if real_window:
                    shown = after["window_presents"] - before["window_presents"]
                    check(shown >= SETTLE // 4,
                          f"{label}: images reach the window ({shown} in {SETTLE} vsyncs)")
        # Windowed again and settled: the presented image is the window's size.
        game.call("system", window_mode="windowed")
        game.run(60)
        w = game.call("system")["window"]
        with Image.open(game.present_screenshot(out / "windowed.png")) as image:
            size = list(image.size)
        check(w["mode"] == "windowed" and size == w["size"],
              f"windowed at the end: the presented image has the window's size ({size} vs {w['size']})")
        log_text = pathlib.Path(log).read_text(errors="replace")
        check("device lost" not in log_text and "fence wait failed" not in log_text,
              "no device loss or failed frame fence in the log")
    finally:
        game.close()
        (out / "windowed.png").unlink(missing_ok=True)  # game imagery: not kept

    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
