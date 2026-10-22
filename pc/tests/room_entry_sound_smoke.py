#!/usr/bin/env python3
"""Room entry sound effects whose auto-pan has no wave, warped to fresh.

Prisoners' Niche (zone 15 room 0) and Underdark Fishmarket (zone 48 room 3):
an entry sound effect slides the auto-pan depth (opcode DF) of a channel that
never enabled auto-pan (opcode BC), so Sound_UpdateSlidesAndDelays reads the
channel's NULL LFO wave. The PS1 reads its RAM at address 0; the PC build
crashed there (SIGSEGV, 64-bit) until ROOD_PC read it through guest RAM
0x80000000 (25AC.c LFO_WAVE).

The crash needs the sound channels as a fresh launch leaves them (after
other rooms' effects the channel may hold a wave), so each room gets its own
game: launched at the card's save room (checkpoint save_room), RPC room_goto
with the room's scripts and enemies, ~600 vsyncs and the entry event driven
to its end. Checks the game arrived, is alive and logged no crash.

    python3 pc/tests/room_entry_sound_smoke.py --card pc/save/card1.mcd [--out DIR]
"""
import argparse
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402

ROOMS = ((15, 0), (48, 3))


def visit(card, out, socket, where):
    """(arrived, event ended, crash logged) for one fresh warp to `where`."""
    log = out / f"game_{where[0]}_{where[1]}.log"
    game = launch_at("save_room", card=card, fast=True, log=log, socket_path=socket)
    arrived = ended = False
    try:
        reply = game.call("room_goto", zone=where[0], room=where[1], scripts=True, enemies=True)
        for _ in range(30):  # the room loads; its entry event and sounds start
            game.run(20)
            st = game.state()
            arrived = arrived or (st["zone"], st["room"]) == where
        for k in range(200):  # the entry event to its end (dialogue: buttons)
            event = game.read_symbol("D_800F4C34", 1)[0]
            if event == 0 and game.call("room_goto", action="status")["phase"] in (0, 4):
                ended = reply["ok"]
                break
            if event:
                game.press("cross" if k % 2 else "circle", vsyncs=6, settle=20)
            game.run(20)
        game.run(60)
        ended = ended and game.state()["zone"] == where[0]
    except (OSError, RpcError):  # the game is gone
        pass
    finally:
        try:
            game.close()
        except (OSError, RpcError):
            pass
    return arrived, ended, "[crash]" in log.read_text(errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    ap.add_argument("--out")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "room_entry_sound"
    out.mkdir(parents=True, exist_ok=True)
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-entry-sound-{os.getpid()}.sock"
    failures = []
    for where in ROOMS:
        arrived, ended, crashed = visit(args.card, out, socket, where)
        ok = arrived and ended and not crashed
        print(("ok:   " if ok else "FAIL: ")
              + f"zone {where[0]} room {where[1]}, scripts and enemies on, from a fresh launch: "
              f"arrived {arrived}, entry event ended {ended}, crash {crashed}")
        if not ok:
            failures.append(where)
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
