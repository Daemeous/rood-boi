#!/usr/bin/env python3
"""The curated emissive catalog (pc/assets/emissive/catalog.json,
pc/docs/Emissive.md) is complete and inside its budgets.

    python3 pc/tests/emissive_catalog_test.py [--no-disc]

- every entry: a unique id, a name, textures (10-hex-digit keys, sizes), a
  mask (shapes, CLUT indices, colour or luminance filters, or a painted
  map), a tint and strength, and light settings in range (light weight
  0..16, light_reach 0 or 32..1000, light_lift 0..64; a rect_light's
  intensity, radius, offset and max_per_room);
- with the user's disc (pc/tools/fixture_lib.py; skipped without it): in
  every room that draws a light entry's texture, the room's glows (its
  additive polygon groups and the emissive light polygons, grouped as
  pc/src/glows.c groups them) stay within the 32 traced glows, every light
  entry's texture is drawn somewhere (a key no room draws is a typo; object
  textures, which only a running game shows, are listed in OBJECT_KEYS),
  and the generated window lights (pc/tools/generate_lighting.py) are up to
  date, at most 16 a room (OpenGL with ray-traced shadows keeps 16 lights).
"""
import argparse
import collections
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
CATALOG = ROOT / "pc/assets/emissive/catalog.json"
TRACED_GLOWS = 32
LIGHTS_PER_ROOM = 16
# Textures of objects (crates, props): not room geometry, so the disc scan cannot see them.
OBJECT_KEYS = {"dc51f82ff7", "eaecfa1949", "2e24028557", "1a7a9a0083", "cb0bfa6a81", "789d8b070b", "0dc55d7442",
               "036c1f39e7", "514db27fa4", "010f253840", "a5093cc91c", "facb3aeb8a", "28d9216215", "1095ec4f34",
               "b86f16f59b", "7d13c69ada"}
RANGES = {"strength": (0.0, 4.0), "light": (0.0, 16.0), "light_lift": (0.0, 64.0)}
RECT = {"intensity": (0.01, 4.0), "radius": (64.0, 2000.0), "offset": (0.0, 32.0), "max_per_room": (1, 16),
        "min_area": (0.0, 4096.0), "shadows": (0, 1)}


def check_entries(entries, problems):
    ids = collections.Counter(e.get("id") for e in entries)
    for e in entries:
        eid = e.get("id", "?")

        def bad(message):
            problems.append(f"{eid}: {message}")
        if not re.fullmatch(r"E\d{3}", str(eid)):
            bad("id is not E###")
        if ids[eid] > 1:
            bad("duplicate id")
        if not e.get("name"):
            bad("no name")
        match = e.get("match") or []
        if not match:
            bad("no textures (match)")
        for m in match:
            if not re.fullmatch(r"[0-9a-f]{10}", str(m.get("key", ""))):
                bad(f"bad key {m.get('key')!r}")
            size = m.get("size")
            if not (isinstance(size, list) and len(size) == 2 and all(isinstance(v, int) and 1 <= v <= 256 for v in size)):
                bad(f"{m.get('key')}: bad size {size!r}")
            if "clut" in m and not re.fullmatch(r"[0-9a-f]{10}", str(m["clut"])):
                bad(f"{m.get('key')}: bad clut {m['clut']!r} (the palette's colours hash; a live scan's is no key)")
        mask = e.get("mask") or {}
        shapes = [k for k in ("rects", "polygons", "indices", "color_min", "color_max", "luma", "map") if mask.get(k)]
        if not shapes:
            bad("no mask (rects, polygons, CLUT indices, colour or luminance filters, or a painted map)")
        if any(not isinstance(i, int) or not 0 <= i <= 255 for i in mask.get("indices", [])):
            bad("CLUT indices outside 0..255")
        color = e.get("color")
        if not (isinstance(color, list) and len(color) == 3 and all(0 <= c <= 2 for c in color) and max(color) > 0):
            bad(f"bad tint {color!r}")
        for key, (lo, hi) in RANGES.items():
            v = e.get(key, 0 if key != "strength" else 1)
            if not lo <= v <= hi:
                bad(f"{key} {v} outside {lo:g}..{hi:g}")
        if e.get("strength", 1) <= 0:
            bad("strength 0: it never shows")
        reach = e.get("light_reach", 0)
        if reach and not 32 <= reach <= 1000:
            bad(f"light_reach {reach} outside 32..1000 (0: the room's)")
        if (reach or e.get("light_lift")) and e.get("light", 0) <= 0:
            bad("light_reach / light_lift on an entry that is not a light")
        rect = e.get("rect_light")
        if rect is not None:
            if not isinstance(rect, dict):
                bad("rect_light is not an object")
                continue
            for key, value in rect.items():
                if key not in RECT:
                    bad(f"rect_light: unknown field {key}")
                elif not RECT[key][0] <= value <= RECT[key][1]:
                    bad(f"rect_light {key} {value} outside {RECT[key][0]:g}..{RECT[key][1]:g}")


