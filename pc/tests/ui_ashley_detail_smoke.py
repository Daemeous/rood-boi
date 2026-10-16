#!/usr/bin/env python3
"""Modern menus, Ashley tab: the model drawn at the window's resolution has
no slivers and no gaps (pc/src/render.c and pc/shaders/vulkan/native.geom:
object culling; pc/src/ui/ashley_bridge.c no_rim_copy; menu_studio.glsl).

    python3 pc/tests/ui_ashley_detail_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

From a copy of the card (skipped without one), in the save room, ui_menus on,
anti-aliasing off, 4:3 in a 1440x1080 window (the detail view: 4x), the
model turned by pointer drags to YAWS views. Each view is drawn paused twice
from one recorded frame (RPC redraw): its colours and its depth view
(graphics show_depth: 2D pixels magenta, 3D grey).

  - the model view's own light: graphics menu_studio 1 and the clone's rim
    copy left out (D_800F4538[1]->unk17FD 0; MAINMENU func_800F9CB0 sets 1:
    a second copy 2 units off and 64 ordering-table entries behind, in its
    own colour, drawn after the clone).
  - the light is data (theme.json "menu_model_light", ui/menu_light.h; here
    a copy of the theme file): RPC graphics menu_light reads the shipped
    rig; the key's intensity doubled brightens the model's left half (the
    key comes from the upper left) by at least LIT_MIN on average, and set
    back the picture is the same as before to within 1 per channel.
  - no gaps: the clone alone (unk17FD forced 0, so nothing fills what the
    geometry leaves open), at most GAP_MAX pixels over all the views of 2D enclosed by
    the model's 3D pixels in thin holes (a component that 3 erosions take
    away: under about one PS1 pixel across; wider ones are gaps between
    limbs). The PS1 culls a polygon whose whole-pixel NCLIP is 0 (it covers
    no pixel there); drawn at 4x and up its neighbours leave a sliver open
    (the nose, the eyelids, the jaw line, the weapon's edges).
  - no rim copy drawn: as the menu shows it, the frame's object instances
    are those of the clone alone (render_stats object_instances; before:
    one more, the copy, which showed through those slivers white and orange
    and peeked out along edges).
--out keeps the pictures (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4
WINDOW = (1440, 1080)
YAWS = 8                 # views, a pointer drag of 64 layout units (512 angle units, 45 degrees) apart
THIN = 3                 # a thin hole: an enclosed 2D component that this many erosions take away
GAP_MAX = 150            # thin-hole pixels allowed over all the views (measured after the fix: 93, mostly an
                         # antenna's loop and a slit where the arm meets the body, in every build; before it:
                         # 298, the face and the weapon's edges besides)
MAGENTA = (153, 0, 153)  # gl_present.c / scaled.frag show_depth: 2D
LIT_MIN = 8              # the key doubled: the model's left half at least this much brighter (mean of r, g, b)
SHIPPED = {"key_intensity": 0.5, "key_direction": [-0.45, -0.5, -0.74], "fill_intensity": 0.15,
           "rim_intensity": 0.45, "rim_power": 2, "exposure": 1, "saturation": 1}  # theme.json's block


def pictures(g, path):
    """The paused frame's colours and depth view (redrawn), as int arrays."""
    import numpy as np
    from PIL import Image

    def dump(name):
        ppm = pathlib.Path(f"{path}-{name}.ppm")
        ppm.unlink(missing_ok=True)
        g.call("redraw")
        g.wait_presents(3)
        g.call("present_dump", path=str(ppm.resolve()))
        for _ in range(300):
            if ppm.exists() and ppm.stat().st_size > 0:
                break
            g.wait_presents(1)
        image = np.asarray(Image.open(ppm).convert("RGB")).astype(np.int16)
        ppm.unlink()
        return image

    colour = dump("colour")
    g.call("graphics", show_depth=True)
    depth = dump("depth")
    g.call("graphics", show_depth=False)
    return colour, depth


def model_masks(depth):
    """3D (the model) and 2D (magenta) pixels of a depth view."""
    import numpy as np
    d = depth
    flat = np.abs(d[..., 0] - MAGENTA[0]) + np.abs(d[..., 1] - MAGENTA[1]) + np.abs(d[..., 2] - MAGENTA[2])
    two_d = flat < 30
    grey = (np.abs(d[..., 0] - d[..., 1]) < 6) & (np.abs(d[..., 1] - d[..., 2]) < 6) & (d[..., 0] > 8)
    return grey, two_d


