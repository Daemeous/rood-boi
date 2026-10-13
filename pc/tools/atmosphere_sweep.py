#!/usr/bin/env python3
"""Visibility sweep of the rooms' atmosphere (haze, ground fog, fog volumes,
sun shafts): every room the lighting packs give a haze or a ground fog,
seen from several camera angles, must stay readable.

For each room it warps there (the Room Browser's warp), stops the fog's drift
(RPC environment {fog_time}) and renders, per pose, the frame with the
atmosphere (A), the atmosphere's light alone over black (H: RPC graphics
{haze_debug}) and the frame without it (B: ground fog and volumes off,
haze density 0). Poses: the game's own camera, then the editor's free camera
orbiting Ashley like the game's (ORBIT units behind her, LIFT above) from
YAWS directions, alternately looking down PITCH degrees and level along the
street, and looking up toward the moon. On the 3D view (below the HUD's top
band, above its bottom one), luminance 0-255:

- detail: Weber contrast of the picture's texture and edges, the mean
  gradient magnitude over (mean + DARK), of A over B's (1: unchanged; the
  atmosphere's extinction and its added light both lower it);
- veil: the share of pixels where the atmosphere's light (H) is over VEIL
  and brighter than the scene it covers (B): a sheet over the picture;
- shift: mean(A) - mean(B); haze_mean: mean(H).

A pose fails when detail < MIN_DETAIL or veil > MAX_VEIL, a room when the
fog's floor under Ashley (the floor grid) is above her feet; poses whose frame
without atmosphere is nearly flat (the camera in a wall, a black void: B's
deviation under 3) are skipped. Deterministic: frame-based stepping, the
drift stopped, fixed poses.

    python3 pc/tools/atmosphere_sweep.py                   every room (about 45 min; --yaws 4 about 25)
    python3 pc/tools/atmosphere_sweep.py --quick           four rooms, five poses (pc/tests/run.py suite)
    python3 pc/tools/atmosphere_sweep.py --rooms 32/0 40/6 --shots DIR
    python3 pc/tools/atmosphere_sweep.py --legacy --rooms 32/0   the medium without its limits and floor
                                                                 grid (shows what the test catches)

ROOD_ARCH=64 ROOD_RENDERER=vulkan pick the 64-bit Vulkan build (as pc/tests/run.py).
Exit status 0 when every pose passes (77: no memory card or no generated
atmosphere). Writes report.json (and contact sheets with --shots).
"""
import argparse
import json
import math
import os
import pathlib
import re
import shutil
import sys
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1].parent
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game  # noqa: E402

MIN_DETAIL = 0.5
DARK = 10
VEIL = 40
MAX_VEIL = 0.3
YAWS = 8
FLOOR_SLACK = 64  # world units the fog's floor may sit above Ashley's feet (a step, the grid's blur)
ORBIT, LIFT, PITCH = 650, 380, -22
# Rue Vermillion (the playtest's washout), Rue Lejour, Snowfly Forest, Undercity West.
QUICK = [(32, 0), (32, 15), (40, 6), (48, 4)]
LUMA = np.array([0.299, 0.587, 0.114], np.float32)


def default_card():
    from editor_cutaway_smoke import default_card as card
    return card()


def atmosphere_rooms(packs):
    """(zone, room) of every room file in the packs with a haze or a ground fog."""
    rooms = set()
    for path in sorted(packs.glob("*/generated/zone*_room*.txt")) + sorted(packs.glob("*/rooms/zone*_room*.txt")):
        m = re.match(r"zone(\d+)_room(\d+)\.txt", path.name)
        text = path.read_text(errors="replace")
        if m and ("env fog" in text or "haze_density" in text or "\nfog " in text):
            rooms.add((int(m.group(1)), int(m.group(2))))
    return sorted(rooms)


