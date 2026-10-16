#!/usr/bin/env python3
"""The editor through room loads: its room groups follow the game's loaded
room block, never a freed one.

    python3 pc/tests/editor_roomload_smoke.py [--card pc/save/card1.mcd] [--out DIR]

With the editor open, the undo snapshot (editor_undo.cpp capture) reads the
room's groups every overlay frame (pc_room_editor_groups, room_native.c).
It used to read the block scene_capture.c last captured even after the game
freed it (func_8008ABF0) and the disc was loading the next room into that
memory: a half-written block passed the group-count check and a garbage
geometry offset read outside guest RAM (segfault right after a room load).
Now the groups are read only from the game's current block
(vs_battle_roomData: loaded, its geometry section the captured one), and
every group offset and polygon count is checked against the section's size.

From the card's save, the editor open, the game is stepped one vsync at a
time through room changes (room_goto: a door in the same zone, three other
zones, the return home; and walking through a door from a room's entry),
and every vsync:
- RPC room_groups reports the groups of the game's loaded block or none:
  while the game has no room loaded it reports none (the old code reported
  the freed block's), with one loaded the same group count and the first
  group's position as the block in guest RAM;
- room_groups' "room" state is "loading" while no room block is loaded or
  it is not captured yet, "ready" when the groups are read;
and after each change: "ready" with a sane group count (1..4096) and the
scene captured; every change of room shows "loading" vsyncs (the guard
engaged); no crash. On the old code the warps to other zones fail: groups
of the freed block for the vsyncs the game had no room loaded.
"""
import argparse
import os
import pathlib
import shutil
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402

