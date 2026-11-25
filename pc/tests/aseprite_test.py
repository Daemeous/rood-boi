#!/usr/bin/env python3
"""The Aseprite round trip without Aseprite or the game: pc/tools/aseprite_io.py, the .aseprite files of paint_roundtrip.py
(export --painter aseprite, import, watch), the WebSocket (miniws.py) and the live bridge (paint_live.py) up to the RPC call.

    python3 pc/tests/aseprite_test.py"""
import argparse
import io
import json
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
os.environ["ROOD_PAINT_PRESETS_USER"] = os.path.join(tempfile.gettempdir(), "aseprite_test_no_user_presets.json")  # (the owner's own file is not read)
import aseprite_io as ase  # noqa: E402
import material_presets as mp  # noqa: E402
import paint_channels as pch  # noqa: E402
import miniws  # noqa: E402
import paint_live  # noqa: E402
import paint_roundtrip as pr  # noqa: E402
import room_blender as rb  # noqa: E402

failures = 0


def check(ok, message):
    global failures
    print(("ok:   " if ok else "FAIL: ") + message)
    failures += not ok


def chunk(kind, body):
    return struct.pack("<IH", 6 + len(body), kind) + body


def build(path, w, h, layers, frames, depth=32, palette=None, old_palette=None, transparent=0):
    """A file the way Aseprite lays it out, with the variations the writer does not make. layers: (name, kind, flags, level);
    frames: per frame a list of cels (layer index, x, y, kind 0|1|2, data: RGBA/gray/index array, or the linked frame)."""
    body = b""
    for fi, cels in enumerate(frames):
        chunks = []
        if fi == 0:
            if palette:
                chunks.append(chunk(0x2019, struct.pack("<III8x", len(palette), 0, len(palette) - 1)
                                    + b"".join(struct.pack("<HBBBB", 0, *c) for c in palette)))
            if old_palette:
                chunks.append(chunk(0x0004, struct.pack("<HBB", 1, 0, len(old_palette) % 256) + b"".join(bytes(c) for c in old_palette)))
            for name, kind, flags, level in layers:
                n = name.encode()
                chunks.append(chunk(0x2004, struct.pack("<HHHHHHB3x", flags, kind, level, 0, 0, 0, 255) + struct.pack("<H", len(n)) + n))
        chunks.append(chunk(0x2008, b"\0" * 4))  # an unknown chunk the reader must skip
        for index, x, y, kind, data in cels:
            head = struct.pack("<HhhBHh5x", index, x, y, 255, kind, 0)
            if kind == 1:
                chunks.append(chunk(0x2005, head + struct.pack("<H", data)))
                continue
            payload = np.ascontiguousarray(data, np.uint8)
            hh, ww = payload.shape[:2]
            raw = payload.tobytes()
            chunks.append(chunk(0x2005, head + struct.pack("<HH", ww, hh) + (raw if kind == 0 else zlib.compress(raw))))
        frame_body = b"".join(chunks)
        body += struct.pack("<IHHHHI", 16 + len(frame_body), 0xF1FA, len(chunks), 100, 0, len(chunks)) + frame_body
    header = struct.pack("<IHHHHHI", 128 + len(body), 0xA5E0, len(frames), w, h, depth, 1) + struct.pack("<HII", 100, 0, 0)
    header += struct.pack("<B3xH", transparent, 256)
    header = header.ljust(128, b"\0")
    pathlib.Path(path).write_bytes(header + body)


def gray(values):
    out = np.empty(values.shape + (4,), np.uint8)
    out[..., :3] = values[..., None]
    out[..., 3] = 255
    return out


