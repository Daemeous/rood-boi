#!/usr/bin/env python3
"""A before/after review gallery of generated lighting.

    python3 pc/tools/lighting_gallery.py --zone 9 --out pc/build/gallery/zone09
    python3 pc/tools/lighting_gallery.py --zone 9 --room 0 --room 5 --out DIR
    python3 pc/tools/lighting_gallery.py --all --out DIR --resume     every room with generated lighting
    ROOD_ARCH=64 ROOD_RENDERER=vulkan python3 pc/tools/lighting_gallery.py --zone 9 --out DIR

Continues a copy of the memory card's save (--card, default pc/save/card1.mcd)
headless, then warps (RPC room_goto, enemies off) into every room that a
door leads to and captures it twice: without the packs (before: the game's
own lighting) and with them (after: the generated layer and the authored
one), from the game's camera and from an editor overview above the room,
and close-ups of generated light actors (--closeups N per type and zone;
0: none). If the game dies the room is reported and the next one continues
in a fresh game; --resume skips rooms already captured (DIR/gallery.json).

Writes plain JPEG pages that open anywhere: DIR/pages/zoneZZ_N.jpg (rooms:
label, game camera before | after, overview before | after),
DIR/pages/types/Txxx.jpg (a fixture type's close-ups across the game,
before | after) and DIR/issues.txt (rooms to look at by hand: over the
renderers' light budget, much brighter or darker than before, clipped,
not reached, flame counts that differ from the type). DIR/index.html and
types.html show the same images in a browser.

The images are game imagery: they stay local (keep DIR under pc/build or a
scratch directory) and are never committed. The game's room names are read
from the disc at run time (RPC rooms).
"""
import argparse
import html
import json
import math
import os
import pathlib
import shutil
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixture_lib as fl  # noqa: E402
import lighting_format as lf  # noqa: E402

ROOT = fl.ROOT
SETTLE = 90  # vsyncs for the lights and the path tracer to settle after a change
CLOSE = 260.0  # close-up distance (units)


def pose_looking_at(eye, target):
    d = [target[i] - eye[i] for i in range(3)]
    yaw = math.degrees(math.atan2(d[0], d[2]))
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, -d[1] / (math.sqrt(sum(x * x for x in d)) or 1)))))
    return dict(x=eye[0], y=eye[1], z=eye[2], yaw=yaw, pitch=pitch)


def capture(game, path, width):
    """The presented frame, scaled to `width`, saved as JPEG (or PNG by suffix)."""
    from PIL import Image
    ppm = pathlib.Path(path).with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    game.call("present_dump", path=str(ppm.resolve()))
    for _ in range(8):
        game.run(1)
        if ppm.exists() and ppm.stat().st_size:
            break
    time.sleep(0.05)
    im = Image.open(ppm).convert("RGB")
    ppm.unlink()
    if im.width > width:
        im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
    im.save(path, quality=88)
    return im


def image_stats(im):
    """Mean luminance (0-255) and the share of clipped pixels."""
    a = np.asarray(im.convert("L"), np.float32)
    return {"mean": round(float(a.mean()), 2), "clipped": round(float((a >= 250).mean()), 4)}


def room_plan(disc, vram, zone, room, name, generated):
    """Offline: the room's bounds and its generated actors (id, type, position, yaw)."""
    r = fl.load_room(disc, zone, room, vram, name)
    pts = np.concatenate([p["V"] for p in r["polys"]]) if r["polys"] else np.zeros((1, 3))
    lo, hi = np.percentile(pts, 2, axis=0), np.percentile(pts, 98, axis=0)
    actors = []
    f = lf.read_file(str(generated / lf.room_file_name(zone, room)))
    for e in f.entries:
        if e.verb != "define" or e.item.kind != "actor":
            continue
        pos = [float(x) for x in e.item.get("pos").split(",")]
        rot = [float(x) for x in (e.item.get("rot") or "0,0,0").split(",")]
        actors.append({"id": e.id, "type": e.item.get("fixture_type"), "pos": pos, "yaw": rot[0],
                       "enabled": e.item.get("enabled") != "0", "prefab": e.item.get("prefab")})
    glows = [e for e in f.entries if e.verb == "define" and e.item.kind == "glow"]
    return {"lo": lo.tolist(), "hi": hi.tolist(), "actors": actors, "glows": len(glows), "cell": floor_cell(r, lo, hi)}


