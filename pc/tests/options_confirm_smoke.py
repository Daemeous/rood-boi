#!/usr/bin/env python3
""""Keep these settings?" (pc/src/display_confirm.c through the settings
catalog's confirm rows; plan pc/docs/plans/2026-10-18-lua-m1-options.md C2,
E3) in both Option screens, and the two screens' rows from the same page data.

    python3 pc/tests/options_confirm_smoke.py [--card CARD] [--out DIR]

From a copy of the card (skipped without one), the save room, ROOD_SETTINGS on
a fresh file; the countdown shortened through rood.options' look (RPC options
{style: {confirm_seconds: 3}}):
  - classic (the game's Option screen > Display > Window): the flyout's
    Fullscreen applies at once (system window_mode) and asks KEEP / REVERT;
    while it asks, settings.cfg keeps Windowed (a save in between writes the
    old value); Circle on KEEP: kept and saved fullscreen. Windowed again,
    then Cross on the question: back to fullscreen, nothing saved. Borderless
    with no answer: back to fullscreen by itself after the countdown (waited
    on the RPC's pending flag, real time), the page again
  - RPC options {keep: true} answers too (Windowed kept)
  - modern (System > Display > Window, Right): the modal asks (ui_menu
    system.confirm, focus Keep); no answer: it reverts by itself, the modal
    goes
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
           "ROOD_GI": "0"}
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

        # ---- classic: Option > Display > Window ----
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
        for _ in range(40):
            if wait("page")["focus_id"] == "window_mode":
                break
            press("down")

        def pick(index):
            """Circle on Window, the flyout's choice `index` (0 Windowed .. 2 Fullscreen), Circle. The flyout
            opens on the current choice; a list this short wraps."""
            current = next(r for r in wait("page")["rows"] if r["id"] == "window_mode")["index"]
            press("circle")
            o = wait("flyout")
            for _ in range((index - current) % 3):
                press("down")
            press("circle")
            return o

        pick(2)  # Fullscreen
        o = wait("confirm")
        check(window_mode() == "fullscreen" and o["confirm"]["pending"] and o["confirm"]["keys"] == ["window_mode"],
              f"Fullscreen applies at once and asks ({window_mode()}, {o['confirm']})")
        check("Keep these settings?" in o["info"], f"the question in the information box ({o['info']!r})")
        game.call("settings", action="save")
        check(saved("window_mode") == "windowed", f"while it asks, a save writes Windowed ({saved('window_mode')})")
        press("circle")  # KEEP (the focus starts there)
        o = wait("page")
        check(window_mode() == "fullscreen" and not o["confirm"]["pending"] and saved("window_mode") == "fullscreen",
              f"KEEP: kept and saved ({window_mode()}, saved {saved('window_mode')})")

        pick(0)  # Windowed, then Cross on the question
        wait("confirm")
        press("cross")
        o = wait("page")
        check(window_mode() == "fullscreen" and saved("window_mode") == "fullscreen",
              f"Cross on the question: reverted to fullscreen ({window_mode()}, saved {saved('window_mode')})")

        pick(1)  # Borderless, no answer
        wait("confirm")
        took = until_reverted(SECONDS + 60)
        o = wait("page")
        check(window_mode() == "fullscreen" and o["focus_id"] == "window_mode",
              f"no answer: back to fullscreen by itself after {took:.1f} s (countdown {SECONDS} s), on the page")

        pick(0)  # Windowed, kept through the RPC
        wait("confirm")
        game.call("options", keep=True)
        o = wait("page")
        check(window_mode() == "windowed" and not o["confirm"]["pending"],
              f"RPC options {{keep}}: Windowed kept ({window_mode()})")

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
        for _ in range(30):
            if sysv()["row"] == "window_mode":
                break
            press("down")
        press("right")
        c = sysv()["confirm"]
        check(c["pending"] and c["focus"] == "keep" and window_mode() == "borderless",
              f"modern Right on Window: Borderless at once, the modal asks on Keep ({c}, {window_mode()})")
        took = until_reverted(SECONDS + 60)
        c = sysv()["confirm"]
        check(not c["pending"] and window_mode() == "windowed",
              f"modern, no answer: back to Windowed by itself after {took:.1f} s ({window_mode()})")
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
