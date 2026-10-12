#!/usr/bin/env python3
"""Room fixtures read from the user's own disc: the shared code of
fixture_scan.py (builds and checks the fixture catalog) and
generate_lighting.py (lights every catalogued fixture of a room).

Nothing here is game data: rooms, textures and polygons are read from the
disc image at run time (ROOD_DISC, default disks/SLUS-01040.bin); the
catalog (pc/assets/fixtures/catalog.json, pc/docs/Fixture_Catalog.md) holds
only hashes and numbers.

Pipeline (one room):
  load_room()     the room's polygons in world space, as the game captures
                  them (pc/src/scene_capture.c: groups with flags 0x500 are
                  left out), each with its texture's content key (ikey: a
                  hash of the texel indices under its UV box, from the zone's
                  textures as loaded), additive flag, VRAM animation, room
                  texture effect and warm "flame" texels;
  clusters()      seeds (additive, animated, flame texels, UV effect) merged
                  into emitters, plus the small connected meshes touching
                  them (the holder): one cluster per fixture;
  describe()      its frame (origin: bottom centre of the body's bounds, +y
                  down; yaw from the wall it hangs on, else its principal
                  axis), flames (flame sprites: additive polygons with a
                  flame texture, 16 units apart), extents, signatures;
  glow_groups()   the room's glows exactly as pc/src/glows.c groups them
                  (their keys "room:<tag>@x,y,z"), so generated actors link
                  the glow the game will find;
  classify()      the scan's heuristic class of a cluster (only used for
                  clusters no catalog type matches: new types to review).
"""
import collections
import hashlib
import math
import os
import pathlib
import struct

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
CATALOG = ROOT / "pc/assets/fixtures/catalog.json"

# ---- the disc ----


