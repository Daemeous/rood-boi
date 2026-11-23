#!/usr/bin/env python3
"""Painting metal and roughness in Aseprite, live (pc/tools/paint_roundtrip.py watch and live; pc/tools/paint_live.py; Gitea #20).

    python3 pc/tests/aseprite_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Exports the save room with --painter aseprite, starts a headless game on the user layer, then plays the part of Aseprite:

- watch: a rewritten .aseprite file (the whole Metal layer white, as a Ctrl+S would leave it) is imported by `watch` and the
  object under Ashley's feet changes in the game's picture, with no reload.
- live: the same layer sent as a WebSocket frame to the bridge (the extension's message) changes the picture with no file written,
  and the time from the send to the frame that shows it is measured (target: under about 150 ms), also for a one-texel stroke.
- the in-memory map gives way to the file: after the watch imports a save, the picture is the file's.
- an emptied layer (erase to reset) puts the game's own picture back.
- a Material stroke (a preset's swatch colour on the Material layer) sets both maps at once: the picture changes, the bridge holds a
  metal and a roughness map for it, and erasing the layer puts the game's picture back.
- a Height stroke (a ramp over the texture) changes the relief in the relief normals view (Vulkan), erasing puts it back.
"""
import argparse
import json
import os
import pathlib
import shutil
import struct
import sys
import threading
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
os.environ.setdefault("ROOD_ARCH", "64")  # (before rood_rpc is imported: the 64-bit game)
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
os.environ["ROOD_PAINT_PRESETS_USER"] = "/nonexistent/paint-presets.json"  # (the owner's own preset file is not read)
import aseprite_io as ase  # noqa: E402
import editor_selection_smoke as es  # noqa: E402
import material_presets as swatches  # noqa: E402
import miniws  # noqa: E402
import paint_live  # noqa: E402
import paint_roundtrip as pr  # noqa: E402
from material_instances_smoke import frame  # noqa: E402
from rood_rpc import RpcError  # noqa: E402

