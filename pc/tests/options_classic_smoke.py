#!/usr/bin/env python3
"""The Option pages in the game's own Option screen (classic look:
pc/src/ui/classic_options.c, MENU8's ROOD_PC hooks; plan
pc/docs/plans/2026-10-18-lua-m1-options.md C1, E1, E2), driven by the pad
(RPC press) and read back through RPC options.

    python3 pc/tests/options_classic_smoke.py [--card CARD] [--out DIR] [--update]

From a copy of the card (skipped without one), the save room, the classic
menus, ROOD_SETTINGS on a fresh file and the fixture plugin test.menu (a page
of its own: a header, a help line, settings it defines, an action):
  - Option: the nine vanilla rows, then the page model's entries in order
    (Gameplay ... Controls, then Tweaks); the core pages' labels all fit their
    column (none cut)
  - Display: rows = rood.menu.rows(id, "classic") in order; the info line is
    the focused row's help, set as the game's own descriptions are (two lines
    of up to 270 pixels with its newline: Widescreen's in one box, Frame
    rate's longer one a box of two lines at a time); Right on Filter changes it live and Cross saves
    it (settings.cfg) on the way back; the list's cursor is back on Display
    (an entry past the nine: cursor memory)
  - Scaling: Circle opens the game's flyout on the current choice, Down +
    Circle picks the next one (live), Cross in a flyout changes nothing
  - Controls > Bindings: the link opens the sub-page (depth 1), Cross comes
    back to Controls on the link
  - Tweaks (the fixture): the header is skipped (focus on Fast), Circle flips
    Fast (rood.settings), the action runs (the plugin's counter), the help
    line shows; editing the plugin's main.lua while the page is open reloads
    it: the new row shows, the focus stays
  - Triangle on a page closes the menu
  - the game's own options (vs_main_settings) and the card are byte for byte
    what they were
Goldens (--update writes them; compared when present, else noted): the
Display page at rest, original glyphs and ui_text font, cropped to the menu
rows, in pc/build/golden/<renderer>/ (game imagery: never committed).
Waits are on conditions (RPC options screen + ready), never vsync counts."""
import argparse
import json
import os
import pathlib
import shutil
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import default_save  # noqa: E402
from menu_nav import top_command  # noqa: E402

OPTION_COMMAND = 7  # the top level's Option row (currentMenu 8: MENU8)
VANILLA_ROWS = 9
FIXTURE = ROOT / "pc/tests/lua/plugins/test.menu"
CORE_PAGES = ["rood.options/gameplay", "rood.options/display", "rood.options/graphics", "rood.options/audio",
              "rood.options/interface", "rood.options/controls"]
GOLDEN = ROOT / "pc/build/golden"
CROP = (180, 140, 960, 560)  # the page's rows at 960 x 720 (PS1 x 60..320, y 46..186)


