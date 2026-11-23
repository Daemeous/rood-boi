#!/usr/bin/env python3
"""The paint channel table (pc/tools/aseprite/channels.json): which maps of a material a painter can paint, and how each one rides through the
export (.aseprite layer, seed image), the import, the live bridge and the extension. One table, read by paint_roundtrip.py, paint_live.py and
material_presets.py; the extension gets the layer names from the bridge's greeting, so a new channel (alpha, emissive...) is a row of the
table. The game's own list is materials.cpp known_channel / blank_value (pc/docs/Materials.md); a test keeps blank values the same."""
import json
import os
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]


def table_file():
    return pathlib.Path(os.environ.get("ROOD_PAINT_CHANNELS") or ROOT / "pc/tools/aseprite/channels.json")


def _load():
    data = json.loads(table_file().read_text())
    rows = []
    for row in data["channels"]:
        row = dict(row)
        for need in ("channel", "layer", "blank", "seed"):
            if need not in row:
                raise ValueError(f"channels.json: a row needs {need}: {row}")
        row.setdefault("encoding", "gray")
        row.setdefault("presets", False)
        row.setdefault("visible", False)
        row.setdefault("opacity", 255)
        rows.append(row)
    return data["reference"]["layer"], data["preset_layer"], rows


REFERENCE, PRESET_LAYER, ROWS = _load()  # (read once: a tool run is one table)
BY_CHANNEL = {row["channel"]: row for row in ROWS}
NAMES = tuple(row["channel"] for row in ROWS)
PRESET_CHANNELS = tuple(row["channel"] for row in ROWS if row["presets"])
LAYERS = {row["channel"]: row["layer"] for row in ROWS}  # channel -> layer name, bottom to top


def blank(channel):
    """The 8-bit value of a map nobody painted."""
    return int(BY_CHANNEL[channel]["blank"])


def seed_image(channel):
    """(the image the export keeps the game's map in, its colour plane or None)."""
    seed = BY_CHANNEL[channel]["seed"]
    return seed["image"], seed.get("plane")


def own_images():
    """The seed images that belong to one channel alone (not packed with another): {image name: channel}."""
    return {row["seed"]["image"]: row["channel"] for row in ROWS if row["seed"].get("plane") is None}


def layer_channel(layer_name):
    """The channel a layer name paints (any case), 'material' for the preset layer, else None."""
    name = str(layer_name).strip().lower()
    if name == PRESET_LAYER["layer"].lower():
        return "material"
    return next((row["channel"] for row in ROWS if row["layer"].lower() == name), None)


def greeting():
    """What the live bridge tells the extension: the layers it sends and the one it paints presets on."""
    return {"layers": [{"layer": row["layer"], "channel": row["channel"]} for row in ROWS], "preset_layer": PRESET_LAYER["layer"]}
