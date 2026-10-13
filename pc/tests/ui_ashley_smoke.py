#!/usr/bin/env python3
"""Modern menus, Ashley tab (pc/src/ui/ashley_bridge.c, ui_ashley_draw.cpp):
the summary's values, the live model and the state it gives back.

    python3 pc/tests/ui_ashley_smoke.py [--card pc/save/card1.mcd] [--out DIR]

From a copy of the card (skipped without one), in the save room, ui_menus on,
4:3 (960 x 720 layout units, 1:1 in a 960x720 window):
  - data: the ui_menu JSON's HP / MP / RISK, STR / INT / AGL (the original
    Status' EQP column: the record's value plus the accessory's), the limbs
    (hp, max, the state rule of vs_battle_getLimbStatus) equal Ashley's
    record in RAM; with STR-up (status bit 0x40, statusTimers[0]) and curse
    (0x10000) set in RAM: both listed with names, STR-up a buff with its
    timer's seconds, curse an ailment without a timer (-1)
  - the model: `model` reaches 2 (shown); the world is frozen meanwhile
    (D_800F190C 0, D_800EB9AE 1); the JSON's heap_free (the game heap's
    largest free block) covers heap_need (what the clone took, plus a
    margin) minus the clone's own bytes; in the presented picture the model
    column shows the model (a torso sample at the column's centre, 45% down,
    is not the ground colour) over the theme's ground colour (a sample inset
    from the column's top left corner)
  - the heap check: with ui_menu {model_heap_extra} asking for more than the
    heap has, the tab shows no model (model 0, model_blocked "heap", the
    world not frozen, slot 1 empty); back to 0, the model is built
  - the editor's free camera on (editor_camera at the game camera's place):
    the model goes (model 0, model_blocked "free_camera", the world runs);
    off again, it comes back
  - turning: idle, the yaw stays (no auto-rotation); the RPC right stick
    turns it (yaw and picture change); a pointer drag in the model column
    turns it (8 angle units per layout unit: right lowers the yaw, down
    raises the pitch)
  - tabs: R1 to Equipment frees the clone (model 0, actor slot 1 empty) and
    gives the world back (D_800F190C 1, D_800EB9AE 0); L1 back shows it
    again; another weapon equipped on Equipment, then Ashley: the JSON's
    weapon name follows, the model comes back holding the new weapon (the
    clone's weapon model id = actor 0's)
  - Square opens the original Status (currentMenu 0x45, model 0); Cross
    back: model 2, the yaw kept
  - Triangle closes: D_800F190C 1, D_800EB9AE 0, the HUD flag as before the
    open, vs_main_projectionDistance and the camera (scratchpad 0x1F800014,
    0x54 bytes) equal to their values before the menu opened
  - 16:9 (graphics widescreen, letterboxed in the same window): the model
    shows over the ground colour, the stick turns it, closing restores
  - a soft reset (L1 L2 R1 R2 Start Select) with the model shown: the title
    comes back without the shell; Continue, the menu shows the model again.
--out keeps screenshots (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

CLOSED = 0x3F
STATUS = 0x45  # the original Status (MENU4: command 4 -> 5 | 0x40)
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (characterState, D_800F4538, wepModels)
CAMERA, CAMERA_BYTES = 0x1F800014, 0x54  # vs_scratch.camera


def theme_ground():
    """The theme's ground colour (pc/assets/ui/theme.json; ashley_bridge.c clear_behind paints it)."""
    import json
    colour = json.loads((ROOT / "pc/assets/ui/theme.json").read_text())["colors"]["ground"]
    return tuple(int(colour[i:i + 2], 16) for i in (1, 3, 5))


GROUND = theme_ground()

# vs_battle_actor2 (146C.h)
HP, HP_MAX, MP, MP_MAX, RISK = 0x18, 0x1A, 0x1C, 0x1E, 0x20
STR, INT, AGL = 0x22, 0x26, 0x2A
ACC_STR, ACC_INT, ACC_AGL = 0x338 + 0x30, 0x338 + 0x32, 0x338 + 0x34  # accessory.currentStr ...
LIMBS, LIMB = 0x398, 0xDC  # limbs[6]: hp, maxHp, ..., nameIndex at +6
STATUSES, TIMERS = 0x948, 0x94C
STR_UP, CURSE = 0x40, 0x10000
MODEL_ID = 14  # vs_battle_wepModels_t.modelId (ui_equip_smoke MODEL_HEADER)



