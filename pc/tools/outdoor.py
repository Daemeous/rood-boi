#!/usr/bin/env python3
"""Rooms open to the sky, and their default sun and sky (the generated
layer's `env sun` / `env sky` items; lighting/README.md "Environment").

    python3 pc/tools/outdoor.py                 every room: its measures and verdict (a table)
    python3 pc/tools/outdoor.py --zone 32       one zone
    python3 pc/tools/outdoor.py --summary       outdoor rooms per area

Detection, from the room's own geometry and baked light (the user's disc):

- floor samples on every upward-facing surface the game draws (front faces
  whose winding normal points down the +y axis: the room is NCLIP-culled, so
  a floor's front face is the one seen from above), from each a fan of 8
  rays within 35 degrees of straight up, tested against all of the room's
  triangles: `open`, the area-weighted share that escapes;
- `ceiling`: the area of ceilings (front faces seen from below) over the
  floor's area;
- `light`: the luminance of the baked vertex colours on the floor that sees
  the sky.

Vagrant Story's rooms model a ceiling only where a camera can see it, so
openness alone does not tell a street from a cellar (the Wine Cellar's
rooms are 17-47 % open). Measured over the whole game, roofed rooms have
ceilings of 0.37-2.3 x their floor area and black floor vertex colours
(their light comes from the room's lighting section, not the vertices),
while streets, courtyards, the forest and the cathedral's roofs have
ceilings under 0.3 x the floor, open floors over 40 % and lit vertices. A
room is outdoor when all three hold:

    open >= 0.4 and ceiling < 0.3 and light >= 0.05

The rest is an explicit review list, pc/assets/environment/outdoor.json:
whole zones or single rooms decided by a person, with the reason (the
Undercity's vaults are unmodelled, so the measures call it outdoor). It
applies to every room it names at once, like the fixture catalog.

The defaults lean moody (Lea Monde is mostly seen at dusk, by night or
under cloud; moody survival horror rather than daylight): a low, cool, soft moon
and a dim overcast night sky, both leaning toward the hue of the room's
own floor vertex colours (the time of day the game lit it for).
Deterministic: the same disc gives the same numbers and files.
"""
import argparse
import json
import math
import os
import pathlib
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixture_lib as fl  # noqa: E402

REVIEW = fl.ROOT / "pc/assets/environment/outdoor.json"
OPEN_MIN = 0.4  # share of upward floor rays that escape
CEILING_MAX = 0.3  # ceiling area / floor area
LIGHT_MIN = 0.05  # luminance of the open floor's vertex colours (0..1)
RAY_CONE = math.radians(35.0)
RAYS_PER_SAMPLE = 8
SAMPLE_SPACING = 96.0  # world units between floor samples (per triangle, at least one)


def triangles(r):
    """The room's triangles as the game captures them: (T, 3, 3)."""
    tris = []
    for p in r["polys"]:
        if not fl.captured(p):
            continue
        v = p["V"]
        tris.append(v[[0, 1, 2]])
        if p["quad"]:
            tris.append(v[[1, 3, 2]])
    return np.array(tris, float) if tris else np.zeros((0, 3, 3))


def any_hit(origins, dirs, tris, t_max=1e6, chunk=128):
    """Whether each ray o + d t (0 < t < t_max) hits any triangle."""
    hit = np.zeros(len(origins), bool)
    if not len(tris):
        return hit
    a, b, c = tris[:, 0], tris[:, 1], tris[:, 2]
    e1, e2 = b - a, c - a
    for s in range(0, len(origins), chunk):
        o = origins[s:s + chunk, None, :]
        d = dirs[s:s + chunk, None, :]
        p = np.cross(d, e2[None])
        det = np.einsum("rtk,tk->rt", p, e1)
        ok = np.abs(det) > 1e-9
        f = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        sv = o - a[None]
        u = f * np.einsum("rtk,rtk->rt", sv, p)
        q = np.cross(sv, e1[None])
        v = f * np.einsum("rtk,rtk->rt", np.broadcast_to(d, q.shape), q)
        t = f * np.einsum("tk,rtk->rt", e2, q)
        m = ok & (u >= 0) & (u <= 1) & (v >= 0) & (u + v <= 1) & (t > 1e-3) & (t < t_max)
        hit[s:s + chunk] = m.any(1)
    return hit


