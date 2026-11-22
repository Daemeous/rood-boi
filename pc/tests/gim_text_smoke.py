#!/usr/bin/env python3
"""The game's pictures of text drawn as real text from a pack (gitea #14): what is replaced, what is not.

    python3 pc/tests/gim_text_smoke.py [--out DIR]

The staff card of the evt-0001 scene (STAFF004.GIM, the player's own disc file; SKIP without it) is shown four ways at 1280 x 960
(4:3), the same frame each (the scene played by the cutscene player, 1620 vsyncs):
  A  ui_text original, a pack present         the picture as the game draws it
  B  ui_text original, no pack                = A: the picture is untouched (byte for byte)
  C  ui_text font, no pack                    = A (a pack is what turns the picture into text)
  D  ui_text font, a pack                     the words replaced: the frame differs from A in as many bands of rows as the pack has lines, and
       the ink the pack's boxes do not cover (the rule under the title, which the pack tool leaves out of them) is still there as A had it.
Then the same pack at 16:9 (1280 x 720, widescreen) with A and D only: the same bands, the rule, centred.
The same checks run with ui_text_overlay auto (the words at the window's resolution unless something is drawn over them) and off (into the picture at the internal
resolution): the words are items in the game's draw order either way, so its scanline wipes and fades apply to them. The quote (JO) is the fade check: New Game's
opening, Start again, and the quote fades in under the game's veil; the share of the words visible follows the original ink's.
The pack is made here from the picture's own ink (pc/tools/gim_text_pack.py, no OCR) with placeholder words: no game text is stored."""
import argparse
import os
import pathlib
import shutil
import subprocess
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

STEMS = __import__("re").findall(r'"([A-Z0-9_]+)"', (ROOT / "pc/src/gim_text.cpp").read_text().split("STEMS[] = {")[1].split("};")[0])  # (the game's GIM table, in order)
ACTIVE = {}  # work directory -> per frame, the GIM files the loader's layers show (set by frames())
STEP, SAMPLES = 30, 100  # the frames: every 30 vsyncs of the scene's replay, 100 of them


def make_pack(directory, names=("STAFF004", "JO", "VKP_2")):
    """A pack of the picture's lines with placeholder words (the tool's boxes and colours: its words are the player's, here none)."""
    directory.mkdir(parents=True, exist_ok=True)
    done = subprocess.run([sys.executable, str(ROOT / "pc/tools/gim_text_pack.py"), "--ocr", "off", "--out", str(directory), *names],
                          capture_output=True, text=True)
    count = 0
    for name in names:
        path = directory / f"{name}.txt"
        if done.returncode or not path.exists():
            return 0
        lines, n = [], 0
        for line in path.read_text().splitlines():
            if line.startswith("line ") and line.rstrip().endswith("|"):
                box = [int(v) for v in line.split("|")[0].split()[1:5]]
                chars = max(4, round((box[2] - box[0]) / 5.5))  # (about as many letters as the picture's line holds: the words' size follows their count)
                line = line.rstrip() + " ~" + ("Lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor" * 3)[:chars].strip()
                n += 1
            lines.append(line)
        path.write_text("\n".join(lines) + "\n")
        if name == names[0]:
            count = n
    return count


def frames(work, text, pack, wide=False, step=None, samples=None, skip=0, overlay="auto", scene="evt-0001"):
    """The scene's presented frames every STEP vsyncs from the start of its replay (the cutscene player: paused at the scene's first
    frame and run from there, exact: the same frames in every run), as .ppm files."""
    work.mkdir(parents=True, exist_ok=True)
    (work / "prefabs").mkdir(exist_ok=True)
    (work / "settings.cfg").write_text(f"ui_menus 0\nui_text {text}\ninterpolate 0\nwidescreen {1 if wide else 0}\naspect 0\nui_text_overlay {overlay}\n")
    env = {"ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_WINDOW": "1280x720" if wide else "1280x960", "ROOD_AUDIO": "none", "ROOD_OUT": str(work),
           "ROOD_LIGHTS": str(work / "lights"), "ROOD_PREFABS": str(work / "prefabs"), "ROOD_GIM_TEXT": str(pack), "ROOD_SCALE": "1"}
    if wide:
        env["ROOD_WIDESCREEN"] = "1"
    g = launch_at("title", fast=True, log=work / "game.log", extra_env=env)
    try:
        g.call("cutscene", action="start", scene=scene)
        last, still = g.status()["vsync"], time.monotonic()
        while True:
            time.sleep(0.1)
            status = g.call("cutscene")
            if status["on"] and not status["busy"]:
                break
            now = g.status()["vsync"]
            if now != last:
                last, still = now, time.monotonic()
            elif time.monotonic() - still > 30:
                raise RuntimeError("evt-0001 did not start")
        g.call("cutscene", action="pause")
        g.call("cutscene", action="seek", frame=g.call("cutscene")["first"])
        for _ in range(300):
            time.sleep(0.1)
            if not g.call("cutscene")["busy"]:
                break
        g.run(1)
        if skip:
            g.run(skip)
        for i in range(samples or SAMPLES):
            g.run(step or STEP)
            g.wait_presents(2)
            path = work / f"f{i:03d}.ppm"
            g.call("present_dump", path=str(path))
            for _ in range(50):
                if path.exists() and path.stat().st_size > 0:
                    break
                g.run(2)
                g.wait_presents(1)
            shown = set()
            for k in range(3):
                try:
                    gid, bright = g.value(f"vs_render_gimLayers[{k}].id"), g.value(f"vs_render_gimLayers[{k}].unk3")
                except Exception:
                    continue
                if isinstance(gid, int) and 0 <= gid < len(STEMS) and bright:
                    shown.add(STEMS[gid])
            ACTIVE.setdefault(str(work), []).append(shown)
        return sorted(work.glob("f[0-9][0-9][0-9].ppm"))
    finally:
        g.close()


