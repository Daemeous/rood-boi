#!/usr/bin/env python3
"""Node material graphs (pc/docs/plans/2026-10-30-node-materials-plan.md; RPC material_graph, the
generated include through the shader hot reload, the Lua editor).

    python3 pc/tests/material_graph_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only a,b]

Continues from a copy of the card into its save room with the editor open, scratch material folders
(the user's graphs and generated shaders under the output folder), then:

- generate: the node library (built-ins and the shipped files), the shipped standard_surface_graph
  generates, a new graph is texel + planes -> output, nodes added and linked, a knob set; a type
  mismatch, a second link into one input, a cycle, an unknown node type and an unknown knob are
  refused naming the node; the whole include names the slots.
- identity (Vulkan): the Keep (29/3) with a user layer of three entries on standard_surface, then the
  same entries on standard_surface_graph: the frames are byte-identical, the slots are in the planes
  and native_frag is overridden by the reload.
- live (Vulkan): a tint graph assigned to the same entries changes the frame within a few seconds, a
  knob edit changes it again, a broken graph keeps the picture and reports its node, promote makes a
  master parameter an instance can set (the frame follows), removing the assignment restores the
  first frame and drops the generated file.
- editor: Window > Material graph (the Lua plugin) opens by RPC, lists the graphs, selects one and
  edits a knob through the panel's own code path.
"""
import argparse
import json
import os
import pathlib
import shutil
import sys
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import RpcError  # noqa: E402
import editor_selection_smoke as es  # noqa: E402

VULKAN = os.environ.get("ROOD_RENDERER") == "vulkan"
PLUGIN = "rood.material_graph"
WINDOW = PLUGIN + "/graph"


def entries(master):
    """Three Keep entries (the floor palette, a wall palette, a texture), specular on so the fixed path lights them."""
    e = [
        {"id": "G1", "name": "floor", "match": {"clut": "415127aebd"}, "scope": {"room": [29, 3]},
         "roughness": 0.45, "specular": 0.6, "metal": 0.1},
        {"id": "G2", "name": "walls", "match": {"clut": "97ad08ce0c"}, "scope": {"room": [29, 3]},
         "roughness": 0.7, "specular": 0.4},
        {"id": "G3", "name": "tile", "match": {"key": "66b4521ff4", "size": [31, 31]}, "scope": {"room": [29, 3]},
         "roughness": 0.3, "specular": 0.8, "metal": 0.5},
    ]
    if master != "standard_surface":
        for x in e:
            x["master"] = master
    return {"format": 2, "presets": {}, "entries": e}


def write_json(path, data):
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, indent=1) + "\n")
    os.replace(tmp, path)


def settle(game):
    game.call("material", action="reload")
    for _ in range(3):
        game.call("redraw")
        game.wait_presents(2)


def frame(game, path):
    game.call("redraw")
    game.wait_presents(6)
    game.present_screenshot(str(path))
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)


def next_frame(game, path):
    """The very next presented frame after an edit (no settling: a value edit must show at once)."""
    game.call("redraw")
    game.wait_presents(1)
    game.present_screenshot(str(path))
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)


def reloads(s):
    """(the shader reload's generation, the include's write count): both stay put across a value edit."""
    return s.game.call("shaders", action="status")["generation"], rpc(s, action="status")["generation"]


def differ(a, b):
    return int((np.abs(a - b).max(2) > 0).sum())


def rpc(s, **params):
    return s.game.call("material_graph", **params)


