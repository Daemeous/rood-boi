#!/usr/bin/env python3
"""The physical camera's depth of field, its bokeh and the first-person
view's own depth of field (pc/docs/Post_Processing.md, post.h, dof.glsl).

    python3 pc/tests/dof_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

Frame-based (RPC-exact stepping, paused redraws), 960 x 720, internal
resolution 4x, no lights, no path tracing, on the renderer selected
(ROOD_RENDERER):

1. The title (checkpoint `title`): off by default (no depth of field, no
   first-person depth of field, every bokeh field neutral); turned on (both,
   f/1.2, the largest blur, blades, fringe, cat's eye) the title and the
   attract demo (1800 vsyncs idle) stay identical to it off.
2. The opening (checkpoint `opening`, a cutscene): identical with both on.
3. The save room (checkpoint `save_room`, needs the card), field play:
   - the lens: the focal length follows the game's field of view (about
     50 mm on a 24 mm sensor), the override replaces it; the blur far away
     grows as the f-stop drops and with a longer lens;
   - blur in the picture grows with a lower f-stop (the 3D's edge energy
     falls f/8 > f/2.8 > f/1.2) and with the distance from the focal plane
     (manual focus; depth bins from the depth view);
   - the 2D/UI stays exactly as it was; Ashley in focus keeps her edges;
   - the bokeh test card: a bright dot's bokeh is a polygon of the blade
     count (the strongest harmonic of its outline's radius by angle is the
     blade count for 5, 6 and 8 blades; a round aperture has none), the
     anamorphic squeeze makes it taller than wide, the cat's eye flattens it
     toward the corners (narrower along the radius than across);
   - a field menu and a forced cutscene turn it off (identical picture);
   - first-person view (Start): the third-person depth of field does not
     apply there; its own focuses on the centre of the view (the probe's
     depth matches the depth view's there), and looking down at the floor
     refocuses nearer, eased over the refocus time (between, not a jump);
   - Vulkan: the GPU time of the depth of field (RPC perf, section "dof")
     at 4x internal resolution is printed (and must stay under 3 ms).
No crash, shader error or post message. --shots saves pictures (game
imagery: keep them local).
"""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from post_smoke import capture, edge_energy, luma, masks, redrawn  # noqa: E402

ATTRACT_VSYNC = 1800
failures = []


def check(ok, message):
    print(("ok   " if ok else "FAIL ") + message, flush=True)
    if not ok:
        failures.append(message)


def depth_sz(depth):
    """GTE SZ per pixel from the depth view (log grey, near bright: scaled.frag show_depth); NaN elsewhere."""
    g = depth[..., 1].astype(np.float32) / 255.0
    sz = 2.0 ** (6.0 + 8.0 * (1.0 - g))
    ui = (depth[..., 0] > 140) & (depth[..., 1] < 20)
    far = depth.max(axis=2) < 4
    sz[ui | far] = np.nan
    return sz


def outline_harmonics(image, centre, reach):
    """The bright blob around `centre` (y, x): its outline's radius by angle and the strength of each
    harmonic 1..12 of it (relative to the mean radius)."""
    y0, x0 = centre
    y = luma(image).astype(np.float32)
    patch = y[max(0, y0 - reach): y0 + reach + 1, max(0, x0 - reach): x0 + reach + 1]
    ky, kx = np.ones(5) / 5, np.ones(5) / 5
    smooth = np.apply_along_axis(lambda r: np.convolve(r, kx, "same"), 1, patch)
    smooth = np.apply_along_axis(lambda c: np.convolve(c, ky, "same"), 0, smooth)
    cy, cx = y0 - max(0, y0 - reach), x0 - max(0, x0 - reach)
    level = 0.25 * smooth.max()
    angles = np.linspace(0, 2 * np.pi, 360, endpoint=False)
    radius = np.zeros(len(angles))
    for i, a in enumerate(angles):
        r = 0.0
        for step in np.arange(0, reach - 1, 0.25):
            py, px = int(round(cy + step * np.sin(a))), int(round(cx + step * np.cos(a)))
            if not (0 <= py < smooth.shape[0] and 0 <= px < smooth.shape[1]):
                break
            if smooth[py, px] >= level:
                r = step
        radius[i] = r
    spectrum = np.abs(np.fft.rfft(radius)) / len(radius)
    mean = radius.mean()
    return radius, {k: float(spectrum[k] / max(mean, 1e-6)) for k in range(1, 13)}


