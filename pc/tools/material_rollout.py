#!/usr/bin/env python3
"""The game-wide material rollout (pc/docs/Materials.md, "Rolling out"):
survey every room, group what covers the screen by palette, propose a
preset per palette, and review the proposals on a local page.

    python3 pc/tools/material_rollout.py survey [--until 21:30] [--zone N ...] [--rooms 29/3,9/1] [--max N]
    python3 pc/tools/material_rollout.py report
    python3 pc/tools/material_rollout_apply.py REVIEW.json        (the accepted proposals into the catalog)

survey: ONE headless game at a time (material_survey.py's: its own RPC
socket, scratch lighting and material layers, never a running game) warps
room after room (RPC room_goto, no enemies, no scripts) through the game's
own room table (RPC rooms: every room read from the disc, with its name)
and surveys each from material_survey.py's views. Resumable: each room's
survey lands in <out>/rooms/<zone>-<room>.json and <out>/state.json notes
it done (or failed, retried once on a later run), so a run skips what is
done. --until HH:MM (local time) parks it: no room is started after then.
A game that dies or stalls is killed (by its PID) and relaunched.

report: aggregates the rooms surveyed: every distinct texture (key, size,
palette) and palette (CLUT key), the rooms each is in and its coverage (the
sum over rooms of its mean share of the views: "rooms' worth of screen"),
whether the catalog already covers it, and its look (luminance, contrast,
saturation, hue, the streaks' direction, from its texels) and the survey's
hints (how much of it faces up, sideways or down; drawn by object lists:
props and characters). One proposal per palette (per texture for 15-bit
textures, which have none), a preset with a reason and a confidence, sorted
by coverage, into <out>/proposals.json and <out>/review.html: thumbnails,
rooms, the proposal, and accept / reject / change controls that export a
review JSON for material_rollout_apply.py.

Everything under <out> (default pc/build/material_rollout) is game art and
stays local: never committed, never uploaded.
"""
import argparse
import colorsys
import datetime
import html
import json
import math
import os
import re
import pathlib
import shutil
import signal
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
OUT = ROOT / "pc/build/material_rollout"
CATALOG = ROOT / "pc/assets/materials/catalog.json"
PRESETS = ["rough stone", "flagstone", "stone", "wet stone", "plaster", "worn wood", "worn iron", "polished metal",
           "glass", "gel", "cloth", "foliage", "grass", "earth", "bark", "moss"]
RULES = ROOT / "pc/tools/material_rollout_rules.json"  # the proposals' tunables (thresholds, room-name words)


def load_rules(path=RULES):
    rules = json.loads(pathlib.Path(path).read_text())
    rules.pop("about", None)
    return rules
NONE = "none"  # no entry: the texture stays as it is (matte, no relief)
THUMB_LIMIT = 0.001  # textures below this mean share of a room's views get no thumbnail from that room
ROOM_TIMEOUT = 120  # seconds a room may take before the game is taken for stalled


# ---------------------------------------------------------------- state

def until_time(text, now=None):
    """--until HH:MM as a timestamp today (local time); None for no limit."""
    if not text:
        return None
    hh, mm = (int(x) for x in text.split(":"))
    now = now or datetime.datetime.now()
    return now.replace(hour=hh, minute=mm, second=0, microsecond=0).timestamp()


def room_id(zone, room):
    return f"{zone}-{room}"


def load_state(out):
    path = out / "state.json"
    if path.exists():
        return json.loads(path.read_text())
    return {"format": 1, "done": {}, "failed": {}}


def write_json(path, data, indent=None):
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=indent) + "\n")
    os.replace(tmp, path)


def targets(rooms, state, zones=None, only=None, retries=1):
    """The rooms still to survey, in the table's order: not done, not failed
    more than `retries` times; --zone / --rooms narrow them."""
    out = []
    for r in rooms:
        zone, room = r["zone"], r["room"]
        rid = room_id(zone, room)
        if zones and zone not in zones:
            continue
        if only and (zone, room) not in only:
            continue
        if rid in state["done"]:
            continue
        if state["failed"].get(rid, {}).get("attempts", 0) > retries:
            continue
        out.append((zone, room))
    return out


# ---------------------------------------------------------------- survey

class Stalled(Exception):
    pass


def _alarm(signum, frame):
    raise Stalled("room took too long")


def launch(scratch, args):
    from checkpoints import launch_at
    card = scratch / "card1.mcd"
    shutil.copyfile(args.card, card)
    (scratch / "settings.cfg").write_text("light_animation 0\ndust 0\n")
    (scratch / "lights").mkdir(exist_ok=True)
    env = {"ROOD_LIGHTS": str(scratch / "lights"), "ROOD_LIGHTING": str(ROOT / "lighting"),
           "ROOD_SETTINGS": str(scratch / "settings.cfg"), "ROOD_SCALE": "2", "ROOD_WINDOW": "1280x960",
           "ROOD_MATERIAL_CATALOG_USER": str(scratch / "materials.json"), "ROOD_MATERIALS": str(scratch / "maps"),
           "ROOD_MATERIAL_EXPORTS": str(scratch / "exports"), "ROOD_OUT": str(scratch)}
    socket = str(pathlib.Path(tempfile.gettempdir()) / f"rood-rollout-{os.getpid()}.sock")
    return launch_at("save_room", card=card, fast=True, log=scratch / "game.log", extra_env=env, socket_path=socket)


