#!/usr/bin/env python3
"""One room through Blender and back (Gitea #20; pc/docs/Materials.md, "Blender round trip").

    python3 pc/tools/room_blender.py export [--zone Z --room R] [--out DIR]
    python3 pc/tools/room_blender.py bake   [--out DIR] [--samples N]
    python3 pc/tools/room_blender.py import [--out DIR]
    python3 pc/tools/room_blender.py shot   [--out DIR]
    python3 pc/tools/room_blender.py all    [--zone Z --room R] [--out DIR]

export  The room the game is in (or RPC room_goto Z R) to glTF: the captured room geometry (RPC scene_dump: positions,
        baked vertex colours, each polygon's texels, texture page and CLUT, object group) with one material per
        texture (the game's own texture content key: RPC emissive uses, the texels as drawn: RPC material texture).
        The model is positioned in 1/512 game units, Y up, its triangles turned to face the room's middle (the room is
        open geometry with mixed winding: the occlusion and bevel passes need the side that faces in). Each texture
        is one primitive whose UVs run over the texture's own box (texel for texel); a texture used by more than one room
        group is one primitive per group (`<key>@g<block offset>`), baked on its own. export.json holds what comes back
        to the game: key, size, page, CLUT, box, and for the split ones the texture and object.
bake    Blender (blender -b, Cycles on the CPU, a modest sample count) runs pc/tools/blender/bake_room.py: each texture
        is baked texel for texel from the room's geometry: edge wear (a bevel of the geometry against its own faces),
        cavity (ambient occlusion) and which texels any triangle covers.
import  The bakes become maps in the baked materials layer (pc/build/materials-style layer under <out>/materials): height,
        roughness (and normal.png for other tools) at exactly the texture's box size, 8-bit grayscale PNG, derived by
        pc/tools/room_blender.json (the tunables). Texels no triangle covers keep the blank templates (height 128). A texture
        baked per room group has its maps in <key>/@g<offset>/ (materials by object: read for that group's entry only).
shot    The room in the game twice, with an entry for each baked texture (the "rough stone" preset) and the baked layer
        off, then on: before / after screenshots into <out>/shots.

Everything under <out> (default pc/build/room_blender/<zone>-<room>) is game art derived from the game: local only,
never committed. Only these tools are."""
import argparse
import base64
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
os.environ.setdefault("ROOD_ARCH", "64")  # the 32-bit build is retired: run by hand, these tools use the 64-bit game
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
CONFIG = ROOT / "pc/tools/room_blender.json"
BLENDER = "/usr/bin/blender"
SCALE = 1.0 / 512.0  # game units to glTF metres
FACE = re.compile(r"^f (\d+) (\d+) (\d+) # tpage ([0-9a-f]+) clut ([0-9a-f]+) group (\d+) flags ([0-9a-f]+) "
                  r"uv (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) code ([0-9a-f]+) quad (\d+)(?: polygon ([0-9a-f]+))?")


def config():
    return json.loads(CONFIG.read_text())


def room_dir(args):
    if args.out:
        return pathlib.Path(args.out)
    name = f"{args.zone}-{args.room}" if args.zone is not None else "current"
    return ROOT / "pc/build/room_blender" / name


# ---------------------------------------------------------------------------------------------- the dump

def parse_dump(path):
    """(positions[n,3,3] float, colours[n,3,3] float 0..1, triangles: dicts of tpage, clut, group, flags, uv[3][2])."""
    positions, colours, tris = [], [], []
    verts = []
    for line in pathlib.Path(path).read_text().splitlines():
        if line.startswith("v "):
            p = line.split()
            verts.append([float(x) for x in p[1:7]])
        elif line.startswith("f "):
            m = FACE.match(line)
            if not m:
                continue
            a, b, c = (int(m.group(k)) - 1 for k in (1, 2, 3))
            tris.append({"tpage": int(m.group(4), 16), "clut": int(m.group(5), 16), "group": int(m.group(6)),
                         "flags": int(m.group(7), 16), "uv": [[int(m.group(8)), int(m.group(9))],
                                                              [int(m.group(10)), int(m.group(11))],
                                                              [int(m.group(12)), int(m.group(13))]],
                         "code": int(m.group(14), 16), "quad": int(m.group(15)),
                         "polygon": int(m.group(16), 16) if m.group(16) else None})
            positions.append([verts[a][0:3], verts[b][0:3], verts[c][0:3]])
            colours.append([verts[a][3:6], verts[b][3:6], verts[c][3:6]])
    return np.array(positions, np.float32), np.array(colours, np.float32), tris