def quote_frames(work, text, pack, overlay="auto", step=4, samples=64):
    """The opening's quote (JO.GIM) fading in: New Game from the title, Start again during the manor card (the game skips to the quote), then the frames every `step` vsyncs
    (the route build_compare's opening_quote plays: the same frames in every run)."""
    work.mkdir(parents=True, exist_ok=True)
    (work / "prefabs").mkdir(exist_ok=True)
    (work / "settings.cfg").write_text(f"ui_menus 0\nui_text {text}\ninterpolate 0\nwidescreen 0\naspect 0\nui_text_overlay {overlay}\n")
    env = {"ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_WINDOW": "1280x960", "ROOD_AUDIO": "none", "ROOD_OUT": str(work),
           "ROOD_LIGHTS": str(work / "lights"), "ROOD_PREFABS": str(work / "prefabs"), "ROOD_GIM_TEXT": str(pack), "ROOD_SCALE": "1"}
    g = launch_at("title", fast=True, log=work / "game.log", extra_env=env)
    try:
        g.run(60)
        g.press("start", vsyncs=4, settle=90)
        for _ in range(15):
            g.run(60)
        g.press("start", vsyncs=4, settle=0)
        for i in range(samples):
            g.run(step)
            g.wait_presents(2)
            path = work / f"f{i:03d}.ppm"
            g.call("present_dump", path=str(path))
            for _ in range(50):
                if path.exists() and path.stat().st_size > 0:
                    break
                g.run(2)
                g.wait_presents(1)
        return sorted(work.glob("f[0-9][0-9][0-9].ppm"))
    finally:
        g.close()


def forced_frame(work, text, pack, mode, bright=64):
    """The staff card with its layer's drawer forced to `mode` (1 framebuffer fade, 3 crossfade) at a partial brightness: no reachable screen draws a card with them."""
    work.mkdir(parents=True, exist_ok=True)
    (work / "prefabs").mkdir(exist_ok=True)
    (work / "settings.cfg").write_text(f"ui_menus 0\nui_text {text}\ninterpolate 0\nwidescreen 0\naspect 0\n")
    env = {"ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_WINDOW": "1280x960", "ROOD_AUDIO": "none", "ROOD_OUT": str(work),
           "ROOD_LIGHTS": str(work / "lights"), "ROOD_PREFABS": str(work / "prefabs"), "ROOD_GIM_TEXT": str(pack), "ROOD_SCALE": "1"}
    g = launch_at("title", fast=True, log=work / "game.log", extra_env=env)
    try:
        g.call("cutscene", action="start", scene="evt-0001")
        while True:
            time.sleep(0.1)
            status = g.call("cutscene")
            if status["on"] and not status["busy"]:
                break
        g.call("cutscene", action="pause")
        g.call("cutscene", action="seek", frame=g.call("cutscene")["first"])
        for _ in range(300):
            time.sleep(0.1)
            if not g.call("cutscene")["busy"]:
                break
        g.run(1)
        g.run(1470 + 4 * 30)  # (the card is up)
        path = work / "forced.ppm"
        for _ in range(6):
            g.store("vs_render_gimLayers[0].unk0_3", mode)
            g.store("vs_render_gimLayers[0].unk3", bright)
            g.run(1)
            g.wait_presents(2)
            g.call("present_dump", path=str(path))
        for _ in range(50):
            if path.exists() and path.stat().st_size > 0:
                break
            g.run(2)
            g.wait_presents(1)
        return load(path)
    finally:
        g.close()