class Timeout(Exception):
    pass


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out")
    parser.add_argument("--update", action="store_true", help="write the golden images")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "options_classic"
    out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="options-", dir=out))
    card = work / "card1.mcd"
    shutil.copyfile(args.card, card)
    settings = work / "settings.cfg"
    plugins = work / "plugins"
    shutil.copytree(FIXTURE, plugins / "test.menu")
    (work / "lights").mkdir()
    env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(settings), "ROOD_LIGHTS": str(work / "lights"),
           "ROOD_GI": "0", "ROOD_PLUGINS": str(plugins), "ROOD_PLUGIN_DATA": str(work / "plugin-data")}
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    card_before = card.read_bytes()
    game = launch_at("save_room", card=card, log=out / "game.log", extra_env=env)
    sym = symbols()
    try:
        game.call("ui_menu", modern=False)  # the game's own menus

        def opt():
            return game.call("options")["classic"]

        def wait(screen, limit=900):
            for _ in range(limit):
                o = opt()
                if o["screen"] == screen and o["ready"]:
                    return o
                game.run(1)
            raise Timeout(f"options screen {screen}: {json.dumps(opt())[:300]}")

        def press(button):
            game.press(button, vsyncs=2, settle=2)

        def list_to(index):
            for _ in range(40):
                if wait("list")["list_focus"] == index:
                    return
                press("down")
            raise Timeout(f"Option list entry {index}")

        def page_to(row_id):
            for _ in range(80):
                o = wait("page")
                if o["focus_id"] == row_id:
                    return o
                press("down")
            raise Timeout(f"row {row_id}")

        def row(o, row_id):
            return next(r for r in o["rows"] if r["id"] == row_id)

        def row_help(row_id):
            return lua(f'for _, r in ipairs(rood.menu.rows("rood.options/display", "classic")) do '
                       f'if r.id == "{row_id}" then return r.help end end')[0]

        def lua(code):
            r = game.lua(code)
            if not r.get("ok", True) and r.get("error"):
                raise RuntimeError(r["error"])
            return r["results"]

        game_options = game.read(sym["vs_main_settings"], 32)

        # ---- the Option list ----
        game.press("triangle", vsyncs=3, settle=30)
        top_command(game, sym, OPTION_COMMAND)
        press("circle")
        o = wait("list")
        entries = o["entries"]
        check(entries[:6] == CORE_PAGES and "test.menu/tweaks" in entries,
              f"Option entries after the nine rows: the core pages, then the plugin's ({entries})")
        check(not [c for c in o["cut"] if c], f"no label cut in the classic column ({o['cut']})")
        display = VANILLA_ROWS + entries.index("rood.options/display")

        # ---- Display ----
        list_to(display)
        press("circle")
        o = wait("page")
        classic_ids = lua('local t = {} for _, r in ipairs(rood.menu.rows("rood.options/display", "classic")) '
                          'do t[#t + 1] = r.id end return t')[0]
        check(o["page"] == "rood.options/display" and [r["id"] for r in o["rows"]] == classic_ids,
              f"Display: the page model's classic rows ({[r['id'] for r in o['rows']]})")
        check(o["info"] == row(o, o["focus_id"]).get("reason") or o["info"] == lua(
              f'for _, r in ipairs(rood.menu.rows("rood.options/display", "classic")) do '
              f'if r.id == "{o["focus_id"]}" then return r.help end end')[0],
              f"the information line is the focused row's help ({o['info']!r})")
        golden(game, out, args, check)
        # the help set as the game's own descriptions are: up to two lines of up to 270 pixels, the game's newline
        o = page_to("widescreen")
        help_text = row_help("widescreen")
        check(o["info_boxes"] == 1 and 1 < len(o["info_lines"]) <= 2 and " ".join(o["info_lines"]) == help_text
              and all(len(line) <= 45 for line in o["info_lines"]),
              f"Widescreen's help in one box of two lines, as the game's descriptions ({o['info_lines']})")
        o = page_to("filter")
        before = row(o, "filter")["value"]
        press("right")
        o = wait("page")
        after = row(o, "filter")["value"]
        check(after != before and o["focus_id"] == "filter", f"Right on Filter: {before} -> {after}")
        filter_value = game.call("settings", get="filter")["line"]
        o = page_to("present_rate")  # (a help longer than a box: two boxes in turn, two lines each)
        lines = o["info_lines"]
        check(o["info_boxes"] == 2 and o["info_box"] == 0 and len(lines) == 2
              and row_help("present_rate").startswith(" ".join(lines)),
              f"Frame rate's longer help: box 1 of {o['info_boxes']}, two lines ({lines})")
        press("cross")
        o = wait("list")
        saved = settings.read_text() if settings.exists() else ""
        check(filter_value in saved.splitlines(), f"Cross: saved '{filter_value}'")
        check(o["list_focus"] == display, f"the list's cursor back on Display ({o['list_focus']} == {display})")

        # ---- Scaling: the flyout ----
        press("circle")
        o = page_to("aspect")
        current = row(o, "aspect")
        press("circle")
        wait("flyout")
        press("cross")  # back: nothing changes
        o = wait("page")
        check(row(o, "aspect")["value"] == current["value"] and o["focus_id"] == "aspect",
              f"Cross in the flyout: Scaling still {current['value']}")
        press("circle")
        wait("flyout")
        press("down")
        press("circle")
        o = wait("page")
        picked = row(o, "aspect")
        check(picked["index"] == (current["index"] + 1) % current["count"],
              f"flyout Down + Circle: Scaling {current['value']} -> {picked['value']}")
        check(game.call("settings", get="aspect")["line"] == f"aspect {picked['index']}",
              f"live: {game.call('settings', get='aspect')['line']}")
        press("cross")
        wait("list")

        # ---- Controls > Bindings ----
        list_to(VANILLA_ROWS + entries.index("rood.options/controls"))
        press("circle")
        page_to("rood.options/bindings")
        press("circle")
        o = wait("page")
        check(o["page"] == "rood.options/bindings" and o["depth"] == 1 and o["focus_id"] == "bind_circle",
              f"the Bindings link: its page, a level down ({o['page']}, depth {o['depth']}, {o['focus_id']})")
        press("cross")
        o = wait("page")
        check(o["page"] == "rood.options/controls" and o["depth"] == 0 and o["focus_id"] == "rood.options/bindings",
              f"Cross: back on Controls' link ({o['page']}, {o['focus_id']})")
        press("cross")
        wait("list")

        # ---- the fixture's page ----
        list_to(VANILLA_ROWS + entries.index("test.menu/tweaks"))
        press("circle")
        o = wait("page")
        check(o["rows"][0]["kind"] == "header" and o["focus_id"] == "plugin:test.menu/fast",
              f"Tweaks: the header skipped, focus on Fast ({o['focus_id']})")
        fast = row(o, "plugin:test.menu/fast")["value"]
        press("circle")
        o = wait("page")
        flipped = row(o, "plugin:test.menu/fast")["value"]
        check({fast, flipped} == {"On", "Off"}, f"Circle on Fast flips it ({fast} -> {flipped})")
        page_to("test.menu/clear")
        press("circle")
        wait("page")
        check(game.lua("return cleared", plugin="test.menu")["results"] == [1], "Circle on Clear runs the action")
        check(opt()["info"] == "Counts.", f"its help in the information box ({opt()['info']!r})")
        page_to("plugin:test.menu/fast")
        main_lua = plugins / "test.menu/main.lua"
        main_lua.write_text(main_lua.read_text().replace(
            '  "widescreen",', '  "widescreen",\n  { define = "extra", kind = "toggle", default = true, label = "Extra" },'))
        for _ in range(600):
            o = opt()
            if any(r["id"] == "plugin:test.menu/extra" for r in o["rows"]) and o["screen"] == "page":
                break
            game.run(2)
        check(any(r["id"] == "plugin:test.menu/extra" for r in o["rows"]) and o["focus_id"] == "plugin:test.menu/fast",
              f"hot reload with the page open: the new row shows, the focus stays ({o['focus_id']})")

        # ---- Triangle closes ----
        press("triangle")
        for _ in range(300):
            if opt()["screen"] == "none":
                break
            game.run(1)
        check(opt()["screen"] == "none", "Triangle on a page closes the menu")
        game.run(30)
        check(game.read(sym["vs_main_settings"], 32) == game_options,
              "the game's own options (vs_main_settings) unchanged")
    except Timeout as e:
        check(False, f"timed out: {e}")
    finally:
        game.close()
    check(card.read_bytes() == card_before, "the card copy byte for byte as before")
    if not args.out:
        shutil.rmtree(work, ignore_errors=True)
    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


