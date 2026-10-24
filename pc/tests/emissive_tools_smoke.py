#!/usr/bin/env python3
"""Scratch-user emissive tools: shared UI/RPC operations, maps, undo and lossless entries.

Uses a copy of the canonical card and editor_selection_smoke's isolated lighting
and settings directories. Never writes the real user layer or commits texture art.
"""
import argparse
import copy
import pathlib
import sys

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from editor_selection_smoke import finish, pinned_card, start


def check_tools(s):
    g = s.game
    picked = None
    for y in range(560, 700, 30):
        for x in range(200, 860, 60):
            p = g.call("emissive", action="pick", x=x, y=y)
            if p.get("valid") and p.get("room") and min(p["size"]) >= 16:
                picked = p
                break
        if picked:
            break
    if not s.check(picked is not None, "picked a live room texture"):
        return
    key, (w, h) = picked["key"], picked["size"]
    ident = g.call("emissive", action="new")["id"]
    g.call("emissive", action="set", id=ident, mask={"map": "emissive"})
    g.call("emissive", action="select", id=ident)
    path = s.lights_dir / "materials" / key / "emissive.png"

    def pixels():
        with Image.open(path) as image:
            return list(image.convert("L").getdata())

    def paint(tool, x=3, y=4, **kw):
        return g.call("emissive", action="ui", tool=tool, paint={"key": key, "x": x, "y": y, **kw})

    g.call("emissive", action="ui", brush={"value": 17})
    paint("pencil")
    g.call("emissive", action="ui", brush={"value": 255})
    expected = [0] * (w * h)
    expected[4 * w + 3] = 255
    s.check(pixels() == expected, "pencil is exactly one texel in user emissive.png")
    paint("eraser")
    s.check(not any(pixels()), "eraser removes that texel")
    g.call("editor_undo", action="undo")
    s.check(pixels() == expected, "ordinary editor undo restores erased map texel")
    g.call("editor_undo", action="redo")
    s.check(not any(pixels()), "ordinary editor redo erases it again")

    indices = g.call("emissive", action="texture", tpage=picked["tpage"], clut=picked["clut"],
                     box=picked["box"])["indices"]
    pending, seen, region = [(3, 4)], set(), set()
    want = indices[4][3]
    while pending:
        x, y = pending.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        seen.add((x, y))
        if indices[y][x] != want:
            continue
        region.add((x, y))
        pending.extend(((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)))
    paint("fill")
    expected = [255 if (x, y) in region else 0 for y in range(h) for x in range(w)]
    s.check(pixels() == expected, "fill is four-connected same-CLUT, not global same-colour")
    g.call("editor_undo", action="undo")
    s.check(not any(pixels()), "fill is one undo transaction")

    paint("rectangle", 2, 2, x1=5, y1=6)
    expected = [255 if 2 <= x <= 5 and 2 <= y <= 6 else 0 for y in range(h) for x in range(w)]
    s.check(pixels() == expected, "rectangle adds inclusive endpoint texels")
    paint("rectangle", 3, 3, x1=4, y1=5, subtract=True)
    for y in range(3, 6):
        for x in range(3, 5):
            expected[y * w + x] = 0
    s.check(pixels() == expected, "rectangle subtract removes only its area")
    g.call("emissive", action="ui", brush={"radius": 2, "hardness": 0, "opacity": 0.5})
    paint("brush", w - 4, h - 4)
    s.check(0 < pixels()[(h - 4) * w + w - 4] < 255, "brush applies radius, hardness and opacity")

    def entry():
        return next(e for e in g.call("emissive")["entries"] if e["id"] == ident)

    paint("colour")
    s.check(entry()["mask"]["indices"] == [want], "select colour enables the global index filter")
    paint("colour")
    s.check(entry()["mask"]["indices"] == [], "select colour toggles the same index off")
    g.call("editor_undo", action="undo")
    s.check(entry()["mask"]["indices"] == [want], "select colour participates in ordinary undo")

    from rood_rpc import RpcError
    for invalid in (1e100, 2.5, "3"):
        try:
            paint("pencil", invalid, 4)
        except RpcError:
            s.check(True, f"invalid coordinate rejected: {invalid!r}")
        else:
            s.check(False, f"invalid coordinate accepted: {invalid!r}")

    # Toolbar whole-map commands and imports use the same ordinary history.
    g.call("emissive", action="ui", paint={"key": key, "operation": "clear"})
    s.check(not any(pixels()), "toolbar clear zeros the map")
    g.call("emissive", action="ui", paint={"key": key, "operation": "invert"})
    s.check(all(v == 255 for v in pixels()), "toolbar invert complements map values")
    imported = s.out / "import-mask.png"
    Image.new("L", (w, h), 73).save(imported)
    g.call("emissive", action="import_map", key=key, path=str(imported.resolve()))
    s.check(all(v == 73 for v in pixels()), "import replaces map through tracked path")
    g.call("editor_undo", action="undo")
    s.check(all(v == 255 for v in pixels()), "undo import restores pre-import pixels")
    g.call("editor_undo", action="redo")
    s.check(all(v == 73 for v in pixels()), "redo import restores imported pixels")

    # Convert all matches, including a synthetic offscreen texture, not just the picked one.
    original_match = entry()["match"]
    other_key = "deadbeef00"
    g.call("emissive", action="set", id=ident,
           match=original_match + [{"key": other_key, "size": [8, 8]}],
           mask={"rects": [[1, 1, 3, 3]]})
    paint("pencil", 8, 8)
    s.check(pixels()[8 * w + 8] == 255 and not entry()["mask"].get("rects"),
            "multi-match conversion allows disjoint paint outside original shapes")
    other_path = s.lights_dir / "materials" / other_key / "emissive.png"
    with Image.open(other_path) as image:
        s.check(list(image.convert("L").getdata()) ==
                [255 if 1 <= x < 3 and 1 <= y < 3 else 0 for y in range(8) for x in range(8)],
                "conversion preserves offscreen match's shapes in its own map")
    g.call("editor_undo", action="undo")
    s.check(entry()["mask"].get("rects") == [[1, 1, 3, 3]], "undo conversion restores shared shapes")
    g.call("editor_undo", action="redo")
    g.call("emissive", action="set", id=ident, match=original_match)

    # Opaque extension fields at every editable object level must survive.
    e = entry()
    e["future"] = {"array": [False, None, {"nested": "keep"}]}
    e["rect_light"] = {"intensity": 2.5, "radius": 700, "offset": 3, "pattern": True,
                       "future": {"nested": [1, 2, 3]}}
    e["match"][0]["future"] = {"keep": 42}
    e["mask"]["future"] = [1, "opaque"]
    e["scope"] = {"future": {"keep": True}}
    expected_entry = copy.deepcopy(e)
    g.call("emissive", action="set", entry=e)
    g.call("emissive", action="set", id=ident, name="edited without losing metadata")
    before = pixels()
    g.call("emissive", action="save")
    g.call("emissive", action="reload")
    actual = entry()
    for field in ("future", "rect_light", "match", "mask", "scope"):
        s.check(actual[field] == expected_entry[field], f"save/reload preserves {field}, including extension fields")
    s.check(pixels() == before, "save/reload keeps painted user map")
    g.call("emissive", action="set", id=ident, mask={"map": "emissive"})
    s.check("indices" not in entry()["mask"], "removing a known optional field does not resurrect its old value")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out")
    args = parser.parse_args()
    s = start(args, "emissive_tools", extra_env={"ROOD_EMISSIVE_USER": "", "ROOD_MATERIALS": ""})
    if s is None:
        return 77
    try:
        check_tools(s)
    except Exception as error:
        s.check(False, f"unexpected error: {error}")
        raise
    finally:
        status = finish(s, "emissive texture tools and lossless save/reload")
    return status


if __name__ == "__main__":
    sys.exit(main())
