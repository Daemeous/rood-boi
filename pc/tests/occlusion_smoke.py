#!/usr/bin/env python3
"""Occlusion in play (settings `occlusion`, occlusion.h; Native_Rendering.md
"Occlusion"): what hides Ashley (and in battle the enemies near her) fades in
a soft eased ellipse around her; silhouettes show what still hides a subject.

    python3 pc/tests/occlusion_smoke.py [--card pc/build/test-card1.mcd] [--shots DIR]

Deterministic (ROOD_DETERMINISTIC, RPC-exact stepping), 960 x 720, no lights
or path tracing, on the renderer selected (ROOD_RENDERER: run it once for
OpenGL and once for Vulkan):

1. Field: zone 51 room 1 (the Undercity save point, the user's report: a
   dark beam crosses Ashley's legs), Ashley moved behind the beam
   (704, 0, 960), the camera turned to yaw 0x400. With occlusion off her
   legs are hidden; with `fade` (the default) the RPC says she is hidden
   (rays blocked) and her fade eases in over several game frames (values
   strictly between 0 and 1 on the way, not a jump); then most of the
   pixels around her knees are what the picture shows with the beam wholly
   gone (visibility 0), against few with occlusion off; the beam away from
   her is unchanged (the rest of the wall stays opaque); the dithered
   pixels stay put frame to frame (no shimmer); `silhouette` tints some of
   her hidden pixels and nothing outside her box.
2. Battle: zone 9 room 18 with its enemies, the weapon drawn: the enemy that
   comes near is a subject (enemy 1); the camera turned until a wall hides
   it (rays blocked, fade 1). Played twice from the same start (the game is
   deterministic): once drawn solid (visibility 1, no silhouettes), once
   with fade_silhouette; at the same vsync the enemy's ellipse differs
   between the two and the picture away from the subjects is the same.
3. Nothing in front stays solid: at Rue Vermillion's pillars (zone 32 room
   0, camera yaw 0: their narrow sides seen edge on and a step's lip stand
   in front of her) and at the beam, wholly faded with her silhouette in
   magenta, almost none of her body inside the ellipse stays hidden.
No crash, shader error or validation message. --shots saves pictures (game
imagery: keep them local)."""
import argparse
import os
import pathlib
import shutil
import struct
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import golden_frames as gf  # noqa: E402

POSITION = 28  # D_800F4538_t unk0.position (3 shorts, world units, y down)
YAW = 0x1F800058  # scratch: camera.angles.vy
SPOT = (704, 0, 960)  # behind the beam at zone 51 room 1's save point
failures = []


def check(ok, message):
    print(("ok   " if ok else "FAIL ") + message, flush=True)
    if not ok:
        failures.append(message)


def pointer_size():
    return 8 if os.environ.get("ROOD_ARCH") == "64" else 4


def goto(game, zone, room, enemies):
    game.call("room_goto", zone=zone, room=room, enemies=enemies, scripts=False)
    for _ in range(80):
        game.run(20)
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
    state = game.state()
    return (state["zone"], state["room"]) == (zone, room)


def yaw(game):
    return struct.unpack("<I", game.read(YAW, 4))[0] & 0xFFF


def turn_to(game, target):
    for _ in range(8):
        if abs(yaw(game) - target) < 8:
            return True
        game.call("input", buttons=["r1"], vsyncs=4)
        game.run(60)
    return abs(yaw(game) - target) < 8


def subjects(game):
    return game.call("occlusion")["subjects"]


def screen(game, point, shape):
    """A world point on the window (the game camera's projection; 320 x 240 scaled to the window)."""
    cam = game.call("scene")["camera"]
    v = np.array(cam["rotation"]) @ np.array(point, dtype=np.float64) + np.array(cam["translation"])
    x = cam["offset"][0] + cam["h"] * v[0] / v[2]
    y = cam["offset"][1] + cam["h"] * v[1] / v[2]
    return x * shape[1] / 320.0, y * shape[0] / 240.0, v[2], cam["h"] * shape[0] / 240.0


def box(shape, cx, cy, hw, hh):
    m = np.zeros(shape[:2], bool)
    m[max(0, int(cy - hh)):max(0, int(cy + hh)), max(0, int(cx - hw)):max(0, int(cx + hw))] = True
    return m


def close(a, b, tol=24):
    return np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2) <= tol


