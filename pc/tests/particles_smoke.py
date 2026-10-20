#!/usr/bin/env python3
"""The port's particle system and its first use, dust motes (pc/src/particles.cpp,
particle_math.h; Vulkan vk_particles.inc, OpenGL gl_present.c; pc/docs/Particles.md).

    python3 pc/tests/particles_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

From a copy of the pinned card into its save room, with a scratch lights
directory (ROOD_LIGHTS) and settings file (ROOD_SETTINGS); the room's packs
(ROOD_LIGHTING) as in the game. Sections:

- items: an emitter added over RPC with a few fields saves as one
  `emitter <id> asset=dust_motes ...` line (only what differs from its
  asset), comes back from the file with the same fields, and goes; an RPC
  call with a bad entry changes nothing;
- assets: an emitter of an asset that is not there keeps what it writes,
  and follows the asset once it is reloaded; an asset edited and reloaded
  changes its emitters (nothing pinned, nothing unsaved); edited on disk
  without any RPC, it is read again live (polled about twice a second);
- motes: with the time frozen the motes are the same from frame to frame,
  inside their box; Dust Off draws none, Low its share, High all; time moves
  them by their drift;
- picture: Dust Off is the picture without emitters, pixel for pixel; a
  wall between a light and the motes leaves them dark (none shows), with
  the path tracer and without it (GI off: the motes' own scene); without
  any tracer a source that casts shadows gives them no light; a low sun
  behind the wall leaves them dark (and lights them without its shadows);
  the motes a light in the room reaches show as bright game pixels, each a
  uniform square of 1 or 2 game pixels with nothing around it;
- editor: an emitter is an object of the editor (icon, click, Outliner),
  Details edits it (one undo step, reset to its asset's value), R sizes a
  box or a disc's radius, Add emitter (here) places one on the surface under
  the view's centre, Show > Emitters off hides its icon, the W gizmo moves
  it with the mouse, Delete and undo, duplicate;
- cost: the motes' GPU time (Vulkan, RPC perf section "particles") or CPU
  time (OpenGL, RPC particles cpu_ms and perf cpu_sections) for 4096 motes;
- example: the town house (zone 34 room 0) has its authored dust, subtle;
  its motes glint in the windows' light, and those the windows' light
  would reach but something blocks take no pixel.
"""
import argparse
import math
import os
import pathlib
import shutil
import sys
import tempfile

import json

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_selection_smoke import pinned_card  # noqa: E402

EMITTER = "dusttest000001"  # the test's emitters' ids (items emitter:<id>)
FAR = "dusttest000002"


class Session:
    def __init__(self, game, out, user, vulkan):
        self.game, self.out, self.user, self.vulkan = game, out, user, vulkan
        self.failures = []
        self.assets = out / "emitters"
        self.player = self.zone = self.room = None

    def check(self, ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            self.failures.append(message)
        return ok

    def particles(self, **params):
        return self.game.call("particles", **params)

    def emitter(self, ident=EMITTER):
        return next((e for e in self.particles()["emitters"] if e["id"] == ident), None)

    def room_file(self):
        return self.user / f"zone{self.zone:02d}_room{self.room:02d}.txt"

    def shot(self, name, frames=8):
        if frames:
            self.game.run(frames)
        path = self.game.present_screenshot(self.out / f"{name}.png")
        return np.asarray(Image.open(path).convert("RGB"), np.float32) / 255.0


def start(args):
    card = pathlib.Path(args.card)
    if not card.exists():
        print("SKIP: no memory card", card)
        return None
    out = pathlib.Path(args.out) if args.out else OUT / "particles_smoke"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    shutil.copyfile(card, out / "card1.mcd")
    user = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    assets = out / "emitters"  # a copy: the assets section edits it
    shutil.copytree(ROOT / "pc/assets/emitters", assets)
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    # Lights hold still, no anti-aliasing filter (motes are exact game pixels), Dust Off as by default.
    (out / "settings.cfg").write_text("light_animation 0\naa off\n")
    env = {"ROOD_LIGHTS": str(user), "ROOD_LIGHTING": str(ROOT / "lighting"), "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_GI": "1", "ROOD_SCALE": "2", "ROOD_WINDOW": "960x720", "ROOD_LIGHTING_PRESET": "ultra",
           "ROOD_PERF": "1", "ROOD_EMITTERS": str(assets)}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-dust-{os.getpid()}.sock"
    game = launch_at("save_room", card=out / "card1.mcd", fast=True, log=out / "game.log", extra_env=env,
                     socket_path=socket)
    s = Session(game, out, user, vulkan)
    st = game.state()
    s.player, s.zone, s.room = st.get("player"), st.get("zone"), st.get("room")
    if not s.player:
        s.check(False, f"no room with Ashley after Continue: {st}")
    return s


def finish(s):
    s.game.close()
    log = pathlib.Path(s.game.log).read_text(errors="replace")
    bad = [line for line in log.splitlines()
           if "[crash]" in line or "[gl] shader" in line or "validation:" in line or "[particles] the" in line]
    s.check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))
    shutil.rmtree(s.user, ignore_errors=True)
    print("PASS: particles" if not s.failures else f"FAIL: {len(s.failures)} check(s)")
    return 1 if s.failures else 0


