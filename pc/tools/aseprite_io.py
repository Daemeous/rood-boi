#!/usr/bin/env python3
"""Reading and writing Aseprite's .aseprite files (the public format: github.com/aseprite/aseprite docs/ase-file-specs.md) with
zlib, struct and numpy only: no Aseprite needed. Used by paint_roundtrip.py to hand the game's metal and roughness maps to a pixel
painter and to read them back.

read(path) -> Ase: .width, .height, .layers (bottom to top, groups included: a Layer has name, kind, flags, opacity, blend,
level and `pixels`, the first frame's cel as a full-canvas RGBA array (H, W, 4) or None where the layer has no cel there).
Ase.palette: the file's palette, [(r, g, b, a, name)] (Aseprite's colour names). set_palette(path, entries) replaces just that.
Indexed and grayscale files come out as RGBA. Linked cels are followed, a cel may lie anywhere (offset, partly or wholly off the
canvas: it is clipped); tilemap cels and unknown chunks are skipped.
write(path, width, height, layers) writes one frame in RGBA colour mode; each layer is a dict of name, pixels (H, W, 4), and
optionally visible, locked, opacity (0..255), blend (0 = normal); `palette` the entries (r, g, b, a, name)."""
import os
import struct
import zlib

import numpy as np

MAGIC_FILE, MAGIC_FRAME = 0xA5E0, 0xF1FA
LAYER_VISIBLE, LAYER_EDITABLE = 1, 2
KIND_NORMAL, KIND_GROUP, KIND_TILEMAP = 0, 1, 2


class Layer:
    def __init__(self, name, kind, flags, opacity, blend, level):
        self.name, self.kind, self.flags, self.opacity, self.blend, self.level = name, kind, flags, opacity, blend, level
        self.pixels = None

    visible = property(lambda self: bool(self.flags & LAYER_VISIBLE))
    locked = property(lambda self: not self.flags & LAYER_EDITABLE)


class Ase:
    def __init__(self, width, height):
        self.width, self.height, self.layers = width, height, []
        self.palette = []  # [(r, g, b, a, name)] by index (Aseprite's colour names: "" where an entry has none)

    def layer(self, name):
        """The topmost image layer called `name` (any case; a group is never one), or None."""
        found = [layer for layer in self.layers if layer.kind == KIND_NORMAL and layer.name.strip().lower() == name.lower()]
        return found[-1] if found else None


def _cut(data, pos, fmt):
    size = struct.calcsize(fmt)
    if pos + size > len(data):
        raise ValueError("aseprite: the file ends inside a chunk")
    return struct.unpack_from(fmt, data, pos), pos + size


def _string(data, pos):
    (n,), pos = _cut(data, pos, "<H")
    return data[pos:pos + n].decode("utf-8", "replace"), pos + n


def _palette_new(body):
    (size, first, last), pos = _cut(body, 0, "<III")
    pos += 8
    colours = {}
    for i in range(first, last + 1):
        (flags, r, g, b, a), pos = _cut(body, pos, "<HBBBB")
        name = ""
        if flags & 1:
            name, pos = _string(body, pos)
        colours[i] = (r, g, b, a, name)
    return colours


def _palette_old(body):
    (packets,), pos = _cut(body, 0, "<H")
    colours, index = {}, 0
    for _ in range(packets):
        (skip, count), pos = _cut(body, pos, "<BB")
        index += skip
        for _ in range(count or 256):
            (r, g, b), pos = _cut(body, pos, "<BBB")
            colours[index] = (r, g, b, 255, "")
            index += 1
    return colours


def _pixels(raw, w, h, depth, palette, transparent):
    """A cel's raw bytes as RGBA (h, w, 4)."""
    if depth == 32:
        return np.frombuffer(raw, np.uint8, w * h * 4).reshape(h, w, 4)
    if depth == 16:
        gray = np.frombuffer(raw, np.uint8, w * h * 2).reshape(h, w, 2)
        return np.concatenate([np.repeat(gray[..., :1], 3, axis=2), gray[..., 1:]], axis=2)
    index = np.frombuffer(raw, np.uint8, w * h).reshape(h, w)
    table = np.zeros((256, 4), np.uint8)
    for i, c in palette.items():
        if i < 256:
            table[i] = c[:4]
    out = table[index]
    out[index == transparent] = 0
    return out


