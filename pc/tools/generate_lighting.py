#!/usr/bin/env python3
"""Generate the lighting packs' generated layer from the fixture catalog.

    python3 pc/tools/generate_lighting.py --zone 9              (re)write zone 9's generated files
    python3 pc/tools/generate_lighting.py --zone 9 --room 17    one room
    python3 pc/tools/generate_lighting.py --zone 9 --dry-run    what would change, nothing written
    python3 pc/tools/generate_lighting.py --zone 9 --check      exit 1 when a file is not up to date
    python3 pc/tools/generate_lighting.py --zone 9 --gallery DIR  then a before/after review gallery
                                                                (pc/tools/lighting_gallery.py; local only)

For every room it finds the fixtures (pc/tools/fixture_lib.py, from the
user's own disc), matches each to its type in pc/assets/fixtures/catalog.json
and writes lighting/<pack>/generated/zoneZZ_roomRR.txt (lighting/README.md):

- one light actor per flame of every type whose class has a prefab
  ("actor gen.<type>.<hash>": the hash is of the zone, room and the flame
  polygon's offset in the room data, so ids stay the same across
  regenerations and authored overrides keep applying): the prefab, the
  flame's centre as pivot, the fixture's yaw, a seed from the id, its
  fixture (fixture=..., the fixture's polygons cast no shadow from it),
  fixture_type= and flame= (which flame of the type); types switched off
  (inferred fill lights: save points, windows) get enabled=0;
- the game glow the flame or emitter belongs to, when it has one (glow=,
  glowkey=: the key pc/src/glows.c gives it), so the actor drives or
  suppresses it (the candle and torch flames are camera-facing sprites,
  which have no glow);
- every game glow that belongs to one catalogued fixture: "glow <key>
  fixture_type=T..." (cobwebs, floor arrows, decals, light shafts: their
  class says suppress, so they stop counting as lights);
- for a room open to the sky (pc/tools/outdoor.py: measured from its
  geometry and floor light, or decided in pc/assets/environment/outdoor.json),
  its moon and sky: "env sun ..." and "env sky ..." (moody night defaults
  leaning toward the room's own vertex light); roofed rooms get none;
- the area's atmosphere mood (pc/assets/environment/atmosphere.json): "set
  haze_density" / "set haze_anisotropy" and a ground fog "env fog ..."
  (Snowfly Forest, the Undercity, the town at night, other open rooms);
- a rect light on every window pane of an emissive entry with "rect_light"
  (pc/assets/emissive/catalog.json, pc/docs/Emissive.md "Window lights"):
  "light gen.window_<entry>.<hash>" fitted to the pane's polygons (coplanar
  polygons of the entry's textures that touch), facing into the room from
  the pane's front, in the colour of the pane's glowing texels (its mask's
  CLUT indices) times the entry's tint, with the entry's intensity, radius
  and offset (what "Fit rect light to window" does in the editor).

The type's parameters, light offset, fixture radius and glow treatment are
not written: the game reads them from the catalog (pc/src/fixture_catalog.cpp)
for every actor naming the type, so a catalog edit (by hand or in the
editor's "Edit type") changes all instances at once, live. Prefab, enabled
and which fixtures exist are written here: regenerate after changing them.
Output is deterministic (sorted ids, fixed precision): running twice gives
identical files. Never edit generated files; override items by id in
lighting/<pack>/rooms/ (or the editor's user layer).
"""
import argparse
import collections
import hashlib
import json
import os
import pathlib
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixture_lib as fl  # noqa: E402
import lighting_format as lf  # noqa: E402
import outdoor  # noqa: E402

ROOT = fl.ROOT
GLOW_MODES = ("leave", "drive", "suppress")


def vec(v, n=1):
    return ",".join(lf.format_number(float(x), n) for x in v)


def seed_of(item_id):
    return int(hashlib.sha1(item_id.encode()).hexdigest()[:8], 16) % 99991 + 1