def check_rooms(entries, problems, report):
    import fixture_lib as fl
    import lighting_format as lf
    try:
        disc = fl.Disc()
    except OSError as error:
        print(f"skip: the rooms (no disc: {error})")
        return
    from glow_scan import clut_key
    light_keys = {}  # key -> (entry id, the palette it needs or None)
    for e in entries:
        if e.get("enabled", True) and e.get("light", 0) > 0:
            for m in e["match"]:
                light_keys[m["key"]] = (e["id"], m.get("clut"))
    window_keys = {m["key"] for e in entries if e.get("rect_light") for m in e["match"]}
    seen, worst, rooms_with = set(), (0, ()), 0
    window_zones = set()
    for zone in disc.zones():
        try:
            vram, names = fl.zone_vram(disc, zone)
        except (OSError, KeyError, ValueError, IndexError):
            continue
        for room, name in enumerate(names):
            if not name:
                continue
            r = fl.load_room(disc, zone, room, vram, name)
            # the polygons drawing a light entry's texture (with its palette, when the entry names one)
            uses = [i for i, p in enumerate(r["polys"]) if p["ikey"] in light_keys
                    and light_keys[p["ikey"]][1] in (None, clut_key(vram, p["tpage"], p["clut"]))]
            seen |= {r["polys"][i]["ikey"] for i in uses}
            lit = [i for i in uses if fl.captured(r["polys"][i]) and not r["polys"][i]["additive"]]
            if any(p["ikey"] in window_keys for p in r["polys"]):
                window_zones.add(zone)
            if not lit:
                continue
            rooms_with += 1
            groups = fl.glow_groups(r, lit)
            before = len(fl.glow_groups(r))
            ours = sum(1 for g in groups if set(g["polys"]) & set(lit))
            report.append((zone, room, len(groups), ours, before))
            worst = max(worst, (ours, (zone, room)))
            # The emissive glows must fit the traced glows, and must not be what takes a room past them (a
            # room whose own additive glows are more than 32 already keeps its strongest, as before).
            if ours > TRACED_GLOWS or (before <= TRACED_GLOWS < len(groups)):
                problems.append(f"zone {zone} room {room}: {len(groups)} room glows ({ours} emissive, {before} "
                                f"without them), more than the {TRACED_GLOWS} traced")
    for key, (eid, _) in sorted(light_keys.items()):
        if key not in seen and key not in OBJECT_KEYS:
            problems.append(f"{eid}: texture {key} is drawn in no room (a typo, or an object's: OBJECT_KEYS)")
    print(f"rooms with emissive light polygons: {rooms_with}; most emissive room glows: {worst[0]} "
          f"(zone, room {worst[1]})")
    # the generated window lights: up to date, and few enough a room
    args = [sys.executable, str(ROOT / "pc/tools/generate_lighting.py"), "--check", "--quiet", "--jobs", "1"]
    for z in sorted(window_zones):
        args += ["--zone", str(z)]
    if window_zones:
        run = subprocess.run(args, capture_output=True, text=True)
        if run.returncode:
            problems.append("generated window lights not up to date: run pc/tools/generate_lighting.py "
                            + " ".join(f"--zone {z}" for z in sorted(window_zones)) + "\n" + run.stdout[-2000:])
    most = (0, "")
    for path in sorted((ROOT / "lighting/default/generated").glob("zone*_room*.txt")):
        n = sum(1 for e in lf.parse_text(path.read_text()).entries if e.id.startswith("light:gen.window_"))
        most = max(most, (n, path.name))
        if n > LIGHTS_PER_ROOM:
            problems.append(f"{path.name}: {n} window lights, more than {LIGHTS_PER_ROOM}")
    print(f"window lights: zones {sorted(window_zones)}; most in a room: {most[0]} ({most[1]})")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--no-disc", action="store_true", help="only the entries (no room checks)")
    parser.add_argument("--catalog", default=str(CATALOG))
    parser.add_argument("--verbose", action="store_true", help="each room's glow count")
    args = parser.parse_args()
    entries = json.loads(pathlib.Path(args.catalog).read_text(encoding="utf-8")).get("entries", [])
    problems, report = [], []
    check_entries(entries, problems)
    print(f"{len(entries)} curated entries checked")
    if not args.no_disc:
        check_rooms(entries, problems, report)
    if args.verbose:
        for zone, room, n, ours, before in report:
            print(f"  zone {zone} room {room}: {n} room glows ({ours} with emissive light, {before} without)")
    for p in problems:
        print("FAIL:", p)
    print("FAIL" if problems else "PASS", f"({len(problems)} problems)" if problems else "")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
