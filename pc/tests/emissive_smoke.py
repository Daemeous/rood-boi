#!/usr/bin/env python3
"""Emissive texels (pc/docs/Emissive.md): off is the picture without them,
marked texels glow and nothing else changes, the catalog finds its curated
textures in their rooms, an editor-made mask survives save and reload, with
bloom on the glowing texels are what blooms, and with ray tracing (Vulkan)
the forge's coals light the hood above them.

    python3 pc/tests/emissive_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--trace]

Continues from the card's save into Workshop "Metal Works" (zone 45 room 0,
RPC room_goto): its forge's coals (curated E004) in the game's view, the
game paused, the frame redrawn with each setting (RPC redraw).

- off: the frame with emissive 0, with every entry disabled (emissive 1,
  nothing marked) and with emissive 0 again are identical, pixel for pixel
  (Vulkan: on the Original preset; lit, no more apart than two redraws);
- on: the coals (the pixels that change) are at least 15% brighter, and no
  pixel outside the forge's part of the picture changes (path tracing off:
  no light reaches other surfaces); with bloom (threshold 1) a halo
  appears around them;
- matcher: every curated entry marks its textures in its room (32/3 with its
  floating blocks shown, 34/0, 45/0, 53/21);
- editor: a mask made over RPC on a texture picked in the view (a rect and a
  CLUT index set) marks texels, is saved to the user file, and after a
  reload from disk marks the same texels with the same emission;
- scopes: an entry on the forge bed's texture (sampled by a dozen polygons)
  lights every one of them; "show other uses" (RPC emissive surfaces) counts
  them, and each one visible in the view changes on screen exactly when it is
  counted; limited to one picked polygon only that one changes (the others,
  same texels, stay as without it), then two; a room scope of another room
  marks nothing, of this one everything; saved and reloaded the scope is the
  same; a light entry limited to a polygon is a light only there; a mask on
  Ashley's texture limited to another model shows nowhere, to hers again on
  her;
- filtered light: the coals narrowed to their hottest CLUT index (a few
  percent of the bed's texels) still make light triangles with light;
- painted maps (pc/docs/Materials.md): an entry on the floor's texture with
  a painted emissive map glows nowhere without the map; exporting its
  reference writes reference.png (RGBA, the texture's size) into the export
  folder and an empty map into the user layer, and refuses pc/assets; a
  rect painted over RPC glows exactly there (picks across the floor: a
  texel glows when it is painted), the map file holds the same pixels and
  so does a reload; an imported PNG (grayscale, and an alpha-only one)
  replaces it, one of another size is refused; a map written straight to
  disk is picked up while the game runs; with "light" the painted floor
  adds light triangles;
- --trace (or ROOD_RENDERER=vulkan): with path traced GI (Ultra) the hood
  and wall above the forge get brighter when the coals glow (their light),
  and still when the coals are narrowed to their hottest index; lights on
  objects: the magic crates (E005, 53/21) are traced object glows and turn
  the floor and Ashley beside them cyan (blue rises, more than red; their
  glows used to sit inside the crates: every shadow ray hit the crate, no
  light and a darker floor), and a floating block's circle (E001, 32/3: a
  room group the room's capture leaves out, which made no light at all) is
  an object glow that lights the front of Ashley's legs, standing on it,
  green; no crate glow faces the floor; an entry's light_reach and
  light_lift set its glows' reach and how far out they sit; a floor glyph
  tile (E006, 29/3) lights from 48 units over the floor with reach 320.
"""
import argparse
import json
import math
import os
import pathlib
import shutil
import struct
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402

SETTLE = 45
MATCH_ROOMS = [((29, 3), ["E006"]), ((32, 3), ["E001"]), ((34, 0), ["E002", "E003"]), ((45, 0), ["E004"]),
               ((53, 21), ["E005"])]


def frame(game, path):
    game.present_screenshot(path)
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)


def redraw(game):
    """The paused game's newest frame drawn again with the current settings (RPC redraw), then shown."""
    game.call("redraw")
    game.wait_presents(4)


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


def window_camera(game, yaw=270):
    """The free camera inside Dinas Walk looking at its two lattice windows (yaw 270: the west wall; 90: the
    east wall and its octagonal window)."""
    lo, hi = game.call("scene")["bounds"]
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    r = max(hi[0] - lo[0], hi[2] - lo[2]) * 0.45
    a = math.radians(yaw)
    game.call("editor_camera", x=cx - math.sin(a) * r, y=-1100, z=cz - math.cos(a) * r, yaw=yaw, pitch=-3)


def show_platforms(game):
    data = game.read_symbol("vs_battle_roomData", 0xB0)
    table = struct.unpack("<I", data[0xA8:0xAC])[0]
    count = struct.unpack("<I", data[0x48:0x4C])[0] // 184
    for i in range(count):
        word = struct.unpack("<I", game.read(table + i * 184, 4))[0]
        game.write(table + i * 184, struct.pack("<I", word & ~(1 << 9)))
    game.run(2)
    game.write(symbols()["D_800F1D6E"], b"\x01")


def entry_of(game, eid):
    for e in game.call("emissive")["entries"]:
        if e["id"] == eid:
            return e
    return None


def block(changed, x, y, r=4):
    return int(changed[max(0, y - r):y + r + 1, max(0, x - r):x + r + 1].sum())