def floor_cell(r, lo, hi):
    """A floor cell (128 units) near the room's middle: where a made-up door
    puts Ashley in a room no door leads into (RPC room_goto {cell})."""
    c = (np.asarray(lo) + np.asarray(hi)) / 2
    best = None
    for p in r["polys"]:
        if p["area"] < 2000 or p["normal"][1] > -0.8 and p["normal"][1] < 0.8:
            continue
        m = p["V"].mean(0)
        if not (0 <= m[0] < 4096 and 0 <= m[2] < 4096):  # a door's cell is 5 bits each way
            continue
        d =(m[0] - c[0]) ** 2 + (m[2] - c[2]) ** 2
        if best is None or d < best[0]:
            best = (d, m)
    m = best[1] if best is not None else c
    return [int(max(0, min(31, m[0] // 128))), 0, int(max(0, min(31, m[2] // 128))), 0]


def overview_pose(plan):
    lo, hi = np.array(plan["lo"]), np.array(plan["hi"])
    c = (lo + hi) / 2
    diag = float(np.linalg.norm(hi - lo))
    d = np.array([-0.55, -0.75, -0.55])
    d /= np.linalg.norm(d)
    eye = c + d * max(diag * 0.75, 400)
    return pose_looking_at(eye.tolist(), c.tolist())


def closeup_pose(plan, a):
    """From the room's side of the fixture (a wall fixture faces into the room)."""
    p = np.array(a["pos"])
    c = (np.array(plan["lo"]) + np.array(plan["hi"])) / 2
    yaw = math.radians(a["yaw"])
    fwd = np.array([math.sin(yaw), 0, math.cos(yaw)])
    to_centre = c - p
    to_centre[1] = 0
    if a["yaw"] == 0 or np.linalg.norm(to_centre) < 1:
        fwd = to_centre / (np.linalg.norm(to_centre) or 1)
    elif fwd @ to_centre < 0:
        fwd = -fwd
    eye = p + fwd * CLOSE + np.array([0, -90, 0])
    return pose_looking_at(eye.tolist(), (p + np.array([0, 10, 0])).tolist())


def warp(game, zone, room, cell=None):
    reply = game.call("room_goto", zone=zone, room=room, enemies=False)
    if not reply.get("ok") and cell and "no door" in reply.get("message", ""):
        reply = game.call("room_goto", zone=zone, room=room, enemies=False, cell=cell)  # a made-up door
    if not reply.get("ok"):
        return False, reply.get("message", "refused")
    status = {}
    for _ in range(90):
        game.run(20)
        status = game.call("room_goto", action="status")
        if status["phase"] in (0, 4):
            break
    game.run(30)
    st = game.state()
    return (st["zone"], st["room"]) == (zone, room), status.get("message", "")


def pick_closeups(plans, per_type):
    """Which actors get close-ups: up to per_type of each type in each zone
    (spread over its rooms), so every type is seen everywhere it occurs."""
    chosen, seen = set(), {}
    for (zone, room), plan in sorted(plans.items()):
        for k, a in enumerate(plan["actors"]):
            n = seen.get((zone, a["type"]), 0)
            if n < per_type:
                seen[(zone, a["type"])] = n + 1
                chosen.add((zone, room, k))
    return chosen


def start_game(args, work, env):
    from rood_rpc import Game
    socket = f"/tmp/rood-gallery-{os.getpid()}.sock"
    game = Game.launch(fast=True, card=str(work / "card1.mcd"), log=work / "game.log", extra_env=env,
                       socket_path=socket)
    game.sock.settimeout(300)  # a hung game fails the room instead of the whole run
    game.wait_title()
    game.press("start", vsyncs=4, settle=90)
    for _ in range(3):  # Continue, the card, its first save
        game.press("circle", vsyncs=3, settle=90)
    game.run(400)
    game.call("graphics", gi=True)
    return game


def capture_room(game, args, out, plan, zone, room, closeups, res):
    ok, message = warp(game, zone, room, plan.get("cell"))
    if not ok:
        res["error"] = f"not reached ({message or 'no door leads in'})"
        return
    fixtures = game.call("fixtures")
    res["live"] = {"actors": len(fixtures["actors"]),
                   "suppressed": sum(1 for g in fixtures["glows"] if g["muted"] == 4)}
    tag = f"z{zone:02d}r{room:02d}"
    for phase, packs in (("before", ""), ("after", args.pack)):
        game.call("lighting", action="packs", packs=packs)
        game.call("editor_camera", off=1)
        game.run(args.settle)
        im = capture(game, out / "img" / f"{tag}-{phase}-game.jpg", 960)
        res["stats"][phase] = image_stats(im)
        if phase == "after":
            live = game.call("actors")
            res["live"]["lights"] = live.get("lights_wanted", 0)
            res["live"]["dropped"] = live.get("dropped", 0)
        game.call("editor_camera", **overview_pose(plan))
        game.run(args.settle // 2)
        im = capture(game, out / "img" / f"{tag}-{phase}-overview.jpg", 960)
        res["stats"][phase + "_overview"] = image_stats(im)
        res["images"][phase] = [f"img/{tag}-{phase}-game.jpg", f"img/{tag}-{phase}-overview.jpg"]
        for k, a in enumerate(plan["actors"]):
            if (zone, room, k) not in closeups:
                continue
            game.call("editor_camera", **closeup_pose(plan, a))
            game.run(24)
            name = f"img/{tag}-a{k:02d}-{phase}.jpg"
            capture(game, out / name, 320)
            res["closeups"].setdefault(str(k), {})[phase] = name
    game.call("editor_camera", off=1)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--zone", type=int, action="append")
    ap.add_argument("--all", action="store_true", help="every zone with generated lighting")
    ap.add_argument("--room", type=int, action="append")
    ap.add_argument("--out", required=True, help="the gallery's directory (local; game imagery)")
    ap.add_argument("--root", default=str(ROOT / "lighting"), help="the packs' directory")
    ap.add_argument("--pack", default="default")
    ap.add_argument("--card", default=None)
    ap.add_argument("--window", default="1024x768")
    ap.add_argument("--closeups", type=int, default=99, help="close-ups per type and zone (0: none)")
    ap.add_argument("--no-closeups", action="store_true")
    ap.add_argument("--settle", type=int, default=SETTLE)
    ap.add_argument("--resume", action="store_true", help="keep rooms already captured in DIR")
    ap.add_argument("--pages-only", action="store_true", help="only rewrite the pages from DIR/gallery.json")
    args = ap.parse_args(argv)
    if not args.zone and not args.all:
        ap.error("--zone or --all")
    card = pathlib.Path(args.card) if args.card else ROOT / "pc/save/card1.mcd"
    if not card.exists():
        sys.path.insert(0, str(ROOT / "pc/tests"))
        from editor_cutaway_smoke import default_card
        card = default_card()
    out = pathlib.Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / "img").mkdir(exist_ok=True)
    cat = fl.load_catalog()
    types = {t["id"]: t for t in cat["types"]}
    state_path = out / "gallery.json"
    done = {}
    if (args.resume or args.pages_only) and state_path.exists():
        for r in json.loads(state_path.read_text()):
            done[(r["zone"], r["room"])] = r
    if args.pages_only:
        results = [done[k] for k in sorted(done)]
        write_all(out, results, types, cat)
        return 0
    if not card.exists():
        print("no memory card (--card): the gallery continues a save")
        return 1
    work = out / "work"
    shutil.rmtree(work, ignore_errors=True)
    (work / "user").mkdir(parents=True)
    shutil.copyfile(card, work / "card1.mcd")
    disc = fl.Disc()
    generated = pathlib.Path(args.root) / args.pack / "generated"
    zones = args.zone or sorted({int(p.name[4:6]) for p in generated.glob("zone*_room*.txt")})
    plans = {}
    for zone in zones:
        vram, rooms = fl.zone_vram(disc, zone)
        for room, name in enumerate(rooms):
            if name and (not args.room or room in args.room) and (generated / lf.room_file_name(zone, room)).exists():
                plans[(zone, room)] = room_plan(disc, vram, zone, room, name, generated)
    closeups = set() if args.no_closeups or args.closeups <= 0 else pick_closeups(plans, args.closeups)
    env = {"ROOD_LIGHTS": str(work / "user"), "ROOD_LIGHTING": str(pathlib.Path(args.root).resolve()), "ROOD_GI": "1",
           "ROOD_WINDOW": args.window, "ROOD_SCALE": "3"}
    from rood_rpc import RpcError
    results = {k: v for k, v in done.items() if k in plans or not args.zone}
    t0 = time.time()
    game = None
    names = {}
    try:
        for (zone, room), plan in sorted(plans.items()):
            if (zone, room) in results and not results[(zone, room)].get("error"):
                continue
            res = {"zone": zone, "room": room, "name": "", "area": "", "actors": plan["actors"],
                   "glows": plan["glows"], "images": {}, "closeups": {}, "stats": {}}
            for attempt in range(2):
                try:
                    if game is None:
                        game = start_game(args, work, env)
                        names = {(r["zone"], r["room"]): r for r in game.call("rooms")["rooms"]}
                    info = names.get((zone, room), {})
                    res["name"], res["area"] = info.get("name") or "", info.get("area") or ""
                    res.pop("error", None)
                    if attempt:  # a made-up door's cell can be outside the room's map: another one
                        plan = dict(plan, cell=[8, 0, 8, 0])
                    capture_room(game, args, out, plan, zone, room, closeups, res)
                    break
                except (RpcError, OSError, ValueError) as error:  # the game died or hung: a fresh one
                    res["error"] = f"game failed here ({error})"
                    print(f"zone {zone} room {room}: {res['error']}; restarting the game")
                    try:
                        game.close()
                    except Exception:
                        pass
                    game = None
            results[(zone, room)] = res
            print(f"zone {zone} room {room}: {len(plan['actors'])} actors, {plan['glows']} typed glows"
                  + (f" ({res['error']})" if res.get("error") else "") + f" ({time.time() - t0:.0f} s)", flush=True)
            state_path.write_text(json.dumps([results[k] for k in sorted(results)], indent=1, default=str))
    finally:
        if game is not None:
            try:
                game.call("room_goto", action="return")
                game.run(200)
            except Exception:
                pass
            game.close()
    ordered = [results[k] for k in sorted(results)]
    state_path.write_text(json.dumps(ordered, indent=1, default=str))
    write_all(out, ordered, types, cat)
    shutil.rmtree(work / "user", ignore_errors=True)
    print(f"gallery: {out / 'pages'} and {out / 'index.html'} ({time.time() - t0:.0f} s)")
    return 0


def write_all(out, results, types, cat):
    write_html(out, results, types, cat)
    pages(out, results)
    type_pages(out, results, types)
    issues(out, results, types, cat)


def esc(s):
    return html.escape(str(s))


STYLE = """<style>
:root{--bg:#16171b;--fg:#e7e7ea;--dim:#9a9aa3;--card:#202127;--accent:#f2b45a}
@media (prefers-color-scheme: light){:root{--bg:#f6f6f7;--fg:#1d1d22;--dim:#60606a;--card:#fff;--accent:#a8650a}}
body{background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif;margin:0 auto;max-width:1980px;padding:16px}
h1,h2,h3{font-weight:600} h2{margin-top:2em;border-bottom:1px solid var(--dim)} .dim{color:var(--dim)}
.pair{display:grid;grid-template-columns:1fr 1fr;gap:8px} .pair img{width:100%;image-rendering:auto}
.card{background:var(--card);padding:10px;border-radius:8px;margin:10px 0}
.grid{display:flex;flex-wrap:wrap;gap:8px} .cu{background:var(--card);padding:6px;border-radius:6px;width:660px}
.cu img{width:320px} code{color:var(--accent)} table{border-collapse:collapse} td,th{padding:2px 8px;text-align:left}
nav a{margin-right:10px}
</style>"""


def write_html(out, results, types, cat):
    def type_label(tid):
        t = types.get(tid)
        if not t:
            return esc(tid)
        f = fl.type_fields(cat, t)
        return (f"<code>{esc(tid)}</code> {esc(t['class'])} <span class=dim>({esc(f['prefab'])}, glow {esc(f['glow'])}"
                f"{', off by default' if not f['enabled'] else ''}; review {esc(t.get('review', ''))})</span>")
    parts = [f"<!doctype html><meta charset=utf-8><title>Lighting gallery</title>{STYLE}",
             "<h1>Generated lighting: before / after</h1>",
             "<p class=dim>Left: the game's own lighting (no packs). Right: with the packs (generated fixture lights). "
             "Game camera, then an editor overview. Local only: game imagery, never commit. "
             "<a href=types.html>Per-type sheets</a></p><nav>"]
    for r in results:
        parts.append(f"<a href=#z{r['zone']}r{r['room']}>{r['zone']}/{r['room']}</a>")
    parts.append("</nav>")
    for r in results:
        parts.append(f"<h2 id=z{r['zone']}r{r['room']}>Zone {r['zone']} room {r['room']}: {esc(r['name'])} "
                     f"<span class=dim>{esc(r['area'])}</span></h2>")
        counts = {}
        for a in r["actors"]:
            counts[a["type"]] = counts.get(a["type"], 0) + 1
        live = r.get("live") or {}
        parts.append(f"<p>{len(r['actors'])} generated actors, {r['glows']} typed glows"
                     + (f"; in game: {live.get('actors')} actors, {live.get('suppressed')} glows suppressed, "
                        f"{live.get('dropped', 0)} lights over the budget" if live else "")
                     + "</p><table>" + "".join(f"<tr><td>{n}x</td><td>{type_label(t)}</td></tr>"
                                                for t, n in sorted(counts.items())) + "</table>")
        if r.get("error") or "after" not in r["images"]:
            parts.append(f"<p class=dim>{esc(r.get('error', 'no images'))}</p>")
            continue
        for i in range(2):
            parts.append("<div class='pair card'>"
                         f"<a href={r['images']['before'][i]}><img src={r['images']['before'][i]} loading=lazy></a>"
                         f"<a href={r['images']['after'][i]}><img src={r['images']['after'][i]} loading=lazy></a></div>")
        if r["closeups"]:
            parts.append("<div class=grid>")
            for k, c in sorted(r["closeups"].items(), key=lambda kv: int(kv[0])):
                a = r["actors"][int(k)]
                parts.append(f"<div class=cu><div><img src={c.get('before', '')} loading=lazy> "
                             f"<img src={c.get('after', '')} loading=lazy></div>{type_label(a['type'])} "
                             f"<span class=dim>{esc(a['id'])}{'' if a['enabled'] else ' (off)'}</span></div>")
            parts.append("</div>")
    (out / "index.html").write_text("\n".join(parts))
    parts = [f"<!doctype html><meta charset=utf-8><title>Lighting gallery: types</title>{STYLE}",
             "<h1>Fixture types: every close-up (before | after)</h1><p><a href=index.html>Rooms</a></p>"]
    by_type = closeups_by_type(results)
    for tid in sorted(by_type):
        parts.append(f"<h2>{type_label(tid)}</h2><p><a href=pages/types/{tid}.jpg>contact page</a> "
                     f"({len(by_type[tid])} close-ups)</p><div class=grid>")
        for r, a, c in by_type[tid]:
            parts.append(f"<div class=cu><div><img src={c.get('before', '')} loading=lazy> <img src={c.get('after', '')} "
                         f"loading=lazy></div><span class=dim>zone {r['zone']} room {r['room']} {esc(a['id'])}</span></div>")
        parts.append("</div>")
    (out / "types.html").write_text("\n".join(parts))


def closeups_by_type(results):
    by_type = {}
    for r in results:
        for k, c in sorted(r.get("closeups", {}).items(), key=lambda kv: int(kv[0])):
            a = r["actors"][int(k)]
            if "before" in c and "after" in c:
                by_type.setdefault(a["type"], []).append((r, a, c))
    return by_type


def font(size):
    from PIL import ImageFont
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return ImageFont.load_default()


PAGE_ROOMS = 3  # rooms per page: each 2 x 640 wide, game camera and overview rows
TILE = (640, 480)


def room_label(r):
    live = r.get("live") or {}
    on = sum(1 for a in r["actors"] if a["enabled"])
    s = f"zone {r['zone']} room {r['room']}"
    if r.get("name"):
        s += f"  {r['name']}"
    if r.get("area"):
        s += f"  ({r['area']})"
    s += f"   {len(r['actors'])} actors ({on} on), {r['glows']} typed glows"
    if live.get("dropped"):
        s += f", {live['dropped']} lights over the budget"
    return s


def pages(out, results):
    """pages/zoneZZ_N.jpg: rooms with label, before | after (game camera, then overview)."""
    from PIL import Image, ImageDraw
    pdir = out / "pages"
    pdir.mkdir(exist_ok=True)
    for old in pdir.glob("zone*_*.jpg"):
        old.unlink()
    f_label, f_small = font(22), font(16)
    by_zone = {}
    for r in results:
        by_zone.setdefault(r["zone"], []).append(r)
    w, h = TILE
    head = 34
    for zone, rooms in sorted(by_zone.items()):
        for p in range(0, len(rooms), PAGE_ROOMS):
            chunk = rooms[p:p + PAGE_ROOMS]
            heights = [head + (2 * h + 4 if "after" in r["images"] else 40) for r in chunk]
            sheet = Image.new("RGB", (2 * w + 4, sum(heights) + 8 * len(chunk) + 26), (18, 18, 22))
            d = ImageDraw.Draw(sheet)
            d.text((6, 4), f"zone {zone}: before (game lighting) | after (generated fixture lights)",
                   fill=(160, 160, 170), font=f_small)
            y = 26
            for r, hh in zip(chunk, heights):
                d.rectangle([0, y, 2 * w + 4, y + head - 4], fill=(40, 36, 30))
                d.text((8, y + 4), room_label(r), fill=(242, 190, 110), font=f_label)
                if "after" in r["images"]:
                    for row in range(2):
                        for col, phase in enumerate(("before", "after")):
                            im = Image.open(out / r["images"][phase][row]).convert("RGB").resize((w, h), Image.LANCZOS)
                            sheet.paste(im, (col * (w + 4), y + head + row * (h + 4)))
                else:
                    d.text((8, y + head + 6), r.get("error", "not captured"), fill=(220, 120, 120), font=f_small)
                y += hh + 8
            sheet.save(pdir / f"zone{zone:02d}_{p // PAGE_ROOMS + 1}.jpg", quality=80, optimize=True)


def type_pages(out, results, types):
    """pages/types/Txxx.jpg: a type's close-ups across the game (before | after)."""
    from PIL import Image, ImageDraw
    tdir = out / "pages" / "types"
    tdir.mkdir(parents=True, exist_ok=True)
    for old in tdir.glob("*.jpg"):
        old.unlink()
    f_label, f_small = font(20), font(14)
    w, h = 320, 240
    for tid, items in closeups_by_type(results).items():
        items = items[:36]
        cols = 3
        rows = (len(items) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * (2 * w + 12), rows * (h + 22) + 32), (18, 18, 22))
        d = ImageDraw.Draw(sheet)
        t = types.get(tid, {})
        d.text((6, 4), f"{tid} {t.get('class', '')} ({t.get('instances', '?')} on the disc, zones {t.get('zones', '')}):"
                       " before | after", fill=(242, 190, 110), font=f_label)
        for i, (r, a, c) in enumerate(items):
            x, y = (i % cols) * (2 * w + 12), 32 + (i // cols) * (h + 22)
            for j, phase in enumerate(("before", "after")):
                im = Image.open(out / c[phase]).convert("RGB").resize((w, h))
                sheet.paste(im, (x + j * w, y))
            d.text((x + 4, y + h + 2), f"zone {r['zone']} room {r['room']}{'' if a['enabled'] else ' (off)'}",
                   fill=(220, 220, 220), font=f_small)
        sheet.save(tdir / f"{tid}.jpg", quality=80, optimize=True)


BUDGET = 16  # lights the renderers take


def issues(out, results, types, cat):
    """issues.txt: rooms worth a look by hand, most notable first."""
    lines = []
    for r in results:
        why = []
        if r.get("error"):
            why.append(r["error"])
        live = r.get("live") or {}
        if live.get("dropped"):
            why.append(f"{live['dropped']} lights over the renderers' budget of {BUDGET}")
        st = r.get("stats", {})
        for view in ("", "_overview"):
            b, a = st.get("before" + view), st.get("after" + view)
            if not (a and b):
                continue
            name = "game camera" if not view else "overview"
            ratio = (a["mean"] + 4) / (b["mean"] + 4)
            if ratio > 1.6:
                why.append(f"{name} {ratio:.2f}x brighter than the game's lighting")
            elif ratio < 0.85:
                why.append(f"{name} darker than the game's lighting ({ratio:.2f}x)")
            if a["clipped"] > 0.03 and a["clipped"] > 2 * b["clipped"] + 0.01:
                why.append(f"{name}: {a['clipped'] * 100:.1f}% of pixels clipped (before {b['clipped'] * 100:.1f}%)")
        if why:
            lines.append((r["zone"], r["room"], r.get("name", ""), why))
    text = ["Rooms to check by hand (pages/zoneZZ_N.jpg; per type: pages/types/Txxx.jpg)", ""]
    for z, rm, name, why in lines:
        text.append(f"zone {z} room {rm} {name}:")
        text += [f"    - {w}" for w in why]
    text += ["", f"{len(results)} rooms captured, {len(lines)} flagged"]
    (out / "issues.txt").write_text("\n".join(text) + "\n")


if __name__ == "__main__":
    sys.exit(main())