def launch(work, out, name):
    settings = work / f"settings-{name}.txt"
    settings.write_text("ui_menus 0\nwidescreen 0\ninterpolate 0\nqol_skip_charging 0\ncamera_stick 0\n"
                        "gameplay_walls original\naa off\n")
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(work / "lights"), "ROOD_GI": "0", "ROOD_SCALE": "3",
           "ROOD_SETTINGS": str(settings), "ROOD_DETERMINISTIC": "1"}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env["ROOD_VK_VALIDATE"] = "1"
    shutil.copyfile(work / "card.mcd", work / f"card-{name}.mcd")
    return launch_at("save_room", card=str(work / f"card-{name}.mcd"), fast=True, log=out / f"game-{name}.log",
                     extra_env=env)


def field(game, out, save):
    if not goto(game, 51, 1, False):
        check(False, "zone 51 room 1 through room_goto")
        return
    width = pointer_size()
    actor = int.from_bytes(game.read(symbols()["D_800F4538"], width), "little")
    game.write(actor + POSITION, struct.pack("<3h", *SPOT))
    game.run(60)
    check(turn_to(game, 0x400), f"the camera at yaw 0x400 ({yaw(game):#05x})")
    game.call("occlusion", occlusion="off")
    game.run(30)
    off = gf.capture(game, out / "f")
    save("field-off", off)
    knees = screen(game, (SPOT[0], SPOT[1] - 30, SPOT[2]), off.shape)
    cx, cy, depth, k = knees
    near = box(off.shape, cx, cy, 30 * k / depth, 30 * k / depth)  # her knees and shins, behind the beam
    beam = near & (off.max(axis=2) < 16)  # there, the beam's (black) pixels
    # The beam inside the ellipse but beside the save point's light (which animates): the dither's own pixels.
    rx = 96 * k / depth
    side = box(off.shape, cx, cy, 0.9 * rx, 30 * k / depth) & ~box(off.shape, cx, cy, 0.55 * rx, 30 * k / depth)
    side &= off.max(axis=2) < 16

    def revealed(image):
        """The share of the beam's pixels around her knees showing something else (what it hides)."""
        return float((~close(image, off, 8))[beam].mean())
    far = box(off.shape, 40, cy, 30, 20)  # the beam, far left of her
    # The beam wholly gone around her (visibility 0): what it hides.
    game.call("occlusion", occlusion="fade", occlusion_visibility=0.0)
    game.run(30)
    s = subjects(game)
    check(bool(s) and s[0]["slot"] == 0 and s[0]["rays_blocked"] >= 1 and s[0]["fade"] > 0.99,
          f"Ashley behind the beam is hidden (rays blocked {s[0]['rays_blocked'] if s else None}), fade 1")
    gone = gf.capture(game, out / "f")
    save("field-visibility0", gone)
    check(beam.mean() > 0 and revealed(gone) > 0.6,
          f"the beam hides her knees with occlusion off ({revealed(gone) * 100:.0f}% of it gone at visibility 0)")
    # Off, then fade: the fade eases in.
    game.call("occlusion", occlusion="off", occlusion_visibility=0.35)
    game.run(30)
    game.call("occlusion", occlusion="fade")
    fades, shares = [], []
    for _ in range(16):
        game.run(2)
        fades.append(subjects(game)[0]["fade"])
        image = gf.capture(game, out / "f")
        shares.append(revealed(image))
    between = [f for f in fades if 0.03 < f < 0.97]
    check(len(between) >= 2 and fades[-1] > 0.99 and all(b >= a - 1e-6 for a, b in zip(fades, fades[1:])),
          "the fade eases in over game frames: " + ", ".join(f"{f:.2f}" for f in fades))
    check(shares[0] < shares[-1] - 0.15 and any(shares[0] + 0.05 < x < shares[-1] - 0.05 for x in shares),
          "her pixels come through gradually: " + ", ".join(f"{x:.2f}" for x in shares))
    game.run(30)
    faded = gf.capture(game, out / "f")
    save("field-fade", faded)
    seen = revealed(faded)
    check(0.4 <= seen <= revealed(gone) - 0.1,
          f"fade: {seen * 100:.0f}% of the beam around her knees shows what it hides (visibility 0: "
          f"{revealed(gone) * 100:.0f}%; some of it kept)")
    d = np.abs(faded.astype(np.int16) - off.astype(np.int16)).max(axis=2)
    check(float(d[far].mean()) < 1.0, f"the beam away from her stays opaque (mean difference {d[far].mean():.2f})")
    # No shimmer: beside the light, the beam's pixels kept stay the same ones while nothing moves.
    kept = []
    for _ in range(8):
        game.run(2)
        kept.append(close(gf.capture(game, out / "f"), off, 2)[side])
    flips = max(float((kept[0] ^ k).mean()) for k in kept[1:])
    check(side.sum() > 100 and 0.05 < kept[0].mean() < 0.95 and flips < 0.01,
          f"the dither stays put frame to frame ({kept[0].mean() * 100:.0f}% of {side.sum()} beam pixels kept; "
          f"at most {flips * 100:.2f}% change)")
    # Silhouettes alone: some of her hidden pixels tinted, nothing far from her.
    game.call("occlusion", occlusion="silhouette")
    game.run(30)
    s = subjects(game)
    sil = gf.capture(game, out / "f")
    save("field-silhouette", sil)
    changed = float((~close(sil, off, 8))[near].mean())
    d = np.abs(sil.astype(np.int16) - off.astype(np.int16)).max(axis=2)
    check(s and s[0]["hidden"] > 0.99 and 0.03 < changed < 0.7 and float(d[far].mean()) < 1.0,
          f"silhouette: {changed * 100:.0f}% of the box tinted (hidden {s[0]['hidden'] if s else None}), "
          f"the beam elsewhere unchanged")
    game.call("occlusion", occlusion="fade")


