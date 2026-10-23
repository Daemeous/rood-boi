#!/usr/bin/env python3
"""The photo mode's free camera sees the whole room and the sky (pc/docs/Extensions.md
"rood.camera", room_editor.h pc_photo_full_room) on a renderer (run.py runs it with
--renderer gl and vulkan):

    python3 pc/tests/photo_full_room_smoke.py [--card ...] [--out DIR]

One game from the save_room checkpoint; the example plugin pc/tests/lua/plugins/test.photo
with its capabilities granted (as photo_foundation_smoke), driven from the REPL:
  - normal play keeps the game's own culling: the room's facing cutaway leaves groups out
    (RPC room_groups drawn), the same after the session as before;
  - held, the HUD and 2D hidden, the free camera turned about Ashley (half a turn, from
    above): every group the room has is drawn (below), and where the room's mesh projects (its front faces, the
    exact projection the render used: RPC scene_dump's camera) the picture is not black:
    at most 6 % of those pixels, at least 3 points more with photo full_room 0 (the
    editor's rules: the game's cutaway for the free camera's yaw: the near beams, crates
    and wall gone), which shows what the test catches;
  - the camera given back (finish): the render is the game camera's again, byte for byte
    the one before the camera began; resumed with the state digest the held one;
  - the render's room list holds every group the room has (RPC render_stats instances): not
    the black shells, the room's own hidden ones (flag 0x100: event scripts) nor the alternate
    set; the facing cutaway's and the game camera's hide list's groups (flag 0x1000: room
    section 11, by where Ashley stands and the camera's direction) drawn: in the save room
    turned, and in Snowfly Forest (zone 40 room 0, two groups on the list) at the game's pose;
  - outdoors (Rue Vermillion, zone 32 room 0): from Ashley's head looking up, the sky (the
    game's background sphere, about the free camera's eye) fills the open part of the view:
    at least 8 % sky-blue pixels (none before: it was centred on the game camera's eye,
    off the screen).
Deterministic: every wait is on a condition (renders counted, frames presented)."""
import argparse
import os
import pathlib
import shutil
import sys
import time

import numpy as np
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT, Deadline  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402
from photo_foundation_smoke import ppm  # noqa: E402

CAPABILITIES = ["game.pause", "camera", "post", "ui.hide", "capture", "input.capture"]
DIGEST = ("ram", "scratch", "io", "vram", "overlays", "vsync")
OUTDOOR = (32, 0)  # Rue Vermillion: open to the sky
LISTED = (40, 0)  # Snowfly Forest: its hide list (room section 11) leaves groups out for the game camera
WINDOW = (960, 720)


def image(p):
    w, h, data = p
    return np.frombuffer(data, np.uint8).reshape(h, w, 3)


def scene(path):
    """(camera {R, T, H, OF}, triangles: [(v0, v1, v2, black, flags)]) of an RPC scene_dump OBJ."""
    camera, vertices, triangles = None, [], []
    for line in pathlib.Path(path).read_text().splitlines():
        if line.startswith("# camera "):
            w = line.split()
            r = [float(x) for x in w[3:12]]
            camera = {"R": np.array(r).reshape(3, 3), "T": np.array([float(x) for x in w[13:16]]),
                      "H": float(w[17]), "OF": (float(w[19]), float(w[20]))}
        elif line.startswith("v "):
            w = line.split()
            vertices.append(([float(x) for x in w[1:4]], max(float(x) for x in w[4:7])))
        elif line.startswith("f "):
            w = line.split()
            a, b, c = (int(x) - 1 for x in w[1:4])
            flags = int(line.split("flags ")[1], 16)
            black = max(vertices[a][1], vertices[b][1], vertices[c][1]) == 0
            triangles.append((vertices[a][0], vertices[b][0], vertices[c][0], black, flags))
    return camera, triangles


def coverage(camera, triangles, size, display_height=240, near=32.0):
    """A mask (size) of the pixels the room's front faces cover in this camera's view (GTE
    projection: s = OF + H xy / z, 320 x display_height game pixels over the window), left out:
    the black shells, the room's hidden groups and the alternate set (flags 0x1100, 0x8000),
    and triangles reaching the near plane (so the mask can only be smaller than the truth)."""
    w, h = size
    sx, sy = w / 320.0, h / float(display_height)
    top = (display_height - 224) / 2.0  # the field's 224 lines centred in the display
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    R, T, H = camera["R"], camera["T"], camera["H"]
    ofx, ofy = camera["OF"]
    for a, b, c, black, flags in triangles:
        if black or flags & 0x9100:
            continue
        p = [R @ np.array(v) + T for v in (a, b, c)]
        if min(q[2] for q in p) < near:
            continue
        s = [(ofx + H * q[0] / q[2], ofy + H * q[1] / q[2]) for q in p]
        # front faces as the PS1 tests them (NCLIP > 0 on screen; y down)
        area = (s[1][0] - s[0][0]) * (s[2][1] - s[0][1]) - (s[2][0] - s[0][0]) * (s[1][1] - s[0][1])
        if area <= 0:
            continue
        draw.polygon([(x * sx, (y + top) * sy) for x, y in s], fill=255)
    m = np.asarray(mask) > 0
    # a few pixels in from every edge: rounding and the frame's own edge never count
    inner = m.copy()
    for dy, dx in ((-6, 0), (6, 0), (0, -6), (0, 6)):
        inner &= np.roll(m, (dy, dx), axis=(0, 1))
    return inner


