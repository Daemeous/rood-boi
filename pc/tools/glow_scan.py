#!/usr/bin/env python3
"""Glow sweep: every texture the rooms draw, ranked by how likely it is to
show light (windows, magic glyphs and circles, fire and coals, lamps), as a
candidate list for the emissive catalog (pc/docs/Emissive.md).

    python3 pc/tools/glow_scan.py                    every room on the disc
    python3 pc/tools/glow_scan.py --zone 29 --zone 34 some zones only
    python3 pc/tools/glow_scan.py --names rooms.json room names from RPC `rooms`
    python3 pc/tools/glow_scan.py --live uses.json   add object textures seen in the game
                                                     (glow_scan.py --collect writes it)

Read offline from your disc (ROOD_DISC, default disks/SLUS-01040.bin) with
fixture_lib.py's room reader: every room polygon the game draws, with its
texture's content key (the same `key` the emissive catalog matches), page,
CLUT, UV box and world area. Textures are grouped:

  variant    one key with one palette (key + the CLUT's colour hash, as the
             catalog's `clut` field): one candidate entry's `match` line;
  candidate  variants whose boxes overlap in VRAM in some zone (one image
             cut into several polygon boxes, like the lattice window's four
             keys), so one entry covers them all and every room they are in;
  palette variants  the same key drawn with other palettes (the green and
             the orange glyph): listed on each candidate.

Each candidate is scored from its texels (bright, saturated or pale motifs
on a dark ground, warm ramps, lattice panes, symmetry), its polygons (floor,
wall, area, additive) and the fixture it belongs to (fixture_lib clusters
matched to pc/assets/fixtures/catalog.json: candles, braziers, lanterns), and
put in one category: windows, glyphs, circles, fire, lamps, other. Entries
already in pc/assets/emissive/catalog.json are marked curated.

Objects (crates, the floating blocks, props) are not room geometry: they are
seen only in the running game. `--collect` drives a game (rood_rpc.Game,
room_goto into every room, RPC `emissive uses {stats}` and `texture`) and
writes their uses; `--live FILE` merges them in.

Output (--out, default pc/exports/glow_scan, gitignored: game imagery,
never commit it): candidates.json, one contact sheet per category
(sheet_<category>.png: crop, id, glow colour swatch, room count, curated
mark), crops/<id>.png, and index.html (the summary to review).
"""
import argparse
import collections
import hashlib
import html
import json
import os
import pathlib
import sys
import time

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fixture_lib as fl  # noqa: E402

ROOT = fl.ROOT
EMISSIVE_CATALOG = ROOT / "pc/assets/emissive/catalog.json"
STRONG = 0.5  # a score from here on is a strong candidate (the summary's counts)
CATEGORIES = ["windows", "glyphs", "circles", "fire", "lamps", "other"]
# fixture catalog classes -> the sweep's category for textures of that fixture
FIXTURE_CATEGORY = {
    "candle": "lamps", "candelabra": "lamps", "candle_flame": "lamps", "wall_torch": "lamps",
    "torch_flame": "lamps", "standing_torch": "lamps", "torch_group": "lamps", "brazier": "fire",
    "fireplace": "fire", "fire_sprite": "fire", "lantern": "lamps", "wall_lantern": "lamps",
    "street_lantern": "lamps", "glass_lamp": "lamps", "save_point": "circles", "magic_glow": "glyphs",
    "window": "windows",
}

# ---- texels ----


def clut_key(vram, tpage, clut):
    """emissive.cpp clut_key: md5 of the palette's 16 or 256 colours."""
    depth = (tpage >> 7) & 3
    if depth >= 2:
        return ""
    n = 16 if depth == 0 else 256
    cx, cy = (clut & 63) * 16, (clut >> 6) & 511
    row = vram[cy, cx:cx + n].astype("<u2")
    return hashlib.md5(row.tobytes()).hexdigest()[:10]


def crop(vram, tpage, clut, box):
    """Raw 15-bit colours and indices of a UV box [u0, v0, u1, v1] (inclusive)."""
    u0, v0, u1, v1 = box
    gu, gv = np.meshgrid(np.arange(u0, u1 + 1), np.arange(v0, v1 + 1))
    return fl.texel(vram, gu, gv, tpage, clut)


def rgb_image(raw):
    """RGBA uint8 of raw texels (0x0000 transparent)."""
    rgb = (fl.rgb15(raw) * 255 + 0.5).astype(np.uint8)
    a = np.where(raw != 0, 255, 0).astype(np.uint8)
    return np.dstack([rgb, a])


def hue_of(rgb):
    mx, mn = rgb.max(-1), rgb.min(-1)
    d = np.maximum(mx - mn, 1e-6)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60
    return np.where(mx - mn < 1e-6, -1, h)


def components(mask):
    """Connected components (4-neighbour) of a boolean mask: their sizes."""
    try:
        from scipy import ndimage
        lab, n = ndimage.label(mask)
        return np.bincount(lab.ravel())[1:] if n else np.zeros(0, int)
    except ImportError:  # (rough fallback: one component)
        return np.array([int(mask.sum())]) if mask.any() else np.zeros(0, int)