def assign(tris, uses):
    """Each triangle's texture: the use on its page and CLUT whose box holds its texels (the smallest such box)."""
    by = {}
    for u in uses:
        by.setdefault((u["tpage"] & 31, u["clut"]), []).append(u)  # (the page, not its colour depth: a model's polygons carry the depth bits)
    out = []
    for t in tris:
        us = [p[0] for p in t["uv"]], [p[1] for p in t["uv"]]
        best, area = None, None
        for u in by.get((t["tpage"] & 31, t["clut"]), []):
            u0, v0, u1, v1 = u["box"]
            if min(us[0]) >= u0 and max(us[0]) <= u1 + 1 and min(us[1]) >= v0 and max(us[1]) <= v1 + 1:
                a = (u1 - u0 + 1) * (v1 - v0 + 1)
                if area is None or a < area:
                    best, area = u, a
        out.append(best)
    return out


def object_of(tri, groups):
    """The room group (its block's byte offset in the room data) a triangle's polygon lies in: `groups` from RPC material
    groups ({offset, first, end}); None where the dump has no polygon offsets or none holds it."""
    p = tri.get("polygon")
    if p is None:
        return None
    for g in groups:
        if g["first"] <= p < g["end"]:
            return g["offset"]
    return None


def split_shared(tris, found, groups, textures, always=False):
    """Per triangle its bake label and the textures with the split ones added. A texture used by more than one room group
    is baked once per group (`<key>@g<offset hex>`: the same texture, the group's own triangles), so the texels they share
    stop taking whichever baked last; the rest keep their key. The split entries say which texture and object they are."""
    seen = {}
    for t, u in zip(tris, found):
        if u:
            seen.setdefault(u["key"], set()).add(object_of(t, groups))
    shared = set(seen) if always else {k for k, o in seen.items() if len(o - {None}) > 1}  # (always: every object owns its own)
    labels, out = [], dict(textures)
    for t, u in zip(tris, found):
        if not u:
            labels.append("unmapped")
        elif u["key"] in shared and object_of(t, groups) is not None:
            off = object_of(t, groups)
            label = f"{u['key']}@g{off:x}"
            if label not in out and u["key"] in textures:
                out[label] = {**textures[u["key"]], "texture": u["key"], "object": f"g{off:x}", "group_offset": off}
            labels.append(label)
        else:
            labels.append(u["key"])
    return labels, out


def face_inward(positions):
    """Winding that faces the room's middle (the capture's own is mixed); returns the flip mask."""
    centre = positions.reshape(-1, 3).mean(axis=0)
    a, b, c = positions[:, 0], positions[:, 1], positions[:, 2]
    n = np.cross(b - a, c - a)
    inward = centre - positions.mean(axis=1)
    return (n * inward).sum(axis=1) < 0


