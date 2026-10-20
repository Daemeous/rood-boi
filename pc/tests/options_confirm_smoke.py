#!/usr/bin/env python3
""""Keep these settings?" (pc/src/display_confirm.c through the settings
catalog's confirm rows; plan pc/docs/plans/2026-10-18-lua-m1-options.md C2,
E3) in both Option screens, and the two screens' rows from the same page data.

    python3 pc/tests/options_confirm_smoke.py [--card CARD] [--out DIR]

Only the exclusive fullscreen mode (fullscreen_mode) asks: the window mode and
the display apply and save at once. A headless display has no exclusive modes,
so the game gets two test ones (ROOD_TEST_FULLSCREEN_MODES, boot_window.c):
Desktop, 1920 x 1080 and 1280 x 720 at 60 Hz.

From a copy of the card (skipped without one), the save room, ROOD_SETTINGS on
a fresh file; the countdown shortened through rood.options' look (RPC options
{style: {confirm_seconds: 3}}):
  - classic (the game's Option screen > Display): Window > Fullscreen applies
    and saves at once, no question. Fullscreen mode > 1920 x 1080 applies at
    once (system display.fullscreen_mode) and asks KEEP / REVERT; while it
    asks, settings.cfg keeps desktop (a save in between writes the old value);
    Circle on KEEP: kept and saved. Desktop, then Cross on the question: back
    to 1920 x 1080, nothing saved. 1280 x 720 with no answer: back to
    1920 x 1080 by itself after the countdown (waited on the RPC's pending
    flag, real time), the page again
  - RPC options {keep: true} answers too (Desktop kept)
  - modern (System > Display): Fullscreen mode, Right: the modal asks
    (ui_menu system.confirm, focus Keep); no answer: it reverts by itself, the
    modal goes. Window, Left: Borderless at once, no modal
  - parity: each core page's rows in the modern System screen (ui_menu, as
    the shell lists them) are rood.menu.rows(id, "modern"); the classic page's
    (RPC options) are rood.menu.rows(id, "classic"): the classic rows are the
    modern ones less the game's own options (where = "modern"), in order
Waits are on conditions (RPC options screen / pending, ui_menu at_rest),
never vsync counts."""
import argparse
import json
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Deadline, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import default_save  # noqa: E402
from menu_nav import top_command  # noqa: E402

OPTION_COMMAND = 7
VANILLA_ROWS = 9
SECONDS = 3  # the countdown here (confirm_seconds' minimum)
MODES = ["desktop", "1920x1080@60", "1280x720@60"]  # fullscreen_mode's choices here (Desktop first)
CORE = ["gameplay", "display", "graphics", "audio", "interface", "controls", "bindings"]


