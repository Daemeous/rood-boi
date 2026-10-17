#!/usr/bin/env python3
"""The post stack and post profiles (pc/docs/Post_Processing.md, post.h).

    python3 pc/tests/post_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

Frame-based (RPC-exact stepping, paused redraws), 960 x 720, internal
resolution 4x, no lights, no path tracing:

0. The intro movie (24-bit, presented from VRAM): identical with every
   effect on.
1. The title (checkpoint `title`) and the attract demo (1800 vsyncs idle at
   the title): depth of field turned on in the gameplay layer stays off
   (RPC post `lens_blur_off` "title" / "attract"), the pictures identical to
   it off; every effect neutral by default (no grade pass). Profile save and
   load through a settings file; a user preset saved and listed; the shipped
   presets never turn depth of field on.
2. The opening (checkpoint `opening`, a cutscene: an event script runs):
   the context is "cutscene", depth of field on in the gameplay layer is off
   (identical picture); the opening card (2D only) is identical with every
   effect on.
3. The save room (checkpoint `save_room`, needs the card): field play.
   - masks from the depth view (2D/UI magenta);
   - each effect alone: the 3D changes as it should (exposure brighter or
     darker, bloom brighter, chromatic aberration moves red and blue but not
     green, more at the edges, vignette darkens the corners, grain adds
     noise that holds on redraws and changes between game frames, every
     tonemapper changes the picture and Reinhard compresses it, white
     balance cooler / warmer, saturation 0 grey, contrast up, gain brighter,
     a channel-swapping LUT swaps red and blue, sharpen raises edge energy,
     the convolution bloom where the renderer has it), the 2D/UI pixels
     exactly unchanged;
   - depth of field blurs the 3D in play, not the HUD; a field menu turns it
     off ("menu"); a forced cutscene context too; profiles blend over the
     blend time on a context change (exposure rises step by step);
   - a room item (`post room exposure=1` in the room's user layer) brightens
     the room, and the editor's layer edits save to the room's file;
   - auto exposure adapts (the save room is dark: the exposure rises);
   - each shipped preset changes the 3D and keeps the 2D.
No crash, shader error or (Vulkan with VK_LAYER_PATH holding the Khronos
layer) validation message. --shots saves before/after pictures (game
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
from rood_rpc import OUT, Game, wait_for_file  # noqa: E402
from checkpoints import launch_at  # noqa: E402

WINDOW = "960x720"
ATTRACT_VSYNC = 1800  # idle at the title: the attract demo (coplanar_order_smoke.py)
LUMA = np.array([0.299, 0.587, 0.114])
failures = []


def check(ok, message):
    print(("ok   " if ok else "FAIL ") + message, flush=True)
    if not ok:
        failures.append(message)


def capture(game, path, timeout=20):
    ppm = path.with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    game.call("present_dump", path=str(ppm))
    wait_for_file(ppm, timeout)
    image = np.asarray(Image.open(ppm).convert("RGB")).astype(np.int16)
    ppm.unlink()
    return image


def redrawn(game, path):
    """The paused game's newest frame drawn again with the current settings."""
    game.wait_presents(2)
    game.call("redraw")
    game.wait_presents(2)
    return capture(game, path)


def profile(game, path, layer="gameplay", **values):
    """Only these values in the layer (others cleared), then the redrawn picture."""
    game.call("post", layer=layer, clear="all", set=values, snap=True)
    return redrawn(game, path)


def luma(image):
    return image.astype(np.float32) @ LUMA


def masks(game, out):
    game.call("graphics", show_depth=True)
    depth = redrawn(game, out / "depth")
    game.call("graphics", show_depth=False)
    ui = (depth[..., 0] > 140) & (depth[..., 1] < 20) & (depth[..., 2] > 140)
    far = depth.max(axis=2) < 4
    return ui, ~ui & ~far, depth


def edge_energy(image, region):
    y = luma(image)
    dx, dy = np.diff(y, axis=1) ** 2, np.diff(y, axis=0) ** 2
    return float(dx[region[:, 1:] & region[:, :-1]].mean() + dy[region[1:, :] & region[:-1, :]].mean())


