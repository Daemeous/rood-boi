"""Blender side of pc/tools/room_blender.py: bakes a room's textures from its geometry.

    blender -b -P pc/tools/blender/bake_room.py -- ROOM.gltf EXPORT.json OUTDIR SAMPLES '{"bevel_radius": ..}'

Imports the room (one mesh, one primitive and material per texture, UVs over the texture's own box), then for each
texture bakes, texel for texel, into <OUTDIR>/<key>/: cover.png (1 where a triangle covers the texel), wear.png (edge wear:
how far the bevelled normal departs from the face's own, the geometry's convex edges), cavity.png (1 - ambient occlusion,
the creases). Cycles on the CPU, Non-Color 8-bit images. Prints "bake_room:" progress lines."""
import json
import sys

import bpy

args = sys.argv[sys.argv.index("--") + 1:]
gltf, meta_path, outdir, samples, params = args[0], args[1], args[2], int(args[3]), json.loads(args[4])
meta = json.load(open(meta_path))
textures = meta["textures"]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=gltf)
objects = [o for o in bpy.context.scene.objects if o.type == "MESH"]
assert objects, "no mesh imported"
obj = objects[0]
bpy.context.view_layer.objects.active = obj
for o in bpy.context.scene.objects:
    o.select_set(o == obj)

scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = samples
scene.cycles.use_denoising = False
scene.cycles.bake_type = "EMIT"
scene.render.bake.margin = 0
scene.render.bake.use_clear = True

PASSES = ("cover", "wear", "cavity")
images = {}
for slot in obj.material_slots:
    mat = slot.material
    key = mat.name.split(".")[0]
    if key not in textures:
        continue
    t = textures[key]
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    # the passes' drivers
    cover = nt.nodes.new("ShaderNodeValue")
    cover.outputs[0].default_value = 1.0
    bevel = nt.nodes.new("ShaderNodeBevel")
    bevel.inputs["Radius"].default_value = params["bevel_radius"]
    bevel.samples = params["bevel_samples"]
    geometry = nt.nodes.new("ShaderNodeNewGeometry")
    dot = nt.nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    nt.links.new(bevel.outputs["Normal"], dot.inputs[0])
    nt.links.new(geometry.outputs["True Normal"], dot.inputs[1])
    one_minus = nt.nodes.new("ShaderNodeMath")
    one_minus.operation = "SUBTRACT"
    one_minus.inputs[0].default_value = 1.0
    nt.links.new(dot.outputs["Value"], one_minus.inputs[1])
    gain = nt.nodes.new("ShaderNodeMath")
    gain.operation = "MULTIPLY"
    gain.use_clamp = True
    gain.inputs[1].default_value = params["wear_gain"]
    nt.links.new(one_minus.outputs[0], gain.inputs[0])
    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.samples = params["ao_samples"]
    ao.inputs["Distance"].default_value = params["ao_distance"]
    ao.only_local = True
    invert = nt.nodes.new("ShaderNodeMath")
    invert.operation = "SUBTRACT"
    invert.inputs[0].default_value = 1.0
    nt.links.new(ao.outputs["AO"], invert.inputs[1])
    drivers = {"cover": cover.outputs[0], "wear": gain.outputs[0], "cavity": invert.outputs[0]}
    tex = nt.nodes.new("ShaderNodeTexImage")
    for name in PASSES:
        img = bpy.data.images.new(f"{name}_{key}", t["w"], t["h"], alpha=False, float_buffer=False)
        img.colorspace_settings.name = "Non-Color"
        images[(name, key)] = img
    mat["drivers"] = True
    images[("nodes", key)] = (nt, emit, tex, drivers)

print(f"bake_room: {len(textures)} textures, {sum(1 for k in images if k[0] == 'cover')} to bake, {samples} samples")
for name in PASSES:
    for key in [k[1] for k in images if k[0] == "cover"]:
        nt, emit, tex, drivers = images[("nodes", key)]
        for link in list(emit.inputs["Color"].links):
            nt.links.remove(link)
        nt.links.new(drivers[name], emit.inputs["Color"])
        tex.image = images[(name, key)]
        nt.nodes.active = tex
        tex.select = True
    bpy.ops.object.bake(type="EMIT", margin=0, use_clear=True)
    print(f"bake_room: {name} baked")
import os
for (name, key), img in list(images.items()):
    if name == "nodes":
        continue
    d = os.path.join(outdir, key)
    os.makedirs(d, exist_ok=True)
    img.filepath_raw = os.path.join(d, f"{name}.png")
    img.file_format = "PNG"
    img.save()
print("bake_room: saved")
