#!/usr/bin/env python3
"""The classic look's HUD drawn by rood.classic (plan
pc/docs/plans/2026-10-20-classic-ui-in-lua.md, phase 1), on a real renderer:
its typography decided by rood.classic's look table and hot-reloaded, its
16:9 placement as Lua data, today's typography fixes kept on the Lua path.

    python3 pc/tests/classic_hud_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

One game from the save_room checkpoint (a copy of the card), the classic look
(ui_menus 0), ui_text font, 16:9, rood.classic from a copy of pc/plugins (so
the test can edit it), no player plugins:
  - rood.classic draws the timing word by default (RPC hud {stats}: claimed,
    suppressed, drawn), and ui_text sets it with rood.classic's typeset (RPC
    ui_text: the word's typeset fields); the gauges too; the Lua's cost a game
    frame (under 0.1 ms, no allocations); its own Lua tests, none skipped;
  - the two-word phrases (RIGHT ON!, WELL TIMED! from x 5, TOO SLOW!) set with
    one space (no wider than 1.15 of the original at size 1) and on the
    320-pixel screen at sizes 1 and 1.6: playtest fix 645a7477, now Lua values;
  - hot reload: look.typeset.blanks "each" written to rood.classic's main.lua
    while WELL TIMED! is held: the word is laid out wider (a space per blank),
    then the file's own values again; each loop timed (the save to the frame
    that shows it), printed; under 2 s;
  - the REPL: M.look.typeset.margin raised live moves the held word away from
    the left edge at the next frames, the file's value back after a reload;
  - 16:9 placement: look.status.place.dx written: the parity gate reports the
    difference as xy (an undeclared placement is a mismatch), the file's own
    values give parity again;
  - screenshots (16:9, <out>/shots): WELL TIMED! drawn by rood.classic, and by
    the game with rood.classic off: the same picture."""
import argparse
import json
import pathlib
import shutil
import statistics
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Deadline  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402

PHRASES = {3: "RIGHT ON!", 5: "WELL TIMED!"}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="logs, screenshots and the plugins' copy")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "classic_hud"
    out.mkdir(parents=True, exist_ok=True)
    with card_guard(out / "cards", source=args.card) as cards:
        return run(out, cards.card1)


