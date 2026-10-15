#!/usr/bin/env python3
"""A moving platform's shadow darkens the floor and water under it as on the
PS1: translucent (subtractive), not a black square.

    python3 pc/tests/platform_shadow_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Rooms with moving platforms (MPD section 12: lifts, the floating blocks
with a magic circle on top in Rue Vermillion) draw a flat square under each
(func_8008CEB0, pc/src/room_shadows.inc): POLY_F4, semi-transparent, after
a B-F draw mode packet, so it subtracts its grey from what is under it.
With the reconstruction of 3D from packets retired (every native switch
on, the default) that packet reached the hardware path as 2D: subtractive
"screen furniture" that replaced the surface under it (a hard, fully black
square on the water). It is now drawn natively (effect_native.c
pc_platform_shadow_native: an effects list, as drop shadows are).

Continues from a copy of the card into zone 32 room 3 (Rue Vermillion, the
user's report: Room Browser warp, no scripts), shows the first platform
(its section 12 entry hidden until a script shows it), stops the
platforms' motion (D_800F1D6E) and looks down at its shadow on the water
with the free camera. Four frames: the default (native) and the reference
packet path (native_shadows 0, which brings the reconstruction back), each
with the shadow and without it (the platform's altitude put above the
floor's reach, so func_8008CEB0 skips it; the platform's room group stays
where it is). The square is found from where the native draw put it
(pc_platform_shadow_records), projected with the game camera and fitted to
the reference frame. Its darkening is the square's light against the water
around it, with the shadow over without it (the water animates between
frames; its surroundings even that out). Checks:

- the square is drawn natively this frame,
- both paths darken (< 0.9; the reference about 0.8),
- the native one is not black (>= 0.25, under a quarter of its pixels
  black; before the fix: 0.02, 98% black),
- it darkens like the reference (within 0.15).

--out keeps the frames."""
import argparse
import os
import pathlib
import shutil
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_cutaway_smoke import default_card  # noqa: E402

ZONE, ROOM = 32, 3
SECTION12 = 0xA8  # vs_battle_roomData.section12 (psx_ptr), its length at 0x48
SECTION12_LEN = 0x48
ENTRY = 184  # sizeof(_mpdRoomSection12)
SETTLE = 40  # vsyncs after a change (lighting filters)


RECORD = 20  # struct pc_platform_shadow_record: serial, shade, centre[3]


def corners(camera, centre, half):
    """The square's corners on screen (GTE pixels: 320 x 240)."""
    r, t = np.array(camera["rotation"]), np.array(camera["translation"])
    ofx, ofy = camera["offset"]
    points = []
    for dx, dz in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        v = r @ np.array([centre[0] + dx * half, centre[1], centre[2] + dz * half]) + t
        points.append((ofx + camera["h"] * v[0] / v[2], ofy + camera["h"] * v[1] / v[2]))
    return points


def regions(square, w, h):
    """The square's middle (shrunk by a quarter about its centre: its edges
    left out) and a ring of floor around it, as masks of a w x h frame."""
    sx, sy = w / 320, h / 240
    cx = sum(p[0] for p in square) / 4
    cy = sum(p[1] for p in square) / 4

    def mask(scale):
        image = Image.new("L", (w, h), 0)
        ImageDraw.Draw(image).polygon([((cx + (p[0] - cx) * scale) * sx, (cy + (p[1] - cy) * scale) * sy)
                                       for p in square], fill=1)
        return np.asarray(image, dtype=bool)
    inside = mask(0.75)
    ring = mask(2.2) & ~mask(1.4)
    ring[: int(h * 0.15), :] = False  # (the HUD)
    return inside, ring