def shares(original, text_frames, steady, weighted=False):
    """The share of the picture's ink (a mask from the steady frame) still visible in each frame, for the original and the words."""
    ia, idd = load(original[steady]), load(text_frames[steady])
    box = np.zeros(ia.shape[:2], bool)
    ys, xs = np.nonzero((ia != idd).any(axis=2))
    if len(ys):
        box[ys.min():ys.max() + 1, xs.min():xs.max() + 1] = True
    mask_a = box & (ia.max(axis=2) > 60)
    mask_d = box & (idd.max(axis=2) > 60)
    if weighted:  # (a fade scales the brightness: the picture's ink by how much of its steady brightness it has)
        total_a, total_d = float(ia.max(axis=2)[mask_a].sum()), float(idd.max(axis=2)[mask_d].sum())
        return ([float(load(f).max(axis=2)[mask_a].sum()) / max(total_a, 1.0) for f in original],
                [float(load(f).max(axis=2)[mask_d].sum()) / max(total_d, 1.0) for f in text_frames])
    share_a = [float(((load(f).max(axis=2) > 60) & mask_a).sum()) / max(int(mask_a.sum()), 1) for f in original]
    share_d = [float(((load(f).max(axis=2) > 60) & mask_d).sum()) / max(int(mask_d.sum()), 1) for f in text_frames]
    return share_a, share_d


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(int)