class Disc:
    """Files of the game's disc image (ISO 9660, 2352-byte sectors)."""

    def __init__(self, path=None):
        path = path or os.environ.get("ROOD_DISC") or str(ROOT / "disks/SLUS-01040.bin")
        self.path = path
        self.f = open(path, "rb")
        self._dirs = {}
        pvd = self.sector(16)
        if pvd[1:6] != b"CD001":
            raise OSError(f"{path}: not an ISO 9660 image")
        root = pvd[156:156 + 34]
        self.root = (struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0])
        self._by_lba = None

    def sector(self, n):
        self.f.seek(n * 2352)
        s = self.f.read(2352)
        if len(s) != 2352:
            raise OSError(f"{self.path}: sector {n} out of range")
        return s[24:24 + 2048] if s[15] == 2 else s[16:16 + 2048]

    def entries(self, lba, size):
        out = []
        for s in range((size + 2047) // 2048):
            d, p = self.sector(lba + s), 0
            while p < 2048 and d[p]:
                ln = d[p]
                elba, esz = struct.unpack("<I", d[p + 2:p + 6])[0], struct.unpack("<I", d[p + 10:p + 14])[0]
                nl = d[p + 32]
                name = d[p + 33:p + 33 + nl].decode("latin1").split(";")[0]
                out.append((name, elba, esz, d[p + 25]))
                p += ln
        return out

    def directory(self, path):
        if path not in self._dirs:
            lba, size = self.root
            for part in [p for p in path.split("/") if p]:
                hit = [e for e in self.entries(lba, size) if e[0] == part]
                if not hit:
                    raise FileNotFoundError(path)
                lba, size = hit[0][1], hit[0][2]
            self._dirs[path] = {e[0]: (e[1], e[2]) for e in self.entries(lba, size) if e[0] not in ("\x00", "\x01")}
        return self._dirs[path]

    def read_at(self, lba, size):
        out = bytearray()
        for s in range((size + 2047) // 2048):
            out += self.sector(lba + s)
        return bytes(out[:size])

    def read(self, path):
        d, _, name = path.rpartition("/")
        lba, size = self.directory(d)[name]
        return self.read_at(lba, size)

    def map_name(self, lba):
        if self._by_lba is None:
            self._by_lba = {v[0]: k for k, v in self.directory("MAP").items()}
        return self._by_lba.get(lba)

    def zones(self):
        return sorted(int(n[4:7]) for n in self.directory("MAP") if n.startswith("ZONE") and n.endswith(".ZND"))


# ---- zones and rooms ----


def zone_rooms(disc, zone):
    """The zone file and its rooms' MPD file names (index = room number)."""
    d = disc.read("MAP/ZONE%03d.ZND" % zone)
    mo, ml = struct.unpack("<2I", d[:8])
    to = struct.unpack("<I", d[16:20])[0]
    rooms = []
    for i in range(ml // 8):
        lba, _ = struct.unpack("<2I", d[mo + i * 8:mo + i * 8 + 8])
        rooms.append(disc.map_name(lba))
    return d, rooms, to


def zone_vram(disc, zone):
    """VRAM (512 x 1024 halfwords) as the zone's textures load it."""
    d, rooms, to = zone_rooms(disc, zone)
    vram = np.zeros((512, 1024), np.uint16)
    t = d[to:]
    p = 16
    n = struct.unpack("<I", t[p:p + 4])[0]
    p += 4
    for _ in range(n):
        sz = struct.unpack("<I", t[p:p + 4])[0]
        p += 4
        tim = t[p:p + sz]
        mode = struct.unpack("<I", tim[4:8])[0]
        q = 8
        if mode & 8:
            q += struct.unpack("<I", tim[q:q + 4])[0]
        _, x, y, w, h = struct.unpack("<I4H", tim[q:q + 12])
        pix = np.frombuffer(tim[q + 12:q + 12 + w * h * 2], np.uint16).reshape(h, w)
        if x >= 768:
            for r in range(h):
                if 0 <= y + r - 224 < 3:
                    vram[y + r, x:x + w] = pix[r]
        else:
            vram[y + 256:y + 256 + h, x - 320:x - 320 + w] = pix
        p += sz
    return vram, rooms


def texel(vram, u, v, tpage, clut):
    tx = (tpage & 15) * 64
    ty = ((tpage >> 4) & 1) * 256
    depth = (tpage >> 7) & 3
    cx = (clut & 63) * 16
    cy = (clut >> 6) & 511
    u = np.asarray(u) & 255
    v = np.asarray(v) & 255
    if depth == 0:
        w = vram[(ty + v) & 511, (tx + (u >> 2)) & 1023]
        idx = (w >> ((u & 3) * 4)) & 15
        return vram[cy, (cx + idx) & 1023], idx
    if depth == 1:
        w = vram[(ty + v) & 511, (tx + (u >> 1)) & 1023]
        idx = (w >> ((u & 1) * 8)) & 255
        return vram[cy, (cx + idx) & 1023], idx
    t = vram[(ty + v) & 511, (tx + u) & 1023]
    return t, t


def rgb15(t):
    t = np.asarray(t).astype(np.int32)
    return np.stack([(t & 31), (t >> 5) & 31, (t >> 10) & 31], -1) / 31.0


def rot_yxz(ax, ay, az):
    a = [x * 2 * math.pi / 4096 for x in (ax, ay, az)]
    sx, cx = math.sin(a[0]), math.cos(a[0])
    sy, cy = math.sin(a[1]), math.cos(a[1])
    sz, cz = math.sin(a[2]), math.cos(a[2])
    rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return ry @ rx @ rz


EFFECT_SIZES = {0: 24, 1: 20, 2: 32, 6: 36}


def load_mpd(disc, name):
    """Room geometry: polygons (world space) and texture effects."""
    d = disc.read("MAP/" + name)
    rs, rl = struct.unpack("<2I", d[:8])
    room = {"name": name, "groups": [], "polys": [], "effects": [], "vram_anims": []}
    if rl < 0x60:
        return room
    lens = struct.unpack("<24I", d[rs:rs + 96])
    offs, o = [], rs + 96
    for ln in lens:
        offs.append(o)
        o += ln
    g = offs[0]
    if lens[0] == 0:
        return room
    n = struct.unpack("<I", d[g:g + 4])[0]
    polys = []
    for gi in range(n):
        w = struct.unpack("<16i", d[g + 4 + gi * 64:g + 4 + gi * 64 + 64])
        flags = w[0] & 0xFFFFFFFF
        pos, ang, off = w[1:4], w[4:7], w[7]
        room["groups"].append({"flags": flags, "pos": pos, "ang": ang})
        shift = 0 if flags & 0x800 else 3
        rot = rot_yxz(*ang) if any(ang) else None
        tris, quads = struct.unpack("<2I", d[g + off:g + off + 8])
        p = g + off + 8
        for k in range(tris + quads):
            q = k >= tris
            pw = struct.unpack("<%dI" % (10 if q else 8), d[p:p + (40 if q else 32)])
            x, y, z = struct.unpack("<3h", d[p:p + 6])
            dl = struct.unpack("<6b", d[p + 6:p + 12])
            verts = [(x, y, z), (x + (dl[0] << shift), y + (dl[1] << shift), z + (dl[2] << shift)),
                     (x + (dl[3] << shift), y + (dl[4] << shift), z + (dl[5] << shift))]
            if q:
                d3 = struct.unpack("<3b", d[p + 32:p + 35])
                verts.append((x + (d3[0] << shift), y + (d3[1] << shift), z + (d3[2] << shift)))
            verts = [tuple(((c + 32768) & 65535) - 32768 for c in v) for v in verts]
            vv = np.array(verts, float)
            if rot is not None:
                vv = vv @ rot.T
            vv = vv + np.array(pos, float)
            cols = [pw[3], pw[4], pw[5]] + ([pw[9]] if q else [])
            uvs = [pw[6] & 0xFFFF, pw[7] & 0xFFFF, (pw[4] >> 24) | ((pw[5] >> 24) << 8)] + (
                [(pw[8] >> 24) | ((pw[9] >> 24) << 8)] if q else [])
            polys.append({"g": gi, "off": p - g, "quad": q, "V": vv, "flags": flags,
                          "rgb": np.array([[c & 255, (c >> 8) & 255, (c >> 16) & 255] for c in cols], float),
                          "uv": np.array([[u & 255, u >> 8] for u in uvs], int),
                          "tpage": ((pw[7] >> 16) + 11) & 0xFFFF, "clut": pw[6] >> 16, "code": pw[3] >> 24})
            p += 40 if q else 32
    room["polys"] = polys
    e, ln, at = offs[11], lens[11], 0
    while at < ln:
        t = d[e + at + 6]
        if t not in EFFECT_SIZES:
            break
        raw = d[e + at:e + at + EFFECT_SIZES[t]]
        if t == 1:
            room["vram_anims"].append(raw)
        else:
            room["effects"].append({"type": t, "polygon": struct.unpack("<I", raw[:4])[0]})
        at += EFFECT_SIZES[t]
    return room


# ---- per-polygon features ----

STEPS = 4
BARY = np.array([(a / STEPS, b / STEPS, 1 - (a + b) / STEPS) for a in range(STEPS + 1) for b in range(STEPS + 1 - a)])


def flame_weight(c):
    lum = 0.299 * c[..., 0] + 0.587 * c[..., 1] + 0.114 * c[..., 2]
    ok = (lum >= 0.6) & (c[..., 0] >= 0.8) & (c[..., 0] - c[..., 2] >= 0.25) & (c[..., 1] >= 0.4)
    return np.where(ok, (lum - 0.62) ** 2 * 10, 0.0)


def footprint(p):
    tx = (p["tpage"] & 15) * 64
    ty = ((p["tpage"] >> 4) & 1) * 256
    depth = (p["tpage"] >> 7) & 3
    div = 4 if depth == 0 else 2 if depth == 1 else 1
    us, vv = p["uv"][:, 0], p["uv"][:, 1]
    return tx + us.min() // div, tx + us.max() // div, ty + vv.min(), ty + vv.max()


def visible(p):
    """Drawn by the game (groups with flag 0x100 are disabled)."""
    return not (p["flags"] & 0x100)


def captured(p):
    """In the game's captured scene, where its room glows come from
    (scene_capture.c also skips camera-facing planes, flag 0x400: the
    candle and torch flame sprites, which therefore have no glow)."""
    return not (p["flags"] & 0x500)


def load_room(disc, zone, room, vram, mpd_name):
    """A room with per-polygon features (only polygons the game captures)."""
    m = load_mpd(disc, mpd_name)
    anims = []
    for raw in m["vram_anims"]:
        x, y, w, h = struct.unpack("<4H", raw[12:20])
        anims.append((x - 320, y + 256, w, h))
    eff = {e["polygon"]: e["type"] for e in m["effects"] if e["type"] in (0, 2)}
    swing = [e["polygon"] for e in m["effects"] if e["type"] == 6]
    out = []
    for p in m["polys"]:
        if not visible(p):
            continue
        vv = p["V"]
        tris = [(0, 1, 2), (1, 3, 2)] if p["quad"] else [(0, 1, 2)]
        area, nrm, mods, poss, raws = 0.0, np.zeros(3), [], [], []
        for (a, b, c) in tris:
            cr = np.cross(vv[b] - vv[a], vv[c] - vv[a])
            area += 0.5 * np.linalg.norm(cr)
            nrm += cr
            uv = p["uv"][[a, b, c]].astype(float)
            col = p["rgb"][[a, b, c]]
            u, v = BARY @ uv[:, 0], BARY @ uv[:, 1]
            t, _ = texel(vram, (u + 0.5).astype(int), (v + 0.5).astype(int), p["tpage"], p["clut"])
            ok = t != 0
            cc = rgb15(t)
            vc = (BARY @ col) / 255.0
            mods.append(np.clip(cc * vc * 2, 0, 1)[ok])
            poss.append((BARY @ vv[[a, b, c]])[ok])
            raws.append(cc[ok])
        mods = np.concatenate(mods)
        poss = np.concatenate(poss)
        fw = flame_weight(mods) if len(mods) else np.zeros(0)
        semi = bool(p["code"] & 2)
        abr = (p["tpage"] >> 5) & 3
        fx0, fx1, fy0, fy1 = footprint(p)
        anim = [k for k, (x, y, w, h) in enumerate(anims) if fx1 >= x and fx0 < x + w and fy1 >= y and fy0 < y + h]
        us, vs_ = p["uv"][:, 0], p["uv"][:, 1]
        gu, gv = np.meshgrid(np.arange(us.min(), us.max() + 1), np.arange(vs_.min(), vs_.max() + 1))
        tt, ii = texel(vram, gu, gv, p["tpage"], p["clut"])
        ikey = hashlib.md5(np.ascontiguousarray(ii.astype(np.uint16)).tobytes()
                           + bytes([us.max() - us.min(), vs_.max() - vs_.min()])).hexdigest()[:10]
        texmean = rgb15(tt[tt != 0]).mean(0) if (tt != 0).any() else np.zeros(3)
        p.update(area=area, normal=nrm / (np.linalg.norm(nrm) + 1e-9), semi=semi, abr=abr,
                 additive=semi and abr in (1, 3), flame=float(fw.sum()),
                 flame_pos=(poss[fw > 0] * fw[fw > 0, None]).sum(0) / fw.sum() if fw.sum() > 0 else None,
                 anim=anim, uveff=eff.get(p["off"]), ikey=ikey, texmean=texmean)
        out.append(p)
    return {"zone": zone, "room": room, "mpd": mpd_name, "groups": m["groups"], "polys": out, "swing": swing}


# ---- clusters (one per fixture) ----


class UF:
    def __init__(self, n):
        self.p = list(range(n))

    def f(self, a):
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]
            a = self.p[a]
        return a

    def u(self, a, b):
        self.p[self.f(a)] = self.f(b)


def seed_reason(p):
    why = []
    if p["additive"]:
        why.append("additive")
    if p["anim"] and p["area"] < 6000:
        why.append("vram_anim")
    if p["flame"] > 0 and p["area"] < 600:
        why.append("flame_texels")
    if p["uveff"] is not None:
        why.append("uv_effect%d" % p["uveff"])
    return why


def components(polys, idx=None):
    idx = list(range(len(polys))) if idx is None else idx
    uf, first = UF(len(polys)), {}
    for i in idx:
        for v in polys[i]["V"]:
            k = tuple(np.round(v).astype(int))
            if k in first:
                uf.u(i, first[k])
            else:
                first[k] = i
    comp = collections.defaultdict(list)
    for i in idx:
        comp[uf.f(i)].append(i)
    return [sorted(c) for c in sorted(comp.values(), key=min)]


MERGE, ATTACH, MAXDIAG, MAXPOLYS = 48, 24, 320, 80


def flame_sprite(p):
    return p["additive"] and p["anim"] and p["area"] < 6000


def clusters(r):
    """Fixture clusters of a room: [{'emit': [poly], 'body': [poly]}]."""
    polys = r["polys"]
    seeds = [i for i, p in enumerate(polys) if seed_reason(p)]
    if not seeds:
        return []
    uf = UF(len(seeds))
    cen = [polys[i]["V"] for i in seeds]
    for a in range(len(seeds)):
        for b in range(a + 1, len(seeds)):
            d = np.min(np.linalg.norm(cen[a][:, None, :] - cen[b][None, :, :], axis=2))
            pa, pb = polys[seeds[a]], polys[seeds[b]]
            glowy = pa["additive"] and pb["additive"] and not flame_sprite(pa) and not flame_sprite(pb)
            if d < MERGE:
                uf.u(a, b)
            elif glowy:
                la, ha = cen[a].min(0) - 32, cen[a].max(0) + 32
                lb, hb = cen[b].min(0), cen[b].max(0)
                if np.all(la <= hb) and np.all(lb <= ha):
                    uf.u(a, b)
    pos = {s: k for k, s in enumerate(seeds)}
    for comp in components(polys, seeds):
        for i in comp[1:]:
            uf.u(pos[comp[0]], pos[i])
    em = collections.defaultdict(list)
    for a in range(len(seeds)):
        em[uf.f(a)].append(seeds[a])
    comps = components(polys)
    comp_of = {}
    for ci, c in enumerate(comps):
        for i in c:
            comp_of[i] = ci
    cinfo = []
    for c in comps:
        pts = np.concatenate([polys[i]["V"] for i in c])
        lo, hi = pts.min(0), pts.max(0)
        cinfo.append((lo, hi, np.linalg.norm(hi - lo), len(c)))
    out = []
    for e in sorted(em.values(), key=min):
        pts = np.concatenate([polys[i]["V"] for i in e])
        elo, ehi = pts.min(0), pts.max(0)
        body = set(e)
        for i in e:
            ci = comp_of[i]
            if cinfo[ci][2] <= MAXDIAG and cinfo[ci][3] <= MAXPOLYS:
                body |= set(comps[ci])
        for ci, (lo, hi, diag, n) in enumerate(cinfo):
            if diag > MAXDIAG or n > MAXPOLYS:
                continue
            if np.all(lo <= ehi + ATTACH) and np.all(hi >= elo - ATTACH):
                pc = np.concatenate([polys[i]["V"] for i in comps[ci]])
                if np.min(np.linalg.norm(pc[:, None, :] - pts[None, :, :], axis=2)) < ATTACH:
                    body |= set(comps[ci])
        out.append({"emit": sorted(e), "body": sorted(body)})
    # several flame sprites whose holders are separate objects: one cluster each
    split = []
    for c in out:
        fs = [i for i in c["emit"] if flame_sprite(polys[i])]
        subs = []
        for i in fs:
            ce = polys[i]["V"].mean(0)
            for g in subs:
                if np.linalg.norm(g["c"] - ce) < 16:
                    g["e"].append(i)
                    break
            else:
                subs.append({"c": ce, "e": [i]})
        if len(subs) < 2:
            split.append(c)
            continue
        rest = [i for i in c["body"] if i not in set(fs)]
        assign, shared = [[] for _ in subs], False
        for comp in components(polys, rest):
            pc = np.concatenate([polys[i]["V"] for i in comp])
            d = [np.min(np.linalg.norm(pc[:, None, :] - np.concatenate([polys[i]["V"] for i in g["e"]])[None, :, :],
                                       axis=2)) for g in subs]
            if sum(1 for x in d if x < ATTACH) >= 2:
                shared = True
                break
            assign[int(np.argmin(d))].extend(comp)
        if shared:
            split.append(c)
            continue
        for k, g in enumerate(subs):
            body = sorted(set(g["e"]) | set(assign[k]))
            emit = sorted(set(g["e"]) | (set(assign[k]) & set(c["emit"])))
            split.append({"emit": emit, "body": body})
    out = split
    changed = True
    while changed:
        changed = False
        for a in range(len(out)):
            for b in range(a + 1, len(out)):
                if set(out[a]["body"]) & set(out[b]["body"]):
                    out[a] = {"emit": sorted(set(out[a]["emit"]) | set(out[b]["emit"])),
                              "body": sorted(set(out[a]["body"]) | set(out[b]["body"]))}
                    del out[b]
                    changed = True
                    break
            if changed:
                break
    return sorted(out, key=lambda c: c["emit"][0])


# Emitter textures (content keys; the same on every disc of this release).
CANDLE = {"8127aac776"}  # small candle flame, 7x15 texels, animated
FIRE = {"eb4eda4936"}  # fire flame, 15x31 texels, animated
FLAME_KEYS = CANDLE | FIRE
GLASS = {"92daea7663", "77b0e2631a", "c35639afb8"}  # lantern panes
SWIRL = {"bf2100f688", "4fcbf921fa", "bb0c779142", "72211c274e", "cc36315fd6", "4476fe0d0e", "d0c6ba51b0",
         "79c9495a3a"}
CIRCLE = {"f335e6e838"}  # save point sigil
COBWEB = {"a665e67cf9"}
ARROW = {"797889a21b", "ef728cdb6c"}
WINDOW4 = {"57e7", "bf8f", "6a6d", "0463", "8949"}


def _r(v, n=1):
    return [round(float(x), n) + 0.0 for x in v]


def flames_of(r, c):
    """Flame sprites of a cluster: additive polygons with a flame texture,
    grouped within 16 units (in polygon order). -> [{'polys', 'world', 'size'}]"""
    polys = r["polys"]
    groups = []
    for i in c["emit"]:
        p = polys[i]
        if not (p["additive"] and p["ikey"] in FLAME_KEYS):
            continue
        ce = p["V"].mean(0)
        for g in groups:
            if np.linalg.norm(g["c"] - ce) < 16:
                g["i"].append(i)
                break
        else:
            groups.append({"c": ce, "i": [i]})
    out = []
    for g in groups:
        pts = np.concatenate([polys[i]["V"] for i in g["i"]])
        out.append({"polys": g["i"], "world": pts.mean(0), "size": float(np.sqrt(max(polys[i]["area"] for i in g["i"]))),
                    "height": float(np.ptp(pts[:, 1])), "key": sorted(polys[i]["ikey"] for i in g["i"])[0]})
    return out


def describe(r, c):
    """A cluster's frame, flames, extents and signatures."""
    polys = r["polys"]
    body = np.concatenate([polys[i]["V"] for i in c["body"]]).astype(float)
    kinds = collections.Counter()
    for i in c["emit"]:
        for k in seed_reason(polys[i]):
            kinds[k] += 1
    tiers = [[i for i in c["emit"] if polys[i]["additive"] and polys[i]["anim"]],
             [i for i in c["emit"] if polys[i]["additive"]],
             [i for i in c["emit"] if polys[i]["flame_pos"] is not None],
             list(c["emit"])]
    for tier, sel in enumerate(tiers):
        if sel:
            break
    w, s = 0.0, np.zeros(3)
    for i in sel:
        p = polys[i]
        if tier == 2:
            s += p["flame_pos"] * p["flame"]
            w += p["flame"]
        else:
            s += p["V"].mean(0) * max(p["area"], 1)
            w += max(p["area"], 1)
    centre = s / max(w, 1e-9)
    lo, hi = body.min(0), body.max(0)
    origin = np.array([(lo[0] + hi[0]) / 2, hi[1], (lo[2] + hi[2]) / 2])
    xz = body[:, [0, 2]] - origin[[0, 2]]
    if len(body) > 2:
        _, evec = np.linalg.eigh(np.cov(xz.T))
        ax = evec[:, 1]
    else:
        ax = np.array([1.0, 0.0])
    # principal axis: canonical direction (x >= 0, then z >= 0)
    if ax[0] < -1e-9 or (abs(ax[0]) <= 1e-9 and ax[1] < 0):
        ax = -ax
    loc = np.stack([xz @ ax, xz @ np.array([-ax[1], ax[0]])], -1)
    ext = [float(np.ptp(loc[:, 0])), float(hi[1] - lo[1]), float(np.ptp(loc[:, 1]))]
    # the wall it hangs on / the floor it stands on: large polygons near it
    bodyset, wall, floor = set(c["body"]), None, None
    for j, p in enumerate(polys):
        if j in bodyset or p["area"] < 1500:
            continue
        vv = p["V"]
        if np.any(vv.min(0) > hi + 48) or np.any(vv.max(0) < lo - 48):
            continue
        n = p["normal"]
        d = (centre - vv[0]) @ n
        if abs(n[1]) < 0.35 and abs(d) < 96 and (wall is None or abs(d) < wall[0]):
            nn = n if d >= 0 else -n  # pointing from the wall to the fixture
            wall = (abs(d), nn)
        if abs(n[1]) > 0.8 and (floor is None or abs(d) < floor[0]):
            floor = (abs(d), n)
    # the fixture's frame (prefab convention: R = Ry(yaw), forward = (sin, 0, cos), right = (cos, 0, -sin))
    if wall is not None:
        f = wall[1]
        yaw = math.degrees(math.atan2(f[0], f[2]))
    else:
        yaw = math.degrees(math.atan2(-ax[1], ax[0]))  # right = principal axis
    flames = flames_of(r, c)
    # emitters: the additive polygons' textures (the light's own: a flame, panes,
    # a glow); a fixture without any: its seeds' textures, marked "opaque:".
    # holder: the textures of every other polygon of the fixture.
    lit = [i for i in c["emit"] if polys[i]["additive"]] or list(c["emit"])
    holder = sorted(polys[i]["ikey"] for i in c["body"] if i not in set(lit))
    emitters = collections.Counter(polys[i]["ikey"] for i in lit)
    opaque = "" if polys[lit[0]]["additive"] else "opaque:"
    cents = np.array([polys[i]["V"].mean(0) for i in c["body"]])
    bc = body.mean(0)
    return {
        "zone": r["zone"], "room": r["room"], "emit": c["emit"], "body": c["body"],
        "shape": sorted(round(polys[i]["area"] / 8) for i in c["body"]),
        "cdist": sorted(np.round(np.linalg.norm(cents - bc, axis=1) / 4).astype(int).tolist()),
        "first": polys[c["emit"][0]]["off"],
        "npoly": len(c["body"]), "nemit": len(c["emit"]), "centre": centre, "origin": origin,
        "yaw": round(yaw, 2) + 0.0, "ext": ext, "kinds": dict(kinds), "flame_tier": tier,
        "keys": sorted(polys[i]["ikey"] for i in c["body"]),
        "ekeys": sorted(polys[i]["ikey"] for i in c["emit"]),
        "akeys": sorted({polys[i]["ikey"] for i in c["emit"] if polys[i]["additive"]}),
        "emitters": opaque + "+".join(f"{k}*{v}" if v > 1 else k for k, v in sorted(emitters.items())),
        "holder": hashlib.sha1(" ".join(holder).encode()).hexdigest()[:10] if holder else "",
        "wall": wall, "floor": floor, "flames": flames,
        "additive_area": float(sum(polys[i]["area"] for i in c["emit"] if polys[i]["additive"])),
    }


def local_of(d, world):
    """World point -> the fixture's local frame (origin bottom centre, +y down,
    x right, z forward of its yaw)."""
    rel = np.asarray(world, float) - d["origin"]
    a = math.radians(d["yaw"])
    right = np.array([math.cos(a), 0, -math.sin(a)])
    fwd = np.array([math.sin(a), 0, math.cos(a)])
    return [float(rel @ right), float(rel[1]), float(rel @ fwd)]


def world_of(d, local):
    a = math.radians(d["yaw"])
    right = np.array([math.cos(a), 0, -math.sin(a)])
    fwd = np.array([math.sin(a), 0, math.cos(a)])
    return d["origin"] + right * local[0] + np.array([0, 1, 0]) * local[1] + fwd * local[2]


# ---- the scan's classes (only for clusters that no catalog type matches) ----

# class id -> (family, description); families: fire, lamp, magic, window (light
# sources), and the non-lights.
CLASSES = {
    "candle": ("fire", "candle (holder and flame)"),
    "candelabra": ("fire", "candelabra / multi-candle holder"),
    "candle_flame": ("fire", "candle flame only (holder is room mesh)"),
    "wall_torch": ("fire", "wall torch"),
    "torch_flame": ("fire", "torch flame only (holder is room mesh)"),
    "standing_torch": ("fire", "standing torch / fire basket"),
    "torch_group": ("fire", "group of torches / multi-flame stand"),
    "brazier": ("fire", "brazier / fire bowl on a stand"),
    "fireplace": ("fire", "fireplace / hearth / campfire (large flame at floor level)"),
    "fire_sprite": ("fire", "large fire sprite only (hearth is room mesh)"),
    "lantern": ("fire", "lantern (hanging / standing cage)"),
    "wall_lantern": ("fire", "wall-bracket lantern"),
    "street_lantern": ("fire", "glass street lantern (flame and glowing panes)"),
    "glass_lamp": ("lamp", "glass lantern (glowing panes, no flame sprite)"),
    "save_point": ("magic", "save point (sigil and light column)"),
    "magic_glow": ("magic", "magic glow (swirl texture)"),
    "window": ("window", "window glow (additive panes)"),
    "shaft": ("shaft", "light shaft / glow volume (additive)"),
    "decal": ("decal", "flat additive floor/ceiling glow (decal)"),
    "cobweb": ("non", "cobweb (additive)"),
    "floor_arrows": ("non", "scrolling floor overlay (additive arrows)"),
    "exit_marker": ("non", "exit floor marker (scrolling, opaque)"),
    "animated_surface": ("non", "animated surface (liquid / texture animation, no glow)"),
    "warm_texels": ("non", "warm stone texels (not a light)"),
    "other": ("other", "other additive glow"),
}


def classify(d):
    """The scan's heuristic class of a described cluster: (class, confidence)."""
    ak = set(d["akeys"])
    ak4 = {k[:4] for k in ak}
    kinds = set(d["kinds"])
    h = d["ext"][1]
    w = max(d["ext"][0], d["ext"][2])
    dmin = min(d["ext"][0], d["ext"][2])
    wall = d["wall"][0] if d["wall"] else 999
    if kinds == {"uv_effect2"}:
        return "exit_marker", "high"
    if ak & ARROW:
        return "floor_arrows", "high"
    if ak & COBWEB:
        return "cobweb", "high"
    if not ak and kinds <= {"flame_texels"}:
        return "warm_texels", "high"
    if not ak and "vram_anim" in kinds:
        return "animated_surface", "medium"
    if ak & (SWIRL | CIRCLE):
        if d["npoly"] >= 8 and h >= 150:
            return "save_point", "high"
        return "magic_glow", "medium"
    fl = d["flames"]
    if ak & CANDLE:
        n = len(fl)
        body = d["npoly"] - sum(len(f["polys"]) for f in fl)
        if n >= 2:
            return "candelabra", "high"
        if body == 0:
            return "candle_flame", "medium"
        return "candle", "high"
    if ak & FIRE:
        n = len(fl)
        s = max(f["size"] for f in fl) if fl else 0
        body = d["npoly"] - sum(len(f["polys"]) for f in fl)
        fy = -local_of(d, d["centre"])[1]
        above = h - fy
        if n >= 2 and body > 0 and w >= 64:
            return "torch_group", "medium"
        if body == 0:
            return ("fire_sprite", "medium") if s >= 100 else ("torch_flame", "medium")
        if s >= 80 and fy - s / 2 < 40:
            return "fireplace", "medium"
        if ak & GLASS:
            return "street_lantern", "high"
        if above >= max(12, 0.3 * h) and fy >= 8 and body >= 10 and w <= 72:
            if wall < 64 and w >= 48:
                return "wall_lantern", "high"
            return "lantern", "high"
        if wall <= 48 and dmin <= 40:
            return "wall_torch", "high"
        if s >= 54 or w >= 60:
            return "brazier", "medium"
        return "standing_torch", "medium"
    if ak & GLASS:
        return "glass_lamp", "high"
    if ak4 & WINDOW4 or (h >= 150 and dmin <= 24 and d["npoly"] >= 2 and w <= 400):
        return "window", "medium"
    if h < 4:
        return "decal", "medium"
    if np.linalg.norm(d["ext"]) >= 200:
        return "shaft", "low"
    return "other", "low"


# ---- glows, as pc/src/glows.c groups the room's additive polygons ----


def scene_triangles(r):
    """The room's triangles as scene_capture.c makes them (quads: two halves,
    quad 1 = vertices 0 1 2, quad 2 = 1 3 2), with their polygon index."""
    out = []
    for i, p in enumerate(r["polys"]):
        if not captured(p):
            continue
        vv = p["V"].astype(np.float32)
        out.append((i, 1 if p["quad"] else 0, vv[[0, 1, 2]]))
        if p["quad"]:
            out.append((i, 2, vv[[1, 3, 2]]))
    return out


def glow_groups(r):
    """[{'key', 'tag', 'centre', 'polys': set of poly indices}] in the order
    glows.c builds them (its room groups; keys 'room:<tag>@x,y,z')."""
    polys = r["polys"]
    tris = [t for t in scene_triangles(r) if polys[t[0]]["additive"]]
    n = len(tris)
    parent = list(range(n))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    box = np.zeros((n, 6), np.float32)
    for a, (_, _, v) in enumerate(tris):
        box[a, :3] = v.min(0) - np.float32(12)
        box[a, 3:] = v.max(0) + np.float32(12)
    if n <= 4096:
        for a in range(n):
            touch = np.all((box[a, :3] <= box[a + 1:, 3:]) & (box[a + 1:, :3] <= box[a, 3:]), axis=1)
            for b in np.nonzero(touch)[0] + a + 1:
                ra, rb = root(a), root(int(b))
                if ra != rb:
                    lo, hi = min(ra, rb), max(ra, rb)
                    parent[hi] = lo
    done = [False] * n
    groups = []
    member = [-1] * n
    for a in range(n):
        if done[a]:
            continue
        ra = root(a)
        mem = [b for b in range(a, n) if root(b) == ra]
        pts = np.concatenate([tris[b][2] for b in mem])
        extent = float(np.sqrt(np.sum((pts.max(0) - pts.min(0)).astype(np.float32) ** 2)))
        first = len(groups)
        for b in mem:
            if done[b]:
                continue
            v = tris[b][2]
            c = (v[0] + v[1] + v[2]) / np.float32(3)
            g = first
            if extent > 384.0:
                while g < len(groups):
                    dd = groups[g]["seed"] - c
                    if float(np.dot(dd, dd)) < 160.0 * 160.0:
                        break
                    g += 1
            else:
                g = len(groups) if first == len(groups) else first
            if g == len(groups):
                if len(groups) == 512:
                    continue
                groups.append({"seed": c, "tag": 2166136261, "members": []})
            done[b] = True
            member[b] = g
            off = polys[tris[b][0]]["off"]
            groups[g]["tag"] = ((groups[g]["tag"] ^ ((off * 2 + tris[b][1]) & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
            groups[g]["members"].append(b)
    out = []
    for g in groups:
        s = np.zeros(3, np.float32)
        area = np.float32(0)
        for b in g["members"]:
            v = tris[b][2]
            e1, e2 = v[1] - v[0], v[2] - v[0]
            x = np.cross(e1, e2).astype(np.float32)
            ar = np.float32(0.5) * np.float32(np.sqrt(np.float32(np.dot(x, x)))) + np.float32(1e-3)
            s += ar * ((v[0] + v[1] + v[2]) / np.float32(3))
            area += ar
        centre = s / area
        tag = g["tag"] & 0x0FFFFFFF
        home = [int(np.rint(c)) for c in centre]
        out.append({"key": "room:%07x@%d,%d,%d" % (tag, *home), "tag": tag, "centre": centre.astype(float),
                    "polys": sorted({tris[b][0] for b in g["members"]})})
    return out


# ---- the catalog file (JSON, written as pc/src/light_prefabs.cpp json_write does) ----


def _number(v):
    v = float(v)
    if v == math.floor(v) and abs(v) < 1e9:
        s = "%.0f" % v
    else:
        s = "%.6g" % v
    return "0" if s == "-0" else s


def _quote(s):
    out = '"'
    for c in s:
        if c in '"\\':
            out += "\\" + c
        elif c == "\n":
            out += "\\n"
        elif c == "\t":
            out += "\\t"
        elif ord(c) < 0x20:
            out += "\\u%04x" % ord(c)
        else:
            out += c
    return out + '"'


def _inline(v):
    if isinstance(v, list):
        return len(v) <= 4 and all(not isinstance(i, (list, dict)) and not (isinstance(i, str) and len(i) > 24)
                                   for i in v)
    return not isinstance(v, dict)


def json_write(v, indent=0):
    """JSON text exactly as prefab::json_write formats it (the editor saves
    the catalog with that; both sides then give identical files)."""
    pad, inner = "  " * indent, "  " * (indent + 1)
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return _number(v)
    if isinstance(v, str):
        return _quote(v)
    if isinstance(v, list):
        if not v:
            return "[]"
        if _inline(v):
            return "[" + ", ".join(json_write(i, 0) for i in v) + "]"
        return "[\n" + ",\n".join(inner + json_write(i, indent + 1) for i in v) + "\n" + pad + "]"
    if not v:
        return "{}"
    flat = len(v) <= 8
    width = 0
    for k, m in v.items():
        flat = flat and _inline(m)
        width += len(k) + 8 + (len(m) if isinstance(m, str) else 8)
    if flat and width < 150 and indent > 0:
        return "{ " + ", ".join(_quote(k) + ": " + json_write(m, 0) for k, m in v.items()) + " }"
    return ("{\n" + ",\n".join(inner + _quote(k) + ": " + json_write(m, indent + 1) for k, m in v.items())
            + "\n" + pad + "}")


def load_catalog(path=None):
    import json
    with open(path or CATALOG, encoding="utf-8") as f:
        return json.load(f)


def save_catalog(cat, path=None):
    text = json_write(cat) + "\n"
    with open(path or CATALOG, "w", encoding="utf-8") as f:
        f.write(text)
    return text


def type_fields(cat, t):
    """A type's effective settings: its class's defaults, then its own."""
    cls = cat["classes"].get(t["class"], {})
    out = {"prefab": cls.get("prefab", "none"), "glow": cls.get("glow", "leave"), "enabled": cls.get("enabled", True),
           "fixture_radius": cls.get("fixture_radius", 16), "params": dict(cls.get("params", {})),
           "offset": cls.get("offset", [0, 0, 0]), "family": cls.get("family", "other")}
    for k in ("prefab", "glow", "enabled", "fixture_radius", "offset"):
        if k in t:
            out[k] = t[k]
    out["params"].update(t.get("params", {}))
    return out


def ext_sorted(d):
    return sorted(float(x) for x in d["ext"])


def match(cat, d):
    """The catalog type of a described cluster: (type, 'exact' | 'rule') or
    (None, why). Exact: its emitter textures, polygon count, size and holder
    (the textures of its other polygons) are those of a known instance; rule:
    all but the holder, or a size within the tolerance but outside the
    known range. Several candidates: holder first, then the size nearest
    the type's range, the tightest range, the lowest id."""
    tol = cat.get("tolerance", {"abs": 4, "rel": 0.1})
    ext = ext_sorted(d)
    cands = []
    for t in cat["types"]:
        m = t["match"]
        if d["emitters"] not in m["emitters"]:
            continue
        if not (m["polygons"][0] <= d["npoly"] <= m["polygons"][1]):
            continue
        lo, hi = m["extent"]["min"], m["extent"]["max"]
        ok = True
        for k in range(3):
            slack = max(tol["abs"], tol["rel"] * hi[k])
            if not (lo[k] - slack <= ext[k] <= hi[k] + slack):
                ok = False
        if not ok:
            continue
        exact = d["holder"] in m["holders"]
        # (ranges are kept to 0.1)
        outside = sum(max(lo[k] - 0.051 - ext[k], ext[k] - hi[k] - 0.051, 0) for k in range(3))
        vol = (hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1)
        cands.append((0 if exact else 1, round(outside, 3), vol, t["id"], t))
    if not cands:
        return None, "no type"
    cands.sort(key=lambda c: c[:4])
    return cands[0][4], "exact" if cands[0][0] == 0 and cands[0][1] == 0 else "rule"
