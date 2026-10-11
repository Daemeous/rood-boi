"""Play New Game like new_game_smoke.py, then from battle frame --after send a
key sequence (--keys "z x", 0.5 s apart; +Up holds, -Up releases) or hold a key (--hold Left).

Keys are X11 key names. --input x11 sends them as X11 events (send_key.c) to
the game's window on a private Xvfb; --input rpc runs headless on EGL (a GPU,
rood_rpc.headless_mode) and presses the same pad buttons through the
developer RPC "input" method. The default follows the headless mode."""

import argparse
import os
import pathlib
import re
import subprocess
import sys
import time

root = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "pc/tools"))
from rood_rpc import OUT, Game, headless_env, headless_mode, start_xvfb  # noqa: E402

# The window's keyboard mapping (boot_window.c keybit) as RPC pad buttons.
RPC_BUTTONS = {"Up": "up", "Down": "down", "Left": "left", "Right": "right", "Return": "start",
               "z": "circle", "x": "cross", "Tab": "select", "a": "square", "s": "triangle",
               "q": "l1", "w": "r1", "1": "l2", "2": "r2"}

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--frames", type=int, default=300)
parser.add_argument("--after", type=int, default=7500)
parser.add_argument("--keys", default="")
parser.add_argument("--hold", default="")
parser.add_argument("--idle", action="store_true", help="let the attract demo start instead of selecting New Game")
parser.add_argument("--realtime", action="store_true", help="pace at 59.94 Hz instead of --fast")
parser.add_argument("--deadline", type=float, help="seconds for the key sequence (default 300, realtime 900)")
parser.add_argument("--input", choices=["x11", "rpc"],
                    help="x11: send_key on Xvfb; rpc: headless EGL with RPC input (default: by headless mode)")
parser.add_argument(
    "--advance-dialogue",
    action="store_true",
    help="press the ordinary confirm button after each 300-frame progress marker",
)
args = parser.parse_args()
use_rpc = (args.input or ("rpc" if headless_mode()[0] == "egl" else "x11")) == "rpc"
env = dict(os.environ, ROOD_CONTROLLER="none", ROOD_GI=os.environ.get("ROOD_GI", "0"))
x = None
rpc = None
if use_rpc:
    headless_env("egl", env)
    env["ROOD_RPC"] = f"/tmp/rood-input-{os.getpid()}.sock"  # sun_path is 108 bytes
    pathlib.Path(env["ROOD_RPC"]).unlink(missing_ok=True)
else:
    send_key = str(OUT / "send_key")
    subprocess.run(["gcc", "pc/tests/send_key.c", "-lX11", "-o", send_key], cwd=root, check=True)
    x, display = start_xvfb("800x600x24")
    # SDL uses X11 on the private display (not the desktop's Wayland), so the
    # X11 events send_key delivers reach the game.
    env.update(DISPLAY=display, SDL_VIDEO_DRIVER="x11")
    env.pop("WAYLAND_DISPLAY", None)
# Keep tests off the player's saves: an empty slot 1 unless ROOD_CARD1 is given
# (a save makes the title default to Continue instead of New Game).
if "ROOD_CARD1" not in os.environ:
    env["ROOD_CARD1"] = str(OUT / "test-card1.mcd")
    pathlib.Path(env["ROOD_CARD1"]).unlink(missing_ok=True)
env.setdefault("ROOD_CARD2", str(OUT / "test-card2-absent.mcd"))
# Software GL under Xvfb is slow at 4x; these tests cover game logic at 1x.
env.setdefault("ROOD_SCALE", "1")
log = OUT / "new-game-test.log"
capture = OUT / "battle-checkpoint.ppm"
result = OUT / "new-game-checkpoint.ppm"
capture.unlink(missing_ok=True)
result.unlink(missing_ok=True)