def ray_fan(n):
    """n directions within RAY_CONE of straight up (-y), deterministic
    (golden-angle spiral), straight up first."""
    out = [(0.0, -1.0, 0.0)]
    for k in range(1, n):
        f = k / (n - 1)
        theta = RAY_CONE * math.sqrt(f)
        phi = k * 2.39996323
        out.append((math.sin(theta) * math.cos(phi), -math.cos(theta), math.sin(theta) * math.sin(phi)))
    return np.array(out, float)


def surfaces(r):
    """Floor samples (positions, area weights, vertex colours 0..1) and the
    ceiling area."""
    pts, areas, cols, ceiling = [], [], [], 0.0
    for p in r["polys"]:
        if not fl.captured(p):
            continue
        v = p["V"]
        for (i, j, k) in ([(0, 1, 2), (1, 3, 2)] if p["quad"] else [(0, 1, 2)]):
            n = np.cross(v[j] - v[i], v[k] - v[i])
            area = 0.5 * np.linalg.norm(n)
            if area < 1.0:
                continue
            ny = n[1] / (2 * area)
            if ny < -0.7:  # a ceiling seen from below
                ceiling += area
            if ny < 0.7:  # not a floor seen from above
                continue
            steps = max(1, int(math.ceil(math.sqrt(area) / SAMPLE_SPACING)))
            for a in range(steps):
                for b in range(steps - a):
                    wa, wb = (a + 1 / 3) / steps, (b + 1 / 3) / steps
                    wc = 1 - wa - wb
                    pts.append(v[i] * wc + v[j] * wa + v[k] * wb)
                    areas.append(area / (steps * (steps + 1) / 2))
                    rgb = p["rgb"]
                    cols.append((rgb[i] * wc + rgb[j] * wa + rgb[k] * wb) / 255.0)
    if not pts:
        return np.zeros((0, 3)), np.zeros(0), np.zeros((0, 3)), ceiling
    return np.array(pts, float), np.array(areas, float), np.array(cols, float), ceiling


def measure(r):
    """{'open', 'ceiling', 'light', 'tint' (mean vertex colour 0..1 of the
    open floor), 'floor_area'} of a room (fixture_lib room dict)."""
    tris = triangles(r)
    pts, areas, cols, ceiling = surfaces(r)
    floor = float(areas.sum())
    out = {"open": 0.0, "ceiling": round(ceiling / floor, 3) if floor else 0.0, "light": 0.0,
           "tint": [0.5, 0.5, 0.5], "floor_area": round(floor)}
    if not len(pts):
        return out
    fan = ray_fan(RAYS_PER_SAMPLE)
    # Per sample, only the triangles its cone of rays can reach: above it
    # (y down) and within the cone's radius at their height (xz bounds).
    lo, hi = tris.min(1), tris.max(1)
    spread = math.tan(RAY_CONE) * 1.01
    per_sample = np.zeros(len(pts))
    for i, q in enumerate(pts + np.array([0.0, -4.0, 0.0])):
        height = q[1] - lo[:, 1]
        dx = np.maximum(np.maximum(lo[:, 0] - q[0], q[0] - hi[:, 0]), 0.0)
        dz = np.maximum(np.maximum(lo[:, 2] - q[2], q[2] - hi[:, 2]), 0.0)
        near = (height > 0) & (np.hypot(dx, dz) <= height * spread + 1.0)
        hit = any_hit(np.repeat(q[None], len(fan), 0), fan, tris[near])
        per_sample[i] = 1.0 - hit.mean()
    w = per_sample * areas
    out["open"] = round(float(w.sum() / floor), 3)
    if w.sum() > 0:
        tint = (cols * w[:, None]).sum(0) / w.sum()
        out["tint"] = [round(float(x), 3) for x in tint]
        out["light"] = round(float(0.299 * tint[0] + 0.587 * tint[1] + 0.114 * tint[2]), 3)
    return out


def measured_outdoor(m):
    return m["open"] >= OPEN_MIN and m["ceiling"] < CEILING_MAX and m["light"] >= LIGHT_MIN


