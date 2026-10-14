#!/usr/bin/env python3
"""Anti-aliasing (pc/README.md "Anti-aliasing"): every method renders, smooths
geometric edges and leaves texels and the 2D/UI alone.

    python3 pc/tests/aa_smoke.py [--shots DIR]

New Game, the first room from a fixed editor camera (golden_frames.py's
pose), 1440 x 1080 window (the 4x target magnified, so stair steps show).
The game is paused and the same frame drawn again (RPC redraw) under each
`aa` mode, so the pictures differ only by the method:

- masks from the same frame: the depth view (UI magenta, far black) and the
  normals view (view_mode 5) give the geometric edges (depth or normal
  breaks) and the 3D interior (3D pixels a few pixels away from any edge);
- edges get smoother: the squared luma steps across the geometric edges
  drop below `off`'s (each method by its own margin);
- texels stay crisp: on the interior every method except FXAA (which blurs
  any contrast, by design) is identical to `off`;
- msaa_active reports the samples asked for (where the GPU has them);
- the opening card (2D only: caption over black) is identical under every
  method but FXAA;
- supersampling (ssaa2) raises the internal resolution and, filtered down
  with the area or Lanczos filter, matches `off` at that resolution closely;
- frame interpolation with msaa4+smaa still moves the camera every vsync
  (interpolation_smoke.py's opening pan);
- no crash, shader error or (Vulkan, where VK_LAYER_PATH has the Khronos
  layer) validation message.
"""
import argparse
import os
import pathlib
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, wait_for_file  # noqa: E402
import golden_frames as gf  # noqa: E402

WINDOW = "1440x1080"
POSE = (768, -900, 836, 270, -20)
# mode: the share of `off`'s edge energy it must stay under
MODES = {"fxaa": 0.97, "smaa": 0.99, "msaa2": 0.95, "msaa4": 0.93, "msaa8": 0.93, "msaa4+smaa": 0.92}
CRISP = ["smaa", "msaa2", "msaa4", "msaa8", "msaa4+smaa"]


def capture(game, path, timeout=20):
    """The next presented image (a paused game keeps presenting)."""
    ppm = path.with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    game.call("present_dump", path=str(ppm))
    wait_for_file(ppm, timeout)  # complete once it exists (written, then renamed)
    image = np.asarray(Image.open(ppm).convert("RGB")).astype(np.int16)
    ppm.unlink()
    return image


def redrawn(game, path, base, **graphics):
    game.call("graphics", **{**base, **graphics})
    game.wait_presents(2)  # the paused game redraws with the new settings
    game.call("redraw")
    game.wait_presents(2)
    return capture(game, path)


def dilate(mask, r):
    out = mask.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= np.roll(np.roll(mask, dy, 0), dx, 1)
    return out


def luma(image):
    return image[..., 0] * 0.299 + image[..., 1] * 0.587 + image[..., 2] * 0.114


def edge_energy(image, edges):
    """Squared luma steps between neighbours where either is on a geometric edge."""
    y = luma(image.astype(np.float32))
    dx, dy = np.diff(y, axis=1) ** 2, np.diff(y, axis=0) ** 2
    mx, my = edges[:, 1:] | edges[:, :-1], edges[1:, :] | edges[:-1, :]
    return float(dx[mx].sum() + dy[my].sum())