def capture(game, path):
    """The presented window image (RGB), taken twice: the page at rest is the same frame."""
    images = []
    for k in range(2):
        game.run(4)
        p = path.with_name(f"{path.stem}-{k}.png")
        game.present_screenshot(p)
        images.append(np.asarray(Image.open(p).convert("RGB")).astype(np.int16))
        p.unlink()
    return images


def golden(game, out, args, check):
    """The Display page at rest with original glyphs and with ui_text font: compared with (or, --update,
    written to) pc/build/golden/<renderer>/options-display-<text>.png, cropped to the rows."""
    renderer = os.environ.get("ROOD_RENDERER") or "gl"
    folder = GOLDEN / renderer
    for text in ("original", "font"):
        game.call("graphics", ui_text=text)
        name = f"options-display-{text}"
        a, b = capture(game, out / name)
        x0, y0, x1, y1 = CROP
        a, b = a[y0:y1, x0:x1], b[y0:y1, x0:x1]
        if np.abs(a - b).mean() > 0.5:
            print(f"note: {name}: the page is not at rest between two frames: no golden comparison")
            continue
        ref = folder / f"{name}.png"
        if args.update:
            folder.mkdir(parents=True, exist_ok=True)
            Image.fromarray(a.astype(np.uint8)).save(ref)
            print(f"note: wrote {ref}")
            continue
        if not ref.exists():
            print(f"note: no golden {ref} (run with --update to write it)")
            continue
        expect = np.asarray(Image.open(ref).convert("RGB")).astype(np.int16)
        diff = np.abs(a - expect)
        mean, share = float(diff.mean()), float((diff.max(axis=2) > 32).mean())
        ok = expect.shape == a.shape and mean <= 1.0 and share <= 0.004
        if not ok:
            Image.fromarray(a.astype(np.uint8)).save(out / f"{name}-actual.png")
        check(ok, f"golden {name}: mean {mean:.2f}, {share * 100:.2f}% pixels apart")
    game.call("graphics", ui_text="original")


if __name__ == "__main__":
    sys.exit(main())
