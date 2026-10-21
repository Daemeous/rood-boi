#!/usr/bin/env python3
"""The depth of field's camera subject (dof_focus `subject`, the default;
pc/docs/Post_Processing.md, post_profile.cpp camera_subject).

    python3 pc/tests/dof_subject_smoke.py [--card pc/build/test-card1.mcd] [--shots DIR]

Deterministic (ROOD_DETERMINISTIC, RPC-exact stepping, the charging skip
off), 960 x 720, no lights or path tracing, on the renderer selected
(ROOD_RENDERER):

1. The save room (checkpoint `save_room`, needs the card), field play: the
   default focus is `subject` (with the rules' defaults); the subject is
   Ashley ("field") and the focus is exactly what `ashley` gives, her chest's
   view depth (checked against her position and the game's camera).
2. Zone 48 room 10 with its enemies (the battle corpus recipe
   enemy_cast_wait, pc/tests/diff/scenarios.py): an enemy casts at Ashley.
   From the wind-up (_cameraMode 4) the subject is the caster ("action");
   the focus eases there (between the two depths on the way, not a jump) and
   during the cast (mode 5) it is the caster's chest depth, measured here
   from its position and the camera, not Ashley's. The caster's pixels (at
   its depth in the depth view) are sharper than with focus on Ashley, and
   Ashley's are softer (f/1.2, 300 mm). After the
   action (the camera's return, mode 11) the subject is Ashley again and the
   focus eases back to her depth.
3. A rule as data: dof_subject_keep_ashley 1 keeps Ashley through the next
   enemy action aimed at her ("targets_ashley").
4. Targeting (zone 9 room 18, enemy_kill's approach): with the targeting
   sphere up the subject is the targeted actor, the focus on the body part
   the camera turns to (near that actor's chest depth).
No crash, shader error or post message. --shots saves pictures (game
imagery: keep them local).
"""
import argparse
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
from post_smoke import edge_energy, masks, redrawn  # noqa: E402
from dof_smoke import depth_sz  # noqa: E402

ROOM = (48, 10)  # enemy_cast_wait's room
TARGET_ROOM = (9, 18)  # enemy_kill's
POSITION = 28  # D_800F4538_t unk0.position (3 shorts, world units, y down)
CHEST = 96  # the subject's point above its feet (post_profile.cpp ashley_depth, actor_chest)
# A long lens wide open, the foreground blurred as much as the background: a few pixels of blur at the
# 10% depth difference between the caster and Ashley.
LENS = {"dof": True, "dof_model": "physical", "dof_fstop": 1.2, "dof_focal_length": 300, "dof_max_blur": 24,
        "dof_near_blur": 1}
failures = []


def check(ok, message):
    print(("ok   " if ok else "FAIL ") + message, flush=True)
    if not ok:
        failures.append(message)


def pointer_size():
    return 8 if os.environ.get("ROOD_ARCH") == "64" else 4


def actor_feet(game, slot):
    """An actor slot's feet (D_800F4538[slot] position), or None when the slot is empty."""
    width = pointer_size()
    actor = int.from_bytes(game.read(symbols()["D_800F4538"] + slot * width, width), "little")
    if not actor:
        return None
    return struct.unpack("<3h", game.read(actor + POSITION, 6))


def view(game, point):
    """A world point in the game camera's view (RPC scene camera): (x, y, depth SZ) and the projection."""
    cam = game.call("scene")["camera"]
    r, t = np.array(cam["rotation"]), np.array(cam["translation"])
    v = r @ np.array(point, dtype=np.float64) + t
    return v, cam


def chest_depth(game, slot):
    feet = actor_feet(game, slot)
    if feet is None:
        return None
    return float(view(game, (feet[0], feet[1] - CHEST, feet[2]))[0][2])


