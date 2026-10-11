"""Verify a rendered opening card reached through the original entry script."""

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
capture = root / out / "opening-checkpoint.ppm"
capture.unlink(missing_ok=True)
result = subprocess.run(
    [
        (("pc/build/boot64" if os.environ.get("ROOD_ARCH") == "64" else "pc/build/boot") + "/rood_battle_probe"),
        "--skip-movies",
        "--fast",
        "--opening-checkpoint",
    ],
    cwd=root,
    capture_output=True,
    timeout=60,
)
(root / out / "opening-test.log").write_bytes(result.stdout + result.stderr)
if result.returncode or b"OPENING SCRIPT CHECKPOINT" not in result.stderr:
    raise SystemExit("Opening script failed; see " + out + "/opening-test.log")
assert b"ROOM RENDER CHECKPOINT" not in result.stderr
magic, dimensions, maximum, pixels = capture.read_bytes().split(b"\n", 3)
assert magic == b"P6" and maximum == b"255"
width, height = map(int, dimensions.split())
assert (width, height) == (320, 224) and len(pixels) == width * height * 3
colors = {pixels[i : i + 3] for i in range(0, len(pixels), 3)}
nonblack = sum(pixels[i : i + 3] != b"\0\0\0" for i in range(0, len(pixels), 3))
caption = sum(
    min(pixels[(y * width + x) * 3 : (y * width + x) * 3 + 3]) >= 180
    for y in range(180, 218)
    for x in range(15, 160)
)
assert len(colors) > 256 and nonblack > 30000, "Opening background missing"
assert caption > 120, "Opening location/time caption missing"
print(
    f"PASS: original opening script, {len(colors)} colors, {nonblack} nonblack pixels, {caption} bright caption pixels (not gameplay)"
)
