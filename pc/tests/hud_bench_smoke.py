#!/usr/bin/env python3
"""The Lua visible layer (pc/src/hud, lua_api_hud.cpp; plan
pc/docs/plans/2026-10-18-lua-visible-layer.md tasks 1-6): the battle's status
word summoned and held, claimed by a plugin, hot-reloaded, and the game's own
drawing as the fallback; then the shipped rood.hud plugin on every kind
(status, chain, buff, damage, travel).

    python3 pc/tests/hud_bench_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

One game from the save_room checkpoint (a copy of the card inside
card_guard), the modern look (ui_menus 1), ui_text font (its status glyph
counter tells when the game draws the word), a test plugin in its own
ROOD_PLUGINS folder, rood.hud from a copy of pc/plugins (so the test can edit
it) turned off until the second part:
  - rood.dev.show("status", {timing = "slow"}) from the Lua console calls
    the game's spawner: SLOW! appears, held past its 46-frame life; with no
    claim the game draws it (status glyphs grow);
  - the plugin claims "status": the element is suppressed (the glyph counter
    stays), the claim draws it (RPC hud {stats}: drawn, its text in the last
    frame's commands), at a fraction of a millisecond, memory flat;
  - the plugin's file edited on disk while the game is paused: the held word
    is redrawn by the new code without a restart (the loop time printed);
  - a draw that raises, the plugin disabled, the classic look (ui_menus 0,
    hud_classic_override 0): the game draws the word again each time;
  - rood.dev.clear lets it go.
rood.hud (shipped, on by default), the test plugin off:
  - each kind summoned (rood.dev.show's spawners: the game's own records)
    with its fields, claimed, suppressed and drawn (its text in the frame's
    commands); the Travel prompt's C++ drawing skipped;
  - all five held at once: the pass's cost (p99 under 1 ms), no dropped
    commands, memory flat;
  - rood.hud's own Lua tests (pc/plugins/rood.hud/tests) in the same game;
  - an edit of its look on disk redrawn on the held word (the loop timed);
  - rood.hud off: the game's (and the C++ prompt's) own drawing of each kind;
  - rood.hud.typography: the status style's fields for this run, its
    settings line unchanged, then the settings' again.
No screenshots: counters only."""
import argparse
import pathlib
import random
import shutil
import statistics
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402

PLUGIN = "test.hudbench"
MANIFEST = f"return {{ id = '{PLUGIN}', name = 'HUD bench test', api_version = 1 }}\n"


