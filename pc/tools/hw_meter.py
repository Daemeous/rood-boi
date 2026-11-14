#!/usr/bin/env python3
"""The renderer hook meter (pc/src/hw_meter.h, T1 plan R0): one short run of a scene with the hardware (Vulkan) renderer, then
every pc_hw_* hook's calls, rates and time, what the return-value hooks answered and why, and the hooks called inside the
draw walk that the producer/consumer split would not list.

    python3 pc/tools/hw_meter.py [--scene fight|field|menu|opening] [--frames N] [--binary PATH] [--json OUT]

Scenes: field (the save room, idle), fight (zone 48 room 10 with its enemies, as build_compare's fight), menu (the main menu
opened), opening (New Game's first frames). Timing needs ROOD_PERF=1 (set here). One run, one game: a few minutes at most."""
import argparse
import json
import os
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def scene(g, name, frames):
    if name == "fight":
        g.call("room_goto", zone=48, room=10, enemies=True, scripts=False)
        for _ in range(80):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.call("hw_meter", reset=True)
    elif name == "menu":
        g.run(60)
        g.call("hw_meter", reset=True)
        g.press("triangle", vsyncs=3, settle=30)
    elif name == "opening":
        g.call("hw_meter", reset=True)
        g.press("start", vsyncs=4, settle=90)
        g.press("circle", vsyncs=3, settle=0)
    else:
        g.run(60)
        g.call("hw_meter", reset=True)
    for _ in range(max(1, frames // 20)):
        g.run(20)
        if name == "menu":
            g.press("down", vsyncs=3, settle=4)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scene", default="fight", choices=["fight", "field", "menu", "opening"])
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--binary", default=str(ROOT / "pc/build/boot64/rood_battle_probe"))
    parser.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--aspect", default="4:3", choices=["4:3", "16:9"])
    parser.add_argument("--digest", help="write the stream digest ring (vsync, digest) as JSON")
    parser.add_argument("--json")
    args = parser.parse_args()
    os.environ.update(ROOD_BINARY=args.binary, ROOD_SYMBOLS=args.binary, ROOD_HEADLESS="egl", ROOD_RENDERER="vulkan",
                      ROOD_ARCH="64", ROOD_TEST_SNAPSHOTS="0")
    sys.path[:0] = [str(ROOT / "pc/tools"), str(ROOT / "pc/tests")]
    from checkpoints import launch_at
    out = pathlib.Path(tempfile.mkdtemp(prefix="hw_meter_"))
    (out / "settings.cfg").write_text("qol_skip_charging 0\nui_menus 0\naspect %s\n" % args.aspect)
    name = "title" if args.scene == "opening" else "save_room"
    g = launch_at(name, card=args.card, fast=True, log=out / "game.log", extra_env={
        "ROOD_GI": "0", "ROOD_INTERPOLATE": "0", "ROOD_SETTINGS": str(out / "settings.cfg"), "ROOD_AUDIO": "none",
        "ROOD_WINDOW": "1280x720" if args.aspect == "16:9" else "960x720", "ROOD_HW_METER": "1", "ROOD_HW_QUEUE_CHECK": "1", "ROOD_PERF": "1", **({"ROOD_WIDESCREEN": "1"} if args.aspect == "16:9" else {})})
    try:
        g.call("graphics", interpolate=False)
        if os.environ.get("ROOD_STALE_CHECK"):
            print("[stale] graphics:", {k: v for k, v in g.call("graphics").items() if "aspect" in k or "wide" in k})
        scene(g, args.scene, args.frames)
        if args.digest:
            st = g.call("stream", count=256)
            pathlib.Path(args.digest).write_text(json.dumps(st))
        meter = g.call("hw_meter")
        try:
            meter["vram_stream"] = g.call("vram_stream")
        except Exception:
            pass
    finally:
        g.close()
    if os.environ.get("ROOD_RT_TRACE") or os.environ.get("ROOD_STALE_CHECK") or os.environ.get("ROOD_STREAM_TRACE"):  # the direct hook calls while the renderer thread is on (render_thread.c), from the game's log
        try:
            for line in (out / "game.log").read_text(errors="ignore").splitlines():
                if line.startswith("[render]") or line.startswith("[stale]") or line.startswith("[stream"):
                    print(line)
        except OSError:
            pass
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(meter, indent=1))
    frames = meter["frames"]
    print(f"scene {args.scene}: {frames} frames, {meter['walks']} draw walks ({meter['walks'] / max(1, frames):.1f} a frame)")
    print(f"{'hook':26} {'cls':3} {'calls':>10} {'/frame':>9} {'max/frame':>9} {'in walk':>9} {'off thr':>8} {'us/frame':>9}")
    for h in sorted(meter["hooks"], key=lambda h: -h["calls"]):
        us = h["ns"] / 1000.0 / max(1, frames)
        print(f"{h['hook']:26} {h['class']:3} {h['calls']:10} {h['per_frame']:9.1f} {h['max_per_frame']:9} {h['in_walk']:9} "
              f"{h['off_thread']:8} {us:9.1f}")
    print("\nreturn-value hooks:")
    for r in meter["results"]:
        print(f"  {r['why']:20} returned 0: {r['returned_0']:9}  returned 1: {r['returned_1']:9}  depends on: {r['depends_on']}")
    stream = meter.get("vram_stream")
    if stream:
        print("\nVRAM stream (blocking fences, by cause):", json.dumps(stream.get("fences", stream))[:300])
    bad = [h for h in meter["hooks"] if h["unlisted_in_walk"]]
    print("\nin the walk but not command, native list, query or VRAM event:",
          ", ".join(f"{h['hook']} ({h['class']}, {h['unlisted_in_walk']})" for h in bad) or "none")
    off = [h for h in meter["hooks"] if h["off_thread"]]
    print("called from another thread:", ", ".join(f"{h['hook']} ({h['off_thread']})" for h in off) or "none")


if __name__ == "__main__":
    main()