class Timeout(Exception):
    pass


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "options_confirm"
    out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="confirm-", dir=out))
    card = work / "card1.mcd"
    shutil.copyfile(args.card, card)
    settings = work / "settings.cfg"
    (work / "lights").mkdir()
    env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(settings), "ROOD_LIGHTS": str(work / "lights"),
           "ROOD_GI": "0", "ROOD_TEST_FULLSCREEN_MODES": ",".join(MODES[1:])}
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, log=out / "game.log", extra_env=env)
    sym = symbols()
    try:
        def opt():
            return game.call("options")

        def classic():
            return opt()["classic"]

        def wait(screen, limit=900):
            for _ in range(limit):
                o = classic()
                if o["screen"] == screen and o["ready"]:
                    return o
                game.run(1)
            raise Timeout(f"options screen {screen}: {json.dumps(classic())[:300]}")

        def press(button):
            game.press(button, vsyncs=2, settle=2)

        def saved(key):
            lines = settings.read_text().splitlines() if settings.exists() else []
            found = [line.split(" ", 1)[1] for line in lines if line.startswith(key + " ")]
            return found[-1] if found else None

        def window_mode():
            return game.call("system")["window_mode"]

        def fullscreen_mode():
            return game.call("system")["display"]["fullscreen_mode"]

        def lua_ids(page, where):
            r = game.lua(f'local t = {{}} for _, r in ipairs(rood.menu.rows("{page}", "{where}")) do '
                         f't[#t + 1] = r.id end return t')
            return r["results"][0]

        def until_reverted(limit):
            """Awake seconds until the countdown reverted (`limit`: a hang guard; a stopped game's pause
            is not counted)."""
            deadline = Deadline(limit)
            while opt()["pending"]:
                if deadline.passed():
                    raise Timeout("the confirm never reverted")
                game.run(2)
            return deadline.elapsed()

        style = opt()["style"]
        check(style["confirm_seconds"] == 10, f"the countdown is rood.options' 10 s ({style['confirm_seconds']})")
        game.call("options", style={"confirm_seconds": SECONDS})
        game.call("ui_menu", modern=False)

        # ---- classic: Option > Display ----
        game.press("triangle", vsyncs=3, settle=30)
        top_command(game, sym, OPTION_COMMAND)
        press("circle")
        o = wait("list")
        display = VANILLA_ROWS + o["entries"].index("rood.options/display")
        for _ in range(40):
            if wait("list")["list_focus"] == display:
                break
            press("down")
        press("circle")

        def focus(row):
            for _ in range(40):
                if wait("page")["focus_id"] == row:
                    return
                press("down")  # (window_mode, then fullscreen_mode below it: only ever down)
            raise Timeout(f"the focus on {row}: {wait('page')['focus_id']}")

        def pick(row, index, count=3):
            """Circle on `row`, the flyout's choice `index`, Circle. The flyout opens on the current choice; a
            list this short wraps."""
            focus(row)
            current = next(r for r in wait("page")["rows"] if r["id"] == row)["index"]
            press("circle")
            o = wait("flyout")
            for _ in range((index - current) % count):
                press("down")
            press("circle")
            return o

        # the window mode: applied and saved at once, no question
        pick("window_mode", 2)  # Fullscreen
        o = wait("page")
        game.call("settings", action="save")
        check(window_mode() == "fullscreen" and not opt()["pending"] and saved("window_mode") == "fullscreen",
              f"Window > Fullscreen: applied and saved at once, no question ({window_mode()}, "
              f"saved {saved('window_mode')}, {o['confirm']})")

        # the exclusive mode asks
        pick("fullscreen_mode", 1)  # 1920 x 1080
        o = wait("confirm")
        check(fullscreen_mode() == MODES[1] and o["confirm"]["pending"] and o["confirm"]["keys"] == ["fullscreen_mode"],
              f"Fullscreen mode > 1920 x 1080 applies at once and asks ({fullscreen_mode()}, {o['confirm']})")
        check("Keep these settings?" in o["info"], f"the question in the information box ({o['info']!r})")
        game.call("settings", action="save")
        check(saved("fullscreen_mode") == "desktop",
              f"while it asks, a save writes desktop ({saved('fullscreen_mode')})")
        press("circle")  # KEEP (the focus starts there)
        o = wait("page")
        check(fullscreen_mode() == MODES[1] and not o["confirm"]["pending"] and saved("fullscreen_mode") == MODES[1],
              f"KEEP: kept and saved ({fullscreen_mode()}, saved {saved('fullscreen_mode')})")

        pick("fullscreen_mode", 0)  # Desktop, then Cross on the question
        wait("confirm")
        press("cross")
        o = wait("page")
        check(fullscreen_mode() == MODES[1] and saved("fullscreen_mode") == MODES[1],
              f"Cross on the question: reverted to 1920 x 1080 ({fullscreen_mode()}, "
              f"saved {saved('fullscreen_mode')})")

        pick("fullscreen_mode", 2)  # 1280 x 720, no answer
        wait("confirm")
        took = until_reverted(SECONDS + 60)
        o = wait("page")
        check(fullscreen_mode() == MODES[1] and o["focus_id"] == "fullscreen_mode",
              f"no answer: back to 1920 x 1080 by itself after {took:.1f} s (countdown {SECONDS} s), on the page")

        pick("fullscreen_mode", 0)  # Desktop, kept through the RPC
        wait("confirm")
        game.call("options", keep=True)
        o = wait("page")
        check(fullscreen_mode() == "desktop" and not o["confirm"]["pending"],
              f"RPC options {{keep}}: Desktop kept ({fullscreen_mode()})")

        # parity: the classic page's rows are the page model's classic rows
        check([r["id"] for r in o["rows"]] == lua_ids("rood.options/display", "classic"),
              "classic Display rows = rood.menu.rows(display, classic)")
        game.press("triangle", vsyncs=3, settle=2)
        for _ in range(600):  # the menu closed (vs_battle_menuState.currentMenu 0x3F)
            if game.read(sym["vs_battle_menuState"], 1)[0] == 0x3F:
                break
            game.run(1)

        # ---- modern: System > Display > Window ----
        game.call("ui_menu", modern=True)

        def shell():
            return game.call("ui_menu")

        def sysv():
            return shell()["system"]

        for _ in range(20):
            if shell()["visible"]:
                break
            game.press("triangle", vsyncs=3, settle=20)
        for _ in range(10):
            if shell().get("tab") == "system":
                break
            game.press("l1", vsyncs=3, settle=10)
        check(shell().get("tab") == "system", f"the System tab ({shell().get('tab')})")
        def enter(page):
            for _ in range(30):
                if sysv()["item"] == page:
                    break
                press("down")
            press("circle")
            return sysv()

        for page in CORE:
            s = enter(page)
            modern_ids = [r["id"] for r in s["rows"]]
            classic_ids = lua_ids("rood.options/" + page, "classic")
            check(s["page"] == page and modern_ids == lua_ids("rood.options/" + page, "modern"),
                  f"modern {page} rows = rood.menu.rows({page}, modern)")
            gone = [i for i in modern_ids if i not in classic_ids]
            extra = [i for i in classic_ids if i not in modern_ids]
            check([i for i in modern_ids if i in classic_ids] == [i for i in classic_ids if i in modern_ids]
                  and all("/" in i for i in extra),
                  f"{page}: the same rows in the same order (modern only: {gone}; classic only, links: {extra})")
            press("cross")
        enter("display")

        def goto(row):
            for _ in range(30):
                if sysv()["row"] == row:
                    return
                press("down" if row == "fullscreen_mode" else "up")

        goto("fullscreen_mode")
        press("right")
        c = sysv()["confirm"]
        check(c["pending"] and c["focus"] == "keep" and fullscreen_mode() == MODES[1],
              f"modern Right on Fullscreen mode: 1920 x 1080 at once, the modal asks on Keep ({c}, "
              f"{fullscreen_mode()})")
        took = until_reverted(SECONDS + 60)
        c = sysv()["confirm"]
        check(not c["pending"] and fullscreen_mode() == "desktop",
              f"modern, no answer: back to Desktop by itself after {took:.1f} s ({fullscreen_mode()})")
        goto("window_mode")
        press("left")
        c = sysv()["confirm"]
        check(not c["pending"] and window_mode() == "borderless",
              f"modern Left on Window: Borderless at once, no modal ({c}, {window_mode()})")
    except Timeout as e:
        check(False, f"timed out: {e}")
    finally:
        game.close()
    if not args.out:
        shutil.rmtree(work, ignore_errors=True)
    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