def to_canvas(width, height, x, y, rgba):
    """A cel's RGBA pixels (h, w, 4) placed at x, y on a transparent canvas, clipped to it."""
    canvas = np.zeros((height, width, 4), np.uint8)
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + rgba.shape[1], width), min(y + rgba.shape[0], height)
    if x1 > x0 and y1 > y0:
        canvas[y0:y1, x0:x1] = rgba[y0 - y:y1 - y, x0 - x:x1 - x]
    return canvas


def read(path):
    data = open(path, "rb").read()
    (size, magic, frames, width, height, depth, flags), pos = _cut(data, 0, "<IHHHHHI")
    if magic != MAGIC_FILE:
        raise ValueError(f"aseprite: {path} is not an Aseprite file")
    if depth not in (8, 16, 32):
        raise ValueError(f"aseprite: colour depth {depth} is not 8, 16 or 32")
    (transparent,), _ = _cut(data, 0x1C, "<B")
    ase, palette, cels = Ase(width, height), {}, {}
    pos = 128
    for frame in range(frames):
        (fsize, fmagic, old_count), p = _cut(data, pos, "<IHH")
        if fmagic != MAGIC_FRAME:
            raise ValueError("aseprite: a frame header is damaged")
        (new_count,), _ = _cut(data, pos + 12, "<I")
        count = new_count or old_count
        p = pos + 16
        for _ in range(count):
            (csize, ctype), _ = _cut(data, p, "<IH")
            body = data[p + 6:p + csize]
            p += csize
            if ctype == 0x2004:  # layer
                (lflags, kind, level, _dw, _dh, blend, opacity), q = _cut(body, 0, "<HHHHHHB")
                name, _ = _string(body, q + 3)
                ase.layers.append(Layer(name, kind, lflags, opacity, blend, level))
            elif ctype == 0x2005:  # cel
                (index, x, y, opacity, ckind, _z), q = _cut(body, 0, "<HhhBHh")
                q += 5
                if ckind in (0, 2):
                    (w, h), q = _cut(body, q, "<HH")
                    raw = body[q:] if ckind == 0 else zlib.decompress(body[q:])
                    cels[(frame, index)] = (x, y, _pixels(raw, w, h, depth, palette, transparent))
                elif ckind == 1:
                    (other,), _ = _cut(body, q, "<H")
                    if (other, index) in cels:
                        cels[(frame, index)] = cels[(other, index)]
            elif ctype == 0x2019:
                palette.update(_palette_new(body))
            elif ctype == 0x0004 and not palette:
                palette.update(_palette_old(body))
        pos += fsize
    ase.palette = [palette[i] for i in sorted(palette)]
    for index, layer in enumerate(ase.layers):
        if (0, index) in cels and layer.kind == KIND_NORMAL:
            layer.pixels = to_canvas(width, height, *cels[(0, index)])
    return ase


def _chunk(kind, body):
    return struct.pack("<IH", 6 + len(body), kind) + body


def palette_chunk(entries):
    """A palette chunk (0x2019) of `entries`: (r, g, b, a, name), the name optional ("" or missing: none)."""
    body = struct.pack("<III8x", len(entries), 0, len(entries) - 1)
    for entry in entries:
        r, g, b, a = entry[:4]
        name = (entry[4] if len(entry) > 4 else "").encode("utf-8")
        body += struct.pack("<HBBBB", 1 if name else 0, r, g, b, a) + (struct.pack("<H", len(name)) + name if name else b"")
    return _chunk(0x2019, body)


