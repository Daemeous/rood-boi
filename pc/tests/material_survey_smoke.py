#!/usr/bin/env python3
"""The material survey (pc/docs/Materials.md, "Surveying a room"): RPC
material survey lists the textures in view with their share of it, agrees
with the texture picker, and the survey's one-click preset (RPC material new
{key, palette, preset}, the panel's "Textures in this room") creates an entry,
for the texture or for its whole palette (a palette entry), live in the user
layer.

    python3 pc/tests/material_survey_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Continues from the card's save room with empty scratch catalogs:

- survey: valid, a grid of the view's aspect, textures sorted by share (each
  > 0, together the textured share), each with key, size, palette and boxes;
  its attribution map agrees with RPC material pick at sampled pixels (the
  back-face rule and the depth test); the survey with both faces drawn
  covers at least as much;
- texture: a listed texture's texels (RGBA, its size);
- new {key, palette, preset}: an entry with the preset's values, this room,
  this palette, review "new", matched at once (RPC material list uses, the
  survey's entry) and unsaved until Save, which writes it to the user layer;
- new {..., whole_palette: true}: a palette entry (no key) that every
  texture of that palette in view takes, except one with its own entry;
  Save writes it without key or size and it parses back after a reload;
- the tool's helpers (pc/tools/material_survey.py: angles, atlas groups).
"""
import argparse
import base64
import json
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import material_survey  # noqa: E402

