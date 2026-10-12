#!/usr/bin/env python3
"""Build and check the fixture catalog (pc/assets/fixtures/catalog.json)
from the user's own disc.

    python3 pc/tools/fixture_scan.py --check            every room: does each fixture match one type?
    python3 pc/tools/fixture_scan.py --update           add types for unmatched fixtures (review: "new"),
                                                        refresh the instance counts
    python3 pc/tools/fixture_scan.py --init             build the catalog from scratch (the scan's
                                                        classes; refuses to replace an existing one
                                                        without --force)
    python3 pc/tools/fixture_scan.py --zone 9 --report  the fixtures of zone 9 and their types

A fixture is a cluster of room polygons around an emitter (an additive or
animated polygon, a flame texture, warm "flame" texels, a texture effect)
with the small meshes touching it (pc/tools/fixture_lib.py). A type is one
fixture model: every instance of it has the same emitter textures, polygon
count, size (within the catalog's tolerance) and holder (the textures of its
other polygons). The catalog says what each type is (its class) and how it
is lit (prefab, parameters, light offset, glow treatment); the generator
(generate_lighting.py) gives every instance of a type the same treatment, so
a fix to a type fixes all its instances.

Classes come from the scan's heuristics once, when a type is first found
(review: "scan" or "new"); after that the catalog is the source of truth:
--update never reclassifies or renumbers a type, and keeps every hand edit.
The disc is read at run time (ROOD_DISC, default disks/SLUS-01040.bin);
the catalog holds hashes and numbers only. Nothing else is written.
"""
import argparse
import collections
import multiprocessing
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixture_lib as fl  # noqa: E402

FIXTURE_FAMILIES = ("fire", "magic", "lamp", "window")

# Per class: what the generator places for its instances (types may override).
# Prefab height parameters are 0: actors sit on the flame sprite's centre.
CLASS_DEFAULTS = {
    "candle": {"prefab": "candle", "params": {"flame_height": 0}, "glow": "drive", "fixture_radius": 14},
    "candelabra": {"prefab": "candle", "params": {"flame_height": 0}, "glow": "drive", "fixture_radius": 14},
    "candle_flame": {"prefab": "candle", "params": {"flame_height": 0}, "glow": "drive", "fixture_radius": 14},
    "wall_torch": {"prefab": "torch", "params": {"flame_height": 0, "reach": 0}, "glow": "drive", "fixture_radius": 24},
    "torch_flame": {"prefab": "torch", "params": {"flame_height": 0, "reach": 0}, "glow": "drive", "fixture_radius": 24},
    "standing_torch": {"prefab": "torch", "params": {"flame_height": 0, "reach": 0}, "glow": "drive",
                       "fixture_radius": 24},
    "torch_group": {"prefab": "torch", "params": {"flame_height": 0, "reach": 0}, "glow": "drive", "fixture_radius": 24},
    "brazier": {"prefab": "brazier", "params": {"height": 0}, "glow": "drive", "fixture_radius": 40},
    "fireplace": {"prefab": "fireplace", "params": {"height": 0, "depth": 0}, "glow": "drive", "fixture_radius": 48},
    "fire_sprite": {"prefab": "fireplace", "params": {"height": 0, "depth": 0}, "glow": "drive", "fixture_radius": 48},
    "lantern": {"prefab": "lantern", "params": {"height": 0}, "glow": "drive", "fixture_radius": 30},
    "wall_lantern": {"prefab": "lantern", "params": {"height": 0}, "glow": "drive", "fixture_radius": 30},
    "street_lantern": {"prefab": "lantern", "params": {"height": 0}, "glow": "drive", "fixture_radius": 30},
    "glass_lamp": {"prefab": "lantern", "params": {"height": 0}, "glow": "drive", "fixture_radius": 30},
    # inferred fill lights: generated, off by default
    "save_point": {"prefab": "magic_glow", "params": {"height": 0}, "glow": "leave", "fixture_radius": 24,
                   "enabled": False},
    "magic_glow": {"prefab": "magic_glow", "params": {"height": 0}, "glow": "leave", "fixture_radius": 24,
                   "enabled": False},
    "window": {"prefab": "window_light", "params": {"inset": 0}, "glow": "leave", "fixture_radius": 16,
               "enabled": False},
    # not lights: their additive polygons are not light sources
    "shaft": {"prefab": "none", "glow": "suppress", "haze": True},
    "decal": {"prefab": "none", "glow": "suppress"},
    "cobweb": {"prefab": "none", "glow": "suppress"},
    "floor_arrows": {"prefab": "none", "glow": "suppress"},
    "exit_marker": {"prefab": "none", "glow": "leave"},
    "animated_surface": {"prefab": "none", "glow": "leave"},
    "warm_texels": {"prefab": "none", "glow": "leave"},
    "other": {"prefab": "none", "glow": "leave"},
}