def scope_tests(game, out, check, trace):
    """Scopes on the forge bed's texture (45/0, the game's camera, paused): see the module docstring."""
    def shot(name):
        redraw(game)
        return frame(game, out / name)

    if trace:  # exact pictures: the original preset (no temporal effects)
        preset = game.call("graphics").get("lighting_preset")
        game.call("graphics", lighting_preset="original")
    e4 = entry_of(game, "E004")
    game.call("emissive", action="set", id="E004", enabled=False)
    test = {"id": "U910", "name": "scope test", "match": e4["match"], "mask": {}, "color": [0.4, 1, 0.4],
            "strength": 3, "light": 0}
    game.call("emissive", action="set", entry=test)
    game.run(4)
    listing = game.call("emissive", action="surfaces", id="U910")
    lit = [u for u in listing["surfaces"] if u["allowed"] and u["glowing"] > 0]
    check(len(lit) >= 3, f"the forge bed's texture lies on several surfaces ({len(lit)} light up)")
    # the surfaces visible in the view: a screen point where a pick finds that very polygon
    visible = {}
    for u in lit:
        for x, y in u.get("screen", []):
            p = game.call("emissive", action="pick", x=x, y=y)
            if p.get("valid") and p["surface"].get("polygon") == u["polygon"] and u["polygon"] not in visible:
                visible[u["polygon"]] = (int(x), int(y))
    check(len(visible) >= 2, f"two or more of them visible ({len(visible)})")
    if len(visible) < 2:
        return
    first = next(iter(visible))
    ax, ay = visible[first]
    others = [k for k in visible if k != first and abs(visible[k][0] - ax) + abs(visible[k][1] - ay) >= 24]
    check(bool(others), "another one apart from the first")
    if not others:
        return
    second = others[0]
    bx, by = visible[second]
    game.call("emissive", action="pick", x=ax, y=ay)  # the picked surface (the editor's)
    # From here the paused frame is only redrawn (no game frame: the bed's VRAM animation and Ashley stay put).
    listing = game.call("emissive", action="surfaces", id="U910")
    check(listing["affected"] == len(lit) and listing["other"] == len(lit) - 1,
          f"show other uses: {listing['affected']} surfaces lit, {listing['other']} besides the picked one "
          f"(expected {len(lit)}, {len(lit) - 1})")
    game.call("emissive", action="set", id="U910", enabled=False)
    off = shot("scope-off.png")
    game.call("emissive", action="set", id="U910", enabled=True)
    on = shot("scope-everywhere.png")
    changed = (on != off).any(2)
    wrong = [k for k in visible if (block(changed, *visible[k]) > 0) != True]  # noqa: E712
    check(not wrong, f"unlimited: every visible surface it counts changes on screen ({len(visible)} checked, "
          f"{len(wrong)} wrong: {wrong})")

    # limited to the picked polygon
    picked = game.call("emissive", action="scope_pick", id="U910")
    check(picked.get("added") and picked.get("scope", {}).get("surfaces") == [{"room": [45, 0], "polygons": [first]}],
          f"limit to the picked surface: scope {picked.get('scope')}")
    scoped = shot("scope-one.png")
    listing = game.call("emissive", action="surfaces", id="U910")
    check(listing["affected"] == 1 and listing["other"] == 0 and listing["excluded"] == len(lit) - 1,
          f"limited: show other uses counts {listing['affected']} lit, {listing['other']} other, "
          f"{listing['excluded']} left out (expected 1, 0, {len(lit) - 1})")
    changed = (scoped != off).any(2)
    a_changed, b_changed = block(changed, ax, ay), block(changed, bx, by, 2)
    check(a_changed > 0 and b_changed == 0, f"limited to polygon {first}: it changes ({a_changed} pixels), polygon "
          f"{second} with the same texels does not ({b_changed})")
    stray = [k for k in visible if k != first and block(changed, *visible[k], 2) > 0]
    check(not stray, f"no other visible surface changes ({stray})")
    p = game.call("emissive", action="pick", x=bx, y=by)
    check(p.get("glows") and not p.get("glows_here"), f"the other surface's texel glows as a texture but not there "
          f"(glows {p.get('glows')}, glows_here {p.get('glows_here')})")
    # add the second one (a click on it with Limit to picked surfaces)
    game.call("emissive", action="scope_pick", id="U910")
    two = shot("scope-two.png")
    listing = game.call("emissive", action="surfaces", id="U910")
    check(listing["affected"] == 2 and listing["other"] == 0,
          f"two picked surfaces: {listing['affected']} lit, {listing['other']} other")
    changed = (two != off).any(2)
    check(block(changed, ax, ay) > 0 and block(changed, bx, by) > 0, "both picked surfaces change")

    # saved and reloaded: the same scope, the same counts
    scope = entry_of(game, "U910")["scope"]
    game.call("emissive", action="save")
    game.call("emissive", action="reload")
    game.run(4)
    again = entry_of(game, "U910")
    listing2 = game.call("emissive", action="surfaces", id="U910")
    check(again and again.get("scope") == scope and listing2["affected"] == 2,
          f"saved and reloaded: scope {again and again.get('scope')}, {listing2['affected']} lit")

    # rooms (the paused frame redrawn: no game frame in between, the bed's VRAM animation stays put)
    game.call("emissive", action="set", id="U910", scope={}, enabled=False)
    off = shot("scope-off-again.png")
    game.call("emissive", action="set", id="U910", enabled=True)
    on = shot("scope-everywhere-again.png")
    game.call("emissive", action="set", id="U910", scope={"rooms": [[45, 1]]})
    elsewhere = shot("scope-room-elsewhere.png")  # (surfaces after a redraw: the map is rebuilt at the sync)
    listing = game.call("emissive", action="surfaces", id="U910")
    check(np.array_equal(elsewhere, off) and listing["affected"] == 0,
          f"limited to another room: nothing here ({int((elsewhere != off).any(2).sum())} pixels, "
          f"{listing['affected']} lit)")
    game.call("emissive", action="set", id="U910", scope={"rooms": [[45, 0]]})
    here = shot("scope-room-here.png")
    listing = game.call("emissive", action="surfaces", id="U910")
    check(np.array_equal(here, on) and listing["affected"] == len(lit),
          f"limited to this room: as unlimited ({int((here != on).any(2).sum())} pixels differ, "
          f"{listing['affected']} lit)")

    # a light limited to one polygon is a light there only
    game.call("emissive", action="set", id="U910", light=1, scope={"surfaces": [{"room": [45, 0], "polygons": [first]}]})
    game.run(4)
    lights = game.call("emissive", action="lights")["triangles"]
    glowing = {t["polygon"] for t in lights if t["glowing"] > 0}
    check(glowing == {first}, f"a light limited to polygon {first} lights from it only ({sorted(glowing)})")

    game.call("emissive", action="remove", id="U910")
    game.call("emissive", action="remove", id="E004")  # the curated coals as shipped
    game.call("emissive", action="save")
    game.run(2)

    # models: a mask on Ashley's texture
    model = None
    for y in range(200, 760, 40):
        for x in range(480, 800, 40):
            p = game.call("emissive", action="pick", x=x, y=y)
            if p.get("valid") and p["surface"]["kind"] == "model" and not p.get("entry"):
                model = (x, y, p)
                break
        if model:
            break
    check(model is not None, "a character's texture picked in the view")
    if model:
        x, y, p = model
        made = game.call("emissive", action="new")
        mid = made["id"]
        game.call("emissive", action="set", id=mid, color=[1, 0.2, 1], strength=3, enabled=False)
        moff = shot("model-off.png")
        results = {}
        for name, scope in (("everywhere", {}), ("another model", {"models": ["00000000"]}),
                            ("her model", {"models": [p["surface"]["model"]]})):
            game.call("emissive", action="set", id=mid, enabled=True, scope=scope)
            results[name] = block((shot(f"model-{name.replace(' ', '-')}.png") != moff).any(2), x, y)
        check(results["everywhere"] > 0 and results["another model"] == 0 and results["her model"] > 0,
              f"a mask on {p['surface']['model']}'s texture: everywhere / another model / hers: {results}")
        game.call("emissive", action="remove", id=mid)
        game.call("emissive", action="save")
    if trace and preset:
        game.call("graphics", lighting_preset=preset)


