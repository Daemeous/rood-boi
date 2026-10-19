#!/usr/bin/env python3
"""Look and timing values that live in data, not code (settings keys, post
fields, the fog's medium record, data files read live; pc/README.md): each one
changes what it should when set, and at its default the picture is as before.

    python3 pc/tests/tunables_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

The save room (checkpoint save_room, a copy of the card), 960 x 720, internal
resolution 2x, path tracing off, a light added over Ashley (so the lit path,
and its soft shoulder, runs), the game paused (frames redrawn):

- RPC settings {apply, get}: every key reads back its default, takes another
  value and goes back (weapon slots' name time, labels and hint; smart walls'
  timing; the fog's detail motion; the post fields as post_<field>; button
  prompt colours, written only when changed);
- post shoulder_start / shoulder_width bend the lit highlights (the 3D
  changes, the 2D/UI not), and back at the defaults the picture is the base
  pixel for pixel; tilt_feather widens the blur's ramp past the sharp band;
  sharpen_gain raises the sharpened 3D's edge energy; vignette_gain darkens
  the corners further;
- button_prompts_color_playstation_circle recolours the circle icon (RPC
  button_prompts icon_color: the native menus' image) and bumps the prompts'
  generation; back at the default the same colour as before;
- smart walls (gameplay_walls_fade_out, _fade_in, _clear): sampled every
  vsync while the camera turns (R1, then L1 back), walls are seen part-way
  faded by default and never with both fades 1 frame;
- the fog's detail motion: RPC environment reports fog_detail_wind, _rise,
  fog_flow_cycle as set by the settings;
- fog presets: a volume naming a preset follows an edit of the presets file
  (ROOD_FOG_PRESETS: a copy) without any RPC (polled about twice a second),
  its own values kept.
The emitter assets' live reload is particles_smoke's, the fog detail's
picture atmosphere_smoke's, the weapon slots' name time and labels
weapon_slots_smoke's.
"""
import argparse
import json
import os
import pathlib
import shutil
import sys
import tempfile
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from post_smoke import edge_energy, luma, masks, redrawn  # noqa: E402

failures = []

# Every new key, its default as the settings file has it, and another value.
KEYS = [
    ("qol_weapon_slots_name_ticks", "150", "40"),
    ("qol_weapon_slots_label0", "242 99 237 101", "230 90 225 92"),
    ("qol_weapon_slots_label7", "81 124 80 127", "70 120 69 123"),
    ("qol_weapon_slots_hint", "160 142", "150 150"),
    ("gameplay_walls_fade_out", "4", "1"),
    ("gameplay_walls_fade_in", "7", "2"),
    ("gameplay_walls_clear", "10", "3"),
    ("fog_detail_wind", "1.3", "0.5"),
    ("fog_detail_rise", "0.01", "0.05"),
    ("fog_flow_cycle", "2.5", "4"),
]
POST_KEYS = [("sharpen_gain", "1.5", "3"), ("vignette_gain", "1.2", "2"), ("tilt_feather", "0.25", "0.5"),
             ("shoulder_start", "0.8", "0.6"), ("shoulder_width", "0.2", "0.4")]


def check(ok, message):
    print(("ok   " if ok else "FAIL ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    parser.add_argument("--shots", help="save pictures here (game imagery: keep local)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = OUT / "tunables"
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="tunables-", dir=out))
    card, lights, presets = work / "card1.mcd", work / "lights", work / "fog_presets.json"
    shutil.copyfile(args.card, card)
    lights.mkdir()
    shutil.copyfile(ROOT / "pc/assets/environment/fog_presets.json", presets)
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "2",
           "ROOD_FOG_PRESETS": str(presets), "ROOD_BUTTON_PROMPTS": "playstation"}
    os.environ.pop("ROOD_SETTINGS", None)
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env,
                     socket_path=os.environ.get("ROOD_RPC") or f"/tmp/rood-tun-{os.getpid()}.sock")
    try:
        run(game, out, shots, presets)
    finally:
        game.close()
    text = pathlib.Path(game.log).read_text(errors="replace")
    bad = [line for line in text.splitlines() if "[crash]" in line or "validation:" in line
           or ("shader" in line and "error" in line.lower())]
    check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))
    shutil.rmtree(work, ignore_errors=True)
    if failures:
        print(f"FAIL: {len(failures)} checks failed")
        return 1
    print("PASS: tunables change what they should; defaults keep the picture")
    return 0