def features(raw):
    """Texel features of an image (raw 15-bit colours)."""
    op = raw != 0
    n = int(op.sum())
    if n < 1:
        return None
    rgb = fl.rgb15(raw)
    lum = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]
    mx, mn = rgb.max(-1), rgb.min(-1)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    hue = hue_of(rgb)
    ref = float(np.percentile(lum[op], 50))
    # hot: clearly brighter than the texture's own ground and bright or vivid
    hot = op & (((lum >= 0.55) | ((mx >= 0.6) & (sat >= 0.5))) & (lum >= ref + 0.12))
    nh = int(hot.sum())
    warm = hot & (sat >= 0.75) & ((hue >= 0) & ((hue <= 45) | (hue >= 340)))  # (gold trims: less saturated)
    pale = hot & (sat < 0.3)
    vivid = hot & (sat >= 0.4)
    bg = op & ~hot
    bg_lum = float(lum[bg].mean()) if bg.any() else float(lum[op].mean())
    hot_lum = float(lum[hot].mean()) if nh else 0.0
    out = {
        "texels": n,
        "hot_frac": nh / n,
        "warm_frac": int(warm.sum()) / max(nh, 1),
        "pale_frac": int(pale.sum()) / max(nh, 1),
        "vivid_frac": int(vivid.sum()) / max(nh, 1),
        "bg_lum": bg_lum,
        "hot_lum": hot_lum,
        "hot_val": float(mx[hot].mean()) if nh else 0.0,
        "contrast": hot_lum - bg_lum if nh else 0.0,
        "glow_rgb": [round(float(x), 3) for x in (rgb[hot].mean(0) if nh else rgb[op].mean(0))],
        "mean_rgb": [round(float(x), 3) for x in rgb[op].mean(0)],
        "blobs": 0, "largest_blob": 0.0, "symmetry": 0.0, "ring": 0.0, "stroke": 0.0,
        "hue_spread": 0.0, "tint": 0.0, "halo": 0.0,
    }
    if nh:
        # strokes: share of hot texels with a non-hot neighbour (thin lines ~1)
        pad = np.pad(hot, 1)
        inner = pad[:-2, 1:-1] & pad[2:, 1:-1] & pad[1:-1, :-2] & pad[1:-1, 2:]
        out["stroke"] = float((hot & ~inner).sum() / nh)
        hh = hue[hot & (sat >= 0.25)]
        if len(hh) >= 3:
            a = np.radians(hh)
            rlen = max(1e-9, float(np.hypot(np.cos(a).mean(), np.sin(a).mean())))
            out["hue_spread"] = float(np.degrees(np.sqrt(-2 * np.log(min(1.0, rlen)))))
        # tint: the glow's chromaticity against the ground's (a coloured line on grey stone)
        if bg.any():
            ch = rgb[hot].mean(0)
            cb = rgb[bg].mean(0)
            out["tint"] = float(np.abs(ch / max(ch.sum(), 1e-6) - cb / max(cb.sum(), 1e-6)).sum())
        # halo: the texels around the hot ones (2 texels) more saturated than the
        # rest of the ground (a glyph's coloured glow fading into the stone)
        near = hot.copy()
        for _ in range(2):
            p2 = np.pad(near, 1)
            near = near | p2[:-2, 1:-1] | p2[2:, 1:-1] | p2[1:-1, :-2] | p2[1:-1, 2:]
        ring_ = near & bg
        far = bg & ~near
        if ring_.any() and far.any():
            out["halo"] = float(sat[ring_].mean() - sat[far].mean())
    if nh:
        sizes = components(hot)
        out["blobs"] = int((sizes >= 2).sum())
        out["largest_blob"] = float(sizes.max() / nh)
        h, w = hot.shape
        if h >= 6 and w >= 6:
            s = min(h, w)
            sq = hot[(h - s) // 2:(h - s) // 2 + s, (w - s) // 2:(w - s) // 2 + s]
            ious = []
            for t in (np.rot90(sq), np.fliplr(sq), np.flipud(sq)):
                u = (sq | t).sum()
                ious.append((sq & t).sum() / u if u else 0)
            out["symmetry"] = float(np.mean(ious))
            # ring: hot texels concentrate at one radius from the centre
            # ring: hot texels at a steady distance from the centre or a corner
            # (a circle drawn as four quarter polygons)
            yy, xx = np.mgrid[:s, :s]
            if sq.sum() >= 8:
                best = 0.0
                for cy, cx, r in (((s - 1) / 2, (s - 1) / 2, s / 2), (0, 0, s), (0, s - 1, s), (s - 1, 0, s),
                                  (s - 1, s - 1, s)):
                    rh = np.hypot(yy - cy, xx - cx)[sq] / r
                    best = max(best, float(max(0.0, 1 - 4 * rh.std()) * (rh.mean() > 0.3)))
                out["ring"] = best
    return out


# ---- the catalog ----


def load_emissive(path=EMISSIVE_CATALOG):
    """key -> [(entry id, clut or None)] of the curated catalog."""
    out = collections.defaultdict(list)
    try:
        cat = json.loads(pathlib.Path(path).read_text())
    except OSError:
        return out, []
    for e in cat.get("entries", []):
        for m in e.get("match", []):
            out[m["key"]].append((e["id"], m.get("clut")))
    return out, cat.get("entries", [])


def curated_of(cur, key, ck):
    return sorted({eid for eid, c in cur.get(key, []) if c is None or c == ck})


# ---- the scan ----


def fixture_tags(r, fcat):
    """poly index -> fixture class, for every polygon of a matched fixture
    (its holder's polygons: the class + " holder")."""
    tags = {}
    try:
        cl = fl.clusters(r)
    except Exception:  # (a room the fixture reader cannot cluster)
        return tags
    for c in cl:
        try:
            d = fl.describe(r, c)
            t, _ = fl.match(fcat, d) if fcat else (None, "")
            cls = t["class"] if t else fl.classify(d)[0]
        except Exception:
            continue
        emit = {i for i in c["emit"] if r["polys"][i]["additive"] or r["polys"][i]["anim"]}
        for i in c["body"]:
            tags[i] = cls if i in emit else cls + " holder"
    return tags


def scan(disc, zones, log=print):
    """Every room's texture uses -> (variants, zone data, room list)."""
    variants = {}
    zdata = {}
    rooms_seen = []
    try:
        fcat = fl.load_catalog()
    except OSError:
        fcat = None
    for zone in zones:
        try:
            vram, names = fl.zone_vram(disc, zone)
        except Exception as e:  # (an empty or odd zone file)
            log(f"zone {zone}: {e}")
            continue
        boxes = collections.defaultdict(list)  # variant id -> boxes in this zone
        for room, name in enumerate(names):
            if not name:
                continue
            try:
                r = fl.load_room(disc, zone, room, vram, name)
            except Exception as e:
                log(f"zone {zone} room {room}: {e}")
                continue
            rooms_seen.append([zone, room])
            tags = fixture_tags(r, fcat)
            for i, p in enumerate(r["polys"]):
                us, vs = p["uv"][:, 0], p["uv"][:, 1]
                box = [int(us.min()), int(vs.min()), int(us.max()), int(vs.max())]
                ck = clut_key(vram, p["tpage"], p["clut"])
                vid = p["ikey"] + ":" + (ck or "direct")
                v = variants.get(vid)
                if v is None:
                    raw, idx = crop(vram, p["tpage"], p["clut"], box)
                    v = variants[vid] = {
                        "id": vid, "key": p["ikey"], "clut_key": ck, "size": [box[2] - box[0] + 1, box[3] - box[1] + 1],
                        "depth": (p["tpage"] >> 7) & 3, "first": {"zone": zone, "room": room, "tpage": p["tpage"],
                                                                 "clut": p["clut"], "box": box},
                        "rooms": set(), "polygons": 0, "area": 0.0, "floor": 0.0, "wall": 0.0, "ceiling": 0.0,
                        "additive": 0, "animated": 0, "uv_effect": 0, "fixture": collections.Counter(),
                        "features": features(raw),
                    }
                v["rooms"].add((zone, room))
                v["polygons"] += 1
                v["area"] += p["area"]
                ny = p["normal"][1]
                if abs(ny) > 0.8:
                    v["floor" if ny < 0 else "ceiling"] += p["area"]
                else:
                    v["wall"] += p["area"]
                v["additive"] += bool(p["additive"])
                v["animated"] += bool(p["anim"])
                v["uv_effect"] += p["uveff"] is not None
                if i in tags:
                    v["fixture"][tags[i]] += 1
                boxes[vid].append((p["tpage"], p["clut"], box))
        zdata[zone] = {"vram": vram, "boxes": boxes}
        log(f"zone {zone}: {len([n for n in names if n])} rooms, {len(boxes)} textures")
    return variants, zdata, rooms_seen


def overlaps(a, b):
    return a[0] <= b[2] and b[0] <= a[2] and a[1] <= b[3] and b[1] <= a[3] and \
        (min(a[2], b[2]) - max(a[0], b[0])) * (min(a[3], b[3]) - max(a[1], b[1])) > 0


def group(variants, zdata):
    """Candidates: variants joined when their boxes overlap in one page with
    one CLUT in some zone (one image cut into several boxes)."""
    ids = sorted(variants)
    pos = {v: k for k, v in enumerate(ids)}
    uf = fl.UF(len(ids))
    for z in zdata.values():
        bypage = collections.defaultdict(list)
        for vid, bl in z["boxes"].items():
            for tp, cl, box in {(t, c, tuple(b)) for t, c, b in bl}:
                bypage[(tp & 0x19F, cl)].append((box, vid))
        for lst in bypage.values():
            if len(lst) < 2 or len(lst) > 400:  # (a whole atlas page: tiles do not overlap)
                for a in range(len(lst)):
                    for b in range(a + 1, min(len(lst), a + 60)):
                        if lst[a][1] != lst[b][1] and overlaps(lst[a][0], lst[b][0]):
                            uf.u(pos[lst[a][1]], pos[lst[b][1]])
                continue
            for a in range(len(lst)):
                for b in range(a + 1, len(lst)):
                    if lst[a][1] != lst[b][1] and overlaps(lst[a][0], lst[b][0]):
                        uf.u(pos[lst[a][1]], pos[lst[b][1]])
    comp = collections.defaultdict(list)
    for v in ids:
        comp[uf.f(pos[v])].append(v)
    return list(comp.values())


def composite(members, variants, zdata):
    """The candidate's image: the union of its boxes in the zone holding most of them."""
    best = None
    for zone, z in zdata.items():
        here = [(vid, z["boxes"][vid][0]) for vid in members if vid in z["boxes"]]
        if here and (best is None or len(here) > len(best[1])):
            best = (zone, here)
    if best is None:
        f = variants[members[0]]["first"]
        return None, f["zone"], [f["box"]], f["tpage"], f["clut"]
    zone, here = best
    tp, cl = here[0][1][0], here[0][1][1]
    bs = [b for _, (t, c, b) in here if t == tp and c == cl]
    u0, v0 = min(b[0] for b in bs), min(b[1] for b in bs)
    u1, v1 = max(b[2] for b in bs), max(b[3] for b in bs)
    if (u1 - u0 + 1) * (v1 - v0 + 1) > 256 * 256:
        u0, v0, u1, v1 = bs[0]
    raw, _ = crop(zdata[zone]["vram"], tp, cl, [u0, v0, u1, v1])
    return raw, zone, [u0, v0, u1, v1], tp, cl


def classify(c):
    """(category, score, reasons) of a candidate from its features, polygons and fixture."""
    f = c["features"]
    if not f:
        return None, 0.0, []
    why = []
    area = max(c["area"], 1.0)
    flat = (c["floor"] + c["ceiling"]) / area
    wall = c["wall"] / area
    hot, con, bgl = f["hot_frac"], f["contrast"], f["bg_lum"]
    w, h = c["size"]
    fix = max(c["fixture"], key=c["fixture"].get) if c["fixture"] else None
    holder = bool(fix and fix.endswith(" holder"))
    fixcat = FIXTURE_CATEGORY.get(fix[:-7] if holder else fix)

    def clamp(x):
        return max(0.0, min(1.0, x))
    # how much it looks like light at all: truly bright texels well above a dark ground
    base = clamp(con / 0.45) * clamp((hot / 0.03) ** 0.5) * clamp((f["hot_val"] - 0.45) / 0.4)
    base *= 1.0 if bgl < 0.3 else 0.75 if bgl < 0.45 else 0.4
    if hot > 0.85:
        base *= 0.4  # (a uniformly bright surface: plaster, sky, sand)
    if min(w, h) < 6:
        base *= 0.3  # (a strip or a single texel: trims, edges)
    if f["blobs"] <= 1 and bgl < 0.03 and hot > 0.2:
        base *= 0.3  # (a white shape on pure black: a cutscene mask or silhouette)
    halo = clamp(f.get("halo", 0.0) / 0.15)
    scores = {k: 0.0 for k in CATEGORIES}
    glyph_shape = (min(w, h) >= 14 and max(w, h) <= 2.2 * min(w, h) and 0.01 <= hot <= 0.25
                   and f["stroke"] >= 0.7 and 1 <= f["blobs"] <= 6 and f["largest_blob"] >= 0.3 and bgl <= 0.25)
    if glyph_shape:
        scores["glyphs"] = base * (0.55 + 0.45 * halo) * (1.1 if flat > 0.5 else 0.9)
        why.append("thin motif on dark stone")
        if halo > 0.5:
            why.append("coloured halo")
    if f["ring"] >= 0.45 and hot >= 0.04 and f["vivid_frac"] >= 0.5 and min(w, h) >= 14:
        scores["circles"] = base * (0.5 + 0.5 * f["ring"]) * (0.6 + 0.4 * f["vivid_frac"])
        why.append("ring %.2f" % f["ring"])
    if f["warm_frac"] >= 0.6 and f["vivid_frac"] >= 0.6 and hot >= 0.03:
        sc = base * (0.6 + 0.4 * f["warm_frac"]) * (1 + min(0.3, f["hue_spread"] / 30))
        scores["fire"] = sc * (0.6 if glyph_shape and f["blobs"] <= 3 else 1.0)
        why.append("warm ramp")
    if wall >= 0.5 and 0.06 <= hot <= 0.95:
        panes = 1.0 if f["blobs"] >= 2 else 0.8
        if f["blobs"] <= 1 and hot > 0.25:
            panes = 0.4  # (one big bright shape: a sky or a silhouette)
        if f["pale_frac"] >= 0.6 and f["hot_val"] >= 0.8 and con >= 0.45:
            scores["windows"] = base * f["pale_frac"] * panes
            why.append("pale panes (%d)" % f["blobs"])
        elif f["blobs"] >= 6 and hot <= 0.7 and f["vivid_frac"] >= 0.3 and bgl < 0.25 and max(w, h) >= 1.3 * min(w, h):
            scores["windows"] = base * 0.8 * panes  # (lead lines between coloured panes, a tall window)
            why.append("stained glass? (%d panes)" % f["blobs"])
    if fixcat and not holder:  # the fixture's emitter (flame, panes, glow)
        scores[fixcat] = max(scores[fixcat], 0.3 + 0.7 * base)
        why.append("fixture " + fix)
    elif fixcat and base >= 0.3:  # a bright part of its holder (a lantern's frame, a brazier's bowl)
        scores[fixcat] = max(scores[fixcat], base)
        why.append("fixture " + fix)
    scores["other"] = base * (0.4 + 0.4 * halo) * 0.8
    cat = max(CATEGORIES, key=lambda k: (scores[k], -CATEGORIES.index(k)))
    if c["additive_share"] > 0.5:
        why.append("additive (already a game glow)")
    if c["animated"]:
        why.append("VRAM animation")
    why.append("flat" if flat > 0.5 else "wall" if wall > 0.5 else "mixed")
    if c.get("everywhere"):
        why.append("in most rooms (Ashley or her gear?)")
        scores[cat] *= 0.3
    if c.get("unmatched"):
        why.append("curated entry lacks %d of its boxes" % len(c["unmatched"]))
    return cat, round(float(scores[cat]), 3), why


def build(variants, zdata, cur, cur_entries, names, live=None):
    """Candidates with their features, category and score."""
    comps = group(variants, zdata)
    by_key = collections.defaultdict(list)
    for v in variants.values():
        by_key[v["key"]].append(v["id"])
    out = []
    for members in comps:
        vs = [variants[m] for m in members]
        raw, zone, box, tp, cl = composite(members, variants, zdata)
        rooms = sorted(set().union(*[v["rooms"] for v in vs]))
        area = sum(v["area"] for v in vs)
        c = {
            "members": sorted(members, key=lambda m: (-variants[m]["polygons"], m)),
            "rooms": [list(r) for r in rooms],
            "room_count": len(rooms),
            "zones": sorted({r[0] for r in rooms}),
            "polygons": sum(v["polygons"] for v in vs),
            "area": round(area, 1),
            "floor": sum(v["floor"] for v in vs), "wall": sum(v["wall"] for v in vs),
            "ceiling": sum(v["ceiling"] for v in vs),
            "additive_share": sum(v["additive"] for v in vs) / max(1, sum(v["polygons"] for v in vs)),
            "animated": any(v["animated"] for v in vs),
            "fixture": sum((v["fixture"] for v in vs), collections.Counter()),
            "image": {"zone": zone, "tpage": tp, "clut": cl, "box": box},
            "size": [box[2] - box[0] + 1, box[3] - box[1] + 1],
            "features": features(raw) if raw is not None else vs[0]["features"],
            "raw": raw,
            "source": "room",
        }
        c["curated"] = sorted({e for v in vs for e in curated_of(cur, v["key"], v["clut_key"])})
        # boxes of a curated image its entry does not list yet (another polygon's cut of it)
        c["unmatched"] = sorted(m for m in members if c["curated"] and not curated_of(cur, *m.split(":")))
        # the same keys with other palettes (colour variants), outside this candidate
        pal = set()
        for v in vs:
            for o in by_key[v["key"]]:
                if o not in members:
                    pal.add(o)
        c["palette_variants"] = sorted(pal)
        out.append(c)
    if live:
        out += live_candidates(live, cur, {v["key"] for v in variants.values()})
    for c in out:
        c["category"], c["score"], c["why"] = classify(c)
        c["id_hint"] = c["members"][0]
    frame_families(out)
    for c in out:
        # rank: score, more for more rooms (one entry covers them all)
        c["rank"] = round(c["score"] * (1 + 0.25 * np.log2(1 + c["room_count"])), 4)
    out = [c for c in out if c["category"]]
    out.sort(key=lambda c: (-bool(c["curated"]), -c["rank"]))
    # stable ids: the first member's key and palette
    for c in out:
        c["id"] = "G" + hashlib.md5(c["members"][0].encode()).hexdigest()[:6]
        c["fixture"] = dict(c["fixture"])
        c["room_names"] = [names.get("%d/%d" % tuple(r), "") for r in c["rooms"][:12]] if names else []
    return out


def frame_ring(raw, ring=3):
    """A square-ish tile's border (outer `ring` texels) as luminance, or None."""
    h, w = raw.shape
    if min(w, h) < 14 or max(w, h) > 2.2 * min(w, h):
        return None
    rgb = fl.rgb15(raw)
    lum = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]
    edge = np.concatenate([lum[:ring].ravel(), lum[-ring:].ravel(), lum[ring:-ring, :ring].ravel(),
                           lum[ring:-ring, -ring:].ravel()])
    return edge if edge.std() > 0.02 else None  # (a plain border says nothing)


