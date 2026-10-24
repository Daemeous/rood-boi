#!/usr/bin/env python3
"""Real-scene window aperture A/B captures and GPU timings (local output only).

Run with ROOD_ARCH=64 ROOD_RENDERER=vulkan. Uses a scratch card, lighting
user layer and settings. The paired paused frames differ only in pattern;
pattern_mode stays aperture in both, so a global angular-model change is
not mistaken for projected detail. --roi x,y,w,h measures a reviewed floor
crop; without it the whole-frame metric is diagnostic, NOT proof of bands.
"""
import argparse
import json
import os
import pathlib
import shutil
import sys
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from checkpoints import launch_at
from emissive_smoke import goto


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    ap.add_argument("--out", default=str(ROOT / "pc/build/window-pattern-visual"))
    ap.add_argument("--rooms", default="37:0,38:0,69:0,28:14")
    ap.add_argument("--settle", type=int, default=90)
    ap.add_argument("--frames", type=int, default=90)
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--dark-roi", help="reviewed projected opaque-band rectangle x,y,w,h; one room only")
    ap.add_argument("--bright-roi", help="adjacent projected bright-pane rectangle x,y,w,h; asserts band contrast")
    ap.add_argument("--roi", help="reviewed floor crop x,y,w,h (for a single room)")
    ap.add_argument("--require-change", action="store_true")
    args = ap.parse_args()
    if bool(args.dark_roi) != bool(args.bright_roi) or (args.dark_roi and "," in args.rooms):
        ap.error("--dark-roi and --bright-roi must be paired with a single --rooms zone:room")
    out = pathlib.Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    user = out / "lighting-user"
    user.mkdir(exist_ok=True)
    settings = out / "settings.cfg"
    settings.write_text("aa fxaa\nlighting_packs default\n")
    env = {"ROOD_LIGHTS": str(user), "ROOD_LIGHTING": str(ROOT / "lighting"),
           "ROOD_SETTINGS": str(settings), "ROOD_EMISSIVE_USER": str(out / "emissive.json"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports"), "ROOD_PERF": "1",
           "ROOD_WINDOW": "1280x960", "ROOD_SCALE": "2", "ROOD_GI": "1"}
    report = {"rooms": [], "metric": "GPU perf RPC; wall time includes paused present pacing", "roi": args.roi}
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env,
                     socket_path=os.environ.get("ROOD_RPC") or f"tcp:{48000 + os.getpid() % 10000}")
    failures = []
    try:
        game.call("graphics", lighting_preset="ultra", gi=True, interpolate=False)
        report["graphics"] = game.call("graphics")
        for pair in args.rooms.split(","):
            zone, room = map(int, pair.split(":"))
            listing = next((r for r in game.call("rooms")["rooms"]
                            if (r["zone"], r["room"]) == (zone, room)), None)
            if listing and not listing["entries"] and all(listing["floor"]):
                game.call("editor_camera", off=1)
                w, h = listing["floor"]
                arrival = game.call("room_goto", zone=zone, room=room,
                                    cell=[w // 2, 0, h // 2, 0], enemies=False, scripts=False)
                for _ in range(60):
                    game.run(20)
                    if game.call("room_goto", action="status")["phase"] in (0, 4):
                        break
                game.run(40)
                reached = (game.state()["zone"], game.state()["room"]) == (zone, room)
            else:
                arrival = {"room": listing}
                reached = goto(game, zone, room)
            if not reached:
                failures.append(f"warp {pair}: {arrival}; {game.call('room_goto', action='status')}; state={game.state()}")
                continue
            lights = game.call("lights")["lights"]
            ids = [light["id"] for light in lights if "gen.window_" in light["id"]]
            row = {"zone": zone, "room": room, "lights": ids, "patterns": {},
                   "state": game.state(), "camera": game.call("editor_camera"),
                   "emissive_uses": game.call("emissive", action="uses"),
                   "emissive_surfaces": {entry: game.call("emissive", action="surfaces", id=entry)
                                         for entry in ("E008", "E014", "E017")}}
            if not ids:
                failures.append(f"no window lights in {pair}")
            images = {}
            # Legacy baseline, uniform aperture, patterned aperture, windows off.
            for label, pattern, mode in (("legacy", 0, 0), (0, 0, 1), (1, 1, 1), ("off", 0, 1)):
                override = user / f"zone{zone:02d}_room{room:02d}.txt"
                override.write_text("rood-lighting 2\n" + "".join(
                    f"override {ident} pattern={pattern} pattern_mode={mode}"
                    + (" intensity=0" if label == "off" else "") + "\n" for ident in ids))
                game.call("lights", action="reload")
                game.call("redraw")
                game.wait_presents(args.settle, timeout=180)
                measurements = []
                for _ in range(args.repeats if label != "off" else 1):
                    game.call("perf", reset=True)
                    start = time.perf_counter()
                    game.wait_presents(args.frames, timeout=180)
                    measurements.append({"perf": game.call("perf"),
                        "wall_ms_per_present": (time.perf_counter() - start) * 1000 / args.frames})
                path = out / f"zone{zone:02d}-room{room:02d}-pattern{label}.png"
                game.present_screenshot(path)
                images[label] = np.asarray(Image.open(path).convert("RGB"), dtype=float)
                row["patterns"][str(label)] = {"measurements": measurements, "mode": mode,
                                               "pattern": pattern, "image": path.name}
            a, b = images[0], images[1]
            Image.fromarray(np.uint8(np.clip(np.abs(a - b) * 4, 0, 255))).save(
                out / f"zone{zone:02d}-room{room:02d}-difference-x4.png")
            if args.roi:
                x, y, w, h = map(int, args.roi.split(","))
                a, b = a[y:y+h, x:x+w], b[y:y+h, x:x+w]
            delta = b.mean(2) - a.mean(2)
            row["comparison"] = {"mean_delta": float(delta.mean()), "std_delta": float(delta.std()),
                                 "darkened_pixels": int((delta < -3).sum()),
                                 "changed_pixels": int((np.abs(delta) > 3).sum()),
                                 "pixels": int(delta.size)}
            if args.require_change and row["comparison"]["changed_pixels"] < 20:
                failures.append(f"no visible pattern difference {pair}")
            if args.dark_roi:
                def band(rect):
                    x, y, w, h = map(int, rect.split(","))
                    uniform = images[0][y:y+h, x:x+w] - images["off"][y:y+h, x:x+w]
                    patterned = images[1][y:y+h, x:x+w] - images["off"][y:y+h, x:x+w]
                    signal = float(uniform.mean())
                    return {"uniform_signal": signal,
                            "retained_light": float(patterned.mean()) / max(signal, 1e-6)}
                dark, bright = band(args.dark_roi), band(args.bright_roi)
                passed = (dark["uniform_signal"] > 2 and bright["uniform_signal"] > 2
                          and dark["retained_light"] < .5
                          and bright["retained_light"] - dark["retained_light"] > .3)
                row["band_regression"] = {"dark": dark, "bright": bright, "passed": passed,
                                           "dark_roi": args.dark_roi, "bright_roi": args.bright_roi}
                if not passed:
                    failures.append(f"projected floor band contrast {pair}")
            report["rooms"].append(row)
            (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps({"zone": zone, "room": room, **row["comparison"]}), flush=True)
    finally:
        game.close()  # this launch's PID only
    report["failures"] = failures
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
