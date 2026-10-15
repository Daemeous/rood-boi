#!/usr/bin/env python3
"""Modern menus, the Map frame (pc/src/ui/map_bridge.c, ui_map.cpp,
ui_map_draw.cpp; MENU5 hooks in src/MENU/MENU5.PRG/4D8.c): the game's own
map screen keeps running and owning the pad; the shell draws its chrome
around the 3D map and turns the pointer into requests MENU5 applies at its
own input point.

    python3 pc/tests/ui_map_smoke.py [--card pc/save/card1.mcd] [--out DIR]
        [--parity-only | --no-parity] [--diff-all]

From copies of the card (skipped without one), in the save room, with a
fixture: areas 1..13 visited (areaFlags) and every countable room seen
(roomFlags |= vs_battle_mapCompletionFlags: an area bit without a seen room
would hang MENU5's _updateRoomIndex), so the area list pages (more than 9).

Parity (three processes, the same pad or pointer story):
  A  ui_menus off: Triangle, Down x3, Circle (MENU5), Circle (its area
     list), Down x4, Circle (the area switch), R1 x2, L2 + Right, Cross (the
     game's menu), Triangle (closed)
  B  ui_menus on: the shell's Leá Monde > Map, then the same pad input
  B2 ui_menus on: the same through the pointer (an area row clicked, room
     rows clicked, a drag), then a right click (back to the shell)
  compared after the open, after the switch and after the close: the map
  status, mapPaling, the cursor memories, the settings, the menu state, the
  HUD flags, vs_battle_sceneBuffer, the field camera (scratch 0x1F800014),
  the projection distance and the VRAM rects MENU5 saves and restores (MASKS:
  what may differ, each with its reason); and MENU5's own statics (scene,
  room, yaw, pitch, distance, pan; read by symbol from its overlay's .bss)
  after the switch and after the rooms and the turn, A against B and B2 (B's
  published model against them too). A waits on MENU5's state, not on time.
  --diff-all: save snapshots at A's and B's ends and list every differing
  symbol (pc/tests/snapshot_diff.py), to derive MASKS.

Frame checks (process B, after the parity story):
  - off control (A): MENU5's controls help at the left edge, "map" null
  - open: map.state reaches ready, the areas equal the areaFlags bits, the
    current area is the scene, completion is vs_battle_getMapCompletion's
    formula on a copy (the RAM's roomFlags unchanged), the picture shows in
    the cut-out and the rail is the ground colour, MENU5's own controls help
    never drawn while framed (map_controls; the off path counts it)
  - pad: R1 / L1 rooms (the tab stays), L2 + Right / Up rotate (pitch
    clamped), R2 + Up / Down zoom (clamped 512..1024), the D-pad pans
    (clamped), Start resets, Select hides the chrome (the backdrop opens)
  - the list: Circle focuses it on the current row, a second Circle while
    its rows come in is ignored (MAINMENU's case 16), Cross back, Triangle
    closes, R1 reopens the shell on the next tab, Down walks every row (the
    window scrolls: cursor memories as MAINMENU's list leaves them), the
    Choose prompt's halves (Up / Down)
  - pointer: drag (8 a layout unit), wheel (64 a notch, up closer), the
    Zoom prompt turns at the clamp, middle drag and Shift + drag (a game
    pixel per 3 units, in MENU5's steps of 4), the shown area's row does
    nothing, an area row, a room row, a click while the area switches is
    dropped (busy), the pad wins over a drag in the same frame, a right
    click (the shell on Leá Monde / Map), a tab click (the shell on that
    tab), prompts (Reset, Hide, the pill)
  - paling: mapPaling[id] set: the area is JAMMED, the camera and room input
    is ignored (pad; pointer: the drag dropped by the frame, the Room prompt
    taken by MENU5 and refused by its paled guard), the paling line is
    published, switching out works
  - close: Cross (the shell on the Map row), Triangle (0x3F, the HUD flag
    back, sceneBuffer NULL, the heap's largest free block as before, the
    VRAM rects as before)
  - robustness: a soft reset at MENU5's state 4 and during an area switch
    (the title without chrome, "map" null, Continue works), a forced
    currentMenu write ends the frame; written back, MENU5 runs on unframed
    (its own controls help and list: the frame lost, as after a save state)
  - 16:9: open, switch, close; the chrome inside 1280; the cut-out shows
    the picture; the list shows every row the rail fits (13 > its 9)
A room whose scene id is 0 (the Map row unselectable) is not pinned for this
card: that case is the shell's (ui_shell_probe) and is skipped here."""
import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, BUILD, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

CLOSED = 0x3F
MAP_MENU = 6 | 0x40
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4
CAMERA, CAMERA_BYTES = 0x1F800014, 0x54  # vs_scratch.camera
MAP_PALING = 0x135  # offsetof(vs_main_flags_t, mapPaling)
AREAS = list(range(1, 14))  # the fixture's visited areas (13 > the list's 9 rows)
TARGET_ROW = 4  # the parity story's area: four rows below the current one
VRAM_STRIDE = 2048
P_ROOM_NEXT, P_ZOOM, P_RESET, P_HIDE, P_LIST_UP, P_LIST_DOWN = 1, 3, 4, 6, 9, 10  # PC_UI_MAP_P_*
# MENU5's statics (4D8.c), by symbol in its overlay's sections (other
# overlays have their own _menuState, _cameraAngles, ...)
MENU5_SECTION = "rood_ovl_menu_menu5_prg"
MENU5_STATICS = ("_currentScene", "_currentRoomIndex", "_menuState", "_cameraAngles", "_cameraDistance",
                 "_geomOffsetX", "_geomOffsetY", "_menuFramesCounter")

# Masked: (region, start, end, reason). Everything else must be equal. None
# needed: every compared region ends equal on both paths, the cursor memories
# included (--diff-all: the rest of RAM differs with the paths' different
# frame counts: music sequencer, frame counters, RNG, the overlays' statics).
MASKS = []