def classes_table():
    out = {}
    for name, (family, description) in fl.CLASSES.items():
        d = CLASS_DEFAULTS[name]
        c = {"family": family, "description": description, "prefab": d["prefab"]}
        if d["prefab"] != "none":
            c["params"] = d.get("params", {})
            c["fixture_radius"] = d.get("fixture_radius", 16)
            c["enabled"] = d.get("enabled", True)
        c["glow"] = d["glow"]
        if d.get("haze"):
            c["haze"] = True
        out[name] = c
    return out


# ---- the scan ----


def scan_zone(zone):
    disc = fl.Disc()
    try:
        vram, rooms = fl.zone_vram(disc, zone)
    except (OSError, KeyError, ValueError, IndexError, struct.error):
        return zone, []
    out = []
    for room, name in enumerate(rooms):
        if not name:
            continue
        r = fl.load_room(disc, zone, room, vram, name)
        for c in fl.clusters(r):
            d = fl.describe(r, c)
            d["class"], d["confidence"] = fl.classify(d)
            d["flames_local"] = [fl.local_of(d, f["world"]) for f in d["flames"]] or [fl.local_of(d, d["centre"])]
            d["centre_local"] = fl.local_of(d, d["centre"])
            for k in ("emit", "body", "centre", "origin", "flames", "wall", "floor"):
                d.pop(k, None)
            out.append(d)
    return zone, out


def scan(zones, jobs):
    found = []
    with multiprocessing.Pool(jobs) as pool:
        for zone, ds in sorted(pool.imap_unordered(scan_zone, zones), key=lambda x: x[0]):
            found += ds
    return found


# ---- grouping fixtures into types (a fresh catalog) ----


def jacc(a, b):
    ca, cb = collections.Counter(a), collections.Counter(b)
    u = sum((ca | cb).values())
    return sum((ca & cb).values()) / u if u else 0


def ext_close(a, b, rel=0.2, absn=16):
    a, b = np.sort(a), np.sort(b)
    return bool(np.all(np.abs(a - b) <= np.maximum(absn, rel * np.maximum(a, b))))


def shape_close(a, b):
    if a["npoly"] != b["npoly"]:
        return False
    sa, sb, ca, cb = map(np.array, (a["shape"], b["shape"], a["cdist"], b["cdist"]))
    return bool(np.all(np.abs(sa - sb) <= np.maximum(2, 0.15 * np.maximum(sa, sb))) and np.all(np.abs(ca - cb) <= 3))