def check_items(s):
    """An emitter round-trips through the lighting file: only what differs from its asset is written."""
    p = s.player
    pos = [round(p["x"]), round(p["y"] - 120), round(p["z"])]
    r = s.particles(emitters=[{"id": EMITTER, "asset": "dust_motes", "pos": pos, "count": 300, "drift": [0, -2, 0],
                               "seed": 11}])
    e = s.emitter()
    s.check(e is not None and e["asset"] == "dust_motes" and e["count"] == 300 and e["fields"]["curl"] == "9"
            and e["fields"]["drift"] == "0,-2,0",
            f"added over RPC: its fields, the rest from its asset ({e and e['fields']})")
    s.check(any(a["id"] == "dust_motes" for a in r["assets"]), "the assets are listed (the library's source)")
    s.game.call("lighting", action="save")
    text = s.room_file().read_text() if s.room_file().exists() else ""
    line = next((l for l in text.splitlines() if l.startswith(f"emitter {EMITTER} ")), "")
    s.check(line == f"emitter {EMITTER} asset=dust_motes pos={pos[0]},{pos[1]},{pos[2]} seed=11 count=300 drift=0,-2,0",
            f"saved as one line, only what differs from its asset ({line})")
    s.particles(emitters=[{"id": EMITTER, "remove": True}])
    s.check(s.emitter() is None, "removed over RPC")
    s.game.call("lighting", action="reload")
    s.game.run(4)
    e = s.emitter()
    s.check(e is not None and e["pos"] == pos and e["count"] == 300 and e["fields"]["drift"] == "0,-2,0"
            and e["fields"]["curl"] == "9", f"read back from the file the same ({e and e['fields']})")
    s.particles(emitters=[{"id": EMITTER, "remove": True}])
    s.game.call("lighting", action="save")
    text = s.room_file().read_text() if s.room_file().exists() else ""
    s.check(f"emitter {EMITTER}" not in text, "removed and saved: the line goes")
    # A call with a bad entry (an unknown field after a good one; add without pos) changes nothing.
    for bad in ({"emitters": [{"id": EMITTER, "asset": "dust_motes", "pos": pos}, {"id": FAR, "wobble": 1}]},
                {"emitters": [{"id": EMITTER, "asset": "dust_motes", "pos": pos}], "add": {"asset": "dust_motes"}}):
        try:
            s.particles(**bad)
            refused = False
        except RpcError:
            refused = True
        s.check(refused and s.emitter() is None and s.emitter(FAR) is None,
                f"a bad call is refused and changes nothing ({list(bad)})")


def check_assets(s):
    """Reloading the assets resolves the emitters again: an asset that was missing is followed once it is
    there (what the item writes stays), an edited asset changes its emitters; neither pins old values."""
    p = s.player
    pos = [round(p["x"]), round(p["y"] - 120), round(p["z"])]
    later = s.assets / "dust_later.json"
    later.unlink(missing_ok=True)
    s.particles(emitters=[{"id": EMITTER, "asset": "dust_later", "pos": pos, "count": 300, "seed": 7}], reload_assets=True)
    e = s.emitter()
    s.check(e is not None and e["count"] == 300 and e["fields"]["curl"] == "0",
            f"an asset that is not there: its own values, the rest the defaults ({e and e['fields']['curl']})")

    def line():
        s.game.call("lighting", action="save")
        text = s.room_file().read_text() if s.room_file().exists() else ""
        return next((l for l in text.splitlines() if l.startswith(f"emitter {EMITTER} ")), "")
    expect = f"emitter {EMITTER} asset=dust_later pos={pos[0]},{pos[1]},{pos[2]} seed=7 count=300"
    s.check(line() == expect, f"saved: only what it writes ({line()})")
    data = json.loads((s.assets / "dust_motes.json").read_text())
    later.write_text(json.dumps(data))
    s.particles(reload_assets=True)
    e = s.emitter()
    s.check(e["count"] == 300 and e["fields"]["curl"] == "9" and e["fields"]["fade_in"] == "2.5",
            f"the asset there and reloaded: its values, the emitter's own count kept ({e['fields']['curl']})")
    s.check(line() == expect, f"and nothing more is written ({line()})")
    data["modules"]["curl"]["amplitude"] = 20
    later.write_text(json.dumps(data))
    s.particles(reload_assets=True)
    e = s.emitter()
    s.check(e["fields"]["curl"] == "20" and line() == expect,
            f"the asset edited and reloaded: the emitter follows it, nothing pinned ({e['fields']['curl']}; {line()})")
    # Edited on disk alone (no reload_assets): the assets are polled about twice a second (the next
    # look waited for: RPC status polls.files).
    data["modules"]["curl"]["amplitude"] = 33
    later.write_text(json.dumps(data))
    s.game.wait_polled("files")
    e = s.emitter()
    s.check(e["fields"]["curl"] == "33" and e["count"] == 300 and line() == expect,
            f"the asset edited on disk, no RPC: the emitter follows it live ({e['fields']['curl']}), its own count kept")
    s.particles(emitters=[{"id": EMITTER, "remove": True}])
    s.game.call("lighting", action="save")
    later.unlink()
    s.particles(reload_assets=True)


def motes(s, ident=EMITTER, limit=4096):
    """The last frame's motes of an emitter, in pages (the RPC's reply holds 256 KB)."""
    out = []
    while len(out) < limit:
        page = s.particles(motes={"emitter": ident, "offset": len(out), "max": min(512, limit - len(out))})["mote_list"]
        out += page
        if len(page) < 512:
            break
    return out


