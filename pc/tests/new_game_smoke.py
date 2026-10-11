"""Select New Game through real X11 input, then observe original battle frames."""

import argparse
import os
import pathlib
import re
import subprocess
import time

# Keep tests off the player's saves (a save makes the title default to
# Continue): an empty slot 1 and no slot 2, relative to the repository root.
os.environ.setdefault("ROOD_CARD1", "pc/build/boot/test-card1.mcd")
os.environ.setdefault("ROOD_CARD2", "pc/build/boot/test-card2-absent.mcd")
# Software GL under Xvfb is slow at 4x; these tests cover game logic at 1x.
os.environ.setdefault("ROOD_SCALE", "1")

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--frames", type=int, default=300)
parser.add_argument(
    "--advance-dialogue",
    action="store_true",
    help="press the ordinary confirm button after each 300-frame progress marker",
)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
subprocess.run(
    ["gcc", "pc/tests/send_key.c", "-lX11", "-o", "pc/build/boot/send_key"],
    cwd=root,
    check=True,
)
r, w = os.pipe()
x = subprocess.Popen(
    ["Xvfb", "-displayfd", str(w), "-screen", "0", "800x600x24"],
    pass_fds=(w,),
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
os.close(w)
number = os.read(r, 64).decode().strip()
os.close(r)
# SDL uses X11 on the private display (not the desktop's Wayland), so the
# X11 events send_key delivers reach the game.
env = dict(os.environ, DISPLAY=":" + number, SDL_VIDEO_DRIVER="x11", ROOD_CONTROLLER="none",
           ROOD_GI=os.environ.get("ROOD_GI", "0"))
log = root / "pc/build/boot/new-game-test.log"
capture = root / "pc/build/boot/battle-checkpoint.ppm"
result = root / "pc/build/boot/new-game-checkpoint.ppm"
capture.unlink(missing_ok=True)
result.unlink(missing_ok=True)
try:
    with log.open("w") as output:
        p = subprocess.Popen(
            [
                (("pc/build/boot64" if os.environ.get("ROOD_ARCH") == "64" else "pc/build/boot") + "/rood_battle_probe"),
                "--window",
                "--skip-movies",
                "--fast",
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
            subprocess.run(
                ["pc/build/boot/send_key", "Return"], cwd=root, env=env, check=True
            )
            if args.advance_dialogue:
                deadline = time.monotonic() + 300
                last_frame = 0
                while p.poll() is None and time.monotonic() < deadline:
                    markers = re.findall(r"completed (\d+) frames", log.read_text())
                    frame = int(markers[-1]) if markers else 0
                    if frame > last_frame:
                        last_frame = frame
                        sent = subprocess.run(
                            ["pc/build/boot/send_key", "z"],
                            cwd=root,
                            env=env,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL,
                        )
                        if sent.returncode and p.poll() is None:
                            raise RuntimeError("Dialogue confirmation input failed")
                    time.sleep(0.05)
                p.wait(timeout=5)
            else:
                p.wait(timeout=300)
            text = log.read_text()
            assert p.returncode == 0, text[-4000:]
            assert "[title] starting mode 0 " in text, "Did not enter New Game"
            assert f"BATTLE FRAME CHECKPOINT: {args.frames} " in text
            assert "ROOM RENDER CHECKPOINT" not in text
            magic, dimensions, maximum, pixels = capture.read_bytes().split(b"\n", 3)
            assert (magic, dimensions, maximum) == (b"P6", b"320 224", b"255")
            assert len(pixels) == 320 * 224 * 3
            assert any(pixels), "New Game rendered an entirely black frame"
            capture.replace(result)
            print(
                f"PASS: keyboard selected New Game, {args.frames} original battle frames, nonblack scene capture (not gameplay)"
            )
        finally:
            if p.poll() is None:
                p.terminate()
                p.wait(timeout=5)
finally:
    x.terminate()
    x.wait(timeout=5)
