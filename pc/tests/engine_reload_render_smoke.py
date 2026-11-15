#!/usr/bin/env python3
"""The engine reload with the renderer thread on (64 bits, Linux; build_boot.py --engine-library, pc/src/engine_reload.c, render_thread.c).

    python3 pc/tests/engine_reload_render_smoke.py [--card pc/build/test-card1.mcd]    (the thread's mode: ROOD_RENDER_THREAD, default 2)

The reload drains the renderer thread and ends it before the library is unloaded (the thread runs in it); the library loaded next starts its own at
its first use. Per scene (a fight, the original menu open) a game reloads in the middle and another never does, and 90 vsyncs later:
- the game state (RPC snapshot hash) is the same and the picture is the same;
- positive control: a library built with an edit the picture shows (perspective-correct texturing off: gpu_hw.c's
  ROOD_DRAW_PERSPECTIVE_DEFAULT=0) shows it after the reload, so the equal pictures above are not an equal blank.
Then 10 reloads in a row: every one goes through, no crash, the resident memory does not grow by more than LEAK_LIMIT MB each, and
every reload ended one thread and started the next (the game log).
"""
import argparse
import json
import os
import pathlib
import re
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import checkpoints  # noqa: E402
import rood_rpc  # noqa: E402
import engine_reload_smoke as base  # noqa: E402

FRAMES = 90
RELOADS = 10
LEAK_LIMIT = 64
DEV = base.DEV
check = base.check


def edited_library(work):
    """The library with gpu_hw.c compiled with ROOD_DRAW_PERSPECTIVE_DEFAULT=0 (the manifest's command and link, as the reload smoke's note build)."""
    manifest = json.loads((DEV / "engine_library.json").read_text())
    unit = next(u for u in manifest["units"] if u["object"].endswith("/gpu_hw.o"))
    obj = work / "gpu_hw_edit.o"
    command = list(unit["command"])
    command[command.index("-o") + 1] = str(obj)
    subprocess.run(command + ["-DROOD_DRAW_PERSPECTIVE_DEFAULT=0"], cwd=ROOT, check=True)
    link = [str(obj) if a == unit["object"] or a == str(ROOT / unit["object"]) else a for a in manifest["link"]]
    library = work / "librood_engine.edit.so"
    link[-1] = str(library)
    subprocess.run(link, cwd=ROOT, check=True)
    return library


def launch(work, name, card, mode):
    library = work / f"{name}.so"
    base.shutil.copyfile(DEV / "librood_engine.so", library)
    env = dict(checkpoints._base_env(work), ROOD_ENGINE_LIBRARY=str(library), ROOD_RENDER_THREAD=str(mode),
               ROOD_SETTINGS=str(work / f"{name}.ini"), ROOD_CARD1=str(card), ROOD_CARD2=str(work / "card2.mcd"),
               ROOD_PLUGINS=str(work / "plugins"), ROOD_PLUGIN_DATA=str(work / "plugin_data"))
    (work / f"{name}.ini").write_text("qol_skip_charging 0\nui_menus 0\ninterpolate 1\npresent_rate 4\n")
    game = checkpoints.launch_at("save_room", card=card, fast=True, socket_path=f"/tmp/rood-reload-{os.getpid()}-{name}.sock",
                                 log=work / f"{name}.log", extra_env=env)
    return game, library


def scene(game, kind):
    if kind == "fight":
        game.call("room_goto", zone=48, room=10, enemies=True, scripts=False)
        for _ in range(80):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(40)
    else:
        game.run(60)
        game.press("triangle", vsyncs=3, settle=30)
        game.press("down", vsyncs=3, settle=10)


def picture(game, path):
    game.run(1)
    game.present_screenshot(str(path))
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)