def check_motes(s):
    """Frozen time: the same motes each frame, inside their box; Dust sets how many; time moves them by their drift."""
    p = s.player
    centre = [round(p["x"]), round(p["y"] - 140), round(p["z"])]
    s.particles(emitters=[{"id": EMITTER, "asset": "dust_motes", "pos": centre, "size": [300, 200, 300], "count": 500,
                           "seed": 5, "lifetime": 30, "drift": [0, 0, 0], "curl": 0}], level=2, time=4.0)
    s.game.run(6)
    a = motes(s)
    s.game.run(6)
    b = motes(s)
    s.check(len(a) == 500 and [m["pos"] for m in a] == [m["pos"] for m in b],
            f"time frozen: the same {len(a)} motes frame after frame")
    inside = all(abs(m["pos"][k] - centre[k]) <= half + 0.05 for m in a for k, half in enumerate((150, 100, 150)))
    s.check(inside, "without motion every mote is in its box")
    s.check(all(m["size"] in (1, 2) for m in a) and {m["size"] for m in a} == {1, 2}, "sizes: 1 and 2 game pixels")
    for level, expect in ((0, 0), (1, 200), (2, 500)):
        s.particles(level=level)
        s.game.run(3)
        r = s.particles()
        drawn = next(e["drawn"] for e in r["emitters"] if e["id"] == EMITTER)
        s.check(drawn == expect and r["motes"] == expect, f"Dust {level}: {drawn} motes drawn ({expect})")
    s.particles(emitters=[{"id": EMITTER, "drift": [5, 0, 0]}], time=4.0)
    s.game.run(3)
    a = motes(s)
    s.particles(time=5.0)
    s.game.run(3)
    b = motes(s)
    moved = [bb["pos"][0] - aa["pos"][0] for aa, bb in zip(a, b) if aa["cycle"] == bb["cycle"]]
    s.check(len(moved) > 400 and all(abs(d - 5) < 0.02 for d in moved),
            f"a second later every mote that did not respawn drifted 5 units ({len(moved)} of {len(a)})")
    s.particles(emitters=[{"id": EMITTER, "remove": True}], level=0)


def lum(image):
    return image @ np.array([0.2126, 0.7152, 0.0722], np.float32)


def view_cells(r):
    """The view's window rectangle and, per window column and row, its game pixel (the composite's
    floor(uv x display))."""
    vx, vy, vw, vh = r["view"]
    dw, dh = r["display"]
    xs = np.floor((np.arange(vw) + 0.5) / vw * dw).astype(int)
    ys = np.floor((np.arange(vh) + 0.5) / vh * dh).astype(int)
    return (vx, vy, vw, vh), xs, ys


def ties(r):
    """The view's window pixels whose centre lies on a game pixel's edge (the window is not a whole multiple
    of the display: 720 / 224 puts every 14th row's edge on a pixel centre), which a renderer's float maths
    may give to either game pixel: compared nowhere."""
    vx, vy, vw, vh = r["view"]
    dw, dh = r["display"]
    fx = (np.arange(vw) + 0.5) / vw * dw
    fy = (np.arange(vh) + 0.5) / vh * dh
    tx, ty = np.abs(fx - np.round(fx)) < 1e-3, np.abs(fy - np.round(fy)) < 1e-3
    return ty[:, None] | tx[None, :]


def walls(s):
    """Wall points in the game's view (editor_select pick): world point and the normal toward Ashley."""
    g, p = s.game, s.player
    feet = np.array([p["x"], p["y"], p["z"]], float)
    g.call("editor_ui", visible=True)
    g.run(6)
    found = []
    for v in (0.4, 0.3, 0.5, 0.2):
        for u in (0.5, 0.35, 0.65, 0.2, 0.8):
            hit = g.call("editor_select", action="pick", u=u, v=v)
            if not hit.get("world"):
                continue
            w, n = np.array(hit["world"], float), np.array(hit["normal"], float)
            if abs(n[1]) > 0.25 or not 150 < np.linalg.norm((w - feet)[[0, 2]]) < 900:
                continue
            n[1] = 0
            n /= max(np.linalg.norm(n), 1e-6)
            found.append((w, n if np.dot(feet - w, n) > 0 else -n))
    g.call("editor_ui", visible=False)
    g.run(6)
    return found


def add_light(s, pos, radius, enabled=True):
    g = s.game
    ident = g.call("lights", action="add", position=[float(x) for x in pos])["lights"][-1]["id"]
    g.call("editor_schema", action="set", id=ident, values={"type": "point"})
    g.call("editor_schema", action="set", id=ident, values={"radius": radius, "intensity": 4, "color": [1, 1, 1],
                                                            "shadows": True, "enabled": enabled})
    return ident


def split(room, far):
    """The wall's test: the motes in the room the light behind it would reach, those of them dark; the motes
    behind it the light reaches, those of them lit."""
    shadowed = [m for m in room if m["open"] > 0.02]
    dark = [m for m in shadowed if m["light"] < 0.05 * m["open"]]
    reached = [m for m in far if m["open"] > 0.02]
    bright = [m for m in reached if m["light"] > 0.8 * m["open"]]
    return shadowed, dark, reached, bright


def closed(shadowed, dark, reached, bright):
    return len(shadowed) > 100 and len(dark) >= 0.9 * len(shadowed) and len(bright) >= 0.9 * max(len(reached), 1)


