#!/usr/bin/env python3
"""Modern menus, Ashley tab: the model's picture is stable from image to
image and drawn at the window's resolution (pc/src/ui/ashley_bridge.c,
pc/src/gpu_hw.c shown_recording / pc_hw_detail_view).

    python3 pc/tests/ui_ashley_stability_smoke.py [--card pc/save/card1.mcd] [--out DIR]

From a copy of the card (skipped without one), in the save room, with the
lighting that lights the world in world space (settings: dynamic lights,
path-traced GI, traced shadows, haze high; ROOD_GI=1), ui_menus on, 4:3 in a
960x720 window:
  - lights by Ashley and over the world's origin; the presented picture's camera (scene RPC: what the
    lighting, haze and path tracing reconstruct world positions through)
    puts Ashley's place in the room inside the model column: the model is
    drawn where he stands, lit by what lights him there, through the camera
    that drew it (before: the clone at the world's origin, the picture's
    camera the world's on live images)
  - present_dump bursts of the model column, the model still (its idle
    animation only) and turning slowly (RPC right stick 0.35): every image
    shows the model (its pixels off the ground colour at least 60% of the
    burst's median, the torso sample not the ground), frame-to-frame change
    below 6% of the column, and no flicker: the share of pixels that change
    one way and straight back over three images (more than 12 levels each
    way) averages below 0.8%. Thin moving parts (the sword) do that by
    themselves now and then; a picture lit or hazed differently on
    alternate images does it everywhere. Before the fix, images drawn live
    were lit through the world's camera and interpolated ones through the
    menu's: about 2% on every image, still or turning.
  - the stick turned it (yaw changed).
  - the detail view: with the internal resolution set to 1x, the Ashley tab
    raises it to the window's (720 lines over the display's 240 or 224:
    3x or 4x) while the model shows;
    R1 (Equipment, the model freed) gives the setting back (1x).
  - the post stack never touches the model, the picture behind it keeps its
    post (gpu_hw.c pc_hw_present_menu_model, post_profile.cpp, the
    presenters' menu backdrop): ui_menu_opacity 0.3 (settings load), a
    strong gameplay profile (exposure, ACES, full grain and vignette,
    bloom, chromatic aberration, no saturation) on before the menu opens;
    with the model shown, the model column of the paused picture is the
    same with that profile and with none (before: graded, about every
    model pixel changed); the model column's empty left strip (the gap
    between list and card on Equipment) shows the world behind as the Equipment tab shows it with the strong profile,
    much closer to that than to Equipment without it (before: the opaque
    ground colour).
--out keeps the crops (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import statistics
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402
from ui_ashley_smoke import GROUND, Picture, near, samples  # noqa: E402

SETTINGS = "dynamic_lights 1\ngi_method path\nshadows traced\nhaze high\n"
FLICK_MEAN = 0.008   # mean share of pixels flicking (one way and back over three images)
CHANGE_MAX = 0.06    # share of the column changed more than 24 levels between two images
PRESENT = 0.6        # each image's model pixels against the burst's median


def model_pixels(crop):
    px = crop.load()
    w, h = crop.size
    return sum(1 for y in range(0, h, 2) for x in range(0, w, 2) if not near(px[x, y], GROUND, 12))


def changed(a, b):
    pa, pb = a.load(), b.load()
    w, h = a.size
    n = sum(1 for y in range(0, h, 2) for x in range(0, w, 2)
            if max(abs(p - q) for p, q in zip(pa[x, y], pb[x, y])) > 24)
    return n / ((w + 1) // 2 * ((h + 1) // 2))


def flicks(a, b, c):
    """Share of pixels whose luminance goes one way (more than 12) and straight back."""
    pa, pb, pc = a.convert("L").load(), b.convert("L").load(), c.convert("L").load()
    w, h = a.size
    n = total = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            d1, d2 = pb[x, y] - pa[x, y], pc[x, y] - pb[x, y]
            total += 1
            if abs(d1) > 12 and abs(d2) > 12 and (d1 > 0) != (d2 > 0):
                n += 1
    return n / total


STRONG_POST = {"exposure": 1, "tonemapper": "aces", "grain": 1, "vignette": 1, "bloom_intensity": 2,
               "chromatic_aberration": 5, "saturation": [1, 1, 1, 0]}
OPACITY = 0.3


def presented(g, path):
    """The paused game's next presented image (redrawn with the current settings) as an int array."""
    import numpy as np
    from PIL import Image
    g.wait_presents(3)
    ppm = pathlib.Path(path).with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    g.call("present_dump", path=str(ppm.resolve()))
    for _ in range(200):
        if ppm.exists():
            break
        g.wait_presents(1)
    image = np.asarray(Image.open(ppm).convert("RGB")).astype(np.int16)
    ppm.unlink()
    return image