def components(mask):
    """Labelled 4-connected components of a boolean array (no scipy): labels, sizes, touches the border."""
    import numpy as np
    h, w = mask.shape
    labels = np.zeros((h, w), np.int32)
    sizes, border = [0], [False]
    ys, xs = np.nonzero(mask)
    for y0, x0 in zip(ys, xs):
        if labels[y0, x0]:
            continue
        n = len(sizes)
        labels[y0, x0] = n
        stack, size, edge = [(y0, x0)], 0, False
        while stack:
            y, x = stack.pop()
            size += 1
            if y == 0 or x == 0 or y == h - 1 or x == w - 1:
                edge = True
            for yy, xx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= yy < h and 0 <= xx < w and mask[yy, xx] and not labels[yy, xx]:
                    labels[yy, xx] = n
                    stack.append((yy, xx))
        sizes.append(size)
        border.append(edge)
    return labels, sizes, border


def erode(mask):
    e = mask.copy()
    e[1:, :] &= mask[:-1, :]
    e[:-1, :] &= mask[1:, :]
    e[:, 1:] &= mask[:, :-1]
    e[:, :-1] &= mask[:, 1:]
    e[0, :] = e[-1, :] = False
    e[:, 0] = e[:, -1] = False
    return e


def gaps(depth, box):
    """2D pixels enclosed by the model in thin holes (components THIN erosions take away)."""
    import numpy as np
    x0, y0, x1, y1 = box
    grey, two_d = model_masks(depth[y0:y1, x0:x1])
    if not grey.any():
        return -1, []
    ys, xs = np.nonzero(grey)
    inside = ~grey[ys.min():ys.max() + 1, xs.min():xs.max() + 1]  # not the model, inside its bounding box
    labels, sizes, border = components(inside)
    holes = []
    for i in range(1, len(sizes)):
        if border[i]:
            continue
        m = np.pad(labels == i, 1)
        for _ in range(THIN):
            m = erode(m)
        if not m.any():
            holes.append(sizes[i])
    return sum(holes), holes


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the pictures")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    import numpy as np
    from PIL import Image
    out = pathlib.Path(args.out) if args.out else OUT / "ui_ashley_detail"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    settings = out / "settings.cfg"
    settings.write_text("aa none\n")
    theme = out / "theme.json"  # a copy: a save from the rig never reaches the repository's
    shutil.copyfile(ROOT / "pc/assets/ui/theme.json", theme)
    env = {"ROOD_WINDOW": f"{WINDOW[0]}x{WINDOW[1]}", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0",
           "ROOD_SETTINGS": str(settings), "ROOD_UI_THEME": str(theme)}
    sym = symbols()
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        state = g.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1

        def ui():
            return g.call("ui_menu")

        def wait(pred, limit, what):
            for _ in range(limit):
                if pred():
                    return
                g.run(1)
            raise TimeoutError(f"{what}: {ui()}")

        def clone():
            return int.from_bytes(g.read(sym["D_800F4538"] + PTR, PTR), "little")

        g.call("ui_menu", modern=True)
        g.run(10)
        g.press("triangle", vsyncs=3, settle=2)
        wait(lambda: ui().get("screen") == "ashley" and (ui().get("ashley") or {}).get("model") == 2, 300,
             "the Ashley model")
        g.run(8)
        address = clone()
        rim = g.read(address + 0x17FD, 1)[0] if address else -1
        studio = g.call("graphics").get("menu_studio")
        check(studio == 1 and rim == 0,
              f"the model view's own light: graphics menu_studio {studio} (want 1), the clone's rim copy "
              f"unk17FD {rim} (want 0: left out)")
        scale = g.call("graphics").get("scale")
        k = min(WINDOW[0] / 960, WINDOW[1] / 720)
        rect = ui()["ashley"]["model_rect"]
        # (the column above the "Turn" hint under the model)
        box = (int(rect[0] * k), int(rect[1] * k), int((rect[0] + rect[2]) * k), int((rect[1] + rect[3] * 0.88) * k))
        cx, cy = rect[0] + rect[2] / 2, rect[1] + rect[3] / 2
        # the light as data: the key's intensity brightens the lit side; set back, the same picture
        rig = g.call("graphics", menu_light=True)["menu_light"]
        flat = lambda v: v if isinstance(v, list) else [v]  # noqa: E731
        shipped = all(len(flat(rig[k])) == len(flat(v)) and all(abs(a - b) < 1e-6 for a, b in zip(flat(rig[k]), flat(v)))
                      for k, v in SHIPPED.items())
        check(shipped and not rig["edited"], f"graphics menu_light: the shipped rig ({ {k: rig[k] for k in SHIPPED} })")
        g.call("pause", paused=True)
        before, before_depth = pictures(g, out / "light-before")
        g.call("graphics", menu_light={"key_intensity": rig["key_intensity"] * 2})
        brighter, _ = pictures(g, out / "light-key")
        g.call("graphics", menu_light={"key_intensity": rig["key_intensity"]})
        after, _ = pictures(g, out / "light-after")
        g.call("pause", paused=False)
        x0, y0, x1, y1 = box
        grey, _ = model_masks(before_depth[y0:y1, x0:x1])
        ys, xs = np.nonzero(grey)
        left = grey.copy()
        if xs.size:
            left[:, (xs.min() + xs.max()) // 2:] = False
        lum = lambda image: image[y0:y1, x0:x1][left].mean() if left.any() else 0.0  # noqa: E731
        gain = lum(brighter) - lum(before)
        check(left.sum() > 1000 and gain >= LIT_MIN,
              f"key_intensity {rig['key_intensity']} -> {rig['key_intensity'] * 2}: the model's left half "
              f"{gain:+.1f} brighter on average over {int(left.sum())} pixels (want >= {LIT_MIN})")
        diff = int(np.abs(after - before).max())
        check(diff <= 1, f"key_intensity set back: the picture as before (largest channel difference {diff}, want <= 1)")
        total_gap, extra = 0, []
        for view in range(YAWS):
            yaw = ui()["ashley"]["model_yaw"]
            # the model as the menu shows it: slivers
            g.call("pause", paused=True)
            shown = g.call("render_stats")["object_instances"]
            colour, depth = pictures(g, out / f"view{view}")
            # the clone alone (nothing behind it): gaps
            g.call("pause", paused=False)
            g.write(address + 0x17FD, bytes([0]))
            g.run(3)
            g.call("pause", paused=True)
            alone_count = g.call("render_stats")["object_instances"]
            alone, alone_depth = pictures(g, out / f"view{view}-alone")
            n_gap, holes = gaps(alone_depth, box)
            g.write(address + 0x17FD, bytes([rim]))
            g.call("pause", paused=False)
            total_gap += n_gap
            extra.append(shown - alone_count)
            print(f"      view {view} (yaw {yaw}, {scale}x): thin holes {n_gap} pixels {sorted(holes)[-6:]}, "
                  f"object instances {shown} as shown, {alone_count} the clone alone", flush=True)
            if args.out:
                x0, y0, x1, y1 = box
                Image.fromarray(colour[y0:y1, x0:x1].astype(np.uint8)).save(out / f"view{view}-colour.png")
                Image.fromarray(alone[y0:y1, x0:x1].astype(np.uint8)).save(out / f"view{view}-alone.png")
                Image.fromarray(alone_depth[y0:y1, x0:x1].astype(np.uint8)).save(out / f"view{view}-depth.png")
            # the next view: a pointer drag of 64 layout units to the right
            for px, py, down in ((cx, cy, True), (cx + 32, cy, True), (cx + 64, cy, True), (cx + 64, cy, False)):
                g.call("ui_menu", pointer={"x": px, "y": py, "down": down})
                g.run(2)
            g.call("ui_menu", pointer={"x": -1, "y": -1})
            g.run(4)
        check(total_gap <= GAP_MAX,
              f"no gaps: {total_gap} thin-hole pixels over {YAWS} views of the clone alone "
              f"(allowed {GAP_MAX}; {scale}x)")
        check(all(n == 0 for n in extra),
              f"no rim copy drawn: object instances as shown minus the clone alone, per view: {extra} (want 0)")
    finally:
        g.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