def nothing_in_front(game, out, save, name, zone, room, presses, spot=None, target=None, limit=0.003):
    """Faded wholly (visibility 0, a crisp edge) with her silhouette in pure magenta at full strength: every
    pixel of Ashley still hidden by something in front of her shows magenta. Inside most of her ellipse none
    may (a sole the floor at her feet hides aside): thin pieces seen edge on, a step's lip, a post's cap or a
    blade fade like the walls. Before they did (rooms' plane test only), 1.5% of it stayed hidden at the
    pillars; now under 0.1%."""
    if not goto(game, zone, room, False):
        check(False, f"zone {zone} room {room} through room_goto")
        return
    if spot:
        width = pointer_size()
        actor = int.from_bytes(game.read(symbols()["D_800F4538"], width), "little")
        game.write(actor + POSITION, struct.pack("<3h", *spot))
        game.run(60)
    for _ in range(presses):
        game.call("input", buttons=["r1"], vsyncs=4)
    game.run(80)
    if target is not None:
        turn_to(game, target)
    game.call("occlusion", occlusion="fade_silhouette", occlusion_visibility=0.0, occlusion_softness=0.05,
              occlusion_silhouette_ashley=1.0, occlusion_silhouette_rim=0.0, occlusion_tint_ashley=[1, 0, 1])
    game.run(30)
    image = gf.capture(game, out / "n")
    save(f"{name}-magenta", image)
    s = subjects(game)
    game.call("occlusion", occlusion="fade", occlusion_visibility=0.35, occlusion_softness=0.45,
              occlusion_silhouette_ashley=0.3, occlusion_silhouette_rim=0.6, occlusion_tint_ashley=[0.42, 0.5, 0.6])
    if not s or s[0]["slot"] != 0 or s[0]["rays_blocked"] < 1 or s[0]["fade"] < 0.99:
        check(False, f"{name}: Ashley is hidden and faded ({s[:1]})")
        return
    x, y, depth, k = screen(game, s[0]["center"], image.shape)
    hw, hh = s[0]["half_w"] * k / depth, s[0]["half_h"] * k / depth
    ys, xs = np.mgrid[0:image.shape[0], 0:image.shape[1]]
    region = ((xs - x) / hw) ** 2 + ((ys - y) / hh) ** 2 < 0.6 ** 2  # her body, down to her feet
    img = image.astype(np.int32)
    magenta = (img[..., 0] > 120) & (img[..., 2] > 120) & (img[..., 1] < 0.5 * np.minimum(img[..., 0], img[..., 2]))
    share = float(magenta[region].mean())
    check(share < limit, f"{name}: nothing in front of Ashley stays solid inside her ellipse ({share * 100:.2f}% of "
          f"{int(region.sum())} pixels still hide her; {s[0]['rays_blocked']} rays blocked)")