def key(name, half=None):
    """Press X11 key `name` (half: "down" holds it, "up" releases it)."""
    global rpc
    if not use_rpc:
        return subprocess.run([send_key, name, *([half] if half else [])], cwd=root, env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
    if rpc is None:
        rpc = Game(env["ROOD_RPC"])
    button = RPC_BUTTONS[name]
    try:
        if half == "up":
            rpc.call("release")
        else:
            # A tap lasts a few vsyncs, as a key press spans several polls.
            rpc.call("input", buttons=[button], vsyncs=1 << 30 if half == "down" else 6)
    except (OSError, RuntimeError):
        return 1  # the game exited (the checkpoint)
    return 0


try:
    with log.open("w") as output:
        p = subprocess.Popen(
            [
                (("pc/build/boot64" if os.environ.get("ROOD_ARCH") == "64" else "pc/build/boot") + "/rood_battle_probe"),
                "--window",
                "--skip-movies",
                *([] if args.realtime else ["--fast"]),
                "--battle-frame-checkpoint",
                str(args.frames),
            ],
            cwd=root,
            env=env,
            stdout=output,
            stderr=output,
        )
        try:
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                if "TITLE MENU READY" in log.read_text():
                    break
                if p.poll() is not None:
                    raise RuntimeError(log.read_text())
                time.sleep(0.05)
            else:
                raise RuntimeError("Title input timeout")
            if not args.idle and key("Return"):
                raise RuntimeError("Return failed")
            if args.advance_dialogue or args.idle or args.keys or args.hold:
                deadline = time.monotonic() + (args.deadline or (900 if args.realtime else 300))
                last_frame = 0
                sequence = args.keys.split()
                titles = 1
                held = False
                while p.poll() is None and time.monotonic() < deadline:
                    markers = re.findall(r"completed (\d+) frames", log.read_text())
                    frame = int(markers[-1]) if markers else 0
                    if frame >= args.after and (args.keys or args.hold):
                        if args.hold and use_rpc:
                            if not held:  # one RPC hold lasts until the game exits
                                held = not key(args.hold, "down")
                            time.sleep(0.05)
                            continue
                        if args.hold:
                            name = args.hold
                        elif sequence:
                            name = sequence.pop(0)
                            if name == "@title":
                                # Wait for the title menu to come back, then pick New Game.
                                seen = log.read_text().count("TITLE MENU READY")
                                while p.poll() is None and log.read_text().count("TITLE MENU READY") <= titles:
                                    time.sleep(0.05)
                                titles = log.read_text().count("TITLE MENU READY")
                                name = "Return"
                            elif name.startswith("@"):
                                # @N: wait until battle frame N before the next key.
                                target = int(name[1:])
                                while p.poll() is None:
                                    marks = re.findall(r"completed (\d+) frames", log.read_text())
                                    if marks and int(marks[-1]) >= target:
                                        break
                                    time.sleep(0.05)
                                continue
                        else:
                            time.sleep(0.05)
                            continue
                        # +Key presses and holds, -Key releases.
                        key(name.lstrip("+-"), {"+": "down", "-": "up"}.get(name[0]))
                        if not args.hold:
                            time.sleep(0.5)
                        continue
                    if args.advance_dialogue and frame > last_frame:
                        last_frame = frame
                        if key("z") and p.poll() is None:
                            raise RuntimeError("Dialogue confirmation input failed")
                    time.sleep(0.05)
                p.wait(timeout=5)
            else:
                p.wait(timeout=300)
            text = log.read_text()
            assert p.returncode == 0, text[-4000:]
            assert args.idle or "[title] starting mode 0 " in text, "Did not enter New Game"
            assert f"BATTLE FRAME CHECKPOINT: {args.frames} " in text
            assert "ROOM RENDER CHECKPOINT" not in text
            magic, dimensions, maximum, pixels = capture.read_bytes().split(b"\n", 3)
            assert (magic, dimensions, maximum) == (b"P6", b"320 224", b"255")
            assert len(pixels) == 320 * 224 * 3
            assert any(pixels), "New Game rendered an entirely black frame"
            capture.replace(result)
            print(
                f"PASS: {'RPC pad' if use_rpc else 'X11 keyboard'} selected New Game, {args.frames} original battle frames, "
                "nonblack scene capture (not gameplay)"
            )
        finally:
            if p.poll() is None:
                p.terminate()
                p.wait(timeout=5)
finally:
    if rpc:
        rpc.sock.close()
    if use_rpc:
        pathlib.Path(env["ROOD_RPC"]).unlink(missing_ok=True)
    if x:
        x.terminate()
        x.wait(timeout=5)