# vs_battle_roomData (_mpdRoomSection, 146C.h): unk0 (1 while a room is
# loaded), header.geometrySectionLen, geometrySection (a 4-byte guest
# pointer on every build).
ROOM_STATE, ROOM_GEOMETRY_LEN, ROOM_GEOMETRY = 0x00, 0x04, 0x64
TARGETS = ((11, 0), (13, 0), (50, 0))  # other zones (the first two listed are used)
MAX_VSYNCS = 3000  # one room change, stepped one vsync at a time

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    ap.add_argument("--out")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "editor_roomload"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = out / "lights"
    lights.mkdir()
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "2"}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-roomload-{os.getpid()}.sock"  # sun_path is short
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)

    def live():
        """The game's loaded room block: (group count, first group's
        position) or None while no room is loaded."""
        raw = game.read_symbol("vs_battle_roomData", ROOM_GEOMETRY + 4)
        geometry = struct.unpack_from("<I", raw, ROOM_GEOMETRY)[0]
        length = struct.unpack_from("<I", raw, ROOM_GEOMETRY_LEN)[0]
        if raw[ROOM_STATE] != 1 or not geometry or length < 4:
            return None
        count = struct.unpack("<I", game.read(geometry, 4))[0]
        position = list(struct.unpack("<3i", game.read(geometry + 8, 12))) if 0 < count <= 4096 else None
        return count, position

    stats = {}

    def sample(label):
        """One vsync's check; returns room_groups' state."""
        reply = game.call("room_groups")
        groups, state = reply["groups"], reply.get("room")
        block = live()
        s = stats.setdefault(label, {"vsyncs": 0, "loading": 0, "unloaded": 0, "stale": [], "mismatch": []})
        s["vsyncs"] += 1
        s["loading"] += state == "loading"
        if block is None:
            s["unloaded"] += 1
            if groups:  # the freed block's groups
                s["stale"].append((game.status()["vsync"], len(groups)))
        elif groups:
            count, position = block
            if len(groups) != count or groups[0]["position"] != position:
                s["mismatch"].append((game.status()["vsync"], len(groups), count, groups[0]["position"], position))
        if state == "ready" and (block is None or not groups):
            s["mismatch"].append((game.status()["vsync"], "ready", len(groups), block))
        return state

    def settle(label, done):
        """Step one vsync at a time until done() and room_groups is ready."""
        for _ in range(MAX_VSYNCS):
            game.run(1)
            state = sample(label)
            if state in ("ready", None) and done():  # (None: a build without the state)
                break
        game.run(2)
        sample(label)

    def report(label, where, changed=True):
        s = stats[label]
        groups = game.call("room_groups")
        n = len(groups["groups"])
        st = game.state()
        block = live()
        check(not s["stale"], f"{label}: no groups read while no room was loaded "
                              f"({s['unloaded']} unloaded vsyncs; stale reads {s['stale'][:3]})")
        check(not s["mismatch"], f"{label}: the groups are always the loaded block's ({s['mismatch'][:3]})")
        # (the same room loaded again can land on the same block, unchanged:
        # it may stay ready throughout)
        check(s["loading"] > 0 or not changed,
              f"{label}: room_groups reported 'loading' for {s['loading']} of {s['vsyncs']} vsyncs")
        check((st["zone"], st["room"]) == where and groups.get("room") == "ready" and 0 < n <= 4096
              and block is not None and n == block[0] and game.call("scene")["triangles"] > 0,
              f"{label}: arrived in zone {st['zone']} room {st['room']}, ready, {n} groups "
              f"(block {block and block[0]})")

    def warp(label, zone, room, **kw):
        st = game.state()
        changed = (st["zone"], st["room"]) != (zone, room)
        game.call("room_goto", zone=zone, room=room, **kw)

        def arrived():
            st = game.state()
            return game.call("room_goto", action="status")["phase"] in (0, 4) and (st["zone"], st["room"]) == (zone, room)

        settle(label, arrived)
        report(label, (zone, room), changed)

    try:
        state = game.state()
        home = (state["zone"], state["room"])
        game.call("graphics", gi=False)
        game.call("editor_ui", visible=True, rooms=True)
        game.run(4)
        first = game.call("room_groups")
        check(first.get("room") == "ready" and len(first["groups"]) == (live() or (0,))[0] > 0,
              f"save room {home}: room_groups ready with the loaded block's {len(first['groups'])} groups")

        rooms = game.call("rooms")
        listed = {(r["zone"], r["room"]): r for r in rooms["rooms"]}
        same = next((r for r in rooms["rooms"] if r["zone"] == home[0] and r["entries"]
                     and (r["zone"], r["room"]) != home), None)
        targets = ([(same["zone"], same["room"])] if same else []) + \
            [t for t in TARGETS if t in listed and listed[t]["entries"] and t[0] != home[0]][:2]
        for zone, room in targets:
            warp(f"warp {zone}/{room}", zone, room)

        game.call("room_goto", action="return")

        def at_home():
            st = game.state()
            return game.call("room_goto", action="status")["phase"] in (0, 4) and (st["zone"], st["room"]) == home

        settle("return home", at_home)
        report("return home", home)

        # Walk through a door: a warp puts Ashley at a room's entry, just
        # inside the door she came in by (the camera looks along a diagonal,
        # so the way back is often two buttons: 9/0's is down+right); walk
        # each way (from the entry again each time) until the room changes,
        # stepping every vsync of the walk and the load.
        start = targets[0] if targets else home
        walked = None
        for n, direction in enumerate((["down", "right"], ["down", "left"], ["up", "left"], ["up", "right"],
                                       ["down"], ["up"], ["left"], ["right"])):
            warp(f"walk start {n + 1}", *start)
            game.press("cross", vsyncs=3, settle=10)  # out of battle mode (doors open only outside it)
            label = f"walk {'+'.join(direction)} from {start[0]}/{start[1]}"
            for i in range(160):
                if i % 8 == 0:  # short holds: none left over after the door
                    game.call("input", buttons=direction, vsyncs=8)
                game.run(1)
                sample(label)
                st = game.state()
                if (st["zone"], st["room"]) != start:
                    walked = (st["zone"], st["room"])
                    break
            if walked:
                # The room id changes as the door is taken; the load follows
                # under the door's screen transition: done once it ends.
                transition = {"seen": False}

                def through():
                    step = struct.unpack("<I", game.read_symbol("vs_battle_screenTransitionStep", 4))[0]
                    transition["seen"] |= step != 0
                    st = game.state()
                    return transition["seen"] and step == 0 and (st["zone"], st["room"]) == walked

                settle(label, through)
                report(label, walked)
                break
            stats.pop(label, None)
        check(walked is not None, f"walked through a door from {start} into {walked}")
    except (OSError, RpcError) as e:  # the game is gone
        check(False, f"the game answers ({e})")
    finally:
        game.close()
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log, "no crash")
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