def draw_code(tag, extra=""):
    """A claim drawing the word with a stroke, then its tag (the edit the test makes)."""
    return (
        "local d = rood.draw\n"
        "local word = d.style{ font = 'status', size = 13, stroke = 1.5, stroke_color = 0x101010,"
        " align = 'center', anchor = 'middle' }\n"
        f"rood.hud.claim('status', function(e, d)\n"
        f"  {extra}\n"
        f"  d.text(word, e.text .. ' {tag}', e.x, e.y, e.rgb)\n"
        "end)\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="logs and the test plugin's folder")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "hud_bench"
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

    plugins = out / "plugins"
    folder = plugins / PLUGIN
    if folder.exists():
        for p in sorted(folder.rglob("*"), reverse=True):
            p.unlink() if p.is_file() else p.rmdir()
    plugins.mkdir(parents=True, exist_ok=True)
    shipped = out / "shipped"
    shutil.rmtree(shipped, ignore_errors=True)
    shutil.copytree(ROOT / "pc/plugins/rood.hud", shipped / "rood.hud")
    settings = out / "settings.cfg"
    settings.write_text("aspect 4:3\nui_menus 1\nui_text font\nplugin_enabled rood.hud 0\n")
    env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(plugins),
           "ROOD_PLUGINS_SHIPPED": str(shipped), "ROOD_PLUGIN_DATA": str(out / "plugin_data")}
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        def stats():
            return g.call("hud", action="stats")

        def status():
            return stats()["kinds"]["status"]

        def glyphs():
            return g.call("ui_text")["status_glyphs"]

        def held():
            return [e for e in g.call("hud", action="list")["elements"] if e["live"] and e["kind"] == "status"]

        def wait(condition, timeout=10.0, step=2):
            """Run the game a little at a time until condition() (real-time menus and reloads: no vsync counts)."""
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                if condition():
                    return True
                g.run(step)
            return condition()

        def game_draws(what):
            """The game drew the word itself over 8 vsyncs (its glyphs counted; nothing suppressed)."""
            g.run(4)
            before, s = glyphs(), status()
            g.run(8)
            after, t = glyphs(), status()
            check(after > before and t["suppressed"] == s["suppressed"] and t["fallback"] > s["fallback"],
                  f"{what}: the game draws the word ({after - before} glyphs, suppressed "
                  f"{t['suppressed'] - s['suppressed']}, fallback {t['fallback'] - s['fallback']})")

        def claim_draws(what, tag):
            g.run(4)  # (the frame built before the claim took over is still drawn by the game)
            before, s = glyphs(), status()
            g.run(8)
            after, t = glyphs(), status()
            last = [c.get("text") for c in stats()["last"]]
            check(after == before and t["suppressed"] > s["suppressed"] and t["drawn"] > s["drawn"]
                  and any(x and x.endswith(tag) for x in last),
                  f"{what}: claimed, suppressed and drawn ({after - before} game glyphs, suppressed "
                  f"{t['suppressed'] - s['suppressed']}, drawn {t['drawn'] - s['drawn']}, commands {last})")

        g.run(4)
        check(g.call("ui_menu")["modern"] if "modern" in g.call("ui_menu") else True, "the modern look (ui_menus 1)")
        dev = g.lua("return type(rood.dev)")
        check(dev["results"] == ["table"], f"rood.dev exists in dev mode (RPC on): {dev['results']}")
        # summoned and held from the console, the game's own drawing (no claim)
        r = g.call("lua", action="console", input='rood.dev.show("status", { timing = "slow" })')
        check(r.get("ok", True) and not r.get("error"), f"rood.dev.show from the console ({r.get('error')})")
        g.run(6)
        words = held()
        if not check(len(words) == 1 and words[0]["text"] == "SLOW!" and words[0]["held"]
                     and not words[0]["synthetic"], f"SLOW! from the game's spawner, held ({words})"):
            return 1
        first_id = words[0]["id"]
        game_draws("no claim")
        g.run(120)  # past its life (46 frames)
        words = held()
        check(len(words) == 1 and words[0]["id"] == first_id and words[0]["age"] == 10,
              f"held past its life: the same element at age 10 ({words})")
        # a plugin claims it
        (folder / "plugin.lua").parent.mkdir(parents=True, exist_ok=True)
        (folder / "plugin.lua").write_text(MANIFEST)
        (folder / "main.lua").write_text(draw_code("v1"))
        g.call("lua", action="rescan")
        check(wait(lambda: status()["claimed_by"] == PLUGIN), f"the plugin claims status ({status()})")
        claim_draws("claimed", "v1")
        g.call("hud", action="stats", reset=True)
        g.run(240)
        s, before = stats(), stats()["claim_memory_kb"]
        g.run(240)
        s = stats()
        check(s["frames"] > 100 and s["p99_ms"] < 1.0,
              f"the pass: {s['frames']} frames, mean {s['mean_ms']:.3f} ms, p99 {s['p99_ms']:.3f} ms, "
              f"max {s['max_ms']:.3f} ms of Lua; drawing {s['render_mean_ms']:.3f} ms")
        check(abs(s["claim_memory_kb"] - before) < 64,
              f"memory flat: {before:.0f} KB then {s['claim_memory_kb']:.0f} KB after 240 vsyncs")
        check(s["dropped"] == 0 and s["styles"] == 1, f"no dropped commands, one style ({s['dropped']}, {s['styles']})")
        # hot reload onto the held word, the game paused: saved -> redrawn
        loops = []
        rng = random.Random(7)
        for k in range(2, 10):
            tag = f"v{k}"
            time.sleep(rng.uniform(0, 0.6))  # saved at any moment of the hot reload check's half second
            t0 = time.monotonic()
            (folder / "main.lua").write_text(draw_code(tag))
            seen = False
            while time.monotonic() - t0 < 10:
                if any((c.get("text") or "").endswith(tag) for c in stats()["last"]):
                    seen = True
                    break
                time.sleep(0.01)
            loops.append(time.monotonic() - t0)
            check(seen, f"edit {tag}: the held word redrawn by the new code in {loops[-1]:.3f} s (paused)")
        print(f"hot reload loop (save -> held word redrawn), paused game: mean {statistics.mean(loops):.3f} s, "
              f"max {max(loops):.3f} s over {len(loops)} edits", flush=True)
        check(max(loops) < 1.5, f"the loop within about a second ({max(loops):.3f} s)")
        words = held()
        check(len(words) == 1 and words[0]["id"] == first_id, f"the held moment survives the reloads ({words})")
        # a draw that raises: the game's own drawing
        (folder / "main.lua").write_text(draw_code("broken", "error('a broken draw')"))
        check(wait(lambda: status()["claim_failed"]), f"a draw error stops the claim ({status()})")
        game_draws("draw error")
        (folder / "main.lua").write_text(draw_code("v10"))
        check(wait(lambda: status()["claimed_by"] == PLUGIN and not status()["claim_failed"]),
              "fixed: claimed again")
        claim_draws("fixed", "v10")
        # the plugin disabled: the game's own drawing
        g.call("lua", action="enable", plugin=PLUGIN, enabled=False)
        check(wait(lambda: status()["claimed_by"] is None), "disabled: no claim")
        game_draws("plugin off")
        g.call("lua", action="enable", plugin=PLUGIN, enabled=True)
        check(wait(lambda: status()["claimed_by"] == PLUGIN), "enabled: claimed again")
        claim_draws("enabled", "v10")
        # the classic look: claims are not honoured (typography only) unless hud_classic_override
        g.call("ui_menu", modern=False)
        check(wait(lambda: not status()["honoured"]), "classic look: the claim is not honoured")
        game_draws("classic look")
        g.call("hud", action="set", classic_override=1)
        claim_draws("classic look, hud_classic_override 1", "v10")
        g.call("hud", action="set", classic_override=0)
        g.call("ui_menu", modern=True)
        check(wait(lambda: status()["honoured"]), "modern look again")
        # cleared: it plays out and goes
        g.lua('rood.dev.clear("status")')
        check(wait(lambda: not held(), timeout=10, step=10), f"cleared: the word goes ({held()})")
        shipped_hud(g, shipped / "rood.hud", check, wait, stats, glyphs)
        log = (out / "game.log").read_text(errors="replace")
        check("[crash]" not in log, "no crash in the log")
    finally:
        g.close()
    print(f"{len(failures)} failure(s)" if failures else "all passed")
    return 1 if failures else 0


