#!/usr/bin/env python3
"""Guess light sources from a room's baked vertex colours.

Vagrant Story bakes lighting into room vertex colours. Given a scene dump (RPC
`scene_dump`, an OBJ with per-vertex colours in world space), this fits a few
point lights that explain the colours:

    colour(v) ~ ambient + sum_k colour_k * |n . l_k| * falloff(|p_k - v| / radius_k)

with falloff(x) = (1 - x^2)^2 for x < 1. Lights are added greedily: seed at the
brightest unexplained hotspot (offset off the surface along its normal), fit
position, radius and colour with robust least squares, subtract, repeat while a
new light still explains a meaningful share of the remaining energy.

    estimate_lights.py scene.obj [-o lights.json] [--max-lights 8] [--preview out.png]

Output JSON: {"ambient": [r,g,b], "lights": [{"position": [x,y,z], "color": [r,g,b],
"radius": r, "explained": fraction}, ...]} with colours in vertex-colour units
(0-255, 128 is neutral).
"""
import argparse
import json
import sys

import numpy as np
from scipy.optimize import least_squares


def load(path):
    positions, colors, faces, flags = [], [], [], []
    for line in open(path):
        if line.startswith("v "):
            values = [float(x) for x in line.split()[1:7]]
            positions.append(values[:3])
            colors.append(values[3:6])
        elif line.startswith("f "):
            parts = line.split()
            faces.append([int(i) - 1 for i in parts[1:4]])
            flags.append(int(parts[parts.index("flags") + 1], 16) if "flags" in parts else 0)
    return np.array(positions), np.array(colors) * 255.0, np.array(faces), np.array(flags)


def samples(positions, colors, faces):
    """One sample per triangle vertex plus the centroid, with the face normal."""
    tri = positions[faces]  # (n, 3, 3)
    normals = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    length = np.linalg.norm(normals, axis=1)
    keep = length > 1e-6
    tri, normals, faces = tri[keep], normals[keep] / length[keep, None], faces[keep]
    area = 0.5 * length[keep]
    points = np.concatenate([tri[:, 0], tri[:, 1], tri[:, 2], tri.mean(axis=1)])
    cols = colors[faces]
    cols = np.concatenate([cols[:, 0], cols[:, 1], cols[:, 2], cols.mean(axis=1)])
    norms = np.concatenate([normals] * 4)
    weights = np.concatenate([area / 4] * 4)
    weights = np.sqrt(weights / weights.mean())  # big faces count more, not overwhelmingly
    return points, norms, cols, weights


def contribution(params, points, normals):
    position, log_radius, color = params[:3], params[3], params[4:7]
    radius = np.exp(log_radius)
    delta = position - points
    distance = np.linalg.norm(delta, axis=1) + 1e-6
    cosine = np.abs(np.einsum("ij,ij->i", normals, delta / distance[:, None]))
    x = np.clip(distance / radius, 0, 1)
    falloff = (1 - x * x) ** 2
    return (cosine * falloff)[:, None] * color[None, :]


def fit(points, normals, residual, weights, max_lights, min_explained, room_lo, room_hi):
    lights = []
    energy0 = float(np.sum(weights[:, None] * residual.clip(0) ** 2))
    # Neighbour smoothing makes the hotspot a region, not a single noisy vertex.
    luminance = residual @ np.array([0.299, 0.587, 0.114])
    for _ in range(max_lights):
        order = np.argsort(-luminance)[:256]
        smoothed = []
        for i in order:
            near = np.linalg.norm(points - points[i], axis=1) < 96
            smoothed.append(luminance[near].mean())
        seed = order[int(np.argmax(smoothed))]
        if luminance[seed] < 6:
            break
        start_color = np.clip(residual[seed], 1, None) * 1.5
        x0 = np.concatenate([points[seed] + normals[seed] * 64, [np.log(512.0)], start_color])
        near = np.linalg.norm(points - points[seed], axis=1) < 2048

        def cost(params):
            model = contribution(params, points[near], normals[near])
            return ((residual[near] - model) * weights[near, None]).ravel()

        # Lights stay inside the room's bounds (inset), never behind its walls.
        lower = np.concatenate([np.maximum(points[seed] - 1024, room_lo), [np.log(48.0)], [0, 0, 0]])
        upper = np.concatenate([np.minimum(points[seed] + 1024, room_hi), [np.log(4096.0)], [512, 512, 512]])
        x0[:3] = np.clip(x0[:3], lower[:3] + 1, upper[:3] - 1)
        result = least_squares(cost, x0, bounds=(lower, upper), loss="linear",
            max_nfev=400)
        model = contribution(result.x, points, normals)
        remaining = residual - model
        before = float(np.sum(weights[:, None] * residual.clip(0) ** 2))
        after = float(np.sum(weights[:, None] * remaining.clip(0) ** 2))
        explained = (before - after) / max(energy0, 1e-9)
        if explained < min_explained:
            break
        lights.append({
            "position": [round(float(v), 1) for v in result.x[:3]],
            "color": [round(float(v), 1) for v in result.x[4:7]],
            "radius": round(float(np.exp(result.x[3])), 1),
            "explained": round(explained, 4),
        })
        residual = remaining
        luminance = residual @ np.array([0.299, 0.587, 0.114])
    return lights, residual