with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)

    # ---------------------------------------------------------------- the file format
    rgba = np.random.default_rng(1).integers(0, 256, (6, 8, 4), dtype=np.uint8)
    ase.write(d / "a.aseprite", 8, 6, [{"name": "Base", "pixels": rgba, "locked": True},
                                       {"name": "Metal", "pixels": gray(np.full((6, 8), 40, np.uint8)), "opacity": 153},
                                       {"name": "Roughness", "pixels": gray(np.full((6, 8), 200, np.uint8)), "visible": False}])
    a = ase.read(d / "a.aseprite")
    check([layer.name for layer in a.layers] == ["Base", "Metal", "Roughness"] and (a.width, a.height) == (8, 6), "the writer's file reads back: layers bottom to top")
    check(np.array_equal(a.layers[0].pixels, rgba) and (a.layers[1].pixels[..., 0] == 40).all(), "...with their pixels")
    check(a.layers[0].locked and not a.layers[1].locked and a.layers[1].opacity == 153 and not a.layers[2].visible and a.layers[1].visible, "...locked, visible and opacity flags")
    raw = (d / "a.aseprite").read_bytes()
    check(struct.unpack_from("<I", raw, 0)[0] == len(raw) and raw[4:6] == b"\xe0\xa5", "the header's size and magic are right")

    # as Aseprite saves: a group with a layer inside, a stray user layer above, order changed (Roughness under Metal), a partial cel at an
    # offset, a linked cel in a second frame, an unknown chunk, a palette chunk and the old one
    layers = [("Game colour (reference)", 0, 3, 0), ("Roughness", 0, 3, 0), ("Group", 1, 3, 0), ("Sketch", 0, 3, 1), ("Metal", 0, 3, 0), ("User", 0, 3, 0)]
    patch = gray(np.array([[250, 251], [252, 253]], np.uint8))
    build(d / "b.aseprite", 8, 6, layers,
          [[(0, 0, 0, 2, rgba), (1, 0, 0, 2, gray(np.full((6, 8), 9, np.uint8))), (3, 7, 5, 2, patch), (4, 3, 2, 2, patch), (5, 0, 0, 0, rgba)],
           [(4, 0, 0, 1, 0)]], palette=[(0, 0, 0, 255), (255, 255, 255, 255)], old_palette=[(1, 2, 3)])
    b = ase.read(d / "b.aseprite")
    check([layer.name for layer in b.layers] == [n for n, *_ in layers] and b.layer("Group") is None and b.layer("metal") is not None, "layer groups are listed but never found as channels; names match in any case")
    m = b.layer("Metal").pixels
    check(m.shape == (6, 8, 4) and (m[2:4, 3:5, 0] == patch[..., 0]).all() and (m[..., 3] > 0).sum() == 4, "a cel that covers part of the canvas sits at its offset, the rest transparent")
    check((b.layer("Sketch").pixels[5, 7, 0] == 250) and (b.layer("Sketch").pixels[..., 3] > 0).sum() == 1, "a cel hanging off the canvas's corner is clipped")
    for kind, depth, data, expect in ((0, 16, np.dstack([np.full((6, 8), 77, np.uint8), np.full((6, 8), 255, np.uint8)]), 77),
                                      (2, 8, np.full((6, 8), 1, np.uint8), 255)):
        build(d / f"c{depth}.aseprite", 8, 6, [("Metal", 0, 3, 0)], [[(0, 0, 0, kind, data)]], depth=depth,
              palette=[(0, 0, 0, 255), (255, 255, 255, 255)], transparent=0)
        c = ase.read(d / f"c{depth}.aseprite").layer("Metal").pixels
        check((c[..., 0] == expect).all() and (c[..., 3] == 255).all(), f"colour depth {depth} reads as RGBA ({'grayscale' if depth == 16 else 'indexed through the palette'})")
    build(d / "linked.aseprite", 8, 6, [("Metal", 0, 3, 0)], [[(0, 0, 0, 0, gray(np.full((6, 8), 5, np.uint8)))], [(0, 0, 0, 1, 0)]])
    check((ase.read(d / "linked.aseprite").layer("Metal").pixels[..., 0] == 5).all(), "a raw cel (type 0) reads")
    try:
        (d / "junk").write_bytes(b"not an aseprite file" * 20)
        ase.read(d / "junk")
        check(False, "junk is refused")
    except ValueError:
        check(True, "a file that is not Aseprite's is refused with a ValueError")

    # ---------------------------------------------------------------- a package with .aseprite files
    positions = np.array([[[0, 0, 0], [512, 0, 0], [0, 0, 512]], [[512, 0, 0], [512, 0, 512], [0, 0, 512]]], np.float32)
    colours = np.full((2, 3, 3), 0.5, np.float32)
    tris = [{"tpage": 17, "clut": 1, "group": 0, "flags": 0, "uv": [[0, 0], [7, 0], [0, 5]], "polygon": 0x108},
            {"tpage": 17, "clut": 1, "group": 1, "flags": 0, "uv": [[7, 0], [7, 5], [0, 5]], "polygon": 0x208}]
    uses = [{"key": "aaaaaaaaaa", "tpage": 17, "clut": 1, "box": [0, 0, 7, 5]}]
    found = rb.assign(tris, uses)
    groups = [{"offset": 0x100, "first": 0x108, "end": 0x128}, {"offset": 0x200, "first": 0x208, "end": 0x228}]
    textures = {"aaaaaaaaaa": {"w": 8, "h": 6, "box": [0, 0, 7, 5], "tpage": 17, "clut": 1}}
    labels, split = rb.split_shared(tris, found, groups, textures, always=True)
    base = np.zeros((6, 8, 4), np.uint8)
    base[..., :3] = (139, 98, 65)
    base[..., 3] = 255
    bake = d / "bake"
    (bake / "aaaaaaaaaa" / "@g100").mkdir(parents=True)
    Image.fromarray(np.full((6, 8), 120, np.uint8), "L").save(bake / "aaaaaaaaaa" / "@g100" / "roughness.png")
    Image.fromarray(np.tile(np.arange(8, dtype=np.uint8) * 20, (6, 1)), "L").save(bake / "aaaaaaaaaa" / "@g100" / "metal.png")

    def make_package(name):
        pkg = d / name
        pkg.mkdir()
        pr.write_package(pkg, positions, colours, tris, found, split, labels, {"aaaaaaaaaa": base}, {"target": "room", "zone": 9, "room": 17},
                         pr.seed_layers([bake]), aseprite=True)
        return pkg
    pkg = make_package("pkg")
    check(sorted(p.name for p in (pkg / "aseprite").iterdir()) == ["aaaaaaaaaa_g100.aseprite", "aaaaaaaaaa_g200.aseprite"], "an .aseprite file per material, named as the material")
    sprite = ase.read(pkg / "aseprite" / "aaaaaaaaaa_g100.aseprite")
    names = [layer.name for layer in sprite.layers]
    check(names == ["Game colour (reference)", "Height", "Metal", "Roughness", "Material"] and (sprite.width, sprite.height) == (8, 6), f"layers bottom to top (the channel table's order) at the texture's own size: {names}")
    check(sprite.layer("Material").opacity == 178 and sprite.layer("Material").visible and not sprite.layer("Material").pixels.any(), "Material above them at 70%, shown, empty")
    check(sprite.layers[0].locked and np.array_equal(sprite.layers[0].pixels, base), "the game's colour is locked and is the texture")
    check(sprite.layer("Metal").opacity == 153 and sprite.layer("Metal").visible and not sprite.layer("Roughness").visible and not sprite.layer("Height").visible,
          "Metal at about 60% and shown, Roughness and Height hidden")
    check((sprite.layer("Roughness").pixels[..., 0] == 120).all() and (sprite.layer("Metal").pixels[..., 0] == np.arange(8) * 20).all(), "Metal and Roughness are seeded from the game's maps")
    check((sprite.layer("Height").pixels[..., 0] == 128).all() and (sprite.layer("Height").pixels[..., 3] == 255).all(), "Height is seeded flat (128) where the game has no height map")

    def args_for(source, name):
        return argparse.Namespace(dir=str(source), layer=str(d / f"layer-{name}"), catalog=str(d / f"cat-{name}.json"), normal_format="auto", keep_seed=False)

    def tree(root):
        return sorted(str(p.relative_to(root)) for p in pathlib.Path(root).rglob("*") if p.is_file()) if pathlib.Path(root).exists() else []

    r = pr.run_import(args_for(pkg, "none"), say=lambda *_: None)
    check(not r["changed"] and not tree(d / "layer-none") and not (d / "cat-none.json").exists(), "an exported .aseprite imported unpainted writes nothing (no map, no catalog entry)")

    # a pencil stroke: one texel of Metal, as Aseprite saves it (a fresh file: order changed, a user layer above, a group, the cel at an offset)
    one = make_package("one")
    time.sleep(0.01)
    s = ase.read(one / "aseprite" / "aaaaaaaaaa_g100.aseprite")
    layout = [("Game colour (reference)", 0, 1, 0), ("Notes", 0, 3, 0), ("Folder", 1, 3, 0), ("Roughness", 0, 3, 0), ("Metal", 0, 3, 0)]
    patch = np.zeros((2, 3, 4), np.uint8)
    patch[1, 1] = (255, 255, 255, 255)  # (a texel at (5, 2) of the canvas)
    build(one / "aseprite" / "aaaaaaaaaa_g100.aseprite", 8, 6, layout,
          [[(0, 0, 0, 2, base), (1, 0, 0, 2, np.full((6, 8, 4), 255, np.uint8)), (3, 0, 0, 2, s.layer("Roughness").pixels), (4, 4, 1, 2, patch)]])
    result = pr.run_import(args_for(one, "one"), say=lambda *_: None)
    layer = d / "layer-one"
    check(tree(layer) == ["aaaaaaaaaa/@g100/metal.png"], f"a stroke on Metal writes that channel only (no roughness, no other object): {tree(layer)}")
    metal = np.asarray(Image.open(layer / "aaaaaaaaaa/@g100/metal.png"))
    seed_metal = np.tile(np.arange(8, dtype=np.uint8) * 20, (6, 1))
    expect = seed_metal.copy()
    expect[2, 5] = 255
    check(np.array_equal(metal, expect), "...and only that texel differs from the game's map (the rest of the layer, in or out of the cel, is the game's)")
    entries = json.loads((d / "cat-one.json").read_text())["entries"]
    check(len(entries) == 1 and entries[0]["object"] == {"groups": [0x100]}, "...with the catalog entry of that object")

    # erase to reset: the layer's cel emptied imports the game's map again, over the file the stroke wrote
    build(one / "aseprite" / "aaaaaaaaaa_g100.aseprite", 8, 6, layout[:1] + layout[3:], [[(0, 0, 0, 2, base)]])
    pr.run_import(args_for(one, "one"), say=lambda *_: None)
    metal = np.asarray(Image.open(layer / "aaaaaaaaaa/@g100/metal.png"))
    check(np.array_equal(metal, seed_metal), "erasing the strokes (an emptied layer) resets the map to the game's: the written file follows")

    # roughness alone, a soft-edged (partially opaque) texel still counts as painted; the colour layer is never imported
    two = make_package("two")
    time.sleep(0.01)
    rough = s.layer("Roughness").pixels.copy()
    rough[0, 0] = (30, 30, 30, 90)
    edited = np.array(base)
    edited[0, 0, :3] = 1
    build(two / "aseprite" / "aaaaaaaaaa_g100.aseprite", 8, 6, layout[:1] + layout[3:], [[(0, 0, 0, 2, edited), (1, 0, 0, 2, rough)]])
    pr.run_import(args_for(two, "two"), say=lambda *_: None)
    check(tree(d / "layer-two") == ["aaaaaaaaaa/@g100/roughness.png"] and int(np.asarray(Image.open(d / "layer-two/aaaaaaaaaa/@g100/roughness.png"))[0, 0]) == 30,
          "a roughness stroke writes roughness.png only; an edited colour layer is not imported")

    # newest wins: a PNG painter's packed export newer than the .aseprite beats it
    three = make_package("three")
    time.sleep(0.05)
    mr_path = three / "tex" / "aaaaaaaaaa_g100_MetallicRoughness.png"
    mr = np.asarray(Image.open(mr_path).convert("RGB")).copy()
    mr[3, 3, 2] = 255
    pr.save_image(mr_path, mr)
    pr.run_import(args_for(three, "three"), say=lambda *_: None)
    got = np.asarray(Image.open(d / "layer-three/aaaaaaaaaa/@g100/metal.png"))
    check(tree(d / "layer-three") == ["aaaaaaaaaa/@g100/metal.png"] and int(got[3, 3]) == 255, "a PNG export saved after the .aseprite file is the newer: it wins its channel")

    # ---------------------------------------------------------------- watch
    four = make_package("four")
    stop, lines = threading.Event(), []
    ns = argparse.Namespace(dir=str(four), layer=str(d / "layer-four"), catalog=str(d / "cat-four.json"), debounce=0.3)
    thread = threading.Thread(target=pr.watch_package, args=(ns, stop, lines.append))
    thread.start()
    time.sleep(0.5)
    started = time.monotonic()
    path = four / "aseprite" / "aaaaaaaaaa_g100.aseprite"
    column = np.zeros((6, 8, 4), np.uint8)
    column[:, 1] = 255
    build(path, 8, 6, layout[:1] + layout[3:], [[(0, 0, 0, 2, base), (1, 0, 0, 2, column)]])
    # (the Roughness layer is all 255 in column 1 only)
    for _ in range(40):
        if (d / "layer-four/aaaaaaaaaa/@g100/roughness.png").exists():
            break
        time.sleep(0.05)
    took = time.monotonic() - started
    stop.set()
    thread.join()
    rough = np.asarray(Image.open(d / "layer-four/aaaaaaaaaa/@g100/roughness.png")) if (d / "layer-four/aaaaaaaaaa/@g100/roughness.png").exists() else None
    check(rough is not None and (rough[:, 1] == 255).all() and (rough[:, 2] == 120).all(), "watch imports a saved file's channel")
    check(took < 1.0, f"...within a second of the save ({took * 1000:.0f} ms, debounced 300 ms)")
    check(sum("aaaaaaaaaa_g100.aseprite" in line for line in lines) == 1, f"...one line per import: {[line for line in lines if 'g100' in line]}")


    # ---------------------------------------------------------------- pack and unpack
    import zipfile
    src = make_package("packsrc")
    painted = np.array(base)
    sw = [pp for pp in mp.load() if pp["id"] == "worn_steel"][0]
    rough_px = layout  # (the layer table of the files written above)
    sprite = ase.read(src / "aseprite" / "aaaaaaaaaa_g100.aseprite")
    metal_l, rough_l, mat_l = sprite.layer("Metal").pixels.copy(), sprite.layer("Roughness").pixels.copy(), sprite.layer("Material").pixels.copy()
    metal_l[1, 2, :3] = 255
    metal_l[4, 6, :3] = 7
    rough_l[0, 0, :3] = 33
    mat_l[3, 3] = (*[int(sw["colour"][i:i + 2], 16) for i in (1, 3, 5)], 255)
    new_layers = [{"name": l.name, "pixels": {"Metal": metal_l, "Roughness": rough_l, "Material": mat_l}.get(l.name, l.pixels), "visible": l.visible,
                   "locked": l.locked, "opacity": l.opacity} for l in sprite.layers]
    ase.write(src / "aseprite" / "aaaaaaaaaa_g100.aseprite", sprite.width, sprite.height, new_layers, palette=sprite.palette)
    pr.run_import(args_for(src, "packdirect"), say=lambda *_: None)
    packfile = d / "x.roodpack.zip"
    check(pr.pack_package(argparse.Namespace(dir=str(src), out=str(packfile)), say=lambda *_: None) == 0 and packfile.exists(), "pack writes one file")
    with zipfile.ZipFile(packfile) as z:
        names = z.namelist()
        meta = json.loads(z.read("pack.json"))
        counts = {n: int((np.asarray(Image.open(io.BytesIO(z.read(n))).convert("RGBA"))[..., 3] > 0).sum()) for n in names if n.endswith(".png")}
    check(not any("reference" in n.lower() or "colour" in n.lower() or n.endswith("BaseColor.png") for n in names) and sorted(counts) == [
        "aaaaaaaaaa_g100/material.png", "aaaaaaaaaa_g100/metal.png", "aaaaaaaaaa_g100/roughness.png"], f"the pack has the painted layers and no reference layer ({names})")
    check(counts["aaaaaaaaaa_g100/metal.png"] == 2 and counts["aaaaaaaaaa_g100/roughness.png"] == 1 and counts["aaaaaaaaaa_g100/material.png"] == 1,
          f"...and only the painted pixels, not the game's own values ({counts})")
    check(packfile.stat().st_size < 3000, f"...so it is small ({packfile.stat().st_size} bytes)")
    texture_bytes = base.tobytes()
    check(all(texture_bytes not in z for z in (zipfile.ZipFile(packfile).read(n) for n in names)) and meta["target"] == "room" and meta["pages"], "the pack's own text names what it is for")
    fresh = make_package("packdst")
    pr.unpack_package(argparse.Namespace(pack=str(packfile), dir=str(fresh), layer=str(d / "layer-packdst"), catalog=str(d / "cat-packdst.json")), say=lambda *_: None)
    same = all(np.array_equal(np.asarray(Image.open(d / "layer-packdirect" / n)), np.asarray(Image.open(d / "layer-packdst" / n)))
               for n in tree(d / "layer-packdirect") if n.endswith(".png")) and tree(d / "layer-packdirect") == tree(d / "layer-packdst")
    check(same and tree(d / "layer-packdst"), f"unpacking onto a fresh export gives the same maps as importing the painted file directly ({tree(d / 'layer-packdst')})")
    after = ase.read(fresh / "aseprite" / "aaaaaaaaaa_g100.aseprite")
    check(int(after.layer("Metal").pixels[1, 2, 0]) == 255 and [l.name for l in after.layers] == [l.name for l in sprite.layers], "...and the fresh file now holds the painting, its layers as they were")
    other = make_package("packother")
    seedpath = other / "seed.json"
    sj = json.loads(seedpath.read_text())
    sj["room"] = 99
    seedpath.write_text(json.dumps(sj))
    try:
        pr.unpack_package(argparse.Namespace(pack=str(packfile), dir=str(other), layer=None, catalog=None), say=lambda *_: None)
        check(False, "a pack for another room is refused")
    except SystemExit as e:
        check("export what the pack is for" in str(e), "a pack for something else is refused, saying what to export")

    # ---------------------------------------------------------------- the WebSocket
    left, right = socket.socketpair()
    server, client = miniws.WebSocket(left), miniws.WebSocket(right, client=True)
    result = {}
    t = threading.Thread(target=lambda: result.update(path=server.accept()))
    t.start()
    client.connect("localhost", "/live")
    t.join()
    check(result["path"] == "/live", "the handshake: Sec-WebSocket-Accept is checked by the client and the path is the server's")
    big = bytes(range(256)) * 300
    client.send("hello")
    client.send(b"\x00\x01binary")
    client.send(big)  # (64-bit length)
    client.send(b"x" * 200)  # (16-bit length)
    right.sendall(miniws.frame(miniws.TEXT, b"frag", mask=True, fin=False) + miniws.frame(miniws.PING, b"p", mask=True)
                  + miniws.frame(miniws.CONT, b"men", mask=True, fin=False) + miniws.frame(miniws.CONT, b"ts", mask=True))
    got = [server.recv() for _ in range(5)]
    check(got[0] == (miniws.TEXT, b"hello") and got[1] == (miniws.BINARY, b"\x00\x01binary") and got[2] == (miniws.BINARY, big) and got[3][1] == b"x" * 200,
          "masked client frames: text, binary, 16-bit and 64-bit lengths")
    check(got[4] == (miniws.TEXT, b"fragments"), "a fragmented message is joined, with a ping in the middle")
    pong = right.recv(16)
    check(pong[:2] == b"\x8a\x01" and pong[2:3] == b"p", "the ping was answered with a pong")
    server.send("ok")
    check(client.recv() == (miniws.TEXT, b"ok"), "the server's frames are unmasked and read by the client")
    right.sendall(miniws.frame(miniws.CLOSE, struct.pack(">H", 1000), mask=True))
    check(server.recv() is None, "a close frame ends the message stream")
    left.close(), right.close()
    left, right = socket.socketpair()
    right.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
    try:
        miniws.WebSocket(left).accept()
        check(False, "a plain HTTP request is refused")
    except miniws.Closed:
        check(right.recv(64).startswith(b"HTTP/1.1 400"), "a request that is not a WebSocket upgrade gets a 400")
    check(miniws.accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "the accept key is RFC 6455's example")

    # ---------------------------------------------------------------- the bridge (without a game)
    five = make_package("five")
    bridge = paint_live.Bridge(five, layer=str(d / "layer-five"), catalog=str(d / "cat-five.json"))
    pushed = []
    bridge.push = lambda key, obj, channel, array: pushed.append((key, obj, channel, array.copy())) or True
    cel = np.zeros((2, 3, 4), np.uint8)
    cel[1, 1] = (255, 255, 255, 255)

    def message(layer_name, cel, x, y, material="aaaaaaaaaa_g100"):
        header = json.dumps({"material": material, "layer": layer_name, "x": x, "y": y, "w": cel.shape[1], "h": cel.shape[0], "canvas": [8, 6]}).encode()
        return struct.pack(">I", len(header)) + header + cel.tobytes()
    reply = bridge.message(None, miniws.BINARY, message("Metal", cel, 4, 1))
    check(reply["ok"] and reply["game"] and len(pushed) == 1, f"a stroke message is applied and pushed: {reply}")
    key, obj, channel, array = pushed[0]
    check((key, obj, channel) == ("aaaaaaaaaa", "g100", "metal") and np.array_equal(array, expect), "...as that object's metal map: the stroke over the game's values, same as import")
    check(not (d / "layer-five").exists() and len((json.loads((d / "cat-five.json").read_text()))["entries"]) == 1, "...no map file written, the catalog entry made")
    again = bridge.message(None, miniws.BINARY, message("Metal", cel, 4, 1))
    check(again["note"] == "unchanged" and len(pushed) == 1, "the same stroke again pushes nothing")
    bad = bridge.message(None, miniws.BINARY, message("Metal", cel, 4, 1, material="nope"))
    check(not bad["ok"] and len(pushed) == 1, "a material that is not in the package is refused")
    resized = json.dumps({"material": "aaaaaaaaaa_g100", "layer": "Metal", "x": 0, "y": 0, "w": 0, "h": 0, "canvas": [16, 12]}).encode()
    check(not bridge.message(None, miniws.BINARY, struct.pack(">I", len(resized)) + resized)["ok"], "a resized canvas is refused")
    empty = json.dumps({"material": "aaaaaaaaaa_g100", "layer": "Metal", "x": 0, "y": 0, "w": 0, "h": 0, "canvas": [8, 6]}).encode()
    bridge.message(None, miniws.BINARY, struct.pack(">I", len(empty)) + empty)
    check(len(pushed) == 2 and np.array_equal(pushed[1][3], seed_metal), "an emptied layer (no cel) pushes the game's map back")
    check(not bridge.message(None, miniws.BINARY, b"\x00")["ok"], "a short message is refused, not a crash")

    # ---------------------------------------------------------------- material presets (swatches on the Material layer)
    presets = mp.load()
    by = {p["id"]: p for p in presets}
    check(len(presets) >= 10 and all(0 <= p["metal"] <= 1 and 0 <= p["roughness"] <= 1 for p in presets), f"the shipped presets: {len(presets)}, values 0..1")
    check({"polished_steel", "worn_steel", "dark_iron", "rusted_iron", "gold", "bronze_brass", "silver", "leather", "cloth", "stone", "wood", "skin"} <= set(by), "...the starter set")
    rgbs = [p["rgb"] for p in presets]
    check(all(max(c) - min(c) >= 60 for c in rgbs) and min(max(abs(a - b) for a, b in zip(x, y)) for i, x in enumerate(rgbs) for y in rgbs[:i]) > 40,
          "...swatches are never gray and every two differ clearly")
    sprite = ase.read(pkg / "aseprite" / "aaaaaaaaaa_g100.aseprite")
    check([e[4] for e in sprite.palette[:len(presets)]] == [p["name"] for p in presets] and [e[:3] for e in sprite.palette[:len(presets)]] == rgbs,
          "the exported file's palette has the swatches, named")
    check(sprite.palette[len(presets)][:4] == (0, 0, 0, 255) and sprite.palette[-1][:4] == (255, 255, 255, 255) and sprite.palette[-1][4] == "1.0", "...then the grays")

    steel, gold = by["worn_steel"], by["gold"]
    sw = np.zeros((6, 8, 4), np.uint8)  # the Material layer: worn steel top left 3 x 2, gold bottom right 2 x 2, a stray colour, a half-transparent steel
    sw[0:2, 0:3] = (*steel["rgb"], 255)
    sw[4:6, 6:8] = (*gold["rgb"], 255)
    sw[3, 3] = (1, 2, 3, 255)
    sw[3, 4] = (*steel["rgb"], 90)
    got = mp.expand(sw, presets)
    metal_v, metal_m = got["metal"]
    rough_v, rough_m = got["roughness"]
    check(metal_m.sum() == 6 + 4 + 1 and (metal_v[0:2, 0:3] == 255).all() and (rough_v[0:2, 0:3] == round(steel["roughness"] * 255)).all()
          and (rough_v[4:6, 6:8] == round(gold["roughness"] * 255)).all() and not metal_m[3, 3], "expand: each swatch is its preset's metal and roughness; a stray colour is no preset")
    check(metal_m[3, 4], "...a translucent stroke of a swatch still counts")
    near = sw.copy()
    near[5, 0] = (*(np.array(steel["rgb"]) - 5), 255)
    check(mp.expand(near, presets)["metal"][1][5, 0], "...a colour within a few levels of a swatch is that swatch (a brush's blending)")
    check(not mp.expand(np.zeros((6, 8, 4), np.uint8), presets), "...an empty layer expands to nothing")

    # through the import: Material alone, then with a Metal stroke on top (precedence)
    six = make_package("six")
    spath = six / "aseprite" / "aaaaaaaaaa_g100.aseprite"

    def save_layers(path, material=None, metal=None, rough=None):
        rows = [{"name": ly.name, "pixels": ly.pixels, "locked": ly.locked, "visible": ly.visible, "opacity": ly.opacity} for ly in ase.read(path).layers]
        if material is not None:
            rows[4]["pixels"] = material
        if metal is not None:
            rows[2]["pixels"] = metal
        if rough is not None:
            rows[3]["pixels"] = rough
        ase.write(path, 8, 6, rows, palette=ase.read(path).palette)
    save_layers(spath, material=sw)
    r = pr.run_import(args_for(six, "six"), say=lambda *_: None)
    check(sorted(n for _, _, ns_ in r["changed"] for n in ns_) == ["metal", "roughness"], f"import: a Material stroke alone writes metal and roughness: {r['changed'] and r['changed'][0][2]}")
    out_metal = np.asarray(Image.open(d / "layer-six/aaaaaaaaaa/@g100/metal.png"))
    out_rough = np.asarray(Image.open(d / "layer-six/aaaaaaaaaa/@g100/roughness.png"))
    seed_m = np.tile(np.arange(8, dtype=np.uint8) * 20, (6, 1))
    check((out_metal[0:2, 0:3] == 255).all() and (out_rough[0:2, 0:3] == 107).all() and (out_rough[4:6, 6:8] == 51).all(), "...the swatches' values where painted")
    outside = np.ones((6, 8), bool)
    outside[0:2, 0:3] = outside[4:6, 6:8] = outside[3, 4] = False
    check((out_metal[outside] == seed_m[outside]).all() and (out_rough[outside] == 120).all(), "...the game's own values everywhere else")
    # precedence: Metal painted at (0,0) = 40 (differs from the game's 0? the seed there is 0: yes), Roughness layer left as exported (the game's values)
    mpx = ase.read(spath).layer("Metal").pixels.copy()
    mpx[0, 1, :3] = 40
    mpx[0, 0, :3] = 0  # (still the game's value 0 at (0,0): untouched, so the preset's metal shows)
    save_layers(spath, metal=mpx)
    pr.run_import(args_for(six, "six"), say=lambda *_: None)
    out_metal = np.asarray(Image.open(d / "layer-six/aaaaaaaaaa/@g100/metal.png"))
    out_rough = np.asarray(Image.open(d / "layer-six/aaaaaaaaaa/@g100/roughness.png"))
    check(out_metal[0, 1] == 40 and out_metal[0, 0] == 255 and out_metal[0, 2] == 255, "precedence: a Metal pixel painted over a swatch wins, an untouched one gives way to the preset")
    check((out_rough[0:2, 0:3] == 107).all(), "...and the other channel (Roughness left as exported) is the preset's still")
    # no Material strokes: exactly the old import; an emptied Material resets
    save_layers(spath, material=np.zeros((6, 8, 4), np.uint8), metal=np.asarray(ase.read(pkg / "aseprite" / "aaaaaaaaaa_g100.aseprite").layer("Metal").pixels))
    pr.run_import(args_for(six, "six"), say=lambda *_: None)
    out_metal = np.asarray(Image.open(d / "layer-six/aaaaaaaaaa/@g100/metal.png"))
    check((out_metal == seed_m).all(), "an emptied Material layer (and Metal back to the game's) resets the map to the game's")

    # a Material layer alone (no Metal / Roughness layer in the file: a user's own file)
    seven = make_package("seven")
    build(seven / "aseprite" / "aaaaaaaaaa_g100.aseprite", 8, 6, [("Game colour (reference)", 0, 3, 0), ("Material", 0, 3, 0)], [[(0, 0, 0, 2, base), (1, 0, 0, 2, sw)]])
    pr.run_import(args_for(seven, "seven"), say=lambda *_: None)
    check(np.asarray(Image.open(d / "layer-seven/aaaaaaaaaa/@g100/metal.png"))[0, 0] == 255, "a file with only a Material layer imports it")

    # user presets: override a value, add a swatch, drop one
    user = d / "user-presets.json"
    user.write_text(json.dumps({"presets": [{"id": "worn_steel", "colour": "#2962ff", "metal": 0.5, "roughness": 0.3},
                                            {"id": "mud", "name": "Mud", "colour": "#aabb00", "roughness": 1.0},
                                            {"id": "stone", "disabled": True},
                                            {"id": "broken", "colour": "nonsense"}, {"colour": "#fff"}]}))
    warned = []
    merged = mp.load([mp.shipped_file(), user], warn=warned.append)
    ids = [p["id"] for p in merged]
    check(ids.index("worn_steel") == ids.index("polished_steel") + 1 and next(p for p in merged if p["id"] == "worn_steel")["metal"] == 0.5, "a user preset of the same id replaces the shipped one in place")
    check(ids[-1] == "mud" and "stone" not in ids and "broken" not in ids and len(warned) == 2, f"...a new id is added after, disabled drops, broken ones are skipped with a warning ({warned})")
    exp = mp.expand(sw, merged)
    check("metal" in exp and (exp["metal"][0][0:2, 0:3] == round(0.5 * 255)).all() and not mp.expand(np.full((2, 2, 4), 255, np.uint8), merged), "...and the expansion follows")
    mud = np.zeros((1, 1, 4), np.uint8)
    mud[0, 0] = (0xaa, 0xbb, 0x00, 255)
    mv = mp.expand(mud, merged)
    check("metal" not in mv and mv["roughness"][0][0, 0] == 255, "a preset with only roughness leaves metal alone")
    check(mp.load([d / "nope.json"]) == [] and mp.load([user, d / "nope.json"], warn=lambda *_: None), "a missing file is skipped")
    (d / "bad.json").write_text("{not json")
    check(mp.load([mp.shipped_file(), d / "bad.json"], warn=warned.append) == presets, "a broken user file is warned about and the shipped presets stay")

    # the palette command: new swatches into existing files, pixels untouched
    before = ase.read(spath)
    names_before = [(ly.name, ly.opacity, ly.flags) for ly in before.layers]
    os.environ["ROOD_PAINT_PRESETS_USER"] = str(user)
    pr.refresh_palettes(argparse.Namespace(dir=str(six)), say=lambda *_: None)
    after = ase.read(spath)
    check([e[4] for e in after.palette[:len(merged)]] == [p["name"] for p in merged] and after.palette[-1][4] == "1.0", "palette: the file's palette is the new swatches, named")
    check([(ly.name, ly.opacity, ly.flags) for ly in after.layers] == names_before
          and all((a.pixels is None and b.pixels is None) or np.array_equal(a.pixels, b.pixels) for a, b in zip(before.layers, after.layers)), "...layers and pixels untouched")
    odd = d / "odd.aseprite"  # a file as Aseprite lays it out: old palette, an unknown chunk, two frames
    build(odd, 8, 6, [("Metal", 0, 3, 0)], [[(0, 0, 0, 2, gray(seed_m))], [(0, 0, 0, 1, 0)]], old_palette=[(1, 2, 3), (4, 5, 6)])
    ase.set_palette(odd, [(9, 8, 7, 255, "Nine")])
    o = ase.read(odd)
    check(o.palette == [(9, 8, 7, 255, "Nine")] and np.array_equal(o.layer("Metal").pixels[..., 0], seed_m), "set_palette on a two-frame file with an old-style palette and an unknown chunk keeps the pixels")
    os.environ["ROOD_PAINT_PRESETS_USER"] = os.path.join(tempfile.gettempdir(), "aseprite_test_no_user_presets.json")

    # the bridge: Material strokes, precedence, and presets in the greeting
    eight = make_package("eight")
    bridge = paint_live.Bridge(eight, layer=str(d / "layer-eight"), catalog=str(d / "cat-eight.json"))
    pushed = []
    bridge.push = lambda key, obj, channel, array: pushed.append((key, obj, channel, array.copy())) or True
    hello = bridge.message(None, miniws.TEXT, b"hello")
    check([p["name"] for p in hello["presets"]] == [p["name"] for p in presets] and hello["presets"][1]["colour"] == "#2962ff", "the bridge's hello carries the presets for the extension's window")
    reply = bridge.message(None, miniws.BINARY, message("Material", sw, 0, 0))
    check(reply["ok"] and sorted(c for _, _, c, _ in pushed) == ["metal", "roughness"], f"a Material stroke pushes both maps: {reply}")
    maps = {c: a for _, _, c, a in pushed}
    check((maps["metal"][0:2, 0:3] == 255).all() and (maps["roughness"][4:6, 6:8] == 51).all() and (maps["roughness"][outside] == 120).all() and (maps["metal"][outside] == seed_m[outside]).all(),
          "...the preset values over the game's maps, same as import")
    pushed.clear()
    stroke_px = np.zeros((1, 1, 4), np.uint8)
    stroke_px[0, 0] = (40, 40, 40, 255)
    bridge.message(None, miniws.BINARY, message("Metal", stroke_px, 1, 0))
    check(len(pushed) == 1 and pushed[0][2] == "metal" and pushed[0][3][0, 1] == 40 and pushed[0][3][0, 0] == 255 and pushed[0][3][1, 2] == 255,
          "then a Metal stroke over the swatch: only metal is pushed, the stroke wins its texel, the swatch stays elsewhere")
    pushed.clear()
    bridge.message(None, miniws.BINARY, message("Material", np.zeros((0, 0, 4), np.uint8), 0, 0))
    maps = {c: a for _, _, c, a in pushed}
    check((maps["roughness"] == 120).all() and maps["metal"][0, 1] == 40 and maps["metal"][0, 0] == seed_m[0, 0], "erasing the Material layer returns the game's values, the Metal stroke staying")
    # a bridge that starts after a saved file: its Material layer is the baseline
    nine = make_package("nine")
    save_layers(nine / "aseprite" / "aaaaaaaaaa_g100.aseprite", material=sw)
    bridge = paint_live.Bridge(nine, layer=str(d / "layer-nine"), catalog=str(d / "cat-nine.json"))
    pushed = []
    bridge.push = lambda key, obj, channel, array: pushed.append((key, obj, channel, array.copy())) or True
    bridge.message(None, miniws.BINARY, message("Roughness", np.zeros((0, 0, 4), np.uint8), 0, 0))
    check(len(pushed) == 1 and (pushed[0][3][0:2, 0:3] == 107).all(), "a Roughness message uses the saved file's Material layer for what the extension has not sent yet")
    # a changed preset file reaches the materials the bridge holds
    n0 = len(pushed)
    user.write_text(json.dumps({"presets": [{"id": "worn_steel", "colour": "#2962ff", "metal": 1.0, "roughness": 0.9}]}))
    os.environ["ROOD_PAINT_PRESETS_USER"] = str(user)
    bridge.message(None, miniws.BINARY, message("Metal", stroke_px, 5, 5))
    check(any(c == "roughness" and (a[0:2, 0:3] == round(0.9 * 255)).all() for _, _, c, a in pushed[n0:]), "a changed preset file is re-read on the next message: the swatch's new values are pushed")

    # ---------------------------------------------------------------- the Height channel (the channel table's row; no code of its own)
    check([r["channel"] for r in pch.ROWS] == ["height", "metal", "roughness"] and all(pch.blank(c) == b for c, b in (("height", 128), ("metal", 0), ("roughness", 255))),
          "the channel table: height, metal, roughness in layer order, blank values as the game's blank_value")
    hbake = d / "hbake"
    (hbake / "aaaaaaaaaa" / "@g100").mkdir(parents=True)
    game_height = np.tile(np.arange(8, dtype=np.uint8) * 10 + 40, (6, 1))
    Image.fromarray(game_height, "L").save(hbake / "aaaaaaaaaa" / "@g100" / "height.png")

    def make_height_package(name):
        pkg = d / name
        pkg.mkdir()
        pr.write_package(pkg, positions, colours, tris, found, split, labels, {"aaaaaaaaaa": base}, {"target": "room", "zone": 9, "room": 17},
                         pr.seed_layers([hbake]), aseprite=True)
        return pkg
    hpkg = make_height_package("hpkg")
    hfile = hpkg / "aseprite" / "aaaaaaaaaa_g100.aseprite"
    check(np.array_equal(ase.read(hfile).layer("Height").pixels[..., 0], game_height), "the Height layer is seeded from the game's height map")
    check(np.array_equal(pr.painted_seed(hpkg, json.loads((hpkg / "seed.json").read_text())["labels"]["aaaaaaaaaa@g100"], "height", 6, 8), game_height)
          and (hpkg / "seed" / "aaaaaaaaaa_g100_Height.png").exists(), "...and the package keeps it as the seed (seed/<material>_Height.png)")
    r = pr.run_import(args_for(hpkg, "hnone"), say=lambda *_: None)
    check(not r["changed"] and not tree(d / "layer-hnone"), "a Height layer left as the game's imports nothing (the relief estimate is not replaced by flat)")
    time.sleep(0.01)
    hl = ase.read(hfile)
    px = hl.layer("Height").pixels.copy()
    px[2, 3, :3] = 250  # a raised texel
    px[4, 6, :3] = 5    # a sunk one
    hrows = [{"name": ly.name, "pixels": ly.pixels, "locked": ly.locked, "visible": ly.visible, "opacity": ly.opacity} for ly in hl.layers]
    hrows[1]["pixels"] = px
    ase.write(hfile, 8, 6, hrows, palette=hl.palette)
    r = pr.run_import(args_for(hpkg, "hone"), say=lambda *_: None)
    check(tree(d / "layer-hone") == ["aaaaaaaaaa/@g100/height.png"], f"a stroke on Height writes height.png and nothing else (no normal): {tree(d / 'layer-hone')}")
    got = np.asarray(Image.open(d / "layer-hone/aaaaaaaaaa/@g100/height.png"))
    want = game_height.copy()
    want[2, 3], want[4, 6] = 250, 5
    check(np.array_equal(got, want), "...the game's height with only the painted texels changed")
    entry = json.loads((d / "cat-hone.json").read_text())["entries"][0]
    check(entry["bump"] > 0, "...and its catalog entry has relief on (bump > 0), which a painted height needs")
    flat_cat = d / "flat-shipped.json"
    flat_cat.write_text(json.dumps({"entries": [{"id": "Z", "match": {"key": "zzzzzzzzzz"}, "bump": 0, "roughness": 0.5}]}))
    was_env = os.environ.get("ROOD_MATERIAL_CATALOG")
    os.environ["ROOD_MATERIAL_CATALOG"] = str(flat_cat)
    zrec = {"key": "aaaaaaaaaa", "object": "g100", "w": 8, "h": 6, "model": None, "entry": "Z", "group_offset": 0x100}
    zargs = argparse.Namespace(catalog=str(d / "cat-z.json"))
    pr.ensure_entries(zargs, {"target": "room", "zone": 9, "room": 17}, [("z", zrec, ["metal"])])
    pr.ensure_entries(zargs, {"target": "room", "zone": 9, "room": 17}, [("z", {**zrec, "object": "g200", "group_offset": 0x200}, ["height"])])
    zentries = {e["id"]: e for e in json.loads((d / "cat-z.json").read_text())["entries"]}
    check(zentries["PT-aaaaaaaaaa-g100"]["bump"] == 0 and zentries["PT-aaaaaaaaaa-g200"]["bump"] == 1.0 and zentries["PT-aaaaaaaaaa-g200"]["roughness"] == 0.5,
          "a painted height on a surface with bump 0 turns it on (1); painting metal on it keeps the 0; the rest of the entry is the game's")
    if was_env is None:
        del os.environ["ROOD_MATERIAL_CATALOG"]
    else:
        os.environ["ROOD_MATERIAL_CATALOG"] = was_env
    # the Material layer's presets carry height when they have it
    user_presets = d / "height-presets.json"
    user_presets.write_text(json.dumps({"presets": [{"id": "bumpy", "name": "Bumpy", "colour": "#12ab34", "height": 1.0}]}))
    os.environ["ROOD_PAINT_PRESETS_USER"] = str(user_presets)
    bumpy = [p for p in mp.load() if p["id"] == "bumpy"][0]
    sw_h = np.zeros((6, 8, 4), np.uint8)
    sw_h[0:2, 0:2] = (*bumpy["rgb"], 255)
    got_h = mp.expand(sw_h, mp.load())
    check(sorted(got_h) == ["height"] and (got_h["height"][0][0:2, 0:2] == 255).all() and got_h["height"][1][0:2, 0:2].all(), "a preset with a height value expands to the height channel only")
    hbridge = paint_live.Bridge(make_height_package("hlive"), layer=str(d / "layer-hlive"), catalog=str(d / "cat-hlive.json"))
    hpushed = []
    hbridge.push = lambda key, obj, channel, array: hpushed.append((key, obj, channel, array.copy())) or True
    hello = hbridge.message(None, miniws.TEXT, b"hello")
    check([x["layer"] for x in hello["layers"]] == ["Height", "Metal", "Roughness"] and hello["preset_layer"] == "Material"
          and next(p for p in hello["presets"] if p["name"] == "Bumpy")["about"] == "height 1", f"the bridge's greeting names the layers and each preset's values: {hello['layers']}")
    unpainted = ase.read(hbridge.folder / "aseprite" / "aaaaaaaaaa_g100.aseprite").layer("Height").pixels
    reply = hbridge.message(None, miniws.BINARY, message("Height", unpainted, 0, 0))
    check(reply["ok"] and reply["note"] == "unchanged" and not hpushed, f"a Height layer still at the game's values pushes nothing (the game keeps its estimate): {reply}")
    stroke_h = np.zeros((1, 1, 4), np.uint8)
    stroke_h[0, 0] = (250, 250, 250, 255)
    reply = hbridge.message(None, miniws.BINARY, message("height", stroke_h, 2, 3))
    key, obj, channel, array = hpushed[-1]
    expect_h = game_height.copy()
    expect_h[3, 2] = 250
    check(reply["ok"] and (key, obj, channel) == ("aaaaaaaaaa", "g100", "height") and np.array_equal(array, expect_h), "a Height stroke is pushed as that object's height map: the stroke over the game's values (the layer name is any case)")
    reply = hbridge.message(None, miniws.BINARY, message("Material", sw_h, 0, 0))
    check(hpushed[-1][2] == "height" and (hpushed[-1][3][0:2, 0:2] == 255).all() and hpushed[-1][3][3, 2] == 250, "a Material swatch with a height value is pushed as height, the Height stroke staying")
    # erasing back to the game's values: a map the game had is pushed again, where it had none the map set in memory is taken away
    hpushed.clear()
    hbridge.message(None, miniws.BINARY, message("Height", np.zeros((0, 0, 4), np.uint8), 0, 0))
    hbridge.message(None, miniws.BINARY, message("Material", np.zeros((0, 0, 4), np.uint8), 0, 0))
    check(hpushed and hpushed[-1][2] == "height" and np.array_equal(hpushed[-1][3], game_height), "the Height layer erased: the game's own height map (it had one) is pushed back")
    nomap = paint_live.Bridge(make_package("nomap"), layer=str(d / "layer-nomap"), catalog=str(d / "cat-nomap.json"))
    npushed, ncleared = [], []
    nomap.push = lambda key, obj, channel, array: npushed.append((key, obj, channel, array.copy())) or True
    nomap.clear = lambda key, obj, channel: ncleared.append((key, obj, channel)) or True
    nomap.message(None, miniws.BINARY, message("Height", stroke_h, 2, 3))
    check([c for _, _, c, _ in npushed] == ["height"] and not ncleared, "a Height stroke on a texture the game has no height map for is pushed")
    nomap.message(None, miniws.BINARY, message("Height", np.zeros((0, 0, 4), np.uint8), 0, 0))
    check(ncleared == [("aaaaaaaaaa", "g100", "height")] and len(npushed) == 1, "...and erased, the map is taken away (the game's estimate shows again), not flat 128 pushed")
    nomap.message(None, miniws.BINARY, message("Metal", stroke_h, 2, 3))
    nomap.message(None, miniws.BINARY, message("Metal", np.zeros((0, 0, 4), np.uint8), 0, 0))
    check(("aaaaaaaaaa", "g100", "metal") not in ncleared and npushed[-1][2] == "metal", "(metal, which this texture's game has a map for, is pushed back as before)")
    os.environ["ROOD_PAINT_PRESETS_USER"] = os.path.join(tempfile.gettempdir(), "aseprite_test_no_user_presets.json")

    # ---------------------------------------------------------------- several targets (rood-studio and a game beside it)
    class Target:
        def __init__(self, fail=False):
            self.calls, self.fail = [], fail

        def call(self, method, **kw):
            if self.fail:
                raise OSError("gone")
            self.calls.append((method, kw.get("action"), kw.get("channel"), kw.get("key")))
            return {"pick": {"serial": 0}} if kw.get("action") == "picked" else {"locate": {"triangles": 1}}
    multi_dir = d / "multi_pkg"
    multi_dir.mkdir(exist_ok=True)
    (multi_dir / "seed.json").write_text(json.dumps({"labels": {}}))
    mb = paint_live.Bridge(multi_dir, rpc=["/nonexistent/a.sock", "/nonexistent/b.sock"], layer=str(d / "layer-multi"), catalog=str(d / "cat-multi.json"))
    first, second = Target(), Target()
    mb.conns = [first, second]
    check(mb.push("k", "o", "metal", np.zeros((2, 2), np.uint8)) and len(first.calls) == 1 and first.calls == second.calls,
          "a map is pushed to every target that is up")
    first.fail = True
    check(mb.push("k", "o", "metal", np.zeros((2, 2), np.uint8)) and mb.conns[0] is None and len(second.calls) == 2,
          "a target that went away is dropped and the other still takes the stroke")
    check(mb.game is second and [t["up"] for t in mb.targets()] == [False, True], "the locator uses the first target that is up")
    mb.conns = [None, None]
    check(not mb.push("k", "o", "metal", np.zeros((2, 2), np.uint8)) and not mb.clear("k", "o", "metal"), "with no target up a stroke is not taken (the saved file still imports)")
    # ---------------------------------------------------------------- the texel locator: Aseprite <-> the studio, with a game that is faked
    class StudioGame:
        """The studio's two actions the bridge uses: locate (recorded) and picked (what the test has the studio pick)."""
        def __init__(self):
            self.located, self.pick = [], {"serial": 0}

        def call(self, method, **kw):
            assert method == "studio"
            if kw["action"] == "locate":
                self.located.append(kw)
                return {"locate": {"triangles": 2 if kw["key"] == "k2" else 0}}
            return {"pick": self.pick}
    loc_dir = d / "locate_pkg"
    (loc_dir).mkdir()
    slices = [{"key": "k1", "box": [16, 32, 47, 63]}, {"key": "k2", "box": [64, 0, 95, 31]}]
    (loc_dir / "seed.json").write_text(json.dumps({"labels": {
        "mabc_p10_c3b70": {"key": "page", "object": "mabc", "w": 256, "h": 256, "model": "abc", "slices": slices},
        "mother_p10_c3b70": {"key": "page", "object": "mother", "w": 256, "h": 256, "model": "other", "slices": [slices[1]]},
        "5555_10": {"key": "5555", "object": "g1", "w": 8, "h": 8}}}))
    lb = paint_live.Bridge(loc_dir, layer=str(d / "layer-locate"), catalog=str(d / "cat-locate.json"))
    lb.game = StudioGame()
    check(lb.texture_of("mabc_p10_c3b70", 20, 40) == ("k1", 4, 8) and lb.texture_of("mabc_p10_c3b70", 70, 5) == ("k2", 6, 5),
          "a pixel of a model's page is a texel of the texture whose box holds it (its content key and the place in it)")
    check(lb.texture_of("mabc_p10_c3b70", 0, 0) is None and lb.texture_of("nothing", 1, 1) is None, "a pixel outside every box, or of no package's sprite, is none")
    check(lb.texture_of("5555_10", 3, 4) == ("5555", 3, 4) and lb.texture_of("5555_10", 9, 4) is None, "a room texture's sprite is the texture itself")
    reply = lb.message(None, miniws.TEXT, b'{"locate":{"material":"mabc_p10_c3b70","x":70,"y":5}}')
    check(lb.game.located == [{"action": "locate", "key": "k2", "x": 6, "y": 5}] and reply["ok"] and reply["locate"] and reply["triangles"] == 2,
          f"the cursor's pixel is asked of the studio as that texel: {lb.game.located} {reply}")
    reply = lb.message(None, miniws.TEXT, b'{"locate":{"material":"mabc_p10_c3b70","x":20,"y":40}}')
    check(reply["ok"] and reply["triangles"] == 0 and "not on the model" in reply["note"], "a texel the model shown does not use is said so")
    lb.message(None, miniws.TEXT, b'{"locate":{"material":"mabc_p10_c3b70","x":0,"y":0}}')
    check(len(lb.game.located) == 2, "a pixel in no texture is not sent to the game")
    check(lb.message(None, miniws.TEXT, b"hello")["presets"] is not None, "a plain text frame is still hello")
    check(lb.pixel_of("k2", 6, 5) == ("mabc_p10_c3b70", 70, 5), "the texel of a texture as a pixel of a page (the page that has it first)")
    check(lb.pixel_of("k2", 6, 5, "other") == ("mother_p10_c3b70", 70, 5), "...the model's own page when the pick says which model")
    check(lb.pixel_of("k2", 40, 5) is None and lb.pixel_of("nokey", 0, 0) is None, "a texel beyond the texture, or of another texture, is none")
    # the studio's pick, passed on once
    lb.game.pick = {"serial": 4, "hit": True, "key": "k1", "lx": 4, "ly": 8, "model": "abc", "scroll": False}
    check(lb.picked() is None, "the first look at the studio's pick only learns it (an old pick is not sent to a new connection)")
    lb.game.pick = {"serial": 5, "hit": True, "key": "k1", "lx": 4, "ly": 8, "model": "abc", "scroll": True}
    check(lb.picked() == {"select": {"material": "mabc_p10_c3b70", "x": 20, "y": 40, "scroll": True}}, "a new pick becomes the texel to select")
    check(lb.picked() is None, "...once")
    lb.game.pick = {"serial": 6, "hit": True, "key": "unknown", "lx": 0, "ly": 0, "scroll": False}
    check(lb.picked() is None, "a pick of a texture no sprite has is not sent")
    # over the socket: the extension's text frame, and the studio's pick pushed
    lb.pick_serial = None
    lb.game.pick = {"serial": 6}
    server = paint_live.serve(lb, 0, log=lambda text: None)
    sock = socket.create_connection(("127.0.0.1", server.server_address[1]))
    sock.settimeout(10)
    client = miniws.WebSocket(sock, client=True)
    client.connect("127.0.0.1")
    check(json.loads(client.recv()[1])["ok"], "the bridge greets the extension")
    time.sleep(0.4)  # (the pick the connection began with is learnt, not sent)
    client.send(json.dumps({"locate": {"material": "mabc_p10_c3b70", "x": 70, "y": 5}}))
    answer = json.loads(client.recv()[1])
    check(answer["locate"] and answer["triangles"] == 2 and lb.game.located[-1]["key"] == "k2", f"over the socket: the cursor's pixel is located ({answer})")
    lb.game.pick = {"serial": 7, "hit": True, "key": "k2", "lx": 6, "ly": 5, "model": "abc", "scroll": True}
    pushed = json.loads(client.recv()[1])
    check(pushed == {"select": {"material": "mabc_p10_c3b70", "x": 70, "y": 5, "scroll": True}}, f"over the socket: the studio's pick arrives as a select ({pushed})")
    sock.close()
    server.shutdown()
    # the extension's side, in Lua with Aseprite's API faked
    manifest = json.loads((ROOT / "pc/tools/aseprite/rood-live/package.json").read_text())
    check(tuple(int(v) for v in manifest["version"].split(".")) >= (1, 3, 0), f"the extension's version is bumped for the locator ({manifest['version']})")
    if shutil.which("lua5.4"):
        run = subprocess.run(["lua5.4", str(ROOT / "pc/tests/aseprite_extension_test.lua"), str(ROOT / "pc/tools/aseprite/rood-live/rood-live.lua")],
                             capture_output=True, text=True, timeout=30)
        for line in run.stdout.splitlines():
            if line.startswith("FAIL"):
                check(False, "extension (lua): " + line[6:])
        check(run.returncode == 0 and "PASS: extension" in run.stdout, f"the extension's locator in Lua (hover sends the cursor's pixel once per change, select and scroll): {run.stdout.count('ok:   ')} checks {run.stderr[-200:]}")
    else:
        print("skip: lua5.4 is not installed (the extension's Lua checks)")

    # ---------------------------------------------------------------- real Aseprite (batch, no window; skipped when it is not installed)
    os.environ["ROOD_PAINT_PRESETS_USER"] = os.path.join(tempfile.gettempdir(), "aseprite_test_no_user_presets.json")
    if shutil.which("aseprite"):
        ten = make_package("ten")
        tpath = ten / "aseprite" / "aaaaaaaaaa_g100.aseprite"
        script = d / "stroke.lua"
        script.write_text(f'''local s = app.open("{tpath}")
local layer
for _, l in ipairs(s.layers) do if l.name == "Material" then layer = l end end
app.layer = layer
local cel = layer:cel(1) or s:newCel(layer, 1)
app.useTool{{ tool = "pencil", color = Color{{ r = {steel["rgb"][0]}, g = {steel["rgb"][1]}, b = {steel["rgb"][2]} }}, layer = layer, cel = cel,
  points = {{ Point(1, 1), Point(3, 1) }}, brush = Brush(1) }}
s:saveAs("{d / "saved.aseprite"}")
''')
        env = {**os.environ, "XDG_CONFIG_HOME": str(d / "cfg"), "HOME": str(d)}
        env.pop("DISPLAY", None)
        run = subprocess.run(["nice", "-n", "10", "aseprite", "-b", "--script", str(script)], env=env, capture_output=True, text=True, timeout=60)
        saved = ase.read(d / "saved.aseprite") if (d / "saved.aseprite").exists() else None
        check(saved is not None and [e[:3] for e in saved.palette[:len(presets)]] == rgbs, f"real Aseprite opens the exported file and saves it with the swatch colours as its palette ({run.stderr[-200:]})")
        names_kept = saved is not None and [e[4] for e in saved.palette[:len(presets)]] == [p["name"] for p in presets]
        print(f"note: Aseprite {'keeps' if names_kept else 'drops'} the palette entries' names when it saves (the names are in the exported file; the extension\'s window lists them either way)")
        shutil.copy(d / "saved.aseprite", tpath)
        pr.run_import(args_for(ten, "ten"), say=lambda *_: None)
        got_m = np.asarray(Image.open(d / "layer-ten/aaaaaaaaaa/@g100/metal.png"))
        got_r = np.asarray(Image.open(d / "layer-ten/aaaaaaaaaa/@g100/roughness.png"))
        check((got_m[1, 1:4] == 255).all() and (got_r[1, 1:4] == 107).all() and got_m[1, 0] == seed_m[1, 0] and got_r[1, 4] == 120,
              "a pencil stroke with the Worn steel swatch on the Material layer, made and saved by real Aseprite, imports as that preset's metal and roughness")
        # the palette command on a file Aseprite wrote
        user.write_text(json.dumps({"presets": [{"id": "mud", "name": "Mud", "colour": "#aabb00", "roughness": 1.0}]}))
        os.environ["ROOD_PAINT_PRESETS_USER"] = str(user)
        pr.refresh_palettes(argparse.Namespace(dir=str(ten)), say=lambda *_: None)
        os.environ["ROOD_PAINT_PRESETS_USER"] = os.path.join(tempfile.gettempdir(), "aseprite_test_no_user_presets.json")
        script.write_text(f'local s = app.open("{tpath}") print(#s.layers, #s.palettes[1], s.palettes[1]:getColor(12).green)')
        run = subprocess.run(["aseprite", "-b", "--script", str(script)], env=env, capture_output=True, text=True, timeout=60)
        check(run.stdout.split() == [str(len(pch.ROWS) + 2), str(len(presets) + 1 + mp.PALETTE_GRAYS), str(0xbb)], f"a file Aseprite wrote opens in it again after `palette`, with the new swatches: {run.stdout.split()}")
    else:
        print("skip: aseprite is not installed (the real-Aseprite checks)")