def post_model(g, out, settings, rect, ui, wait, close, check, keep):
    """The model is drawn without the post stack; the world behind it keeps it (see the docstring)."""
    import numpy as np
    from PIL import Image
    settings.write_text(SETTINGS + f"ui_menu_opacity {OPACITY}\n")
    g.call("settings", action="load")
    g.call("ui_menu", modern=True)
    g.call("post", layer="gameplay", clear="all", set=STRONG_POST, snap=True)
    g.run(20)

    def tab(name):
        for _ in range(8):
            if ui().get("tab") == name:
                return
            g.press("l1", vsyncs=3, settle=6)

    g.press("triangle", vsyncs=3, settle=2)
    wait(lambda: ui().get("visible"), 100, "the menu")
    tab("equipment")
    g.run(20)
    other_strong = presented(g, out / "post-equipment-strong")
    g.call("post", layer="gameplay", clear="all", snap=True)
    other_neutral = presented(g, out / "post-equipment-neutral")
    g.call("post", layer="gameplay", clear="all", set=STRONG_POST, snap=True)
    g.run(4)
    tab("ashley")
    wait(lambda: (ui().get("ashley") or {}).get("model") == 2, 300, "the Ashley model")
    g.run(8)
    model_strong = presented(g, out / "post-model-strong")
    info = g.call("post")
    g.call("post", layer="gameplay", clear="all", snap=True)
    model_neutral = presented(g, out / "post-model-neutral")
    if keep:
        for name, image in (("equipment-strong", other_strong), ("equipment-neutral", other_neutral),
                            ("model-strong", model_strong), ("model-neutral", model_neutral)):
            Image.fromarray(image.astype(np.uint8)).save(out / f"post-{name}.png")
    x, y, w, h = (int(v) for v in rect)  # 960x720 window: layout units are pixels
    col_s, col_n = model_strong[y:y + h, x:x + w], model_neutral[y:y + h, x:x + w]
    d = np.abs(col_s - col_n).max(axis=2)
    check(info.get("menu_model") is True and d.mean() < 0.5 and np.percentile(d, 99.9) <= 4,
          f"post stack off the model: the model column with the strong profile = without (mean difference "
          f"{d.mean():.2f}, 99.9th percentile {np.percentile(d, 99.9):.0f}, {np.mean(d > 8):.2%} of it more than 8; "
          f"post RPC menu_model {info.get('menu_model')})")
    # The model column's left strip (x + 4 .. x + 48, its upper 95%: no model; on Equipment the gap between
    # the list and the card, 4:3): the Ashley tab shows the world behind as the Equipment tab does, with the
    # strong profile's post on it.
    box = (slice(y + 8, y + int(h * 0.95)), slice(x + 4, x + 48))
    m = model_strong[box].astype(np.float32)
    es = float(np.abs(m - other_strong[box]).mean())
    en = float(np.abs(m - other_neutral[box]).mean())
    apart = float(np.abs(other_strong[box] - other_neutral[box]).mean())
    check(es < 12 and es < 0.5 * en and apart > 8,  # (film grain differs from game frame to game frame)
          f"the world behind the model keeps its post, as on the Equipment tab (opacity {OPACITY}): the column's "
          f"left strip against Equipment with the strong profile {es:.1f}, without {en:.1f} (those two apart "
          f"{apart:.1f})")
    close()
    g.call("post", layer="gameplay", clear="all", snap=True)
    settings.write_text(SETTINGS)
    g.call("settings", action="load")
    g.call("ui_menu", modern=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the crops here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_ashley_stability"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    settings = out / "settings.cfg"
    settings.write_text(SETTINGS)
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "1", "ROOD_SETTINGS": str(settings)}
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        state = g.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1

        def ui():
            return g.call("ui_menu")

        def wait(pred, limit, what):
            for _ in range(limit):
                if pred():
                    return
                g.run(1)
            raise TimeoutError(f"{what}: {ui()}")

        def open_tab():
            g.press("triangle", vsyncs=3, settle=2)
            wait(lambda: ui().get("screen") == "ashley" and (ui().get("ashley") or {}).get("model") == 2, 300,
                 "the Ashley model")
            g.run(8)

        def close():
            g.press("triangle", vsyncs=3, settle=4)
            wait(lambda: not ui().get("visible"), 400, "the menu closes")
            g.run(30)

        def burst(name, count, during=None):
            base = out / name
            paths = [pathlib.Path(f"{base}.{i:04d}.ppm") for i in range(count)]
            for q in paths:
                q.unlink(missing_ok=True)
            g.call("present_dump", path=str(base.resolve()), count=count)
            if during:
                during()
            g.run(count + 6)
            for _ in range(60):
                if all(q.exists() for q in paths):
                    break
                g.run(1)
            rect = ui()["ashley"]["model_rect"]
            crops, torsos = [], []
            for i, q in enumerate(paths):
                if not q.exists():
                    crops.append(None)
                    continue
                p = Picture(q, False)
                crops.append(p.box(rect))
                torsos.append(not near(p.at(*samples(rect)["torso"]), GROUND, 12))
                if args.out:
                    crops[-1].save(out / f"{name}-{i:02d}.png")
                q.unlink()
            return crops, torsos

        def judge(name, crops, torsos):
            missing = sum(c is None for c in crops)
            crops = [c for c in crops if c is not None]
            counts = [model_pixels(c) for c in crops]
            median = statistics.median(counts)
            blank = [i for i, n in enumerate(counts) if n < PRESENT * median]
            check(not missing and median > 1000 and not blank and all(torsos),
                  f"{name}: the model in every image ({len(crops)} images, model pixels {min(counts)}..{max(counts)}, "
                  f"median {median}; below {PRESENT:.0%}: {blank}; torso off the ground in "
                  f"{sum(torsos)}/{len(torsos)}; missing images {missing})")
            change = [changed(a, b) for a, b in zip(crops, crops[1:])]
            check(max(change) < CHANGE_MAX,
                  f"{name}: frame-to-frame change below {CHANGE_MAX:.0%} of the column (max {max(change):.4f})")
            f = [flicks(a, b, c) for a, b, c in zip(crops, crops[1:], crops[2:])]
            mean = sum(f) / len(f)
            check(mean < FLICK_MEAN,
                  f"{name}: no flicker: mean {mean:.4f} (< {FLICK_MEAN}), max {max(f):.4f} of the column's pixels "
                  f"flick one way and back ({' '.join(f'{x:.3f}' for x in f)})")

        # Lights (zone 9 room 17): one by Ashley, one over the world's origin (where MENU4's clone stands,
        # and the modern menu's did): a picture lit through the wrong camera or at the wrong place shows
        player = state["player"]
        (lights / "zone09_room17.txt").write_text(
            f"light point {player['x'] + 120} {player['y'] - 300} {player['z'] + 120} 1 0.8 0.5 2 1800 0 1 0 45 1 1\n"
            "light point 0 -300 0 0.5 0.7 1 3 900 0 1 0 45 1 1\n")
        g.reload_lights()
        g.run(10)
        g.call("ui_menu", modern=True)
        open_tab()
        # The presented picture's camera (what lighting, haze and path tracing reconstruct world positions
        # through) is the one that drew the model, and the model stands at Ashley's place: that place
        # projects into the model column.
        cam = g.call("scene")["camera"]
        r, t, h, (ox, oy) = cam["rotation"], cam["translation"], cam["h"], cam["offset"]
        p = (player["x"], player["y"], player["z"])
        v = [sum(r[i][k] * p[k] for k in range(3)) + t[i] for i in range(3)]
        rect = ui()["ashley"]["model_rect"]
        lx, ly = (480 + 3 * (ox + h * v[0] / v[2] - 160), 3 * (oy + h * v[1] / v[2])) if v[2] > 1 else (-1, -1)
        check(rect[0] < lx < rect[0] + rect[2] and rect[1] < ly < rect[1] + rect[3],
              f"lit where it stands: Ashley's place {p} through the presented picture's camera lands at layout "
              f"({lx:.0f}, {ly:.0f}), in the model column {rect}")
        judge("still", *burst("still", 24))
        yaw0 = ui()["ashley"]["model_yaw"]
        judge("turning", *burst("turning", 40, lambda: g.call("input", right_stick=[0.35, 0], vsyncs=60)))
        g.run(30)
        yaw1 = ui()["ashley"]["model_yaw"]
        check(yaw1 != yaw0, f"the stick turned the model (yaw {yaw0} -> {yaw1})")
        close()

        # the detail view: 1x set, 720 window lines over the display's (240, 224: 3x, 4x) while the model
        # shows, 1x back on Equipment
        g.call("graphics", scale=1)
        g.run(4)
        open_tab()
        shown = g.call("graphics")
        g.press("r1", vsyncs=3, settle=2)
        wait(lambda: ui().get("tab") == "equipment", 60, "Equipment")
        g.run(4)
        freed = g.call("graphics")
        check(shown["scale"] in (3, 4) and shown["scale_setting"] == 1 and freed["scale"] == 1,
              f"detail view: setting {shown['scale_setting']}x, {shown['scale']}x while the model shows (want 3x or 4x), "
              f"{freed['scale']}x on Equipment (want 1x)")
        close()
        g.call("graphics", scale=4)
        g.run(10)
        post_model(g, out, settings, rect, ui, wait, close, check, args.out)
    finally:
        g.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
