"""Opens a paint package's glTF in Blender and reports what it holds (pc/tools/paint_roundtrip.py; Gitea #20).

    blender -b -P pc/tools/blender/check_gltf.py -- PAINT.gltf [PREVIEW.png]

Prints one JSON line prefixed "check_gltf:": meshes, materials (with the images each node tree reads), the UV range, the
triangle count, the winding (edges two faces share that the faces wind against each other: 0 when consistent; the signed volume,
positive when the faces point outward), the bounding box, and whether the vertices are joined (distinct positions against
corners: a body in one piece shares its joints). With PREVIEW.png two workbench renders, PREVIEW_minus_y.png and PREVIEW_plus_y.png (seen from each side along y)
(back faces culled, so a face turned the wrong way is a hole)."""
import json
import sys

import bmesh
import bpy
import mathutils

args = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=args[0])
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
report = {"meshes": len(meshes), "materials": [], "triangles": 0}
lo = mathutils.Vector((1e9,) * 3)
hi = mathutils.Vector((-1e9,) * 3)
uv_lo, uv_hi = [1e9, 1e9], [-1e9, -1e9]
corners = 0
positions = set()
for o in meshes:
    me = o.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    uv = bm.loops.layers.uv.active
    for f in bm.faces:
        report["triangles"] += 1
        for l in f.loops:
            p = o.matrix_world @ l.vert.co
            positions.add(tuple(round(c, 4) for c in p))
            corners += 1
            for i in range(3):
                lo[i], hi[i] = min(lo[i], p[i]), max(hi[i], p[i])
            if uv:
                for i in range(2):
                    uv_lo[i], uv_hi[i] = min(uv_lo[i], l[uv].uv[i]), max(uv_hi[i], l[uv].uv[i])
    bm.free()
    joined = bmesh.new()
    joined.from_mesh(me)
    bmesh.ops.remove_doubles(joined, verts=joined.verts, dist=1e-5)
    report["edges_inconsistent"] = report.get("edges_inconsistent", 0) + sum(1 for e in joined.edges if len(e.link_faces) == 2 and not e.is_contiguous)
    report["volume"] = report.get("volume", 0.0) + round(joined.calc_volume(signed=True), 6)
    joined.free()
for m in bpy.data.materials:
    images = sorted({n.image.name for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image}) if m.use_nodes else []
    report["materials"].append({"name": m.name, "images": images})
report.update(bbox_min=[round(c, 3) for c in lo], bbox_max=[round(c, 3) for c in hi], uv_min=uv_lo, uv_max=uv_hi,
              corners=corners, distinct_positions=len(positions))
print("check_gltf: " + json.dumps(report))
if len(args) > 1:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.shading.show_backface_culling = True
    scene.render.resolution_x = scene.render.resolution_y = 768
    centre = (lo + hi) / 2
    size = max(hi - lo)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = size * 1.2
    scene.camera = cam
    for name, side in (("minus_y", -1), ("plus_y", 1)):
        cam.location = centre + mathutils.Vector((0, side * size * 3, 0))
        cam.rotation_euler = (1.5708, 0, 0 if side < 0 else 3.14159)
        scene.render.filepath = args[1].replace(".png", f"_{name}.png")
        bpy.ops.render.render(write_still=True)