def black_share(img, mask):
    if not mask.any():
        return 1.0
    dark = (img.max(axis=2) <= 6)
    return float(dark[mask].mean())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="logs, the plugin's copy, the pictures")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if os.environ.get("ROOD_RENDERER") == "null":
        print("SKIP: the photo mode's renders need a renderer")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "photo_full_room"
    shutil.rmtree(out / "plugins", ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    with card_guard(out / "cards", source=args.card) as cards:
        return run(out, cards.card1)


def run(out, card):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    plugins = out / "plugins"
    shutil.copytree(ROOT / "pc/tests/lua/plugins/test.photo", plugins / "test.photo")
    settings = out / "settings.cfg"
    settings.write_text("aspect 4:3\nui_menus 0\n")
    env = {"ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(plugins),
           "ROOD_PLUGIN_DATA": str(out / "plugin_data"), "ROOD_PHOTOS": str(out / "photos"), "ROOD_GI": "0",
           "ROOD_PAUSED_REDRAW_HZ": "60", "ROOD_DETERMINISTIC": "1"}
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        def value(code):
            r = g.repl("eval", session="photo-full-room", plugin="test.photo", code=code)
            if not r.get("ok"):
                raise rood_rpc.RpcError(f"{code}: {r.get('error')}")
            return r["results"][0] if r.get("results") else None

        def photo(**params):
            return g.call("photo", **params)

        def rendered(code, what):
            """Run `code` (it asks for a render) and wait for that render to be presented."""
            before = photo()["renders"]
            result = value(code) if isinstance(code, str) else code()
            deadline = Deadline(30)
            while photo()["renders"] <= before:
                if deadline.passed():
                    check(False, f"{what}: a render ({photo()})")
                    return result
                time.sleep(0.01)
            g.wait_presents(2)
            return result

        def shot(name):
            path = out / f"{name}.ppm"
            path.unlink(missing_ok=True)
            g.call("present_dump", path=str(path))
            if not rood_rpc.wait_for_file(path, 30):
                raise rood_rpc.RpcError("no frame presented")
            p = ppm(path)
            Image.fromarray(image(p)).save(out / f"{name}.png")
            return p

        def digest():
            h = g.call("snapshot", action="hash")
            return {k: h[k] for k in DIGEST}

        def groups():
            return g.call("room_groups")["groups"]

        def room_shows():
            """The groups a free camera's whole room has: not a black shell, not hidden by the room's own
            flag 0x100 (its event scripts), not the alternate set the room draws without (flag 0x8000 with
            func_8009723C's mode bit 0); the camera's hide list (0x1000) and facing cutaway count."""
            mode = int.from_bytes(g.read_symbol("D_800F19C8", 4), "little") & 1
            gs = groups()
            shown = [x for x in gs if not x["black"] and not x["flags"] & 0x100 and not (x["flags"] >> 15 & mode)]
            listed = [i for i, x in enumerate(gs) if x["flags"] & 0x1000]
            return len(shown), listed

        def whole_room(what):
            """The render's room list holds every group the room has (room_groups' drawn record is the
            game's, rewound to the held moment's; the renderer's count of the list's instances is the
            render's)."""
            expected, _ = room_shows()
            stats = g.call("render_stats")
            return check(stats["instances"] == expected,
                         f"{what}: every group the room has drawn ({stats['instances']} instances of {expected})")

        def warp(zone, room):
            g.call("room_goto", zone=zone, room=room)
            for _ in range(90):
                g.run(20)
                if g.call("room_goto", action="status")["phase"] in (0, 4):
                    break
            g.run(60)
            st = g.state()
            return (st["zone"], st["room"]) == (zone, room)

        for capability in CAPABILITIES:
            g.call("lua", action="grant", plugin="test.photo", capability=capability)
        g.run(4)

        # ---- normal play: the game's culling as it was ----
        play = groups()
        cut_in_play = [i for i, x in enumerate(play) if x["cutaway"] and not x["drawn"]]
        check(len(cut_in_play) > 0, f"in play the game's facing cutaway leaves groups out ({len(cut_in_play)})")
        check(photo()["full_room"] is True, "photo full_room on by default")
        play_stats = g.call("render_stats")

        # ---- held, the camera turned about Ashley ----
        check(value("rood.game.pause()") is True, "rood.game.pause()")
        held = digest()
        rendered("rood.ui.hide()", "the HUD and 2D hidden")
        reference = shot("reference")
        rendered("assert(rood.camera.begin()) return true", "camera.begin")
        rendered("rood.camera.orbit(0, -25) return rood.camera.orbit(180, 0)", "half a turn from above")
        turned = image(shot("turned"))
        dump = out / "turned.obj"
        dump.unlink(missing_ok=True)
        g.call("scene_dump", path=str(dump))
        camera, triangles = scene(dump)
        check(camera is not None and len(triangles) > 100, f"the room's mesh and the render's camera ({len(triangles)})")
        mask = coverage(camera, triangles, WINDOW)
        Image.fromarray((mask * 255).astype(np.uint8)).save(out / "turned-mesh.png")
        share = mask.mean()
        check(share > 0.3, f"the room's front faces cover the view ({share:.0%})")
        whole = black_share(turned, mask)
        whole_room(f"turned ({play_stats['instances']} in play)")
        check(whole <= 0.06, f"the picture is not black where the room's mesh projects ({whole:.1%} black)")

        # the editor's rules (the game's cutaway for the free camera's yaw): what the user saw
        rendered(lambda: photo(full_room=0), "photo full_room 0")
        editor = image(shot("turned-editor-rules"))
        editor_instances = g.call("render_stats")["instances"]
        old = black_share(editor, mask)
        check(old >= whole + 0.03,
              f"with the editor's rules the same view is black where walls belong ({old:.1%} > {whole:.1%}; "
              f"{editor_instances} instances)")
        rendered(lambda: photo(full_room=1), "photo full_room 1")
        again = image(shot("turned-again"))
        check(np.array_equal(again, turned), "full_room again: the same picture")

        # ---- the camera given back ----
        rendered("rood.camera.finish() return true", "camera.finish")
        finished = shot("finished")
        check(finished[2] == reference[2], "the game's camera again: byte for byte the render before begin")
        check(digest() == held, "renders leave the state digest as held")
        value("rood.ui.show() rood.game.resume() return true")
        g.run(8)
        after = groups()
        check([x["drawn"] for x in after] == [x["drawn"] for x in play],
              "resumed: the game's own culling again (the same groups drawn as before the session)")

        # ---- outdoors: the sky ----
        if check(warp(*OUTDOOR), f"warped to zone {OUTDOOR[0]} room {OUTDOOR[1]} (Rue Vermillion)"):
            check(value("rood.game.pause()") is True, "held outdoors")
            rendered("rood.ui.hide()", "hidden")
            rendered("assert(rood.camera.begin()) return true", "camera.begin")
            pose = value("return rood.camera.get()")
            t = pose["target"]
            rendered(f"return rood.camera.set {{ position = {{ {t['x']}, {t['y'] - 100}, {t['z']} }}, yaw = 0, "
                     f"pitch = 88 }}", "from her head, looking up")
            up = image(shot("sky")).astype(np.int32)
            r, gr, b = up[..., 0], up[..., 1], up[..., 2]
            sky = ((b > 120) & (b > r + 15) & (gr > r)).mean()
            check(sky >= 0.08, f"the sky drawn looking up ({sky:.0%} of the picture sky-blue)")
            rendered("rood.camera.finish() return true", "camera.finish")
            value("rood.ui.show() rood.game.resume() return true")
            g.run(4)
            check(not photo()["owners"], "everything given back")

        # ---- the game camera's hide list (room section 11): Snowfly Forest hides groups for it ----
        if check(warp(*LISTED), f"warped to zone {LISTED[0]} room {LISTED[1]} (Snowfly Forest)"):
            _, listed = room_shows()
            in_play = g.call("render_stats")["instances"]
            check(len(listed) > 0, f"the game's hide list leaves groups out for its camera ({listed})")
            check(value("rood.game.pause()") is True, "held")
            rendered("assert(rood.camera.begin()) return true", "camera.begin")
            whole_room(f"Snowfly Forest ({in_play} in play)")
            rendered("rood.camera.finish() return true", "camera.finish")
            value("rood.game.resume() return true")
            g.run(4)
            check(not photo()["owners"], "everything given back")
            check(room_shows()[1] == listed, "resumed: the hide list as it was")
    finally:
        g.close()
    log = (out / "game.log").read_text(errors="replace")
    check("[crash]" not in log, "no crash")
    print(f"photo_full_room_smoke: {'FAIL' if failures else 'PASS'} ({len(failures)} failure(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