def face_outward(positions):
    """Winding that faces away from the model's own surface (a character, weapon or prop is drawn without culling: its winding is
    mixed); returns the flip mask. Triangles are welded by position; across each edge two triangles share, the winding is made
    consistent (the one a flood fill from a triangle sets), and each connected piece then faces outward as a whole: the side its
    area-weighted normals leave the piece's middle by (the divergence: the signed volume of a closed piece)."""
    n = len(positions)
    ids = {}
    corner = np.array([[ids.setdefault(tuple(np.round(p * 64.0).astype(np.int64)), len(ids)) for p in tri] for tri in positions])
    edges = {}
    for t in range(n):
        for k in range(3):
            a, b = corner[t][k], corner[t][(k + 1) % 3]
            if a != b:
                edges.setdefault((min(a, b), max(a, b)), []).append((t, a < b))
    near = [[] for _ in range(n)]  # (triangle, the flip the neighbour needs to wind the same way across the shared edge)
    for sides in edges.values():
        if len(sides) == 2:
            (t0, f0), (t1, f1) = sides
            near[t0].append((t1, f0 == f1))
            near[t1].append((t0, f0 == f1))
    flip = np.zeros(n, bool)
    piece = np.full(n, -1)
    pieces = 0
    for start in range(n):
        if piece[start] >= 0:
            continue
        piece[start] = pieces
        stack = [start]
        while stack:
            t = stack.pop()
            for u, differs in near[t]:
                if piece[u] < 0:
                    piece[u] = pieces
                    flip[u] = flip[t] ^ differs
                    stack.append(u)
        pieces += 1
    a, b, c = positions[:, 0], positions[:, 1], positions[:, 2]
    area_normal = np.cross(b - a, c - a) * np.where(flip, -1.0, 1.0)[:, None]  # (twice the area)
    middle = positions.mean(axis=1)
    for q in range(pieces):
        mine = piece == q
        centre = middle[mine].mean(axis=0)
        if ((middle[mine] - centre) * area_normal[mine]).sum() < 0:
            flip[mine] = ~flip[mine]
    return flip


# ---------------------------------------------------------------------------------------------- export

def goto(game, zone, room, enemies=False):
    import material_rollout
    if not material_rollout.goto(game, zone, room, enemies=enemies):
        raise SystemExit(f"room_blender: the game did not arrive in zone {zone} room {room}")


def export(args):
    import material_rollout
    out = room_dir(args)
    shutil.rmtree(out / "gltf", ignore_errors=True)
    (out / "gltf" / "tex").mkdir(parents=True)
    scratch = out / "scratch"
    scratch.mkdir(parents=True, exist_ok=True)
    margs = argparse.Namespace(card=args.card)
    game = material_rollout.launch(scratch, margs)
    try:
        if args.zone is not None:
            goto(game, args.zone, args.room)
        game.run(30)
        dump = out / "scene.obj"
        game.call("scene_dump", path=str(dump))
        uses = game.call("emissive", action="uses", keys=True)["uses"]
        positions, colours, tris = parse_dump(dump)
        found = assign(tris, uses)
        keys = {}
        for t, u in zip(tris, found):
            if u:
                keys.setdefault(u["key"], u)
        groups = game.call("material", action="groups")["groups"]
        textures = {}
        for key, u in keys.items():
            got = game.call("material", action="texture", key=key)
            w, h = int(got["w"]), int(got["h"])
            Image.frombytes("RGBA", (w, h), base64.b64decode(got["rgba"])).save(out / "gltf" / "tex" / f"{key}.png")
            textures[key] = {"w": w, "h": h, "box": u["box"], "tpage": u["tpage"], "clut": u["clut"]}
        state = game.state()
    finally:
        material_rollout.kill(game)
    labels, textures = split_shared(tris, found, groups, textures)
    write_gltf(out, positions, colours, tris, found, textures, labels)
    meta = {"zone": state["zone"], "room": state["room"], "triangles": len(tris),
            "unassigned": sum(1 for u in found if not u), "textures": textures,
            "scale": SCALE}
    (out / "export.json").write_text(json.dumps(meta, indent=1))
    print(f"export: zone {state['zone']} room {state['room']}: {len(tris)} triangles, {len(textures)} textures "
          f"({sum(1 for t in textures.values() if 'object' in t)} of them per room group), "
          f"{meta['unassigned']} triangles with no texture box -> {out / 'gltf' / 'room.gltf'}")
    return 0