def samples(rect):
    """Sample points (layout units) in the model column: Ashley's torso (the
    column's centre, 45% down: the model is centred in it) and empty ground
    (inset from its top left corner)."""
    x, y, w, h = rect
    return {"torso": (x + w * 0.5, y + h * 0.45), "ground": (x + 12, y + 12)}


def s16(raw, offset):
    return int.from_bytes(raw[offset:offset + 2], "little", signed=True)


def u16(raw, offset):
    return int.from_bytes(raw[offset:offset + 2], "little")


def limb_state(hp, hp_max):
    """vs_battle_getLimbStatus (5BF94.c): critical, damaged, wounded, good, excellent."""
    if hp < 2:
        return 0
    if hp * 4 < hp_max:
        return 1
    if hp * 4 < hp_max * 3:
        return 2
    return 3 if hp < hp_max else 4


class Picture:
    """A presented frame, sampled in layout units (the layout is fitted and
    centred in the window: k = min(w / W, h / 720))."""

    def __init__(self, path, wide):
        from PIL import Image
        self.image = Image.open(path).convert("RGB")
        w, h = self.image.size
        width = 1280 if wide else 960
        self.k = min(w / width, h / 720)
        self.ox, self.oy = (w - width * self.k) / 2, (h - 720 * self.k) / 2

    def xy(self, x, y):
        return int(self.ox + x * self.k), int(self.oy + y * self.k)

    def at(self, x, y):
        return self.image.getpixel(self.xy(x, y))

    def box(self, rect):
        x0, y0 = self.xy(rect[0], rect[1])
        x1, y1 = self.xy(rect[0] + rect[2], rect[1] + rect[3])
        return self.image.crop((x0, y0, x1, y1))


def near(a, b, tolerance=6):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def changed_pixels(a, b, threshold=24):
    """How many pixels of two equal-size crops differ by more than threshold in a channel."""
    pa, pb = a.load(), b.load()
    w, h = min(a.size[0], b.size[0]), min(a.size[1], b.size[1])
    return sum(1 for y in range(0, h, 2) for x in range(0, w, 2)
               if max(abs(p - q) for p, q in zip(pa[x, y], pb[x, y])) > threshold)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_ashley"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    os.environ.pop("ROOD_SETTINGS", None)
    sym = symbols()
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    g = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env)
    try:
        g.wait_title()
        continue_game(g)
        state = g.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1
        t = Test(g, sym, out, args.out is not None, check)
        t.run_43()
        t.run_169()
        t.soft_reset()
    finally:
        g.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


def continue_game(g):
    g.press("start", vsyncs=4, settle=90)
    for _ in range(3):
        g.press("circle", vsyncs=3, settle=90)
    g.run(400)