def model_colors(positions, faces, ambient, lights):
    """Per-vertex colours predicted by the fitted lights (for comparison)."""
    tri = positions[faces]
    normals = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    normals /= np.linalg.norm(normals, axis=1)[:, None] + 1e-9
    out = np.zeros((len(faces), 3, 3))
    for j in range(3):
        c = np.tile(np.array(ambient), (len(faces), 1))
        for light in lights:
            params = np.concatenate([light["position"], [np.log(light["radius"])], light["color"]])
            c = c + contribution(params, tri[:, j], normals)
        out[:, j] = c
    return out


def preview(positions, colors, faces, lights, out):
    """Top-down (x, z) map of vertex colours with the fitted lights."""
    from PIL import Image, ImageDraw
    lo, hi = positions.min(axis=0), positions.max(axis=0)
    scale = 900 / max(hi[0] - lo[0], hi[2] - lo[2], 1)
    image = Image.new("RGB", (int((hi[0] - lo[0]) * scale) + 40, int((hi[2] - lo[2]) * scale) + 40))
    draw = ImageDraw.Draw(image)
    tri = positions[faces]
    order = np.argsort(-tri[:, :, 1].mean(axis=1))  # lowest (largest y) first: floors under walls
    for i in order:
        points = [((p[0] - lo[0]) * scale + 20, (p[2] - lo[2]) * scale + 20) for p in tri[i]]
        c = tuple(int(min(255, v * 1.6)) for v in colors[faces[i]].mean(axis=0))
        draw.polygon(points, fill=c)
    for light in lights:
        x, _, z = light["position"]
        cx, cy = (x - lo[0]) * scale + 20, (z - lo[2]) * scale + 20
        r = light["radius"] * scale
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 255, 0))
        draw.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=(255, 64, 64))
    image.save(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scene")
    parser.add_argument("-o", "--output")
    parser.add_argument("--max-lights", type=int, default=8)
    parser.add_argument("--min-explained", type=float, default=0.03)
    parser.add_argument("--preview")
    parser.add_argument("--rood", help="also write lights in the game's pc/lights format")
    args = parser.parse_args()
    positions, colors, faces, _ = load(args.scene)
    points, normals, cols, weights = samples(positions, colors, faces)
    lit = cols @ np.array([0.299, 0.587, 0.114]) > 4  # black faces are voids, not darkness
    ambient = np.percentile(cols[lit], 10, axis=0)
    room_lo, room_hi = positions.min(axis=0) + 16, positions.max(axis=0) - 16
    lights, residual = fit(points, normals, cols - ambient, weights, args.max_lights, args.min_explained,
        room_lo, room_hi)
    result = {"ambient": [round(float(v), 1) for v in ambient], "lights": lights,
              "unexplained_rms": round(float(np.sqrt(np.mean(residual.clip(0) ** 2))), 2)}
    text = json.dumps(result, indent=2)
    if args.output:
        open(args.output, "w").write(text + "\n")
    print(text)
    if args.rood:
        # Vertex colours modulate textures with 128 as neutral, so a fitted
        # colour c becomes light colour c / max(c) at intensity max(c) / 128.
        with open(args.rood, "w") as f:
            f.write("# suggested by estimate_lights.py from baked vertex colours\n")
            for light in lights:
                peak = max(max(light["color"]), 1e-6)
                c = [v / peak for v in light["color"]]
                x, y, z = light["position"]
                f.write(f"light point {x:.1f} {y:.1f} {z:.1f} {c[0]:.3f} {c[1]:.3f} {c[2]:.3f} "
                        f"{peak / 128:.3f} {light['radius']:.1f} 0 1 0 45 0 1\n")
    if args.preview:
        preview(positions, colors, faces, lights, args.preview)
        modelled = model_colors(positions, faces, result["ambient"], lights)
        flat = np.zeros_like(colors)
        for j in range(3):
            flat[faces[:, j]] = modelled[:, j]
        preview(positions, flat, faces, lights, args.preview.replace(".png", "-model.png"))


if __name__ == "__main__":
    sys.exit(main())
