#!/usr/bin/env python3
"""Material presets for painting a KIND of material in Aseprite in one stroke (Gitea #20; Materials.md "Painting metal and roughness
per pixel in Aseprite (live)"). A preset is data: id, name, colour (its swatch), metal and roughness 0..1, optional notes, and any
other channel a later design adds (height, emissive, alpha...: carried as given, expanded only for the channels in CHANNELS).

The files: the shipped pc/assets/materials/paint-presets.json ($ROOD_PAINT_PRESETS) and the user's <lighting>/paint-presets.json
($ROOD_PAINT_PRESETS_USER; <lighting> = $ROOD_LIGHTS or pc/user/lighting). The user file is read after the shipped one: an entry of the
same id replaces it in place, a new id is added after, "disabled": true drops one.

The "Material" layer of a material's .aseprite file is painted with the swatch colours; expand() turns its cel into a per-texel value
and mask for each channel. A texel whose colour is none of the swatches (within TOLERANCE per channel: a brush's blending) is no preset."""
import json
import os
import pathlib
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
CHANNELS = ("metal", "roughness")  # the preset values that become maps now (0..1 -> 8-bit gray, as the game's maps are)
TOLERANCE = 8  # per RGB channel
LAYER = "Material"
PALETTE_GRAYS = 11  # after the swatches: 0.0, 0.1 ... 1.0 as gray, for the Metal and Roughness layers


def shipped_file():
    return pathlib.Path(os.environ.get("ROOD_PAINT_PRESETS") or ROOT / "pc/assets/materials/paint-presets.json")


def user_file():
    return pathlib.Path(os.environ.get("ROOD_PAINT_PRESETS_USER") or
                        pathlib.Path(os.environ.get("ROOD_LIGHTS") or ROOT / "pc/user/lighting") / "paint-presets.json")


def files():
    return [shipped_file(), user_file()]


def stamp(paths=None):
    """What a change of the preset files changes (for a cheap 'reload?')."""
    out = []
    for path in paths or files():
        try:
            st = path.stat()
            out.append((st.st_mtime_ns, st.st_size))
        except OSError:
            out.append(None)
    return tuple(out)


def parse_colour(text):
    text = str(text).strip().lstrip("#")
    if len(text) == 3:
        text = "".join(c * 2 for c in text)
    if len(text) != 6:
        raise ValueError(f"colour {text!r} is not #rrggbb")
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


def _clean(entry, source):
    if not isinstance(entry, dict) or not entry.get("id"):
        raise ValueError(f"{source}: a preset needs an id")
    out = dict(entry)
    out["id"] = str(entry["id"])
    out["name"] = str(entry.get("name") or entry["id"])
    out["rgb"] = parse_colour(entry["colour"]) if "colour" in entry else None
    for channel in CHANNELS:
        if channel in entry and entry[channel] is not None:
            out[channel] = min(1.0, max(0.0, float(entry[channel])))
    return out


def load(paths=None, warn=lambda text: print(text, file=sys.stderr)):
    """The presets, in order: [{id, name, rgb (r, g, b), metal?, roughness?, notes?, ...}]. A file that is missing is skipped; one that is
    broken is warned about and skipped, as is a preset of it that is (the others are kept); a preset without a colour is dropped (it
    cannot be painted)."""
    order, by_id = [], {}
    for path in paths or files():
        if not path.exists():
            continue
        try:
            data = json.loads(path.read_text())
            entries = data["presets"]
        except (ValueError, KeyError, TypeError, OSError) as e:
            warn(f"presets: {path}: not read ({e})")
            continue
        for entry in entries:
            try:
                preset = _clean(entry, path.name)
            except (ValueError, TypeError, KeyError) as e:
                warn(f"presets: {path.name}: {entry.get('id') if isinstance(entry, dict) else entry!r}: skipped ({e})")
                continue
            if entry.get("disabled"):
                if preset["id"] in by_id:
                    order.remove(preset["id"])
                    del by_id[preset["id"]]
                continue
            if preset["id"] not in by_id:
                order.append(preset["id"])
            by_id[preset["id"]] = preset
    return [by_id[i] for i in order if by_id[i]["rgb"] is not None]


def palette(presets):
    """The Aseprite palette: [(r, g, b, a, name)], the swatches (named) then the grays for the Metal and Roughness layers."""
    entries = [(*p["rgb"], 255, p["name"]) for p in presets]
    for i in range(PALETTE_GRAYS):
        v = round(i * 255 / (PALETTE_GRAYS - 1))
        entries.append((v, v, v, 255, f"{i / (PALETTE_GRAYS - 1):.1f}"))
    return entries


def match(rgba, presets):
    """Per texel the index of the preset whose swatch the texel is (RGB within TOLERANCE, alpha > 0), else -1: (h, w) int."""
    index = np.full(rgba.shape[:2], -1, np.int32)
    if not presets:
        return index
    rgb = rgba[..., :3].astype(np.int32)
    best = np.full(index.shape, 1 << 30, np.int32)
    for i, p in enumerate(presets):
        d = np.abs(rgb - np.array(p["rgb"], np.int32)).max(axis=2)
        hit = (d <= TOLERANCE) & (d < best)
        index[hit], best[hit] = i, d[hit]
    index[rgba[..., 3] == 0] = -1
    return index


def expand(rgba, presets):
    """{channel: (values uint8 (h, w), mask bool (h, w))} of the Material layer's cel (an RGBA canvas): where the mask is set the
    texel's preset gives the channel's value (a preset without that channel leaves the mask clear there). Channels no preset has are
    left out."""
    index = match(rgba, presets)
    out = {}
    for channel in CHANNELS:
        table = np.array([round(p[channel] * 255) if channel in p else -1 for p in presets] + [-1], np.int32)
        got = table[index]  # (index -1 is the last entry: no preset)
        mask = got >= 0
        if mask.any():
            out[channel] = (np.where(mask, got, 0).astype(np.uint8), mask)
    return out


def describe(presets):
    return "\n".join(f"{p['name']:<16} {p['colour'] if 'colour' in p else ''}  metal {p.get('metal', '-')}  roughness {p.get('roughness', '-')}" for p in presets)