def frame_families(cands, corr=0.9):
    """Tiles framed like a strong glyph (the floor glyphs' carved frame, in any
    palette: border luminance correlated) are glyph candidates too: other
    runes, other colours, weaker strokes."""
    rings = {}
    for k, c in enumerate(cands):
        if c["raw"] is not None and c["features"]:
            e = frame_ring(c["raw"])
            if e is not None:
                rings[k] = e
    leads = [k for k in rings if cands[k]["category"] == "glyphs" and cands[k]["score"] >= 0.7]
    for k, e in rings.items():
        c = cands[k]
        kin = [cands[j]["id_hint"] for j in leads if j != k and cands[j]["raw"].shape == c["raw"].shape
               and float(np.corrcoef(rings[j], e)[0, 1]) >= corr]
        if not kin:
            continue
        c["frame_family"] = len(kin)
        if c["category"] != "glyphs" and c["features"]["hot_frac"] >= 0.005:
            c["category"] = "glyphs"
            c["score"] = round(max(c["score"], 0.5), 3)
        c["why"] = c["why"] + ["framed like %d glyph tile(s)" % len(kin)]


def live_candidates(live, cur, known):
    """Textures --collect saw drawn that are not room geometry (objects, props,
    other room blocks): one candidate per key and palette. `known`: the
    room geometry's keys."""
    out = []
    nrooms = max(1, len(live.get("rooms", [])))
    for vid, u in live.get("uses", {}).items():
        if u["key"] in known:
            continue
        raw = np.array(u["raw"], np.uint16) if u.get("raw") else None
        rooms = sorted({tuple(r) for r in u["rooms"]})
        c = {
            "members": [vid], "rooms": [list(r) for r in rooms], "room_count": len(rooms),
            "zones": sorted({r[0] for r in rooms}), "polygons": u.get("refs", 0), "area": 0.0,
            "floor": 0.0, "wall": 0.0, "ceiling": 0.0, "additive_share": 0.0, "animated": False,
            "fixture": collections.Counter(), "image": {"zone": rooms[0][0], "tpage": u["tpage"],
                                                       "clut": u["clut"], "box": u["box"]},
            "size": u["size"], "features": features(raw) if raw is not None else None, "raw": raw,
            "source": "object", "palette_variants": [],
            # in most rooms: Ashley, her gear, the HUD's 3D parts
            "everywhere": len(rooms) >= 0.5 * nrooms and nrooms >= 8,
        }
        c["curated"] = curated_of(cur, u["key"], "") or ([u["entry"]] if u.get("entry") else [])
        c["area"] = 1.0
        out.append(c)
    return out