def set_palette(path, entries):
    """The file's palette replaced by `entries` ((r, g, b, a, name)), every other byte of it kept (the pixels, layers, tags, slices: the
    chunks are copied as they are; old-style palette chunks are dropped); written in place, atomically."""
    data = open(path, "rb").read()
    (size, magic, frames), _ = _cut(data, 0, "<IHH")
    if magic != MAGIC_FILE:
        raise ValueError(f"aseprite: {path} is not an Aseprite file")
    pos, out = 128, []
    for frame in range(frames):
        (fsize, fmagic, old_count), _ = _cut(data, pos, "<IHH")
        if fmagic != MAGIC_FRAME:
            raise ValueError("aseprite: a frame header is damaged")
        (new_count,), _ = _cut(data, pos + 12, "<I")
        count, p, chunks, placed = new_count or old_count, pos + 16, [], False
        for _ in range(count):
            (csize, ctype), _ = _cut(data, p, "<IH")
            if ctype in (0x2019, 0x0004, 0x0011):  # (the new and old palettes)
                if frame == 0 and ctype == 0x2019 and not placed:
                    chunks.append(palette_chunk(entries))
                    placed = True
            else:
                chunks.append(data[p:p + csize])
            p += csize
        if frame == 0 and not placed:  # (before the first layer chunk, where Aseprite writes it)
            at = next((i for i, c in enumerate(chunks) if struct.unpack_from("<H", c, 4)[0] == 0x2004), len(chunks))
            chunks.insert(at, palette_chunk(entries))
        body = b"".join(chunks)
        head = bytearray(data[pos:pos + 16])
        struct.pack_into("<I", head, 0, 16 + len(body))
        struct.pack_into("<H", head, 6, min(len(chunks), 0xFFFF))
        struct.pack_into("<I", head, 12, len(chunks))
        out.append(bytes(head) + body)
        pos += fsize
    result = bytearray(data[:128] + b"".join(out) + data[pos:])
    struct.pack_into("<I", result, 0, len(result))
    tmp = str(path) + ".tmp"
    with open(tmp, "wb") as f:
        f.write(result)
    os.replace(tmp, path)


def write(path, width, height, layers, palette=None):
    """One frame, RGBA mode, `palette` ((r, g, b, a, name) entries; default a gray ramp, as Aseprite writes one too), sRGB; `layers` bottom to top."""
    chunks = [_chunk(0x2007, struct.pack("<HHi", 1, 0, 0))]
    chunks.append(palette_chunk(palette or [(i, i, i, 255) for i in range(256)]))
    for index, layer in enumerate(layers):
        flags = (LAYER_VISIBLE if layer.get("visible", True) else 0) | (0 if layer.get("locked") else LAYER_EDITABLE)
        name = layer["name"].encode("utf-8")
        chunks.append(_chunk(0x2004, struct.pack("<HHHHHHB3x", flags, KIND_NORMAL, 0, 0, 0, layer.get("blend", 0), layer.get("opacity", 255))
                             + struct.pack("<H", len(name)) + name))
        pixels = np.ascontiguousarray(layer["pixels"], np.uint8)
        assert pixels.shape == (height, width, 4), f"layer {layer['name']}: {pixels.shape} is not {(height, width, 4)}"
        chunks.append(_chunk(0x2005, struct.pack("<HhhBHh5xHH", index, 0, 0, 255, 2, 0, width, height) + zlib.compress(pixels.tobytes(), 6)))
    body = b"".join(chunks)
    frame = struct.pack("<IHHHHI", 16 + len(body), MAGIC_FRAME, min(len(chunks), 0xFFFF), 100, 0, len(chunks))
    header = struct.pack("<IHHHHHI", 128 + len(frame) + len(body), MAGIC_FILE, 1, width, height, 32, 1)
    header += struct.pack("<HII", 100, 0, 0) + struct.pack("<B3xH", 0, 256) + struct.pack("<BBhhHH", 1, 1, 0, 0, 16, 16)
    header += bytes(128 - len(header))
    with open(path, "wb") as f:
        f.write(header + frame + body)
