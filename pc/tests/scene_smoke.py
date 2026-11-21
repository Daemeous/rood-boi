"""Check a later VKP scene reached by 4,000 original battle frames.

The captured frame is the picture of the PS1 display area (read back from the renderer's target at the checkpoint). Since the room and the characters are native lists (S5.2 / S5.3,
c5a2cfdcf: no longer built as packets) it holds only the 2D the game draws: here the scene's talk bubble on black. The 3D (the room, the
actor) is checked by the renderer's own suites; this one keeps what the 2D frame can show: the checkpoint is reached and the dialogue's
bubble and text are drawn."""

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
capture = root / out / "battle-checkpoint.ppm"
capture.unlink(missing_ok=True)
result = subprocess.run(
    [
        (("pc/build/boot64" if os.environ.get("ROOD_ARCH") == "64" else "pc/build/boot") + "/rood_battle_probe"),
        "--skip-movies",
        "--fast",
        "--battle-frame-checkpoint",
        "4000",
    ],
    cwd=root,
    capture_output=True,
    timeout=180,
)
(root / out / "scene-test.log").write_bytes(result.stdout + result.stderr)
if result.returncode or b"BATTLE FRAME CHECKPOINT: 4000" not in result.stderr:
    raise SystemExit("Scene checkpoint failed; see " + out + "/scene-test.log")
assert b"ROOM RENDER CHECKPOINT" not in result.stderr
magic, dimensions, maximum, pixels = capture.read_bytes().split(b"\n", 3)
assert magic == b"P6" and maximum == b"255"
width, height = map(int, dimensions.split())
assert (width, height) == (320, 224) and len(pixels) == width * height * 3
colors = {pixels[i : i + 3] for i in range(0, len(pixels), 3)}
nonblack = sum(pixels[i : i + 3] != b"\0\0\0" for i in range(0, len(pixels), 3))
bright = sum(
    min(pixels[(y * width + x) * 3 : (y * width + x) * 3 + 3]) >= 180
    for y in range(10, 100)
    for x in range(10, 185)
)
actor = sum(
    pixels[(y * width + x) * 3 : (y * width + x) * 3 + 3] != b"\0\0\0"
    for y in range(65, 215)
    for x in range(200, 310)
)
assert len(colors) > 10 and nonblack > 5000, "Later scene missing"
assert bright > 2000, "Dialogue missing"
assert actor == 0, "The CPU frame holds 3D again: restore the scene and actor checks (nonblack > 25000, actor > 7000)"
print(f"PASS: 4,000 original scene frames, {len(colors)} colors, {nonblack} nonblack pixels, the dialogue bubble present (2D only: the 3D is native)")