# ---- the live pass (objects) ----


def collect(args):
    """Drive the game into every room (RPC room_goto, enemies and scripts
    off) and note the textures drawn there (every native list: room, objects,
    room blocks) with their texels (RPC emissive uses / texture)."""
    import shutil
    import tempfile
    sys.path.insert(0, str(ROOT / "pc/tests"))
    from checkpoints import launch_at
    work = pathlib.Path(tempfile.mkdtemp(prefix="glow-scan-"))
    card = work / "card1.mcd"
    shutil.copyfile(args.card or ROOT / "pc/build/test-card1.mcd", card)
    env = {"ROOD_LIGHTS": str(work / "lights"), "ROOD_EMISSIVE_USER": str(work / "emissive-user.json"),
           "ROOD_GI": "0", "ROOD_OUT": str(work)}
    (work / "lights").mkdir()
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-glow-{os.getpid()}.sock"
    out = {"format": 1, "uses": {}, "rooms": [], "names": {}, "failed": []}
    seen = {}
    game = launch_at("save_room", card=card, fast=True, log=work / "game.log", extra_env=env, socket_path=socket)
    try:
        game.call("graphics", interpolate=False)
        targets = []
        for r in game.call("rooms").get("rooms", []):
            targets.append((r["zone"], r["room"]))
            out["names"]["%d/%d" % (r["zone"], r["room"])] = (r.get("area", "") + ": " + r.get("name", "")).strip(": ")
        if args.zone:
            targets = [t for t in targets if t[0] in args.zone]
        t0 = time.time()
        for n, (zone, room) in enumerate(targets):
            if not warp(game, zone, room):
                out["failed"].append([zone, room])
                continue
            out["rooms"].append([zone, room])
            for u in game.call("emissive", action="uses", keys=True, limit=8000).get("uses", []):
                # (a zone's palettes stay put: one texture read per key and CLUT in a zone)
                at = (zone, u["key"], u["tpage"], u["clut"])
                vid = seen.get(at)
                if vid is None:
                    tex = game.call("emissive", action="texture", tpage=u["tpage"], clut=u["clut"], box=u["box"])
                    pal = hashlib.md5(json.dumps(tex.get("palette", [])).encode()).hexdigest()[:10]
                    vid = seen[at] = u["key"] + ":live" + pal
                    raw = texture_raw(tex) if vid not in out["uses"] else None
                e = out["uses"].get(vid)
                if e is None:
                    e = out["uses"][vid] = {"key": u["key"], "tpage": u["tpage"], "clut": u["clut"], "box": u["box"],
                                            "size": [u["box"][2] - u["box"][0] + 1, u["box"][3] - u["box"][1] + 1],
                                            "rooms": [], "refs": 0, "raw": raw, "entry": u.get("entry", "")}
                if [zone, room] not in e["rooms"]:
                    e["rooms"].append([zone, room])
                e["refs"] += u.get("refs", 1)
            if n % 20 == 0:
                print(f"  {n + 1}/{len(targets)} rooms, {len(out['uses'])} textures, {time.time() - t0:.0f} s",
                      file=sys.stderr, flush=True)
    finally:
        game.close()
    pathlib.Path(args.collect).write_text(json.dumps(out))
    print(f"{len(out['rooms'])} rooms, {len(out['uses'])} textures, {len(out['failed'])} failed -> {args.collect}")