BUDGET_MS = 150


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(es.pinned_card()))
    parser.add_argument("--out")
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else es.OUT / "aseprite"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    pkg, layer, catalog, maps = out / "pkg", out / "layer", out / "layer" / "materials.json", out / "layer" / "maps"
    layer.mkdir()
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    pr.export_package(argparse.Namespace(target="room", zone=None, room=None, model=None, model_at=None, bake=[], out=str(pkg), card=args.card, painter="aseprite"))
    seed = json.loads((pkg / "seed.json").read_text())
    files = sorted((pkg / "aseprite").glob("*.aseprite"))
    check(len(files) == len(seed["labels"]) > 10, f"an .aseprite file per material ({len(files)})")
    env = {"ROOD_MATERIAL_CATALOG": str(ROOT / "pc/assets/materials/catalog.json"), "ROOD_MATERIAL_CATALOG_USER": str(catalog),
           "ROOD_MATERIALS": str(maps), "ROOD_MATERIALS_SHIPPED": str(out / "shipped"), "ROOD_MATERIALS_BAKED": str(out / "baked"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports"), "ROOD_GI": "0"}
    s = es.start(argparse.Namespace(card=args.card, out=str(out / "game")), "aseprite_smoke", extra_env=env)
    if s is None:
        return 77
    stop, server = threading.Event(), None
    try:
        g, p = s.game, s.player
        feet = s.select(action="project", point=[p["x"], p["y"], p["z"]])["pixel"]
        g.call("editor_inspect", action="pick", x=feet[0], y=feet[1])
        mp = g.call("material", action="pick", x=feet[0], y=feet[1])
        label = f"{mp['key']}@{mp['object']['text']}"
        rec = seed["labels"][label]
        material = pr.safe(label)
        check(label in seed["labels"], f"the object under Ashley's feet is in the package ({label})")
        g.call("editor_ui", visible=False)
        g.call("graphics", interpolate=False, specular=1, view_mode=6)

        def shot(name):
            return frame(g, out / f"{name}.png")

        def changed(a, b):
            return int((np.abs(a - b).max(2) > 0)[quiet].sum())
        base = shot("base")
        quiet = ~(np.abs(base - shot("base2")).max(2) > 0)
        h, w = rec["h"], rec["w"]
        path = pkg / "aseprite" / f"{material}.aseprite"
        sprite = ase.read(path)

        def save(metal=None, roughness=None):
            """The file as Aseprite saves it: the layers it was exported with, Metal / Roughness replaced."""
            rows = [{"name": ly.name, "pixels": ly.pixels, "locked": ly.locked, "visible": ly.visible, "opacity": ly.opacity} for ly in ase.read(path).layers]
            if metal is not None:
                rows[2]["pixels"] = metal
            if roughness is not None:
                rows[3]["pixels"] = roughness
            ase.write(path, w, h, rows)

        def white(value=255):
            px = np.zeros((h, w, 4), np.uint8)
            px[..., :3], px[..., 3] = value, 255
            return px

        # watch: a save
        ns = argparse.Namespace(dir=str(pkg), layer=str(maps), catalog=str(catalog), debounce=0.3)
        lines = []
        thread = threading.Thread(target=pr.watch_package, args=(ns, stop, lines.append), daemon=True)
        thread.start()
        time.sleep(0.6)
        save(metal=white(), roughness=white(30))
        for _ in range(40):
            if (maps / rec["key"] / f"@{rec['object']}" / "metal.png").exists():
                break
            time.sleep(0.05)
        check((maps / rec["key"] / f"@{rec['object']}" / "metal.png").exists(), f"watch imported the saved file: {lines[-1:]}")
        for _ in range(8):
            watched = shot("watched")
            if changed(watched, base) > 50:
                break
        check(changed(watched, base) > 50, f"the saved Metal and Roughness change the picture with the game running ({changed(watched, base)} pixels)")

        # live: strokes over the WebSocket, no file
        save(metal=white(0), roughness=white(int(np.median(pr.painted_seed(pkg, rec, "roughness", h, w)))))  # (back to the game's own values (the seed's); the watch imports it: the files follow)
        for _ in range(100):  # (until the watch has imported the save)
            if sum("imported" in line for line in lines) >= 2:
                break
            time.sleep(0.05)
        time.sleep(0.3)
        for _ in range(8):  # (a paused game shows a changed map on the second redraw)
            reset = shot("reset")
            if changed(reset, watched) > 50:
                break
        check(changed(reset, watched) > 50, "saving the layers back to the game's values changes the picture again (the watch follows the file)")
        bridge = paint_live.Bridge(pkg, rpc=g.path, layer=str(maps), catalog=str(catalog))
        server = paint_live.serve(bridge, 0, log=lambda *_: None)
        port = server.server_address[1]
        ws = miniws.connect("127.0.0.1", port)
        hello = json.loads(ws.recv()[1])
        check(hello["ok"] and hello["game"], f"the bridge says hello and sees the game: {hello}")
        before_files = sorted(str(f) for f in maps.rglob("*") if f.is_file())

        def stroke(layer_name, rgba, x=0, y=0):
            header = json.dumps({"material": material, "layer": layer_name, "x": x, "y": y, "w": rgba.shape[1], "h": rgba.shape[0], "canvas": [w, h]}).encode()
            started = time.monotonic()
            ws.send(struct.pack(">I", len(header)) + header + np.ascontiguousarray(rgba).tobytes())
            reply = json.loads(ws.recv()[1])
            return started, reply

        def until_changed(started, reference, name):
            """Seconds from `started` to the first picture that differs from `reference` (the game redraws on its own while it runs)."""
            for _ in range(60):
                picture = frame(g, out / f"{name}.png")
                if changed(picture, reference) > 0:
                    return time.monotonic() - started, picture
            return None, picture
        started, reply = stroke("Metal", white())
        check(reply["ok"] and reply["game"], f"a stroke frame is accepted and pushed: {reply}")
        took, live = until_changed(started, reset, "live")
        check(took is not None and changed(live, reset) > 50, f"a whole-layer stroke changes the picture ({changed(live, reset)} pixels)")
        print(f"live: stroke to changed picture {took * 1000:.0f} ms (the bridge's own time {reply['ms']} ms; includes the screenshot's)", flush=True)
        # one texel, at an offset (a pencil stroke's cel)
        before = shot("texel-before")
        started, reply = stroke("Roughness", white(0))
        took2, _ = until_changed(started, before, "texel")
        check(took2 is not None, "a Roughness stroke is live as well")
        print(f"live: roughness stroke to changed picture {took2 * 1000:.0f} ms (bridge {reply['ms']} ms)", flush=True)
        check(reply["ms"] < BUDGET_MS and took < 1.0, f"the bridge applies a stroke in {reply['ms']} ms (budget {BUDGET_MS} ms); the picture follows in {took * 1000:.0f} ms with the screenshot's own cost")
        after_files = sorted(str(f) for f in maps.rglob("*") if f.is_file())
        check(after_files == before_files, "no map file was written by the strokes")

        # erase to reset, live
        started, reply = stroke("Metal", np.zeros((0, 0, 4), np.uint8))
        check(reply["ok"] and reply["game"] and reply["note"] == "", f"an emptied Metal layer is pushed: {reply}")
        started, reply = stroke("Roughness", np.zeros((0, 0, 4), np.uint8))
        check(reply["ok"] and reply["game"] and reply["note"] == "", f"an emptied Roughness layer is pushed: {reply}")
        again = None
        for _ in range(10):
            again = shot("erased")
            if changed(again, reset) < changed(live, reset) // 4:
                break
        check(changed(again, reset) < changed(live, reset) // 4, "erasing the layers puts the game's own picture back")

        # Material: a swatch's colour sets metal and roughness together
        gold = next(p for p in swatches.load() if p["id"] == "gold")
        swatch = np.zeros((h, w, 4), np.uint8)
        swatch[..., :3], swatch[..., 3] = gold["rgb"], 255
        before = shot("material-before")
        started, reply = stroke("Material", swatch)
        check(reply["ok"] and reply["game"] and reply["note"] == "", f"a Material stroke is accepted and pushed: {reply}")
        took3, picture = until_changed(started, before, "material")
        check(took3 is not None and changed(picture, before) > 50, f"a Material swatch over the whole texture changes the picture ({changed(picture, before)} pixels)")
        check((label, "metal") in bridge.sent and (label, "roughness") in bridge.sent
              and (bridge.sent[(label, "metal")] == round(gold["metal"] * 255)).all() and (bridge.sent[(label, "roughness")] == round(gold["roughness"] * 255)).all(),
              "...both maps were set, to the preset's values")
        print(f"live: Material stroke to changed picture {took3 * 1000:.0f} ms (bridge {reply['ms']} ms)", flush=True)
        stroke("Material", np.zeros((0, 0, 4), np.uint8))
        for _ in range(10):
            again = shot("material-erased")
            if changed(again, before) < changed(picture, before) // 4:
                break
        check(changed(again, before) < changed(picture, before) // 4, "erasing the Material layer puts the picture back")
        # Height: the layer's relief reaches the game (the relief normals view; a ramp over the texture), erasing puts the picture back
        g.call("graphics", view_mode=9, bump_strength=1)
        before = shot("height-before")
        quiet = quiet & ~(np.abs(before - shot("height-before2")).max(2) > 0)
        ramp = white()
        ramp[..., :3] = (np.arange(w) * 255 // max(w - 1, 1)).astype(np.uint8)[None, :, None]
        started, reply = stroke("Height", ramp)
        check(reply["ok"] and reply["game"] and reply["note"] == "", f"a Height stroke is accepted and pushed: {reply}")
        took4, picture = until_changed(started, before, "height")
        check(took4 is not None and changed(picture, before) > 20, f"a Height ramp over the texture changes the relief ({changed(picture, before)} pixels)")
        check((label, "height") in bridge.sent and (bridge.sent[(label, "height")][:, -1] == 255).all(), "...as the object's height map (the bridge pushed channel height)")
        print(f"live: Height stroke to changed picture {took4 * 1000:.0f} ms (bridge {reply['ms']} ms)", flush=True)
        stroke("Height", np.zeros((0, 0, 4), np.uint8))
        for _ in range(10):
            again = shot("height-erased")
            if changed(again, before) < changed(picture, before) // 4:
                break
        check(changed(again, before) < changed(picture, before) // 4, "erasing the Height layer puts the relief back")
        ws.close()
    except RpcError as e:
        check(False, f"RPC error {e}")
    finally:
        stop.set()
        if server:
            server.shutdown()
        es.finish(s, "aseprite")
    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: watch and live Aseprite painting reach the game")
    return 0


if __name__ == "__main__":
    sys.exit(main())