def screen_box(game, slot, shape, half=60):
    """The window pixels around an actor (its chest projected: GTE offset + H x / z, the 320 x 240 picture
    scaled to the window), as a boolean mask."""
    feet = actor_feet(game, slot)
    v, cam = view(game, (feet[0], feet[1] - CHEST, feet[2]))
    x = cam["offset"][0] + cam["h"] * v[0] / v[2]
    y = cam["offset"][1] + cam["h"] * v[1] / v[2]
    sx, sy = shape[1] / 320.0, shape[0] / 240.0
    cx, cy = int(x * sx), int(y * sy)
    mask = np.zeros(shape[:2], bool)
    mask[max(0, cy - half): cy + half, max(0, cx - half): cx + half] = True
    return mask, (cx, cy)


def step_until(game, condition, limit, track=None):
    """Run a vsync at a time until condition(post reply) holds (at most `limit` vsyncs); the reply, or None."""
    for _ in range(limit):
        game.run(1)
        r = game.call("post")
        if track is not None:
            track.append(r)
        if condition(r):
            return r
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD", str(ROOT / "pc/build/test-card1.mcd")))
    parser.add_argument("--shots", help="save pictures here (game imagery: keep local)")
    args = parser.parse_args()
    card = pathlib.Path(args.card)
    if not card.exists():
        print("SKIP: no pinned test card", card)
        return 77
    out = OUT / "dof_subject"
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)

    def save(name, image):
        if shots:
            Image.fromarray(np.clip(image, 0, 255).astype(np.uint8)).save(shots / f"{name}.png")

    work = pathlib.Path(tempfile.mkdtemp(prefix="dof-subject-", dir=out))
    (work / "lights").mkdir()
    shutil.copyfile(card, work / "card1.mcd")
    settings = work / "settings.txt"
    settings.write_text("ui_menus 0\nwidescreen 0\ninterpolate 0\nqol_skip_charging 0\ncamera_stick 0\n")
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(work / "lights"), "ROOD_GI": "0", "ROOD_SCALE": "2",
           "ROOD_SETTINGS": str(settings), "ROOD_DETERMINISTIC": "1"}
    if vulkan and os.environ.get("VK_LAYER_PATH"):
        env["ROOD_VK_VALIDATE"] = "1"
    game = launch_at("save_room", card=str(work / "card1.mcd"), fast=True, log=out / "game.log", extra_env=env)
    try:
        run(game, out, save)
    finally:
        game.close()
    text = pathlib.Path(game.log).read_text(errors="replace")
    bad = [line for line in text.splitlines() if "validation:" in line or "[crash]" in line or "shader" in line
           and "error" in line.lower() or "[post]" in line]
    check(not bad, "no crash, shader error, validation or post message" + (f": {bad[:3]}" if bad else ""))
    shutil.rmtree(work, ignore_errors=True)
    if failures:
        print(f"FAIL: {len(failures)} checks failed")
        return 1
    print("PASS: the camera subject: Ashley in the field, the caster during its cast, Ashley again after")
    return 0


