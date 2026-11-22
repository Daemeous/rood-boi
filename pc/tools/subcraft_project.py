#!/usr/bin/env python3
"""A SubCraft project (paint.subcraft) made from a paint_roundtrip.py package, so the painter opens with the game's texture already
there instead of an empty stack (Gitea #20).

    python3 pc/tools/subcraft_project.py PACKAGE_DIR [--out FILE]

The project is SubCraft's own format (a zip: manifest.json, document.json, mesh.bin, resources/): the package's mesh with one texture set
per material (named as the material, at the smallest power of two that holds the texture, albedo filter *nearest* so the colour is
pixelated like the game's), and one Fill layer at the bottom of each set (UV projection) whose channels are the package's seed images:
base colour, roughness, metal, normal and occlusion. What a painter exports is then the seed with the paint on top, so unpainted
texels import unchanged. The format is SubCraft's schema 18 (SubCraft 0.4.5): any other version is refused with an explanation (the
caller then falls back to opening paint.gltf), because a project this writer got wrong would open broken or not at all."""
import argparse
import io
import json
import pathlib
import re
import struct
import subprocess
import sys
import uuid
import zipfile

import numpy as np
from PIL import Image

# the SubCraft versions this writer was made against, and the project schema each reads and writes (crates/project schema.rs)
KNOWN = {"0.4.5": 18, "0.5.0": 18}  # 0.5.0: schema unchanged (only a tile-sharing change in decoding)
SCHEMA = 18
NAMESPACE = uuid.UUID("2f1c3a5e-6d2b-4b8e-9a52-5e0c7c1d9a10")
BLEND = [("base_color", "normal"), ("height", "normal"), ("roughness", "normal"), ("metallic", "normal"),
         ("normal", "normal_map_combine"), ("ambient_occlusion", "normal")]
CHANNELS = [("base_color", "bit8"), ("height", "float16"), ("roughness", "bit8"), ("metallic", "bit8"), ("normal", "float16"),
            ("ambient_occlusion", "bit8")]
COMPONENT = {5126: ("f", 4), 5125: ("I", 4), 5123: ("H", 2), 5121: ("B", 1)}
SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


class FormatMismatch(Exception):
    """SubCraft on this machine is not a version whose project format this writer knows."""


def subcraft_version(command="subcraft"):
    out = subprocess.run([command, "--version"], capture_output=True, text=True, timeout=30).stdout
    found = re.search(r"SubCraft (\d+\.\d+\.\d+)", out)
    return found.group(1) if found else None


def check_version(version):
    if version not in KNOWN:
        raise FormatMismatch(
            f"SubCraft {version or '(unknown version)'} is not one whose project format this writer knows (known: "
            f"{', '.join(f'{v} = schema {s}' for v, s in KNOWN.items())}). Not writing a project that may open broken: "
            f"open paint.gltf and add the base colour by hand (docs: Materials.md, painting in SubCraft), or check "
            f"the schema in SubCraft's crates/project/src/schema.rs and add the version to KNOWN in subcraft_project.py.")


