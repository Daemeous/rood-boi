#!/usr/bin/env python3
"""The first render of the photo mode draws the held moment, not the held moment plus render_frames.

    python3 pc/tests/photo_firstmove_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

A render re-runs game frames, so a render from the held moment M used to end render_frames frames on and everything that
moves shifted once at the first one. A hold asked while the game runs is now led (photo.cpp): the game runs on for
render_frames frames, the inputs it reads logged, then M is held; a render replays those frames from the lead's mark and ends
on M. Two games, the game running live with the stick held (Ashley mid-stride in the Crumbling Market, 48/21, with the
column and the room's idlers; the save room as it stands, with its NPC, the column and the sigil moving):

- asked (rood.game.pause), the hold lands with its last frames kept (RPC photo history)
- the first render (the HUD hidden: the game's own camera, so the free camera's slightly different projection does not hide
  what is compared) equals the picture that was on the screen when the hold began, over Ashley and the room (< 0.05
  mean difference; it was 0.57 in the idle save room and 3 or more with her walking), and so does another render
- the game state digest is the held moment's throughout
- resuming continues from M exactly: from an RPC mark, a run that was held (a hold asked live, a render in between) and
  resumed has, at a later vsync, the heap, overlays, VRAM and clock hashes and Ashley's place of a run to that vsync that
  never held. (The draw bookkeeping, a counter of lists drawn, is not the game's state and is left out.)

FM_STICK (the stick's travel, 0.3) and FM_INTERP (frame interpolation on) vary the market game."""
import argparse
import os
import pathlib
import shutil
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT  # noqa: E402
from analog_common import Checks, default_card, field_game, stick_at, warp  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from photo_foundation_smoke import ppm, difference  # noqa: E402

PLUGIN = "rood.photo"
MIDDLE = (0.25, 0.2, 0.62, 0.78)  # Ashley and the room, clear of the HUD and the minimap
ROOM = (0.0, 0.15, 1.0, 0.66)  # the room from the HUD's panel down to the minimap
SAME = 0.05


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if os.environ.get("ROOD_RENDERER") == "null":
        print("SKIP: the photo mode needs a renderer")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "photo_firstmove"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    check = Checks()
    # Ashley walking at 30% of the stick in the Crumbling Market (zone 48 room 21), the column's scroll and the room's
    # idlers in view; and the save room as it stands (the NPC, the column and the sigil moving, Ashley still).
    scenario(out, "market", args.card, check, room=(48, 21), stick=0.3, region=MIDDLE)
    scenario(out, "save_room", args.card, check, room=None, stick=0.0, region=ROOM)
    return check.finish("photo_firstmove_smoke")