def battle(game, out, save, stop=None):
    """Zone 9 room 18, the weapon drawn, the camera turned until a wall hides the enemy that comes. Returns
    the vsync steps taken to that moment, the subjects and the picture."""
    if not goto(game, 9, 18, True):
        check(False, "zone 9 room 18 through room_goto")
        return None
    game.press("circle", 3, 30)  # the weapon drawn
    game.run(560)  # the nearer Zombie comes
    steps = 0
    for turn in range(8):
        game.call("input", buttons=["r1"], vsyncs=4)
        for _ in range(40):
            game.run(2)
            steps += 1
            s = subjects(game)
            enemy = [x for x in s if x["enemy"]]
            if stop is not None and steps == stop:
                return steps, s, gf.capture(game, out / "b")
            if stop is None and enemy and enemy[0]["rays_blocked"] >= 2 and enemy[0]["fade"] > 0.99:
                return steps, s, gf.capture(game, out / "b")
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD", str(ROOT / "pc/build/test-card1.mcd")))
    parser.add_argument("--shots", help="save pictures here (game imagery: keep local)")
    args = parser.parse_args()
    card = pathlib.Path(args.card)
    if not card.exists():
        print("SKIP: no pinned test card", card)
        return 77
    out = OUT / "occlusion"
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)

    def save(name, image):
        if shots:
            Image.fromarray(image).save(shots / f"{name}.png")

    work = pathlib.Path(tempfile.mkdtemp(prefix="occlusion-", dir=out))
    (work / "lights").mkdir()
    shutil.copyfile(card, work / "card.mcd")
    logs = []
    game = launch(work, out, "field")
    logs.append(game.log)
    try:
        r = game.call("occlusion")
        check(r["occlusion"] == "fade" and abs(r["occlusion_visibility"] - 0.35) < 1e-6,
              f"defaults: occlusion {r['occlusion']}, visibility {r['occlusion_visibility']}")
        # Battle first (the second run repeats it from the same start), drawn solid: the moment a wall hides
        # the enemy.
        game.call("occlusion", occlusion="fade_silhouette", occlusion_visibility=1.0, occlusion_silhouette_enemy=0.0,
                  occlusion_silhouette_ashley=0.0)
        found = battle(game, out, save)
    finally:
        game.close()
    game = launch(work, out, "field2")
    logs.append(game.log)
    try:
        field(game, out, save)
        # Thin pieces in front: Rue Vermillion (zone 32 room 0) where the warp puts her, the camera at
        # yaw 0: a pillar's narrow sides and the lip of a step stand between the camera and her.
        nothing_in_front(game, out, save, "pillars", 32, 0, 0, target=0x000)
        # The beam at the Undercity save point (zone 51 room 1).
        nothing_in_front(game, out, save, "beam", 51, 1, 0, spot=SPOT, target=0x400)
    finally:
        game.close()
    if not found:
        check(False, "battle: an enemy near Ashley is a subject and a wall hides it while the camera turns")
    else:
        steps, s, solid = found
        enemy = [x for x in s if x["enemy"]][0]
        check(enemy["slot"] > 0 and enemy["rays_blocked"] >= 2,
              f"battle: enemy slot {enemy['slot']} is a subject, hidden ({enemy['rays_blocked']} rays blocked), "
              f"fade {enemy['fade']:.2f} after {steps} steps")
        save("battle-solid", solid)
        game = launch(work, out, "battle")
        logs.append(game.log)
        try:
            game.call("occlusion", occlusion="fade_silhouette")
            again = battle(game, out, save, stop=steps)
            if again:
                _, s2, faded = again
                save("battle-fade", faded)
                e2 = [x for x in s2 if x["enemy"]]
                cx, cy, depth, k = screen(game, enemy["center"], faded.shape)
                region = box(faded.shape, cx, cy, enemy["half_w"] * k / depth, enemy["half_h"] * k / depth)
                around = region.copy()
                for x in s2:
                    ax, ay, ad, _ = screen(game, x["center"], faded.shape)
                    around |= box(faded.shape, ax, ay, 1.3 * x["half_w"] * k / ad, 1.3 * x["half_h"] * k / ad)
                d = np.abs(faded.astype(np.int16) - solid.astype(np.int16)).max(axis=2)
                same_state = bool(e2) and [round(c) for c in e2[0]["center"]] == [round(c) for c in enemy["center"]]
                check(same_state, f"battle again: the same moment (enemy at {e2[0]['center'] if e2 else None})")
                check(float((d[region] > 24).mean()) > 0.03,
                      f"battle: the enemy's ellipse is faded or silhouetted ({(d[region] > 24).mean() * 100:.1f}% "
                      "of it differs from the solid picture)")
                check(float(d[~around].mean()) < 1.0,
                      f"battle: the picture away from the subjects is the same (mean difference {d[~around].mean():.2f})")
            else:
                check(False, "battle again: reached the same moment")
        finally:
            game.close()
    bad = []
    for log in logs:
        text = pathlib.Path(log).read_text(errors="replace")
        bad += [line for line in text.splitlines() if "validation:" in line or "[crash]" in line
                or ("shader" in line and "error" in line.lower())]
    check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))
    shutil.rmtree(work, ignore_errors=True)
    if failures:
        print(f"FAIL: {len(failures)} checks failed")
        return 1
    print("PASS: occluders fade (eased, stable, only around the subjects), silhouettes tint the hidden, "
          "battle enemies count")
    return 0


if __name__ == "__main__":
    sys.exit(main())