def blob_extent(image, centre, reach):
    """Width and height of the bright blob around `centre` (25% of its peak)."""
    y0, x0 = centre
    y = luma(image).astype(np.float32)
    patch = y[max(0, y0 - reach): y0 + reach + 1, max(0, x0 - reach): x0 + reach + 1]
    lit = patch >= 0.25 * patch.max()
    rows, cols = np.nonzero(lit)
    if not len(rows):
        return 0, 0
    return int(cols.max() - cols.min() + 1), int(rows.max() - rows.min() + 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    parser.add_argument("--shots", help="save pictures here (game imagery: keep local)")
    args = parser.parse_args()
    out = OUT / "dof"
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)

    def save(name, image):
        if shots:
            Image.fromarray(np.clip(image, 0, 255).astype(np.uint8)).save(shots / f"{name}.png")

    work = pathlib.Path(tempfile.mkdtemp(prefix="dof-", dir=out))
    lights, presets = work / "lights", work / "presets"
    lights.mkdir()
    presets.mkdir()
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "4",
           "ROOD_POST_PRESETS": str(presets), "ROOD_PERF": "1"}
    if vulkan and os.environ.get("VK_LAYER_PATH"):
        env["ROOD_VK_VALIDATE"] = "1"
    os.environ.pop("ROOD_SETTINGS", None)
    logs = []
    everything = {"dof": True, "dof_model": "physical", "dof_fp": True, "dof_focus": "centre", "dof_fstop": 1.2, "dof_max_blur": 32,
                  "dof_fp_fstop": 1.2, "dof_fp_max_blur": 32, "bokeh_blades": 6, "bokeh_fringe": 1,
                  "bokeh_cats_eye": 1, "bokeh_squeeze": 1.5, "bokeh_edge": "rim"}

    # ---- 1. The title and the attract demo ----
    game = launch_at("title", fast=True, log=out / "title.log", extra_env=env)
    logs.append(game.log)
    try:
        game.call("graphics", aa="off")
        r = game.call("post")
        t = r["target"]
        neutral = (not t["dof"] and not t["dof_fp"] and t["dof_model"] == "artistic" and t["bokeh_blades"] == 0
                   and t["bokeh_fringe"] == 0 and t["bokeh_cats_eye"] == 0 and t["bokeh_squeeze"] == 1)
        check(neutral and r["flags"] == 0 and not r["probe"]["on"],
              f"off by default: no depth of field, none in first person, neutral bokeh ({r['flags']}, "
              f"dof {t['dof']}, dof_fp {t['dof_fp']}, blades {t['bokeh_blades']})")
        # Lens presets write depth of field and bokeh only, over the layer's other values.
        names = r.get("lens_presets", [])
        check({"subtle", "portrait", "telephoto", "miniature", "anamorphic"} <= set(names),
              f"lens presets listed ({names})")
        game.call("post", layer="gameplay", clear="all", set={"tonemapper": "agx", "dof_fp": True})
        for name in names:
            g = game.call("post", layer="gameplay", lens_preset=name)["layers"]["gameplay"]["set"]
            check(g.get("dof") == "1" and g.get("tonemapper") == "agx" and g.get("dof_fp") == "1"
                  and any(k.startswith("bokeh") for k in g),
                  f"lens preset {name}: depth of field on, the tone mapper and first person kept ({sorted(g)})")
        game.call("post", layer="gameplay", lens_preset="anamorphic")
        g = game.call("post", layer="gameplay", lens_preset="miniature")["layers"]["gameplay"]["set"]
        check("bokeh_squeeze" not in g and g.get("dof_ashley_height") == "0.6",
              f"a lens preset clears the previous one's fields ({sorted(g)})")
        try:
            game.call("post", lens_preset="no_such_lens")
            check(False, "an unknown lens preset is refused")
        except Exception:
            check(True, "an unknown lens preset is refused")
        game.call("post", layer="gameplay", clear="all")
        off = redrawn(game, out / "title")
        game.call("post", set=everything, snap=True)
        on = redrawn(game, out / "title")
        r = game.call("post")
        diff = int(np.abs(on - off).max())
        check(r["lens_blur_off"] == "title" and r["flags"] == 0 and diff == 0,
              f"title: stays unblurred with everything on ({r['lens_blur_off']!r}, difference {diff})")
        save("title-dof-asked", on)
        game.run(ATTRACT_VSYNC)
        r = game.call("post")
        on = redrawn(game, out / "attract")
        game.call("post", clear="all", snap=True)
        off = redrawn(game, out / "attract")
        diff = int(np.abs(on - off).max())
        check(r["lens_blur_off"] == "attract" and r["flags"] == 0 and diff == 0,
              f"attract demo: stays unblurred ({r['lens_blur_off']!r}, difference {diff})")
        save("attract-dof-asked", on)
    finally:
        game.close()

    # ---- 2. The opening: a cutscene ----
    game = launch_at("opening", fast=True, log=out / "opening.log", extra_env=env)
    logs.append(game.log)
    try:
        game.call("graphics", aa="off")
        off = redrawn(game, out / "opening")
        game.call("post", set=everything, snap=True)
        on = redrawn(game, out / "opening")
        r = game.call("post")
        diff = int(np.abs(on - off).max())
        check(r["context"] == "cutscene" and r["flags"] == 0 and diff == 0,
              f"opening cutscene: stays unblurred (event {r['event']}, difference {diff})")
        save("opening-dof-asked", on)
        game.call("post", clear="all", snap=True)
    finally:
        game.close()

    # ---- 3. The save room: field play ----
    card = pathlib.Path(args.card)
    if not card.exists():
        print(f"skip: no card {card}: the field-play checks need a save")
    else:
        scratch = work / "card1.mcd"
        shutil.copy(card, scratch)
        game = launch_at("save_room", card=str(scratch), fast=True, log=out / "field.log", extra_env=env)
        logs.append(game.log)
        try:
            field(game, out, save, vulkan)
        finally:
            game.close()

    for log in logs:
        text = pathlib.Path(log).read_text(errors="replace")
        bad = [line for line in text.splitlines() if "validation:" in line or "[crash]" in line or "shader" in line
               and "error" in line.lower() or "unavailable" in line or "[post]" in line]
        check(not bad, f"{pathlib.Path(log).name}: no crash, shader error, validation or post message"
              + (f": {bad[:3]}" if bad else ""))
    shutil.rmtree(work, ignore_errors=True)
    if failures:
        print(f"FAIL: {len(failures)} checks failed")
        return 1
    print("PASS: physical depth of field, bokeh, first-person depth of field")
    return 0


