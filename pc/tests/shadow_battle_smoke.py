#!/usr/bin/env python3
"""Actual battle-action shadow calls while enemies cast; no corpus coverage claim."""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, BINARY
from snapshot_diff import Symbols
from checkpoints import launch_at


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD", ROOT / "pc/build/test-card1.mcd"))
    card = pathlib.Path(parser.parse_args().card)
    if not card.exists():
        print("SKIP: no pinned test card", card)
        return 77
    with tempfile.TemporaryDirectory(prefix="shadow-battle-", dir=OUT) as directory:
        work = pathlib.Path(directory)
        private_card = work / "card.mcd"
        shutil.copyfile(card, private_card)
        settings = work / "settings.txt"
        settings.write_text("ui_menus 0\nwidescreen 0\ninterpolate 0\nqol_skip_charging 0\ncamera_stick 0\n")
        env = {"ROOD_ORACLE": "1", "ROOD_OWNER": "battle_action=original",
               "ROOD_SETTINGS": str(settings), "ROOD_GI": "0", "ROOD_DETERMINISTIC": "1"}
        game = launch_at("save_room", card=str(private_card), fast=True, log=work / "game.log", extra_env=env)
        try:
            game.call("room_goto", zone=48, room=10, enemies=True, scripts=False)
            for _ in range(80):
                game.run(20)
                if game.call("room_goto", action="status")["phase"] in (0, 4):
                    break
            assert (game.state()["zone"], game.state()["room"]) == (48, 10)
            game.owner("battle_action", "shadow")
            game.call("owner", action="report", reset=True)
            game.run(600)
            result = game.call("owner", action="report")
            print(result, flush=True)
            assert result["calls"] > 0, "no routed battle calls exercised"
            assert result["count"] == 0 and result["errors"] == 0, result
            function = max(result["coverage"], key=lambda row: row["calls"])["function"]
            address = next(address for address, size, name in Symbols(BINARY).rows
                           if name == "vs_main_stateFlags")
            baseline = game.read(address, 1)
            game.call("owner", action="fault", function=function, address=address)
            game.call("owner", action="report", reset=True)
            game.run(120)
            fault = game.call("owner", action="report")
            game.call("owner", action="fault", function="", address=0)
            print("injected wrong owned write:", fault, flush=True)
            assert fault["count"] > 0 and not fault["errors"], fault
            first = fault["mismatches"][0]
            assert first["function"] == function and first["first"] == address and first["frame"] > 0, first
            assert game.read(address, 1) == baseline, "injected owned write escaped rollback"
            print(f"caught {function}: vs_main_stateFlags[0], frame {first['frame']}", flush=True)
            game.owner("battle_action", "original")
        finally:
            game.close()
            shutil.copyfile(work / "game.log", OUT / "shadow-battle.log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