def write_swap_lut(path, n=17):
    """A .cube LUT that swaps red and blue."""
    lines = ["TITLE \"swap red and blue\"", f"LUT_3D_SIZE {n}"]
    for b in range(n):
        for g in range(n):
            for r in range(n):
                lines.append(f"{b / (n - 1):.6f} {g / (n - 1):.6f} {r / (n - 1):.6f}")
    path.write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    parser.add_argument("--shots", help="save before/after pictures here (game imagery: keep local)")
    args = parser.parse_args()
    out = OUT / "post"
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)

    def save(name, image):
        if shots:
            Image.fromarray(np.clip(image, 0, 255).astype(np.uint8)).save(shots / f"{name}.png")

    work = pathlib.Path(tempfile.mkdtemp(prefix="post-", dir=out))
    lights, presets = work / "lights", work / "presets"
    (presets / "luts").mkdir(parents=True)
    lights.mkdir()
    write_swap_lut(presets / "luts" / "swap.cube")
    env = {"ROOD_WINDOW": WINDOW, "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "4",
           "ROOD_POST_PRESETS": str(presets)}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env["ROOD_VK_VALIDATE"] = "1"
    os.environ.pop("ROOD_SETTINGS", None)
    logs = []
    dof_on = {"dof": True, "dof_model": "artistic", "dof_max_blur": 24, "dof_aperture": 16, "dof_focus": "centre"}

    # ---- 0. The intro movie: never touched ----
    everything = {**dof_on, "exposure": 2, "tonemapper": "aces", "grain": 1, "vignette": 1, "bloom_intensity": 2,
                  "chromatic_aberration": 5, "saturation": [1, 1, 1, 0]}
    game = Game.launch(movies=True, fast=True, log=out / "movie.log", extra_env=env,
                       socket_path=os.environ.get("ROOD_RPC") or f"/tmp/rood-post-{os.getpid()}.sock")
    logs.append(game.log)
    try:
        game.pause()
        game.run(max(0, 1300 - game.status()["vsync"]))  # the publisher cards, then the movie (movie_smoke.py)
        playing = game.u32("pc_display_rgb24") == 1
        off = capture(game, out / "movie")
        game.call("post", set=everything, snap=True)
        game.wait_presents(2)
        on = capture(game, out / "movie")
        diff = int(np.abs(on - off).max())
        check(playing and diff == 0, f"the intro movie is untouched with every effect on (difference {diff})")
        save("movie-everything-on", on)
    finally:
        game.close()

    # ---- 1. The title and the attract demo; profile save / load ----
    settings = work / "settings.cfg"
    settings.write_text("aa fxaa\n")
    game = launch_at("title", fast=True, log=out / "title.log", extra_env={**env, "ROOD_SETTINGS": str(settings)})
    logs.append(game.log)
    try:
        game.call("graphics", aa="fxaa")
        r = game.call("post")
        check(not r["grade"] and r["flags"] == 0 and not r["target"]["dof"],
              "defaults: no grade pass, no depth of field, every effect neutral")
        off = redrawn(game, out / "title")
        game.call("post", set=dof_on, snap=True)
        on = redrawn(game, out / "title")
        r = game.call("post")
        diff = int(np.abs(on - off).max())
        check(r["lens_blur_off"] == "title" and r["flags"] == 0 and diff == 0,
              f"title: depth of field stays off ({r['lens_blur_off']!r}, picture difference {diff})")
        save("title-dof-asked", on)
        # Profiles through the settings file.
        game.call("post", layer="gameplay", clear="all", set={"exposure": 0.5, "tonemapper": "agx",
                                                               "saturation": [1, 0.9, 1, 0.8], "lut": "swap.cube"})
        game.call("post", layer="cutscene", clear="all", set={"vignette": 0.4})
        before = game.call("post")["layers"]
        game.call("settings", action="save")
        text = settings.read_text()
        check("post_exposure 0.5" in text and "post_cutscene_vignette 0.4" in text and "post_tonemapper agx" in text,
              "settings save writes the layers' set values (post_*, post_cutscene_*)")
        game.call("post", layer="gameplay", clear="all")
        game.call("post", layer="cutscene", clear="all")
        game.call("settings", action="load")
        after = game.call("post")["layers"]
        check(after["gameplay"] == before["gameplay"] and after["cutscene"] == before["cutscene"],
              "settings load restores the gameplay and cutscene layers")
        r = game.call("post", layer="gameplay", save_preset="smoke_test")
        check("smoke_test" in r["presets"] and (presets / "smoke_test.json").exists(), "a user preset saves and lists")
        for name in ("cinematic", "film", "clean_modern"):
            t = game.call("post", layer="gameplay", clear="all", preset=name)["target"]
            check(name in r["presets"] and not t["dof"] and not t["tilt_shift"],
                  f"preset {name}: loads, no depth of field or tilt-shift")
        game.call("post", layer="gameplay", clear="all", set=dof_on)
        game.call("post", layer="cutscene", clear="all")
        game.run(ATTRACT_VSYNC)
        r = game.call("post")
        on = redrawn(game, out / "attract")
        game.call("post", clear="all", snap=True)
        off = redrawn(game, out / "attract")
        diff = int(np.abs(on - off).max())
        check(game.state()["zone"] == 20 and r["lens_blur_off"] == "attract" and r["flags"] == 0 and diff == 0,
              f"attract demo: depth of field stays off ({r['lens_blur_off']!r}, zone {game.state()['zone']}, "
              f"picture difference {diff})")
        save("attract-dof-asked", on)
    finally:
        game.close()

    # ---- 2. The opening: a cutscene ----
    game = launch_at("opening", fast=True, log=out / "opening.log", extra_env=env)
    logs.append(game.log)
    try:
        game.call("graphics", aa="fxaa")
        off = redrawn(game, out / "opening")
        game.call("post", set=dof_on, snap=True)
        on = redrawn(game, out / "opening")
        r = game.call("post")
        diff = int(np.abs(on - off).max())
        check(r["context"] == "cutscene" and r["event"] >= 0 and r["flags"] == 0 and diff == 0,
              f"opening: a cutscene (event {r['event']}), depth of field stays off (difference {diff})")
        save("opening-dof-asked", on)
        game.call("post", context="gameplay", snap=True)
        forced = redrawn(game, out / "opening")
        r = game.call("post")
        check(r["flags"] & 1 and np.abs(forced - off).mean() > 0.3,
              "the same frame previewed as gameplay blurs (the cutscene layer is what turned it off)")
        save("opening-previewed-as-gameplay", forced)
        game.call("post", context="auto", clear="all", snap=True)
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
            field(game, out, save, lights)
        finally:
            game.close()

    for log in logs:
        text = pathlib.Path(log).read_text(errors="replace")
        bad = [line for line in text.splitlines() if "validation:" in line or "[crash]" in line or "shader" in line
               and "error" in line.lower() or "post stack unavailable" in line or "[post]" in line]
        check(not bad, f"{pathlib.Path(log).name}: no crash, shader error, validation or post message"
              + (f": {bad[:3]}" if bad else ""))
    shutil.rmtree(work, ignore_errors=True)
    if failures:
        print(f"FAIL: {len(failures)} checks failed")
        return 1
    print("PASS: post stack effects, profiles and contexts")
    return 0


