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

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "pc/tools"), str(ROOT / "pc/tests")]
from checkpoints import launch_at  # noqa: E402

failures = []


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
        finally:
            game.close()
    print("PASS: hw_calls_smoke" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