def run(out, card):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    shipped = out / "shipped"
    shutil.rmtree(shipped, ignore_errors=True)
    shutil.copytree(ROOT / "pc/plugins", shipped)
    main_lua = shipped / "rood.classic" / "main.lua"
    original = main_lua.read_text()
    plugins = out / "plugins"
    shutil.rmtree(plugins, ignore_errors=True)
    plugins.mkdir(parents=True)
    shots = out / "shots"
    shots.mkdir(exist_ok=True)
    settings = out / "settings.cfg"
    settings.write_text("aspect 16:9\nui_menus 0\nui_text font\ninterpolate 0\n")
    env = {"ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(plugins), "ROOD_PLUGINS_SHIPPED": str(shipped),
           "ROOD_PLUGIN_DATA": str(out / "plugin_data"), "ROOD_WINDOW": "1280x720"}
    shutil.rmtree(out / "plugin_data", ignore_errors=True)
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        def stats():
            return g.call("hud", action="stats")["kinds"]["status"]

        def words():
            found = {}
            for s in g.call("ui_text")["status"]:  # (newest first)
                found.setdefault(s["text"], s)
            return found

        def show(args):
            g.call("hud", action="clear")
            g.call("hud", action="show", kind="status", hold=True, **args)

        def laid_out(text, cond=lambda w: True, guard=30, after=None):
            """The held word as ui_text last laid it out (after vsync `after`, default now), once cond holds."""
            box = {}
            since = g.status()["vsync"] if after is None else after

            def ready():
                w = words().get(text)
                if w and w["vsync"] > since and cond(w):
                    box["w"] = w
                    return True
                return False
            g.until(ready, guard=guard)
            return box.get("w")

        # rood.classic draws it, its typeset handed to ui_text
        show({"timing": "slow"})
        s0 = stats()
        g.until(lambda: stats()["drawn"] >= s0["drawn"] + 2, guard=60)
        s = stats()
        check(s["claimed_by"] == "rood.classic" and s["honoured"] and s["drawn"] > s0["drawn"]
              and s["suppressed"] > s0["suppressed"],
              f"SLOW!: drawn by rood.classic in the classic look (drawn {s['drawn'] - s0['drawn']}, claimed by "
              f"{s['claimed_by']})")
        w = laid_out("SLOW!", lambda w: w["typeset"] != 0)
        check(w is not None and w["typeset"] == 31, f"SLOW!: set by ui_text with rood.classic's typeset "
                                                    f"(fields {w and w['typeset']})")

        # the gauges: drawn by rood.classic by default too
        gauges = g.call("hud", action="stats")["kinds"]["gauges"]
        g.until(lambda: g.call("hud", action="stats")["kinds"]["gauges"]["drawn"] >= gauges["drawn"] + 2, guard=60)
        g2 = g.call("hud", action="stats")["kinds"]["gauges"]
        check(g2["claimed_by"] == "rood.classic" and g2["honoured"] and g2["suppressed"] > gauges["suppressed"],
              f"gauges: drawn by rood.classic in the classic look (drawn {g2['drawn'] - gauges['drawn']})")

        # the cost: rood.classic's draws (the word and the gauges each game frame) within the plan's 0.1 ms of
        # Lua a game frame for the HUD
        g.call("hud", action="stats", reset=True)
        g.until(lambda: g.call("hud", action="stats")["classic_frames"] >= 120, guard=60)
        st = g.call("hud", action="stats")
        check(st["classic_mean_ms"] < 0.1 and st["classic_allocations_per_frame"] < 1,
              f"cost: {st['classic_frames']} game frames, mean {st['classic_mean_ms']:.4f} ms, p99 "
              f"{st['classic_p99_ms']:.4f} ms of Lua a game frame (budget 0.1), "
              f"{st['classic_allocations_per_frame']:.2f} allocations a frame")

        # rood.classic's own Lua tests, in the field with the classic look (none skipped here)
        g.call("hud", action="clear")
        tests = g.lua_test(plugin="rood.classic")
        check(tests["failed"] == 0 and tests["skipped"] == 0 and tests["passed"] >= 4,
              f"rood.classic's Lua tests: {tests['passed']} passed, {tests['failed']} failed, {tests['skipped']} "
              f"skipped" + "".join(f"; {r['name']}: {r.get('message', '')}" for r in tests["results"]
                                   if r.get("status") != "pass"))

        # playtest fix 645a7477 on the Lua path
        shows = [({"timing": "success", "variant": v}, t) for v, t in PHRASES.items()] + [({"timing": "too_slow"},
                                                                                           "TOO SLOW!")]
        for size in (1.0, 1.6):
            g.call("ui_text", style="status", size=size)
            for args, text in shows:
                show(args)
                w = laid_out(text, lambda w: w["typeset"] != 0)
                if not check(w is not None, f"{text} (size {size}): set as one phrase, one space ({list(words())})"):
                    continue
                x0, x1, o0, o1 = w["extent"][0], w["extent"][2], w["original"][0], w["original"][2]
                if size == 1.0:
                    check((x1 - x0) / (o1 - o0) <= 1.15, f"{text}: the words a space apart, laid out "
                                                         f"{(x1 - x0) / (o1 - o0):.2f} of the original's width")
                check(w["x"] + x0 >= 1 and w["x"] + x1 <= 319,
                      f"{text} (size {size}): on the screen with its outline ({w['x'] + x0:.1f}..{w['x'] + x1:.1f})")
        g.call("ui_text", style="status", size=1.0)

        # screenshots: rood.classic's and the game's (rood.classic off) at 16:9
        def more(field, n=2):  # (the frame shown is the one drawn before: a couple more drawn first)
            start = stats()[field]
            g.until(lambda: stats()[field] >= start + n, guard=30)

        show({"timing": "success", "variant": 5})
        laid_out("WELL TIMED!", lambda w: w["typeset"] != 0)
        more("drawn")
        g.present_screenshot(shots / "well_timed_lua.png")
        g.call("lua", action="enable", plugin="rood.classic", enabled=False)
        laid_out("WELL TIMED!", lambda w: w["typeset"] == 0)
        more("fallback")
        g.present_screenshot(shots / "well_timed_game.png")
        g.call("lua", action="enable", plugin="rood.classic", enabled=True)
        laid_out("WELL TIMED!", lambda w: w["typeset"] != 0)
        same = images_equal(shots / "well_timed_lua.png", shots / "well_timed_game.png")
        check(same is None or same, f"WELL TIMED! drawn by rood.classic looks as the game's own (16:9; shots in "
                                    f"{shots})")

        # hot reload: blanks "each" (a space per blank), then the file's own values
        base = laid_out("WELL TIMED!", lambda w: w["typeset"] != 0)
        width = base["extent"][2] - base["extent"][0]
        loops = []
        for i in range(3):
            for text, wider in ((original.replace('blanks = "one"', 'blanks = "each"'), True), (original, False)):
                assert text != original or not wider
                at = g.status()["vsync"]
                start = time.monotonic()
                main_lua.write_text(text)
                key = "WELL   TIMED!" if wider else "WELL TIMED!"
                w = laid_out(key, lambda w: w["typeset"] != 0 and ((w["extent"][2] - w["extent"][0]) > width + 4)
                             == wider, guard=30, after=at)
                loops.append(time.monotonic() - start)
                if not check(w is not None, f"hot reload {i + 1}: blanks {'each' if wider else 'one'} laid out "
                                            f"({w and (w['extent'][2] - w['extent'][0]):.1f} vs {width:.1f} pixels)"):
                    break
        if loops:
            print(f"hot reload: {len(loops)} edits, save to the frame showing it: mean {statistics.mean(loops):.2f} s, "
                  f"max {max(loops):.2f} s")
            check(max(loops) < 2.0, f"hot reload under 2 s (max {max(loops):.2f} s)")

        # the digits' typography is rood.classic's too: its descender lift edited, then its own again (the
        # setting's line unchanged: the edit is the plugin's, for this run)
        def lift():
            return g.call("ui_text")["tunables"]["descender_lift"]
        setting = g.call("settings", get="ui_text_descender_lift")
        start = time.monotonic()
        main_lua.write_text(original.replace('digits = { descender_lift = S("ui_text_descender_lift") }',
                                             "digits = { descender_lift = 0 }"))
        ok = g.until(lambda: lift() == 0, guard=30)
        took = time.monotonic() - start
        check(bool(ok) and g.call("settings", get="ui_text_descender_lift") == setting,
              f"digits: look.typography.digits.descender_lift 0 applied in {took:.2f} s (the setting's line "
              f"unchanged: {setting})")
        main_lua.write_text(original)
        check(bool(g.until(lambda: lift() == 1, guard=30)), "digits: the file's descender lift again (1)")

        # the same edit with the game running in real time (as when playing): the save to the first frame
        # ui_text sets with it, polled
        realtime = []
        g.pause(False)
        try:
            for i in range(3):
                for text, wider in ((original.replace('blanks = "one"', 'blanks = "each"'), True), (original, False)):
                    at = g.status()["vsync"]
                    start = time.monotonic()
                    main_lua.write_text(text)
                    key = "WELL   TIMED!" if wider else "WELL TIMED!"
                    deadline = Deadline(10)
                    while not deadline.passed():
                        w = words().get(key)
                        if w and w["vsync"] > at and ((w["extent"][2] - w["extent"][0]) > width + 4) == wider:
                            realtime.append(time.monotonic() - start)
                            break
                        time.sleep(0.005)
        finally:
            g.pause()
        if check(len(realtime) == 6, f"hot reload in real time: {len(realtime)} of 6 edits seen"):
            print(f"hot reload, real time: save to the frame showing it: mean {statistics.mean(realtime):.2f} s, "
                  f"max {max(realtime):.2f} s ({', '.join(f'{t:.2f}' for t in realtime)})")
            check(max(realtime) < 2.0, f"hot reload in real time under 2 s (max {max(realtime):.2f} s)")

        # the REPL: a live edit of the look, then the file's value back on reload
        before = laid_out("WELL TIMED!", lambda w: w["typeset"] != 0)
        left = before["x"] + before["extent"][0]
        reply = g.repl(op="eval", plugin="rood.classic", code="M.look.typeset.margin = 12 return true")
        moved = laid_out("WELL TIMED!", lambda w: w["x"] + w["extent"][0] >= left + 6)
        check(moved is not None, f"REPL: the margin raised live moves the word from the edge ({left:.1f} -> "
                                 f"{moved['x'] + moved['extent'][0] if moved else None}; {reply})")
        g.call("lua", action="reload", plugin="rood.classic")
        back = laid_out("WELL TIMED!", lambda w: abs(w["x"] + w["extent"][0] - left) < 0.5)
        check(back is not None, "REPL: the file's margin again after a reload")

        # 16:9 placement is data: an undeclared dx is a parity mismatch (xy), the file's values parity again
        g.call("classic", parity=True, reset=True)
        main_lua.write_text(original.replace("place = { anchor = \"left\", dx = 0, dy = 0 }, -- 16:9",
                                             "place = { anchor = \"left\", dx = 10, dy = 0 }, -- 16:9", 1))
        g.until(lambda: g.call("classic")["kinds"]["status"]["mismatched"] >= 2, guard=30)
        c = g.call("classic")["kinds"]["status"]
        check(c["mismatched"] >= 2 and "xy" in c["fields"] and c.get("first", {}).get("field") == "xy",
              f"place.dx 10: the gate reports xy ({c['fields']}, first {json.dumps(c.get('first', {}).get('field'))})")
        main_lua.write_text(original)
        g.until(lambda: g.call("classic")["kinds"]["status"]["matched"] >= c["matched"] + 2, guard=30)
        c2 = g.call("classic")["kinds"]["status"]
        check(c2["matched"] >= c["matched"] + 2, f"the file's place: parity again ({c2['matched'] - c['matched']} "
                                                 f"frames matched)")
        g.call("classic", parity=False)
        g.call("hud", action="clear")
    finally:
        main_lua.write_text(original)
        g.close()
    print(f"{'PASS' if not failures else 'FAIL'}: classic HUD ({len(failures)} failures)")
    return 1 if failures else 0


def images_equal(a, b):
    """Whether two screenshots show the same HUD (their top-left: the status panel and the word; the
    save circle's glow below animates on its own); None without PIL/numpy."""
    try:
        import numpy as np
        from PIL import Image
    except ImportError:
        return None
    x = np.asarray(Image.open(a).convert("RGB")).astype(int)
    y = np.asarray(Image.open(b).convert("RGB")).astype(int)
    if x.shape != y.shape:
        return False
    h, w = x.shape[:2]
    return float(np.abs(x[:h * 2 // 5, :w * 2 // 5] - y[:h * 2 // 5, :w * 2 // 5]).mean()) < 0.05


if __name__ == "__main__":
    sys.exit(main())
