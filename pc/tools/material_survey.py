#!/usr/bin/env python3
"""Survey a room's textures for materials (pc/docs/Materials.md, "Surveying a room").

    python3 pc/tools/material_survey.py [ZONE ROOM] [--views game,left,right] [--out DIR]

Starts its own headless game (never a running one: its own RPC socket, its
own scratch lighting and material layers; $ROOD_RENDERER picks the renderer,
default vulkan; $ROOD_ARCH 64), continues the test card's save to the save
room, goes to room ZONE/ROOM (default 29 3, the Keep) and surveys it (RPC
material survey) from a few standard views (--views, default game,left,right,back):

  game   the game camera where the room puts the player;
  left   the free camera at the game camera's eye, turned --turn degrees left;
  right  ... turned right;
  back   the free camera on the room's far side looking back (the game
         camera's eye mirrored through the room's centre, turned around):
         the walls the game camera never faces;
  down   at the game camera's eye, pitched --turn degrees further down.

It prints the distinct textures drawn (content key, size, palette), boxes
of the same texture merged, sorted by their mean share of the views, with
each view's share, whether a material entry applies, and an atlas group
(textures whose boxes overlap on the same page and palette: pieces of one
painted region, which usually want the same material). It writes into
--out (default pc/build/material_survey/<zone>-<room>; game art, never
committed):

  survey.json     everything above;
  contact.png     the textures as drawn (from VRAM), labelled: the contact sheet;
  view-<v>.png    each view's frame, and map-<v>.png which texture covers each
                  part of it (the contact sheet's colours, top 24; the rest grey).

The shipped catalog applies (pc/assets/materials/catalog.json, or --catalog);
--user-catalog adds a user layer (read only: it is copied).
"""
import argparse
import base64
import colorsys
import json
import math
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))

TOP_COLOURS = 24


def goto(game, zone, room):
    game.call("editor_camera", off=1)
    game.call("room_goto", zone=zone, room=room, enemies=False, scripts=False)
    for _ in range(60):
        game.run(20)
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
    game.run(40)
    state = game.state()
    return (state["zone"], state["room"]) == (zone, room)


def yaw_pitch(forward):
    """The free camera's angles (RPC editor_camera) for a forward vector."""
    fx, fy, fz = forward
    return math.degrees(math.atan2(fx, fz)), math.degrees(math.asin(max(-1.0, min(1.0, -fy))))


def colour(rank):
    if rank < 0:
        return (0, 0, 0) if rank == -1 else (60, 60, 60)
    if rank >= TOP_COLOURS:
        return (128, 128, 128)
    h = (rank * 0.618034) % 1.0
    r, g, b = colorsys.hsv_to_rgb(h, 0.75 if rank % 2 else 0.95, 0.95 if rank % 3 else 0.75)
    return int(r * 255), int(g * 255), int(b * 255)