def play(work, kind, card, mode, reload=False, edit=None):
    name = f"{kind}-{'edit' if edit else 'reload' if reload else 'plain'}"
    game, library = launch(work, name, card, mode)
    try:
        scene(game, kind)
        if edit:
            base.swap(edit, library)
        if reload or edit:
            reply = game.call("reload")
            check(reply.get("reloaded"), f"{name}: the reload went through ({reply.get('ms')} ms)")
        game.run(FRAMES)
        state, vsync = base.state(game)
        image = picture(game, work / f"{name}.png")
        return state, vsync, image
    finally:
        game.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    args = parser.parse_args()
    if os.environ.get("ROOD_ARCH") != "64":
        print("SKIP: the engine library is 64-bit only (run with ROOD_ARCH=64)")
        return 0
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    mode = int(os.environ.get("ROOD_RENDER_THREAD", "2"))
    base.build()
    work = pathlib.Path(os.environ.get("ROOD_OUT") or rood_rpc.OUT) / "engine_reload_render"
    base.shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    rood_rpc.BINARY = DEV / "rood_host"
    os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
    os.environ["ROOD_SYMBOLS"] = str(DEV / "rood_host")
    card = work / "card1.mcd"
    base.shutil.copyfile(args.card, card)
    edit = edited_library(work)
    results = {}
    for kind in ("fight", "menu"):
        plain = play(work, kind, card, mode)
        reloaded = play(work, kind, card, mode, reload=True)
        results[kind] = (plain, reloaded)
        check(plain[1] == reloaded[1] and plain[0] == reloaded[0], f"{kind}: the state {FRAMES} vsyncs after a reload equals the game that never reloaded (vsync {plain[1]})")
        diff = int((np.abs(plain[2] - reloaded[2]).max(2) > 8).sum())
        check(diff == 0, f"{kind}: the picture after the reload equals the one without it ({diff} pixels differ)")
    edited = play(work, "fight", card, mode, edit=edit)
    plain_image = results["fight"][0][2]
    diff = int((np.abs(plain_image - edited[2]).max(2) > 8).sum())
    check(diff > 1000, f"positive control: an engine edit shows after the reload ({diff} pixels differ)")
    # Ten reloads in a row.
    game, library = launch(work, "loop", card, mode)
    try:
        scene(game, "fight")
        pid = game.processes[0].pid
        first = base.rss_mb(pid)
        for i in range(RELOADS):
            reply = game.call("reload")
            check(reply.get("reloaded"), f"reload {i + 1} of {RELOADS} (generation {reply.get('generation')}, {reply.get('ms')} ms)")
            game.run(30)
        growth = (base.rss_mb(pid) - first) / RELOADS
        check(growth < LEAK_LIMIT, f"resident memory {first:.0f} -> {base.rss_mb(pid):.0f} MB: {growth:.1f} MB per reload")
        text = (work / "loop.log").read_text(errors="replace")
        check("[crash]" not in text, "no crash in the log")
        # What the old library held is freed at its unload (engine_alloc.c, engine_host.c rood_alloc_release): the heap in use stops growing (it was
        # 3 MB a reload: lists, meshes, the Lua state of every generation stayed).
        use = [float(m) for m in re.findall(r"heap after the reload: ([0-9.]+) MB in use", text)]
        if check(len(use) >= RELOADS, f"the host reports the heap after each reload ({len(use)})"):
            grow = (use[-1] - use[3]) / (len(use) - 4)
            check(grow < 0.4, f"the heap in use after a reload: {use[3]:.1f} -> {use[-1]:.1f} MB: {grow:.2f} MB per reload")
            freed = re.findall(r"the old library's heap: (\d+) blocks \(([0-9.]+) MB\) freed, (\d+) kept", text)
            check(len(freed) >= RELOADS, f"every unload freed what the library held ({len(freed)}; {freed[-1] if freed else '-'})")
        if mode >= 1:
            starts, stops = text.count("renderer thread started"), text.count("renderer thread stopped")
            check(stops >= RELOADS and starts >= stops, f"every reload ended a renderer thread and the next started one ({starts} started, {stops} stopped)")
    finally:
        game.close()
    if base.failures:
        print(f"{len(base.failures)} check(s) failed")
        return 1
    print("PASS: engine_reload_render_smoke")
    return 0


if __name__ == "__main__":
    sys.exit(main())