def floor_at(grid, x, z):
    """The floor grid's height at x, z (bilinear between cell centres, as medium.glsl)."""
    if not grid:
        return None
    n, (cx, cz), (sx, sz), h = grid["n"], grid["corner"], grid["cell"], grid["heights"]
    fx, fz = (x - cx) / sx - 0.5, (z - cz) / sz - 0.5
    i, j = int(fx // 1), int(fz // 1)
    tx, tz = fx - i, fz - j

    def cell(a, b):
        return h[min(max(b, 0), n - 1) * n + min(max(a, 0), n - 1)]
    return ((cell(i, j) * (1 - tx) + cell(i + 1, j) * tx) * (1 - tz)
            + (cell(i, j + 1) * (1 - tx) + cell(i + 1, j + 1) * tx) * tz)


def view(image):
    h = image.shape[0]
    return image[int(h * 0.12):int(h * 0.78)]


def gradient(lum):
    return float((np.abs(np.diff(lum, axis=0))[:, :-1] + np.abs(np.diff(lum, axis=1))[:-1, :]).mean())


def metrics(a, b, h):
    sb = float(b.std())
    out = {"mean_on": round(float(a.mean()), 1), "mean_off": round(float(b.mean()), 1),
           "std_off": round(sb, 1), "shift": round(float(a.mean() - b.mean()), 1),
           "veil": round(float(((h > VEIL) & (h > b)).mean()), 3), "haze_mean": round(float(h.mean()), 1)}
    if sb < 3:
        out["skipped"] = "flat without atmosphere"
        return out
    weber_a = gradient(a) / (float(a.mean()) + DARK)
    weber_b = gradient(b) / (float(b.mean()) + DARK)
    out["detail"] = round(weber_a / max(weber_b, 1e-4), 3)
    why = []
    if out["detail"] < MIN_DETAIL:
        why.append(f"detail {out['detail']:.2f} < {MIN_DETAIL}")
    if out["veil"] > MAX_VEIL:
        why.append(f"veil {out['veil']:.2f} > {MAX_VEIL}")
    if why:
        out["fail"] = ", ".join(why)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rooms", nargs="*", help="zone/room, ... (default: every room with atmosphere)")
    ap.add_argument("--quick", action="store_true", help=f"rooms {QUICK}, the game camera, three angles, the moon")
    ap.add_argument("--yaws", type=int, default=YAWS, help="free-camera directions around Ashley")
    ap.add_argument("--frames", type=int, default=40, help="vsyncs to settle each frame")
    ap.add_argument("--shots", help="contact sheets per room (with / without the atmosphere) and the failing "
                                    "poses' frames here")
    ap.add_argument("--keep-frames", action="store_true", help="with --shots: every pose's frames")
    ap.add_argument("--legacy", action="store_true", help="limits off, the main floor alone (the old medium)")
    ap.add_argument("--fog", type=int, default=1, help="ground fog and volumes on (settings key fog)")
    ap.add_argument("--card", default=None)
    ap.add_argument("--packs", help="the lighting packs (default lighting/; e.g. an older checkout's for a before/after)")
    ap.add_argument("--out", help="output directory (default ROOD_OUT/atmosphere_sweep)")
    args = ap.parse_args()
    card = pathlib.Path(args.card) if args.card else default_card()
    if not card.exists():
        print("SKIP: no memory card", card)
        return 77
    packs = pathlib.Path(args.packs) if args.packs else ROOT / "lighting"
    if args.rooms:
        rooms = [tuple(int(x) for x in r.split("/")) for r in args.rooms]
    elif args.quick:
        rooms = QUICK
    else:
        rooms = atmosphere_rooms(packs)
    if not rooms:
        print("SKIP: no room with a generated atmosphere (pc/tools/generate_lighting.py)")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "atmosphere_sweep"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(card, out / "card1.mcd")
    user = out / "user"
    user.mkdir()
    env = {"ROOD_LIGHTS": str(user), "ROOD_LIGHTING": str(packs), "ROOD_GI": "1", "ROOD_SCALE": "2",
           "ROOD_LIGHTING_PRESET": os.environ.get("ROOD_LIGHTING_PRESET", "ultra")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-sweep-{os.getpid()}.sock"
    game = Game.launch(fast=True, card=str(out / "card1.mcd"), log=out / "game.log", extra_env=env, socket_path=socket)
    report = {"renderer": os.environ.get("ROOD_RENDERER", "gl"), "arch": os.environ.get("ROOD_ARCH", "32"),
              "legacy": args.legacy, "thresholds": {"min_detail": MIN_DETAIL, "dark": DARK,
                                                    "veil": VEIL, "max_veil": MAX_VEIL}, "rooms": []}
    failures, poses_run, started, written = [], 0, time.monotonic(), []

    def warp(zone, room):
        """'' when there, else why not."""
        if (game.state()["zone"], game.state()["room"]) == (zone, room):
            return ""
        reply = game.call("room_goto", zone=zone, room=room)
        if not reply.get("ok"):
            return reply.get("message") or "refused"
        for _ in range(120):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(60)
        st = game.state()
        return "" if (st["zone"], st["room"]) == (zone, room) else "did not arrive"

    def frame(path, vsyncs):
        game.run(vsyncs)
        p = game.present_screenshot(path)
        image = np.asarray(Image.open(p).convert("RGB"), np.float32)
        written.append(p)
        return image

    try:
        game.wait_title()
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        for zone, room in rooms:
            entry = {"zone": zone, "room": room, "poses": []}
            report["rooms"].append(entry)
            why = warp(zone, room)
            if why:
                entry["skipped"] = f"no warp: {why}"
                print(f"zone {zone} room {room}: no warp ({why}; skipped)", flush=True)
                continue
            game.call("environment", fog_global=args.fog, fog_time=5, floor_grid=0 if args.legacy else 1)
            if args.legacy:
                game.call("environment", haze={"max_opacity": 1, "max_light": 0, "shafts": 1, "max_forward": 0})
            e = game.call("environment")
            density = e["haze"]["density"]
            entry["haze"], entry["fog"] = e["haze"], {k: v for k, v in e["fog"].items() if k != "floor_grid"}
            player = game.state()["player"]
            # The fog's floor under Ashley (the grid) is never above her feet (y down): a floor
            # on the roofs filled the street with fog (the playtest's washout).
            under = floor_at(e["fog"].get("floor_grid"), player["x"], player["z"])
            if under is not None and e["fog"]["defined"] and not args.legacy:
                entry["floor_under_player"] = round(under, 1)
                if under < player["y"] - FLOOR_SLACK:
                    failures.append(f"zone {zone} room {room}: the fog's floor under Ashley ({under:.0f}) is above "
                                    f"her feet ({player['y']})")
                    print("FAIL:", failures[-1], flush=True)
            poses = [("game", None)]
            yaws = [i * 360 / args.yaws for i in range(args.yaws)] if not args.quick else [0, 135, 225]
            for i, yaw in enumerate(yaws):
                pitch = PITCH if i % 2 == 0 else 0
                poses.append((f"yaw{int(yaw):03d}p{pitch:+d}", (yaw, pitch)))
            if e["sun"]["defined"] and e["sun"]["enabled"]:
                poses.append(("moon", (e["sun"]["azimuth"], min(e["sun"]["elevation"] * 0.6, 25))))
            row = []
            for name, pose in poses:
                if pose:
                    yaw, pitch = pose
                    fx, fz = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
                    game.call("editor_camera", x=player["x"] - fx * ORBIT, y=player["y"] - LIFT,
                              z=player["z"] - fz * ORBIT, yaw=yaw, pitch=pitch)
                else:
                    game.call("editor_camera", off=1)
                stem = (shots or out) / f"z{zone:02d}r{room:02d}-{name}"
                a = frame(stem.with_name(stem.name + "-on.png"), args.frames + 20)
                game.call("graphics", haze_debug=1)
                h = frame(stem.with_name(stem.name + "-haze.png"), 8)
                game.call("graphics", haze_debug=0, fog_density=0)
                game.call("environment", fog_global=0)
                b = frame(stem.with_name(stem.name + "-off.png"), args.frames)
                game.call("graphics", fog_density=density)
                game.call("environment", fog_global=args.fog)
                m = metrics(view(a) @ LUMA, view(b) @ LUMA, view(h) @ LUMA)
                m["pose"] = name
                entry["poses"].append(m)
                poses_run += 1
                status = "SKIP" if "skipped" in m else "FAIL" if "fail" in m else "ok"
                print(f"zone {zone:2d} room {room:2d} {name:7s} {status:4s} detail {m.get('detail', 0):.2f} "
                      f"veil {m['veil']:.2f} shift {m['shift']:+.1f} haze {m['haze_mean']:.1f}"
                      + (f"  ({m['fail']})" if "fail" in m else ""), flush=True)
                if "fail" in m:
                    failures.append(f"zone {zone} room {room} {name}: {m['fail']}")
                if shots:
                    row.append((a, b))
                for p in written:
                    if not shots or not (args.keep_frames or "fail" in m):
                        p.unlink()
                written.clear()
            game.call("editor_camera", off=1)
            if shots and row:
                tiles = [Image.fromarray(np.concatenate([a, b], axis=0).astype(np.uint8)).resize((320, 480))
                         for a, b in row]
                sheet = Image.new("RGB", (320 * len(tiles), 480))
                for i, t in enumerate(tiles):
                    sheet.paste(t, (320 * i, 0))
                sheet.save(shots / f"sheet-z{zone:02d}r{room:02d}.png")
    finally:
        game.close()
    log = (out / "game.log").read_text(errors="replace")
    bad = [line for line in log.splitlines() if "validation:" in line or "[crash]" in line or "[gl] shader" in line]
    if bad:
        failures.append(f"crash, shader error or validation message: {bad[:3]}")
    report["failures"] = failures
    report["seconds"] = round(time.monotonic() - started)
    (out / "report.json").write_text(json.dumps(report, indent=1))
    if shots:
        (shots / "report.json").write_text(json.dumps(report, indent=1))
    print(f"{len(rooms)} rooms, {poses_run} poses, {report['seconds']} s; report {out / 'report.json'}")
    for f in failures:
        print("FAIL:", f)
    print("PASS" if not failures else f"FAIL: {len(failures)} pose(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
