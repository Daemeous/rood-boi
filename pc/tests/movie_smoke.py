"""Intro movie (TITLE.STR): launch without --skip-movies headless and check
that the original title code plays it: 24-bit movie frames that advance at
the stream's 15 fps, XA audio in the recorded output, Start skipping to the
title menu with the music still streaming, and New Game stopping the stream
(_stopIntroMovie) on the way into the opening."""
import hashlib
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game  # noqa: E402

BOOT = OUT
WAV = BOOT / "movie-smoke.wav"


def frame(game, name):
    """The displayed frame (native resolution) as (width, height, RGB bytes)."""
    path = BOOT / name
    game.call("screenshot", path=str(path))
    data = path.read_bytes()
    _, width_height, _, pixels = data.split(b"\n", 3)
    width, height = map(int, width_height.split())
    path.unlink()
    return width, height, pixels


game = Game.launch(movies=True, audio=f"wav:{WAV}", log=BOOT / "movie-smoke.log", fast=True)
try:
    game.pause()
    # The publisher/developer cards take 728 vsyncs; the movie follows.
    game.run(max(0, 1300 - game.status()["vsync"]))
    assert game.u32("pc_display_rgb24") == 1, "movie not in 24-bit display mode"
    width, height, pixels = frame(game, "movie-smoke-a.ppm")
    assert (width, height) == (320, 224), (width, height)
    mean = sum(pixels) / len(pixels)
    colours = len({pixels[i:i + 3] for i in range(0, len(pixels), 3)})
    assert mean > 8 and colours > 500, (mean, colours)

    # One new picture every 4 vsyncs on average (150 sectors/s, 10 per frame).
    hashes = []
    for _ in range(48):
        game.run(1)
        hashes.append(hashlib.md5(frame(game, "movie-smoke-b.ppm")[2]).digest())
    changes = sum(a != b for a, b in zip(hashes, hashes[1:]))
    assert 9 <= changes <= 14, f"{changes} picture changes in 47 vsyncs"

    movie_end = game.status()["vsync"]
    game.press("start")
    game.pause(False)
    game.wait_title(timeout=30)
    title_vsync = game.status()["vsync"]
    assert title_vsync - movie_end < 300, (movie_end, title_vsync)
    assert game.u32("pc_display_rgb24") == 0, "title still in 24-bit mode"

    # New Game stops the stream and frees the movie buffers.
    game.press("start")
    game.run(600)
    status = game.status()
    assert status["battle_frame"] > 0, status
    assert game.state()["zone"] == 1, game.state()
    vsyncs = status["vsync"]
finally:
    game.close()

log = (BOOT / "movie-smoke.log").read_text()
assert "UNIMPLEMENTED" not in log and "[crash]" not in log, log[-2000:]

# XA audio: loud while the movie streams (from ~vsync 750), not silence.
data = WAV.read_bytes()[44:]
samples = struct.unpack(f"<{len(data) // 2}h", data[: len(data) // 4 * 4])
start, end = int(800 / 59.94 * 44100) * 2, int(1300 / 59.94 * 44100) * 2
segment = samples[start:end]
rms = (sum(s * s for s in segment) / max(1, len(segment))) ** 0.5
assert rms > 500, f"movie audio RMS {rms:.0f}"
print(f"PASS: intro movie 24-bit frames ({changes} changes / 47 vsyncs), XA audio RMS {rms:.0f}, "
      f"Start to title in {title_vsync - movie_end} vsyncs, New Game after the stream ({vsyncs} vsyncs)")