def wait_reload(s, after, timeout=8.0):
    """Until the shader reload's generation passes `after` (and no compile is pending)."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = s.game.call("shaders", action="status")
        if st["generation"] > after and not st["pending"]:
            return st
        s.game.call("redraw")
        s.game.wait_presents(2)
    return s.game.call("shaders", action="status")


def refuse(s, label, **params):
    try:
        r = rpc(s, **params)
        s.check(False, f"{label} is refused ({r})")
        return None
    except RpcError as e:
        s.check(True, f"{label} is refused ({e})")
        return str(e)


def check_generate(s):
    nodes = {n["name"]: n for n in rpc(s, action="nodes")["nodes"]}
    s.check({"texel", "planes", "uv", "normal", "vertex_color", "output"} <= set(nodes)
            and all(nodes[n]["builtin"] for n in ("texel", "output")), f"the built-in nodes are listed ({sorted(nodes)})")
    s.check({"lerp", "multiply", "number", "color", "checker"} <= set(nodes) and not nodes["lerp"]["builtin"],
            "the shipped node files are listed")
    lerp = nodes["lerp"]
    s.check([i["name"] for i in lerp["inputs"]] == ["a", "b", "t"] and lerp["inputs"][2]["pin"]
            and lerp["inputs"][2]["type"] == "float" and lerp["outputs"][0]["type"] == "float3",
            f"lerp: inputs a, b, t (t pinned), a float3 result ({lerp})")
    listed = rpc(s, action="list")
    names = {g["name"]: g for g in listed["graphs"]}
    s.check("standard_surface_graph" in names and names["standard_surface_graph"]["ok"] and not names["standard_surface_graph"]["user"],
            f"the shipped standard surface graph is listed and generates ({names})")
    gen = rpc(s, action="generate", name="standard_surface_graph")
    s.check(gen["ok"] and "material = n_planes_surface;" in gen["glsl"] and "relief = n_planes_relief;" in gen["glsl"]
            and "void graph_standard_surface_graph(GraphIn gi, inout vec4 material" in gen["glsl"],
            f"its GLSL writes the planes into the surface and the relief ({gen['glsl']!r})")
    s.check("void material_graph(uint slot, GraphIn gi" in gen["include"] and "switch (slot)" in gen["include"],
            "the include has the slot switch")
    g = rpc(s, action="new", name="t1")
    s.check(g["ok"] and {n["id"] for n in g["graph"]["nodes"]} == {"texel", "planes", "output"} and g["dirty"] and g["user"],
            f"a new graph is texel + planes -> output, unsaved ({g['graph']})")
    refuse(s, "a second graph of the name", action="new", name="t1")
    refuse(s, "a graph named like a master", action="new", name="standard_surface")
    refuse(s, "a bad name", action="new", name="no spaces here")
    g = rpc(s, action="node_add", name="t1", id="mul", type="multiply", x=200, y=60)
    s.check(any(n["id"] == "mul" and n["pos"] == [200, 60] for n in g["graph"]["nodes"]), "a multiply node added at (200, 60)")
    refuse(s, "an unknown node type", action="node_add", name="t1", id="x", type="no_such_node")
    refuse(s, "a node id used twice", action="node_add", name="t1", id="mul", type="lerp")
    g = rpc(s, action="link", name="t1", **{"from": "texel.color", "to": "mul.a"})
    g = rpc(s, action="link", name="t1", **{"from": "mul.result", "to": "output.base"})
    s.check(g["ok"] and {(l["from"], l["to"]) for l in g["graph"]["links"]} >= {("texel.color", "mul.a"), ("mul.result", "output.base")},
            f"texel -> multiply -> base linked ({g['graph']['links']})")
    g = rpc(s, action="knob", name="t1", id="mul", input="b", value=[1, 0.25, 0.25])
    gen = rpc(s, action="generate", name="t1")
    s.check(gen["ok"] and "node_multiply(n_texel_color, vec3(1.0, 0.25, 0.25), n_mul_result);" in gen["glsl"]
            and "base = n_mul_result; set |= 1;" in gen["glsl"], f"the knob is a literal in the call ({gen['glsl']!r})")
    s.check("void node_multiply(vec3 a, vec3 b, out vec3 result)" in gen["functions"], "the node's function is emitted from its file")
    refuse(s, "a knob that is not there", action="knob", name="t1", id="mul", input="zzz", value=1)
    # A type mismatch: a float4 into a float3 input, refused by generation naming the node.
    rpc(s, action="link", name="t1", **{"from": "planes.surface", "to": "mul.b"})
    gen = rpc(s, action="generate", name="t1")
    s.check(not gen["ok"] and gen["error_node"] == "mul" and "float4" in gen["error"], f"a float4 into a float3 input: the node is named ({gen['error']})")
    rpc(s, action="unlink", name="t1", to="mul.b")
    s.check(rpc(s, action="generate", name="t1")["ok"], "unlinked: generates again")
    # A float broadcasts into a float3.
    rpc(s, action="node_add", name="t1", id="k", type="number")
    rpc(s, action="link", name="t1", **{"from": "k.result", "to": "mul.b"})
    gen = rpc(s, action="generate", name="t1")
    s.check(gen["ok"] and "vec3(n_k_result)" in gen["glsl"], f"a float into a float3 input broadcasts ({gen['glsl']!r})")
    rpc(s, action="unlink", name="t1", to="mul.b")
    # A cycle.
    rpc(s, action="node_add", name="t1", id="s", type="scale")
    rpc(s, action="link", name="t1", **{"from": "mul.result", "to": "s.a"})
    rpc(s, action="link", name="t1", **{"from": "s.result", "to": "mul.a"})
    gen = rpc(s, action="generate", name="t1")
    s.check(not gen["ok"] and "cycle" in gen["error"] and gen["error_node"] in ("mul", "s"), f"a cycle is refused naming a node on it ({gen['error']})")
    rpc(s, action="link", name="t1", **{"from": "texel.color", "to": "mul.a"})
    s.check(rpc(s, action="generate", name="t1")["ok"], "the cycle cut: generates again")
    g = rpc(s, action="node_remove", name="t1", id="s")
    s.check(not any(n["id"] == "s" for n in g["graph"]["nodes"]) and not any(l["from"].startswith("s.") or l["to"].startswith("s.") for l in g["graph"]["links"]),
            "removing a node drops its links")
    # Pin and promote.
    g = rpc(s, action="pin", name="t1", id="mul", input="a", on=True)
    node = next(n for n in g["graph"]["nodes"] if n["id"] == "mul")
    s.check(node["pinned"] == ["b", "a"], f"pinning a adds it to the definition's pins ({node['pinned']})")
    # A colour knob promotes as a colour parameter; a float3 that is not a colour does not.
    g = rpc(s, action="promote", name="t1", id="mul", input="b", param="tint")
    masters = {m["name"]: m for m in s.game.call("material_instance", action="masters")["masters"]}
    tint = next((p for p in masters["t1"]["parameters"] if p["name"] == "tint"), None)
    s.check(g["params"] == ["tint"] and tint and tint["type"] == "color" and tint["default"] == [1, 0.25, 0.25],
            f"a colour knob promoted is a colour parameter, its knob's colour the default ({g['params']}, {tint})")
    gen = rpc(s, action="generate", name="t1")
    s.check("vec3 p_tint" in gen["glsl"] and "node_multiply(n_texel_color, p_tint, n_mul_result);" in gen["glsl"],
            f"...a vec3 argument of the graph's function ({gen['glsl']!r})")
    refuse(s, "promoting a float3 knob that is not a colour", action="promote", name="t1", id="mul", input="a", param="aa")
    rpc(s, action="promote", name="t1", id="mul", input="b", param=None)
    rpc(s, action="node_add", name="t1", id="amount", type="scale")
    rpc(s, action="link", name="t1", **{"from": "mul.result", "to": "amount.a"})
    rpc(s, action="link", name="t1", **{"from": "amount.result", "to": "output.base"})
    g = rpc(s, action="promote", name="t1", id="amount", input="k", param="strength")
    s.check(g["params"] == ["strength"] and not g["dirty"], f"a float knob promoted is the master's parameter; promote saves ({g['params']}, dirty {g['dirty']})")
    path = pathlib.Path(g["path"])
    data = json.loads(path.read_text())
    pnames = [p["name"] for p in data["parameters"]]
    s.check(path.parent == s.graphs and data["shader"] == "graph:t1" and pnames[:5] == ["roughness", "specular", "metal", "auto_height", "bump"]
            and pnames[5:] == ["strength"] and data["graph"]["promoted"] == {"amount.k": "strength"},
            f"saved in the user's graph folder as a master: the standard five then the promoted knob ({path}, {pnames})")
    masters = {m["name"]: m for m in s.game.call("material_instance", action="masters")["masters"]}
    s.check("t1" in masters and [p["name"] for p in masters["t1"]["parameters"]] == pnames, "material_instance lists the graph master with those parameters")
    gen = rpc(s, action="generate", name="t1")
    s.check("float p_strength" in gen["glsl"] and "node_scale(n_mul_result, p_strength, n_amount_result);" in gen["glsl"],
            f"the parameter is the function's argument ({gen['glsl']!r})")
    g = rpc(s, action="promote", name="t1", id="amount", input="k", param=None)
    s.check(g["params"] == [], "unpromoted")
    g = rpc(s, action="promote", name="t1", id="amount", input="k", param="strength")
    st = rpc(s, action="status")
    s.check(st["slots"] == 0 and not st["written"], f"no graph instance: nothing generated on disk ({st})")


def check_identity(s):
    if not VULKAN:
        print("skip: identity is Vulkan (GL draws no material response)")
        return
    g = s.game
    if not s.check(es.goto(g, 29, 3), "to the Keep (29/3)"):
        return
    g.call("editor_ui", visible=False)
    g.call("graphics", interpolate=False)
    write_json(s.user_file, entries("standard_surface"))
    settle(g)
    s.fixed = frame(g, s.out / "keep-fixed.png")
    uses = {u["entry"] for u in g.call("material", action="list")["uses"]}
    s.check({"G1", "G2", "G3"} <= uses, f"the three entries are drawn in the Keep ({sorted(uses)})")
    s.check(rpc(s, action="status")["slots"] == 0, "no slots on the standard surface")
    gen0 = g.call("shaders", action="status")["generation"]
    write_json(s.user_file, entries("standard_surface_graph"))
    settle(g)
    st = rpc(s, action="status")
    s.check(st["slots"] == 3 and st["written"] and not st["error"], f"three slots, the include written ({st})")
    info = rpc(s, action="get", name="standard_surface_graph")
    s.check([i["id"] for i in info["instances"]] == ["G1", "G2", "G3"] and [i["slot"] for i in info["instances"]] == [1, 2, 3],
            f"the instances have slots 1..3 by id ({info['instances']})")
    sh = wait_reload(s, gen0)
    s.check(sh["generation"] > gen0 and "native_frag" in sh["overridden"] and not sh["errors"],
            f"the reload compiled native_frag with the generated include ({sh['generation']}, {sh['overridden']}, {sh['errors']})")
    text = pathlib.Path(st["path"]).read_text()
    s.check("case 1u: graph_standard_surface_graph(gi, material, relief, base, glow, set); break;" in text, "the switch calls the graph for slot 1")
    settle(g)
    graphed = frame(g, s.out / "keep-graph.png")
    d = differ(s.fixed, graphed)
    s.check(d == 0, f"the Keep renders byte-identically through the standard surface graph ({d} pixels differ)")
    cells = g.call("material", action="list")
    s.check(cells["cells"] > 0, f"the planes hold material cells ({cells['cells']})")
    g.call("editor_ui", visible=True)


def check_live(s):
    if not VULKAN or s.fixed is None:
        print("skip: live is Vulkan")
        return
    g = s.game
    g.call("editor_ui", visible=False)
    # A tint graph: texel * colour -> base, the colour's amount a parameter.
    rpc(s, action="new", name="tint")
    rpc(s, action="node_add", name="tint", id="mul", type="multiply", x=200, y=60)
    rpc(s, action="link", name="tint", **{"from": "texel.color", "to": "mul.a"})
    rpc(s, action="link", name="tint", **{"from": "mul.result", "to": "output.base"})
    rpc(s, action="knob", name="tint", id="mul", input="b", value=[1, 0.2, 0.2])
    rpc(s, action="save", name="tint")
    gen0 = g.call("shaders", action="status")["generation"]
    write_json(s.user_file, entries("tint"))
    settle(g)
    sh = wait_reload(s, gen0)
    settle(g)
    red = frame(g, s.out / "keep-tint.png")
    s.check(sh["generation"] > gen0 and not sh["errors"] and differ(s.fixed, red) > 1000,
            f"the tint graph assigned: the frame changes ({differ(s.fixed, red)} pixels, errors {sh['errors']})")
    text0 = pathlib.Path(rpc(s, action="status")["path"]).read_text()
    s.check("graph_values[" in text0 and "vec3(1.0, 0.2, 0.2)" not in text0,
            "the knob is a read of the values table, not a literal in the include")
    # Slider speed: a knob edit is a table write; the very next frame shows it, nothing regenerates or reloads.
    before = reloads(s)
    rpc(s, action="knob", name="tint", id="mul", input="b", value=[0.2, 1, 0.2])
    green = next_frame(g, s.out / "keep-tint-green.png")
    s.check(differ(red, green) > 1000, f"a knob edit shows on the next presented frame ({differ(red, green)} pixels)")
    for _ in range(3):
        g.call("redraw")
        g.wait_presents(2)
    s.check(reloads(s) == before and pathlib.Path(rpc(s, action="status")["path"]).read_text() == text0,
            f"...with no shader reload and the include untouched (reload, writes {reloads(s)} == {before})")
    # A drag: twenty knob values, one a frame; every frame follows and still nothing reloads.
    last, moved = green, 0
    for k in range(20):
        v = 0.2 + 0.8 * (k + 1) / 20
        rpc(s, action="knob", name="tint", id="mul", input="b", value=[v, 1, 0.2])
        f = next_frame(g, s.out / "keep-tint-drag.png")
        moved += differ(last, f) > 0
        last = f
    s.check(moved >= 18 and reloads(s) == before, f"a dragged knob: {moved}/20 frames changed, reloads {reloads(s)} == {before}")
    rpc(s, action="knob", name="tint", id="mul", input="b", value=[0.2, 1, 0.2])
    green = next_frame(g, s.out / "keep-tint-green.png")
    sh = g.call("shaders", action="status")
    # Broken: the picture and the shader stay; the node is named.
    gen0 = sh["generation"]
    st0 = rpc(s, action="status")
    rpc(s, action="link", name="tint", **{"from": "planes.surface", "to": "mul.b"})
    for _ in range(4):
        g.call("redraw")
        g.wait_presents(2)
    st = rpc(s, action="status")
    info = rpc(s, action="get", name="tint")
    s.check(not info["ok"] and info["error_node"] == "mul" and "tint:" in st["error"] and st["generation"] == st0["generation"],
            f"a broken graph names its node and the include is not rewritten ({st['error']}, {st['generation']} == {st0['generation']})")
    s.check(pathlib.Path(st["path"]).read_text() == text0, "the last good include is still on disk")
    still = frame(g, s.out / "keep-tint-broken.png")
    s.check(differ(green, still) == 0 and g.call("shaders", action="status")["generation"] == gen0, "the picture keeps the last good shader")
    rpc(s, action="unlink", name="tint", to="mul.b")
    for _ in range(4):
        g.call("redraw")
        g.wait_presents(2)
    s.check(not rpc(s, action="status")["error"], "fixed: no error")
    # Promote the amount: an instance sets it, the frame follows.
    rpc(s, action="node_add", name="tint", id="amount", type="scale")
    rpc(s, action="link", name="tint", **{"from": "mul.result", "to": "amount.a"})
    rpc(s, action="link", name="tint", **{"from": "amount.result", "to": "output.base"})
    gen0 = g.call("shaders", action="status")["generation"]
    rpc(s, action="promote", name="tint", id="amount", input="k", param="amount")
    sh = wait_reload(s, gen0)
    inst = g.call("material_instance", action="get", id="G1")
    s.check("amount" in inst["values"] and inst["values"]["amount"] == 1, f"the instance sees the promoted parameter at its default ({inst['values']})")
    settle(g)
    green = frame(g, s.out / "keep-tint-green2.png")
    text = pathlib.Path(rpc(s, action="status")["path"]).read_text()
    s.check("case 1u: graph_tint(gi, graph_values[" in text and "graph_tint(gi, 1.0" not in text,
            "the instances' parameters are reads of the values table in the switch")
    before = reloads(s)
    g.call("material_instance", action="set", id="G1", params={"amount": 0.2})
    dim = next_frame(g, s.out / "keep-tint-dim.png")
    s.check(differ(green, dim) > 1000, f"an instance's parameter edit shows on the next presented frame ({differ(green, dim)} pixels)")
    for _ in range(3):
        g.call("redraw")
        g.wait_presents(2)
    s.check(reloads(s) == before and pathlib.Path(rpc(s, action="status")["path"]).read_text() == text,
            f"...with no shader reload and the include untouched ({reloads(s)} == {before})")
    g.call("material_instance", action="revert")
    check_colour_param(s, green)
    # The master switched from the Materials panel's path (material_instance set {master}): G1 back on the
    # standard surface takes slot 0, the other two keep theirs.
    r = g.call("material_instance", action="set", id="G1", master="standard_surface")
    settle(g)
    info = rpc(s, action="get", name="tint")
    s.check(r["master"] == "standard_surface" and [i["id"] for i in info["instances"]] == ["G2", "G3"],
            f"an instance's master set through material_instance: G1 leaves the graph ({info['instances']})")
    try:
        g.call("material_instance", action="set", id="G1", master="no_such_master")
        s.check(False, "an unknown master is refused")
    except RpcError as e:
        s.check(True, f"an unknown master is refused ({e})")
    g.call("material_instance", action="revert")
    # Back to the fixed path: the generated file goes, the first frame returns.
    gen0 = g.call("shaders", action="status")["generation"]
    write_json(s.user_file, entries("standard_surface"))
    settle(g)
    st = rpc(s, action="status")
    s.check(st["slots"] == 0 and not st["written"] and not pathlib.Path(st["path"]).exists(), f"no graph instance: the generated file is removed ({st})")
    wait_reload(s, gen0, timeout=4)
    settle(g)
    back = frame(g, s.out / "keep-back.png")
    s.check(differ(s.fixed, back) == 0, f"the fixed path renders as before ({differ(s.fixed, back)} pixels differ)")
    g.call("editor_ui", visible=True)


def check_colour_param(s, green):
    """The tint graph's colour knob promoted: every instance has a colour parameter (a swatch with Reset in
    Window > Materials); setting it is a table write (the next frame, no reload); null resets it; it is
    saved as [r, g, b]."""
    g = s.game
    gen0 = g.call("shaders", action="status")["generation"]
    rpc(s, action="promote", name="tint", id="mul", input="b", param="tint")
    wait_reload(s, gen0)
    settle(g)
    base = frame(g, s.out / "keep-colour-default.png")
    masters = {m["name"]: m for m in g.call("material_instance", action="masters")["masters"]}
    tint = next((p for p in masters["tint"]["parameters"] if p["name"] == "tint"), None)
    inst = g.call("material_instance", action="get", id="G1")
    s.check(tint and tint["type"] == "color" and tint["default"] == [0.2, 1, 0.2]
            and [inst["values"].get("tint" + c) for c in (".r", ".g", ".b")] == [0.2, 1, 0.2],
            f"the colour parameter: its default the knob's colour, resolved per instance ({tint}, {inst['values']})")
    s.check(differ(green, base) == 0, f"promoting the knob keeps the picture ({differ(green, base)} pixels)")
    before = reloads(s)
    g.call("material_instance", action="set", id="G1", params={"tint": [1, 0.2, 0.2]})
    red = next_frame(g, s.out / "keep-colour-red.png")
    inst = g.call("material_instance", action="get", id="G1")
    s.check(differ(base, red) > 1000 and inst["own"].get("tint.r") == 1 and inst["from"].get("tint.r") == "G1",
            f"an instance's colour set: the next frame shows it ({differ(base, red)} pixels, own {inst['own']})")
    for c in ([0.5], [2, 0, 0], 0.5):
        try:
            g.call("material_instance", action="set", id="G1", params={"tint": c})
            s.check(False, f"a bad colour {c} is refused")
        except RpcError as e:
            s.check(True, f"a bad colour {c} is refused ({e})")
    g.call("material_instance", action="set", id="G1", params={"tint": None})
    back = next_frame(g, s.out / "keep-colour-reset.png")
    inst = g.call("material_instance", action="get", id="G1")
    s.check(differ(base, back) == 0 and not any(k.startswith("tint") for k in inst["own"]),
            f"Reset to parent: the three parts go, the picture returns ({differ(base, back)} pixels, own {inst['own']})")
    s.check(reloads(s) == before, f"...no shader reload throughout ({reloads(s)} == {before})")
    # Saved as [r, g, b] under the colour's name, read back as its parts.
    g.call("material_instance", action="set", id="G1", params={"tint": [1, 0.2, 0.2]})
    g.call("material_instance", action="save")
    data = json.loads(s.user_file.read_text())
    g1 = next(e for e in data["entries"] if e["id"] == "G1")
    s.check(g1.get("params", {}).get("tint") == [1, 0.2, 0.2] and not any("." in k for k in g1.get("params", {})),
            f"saved as one [r, g, b] ({g1.get('params')})")
    g.call("material", action="reload")
    inst = g.call("material_instance", action="get", id="G1")
    s.check([inst["own"].get("tint" + c) for c in (".r", ".g", ".b")] == [1, 0.2, 0.2], f"...and read back ({inst['own']})")
    write_json(s.user_file, entries("tint"))
    settle(g)


def check_time(s):
    """The Time source and the shipped Pulse node (its time input unlinked: the game time): the game time is
    the values table's entry 0, vsyncs / 60; it moves with the game, holds while the game is held and is the
    held moment's in every photo render (the same picture each time)."""
    g = s.game
    nodes = {n["name"]: n for n in rpc(s, action="nodes")["nodes"]}
    t = nodes.get("time")
    pulse = nodes.get("pulse")
    s.check(t and t["builtin"] and [o["name"] for o in t["outputs"]] == ["seconds", "phase"] and t["inputs"][0]["name"] == "loop",
            f"the Time source: seconds and phase in its loop ({t})")
    s.check(pulse and not pulse["builtin"] and pulse["inputs"][0]["time"] and not pulse["inputs"][0]["knob"],
            f"the shipped Pulse node: its time input reads the game time when unlinked ({pulse and pulse['inputs'][0]})")
    rpc(s, action="new", name="looped")
    rpc(s, action="node_add", name="looped", id="clock", type="time")
    rpc(s, action="node_add", name="looped", id="p", type="pulse")
    rpc(s, action="link", name="looped", **{"from": "clock.seconds", "to": "p.time"})
    rpc(s, action="link", name="looped", **{"from": "p.result", "to": "output.glow"})
    gen = rpc(s, action="generate", name="looped")
    s.check(gen["ok"] and "float n_clock_seconds = mod(graph_values[0].x, max(60.0, 0.001));" in gen["glsl"]
            and "node_pulse(n_clock_seconds," in gen["glsl"] and "glow = vec3(n_p_result); set |= 2;" in gen["glsl"],
            f"Time -> Pulse -> glow generates ({gen['glsl']!r})")
    s.check(not any(m["name"] == "looped" for m in g.call("material_instance", action="masters")["masters"]),
            "(unsaved: not a master yet)")
    if not VULKAN or s.fixed is None:
        print("skip: the time in pictures is Vulkan")
        return
    g.call("editor_ui", visible=False)
    rpc(s, action="new", name="pulsing")
    rpc(s, action="node_add", name="pulsing", id="p", type="pulse")
    rpc(s, action="link", name="pulsing", **{"from": "p.result", "to": "output.glow"})
    rpc(s, action="knob", name="pulsing", id="p", input="rate", value=1)
    rpc(s, action="knob", name="pulsing", id="p", input="high", value=0.6)
    gen = rpc(s, action="generate", name="pulsing")
    s.check(gen["ok"] and "node_pulse(graph_values[0].x, 1.0, 0.0, 0.6, n_p_result);" in gen["glsl"],
            f"the Pulse's unlinked time is the game time ({gen['glsl']!r})")
    rpc(s, action="save", name="pulsing")
    gen0 = g.call("shaders", action="status")["generation"]
    write_json(s.user_file, entries("pulsing"))
    settle(g)
    wait_reload(s, gen0)
    settle(g)
    t0 = rpc(s, action="status")["time"]
    g.run(15)
    t1 = rpc(s, action="status")["time"]
    s.check(abs((t1 - t0) - 0.25) < 0.02, f"the game time moves with the game's vsyncs (15 vsyncs: {t1 - t0:.4f} s)")
    a = frame(g, s.out / "pulse-a.png")
    g.run(15)
    b = frame(g, s.out / "pulse-b.png")
    s.check(differ(a, b) > 1000, f"the pulse moves with the game ({differ(a, b)} pixels)")
    # Held (the photo mode's pause): the time and the picture stay, however long it is held.
    def photo_lua(code):
        r = g.lua(code, plugin="rood.photo")
        return (r.get("results") or [None])[0] if r.get("ok") else {"error": r.get("error")}
    if not s.check(photo_lua("return rood.game.pause()") is True, "held (rood.game.pause, the photo mode's plugin)"):
        return
    th = rpc(s, action="status")["time"]
    h1 = frame(g, s.out / "pulse-held-1.png")
    time.sleep(0.6)
    g.run(15)
    h2 = frame(g, s.out / "pulse-held-2.png")
    s.check(rpc(s, action="status")["time"] == th and differ(h1, h2) == 0,
            f"held: the time stays ({rpc(s, action='status')['time']} == {th}) and so does the picture ({differ(h1, h2)} pixels)")
    # Photo renders (the held moment drawn again under a camera: the game run from M and rewound): each the same.
    def render(code, label):
        before = g.call("photo")["renders"]
        photo_lua(code)
        t_end = time.time() + 20
        while g.call("photo")["renders"] <= before and time.time() < t_end:
            time.sleep(0.05)
        g.wait_presents(4)
        path = s.out / f"pulse-{label}.png"
        g.present_screenshot(str(path))
        return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)
    r1 = render("rood.camera.begin() return true", "render-1")
    r2 = render("rood.camera.orbit(0, 0) return true", "render-2")
    time.sleep(0.6)
    g.wait_presents(4)
    g.present_screenshot(str(s.out / "pulse-render-3.png"))
    r3 = np.asarray(Image.open(s.out / "pulse-render-3.png").convert("RGB")).astype(np.int32)
    s.check(differ(r1, r2) == 0 and differ(r2, r3) == 0 and rpc(s, action="status")["time"] == th,
            f"photo renders of the held moment: the same picture each time ({differ(r1, r2)}, {differ(r2, r3)} pixels; "
            f"time {rpc(s, action='status')['time']} == {th})")
    photo_lua("rood.camera.finish() rood.game.resume() return true")
    g.run(4)
    write_json(s.user_file, entries("standard_surface"))
    settle(g)
    g.call("editor_ui", visible=True)