def room_items(cat, r, stats):
    """The generated items of one room (lighting_format Items) and notes."""
    polys = r["polys"]
    glows = fl.glow_groups(r)
    glow_of = {}
    for gi, g in enumerate(glows):
        for i in g["polys"]:
            glow_of[i] = gi
    items, notes = [], []
    owners = collections.defaultdict(set)
    for c in fl.clusters(r):
        d = fl.describe(r, c)
        t, how = fl.match(cat, d)
        where = f"zone {r['zone']} room {r['room']} polygon {polys[c['emit'][0]]['off']}"
        if t is None:
            notes.append(f"{where}: no catalog type ({fl.classify(d)[0]}?, {d['npoly']} polygons): not lit")
            stats["unmatched"] += 1
            continue
        if how != "exact":
            notes.append(f"{where}: {t['id']} by rule (not a known instance; check it)")
            stats["rule"] += 1
        stats["fixtures"] += 1
        for i in c["body"]:
            if i in glow_of:
                owners[glow_of[i]].add(t["id"])
        f = fl.type_fields(cat, t)
        if f["prefab"] == "none":
            continue
        anchors = [(fl_["world"], fl_["polys"]) for fl_ in d["flames"]] or [(d["centre"], list(c["emit"]))]
        if len(anchors) != len(t.get("flames", [])) and t.get("flames"):
            notes.append(f"{where}: {len(anchors)} flames, type {t['id']} has {len(t['flames'])}")
        for k, (world, ps) in enumerate(anchors):
            first = min(polys[i]["off"] for i in ps)
            item_id = lf.generated_id("actor", t["id"], r["zone"], r["room"], first)
            fields = [("name", f"{t['class']} {t['id']}"), ("prefab", f["prefab"]), ("pos", vec(world)),
                      ("rot", vec([d["yaw"], 0, 0], 2)), ("seed", str(seed_of(item_id)))]
            if not f["enabled"]:
                fields.append(("enabled", "0"))
            linked = sorted({glow_of[i] for i in ps if i in glow_of})
            if linked:
                g = glows[linked[0]]
                fields += [("glow", vec(g["centre"])), ("glowkey", g["key"])]
                stats["linked"] += 1
            fields += [("fixture", vec(world)), ("fixture_type", t["id"])]
            if k:
                fields.append(("flame", str(k)))
            items.append(lf.Item("actor", item_id.split(":", 1)[1], fields))
            stats["actors"] += 1
            stats["off" if not f["enabled"] else "on"] += 1
            stats["by_type"][t["id"]] += 1
    for gi, g in enumerate(glows):
        ts = owners.get(gi, set())
        if len(ts) == 1:
            tid = next(iter(ts))
            items.append(lf.Item("glow", g["key"], [("pos", vec(g["centre"])), ("fixture_type", tid)]))
            stats["glows"] += 1
            t = next(x for x in cat["types"] if x["id"] == tid)
            if fl.type_fields(cat, t)["glow"] == "suppress":
                stats["suppressed"] += 1
        elif len(ts) > 1:
            notes.append(f"zone {r['zone']} room {r['room']}: glow {g['key']} spans fixtures of types "
                         f"{', '.join(sorted(ts))}: left as it is")
    return items, notes


EMISSIVE_CATALOG = ROOT / "pc/assets/emissive/catalog.json"


def load_window_entries(path=EMISSIVE_CATALOG):
    """The emissive entries that put rect lights on their panes."""
    try:
        entries = json.loads(pathlib.Path(path).read_text(encoding="utf-8")).get("entries", [])
    except OSError:
        return []
    return [e for e in entries if e.get("enabled", True) and isinstance(e.get("rect_light"), dict)]