class Test:
    def __init__(self, g, sym, out, shots, check):
        self.g, self.sym, self.out, self.shots, self.check = g, sym, out, shots, check
        self.picture_n = 0

    # ---- reads ----
    def ptr(self, address):
        return int.from_bytes(self.g.read(address, PTR), "little")

    def i32(self, name):
        return int.from_bytes(self.g.read(self.sym[name], 4), "little", signed=True)

    def u8(self, name):
        return self.g.read(self.sym[name], 1)[0]

    def menu(self):
        return self.u8("vs_battle_menuState")

    def actor2(self):
        actor = self.ptr(self.sym["vs_battle_characterState"])
        return int.from_bytes(self.g.read(actor + 0x3C, 4), "little")

    def clone(self):
        return self.ptr(self.sym["D_800F4538"] + PTR)  # D_800F4538[1]: the menu clone's slot

    def model_id(self, i):
        p = self.ptr(self.sym["vs_battle_wepModels"] + i * PTR)
        return self.g.read(p + MODEL_ID, 1)[0] if p else None

    def world(self):
        """What the menu changes and must give back."""
        return {"190C": self.i32("D_800F190C"), "9AE": self.u8("D_800EB9AE"), "hud": self.u8("D_800EB9AF"),
                "H": self.i32("vs_main_projectionDistance"), "camera": self.g.read(CAMERA, CAMERA_BYTES)}

    def ui(self, **params):
        return self.g.call("ui_menu", **params)

    def ash(self):
        return self.ui().get("ashley") or {}

    # ---- waits (frame by frame; raise on timeout) ----
    def wait(self, pred, limit, what, step=1, quiet=False):
        """Vsyncs until pred() holds; quiet: None on timeout instead of raising."""
        for n in range(0, limit + 1, step):
            if pred():
                return n
            self.g.run(step)
        if quiet:
            return None
        raise TimeoutError(f"{what}: not after {limit} vsyncs (currentMenu {self.menu():#x}, shell {self.ui()})")

    def wait_model(self, want, limit=180):
        return self.wait(lambda: self.ui().get("screen") == "ashley" and self.ash().get("model") == want,
                         limit, f"the Ashley model state {want}")

    def wait_tab(self, tab, limit=60):
        return self.wait(lambda: self.ui().get("tab") == tab and self.ui().get("visible"), limit, f"tab {tab}")

    def wait_menu(self, want, limit=300):
        return self.wait(lambda: self.menu() == want, limit, f"currentMenu {want:#x}", step=2)

    def close_menu(self):
        self.g.press("triangle", vsyncs=3, settle=4)
        self.wait(lambda: self.menu() == CLOSED and not self.ui().get("visible"), 400, "the menu closes", step=4)

    def picture(self, name, wide):
        self.picture_n += 1
        path = self.out / (f"{name}.png" if self.shots else f"shot-{self.picture_n % 2}.png")
        self.g.present_screenshot(path)
        return Picture(path, wide)

    # ---- checks ----
    def data_checks(self):
        check, g = self.check, self.g
        a = self.ash()
        rec = g.read(self.actor2(), 0x964)
        want = {"hp": s16(rec, HP), "hp_max": s16(rec, HP_MAX), "mp": s16(rec, MP), "mp_max": s16(rec, MP_MAX),
                "risk": u16(rec, RISK)}
        got = {k: a.get(k) for k in want}
        check(a.get("valid") and got == want, f"HP / MP / RISK as Ashley's record: {got} (RAM {want})")
        eqp = (u16(rec, STR) + s16(rec, ACC_STR), u16(rec, INT) + s16(rec, ACC_INT), u16(rec, AGL) + s16(rec, ACC_AGL))
        check((a.get("str"), a.get("int"), a.get("agl")) == eqp,
              f"STR / INT / AGL {a.get('str')}/{a.get('int')}/{a.get('agl')} = the record's + the accessory's {eqp}")
        limbs = []
        for i in range(6):
            raw = rec[LIMBS + i * LIMB:LIMBS + (i + 1) * LIMB]
            if raw[6] == 0:
                break
            hp, hp_max = s16(raw, 0), s16(raw, 2)
            limbs.append((hp, hp_max, limb_state(hp, hp_max)))
        got = [(x["hp"], x["hp_max"], x["state"]) for x in a.get("limbs", [])]
        check(got == limbs and len(limbs) == 5 and all(x["name"] for x in a["limbs"]),
              f"limbs {[x['name'] for x in a.get('limbs', [])]}: hp, max, state {got} (RAM {limbs})")
        check(bool(a.get("weapon")) and a.get("area") is not None and a.get("play"),
              f"weapon '{a.get('weapon')}', shield '{a.get('shield')}', place '{a.get('area')}' / "
              f"'{a.get('room')}', play time {a.get('play')}")

        # status effects: STR-up with its timer, curse (no timer)
        address = self.actor2()
        before_effects = len(a.get("effects", []))
        statuses = int.from_bytes(g.read(address + STATUSES, 4), "little")
        timer0 = g.read(address + TIMERS, 1)
        g.write(address + STATUSES, (statuses | STR_UP | CURSE).to_bytes(4, "little"))
        g.write(address + TIMERS, bytes([77]))
        g.run(2)
        effects = self.ash().get("effects", [])
        left = g.read(address + TIMERS, 1)[0]
        buffs = [e for e in effects if e["buff"]]
        ailments = [e for e in effects if not e["buff"]]
        check(before_effects == 0 and len(effects) == 2 and all(e["name"] for e in effects),
              f"two effects listed with names: {effects} (before: {before_effects})")
        check(len(buffs) == 1 and buffs[0]["seconds"] == left and 70 <= left <= 77,
              f"STR-up: a buff with its timer's seconds ({buffs}, statusTimers[0] {left})")
        check(len(ailments) == 1 and ailments[0]["seconds"] == -1, f"curse: an ailment without a timer ({ailments})")
        if self.shots:
            self.picture("ashley-effects", False)
        g.write(address + STATUSES, statuses.to_bytes(4, "little"))
        g.write(address + TIMERS, timer0)
        g.run(2)
        check(len(self.ash().get("effects", [])) == before_effects, "the effects go with the bits")

    def model_shown(self, wide, name):
        """The model column: the torso sample is the model, the ground sample the ground colour.
        The presented image trails the tick by up to two frames (measured: the
        model's first draw shows 3 vsyncs after `model` reads 2): 8 vsyncs first."""
        self.g.run(8)
        p = self.picture(name, wide)
        rect = self.ash().get("model_rect")
        s = samples(rect)
        torso, ground = p.at(*s["torso"]), p.at(*s["ground"])
        self.check(not near(torso, GROUND, 12) and near(ground, GROUND),
                   f"{name}: the model shows in its column {rect} (torso {torso}, ground {ground}, "
                   f"ground colour {GROUND})")
        return p

    def stick_turn(self, wide, before, name):
        g = self.g
        yaw0 = self.ash()["model_yaw"]
        g.call("input", right_stick=[1, 0], vsyncs=30)
        g.run(40)
        yaw1 = self.ash()["model_yaw"]
        after = self.picture(name, wide)
        rect = self.ash()["model_rect"]
        n = changed_pixels(before.box(rect), after.box(rect))
        self.check(0 < (yaw0 - yaw1) % 4096 < 2048 and n > 200,
                   f"{name}: the right stick turns the model (yaw {yaw0} -> {yaw1}; {n} sampled pixels changed)")
        return after

    def run_43(self):
        g, check = self.g, self.check
        before = self.world()
        g.call("ui_menu", modern=True)
        g.press("triangle", vsyncs=3, settle=2)
        self.wait_tab("ashley")
        n = self.wait_model(2)
        w = self.world()
        check(True, f"the model shows {n} vsyncs after the Ashley tab")
        check(w["190C"] == 0 and w["9AE"] == 1 and self.clone() != 0,
              f"meanwhile the world is frozen (D_800F190C {w['190C']}, D_800EB9AE {w['9AE']}), clone built")
        a = self.ash()
        # the clone's allocations: records 0x1900, animation 0x7800, two weapons (0x5E8 + 0x800 each), headers
        check(40000 < a.get("heap_need", 0) < 80000 and a.get("heap_free", 0) > 0 and a.get("model_blocked") == "",
              f"the heap: the clone asked for {a.get('heap_need')} bytes (with the margin); largest free block "
              f"after the build {a.get('heap_free')}")
        self.data_checks()

        p = self.model_shown(False, "ashley")
        a = self.ash()
        yaw, pitch = a["model_yaw"], a["model_pitch"]
        g.run(60)
        a = self.ash()
        check((a["model_yaw"], a["model_pitch"]) == (yaw, pitch) and a["model"] == 2,
              f"idle 60 vsyncs: no auto-rotation (yaw {yaw} -> {a['model_yaw']}, pitch {pitch} -> {a['model_pitch']})")
        self.stick_turn(False, p, "ashley-stick")

        # pointer drag: 30 right, 20 down, then 10 up
        a = self.ash()
        r, yaw, pitch = a["model_rect"], a["model_yaw"], a["model_pitch"]
        x, y = r[0] + r[2] / 2, r[1] + r[3] / 2
        for px, py, down in ((x, y, True), (x + 30, y + 20, True), (x + 30, y + 10, True), (x + 30, y + 10, False)):
            self.ui(pointer={"x": px, "y": py, "down": down})
            g.run(2)
        a = self.ash()
        dyaw, dpitch = (a["model_yaw"] - yaw) % 4096, a["model_pitch"] - pitch
        check(abs(dyaw - (4096 - 240)) <= 8 and abs(dpitch - 80) <= 8,
              f"pointer drag (+30, +20, -10): yaw {yaw} -> {a['model_yaw']} (-240), "
              f"pitch {pitch} -> {a['model_pitch']} (+80)")
        self.ui(pointer={"x": -1, "y": -1})
        g.run(2)
        if self.shots:
            self.picture("ashley-dragged", False)

        self.heap_and_free_camera()

        # R1: Equipment gives the world back; L1: the model again
        g.press("r1", vsyncs=3, settle=2)
        self.wait_tab("equipment")
        g.run(2)
        w = self.world()
        a = self.ui().get("ashley")
        check(w["190C"] == 1 and w["9AE"] == 0 and self.clone() == 0,
              f"R1 Equipment: the clone is freed (slot 1 {self.clone():#x}), the world runs "
              f"(D_800F190C {w['190C']}, D_800EB9AE {w['9AE']})")
        g.press("l1", vsyncs=3, settle=2)
        n = self.wait_model(2)
        check(self.world()["190C"] == 0, f"L1 Ashley: the model again ({n} vsyncs)")

        # another weapon on Equipment, then Ashley
        weapon0 = self.ash().get("weapon")
        g.press("r1", vsyncs=3, settle=2)
        self.wait_tab("equipment")
        err = self.equip_other_weapon()
        g.press("l1", vsyncs=3, settle=2)
        self.wait_model(2)
        g.run(4)
        a = self.ash()
        clone_wep, actor_wep = self.model_id(2), self.model_id(0)
        check(err is None and a.get("weapon") and a.get("weapon") != weapon0 and a["model"] == 2
              and clone_wep is not None and clone_wep == actor_wep,
              f"another weapon: '{weapon0}' -> '{a.get('weapon')}', model {a['model']}, the clone's weapon "
              f"model {clone_wep} = actor 0's {actor_wep} {err or ''}")
        if self.shots:
            self.picture("ashley-weapon", False)

        # Square: the original Status; Cross back
        yaw = self.ash()["model_yaw"]
        g.press("square", vsyncs=3, settle=2)
        self.wait_menu(STATUS)
        g.run(30)
        a = self.ui().get("ashley") or {}
        check(a.get("model", 0) == 0 and not self.ui().get("visible"),
              f"Square: the original Status ({self.menu():#x}), model {a.get('model')}")
        if self.shots:
            self.picture("status", False)
        # Cross before MENU4 has drawn its screen in does nothing: again until it leaves
        for _ in range(6):
            g.press("cross", vsyncs=3, settle=2)
            if self.wait(lambda: self.menu() != STATUS, 60, "", step=2, quiet=True) is not None:
                break
        n = self.wait_model(2, limit=300)
        a = self.ash()
        check(a["model_yaw"] == yaw and self.menu() == 0,
              f"Cross: back on Ashley, model 2 ({n} vsyncs), yaw kept ({yaw} -> {a['model_yaw']})")

        # Triangle: everything given back
        self.close_menu()
        g.run(2)
        self.restored(before, "4:3")

    def heap_and_free_camera(self):
        """No model without heap for it, nor with the editor's free camera on."""
        g, check = self.g, self.check
        # a heap too small: R1 / L1 rebuilds with the check asking for 64 MB more
        g.press("r1", vsyncs=3, settle=2)
        self.wait_tab("equipment")
        self.ui(model_heap_extra=64 << 20)
        g.press("l1", vsyncs=3, settle=2)
        self.wait_tab("ashley")
        n = self.wait(lambda: self.ash().get("model_blocked") == "heap", 120, "the heap check", quiet=True)
        g.run(10)
        a, w = self.ash(), self.world()
        check(n is not None and a.get("model") == 0 and w["190C"] == 1 and self.clone() == 0
              and a.get("heap_need", 0) > a.get("heap_free", 0),
              f"heap too small: no model (model {a.get('model')}, blocked '{a.get('model_blocked')}', need "
              f"{a.get('heap_need')} > free {a.get('heap_free')}), the world runs (D_800F190C {w['190C']}), "
              f"slot 1 {self.clone():#x}")
        if self.shots:
            self.picture("ashley-no-heap", False)
        self.ui(model_heap_extra=0)
        n = self.wait_model(2)
        check(self.ash().get("model_blocked") == "", f"the heap check passes again: the model ({n} vsyncs)")

        # the free camera: at the game camera's place, then off
        cam = g.call("editor_camera")
        on = g.call("editor_camera", x=cam["x"], y=cam["y"], z=cam["z"], yaw=cam["yaw"], pitch=cam["pitch"])
        n = self.wait(lambda: self.ash().get("model_blocked") == "free_camera", 60, "the free camera", quiet=True)
        g.run(4)
        a, w = self.ash(), self.world()
        check(on.get("active") and n is not None and a.get("model") == 0 and w["190C"] == 1 and self.clone() == 0,
              f"free camera on: no model (model {a.get('model')}, blocked '{a.get('model_blocked')}'), the world "
              f"runs (D_800F190C {w['190C']})")
        g.call("editor_camera", off=1)
        n = self.wait_model(2)
        check(not g.call("editor_camera").get("active"), f"free camera off: the model again ({n} vsyncs)")

    def restored(self, before, name):
        w = self.world()
        diff = [k for k in before if before[k] != w[k]]
        self.check(not diff and w["190C"] == 1 and w["9AE"] == 0 and self.clone() == 0,
                   f"{name}: closed, the world given back (D_800F190C {w['190C']}, D_800EB9AE {w['9AE']}, "
                   f"HUD {w['hud']}, H {w['H']}; differ from before the open: {diff})")

    def equip_other_weapon(self):
        """On Equipment > gear: the weapon slot, another bag weapon, Circle."""
        g = self.g

        def eq():
            return self.ui().get("equipment") or {}
        if eq().get("sub") != "gear":
            g.press("r2", vsyncs=3, settle=6)
        for _ in range(9):
            if eq().get("slot") == "weapon":
                break
            g.press("up", vsyncs=3, settle=6)
        if eq().get("slot") != "weapon":
            return "no weapon slot"
        current = eq()["slots"][0]["inventory"]
        g.press("circle", vsyncs=3, settle=4)
        self.wait(lambda: eq().get("mode") == "pick" and not eq().get("busy") and eq().get("candidates") is not None,
                  120, "the weapon list")
        cands = [c["inventory"] for c in eq()["candidates"]]
        target = next((c for c in cands if c not in (current, -1)), None)
        if target is None:
            return f"no other weapon among {cands}"
        for _ in range(len(cands) + 1):
            if eq().get("cursor") == cands.index(target):
                break
            g.press("down", vsyncs=3, settle=4)
        g.press("circle", vsyncs=3, settle=4)
        self.wait(lambda: not self.ui().get("holding") and not eq().get("busy") and eq().get("mode") == "gear"
                  and eq()["slots"][0]["inventory"] == target, 600, "the weapon equip", step=4)
        return None

    def run_169(self):
        g = self.g
        g.call("graphics", widescreen=True)
        g.run(30)
        before = self.world()
        g.press("triangle", vsyncs=3, settle=2)
        self.wait_tab("ashley")
        self.wait_model(2)
        g.run(2)
        p = self.model_shown(True, "ashley-169")
        self.stick_turn(True, p, "ashley-169-stick")
        self.close_menu()
        g.run(2)
        self.restored(before, "16:9")
        g.call("graphics", widescreen=False)
        g.run(30)

    def soft_reset(self):
        g, check = self.g, self.check
        hud = self.u8("D_800EB9AF")
        g.press("triangle", vsyncs=3, settle=2)
        self.wait_tab("ashley")
        self.wait_model(2)
        titles = pathlib.Path(g.log).read_text().count("TITLE MENU READY")
        g.call("input", buttons=["l1", "l2", "r1", "r2", "start", "select"], vsyncs=6)
        self.wait(lambda: pathlib.Path(g.log).read_text().count("TITLE MENU READY") > titles, 3000,
                  "the title after a soft reset", step=30)
        s = self.ui()
        model = (s.get("ashley") or {}).get("model", 0)
        check(not s.get("visible") and not s.get("holding") and model == 0,
              f"reset: over the title no shell (visible {s.get('visible')}), model {model}")
        if self.shots:
            self.picture("reset-title", False)
        continue_game(g)
        state = g.state()
        g.press("triangle", vsyncs=3, settle=2)
        self.wait_tab("ashley")
        n = self.wait_model(2)
        a = self.ash()
        check((state["zone"], state["room"]) == (9, 17) and a["model"] == 2,
              f"reset: Continue, the menu shows the model again ({n} vsyncs, yaw {a['model_yaw']})")
        if self.shots:
            self.picture("reset-ashley", False)
        self.close_menu()
        check(self.u8("D_800EB9AF") == hud and self.i32("D_800F190C") == 1, "reset: and closes")


if __name__ == "__main__":
    sys.exit(main())