def kill(game):
    """Close the game; one that does not answer is killed by its PID."""
    signal.alarm(20)
    try:
        game.close()
    except BaseException:
        for p in getattr(game, "processes", []):
            try:
                os.kill(p.pid, signal.SIGKILL)
            except OSError:
                pass
    finally:
        signal.alarm(0)


def save_thumbs(game, data, tex_dir):
    """Each texture's texels as drawn (RPC material texture), once per texture."""
    from PIL import Image
    import base64
    saved = 0
    for t in data["textures"]:
        if t["share"] < THUMB_LIMIT:
            continue
        path = tex_dir / f"{texture_id(t)}.png"
        if path.exists():
            continue
        try:
            got = game.call("material", action="texture", key=t["key"], palette=t["clut"])
        except Exception:
            continue
        w, h = int(got["w"]), int(got["h"])
        Image.frombytes("RGBA", (w, h), base64.b64decode(got["rgba"])).save(path)
        saved += 1
    return saved


def goto(game, zone, room, cell=None, enemies=False):
    """material_survey.goto, with a cell for rooms no door leads into (the
    warp makes up a door onto it, as the game's pit falls do), and the room's
    enemies when asked (their models are then drawn: the model painting round trip)."""
    game.call("editor_camera", off=1)
    extra = {"cell": cell} if cell else {}
    game.call("room_goto", zone=zone, room=room, enemies=enemies, scripts=enemies, **extra)
    for _ in range(60):
        game.run(20)
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
    game.run(40)
    state = game.state()
    return (state["zone"], state["room"]) == (zone, room)