def run(game, out, shots, presets):
    def save(name, image):
        if shots:
            Image.fromarray(np.clip(image, 0, 255).astype(np.uint8)).save(shots / f"{name}.png")

    def setting(key):
        return game.call("settings", get=key).get("line")

    def apply(*lines):
        game.call("settings", apply=list(lines))

    # ---- settings keys: defaults, a change, back ----
    for key, default, other in KEYS + [("post_" + k, d, o) for k, d, o in POST_KEYS]:
        before = setting(key)
        apply(f"{key} {other}")
        changed = setting(key)
        apply(f"{key} {default}")
        after = setting(key)
        # post_* lines are written only when set: the default reads back as no line or the default
        default_ok = before in (f"{key} {default}", None) and after in (f"{key} {default}", None)
        check(default_ok and changed == f"{key} {other}",
              f"settings {key}: default {before!r}, set {changed!r}, back {after!r}")
    check(setting("button_prompts_color_playstation_circle") is None,
          "button prompt colours: no line at the default (written only when changed)")
    apply("button_prompts_color_playstation_circle #102030")
    check(setting("button_prompts_color_playstation_circle") == "button_prompts_color_playstation_circle #102030",
          "button_prompts_color_playstation_circle reads back when changed")
    apply("button_prompts_color_playstation_circle #f06070")

    game.pause()
    # ---- the weapon slots' and walls' values reach their modules ----
    apply("qol_weapon_slots_name_ticks 77", "qol_weapon_slots_hint 150 148")
    ws = game.call("weapon_slots")
    check(ws["name_ticks"] == 77 and ws["hint"] == [150, 148] and ws["labels"][0] == [242, 99, 237, 101],
          f"weapon_slots reports the settings (name_ticks {ws['name_ticks']}, hint {ws['hint']}, label 0 "
          f"{ws['labels'][0]})")
    apply("qol_weapon_slots_name_ticks 150", "qol_weapon_slots_hint 160 142")
    apply("fog_detail_wind 0.5", "fog_detail_rise 0.04", "fog_flow_cycle 3")
    e = game.call("environment")
    check(abs(e["detail_wind"] - 0.5) < 1e-6 and abs(e["detail_rise"] - 0.04) < 1e-6 and abs(e["flow_cycle"] - 3) < 1e-6,
          f"environment reports the fog's detail motion ({e['detail_wind']}, {e['detail_rise']}, {e['flow_cycle']})")
    game.call("environment", detail_wind=1.3, detail_rise=0.01, flow_cycle=2.5)
    check(setting("fog_detail_wind") == "fog_detail_wind 1.3" and setting("fog_flow_cycle") == "fog_flow_cycle 2.5",
          "RPC environment {detail_wind, detail_rise, flow_cycle} sets them back")

    # ---- the post fields in the picture ----
    p = game.state()["player"]
    game.call("lights", action="add", position=[p["x"], p["y"] - 90, p["z"]])
    game.call("graphics", aa="off")
    ui, solid, _ = masks(game, out)
    check(ui.sum() > 500 and solid.mean() > 0.3, f"masks: {ui.sum()} 2D/UI pixels, {solid.mean() * 100:.0f}% 3D")

    def picture(name, **values):
        game.call("post", layer="gameplay", clear="all", set=values, snap=True)
        image = redrawn(game, out / "p")
        save(name, image)
        return image

    base = picture("base")
    bright = float((luma(base)[solid] > 0.6 * 255).mean())
    soft = picture("shoulder", shoulder_start=0.5, shoulder_width=0.5)
    moved, ui_diff = float(np.abs(soft - base)[solid].mean()), int(np.abs(soft - base)[ui].max())
    check(moved > 0.05 and ui_diff == 0,
          f"shoulder_start 0.5 / width 0.5: the lit 3D changes ({moved:.3f}; {bright * 100:.1f}% above 0.6), "
          f"2D/UI max difference {ui_diff}")
    again = picture("base-again")
    check(int(np.abs(again - base).max()) == 0, "back at the defaults: the base picture, pixel for pixel")

    tilt = {"tilt_shift": True, "tilt_blur": 16, "tilt_width": 0.05, "tilt_centre": 0.5}
    narrow = picture("tilt", **tilt)
    wide = picture("tilt-feather", **tilt, tilt_feather=1.0)
    h = base.shape[0]
    band = np.zeros_like(solid)
    band[int(h * 0.58):int(h * 0.72)] = True  # just past the band: blurred, less so with a longer feather
    region = solid & band
    e_narrow, e_wide = edge_energy(narrow, region), edge_energy(wide, region)
    check(e_wide > e_narrow * 1.05, f"tilt_feather 1: more detail just past the band ({e_narrow:.1f} -> {e_wide:.1f})")

    sharp = picture("sharpen", sharpen=1)
    sharper = picture("sharpen-gain", sharpen=1, sharpen_gain=4)
    e1, e2 = edge_energy(sharp, solid), edge_energy(sharper, solid)
    check(e2 > e1 * 1.03, f"sharpen_gain 4: more edge energy than 1.5 ({e1:.1f} -> {e2:.1f})")

    hh, w = solid.shape
    corner = np.zeros_like(solid)
    corner[: hh // 5, : w // 5] = corner[: hh // 5, -w // 5:] = corner[-hh // 5:, : w // 5] = corner[-hh // 5:, -w // 5:] = True
    v1 = picture("vignette", vignette=0.6)
    v2 = picture("vignette-gain", vignette=0.6, vignette_gain=3)
    c1, c2 = float(luma(v1)[solid & corner].mean()), float(luma(v2)[solid & corner].mean())
    check(c2 < c1 - 0.5, f"vignette_gain 3: darker corners than 1.2 ({c1:.1f} -> {c2:.1f})")
    game.call("post", layer="gameplay", clear="all", snap=True)

    # ---- button prompt colours ----
    bp = game.call("button_prompts", button_prompts="playstation", style="color_outline", icon_color="circle")
    colour, generation = bp["icon_color"], bp["generation"]
    check(colour is not None and colour[0] > colour[1] + 60, f"the circle icon is the PlayStation red ({colour})")
    bp = game.call("button_prompts", colors={"playstation_circle": "#20e040"}, icon_color="circle")
    green = bp["icon_color"]
    check(green is not None and green[1] > green[0] + 60 and bp["generation"] != generation
          and bp["colors"]["playstation_circle"] == "#20e040",
          f"button_prompts_color_playstation_circle #20e040: the icon turns green ({green}), generation "
          f"{generation} -> {bp['generation']}")
    apply("button_prompts_color_playstation_circle #f06070")
    back = game.call("button_prompts", icon_color="circle")["icon_color"]
    check(back == colour, f"back at the default colour: the same icon colour ({back})")
    bp = game.call("button_prompts", dim_mix=0.3)
    check(abs(bp["dim_mix"] - 0.3) < 1e-6 and setting("button_prompts_dim_mix") == "button_prompts_dim_mix 0.3",
          f"button_prompts_dim_mix reads back ({bp['dim_mix']})")
    apply("button_prompts_dim_mix 0.62")
    check(setting("button_prompts_dim_mix") is None, "dim_mix at its default: no line")

    # ---- smart walls' timing ----
    walls(game, apply)

    # ---- fog presets: a volume follows an edit of the presets file, no RPC ----
    data = json.loads(presets.read_text())
    name = next(iter(data["presets"]))
    density = float(data["presets"][name]["density"])
    vol = {"id": "tune0000000001", "pos": [p["x"], p["y"] - 40, p["z"]], "preset": name, "edge": 0.33}
    e = game.call("environment", volumes=[vol])
    v = next(x for x in e["volumes"] if x["id"] == vol["id"])
    serial = e["presets_serial"]
    check(abs(v["density"] - density) < 1e-6, f"a volume with preset {name}: its density ({v['density']})")
    data["presets"][name]["density"] = round(density * 2.5, 5)
    presets.write_text(json.dumps(data, indent=2) + "\n")
    game.pause(False)
    deadline = time.time() + 10
    while time.time() < deadline:
        game.run(10)
        e = game.call("environment")
        if e["presets_serial"] != serial:
            break
    v = next((x for x in e["volumes"] if x["id"] == vol["id"]), None)
    check(e["presets_serial"] != serial and v is not None and abs(v["density"] - density * 2.5) < 1e-4
          and abs(v["edge"] - 0.33) < 1e-6,
          f"the presets file edited: reloaded without RPC (serial {serial} -> {e['presets_serial']}), the volume "
          f"follows (density {v and v['density']}), its own edge kept ({v and v['edge']})")
    game.call("environment", volumes=[{"id": vol["id"], "remove": True}])


def walls(game, apply):
    """Smart walls fade over gameplay_walls_fade_out / _fade_in game frames: while the camera turns (R1, then
    L1 back) walls come into the way and go; sampled every vsync, by default some are seen part-way faded,
    with both fades 1 frame (and gameplay_walls_clear 1) none is: only solid or gone."""
    game.pause(False)

    def shown():
        return [g["shown"] for g in game.call("room_groups").get("groups", []) if "shown" in g]

    def experiment(fast):
        apply(*(["gameplay_walls_fade_out 1", "gameplay_walls_fade_in 1", "gameplay_walls_clear 1"] if fast else
                ["gameplay_walls_fade_out 4", "gameplay_walls_fade_in 7", "gameplay_walls_clear 10"]))
        game.call("graphics", gameplay_walls="smart")
        game.run(30)
        values, changed = set(), 0
        for button in ("r1", "l1"):
            before = shown()
            game.call("input", buttons=[button], vsyncs=4)
            for _ in range(90):
                game.run(1)
                values.update(shown())
            changed += sum(1 for x, y in zip(before, shown()) if x != y)
        return sorted(v for v in values if 0 < v < 1), changed

    partial0, changed0 = experiment(False)
    partial1, changed1 = experiment(True)
    check(changed0 > 0 or partial0, f"smart walls: walls come into the way and go as the camera turns "
                                    f"({changed0} changed; {len(partial0)} part-way values seen)")
    check(partial0 and not partial1,
          f"gameplay_walls_fade_out / _fade_in 1: no wall seen part-way faded (by default {partial0[:6]}; "
          f"with 1: {partial1[:6]})")
    apply("gameplay_walls_fade_out 4", "gameplay_walls_fade_in 7", "gameplay_walls_clear 10")
    game.call("graphics", gameplay_walls="original")
    game.run(30)
    game.pause()


if __name__ == "__main__":
    sys.exit(main())