def run(game, out, save):
    game.call("graphics", aa="off")
    # ---- 1. Field play: the default is the camera subject, which is Ashley there ----
    r = game.call("post")
    t = r["target"]
    check(t["dof_focus"] == "subject" and t["dof_subject_others"] == "actor" and t["dof_subject_ashley"] == "actor"
          and not t["dof_subject_keep_ashley"] and t["dof_subject_targeting"] == "target",
          f"default focus: the camera subject ({t['dof_focus']}; others {t['dof_subject_others']}, ashley "
          f"{t['dof_subject_ashley']}, keep {t['dof_subject_keep_ashley']}, targeting {t['dof_subject_targeting']})")
    game.call("post", layer="gameplay", clear="all", set=LENS, snap=True)
    game.run(2)
    r = game.call("post")
    s = r["subject"]
    ashley = chest_depth(game, 0)
    subject_focus = r["focus"]
    game.call("post", layer="gameplay", set={"dof_focus": "ashley"}, snap=True)
    game.run(2)
    ashley_focus = game.call("post")["focus"]
    check(s["kind"] == "ashley" and s["why"] == "field" and subject_focus == ashley_focus
          and abs(subject_focus - ashley) < 0.01 * ashley,
          f"field: the subject is Ashley ({s['kind']}, {s['why']}), focus SZ {subject_focus:.1f} = On Ashley's "
          f"{ashley_focus:.1f} (her chest {ashley:.1f})")
    game.call("post", layer="gameplay", clear="all", set=LENS, snap=True)

    # ---- 2. An enemy's cast in enemy_cast_wait's room ----
    game.call("room_goto", zone=ROOM[0], room=ROOM[1], enemies=True, scripts=False)
    for _ in range(80):
        game.run(20)
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
    state = game.state()
    check((state["zone"], state["room"]) == ROOM, f"in zone {ROOM[0]} room {ROOM[1]} ({state['zone']}, {state['room']})")
    r = step_until(game, lambda r: r["subject"]["why"] == "field" and not r["subject"]["pulling"], 120)
    check(r is not None, "the room's field play: the subject is Ashley")
    before = r["focus"] if r else 0
    track = []
    r = step_until(game, lambda r: r["subject"]["why"] == "action", 900, track)
    if r is None:
        check(False, "an enemy acts within 900 vsyncs")
        return
    s = r["subject"]
    caster = s["performer"]
    check(caster > 0 and s["kind"] == "actor" and s["actor"] == caster and s["mode"] == 4,
          f"the wind-up (mode {s['mode']}): the subject is the caster, actor {s['actor']} (performer {caster}, "
          f"aimed at {s['action_target']})")
    # The cast (mode 5), the focus settled there.
    r = step_until(game, lambda r: r["subject"]["mode"] == 5 and not r["subject"]["pulling"], 600, track)
    if r is None:
        check(False, "the cast (mode 5) with the focus settled")
        return
    game.run(20)
    r = game.call("post")
    s = r["subject"]
    caster_depth, ashley = chest_depth(game, caster), chest_depth(game, 0)
    focus = r["focus"]
    check(s["kind"] == "actor" and s["actor"] == caster and abs(focus - caster_depth) < 0.02 * caster_depth
          and abs(caster_depth - ashley) > 0.04 * ashley and abs(focus - caster_depth) < abs(focus - ashley),
          f"the cast (mode {s['mode']}): focus SZ {focus:.0f} on the caster's chest {caster_depth:.0f}, "
          f"not Ashley's {ashley:.0f}")
    way = [x["focus"] for x in track if x["subject"]["kind"] == "actor"]
    lo, hi = sorted((before, caster_depth))
    between = [x for x in way if lo + 0.1 * (hi - lo) < x < hi - 0.1 * (hi - lo)]
    check(len(between) >= 3, f"the focus eases to the caster: SZ {before:.0f} -> {caster_depth:.0f} through "
          + ", ".join(f"{x:.0f}" for x in way[:12]))
    # The picture: the caster's own pixels (around its chest on screen, at its depth in the depth view) are
    # sharper focused on it than with focus on Ashley, and Ashley's the other way round.
    image = redrawn(game, out / "cast")
    save("cast-subject", image)
    _, _, depth = masks(game, out)
    sz = depth_sz(depth)
    game.call("post", layer="gameplay", set={"dof_focus": "ashley"}, snap=True)
    on_ashley = redrawn(game, out / "cast")
    save("cast-on-ashley", on_ashley)
    game.call("post", layer="gameplay", set={"dof_focus": "subject"}, snap=True)
    for slot, name, mine, other in ((caster, "the caster", image, on_ashley), (0, "Ashley", on_ashley, image)):
        box, centre = screen_box(game, slot, image.shape)
        d = chest_depth(game, slot)
        own = box & (np.abs(sz - d) < 0.04 * d)
        sharp, soft = (edge_energy(mine, own), edge_energy(other, own)) if own.sum() > 200 else (0, 0)
        check(sharp > 1.1 * soft, f"{name} (around {centre}, {int(own.sum())} pixels at its depth) is sharper "
              f"focused on: edge energy {sharp:.1f} vs {soft:.1f}")
    # After the action: Ashley again, eased back.
    track = []
    r = step_until(game, lambda r: r["subject"]["kind"] == "ashley" and r["subject"]["why"] == "field"
                   and not r["subject"]["pulling"], 900, track)
    if r is None:
        check(False, "after the action the subject is Ashley again, the focus settled")
        return
    game.run(10)
    r = game.call("post")
    ashley = chest_depth(game, 0)
    way = [x["focus"] for x in track if x["subject"]["kind"] == "ashley"]
    lo, hi = sorted((caster_depth, ashley))
    between = [x for x in way if lo + 0.1 * (hi - lo) < x < hi - 0.1 * (hi - lo)]
    check(abs(r["focus"] - ashley) < 0.02 * ashley and len(between) >= 3,
          f"after the action: focus SZ {r['focus']:.0f} on Ashley's chest {ashley:.0f}, eased back through "
          + ", ".join(f"{x:.0f}" for x in way[:12]))
    save("after", redrawn(game, out / "after"))

    # ---- 3. A rule: keep Ashley when an enemy's action targets her ----
    game.call("post", layer="gameplay", set={"dof_subject_keep_ashley": True}, snap=True)
    track = []
    r = step_until(game, lambda r: 4 <= r["subject"]["mode"] <= 10 and r["subject"]["performer"] > 0, 900, track)
    if r is None:
        seen = []
        for x in track:
            k = tuple(x["subject"][f] for f in ("kind", "why", "state", "mode", "performer", "action_target"))
            if not seen or seen[-1] != k:
                seen.append(k)
        check(False, f"another enemy action within 900 vsyncs ({seen[:20]})")
    else:
        s = r["subject"]
        check(s["action_target"] == 0 and s["kind"] == "ashley" and s["why"] == "targets_ashley",
              f"dof_subject_keep_ashley: an enemy's action at Ashley keeps her ({s['kind']}, {s['why']}, performer "
              f"{s['performer']} aimed at {s['action_target']})")
    game.call("post", layer="gameplay", set={"dof_subject_keep_ashley": False}, snap=True)

    # ---- 4. Targeting: the targeted actor (enemy_kill's room and approach, scenarios.py) ----
    game.call("room_goto", zone=TARGET_ROOM[0], room=TARGET_ROOM[1], enemies=True, scripts=False)
    for _ in range(80):
        game.run(20)
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
    game.press("circle", 3, 30)  # the weapon drawn
    game.run(600)  # the nearer Zombie comes
    game.hold(["right", "down"], 50)
    game.press("circle", 3, 2)  # the targeting sphere
    track = []
    r = step_until(game, lambda r: r["subject"]["why"] == "targeting" and not r["subject"]["pulling"], 300, track)
    if r is None:
        check(False, "the targeting sphere: a target chosen, the focus settled on it")
    else:
        game.run(4)
        r = game.call("post")
        s = r["subject"]
        chest = chest_depth(game, s["actor"]) if s["actor"] > 0 else None
        check(s["kind"] == "target" and s["actor"] > 0 and chest and abs(r["focus"] - s["depth"]) < 0.02 * s["depth"]
              and abs(s["depth"] - chest) < 0.15 * chest,
              f"targeting: the subject is the targeted actor {s['actor']}, focus SZ {r['focus']:.0f} on its body part "
              f"{s['depth']:.0f} (its chest {chest if chest is None else round(chest)})")
        save("targeting", redrawn(game, out / "target"))
    game.call("post", layer="gameplay", clear="all", snap=True)


if __name__ == "__main__":
    sys.exit(main())
