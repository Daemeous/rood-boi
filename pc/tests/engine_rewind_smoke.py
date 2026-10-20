#!/usr/bin/env python3
"""Split-host no-reload replay: one mark, two rewind/600-frame legs.

Uses the ordinary snapshot_diff, on GL/Vulkan and null. No address masks,
allocator writes, or reloads. ROOD_ENGINE_SMOKE_NO_BUILD=1 uses dev64 as-is;
ROOD_REWIND_BINARY can select a private pre-fix artifact for the negative control.
"""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import rood_rpc
import snapshot_diff
from checkpoints import launch_at, _base_env
from engine_reload_smoke import build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD", ROOT / "pc/build/test-card1.mcd"))
    args = parser.parse_args()
    if os.environ.get("ROOD_ARCH") != "64" or not sys.platform.startswith("linux"):
        print("SKIP: split-host replay requires Linux and ROOD_ARCH=64")
        return 77
    card = pathlib.Path(args.card)
    if not card.exists():
        print("SKIP: no pinned test card", card)
        return 77
    build()
    rood_rpc.BINARY = pathlib.Path(os.environ.get("ROOD_REWIND_BINARY", ROOT / "pc/build/dev64/rood_host"))
    os.environ["ROOD_SYMBOLS"] = str(rood_rpc.BINARY)
    os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
    out = pathlib.Path(os.environ.get("ROOD_OUT") or rood_rpc.OUT)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="engine-rewind-", dir=out) as directory:
        work = pathlib.Path(directory)
        private_card = work / "card.mcd"
        shutil.copyfile(card, private_card)
        settings = work / "settings.txt"
        settings.write_text("ui_menus 0\nwidescreen 0\ninterpolate 0\nqol_skip_charging 0\ncamera_stick 0\n")
        env = dict(_base_env(work), ROOD_ORACLE="1", ROOD_DETERMINISTIC="1",
                   ROOD_SETTINGS=str(settings), ROOD_GI="0")
        game = launch_at("save_room", card=str(private_card), fast=True,
                         socket_path=f"/tmp/rood-rewind-{os.getpid()}.sock",
                         log=out / "engine-rewind.log", extra_env=env)
        try:
            game.call("room_goto", zone=48, room=10, enemies=True, scripts=False)
            for _ in range(80):
                game.run(20)
                if game.call("room_goto", action="status")["phase"] in (0, 4):
                    break
            assert (game.state()["zone"], game.state()["room"]) == (48, 10), "enemy-cast room not loaded"
            pid = game.processes[0].pid
            game.mark()
            states = []
            for leg in ("a", "b"):
                game.rewind()
                before = game.status()
                game.run(600)
                after = game.status()
                assert after["vsync"] == before["vsync"] + 600
                if os.environ.get("ROOD_RENDERER") != "null":
                    assert after["presents"] >= before["presents"] + 600
                path = work / f"{leg}.snap"
                game.call("snapshot", action="save", path=str(path))
                states.append(snapshot_diff.read(path))
            differences = snapshot_diff.diff(*states, snapshot_diff.Symbols(rood_rpc.BINARY))
            assert game.processes[0].pid == pid and game.processes[0].poll() is None
            assert not differences and states[0]["vsync"] == states[1]["vsync"], differences
            print("PASS: no-reload rewind/600-frame replay is byte exact")
        finally:
            game.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