def bands(rows, gap=8):
    out = []
    for y in np.flatnonzero(rows):
        if out and y - out[-1][1] <= gap:
            out[-1][1] = y
        else:
            out.append([y, y])
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", help="keep the work here")
    args = parser.parse_args()
    if not (ROOT / "data/GIM/STAFF004.GIM").exists() or not (ROOT / "data/GIM/JO.GIM").exists():
        print("SKIP: no data/GIM/STAFF004.GIM or JO.GIM")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "gim_text"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    pack, empty = out / "pack", out / "empty"
    empty.mkdir()
    lines = make_pack(pack)
    if not lines:
        print("FAIL: the pack tool wrote no lines")
        return 1
    failures = 0

    def check(ok, what):
        nonlocal failures
        print(("ok:   " if ok else "FAIL: ") + what)
        failures += not ok

    fa = frames(out / "a", "original", pack)
    fb = frames(out / "b", "original", empty)
    fc = frames(out / "c", "font", empty)
    check(len(fa) == len(fb) == len(fc) == SAMPLES, f"{SAMPLES} frames each way")
    same_b = sum(int((load(x) != load(y)).any()) for x, y in zip(fa, fb))
    check(same_b == 0, f"original text with a pack = with none: {same_b} of {len(fa)} frames differ")
    ta = frames(out / "ta", "original", pack, step=4, samples=60, skip=1470)
    qa = quote_frames(out / "qa", "original", pack)
    qb = quote_frames(out / "qb", "original", empty)
    check(sum(int((load(x) != load(y)).any()) for x, y in zip(qa, qb)) == 0, "the quote: original text with a pack = with none, every frame")
    shown_auto = []
    for overlay in ("auto", "off"):
        tag = f"[overlay {overlay}] "
        fd = frames(out / f"d-{overlay}", "font", pack, overlay=overlay)
        counts = [int((load(x) != load(y)).any(axis=2).sum()) for x, y in zip(fc, fd)]
        shown = [i for i, n in enumerate(counts) if n > 50000]
        if overlay == "auto":
            shown_auto = shown
            same_c = sum(int((load(fa[i]) != load(fc[i])).any()) for i in shown)
            check(same_c == 0, f"modern text with no pack = original text on the card's frames: {same_c} of {len(shown)} differ")
        check(len(shown) >= 3, f"{tag}the pack replaces the card's words in {len(shown)} frames (samples {shown[:1]}..{shown[-1:]})")
        if shown:
            i = shown[len(shown) // 2]
            a, d = load(fa[i]), load(fd[i])
            diff = (a != d).any(axis=2)
            found = bands(diff.any(axis=1))
            check(len(found) == lines, f"{tag}sample {i}: the differing rows form {len(found)} bands, one per pack line ({lines})")
            ornament = (a.max(axis=2) > 40) & ~diff
            check(int(ornament.sum()) > 100, f"{tag}sample {i}: the ink outside the lines' boxes stays (the rule under the title): {int(ornament.sum())} pixels")
            check(int((d.max(axis=2) > 40).sum()) > int(ornament.sum()), f"{tag}sample {i}: and the words are there in modern text")
        # Transitions: the card is wiped in and out by the game's scanline wipe, the words with it.
        td = frames(out / f"td-{overlay}", "font", pack, step=4, samples=60, skip=1470, overlay=overlay)
        share_a, share_d = shares(ta, td, len(ta) // 2)
        partial = [i for i, v in enumerate(share_a) if 0.1 < v < 0.9]
        check(len(partial) >= 2, f"{tag}the wipes are in the frames: {len(partial)} with the card part visible")
        worst = max(abs(x - y) for x, y in zip(share_a, share_d))
        check(worst < 0.3, f"{tag}the words follow the wipes: the share of the words visible is within {worst:.2f} of the original ink's, every frame")
        # The quote fades in under the game's veil (a later draw over the picture): the words too.
        qd = quote_frames(out / f"qd-{overlay}", "font", pack, overlay=overlay)
        share_a, share_d = shares(qa, qd, len(qa) - 1, weighted=True)
        partial = [i for i, v in enumerate(share_a) if 0.1 < v < 0.9]
        check(len(partial) >= 2, f"{tag}the quote's fade is in the frames: {len(partial)} with the quote part visible")
        worst = max(abs(x - y) for x, y in zip(share_a, share_d))
        check(worst < 0.3, f"{tag}the words follow the quote's fade: the share of the words visible is within {worst:.2f} of the original ink's, every frame")
    # No drawer may show the picture's original ink under the real words: the card drawn by the framebuffer fade (mode 1) and the crossfade (mode 3) drawers.
    for mode in (1, 3):
        fa_, fd_ = forced_frame(out / f"fa{mode}", "original", pack, mode), forced_frame(out / f"fd{mode}", "font", pack, mode)
        diff = (fa_ != fd_).any(axis=2)
        found = bands(diff.any(axis=1))
        check(lines <= len(found) <= lines + 3, f"drawer mode {mode}: the differing rows form {len(found)} bands around the pack's {lines} lines")
        check(int(diff.sum()) > 20000, f"drawer mode {mode}: the words replace the card's ink: {int(diff.sum())} pixels differ")
    # Another picture, the Valendia Knights' incident report (VKP_2.GIM, scene evt-0013: two pages of plain lines, a wide line cut into pieces): the pack's words replace it.
    ra = frames(out / "ra", "original", pack, scene="evt-0013", step=30, samples=140)
    rd = frames(out / "rd", "font", pack, scene="evt-0013", step=30, samples=140)
    rcounts = [int((load(x) != load(y)).any(axis=2).sum()) for x, y in zip(ra, rd)]
    rshown = [i for i, n in enumerate(rcounts) if n > 3000 and "VKP_2" in ACTIVE[str(out / "rd")][i]]  # (the frames the report is shown in)
    check(len(rshown) >= 3, f"VKP_2: the pack replaces the report's words in {len(rshown)} frames (the frames it is shown in)")
    if rshown:
        i = rshown[len(rshown) // 2]
        diff = (load(ra[i]) != load(rd[i])).any(axis=2)
        check(len(bands(diff.any(axis=1))) >= 4, f"VKP_2 sample {i}: the differing rows form {len(bands(diff.any(axis=1)))} bands, one per line")
    # The same pack at 16:9 (a 1280 x 720 view with the widescreen picture): no per-aspect data, one mapping of the picture's pixels to the screen.
    wa = frames(out / "wa", "original", pack, wide=True)
    wd = frames(out / "wd", "font", pack, wide=True)
    wcounts = [int((load(x) != load(y)).any(axis=2).sum()) for x, y in zip(wa, wd)]
    wshown = [i for i, n in enumerate(wcounts) if n > 20000 and i in shown_auto]
    check(len(wshown) >= 3, f"16:9: the same pack replaces the card's words in {len(wshown)} frames")
    if wshown:
        i = wshown[len(wshown) // 2]
        a, d = load(wa[i]), load(wd[i])
        diff = (a != d).any(axis=2)
        found = bands(diff.any(axis=1))
        check(len(found) == lines, f"16:9 sample {i}: the differing rows form {len(found)} bands, one per pack line ({lines})")
        ornament = (a.max(axis=2) > 40) & ~diff
        check(int(ornament.sum()) > 100, f"16:9 sample {i}: the rule under the title stays: {int(ornament.sum())} pixels")
        columns = np.flatnonzero(diff.any(axis=0))
        centre = (columns.min() + columns.max()) / 2
        check(abs(centre - a.shape[1] / 2) < 0.02 * a.shape[1], f"16:9 sample {i}: the words are centred in the view as the picture is ({centre:.0f} of {a.shape[1]})")
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