def check_editor(s):
    g = s.game
    windows = {w["id"]: w for w in g.call("lua", action="windows")["windows"]}
    if not s.check(WINDOW in windows, f"the Material graph window is registered ({sorted(windows)})"):
        return
    g.call("lua", action="window", id=WINDOW, open=True, focus=True)
    for _ in range(4):
        g.call("redraw")

    def lua(code):
        r = g.lua(code, plugin=PLUGIN)
        if not r.get("ok"):
            return {"error": r.get("error")}
        res = r.get("results") or []
        return res[0] if res else None

    s.check(lua("return frames > 0") is True, "the window drew")
    s.check(lua("return select_graph('t1')") is True, "the editor selects t1")
    for _ in range(3):
        g.call("redraw")
    st = lua("return { name = current and current.name, nodes = current and #current.graph.nodes, ok = current and current.ok }")
    s.check(st and st["name"] == "t1" and st["nodes"] >= 4, f"the editor holds the graph ({st})")
    s.check(lua("return set_knob('mul', 'b', {0.5, 0.5, 1})") is True, "a knob set through the editor's path")
    for _ in range(3):
        g.call("redraw")
    node = next(n for n in rpc(s, action="get", name="t1")["graph"]["nodes"] if n["id"] == "mul")
    s.check(node["knobs"]["b"] == [0.5, 0.5, 1], f"...reaches the graph ({node['knobs']})")
    s.check(lua("return select_node('mul') and selected_node") == "mul", "a node selected shows in the panel")
    for _ in range(3):
        g.call("redraw")
    g.wait_presents(2)
    g.present_screenshot(str(s.out / "editor.png"))  # (for a look; the test checks nothing in it)
    s.check(lua("return draw_errors") == 0, "no draw errors")
    windows = {w["id"]: w for w in g.call("lua", action="windows")["windows"]}
    s.check(windows[WINDOW]["open"], "the window is open")


