#!/usr/bin/env python3
"""An emissive entry's palette (`clut`) applies through the palette's animation
(pc/docs/Materials.md "Animated palettes", pc/docs/Extensions.md).

    python3 pc/tests/emissive_palette_anim_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The Catacombs' first room (9/0) entered with its enemies (no scripts): their
palettes animate (CLUT uploads that flash and cycle colours while the texels
stay). RPC material palettes names the drawn palettes by CLUT slot: the colours'
key now, the slot's identity (its most-drawn variant) and its variants this room.

- An animated slot is found (its key changes over the frames sampled), and one
  of its textures among the emissive uses (RPC emissive uses keys);
- an emissive entry for that texture with `clut` the slot's identity (scoped to
  the room, in a scratch user layer under pc/build) is read while the slot shows
  another variant (four times): it applies on the next frame; then over
  consecutive frames, the ones where the slot's colours are another variant
  included, the texture's uses keep the entry (RPC emissive uses matched): no
  frame drops the glow;
- the emissive picker (RPC emissive pick, over a grid of the screen) on the
  slot while it shows another variant names its palette by the identity (what
  "Only with the picked texture's palette" writes).
"""
import argparse
import collections
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

ROOM = (9, 0)
ENTRY = "UPALANIM"


def goto(game, zone, room):
    game.call("editor_camera", off=1)
    game.call("room_goto", zone=zone, room=room, enemies=True, scripts=False)
    for _ in range(60):
        game.run(20)
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
    game.run(40)
    state = game.state()
    return (state["zone"], state["room"]) == (zone, room)


def palettes(game):
    return {(p["depth"], p["clut"]): p for p in game.call("material", action="palettes")["palettes"]}


def slot_uses(game, slot, matched=False):
    return [u for u in game.call("emissive", action="uses", keys=True, matched=matched)["uses"]
            if ((u["tpage"] >> 7) & 3, u["clut"]) == slot]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep the logs here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "emissive_palette_anim"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    (out / "lights").mkdir()
    (out / "settings.cfg").write_text("light_animation 0\ndust 0\naa off\n")
    user = out / "emissive.json"
    user.write_text(json.dumps({"format": 1, "entries": []}))
    materials = out / "materials.json"
    materials.write_text(json.dumps({"format": 1, "presets": {}, "entries": []}))
    env = {"ROOD_LIGHTS": str(out / "lights"), "ROOD_GI": "0", "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_EMISSIVE_USER": str(user), "ROOD_MATERIAL_CATALOG_USER": str(materials),
           "ROOD_MATERIALS": str(out / "maps"), "ROOD_MATERIALS_BAKED": str(out / "baked"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-empal-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        if not goto(game, *ROOM):
            print(f"FAIL: could not reach {ROOM}")
            return 1
        # 1. An animated slot with a texture the emissive uses list (hashed: keys).
        seen = collections.defaultdict(set)
        for _ in range(240):
            game.run(1)
            for slot, p in palettes(game).items():
                seen[slot].add(p["key"])
        animated = sorted(((len(v), s) for s, v in seen.items() if len(v) > 1), reverse=True)
        check(bool(animated), f"an animated palette in {ROOM}: {[(s, n) for n, s in animated]}")
        slot = texture = None
        for _, s in animated:
            found = [u for u in slot_uses(game, s) if u["key"]]
            if found:
                # (the biggest texture of the slot: the most texels to see)
                slot = s
                texture = max(found, key=lambda u: (u["box"][2] - u["box"][0] + 1) * (u["box"][3] - u["box"][1] + 1))
                break
        check(texture is not None, f"an emissive use on an animated slot: {slot} {texture and texture['key']}")
        if texture is None:
            return 1
        now = palettes(game)[slot]
        identity = now["identity"]
        check(identity in seen[slot], f"slot {slot}: identity {identity} is one of its {now['variants']} variants")
        size = [texture["box"][2] - texture["box"][0] + 1, texture["box"][3] - texture["box"][1] + 1]

        # 2. An emissive entry with the identity as its palette glows on every frame.
        user.write_text(json.dumps({"format": 1, "entries": [
            {"id": ENTRY, "name": "palette animation test", "match": [
                {"key": texture["key"], "size": size, "clut": identity}],
             "mask": {}, "color": [1, 0.5, 0.25], "strength": 1, "light": 0,
             "scope": {"room": list(ROOM)}}]}, indent=1))
        # (read while the slot shows another variant: every use is matched again then)
        def other_variant():
            for _ in range(600):
                p = palettes(game).get(slot)
                if p and p["key"] != identity:
                    return True
                game.run(1)
            return False

        late = 0
        for round in range(4):
            check(other_variant(), f"read {round + 1}: a frame in another variant")
            listed = game.call("emissive", action="reload")
            game.run(1)
            uses = [u for u in slot_uses(game, slot) if u["key"] == texture["key"]]
            late += not uses or any(u["entry"] != ENTRY for u in uses)
        check(any(e["id"] == ENTRY for e in listed["entries"]), "the scratch layer's entry is read")
        check(late == 0, f"read while another variant shows, the entry applies on the next frame ({late} of 4 late)")
        frames = other = dropped = 0
        for _ in range(240):
            game.run(1)
            p = palettes(game).get(slot)
            uses = [u for u in slot_uses(game, slot) if u["key"] == texture["key"]]
            if p is None or not uses:
                continue
            frames += 1
            other += p["key"] != identity
            dropped += any(u["entry"] != ENTRY for u in uses)
        check(frames > 100 and other > 0,
              f"the texture drawn in {frames} frames, {other} of them in another variant than its identity")
        check(dropped == 0, f"the texture keeps the emissive entry in every frame ({dropped} dropped)")

        # 3. The picker names the slot's palette by its identity while another variant shows.
        picked = None
        other_variant()
        p = palettes(game).get(slot)
        check(p is not None and p["key"] != identity, f"a frame in another variant ({p and p['key']})")
        # (the entry's surfaces' screen points first, then a grid of the window)
        points = [tuple(xy) for u in game.call("emissive", action="surfaces", id=ENTRY)["surfaces"]
                  for xy in u.get("screen", [])]
        points += [(x, y) for y in range(16, 768, 32) for x in range(16, 1024, 32)]
        for x, y in points:
            r = game.call("emissive", action="pick", x=x, y=y)
            if r.get("valid") and ((r["tpage"] >> 7) & 3, r["clut"]) == slot:
                picked = r
                break
        check(picked is not None, f"the picker finds the slot on screen ({picked and picked['key']})")
        if picked:
            check(picked["clut_key"] == identity,
                  f"the picker names the palette by its identity ({picked['clut_key']}, identity {identity})")
    finally:
        game.close()
    print("PASS" if not failures else f"FAIL ({len(failures)})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
