#!/usr/bin/env python3
"""A before/after review gallery of generated lighting, one zone at a time.

    python3 pc/tools/lighting_gallery.py --zone 9 --out pc/build/gallery/zone09
    python3 pc/tools/lighting_gallery.py --zone 9 --room 0 --room 5 --out DIR
    ROOD_ARCH=64 ROOD_RENDERER=vulkan python3 pc/tools/lighting_gallery.py --zone 9 --out DIR

Continues a copy of the memory card's save (--card, default pc/save/card1.mcd)
headless, then warps (RPC room_goto) into every room of the zone that a
door leads to and captures it twice: without the packs (before: the game's
own lighting) and with them (after: the generated layer and the authored
one), from the game's camera and from an editor overview above the room,
and a close-up of every generated light actor. Writes DIR/index.html (rooms,
counts, images), DIR/types.html and DIR/contact/<type>.png (every instance of
each fixture type in the zone, before | after), so a type can be judged
across all its instances at once.

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
CLOSE = 150.0  # close-up distance (units)


def pose_looking_at(eye, target):
    d = [target[i] - eye[i] for i in range(3)]
    yaw = math.degrees(math.atan2(d[0], d[2]))
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, -d[1] / (math.sqrt(sum(x * x for x in d)) or 1)))))
    return dict(x=eye[0], y=eye[1], z=eye[2], yaw=yaw, pitch=pitch)


def capture(game, path, width):
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
    im.save(path)
    return im


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
    return {"lo": lo.tolist(), "hi": hi.tolist(), "actors": actors, "glows": len(glows)}


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
    eye = p + fwd * CLOSE + np.array([0, -50, 0])
    return pose_looking_at(eye.tolist(), (p + np.array([0, 10, 0])).tolist())


def warp(game, zone, room):
    reply = game.call("room_goto", zone=zone, room=room)
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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--zone", type=int, action="append", required=True)
    ap.add_argument("--room", type=int, action="append")
    ap.add_argument("--out", required=True, help="the gallery's directory (local; game imagery)")
    ap.add_argument("--root", default=str(ROOT / "lighting"), help="the packs' directory")
    ap.add_argument("--pack", default="default")
    ap.add_argument("--card", default=None)
    ap.add_argument("--window", default="1024x768")
    ap.add_argument("--no-closeups", action="store_true")
    ap.add_argument("--settle", type=int, default=SETTLE)
    args = ap.parse_args(argv)
    from rood_rpc import Game
    from PIL import Image, ImageDraw
    card = pathlib.Path(args.card) if args.card else ROOT / "pc/save/card1.mcd"
    if not card.exists():
        sys.path.insert(0, str(ROOT / "pc/tests"))
        from editor_cutaway_smoke import default_card
        card = default_card()
    if not card.exists():
        print("no memory card (--card): the gallery continues a save")
        return 1
    out = pathlib.Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / "img").mkdir(exist_ok=True)
    (out / "contact").mkdir(exist_ok=True)
    work = out / "work"
    shutil.rmtree(work, ignore_errors=True)
    (work / "user").mkdir(parents=True)
    shutil.copyfile(card, work / "card1.mcd")
    cat = fl.load_catalog()
    types = {t["id"]: t for t in cat["types"]}
    disc = fl.Disc()
    generated = pathlib.Path(args.root) / args.pack / "generated"
    plans = {}
    for zone in args.zone:
        vram, rooms = fl.zone_vram(disc, zone)
        for room, name in enumerate(rooms):
            if name and (not args.room or room in args.room):
                plans[(zone, room)] = room_plan(disc, vram, zone, room, name, generated)
    env = {"ROOD_LIGHTS": str(work / "user"), "ROOD_LIGHTING": str(pathlib.Path(args.root).resolve()), "ROOD_GI": "1",
           "ROOD_WINDOW": args.window, "ROOD_SCALE": "3"}
    socket = f"/tmp/rood-gallery-{os.getpid()}.sock"
    game = Game.launch(fast=True, card=str(work / "card1.mcd"), log=work / "game.log", extra_env=env,
                       socket_path=socket)
    results = []
    t0 = time.time()
    try:
        game.wait_title()
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        game.call("graphics", gi=True)
        listed = {(r["zone"], r["room"]): r for r in game.call("rooms")["rooms"]}
        for (zone, room), plan in sorted(plans.items()):
            info = listed.get((zone, room), {})
            res = {"zone": zone, "room": room, "name": info.get("name") or "", "area": info.get("area") or "",
                   "actors": plan["actors"], "glows": plan["glows"], "images": {}, "closeups": {}}
            results.append(res)
            ok, message = warp(game, zone, room)
            if not ok:
                res["error"] = f"not reached ({message or 'no door leads in'})"
                print(f"zone {zone} room {room}: {res['error']}")
                continue
            fixtures = game.call("fixtures")
            res["live"] = {"actors": len(fixtures["actors"]), "glows": fixtures["glows"]}
            tag = f"z{zone:02d}r{room:02d}"
            for phase, packs in (("before", ""), ("after", args.pack)):
                game.call("lighting", action="packs", packs=packs)
                game.call("editor_camera", off=1)
                game.run(args.settle)
                capture(game, out / "img" / f"{tag}-{phase}-game.png", 960)
                game.call("editor_camera", **overview_pose(plan))
                game.run(args.settle // 2)
                capture(game, out / "img" / f"{tag}-{phase}-overview.png", 960)
                res["images"][phase] = [f"img/{tag}-{phase}-game.png", f"img/{tag}-{phase}-overview.png"]
                if not args.no_closeups:
                    for k, a in enumerate(plan["actors"]):
                        game.call("editor_camera", **closeup_pose(plan, a))
                        game.run(24)
                        name = f"img/{tag}-a{k:02d}-{phase}.png"
                        capture(game, out / name, 320)
                        res["closeups"].setdefault(k, {})[phase] = name
            game.call("editor_camera", off=1)
            print(f"zone {zone} room {room}: {len(plan['actors'])} actors, {plan['glows']} typed glows "
                  f"({time.time() - t0:.0f} s)")
        game.call("room_goto", action="return")
        game.run(200)
    finally:
        game.close()
    write_html(out, results, types, cat)
    contact_sheets(out, results, types)
    (out / "gallery.json").write_text(json.dumps(results, indent=1, default=str))
    shutil.rmtree(work / "user", ignore_errors=True)
    print(f"gallery: {out / 'index.html'} ({time.time() - t0:.0f} s)")
    return 0


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
        parts.append(f"<p>{len(r['actors'])} generated actors, {r['glows']} typed glows"
                     + (f"; in game: {r['live']['actors']} actors, "
                        f"{sum(1 for g in r['live']['glows'] if g['muted'] == 4)} glows suppressed" if r.get("live") else "")
                     + "</p><table>" + "".join(f"<tr><td>{n}x</td><td>{type_label(t)}</td></tr>"
                                                for t, n in sorted(counts.items())) + "</table>")
        if r.get("error"):
            parts.append(f"<p class=dim>{esc(r['error'])}</p>")
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
    # per type
    by_type = {}
    for r in results:
        for k, c in r["closeups"].items():
            a = r["actors"][int(k)]
            by_type.setdefault(a["type"], []).append((r, a, c))
    parts = [f"<!doctype html><meta charset=utf-8><title>Lighting gallery: types</title>{STYLE}",
             "<h1>Fixture types: every instance (before | after)</h1><p><a href=index.html>Rooms</a></p>"]
    for tid in sorted(by_type):
        parts.append(f"<h2>{type_label(tid)}</h2><p><a href=contact/{tid}.png>contact sheet</a> "
                     f"({len(by_type[tid])} instances here)</p><div class=grid>")
        for r, a, c in by_type[tid]:
            parts.append(f"<div class=cu><div><img src={c.get('before', '')} loading=lazy> <img src={c.get('after', '')} "
                         f"loading=lazy></div><span class=dim>zone {r['zone']} room {r['room']} {esc(a['id'])}</span></div>")
        parts.append("</div>")
    (out / "types.html").write_text("\n".join(parts))


def contact_sheets(out, results, types):
    from PIL import Image, ImageDraw
    by_type = {}
    for r in results:
        for k, c in r["closeups"].items():
            a = r["actors"][int(k)]
            if "before" in c and "after" in c:
                by_type.setdefault(a["type"], []).append((f"{r['zone']}/{r['room']}", c))
    for tid, items in by_type.items():
        cols = 3
        w, h = 320, 240
        rows = (len(items) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * (2 * w + 12), rows * (h + 18) + 24), (20, 20, 24))
        d = ImageDraw.Draw(sheet)
        t = types.get(tid, {})
        d.text((6, 4), f"{tid} {t.get('class', '')}: before | after", fill=(240, 200, 120))
        for i, (label, c) in enumerate(items):
            x, y = (i % cols) * (2 * w + 12), 24 + (i // cols) * (h + 18)
            for j, phase in enumerate(("before", "after")):
                im = Image.open(out / c[phase]).resize((w, h))
                sheet.paste(im, (x + j * w, y))
            d.text((x + 4, y + h + 2), label, fill=(220, 220, 220))
        sheet.save(out / "contact" / f"{tid}.png")


if __name__ == "__main__":
    sys.exit(main())