def write_gltf(out, positions, colours, tris, found, textures, labels=None, pbr=None, folder="gltf", name="room", outward=False):
    """`pbr` ({label: {channel: uri}}): the painting round trip's glTF (pc/tools/paint_roundtrip.py): each label's material takes its own
    images (basecolor, metallicroughness, normal, occlusion) in place of the shared texture's. `outward`: a model's faces point away from
    its surface (a room's face the room's middle)."""
    flip = face_outward(positions) if outward else face_inward(positions)
    # glTF: x right, y up, z towards the viewer; the game's y is down and z away: a half turn about x
    pos = positions.copy()
    pos[..., 1] *= -1
    pos[..., 2] *= -1
    pos *= SCALE
    order = np.array([[0, 1, 2]] * len(tris))
    order[flip] = [0, 2, 1]
    groups = {}
    for i, u in enumerate(found):
        groups.setdefault(labels[i] if labels else u["key"] if u else "unmapped", []).append(i)
    buffer = bytearray()
    views, accessors, primitives, materials, images, textures_json = [], [], [], [], [], []
    def add(data, target, kind, ctype, count, mins=None, maxs=None):
        while len(buffer) % 4:
            buffer.append(0)
        views.append({"buffer": 0, "byteOffset": len(buffer), "byteLength": len(data), "target": target})
        buffer.extend(data)
        a = {"bufferView": len(views) - 1, "componentType": ctype, "count": count, "type": kind}
        if mins is not None:
            a["min"], a["max"] = mins, maxs
        accessors.append(a)
        return len(accessors) - 1
    for gi, (key, idx) in enumerate(sorted(groups.items())):
        p = np.stack([pos[i][order[i]] for i in idx]).reshape(-1, 3).astype(np.float32)
        c = np.stack([colours[i][order[i]] for i in idx]).reshape(-1, 3).astype(np.float32)
        t = textures.get(key)
        uv = np.zeros((len(idx) * 3, 2), np.float32)
        if t:
            u0, v0 = t["box"][0], t["box"][1]
            for n, i in enumerate(idx):
                for k in range(3):
                    s = tris[i]["uv"][order[i][k]]
                    uv[n * 3 + k] = ((s[0] - u0) / t["w"], (s[1] - v0) / t["h"])
        attrs = {"POSITION": add(p.tobytes(), 34962, "VEC3", 5126, len(p), p.min(axis=0).tolist(), p.max(axis=0).tolist()),
                 "TEXCOORD_0": add(uv.tobytes(), 34962, "VEC2", 5126, len(uv)),
                 "COLOR_0": add(c.tobytes(), 34962, "VEC3", 5126, len(c))}
        if pbr is not None and key in pbr:
            files = pbr[key]
            material = {"name": files["name"], "pbrMetallicRoughness": {"metallicFactor": 1.0, "roughnessFactor": 1.0}}
            for channel, slot in (("basecolor", "baseColorTexture"), ("metallicroughness", "metallicRoughnessTexture"),
                                  ("normal", "normalTexture"), ("occlusion", "occlusionTexture")):
                if channel not in files:
                    continue
                images.append({"uri": files[channel]})
                textures_json.append({"source": len(images) - 1, "sampler": 0})
                ref = {"index": len(textures_json) - 1}
                (material["pbrMetallicRoughness"] if slot in ("baseColorTexture", "metallicRoughnessTexture") else material)[slot] = ref
            material["extras"] = {"key": key, **({"w": t["w"], "h": t["h"]} if t else {})}
            materials.append(material)
        else:
            materials.append({"name": key, "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.8,
                              **({"baseColorTexture": {"index": len(textures_json)}} if t else {})},
                              "extras": {"key": key, **({"w": t["w"], "h": t["h"]} if t else {})}})
            if t:
                images.append({"uri": f"tex/{t.get('texture', key)}.png"})
                textures_json.append({"source": len(images) - 1, "sampler": 0})
        primitives.append({"attributes": attrs, "material": len(materials) - 1, "mode": 4,
                           "extras": {"key": key, "triangles": [{"group": tris[i]["group"], "flags": tris[i]["flags"],
                                                                 "tpage": tris[i]["tpage"], "clut": tris[i]["clut"]}
                                                                for i in idx]}})
    (out / folder / f"{name}.bin").write_bytes(bytes(buffer))
    gltf = {"asset": {"version": "2.0", "generator": "rood-boi room_blender.py"},
            "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
            "meshes": [{"name": name, "primitives": primitives}], "materials": materials,
            "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33071}],
            "images": images, "textures": textures_json, "accessors": accessors, "bufferViews": views,
            "buffers": [{"uri": f"{name}.bin", "byteLength": len(buffer)}]}
    (out / folder / f"{name}.gltf").write_text(json.dumps(gltf))


