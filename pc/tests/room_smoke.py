"""Validate the explicit loaded-room preview, not gameplay or script progress."""

import pathlib
import subprocess
import os

# Keep tests off the player's saves (a save makes the title default to
# Continue): an empty slot 1 and no slot 2, relative to the repository root.
# ROOD_OUT (pc/tests/run.py) gives parallel runs their own files.
out = os.environ.get("ROOD_OUT") or "pc/build/boot"
os.environ.setdefault("ROOD_CARD1", f"{out}/test-card1.mcd")
os.environ.setdefault("ROOD_CARD2", f"{out}/test-card2-absent.mcd")
# Software GL under Xvfb is slow at 4x; these tests cover game logic at 1x.
os.environ.setdefault("ROOD_SCALE", "1")

root = pathlib.Path(__file__).resolve().parents[2]
capture = root / out / "room-checkpoint.ppm"
capture.unlink(missing_ok=True)
result = subprocess.run(
    [(("pc/build/boot64" if os.environ.get("ROOD_ARCH") == "64" else "pc/build/boot") + "/rood_battle_probe"), "--skip-movies", "--fast", "--room-checkpoint"],
    cwd=root,
    capture_output=True,
    timeout=60,
)
(root / out / "room-test.log").write_bytes(result.stdout + result.stderr)
if result.returncode or b"ROOM RENDER CHECKPOINT" not in result.stderr:
    raise SystemExit("Room diagnostic failed; see " + out + "/room-test.log")
magic, dimensions, maximum, pixels = capture.read_bytes().split(b"\n", 3)
assert magic == b"P6" and maximum == b"255"
width, height = map(int, dimensions.split())
assert (width, height) == (320, 224) and len(pixels) == width * height * 3
colors = {pixels[i : i + 3] for i in range(0, len(pixels), 3)}
nonblack = sum(pixels[i : i + 3] != b"\0\0\0" for i in range(0, len(pixels), 3))
assert len(colors) > 8 and nonblack > 200, "Empty or effectively blank room preview"
print(
    f"PASS: loaded-room preview {width}x{height}, {len(colors)} colors, {nonblack} nonblack pixels (not gameplay)"
)