def group(ds):
    """The scan's grouping (exact signatures, then similar models) -> [[index]]."""
    ex = collections.OrderedDict()
    for i, d in enumerate(ds):
        key = (d["class"], tuple(d["keys"]), d["npoly"], tuple(np.round(np.sort(d["ext"]) / 8).astype(int)))
        ex.setdefault(key, []).append(i)
    reps = list(ex)
    info = [ds[ex[k][0]] for k in reps]
    par = list(range(len(reps)))

    def f(a):
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a
    for a in range(len(reps)):
        for b in range(a + 1, len(reps)):
            x, y = info[a], info[b]
            if x["class"] != y["class"] or set(x["kinds"]) != set(y["kinds"]) or not ext_close(x["ext"], y["ext"]):
                continue
            model = False
            if (fl.CLASSES[x["class"]][0] in FIXTURE_FAMILIES and x["npoly"] == y["npoly"]
                    and set(x["keys"]) & fl.FLAME_KEYS == set(y["keys"]) & fl.FLAME_KEYS):
                fa, fb = np.array(x["centre_local"]), np.array(y["centre_local"])
                model = abs(fa[1] - fb[1]) <= 6 and abs(np.hypot(fa[0], fa[2]) - np.hypot(fb[0], fb[2])) <= 8
            if jacc(x["keys"], y["keys"]) >= 0.6 or (jacc(x["ekeys"], y["ekeys"]) >= 0.8 and shape_close(x, y)) or model:
                par[f(a)] = f(b)
    types = collections.OrderedDict()
    for a, k in enumerate(reps):
        types.setdefault(f(a), []).extend(ex[k])
    return [sorted(t) for t in types.values()]


def round_list(v, n=1):
    return [round(float(x), n) + 0.0 for x in v]


def match_rule(members):
    ext = np.array([fl.ext_sorted(d) for d in members])
    return {
        "emitters": sorted({d["emitters"] for d in members}),
        "polygons": [min(d["npoly"] for d in members), max(d["npoly"] for d in members)],
        "extent": {"min": round_list(ext.min(0)), "max": round_list(ext.max(0))},
        "holders": sorted({d["holder"] for d in members}),
    }


def stats(t, members):
    t["instances"] = len(members)
    t["rooms"] = len({(d["zone"], d["room"]) for d in members})
    t["zones"] = ",".join(str(z) for z in sorted({d["zone"] for d in members}))


def new_type(tid, members, review):
    rep = members[0]
    t = {"id": tid, "class": rep["class"], "confidence": rep["confidence"], "review": review}
    stats(t, members)
    t["match"] = match_rule(members)
    t["flames"] = [round_list(p) for p in rep["flames_local"]]
    return t


def order_key(cls, members):
    fam = fl.CLASSES[cls][0]
    first = members[0]
    return (fam not in FIXTURE_FAMILIES, fam, -len(members), first["zone"], first["room"], first["first"])


def settle(ds, groups):
    """Types whose rules overlap (the grouping is fuzzy): move each fixture
    to the type its rule matches, until that changes nothing."""
    for _ in range(8):
        cat = {"types": [dict(new_type("t%d" % n, [ds[i] for i in g], ""), _n=n) for n, g in enumerate(groups)]}
        moved = collections.defaultdict(list)
        for i, d in enumerate(ds):
            t, _ = fl.match(cat, d)
            moved[t["_n"]].append(i)
        new = [sorted(moved[n]) for n in range(len(groups)) if moved.get(n)]
        if sorted(new) == sorted(groups):
            return groups
        groups = new
    return groups


def fresh_catalog(ds):
    groups = settle(ds, group(ds))
    groups.sort(key=lambda g: order_key(ds[g[0]]["class"], [ds[i] for i in g]))
    cat = {
        "format": 1,
        "comment": "Fixture types of the game's rooms (pc/docs/Fixture_Catalog.md). Hashes and numbers only; "
                   "built from the user's disc by pc/tools/fixture_scan.py, then edited by hand or in the editor.",
        "tolerance": {"abs": 4, "rel": 0.1},
        "classes": classes_table(),
        "types": [],
    }
    for n, g in enumerate(groups, 1):
        cat["types"].append(new_type("T%03d" % n, [ds[i] for i in g], "scan"))
    return cat


