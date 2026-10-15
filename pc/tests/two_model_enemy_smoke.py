#!/usr/bin/env python3
"""Two-model enemies (a Ghost) and the zone context's enemy tables on LP64.

Enemies whose SHP id is flagged in D_8004FE88 (Ghost 26, Wraith 71,
Guildenstern 79/175) take two actor slots: func_800775C0 loads a second model
into slot id + 4 with model id _zoneContext.zndEnemies[zud].unk2 + 1. The
decompiled code read that table through D_800F188C, which is
_zoneContext.zndEnemies (0x800F1880 + 0xC); func_8008A3A0 (leaving a room)
freed _zoneContext.mpdEnemies through D_800F18E4 (+0x64). On the 64-bit build
those names were host pointers in native slots of their own that nothing
wrote (pc/tools/lp64_alias_scan.py): a Ghost dereferenced NULL, and every
room's enemy spawn table leaked from the game's heap.

Continues a copy of the card's save, warps with enemies to the Catacombs'
Lamenting Mother (zone 13 room 4, one Ghost) and checks that the Ghost has
a two-model actor, the room is drawn, and the game plays on; then leaves and
re-enters the room and checks the game heap's free total comes back each time.

    python3 pc/tests/two_model_enemy_smoke.py --card pc/save/card1.mcd [--out DIR]
"""
import argparse
import os
import pathlib
import shutil
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402

GHOST_ROOM = (13, 4)  # Catacombs, The Lamenting Mother: one Ghost
OTHER_ROOM = (13, 1)  # Catacombs, The Last Blessing: three one-model enemies


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    ap.add_argument("--out")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "two_model_enemy"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    for d in ("lights", "prefabs", "lighting"):
        (out / d).mkdir()
    env = {"ROOD_GI": "0", "ROOD_LIGHTS": str(out / "lights"), "ROOD_PREFABS": str(out / "prefabs"),
           "ROOD_LIGHTING": str(out / "lighting")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-twomodel-{os.getpid()}.sock"
    ptr = 8 if os.environ.get("ROOD_ARCH") == "64" else 4
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)

    def u32(address):
        return int.from_bytes(game.read(address, 4), "little")

    def warp(zone, room):
        reply = game.call("room_goto", zone=zone, room=room, enemies=True, scripts=False)
        for _ in range(80):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(60)
        st = game.state()
        check(reply["ok"] and (st["zone"], st["room"]) == (zone, room),
              f"warped to zone {zone} room {room} (now zone {st['zone']} room {st['room']})")

    def actors():
        raw = game.read_symbol("vs_battle_actors", 16 * ptr)
        return [int.from_bytes(raw[i * ptr:(i + 1) * ptr], "little") for i in range(16)]

    def heap_free():
        """Free 16-byte blocks on the game heap (heapA's circular free list)."""
        head = symbols()["heapA"]
        raw = game.read_symbol("heapA", 12)
        node, total = struct.unpack_from("<I", raw, 4)[0], 0
        for _ in range(4096):
            if node == head or node == 0:
                break
            total += u32(node + 8)
            node = u32(node + 4)
        return total

    try:
        check(game.state().get("player") is not None, "continued into gameplay")
        warp(*GHOST_ROOM)
        flags = game.read_symbol("D_8004FE88", 256)
        zone = game.read_symbol("_zoneContext", 0x6C)
        znd, count, mpd = (struct.unpack_from("<I", zone, o)[0] for o in (0xC, 0x60, 0x64))
        shps = [game.read(znd + game.read(mpd + i * 0x28 + 4, 1)[0] * 0x464 + 2, 1)[0] for i in range(count)]
        check(any(flags[s] for s in shps), f"the room spawns a two-model enemy (SHP ids {shps})")
        # A two-model actor (0x3BB4 bytes, unk40 = 2) reserves data slot
        # id + 4 for the second model, which func_800775C0 loads with the
        # model id read from zndEnemies.
        two = [j for j, a in enumerate(actors()) if 2 <= j < 10 and a and u32(a + 0x40) == 2]
        check(bool(two), f"the enemy is a two-model actor (actors with unk40 = 2: {two})")
        frames = []
        for k in range(3):
            game.run(20)
            path = out / f"ghost_{k}.png"
            game.screenshot(str(path))
            frames.append(path.read_bytes())
        check(len(set(frames)) > 1, "the room is drawn and animating")
        game.run(300)
        check(game.state()["zone"] == GHOST_ROOM[0], "the game plays on for 300 vsyncs")

        # Leaving a room frees its spawn table (func_8008A3A0): the heap's
        # free total is the same on each return.
        warp(*OTHER_ROOM)  # the first visit came from the save room
        warp(*GHOST_ROOM)
        free = [heap_free()]
        for _ in range(3):
            warp(*OTHER_ROOM)
            warp(*GHOST_ROOM)
            free.append(heap_free())
        check(len(set(free)) == 1, f"game heap free blocks on each return to the Ghost's room: {free}")
    finally:
        game.close()
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log, "no crash")
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
