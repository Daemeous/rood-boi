#!/usr/bin/env python3
"""The game side's requests into the renderer (pc/src/hw_calls.c) each have a definition and answer from the game thread: the RPC hw_calls
makes every one once. A renamed or dropped definition (the build also refuses one: build_boot.py allows no trap but SystemError) would abort
the game here. The native VRAM texture's handle is the one the editor's view, the path tracer and the probe GI take; it is a real texture
under OpenGL and 0 under Vulkan.

    python3 pc/tests/hw_calls_smoke.py [--card pc/build/test-card1.mcd]

Runs the field scene in render_thread 0, 1 and 2 (the request runs on the renderer's thread, after every record this thread made)."""
import argparse
import os
import pathlib
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "pc/tools"), str(ROOT / "pc/tests")]
from checkpoints import launch_at  # noqa: E402

failures = []
results = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(tempfile.mkdtemp(prefix="hw-calls-"))
    gl = os.environ.get("ROOD_RENDERER") == "gl"
    for thread in (0, 1, 2) if not gl else (0,):
        os.environ["ROOD_RENDER_THREAD"] = str(thread)
        game = launch_at("save_room", card=args.card, fast=True)
        try:
            game.run(60)
            answer = game.call("hw_calls")
            print(answer)
            check(answer["display"][2] > 0 and answer["display"][3] > 0, f"render_thread {thread}: the display rectangle is answered {answer['display']}")
            check(answer["native_per_output"] > 0, f"render_thread {thread}: native pixels per output pixel {answer['native_per_output']}")
            check(answer["casters"] >= 0, f"render_thread {thread}: the casters ({answer['casters']}, serial {answer['caster_serial']})")
            check(answer["all_lists"] > 0, f"render_thread {thread}: the room's lists are answered ({answer['all_lists']})")
            check((answer["native_texture"] != 0) == gl, f"render_thread {thread}: the native texture handle ({answer['native_texture']}; a texture under OpenGL only)")
            game.run(5)  # (the game goes on: the requests aborted nothing)
            # The draw settings travel in the stream (hw_cmd.c C_SET_SETTINGS): a change on the game thread reaches the renderer's copy.
            def picture(name):
                game.run(6)
                path = out / f"{name}.png"
                game.present_screenshot(str(path))
                return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
            game.call("settings", apply=["perspective_textures 1", "smooth_textures 0"])
            base = picture("base")
            game.call("settings", apply=["perspective_textures 0"])
            affine = picture("affine")
            game.call("settings", apply=["perspective_textures 1", "smooth_textures 1"])
            smooth = picture("smooth")
            game.call("settings", apply=["perspective_textures 1", "smooth_textures 0"])
            again = picture("again")
            counts = [int((np.abs(base - other).max(2) > 8).sum()) for other in (affine, smooth, again)]
            print("diff pixels (affine, smooth, back again):", counts)
            check(counts[0] > 1000, f"render_thread {thread}: turning perspective-correct texturing off changes the picture ({counts[0]} pixels)")
            check(counts[1] > 1000, f"render_thread {thread}: turning texture smoothing on changes the picture ({counts[1]} pixels)")
            results.append(counts)
            check(results[-1] == results[0], f"render_thread {thread}: the same pictures as render_thread 0 ({results[-1]} against {results[0]})")
        finally:
            game.close()
    print("PASS: hw_calls_smoke" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
