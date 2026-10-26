#!/usr/bin/env python3
"""Disc-free validation of the provisional material catalog and the publish guard."""
import importlib.util
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "pc/assets/materials/catalog.json"
GUARD = ROOT / "pc/tools/check_material_maps.py"


def main():
    spec = importlib.util.spec_from_file_location("material_guard", GUARD)
    guard = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guard)
    data = json.loads(CATALOG.read_text())
    assert data["format"] == 1
    assert {"wet stone", "worn wood", "polished metal", "glass", "gel", "rough stone", "flagstone", "plaster",
            "cloth"} <= data["presets"].keys()
    assert data["entries"]
    ids = set()
    for entry in data["entries"]:
        assert entry["id"] not in ids
        ids.add(entry["id"])
        assert entry["where"] and entry["review"] == "new"
        m = entry["match"]
        if "key" in m:
            assert len(m["key"]) == 10 and set(m["key"]) <= set("0123456789abcdef")
        else:  # a palette entry: every texture with the palette (a texture's own entry wins); scope it to rooms
            assert "clut" in m and "scope" in entry, entry["id"]
        if "key" in m or "size" in m:
            assert len(m["size"]) == 2 and all(type(n) is int and 1 <= n <= 256 for n in m["size"])
        if "clut" in m:
            assert len(m["clut"]) == 10 and set(m["clut"]) <= set("0123456789abcdef")
        if "preset" in entry:
            assert entry["preset"] in data["presets"]
        for value in [data["presets"].get(entry.get("preset"), {}), entry]:
            for field in ("roughness", "specular", "metal"):
                if field in value:
                    assert type(value[field]) in (int, float) and 0 <= value[field] <= 1
    for preset in data["presets"].values():
        assert all(0 <= preset[k] <= 1 for k in ("roughness", "specular", "metal"))
    # Relief fields (entries and presets): auto_height a boolean, bump a multiplier in [0, 4].
    for value in list(data["presets"].values()) + data["entries"]:
        if "auto_height" in value:
            assert type(value["auto_height"]) is bool
        if "bump" in value:
            assert type(value["bump"]) in (int, float) and 0 <= value["bump"] <= 4
    # Publish permits single-channel maps only, rejects extracted colour art.
    path = "pc/assets/materials/0123456789/roughness.png"
    png = b"\x89PNG\r\n\x1a\n" + struct.pack(">I4sIIBB", 13, b"IHDR", 31, 31, 8, 0)
    assert guard.problem(path, png) is None
    assert guard.problem(path, png[:-1] + b"\x06") is not None
    assert guard.problem("pc/assets/materials/0123456789/reference.png", png) is not None
    for channel in ("height", "metal"):
        assert guard.problem(f"pc/assets/materials/0123456789/{channel}.png", png) is None
    # Every shipped map passes the guard.
    for path in sorted((ROOT / "pc/assets/materials").glob("*/*.png")):
        rel = path.relative_to(ROOT).as_posix()
        assert guard.problem(rel, path.read_bytes()) is None, rel
    print(f"material catalog: {len(ids)} provisional entries, {len(data['presets'])} presets; grayscale publish guard OK")


if __name__ == "__main__":
    main()