def warp(game, zone, room, tries=60):
    """emissive_smoke.py's goto: the game's own door transition into the room."""
    try:
        game.call("room_goto", zone=zone, room=room, enemies=False, scripts=False)
    except Exception:
        return False
    for _ in range(tries):
        game.run(20)
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
    game.run(40)
    state = game.state()
    return (state["zone"], state["room"]) == (zone, room)


def texture_raw(tex):
    """15-bit colours of a `texture` reply: its index rows through its palette
    ([r, g, b] 5-bit; 16-bit textures: the indices are the colours)."""
    rows, pal = tex.get("indices"), tex.get("palette")
    if not rows:
        return None
    a = np.array(rows, np.int64)
    if pal:
        p = np.array([r | g << 5 | b << 10 for r, g, b in pal], np.int64)
        a = p[np.clip(a, 0, len(p) - 1)]
    return a.tolist()


# ---- output ----


def font(size):
    from PIL import ImageFont
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    try:
        return ImageFont.load_default(size)
    except TypeError:
        return ImageFont.load_default()


def thumb(raw, cell):
    """The texture scaled up by an integer (nearest), on a checker for transparency."""
    from PIL import Image
    im = Image.fromarray(rgb_image(raw), "RGBA")
    k = max(1, min(cell // max(im.width, 1), cell // max(im.height, 1), 8))
    im = im.resize((im.width * k, im.height * k), Image.NEAREST)
    if im.width > cell or im.height > cell:
        s = min(cell / im.width, cell / im.height)
        im = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.NEAREST)
    bg = Image.new("RGBA", im.size, (40, 40, 40, 255))
    for y in range(0, im.height, 8):
        for x in range(0, im.width, 8):
            if (x // 8 + y // 8) % 2:
                bg.paste((56, 56, 56, 255), (x, y, min(x + 8, im.width), min(y + 8, im.height)))
    bg.alpha_composite(im)
    return bg


def contact_sheet(path, cands, title):
    from PIL import Image, ImageDraw
    cell, pad, cols, text_h = 128, 10, 8, 58
    rows = max(1, (len(cands) + cols - 1) // cols)
    w = cols * (cell + pad) + pad
    h = 40 + rows * (cell + text_h + pad) + pad
    sheet = Image.new("RGB", (w, h), (18, 18, 20))
    d = ImageDraw.Draw(sheet)
    f12, f11, f16 = font(12), font(11), font(16)
    d.text((pad, 10), title, fill=(230, 230, 230), font=f16)
    for k, c in enumerate(cands):
        x = pad + (k % cols) * (cell + pad)
        y = 40 + (k // cols) * (cell + text_h + pad)
        d.rectangle([x - 1, y - 1, x + cell, y + cell], outline=(70, 70, 70))
        if c["raw"] is not None:
            t = thumb(c["raw"], cell)
            sheet.paste(t.convert("RGB"), (x + (cell - t.width) // 2, y + (cell - t.height) // 2))
        if c["curated"]:
            d.rectangle([x - 2, y - 2, x + cell + 1, y + cell + 1], outline=(80, 220, 120), width=2)
            d.text((x + 3, y + 2), ",".join(c["curated"]), fill=(80, 255, 140), font=f12)
        g = c["features"]["glow_rgb"] if c["features"] else [0, 0, 0]
        sw = tuple(int(255 * v) for v in g)
        d.rectangle([x, y + cell + 4, x + 14, y + cell + 18], fill=sw, outline=(200, 200, 200))
        d.text((x + 18, y + cell + 4), f"{k + 1}. {c['id']}", fill=(235, 235, 235), font=f12)
        d.text((x, y + cell + 21), f"{c['room_count']} rooms  {c['score']:.2f}", fill=(180, 180, 180), font=f11)
        key = c["members"][0].split(":")[0]
        d.text((x, y + cell + 36), f"{key} {c['size'][0]}x{c['size'][1]}", fill=(140, 140, 150), font=f11)
    sheet.save(path)


def to_json(c):
    return {k: v for k, v in c.items() if k != "raw"}


def write(out_dir, cands, variants, rooms_seen, top, names):
    from PIL import Image
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "crops").mkdir(exist_ok=True)
    for old in (out_dir / "crops").glob("G*.png"):
        old.unlink()
    shown = {k: [] for k in CATEGORIES}
    for c in cands:
        if c["curated"] or c["score"] >= 0.15:
            shown[c["category"]].append(c)
    for c in cands:
        if c["raw"] is not None and (c["curated"] or c in shown[c["category"]][:top]):
            Image.fromarray(rgb_image(c["raw"]), "RGBA").save(out_dir / "crops" / f"{c['id']}.png")
    sizes = {v["id"]: v["size"] for v in variants.values()}
    data = []
    for c in cands:
        j = to_json(c)
        j["entry_match"] = [{"key": m.split(":")[0], "size": sizes.get(m, c["size"]),
                             **({"clut": m.split(":")[1]} if m.split(":")[1] not in ("", "direct") else {})}
                            for m in c["members"]]
        data.append(j)
    summary = {
        "about": "Glow sweep candidates (pc/tools/glow_scan.py). Game-derived: local only, never commit.",
        "rooms_scanned": len(rooms_seen),
        "candidates": len(cands),
        "per_category": {k: len(v) for k, v in shown.items()},
        "strong_per_category": {k: sum(1 for c in v if c["score"] >= STRONG) for k, v in shown.items()},
        "per_category_all": dict(collections.Counter(c["category"] for c in cands)),
        "candidates_list": data,
    }
    (out_dir / "candidates.json").write_text(json.dumps(summary, indent=1, default=lambda o: o.tolist()
                                                        if hasattr(o, "tolist") else str(o)))
    for k in CATEGORIES:
        lst = [c for c in shown[k]]
        lst.sort(key=lambda c: (-bool(c["curated"]), -c["rank"]))
        contact_sheet(out_dir / f"sheet_{k}.png", lst[:top],
                      f"{k}: {len(shown[k])} candidates (top {min(top, len(lst))}; green = curated)")
    write_html(out_dir, shown, top, len(rooms_seen), names)
    return summary


def write_html(out_dir, shown, top, nrooms, names):
    e = html.escape
    rows = []
    for k in CATEGORIES:
        lst = sorted(shown[k], key=lambda c: (-bool(c["curated"]), -c["rank"]))[:top]
        rows.append(f"<h2 id='{k}'>{e(k)} <small>{len(shown[k])} candidates, top {len(lst)}</small></h2>")
        rows.append(f"<p><a href='sheet_{k}.png'>contact sheet</a></p><table><tr><th>#</th><th>crop</th>"
                    "<th>id</th><th>score</th><th>rooms</th><th>keys (palette)</th><th>why</th><th>curated</th></tr>")
        for i, c in enumerate(lst):
            g = c["features"]["glow_rgb"] if c["features"] else [0, 0, 0]
            sw = "rgb(%d,%d,%d)" % tuple(int(255 * v) for v in g)
            rl = ", ".join("%d/%d" % tuple(r) for r in c["rooms"][:10]) + (" ..." if len(c["rooms"]) > 10 else "")
            nm = [names.get("%d/%d" % tuple(r)) for r in c["rooms"][:3]] if names else []
            nm = "; ".join(n for n in nm if n)
            keys = "<br>".join(e(m) for m in c["members"][:6]) + ("<br>..." if len(c["members"]) > 6 else "")
            img = f"<img src='crops/{c['id']}.png'>" if (out_dir / "crops" / f"{c['id']}.png").exists() else ""
            im = c["image"]
            rows.append(
                f"<tr class='{'cur' if c['curated'] else ''}'><td>{i + 1}</td><td class='im'>{img}</td>"
                f"<td><b>{c['id']}</b><br><span class='sw' style='background:{sw}'></span> "
                f"{c['size'][0]}x{c['size'][1]}<br>page {im['tpage']} clut {im['clut']}<br>{e(c['source'])}</td>"
                f"<td>{c['score']:.2f}</td><td>{c['room_count']}: {e(rl)}<br><i>{e(nm)}</i></td>"
                f"<td class='k'>{keys}<br>+{len(c['palette_variants'])} palette variants</td>"
                f"<td>{e(', '.join(c['why']))}</td><td>{e(', '.join(c['curated']))}</td></tr>")
        rows.append("</table>")
    counts = " · ".join(f"<a href='#{k}'>{k} {len(shown[k])}</a>" for k in CATEGORIES)
    page = f"""<!doctype html><html><head><meta charset="utf-8"><title>Glow sweep candidates</title>
<style>body{{font:13px system-ui,sans-serif;background:#141416;color:#ddd;margin:16px}}
table{{border-collapse:collapse;width:100%}}td,th{{border-bottom:1px solid #333;padding:4px 6px;vertical-align:top;text-align:left}}
td.im img{{image-rendering:pixelated;width:96px}}tr.cur{{background:#183020}}.k{{font-family:monospace;font-size:11px}}
.sw{{display:inline-block;width:12px;height:12px;border:1px solid #aaa;vertical-align:middle}}a{{color:#8cf}}
small{{color:#999;font-weight:normal}}</style></head><body>
<h1>Glow sweep candidates</h1><p>{nrooms} rooms scanned (room geometry from the disc). {counts}.
Green rows are already curated. Game imagery: local only.</p>
{''.join(rows)}</body></html>"""
    (out_dir / "index.html").write_text(page)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--zone", type=int, action="append", help="only these zones (default: all)")
    ap.add_argument("--out", default=str(ROOT / "pc/exports/glow_scan"), help="output directory (gitignored)")
    ap.add_argument("--top", type=int, default=64, help="candidates per contact sheet")
    ap.add_argument("--names", help="room names: a saved RPC `rooms` reply (or --collect's output)")
    ap.add_argument("--live", help="merge object textures from --collect's output")
    ap.add_argument("--collect", help="drive the game into every room and write object texture uses here")
    ap.add_argument("--card", help="memory card for --collect (default: rood_rpc's)")
    args = ap.parse_args(argv)
    out_dir = pathlib.Path(args.out).resolve()
    if ROOT / "pc/assets" in out_dir.parents or out_dir == ROOT / "pc/assets":
        sys.exit("glow_scan: the output is game imagery; not under pc/assets")
    if args.collect:
        collect(args)
        return 0
    t0 = time.time()
    disc = fl.Disc()
    zones = args.zone or disc.zones()
    variants, zdata, rooms_seen = scan(disc, zones, log=lambda s: print(s, file=sys.stderr))
    cur, cur_entries = load_emissive()
    names, live = {}, None
    if args.names:
        j = json.loads(pathlib.Path(args.names).read_text())
        names = j.get("names") or {"%d/%d" % (z["zone"], r["room"]): r.get("name", "")
                                    for z in j.get("zones", []) for r in z.get("rooms", [])}
    if args.live:
        live = json.loads(pathlib.Path(args.live).read_text())
        names = names or live.get("names", {})
    cands = build(variants, zdata, cur, cur_entries, names, live)
    s = write(out_dir, cands, variants, rooms_seen, args.top, names)
    print(f"{s['rooms_scanned']} rooms, {len(variants)} textures, {s['candidates']} candidate images "
          f"in {time.time() - t0:.0f} s")
    for k in CATEGORIES:
        print(f"  {k:8s} {s['per_category'][k]:4d} listed, {s['strong_per_category'][k]:3d} strong")
    found = sorted({e for c in cands for e in c["curated"]})
    print("  curated found:", ", ".join(found) or "none",
          "| not in room geometry:", ", ".join(sorted({e["id"] for e in cur_entries} - set(found))) or "none")
    print("->", out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