# rood.dev.show's args per kind, the element's text, fields it must have.
SHOWS = [
    ("status", {"timing": "slow"}, "SLOW!", {"timing": "slow", "anchor": 1}),
    ("chain", {"count": 3}, "3 CHAINS", {"value": 3, "life": 91}),
    ("buff", {"slot": "weapon", "stat": "human", "value": 2}, "WEAPON HUMAN +2",
     {"value": 2, "sign": 1, "slot": 0, "stat": 0}),
    ("damage", {"value": 123}, "123", {"value": 123, "sign": -1, "pool": "hp", "anchor": 0}),
    ("travel", {}, "TRAVEL", {"anchor": 3}),
]
# What the claim's commands show of each (the travel: its verb; a buff: its words, each a command).
DRAWN = {"status": "SLOW!", "chain": "3 CHAINS", "buff": "WEAPON", "damage": "3", "travel": "TRAVEL"}


def shipped_hud(g, folder, check, wait, stats, glyphs):
    """rood.hud on every kind (tasks 4-6), the test plugin off."""
    def kind(k):
        return stats()["kinds"][k]

    def elements(k):
        return [e for e in g.call("hud", action="list")["elements"] if e["live"] and e["kind"] == k]

    def prompt_drawn():
        return g.call("fast_travel")["prompt_drawn"]

    def texts():
        return [c.get("text") for c in stats()["last"]]

    g.call("lua", action="enable", plugin=PLUGIN, enabled=False)
    g.call("lua", action="enable", plugin="rood.hud", enabled=True)
    kinds = [k for k, *_ in SHOWS]
    check(wait(lambda: all(kind(k)["claimed_by"] == "rood.hud" for k in kinds)),
          f"rood.hud claims every kind ({[kind(k)['claimed_by'] for k in kinds]})")
    # the Travel prompt's icon: a device with something bound to Travel (the pad's back)
    g.call("button_prompts", button_prompts="auto")
    g.call("button_prompts", device="playstation", variant=4)
    for k, args, text, fields in SHOWS:
        g.call("hud", action="clear")
        r = g.call("hud", action="show", kind=k, **args)
        found = wait(lambda: any(e["text"] == text and e["held"] for e in elements(k)), timeout=10)
        e = next((e for e in elements(k) if e["text"] == text), {})
        check(r.get("ok") and found and not e.get("synthetic", True)
              and all(e.get(f) == v for f, v in fields.items()),
              f"{k}: the game's element, held, its fields ({ {f: e.get(f) for f in ['text', *fields]} })")
        s, before, cpp = kind(k), glyphs(), prompt_drawn()
        ok = wait(lambda: kind(k)["drawn"] >= s["drawn"] + 4 and any(t and DRAWN[k] in t for t in texts()))
        t = kind(k)
        game_drawn = (glyphs() - before) if k in ("status", "chain") else (prompt_drawn() - cpp if k == "travel" else 0)
        check(ok and t["suppressed"] > s["suppressed"] and t["fallback"] == s["fallback"] and game_drawn == 0,
              f"{k}: claimed by rood.hud, suppressed and drawn (drawn {t['drawn'] - s['drawn']}, suppressed "
              f"{t['suppressed'] - s['suppressed']}, fallback {t['fallback'] - s['fallback']}, the game's own "
              f"{game_drawn}; commands {texts()[:6]})")
    if any(not e.get("synthetic") for e in elements("buff")):
        parts = elements("buff")[0]["parts"] if elements("buff") else []
        check([p["text"] for p in parts] == ["WEAPON", "HUMAN", "+", "2"] and all(p["w"] > 0 for p in parts),
              f"buff: its four boxes from the game's quads ({parts})")
    # all five at once: the pass's cost
    g.call("hud", action="clear")
    for k, args, *_ in SHOWS:
        g.call("hud", action="show", kind=k, **args)
    check(wait(lambda: all(elements(k) for k in kinds)), "all five held at once")
    g.call("hud", action="stats", reset=True)
    g.run(120)
    before = stats()["claim_memory_kb"]
    g.run(240)
    s = stats()
    check(s["frames"] > 100 and s["p99_ms"] < 1.0 and s["dropped"] == 0,
          f"rood.hud, five elements: {s['frames']} frames, mean {s['mean_ms']:.3f} ms, p99 {s['p99_ms']:.3f} ms, "
          f"max {s['max_ms']:.3f} ms of Lua; drawing {s['render_mean_ms']:.3f} ms; {s['commands']} commands")
    check(abs(s["claim_memory_kb"] - before) < 64,
          f"rood.hud's memory flat: {before:.0f} KB then {s['claim_memory_kb']:.0f} KB after 240 vsyncs")
    # its own Lua tests, in this game (the field, the modern look)
    g.call("hud", action="clear")
    run = g.lua_test(plugin="rood.hud")
    for r in run["results"]:
        print(f"      {r['status'].upper():<5} rood.hud {r['name']} ({r['ms']} ms)" + (
            f": {r['message']}" if r["status"] == "fail" else ""))
    check(run["failed"] == 0 and run["passed"] >= 8, f"rood.hud's Lua tests: {run['passed']} passed, "
          f"{run['failed']} failed, {run['skipped']} skipped")
    # an edit of its look on the held word: saved -> redrawn
    g.call("hud", action="show", kind="status", timing="slow")
    check(wait(lambda: any(c.get("text") == "SLOW!" for c in stats()["last"])), "SLOW! drawn by rood.hud")
    main = folder / "main.lua"
    source = main.read_text()
    loops = []
    for k in range(1, 6):
        nudge = 0.5 + k
        time.sleep(random.Random(k).uniform(0, 0.6))
        t0 = time.monotonic()
        main.write_text(source.replace("nudge = 0.5,", f"nudge = {nudge},"))
        seen = False
        while time.monotonic() - t0 < 10:
            if any(c.get("text") == "SLOW!" and abs(c["y"] - (53 + nudge)) < 0.01 for c in stats()["last"]):
                seen = True
                break
            time.sleep(0.01)
        loops.append(time.monotonic() - t0)
        check(seen, f"rood.hud edit {k}: the held word redrawn {nudge} pixels down in {loops[-1]:.3f} s (paused)")
    print(f"rood.hud's hot reload loop (save -> held word redrawn), paused game: mean {statistics.mean(loops):.3f} s, "
          f"max {max(loops):.3f} s over {len(loops)} edits", flush=True)
    main.write_text(source)
    # rood.hud off: the game's own drawing of each kind (and the C++ Travel prompt)
    g.call("lua", action="enable", plugin="rood.hud", enabled=False)
    check(wait(lambda: all(kind(k)["claimed_by"] is None for k in kinds)), "rood.hud off: no claims")
    for k, args, *_ in SHOWS:
        g.call("hud", action="clear")
        g.call("hud", action="show", kind=k, **args)
        g.run(6)
        s, before, cpp = kind(k), glyphs(), prompt_drawn()
        g.run(10)
        t = kind(k)
        own = (glyphs() - before) if k in ("status", "chain") else (prompt_drawn() - cpp if k == "travel" else 1)
        check(t["fallback"] > s["fallback"] and t["suppressed"] == s["suppressed"] and own > 0,
              f"{k}, rood.hud off: the game draws it (fallback {t['fallback'] - s['fallback']}, suppressed "
              f"{t['suppressed'] - s['suppressed']}, its own {own})")
    g.call("hud", action="clear")
    # typography (what the classic look may change): a ui_text style's fields for this run, never saved
    def status_style():
        return {st["name"]: st for st in g.call("ui_text")["styles"]}["status"]

    size, line = status_style()["size"], g.call("settings", get="ui_text_status_size").get("line")
    r = g.call("lua", action="console", input='rood.hud.typography("status", { size = 1.5, tracking = 0.1 })')
    st = status_style()
    check(not r.get("error") and st["size"] == 1.5 and abs(st["tracking"] - 0.1) < 1e-6
          and g.call("settings", get="ui_text_status_size").get("line") == line,
          f"typography: the status style overridden ({st['size']}, {st['tracking']:.3f}), the setting's line kept "
          f"({line!r}; {r.get('error')})")
    g.call("lua", action="console", input='rood.hud.typography("status", nil)')
    check(status_style()["size"] == size, f"typography: nil gives the settings' back ({status_style()['size']})")


if __name__ == "__main__":
    sys.exit(main())