def field(game, out, save, lights):
    game.call("graphics", aa="fxaa")
    state = game.state()
    r = game.call("post")
    check(r["context"] == "gameplay" and r["lens_blur_off"] == "", f"save room: field play ({r['context']}, "
          f"zone {state['zone']} room {state['room']})")
    ui, solid, depth = masks(game, out)
    check(ui.sum() > 500 and solid.mean() > 0.3, f"masks: {ui.sum()} 2D/UI pixels, {solid.mean() * 100:.0f}% 3D")
    base = profile(game, out / "f")
    save("field-base", base)
    b = luma(base)
    h, w = b.shape
    corner = np.zeros_like(solid)
    corner[: h // 5, : w // 5] = corner[: h // 5, -w // 5:] = corner[-h // 5:, : w // 5] = corner[-h // 5:, -w // 5:] = True
    centre = np.zeros_like(solid)
    centre[2 * h // 5: 3 * h // 5, 2 * w // 5: 3 * w // 5] = True

    def effect(name, expect, **values):
        image = profile(game, out / "f", **values)
        save(f"field-{name}", image)
        r = game.call("post")
        ui_diff = int(np.abs(image - base)[ui].max()) if ui.any() else 0
        moved = float(np.abs(image - base)[solid].mean())
        ok, detail = expect(image)
        check(r["grade"] and ui_diff == 0 and moved > 0.2 and ok,
              f"{name}: 3D changed {moved:.2f}, 2D/UI max difference {ui_diff}; {detail}")
        return image

    def mean3d(image, region=None):
        return float(luma(image)[solid if region is None else solid & region].mean())

    base_mean = mean3d(base)
    effect("exposure+1", lambda i: (mean3d(i) > base_mean + 3, f"mean {base_mean:.1f} -> {mean3d(i):.1f}"), exposure=1)
    effect("exposure-1", lambda i: (mean3d(i) < base_mean - 3, f"mean {base_mean:.1f} -> {mean3d(i):.1f}"), exposure=-1)
    effect("bloom-gaussian", lambda i: (mean3d(i) > base_mean + 0.5 and game.call("post")["bloom"] == "gaussian",
                                        f"mean {base_mean:.1f} -> {mean3d(i):.1f}, {game.call('post')['bloom']}"),
           bloom_method="gaussian", bloom_intensity=2, bloom_threshold=0.2)
    if game.call("post")["fft_available"]:
        effect("bloom-convolution",
               lambda i: (mean3d(i) > base_mean + 0.5 and game.call("post")["bloom"] == "convolution",
                          f"mean {base_mean:.1f} -> {mean3d(i):.1f}, {game.call('post')['bloom']}"),
               bloom_method="convolution", bloom_intensity=2, bloom_threshold=0.2, bloom_kernel_streaks=6,
               bloom_kernel_streak_intensity=0.4)
    else:
        print("     (no convolution bloom on this renderer: the Gaussian one stands in)")

    def ca(i):
        d = np.abs(i - base).astype(np.float32)
        g = float(d[..., 1][solid].mean())
        edge_rb = float(d[..., [0, 2]][solid & corner].mean())
        mid_rb = float(d[..., [0, 2]][solid & centre].mean()) if (solid & centre).any() else 0.0
        return g < 0.6 and edge_rb > 2 * mid_rb + 0.3, f"green {g:.2f}, red/blue at the edges {edge_rb:.2f}, centre {mid_rb:.2f}"
    effect("chromatic-aberration", ca, chromatic_aberration=5, chromatic_aberration_start=0.4)

    def vignette(i):
        c_drop = mean3d(base, corner) - mean3d(i, corner)
        m_drop = mean3d(base, centre) - mean3d(i, centre)
        return c_drop > 3 * max(m_drop, 0.1), f"corners darker by {c_drop:.1f}, centre by {m_drop:.1f}"
    effect("vignette", vignette, vignette=1)

    grainy = effect("grain", lambda i: (edge_energy(i, solid) > edge_energy(base, solid) * 1.3,
                                        f"edge energy {edge_energy(base, solid):.1f} -> {edge_energy(i, solid):.1f}"),
                    grain=1, grain_size=1)
    again = redrawn(game, out / "f")
    check(int(np.abs(again - grainy).max()) == 0, "grain holds on a redraw of the same game frame")
    game.run(4)
    later = redrawn(game, out / "f")
    base_later = profile(game, out / "f")
    game.call("post", set={"grain": 1, "grain_size": 1}, snap=True)
    later = redrawn(game, out / "f")
    noise_a, noise_b = (grainy - base)[solid], (later - base_later)[solid]
    check(float(np.abs(noise_a - noise_b).mean()) > 1.0, "grain changes from one game frame to the next")
    base = base_later
    base_mean = mean3d(base)

    for tm in ("aces", "agx", "filmic", "reinhard", "neutral"):
        effect(f"tonemap-{tm}", lambda i: (True, f"mean {base_mean:.1f} -> {mean3d(i):.1f}"), tonemapper=tm)
    effect("tonemap-reinhard-white2", lambda i: (mean3d(i) < base_mean, f"compresses: {base_mean:.1f} -> {mean3d(i):.1f}"),
           tonemapper="reinhard", tonemap_white=2)

    def blue_shift(i):
        d0 = float((base[..., 2] - base[..., 0])[solid].mean())
        d1 = float((i[..., 2] - i[..., 0])[solid].mean())
        return d1 > d0 + 2, f"blue - red {d0:.1f} -> {d1:.1f}"
    effect("temperature-3500", blue_shift, temperature=3500)

    def red_shift(i):
        d0 = float((base[..., 0] - base[..., 2])[solid].mean())
        d1 = float((i[..., 0] - i[..., 2])[solid].mean())
        return d1 > d0 + 2, f"red - blue {d0:.1f} -> {d1:.1f}"
    effect("temperature-9000", red_shift, temperature=9000)
    effect("saturation-0", lambda i: (float(np.ptp(i[solid], axis=1).mean()) < 2.5,
                                      f"channel spread {float(np.ptp(base[solid], axis=1).mean()):.1f} -> "
                                      f"{float(np.ptp(i[solid], axis=1).mean()):.1f}"), saturation=[1, 1, 1, 0])
    # Contrast pivots on mid grey (0.18 linear: about 118 on the screen): pixels move away from it.
    def spread(i):
        return float(np.abs(luma(i)[solid] - 118).mean())
    effect("contrast-1.5", lambda i: (spread(i) > spread(base) + 1,
                                      f"distance from mid grey {spread(base):.1f} -> {spread(i):.1f}"),
           contrast=[1, 1, 1, 1.5])
    effect("gain-1.5", lambda i: (mean3d(i) > base_mean + 3, f"mean {base_mean:.1f} -> {mean3d(i):.1f}"),
           gain=[1, 1, 1, 1.5])

    def swapped(i):
        e = float(np.abs(i[..., 0] - base[..., 2])[solid].mean() + np.abs(i[..., 2] - base[..., 0])[solid].mean()) / 2
        return e < 3, f"red <-> blue within {e:.2f}"
    effect("lut-swap", swapped, lut="swap.cube")
    effect("sharpen", lambda i: (edge_energy(i, solid) > edge_energy(base, solid) * 1.05,
                                 f"edge energy {edge_energy(base, solid):.1f} -> {edge_energy(i, solid):.1f}"), sharpen=1)

    # Depth of field in play; a field menu turns it off.
    blurred = profile(game, out / "f", dof=True, dof_model="artistic", dof_max_blur=24, dof_aperture=16)
    r = game.call("post")
    check(r["flags"] & 1 and float(np.abs(blurred - base)[solid].mean()) > 0.3
          and int(np.abs(blurred - base)[ui].max()) == 0,
          f"depth of field in play: the 3D blurs ({float(np.abs(blurred - base)[solid].mean()):.2f}), the HUD stays")
    save("field-dof", blurred)
    game.press("triangle", vsyncs=3, settle=40)
    r = game.call("post")
    check(r["lens_blur_off"] == "menu" and r["flags"] == 0, f"a field menu turns it off ({r['lens_blur_off']!r})")
    game.press("triangle", vsyncs=3, settle=60)
    r = game.call("post")
    check(r["lens_blur_off"] == "" and r["flags"] & 1, "closing the menu turns it back on")

    # Blending: a context change (the cutscene layer brighter) blends over the blend time.
    game.call("post", layer="gameplay", clear="all", snap=True)
    game.call("post", layer="cutscene", clear="all", set={"exposure": 2})
    game.call("post", blend_time=1.0)
    game.run(4)
    start = mean3d(redrawn(game, out / "f"))
    game.call("post", context="cutscene")
    means, progress = [], []
    for _ in range(4):
        game.run(15)
        means.append(mean3d(capture(game, out / "f")))
        progress.append(game.call("post")["blend"])
    game.run(40)
    final = game.call("post")["blend"]
    check(all(b > a for a, b in zip([start] + means, means)) and 0 < progress[0] < 1 and final == 1,
          f"a context change blends: mean {start:.1f} -> {', '.join(f'{m:.1f}' for m in means)}, "
          f"progress {', '.join(f'{p:.2f}' for p in progress)} -> {final}")
    game.call("post", context="auto", layer="cutscene", clear="all", snap=True)

    # A room item.
    state = game.state()
    room_file = lights / f"zone{state['zone']:02d}_room{state['room']:02d}.txt"
    room_file.write_text("rood-lighting 2\npost room exposure=1\n")
    game.reload_lights()
    game.wait_presents(2)
    r = game.call("post")
    lit = redrawn(game, out / "f")
    check(r["layers"]["room"]["set"].get("exposure") == "1" and mean3d(lit) > base_mean + 3,
          f"the room's `post room` item applies (mean {base_mean:.1f} -> {mean3d(lit):.1f})")
    game.call("post", layer="room", set={"vignette": 0.3})
    game.call("lighting", action="save")
    text = room_file.read_text()
    check("post room" in text and "vignette=0.3" in text, "the room layer's edits save to the room's file")
    room_file.unlink()
    game.reload_lights()
    game.call("post", layer="room", clear="all", snap=True)

    # Auto exposure: the save room is dark, the exposure rises.
    game.call("post", layer="gameplay", clear="all", set={"exposure_mode": "auto", "auto_exposure_max": 2,
                                                          "auto_exposure_speed_up": 20}, snap=True)
    game.run(90)
    r = game.call("post")
    check(r["ev"] > 0.1, f"auto exposure adapts: {r['ev']:+.2f} EV")
    game.call("post", layer="gameplay", clear="all", snap=True)

    ui, solid, _ = masks(game, out)  # the game ran on: this frame's masks
    base = redrawn(game, out / "f")
    for name in ("cinematic", "film", "clean_modern"):
        game.call("post", layer="gameplay", clear="all", preset=name, snap=True)
        image = redrawn(game, out / "f")
        save(f"field-preset-{name}", image)
        check(float(np.abs(image - base)[solid].mean()) > 0.5 and int(np.abs(image - base)[ui].max()) == 0,
              f"preset {name}: the 3D changes, the 2D/UI stays")
    save("field-base-final", base)
    game.call("post", layer="gameplay", clear="all", snap=True)


if __name__ == "__main__":
    sys.exit(main())
