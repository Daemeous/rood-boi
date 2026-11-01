#!/usr/bin/env python3
"""The classic menus' model, rood.game.menu() (plan
pc/docs/plans/2026-10-20-classic-ui-in-lua.md §2.3, phase 2 step 1), read
against RPC options (pc/src/ui/classic_options.c) in the game's own Option
screen.

    python3 pc/tests/menu_model_smoke.py [--card CARD] [--out DIR]

From a copy of the card (skipped without one), the save room, the classic
menus (ui_menus 0), the null renderer:
  - Option's list: screen "options", MAINMENU loaded, the cursor (row + page)
    is RPC options' list_focus, its rows have text
  - Display: the page's rows are menu items 20.. with the page model's
    labels (capitals, as drawn) from its top row, the focused one selected;
    info is the help box RPC options reports (its lines); the same table
    for every query in one vsync, a fresh one the next
  - Scaling's flyout: rows 30.. with the current choice's text among them
  - read-only: the game's own options (vs_main_settings) unchanged, the card
    byte for byte"""
import argparse
import json
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
from card_fixtures import default_save  # noqa: E402
from menu_nav import top_command  # noqa: E402

OPTION_COMMAND = 7
ROW_BASE, ROWS, FLY_BASE = 20, 8, 30  # classic_options.c: the page's rows, the flyout's


class Timeout(Exception):
    pass


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out")
    parser.add_argument("--renderer", choices=["null", "gl", "vulkan"], default="null")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "menu_model"
    out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="menu-model-", dir=out))
    card = work / "card1.mcd"
    shutil.copyfile(args.card, card)
    (work / "lights").mkdir()
    env = {"ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_LIGHTS": str(work / "lights"), "ROOD_GI": "0",
           "ROOD_PLUGIN_DATA": str(work / "plugin-data")}
    if args.renderer == "null":
        env.update({"ROOD_RENDERER": "null", "SDL_VIDEO_DRIVER": "invalid-null-menu-model"})
    else:
        env["ROOD_RENDERER"] = args.renderer  # named: the game's own default is Vulkan
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    card_before = card.read_bytes()
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    sym = symbols()
    try:
        game.call("ui_menu", modern=False)

        def lua(code):
            r = game.lua(code)
            if not r.get("ok", True) and r.get("error"):
                raise RuntimeError(r["error"])
            return r["results"]

        def menu():  # (a plain copy: the RPC's results don't see through the read-only views)
            return lua("local function copy(t) if type(t) ~= 'table' then return t end local o = {} "
                       "for k, v in pairs(t) do o[k] = copy(v) end return o end return copy(rood.game.menu())")[0]

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

        def rows_by_id(m):
            return {r["id"]: r for r in (m or {}).get("rows", [])}

        game_options = game.read(sym["vs_main_settings"], 32)

        # ---- the Option list ----
        game.press("triangle", vsyncs=3, settle=30)
        top_command(game, sym, OPTION_COMMAND)
        press("circle")
        o = wait("list")
        m = menu()
        check(m and m["screen"] == "options" and m["menu"] & 0x3F == 8 and m["loaded"],
              f"the list: screen options, MAINMENU loaded ({m and (m['screen'], m['menu'], m['loaded'])})")
        check(m["cursor"]["row"] + m["cursor"]["page"] == o["list_focus"],
              f"the cursor: row {m['cursor']['row']} + page {m['cursor']['page']} = list_focus {o['list_focus']}")
        live = [r for r in m["rows"] if r["state"] != "inactive"]
        check(live and all(r["text"] for r in live if r["id"] < 20),
              f"the list's rows have text ({[(r['id'], r['text']) for r in live][:12]})")
        press("down")
        o = wait("list")
        m = menu()
        check(m["cursor"]["row"] + m["cursor"]["page"] == o["list_focus"],
              f"Down: the cursor follows ({m['cursor']['row']} + {m['cursor']['page']} = {o['list_focus']})")
        same = lua("return rawequal(rood.game.menu(), rood.game.menu())")[0]
        check(same, "one table per vsync (two queries, the same table)")
        stale = lua("_G.menu_model_kept = rood.game.menu() return true")[0]
        game.run(1)
        fresh = lua("return not rawequal(_G.menu_model_kept, rood.game.menu())")[0]
        check(stale and fresh, "a fresh table the next vsync")
        written = lua("return pcall(function() rood.game.menu().rows[1].x = 0 end)")[0]
        check(written is False, "read-only: a row cannot be written")

        # ---- Display ----
        display = 9 + o["entries"].index("rood.options/display")
        for _ in range(40):
            if wait("list")["list_focus"] == display:
                break
            press("down" if wait("list")["list_focus"] < display else "up")
        press("circle")
        o = wait("page")
        m = menu()
        rows = rows_by_id(m)
        drawn = [r for r in o["rows"]][o["top"]:o["top"] + ROWS]
        want = [("" if r["kind"] == "note" else r["label"].upper()) for r in drawn]
        got = [rows.get(ROW_BASE + i, {}).get("text", None) for i in range(len(drawn))]
        cut = [r.get("cut", False) for r in drawn]
        check(all(g == w or c for g, w, c in zip(got, want, cut)) and len(got) == len(want),
              f"Display: items 20.. are the page model's rows from its top ({got} vs {want})")
        focus = ROW_BASE + o["focus"] - o["top"]
        check(rows.get(focus, {}).get("selected"), f"the focused row (item {focus}) is selected")
        info = m.get("info") or ""
        check(info.split() == " ".join(o["info_lines"]).split(),
              f"info: text box 7 is the help RPC options shows ({info!r} / {o['info_lines']})")

        # ---- Scaling's flyout ----
        for _ in range(80):
            o = wait("page")
            if o["focus_id"] == "aspect":
                break
            press("down")
        current = next(r for r in o["rows"] if r["id"] == "aspect")["value"]
        press("circle")
        wait("flyout")
        m = menu()
        fly = [r for r in m["rows"] if r["id"] >= FLY_BASE]
        check(fly and any(r["text"] == current.upper() for r in fly),
              f"the flyout: items 30.. with the current choice {current!r} ({[r['text'] for r in fly]})")
        press("cross")
        wait("page")
        press("cross")
        wait("list")
        press("triangle")
        for _ in range(300):
            if opt()["screen"] == "none":
                break
            game.run(1)
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


if __name__ == "__main__":
    sys.exit(main())