def assign(cat, ds):
    """Each fixture's type: {index: (type id, 'exact'|'rule')}; unmatched listed."""
    out, unmatched = {}, []
    for i, d in enumerate(ds):
        t, how = fl.match(cat, d)
        if t is None:
            unmatched.append(i)
        else:
            out[i] = (t["id"], how)
    return out, unmatched


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--catalog", default=str(fl.CATALOG))
    ap.add_argument("--zone", type=int, action="append", help="only these zones (repeatable; default all)")
    ap.add_argument("--jobs", type=int, default=6)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--check", action="store_true", help="validate (default)")
    g.add_argument("--update", action="store_true", help="add new types for unmatched fixtures, refresh counts")
    g.add_argument("--init", action="store_true", help="build a fresh catalog")
    ap.add_argument("--force", action="store_true", help="--init: replace an existing catalog")
    ap.add_argument("--report", action="store_true", help="list every fixture and its type")
    args = ap.parse_args()
    disc = fl.Disc()
    zones = args.zone or disc.zones()
    ds = scan(zones, args.jobs)
    print(f"{len(ds)} fixtures in {len({(d['zone'], d['room']) for d in ds})} rooms of {len(zones)} zones")
    if args.init:
        if os.path.exists(args.catalog) and not args.force:
            print(f"{args.catalog} exists (use --update, or --init --force to start over)")
            return 1
        if args.zone:
            print("note: --init with --zone builds a catalog of those zones only")
        cat = fresh_catalog(ds)
        os.makedirs(os.path.dirname(args.catalog), exist_ok=True)
        fl.save_catalog(cat, args.catalog)
        fams = collections.Counter(fl.CLASSES[t["class"]][0] for t in cat["types"])
        print(f"wrote {args.catalog}: {len(cat['types'])} types ({dict(sorted(fams.items()))})")
    cat = fl.load_catalog(args.catalog)
    types = {t["id"]: t for t in cat["types"]}
    got, unmatched = assign(cat, ds)
    if args.update:
        if unmatched:
            fresh = [ds[i] for i in unmatched]
            groups = group(fresh)
            groups.sort(key=lambda g: order_key(fresh[g[0]]["class"], [fresh[i] for i in g]))
            n = max(int(t["id"][1:]) for t in cat["types"]) if cat["types"] else 0
            for g in groups:
                n += 1
                cat["types"].append(new_type("T%03d" % n, [fresh[i] for i in g], "new"))
            print(f"added {len(groups)} new types for {len(unmatched)} unmatched fixtures (review: \"new\")")
            got, unmatched = assign(cat, ds)
        if not args.zone:  # counts are over the whole disc
            members = collections.defaultdict(list)
            for i, (tid, _) in got.items():
                members[tid].append(ds[i])
            for t in cat["types"]:
                stats(t, members.get(t["id"], []))
        fl.save_catalog(cat, args.catalog)
        types = {t["id"]: t for t in cat["types"]}
    how = collections.Counter(h for _, h in got.values())
    per = collections.Counter(tid for tid, _ in got.values())
    if args.report:
        for i, d in enumerate(ds):
            tid, h = got.get(i, ("-", "unmatched"))
            cls = types[tid]["class"] if tid in types else d["class"] + "?"
            print(f"zone {d['zone']:3d} room {d['room']:2d} poly {d['first']:6d}  {tid:5s} {h:9s} {cls:16s} "
                  f"flames {len(d['flames_local'])} npoly {d['npoly']}")
    print(f"matched {len(got)} ({how['exact']} exact, {how['rule']} by rule), unmatched {len(unmatched)}")
    problems = 0
    for i in unmatched[:40]:
        d = ds[i]
        print(f"  unmatched: zone {d['zone']} room {d['room']} poly {d['first']} ({d['class']}, {d['npoly']} polygons, "
              f"emitters {d['emitters']})")
    problems += len(unmatched)
    if not args.zone and not args.update:
        for t in cat["types"]:
            if per.get(t["id"], 0) != t.get("instances", 0):
                print(f"  {t['id']}: {per.get(t['id'], 0)} instances on this disc, the catalog says {t.get('instances')}")
    for t in cat["types"]:
        if t["class"] not in cat["classes"]:
            print(f"  {t['id']}: unknown class {t['class']}")
            problems += 1
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