def burst(game, base, count=20):
    """Mean absolute difference of each presented image from the one before."""
    for old in base.parent.glob(base.name + ".*.ppm"):
        old.unlink()
    game.call("present_dump", path=str(base), count=count)
    game.run(count + 6)
    paths = [pathlib.Path(f"{base}.{i:04d}.ppm") for i in range(count)]
    for _ in range(30):  # written a few presents later (on the game thread)
        if all(p.exists() for p in paths):
            break
        game.run(1)
    frames = [np.asarray(Image.open(p).convert("RGB")).astype(np.float32) for p in paths]
    for p in paths:
        p.unlink()
    return np.array([np.mean(np.abs(b - a)) for a, b in zip(frames, frames[1:])])[2:]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--shots", help="also save the pictures here (game imagery: keep local)")
    args = parser.parse_args()
    out = OUT / "aa"
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": WINDOW, "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "4"}
    if os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("VK_LAYER_PATH"):
        env.setdefault("ROOD_VK_VALIDATE", "1")
    os.environ.pop("ROOD_SETTINGS", None)
    failures = []

    def check(ok, message):
        print(("ok   " if ok else "FAIL ") + message, flush=True)
        if not ok:
            failures.append(message)

    def save(name, image):
        if shots:
            Image.fromarray(image.astype(np.uint8)).save(shots / f"{name}.png")

    game = Game.launch(fast=True, log=out / "game.log", extra_env=env)
    try:
        game.wait_title()
        defaults = {**{k: v for k, v in gf.DEFAULTS.items() if k != "aa"}, "scale": 4}
        game.call("graphics", **defaults)
        vsync = game.status()["vsync"]
        game.run(8 - vsync % 8)
        start = game.status()["vsync"]
        game.press("start", vsyncs=4, settle=0)

        # The opening card: 2D only.
        game.run(start + gf.OPENING_CARD - game.status()["vsync"])
        game.call("pause", paused=True)
        card = redrawn(game, out / "card.png", defaults, aa="off")
        save("card-off", card)
        for mode in CRISP:
            image = redrawn(game, out / "card.png", defaults, aa=mode)
            diff = int(np.abs(image - card).max())
            check(diff == 0, f"opening card (2D) unchanged under {mode} (max difference {diff})")
        game.call("graphics", **defaults, aa="off")
        game.call("pause", paused=False)

        # The first room.
        game.run(start + gf.FIRST_ROOM - game.status()["vsync"])
        x, y, z, yaw, pitch = POSE
        game.call("editor_camera", x=x, y=y, z=z, yaw=yaw, pitch=pitch)
        game.run(gf.SETTLE)
        game.call("pause", paused=True)
        depth = redrawn(game, out / "depth.png", defaults, aa="off", show_depth=True)
        normals = redrawn(game, out / "normals.png", defaults, aa="off", view_mode=5)
        game.call("graphics", show_depth=False, view_mode=0)
        off = redrawn(game, out / "off.png", defaults, aa="off")
        save("room-off", off)
        ui = (depth[..., 0] > 140) & (depth[..., 1] < 20) & (depth[..., 2] > 140)
        far = depth.max(axis=2) < 4
        solid = ~ui & ~far
        # Geometric edges: depth or normal breaks between neighbours.
        breaks = np.zeros(ui.shape, bool)
        for picture, step in ((depth, 6), (normals, 24)):
            p = picture.astype(np.int32)
            h = np.abs(np.diff(p, axis=1)).max(axis=2) > step
            v = np.abs(np.diff(p, axis=0)).max(axis=2) > step
            breaks[:, 1:] |= h
            breaks[:, :-1] |= h
            breaks[1:, :] |= v
            breaks[:-1, :] |= v
        edges = breaks & ~ui
        save("depth", depth)
        save("normals", normals)
        save("edges", np.repeat((edges * 255)[..., None], 3, axis=2))
        interior = solid & ~dilate(edges | ui | far, 4)
        print(f"masks: {edges.mean() * 100:.1f}% edge pixels, {interior.mean() * 100:.1f}% interior, "
              f"{ui.mean() * 100:.1f}% UI", flush=True)
        check(edges.mean() > 0.002 and interior.mean() > 0.2, "the scene has geometric edges and textured interior")
        base_energy = edge_energy(off, edges)
        for mode, limit in MODES.items():
            image = redrawn(game, out / f"{mode}.png", defaults, aa=mode)
            save(f"room-{mode.replace('+', '-')}", image)
            g = game.call("graphics")
            energy = edge_energy(image, edges) / base_energy
            check(energy < limit, f"{mode}: edge energy {energy:.3f} of off's (< {limit})")
            changed = (np.abs(image - off).max(axis=2) > 0) & interior
            blocks = changed.copy()  # 3 x 3 patches (a seam is 1-2 pixels wide in a magnified view)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    blocks &= np.roll(np.roll(changed, dy, 0), dx, 1)
            share, block_share = changed.sum() / interior.sum(), blocks.sum() / interior.sum()
            save(f"diff-{mode.replace('+', '-')}", np.stack([changed * 255, edges * 160, interior * 60], 2))
            if mode in CRISP:
                # Polygon seams inside a surface (two polygons' texels meeting) are edges to MSAA
                # too: thin lines may change; blurred texels would change in patches.
                check(share < 0.01 and block_share < 0.0002,
                      f"{mode}: texel interior kept ({share * 100:.3f}% of it changed, on seams; "
                      f"{block_share * 100:.4f}% in 3x3 patches)")
            else:
                print(f"     {mode}: {share * 100:.3f}% of the interior changed, {block_share * 100:.4f}% in 3x3 patches")
            if mode.startswith("msaa"):
                want = min(int(mode.split("+")[0][4:]), g["msaa_max"])
                check(g["msaa_active"] == want and want >= 2,
                      f"{mode}: drawn with {g['msaa_active']} samples (this GPU runs up to {g['msaa_max']})")

        # Supersampling: the internal resolution first (changing it drops the recorded frame).
        game.call("pause", paused=False)
        game.call("graphics", **defaults, aa="ssaa2")
        game.run(30)
        scale = game.call("graphics")["scale"]
        check(scale >= 6, f"ssaa2 raises the internal resolution (scale {scale})")
        game.call("graphics", **{**defaults, "scale": scale}, aa="off")
        game.run(30)
        game.call("pause", paused=True)
        off_big = redrawn(game, out / "off-big.png", {**defaults, "scale": scale}, aa="off")
        for filt in ("area", "lanczos"):
            image = redrawn(game, out / "ssaa.png", {**defaults, "scale": scale}, aa="ssaa2", aa_downsample=filt)
            save(f"room-ssaa2-{filt}", image)
            mean = float(np.abs(image - off_big).mean())
            check(mean < 3.0, f"ssaa2 ({filt}): close to off at {scale}x (mean difference {mean:.2f})")
        game.call("graphics", **defaults, aa="off", aa_downsample="area")
        game.call("editor_camera", off=1)
        game.call("pause", paused=False)

        # Interpolation with MSAA + SMAA: the opening pan still moves every presented image.
        game.call("graphics", **{**defaults, "aa": "msaa4+smaa", "interpolate": True})
        game.run(max(8, 2490 - game.status()["vsync"]))  # interpolation_smoke.py's pan
        smooth = burst(game, out / "interp")
        median = float(np.median(smooth))
        check(smooth.min() > 0.25 * median and median > 0.2,
              f"interpolated frames with msaa4+smaa move (differences {smooth.min():.2f}..{smooth.max():.2f})")
    finally:
        game.close()
    log = pathlib.Path(game.log).read_text(errors="replace")
    bad = [line for line in log.splitlines() if "validation:" in line or "[crash]" in line or "[gl] shader" in line
           or "SMAA unavailable" in line or "MSAA targets incomplete" in line or "no MSAA images" in line]
    check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))
    if failures:
        print(f"FAIL: {len(failures)} checks failed; log {game.log}")
        return 1
    print("PASS: anti-aliasing methods render, smooth edges, keep texels and 2D")
    return 0


if __name__ == "__main__":
    sys.exit(main())