def window_lights(entries, r, vram, stats):
    """Rect lights fitted to the panes of window entries (load_window_entries) in room r."""
    items = []
    polys = r["polys"]
    for e in entries:
        rooms = (e.get("scope") or {}).get("rooms")
        if rooms and [r["zone"], r["room"]] not in [list(x) for x in rooms]:
            continue
        keys = {m["key"] for m in e.get("match", [])}
        idx = [i for i, p in enumerate(polys) if p["ikey"] in keys and fl.captured(p)]
        if not idx:
            continue
        spec = e["rect_light"]
        indices = set((e.get("mask") or {}).get("indices") or range(256))
        # panes: coplanar polygons whose bounds touch (4 units)
        uf = fl.UF(len(idx))
        lo = [polys[i]["V"].min(0) - 4 for i in idx]
        hi = [polys[i]["V"].max(0) + 4 for i in idx]
        for a in range(len(idx)):
            na = polys[idx[a]]["normal"]
            for b in range(a + 1, len(idx)):
                nb = polys[idx[b]]["normal"]
                if float(np.dot(na, nb)) < 0.95:
                    continue
                if abs(float(np.dot(na, polys[idx[b]]["V"][0] - polys[idx[a]]["V"][0]))) > 4:
                    continue
                if np.all(lo[a] <= hi[b]) and np.all(lo[b] <= hi[a]):
                    uf.u(a, b)
        panes = collections.defaultdict(list)
        for a in range(len(idx)):
            panes[uf.f(a)].append(idx[a])
        panes = sorted(panes.values(), key=lambda m: min(polys[i]["off"] for i in m))
        # More panes than the entry's max_per_room (a hall of tall windows): the panes of one wall (the
        # same plane) become one light along it, so the room stays inside the lights' budgets (OpenGL
        # with ray-traced shadows keeps 16, plain lights first).
        if len(panes) > spec.get("max_per_room", 6):
            walls = collections.OrderedDict()
            for m in panes:
                nrm = -polys[m[0]]["normal"]
                plane = (tuple(int(round(float(x))) for x in nrm),
                         int(round(float(np.dot(nrm, polys[m[0]]["V"][0])) / 16)))
                walls.setdefault(plane, []).extend(m)
            panes = list(walls.values())
            stats["window_walls"] += 1
        for members in panes:
            pts = np.concatenate([polys[i]["V"] for i in members])
            area = sum(polys[i]["area"] for i in members)
            n = -sum(polys[i]["normal"] * polys[i]["area"] for i in members)  # the front: -(e1 x e2)
            n = n / (np.linalg.norm(n) + 1e-9)
            up = np.array([0.0, -1.0, 0.0]) if abs(n[1]) < 0.7 else np.array([0.0, 0.0, 1.0])
            up = up - n * float(np.dot(up, n))
            up /= np.linalg.norm(up) + 1e-9
            right = np.cross(up, n)
            u, v = pts @ right, pts @ up
            if area < spec.get("min_area", 64) or u.max() - u.min() < 2 or v.max() - v.min() < 2:
                continue
            centre = pts.mean(0)
            centre = centre + right * ((u.max() + u.min()) / 2 - float(centre @ right)) \
                + up * ((v.max() + v.min()) / 2 - float(centre @ up))
            # the pane's colour: its glowing texels' mean (the mask's CLUT indices), x the tint
            rgb, count = np.zeros(3), 0
            for i in members:
                p = polys[i]
                us, vs = p["uv"][:, 0], p["uv"][:, 1]
                gu, gv = np.meshgrid(np.arange(us.min(), us.max() + 1), np.arange(vs.min(), vs.max() + 1))
                t, ii = fl.texel(vram, gu, gv, p["tpage"], p["clut"])
                ok = np.isin(ii, list(indices)) & (t != 0)
                if ok.any():
                    rgb += fl.rgb15(t[ok]).sum(0)
                    count += int(ok.sum())
            rgb = rgb / count if count else np.ones(3)
            rgb = rgb * np.array(e.get("color", [1, 1, 1]), float)
            rgb = rgb / max(float(rgb.max()), 1e-6)
            pos = centre + n * spec.get("offset", 2)
            first = min(polys[i]["off"] for i in members)
            item_id = lf.generated_id("light", f"window_{e['id']}", r["zone"], r["room"], first)
            fields = [("name", f"Window light {e['id']}"), ("type", "rect"), ("pos", vec(pos)),
                      ("dir", ",".join(lf.format_number(float(x), 4) for x in n)),
                      ("color", ",".join(lf.format_number(float(x), 3) for x in rgb)),
                      ("intensity", lf.format_number(float(spec.get("intensity", 1)), 3)),
                      ("radius", lf.format_number(float(spec.get("radius", 600)), 1)),
                      ("size", vec([max(u.max() - u.min(), 8), max(v.max() - v.min(), 8)])),
                      ("shadows", "1" if spec.get("shadows", 1) else "0")]
            items.append(lf.Item("light", item_id.split(":", 1)[1], fields))
            stats["windows"] += 1
    return items


def room_text(zone, room, items):
    entries = [lf.Entry("define", it.id, item=it) for it in items]
    comment = (f"zone {zone} room {room}: generated by pc/tools/generate_lighting.py from "
               "pc/assets/fixtures/catalog.json; do not edit (override by id in rooms/)")
    if any(it.id.startswith("light:gen.window_") for it in items):
        comment += "; window lights from pc/assets/emissive/catalog.json"

    return lf.format_file(entries, comment)


