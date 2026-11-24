#!/usr/bin/env python3
"""The model studio (pc/src/studio.cpp, studio.c; pc/docs/plans/2026-11-22-model-studio-and-paint-channels-design.md, P2):

    python3 pc/tests/studio_smoke.py [--card ...] [--out DIR]

One game from the save_room checkpoint (headless EGL under run.py, a renderer with ray tracing). The studio holds the game
with the photo mode's pause and draws every picture as one of its renders, so what is checked is the picture and the state:
  - the catalog: every character, every Bestiary enemy (named from the disc) and every weapon the data lists; the catalog
    file is what pc/tools/studio_scan.py writes;
  - the stage and its Look: in `flat` (ambient only) every backdrop pixel is exactly the stage's grey, away from the model;
    the grey changed through RPC is the new pixel value; in `neutral` (a key light and a sky) the backdrop is lit (a
    gradient, not the grey) and is a function of the Look alone (the same twice, brighter with a brighter grey);
  - summon an enemy, a character and a weapon in both looks: the model is on the stage, its pixels centred in the picture,
    its size a fair share of it; a bad summon is refused;
  - orbit changes the picture, zoom changes the model's size, F-framing returns it;
  - leaving restores the game: the state digest (heap, VRAM, overlays, vsync) is the one it was held at before the first
    summon and the one after the last, the picture is the one from before entering (the game's camera and HUD again; the ray
    tracer's noise aside), the photo mode holds nothing, settings.cfg and the environment are as they were;
  - classic frames identical: a second visit gives the same pictures as the first (the studio leaves nothing behind).
Pictures are written to the output folder (PNG). Deterministic: every wait is on a condition."""
import argparse
import json
import os
import pathlib
import shutil
import sys
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT, Deadline  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402
from photo_foundation_smoke import ppm  # noqa: E402

DIGEST = ("heap", "vram", "overlays", "vsync")
WINDOW = (960, 720)
# one of each: a Bestiary enemy with a weapon (Z009U01), a character (a woman of the Undercity), a dagger
ITEMS = (("enemy", 14), ("character", 3), ("weapon", 1))


def image(path):
    w, h, data = ppm(path)
    return np.frombuffer(data, np.uint8).reshape(h, w, 3)


def mean_difference(a, b):
    return float(np.abs(a.astype(int) - b.astype(int)).mean())