def load_review(path=None):
    path = pathlib.Path(path or REVIEW)
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def decide(m, zone, room, review):
    """(outdoor, why): the review list's decision for the room or its zone,
    else the measures'."""
    if zone >= 100:  # zone files the game's location table never names (no lighting files)
        return False, "not a zone of the game's locations"
    for entry in review.get("rooms", []):
        if entry.get("zone") == zone and entry.get("room") == room:
            return bool(entry["outdoor"]), "review: " + entry.get("why", "")
    for entry in review.get("zones", []):
        if entry.get("zone") == zone:
            return bool(entry["outdoor"]), "review: " + entry.get("why", "")
    return measured_outdoor(m), "measured"


def _lean(tint, amount):
    """Per-channel factors (mean 1) leaning `amount` toward the tint's hue."""
    t = np.maximum(np.array(tint, float), 1e-3)
    hue = t / t.mean()
    hue = np.clip(hue, 0.7, 1.4)
    return (1 - amount) + amount * hue


def environment(m, zone):
    """The default moon and sky for an outdoor room: (sun fields, sky
    fields), lists of (key, value) strings for `env sun` / `env sky`."""
    lean = _lean(m["tint"], 0.35)

    def col(rgb):
        c = np.clip(np.array(rgb, float) * lean, 0, 8)
        return ",".join(_num(x, 3) for x in c)

    # One moon per zone (the streets of a zone agree), turned by zone so the
    # town and the forest are lit from different sides.
    azimuth = (205.0 + 37.0 * (zone % 5)) % 360.0
    sun = [("azimuth", _num(azimuth, 2)), ("elevation", "35"), ("color", col([0.55, 0.65, 0.95])),
           ("intensity", "0.7"), ("angle", "2")]
    sky = [("zenith", col([0.07, 0.09, 0.16])), ("horizon", col([0.17, 0.18, 0.23])),
           ("ground", col([0.03, 0.03, 0.035])), ("sun_tint", "0.25"), ("ambient", "0.6")]
    return sun, sky


ATMOSPHERE = fl.ROOT / "pc/assets/environment/atmosphere.json"


def load_atmosphere(path=None):
    try:
        return json.loads(pathlib.Path(path or ATMOSPHERE).read_text())
    except (OSError, ValueError):
        return {}


def atmosphere(moods, zone, is_outdoor):
    """The room's haze and ground fog from its area's mood
    (pc/assets/environment/atmosphere.json): (set items [(key, value)],
    fog fields [(key, value)] or None). Roofed rooms of unlisted zones: none."""
    mood = None
    for entry in moods.get("zones", []):
        if zone in entry["zones"] and (entry.get("rooms") == "all" or is_outdoor):
            mood = entry["mood"]
            break
    if mood is None and is_outdoor:
        mood = moods.get("outdoor_default")
    m = moods.get("moods", {}).get(mood) if mood else None
    if not m:
        return [], None
    sets = [(f"haze_{k}", _num(v, 6)) for k, v in sorted(m.get("haze", {}).items())]
    fog = [(k, v if isinstance(v, str) else _num(v, 6)) for k, v in sorted(m["fog"].items())] if "fog" in m else None
    return sets, fog


def _num(v, decimals):
    s = "%.*f" % (decimals, v)
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--zone", type=int, action="append")
    ap.add_argument("--room", type=int, action="append")
    ap.add_argument("--summary", action="store_true", help="count outdoor rooms per zone")
    args = ap.parse_args()
    disc = fl.Disc()
    review = load_review()
    total = outdoor_n = 0
    for zone in args.zone or disc.zones():
        try:
            _, rooms, _ = fl.zone_rooms(disc, zone)
        except (OSError, KeyError, ValueError, IndexError):
            continue
        zone_out = []
        for room, name in enumerate(rooms):
            if not name or (args.room and room not in args.room):
                continue
            mpd = fl.load_mpd(disc, name)
            m = measure({"polys": [p for p in mpd["polys"] if fl.visible(p)]})
            out, why = decide(m, zone, room, review)
            total += 1
            outdoor_n += out
            zone_out.append(out)
            if not args.summary:
                print(f"zone {zone:3d} room {room:2d}  open {m['open']:.2f}  ceiling {m['ceiling']:.2f}  "
                      f"light {m['light']:.2f}  {'OUTDOOR' if out else 'roofed '}  ({why})")
        if args.summary and zone_out:
            print(f"zone {zone:3d}: {sum(zone_out)}/{len(zone_out)} outdoor")
    print(f"{outdoor_n} of {total} rooms outdoor")
    return 0


if __name__ == "__main__":
    sys.exit(main())