def zone_rooms(task):
    """One zone's rooms (a worker): [(room, text or None, notes)] and stats."""
    catalog, zone, rooms_wanted = task
    cat = fl.load_catalog(catalog)
    disc = fl.Disc()
    stats = collections.Counter()
    stats["by_type"] = collections.Counter()
    out = []
    review = outdoor.load_review()
    moods = outdoor.load_atmosphere()
    windows = load_window_entries()
    try:
        vram, rooms = fl.zone_vram(disc, zone)
    except (OSError, KeyError, ValueError, IndexError):
        return out, stats
    for room, name in enumerate(rooms):
        if not name or (rooms_wanted and room not in rooms_wanted):
            continue
        r = fl.load_room(disc, zone, room, vram, name)
        items, notes = room_items(cat, r, stats)
        items += window_lights(windows, r, vram, stats)
        # Open to the sky: the room's moon and sky (outdoor.py).
        m = outdoor.measure(r)
        is_outdoor, _ = outdoor.decide(m, zone, room, review)
        if is_outdoor:
            sun, sky = outdoor.environment(m, zone)
            items += [lf.Item("env", "sun", sun), lf.Item("env", "sky", sky)]
            stats["outdoor"] += 1
        # The area's atmosphere: haze settings and a ground fog (atmosphere.json).
        sets, fog = outdoor.atmosphere(moods, zone, is_outdoor)
        items += [lf.Item("set", key, [("value", value)]) for key, value in sets]
        if fog is not None:
            items.append(lf.Item("env", "fog", fog))
            stats["fog"] += 1
        stats["hazed"] += bool(sets)
        out.append((room, room_text(zone, room, items) if items else None, notes, [it.id for it in items]))
    return out, stats


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--zone", type=int, action="append", help="zones (repeatable; default every zone)")
    ap.add_argument("--room", type=int, action="append", help="rooms of those zones (repeatable)")
    ap.add_argument("--pack", default="default")
    ap.add_argument("--root", default=str(ROOT / "lighting"), help="the packs' directory")
    ap.add_argument("--catalog", default=str(fl.CATALOG))
    ap.add_argument("--dry-run", action="store_true", help="report what would change; write nothing")
    ap.add_argument("--check", action="store_true", help="exit 1 when a generated file is not up to date")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 2) // 2)),
                    help="zones read in parallel (the output does not depend on it)")
    ap.add_argument("--gallery", metavar="DIR", help="then capture a before/after gallery (local only)")
    args = ap.parse_args()
    disc = fl.Disc()
    zones = args.zone or disc.zones()
    out_dir = pathlib.Path(args.root) / args.pack / "generated"
    stats = collections.Counter()
    stats["by_type"] = collections.Counter()
    changed, written, removed, notes_all, rooms_seen = [], 0, 0, [], 0
    tasks = [(args.catalog, zone, args.room) for zone in zones]
    if args.jobs > 1 and len(tasks) > 1:
        import multiprocessing
        with multiprocessing.Pool(min(args.jobs, len(tasks))) as pool:
            results = pool.map(zone_rooms, tasks, chunksize=1)
    else:
        results = [zone_rooms(t) for t in tasks]
    for zone, (rooms, zstats) in zip(zones, results):
        by_type = zstats.pop("by_type")
        stats.update(zstats)
        stats["by_type"].update(by_type)
        for room, new, notes, ids in rooms:
            rooms_seen += 1
            notes_all += notes
            path = out_dir / lf.room_file_name(zone, room)
            old = path.read_text(encoding="utf-8") if path.exists() else None
            if old == new:
                continue
            before = {e.id for e in lf.parse_text(old).entries} if old else set()
            after = set(ids)
            changed.append((path, len(after - before), len(before - after), len(before & after)))
            if args.dry_run or args.check:
                continue
            if new is None:
                path.unlink()
                removed += 1
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(new, encoding="utf-8")
                written += 1
    if not args.quiet:
        for n in notes_all:
            print("note:", n)
        for path, added, gone, kept in changed:
            verb = "would change" if args.dry_run or args.check else "wrote"
            print(f"{verb} {os.path.relpath(path, ROOT)}: +{added} -{gone} ({kept} kept or changed)")
    by_type = ", ".join(f"{k} {v}" for k, v in sorted(stats["by_type"].items()))
    print(f"{stats['fixtures']} fixtures matched ({stats['rule']} by rule, {stats['unmatched']} unmatched); "
          f"{stats['actors']} actors ({stats['on']} on, {stats['off']} off by default; {stats['linked']} linked to "
          f"a game glow); {stats['glows']} glows typed ({stats['suppressed']} suppressed); "
          f"{stats['outdoor']} rooms open to the sky (env sun, env sky); {stats['hazed']} with an area haze, "
          f"{stats['fog']} with a ground fog (env fog); {stats['windows']} window lights")
    if by_type and not args.quiet:
        print(f"actors by type: {by_type}")
    if args.dry_run or args.check:
        print(f"{len(changed)} file(s) not up to date")
    else:
        print(f"{written} file(s) written, {removed} removed, {rooms_seen - len(changed)} room(s) unchanged")
    if args.gallery:
        import lighting_gallery
        gallery_args = ["--out", args.gallery]
        for z in args.zone or []:
            gallery_args += ["--zone", str(z)]
        for rm in args.room or []:
            gallery_args += ["--room", str(rm)]
        return lighting_gallery.main(gallery_args)
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