# ---------------------------------------------------------------------------------------------- bake

def run_bake(gltf, meta, bakes, samples, threads, params):
    """Blender (pc/tools/blender/bake_room.py) bakes each texture of `gltf` (the materials `meta` ({"textures": {name: {w, h}}}) names) into
    <bakes>/<name>/{cover,wear,cavity}.png; the exit status."""
    shutil.rmtree(bakes, ignore_errors=True)
    bakes.mkdir(parents=True)
    cmd = ["nice", "-n", "19", BLENDER, "-b", "--threads", str(threads), "-P", str(ROOT / "pc/tools/blender/bake_room.py"),
           "--", str(gltf), str(meta), str(bakes), str(samples), json.dumps(params)]
    done = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write("".join(l + "\n" for l in done.stdout.splitlines() if l.startswith("bake_room:")))
    if done.returncode:
        sys.stderr.write(done.stdout[-3000:] + done.stderr[-2000:])
    return done.returncode


def bake(args):
    out = room_dir(args)
    meta = json.loads((out / "export.json").read_text())
    cfg = config()
    code = run_bake(out / "gltf" / "room.gltf", out / "export.json", out / "bakes", args.samples or cfg["samples"], args.threads, cfg["bake"])
    if not code:
        print(f"bake: {len(list((out / 'bakes').glob('*/wear.png')))} of {len(meta['textures'])} textures baked -> {out / 'bakes'}")
    return code


# ---------------------------------------------------------------------------------------------- import

def gray(path):
    return np.asarray(Image.open(path).convert("L"), np.float32) / 255.0