# ---- finding the game: the socket pc/run-game.sh opens, and the model summoned in the studio ----
saved_rpc = os.environ.pop("ROOD_RPC", None)
try:
    check(paint_live.default_socket() in (str(pr.ROOT / "pc/build/boot/rpc-live.sock"), paint_live.rood_rpc.DEFAULT_SOCKET), "the bridge's default socket is the played game's (rpc-live.sock) when there is one")
    os.environ["ROOD_RPC"] = "/x/y.sock"
    check(paint_live.default_sockets() == ["/x/y.sock"], "$ROOD_RPC names the one socket")
    os.environ.pop("ROOD_RPC")
    check(len(paint_live.default_sockets()) == 2 and paint_live.default_sockets()[0].endswith("rpc-studio.sock")
          and paint_live.default_sockets()[1].endswith("rpc-live.sock"), "by default the studio's socket first, then the game's")
    os.environ["ROOD_RPC"] = "/x/y.sock"
    check(paint_live.default_socket() == "/x/y.sock", "$ROOD_RPC names the socket first")
finally:
    os.environ.pop("ROOD_RPC", None)
    if saved_rpc is not None:
        os.environ["ROOD_RPC"] = saved_rpc


class FakeStudio:
    def __init__(self, entered, model_at):
        self.entered, self.model_at, self.picks = entered, model_at, []

    def call(self, method, **p):
        if method == "studio":
            return {"entered": self.entered, "item": {"kind": "character", "id": 3} if self.entered else None}
        if method == "present_dump":
            pathlib.Path(p["path"]).write_bytes(b"P6\n960 720\n255\n")
            return {}
        if method == "material":
            self.picks.append((p["x"], p["y"]))
            hit = (p["x"], p["y"]) == self.model_at
            return {"object": {"text": "m61c3d37af4" if hit else "g100"}}
        raise AssertionError(method)


fake = FakeStudio(True, (492, 360))
check(pr.studio_model(fake) == "61c3d37af4" and fake.picks[0] == (480, 360), f"--model studio finds the summoned model by looking out from the middle of the picture ({fake.picks})")
try:
    pr.studio_model(FakeStudio(False, None))
    check(False, "--model studio without a studio says so")
except SystemExit as e:
    check("model studio" in str(e), "--model studio without a studio says so")

print(f"{'FAIL' if failures else 'PASS'}: aseprite round trip ({failures} failures)")
sys.exit(1 if failures else 0)