def check_picture(s):
    """A wall leaves motes dark (none shows) and the motes a room's light reaches glint (the room's lighting,
    traced); then on exact pictures (the Original preset) Dust Off is the picture without motes, pixel for
    pixel, and lit motes are uniform squares of 1 or 2 game pixels with nothing around them (nearest)."""
    g = s.game
    preset = g.call("graphics").get("lighting_preset")  # before any key makes it Custom: put back at the end
    g.call("post", layer="room", clear="all", snap=True)
    g.call("post", layer="gameplay", clear="all", snap=True)
    g.call("graphics", haze="off")
    g.call("pause", paused=True)
    s.particles(level=0, time=10.0)

    def frame(name):
        return s.shot(name, 40)  # 40 presents: OpenGL's ray budget relights every mote several times

    def animated(pics, threshold=0.0):
        """What changes by itself between pictures of a still scene by more than `threshold` (the HUD's
        blinking digits and cursor, the save point's scrolling column, which pausing does not stop),
        grown by 3 pixels: compared nowhere."""
        m = np.zeros(pics[0].shape[:2], bool)
        for a in pics[1:]:
            m |= np.abs(a - pics[0]).max(axis=2) > threshold
        for _ in range(3):
            m[1:, :] |= m[:-1, :].copy()
            m[:-1, :] |= m[1:, :].copy()
            m[:, 1:] |= m[:, :-1].copy()
            m[:, :-1] |= m[:, 1:].copy()
        return m

    def change(a, b, mask):
        d = np.abs(a - b).max(axis=2)[~mask]
        return float(d.max()) if d.size else 0.0

    def noise_of(a, b, mask):
        """Redraw noise outside `mask` (the lit room: its lights' and glows' own flicker, traced light on
        Vulkan): twice the largest difference between two redraws, and a margin; 0 when they are equal."""
        d = change(a, b, mask)
        return 0.0 if d == 0 else d * 2 + 2 / 255

    def delete(*idents):
        for ident in idents:
            g.call("editor_select", action="set", ids=[ident])
            g.call("editor_select", action="delete")

    # 1. The room's lighting (traced): a wall between a light and motes.
    candidates = walls(s)
    # A closed wall: in the room every mote it shadows is dark, behind it every one lit. A broken
    # shadow ray fails one side or the other, so trying up to four walls cannot pass a wrong renderer.
    if not s.check(bool(candidates), f"a wall in the game's view ({len(candidates)} found)"):
        return
    for wall, n in candidates[:4]:
        yaw = math.degrees(math.atan2(n[0], n[2]))
        # 140 wide: a 240-wide box reached past the save room's wall corners, where a side wall really
        # shadows part of the motes behind it (measured: every dark one 75-117 units to one side).
        common = {"asset": "dust_motes", "size": [140, 140, 100], "yaw": round(yaw, 2), "count": 1200, "lifetime": 1000,
                  "lifetime_jitter": 0, "drift": [0, 0, 0], "curl": 0, "fade_in": 0, "fade_out": 0, "variation": 0,
                  "brightness": 1.5, "pixel_min": 1, "pixel_max": 2}
        s.particles(emitters=[dict(common, id=EMITTER, pos=[round(float(x), 1) for x in wall + n * 70], seed=3),
                              dict(common, id=FAR, pos=[round(float(x), 1) for x in wall - n * 70], seed=4)])
        behind = add_light(s, wall - n * 120, 900)
        inside = add_light(s, wall + n * 260, 230, enabled=False)
        g.run(120)  # the new light's indirect light settles (path tracing accumulates)
        off = frame("picture-off")
        off2 = frame("picture-off2")
        off3 = frame("picture-off3")
        mask = animated([off, off2, off3], 8 / 255)  # beyond the lit room's own small flicker
        noise = noise_of(off, off2, mask)
        s.particles(level=2)
        on = frame("picture-wall")
        path = s.particles()["path"]
        if not s.check(path == "traced", f"the motes' light is traced ({path})"):
            return
        room, far = motes(s, EMITTER), motes(s, FAR)
        shadowed, dark, reached, bright = split(room, far)
        ok_geometry = closed(shadowed, dark, reached, bright)
        if not ok_geometry:  # not a closed wall here (a doorway, a thin edge): the next
            print(f"info: wall at {wall.round()} leaves {len(dark)}/{len(shadowed)} dark, {len(bright)}/{len(reached)} "
                  "lit behind it; trying the next")
            s.particles(level=0, emitters=[{"id": EMITTER, "remove": True}, {"id": FAR, "remove": True}])
            delete(behind, inside)
            continue
        s.check(True, f"the wall shadows the motes in the room ({len(dark)} of {len(shadowed)} the light behind it "
                      f"reaches stay dark); behind it they are lit ({len(bright)} of {len(reached)})")
        break
    else:
        s.check(False, "a closed wall to test shadowed motes")
        return
    print(f"info: {int(mask.sum())} pixels change by themselves (the HUD, the save point's column): not compared; "
          f"redraw noise elsewhere {noise * 255:.1f}/255")
    # The lit room flickers a little and its traced light has fireflies: compared where the motes would
    # show (every game pixel one covers, in front of the wall or behind it), a shown mote adds over 40/255
    # there; the share of those pixels that change so much is the redraw's own.
    r = s.particles()
    (vx, vy, vw, vh), xs, ys = view_cells(r)
    where = np.zeros((r["display"][1], r["display"][0]), bool)
    for m in room + far:
        cx, cy, k = m["cell"][0], m["cell"][1], m["size"]
        where[max(cy, 0):max(cy + k, 0), max(cx, 0):max(cx + k, 0)] = True
    region = np.zeros(mask.shape, bool)
    region[vy:vy + vh, vx:vx + vw] = where[np.ix_(ys, xs)]
    region &= ~mask

    def share(a, b):
        return float((np.abs(a - b).max(axis=2)[region] > 40 / 255).mean()) if region.any() else 1.0
    s.check(region.sum() > 200 and share(on, off) <= share(off2, off) + 0.005,
            f"no mote shows: those in the wall's shadow are dark, those behind it are hidden by it ("
            f"{share(on, off) * 100:.2f} % of their {int(region.sum())} pixels change by over 40/255, a redraw "
            f"{share(off2, off) * 100:.2f} %)")
    lit_ids = {m["g"] for m in bright}
    # Without the path tracer (GI, the traced shadows and AO off): the motes trace against a scene of their own.
    g.call("graphics", gi_method="off", shadows="maps", ao=False, characters_shadow_baked=False)
    frame("picture-wall-nogi")
    r = s.particles()
    sd, dk, rc, br = split(motes(s, EMITTER), motes(s, FAR))
    scene = "own" if s.vulkan else "cpu"
    s.check(r["path"] == "traced" and r["scene"] == scene and closed(sd, dk, rc, br),
            f"GI off: the motes trace their own scene ({r['path']}, {r['scene']}): {len(dk)} of {len(sd)} in the "
            f"wall's shadow dark, {len(br)} of {len(rc)} behind it lit")
    # No tracer at all (as on a device without one): a source that casts shadows lights no mote.
    s.particles(untraced=True)
    frame("picture-wall-untraced")
    r = s.particles()
    far_now = {m["g"]: m for m in motes(s, FAR)}
    gone = [g_ for g_ in lit_ids if g_ in far_now and far_now[g_]["light"] < 0.05 * far_now[g_]["open"]
            and not far_now[g_]["won"]]
    s.check(r["path"] == "untraced" and len(gone) >= 0.9 * max(len(lit_ids), 1),
            f"no tracer: the motes the light behind the wall lit are dark, no pixel ({len(gone)} of {len(lit_ids)}; "
            f"{r['path']})")
    s.particles(untraced=False)
    if preset:
        g.call("graphics", lighting_preset=preset)
    g.run(60)
    # A low sun behind the wall (shadows on): the motes in the room stay dark; without its shadows it lights them.
    if g.call("environment")["sun"]["defined"]:
        print("info: the room has a sun of its own: the sun case is skipped")
    else:
        before = motes(s, EMITTER)
        az = math.degrees(math.atan2(-n[0], -n[2])) % 360
        g.call("environment", sun={"enabled": True, "azimuth": round(az, 2), "elevation": 3, "color": [1, 1, 1],
                                   "intensity": 2, "angle": 0.5, "shadows": True})
        frame("picture-sun")
        shaded = motes(s, EMITTER)
        g.call("environment", sun={"shadows": False})
        frame("picture-sun-open")
        open_ = motes(s, EMITTER)
        g.call("environment", remove="sun")
        g.run(10)
        sun_open = [b["open"] - a["open"] for a, b in zip(before, shaded)]
        reached_ = [k for k, o in enumerate(sun_open) if o > 0.02]
        dark_ = [k for k in reached_ if shaded[k]["light"] - before[k]["light"] < 0.05 * sun_open[k]]
        lit_ = [k for k in reached_ if open_[k]["light"] - before[k]["light"] > 0.8 * sun_open[k]]
        s.check(len(reached_) > 100 and len(dark_) >= 0.9 * len(reached_),
                f"a low sun behind the wall: {len(dark_)} of {len(reached_)} motes in the room get none of it")
        s.check(len(lit_) >= 0.9 * max(len(reached_), 1),
                f"the same sun without its shadows lights them ({len(lit_)} of {len(reached_)}): the wall darkens them")
    # A light in the room: the motes it reaches glint.
    s.particles(emitters=[{"id": EMITTER, "count": 400}], level=0)
    g.call("editor_schema", action="set", id=inside, values={"enabled": True})
    g.run(120)
    lit_off = frame("picture-lit-off")
    s.particles(level=2)
    lit_on = frame("picture-lit")
    r = s.particles()
    (vx, vy, vw, vh), xs, ys = view_cells(r)
    diff = lum(np.abs(lit_on - lit_off))[vy:vy + vh, vx:vx + vw]
    room = motes(s, EMITTER)
    # (Nothing changing outside the motes' pixels is checked exactly below, on the Original preset.)
    # Lit: the room's light reaches it (the light behind the wall stays shadowed).
    glints = [m for m in room if m["won"] and m["light"] > 0.1]
    shown = 0
    for m in glints:
        cx, cy, k = m["cell"][0], m["cell"][1], m["size"]
        block = diff[np.ix_((ys >= cy) & (ys < cy + k), (xs >= cx) & (xs < cx + k))]
        shown += bool(block.size) and block.mean() > 10 / 255
    s.check(len(glints) >= 20 and shown >= 0.8 * len(glints),
            f"the motes the room's light reaches glint ({shown} of {len(glints)} lit motes raise their game pixels "
            "by over 10/255)")
    delete(behind, inside)
    s.particles(level=0, emitters=[{"id": FAR, "remove": True}])

    # 2. Exact pictures (the Original preset: nothing traced, no dynamic light): Dust Off, and the squares.
    g.call("graphics", lighting_preset="original")
    g.run(20)
    # The game runs between pictures: what changes by itself (the HUD's numbers count up in the save
    # point's circle, its column scrolls) is found over every Dust Off picture, first to last.
    pre = frame("exact-pre")
    # No emitter at all: the picture Dust Off must equal, emitter or not.
    s.particles(emitters=[{"id": EMITTER, "remove": True}])
    g.run(20)
    none = frame("exact-none")
    # Lit (its ambient), uniform (no variation): every mote a known flat colour. 150 of them, so most stand alone.
    s.particles(emitters=[dict(common, id=EMITTER, pos=[round(float(x), 1) for x in wall + n * 70], seed=3, count=150,
                               lit=1, ambient=0.15, brightness=1, color=[1, 1, 1])])
    g.run(20)
    base = frame("exact-off")
    base2 = frame("exact-off2")
    stills = [pre, base, base2, frame("exact-off3"), frame("exact-off4"), frame("exact-off5")]
    s.particles(level=2, emitters=[{"id": EMITTER, "enabled": False}])
    disabled = frame("exact-disabled")
    s.particles(level=0, emitters=[{"id": EMITTER, "enabled": True}])
    again = frame("exact-off-again")
    s.particles(level=2)
    exact = frame("exact-on")
    r = s.particles()
    room = motes(s, EMITTER)
    s.particles(level=0)
    stills.append(frame("exact-off-end"))
    emask = animated(stills)
    tol = 0.0 if not s.vulkan else noise_of(base, base2, emask)
    s.check(change(base, base2, emask) == 0 or s.vulkan,
            f"Original: a still picture redraws exactly ({int(emask.sum())} pixels change by themselves: the HUD, "
            f"the save point's column)")
    d = change(disabled, base, emask)
    s.check(d <= tol, f"Dust High with the emitter disabled: the picture as without it ({d * 255:.1f}/255)")
    d = change(again, base, emask)
    s.check(d <= tol, f"Dust Off with the emitter: pixel-identical to the picture without it ({d * 255:.1f}/255)")
    d = change(none, base, emask)
    s.check(d <= tol, f"Dust Off: the room with an emitter is the room without one ({d * 255:.1f}/255)")
    (vx, vy, vw, vh), xs, ys = view_cells(r)
    diff = lum(np.abs(exact - base))[vy:vy + vh, vx:vx + vw]
    moving = emask[vy:vy + vh, vx:vx + vw]
    tie = ties(r)
    still = ~moving & ~tie
    covered = np.zeros((r["display"][1], r["display"][0]), bool)
    for m in room:
        if sum(m["colour"]) > 1e-4 and m["cell"][0] > -m["size"]:
            cx, cy, k = m["cell"][0], m["cell"][1], m["size"]
            covered[max(cy, 0):max(cy + k, 0), max(cx, 0):max(cx + k, 0)] = True
    margin = 0
    grown = covered.copy()
    for _ in range(margin):
        grown[1:, :] |= grown[:-1, :].copy()
        grown[:-1, :] |= grown[1:, :].copy()
        grown[:, 1:] |= grown[:, :-1].copy()
        grown[:, :-1] |= grown[:, 1:].copy()
    cells = grown[np.ix_(ys, xs)]
    outside = diff[~cells & still]
    s.check(outside.max() <= tol + 1e-6, f"Dust High: nothing changes outside the motes' game pixels "
                                         f"({outside.max() * 255:.1f}/255)")
    # Each isolated mote: the game pixels around it that change form exactly its k x k square, each changed
    # in all its window pixels, the others not at all (whole game pixels, nearest: no soft edge). OpenGL
    # adds its colour exactly: the square is uniform too (Vulkan encodes the sum: it varies with the
    # texture under it).
    reach = 1 + margin
    shown = alone = square = edges = 0
    sizes = set()
    for m in room:
        if not m["won"] or sum(m["colour"]) <= 1e-4:
            continue
        cx, cy, k = m["cell"][0], m["cell"][1], m["size"]
        if covered[max(cy - reach - margin, 0):cy + k + reach + margin,
                   max(cx - reach - margin, 0):cx + k + reach + margin].sum() != k * k:
            continue  # another mote near it
        state, blocks, ok = {}, [], True
        for gy in range(cy - reach, cy + k + reach):
            for gx in range(cx - reach, cx + k + reach):
                sel = np.ix_(ys == gy, xs == gx)
                w = diff[sel][~tie[sel]]
                if not w.size or moving[sel].any():
                    ok = False
                    break
                state[gx, gy] = 1 if w.min() > 10 / 255 else 0 if w.max() <= tol + 1e-6 else -1
                if state[gx, gy] == 1:
                    blocks.append(w)
            if not ok:
                break
        if not ok or not blocks:
            continue  # at the view's edge, by an animated pixel, or hidden
        shown += 1
        if -1 in state.values():
            edges += 1  # part of a game pixel changed: at the scene's edge (depth is per internal pixel)
            print(f"info: mote {m['g']} at {m['cell']} size {k}: a game pixel partly changed (the scene's edge?)")
            continue
        alone += 1
        on = [c for c, v in state.items() if v == 1]
        x0, y0 = min(c[0] for c in on), min(c[1] for c in on)
        is_square = len(on) == k * k and all((x0 + i, y0 + j) in on for i in range(k) for j in range(k))
        flat = np.concatenate([b.ravel() for b in blocks])
        uniform = s.vulkan or flat.max() - flat.min() <= 1 / 255
        inside = max(c[0] for c in on) - x0 < k and max(c[1] for c in on) - y0 < k
        if k in (1, 2) and is_square and uniform:
            square += 1
            sizes.add(k)
        elif k in (1, 2) and inside and uniform:  # part of its square behind the scene, by whole game pixels
            square += 1
            edges += 1
            print(f"info: mote {m['g']} at {m['cell']} size {k}: {len(on)} of its game pixels in front of the scene")
        else:
            print(f"info: mote {m['g']} at {m['cell']} size {k}: changed game pixels {sorted(on)}, "
                  f"{flat.min() * 255:.1f}..{flat.max() * 255:.1f}/255")
    s.check(alone >= 10 and square == alone and sizes == {1, 2} and edges <= max(2, alone // 5),
            f"each lit mote a square of its 1 or 2 whole game pixels with nothing around it, nearest ({square} of "
            f"{alone} isolated motes; {edges} at the scene's edges; sizes {sorted(sizes)})")
    s.particles(level=0, emitters=[{"id": EMITTER, "remove": True}])
    if preset:
        g.call("graphics", lighting_preset=preset)
    g.call("pause", paused=False)


def check_editor(s):
    """An emitter is an editor object: listed with its icon, clicked, edited in Details (one undo step,
    reset to its asset's value), R sizes its box, deleted and back, duplicated."""
    g, p = s.game, s.player
    ref = "emitter:" + EMITTER
    s.particles(emitters=[{"id": EMITTER, "asset": "dust_motes", "pos": [round(p["x"] + 150), round(p["y"] - 100),
                                                                        round(p["z"])], "size": [160, 120, 160]}])
    g.call("graphics", editor_cutaway=False)
    g.call("editor_ui", visible=True)
    g.run(8)

    def objects():
        return {o["id"]: o for o in g.call("editor_select")["objects"]}

    def history():
        return g.call("editor_undo", action="list")

    o = objects().get(ref)
    s.check(o is not None and o["kind"] == "emitter" and o["label"] == "Emitter 1: Dust motes",
            f"an emitter is an object of the editor ({o})")
    if o and o["screen"]:
        r = g.call("editor_select", action="click", x=o["screen"][0], y=o["screen"][1])
        s.check(r["under"] == ref and r["selection"] == [ref], f"a click on its icon selects it ({r['selection']})")
    else:
        s.check(False, "its icon is on screen")
    got = g.call("editor_schema", action="get", id=ref)
    f = got["fields"]
    s.check(got["kind"] == "emitter" and f["count"]["value"] == 900 and f["count"]["source"] == "asset"
            and not f["count"]["overridden"] and f["curl"]["value"] == 9,
            f"Details: its fields, from its asset ({got['kind']}, count {f['count']})")
    done = history()["done"]
    g.call("editor_schema", action="set", id=ref, values={"count": 250, "drift": [0, -3, 0]})
    h = history()
    e = s.emitter()
    s.check(e["count"] == 250 and e["fields"]["drift"] == "0,-3,0" and h["done"] == done + 1
            and h["undo"].startswith("Edit"), f"set through the schema: the emitter changes, one undo step ({h['undo']})")
    g.call("editor_schema", action="reset", id=ref, fields=["count"])
    f = g.call("editor_schema", action="get", id=ref)["fields"]
    s.check(f["count"]["value"] == 900 and not f["count"]["overridden"], "the reset arrow: its asset's value again")
    g.call("editor_undo", action="undo")
    g.call("editor_undo", action="undo")
    s.check(s.emitter()["count"] == 900 and s.emitter()["fields"]["drift"] == "1.2,0.4,0.8", "undo puts both back")
    before = s.emitter()["size"]
    g.call("editor_transform", action="scale", id=ref, ratio=[2, 1, 0.5])
    after = s.emitter()["size"]
    s.check(after == [before[0] * 2, before[1], before[2] * 0.5] and history()["undo"] == "Scale emitter",
            f"R on an emitter: its box {before} -> {after}")
    g.call("editor_undo", action="undo")
    g.call("editor_schema", action="set", id=ref, values={"shape": "Disc", "radius": 50})
    before = s.emitter()["size"]
    g.call("editor_transform", action="scale", id=ref, ratio=[2, 2, 2])
    e = s.emitter()
    s.check(e["fields"]["radius"] == "100" and e["size"] == before and history()["undo"] == "Scale emitter",
            f"R on a disc: its radius 50 -> {e['fields']['radius']}, its box unchanged")
    g.call("editor_undo", action="undo")
    g.call("editor_undo", action="undo")  # the shape: back to a box
    s.check(s.emitter()["fields"]["radius"] == "64" and s.emitter()["fields"]["shape"] == "box", "undo: a box again")
    # Show > Emitters off: no icon, nothing to click.
    xy = objects()[ref]["screen"]
    s.particles(show=False)
    g.run(4)
    hidden = objects().get(ref)
    g.call("editor_select", action="clear")
    r = g.call("editor_select", action="click", x=xy[0], y=xy[1]) if xy else {"under": None}
    s.check(hidden is not None and hidden["screen"] is None and r["under"] != ref,
            f"Show > Emitters off: no icon to click ({hidden and hidden['screen']}, under {r['under']})")
    s.particles(show=True)
    g.run(4)
    # Add emitter (Details > Atmosphere): on the surface under the view's centre, selected, one undo step.
    done = history()["done"]
    hit = g.call("editor_select", action="pick", u=0.5, v=0.5)
    r = s.particles(add={"asset": "dust_motes", "here": True})
    added = r.get("added")
    e = s.emitter(added) if added else None
    want = hit.get("world") and [hit["world"][0], hit["world"][1] - 128 - 8, hit["world"][2]]  # its box's bottom 8 above
    s.check(e is not None and g.call("editor_select")["selection"] == ["emitter:" + added]
            and want and all(abs(a - b) <= 2 for a, b in zip(e["pos"], want))
            and history()["done"] == done + 1 and history()["undo"] == "Add emitter",
            f"Add emitter: on the surface under the view's centre ({e and e['pos']}, {want}), selected, one undo step")
    if added:
        s.particles(emitters=[{"id": added, "remove": True}])
    g.call("editor_select", action="set", ids=[ref])
    g.call("editor_select", action="delete")
    s.check(s.emitter() is None and g.call("editor_select")["selection"] == [], "Delete: it goes, with the selection")
    g.call("editor_undo", action="undo")
    s.check(s.emitter() is not None, "undo: it is back")
    g.call("editor_select", action="set", ids=[ref])
    r = g.call("editor_select", action="duplicate")
    copies = [e for e in s.particles()["emitters"] if e["id"] != EMITTER and e["asset"] == "dust_motes"]
    s.check(r["count"] == 1 and len(copies) == 1 and copies[0]["pos"][0] == s.emitter()["pos"][0] + 64,
            "Ctrl+D: a copy 64 units along x, a new id")
    for c in copies:
        s.particles(emitters=[{"id": c["id"], "remove": True}])
    g.call("editor_ui", visible=False)


def check_viewport(s):
    """Real mouse events: a click on an emitter's icon selects it, the W gizmo's centre drags it (one undo step)."""
    g, p = s.game, s.player
    ref = "emitter:" + EMITTER
    s.particles(emitters=[{"id": EMITTER, "asset": "dust_motes", "pos": [round(p["x"] + 150), round(p["y"] - 100),
                                                                        round(p["z"])], "size": [160, 120, 160]}])
    g.call("editor_ui", visible=True)
    g.run(8)

    def mouse(xy, down):
        g.call("system", mouse=[float(xy[0]), float(xy[1])], button=1, down=down)
        g.run(3)

    xy = {o["id"]: o for o in g.call("editor_select")["objects"]}[ref]["screen"]
    if not s.check(xy is not None, "its icon is on screen"):
        return
    g.call("editor_select", action="clear")
    mouse(xy, True)
    mouse(xy, False)
    r = g.call("editor_select")
    s.check(r["selection"] == [ref] and r["source"] == "viewport", f"a mouse click on its icon selects it ({r['selection']})")
    g.call("editor_input", keys=["w"])
    g.run(4)
    before, done = s.emitter()["pos"], g.call("editor_undo", action="list")["done"]
    mouse(xy, True)
    mouse([xy[0] + 30, xy[1]], True)
    mouse([xy[0] + 60, xy[1] + 10], True)
    mouse([xy[0] + 60, xy[1] + 10], False)
    after, h = s.emitter()["pos"], g.call("editor_undo", action="list")
    moved = math.dist(before, after)
    s.check(moved > 10 and h["done"] == done + 1 and h["undo"] == "Move emitter",
            f"the W gizmo's centre dragged 60 px: it moves {moved:.0f} units in the view's plane, one undo step ({h['undo']})")
    g.call("editor_undo", action="undo")
    s.check(s.emitter()["pos"] == before, "undo puts it back")
    s.particles(emitters=[{"id": EMITTER, "remove": True}])
    g.call("editor_ui", visible=False)


def check_setting(s):
    """Dust is a setting: RPC graphics sets it, particles reports it, settings save writes it."""
    g = s.game
    r = g.call("graphics", dust=1)
    s.check(r.get("dust") == 1 and s.particles()["level"] == 1, f"graphics {{dust: 1}} ({r.get('dust')})")
    g.call("settings", action="save")
    text = (s.out / "settings.cfg").read_text()
    s.check("dust 1" in text.splitlines(), "settings save writes `dust 1`")
    g.call("graphics", dust=0)
    g.call("settings", action="save")
    s.check(s.particles()["level"] == 0, "and back to Off")


def check_cost(s):
    """4096 motes: Vulkan's pass under 0.3 ms of GPU time (RPC perf section "particles", ROOD_PERF=1),
    OpenGL's CPU path under 3 ms a frame (RPC particles cpu_ms)."""
    g, p = s.game, s.player
    s.particles(emitters=[{"id": EMITTER, "asset": "dust_motes", "pos": [round(p["x"]), round(p["y"] - 120), round(p["z"])],
                           "count": 4096, "size": [600, 300, 600]}], level=2, time=-1)
    g.run(30)
    if s.vulkan:
        g.call("perf", reset=True)
        g.run(240)
        sections = g.call("perf").get("sections") or {}
        cost = sections.get("particles")
        s.check(cost is not None and cost < 0.3, f"Vulkan: the particles' pass takes {cost} ms a frame for 4096 motes")
    else:
        g.call("perf", reset=True)
        g.run(120)
        r = s.particles()
        s.check(r["motes"] == 4096 and 0 < r["cpu_ms"] < 3.0,
                f"OpenGL: the CPU path takes {r['cpu_ms']:.3f} ms a frame for {r['motes']} motes ({r['path']})")
    cpu = (g.call("perf").get("cpu_sections") or {}).get("particles")
    s.check(cpu is not None and (cpu < 3.0 if not s.vulkan else cpu < 1.0),
            f"RPC perf cpu_sections: the particles' CPU time {cpu} ms a frame")
    s.particles(emitters=[{"id": EMITTER, "remove": True}], level=0)
    g.run(10)
    if s.vulkan:
        g.call("perf", reset=True)
        g.run(60)
        sections = g.call("perf").get("sections") or {}
        s.check("particles" not in sections, "Dust Off: no pass at all")
    g.call("perf", reset=True)
    g.run(30)
    s.check("particles" not in (g.call("perf").get("cpu_sections") or {}), "Dust Off: no CPU time either")


EXAMPLE = "68f0c0de000001"  # lighting/default/rooms/zone34_room00.txt


def check_example(s):
    """The town house (zone 34 room 0): its authored dust glints in the windows' light, subtly."""
    g = s.game
    g.call("pause", paused=False)
    g.call("room_goto", zone=34, room=0, enemies=False, scripts=False)
    for _ in range(120):
        g.run(20)
        if g.call("room_goto", action="status")["phase"] in (0, 4):
            break
    g.run(60)
    s.check(g.state()["zone"] == 34, "warped to the town house (zone 34 room 0)")
    e = s.emitter(EXAMPLE)
    if not s.check(e is not None and e["asset"] == "dust_motes", f"its authored dust is there ({e and e['fields']})"):
        return
    g.call("post", layer="room", clear="all", snap=True)
    g.call("pause", paused=True)
    s.particles(level=0, time=12.0)
    off = s.shot("example-off", 40)
    s.particles(level=2)
    on = s.shot("example-high", 40)
    r = s.particles()
    (vx, vy, vw, vh), _, _ = view_cells(r)
    change = np.abs(on - off)[vy:vy + vh, vx:vx + vw].max(axis=2) * 255
    share, mean = float((change > 2).mean()), float(change.mean())
    every = motes(s, EXAMPLE)
    glints = [m for m in every if m["won"] and m["light"] > 0.05]
    windows = [x for x in g.call("glows")["glows"] if x.get("origin") == "room"]  # its windows' glows
    s.check(bool(windows) and len(glints) >= 10,
            f"its motes glint in the windows' light ({len(glints)} lit; {len(windows)} window glows)")
    # The motes the light would reach but something blocks (a wall, the room's own walls between window and
    # mote) from every source: dark, without a pixel. (A mote blocked from one source may be lit faintly by
    # another: that one shows, as dimly as it is lit.)
    blocked = [m for m in every if m["open"] > 0.02 and m["light"] < 1e-5]
    shown = [m for m in blocked if m["won"] or sum(m["colour"]) > 1e-4]
    s.check(len(blocked) >= 10 and not shown,
            f"in shadow they vanish: {len(blocked)} motes the windows would light are in shadow, {len(shown)} of them "
            f"take a pixel" + (f": {shown[:3]}" if shown else ""))
    s.check(share <= 0.02 and mean < 1.5, f"subtle: {share * 100:.2f} % of the view changes, by {mean:.2f}/255 on average")
    s.particles(level=0)
    g.call("pause", paused=False)


# In order: later sections start from where earlier ones leave the room.
SECTIONS = [check_items, check_assets, check_motes, check_picture, check_editor, check_viewport, check_setting, check_cost,
            check_example]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (items, motes, ...)")
    args = parser.parse_args()
    s = start(args)
    if s is None:
        return 77
    try:
        only = set(args.only.split(",")) if args.only else None
        for section in SECTIONS if not s.failures else []:
            name = section.__name__[len("check_"):]
            if only is None or name in only:
                print(f"-- {name}", flush=True)
                section(s)
    finally:
        status = finish(s)
    return status


if __name__ == "__main__":
    sys.exit(main())