def luminance(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    return 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the frames and logs here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "platform_shadow"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    # The room's lights from the committed packs (a private user layer): the
    # lit picture the bug showed in. (Unlit, the raw PS1 picture, this water
    # is darker, about (8, 12, 16), than the square's grey (32): B-F leaves
    # it black there on both paths, as on the PS1.)
    user = out / "lights"
    shutil.rmtree(user, ignore_errors=True)
    user.mkdir()
    env = {"ROOD_SCALE": "2", "ROOD_WINDOW": "1280x960", "ROOD_LIGHTS": str(user),
           "ROOD_LIGHTING": str(ROOT / "lighting")}
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-platform-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        game.call("room_goto", zone=ZONE, room=ROOM, enemies=False, scripts=False)
        for _ in range(60):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(30)
        state = game.state()
        if (state["zone"], state["room"]) != (ZONE, ROOM):
            check(False, f"warped to zone {ZONE} room {ROOM} ({state['zone']}/{state['room']})")
            return 1
        data = game.read_symbol("vs_battle_roomData", 0xB0)
        table = struct.unpack("<I", data[SECTION12:SECTION12 + 4])[0]
        count = struct.unpack("<I", data[SECTION12_LEN:SECTION12_LEN + 4])[0] // ENTRY
        check(table != 0 and count >= 1, f"the room has moving platforms ({count})")
        if not table or not count:
            return 1
        word = struct.unpack("<I", game.read(table, 4))[0]
        game.write(table, struct.pack("<I", word & ~(1 << 9)))  # shown (unk0_9: hidden)
        game.run(2)
        game.write(symbols()["D_800F1D6E"], b"\x01")  # the platforms stop (func_8008C8A8 returns)
        x, altitude, z = (v / 4096 for v in struct.unpack("<iii", game.read(table + 0xA8, 12)))
        print(f"platform at x {x:.0f} z {z:.0f}, {altitude:.0f} above y 0")
        game.call("editor_camera", x=x, y=-1300, z=z - 700, yaw=0, pitch=-60)
        game.run(SETTLE)

        frames, square = {}, None
        for shadow in (True, False):
            # The platform's altitude: its own, or out of the floor's reach.
            game.write(table + 0xAC, struct.pack("<i", int(altitude * 4096) if shadow else -0x7000000))
            for name, native in (("native", True), ("reference", False)):
                game.call("graphics", native_shadows=native)
                game.run(SETTLE)
                tag = f"{name}-{'shadow' if shadow else 'none'}"
                path = game.present_screenshot(out / f"{tag}.png")
                frames[tag] = luminance(path)
                stats = game.call("render_stats")
                if shadow:
                    print(f"{tag}: shadow_markers {stats['shadow_markers']}, "
                          f"legacy_other_triangles {stats['legacy_other_triangles']}")
                if shadow and native:
                    # Where it is (effect_native.c): its centre and shade, and the camera.
                    record = game.read(symbols()["pc_platform_shadow_records"], RECORD)
                    serial, shade = struct.unpack("<Ii", record[:8])
                    check(serial and stats["frame_serial"] - serial <= 2 and stats["shadow_markers"] >= 1,
                          f"the platform's shadow is drawn natively (frame {serial}, now {stats['frame_serial']})")
                    centre = struct.unpack("<3f", record[8:20])
                    square = corners(game.call("scene")["camera"], centre, 32 + shade // 8)
                    print(f"square at {[round(c) for c in centre]}, shade {shade}: screen {square}")
        game.call("graphics", native_shadows=True)
    finally:
        game.close()

    ref, ref0 = frames["reference-shadow"], frames["reference-none"]
    nat, nat0 = frames["native-shadow"], frames["native-none"]
    h, w = ref.shape
    # The square where the reference draws it: the projection placed within
    # a few pixels (the display's vertical offset), the darkest fit wins.
    best = None
    for dy in range(-16, 17):
        for dx in range(-4, 5):
            moved = [(p[0] + dx, p[1] + dy) for p in square]
            inside, ring = regions(moved, w, h)
            if inside.sum() < 200 or ring.sum() < 200:
                continue
            fit = (ref[inside].mean() / ref[ring].mean()) / (ref0[inside].mean() / ref0[ring].mean())
            if best is None or fit < best[0]:
                best = (fit, dx, dy)
    if best:
        print(f"the square in the reference: {best[1]:+d}, {best[2]:+d} pixels from its projection")
        square = [(p[0] + best[1], p[1] + best[2]) for p in square]
    inside, ring = regions(square, w, h)
    check(inside.sum() >= 200 and ring.sum() >= 200, f"the square is on screen ({int(inside.sum())} pixels)")
    if inside.sum() >= 200 and ring.sum() >= 200:
        def darkening(a, a0):
            """The square's light against the water around it, with the shadow over without it."""
            return float((a[inside].mean() / a[ring].mean()) / (a0[inside].mean() / a0[ring].mean()))
        r_ref, r_nat = darkening(ref, ref0), darkening(nat, nat0)
        dark = float((nat[inside] < 6).mean())
        print(f"under the shadow: reference {r_ref:.2f}, native {r_nat:.2f} of the light without it; "
              f"black pixels {dark:.0%}")
        check(r_ref < 0.9, f"the reference darkens ({r_ref:.2f} < 0.9)")
        check(r_nat < 0.9, f"the native shadow darkens ({r_nat:.2f} < 0.9)")
        check(r_nat >= 0.25 and dark < 0.25, f"the native shadow is not black ({r_nat:.2f} >= 0.25, {dark:.0%} black)")
        check(abs(r_nat - r_ref) <= 0.15, f"it darkens like the packet path ({r_nat:.2f} vs {r_ref:.2f})")
    if not args.out and not failures:
        shutil.rmtree(out, ignore_errors=True)
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
