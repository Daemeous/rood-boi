#!/usr/bin/env python3
"""Quit to title and Quit to desktop (pc/src/quit_options.c; src hook in
src/SLUS_010.40/main.c processPadState, ROOD_PC): the soft reset's path and the
window's close, asked in the modern System list, the classic Option screen,
the RPC (quit {to}) and the F1 overlay's File menu (a Dear ImGui window: not
driven here).

    python3 pc/tests/quit_smoke.py [--card CARD] [--out DIR]

Four games, each from a copy of the card at the save room (skipped without
one), ROOD_SETTINGS on a scratch file:
  modern   System > Quit to title: "Unsaved progress will be lost" opens on
           NO (focus no); Circle on NO: back to the list, the menu still up,
           the room kept; again, Left (YES), Circle: the title menu (the soft
           reset: the menu closed, monoSound 0 as the chord leaves it), then
           Continue loads the save's room again
  classic  (modern UI off) Option screen: Quit to title and Quit to desktop
           are the last two entries, after the pages; Circle on Quit to
           title: the question in the game's own flyout (YES / NO, NO
           first); Cross: back to the list; again, Up (YES), Circle: the
           title; Continue
  rpc      quit {to: title} from the field: the title, Continue; the same at
           the title: nothing happens (done false); quit {to: desktop, ask:
           true} on the title: no question, the process exits with code 0
  desktop  a changed setting (Audio > Master volume), System > Quit to
           desktop: No keeps the game, Yes exits with code 0 and the setting
           is in the scratch settings file
Waits are on conditions (the log's TITLE MENU READY count, the RPC's screens,
the process), never vsync counts."""
import argparse
import json
import os
import pathlib
import shutil
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Deadline, RpcError, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import default_save  # noqa: E402
from menu_nav import top_command  # noqa: E402

CLOSED = 0x3F
OPTION_COMMAND = 7
VANILLA_ROWS = 9
MONO_SOUND = 10  # vs_main_settings_t (src/SLUS_010.40/main.h)
VIBRATION_ON = 11


class Timeout(Exception):
    pass