def arrival_cell(record):
    """No door into the room: the middle of its floor grid (None: use a door)."""
    if not record or record.get("entries"):
        return None
    w, h = record.get("floor", [0, 0])
    return [w // 2, 0, h // 2, 0] if w > 0 and h > 0 else None


def survey_one(game, zone, room, args, out, record=None):
    import material_survey
    from PIL import Image
    if not goto(game, zone, room, arrival_cell(record)):
        raise RuntimeError("could not reach the room")
    work = out / "work"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    ns = argparse.Namespace(views=args.views, grid=args.grid, turn=args.turn)
    data = material_survey.survey_room(game, ns, work)
    data["thumbs"] = save_thumbs(game, data, out / "tex")
    for t in data["textures"]:
        t.pop("rank", None)
    shot = work / "view-game.png"
    if shot.exists():
        Image.open(shot).convert("RGB").resize((320, 240), Image.BILINEAR).save(out / "views" / f"{zone}-{room}.jpg",
                                                                                 quality=80)
    shutil.rmtree(work, ignore_errors=True)
    return data


def cmd_survey(args):
    os.environ.setdefault("ROOD_ARCH", "64")
    os.environ.setdefault("ROOD_RENDERER", "vulkan")
    out = pathlib.Path(args.out)
    for d in ("rooms", "tex", "views"):
        (out / d).mkdir(parents=True, exist_ok=True)
    if not pathlib.Path(args.card).exists():
        raise SystemExit(f"no memory card {args.card} (run pc/tests/run.py once, or --card)")
    stop = until_time(args.until)
    if stop and time.time() >= stop:
        print(f"parked: it is past {args.until}")
        return 0
    state = load_state(out)
    only = {tuple(int(x) for x in r.split("/")) for r in args.rooms.split(",")} if args.rooms else None
    signal.signal(signal.SIGALRM, _alarm)
    scratch = pathlib.Path(tempfile.mkdtemp(prefix="rollout-", dir=out))
    game = None
    surveyed = launches = 0
    t0 = time.time()
    try:
        todo = None
        while True:
            if game is None:
                if launches > args.max_launches:
                    print("too many relaunches: stopping")
                    break
                shutil.rmtree(scratch, ignore_errors=True)
                scratch.mkdir()
                game = launch(scratch, args)
                launches += 1
                if not (out / "rooms.json").exists():
                    listed = game.call("rooms")
                    if listed.get("error") or not listed.get("rooms"):
                        raise SystemExit(f"no room table: {listed.get('error')}")
                    write_json(out / "rooms.json", listed["rooms"], indent=0)
            if todo is None:
                rooms = json.loads((out / "rooms.json").read_text())
                records = {(r["zone"], r["room"]): r for r in rooms}
                todo = targets(rooms, state, set(args.zone or []) or None, only)
                print(f"{len(todo)} rooms to survey of {len(rooms)} ({len(state['done'])} done)", flush=True)
            if not todo or (args.max and surveyed >= args.max):
                break
            if stop and time.time() >= stop:
                print(f"parked at {args.until}: {len(todo)} rooms left")
                break
            zone, room = todo.pop(0)
            rid = room_id(zone, room)
            started = time.time()
            signal.alarm(ROOM_TIMEOUT)
            try:
                data = survey_one(game, zone, room, args, out, records.get((zone, room)))
                signal.alarm(0)
            except BaseException as e:  # noqa: B902 (SystemExit from the survey: no view)
                signal.alarm(0)
                if isinstance(e, KeyboardInterrupt):
                    raise
                why = f"{type(e).__name__}: {e}"[:200]
                f = state["failed"].setdefault(rid, {"attempts": 0})
                f["attempts"] += 1
                f["why"] = why
                write_json(out / "state.json", state, indent=0)
                print(f"  {rid}: failed ({why})", flush=True)
                if isinstance(e, Stalled) or "connection" in why or "Broken pipe" in why or "RpcError" in why:
                    kill(game)  # dead, stalled or confused: start over
                    game = None
                continue
            write_json(out / "rooms" / f"{rid}.json", data)
            state["done"][rid] = {"s": round(time.time() - started, 1), "textures": len(data["textures"])}
            state["failed"].pop(rid, None)
            write_json(out / "state.json", state, indent=0)
            surveyed += 1
            if surveyed % 10 == 0 or surveyed < 3:
                print(f"  {rid}: {len(data['textures'])} textures in {time.time() - started:.1f} s; {surveyed} "
                      f"this run, {len(state['done'])} done, {len(todo)} left, {time.time() - t0:.0f} s",
                      flush=True)
            if args.restart_every and surveyed % args.restart_every == 0:
                kill(game)
                game = None
    finally:
        if game is not None:
            kill(game)
        shutil.rmtree(scratch, ignore_errors=True)
        shutil.rmtree(out / "work", ignore_errors=True)
    print(f"surveyed {surveyed} rooms this run; {len(state['done'])} done, {len(state['failed'])} failed")
    return 0


# ---------------------------------------------------------------- the look

def texture_id(t):
    return f"{t['key']}-{t['size'][0]}x{t['size'][1]}-{t['clut'] or 'direct'}"


def texture_stats(path):
    """Its look from its opaque texels: luminance mean and spread, saturation,
    hue (degrees, saturation-weighted), how many bright specks, the dark
    seams, and the streaks: the share of the gradient energy across x (a
    texture with vertical planks or grain has most of it) - 0.5 is none."""
    from PIL import Image
    import numpy as np
    im = np.asarray(Image.open(path).convert("RGBA"), dtype=np.float64) / 255.0
    return stats_of(im)


def stats_of(im):
    import numpy as np
    rgb, a = im[..., :3], im[..., 3]
    opaque = a > 0.5
    n = int(opaque.sum())
    if n == 0:
        return {"opaque": 0.0}
    lum = rgb @ np.array([0.2126, 0.7152, 0.0722])
    mx, mn = rgb.max(-1), rgb.min(-1)
    sat = np.where(mx > 1e-6, (mx - mn) / np.maximum(mx, 1e-6), 0.0)
    l, s = lum[opaque], sat[opaque]
    r, g, b = (rgb[..., k][opaque] for k in range(3))
    hues = np.array([colorsys.rgb_to_hsv(*px)[0] for px in zip(r[::7], g[::7], b[::7])])
    w = s[::7]
    if w.sum() > 1e-6:
        ang = math.degrees(math.atan2((w * np.sin(hues * 2 * math.pi)).sum(), (w * np.cos(hues * 2 * math.pi)).sum()))
        hue = ang % 360
    else:
        hue = 0.0
    gx = np.abs(np.diff(lum, axis=1))[:, :][opaque[:, 1:] & opaque[:, :-1]] if lum.shape[1] > 1 else np.zeros(1)
    gy = np.abs(np.diff(lum, axis=0))[opaque[1:, :] & opaque[:-1, :]] if lum.shape[0] > 1 else np.zeros(1)
    ex, ey = float((gx ** 2).mean()) if gx.size else 0.0, float((gy ** 2).mean()) if gy.size else 0.0
    mean = float(l.mean())
    return {"opaque": round(n / a.size, 3), "lum": round(mean, 3), "contrast": round(float(l.std()), 3),
            "sat": round(float(s.mean()), 3), "hue": round(hue, 1),
            "specks": round(float((l > max(0.75, mean + 2.5 * l.std())).mean()), 4),
            "seams": round(float((l < mean * 0.5).mean()), 3),
            "streak": round(ex / (ex + ey), 3) if ex + ey > 1e-9 else 0.5,
            "rough": round(math.sqrt((ex + ey) / 2), 3)}


def blend(stats, weights):
    """Coverage-weighted mean of textures' stats (hue as an angle)."""
    total = sum(weights) or 1
    keys = ("lum", "contrast", "sat", "specks", "seams", "streak", "rough", "opaque")
    out = {k: round(sum(s.get(k, 0) * w for s, w in zip(stats, weights)) / total, 3) for k in keys}
    hx = sum(math.cos(math.radians(s.get("hue", 0))) * s.get("sat", 0) * w for s, w in zip(stats, weights))
    hy = sum(math.sin(math.radians(s.get("hue", 0))) * s.get("sat", 0) * w for s, w in zip(stats, weights))
    out["hue"] = round(math.degrees(math.atan2(hy, hx)) % 360, 1)
    return out


_RULES = None


def default_rules():
    global _RULES
    if _RULES is None:
        _RULES = load_rules()
    return _RULES


def propose(look, hints, names=(), rules=None):
    """A preset for a palette (or texture) with a short reason and a
    confidence (high, medium, low): heuristics, suggestions for review.
    look: blended texture stats; hints: floor/wall/ceiling/object shares of
    its coverage (fractions summing to about 1, object separate); rules:
    the tunables (material_rollout_rules.json)."""
    R = rules or default_rules()
    floor, wall, ceil, obj = (hints.get(k, 0) for k in ("floor", "wall", "ceiling", "object"))
    # (no thumbnail: a mid-grey, even look, so the facing decides)
    lum, con, sat, hue = look.get("lum", 0.3), look.get("contrast", 0), look.get("sat", 0), look.get("hue", 0)
    streak, specks, seams, rough = look.get("streak", 0.5), look.get("specks", 0), look.get("seams", 0), \
        look.get("rough", 0)
    opaque = look.get("opaque", 1)

    def most(vocabulary):  # at least half its rooms are named so (whole words, plurals)
        hits = 0
        for name in names:
            words = set(re.findall(r"[a-z]+", name.lower()))
            hits += any(w in words or w + "s" in words for w in vocabulary)
        return bool(names) and hits * 2 >= len(names)
    words = R["words"]
    wet, damp, woody, forest = (most(words[k]) for k in ("wet", "damp", "wood", "forest"))
    brown = 15 <= hue <= 50 and 0.2 <= sat <= 0.75
    veg = R["vegetation_hue"][0] <= hue <= R["vegetation_hue"][1] and sat >= R["vegetation_sat"] and look != {}
    earthy = R["earth_hue"][0] <= hue <= R["earth_hue"][1] and sat >= R["earth_sat"]
    grain = streak > R["grain_streak"] or streak < 1 - R["grain_streak"]
    weak = con < R["weak_contrast"] and seams < R["weak_seams"]
    alts = []
    where = "floor" if floor >= 0.6 else "ceiling" if ceil >= 0.6 else "wall" if wall >= 0.6 else "mixed"
    if obj >= R["object_share"]:
        return NONE, "low", (f"{obj:.0%} drawn by object lists (props, characters): actor materials are a later "
                             "step; review case by case"), ["worn wood", "worn iron", "cloth"]
    if look and lum < R["void_lum"]:
        return NONE, "medium", f"near black (luminance {lum:.2f}: the void beyond the walls, shadow fill)", []
    place = " in a forest or garden (most of its rooms)" if forest else ""
    if look and opaque < R["cutout"] and (veg or (forest and earthy)):
        if veg:
            return "foliage", "high" if forest else "medium", \
                f"a green cut-out ({opaque:.0%} opaque, hue {hue:.0f}, saturation {sat:.2f}): leaves{place}", \
                ["grass", NONE]
        return "bark", "low", f"a brown cut-out ({opaque:.0%} opaque){place}: branches, twigs", ["foliage", NONE]
    if look and opaque < R["transparent"]:
        return NONE, "medium", "mostly transparent texels (grilles, cut-outs)", ["worn iron", "foliage"]
    if look and opaque < R["cutout"]:
        return NONE, "low", f"a cut-out ({opaque:.0%} opaque): leaves, grilles, railings? not a solid surface", \
            ["foliage", "worn iron", "worn wood"]
    if veg:
        if seams > R["vegetation_max_seams"]:  # green stone: blocks with moss, or tinted stone
            return ("moss" if where != "floor" else "grass"), "low", \
                f"green (hue {hue:.0f}, {sat:.2f}) but jointed (seams {seams:.0%}): mossy stone or tinted blocks?", \
                ["flagstone" if where == "floor" else "rough stone", "moss", "wet stone"]
        if where == "floor" or (where == "mixed" and floor >= wall):
            return "grass", "high" if forest else "medium", \
                f"green and saturated (hue {hue:.0f}, {sat:.2f}) facing up ({floor:.0%}){place}: grass, undergrowth", \
                ["earth", "moss", "foliage"]
        return "moss", "medium" if forest else "low", \
            f"green and saturated (hue {hue:.0f}, {sat:.2f}) facing sideways or down{place}: moss, ivy, mossy rock", \
            ["foliage", "rough stone", "wet stone"]
    if forest and (earthy or sat < R["earth_sat"]) and not (seams > R["stone_seams"] and sat < 0.2):
        if where == "floor":
            return "earth", "medium" if earthy else "low", \
                f"faces up ({floor:.0%}){place}, earthy (hue {hue:.0f}, saturation {sat:.2f}): soil, a path", \
                ["grass", "flagstone", "wet stone" if wet else "moss"]
        if grain:
            return "bark", "medium", f"directional grain ({streak:.2f} of the edges one way){place}: tree trunks, roots", \
                ["worn wood", "rough stone", "moss"]
        return "rough stone", "low", f"faces sideways{place}, no grain: rock? (contrast {con:.2f})", \
            ["earth", "moss", "bark"]
    if sat < 0.18 and specks > 0.01 and con > 0.12 and lum < 0.45:
        return "worn iron", "low", (f"grey with bright specks ({specks:.1%}): riveted or worn metal?"), \
            ["polished metal", "rough stone"]
    strong = streak > R["strong_grain"] or streak < 1 - R["strong_grain"]
    if brown and ((grain and sat > 0.3) or strong):
        conf = "medium" if woody else "low"
        return "worn wood", conf, (f"brown (hue {hue:.0f}) with directional grain ({streak:.2f} of the edges one "
                                   f"way): planks, beams"), ["rough stone", "flagstone"]
    if where == "floor":
        if wet:
            return "wet stone", "medium", "a floor in rooms named for rain or water (most of its rooms)", ["flagstone"]
        if lum > 0.62 and con < 0.07 and sat < 0.2:
            return "stone", "low", "a pale, even floor: smooth or polished stone?", ["flagstone", "plaster"]
        if earthy and sat >= R["earth_floor_sat"] and seams < R["floor_seams"]:
            return "earth", "low", f"a strongly coloured earthy floor without joints (hue {hue:.0f}, " \
                f"saturation {sat:.2f}): soil, sand?", ["flagstone", "rough stone"]
        cued = seams > R["floor_seams"] or con > R["floor_contrast"]
        conf = "high" if floor >= 0.85 and not brown and cued and not forest else "low" if weak else "medium"
        reason = f"faces up ({floor:.0%} of its coverage): a worn floor"
        reason += "" if cued else f" (weak cues: contrast {con:.2f}, seams {seams:.0%})"
        if damp:
            reason += "; damp area (cellar, catacombs, undercity): consider wet stone"
            alts.append("wet stone")
        return "flagstone", conf, reason, alts + ["rough stone", "worn wood", "earth"]
    if lum > 0.55 and con < 0.08 and sat < 0.25:
        return "plaster", "medium" if where in ("wall", "ceiling") else "low", \
            f"pale and even (luminance {lum:.2f}, contrast {con:.2f}): plaster, render", ["stone"]
    if sat > 0.5 and con < 0.12 and lum > 0.22 and not brown:  # (dark texels' saturation is noise)
        return "cloth", "low", f"strongly coloured and even (saturation {sat:.2f}): banners, drapes, rugs?", \
            ["plaster"]
    if wet and where != "ceiling":
        return "wet stone", "low", "a wall in rooms named for rain or water (most of its rooms)", ["rough stone"]
    blocks = seams > R["stone_seams"] and rough > R["stone_edges"] and sat < R["stone_max_sat"]
    conf = "high" if where in ("wall", "ceiling") and blocks and not forest else "low" if weak or where == "mixed" else "medium"
    reason = f"faces sideways ({wall:.0%})" if where == "wall" else f"faces down ({ceil:.0%})" \
        if where == "ceiling" else f"mixed facing (floor {floor:.0%}, wall {wall:.0%})"
    reason += f"; blocks and mortar (seams {seams:.0%}, edge contrast {rough:.2f})" if seams > R["stone_seams"] \
        else f"; weak cues (contrast {con:.2f}, seams {seams:.0%})"
    return "rough stone", conf, reason, ["stone", "plaster", "flagstone"]


# ---------------------------------------------------------------- report

def load_catalog(path):
    data = json.loads(pathlib.Path(path).read_text())
    keys, cluts = {}, {}
    for e in data.get("entries", []):
        m = e.get("match", {})
        if "key" in m:
            keys.setdefault((m["key"], tuple(m.get("size", [])), m.get("clut", "")), []).append(e["id"])
            keys.setdefault((m["key"], tuple(m.get("size", [])), ""), []).append(e["id"])
        elif "clut" in m:
            cluts.setdefault(m["clut"], []).append(e["id"])
    return keys, cluts


def aggregate(rooms_dir, names=None):
    """Textures and palettes over every surveyed room."""
    names = names or {}
    textures, palettes, rooms = {}, {}, []
    for path in sorted(pathlib.Path(rooms_dir).glob("*.json")):
        data = json.loads(path.read_text())
        zone, room = data["room"]
        rid = room_id(zone, room)
        rooms.append(rid)
        for t in data["textures"]:
            tid = texture_id(t)
            x = textures.setdefault(tid, {"id": tid, "key": t["key"], "size": list(t["size"]), "clut": t["clut"],
                                          "rooms": {}, "coverage": 0.0, "where": [0.0, 0.0, 0.0, 0.0],
                                          "entries": set()})
            x["rooms"][rid] = round(x["rooms"].get(rid, 0) + t["share"], 5)
            x["coverage"] += t["share"]
            for i, v in enumerate(list(t.get("facing", [0, 0, 0])) + [t.get("object", 0)]):
                x["where"][i] += v
            if t.get("entry"):
                x["entries"].add(t["entry"])
                x.setdefault("entry_rooms", set()).add(rid)
            p = palettes.setdefault(t["clut"] or "", {"clut": t["clut"] or "", "textures": set(), "rooms": {},
                                                      "coverage": 0.0})
            p["textures"].add(tid)
            p["rooms"][rid] = round(p["rooms"].get(rid, 0) + t["share"], 5)
            p["coverage"] += t["share"]
    return textures, palettes, rooms


def hints_of(where, coverage):
    c = coverage or 1
    return {"floor": where[0] / c, "wall": where[1] / c, "ceiling": where[2] / c, "object": where[3] / c}


def build(out, catalog=CATALOG, rules=None):
    """proposals.json from the rooms surveyed (and the thumbnails' looks)."""
    out = pathlib.Path(out)
    names = {}
    if (out / "rooms.json").exists():
        for r in json.loads((out / "rooms.json").read_text()):
            names[room_id(r["zone"], r["room"])] = (r.get("area", "") + ": " + r.get("name", "")).strip(": ")
    textures, palettes, rooms = aggregate(out / "rooms", names)
    keys, cluts = load_catalog(catalog)
    looks = {}
    for tid in textures:
        png = out / "tex" / f"{tid}.png"
        if png.exists():
            try:
                looks[tid] = texture_stats(png)
            except Exception:
                pass
    proposals = []

    def make(kind, clut, members, room_shares, coverage):
        where = [0.0] * 4
        for t in members:
            where = [a + b for a, b in zip(where, t["where"])]
        hints = hints_of(where, coverage)
        with_look = [t for t in members if t["id"] in looks]
        look = blend([looks[t["id"]] for t in with_look], [t["coverage"] for t in with_look]) if with_look else {}
        room_names = [names.get(r, "") for r in room_shares]
        preset, confidence, reason, alts = propose(look, hints, room_names, rules)
        if not look:
            confidence, reason = "low", reason + " (no thumbnail: facing only)"
        covered = sorted({i for t in members for i in t["entries"]})
        covered_rooms = sorted({r for t in members for r in t.get("entry_rooms", ())})
        catalogued = sorted(set(cluts.get(clut, [])) | {i for t in members for i in
                                                        keys.get((t["key"], tuple(t["size"]), t["clut"]), [])})
        top = sorted(members, key=lambda t: -t["coverage"])
        return {"kind": kind, "clut": clut, "key": top[0]["key"] if kind == "texture" else "",
                "size": top[0]["size"] if kind == "texture" else None,
                "coverage": round(coverage, 4), "rooms": {r: round(v, 4) for r, v in
                                                          sorted(room_shares.items(), key=lambda kv: -kv[1])},
                "room_names": {r: names.get(r, "") for r in room_shares},
                "textures": [{"id": t["id"], "key": t["key"], "size": t["size"], "coverage": round(t["coverage"], 4),
                              "rooms": len(t["rooms"]), "thumb": (out / "tex" / f"{t['id']}.png").exists()}
                             for t in top],
                "hints": {k: round(v, 3) for k, v in hints.items()}, "look": look,
                "preset": preset, "confidence": confidence, "reason": reason,
                "alternatives": [a for a in alts if a != preset],
                "entries_seen": covered, "covered_rooms": covered_rooms, "in_catalog": catalogued}

    for clut, p in palettes.items():
        members = [textures[t] for t in p["textures"]]
        if clut:
            proposals.append(make("palette", clut, members, p["rooms"], p["coverage"]))
        else:  # 15-bit textures have no palette: one proposal each
            for t in members:
                proposals.append(make("texture", "", [t], t["rooms"], t["coverage"]))
    proposals.sort(key=lambda q: -q["coverage"])
    for i, q in enumerate(proposals):
        q["id"] = f"P{i + 1:04d}"
    data = {"format": 1, "rooms_surveyed": len(rooms), "rooms_total": len(names) or None,
            "textures": len(textures), "palettes": len([c for c in palettes if c]), "proposals": proposals}
    write_json(out / "proposals.json", data, indent=1)
    return data


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Material rollout review</title>
<style>
:root { --bg: #f6f5f2; --fg: #1d1d1f; --muted: #6b6b70; --card: #fff; --line: #dedcd6; --accept: #2f7d4f;
  --reject: #b23b3b; --change: #a86b12; --chip: #ecebe7; }
@media (prefers-color-scheme: dark) { :root { --bg: #17171a; --fg: #ececee; --muted: #9a9aa2; --card: #212126;
  --line: #34343b; --chip: #2b2b31; } }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--fg); font: 14px/1.45 system-ui, sans-serif; }
header { position: sticky; top: 0; z-index: 2; background: var(--bg); border-bottom: 1px solid var(--line);
  padding: 12px 16px; display: flex; flex-wrap: wrap; gap: 8px 16px; align-items: center; }
h1 { font-size: 17px; margin: 0 12px 0 0; }
.stat { color: var(--muted); }
main { padding: 16px; display: grid; gap: 12px; max-width: 1400px; margin: 0 auto; }
.card { background: var(--card); border: 1px solid var(--line); border-radius: 8px; padding: 12px;
  display: grid; grid-template-columns: 260px 1fr 250px; gap: 14px; border-left: 5px solid var(--line); }
.card.accept { border-left-color: var(--accept); } .card.reject { border-left-color: var(--reject); opacity: .7; }
.card.change { border-left-color: var(--change); }
@media (max-width: 900px) { .card { grid-template-columns: 1fr; } }
.thumbs { display: flex; flex-wrap: wrap; gap: 4px; align-content: flex-start; }
.thumbs img { image-rendering: pixelated; background: repeating-conic-gradient(#888 0 25%, #666 0 50%) 0 0/8px 8px;
  height: 60px; width: auto; max-width: 120px; border-radius: 2px; }
.thumbs .more { color: var(--muted); font-size: 12px; align-self: center; }
.title { font-weight: 600; font-family: ui-monospace, monospace; }
.chip { display: inline-block; background: var(--chip); border-radius: 10px; padding: 1px 8px; margin: 2px 4px 2px 0;
  font-size: 12px; }
.conf-high { color: var(--accept); } .conf-medium { color: var(--change); } .conf-low { color: var(--reject); }
.rooms { color: var(--muted); font-size: 12px; max-height: 90px; overflow: auto; margin-top: 6px; }
.views { display: flex; gap: 4px; margin-top: 6px; flex-wrap: wrap; }
.views img { width: 120px; height: 90px; object-fit: cover; border-radius: 3px; }
.controls { display: grid; gap: 6px; align-content: start; }
.controls .row { display: flex; gap: 4px; flex-wrap: wrap; }
button, select, input { font: inherit; color: var(--fg); background: var(--card); border: 1px solid var(--line);
  border-radius: 5px; padding: 4px 8px; }
button.on.acc { background: var(--accept); color: #fff; } button.on.rej { background: var(--reject); color: #fff; }
button.primary { background: var(--fg); color: var(--bg); }
.note { width: 100%; }
.look { font-size: 12px; color: var(--muted); }
</style></head><body>
<header>
  <h1>Material rollout review</h1>
  <span class="stat" id="summary"></span>
  <label>Show <select id="filter"><option value="all">all</option><option value="open">undecided</option>
    <option value="accept">accepted</option><option value="reject">rejected</option>
    <option value="todo">rooms not in catalog</option></select></label>
  <label>Min coverage <input id="mincov" type="number" step="0.05" value="0" style="width:70px"></label>
  <button id="acceptHigh">Accept all high confidence</button>
  <button class="primary" id="export">Export review JSON</button>
</header>
<main id="list"></main>
<script id="data" type="application/json">__DATA__</script>
<script>
const DATA = JSON.parse(document.getElementById('data').textContent);
const PRESETS = __PRESETS__;
const STORE = 'material-rollout-review';
let decisions = {};
try { decisions = JSON.parse(localStorage.getItem(STORE) || '{}'); } catch (e) { decisions = {}; }
function save() { try { localStorage.setItem(STORE, JSON.stringify(decisions)); } catch (e) {} summary(); }
function esc(s) { return String(s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])); }
function dec(p) { return decisions[p.id] || {decision: '', preset: p.preset, scope: 'rooms', note: ''}; }
function summary() {
  const n = DATA.proposals.length, d = Object.values(decisions);
  document.getElementById('summary').textContent = `${DATA.rooms_surveyed} rooms surveyed` +
    (DATA.rooms_total ? ` of ${DATA.rooms_total}` : '') + ` · ${DATA.palettes} palettes · ${n} proposals · ` +
    `${d.filter(x => x.decision === 'accept').length} accepted, ${d.filter(x => x.decision === 'reject').length} rejected`;
}
function card(p) {
  const d = dec(p), el = document.createElement('section');
  el.className = 'card ' + (d.decision === 'accept' ? (d.preset !== p.preset ? 'change' : 'accept') : d.decision);
  const thumbs = p.textures.slice(0, 12).filter(t => t.thumb).map(t =>
    `<img loading="lazy" src="tex/${t.id}.png" title="${esc(t.key)} ${t.size[0]}x${t.size[1]} · ${t.coverage} in ${t.rooms} rooms">`).join('');
  const more = p.textures.length > 12 ? `<span class="more">+${p.textures.length - 12} more</span>` : '';
  const rooms = Object.entries(p.rooms);
  const roomList = rooms.map(([r, v]) => `${r} <i>${esc(p.room_names[r] || '')}</i> ${(v * 100).toFixed(1)}%`).join(' · ');
  const views = rooms.slice(0, 4).map(([r]) => `<img loading="lazy" src="views/${r}.jpg" title="${r} ${esc(p.room_names[r] || '')}">`).join('');
  const h = p.hints, l = p.look || {};
  const opts = [...PRESETS, 'none'].map(x => `<option ${x === d.preset ? 'selected' : ''}>${x}</option>`).join('');
  el.innerHTML = `
    <div class="thumbs">${thumbs || '<span class="more">no thumbnail</span>'}${more}</div>
    <div>
      <div class="title">${p.id} · ${p.kind === 'palette' ? 'palette ' + p.clut : 'texture ' + p.key + ' ' + p.size.join('x')}</div>
      <div><span class="chip">coverage ${p.coverage.toFixed(2)}</span><span class="chip">${rooms.length} rooms</span>
        <span class="chip">${p.textures.length} textures</span>
        ${p.in_catalog.length || p.covered_rooms.length ? `<span class="chip">in catalog: ${esc([...new Set([...p.in_catalog, ...p.entries_seen])].join(', '))} (${p.covered_rooms.length} of ${rooms.length} rooms)</span>` : ''}</div>
      <div>Proposed <b>${esc(p.preset)}</b> <span class="conf-${p.confidence}">(${p.confidence})</span>: ${esc(p.reason)}
        ${p.alternatives.length ? `<span class="look">· or ${esc(p.alternatives.join(', '))}</span>` : ''}</div>
      <div class="look">floor ${(h.floor * 100).toFixed(0)}% · wall ${(h.wall * 100).toFixed(0)}% · ceiling ${(h.ceiling * 100).toFixed(0)}% · objects ${(h.object * 100).toFixed(0)}%
        ${l.lum !== undefined ? ` · lum ${l.lum} · contrast ${l.contrast} · sat ${l.sat} · hue ${l.hue} · streak ${l.streak} · seams ${l.seams}` : ''}</div>
      <div class="views">${views}</div>
      <div class="rooms">${roomList}</div>
    </div>
    <div class="controls">
      <div class="row"><button class="acc ${d.decision === 'accept' ? 'on' : ''}" data-act="accept">Accept</button>
        <button class="rej ${d.decision === 'reject' ? 'on' : ''}" data-act="reject">Reject</button>
        <button data-act="clear">Undecided</button></div>
      <label>Preset <select data-field="preset">${opts}</select></label>
      <label>Scope <select data-field="scope"><option value="rooms" ${d.scope === 'rooms' ? 'selected' : ''}>each room (${rooms.length} entries)</option>
        ${p.kind === 'texture' ? `<option value="global" ${d.scope === 'global' ? 'selected' : ''}>everywhere (one entry)</option>` : ''}</select></label>
      <input class="note" data-field="note" placeholder="note / name" value="${esc(d.note || '')}">
    </div>`;
  el.querySelectorAll('button[data-act]').forEach(b => b.onclick = () => {
    const x = dec(p); x.decision = b.dataset.act === 'clear' ? '' : b.dataset.act; decisions[p.id] = x; save(); render();
  });
  el.querySelectorAll('[data-field]').forEach(f => f.onchange = () => {
    const x = dec(p); x[f.dataset.field] = f.value;
    if (f.dataset.field === 'preset' && !x.decision) x.decision = 'accept';
    decisions[p.id] = x; save(); render();
  });
  return el;
}
function render() {
  const f = document.getElementById('filter').value, min = parseFloat(document.getElementById('mincov').value) || 0;
  const list = document.getElementById('list'); list.innerHTML = '';
  for (const p of DATA.proposals) {
    const d = dec(p).decision;
    if (p.coverage < min) continue;
    if (f === 'open' && d) continue;
    if ((f === 'accept' || f === 'reject') && d !== f) continue;
    if (f === 'todo' && p.covered_rooms.length >= Object.keys(p.rooms).length) continue;
    list.appendChild(card(p));
  }
}
document.getElementById('filter').onchange = render;
document.getElementById('mincov').onchange = render;
document.getElementById('acceptHigh').onclick = () => {
  for (const p of DATA.proposals) if (p.confidence === 'high' && !dec(p).decision && p.preset !== 'none') {
    const x = dec(p); x.decision = 'accept'; decisions[p.id] = x; }
  save(); render();
};
document.getElementById('export').onclick = () => {
  const out = {format: 1, kind: 'material_rollout_review', exported: new Date().toISOString(), decisions: []};
  for (const p of DATA.proposals) {
    const d = decisions[p.id]; if (!d || !d.decision) continue;
    out.decisions.push({id: p.id, decision: d.decision, kind: p.kind, clut: p.clut, key: p.key, size: p.size,
      proposed: p.preset, preset: d.preset, changed: d.preset !== p.preset, scope: d.scope, note: d.note || '',
      rooms: Object.keys(p.rooms).map(r => r.split('-').map(Number)), room_names: p.room_names,
      coverage: p.coverage, reason: p.reason});
  }
  const blob = new Blob([JSON.stringify(out, null, 1)], {type: 'application/json'});
  const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = 'material_review.json'; a.click();
};
summary(); render();
</script></body></html>
"""


def page(data):
    blob = json.dumps(data).replace("</", "<\\/")
    return PAGE.replace("__DATA__", blob).replace("__PRESETS__", json.dumps(PRESETS))


def cmd_report(args):
    out = pathlib.Path(args.out)
    data = build(out, args.catalog, load_rules(args.rules))
    (out / "review.html").write_text(page(data))
    q = data["proposals"]
    print(f"{data['rooms_surveyed']} rooms, {data['textures']} textures, {data['palettes']} palettes, "
          f"{len(q)} proposals")
    print(f"{'id':6} {'cover':>6} {'rooms':>5}  {'palette/texture':22} {'preset':14} {'conf':6} catalog")
    for p in q[:args.top]:
        what = p["clut"] if p["kind"] == "palette" else f"{p['key']} {p['size'][0]}x{p['size'][1]}"
        print(f"{p['id']:6} {p['coverage']:6.2f} {len(p['rooms']):5d}  {what:22} {p['preset']:14} "
              f"{p['confidence']:6} {','.join(p['in_catalog']) or '-'}")
    dist = {}
    for p in q:
        d = dist.setdefault(p["preset"], {"n": 0, "coverage": 0.0, "high": 0, "medium": 0, "low": 0})
        d["n"] += 1
        d["coverage"] += p["coverage"]
        d[p["confidence"]] += 1
    print("presets (proposals, coverage, high/medium/low):")
    for name, d in sorted(dist.items(), key=lambda kv: -kv[1]["coverage"]):
        print(f"  {name:14} {d['n']:4d} {d['coverage']:7.2f}  {d['high']}/{d['medium']}/{d['low']}")
    print(f"wrote {out / 'proposals.json'} and {out / 'review.html'} (game art: keep local)")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(OUT), help="the rollout's folder (default pc/build/material_rollout)")
    sub = parser.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("survey", help="survey rooms (resumable)")
    s.add_argument("--until", help="park at HH:MM local time: start no room after it")
    s.add_argument("--zone", type=int, action="append", help="only this zone (repeatable)")
    s.add_argument("--rooms", help="only these rooms: Z/R,Z/R")
    s.add_argument("--max", type=int, default=0, help="at most this many rooms this run")
    s.add_argument("--views", default="game,left,right,back")
    s.add_argument("--turn", type=float, default=30)
    s.add_argument("--grid", type=int, default=160)
    s.add_argument("--restart-every", type=int, default=60, help="relaunch the game every N rooms (default 60)")
    s.add_argument("--max-launches", type=int, default=40)
    s.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    r = sub.add_parser("report", help="aggregate, propose, write the review page")
    r.add_argument("--catalog", default=str(CATALOG))
    r.add_argument("--top", type=int, default=25)
    r.add_argument("--rules", default=str(RULES), help="the proposals' tunables (default pc/tools/material_rollout_rules.json)")
    args = parser.parse_args(argv)
    return cmd_survey(args) if args.cmd == "survey" else cmd_report(args)


if __name__ == "__main__":
    sys.exit(main())