def skin_share(img, box):
    """The share of warm, skin-toned pixels among the model's pixels in the top eighth of its box (a face, not hair)."""
    x0, y0, x1, y1 = box
    head = img[y0:y0 + max((y1 - y0) // 8, 1), x0:x1 + 1].astype(int)
    r, g, b = head[..., 0], head[..., 1], head[..., 2]
    model = (np.abs(head - 95).max(axis=2) > 6) & (head.max(axis=2) > 6)
    skin = (r > g + 8) & (g > b) & (r > 90)
    return float((skin & model).sum() / max(model.sum(), 1))


def same_picture(a, b, mean=1.5, strong=0.005):
    """The game's own picture twice: the ray tracer's denoiser leaves noise, never more than `mean` on average and
    never a strong difference over more than `strong` of the picture."""
    d = np.abs(a.astype(int) - b.astype(int))
    return float(d.mean()) <= mean and float((d.max(axis=2) > 24).mean()) <= strong


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="logs and the pictures")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if os.environ.get("ROOD_RENDERER") == "null":
        print("SKIP: the studio's renders need a renderer")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "studio"
    out.mkdir(parents=True, exist_ok=True)
    with card_guard(out / "cards", source=args.card) as cards:
        return run(out, cards.card1)


def run(out, card):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    work = out / "work"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    settings = work / "settings.cfg"
    settings.write_text("aspect 4:3\nui_menus 0\nlight_animation 0\ndust 0\naa off\n")
    (work / "lights").mkdir()
    env = {"ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_SETTINGS": str(settings), "ROOD_GI": "1", "ROOD_SCALE": "2",
           "ROOD_LIGHTS": str(work / "lights"), "ROOD_STUDIO": str(work / "user" / "studio.json"),
           "ROOD_PAUSED_REDRAW_HZ": "60"}
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        def studio(**params):
            return g.call("studio", **params)

        def settled(what, timeout=40, presents=3):
            """Wait until the studio's picture is the one asked for: held, nothing rendering, the camera fitted."""
            deadline = Deadline(timeout)
            while True:
                s = studio()
                if s["entered"] and s["settled"] and s["framed"] and g.call("photo")["phase"] == "held":
                    g.wait_presents(presents)  # (the ray tracer's history needs its frames after a light moved)
                    return s
                if deadline.passed():
                    check(False, f"{what}: not settled ({s})")
                    raise SystemExit(1)
                time.sleep(0.02)

        def shot(name):
            path = out / f"{name}.ppm"
            path.unlink(missing_ok=True)
            g.call("present_dump", path=str(path))
            if not rood_rpc.wait_for_file(path, 30):
                raise rood_rpc.RpcError("no frame presented")
            img = image(path)
            Image.fromarray(img).save(out / f"{name}.png")
            path.unlink()
            return img

        def digest():
            h = g.call("snapshot", action="hash")
            return {k: h[k] for k in DIGEST}

        def model_box(img, grey, margin=6):
            """The box (x0, y0, x1, y1) of the pixels that are not the flat backdrop; None when there are none."""
            diff = np.abs(img.astype(int) - grey).max(axis=2) > margin
            ys, xs = np.nonzero(diff)
            if len(xs) < 50:
                return None
            return int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())

        effects_before = g.call("render_stats")["uv_effects"]
        settings_before = settings.read_text()
        env_before = g.call("environment")
        g.run(4)
        held_vsync = g.status()["vsync"]
        before_digest = digest()
        before_frame = shot("before")
        before_shot2 = shot("before_again")
        check(mean_difference(before_frame, before_shot2) == 0, "the game paused draws the same picture twice")

        # ---- the catalog ----
        import studio_scan
        listed = studio(action="list")
        kinds = {k: [i for i in listed["items"] if i["kind"] == k] for k in ("character", "enemy", "weapon")}
        scanned = studio_scan.scan(ROOT / "data")
        check(len(kinds["character"]) == len(scanned["characters"]) and len(kinds["enemy"]) == len(scanned["enemies"])
              and len(kinds["weapon"]) == len(scanned["weapons"]) and len(kinds["character"]) > 150
              and len(kinds["enemy"]) >= 70 and len(kinds["weapon"]) > 100,
              f"the catalog lists what the disc has ({len(kinds['character'])} characters, {len(kinds['enemy'])} enemies, "
              f"{len(kinds['weapon'])} weapons)")
        check(all(e["name"] and not e["name"].startswith("Enemy ") for e in kinds["enemy"]),
              "every enemy has its Bestiary name from the disc")
        zombie = studio(action="list", kind="enemy", filter="zombie")
        check(zombie["count"] >= 1, f"the list filters by name ({zombie['count']} zombies)")
        check(studio()["entered"] is False, "not in the studio before entering")

        # ---- enter: the empty stage ----
        studio(action="enter")
        s = settled("enter")
        check(s["look"] == "neutral" and s["item"] is None, f"entered, the neutral look, nothing summoned ({s['look']})")
        p = g.call("photo")
        check(p["owners"] == ["studio"] and p["phase"] == "held" and p["held_vsync"] == held_vsync,
              f"the photo mode holds the game for the studio at the vsync it was at ({p['held_vsync']} == {held_vsync})")
        check(digest() == before_digest, "held: the state digest unchanged")
        grey = int(studio(action="get", path="stage.grey")["value"])

        studio(action="look", name="flat")
        settled("flat")
        empty_flat = shot("empty_flat")
        exact = float((empty_flat == grey).all(axis=2).mean())
        check(exact == 1.0, f"flat look, empty stage: every backdrop pixel is the stage's grey {grey} ({exact * 100:.3f}%)")
        studio(action="look", set="stage.grey", value=grey + 40)
        settled("grey")
        brighter = shot("empty_flat_grey")
        check(float((brighter == grey + 40).all(axis=2).mean()) == 1.0,
              f"the grey changed through RPC is the backdrop's pixel value ({grey + 40})")
        studio(action="look", set="stage.grey", value=grey)
        settled("grey back")
        check(mean_difference(shot("empty_flat_again"), empty_flat) == 0, "the grey put back: the same picture")

        # the stage whole from every side (the old room's texture effects once wrote UV bytes into its vertex words: black facets)
        black_views = []
        for el in (12, 35):
            for yaw in ((0, 30, 330) if el == 12 else range(0, 360, 30)):
                studio(action="camera", yaw=yaw, elevation=el)
                settled("sweep")
                black_views.append((el, yaw, int((shot("sweep").max(axis=2) <= 6).sum())))
        check(all(n == 0 for _, _, n in black_views), f"the empty flat stage has no black pixel from {len(black_views)} views ({[v for v in black_views if v[2]]})")
        studio(action="camera", frame=True, yaw=20, elevation=12)
        settled("sweep back")
        studio(action="look", name="neutral")
        settled("neutral")
        empty_neutral = shot("empty_neutral")
        lit = float((empty_neutral == grey).all(axis=2).mean())
        check(lit < 0.05 and empty_neutral.std() > 4, f"neutral look: the backdrop is lit, a gradient ({lit * 100:.1f}% at the grey, "
              f"spread {empty_neutral.std():.1f})")
        studio(action="look", set="stage.grey", value=grey + 40)
        settled("neutral grey")
        neutral_bright = shot("empty_neutral_grey")
        check(neutral_bright.mean() > empty_neutral.mean() + 8, "a brighter grey, a brighter backdrop in the neutral look")
        studio(action="look", set="stage.grey", value=grey)
        settled("neutral grey back")
        check(mean_difference(shot("empty_neutral_again"), empty_neutral) == 0, "the Look decides the picture: the same twice")
        check(mean_difference(empty_neutral, empty_flat) > 10, "the two looks differ")

        # ---- summon ----
        refused = False
        try:
            studio(action="summon", kind="enemy", id=9999)
        except rood_rpc.RpcError:
            refused = True
        check(refused, "a model the catalog does not have is refused")
        pictures = {}
        for kind, ident in ITEMS:
            studio(action="summon", kind=kind, id=ident)
            for look in ("flat", "neutral"):
                studio(action="look", name=look)
                s = settled(f"{kind} {look}")
                img = shot(f"{kind}_{look}")
                pictures[(kind, look)] = img
                if look == "flat":
                    box = model_box(img, grey)
                    check(s["message"] == "" and s["slot"] >= 1 and box is not None and "bytes" in s["info"],
                          f"{kind} {ident} ({s['item']['name']}): summoned into slot {s['slot']} ({s['info']})")
                    if box:
                        cx, cy = (box[0] + box[2]) / 2 / WINDOW[0], (box[1] + box[3]) / 2 / WINDOW[1]
                        share = (box[3] - box[1]) / WINDOW[1]
                        check(abs(cx - 0.5) < 0.08 and abs(cy - 0.5) < 0.1,
                              f"{kind}: centred in the picture ({cx:.2f}, {cy:.2f})")
                        check(0.12 < share < 0.9, f"{kind}: a fair share of the picture's height ({share:.2f})")
                    away = np.ones(img.shape[:2], bool)
                    if box:
                        away[max(box[1] - 40, 0):box[3] + 40, max(box[0] - 40, 0):box[2] + 40] = False
                    share = float((img[away] == grey).all(axis=1).mean())
                    check(share == 1.0, f"{kind}: the flat backdrop away from the model is exactly the grey ({share * 100:.3f}%)")
                else:
                    check(mean_difference(img, pictures[(kind, "flat")]) > 8, f"{kind}: the neutral look is not the flat one")

        # ---- the front: camera.yaw 0 is the model's front, the default view is from it ----
        studio(action="summon", kind="character", id=3)
        studio(action="look", name="flat")
        settled("front")
        front_img = shot("front_default")
        fbox = model_box(front_img, grey)
        studio(action="camera", yaw=180)
        settled("back")
        back_img = shot("front_from_behind")
        bbox = model_box(back_img, grey)
        f_skin, b_skin = skin_share(front_img, fbox), skin_share(back_img, bbox)
        check(f_skin > b_skin + 0.15, f"the default view shows the character's face and chest, not her back (skin in the head: {f_skin:.2f} vs {b_skin:.2f} from behind)")
        studio(action="summon", kind="weapon", id=1)
        settled("weapon front")
        wbox = model_box(shot("weapon_default"), grey)
        check(wbox and (wbox[2] - wbox[0]) > 2.5 * (wbox[3] - wbox[1]),
              f"a weapon's default view is of the flat of its blade (box {wbox[2] - wbox[0]} x {wbox[3] - wbox[1]})")

        # ---- a solid backdrop: no stage, the model on one colour ----
        studio(action="summon", kind="character", id=3)
        studio(action="look", name="neutral")
        solid = {}
        for name, rgb in (("black", (0, 0, 0)), ("grey", (119, 119, 119)), ("white", (255, 255, 255)), ("custom", (40, 90, 200))):
            if name == "custom":
                studio(action="backdrop", mode="solid", color=list(rgb))
            else:
                studio(action="backdrop", mode="solid", preset=name)
            s = settled(f"backdrop {name}")
            img = shot(f"backdrop_{name}")
            solid[name] = img
            diff = (img.astype(int) != np.array(rgb)).any(axis=2)
            ys, xs = np.nonzero(diff)
            check(len(xs) > 500, f"backdrop {name}: the model is there ({len(xs)} pixels differ from the colour)")
            away = np.ones(diff.shape, bool)
            away[max(ys.min() - 30, 0):ys.max() + 30, max(xs.min() - 30, 0):xs.max() + 30] = False
            exact = float((~diff[away]).mean())
            check(exact == 1.0, f"backdrop {name} {rgb}: every pixel away from the model is exactly the colour, as drawn, no denoiser noise "
                                f"({exact * 100:.4f}% of {int(away.sum())})")
        grey_img = solid["grey"]
        check(float((grey_img == 119).all(axis=2).mean()) > 0.85 and mean_difference(solid["black"], solid["white"]) > 100,
              "the backdrops are different pictures of the same lit model")
        studio(action="look", name="flat")
        flat_solid = shot("backdrop_flat") if settled("backdrop flat") else None
        check(float((flat_solid == (40, 90, 200)).all(axis=2).mean()) > 0.85, "the flat look on the solid backdrop has the same solid background")
        studio(action="look", name="neutral")
        settled("neutral again")
        # lit by the key light: the model's brightest pixel is brighter than its darkest side
        lit = grey_img.astype(int)[(grey_img != 119).any(axis=2)].max(axis=1)
        check(lit.max() - np.percentile(lit, 5) > 60, "the model is still lit by the key light on the backdrop (a spread of light across it)")

        # ---- the turntable ----
        studio(action="turntable", model=False, light=False, paused=False, model_speed=40, light_speed=-60, fixed_dt=0.5, reset=True)
        settled("turntable idle")
        still_a = shot("turn_idle_a")
        still_b = shot("turn_idle_b")
        check(mean_difference(still_a, still_b) == 0 and studio()["turntable"]["model_angle"] == 0, "the turntable off: nothing turns")
        studio(action="turntable", model=True, model_angle=300)
        first = studio()
        deadline = Deadline(60)
        while studio()["scenes"] - first["scenes"] < 40 and not deadline.passed():
            time.sleep(0.02)
        later = studio()
        k = later["scenes"] - first["scenes"]
        moved = (later["turntable"]["model_angle"] - first["turntable"]["model_angle"]) % 360
        check(k >= 40 and abs(moved - (20.0 * k) % 360) < 0.01 and first["turntable"]["running"],
              f"{k} renders turned the model by {moved:.3f} degrees ({(20.0 * k) % 360:.3f} expected: 40 degrees a second x 0.5 s a render, "
              f"{20.0 * k:.0f} in all: through the wrap at 360 twice)")
        studio(action="turntable", paused=True)
        time.sleep(0.5)
        t = settled("turntable paused")
        t = studio()
        held = t["turntable"]["model_angle"]
        pic_a = shot("turn_paused_a")
        time.sleep(0.5)
        pic_b = shot("turn_paused_b")
        check(mean_difference(pic_a, pic_b) == 0 and studio()["turntable"]["model_angle"] == held and studio()["scenes"] == t["scenes"]
              and not t["turntable"]["running"], "paused: the angle and the picture hold, no render is asked for")
        # resumed from where it stood: no jump (the first step after is one step)
        studio(action="turntable", paused=False)
        r0 = studio()
        while studio()["scenes"] - r0["scenes"] < 1:
            time.sleep(0.01)
        r1 = studio()
        first_step = (r0["turntable"]["model_angle"] - held) % 360
        step = (r1["turntable"]["model_angle"] - r0["turntable"]["model_angle"]) % 360
        check(first_step in (0, 20) and abs(step - 20.0 * (r1["scenes"] - r0["scenes"])) < 0.01 and r1["scenes"] > r0["scenes"],
              f"resumed from the held angle: the first step {first_step:.0f} degrees, then {step:.0f} for {r1['scenes'] - r0['scenes']} render(s), no jump")
        studio(action="turntable", paused=True)
        time.sleep(0.3)
        # the pictures: the same at 0 and 360, a different one at 20; a turn is a turn of the model alone (the camera stays)
        studio(action="turntable", model_angle=0)
        settled("turn 0")
        p0 = shot("turn_000")
        studio(action="turntable", model_angle=359.999)
        settled("turn 360")
        p360 = shot("turn_360")
        studio(action="turntable", model_angle=20)
        settled("turn 20")
        p20 = shot("turn_020")
        check(same_picture(p0, p360, mean=0.2) and mean_difference(p0, p20) > 3,
              f"the model at 0 and at 360 degrees is one picture ({mean_difference(p0, p360):.3f}), at 20 it is another ({mean_difference(p0, p20):.2f})")
        # the strip: eight frames of the turntable on the grey backdrop
        studio(action="backdrop", mode="solid", preset="grey")
        for i in range(8):
            studio(action="turntable", model_angle=45 * i)
            settled("strip")
            shot(f"turn_strip_{i}")
        # the light: its yaw goes round, the model stays
        studio(action="turntable", model=False, light=True, paused=True, model_angle=0, light_angle=90)
        settled("light 90", presents=30)
        l90 = shot("light_090")
        studio(action="turntable", light_angle=270)
        settled("light 270", presents=30)
        l270 = shot("light_270")
        studio(action="turntable", light_angle=0)
        settled("light 0", presents=30)
        l0 = shot("light_000")
        studio(action="turntable", light_angle=360 - 0.001)
        settled("light 360", presents=30)
        l360 = shot("light_360")
        # (the model's silhouette is the same; its shading moves)
        sil90 = (l90 != 119).any(axis=2)
        sil270 = (l270 != 119).any(axis=2)
        check((sil90 ^ sil270).mean() < 0.002 and mean_difference(l90, l270) > 0.5 and same_picture(l0, l360, mean=0.3),
              f"the key light's turn changes the shading ({mean_difference(l90, l270):.2f} between 90 and 270), not the silhouette, "
              f"and 360 is 0 ({mean_difference(l0, l360):.3f})")
        studio(action="turntable", light=True, model=False, paused=False, light_speed=-60, light_angle=10)
        l_first = studio()
        while studio()["scenes"] - l_first["scenes"] < 6:
            time.sleep(0.02)
        l_later = studio()
        k = l_later["scenes"] - l_first["scenes"]
        moved = (l_later["turntable"]["light_angle"] - l_first["turntable"]["light_angle"]) % 360
        check(abs(moved - (-30.0 * k) % 360) < 0.01 and l_later["turntable"]["model_angle"] == l_first["turntable"]["model_angle"],
              f"the light turned the other way, -60 degrees a second ({k} renders: {moved:.2f} degrees, the model not at all)")
        studio(action="turntable", paused=True)
        time.sleep(0.3)
        # both at once, then off
        studio(action="turntable", model=True, light=True, paused=False)
        a1 = studio()["scenes"]
        while studio()["scenes"] - a1 < 4:
            time.sleep(0.02)
        studio(action="turntable", model=False, light=False, paused=False, reset=True)
        time.sleep(0.3)
        settled("turntable off")
        check(not studio()["turntable"]["running"] and studio()["turntable"]["model_angle"] == 0, "both toggles together, then off and reset")
        studio(action="backdrop", mode="stage")
        studio(action="look", name="flat")
        settled("stage again")

        # ---- orbit, zoom, frame ----
        studio(action="summon", kind="enemy", id=14)
        studio(action="look", name="flat")
        settled("orbit base")
        base = shot("orbit_base")
        box0 = model_box(base, grey)
        studio(action="camera", orbit_yaw=90)
        settled("orbit")
        turned = shot("orbit_90")
        check(mean_difference(base, turned) > 0.4 and model_box(turned, grey) is not None,
              f"orbit changes the picture (mean difference {mean_difference(base, turned):.2f})")
        studio(action="camera", orbit_elevation=35)
        settled("pitch")
        check(mean_difference(turned, shot("orbit_pitch")) > 0.4, "pitching the orbit changes it again")
        studio(action="camera", frame=True, yaw=20, elevation=12)
        settled("frame")
        again_frame = shot("frame_again")
        check(mean_difference(again_frame, base) < 4 and mean_difference(again_frame, turned) > 1.3 * mean_difference(again_frame, base),
              f"frame: the first view is back, not the turned one (differences {mean_difference(again_frame, base):.2f} and {mean_difference(again_frame, turned):.2f})")
        studio(action="camera", zoom_by=0.5)
        settled("zoom")
        box1 = model_box(shot("zoom_in"), grey)
        check(box0 and box1 and (box1[3] - box1[1]) > 1.4 * (box0[3] - box0[1]),
              f"zoom: the model's height grows ({box0[3] - box0[1]} -> {box1[3] - box1[1]} pixels)")
        check(digest() == before_digest, "held through summons and orbits: the state digest is the one before")

        # ---- leaving restores the game ----
        studio(action="leave")
        deadline = Deadline(30)
        while g.call("photo")["phase"] != "running" or g.call("photo")["owners"]:
            if deadline.passed():
                check(False, f"left: the photo mode still holds {g.call('photo')}")
                break
            time.sleep(0.02)
        g.wait_presents(4)
        after_digest = digest()
        check(after_digest == before_digest, f"left: the state digest is the game's from before ({after_digest})")
        check(g.status()["vsync"] == held_vsync, "left: the game is at the vsync it was held at")
        after_frame = shot("after")
        check(same_picture(after_frame, before_frame),
              f"left: the picture is the one from before ({mean_difference(after_frame, before_frame):.3f} mean difference)")
        check(studio()["entered"] is False and g.call("photo")["camera"] is None, "left: not entered, the game's own camera")
        g.run(30)
        check(effects_before > 0 and g.call("render_stats")["uv_effects"] == effects_before,
              f"left: the room's texture effects are back and running ({effects_before} animated materials before, {g.call('render_stats')['uv_effects']} after 30 vsyncs)")
        check(settings.read_text() == settings_before, "settings.cfg unchanged")
        env_after = g.call("environment")
        check({k: env_after[k] for k in ("sun", "sky", "global")} == {k: env_before[k] for k in ("sun", "sky", "global")},
              "the room's sun and sky as they were")
        check(not (work / "user" / "studio.json").exists(), "nothing of the user's written")

        # ---- a second visit: the same pictures (nothing is left behind) ----
        g.run(2)
        second_before, second_before_frame = digest(), shot("before_second")
        studio(action="enter")
        studio(action="look", name="flat")
        studio(action="summon", kind="enemy", id=14)
        settled("second visit")
        again = shot("second_visit")
        check(mean_difference(again, base) < 0.3, f"a second visit draws the same picture ({mean_difference(again, base):.3f})")
        studio(action="leave")
        deadline = Deadline(30)
        while g.call("photo")["phase"] != "running":
            if deadline.passed():
                break
            time.sleep(0.02)
        g.wait_presents(4)
        second_digest, second_frame = digest(), shot("after_second")
        check(second_digest == second_before and same_picture(second_frame, second_before_frame),
              f"left again: the state and the picture are the game's from before ({second_digest == second_before}, {mean_difference(second_frame, second_before_frame):.3f})")
    finally:
        g.close()
    print("studio_smoke:", "FAILED" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