class Run:
    """One launched game with the checks around it."""

    def __init__(self, name, card_source, out, extra=None):
        self.name = name
        self.work = pathlib.Path(tempfile.mkdtemp(prefix=name + "-", dir=out))
        self.card = self.work / "card1.mcd"
        shutil.copyfile(card_source, self.card)
        self.settings = self.work / "settings.cfg"
        (self.work / "lights").mkdir()
        # (a unix socket's path is at most ~108 bytes: a short directory of its own, not the suite's output)
        self.sockdir = pathlib.Path(tempfile.mkdtemp(prefix="quit-"))
        socket = self.sockdir / f"{name}.sock"
        env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(self.settings), "ROOD_LIGHTS": str(self.work / "lights"),
               "ROOD_GI": "0", "ROOD_OUT": str(self.work)}
        env.update(extra or {})
        self.game = launch_at("save_room", card=self.card, log=self.work / "game.log", extra_env=env,
                              socket_path=str(socket))
        self.sym = symbols()
        self.room = self.game.state()["zone"], self.game.state()["room"]

    # ---- reading ----
    def titles(self):
        return pathlib.Path(self.game.log).read_text(errors="replace").count("TITLE MENU READY")

    def menu(self):
        return self.game.read(self.sym["vs_battle_menuState"], 1)[0]

    def shell(self):
        return self.game.call("ui_menu")

    def classic(self):
        return self.game.call("options")["classic"]

    def settings_value(self, key):
        lines = self.settings.read_text().splitlines() if self.settings.exists() else []
        found = [line.split(" ", 1)[1].strip() for line in lines if line.startswith(key + " ")]
        return found[-1] if found else None

    def press(self, button, settle=2):
        self.game.press(button, vsyncs=2, settle=settle)

    def until(self, condition, what, limit=1500, step=2):
        for _ in range(limit):
            value = condition()
            if value:
                return value
            self.game.run(step)
        raise Timeout(what)

    # ---- the game's states ----
    def open_modern_system(self):
        self.game.call("ui_menu", modern=True)
        for _ in range(20):
            if self.shell()["visible"]:
                break
            self.game.press("triangle", vsyncs=3, settle=20)
        for _ in range(10):
            if self.shell().get("tab") == "system":
                break
            self.game.press("l1", vsyncs=3, settle=10)
        if self.shell().get("tab") != "system":
            raise Timeout("the System tab")

    def focus_item(self, item):
        for _ in range(40):
            if self.shell()["system"]["item"] == item:
                return
            self.press("down")
        raise Timeout(f"System list: {item} (at {self.shell()['system']['item']})")

    def wait_title(self, before, what):
        self.until(lambda: self.titles() > before, "the title menu: " + what, limit=2400, step=30)
        self.game.run(60)

    def starts(self):
        return pathlib.Path(self.game.log).read_text(errors="replace").count("[title] starting mode")

    def continue_game(self):
        """Start, then Continue / the card / its first save, as the save_room checkpoint does: the log says the
        title started a game, then the field (D_800F196C 1, the menu closed) is in the save's room."""
        before = self.starts()
        self.game.press("start", vsyncs=4, settle=90)
        for _ in range(3):
            self.game.press("circle", vsyncs=3, settle=90)
        self.until(lambda: self.starts() > before, "Continue: the title started a game", limit=2400, step=8)
        self.until(lambda: self.menu() == CLOSED and self.game.read(self.sym["D_800F196C"], 1)[0] == 1
                   and (self.game.state()["zone"], self.game.state()["room"]) == self.room,
                   "Continue: the save's room again", limit=2400, step=8)
        self.game.run(30)

    def close(self):
        self.game.close()
        shutil.rmtree(self.work, ignore_errors=True)
        shutil.rmtree(self.sockdir, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out")
    parser.add_argument("--cases", default="modern,classic,rpc,desktop")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "quit"
    out.mkdir(parents=True, exist_ok=True)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    def soft_reset_state(run):
        """What the soft reset leaves in the settings it changes (main.c: monoSound 0, vibrationOn 1)."""
        base = run.sym["vs_main_settings"]
        return run.game.read(base + MONO_SOUND, 1)[0], run.game.read(base + VIBRATION_ON, 1)[0]

    def prime(run):
        """Settings the soft reset changes, set the other way first (so the check shows the reset ran)."""
        base = run.sym["vs_main_settings"]
        run.game.write(base + MONO_SOUND, bytes([1]))
        run.game.write(base + VIBRATION_ON, bytes([0]))

    cases = args.cases.split(",")

    # ---- modern: System > Quit to title ----
    if "modern" in cases:
        run = Run("modern", args.card, out)
        try:
            g = run.game
            run.open_modern_system()
            prime(run)
            run.focus_item("quit_title")
            ids = [r["id"] for r in run.shell()["rows"]]
            check(ids[-2:] == ["quit_title", "quit_desktop"], f"the list ends with the two quit entries ({ids[-3:]})")
            run.press("circle")
            q = run.shell()["system"]["quit"]
            check(q == {"pending": "title", "focus": "no"}, f"Circle: the question, on No ({q})")
            check(run.shell()["visible"] and run.titles() == 1, "nothing left yet: the menu up, no new title")
            run.press("circle")  # No
            s = run.shell()
            check(s["visible"] and s["system"]["quit"]["pending"] == "" and s["system"]["item"] == "quit_title",
                  f"No: back to the list on Quit to title ({s['system']['quit']})")
            run.press("circle")
            run.press("left")
            q = run.shell()["system"]["quit"]
            check(q == {"pending": "title", "focus": "yes"}, f"Left: Yes focused ({q})")
            run.press("right")
            run.press("cross")  # Cross answers No
            check(run.shell()["system"]["quit"]["pending"] == "" and run.shell()["visible"], "Cross: No")
            before = run.titles()
            run.press("circle")
            run.press("left")
            run.press("circle")  # Yes
            run.wait_title(before, "Yes")
            sh = run.shell()
            check(not sh["visible"] and not sh["holding"] and sh["system"] is None or not sh["visible"],
                  f"Yes: the title menu, the modern menu closed ({sh['visible']}, {sh['holding']})")
            mono, vibration = soft_reset_state(run)
            check((mono, vibration) == (0, 1), f"the soft reset's own settings (monoSound {mono}, vibrationOn {vibration})")
            run.continue_game()
            check(True, f"Continue loads the save's room again {run.room}")
        except Timeout as e:
            check(False, f"modern: timed out: {e}")
        finally:
            run.close()

    # ---- classic: the Option screen's last entries ----
    if "classic" in cases:
        run = Run("classic", args.card, out)
        try:
            g = run.game
            g.call("ui_menu", modern=False)
            prime(run)
            g.press("triangle", vsyncs=3, settle=30)
            top_command(g, run.sym, OPTION_COMMAND)
            run.press("circle")
            o = run.until(lambda: (lambda c: c if c["screen"] == "list" and c["ready"] else None)(run.classic()),
                          "the Option list")
            entries = o["entries"]
            check(entries[-2:] == ["quit_title", "quit_desktop"] and "rood.options/display" in entries,
                  f"the Option list ends with the two quit entries, after the pages ({entries[-3:]})")
            row = VANILLA_ROWS + entries.index("quit_title")

            def focus_row(row):
                for _ in range(40):
                    c = run.until(lambda: (lambda c: c if c["screen"] == "list" and c["ready"] else None)(run.classic()),
                                  "the list")
                    if c["list_focus"] == row:
                        return
                    run.press("down")
                raise Timeout(f"list row {row}")

            focus_row(row)
            run.press("circle")
            o = run.until(lambda: (lambda c: c if c["screen"] == "quit" and c["ready"] else None)(run.classic()),
                          "the question")
            check(o["quit"]["to"] == "title" and o["quit"]["focus"] == "no" and "Unsaved progress" in o["info"],
                  f"Circle on Quit to title: the question in the information box, on NO ({o['quit']}, {o['info']!r})")
            run.press("cross")
            run.until(lambda: run.classic()["screen"] == "list" and run.classic()["ready"], "Cross: back to the list")
            check(run.titles() == 1 and run.classic()["list_focus"] == row, "Cross: No, the list on Quit to title")
            run.press("circle")
            run.until(lambda: run.classic()["screen"] == "quit" and run.classic()["ready"], "the question again")
            run.press("up")
            o = run.classic()
            check(o["quit"]["focus"] == "yes", f"Up: YES ({o['quit']})")
            run.press("down")
            run.press("circle")  # NO
            run.until(lambda: run.classic()["screen"] == "list" and run.classic()["ready"], "Circle on NO: the list")
            run.press("circle")
            run.until(lambda: run.classic()["screen"] == "quit" and run.classic()["ready"], "the question a third time")
            before = run.titles()
            run.press("up")
            run.press("circle")  # YES
            run.wait_title(before, "YES")
            check(run.classic()["screen"] == "none", f"YES: the title menu ({run.classic()['screen']})")
            mono, vibration = soft_reset_state(run)
            check((mono, vibration) == (0, 1), f"the soft reset's own settings (monoSound {mono}, vibrationOn {vibration})")
            run.continue_game()
            check(True, f"Continue loads the save's room again {run.room}")
        except Timeout as e:
            check(False, f"classic: timed out: {e}")
        finally:
            run.close()

    # ---- rpc: quit {to} ----
    if "rpc" in cases:
        run = Run("rpc", args.card, out)
        try:
            g = run.game
            before = run.titles()
            r = g.call("quit", to="title")
            check(r["to"] == "title" and r["done"], f"quit {{to: title}} from the field ({r})")
            run.wait_title(before, "the RPC")
            run.continue_game()
            check(True, "Continue after the RPC quit")
            before = run.titles()
            g.call("quit", to="title")
            run.wait_title(before, "the RPC again")
            r = g.call("quit", to="title")
            check(not r["done"] and not r["queued"], f"quit {{to: title}} on the title: nothing ({r})")
            g.run(120)
            check(run.titles() == before + 1, "the title is not entered again")
            try:
                g.call("quit", to="elsewhere")
                check(False, "a bad target is refused")
            except Exception as e:  # RpcError
                check("title" in str(e), f"a bad target is refused ({e})")
            r = g.call("quit", to="desktop", ask=True)
            check(r.get("done") is True, f"quit {{to: desktop, ask}} on the title asks nothing ({r})")
            code = g.processes[0].wait(timeout=30)
            check(code == 0, f"and the process exits with code 0 ({code})")
        except Timeout as e:
            check(False, f"rpc: timed out: {e}")
        finally:
            run.close()

    # ---- desktop: the modern entry ----
    if "desktop" in cases:
        run = Run("desktop", args.card, out)
        try:
            g = run.game
            run.open_modern_system()
            run.focus_item("audio")
            run.press("circle")
            for _ in range(10):
                if run.shell()["system"]["row"] == "master_volume":
                    break
                run.press("down")
            before = run.settings_value("master_volume")
            run.press("left")
            s = run.shell()["system"]
            check(s["dirty"] or s["saves"] > 0, f"Master volume changed ({s['dirty']}, saves {s['saves']})")
            run.press("cross")  # the page closes: saved
            run.focus_item("quit_desktop")
            run.press("circle")
            q = run.shell()["system"]["quit"]
            check(q == {"pending": "desktop", "focus": "no"}, f"Quit to desktop asks, on No ({q})")
            run.press("circle")
            check(g.processes[0].poll() is None and run.shell()["visible"], "No: the game goes on")
            run.press("circle")
            run.press("left")
            try:
                run.press("circle")  # Yes: the game goes before the press has settled
            except RpcError:
                pass
            code = None
            deadline = Deadline(60)
            while code is None and not deadline.passed():
                code = g.processes[0].poll()
                time.sleep(0.05)
            check(code == 0, f"Yes: the process exits with code 0 ({code})")
            after = run.settings_value("master_volume")
            check(after is not None and after != before, f"settings written: master_volume {before} -> {after}")
        except Timeout as e:
            check(False, f"desktop: timed out: {e}")
        finally:
            run.close()

    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