def groups(textures):
    """Atlas groups: textures whose boxes overlap on the same page and palette."""
    parent = list(range(len(textures)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    boxes = []
    for i, t in enumerate(textures):
        for b in t["boxes"]:
            boxes.append((t["tpage"] & 0x1f, t["palette"], b, i))
    for a in range(len(boxes)):
        pa, ca, ba, ia = boxes[a]
        for b in range(a + 1, len(boxes)):
            pb, cb, bb, ib = boxes[b]
            if pa == pb and ca == cb and ba[0] <= bb[2] and bb[0] <= ba[2] and ba[1] <= bb[3] and bb[1] <= ba[3]:
                parent[find(ia)] = find(ib)
    names = {}
    out = []
    for i in range(len(textures)):
        root = find(i)
        if root not in names:
            n = len(names)
            names[root] = (chr(ord("A") + n % 26) + (str(n // 26) if n >= 26 else ""))
        out.append(names[root])
    return out


def survey_room(game, args, out):
    from PIL import Image
    views = [v.strip() for v in args.views.split(",") if v.strip()]
    game.call("editor_camera", off=1)
    game.run(4)
    base = game.call("material", action="survey", grid=args.grid, map=True)
    if not base.get("valid"):
        raise SystemExit("no view to survey")
    yaw, pitch = yaw_pitch(base["forward"])
    eye = base["eye"]
    results = {}
    lo, hi = game.call("scene")["bounds"]
    centre = [(lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2]
    for v in views:
        if v == "game":
            game.call("editor_camera", off=1)
        elif v == "back":
            game.call("editor_camera", x=2 * centre[0] - eye[0], y=eye[1], z=2 * centre[1] - eye[2], yaw=yaw + 180,
                      pitch=pitch)
        else:
            dy = {"left": -args.turn, "right": args.turn}.get(v, 0)
            dp = args.turn if v == "down" else 0
            if v not in ("left", "right", "down"):
                raise SystemExit(f"unknown view {v} (game, left, right, back, down)")
            game.call("editor_camera", x=eye[0], y=eye[1], z=eye[2], yaw=yaw + dy, pitch=max(-89, min(89, pitch - dp)))
        for _ in range(2):  # the tools' view follows the camera on the next drawn frame
            game.run(4)
            game.call("redraw")
            game.wait_presents(4)
        results[v] = game.call("material", action="survey", grid=args.grid, map=True)
        game.present_screenshot(str(out / f"view-{v}.png"))
    # Merge by texture (key, size, palette).
    merged = {}
    for v, r in results.items():
        cells = r["grid"][0] * r["grid"][1]
        for t in r["textures"]:
            ident = (t["key"], tuple(t["size"]), t["clut"])
            m = merged.setdefault(ident, dict(key=t["key"], size=t["size"], clut=t["clut"], tpage=t["tpage"],
                                              palette=t["palette"], boxes=[], views={}, entry=""))
            for b in t["boxes"]:
                if b not in m["boxes"]:
                    m["boxes"].append(b)
            m["views"][v] = round(t["cells"] / cells, 5)
            m["entry"] = m["entry"] or t["entry"]
    textures = list(merged.values())
    for t in textures:
        t["share"] = round(sum(t["views"].get(v, 0) for v in views) / len(views), 5)
    textures.sort(key=lambda t: -t["share"])
    for i, (t, g) in enumerate(zip(textures, groups(textures))):
        t["rank"], t["group"] = i, g
    rank_of = {(t["key"], tuple(t["size"]), t["clut"]): t["rank"] for t in textures}
    # The attribution maps, in the contact sheet's colours.
    for v, r in results.items():
        gw, gh = r["grid"]
        local = [rank_of[(t["key"], tuple(t["size"]), t["clut"])] for t in r["textures"]]
        img = Image.new("RGB", (gw, gh))
        img.putdata([colour(local[i] if i >= 0 else i) for i in r["map"]])
        img.resize((gw * 4, gh * 4), Image.NEAREST).save(out / f"map-{v}.png")
    return dict(room=base["room"], views=views, grid=base["grid"], eye=eye, yaw=round(yaw, 2),
                pitch=round(pitch, 2), turn=args.turn, textures=textures,
                textured={v: round(r["textured"], 4) for v, r in results.items()})


def contact_sheet(game, data, out, limit):
    from PIL import Image, ImageDraw
    shown = [t for t in data["textures"] if t["share"] >= limit]
    cols, cell_w, cell_h, tile = 8, 200, 168, 112
    rows = max(1, (len(shown) + cols - 1) // cols)
    sheet = Image.new("RGB", (cols * cell_w, rows * cell_h), (24, 24, 28))
    draw = ImageDraw.Draw(sheet)
    for n, t in enumerate(shown):
        x0, y0 = (n % cols) * cell_w, (n // cols) * cell_h
        draw.rectangle([x0 + 2, y0 + 2, x0 + 14, y0 + 14], fill=colour(t["rank"]))
        try:
            got = game.call("material", action="texture", key=t["key"], palette=t["clut"])
            w, h = int(got["w"]), int(got["h"])
            img = Image.frombytes("RGBA", (w, h), base64.b64decode(got["rgba"]))
            scale = max(1, min(tile // max(w, h), 8)) if max(w, h) <= tile else tile / max(w, h)
            img = img.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.NEAREST)
            back = Image.new("RGBA", img.size, (60, 0, 60, 255))
            back.alpha_composite(img)
            sheet.paste(back.convert("RGB"), (x0 + 18, y0 + 4))
        except Exception as e:  # not drawn any more: leave the tile empty
            draw.text((x0 + 18, y0 + 40), f"({e})"[:30], fill=(200, 80, 80))
        views = " ".join(f"{v[0]}{t['views'].get(v, 0) * 100:.1f}" for v in data["views"])
        lines = [f"#{t['rank'] + 1} {t['group']}  {t['share'] * 100:.2f}%  {t['entry'] or '-'}",
                 f"{t['key']} {t['size'][0]}x{t['size'][1]}", f"pal {t['clut'] or '-'}", views]
        for i, line in enumerate(lines):
            draw.text((x0 + 4, y0 + tile + 8 + i * 11), line, fill=(230, 230, 230) if i == 0 else (170, 170, 170))
    sheet.save(out / "contact.png")
    return len(shown)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("zone", type=int, nargs="?", default=29)
    parser.add_argument("room", type=int, nargs="?", default=3)
    parser.add_argument("--views", default="game,left,right,back")
    parser.add_argument("--turn", type=float, default=30, help="degrees the free camera turns (default 30)")
    parser.add_argument("--grid", type=int, default=160, help="survey grid width in cells (default 160)")
    parser.add_argument("--limit", type=float, default=0.001, help="contact sheet: least mean share (default 0.1%%)")
    parser.add_argument("--top", type=int, default=40, help="rows printed (default 40)")
    parser.add_argument("--catalog", help="the shipped catalog (default pc/assets/materials/catalog.json)")
    parser.add_argument("--user-catalog", help="a user layer to apply (copied: never written)")
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="output directory (default pc/build/material_survey/ZONE-ROOM)")
    args = parser.parse_args()
    os.environ.setdefault("ROOD_ARCH", "64")
    os.environ.setdefault("ROOD_RENDERER", "vulkan")
    from checkpoints import launch_at  # after ROOD_ARCH: rood_rpc picks the binary at import
    if not pathlib.Path(args.card).exists():
        raise SystemExit(f"no memory card {args.card} (run pc/tests/run.py once, or --card)")
    out = pathlib.Path(args.out) if args.out else ROOT / f"pc/build/material_survey/{args.zone}-{args.room}"
    out.mkdir(parents=True, exist_ok=True)
    scratch = pathlib.Path(tempfile.mkdtemp(prefix="survey-", dir=out))
    try:
        card = scratch / "card1.mcd"
        shutil.copyfile(args.card, card)
        (scratch / "settings.cfg").write_text("light_animation 0\ndust 0\n")
        (scratch / "lights").mkdir()
        user = scratch / "materials.json"
        if args.user_catalog:
            shutil.copyfile(args.user_catalog, user)
        env = {"ROOD_LIGHTS": str(scratch / "lights"), "ROOD_LIGHTING": str(ROOT / "lighting"),
               "ROOD_SETTINGS": str(scratch / "settings.cfg"), "ROOD_SCALE": "2", "ROOD_WINDOW": "1280x960",
               "ROOD_MATERIAL_CATALOG_USER": str(user), "ROOD_MATERIALS": str(scratch / "maps"),
               "ROOD_MATERIAL_EXPORTS": str(scratch / "exports"), "ROOD_OUT": str(scratch)}
        if args.catalog:
            env["ROOD_MATERIAL_CATALOG"] = str(pathlib.Path(args.catalog).resolve())
        socket = os.environ.get("ROOD_RPC") or str(pathlib.Path(tempfile.gettempdir()) / f"rood-survey-{os.getpid()}.sock")
        game = launch_at("save_room", card=card, fast=True, log=scratch / "game.log", extra_env=env, socket_path=socket)
        try:
            if not goto(game, args.zone, args.room):
                raise SystemExit(f"could not reach room {args.zone}/{args.room}")
            data = survey_room(game, args, out)
            shown = contact_sheet(game, data, out, args.limit)
        finally:
            game.close()
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    textures = data["textures"]
    total = sum(t["share"] for t in textures) or 1
    with_entry = sum(t["share"] for t in textures if t["entry"])
    print(f"room {data['room'][0]}/{data['room'][1]}: {len(textures)} textures over views {', '.join(data['views'])}; "
          f"{sum(1 for t in textures if t['entry'])} with a material ({with_entry / total * 100:.1f}% of the textured "
          "coverage)")
    # Palettes: what a palette entry (match {"clut"}) would cover.
    palettes = {}
    for t in textures:
        q = palettes.setdefault(t["clut"], dict(clut=t["clut"], share=0.0, textures=0, entries=set(), top=t["rank"] + 1))
        q["share"] += t["share"]
        q["textures"] += 1
        if t["entry"]:
            q["entries"].add(t["entry"])
    data["palettes"] = sorted(({**q, "share": round(q["share"], 5), "entries": sorted(q["entries"])}
                               for q in palettes.values()), key=lambda q: -q["share"])
    (out / "survey.json").write_text(json.dumps(data, indent=1) + "\n")
    print(f"palettes (a palette entry covers every texture drawn with one):")
    cum = 0
    for q in data["palettes"][:12]:
        cum += q["share"]
        print(f"  {q['clut'] or '-':10} {q['share'] / total * 100:5.1f}% (cum {cum / total * 100:5.1f}%), "
              f"{q['textures']:3d} textures, first #{q['top']}, entries: {', '.join(q['entries']) or '-'}")
    print(f"{'#':>3} {'grp':>3} {'mean%':>6} {'cum%':>5}  {'key':10} {'size':>7} {'palette':10} "
          + " ".join(f"{v[:5]:>5}" for v in data["views"]) + "  entry")
    cum = 0
    for t in textures[:args.top]:
        cum += t["share"]
        print(f"{t['rank'] + 1:3d} {t['group']:>3} {t['share'] * 100:6.2f} {cum / total * 100:5.1f}  {t['key']:10} "
              f"{t['size'][0]:>3}x{t['size'][1]:<3} {t['clut'] or '-':10} "
              + " ".join(f"{t['views'].get(v, 0) * 100:5.1f}" for v in data["views"]) + f"  {t['entry'] or '-'}")
    print(f"wrote {out}/survey.json, contact.png ({shown} textures), view-*.png, map-*.png (game art: keep local)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
