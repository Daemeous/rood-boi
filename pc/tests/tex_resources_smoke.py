#!/usr/bin/env python3
"""The texture resources (T5-7b, pc/src/gpu_hw.c "the texture resources", vk_hw.c tex_pages / tex_palettes / tex_table).

    python3 pc/tests/tex_resources_smoke.py [--card pc/save/card1.mcd]

ROOD_TEX_CHECK=1 fills the page and palette resources for every textured draw and compares what the draw reaches with the sampled copy of VRAM (what the shaders
read); ROOD_TEX_RESOURCES=1 makes the pure-upload draws of the packet path (hw_frag.glsl) read the resources. In the save room and with a menu open the game must print
"[tex] check: N boxes compared ..., 0 texels differ, ... M draws read the resources" at exit, with boxes compared, fills and draws > 0.
Vulkan only (the resources are the Vulkan renderer's)."""
import argparse
import pathlib
import re
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from checkpoints import launch_at  # noqa: E402
from port_arena_smoke import default_card  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(tempfile.mkdtemp(prefix="tex-resources-"))
    shutil.copyfile(args.card, out / "card1.mcd")
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=out / "card1.mcd", fast=True, log=out / "game.log",
                     extra_env={"ROOD_TEX_CHECK": "1", "ROOD_TEX_RESOURCES": "1", "ROOD_GI": "0"})
    try:
        game.run(240)
        game.press("triangle", vsyncs=4, settle=60)
        game.run(120)
    finally:
        game.close()
    log = (out / "game.log").read_text(errors="replace")
    summary = re.findall(r"\[tex\] check: (\d+) boxes compared \((\d+) texels\), (\d+) texels differ, (\d+) boxes skipped, (\d+) fills, (\d+) draws read the resources, (\d+) native lists", log)
    check(bool(summary), "the check ran and reported at exit")
    if summary:
        boxes, texels, bad, skipped, fills, draws, lists = map(int, summary[-1])
        check(boxes > 50 and fills > 10, f"{boxes} boxes ({texels} texels) compared, {fills} fills, {skipped} skipped")
        check(bad == 0, f"0 texels differ ({bad})")
        check(draws > 50, f"{draws} draws read the resources")
        check(lists > 20, f"{lists} native lists read the resources")
    for line in [l for l in log.splitlines() if l.startswith("[tex] page")][:8]:
        print(line)
    print(f"{len(failures)} failure(s)" if failures else "tex_resources_smoke: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