def maps(wear, cavity, cover, p):
    """height, roughness and normal (uint8 arrays) from the baked wear and cavity, blank where nothing is baked."""
    height = p["height_base"] + p["height_wear"] * wear + p["height_cavity"] * cavity
    rough = (p["rough_base"] + p["rough_wear"] * wear + p["rough_cavity"] * cavity) * 255.0
    height = np.where(cover > 0.5, height, 128.0)
    rough = np.where(cover > 0.5, rough, p["rough_base"] * 255.0)
    height = np.clip(height, 0, 255)
    padded = np.pad(height / 255.0, 1, mode="edge")  # (a texture one texel wide has neighbours too: its own edge)
    gx = (padded[1:-1, 2:] - padded[1:-1, :-2]) * 0.5
    gy = (padded[2:, 1:-1] - padded[:-2, 1:-1]) * 0.5
    n = np.stack([-gx * p["normal_strength"], -gy * p["normal_strength"], np.ones_like(gx)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    normal = ((n * 0.5 + 0.5) * 255.0 + 0.5).astype(np.uint8)
    return (np.clip(height + 0.5, 0, 255).astype(np.uint8), np.clip(rough + 0.5, 0, 255).astype(np.uint8), normal)


def window(x, lo, hi, soft):
    """1 inside [lo, hi], falling to 0 over `soft` outside it."""
    return np.clip((x - (lo - soft)) / max(soft, 1e-6), 0, 1) * np.clip(((hi + soft) - x) / max(soft, 1e-6), 0, 1)


def hue_degrees(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx, mn = rgb.max(-1), rgb.min(-1)
    d = np.maximum(mx - mn, 1e-6)
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60.0
    return h


def classify_metal(rgba, cfg):
    """(metal 0..1, the metal's own roughness; nan where none) per texel from the texture's colours; zero for a texture that has too
    little or too much that looks like metal (room_blender.json "metal")."""
    rgb = rgba[..., :3].astype(np.float32) / 255.0
    opaque = rgba[..., 3] > 127
    mx, mn = rgb.max(-1), rgb.min(-1)
    sat = np.where(mx > 1e-6, (mx - mn) / np.maximum(mx, 1e-6), 0.0)
    lum = rgb @ np.array([0.299, 0.587, 0.114], np.float32)
    hue = hue_degrees(rgb)
    metal = np.zeros(sat.shape, np.float32)
    rough = np.full(sat.shape, np.nan, np.float32)
    for m in cfg["materials"]:
        soft = m["soft"]
        rule = m.get("neutral")
        if rule:  # the greys of a texture that has colour too
            coloured = float(((sat >= rule["colour_sat"]) & opaque).sum()) / max(1, int(opaque.sum()))
            if coloured < rule["colour_share"]:
                continue
            w = window(sat, 0.0, rule["sat_max"], soft["sat"]) * window(lum, *m["lum"], soft["lum"]) * m["metal"]
            better = w > metal
            metal = np.where(better, w, metal)
            rough = np.where(better, m["rough"], rough)
            continue
        lo, hi = m["hue"]
        centre, half = (lo + hi) / 2.0, (hi - lo) / 2.0
        distance = np.abs((hue - centre + 180.0) % 360.0 - 180.0)  # (the hue circle)
        h = np.clip((half + soft["hue"] - distance) / max(soft["hue"], 1e-6), 0, 1)
        h = np.where(sat < 0.02, 1.0, h)  # (a true grey has no hue)
        w = h * window(sat, *m["sat"], soft["sat"]) * window(lum, *m["lum"], soft["lum"]) * m["metal"]
        better = w > metal
        metal = np.where(better, w, metal)
        rough = np.where(better, m["rough"], rough)
    metal = np.where(opaque & (metal >= cfg.get("floor", 0.0)), metal, 0.0)  # (the soft edge of a window is not a metal)
    hit = metal > 0.5
    if opaque.any():
        share = hit.sum() / float(opaque.sum())
        if share < cfg["min_share"] or share > cfg["max_share"]:
            return np.zeros_like(metal), rough
    n = cfg.get("despeckle", 0)
    if n > 0:  # a metal texel needs `n` metal texels among its eight neighbours (a gate's bar, not a speckle)
        padded = np.pad(hit.astype(np.int32), 1)
        around = sum(padded[1 + dy:padded.shape[0] - 1 + dy, 1 + dx:padded.shape[1] - 1 + dx]
                     for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dx or dy)
        metal = np.where(hit & (around < n), 0.0, metal)
    area = cfg.get("min_area", 0)
    if area > 1:  # a piece of metal is a bar or a band, not a few texels: connected pieces (four ways) under `min_area` texels go
        hit = metal > 0.5
        seen = np.zeros(hit.shape, bool)
        for y0, x0 in zip(*np.nonzero(hit)):
            if seen[y0, x0]:
                continue
            stack, piece = [(y0, x0)], []
            seen[y0, x0] = True
            while stack:
                y, x = stack.pop()
                piece.append((y, x))
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < hit.shape[0] and 0 <= xx < hit.shape[1] and hit[yy, xx] and not seen[yy, xx]:
                        seen[yy, xx] = True
                        stack.append((yy, xx))
            if len(piece) < area:
                for y, x in piece:
                    metal[y, x] = 0.0
    return metal, rough


def metal_maps(rgba, wear, cavity, cover, rough, cfg):
    """(metal uint8 or None when the texture has none, roughness uint8 with the metal's own roughness where it is metal): polished at
    worn edges, dull and dirty in crevices."""
    metal, own = classify_metal(rgba, cfg)
    if metal.max() < 0.05:
        return None, rough
    metal = np.clip(metal * (1.0 + cfg["wear_boost"] * wear - cfg["cavity_drop"] * cavity), 0.0, 1.0)
    shaped = np.clip((np.nan_to_num(own, nan=0.5) + cfg["rough_wear"] * wear + cfg["rough_cavity"] * cavity) * 255.0, 0, 255)
    blend = np.clip(metal / 0.5, 0.0, 1.0)  # (a metal texel's roughness is the metal's; between, the surface's)
    rough = np.clip(rough.astype(np.float32) * (1.0 - blend) + shaped * blend + 0.5, 0, 255).astype(np.uint8)
    return np.clip(metal * 255.0 + 0.5, 0, 255).astype(np.uint8), rough


def map_dir(layer, key, t):
    """Where a baked texture's maps go: <layer>/<key>/ (the texture's), or <layer>/<key>/@<object>/ for a texture baked per
    room group (the game reads the object's maps for an entry keyed by that object: materials.h channel)."""
    return layer / t["texture"] / f"@{t['object']}" if "object" in t else layer / key


def import_maps(args):
    out = room_dir(args)
    meta = json.loads((out / "export.json").read_text())
    p = config()["maps"]
    layer = out / "materials"
    shutil.rmtree(layer, ignore_errors=True)
    done = metals = 0
    for key, t in meta["textures"].items():
        d = out / "bakes" / key
        if not (d / "wear.png").exists():
            continue
        dest = map_dir(layer, key, t)
        wear, cavity, cover = gray(d / "wear.png"), gray(d / "cavity.png"), gray(d / "cover.png")
        if wear.shape != (t["h"], t["w"]):
            raise SystemExit(f"room_blender: {key}: the bake is {wear.shape[::-1]}, the texture {t['w']} x {t['h']}")
        height, rough, normal = maps(wear, cavity, cover, p)
        cfg = config().get("metal", {})
        picture = out / "gltf" / "tex" / f"{t.get('texture', key)}.png"
        metal = None
        if cfg.get("enabled") and picture.exists():
            rgba = np.asarray(Image.open(picture).convert("RGBA"))
            if rgba.shape[:2] == wear.shape:
                metal, rough = metal_maps(rgba, wear, cavity, cover, rough, cfg)
        dest.mkdir(parents=True, exist_ok=True)
        if metal is not None:
            Image.fromarray(metal, "L").save(dest / "metal.png")
            metals += 1
        Image.fromarray(height, "L").save(dest / "height.png")
        Image.fromarray(rough, "L").save(dest / "roughness.png")
        Image.fromarray(normal, "RGB").save(dest / "normal.png")
        done += 1
    print(f"import: {done} textures' height, roughness and normal maps, {metals} of them with a metal map -> {layer} (the baked layer: $ROOD_MATERIALS_BAKED)")
    return 0


# ---------------------------------------------------------------------------------------------- shot

def side_by_side(before, after, path):
    """The two pictures side by side (a black gap between) in one PNG."""
    both = Image.new("RGB", (before.width * 2 + 10, before.height), "black")
    both.paste(before, (0, 0))
    both.paste(after, (before.width + 10, 0))
    both.save(path)


def shot(args):
    import material_rollout
    out = room_dir(args)
    meta = json.loads((out / "export.json").read_text())
    shots = out / "shots"
    shutil.rmtree(shots, ignore_errors=True)
    shots.mkdir(parents=True)
    zr = [meta["zone"], meta["room"]]
    entries = []
    for n, name in enumerate(sorted(k for k, t in meta["textures"].items() if (map_dir(out / "materials", k, t) / "height.png").exists())):
        t = meta["textures"][name]
        e = {"id": f"RB{n:03d}", "name": f"room_blender {name}",
             "match": {"key": t.get("texture", name), "size": [t["w"], t["h"]]}, "preset": args.preset}
        if "object" in t:  # a texture shared by room groups: an entry for each group (its own maps)
            e["object"] = {"groups": [t["group_offset"]]}
            e["scope"] = {"room": zr}
        entries.append(e)
    # plain: the game as it is (no entry); before: the preset on every baked texture, the relief the game derives from the
    # texture itself; after: the same entries with the maps the round trip made
    for name, with_entries, baked in (("plain", False, False), ("before", True, False), ("after", True, True)):
        scratch = out / f"shot-{name}"
        shutil.rmtree(scratch, ignore_errors=True)
        scratch.mkdir(parents=True)
        (scratch / "materials.json").write_text(json.dumps({"format": 2, "presets": {},
                                                            "entries": entries if with_entries else []}))
        margs = argparse.Namespace(card=args.card)
        os.environ["ROOD_MATERIALS_BAKED"] = str(out / "materials" if baked else scratch / "no-maps")
        os.environ["ROOD_MATERIAL_CATALOG_USER"] = str(scratch / "materials.json")
        game = material_rollout.launch(scratch, margs)
        try:
            if args.go:
                goto(game, meta["zone"], meta["room"])
            game.run(60)
            if args.camera:
                game.call("editor_camera", **json.loads(args.camera))
                game.run(30)
            path = game.present_screenshot(str(shots / f"{name}.ppm"))
            Image.open(path).convert("RGB").save(shots / f"{name}.png")
            if args.light_at:
                # a light beside the picked spot, on two sides: the highlights should move with it
                x, y = (int(v) for v in args.light_at.split(","))
                at = game.call("material", action="pick", x=x, y=y)["world"]
                for side, dx in (("a", -260), ("b", 260)):
                    game.call("lights", action="reload")
                    game.call("lights", action="add", position=[at[0] + dx, at[1] - 150, at[2] + 60])
                    game.run(20)
                    path = game.present_screenshot(str(shots / f"{name}-lit-{side}.ppm"))
                    Image.open(path).convert("RGB").save(shots / f"{name}-lit-{side}.png")
                    game.call("graphics", view_mode=7)  # the specular lobe alone: where the highlights are
                    game.run(4)
                    path = game.present_screenshot(str(shots / f"{name}-spec-{side}.ppm"))
                    Image.open(path).convert("RGB").save(shots / f"{name}-spec-{side}.png")
                    game.call("graphics", view_mode=0)
                game.call("lights", action="reload")
        finally:
            material_rollout.kill(game)
    before = Image.open(shots / "before.png")
    side_by_side(before, Image.open(shots / "after.png"), shots / "before_after.png")
    if args.light_at:
        grid = Image.new("RGB", (before.width * 2 + 10, before.height * 2 + 10), "black")
        for row, name in enumerate(("before", "after")):
            for col, side in enumerate(("a", "b")):
                grid.paste(Image.open(shots / f"{name}-lit-{side}.png").convert("RGB"), (col * (before.width + 10), row * (before.height + 10)))
        grid.save(shots / "highlights.png")
    print(f"shot: {shots} (plain, before, after, before_after.png)")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["export", "bake", "import", "shot", "all"])
    parser.add_argument("--zone", type=int)
    parser.add_argument("--room", type=int)
    parser.add_argument("--out")
    parser.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--samples", type=int, help="Cycles samples per bake (default: room_blender.json)")
    parser.add_argument("--threads", type=int, default=4, help="Blender's threads (default 4)")
    parser.add_argument("--preset", default="rough stone", help="shot: the material preset given to every baked texture")
    parser.add_argument("--go", action="store_true", help="shot: warp to the exported room first")
    parser.add_argument("--camera", help="shot: RPC editor_camera parameters (JSON)")
    parser.add_argument("--light-at", help="shot: X,Y a pixel of the view: a light is put beside what is there, on one side and the other (highlights.png: before above, after below)")
    args = parser.parse_args()
    if (args.zone is None) != (args.room is None):
        parser.error("--zone and --room go together")
    steps = ["export", "bake", "import", "shot"] if args.command == "all" else [args.command]
    for step in steps:
        code = {"export": export, "bake": bake, "import": import_maps, "shot": shot}[step](args)
        if code:
            return code
    return 0


if __name__ == "__main__":
    sys.exit(main())