SECTIONS = [check_generate, check_identity, check_live, check_time, check_editor]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(es.pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names")
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else es.OUT / "material_graph"
    out.mkdir(parents=True, exist_ok=True)
    user_file = out / "materials.json"
    write_json(user_file, {"format": 1, "presets": {}, "entries": []})
    for d in ("graphs", "generated"):
        shutil.rmtree(out / d, ignore_errors=True)
    env = {"ROOD_MATERIAL_CATALOG": str(ROOT / "pc/assets/materials/catalog.json"),
           "ROOD_MATERIAL_CATALOG_USER": str(user_file), "ROOD_MATERIALS": str(out / "maps"),
           "ROOD_MATERIALS_SHIPPED": str(out / "shipped-maps"), "ROOD_MATERIALS_BAKED": str(out / "baked"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports"), "ROOD_MATERIAL_GRAPHS": str(out / "graphs"),
           "ROOD_SHADERS_GENERATED": str(out / "generated"), "ROOD_GI": "0"}
    args.out = str(out)
    s = es.start(args, "material_graph", extra_env=env)
    if s is None:
        return 77
    s.user_file, s.graphs, s.fixed = user_file, out / "graphs", None
    try:
        only = set(args.only.split(",")) if args.only else None
        for section in SECTIONS if not s.failures else []:
            name = section.__name__[len("check_"):]
            if only is None or name in only:
                print(f"-- {name}", flush=True)
                try:
                    section(s)
                except RpcError as e:
                    s.check(False, f"{name}: RPC error {e}")
    finally:
        status = es.finish(s, "node material graphs: generation, identity, live reload, the editor")
    return status


if __name__ == "__main__":
    sys.exit(main())