def menu5_symbols():
    """MENU5's statics: each name must resolve to one address in its overlay."""
    image = BUILD / "rood_battle_probe"
    out = subprocess.run([os.environ.get("ROOD_OBJDUMP", "objdump"), "-t", str(image)], capture_output=True,
                         text=True).stdout
    found = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 6 and parts[-1] in MENU5_STATICS and parts[-3].startswith(MENU5_SECTION):
            found.setdefault(parts[-1], []).append(int(parts[0], 16))
    bad = [n for n in MENU5_STATICS if len(found.get(n, [])) != 1]
    if bad:
        raise RuntimeError(f"MENU5 statics not resolved to one address: {bad} ({found})")
    return {n: a[0] for n, a in found.items()}


def ground_like(px, ground):
    return all(abs(int(a) - int(b)) <= 6 for a, b in zip(px, ground))


class Session:
    def __init__(self, name, out, card, env, shots):
        self.name, self.out, self.shots = name, out, shots
        self.game = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env)
        self.game.call("graphics", interpolate=False)
        sym = symbols()
        self.sym = sym
        self.ms = sym["vs_main_mapStatus"]
        self.cf = sym["vs_battle_mapCompletionFlags"]
        self.flags = sym["vs_main_stateFlags"]
        self.menu_state = sym["vs_battle_menuState"]
        self.hud = sym["D_800EB9AF"]
        self.vram = sym["vram"]
        self.m5 = menu5_symbols()

    def close(self):
        self.game.close()

    # ---- reads ----
    def shell(self):
        return self.game.call("ui_menu")

    def map(self):
        return self.shell().get("map")

    def current_menu(self):
        return self.game.read(self.menu_state, 1)[0]

    def menu5(self):
        """MENU5's own statics, as the frame's model names them."""
        g, m5 = self.game, self.m5

        def i32(name):
            return struct.unpack("<i", g.read(m5[name], 4))[0]
        vx, vy, _vz = struct.unpack("<3h", g.read(m5["_cameraAngles"], 6))
        return {"scene": i32("_currentScene"), "room": i32("_currentRoomIndex"), "yaw": vy, "pitch": vx,
                "distance": i32("_cameraDistance"), "geom": [i32("_geomOffsetX"), i32("_geomOffsetY")],
                "menu_state": i32("_menuState")}

    def wait_menu5(self, pred, limit=600):
        """Until MENU5's statics satisfy pred (the off path publishes nothing)."""
        for _ in range(limit // 2):
            m = self.menu5()
            if pred(m):
                return m
            self.game.run(2)
        return self.menu5()

    def align(self):
        """Just after a map frame (it runs every second vsync): pad holds
        then cover the same frames in every process."""
        n = self.game.read(self.m5["_menuFramesCounter"], 4)
        for _ in range(4):
            self.game.run(1)
            if self.game.read(self.m5["_menuFramesCounter"], 4) != n:
                return

    def controls(self):
        return self.shell().get("map_controls")

    def scene_buffer(self):
        return int.from_bytes(self.game.read(self.sym["vs_battle_sceneBuffer"], PTR), "little")

    def room_flags(self):
        return self.game.read(self.ms, 64)

    def area_flags(self):
        return struct.unpack("<2I", self.game.read(self.ms + 64, 8))

    def vram_rows(self, y0, y1, x0, x1):
        g, out = self.game, b""
        for y in range(y0, y1, 16):
            n = min(16, y1 - y)
            raw = g.read(self.vram + y * VRAM_STRIDE, n * VRAM_STRIDE)
            for k in range(n):
                out += raw[k * VRAM_STRIDE + x0 * 2:k * VRAM_STRIDE + x1 * 2]
        return out

    def dump(self):
        g = self.game
        return {
            "mapStatus": g.read(self.ms, 72),
            "mapPaling": g.read(self.flags + MAP_PALING, 32),
            "cursorMemories": g.read(self.sym["D_800F4EE8"], 52),
            "settings": g.read(self.sym["vs_main_settings"], 32),
            "menuState": g.read(self.menu_state, 4),
            "hud": g.read(self.hud, 1) + g.read(self.sym["D_800EB9B0"], 4),
            "sceneBuffer": g.read(self.sym["vs_battle_sceneBuffer"], PTR),
            "camera": g.read(CAMERA, CAMERA_BYTES),
            "projection": g.read(self.sym["vs_main_projectionDistance"], 4),
            "vram_bg": self.vram_rows(256, 496, 640, 800),
            "vram_clut": self.vram_rows(227, 228, 768, 1024),
        }

    def shot(self, name, force=False):
        if self.shots or force:
            path = self.out / f"{self.name}-{name}.png"
            self.game.present_screenshot(path)
            return path
        return None

    def image(self, name):
        """The next presented image as an array (kept with --out)."""
        import numpy as np
        from PIL import Image
        path = self.out / f"{self.name}-{name}.png"
        self.game.present_screenshot(path)
        image = np.asarray(Image.open(path).convert("RGB"))
        if not self.shots:
            path.unlink()
        return image

    # ---- the fixture ----
    def fixture(self):
        g = self.game
        print(f"{self.name}: card areaFlags {self.area_flags()[0]:#x}, roomFlags {self.room_flags().hex()}")
        rf = struct.unpack("<16I", self.room_flags())
        cf = struct.unpack("<16I", g.read(self.cf, 64))
        g.write(self.ms, struct.pack("<16I", *[a | b for a, b in zip(rf, cf)]))
        area = self.area_flags()[0]
        for i in AREAS:
            area |= 1 << i
        g.write(self.ms + 64, struct.pack("<I", area))

    # ---- waits ----
    def wait_map(self, pred, limit=600, step=2):
        g = self.game
        for _ in range(limit // step):
            m = self.map()
            if pred(m):
                return m
            g.run(step)
        return self.map()

    def wait_ready(self, limit=600):
        return self.wait_map(lambda m: m and m["state"] == "ready", limit)

    def wait_shell(self, limit=300):
        g = self.game
        for _ in range(limit // 3):
            s = self.shell()
            if s["visible"] and self.current_menu() == 0:
                g.run(20)  # the shell takes the pad once it has settled
                return self.shell()
            g.run(3)
        return self.shell()

    def wait_closed(self, limit=300):
        g = self.game
        for _ in range(limit // 5):
            if self.current_menu() == CLOSED and not self.shell()["visible"]:
                g.run(20)
                return True
            g.run(5)
        return False

    # ---- the pointer (layout units) ----
    def ptr(self, run=2, **p):
        self.game.call("ui_menu", pointer=p)
        self.game.run(run)

    def target(self, kind, index):
        m = self.map()
        # the last one drawn wins (the hidden chrome's pill over a fading prompt)
        for h in reversed(m.get("hits", [])):
            if h["k"] == kind and h["i"] == index:
                r = h["r"]
                return (r[0] + r[2]) / 2, (r[1] + r[3]) / 2
        return None

    def click(self, kind, index, run=2):
        xy = self.target(kind, index)
        if xy is None and kind in ("area", "room"):
            xy = self.scroll_to(kind, index)
        if xy is None:
            return False
        self.ptr(run, x=xy[0], y=xy[1], click=True)
        return True

    def scroll_to(self, kind, index):
        """The rail's wheel until that row is drawn (the rail shows a window)."""
        rail = self.target("rail", 0)
        if rail is None:
            return None
        for wheel in (-1, 1):
            for _ in range(24):
                self.ptr(2, x=rail[0], y=rail[1], wheel=wheel)
                xy = self.target(kind, index)
                if xy is not None:
                    return xy
        return None

    def drag(self, dx, dy, middle=False, shift=False):
        c = self.map()["column"]
        x, y = c[0] + c[2] / 2, c[1] + c[3] / 2
        held = {"middle": True} if middle else {"down": True, "shift": True} if shift else {"down": True}
        self.ptr(1, x=x, y=y, **held)
        self.ptr(1, x=x + dx, y=y + dy, **held)
        self.ptr(3, x=x + dx, y=y + dy)

    # ---- the shell's Map row ----
    def open_shell_map(self):
        g = self.game
        g.press("triangle", vsyncs=3, settle=40)
        for _ in range(8):
            if self.shell()["tab"] == "lea_monde":
                break
            g.press("r1", vsyncs=3, settle=10)
        g.press("circle", vsyncs=3, settle=2)
        return self.wait_ready()

    def reopen_from_shell(self):
        """The shell is up: Leá Monde > Map again."""
        g = self.game
        for _ in range(8):
            if self.shell()["tab"] == "lea_monde":
                break
            g.press("r1", vsyncs=3, settle=10)
        g.press("circle", vsyncs=3, settle=2)
        return self.wait_ready()


def diff(a, b):
    found = []
    for region in a:
        x, y = a[region], b[region]
        if len(x) != len(y):
            found.append((region, 0, max(len(x), len(y))))
            continue
        i = 0
        while i < len(x):
            if x[i] != y[i] and not any(r == region and s <= i < e for r, s, e, _ in MASKS):
                j = i
                while j < len(x) and x[j] != y[j]:
                    j += 1
                found.append((region, i, j))
                i = j
            else:
                i += 1
    return found


def describe(a, b, rest):
    return ", ".join(f"{r} {s:#x}..{e:#x} {a[r][s:e][:16].hex()} vs {b[r][s:e][:16].hex()}" for r, s, e in rest[:8])


def completion(room_flags, comp_flags):
    rf = list(struct.unpack("<16I", room_flags))
    cf = struct.unpack("<16I", comp_flags)
    if rf[1] & 0x800000:
        rf[1] |= 0x400000
    return sum(bin(a & b).count("1") for a, b in zip(rf, cf)) * 100 // 361


def snapshot(s, path):
    s.game.call("snapshot", action="save", path=str(path.resolve()))


# ---- the parity story ----
def target_area(s, scene):
    """The parity story's area: TARGET_ROW rows below `scene`'s (Down wraps)."""
    areas = [i for i in range(1, 32) if s.area_flags()[0] & (1 << i)]
    return areas[(areas.index(scene) + TARGET_ROW) % len(areas)]


def statics(m):
    return {k: m[k] for k in ("scene", "room", "yaw", "pitch", "distance", "geom")}


def rooms_and_turn(s):
    """R1 x2 and L2 + Right from a map frame's start (every path the same)."""
    g = s.game
    for _ in range(2):
        g.press("r1", vsyncs=3, settle=10)
    s.align()
    g.hold(["l2", "right"], 4)
    g.run(10)


def story_original(a, check, dumps, views):
    g = a.game
    g.press("triangle", vsyncs=3, settle=40)
    for _ in range(3):
        g.press("down", vsyncs=3, settle=8)
    g.press("circle", vsyncs=3, settle=150)
    check(a.current_menu() == MAP_MENU, f"off: Triangle, Down x3, Circle opens the game's map ({a.current_menu():#x})")
    check(a.map() is None, "off: no frame (map null)")
    m5 = a.wait_menu5(lambda m: m["menu_state"] == 1)
    check(m5["menu_state"] == 1, f"off: MENU5 takes input (processInput, {m5['menu_state']})")
    import numpy as np  # noqa: F401
    from widescreen_smoke import first_bright_column
    image = a.image("off-open")
    help_x = first_bright_column(image, slice(500, 560))
    check(help_x < 120, f"off: MENU5's controls help at the left edge (first bright column {help_x})")
    drawn = a.controls()
    g.run(20)
    check(drawn is not None and a.controls() > drawn, f"off: MENU5 draws its controls help ({drawn} -> "
                                                      f"{a.controls()})")
    dumps.append(("open", a.dump()))
    target = target_area(a, m5["scene"])
    g.press("circle", vsyncs=3, settle=40)  # MENU5's area list; its rows slide in
    for _ in range(TARGET_ROW):
        g.press("down", vsyncs=3, settle=8)
    g.press("circle", vsyncs=3, settle=200)
    m5 = a.wait_menu5(lambda m: m["scene"] == target and m["menu_state"] == 1)
    check(m5["scene"] == target and m5["menu_state"] == 1,
          f"off: the area list switched to {target} ({m5['scene']}, state {m5['menu_state']})")
    dumps.append(("switch", a.dump()))
    views.append(("switch", statics(m5)))
    rooms_and_turn(a)
    views.append(("turn", statics(a.menu5())))
    g.press("cross", vsyncs=3, settle=120)
    check(a.current_menu() == 0, f"off: Cross back to the game's menu ({a.current_menu():#x})")
    g.press("triangle", vsyncs=3, settle=120)
    check(a.current_menu() == CLOSED, f"off: Triangle closes it ({a.current_menu():#x})")
    dumps.append(("close", a.dump()))


def story_pad(b, check, dumps, views):
    g = b.game
    g.call("ui_menu", modern=True)
    m = b.open_shell_map()
    check(m and m["state"] == "ready" and m["framed"] == 1, f"on: Leá Monde > Map: the frame is ready ({m and m['state']})")
    dumps.append(("open", b.dump()))
    target = target_area(b, m["scene"])
    g.press("circle", vsyncs=3, settle=40)  # the list's rows come in (the pad waits, as MAINMENU's)
    for _ in range(TARGET_ROW):
        g.press("down", vsyncs=3, settle=8)
    g.press("circle", vsyncs=3, settle=4)
    m = b.wait_map(lambda m: m and m["scene"] == target and m["state"] == "ready")
    check(m["scene"] == target, f"on: the area list switched to {target} ({m['scene']})")
    g.run(200 - 4 - 2 * 4)  # the original's timing is not needed: settle as long
    dumps.append(("switch", b.dump()))
    m = b.map()
    views.append(("switch", statics(b.menu5())))
    check(statics(m) == views[-1][1], f"on: the published model is MENU5's statics ({statics(m)} vs {views[-1][1]})")
    room0, yaw0 = m["room"], m["yaw"]
    rooms_and_turn(b)
    m = b.map()
    views.append(("turn", statics(b.menu5())))
    turned = m["yaw"] - yaw0
    rooms = (room0, m["room"])
    g.press("cross", vsyncs=3, settle=4)
    s = b.wait_shell()
    check(s["visible"] and s["tab"] == "lea_monde" and s.get("row") == "map",
          f"on: Cross: the shell on Leá Monde / Map ({s['tab']}, {s.get('row')})")
    g.press("triangle", vsyncs=3, settle=4)
    check(b.wait_closed(), f"on: Triangle closes the menu ({b.current_menu():#x})")
    g.run(100)
    dumps.append(("close", b.dump()))
    return turned, rooms


def story_pointer(b2, check, dumps, views, turned, rooms):
    g = b2.game
    g.call("ui_menu", modern=True)
    m = b2.open_shell_map()
    dumps.append(("open", b2.dump()))
    ids = [a["id"] for a in m["areas"]]
    target = ids[(ids.index(m["scene"]) + TARGET_ROW) % len(ids)]  # as Down x4 from the current row
    ok = b2.click("area", target)
    m = b2.wait_map(lambda m: m and m["scene"] == target and m["state"] == "ready")
    check(ok and m["scene"] == target, f"pointer: an area row clicked switches to it ({m['scene']}, want {target})")
    g.run(200 - 2)
    dumps.append(("switch", b2.dump()))
    views.append(("switch", statics(b2.menu5())))
    # the rooms R1 x2 reached, by their rows (the ring's next twice)
    ring = m["ring"]
    for want in [ring[(ring.index(rooms[0]) + 1) % len(ring)], ring[(ring.index(rooms[0]) + 2) % len(ring)]]:
        b2.click("room", want, run=6)
    check(b2.map()["room"] == rooms[1], f"pointer: room rows clicked reach R1 x2's room ({b2.map()['room']}, "
                                        f"want {rooms[1]})")
    yaw = b2.map()["yaw"]
    b2.drag(turned / 8, 0)
    g.run(6)
    m = b2.map()
    check(m["yaw"] - yaw == turned, f"pointer: the drag turns as L2 + Right did ({m['yaw'] - yaw}, want {turned})")
    views.append(("turn", statics(b2.menu5())))
    b2.ptr(4, x=10, y=400, right=True)
    s = b2.wait_shell()
    check(s["visible"] and s["tab"] == "lea_monde" and s.get("row") == "map",
          f"pointer: a right click: the shell on Leá Monde / Map ({s['tab']}, {s.get('row')})")
    g.press("triangle", vsyncs=3, settle=4)
    check(b2.wait_closed(), "pointer: Triangle closes the menu")
    g.run(100)
    dumps.append(("close", b2.dump()))


# ---- the frame checks (process B continues) ----
def frame_checks(b, check, ground):
    g = b.game
    import numpy as np  # noqa: F401
    flags_before = b.room_flags()
    heap_before = b.shell().get("map_heap_free")
    check(heap_before is not None, f"closed: map_heap_free published ({heap_before})")
    vram_before = (b.vram_rows(256, 496, 640, 800), b.vram_rows(227, 228, 768, 1024))
    hud_before = g.read(b.hud, 1)[0]
    s = b.shell()
    check(s.get("map") is None, "closed: map null")

    # open: the model
    m = b.open_shell_map()
    areas = [i for i in range(1, 32) if b.area_flags()[0] & (1 << i)]
    check([a["id"] for a in m["areas"]] == areas, f"open: the areas are the areaFlags bits ({len(m['areas'])})")
    current = [a["id"] for a in m["areas"] if a["current"]]
    check(current == [m["scene"]] and m["current_scene"] == m["scene"],
          f"open: the current area is the scene ({current}, scene {m['scene']})")
    want = completion(b.room_flags(), g.read(b.cf, 64))
    check(m["completion"] == want, f"open: completion {m['completion']}% = the game's formula ({want}%)")
    check(b.room_flags() == flags_before, "open: roomFlags unchanged by the frame's completion")
    image = b.image("open")
    c = m["column"]
    cx, cy = int(c[0] + c[2] / 2), int(c[1] + c[3] / 2)
    sx, sy = image.shape[1] / (1280 if c[0] > 400 else 960), image.shape[0] / 720
    centre = image[int(cy * sy), int(cx * sx)]
    rail = image[int(400 * sy), int(10 * sx)]
    check(not ground_like(centre, ground), f"open: the cut-out shows the picture ({tuple(centre)})")
    check(ground_like(rail, ground), f"open: the rail is the ground colour ({tuple(rail)} vs {ground})")
    drawn = b.controls()
    g.run(20)
    check(drawn is not None and b.controls() == drawn,
          f"open: MENU5's own controls help is not drawn while framed ({drawn} -> {b.controls()})")

    # pad: rooms, rotate, zoom, pan, reset, hide
    room = m["room"]
    g.press("r1", vsyncs=3, settle=6)
    m = b.map()
    check(m["room"] != room and b.shell()["tab"] == "lea_monde", f"pad: R1 steps the room ({room} -> {m['room']}), "
                                                                 "the tab stays")
    g.press("l1", vsyncs=3, settle=6)
    check(b.map()["room"] == room, "pad: L1 steps back")
    yaw, pitch = m["yaw"], m["pitch"]
    # (the map runs a frame every second vsync: holds are in vsyncs)
    g.hold(["l2", "right"], 4)
    g.run(4)
    m = b.map()
    turn = m["yaw"] - yaw
    check(turn > 0 and turn % 64 == 0, f"pad: L2 + Right turns 64 a frame ({turn})")
    g.hold(["l2", "up"], 40)
    g.run(4)
    top = pitch + 64 * -(-(1024 - pitch) // 64)  # the D-pad adds 64 while below 1024 (4D8.c 674-678)
    m = b.map()
    g.hold(["l2", "up"], 20)
    g.run(4)
    check(m["pitch"] == top and b.map()["pitch"] == top,
          f"pad: L2 + Up raises the pitch to its clamp ({m['pitch']}, from {pitch}, want {top})")
    g.hold(["r2", "up"], 80)
    g.run(4)
    check(b.map()["distance"] == 512, f"pad: R2 + Up zooms in to 512 ({b.map()['distance']})")
    g.hold(["r2", "down"], 80)
    g.run(4)
    check(b.map()["distance"] == 1024, f"pad: R2 + Down zooms out to 1024 ({b.map()['distance']})")
    g.hold(["right"], 80)
    g.hold(["down"], 80)
    g.run(4)
    check(b.map()["geom"] == [96, 80], f"pad: the D-pad pans to its clamps ({b.map()['geom']})")
    g.hold(["start"], 2)
    g.run(6)
    m = b.map()
    check(m["geom"] == [0, 0] and m["distance"] == 768 and m["pitch"] == 568 and m["yaw"] == m["yaw0"],
          f"pad: Start resets the view ({m['geom']}, {m['distance']}, {m['pitch']}, {m['yaw']} / {m['yaw0']})")
    g.press("select", vsyncs=3, settle=30)
    m = b.map()
    image = b.image("hidden")
    rail = image[int(400 * sy), int(10 * sx)]
    check(m["hide_ui"] == 1 and not ground_like(rail, ground),
          f"pad: Select hides the chrome: the backdrop opens ({m['hide_ui']}, rail {tuple(rail)})")
    ok = b.click("prompt", 6)  # the pill: Select Show (PC_UI_MAP_P_HIDE)
    g.run(30)
    check(ok and b.map()["hide_ui"] == 0, f"pointer: the pill shows the chrome again ({b.map()['hide_ui']})")

    # the list by pad
    m = b.map()
    cur_row = [a["id"] for a in m["areas"]].index(m["scene"])
    scene, seq = m["scene"], m["req_seq"]
    g.press("circle", vsyncs=3, settle=4)
    m = b.map()
    check(m["focus"] == "list" and m["cursor"][0] + m["cursor"][1] == cur_row,
          f"list: Circle focuses it on the current row ({m['focus']}, {m['cursor']}, row {cur_row})")
    g.press("circle", vsyncs=3, settle=4)  # a double tap: the rows still come in (MAINMENU case 16)
    m = b.map()
    check(m["focus"] == "list" and m["state"] == "list" and m["scene"] == scene,
          f"list: a second Circle while the rows come in is ignored ({m['focus']}, {m['state']})")
    g.run(40)
    g.press("cross", vsyncs=3, settle=6)
    check(b.map()["focus"] == "map" and b.map()["state"] == "ready", "list: Cross back to the map")
    # the Choose prompt: its Up half up, its Down half down
    g.press("circle", vsyncs=3, settle=40)
    c0 = b.map()["cursor"]
    ok_down = b.click("prompt", P_LIST_DOWN, run=4)
    c1 = b.map()["cursor"]
    ok_up = b.click("prompt", P_LIST_UP, run=4)
    c2 = b.map()["cursor"]
    check(ok_down and ok_up and sum(c1) == (sum(c0) + 1) % len(m["areas"]) and sum(c2) == sum(c0),
          f"list: the Choose prompt's halves: Down then Up ({c0} -> {c1} -> {c2})")
    g.press("cross", vsyncs=3, settle=6)
    check(b.map()["req_seq"] > seq, "list: the prompts went through the mailbox")
    # every row: Down through the list (it wraps), the window as MAINMENU's
    g.press("circle", vsyncs=3, settle=40)
    n = len(m["areas"])
    seen, windows = set(), True
    for _ in range(n):
        g.press("down", vsyncs=3, settle=4)
        c = b.map()["cursor"]
        seen.add(c[0] + c[1])
        windows = windows and 0 <= c[0] < 9 and 0 <= c[1] <= max(0, n - 9)
    check(len(seen) == n and windows, f"list: Down walks every row in a 9-row window ({sorted(seen)}, {windows})")
    g.press("cross", vsyncs=3, settle=6)
    cm = g.read(b.sym["D_800F4EE8"], 2)
    c = b.map()["cursor"]
    check(list(cm) == c, f"list: Cross leaves the cursor memories as the window ({list(cm)} vs {c})")
    g.press("circle", vsyncs=3, settle=40)
    g.press("r1", vsyncs=3, settle=4)
    s = b.wait_shell()
    check(s["visible"] and s["tab"] == "system", f"list: R1 reopens the shell on the next tab ({s['tab']})")
    m = b.reopen_from_shell()
    g.press("circle", vsyncs=3, settle=40)
    g.press("triangle", vsyncs=3, settle=4)
    check(b.wait_closed(), "list: Triangle closes the menu")
    m = b.open_shell_map()

    # pointer
    yaw, pitch, dist = m["yaw"], m["pitch"], m["distance"]
    b.drag(10, -5)
    m = b.map()
    check(m["yaw"] - yaw == 80 and m["pitch"] - pitch == -40,
          f"pointer: a drag turns 8 a layout unit ({m['yaw'] - yaw}, {m['pitch'] - pitch})")
    c = m["column"]
    b.ptr(4, x=c[0] + 100, y=c[1] + 100, wheel=1)
    check(b.map()["distance"] == dist - 64, f"pointer: a wheel notch up zooms in 64 ({b.map()['distance']})")
    for _ in range(12):
        b.ptr(1, x=c[0] + 100, y=c[1] + 100, wheel=1)
    g.run(4)
    check(b.map()["distance"] == 512, f"pointer: the wheel stops at 512 ({b.map()['distance']})")
    ok = b.click("prompt", P_ZOOM, run=4)
    check(ok and b.map()["distance"] == 576, f"pointer: the Zoom prompt at the near clamp zooms out "
                                             f"({b.map()['distance']})")
    geom = b.map()["geom"]
    b.drag(36, 12, middle=True)
    m = b.map()
    check([m["geom"][0] - geom[0], m["geom"][1] - geom[1]] == [12, 4],
          f"pointer: a middle drag pans a game pixel per 3 units ({geom} -> {m['geom']})")
    geom = m["geom"]
    b.drag(-10, 5, middle=True)  # 3.3 / 1.7 game pixels: one step of 4 left, the rest kept
    m = b.map()
    check([m["geom"][0] - geom[0], m["geom"][1] - geom[1]] == [0, 0] and all(v % 4 == 0 for v in m["geom"]),
          f"pointer: the pan goes in MENU5's steps of 4 ({geom} -> {m['geom']})")
    geom = m["geom"]
    b.drag(-26, -11, shift=True)  # with the -3.3 / 1.7 kept: -12 / -2 game pixels, -12 / 0 taken
    m = b.map()
    check([m["geom"][0] - geom[0], m["geom"][1] - geom[1]] == [-12, 0],
          f"pointer: a Shift + drag pans ({geom} -> {m['geom']})")
    # the pad wins over a drag in the same frame
    yaw = m["yaw"]
    g.call("input", buttons=["l2", "right"], vsyncs=8)
    b.drag(10, 0)
    g.run(6)
    m = b.map()
    turn = m["yaw"] - yaw
    check(turn > 0 and turn % 64 == 0, f"pointer: the pad wins over a drag in the same frame ({turn})")
    # a room row (visited), an unvisited one ignored (none here: all seen; the
    # bridge only draws visited rows), the header's arrows
    ring = m["ring"]
    want = ring[(ring.index(m["room"]) + 2) % len(ring)]  # in the rail's window of rooms
    ok = b.click("room", want, run=6)
    check(ok and b.map()["room"] == want, f"pointer: a room row selects it ({b.map()['room']}, want {want})")
    room = b.map()["room"]
    ok = b.click("step", 1, run=6)
    check(ok and b.map()["room"] == ring[(ring.index(room) + 1) % len(ring)], "pointer: the header's > steps")
    # Reset prompt
    ok = b.click("prompt", 4, run=6)  # PC_UI_MAP_P_RESET
    m = b.map()
    check(ok and m["geom"] == [0, 0] and m["distance"] == 768 and m["yaw"] == m["yaw0"],
          f"pointer: the Reset prompt resets the view ({m['geom']}, {m['distance']})")
    # the shown area's row: nothing to switch to
    seq = m["req_seq"]
    ok = b.click("area", m["scene"], run=6)
    m2 = b.map()
    check(ok and m2["state"] == "ready" and m2["scene"] == m["scene"] and m2["req_seq"] == seq,
          f"pointer: the shown area's row does nothing ({m2['state']}, seq {seq} -> {m2['req_seq']})")
    # an area click, then a click during the switch: dropped (busy)
    other = next(a["id"] for a in m["areas"] if a["id"] != m["scene"])
    b.click("area", other, run=1)
    m = b.wait_map(lambda m: m and m["state"] == "switching", 100, 1)
    busy, seq, applied = m["busy"], m["req_seq"], m["applied_seq"]
    ok = b.click("step", 1, run=2)  # the header's > (drawn while switching)
    m = b.map()
    check(ok and m["state"] == "switching" and m["busy"] == busy + 1 and m["req_seq"] == seq
          and m["applied_seq"] == applied,
          f"pointer: a click while the area switches is dropped ({m['state']}, busy {busy} -> {m['busy']})")
    m = b.wait_map(lambda m: m and m["scene"] == other and m["state"] == "ready")
    check(m["scene"] == other, f"pointer: the area click switched ({m['scene']}, want {other})")
    # a tab click: the shell on that tab
    ok = b.click("tab", 1, run=4)
    s = b.wait_shell()
    check(ok and s["visible"] and s["tab"] == "equipment", f"pointer: a tab click reopens the shell there ({s['tab']})")
    b.reopen_from_shell()

    # paling
    pal = next(a["id"] for a in b.map()["areas"] if a["id"] not in (b.map()["scene"],))
    old_pal = g.read(b.flags + MAP_PALING + pal, 1)
    g.write(b.flags + MAP_PALING + pal, b"\x01")
    b.click("area", pal, run=3)
    m = b.wait_map(lambda m: m and m["scene"] == pal and m["state"] == "ready")
    jam = [a for a in m["areas"] if a["id"] == pal]
    check(m["paling"] and jam and jam[0]["paling"] and m["paling_text"],
          f"paling: the area is JAMMED with the paling's line ({m['paling']}, {m['paling_text']!r})")
    room, yaw = m["room"], m["yaw"]
    g.press("r1", vsyncs=3, settle=6)
    g.hold(["l2", "right"], 3)
    g.run(6)
    busy = b.map()["busy"]
    b.drag(10, 0)
    check(b.map()["busy"] > busy, f"paling: the frame drops a drag ({busy} -> {b.map()['busy']})")
    applied = b.map()["applied_seq"]
    ok = b.click("prompt", P_ROOM_NEXT, run=6)  # drawn unlit; MENU5 takes it and its paled guard refuses it
    m = b.map()
    check(ok and m["applied_seq"] > applied, f"paling: MENU5 takes the Room prompt's request ({applied} -> "
                                             f"{m['applied_seq']})")
    check(m["room"] == room and m["yaw"] == yaw, f"paling: room and camera input ignored ({m['room']}, {m['yaw']})")
    b.shot("paling")
    out_id = next(a["id"] for a in m["areas"] if a["id"] != pal)
    b.click("area", out_id, run=3)
    m = b.wait_map(lambda m: m and m["scene"] == out_id and m["state"] == "ready")
    check(m["scene"] == out_id and not m["paling"], f"paling: switching out works ({m['scene']})")
    g.write(b.flags + MAP_PALING + pal, old_pal)

    # close: Cross (shell on the Map row), Triangle (closed, everything back)
    g.press("cross", vsyncs=3, settle=4)
    s = b.wait_shell()
    check(s["tab"] == "lea_monde" and s.get("row") == "map" and s.get("map") is None,
          f"close: Cross gives the shell on the Map row ({s['tab']}, {s.get('row')})")
    b.reopen_from_shell()
    g.press("triangle", vsyncs=3, settle=4)
    check(b.wait_closed(), "close: Triangle closes the menu")
    g.run(60)
    s = b.shell()
    check(g.read(b.hud, 1)[0] == hud_before, f"close: the HUD flag is back ({g.read(b.hud, 1)[0]} vs {hud_before})")
    check(b.scene_buffer() == 0, "close: vs_battle_sceneBuffer is NULL")
    check(s.get("map_heap_free") is not None and s.get("map_heap_free") == heap_before,
          f"close: the heap's largest free block as before ({s.get('map_heap_free')} vs {heap_before})")
    check((b.vram_rows(256, 496, 640, 800), b.vram_rows(227, 228, 768, 1024)) == vram_before,
          "close: the VRAM rects MENU5 saved are restored")


def robustness(b, check):
    g = b.game
    # soft resets: at MENU5's state 4 and during an area switch
    for when in ("exec4", "switch"):
        titles = pathlib.Path(g.log).read_text().count("TITLE MENU READY")
        if when == "exec4":
            g.press("triangle", vsyncs=3, settle=40)
            if b.shell()["visible"]:
                for _ in range(8):
                    if b.shell()["tab"] == "lea_monde":
                        break
                    g.press("r1", vsyncs=3, settle=10)
                g.press("circle", vsyncs=3, settle=1)
                m = b.wait_map(lambda m: m and m["exec_state"] >= 4, 200, 1)
                at = m and m["exec_state"]
            else:
                at = None
        else:
            m = b.wait_ready() if b.map() else None
            if not m:
                at = None
            else:
                other = next(a["id"] for a in m["areas"] if a["id"] != m["scene"])
                b.click("area", other, run=2)
                m = b.wait_map(lambda m: m and m["menu_state"] == 4, 100, 1)
                at = m and m["menu_state"]
        check(at is not None and at >= 4, f"robust: the reset ({when}) lands while MENU5 runs ({at})")
        g.call("input", buttons=["l1", "l2", "r1", "r2", "start", "select"], vsyncs=6)
        for _ in range(100):
            g.run(30)
            if pathlib.Path(g.log).read_text().count("TITLE MENU READY") > titles:
                break
        else:
            check(False, f"robust: reset ({when}): the title came back")
            return
        s = b.shell()
        check(not s["visible"] and s.get("map") is None,
              f"robust: reset at {when} ({at}): the title without chrome, map null")
        b.shot(f"reset-{when}")
        g.press("start", vsyncs=4, settle=90)
        for _ in range(3):
            g.press("circle", vsyncs=3, settle=90)
        g.run(400)
        st = g.state()
        b.fixture()
        g.call("ui_menu", modern=True)
        m = b.open_shell_map()
        check((st["zone"], st["room"]) == (9, 17) and m and m["state"] == "ready",
              f"robust: Continue after the reset ({when}): the field and the map work")
        if when == "exec4":
            continue
    # a forced currentMenu write ends the frame (the heartbeat; MENU5 is left
    # mid-run, so this is the last case)
    if not b.map():
        b.open_shell_map()
    g.write(b.menu_state, bytes([CLOSED]))
    g.run(4)
    s = b.shell()
    check(s.get("map") is None, f"robust: a forced currentMenu write ends the frame ({s.get('map') and 'map'})")
    # written back, MENU5 runs on with its latch but no frame (as a save
    # state restored with the map open): its own controls help and list
    g.write(b.menu_state, bytes([MAP_MENU]))
    g.run(10)
    drawn = b.controls()
    g.run(20)
    m5 = b.menu5()
    check(b.map() is None and m5["menu_state"] == 1 and b.controls() > drawn,
          f"robust: MENU5 without its frame draws its own help ({drawn} -> {b.controls()}, state {m5['menu_state']})")
    g.press("circle", vsyncs=3, settle=40)
    m5 = b.menu5()
    check(m5["menu_state"] == 2 and b.map() is None, f"robust: and Circle opens its own list ({m5['menu_state']})")
    g.press("cross", vsyncs=3, settle=10)
    check(b.menu5()["menu_state"] == 1, f"robust: its list's Cross back ({b.menu5()['menu_state']})")


def wide_checks(b, check, ground):
    g = b.game
    if b.map():
        g.press("triangle", vsyncs=3, settle=4)
        b.wait_closed()
    g.call("graphics", widescreen=True)
    g.run(30)
    m = b.open_shell_map()
    c = m and m["column"]
    check(c and c[0] == 448 and c[0] + c[2] == 1096, f"16:9: the cut-out is the 4:3 picture's map rect ({c})")
    image = b.image("wide-open")
    sx, sy = image.shape[1] / 1280, image.shape[0] / 720
    centre = image[int((c[1] + c[3] / 2) * sy), int((c[0] + c[2] / 2) * sx)]
    check(not ground_like(centre, ground), f"16:9: the cut-out shows the picture ({tuple(centre)})")
    hits = m.get("hits", [])
    inside = all(0 <= h["r"][0] and h["r"][2] <= 1280 for h in hits)
    check(hits and inside, f"16:9: the chrome's targets inside 1280 ({len(hits)})")
    g.press("circle", vsyncs=3, settle=40)
    g.press("up", vsyncs=3, settle=6)  # from the current row (0) up: the last row, the window scrolled
    m = b.map()
    rows = sorted(h["i"] for h in m.get("hits", []) if h["k"] == "area")
    check(m["cursor"][1] > 0 and rows == sorted(a["id"] for a in m["areas"]),
          f"16:9: the list shows every row the rail fits ({m['cursor']}, {len(rows)} of {len(m['areas'])})")
    g.press("cross", vsyncs=3, settle=6)
    m = b.wait_ready()
    other = next(a["id"] for a in m["areas"] if a["id"] != m["scene"])
    b.click("area", other, run=3)
    m = b.wait_map(lambda m: m and m["scene"] == other and m["state"] == "ready")
    check(m["scene"] == other, "16:9: an area switch")
    b.shot("wide-switched")
    g.press("triangle", vsyncs=3, settle=4)
    check(b.wait_closed(), "16:9: Triangle closes")
    g.call("graphics", widescreen=False)
    g.run(10)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    parser.add_argument("--parity-only", action="store_true", help="only the parity story")
    parser.add_argument("--no-parity", action="store_true", help="only the frame checks")
    parser.add_argument("--diff-all", action="store_true", help="snapshot A's and B's ends and list every difference")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_map"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_DETERMINISTIC": "1"}
    os.environ.pop("ROOD_SETTINGS", None)
    shots = bool(args.out)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    dumps, views = {}, {}
    if not args.no_parity:
        shutil.copyfile(args.card, card)
        a = Session("A", out, card, env, shots)
        try:
            a.fixture()
            dumps["A"] = []
            views["A"] = []
            story_original(a, check, dumps["A"], views["A"])
            if args.diff_all:
                snapshot(a, out / "A.snap")
        finally:
            a.close()

    shutil.copyfile(args.card, card)
    b = Session("B", out, card, env, shots)
    try:
        b.fixture()
        g = b.game
        ground = None
        if not args.no_parity:
            dumps["B"] = []
            views["B"] = []
            turned, rooms = story_pad(b, check, dumps["B"], views["B"])
            if args.diff_all:
                snapshot(b, out / "B.snap")
            for (name, da), (_, db) in zip(dumps["A"], dumps["B"]):
                rest = diff(da, db)
                check(not rest, f"parity A/B {name}: equal" + (f" (differs: {describe(da, db, rest)})" if rest else ""))
            for (name, va), (_, vb) in zip(views["A"], views["B"]):
                check(va == vb, f"parity A/B {name}: MENU5's statics ({va} vs {vb})")
        if not args.parity_only:
            g.call("ui_menu", modern=True)
            # the theme's ground colour as drawn: the margin left of an open frame's rail
            b.open_shell_map()
            image = b.image("ground")
            ground = tuple(int(v) for v in image[int(400 * image.shape[0] / 720), int(10 * image.shape[1] / 960)])
            g.press("triangle", vsyncs=3, settle=4)
            b.wait_closed()
            frame_checks(b, check, ground)
            wide_checks(b, check, ground)
            robustness(b, check)
    finally:
        b.close()

    if not args.no_parity and args.diff_all:
        subprocess.run([sys.executable, str(ROOT / "pc/tests/snapshot_diff.py"), str(out / "A.snap"),
                        str(out / "B.snap"), "--binary", str(BUILD / "rood_battle_probe")])

    if not args.no_parity and not args.parity_only or (args.parity_only and not args.no_parity):
        shutil.copyfile(args.card, card)
        b2 = Session("B2", out, card, env, shots)
        try:
            b2.fixture()
            dumps["B2"] = []
            views["B2"] = []
            story_pointer(b2, check, dumps["B2"], views["B2"], turned, rooms)
            for (name, da), (_, db) in zip(dumps["A"], dumps["B2"]):
                rest = diff(da, db)
                check(not rest, f"parity A/B2 {name}: equal" + (f" (differs: {describe(da, db, rest)})" if rest else ""))
            for (name, va), (_, vb) in zip(views["A"], views["B2"]):
                check(va == vb, f"parity A/B2 {name}: MENU5's statics ({va} vs {vb})")
        finally:
            b2.close()

    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("ui_map_smoke: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