def painted_tests(game, out, lights_dir, check):
    """Painted maps on the floor texture under the screen centre (45/0, the game's camera)."""
    from rood_rpc import RpcError

    floor = None  # the room texture under most of the lower half of the view
    counts, found = {}, {}
    for y in range(560, 940, 40):
        for x in range(160, 1120, 80):
            p = game.call("emissive", action="pick", x=x, y=y)
            if p.get("valid") and p.get("room") and not p.get("entry") and p["surface"]["kind"] == "room":
                counts[p["key"]] = counts.get(p["key"], 0) + 1
                found.setdefault(p["key"], p)
    if counts:
        floor = found[max(counts, key=counts.get)]
    check(floor is not None, "painted maps: the floor's texture picked")
    if not floor:
        return
    key, (w, h) = floor["key"], floor["size"]
    box, tpage, clut = floor["box"], floor["tpage"], floor["clut"]
    entry = {"id": "U920", "name": "painted", "match": [{"key": key, "size": [w, h]}], "mask": {"map": "emissive"},
             "color": [1, 0.6, 0.2], "strength": 3, "light": 0}
    game.call("emissive", action="set", entry=entry)
    game.run(4)
    state = game.call("emissive", action="materials", key=key)
    problems = [q for q in state["problems"] if q["entry"] == "U920"]
    check(problems and "no emissive map" in problems[0]["problem"], f"without its map nothing glows ({problems})")

    exports = out / "exports"
    try:
        game.call("emissive", action="export_reference", key=key, dir="pc/assets/materials")
        refused = False
    except RpcError as e:
        refused = "refusing" in str(e)
    check(refused, "the reference export refuses pc/assets")
    written = game.call("emissive", action="export_reference", key=key, dir=str(exports))["written"]
    reference = exports / key / "reference.png"
    template = lights_dir / "materials" / key / "emissive.png"
    ok = reference.exists() and template.exists()
    if ok:
        ref, tmpl = Image.open(reference), Image.open(template)
        ok = ref.size == (w, h) and ref.mode == "RGBA" and tmpl.size == (w, h) and tmpl.mode == "L" \
            and not np.asarray(tmpl).any()
    check(ok, f"export: the reference ({w} x {h} RGBA) in the export folder, an empty map in the user layer ({written})")

    # the texture's colours (a texel of colour 0 is transparent: it never glows)
    tex = game.call("emissive", action="texture", tpage=tpage, clut=clut, box=box)
    palette = tex["palette"]
    solid = np.array([[bool(palette and any(palette[i]) or (not palette and i)) for i in row] for row in tex["indices"]])

    def texel_glows(x, y):
        return game.call("emissive", action="texel", u=box[0] + x, v=box[1] + y, tpage=tpage, clut=clut)["glows"]

    def solid_in(x0, y0, x1, y1):
        """A texel of the region [x0, x1) x [y0, y1) that is not transparent."""
        for y in range(y0, y1):
            for x in range(x0, x1):
                if solid[y, x]:
                    return x, y
        return x0, y0

    def screen_mismatches(expected):
        """Picks across the floor: a picked texel of this texture glows exactly when `expected(x, y)`."""
        seen = wrong = 0
        for sy in range(560, 940, 24):
            for sx in range(200, 1100, 48):
                q = game.call("emissive", action="pick", x=sx, y=sy)
                if not q.get("valid") or q.get("key") != key:
                    continue
                tx, ty = q["texel"]
                seen += 1
                wrong += bool(q["glows"]) != (expected(tx, ty) and solid[ty, tx])
        return seen, wrong

    # paint over RPC: the top-left quarter
    qw, qh = max(1, w // 2), max(1, h // 2)
    game.call("emissive", action="paint", key=key, fill=0, rects=[[0, 0, qw, qh, 255]])
    redraw(game)
    inside = lambda x, y: x < qw and y < qh  # noqa: E731
    painted_texel, unpainted_texel = solid_in(0, 0, qw, qh), solid_in(qw, qh, w, h)
    check(texel_glows(*painted_texel) and not texel_glows(*unpainted_texel),
          "painted: a painted texel glows, an unpainted one not")
    seen, wrong = screen_mismatches(inside)
    check(seen >= 5 and wrong == 0, f"painted: across the floor every picked texel glows exactly when painted "
          f"({seen} picks, {wrong} wrong)")
    expected = np.zeros((h, w), np.uint8)
    expected[:qh, :qw] = 255
    on_disk = np.asarray(Image.open(template))
    check(on_disk.shape == (h, w) and np.array_equal(on_disk, expected), "painted: the map file holds those pixels")
    game.call("emissive", action="save")
    game.call("emissive", action="reload")
    redraw(game)
    seen2, wrong2 = screen_mismatches(inside)
    check(texel_glows(*painted_texel) and not texel_glows(*unpainted_texel) and wrong2 == 0,
          f"painted: saved and reloaded, the same texels glow ({seen2} picks, {wrong2} wrong)")

    # import: the right half, grayscale; then an alpha-only image (black, painted in the alpha)
    right = np.zeros((h, w), np.uint8)
    right[:, w // 2:] = 200
    gray = out / "paint-right.png"
    Image.fromarray(right, "L").save(gray)
    game.call("emissive", action="import_map", key=key, path=str(gray))
    redraw(game)
    left, right_texel = solid_in(0, 0, w // 2, h), solid_in(w // 2, 0, w, h)
    check(texel_glows(*right_texel) and not texel_glows(*left), "import: a grayscale PNG replaces the map")
    alpha = np.zeros((h, w, 4), np.uint8)
    alpha[h // 2:, :, 3] = 255
    alpha_png = out / "paint-alpha.png"
    Image.fromarray(alpha, "RGBA").save(alpha_png)
    game.call("emissive", action="import_map", key=key, path=str(alpha_png))
    redraw(game)
    top, bottom = solid_in(0, 0, w, h // 2), solid_in(0, h // 2, w, h)
    check(texel_glows(*bottom) and not texel_glows(*top), "import: an alpha-only PNG is its alpha")
    wrong_size = out / "paint-wrong.png"
    Image.fromarray(np.zeros((h + 1, w + 1), np.uint8), "L").save(wrong_size)
    try:
        game.call("emissive", action="import_map", key=key, path=str(wrong_size))
        refused = False
    except RpcError as e:
        refused = "the texture is" in str(e)
    check(refused, "import: a PNG of another size is refused")

    # live: the map written straight to disk (an outside editor) is picked up while the game runs
    Image.fromarray(np.full((h, w), 255, np.uint8), "L").save(template)
    game.run(150)
    redraw(game)
    check(texel_glows(*top) and texel_glows(*left), "live: a map saved by another program is reloaded")

    # light: the painted floor is a light (more light triangles with light)
    def lit():
        return sum(1 for t in game.call("emissive", action="lights")["triangles"] if t["glowing"] > 0)
    before = lit()
    game.call("emissive", action="set", id="U920", light=1)
    game.run(4)
    after = lit()
    check(after > before, f"light: the painted floor adds light triangles ({before} -> {after})")
    game.call("emissive", action="remove", id="U920")
    game.call("emissive", action="save")
    game.run(2)


def object_light_tests(game, out, check):
    """Emissive lights on objects, path traced (Ultra, Vulkan): see the module docstring. Each entry's light
    weight 0 against W (the glow on screen is the same either way), every time long enough for the glows to
    become steady magic glows (an effect glow that stays 4 seconds; magic glows cast shadows)."""
    def light(eid, weight):
        game.call("emissive", action="set", id=eid, light=weight)
        game.run(330)

    # The magic crates (E005) in the game's view, Ashley beside them: the floor in front of them and her legs
    # (the middle of the lower half of the frame) take on their cyan.
    if not goto(game, 53, 21):
        check(False, "warped to zone 53 room 21")
        return
    means = {}
    for weight in (0, 1):
        light("E005", weight)
        means[weight] = np.mean([frame(game, out / f"crates-{weight}-{k}.png") for k in range(4)], axis=0)
        if weight:
            noise = float(np.abs(np.mean([frame(game, out / f"crates-again-{k}.png") for k in range(4)], axis=0)
                                 - means[1]).mean())
            glows = [g for g in game.call("glows")["glows"] if g["origin"] == "object"]
    h, w, _ = means[0].shape
    rise = (means[1] - means[0])[h // 2: h * 3 // 4, w // 4: w * 3 // 4].reshape(-1, 3).mean(0)
    check(glows and all(g["traced"] for g in glows), f"the crates are traced object glows ({len(glows)})")
    check(rise[2] >= max(0.6, 5 * noise) and rise[2] >= rise[0] + 0.3,
          f"the crates light the floor and Ashley beside them in their cyan: +{rise[0]:.2f} +{rise[1]:.2f} "
          f"+{rise[2]:.2f} (rgb levels, light 1 against 0; frame noise {noise:.3f})")
    # Their shape and its tunables (room settings glow_emissive_*, RPC glows): each a solid disc in front of the
    # side it faces (its tag's bits 23-25: 1 + 2 axis + negative); half the disc factor halves the radius, ten
    # more units of lift move each glow ten units out along its side.
    def side_of(g):
        side = (int(g["key"].split(":")[1].split("@")[0], 16) >> 23) & 7
        return (side - 1) // 2, -1 if (side - 1) % 2 else 1

    def object_glows():
        return {g["key"]: g for g in game.call("glows")["glows"] if g["origin"] == "object"}
    check(glows and all(g["shape"]["kind"] == "solid" and 0 <= side_of(g)[0] <= 2 for g in glows),
          f"each crate glow is a solid disc facing its side ({sorted({g['shape']['kind'] for g in glows})})")
    defaults = game.call("glows")["emissive"]
    was = object_glows()
    game.call("glows", emissive_disc=defaults["disc"] / 2, emissive_lift=defaults["lift"] + 10)
    game.run(60)
    now = object_glows()
    halved = [now[k]["shape"]["radius"] / g["shape"]["radius"] for k, g in was.items() if k in now]
    moved = [(now[k]["position"][side_of(g)[0]] - g["position"][side_of(g)[0]]) * side_of(g)[1]
             for k, g in was.items() if k in now]
    check(halved and all(abs(r - 0.5) < 0.03 for r in halved),
          f"glows emissive_disc {defaults['disc'] / 2:g} halves the discs (ratios "
          f"{sorted(set(round(r, 2) for r in halved))})")
    check(moved and all(abs(m - 10) < 1 for m in moved),
          f"glows emissive_lift {defaults['lift'] + 10:g} moves them 10 out (by "
          f"{sorted(set(round(m, 1) for m in moved))})")
    game.call("glows", emissive_disc=defaults["disc"], emissive_lift=defaults["lift"])
    # No glow on a side turned to the floor (a crate's bottom lit only the floor under it and took a traced
    # glow): side 3 is +y, down.
    check(glows and all(side_of(g) != (1, 1) for g in glows),
          f"no crate glow faces the floor (sides {sorted({side_of(g) for g in glows})})")
    # The entry's own light shape (light_reach, light_lift): the glows reach that far, and sit that far out.
    game.call("emissive", action="set", id="E005", light=1, light_reach=150, light_lift=defaults["lift"] + 10)
    game.run(60)
    now = object_glows()
    reaches = sorted({round(g["reach"]) for g in now.values()})
    moved = [(now[k]["position"][side_of(g)[0]] - g["position"][side_of(g)[0]]) * side_of(g)[1]
             for k, g in was.items() if k in now]
    check(reaches == [150], f"E005 light_reach 150: its glows reach 150 ({reaches})")
    check(moved and all(abs(m - 10) < 1 for m in moved),
          f"E005 light_lift {defaults['lift'] + 10:g}: its glows sit 10 further out "
          f"({sorted(set(round(m, 1) for m in moved))})")
    game.call("emissive", action="remove", id="E005")
    # A floor glyph tile (E006, 29/3: room geometry) lights from above the floor (light_lift 48) with its
    # tight reach (light_reach 320): its room glow sits 48 units over the tile (the floor is at y 0; up is -y).
    if goto(game, 29, 3):
        game.run(60)
        tiles = [g for g in game.call("glows")["glows"] if g["origin"] == "room"
                 and abs(g["position"][0] - 832) < 40 and abs(g["position"][2] - 320) < 40]
        check(len(tiles) == 1 and abs(tiles[0]["position"][1] + 48) < 2 and abs(tiles[0]["reach"] - 320) < 1,
              f"the glyph tile's glow: 48 over the floor, reach 320 "
              f"({[(g['position'], g['reach']) for g in tiles]})")
    else:
        check(False, "warped to zone 29 room 3")

    # A floating block (E001: its top's magic circle; a room group the room's capture leaves out) with Ashley
    # standing on it, in the free camera: green light on the front of her legs, in the lighting-only view (her
    # legs and the block's top around them in the middle of the frame) and, on Vulkan, in the glow light
    # there (RPC gi_probe; OpenGL's probe reads its half-resolution light, too coarse for her legs).
    if not goto(game, 32, 3):
        check(False, "warped to zone 32 room 3")
        return
    show_platforms(game)
    data = game.read_symbol("vs_battle_roomData", 0xB0)
    table = struct.unpack("<I", data[0xA8:0xAC])[0]
    x, altitude, z = (v / 4096 for v in struct.unpack("<iii", game.read(table + 0xA8, 12)))
    game.call("editor_camera", x=x, y=-altitude - 500, z=z - 600, yaw=0, pitch=-35)
    actor = struct.unpack("<I", game.read_symbol("D_800F4538", 4))[0]  # Ashley's record: position at 0x1C
    game.write(actor + 0x1C, struct.pack("<hhh", int(x), int(-altitude - 10), int(z)))
    game.run(30)
    on = game.state()["player"]
    legs, views = {}, {}
    for weight in (0, 3):
        light("E001", weight)
        game.call("graphics", view_mode=2)
        game.run(30)
        views[weight] = np.mean([frame(game, out / f"block-{weight}-{k}.png") for k in range(4)], axis=0)
        if weight:
            view_noise = float(np.abs(np.mean([frame(game, out / f"block-again-{k}.png") for k in range(4)], axis=0)
                                      - views[3]).mean())
        game.call("graphics", view_mode=0)
        values = []
        for dy in (-15, -35):
            for dx in (-10, 10):
                for _ in range(4):
                    game.run(3)
                    try:
                        values.append(game.call("gi_probe", x=on["x"] + dx, y=on["y"] + dy, z=on["z"] - 14)["extra"])
                    except Exception:
                        pass
        legs[weight] = np.mean(values, axis=0) if values else np.zeros(3)
        if weight:
            glows = [g for g in game.call("glows")["glows"] if g["origin"] == "object"
                     and math.hypot(g["position"][0] - x, g["position"][2] - z) < 128]
    check(abs(on["y"] + altitude) < 24, f"Ashley stands on the block ({on}, its top at {-altitude:.0f})")
    check(bool(glows), f"the block's circle is an object glow ({[g['name'] for g in glows]})")
    h, w, _ = views[0].shape
    rise = (views[3] - views[0])[h * 47 // 100: h * 73 // 100, w * 44 // 100: w * 58 // 100].reshape(-1, 3).mean(0)
    check(rise[1] >= max(1.0, 5 * view_noise) and rise[1] >= rise[0] + 0.5,
          f"the circle lights Ashley standing on it, green, in the lighting-only view: +{rise[0]:.2f} "
          f"+{rise[1]:.2f} +{rise[2]:.2f} (rgb levels, light 3 against 0; frame noise {view_noise:.3f})")
    lift = legs[3] - legs[0]
    if os.environ.get("ROOD_RENDERER") == "vulkan":
        check(lift[1] >= 0.01 and lift[1] > lift[0],
              f"the circle lights the front of her legs, green: +{lift[0]:.4f} +{lift[1]:.4f} +{lift[2]:.4f} "
              f"(glow light, RPC gi_probe, light 3 against 0)")
    game.call("emissive", action="remove", id="E001")
    game.call("editor_camera", off=1)


def narrow_coals(game):
    """The coals (E004) narrowed to their hottest CLUT index: (the index, the share of the bed's texels)."""
    game.call("emissive", action="set", id="E004", mask={"indices": [15]}, light=1)
    game.run(4)
    return game.call("emissive", action="lights")["triangles"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep the frames and logs here")
    parser.add_argument("--trace", action="store_true", help="also the ray-traced light (Vulkan)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    trace = args.trace or os.environ.get("ROOD_RENDERER") == "vulkan"
    out = pathlib.Path(args.out) if args.out else OUT / "emissive"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    user = out / "emissive-user.json"
    user.unlink(missing_ok=True)
    env = {"ROOD_LIGHTS": str(lights), "ROOD_EMISSIVE_USER": str(user), "ROOD_SCALE": "2",
           "ROOD_MATERIAL_EXPORTS": str(out / "exports"),
           "ROOD_WINDOW": "1280x960", "ROOD_GI": "1" if trace else "0"}
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-emissive-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        listing = game.call("emissive")
        curated = [e["id"] for e in listing["entries"] if e["origin"] == "curated"]
        check(len(curated) >= 5, f"the curated catalog loads ({len(curated)} entries)")
        game.call("graphics", interpolate=False)

        # ---- off, on: the forge of Workshop "Metal Works" (45/0, the game's camera; path tracing off unless
        # --trace). The game stays paused on one frame; RPC redraw draws it again with each setting.
        check(goto(game, 45, 0), "warped to zone 45 room 0")
        if trace:
            game.call("graphics", gi=False)  # pixel-exact comparisons first: no path-traced noise
        game.run(SETTLE)
        def identity(tag, exact):
            """emissive 0, emissive 1 with every entry disabled, emissive 0 again: the same picture."""
            game.call("graphics", emissive=0)
            redraw(game)
            off = frame(game, out / f"off{tag}.png")
            game.call("graphics", emissive=1)
            for e in curated:
                game.call("emissive", action="set", id=e, enabled=False)
            redraw(game)
            listing = game.call("emissive")
            check(listing["marked_texels"] == 0, f"every entry disabled: nothing marked ({listing['marked_texels']})")
            nothing = frame(game, out / f"on-nothing-marked{tag}.png")
            game.call("emissive", action="reload")  # the user layer's disabled copies dropped (unsaved)
            game.call("graphics", emissive=0)
            redraw(game)
            off2 = frame(game, out / f"off-again{tag}.png")
            if exact:
                check(np.array_equal(off, nothing), f"{tag or 'lit'}: emissive 1 with nothing marked is "
                      f"pixel-identical to emissive 0 ({int((off != nothing).any(2).sum())} pixels differ)")
                check(np.array_equal(off, off2), f"{tag or 'lit'}: emissive 0 again is pixel-identical "
                      f"({int((off != off2).any(2).sum())} pixels differ)")
            else:
                # Vulkan's lit picture redraws with temporal noise (haze, AO, probes): the change must be no
                # larger than a redraw's own
                noise = np.abs(off - off2).mean()
                change = np.abs(off - nothing).mean()
                check(change <= noise * 2 + 0.02, f"{tag or 'lit'}: emissive 1 with nothing marked differs from "
                      f"emissive 0 no more than a redraw does ({change:.3f} vs {noise:.3f})")
            return off

        if trace:
            # Vulkan: exact on the original picture (no temporal effects), then the lit one within its noise
            preset0 = game.call("graphics").get("lighting_preset")
            game.call("graphics", lighting_preset="original")
            identity("-original", True)
            if preset0:
                game.call("graphics", lighting_preset=preset0)
            game.call("graphics", gi=False)
            off = identity("", False)
        else:
            off = identity("", True)
        game.call("graphics", emissive=1)
        redraw(game)
        listing = game.call("emissive")
        marked = {e["id"]: e["marked"] for e in listing["entries"]}
        check(marked.get("E004", 0) >= 1, f"the forge coals' textures are marked ({marked.get('E004')})")
        on = frame(game, out / "on.png")
        h, w, _ = on.shape
        region = np.zeros(on.shape[:2], bool)  # the forge's bed: right of the frame, middle height
        region[h * 25 // 100: h * 60 // 100, w * 65 // 100:] = True
        if True:  # path tracing off here (OpenGL; Vulkan with gi off)
            changed = (on != off).any(2)
            lum_on, lum_off = on.mean(2), off.mean(2)
            coals = changed & region
            outside = changed & ~region
            gain = lum_on[coals].mean() / max(lum_off[coals].mean(), 1e-3) if coals.any() else 0
            check(coals.sum() >= 1000 and gain >= 1.15,
                  f"the coals glow: {int(coals.sum())} pixels, {gain:.2f} x their lit light (>= 1.15)")
            check(outside.sum() == 0, f"nothing else changes ({int(outside.sum())} pixels outside the forge)")
            # (anti-aliasing blends a few edge pixels with their brighter neighbours' old value)
            darker = int((lum_on[coals] < lum_off[coals] - 2).sum())
            check(darker <= max(20, coals.sum() // 25), f"no marked pixel gets darker ({darker} edge pixels)")
            # Bloom (post stack, threshold 1: only light above white blooms): the glowing coals are what
            # blooms, a halo around them that emissive 0 does not have.
            game.call("post", layer="gameplay", clear="all", set={"bloom_method": "gaussian", "bloom_intensity": 1,
                                                                  "bloom_threshold": 1.0, "bloom_knee": 0}, snap=True)
            halos = {}
            for value in (0, 1):
                game.call("graphics", emissive=value)
                redraw(game)
                halos[value] = frame(game, out / f"bloom-{value}.png").mean(2)
            ring = changed.copy()  # the pixels within 12 of a coal, the coals left out
            for _ in range(3):
                grown = ring.copy()
                for dy, dx in ((-4, 0), (4, 0), (0, -4), (0, 4)):
                    grown |= np.roll(np.roll(ring, dy, 0), dx, 1)
                ring = grown
            ring &= ~changed
            halo = float(halos[1][ring].mean() - halos[0][ring].mean())
            check(halo >= 0.3, f"with bloom on, the coals bloom: +{halo:.2f} levels around them")
            game.call("post", layer="gameplay", clear="all", snap=True)
            game.call("graphics", emissive=1)

        # ---- scopes, "show other uses"
        scope_tests(game, out, check, trace)

        # ---- painted maps
        painted_tests(game, out, lights, check)

        # ---- filtered light: the coals narrowed to a few texels are still a light
        tris = narrow_coals(game)
        lit = [t for t in tris if t["glowing"] > 0]
        share = max((t["glowing"] / t["texels"] for t in lit), default=0)
        check(lit and share < 0.25 and all(sum(t["rgb"]) > 0 for t in lit),
              f"the coals narrowed to index 15: {len(lit)} light triangles with light, the hottest "
              f"{share:.0%} of a triangle's texels (a 15-point grid often missed them)")
        game.call("emissive", action="remove", id="E004")
        game.run(2)
        if trace:
            # ---- the ray-traced light: the forge's surroundings (the coals left out), path traced (Ultra)
            preset = game.call("graphics").get("lighting_preset")
            game.call("graphics", lighting_preset="ultra", gi=True)
            means = {}
            for value in (0, 1):
                game.call("graphics", emissive=value)
                game.run(SETTLE * 3)
                means[value] = np.mean([frame(game, out / f"trace-{value}-{k}.png") for k in range(4)], axis=0)
            lum0, lum1 = means[0].mean(2), means[1].mean(2)
            noise = float(np.abs(frame(game, out / "trace-1-again.png").mean(2) - lum1).mean())
            # the hood and the wall above the forge (the coals themselves are lower in the frame)
            near = (slice(0, h * 30 // 100), slice(w * 40 // 100, w * 85 // 100))
            lift = float(lum1[near].mean() - lum0[near].mean())
            check(lift >= max(0.3, 10 * noise), f"the forge lights the hood and wall above it: "
                  f"{lum0[near].mean():.2f} -> {lum1[near].mean():.2f} (+{lift:.2f} levels; frame noise {noise:.3f})")
            # narrowed to their hottest index (a few percent of the texels): still a light (light 1 against 0;
            # the coals glow on screen the same either way)
            narrowed = {}
            for weight in (0, 1):
                narrow_coals(game)
                game.call("emissive", action="set", id="E004", light=weight)
                game.run(SETTLE * 3)
                narrowed[weight] = np.mean([frame(game, out / f"trace-narrow-{weight}-{k}.png").mean(2)
                                            for k in range(4)], axis=0)
            nlift = float(narrowed[1][near].mean() - narrowed[0][near].mean())
            check(nlift >= max(0.15, 5 * noise), f"the coals narrowed to index 15 still light the hood: "
                  f"+{nlift:.2f} levels (light 1 against 0; frame noise {noise:.3f})")
            game.call("emissive", action="remove", id="E004")
            # ---- lights on objects: the magic crates (53/21) and a floating block's circle (32/3)
            object_light_tests(game, out, check)
            if preset:
                game.call("graphics", lighting_preset=preset)

        # ---- the matcher: curated entries in their rooms
        for (zone, room), ids in MATCH_ROOMS:
            if not goto(game, zone, room):
                check(False, f"warped to zone {zone} room {room}")
                continue
            if (zone, room) == (32, 3):
                show_platforms(game)
                game.run(SETTLE)
            if (zone, room) == (34, 0):  # the high windows: in the free camera's views (the game's culls them)
                for yaw in (270, 90):
                    window_camera(game, yaw)
                    game.run(10)
            game.run(10)
            marked = {e["id"]: e["marked"] for e in game.call("emissive")["entries"]}
            for i in ids:
                check(marked.get(i, 0) >= 1, f"{i} found in zone {zone} room {room} ({marked.get(i, 0)} textures)")

        # ---- editor round trip: a mask on the floor texture under the screen centre
        game.call("editor_camera", off=1)
        game.run(10)
        picked = None
        for y in range(560, 900, 40):
            p = game.call("emissive", action="pick", x=640, y=y)
            if p.get("valid") and p.get("room") and not p.get("entry"):
                picked = p
                break
        check(picked is not None, "a room texture picked in the view")
        if picked:
            w, h = picked["size"]
            entry = {"id": "U900", "name": "test mask", "match": [{"key": picked["key"], "size": [w, h]}],
                     "mask": {"rects": [[0, 0, max(1, w // 2), max(1, h // 2)]], "indices": list(range(0, 16))},
                     "color": [1, 0.5, 0.25], "strength": 2, "light": 0}
            game.call("emissive", action="set", entry=entry)
            game.run(4)
            before = game.call("emissive")
            texel = game.call("emissive", action="texel", u=picked["box"][0], v=picked["box"][1],
                              tpage=picked["tpage"], clut=picked["clut"])
            check(before["marked_texels"] > 0 and texel["slot"] > 0,
                  f"the new mask marks texels ({before['marked_texels']}, slot {texel['slot']})")
            game.call("emissive", action="save")
            saved = json.loads(user.read_text()) if user.exists() else {}
            ids = [e.get("id") for e in saved.get("entries", [])]
            check("U900" in ids, f"saved to the user file ({ids})")
            game.call("emissive", action="reload")
            game.run(4)
            after = game.call("emissive")
            texel2 = game.call("emissive", action="texel", u=picked["box"][0], v=picked["box"][1],
                               tpage=picked["tpage"], clut=picked["clut"])
            mine = [e for e in after["entries"] if e["id"] == "U900"]
            check(bool(mine) and mine[0]["mask"] == entry["mask"] and mine[0]["match"][0]["key"] == picked["key"],
                  "reloaded: the same entry")
            check(after["marked_texels"] == before["marked_texels"] and texel2 == texel,
                  f"reloaded: the same texels and emission ({before['marked_texels']} -> {after['marked_texels']})")
            exported = game.call("emissive", action="export", id="U900")
            check('"key"' in exported.get("json", ""), "export gives the entry's JSON for the curated catalog")
    finally:
        game.close()
    if not args.out and not failures:
        shutil.rmtree(out, ignore_errors=True)
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
