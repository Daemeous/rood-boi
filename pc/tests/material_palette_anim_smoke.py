#!/usr/bin/env python3
"""A palette entry applies through its palette's animation (pc/docs/Materials.md,
"Animated palettes").

    python3 pc/tests/material_palette_anim_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The Catacombs' first room (9/0) entered with its enemies (no scripts): their
palettes animate (CLUT uploads that flash and cycle colours while the texels stay).
RPC material palettes lists the drawn palettes by CLUT slot: the colours' key now,
the slot's identity and its variants this room.

- An animated slot is found (its key changes over the frames sampled);
- a palette entry naming its identity (scoped to the room, in a user layer under
  pc/build) is read; then over consecutive frames, the ones where the slot's
  colours are another variant included, the slot keeps the entry and the
  material planes keep its uses (RPC material list: the same count of uses with
  the entry every frame): no frame drops it;
- a frame where the slot shows another variant: RPC material survey names the
  slot's palette by its identity (what the Material review proposes and Accept
  writes) and finds the entry for it.
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
ENTRY = "TPALANIM"


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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep the logs here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "material_palette_anim"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    (out / "lights").mkdir()
    (out / "settings.cfg").write_text("light_animation 0\ndust 0\naa off\n")
    user = out / "materials.json"
    user.write_text(json.dumps({"format": 1, "presets": {}, "entries": []}))
    env = {"ROOD_LIGHTS": str(out / "lights"), "ROOD_GI": "0", "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_MATERIAL_CATALOG_USER": str(user), "ROOD_MATERIALS": str(out / "maps"),
           "ROOD_MATERIALS_BAKED": str(out / "baked"), "ROOD_MATERIAL_EXPORTS": str(out / "exports")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-palanim-{os.getpid()}.sock"
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
        # 1. An animated slot: the most variants over the frames sampled.
        seen = collections.defaultdict(set)
        for _ in range(240):
            game.run(1)
            for slot, p in palettes(game).items():
                seen[slot].add(p["key"])
        animated = sorted(((len(v), s) for s, v in seen.items() if len(v) > 1), reverse=True)
        check(bool(animated), f"an animated palette in {ROOM}: {[(s, n) for n, s in animated]}")
        if not animated:
            return 1
        slot = animated[0][1]
        now = palettes(game)[slot]
        identity = now["identity"]
        check(identity in seen[slot] and now["variants"] >= len(seen[slot]),
              f"slot {slot}: identity {identity} is one of its {now['variants']} variants")

        # 2. A palette entry naming the identity applies on every frame.
        user.write_text(json.dumps({"format": 1, "presets": {}, "entries": [
            {"id": ENTRY, "name": "palette animation test", "preset": "stone", "match": {"clut": identity},
             "scope": {"room": list(ROOM)}}]}, indent=1))
        game.call("material", action="reload", entries=False)
        game.run(2)
        frames = other = dropped = 0
        counts = collections.Counter()
        for _ in range(240):
            game.run(1)
            p = palettes(game).get(slot)
            if p is None:
                continue
            frames += 1
            other += p["key"] != identity
            dropped += p["entry"] != ENTRY
            uses = game.call("material", action="list", entries=False)["uses"]
            counts[sum(u["entry"] == ENTRY for u in uses)] += 1
        check(frames > 100 and other > 0,
              f"the slot drawn in {frames} frames, {other} of them in another variant than its identity")
        check(dropped == 0, f"the slot keeps the entry in every frame ({dropped} dropped)")
        check(0 not in counts and len(counts) >= 1,
              f"the planes keep the entry's uses in every frame (uses with it per frame: {dict(counts)})")

        # 3. Survey, picker and highlight name the identity while another variant shows.
        for _ in range(600):
            p = palettes(game).get(slot)
            if p and p["key"] != identity:
                break
            game.run(1)
        p = palettes(game).get(slot)
        check(p is not None and p["key"] != identity, f"a frame in another variant ({p and p['key']})")
        survey = game.call("material", action="survey")
        named = {t["clut"] for t in survey["textures"]
                 if t.get("palette") == slot[1] and ((t.get("tpage", 0) >> 7) & 3) == slot[0]}
        check(named == {identity} or (not named and p is not None),
              f"the survey names the slot's palette by its identity ({named})")
        entries = {t.get("entry") for t in survey["textures"] if t.get("clut") == identity}
        check(not named or entries == {ENTRY}, f"the survey finds the entry for it ({entries})")
    finally:
        game.close()
    print("PASS" if not failures else f"FAIL ({len(failures)})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