EMPTY = {"format": 1, "presets": {}, "entries": []}
PRESETS = {"rough stone": {"roughness": 0.82, "specular": 1, "metal": 0, "auto_height": True},
           "worn wood": {"roughness": 0.8, "specular": 1, "metal": 0}}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep the logs here")
    args = parser.parse_args()
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    # The tool's helpers (no game).
    yaw, pitch = material_survey.yaw_pitch([-0.5, 0.70710678, -0.5])
    check(abs(yaw + 135) < 0.01 and abs(pitch + 45) < 0.01, f"forward to free-camera angles: {yaw:.2f}, {pitch:.2f}")
    t = lambda page, pal, box: dict(tpage=page, palette=pal, boxes=[box])  # noqa: E731
    g = material_survey.groups([t(20, 1, [0, 0, 30, 32]), t(20, 1, [0, 30, 30, 62]), t(20, 2, [0, 0, 30, 30]),
                                t(21, 1, [0, 0, 30, 30])])
    check(g[0] == g[1] and len(set(g)) == 3, f"atlas groups: overlapping boxes of one page and palette only: {g}")

    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77 if not failures else 1
    out = pathlib.Path(args.out) if args.out else OUT / "material_survey"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    shipped, user = out / "catalog.json", out / "materials.json"
    shipped.write_text(json.dumps(dict(EMPTY, presets=PRESETS)))
    user.unlink(missing_ok=True)
    (out / "lights").mkdir(exist_ok=True)
    (out / "settings.cfg").write_text("light_animation 0\ndust 0\n")
    env = {"ROOD_LIGHTS": str(out / "lights"), "ROOD_LIGHTING": str(ROOT / "lighting"), "ROOD_SCALE": "2",
           "ROOD_WINDOW": "1280x960", "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_MATERIAL_CATALOG": str(shipped), "ROOD_MATERIAL_CATALOG_USER": str(user),
           "ROOD_MATERIALS": str(out / "maps"), "ROOD_MATERIALS_SHIPPED": str(out / "shipped-maps")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-survey-smoke-{os.getpid()}.sock"
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        game.call("graphics", interpolate=False)
        game.run(20)
        game.call("redraw")
        game.wait_presents(4)
        s = game.call("material", action="survey", grid=96, map=True)
        check(s.get("valid"), "survey of the game camera's view")
        if not s.get("valid"):
            return 1
        gw, gh = s["grid"]
        textures = s["textures"]
        shares = [x["share"] for x in textures]
        check(gw == 96 and gh == 72, f"a grid of the view's aspect: {gw} x {gh}")
        check(len(textures) >= 5 and all(v > 0 for v in shares) and shares == sorted(shares, reverse=True),
              f"{len(textures)} textures, sorted by share (top {shares[:3]})")
        check(abs(sum(shares) - s["textured"]) < 0.01 and s["textured"] > 0.3,
              f"the shares add up to the textured view ({sum(shares):.3f} vs {s['textured']:.3f})")
        check(all(len(x["key"]) == 10 and x["size"][0] > 0 and x["boxes"] for x in textures),
              "each texture has its key, size and boxes")
        check(len(s["map"]) == gw * gh and max(s["map"]) == len(textures) - 1, "the attribution map")
        agree = total = 0
        for gy in range(4, gh, 9):
            for gx in range(4, gw, 9):
                pick = game.call("material", action="pick", x=(gx + 0.5) / gw * 1280, y=(gy + 0.5) / gh * 960)
                index = s["map"][gy * gw + gx]
                mine = textures[index]["key"] if index >= 0 else None
                if pick.get("valid") or mine:
                    total += 1
                    agree += pick.get("key") == mine
        check(total >= 20 and agree >= 0.85 * total, f"the survey agrees with the picker at {agree} of {total} pixels")
        both = game.call("material", action="survey", grid=96, cull=0)
        check(both["textured"] + both["untextured"] >= s["textured"] + s["untextured"] - 1e-6,
              "with both faces the survey covers at least as much")

        top = next((x for x in textures if x["size"][0] * x["size"][1] >= 16), textures[0])
        got = game.call("material", action="texture", key=top["key"], palette=top["clut"])
        rgba = base64.b64decode(got["rgba"])
        check(got["w"] == top["size"][0] and got["h"] == top["size"][1] and len(rgba) == got["w"] * got["h"] * 4,
              f"texture {top['key']}: {got['w']} x {got['h']} RGBA")

        made = game.call("material", action="new", key=top["key"], palette=top["clut"], preset="worn wood",
                         share=top["share"])
        state = game.state()
        check(made.get("preset") == "worn wood" and made.get("review") == "new"
              and made["match"].get("key") == top["key"] and made.get("scope", {}).get("room") == [state["zone"], state["room"]]
              and (made["match"].get("clut") or "") == top["clut"] and made["value"]["roughness"] == 0.8,
              f"one click: an entry with the preset for the texture, this room and palette: {made}")
        game.call("redraw")
        game.wait_presents(2)
        listing = game.call("material")
        check(any(u["entry"] == made["id"] for u in listing["uses"]) and listing["unsaved"],
              f"live (matched at once, unsaved): {[u['entry'] for u in listing['uses']]}")
        again = game.call("material", action="survey", grid=96)
        check(any(x["key"] == top["key"] and x["entry"] == made["id"] for x in again["textures"]),
              "the survey shows the texture's entry")

        # A palette entry: the most common palette in view (two textures at least).
        counts = {}
        for x in again["textures"]:
            if x["clut"]:
                counts[x["clut"]] = counts.get(x["clut"], 0) + 1
        palette = max(counts, key=counts.get) if counts else ""
        other = next((x for x in again["textures"] if x["clut"] == palette and x["key"] != top["key"]), None)
        check(bool(palette) and counts[palette] >= 2 and other is not None,
              f"a palette with several textures in view: {palette} ({counts.get(palette)})")
        if other:
            wide = game.call("material", action="new", key=other["key"], palette=palette, preset="rough stone",
                             whole_palette=True)
            check("key" not in wide["match"] and "size" not in wide["match"] and wide["match"].get("clut") == palette,
                  f"whole palette: a palette entry {wide['id']}: {wide['match']}")
            game.call("redraw")
            game.wait_presents(2)
            third = game.call("material", action="survey", grid=96)
            mine = [x for x in third["textures"] if x["clut"] == palette]
            want = [made["id"] if x["key"] == top["key"] and top["clut"] == palette else wide["id"] for x in mine]
            check([x["entry"] for x in mine] == want,
                  f"every texture of the palette takes it, but one with its own entry ({len(mine)} textures)")
            saved = game.call("material", action="save")
            on_disk = json.loads(user.read_text()) if user.exists() else {}
            ids = [e["id"] for e in on_disk.get("entries", [])]
            check(not saved["unsaved"] and made["id"] in ids and wide["id"] in ids,
                  f"Save writes both into the user layer: {ids}")
            written = next((e for e in on_disk.get("entries", []) if e["id"] == wide["id"]), {})
            check(written.get("match") == {"clut": palette} and written.get("preset") == "rough stone",
                  f"the palette entry as written: {written}")
            game.call("material", action="reload")
            game.call("redraw")
            game.wait_presents(2)
            fourth = game.call("material", action="survey", grid=96)
            check([x["entry"] for x in fourth["textures"] if x["clut"] == palette] == want,
                  "read back from the file, it matches the same textures")
    finally:
        game.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("material survey: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
