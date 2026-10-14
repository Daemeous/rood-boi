#!/usr/bin/env python3
"""Route continuation through a room link (766FC.s L800DF8A4) in the game.

The path an enemy takes after it has used one of a room's links (a gate,
byte 0x19B of its AI state, set by func_800DFBCC; the room records held
gates in byte 0xE of D_800F58BC). Its next movement command for priority
state 0 (func_800E4B2C / func_800E4B70, or the route records of
func_800E5308) continues the route through the link; on ordinary ground it
releases the gate. The port used to stop there ("[UNIMPLEMENTED] actor route
continuation L800DF8A4").

Continues a copy of the card's save, warps with enemies (no scripts) to the
Wine Guild Hall (zone 9 room 5, one of the few rooms with links), marks
gate 3 held by every enemy and in the room, and plays: the enemies whose
movement runs through func_800E4B2C/func_800E4B70 continue the route
(pc_route_continuations counts it), release the gate (state byte 0x19B 0,
far end 0x19C -1, room bit 3 clear), and the game goes on.

    python3 pc/tests/route_continuation_smoke.py --card pc/save/card1.mcd [--out DIR]
"""
import argparse
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402

GATE = 3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    ap.add_argument("--out")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "route_continuation"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    for d in ("lights", "prefabs", "lighting"):
        (out / d).mkdir()
    env = {"ROOD_GI": "0", "ROOD_LIGHTS": str(out / "lights"), "ROOD_PREFABS": str(out / "prefabs"),
           "ROOD_LIGHTING": str(out / "lighting")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-route-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    ptr = 8 if os.environ.get("ROOD_ARCH") == "64" else 4

    def u32(address):
        return int.from_bytes(game.read(address, 4), "little")

    def continuations():
        return int.from_bytes(game.read_symbol("pc_route_continuations", 4), "little")

    try:
        check(game.state().get("player") is not None, "continued into gameplay")
        reply = game.call("room_goto", zone=9, room=5, enemies=True, scripts=False)
        check(reply["ok"], f"warp requested: {reply.get('message')}")
        for _ in range(80):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(30)
        st = game.state()
        check((st["zone"], st["room"]) == (9, 5), f"in the Wine Guild Hall (zone {st['zone']} room {st['room']})")
        raw = game.read_symbol("D_800F5878", 16 * ptr)
        states = [int.from_bytes(raw[i * ptr:(i + 1) * ptr], "little") for i in range(16)]
        enemies = [a for i, a in enumerate(states) if i and a]
        free = [a for a in enemies if game.read(a + 0x125, 1)[0] == 0]
        check(len(free) > 0, f"{len(enemies)} enemies with AI states, {len(free)} moving through func_800E4B2C/4B70")
        room = int.from_bytes(game.read_symbol("D_800F58BC", 4), "little")
        check(room != 0, f"the room's navigation record at {room:#x}")

        before = continuations()
        released = 0
        for attempt in range(12):
            for a in enemies:
                game.write(a + 0x19b, bytes([GATE]))
                game.write(a + 0x19c, (0x00050004).to_bytes(4, "little"))
            game.write(room + 0xE, bytes([1 << GATE]))
            game.run(30)
            for a in free:
                if game.read(a + 0x19b, 1)[0] == 0:
                    released += 1
                    check(u32(a + 0x19c) == 0xFFFFFFFF, f"  the enemy's far end is reset ({u32(a + 0x19c):#x})")
                    check(not (game.read(room + 0xE, 1)[0] >> GATE) & 1, "  and the room's gate bit is clear")
            if released:
                break
            game.press("circle", 3, 30)  # battle mode: the enemies re-plan
        ran = continuations() - before
        check(ran > 0 and released > 0, f"route continuations ran {ran} times; {released} gate releases seen")
        for a in enemies:  # let go of the remaining marks
            game.write(a + 0x19b, bytes([0]))
        game.run(600)
        check(game.state()["zone"] == 9, "the game plays on for 600 vsyncs")
    finally:
        game.close()
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[UNIMPLEMENTED]" not in log and "[crash]" not in log, "no [UNIMPLEMENTED] or crash")
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