def accessor(gltf, blob, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    fmt, size = COMPONENT[acc["componentType"]]
    n = SIZES[acc["type"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    values = np.frombuffer(blob, dtype=np.dtype(fmt).newbyteorder("<"), count=acc["count"] * n, offset=start)
    return values.reshape(acc["count"], n)


def mesh_blob(package):
    """SubCraft's mesh blob (serialize.rs, version 2) of the package's glTF: un-indexed corners, flat normals, tangents from the UVs,
    one submesh per material, UVs as the file has them (its glTF importer does not flip them)."""
    gltf = json.loads((package / "paint.gltf").read_text())
    blob = (package / gltf["buffers"][0]["uri"]).read_bytes()
    materials = [m["name"] for m in gltf["materials"]]
    pos, nrm, uvs, tan, tri_material, submeshes = [], [], [], [], [], []
    for prim in gltf["meshes"][0]["primitives"]:
        p = accessor(gltf, blob, prim["attributes"]["POSITION"]).astype(np.float32)
        uv = accessor(gltf, blob, prim["attributes"]["TEXCOORD_0"]).astype(np.float32)
        if "indices" in prim:
            idx = accessor(gltf, blob, prim["indices"]).reshape(-1).astype(np.int64)
            p, uv = p[idx], uv[idx]
        tris = len(p) // 3
        a, b, c = p[0::3], p[1::3], p[2::3]
        n = np.cross(b - a, c - a)
        n = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        gl = np.stack([uv[:, 0], 1.0 - uv[:, 1]], axis=1)  # (SubCraft's tangents use v up, whatever its UVs say)
        e1, e2, u1, u2 = b - a, c - a, gl[1::3] - gl[0::3], gl[2::3] - gl[0::3]
        det = u1[:, 0] * u2[:, 1] - u2[:, 0] * u1[:, 1]
        det = np.where(np.abs(det) < 1e-12, 1e-12, det)
        t = (e1 * u2[:, 1:2] - e2 * u1[:, 1:2]) / det[:, None]
        b_ = (e2 * u1[:, 0:1] - e1 * u2[:, 0:1]) / det[:, None]
        t = t - n * (n * t).sum(1, keepdims=True)
        t = t / np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-12)
        w = np.where((np.cross(n, t) * b_).sum(1) < 0, -1.0, 1.0)
        slot = prim["material"]
        submeshes.append((slot, len(tri_material), tris))
        tri_material.extend([slot] * tris)
        pos.append(p)
        nrm.append(np.repeat(n, 3, axis=0))
        uvs.append(uv)
        tan.append(np.repeat(np.concatenate([t, w[:, None]], axis=1), 3, axis=0))
    pos, nrm, uvs, tan = (np.concatenate(x).astype("<f4") for x in (pos, nrm, uvs, tan))
    out = io.BytesIO()
    u32 = lambda v: out.write(struct.pack("<I", v))  # noqa: E731

    def text(s):
        u32(len(s.encode()))
        out.write(s.encode())

    def floats(a):
        u32(a.size)
        out.write(a.tobytes())
    out.write(b"SCMESH" + struct.pack("<H", 2))
    text("paint")
    u32(len(materials))
    for m in materials:
        text(m)
    for a in (pos, nrm, uvs, tan):
        floats(a)
    u32(len(pos))
    out.write(np.arange(len(pos), dtype="<u4").tobytes())
    u32(len(tri_material))
    out.write(np.asarray(tri_material, dtype="<u4").tobytes())
    u32(len(submeshes))
    for s in submeshes:
        out.write(struct.pack("<III", *s))
    u32(0)  # no objects
    u32(0)  # no tri_object
    return out.getvalue(), materials


def png(array):
    buf = io.BytesIO()
    Image.fromarray(array).save(buf, "PNG")
    return buf.getvalue()


def resolution(w, h):
    size = 128
    while size < max(w, h) and size < 4096:
        size *= 2
    return size


def build(package, out, version=None):
    """Writes the project; `version` is SubCraft's (None: asked from the `subcraft` on the PATH). Raises FormatMismatch."""
    package = pathlib.Path(package)
    check_version(version if version is not None else subcraft_version())
    seed = json.loads((package / "seed.json").read_text())
    labels = {k.replace("@", "_"): v for k, v in seed["labels"].items()}  # (the material is named as paint_roundtrip.safe() has it)
    mesh, materials = mesh_blob(package)
    resources, files, sets = [], {}, []

    def resource(name, array):
        rid = str(uuid.uuid5(NAMESPACE, name))
        resources.append({"id": rid, "name": name, "kind": "texture", "file": f"resources/{rid}.png"})
        files[f"resources/{rid}.png"] = png(array)
        return rid
    for slot, name in enumerate(materials):
        rec = labels[name]
        read = lambda ch, mode: np.asarray(Image.open(package / "seed" / pathlib.Path(rec["files"][ch]).name).convert(mode))  # noqa: E731
        mr = read("metallicroughness", "RGB")
        images = {"base_color": read("basecolor", "RGBA"), "roughness": mr[..., 1], "metallic": mr[..., 2],
                  "normal": read("normal", "RGB"), "ambient_occlusion": read("occlusion", "L")}
        channels = [{"channel": ch, "enabled": True, "source": {"type": "bitmap", "resource": resource(f"{name}_{ch}", im)}, "strength": 1.0}
                    for ch, im in images.items()]
        layer = {"id": str(uuid.uuid5(NAMESPACE, f"{name}/layer")), "name": "Game texture (seed)", "visible": True,
                 "blend": [{"channel": ch, "mode": mode, "opacity": 1.0} for ch, mode in BLEND],
                 "content": {"type": "fill", "projection": {"mode": "uv", "scale": [1.0, 1.0], "rotation_deg": 0.0, "offset": [0.0, 0.0],
                                                           "triplanar_sharpness": 8.0, "planar_axis": 2}, "channels": channels}}
        sets.append({"id": str(uuid.uuid5(NAMESPACE, f"{name}/set")), "name": name, "material_slot": slot,
                     "resolution": resolution(rec["w"], rec["h"]), "albedo_filter": "nearest",
                     "mixing": {"normal": "combine", "height_to_normal": "smooth", "ao": "multiply", "uv_padding": "space3d"},
                     "channels": [{"kind": k, "format": f} for k, f in CHANNELS], "layers": [layer]})
    manifest = {"format": "subcraft-project", "schema_version": SCHEMA, "writer": "rood-boi subcraft_project.py",
                "mesh": "mesh.bin", "document": "document.json"}
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(manifest))
        z.writestr("document.json", json.dumps({"texture_sets": sets, "resources": resources}))
        z.writestr("mesh.bin", mesh)
        for name, data in files.items():
            z.writestr(name, data)
    return pathlib.Path(out)



def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package")
    parser.add_argument("--out")
    args = parser.parse_args()
    out = pathlib.Path(args.out or pathlib.Path(args.package) / "paint.subcraft")
    try:
        print(f"subcraft_project: {build(args.package, out)}")
    except FormatMismatch as e:
        print(f"subcraft_project: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