def field(game, out, save, vulkan):
    game.call("graphics", aa="off")
    r = game.call("post")
    check(r["context"] == "gameplay" and r["lens_blur_off"] == "", f"save room: field play ({r['context']})")
    ui, solid, depth = masks(game, out)
    sz = depth_sz(depth)
    save("field-depth", depth)
    base = redrawn(game, out / "f")
    save("field-base", base)
    region = solid & ~ui

    def shot(**values):
        values = {"dof_model": "physical", **values}
        game.call("post", layer="gameplay", clear="all", set=values, snap=True)
        game.wait_presents(4)
        return redrawn(game, out / "f"), game.call("post")

    # The lens.
    _, r = shot(dof=True, dof_focus="ashley", dof_fstop=2.8)
    lens = r["lens"]
    check(r["flags"] & 1 and lens["model"] == "physical" and 25 < lens["focal_mm"] < 120 and lens["sensor_mm"] == 24,
          f"the lens from the game's field of view: {lens['focal_mm']:.1f} mm on {lens['sensor_mm']} mm, "
          f"focus {lens['focus_m']:.2f} m, blur far away {lens['far_px']:.1f} px")
    far = {}
    for n in (8, 2.8, 1.2):
        far[n] = shot(dof=True, dof_focus="ashley", dof_fstop=n)[1]["lens"]["far_px"]
    long_lens = shot(dof=True, dof_focus="ashley", dof_fstop=2.8, dof_focal_length=135)[1]["lens"]
    check(far[8] < far[2.8] < far[1.2] and long_lens["focal_mm"] == 135 and long_lens["far_px"] > far[2.8],
          f"blur far away grows as the f-stop drops ({far[8]:.1f} < {far[2.8]:.1f} < {far[1.2]:.1f} px) and with a "
          f"135 mm lens ({long_lens['far_px']:.1f} px)")

    # Blur in the picture: lower f-stops blur more (manual focus near the camera: most of the room behind). The
    # game's view is long (Ashley about 15 m away): a 135 mm lens makes the differences show.
    near_sz = float(np.nanpercentile(sz[region], 5))
    energies = {}
    for n in (8, 2.8, 1.2):
        image, r = shot(dof=True, dof_focus="manual", dof_focus_depth=near_sz, dof_fstop=n, dof_max_blur=24,
                        dof_focal_length=135)
        energies[n] = edge_energy(image, region)
        save(f"field-fstop-{n}", image)
        ui_diff = int(np.abs(image - base)[ui].max()) if ui.any() else 0
        check(ui_diff == 0, f"f/{n}: the 2D/UI unchanged (max difference {ui_diff})")
    e0 = edge_energy(base, region)
    check(e0 > energies[8] > energies[2.8] > energies[1.2],
          f"lower f-stops blur more: edge energy {e0:.1f} (off) > f/8 {energies[8]:.1f} > f/2.8 {energies[2.8]:.1f} "
          f"> f/1.2 {energies[1.2]:.1f}")

    # ... and blur grows with the distance from the focal plane (the middle depth in focus).
    focus = float(np.nanmedian(sz[region]))
    image, r = shot(dof=True, dof_focus="manual", dof_focus_depth=focus, dof_fstop=2.8, dof_max_blur=24,
                    dof_near_blur=1, dof_focal_length=135)
    save("field-focus-middle", image)
    away = np.abs(1.0 - focus / sz)
    ratios = []
    for lo, hi in ((0.0, 0.03), (0.06, 0.15), (0.2, 10.0)):
        bin_ = region & (away >= lo) & (away < hi)
        if bin_.sum() < 2000:
            ratios.append(None)
            continue
        ratios.append(edge_energy(image, bin_) / max(edge_energy(base, bin_), 1e-6))
    known = [x for x in ratios if x is not None]
    check(len(known) >= 2 and all(a > b for a, b in zip(known, known[1:])),
          "blur grows with the distance from the focal plane: kept edge energy "
          + ", ".join("-" if x is None else f"{x:.2f}" for x in ratios) + " (in focus, nearer, farthest)")

    # Ashley in focus keeps her edges (her depth around the focus, no halo of the background).
    image, r = shot(dof=True, dof_focus="ashley", dof_fstop=1.2, dof_max_blur=24, dof_focal_length=135)
    save("field-ashley", image)
    f_sz = r["focus"]
    near_ashley = region & (np.abs(1.0 - f_sz / sz) < 0.04)
    if near_ashley.sum() > 500:
        kept = edge_energy(image, near_ashley) / max(edge_energy(base, near_ashley), 1e-6)
        rest = region & (np.abs(1.0 - f_sz / sz) > 0.5)
        blurred = edge_energy(image, rest) / max(edge_energy(base, rest), 1e-6) if rest.sum() > 2000 else 0
        check(kept > 0.75 and kept > blurred + 0.2,
              f"focus on Ashley (SZ {f_sz:.0f}): the focal plane keeps {kept:.2f} of its edges, far away "
              f"{blurred:.2f}")
    else:
        check(False, f"focus on Ashley: too few pixels near the focal plane ({int(near_ashley.sum())})")

    # The bokeh test card: the blade count in a bright dot's bokeh.
    h, w = base.shape[:2]
    dot = (h // 2, 3 * w // 8)
    dots = [(h // 2, (2 * i + 1) * w // 8) for i in range(4)] + [(h // 6, (2 * i + 1) * w // 8) for i in (1, 2, 3)] \
        + [(5 * h // 6, (2 * i + 1) * w // 8) for i in (1, 2)]
    reach = int(32 * h / 720) + 8
    card = {"dof": True, "dof_focus": "ashley", "dof_max_blur": 32, "bokeh_samples": 160, "bokeh_highlights": 0}
    game.call("post", bokeh_test=True)
    try:
        for blades in (0, 5, 6, 8):
            image, r = shot(**card, bokeh_blades=blades, bokeh_curvature=0)
            save(f"bokeh-blades-{blades}", image)
            # The dots clear of the HUD, their outlines averaged (each is the same kernel).
            radius = np.mean([outline_harmonics(image, d, reach)[0] for d in dots], axis=0)
            spectrum = np.abs(np.fft.rfft(radius)) / len(radius)
            harmonics = {k: float(spectrum[k] / max(radius.mean(), 1e-6)) for k in range(1, 13)}
            best = max(range(3, 13), key=lambda k: harmonics[k])
            detail = ", ".join(f"{k}: {harmonics[k]:.3f}" for k in (3, 4, 5, 6, 7, 8, 9, 10))
            if blades == 0:
                check(max(harmonics[k] for k in range(3, 13)) < 0.02 and radius.mean() > 0.6 * 32 * h / 720,
                      f"round aperture: no polygon in the outline (mean radius {radius.mean():.1f}; {detail})")
            else:
                others = [harmonics[k] for k in range(3, 13) if k % blades]
                check(best == blades and harmonics[blades] > 3 * max(others),
                      f"{blades} blades: the outline's strongest harmonic is {best} ({detail})")
        image, _ = shot(**card, bokeh_squeeze=2)
        save("bokeh-anamorphic", image)
        bw, bh = blob_extent(image, dot, reach)
        check(bh > 1.6 * bw, f"anamorphic squeeze 2: taller than wide ({bw} x {bh})")
        image, _ = shot(**card, bokeh_cats_eye=1)
        save("bokeh-cats-eye", image)
        corner = (h // 6, w // 8)  # the top left dot: its radius runs toward the top left corner
        cw, ch = blob_extent(image, (h // 2, 3 * w // 8), reach)  # nearer the middle: its radius runs along x
        radius_c, _ = outline_harmonics(image, corner, reach)
        # Along the radius (toward the top left: 225 degrees in picture angles) vs across it (135 / 315).
        along = (radius_c[225] + radius_c[45]) / 2
        across = (radius_c[135] + radius_c[315]) / 2
        check(along < 0.8 * across and along / across < cw / ch - 0.1,
              f"cat's eye: a corner's bokeh is narrower along the radius ({along:.1f}) than across ({across:.1f}), "
              f"more than one nearer the middle ({cw} x {ch})")
    finally:
        game.call("post", bokeh_test=False)

    # Menus and cutscenes: off.
    shot(dof=True, dof_fp=True, dof_fstop=1.2, dof_max_blur=24)
    game.press("triangle", vsyncs=3, settle=40)
    r = game.call("post")
    check(r["lens_blur_off"] == "menu" and r["flags"] == 0, f"a field menu turns it off ({r['lens_blur_off']!r})")
    game.press("triangle", vsyncs=3, settle=60)
    game.call("post", context="cutscene", snap=True)
    game.wait_presents(3)
    r = game.call("post")
    check(r["flags"] == 0 and not r["target"]["dof"] and not r["target"]["dof_fp"],
          "a cutscene turns both depths of field off")
    game.call("post", context="auto", snap=True)

    # GPU time (Vulkan sections; a grade pass after it ends the section).
    if vulkan:
        shot(dof=True, dof_focus="ashley", dof_fstop=1.2, dof_max_blur=24, vignette=0.01)
        game.call("perf", reset=True)
        game.run(60)
        perf = game.call("perf")
        ms = (perf.get("sections") or {}).get("dof")
        print(f"     GPU: depth of field {ms} ms per frame at 4x internal resolution ({perf})", flush=True)
        check(ms is not None and ms < 3.0, f"depth of field GPU time {ms} ms (< 3 ms)")

    # First-person view: the third-person depth of field off there, its own on the centre.
    game.call("post", layer="gameplay", clear="all", set={"dof": True, "dof_model": "physical", "dof_fstop": 1.2}, snap=True)
    game.press("start", vsyncs=3, settle=90)
    r = game.call("post")
    check(r["first_person"] and r["flags"] == 0,
          f"first-person view: the third-person depth of field does not apply ({r['first_person']}, {r['flags']})")
    game.call("post", layer="gameplay", clear="all", set={"dof_fp": True, "dof_model": "physical", "dof_fp_fstop": 1.2,
                                                          "dof_fp_focus_speed": 0.5}, snap=True)
    game.run(30)
    r = game.call("post")
    _, _, fp_depth = masks(game, out)
    fsz = depth_sz(fp_depth)
    ch, cw = fsz.shape[0] // 2, fsz.shape[1] // 2
    centre = float(np.nanmean(fsz[ch - 10: ch + 10, cw - 10: cw + 10]))
    check(r["flags"] & 1 and r["probe"]["on"] and r["probe"]["uv"] == [0.5, 0.5]
          and abs(r["focus"] - centre) < 0.25 * centre,
          f"first-person focus on the centre: SZ {r['focus']:.0f}, the depth view there {centre:.0f} "
          f"(probe {r['probe']})")
    save("fp-focus-centre", redrawn(game, out / "f"))
    # Look down at the floor: the focus comes nearer, eased.
    start = r["focus"]
    game.hold("up", 40)  # up looks down (qol_fp_invert_y off)
    track = []
    for _ in range(8):
        game.run(4)
        track.append(game.call("post")["focus"])
    game.run(60)
    end = game.call("post")["focus"]
    _, _, fp_depth = masks(game, out)
    fsz = depth_sz(fp_depth)
    centre = float(np.nanmean(fsz[ch - 10: ch + 10, cw - 10: cw + 10]))
    between = [x for x in track if min(start, end) + 1 < x < max(start, end) - 1]
    check(abs(end - centre) < 0.25 * centre and abs(end - start) > 0.1 * start and between,
          f"the focus follows the centre: SZ {start:.0f} -> {end:.0f} (depth view {centre:.0f}), easing through "
          + ", ".join(f"{x:.0f}" for x in track))
    save("fp-focus-down", redrawn(game, out / "f"))
    game.press("start", vsyncs=3, settle=60)
    game.call("post", layer="gameplay", clear="all", snap=True)


if __name__ == "__main__":
    sys.exit(main())