def scenario(out, name, card_source, check, room, stick, region):
    work = out / name
    work.mkdir()
    card = work / "card1.mcd"
    shutil.copyfile(card_source, card)
    (work / "settings.cfg").write_text("aspect 4:3\nui_menus 0\n")
    env = {"ROOD_WINDOW": "960x720", "ROOD_GI": "0", "ROOD_SETTINGS": str(work / "settings.cfg"),
           "ROOD_PLUGINS": str(work / "plugins"), "ROOD_PLUGIN_DATA": str(work / "plugin_data"),
           "ROOD_PHOTOS": str(work / "photos"), "ROOD_PAUSED_REDRAW_HZ": "60"}
    g = launch_at("save_room", card=card, fast=True, log=work / "game.log", extra_env=env)
    try:
        g.run(300)
        for _ in range(2):
            g.press("cross", vsyncs=3, settle=30)
        if room:
            check(warp(g, *room), f"{name}: warped to zone {room[0]} room {room[1]}")
        g.call("settings", apply=["analog_move 1", "analog_pace 1", "analog_dead_zone 20", "analog_min_speed 10",
                                  "analog_curve 100", "analog_anim_rate 100", "anim_blend 100"])
        g.call("graphics", interpolate=bool(os.environ.get("FM_INTERP")))
        g.run(30)

        def value(code):
            r = g.repl("eval", session="photo-firstmove", plugin=PLUGIN, code=code)
            if not r.get("ok"):
                raise rood_rpc.RpcError(f"{code}: {r.get('error')}")
            return r["results"][0] if r.get("results") else None

        def shot(tag):
            g.wait_presents(8)
            path = work / f"{tag}.ppm"
            path.unlink(missing_ok=True)
            g.call("present_dump", path=str(path))
            if not rood_rpc.wait_for_file(path, 30):
                raise rood_rpc.RpcError("no frame presented")
            return ppm(path)

        def held():
            deadline = time.monotonic() + 30
            while g.call("photo")["phase"] != "held" and time.monotonic() < deadline:
                time.sleep(0.02)
            return g.call("photo")

        # ---- the game runs live (not held by the RPC) with the stick held, so the hold is led ----
        g.call("input", vsyncs=100000, left_stick=stick_at(160, stick))
        g.pause(False)
        g.wait_presents(30)
        check(value("return rood.game.pause()") is True, f"{name}: asked (rood.game.pause)")
        p = held()
        check(p["phase"] == "held" and p.get("history") is True,
              f"{name}: held, its last frames kept (phase {p['phase']}, history {p.get('history')})")
        digest = g.call("snapshot", action="hash")
        entry = shot(f"entry")  # what was on the screen at M
        r0 = g.call("photo")["renders"]
        # (a render by the game's own camera: the HUD hidden, the camera not begun; the photo camera's own projection is
        # a fraction of a pixel off the game's, which would hide what is compared)
        value("return rood.ui.hide { hud = true }")
        first = shot("first")
        check(g.call("photo")["renders"] > r0, f"{name}: hiding the HUD is a render")
        d = difference(entry, first, region)
        check(d < SAME, f"{name}: the first render is the entry picture ({d:.3f} over {region})")
        value("return rood.ui.hide { hud = true, overlay = true }")
        d = difference(entry, shot("again"), region)
        check(d < SAME, f"{name}: another render: the same ({d:.3f})")
        check(g.call("snapshot", action="hash") == digest, f"{name}: the game state digest is the held moment's")
        value("rood.ui.show() rood.game.resume() return true")

        # ---- resuming continues from M: the state at a vsync after the hold equals a run that never held ----
        g.pause(True)
        g.mark()
        x = g.status()["vsync"]
        g.call("input", vsyncs=100000, left_stick=stick_at(160, stick))
        g.pause(False)
        g.wait_presents(5)
        check(value("return rood.game.pause()") is True, f"{name}: asked again")
        p = held()
        m = p["held_vsync"]
        check(p["phase"] == "held" and p.get("history") is True and m > x,
              f"{name}: held {m - x} vsyncs on (phase {p['phase']}, history {p.get('history')})")
        value("return rood.ui.hide { hud = true }")  # (a render in between)
        shot("second")
        value("rood.ui.show() rood.game.resume() return true")
        g.wait_presents(10)
        g.pause(True)
        t = g.status()["vsync"]
        held_run = g.call("snapshot", action="hash")
        held_state = g.state()
        g.rewind()
        g.call("input", vsyncs=100000, left_stick=stick_at(160, stick))
        g.run(t - x)
        plain = g.call("snapshot", action="hash")
        # (the game's heap and overlays, its VRAM and clock: the host's draw bookkeeping, a counter of the lists drawn, is not
        # the game's state)
        keys = ("heap", "overlays", "vram", "vsync")
        check(all(held_run[k] == plain[k] for k in keys),
              f"{name}: held, rendered and resumed, the game at vsync {t} is the run that never held")
        for k in keys:
            if held_run[k] != plain[k]:
                print(f"  {k}: held {held_run[k]}, plain {plain[k]}")
        check(held_state == g.state(), f"{name}: ... and so is its place ({held_state['player']})")
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
