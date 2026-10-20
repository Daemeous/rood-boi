#!/usr/bin/env python3
"""The Lua visible layer (pc/src/hud, lua_api_hud.cpp; plan
pc/docs/plans/2026-10-18-lua-visible-layer.md): the battle's status
word summoned and held, claimed by a plugin, hot-reloaded, and the game's own
drawing as the fallback; the classic flavour; then the shipped rood.hud plugin
on every kind (status, chain, buff, damage, travel) with the performance
budget; the queries, the name window and the events; the gallery.

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
  - the classic flavour (hud_classic_override 1): the claim draws at the
    game's hook as packets (the game's printers: ui_text restyles the word),
    every classic primitive, the game's font measured; the primitives refuse
    in the modern flavour; a classic draw error gives the game's drawing;
  - rood.dev.clear lets it go.
rood.hud (shipped, on by default), the test plugin off:
  - each kind summoned (rood.dev.show's spawners: the game's own records)
    with its fields, claimed, suppressed and drawn (its text in the frame's
    commands); the Travel prompt's C++ drawing skipped;
  - all five held at once: the pass's cost (mean under 0.25 ms, p99 under
    1 ms), no dropped commands, no allocations, memory flat; the four game
    kinds in the classic flavour: under 0.1 ms of Lua a game frame;
  - rood.hud's own Lua tests (pc/plugins/rood.hud/tests) in the same game;
  - an edit of its look on disk redrawn on the held word (the loop timed);
  - rood.hud off: the game's (and the C++ prompt's) own drawing of each kind;
  - rood.hud.typography: the status style's fields for this run, its
    settings line unchanged, then the settings' again.
Before them rood.game (task 7) against RPC state, and the name window (task
9): the game's own call, held, its hud.name_window event once, cleared, a
show without hold closed after its life; a damage number's hud.damage event.
Last the gallery (task 10, ROOD_BENCH in the output folder): a held word
saved, listed, the moment changed, loaded (the process restarts resuming it:
the word held again, the seconds printed); a stale entry refused.
No screenshots: counters only."""
import argparse
import os
import pathlib
import random
import shutil
import statistics
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Deadline, Game, RpcError, awake, listening  # noqa: E402
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
           "ROOD_PLUGINS_SHIPPED": str(shipped), "ROOD_PLUGIN_DATA": str(out / "plugin_data"),
           "ROOD_BENCH": str(out / "bench")}
    shutil.rmtree(out / "bench", ignore_errors=True)
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

        def wait(condition, step=2):
            """Run the game a little at a time until condition() (real-time menus and reloads: no vsync
            counts); the guard only bounds a hang (60 awake seconds: a stopped game's pause not counted)."""
            return g.until(condition, step=step, guard=60)

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
        queries(g, check)
        name_window(g, check)
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
            # (the loop's time in awake seconds: a pause of the stopped game is not the reload's)
            t0 = awake()
            (folder / "main.lua").write_text(draw_code(tag))
            seen = False
            while awake() - t0 < 30:  # (a hang guard)
                if any((c.get("text") or "").endswith(tag) for c in stats()["last"]):
                    seen = True
                    break
                time.sleep(0.01)
            loops.append(awake() - t0)
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
        classic(g, folder, check, wait, stats, status, glyphs)
        g.call("hud", action="set", classic_override=0)
        (folder / "main.lua").write_text(draw_code("v10"))
        check(wait(lambda: status()["claimed_by"] == PLUGIN and not status()["claim_failed"]), "v10 claims again")
        g.call("ui_menu", modern=True)
        check(wait(lambda: status()["honoured"]), "modern look again")
        # cleared: it plays out and goes
        g.lua('rood.dev.clear("status")')
        check(wait(lambda: not held(), step=10), f"cleared: the word goes ({held()})")
        shipped_hud(g, shipped / "rood.hud", check, wait, stats, glyphs)
        g = gallery(g, out / "bench", check)
        log = (out / "game.log").read_text(errors="replace")
        check("[crash]" not in log, "no crash in the log")
    finally:
        g.close()
    print(f"{len(failures)} failure(s)" if failures else "all passed")
    return 1 if failures else 0


CLASSIC_CODE = """
local d = rood.draw
local word = d.style{ font = 'status', align = 'center', anchor = 'middle' }
local menu = d.style{ font = 'menu' }
errors, widths = {}, {}
rood.hud.claim('status', function(e, d)
  if e.flavour ~= 'classic' then
    local ok, err = pcall(d.window, 10, 10, 50, 20)
    errors[1] = err
    return
  end
  d.window(20, 120, 140, 34)
  d.gradient(26, 126, 40, 8, 0xFF0000, 0x0000FF)
  d.rect(70, 126, 10, 8, 0x00FF00, 0.5)
  d.glyph('circle', 26, 146, 16)
  d.cursor(4, 138)
  d.bar(84, 140, 30, 100, 'hp')
  d.sprite(140, 126, { page_x = 768, page_y = 0, bits = 4, u = 0, v = 128, w = 16, h = 16, clut_x = 944,
                       clut_y = 223 })
  d.text(menu, 'Ashley v11', 84, 124)
  d.text(word, e.text .. ' v11', e.x, e.y)
  widths[1], widths[2] = d.measure(word, 'SLOW!'), d.glyph_width('circle', 8)
end)
"""


def classic(g, folder, check, wait, stats, status, glyphs):
    """The classic flavour (task 8): with the classic look and hud_classic_override the claim runs at the game's
    hook, in the game's frame, and draws PS1 packets (its text through the game's printers: the status style's
    word restyled by ui_text as SLOW! is); nothing goes to the modern pass."""
    g.run(4)
    s, before, f0 = status(), glyphs(), stats()["classic_frames"]
    g.run(8)
    t, st, after = status(), stats(), glyphs()
    texts = [c.get("text") for c in st["classic_last"]]
    check(t["suppressed"] > s["suppressed"] and t["drawn"] > s["drawn"] and t["fallback"] == s["fallback"]
          and st["classic_frames"] > f0 and any(x and x.endswith("v10") for x in texts) and after > before
          and not st["last"],
          f"classic flavour: claimed at the hook and drawn as packets (suppressed {t['suppressed'] - s['suppressed']}, "
          f"drawn {t['drawn'] - s['drawn']}, classic frames {st['classic_frames'] - f0}, restyled glyphs "
          f"{after - before}, commands {texts}, modern {len(st['last'])})")
    (folder / "main.lua").write_text(CLASSIC_CODE)
    check(wait(lambda: any(c["type"] == "window" for c in stats()["classic_last"])), "the classic primitives' claim")
    g.run(4)
    st = stats()
    types = [c["type"] for c in st["classic_last"]]
    check(types == ["window", "gradient", "rect", "glyph", "cursor", "bar", "sprite", "text", "text"]
          and st["dropped"] == 0,
          f"classic primitives: window, gradient, rect, glyph, cursor, bar, sprite, menu and status text ({types})")
    w = g.lua("return widths[1], widths[2]", plugin=PLUGIN)["results"] + [None, None]
    check(w[0] and w[0] == int(w[0]) and 20 <= w[0] <= 40 and w[1] == 16,
          f"classic measures: the game's font (SLOW! {w[0]} pixels), its button sprites (16: {w[1]})")
    g.call("hud", action="set", classic_override=0)
    g.call("ui_menu", modern=True)
    check(wait(lambda: status()["honoured"]), "modern look again")
    check(wait(lambda: g.lua("return errors[1]", plugin=PLUGIN)["results"][0] is not None), "the modern pass ran the claim")
    e = g.lua("return errors[1]", plugin=PLUGIN)["results"][0] or ""
    check("the classic flavour only" in e, f"modern flavour: the classic primitives refuse ({e.splitlines()[0]})")
    g.call("ui_menu", modern=False)
    g.call("hud", action="set", classic_override=1)
    (folder / "main.lua").write_text(draw_code("broken", "error('a broken classic draw')"))
    check(wait(lambda: status()["claim_failed"]), f"a classic draw error stops the claim ({status()})")
    g.run(4)
    s, before = status(), glyphs()
    g.run(8)
    t = status()
    check(t["fallback"] > s["fallback"] and t["suppressed"] == s["suppressed"] and glyphs() > before,
          f"a classic draw error: the game draws the word (fallback {t['fallback'] - s['fallback']})")


def gallery(g, bench, check):
    """The gallery (task 10): a held moment saved (the game's save state and a Lua table of the held moments),
    listed, the moment changed, then loaded: the process restarts resuming it (same socket and cards) and holds
    the word again; a stale entry is listed as such and refused. Returns the game's new handle."""
    def live(kind):
        return [e for e in g.call("hud", action="list")["elements"] if e["live"] and e["kind"] == kind]

    g.call("hud", action="clear")
    g.call("hud", action="show", kind="status", timing="slow")
    g.run(10)
    r = g.lua('return rood.dev.bench.save("slow-word")')
    check(not r.get("error") and str(r["results"][0]).endswith("slow-word.snap"), f"bench.save ({r})")
    g.run(2)
    entries = g.lua("return rood.dev.bench.list()")["results"][0] or []
    e = next((x for x in entries if x["name"] == "slow-word"), {})
    st = g.call("state")
    check(e.get("valid") and e.get("held") == ["status"] and e.get("zone") == st["zone"] and e.get("room") == st["room"]
          and (bench / "slow-word.snap").stat().st_size > 0 and (bench / "slow-word.lua").exists(),
          f"listed: valid, the held word, the room ({e})")
    text = (bench / "slow-word.lua").read_text()
    check(text.lstrip().startswith("--") and "return {" in text and "kind = \"status\"" in text,
          "the entry is a Lua table")
    # the moment changes, then the gallery brings it back
    g.call("hud", action="clear")
    g.call("hud", action="show", kind="chain", count=4)
    g.run(30)
    check(not live("status") and live("chain"), "the moment changed (a chain, no word)")
    old = os.stat(g.path).st_ino
    t0 = awake()
    r = g.lua('return rood.dev.bench.load("slow-word")')
    check(not r.get("error"), f"bench.load ({r.get('error')})")
    g.file.close()
    g.sock.close()
    process = g.processes[0]
    gone = False  # (the old socket unlinked: the new one may reuse its inode)
    guard = Deadline(120)  # a hang guard (awake seconds)
    while not guard.passed():
        try:
            gone = gone or os.stat(g.path).st_ino != old
        except OSError:
            gone = True
        if gone and listening(g.path):
            break
        if process.poll() is not None:
            raise RpcError(f"the game exited (status {process.returncode})")
        time.sleep(0.02)
    g = Game(g.path, g.processes)
    seconds = awake() - t0
    g.run(6)
    words = live("status")
    check(len(words) == 1 and words[0]["text"] == "SLOW!" and words[0]["held"] and not words[0]["synthetic"]
          and not live("chain"),
          f"loaded in {seconds:.2f} s (load -> the game's socket back): the word held again, no chain ({words})")
    g.run(120)
    words = live("status")
    check(len(words) == 1 and words[0]["age"] == 10, f"held past its life after the load ({words})")
    cur = g.lua("return rood.dev.bench.current()")["results"]
    check(cur == ["slow-word"], f"bench.current ({cur})")
    print(f"gallery load (bench.load -> socket back, the held word resumed): {seconds:.2f} s", flush=True)
    # a stale entry: its save state not this build's
    (bench / "old.lua").write_text("return { name = 'old', held = {} }\n")
    data = bytearray((bench / "slow-word.snap").read_bytes())
    data[16] ^= 0xFF  # the header's code key (snapshot.c), as a game-code rebuild changes it
    (bench / "old.snap").write_bytes(bytes(data))
    lst = g.call("hud", action="bench", op="list")["entries"]
    old_e = next((x for x in lst if x["name"] == "old"), {})
    check(old_e.get("valid") is False and "stale" in old_e.get("why", ""), f"a stale entry is listed as such ({old_e})")
    e = g.lua('return rood.dev.bench.load("old")').get("error") or ""
    check("stale" in e, f"and refused ({e.splitlines()[0] if e else e})")
    e = g.lua('return rood.dev.bench.save("Bad Name")').get("error") or ""
    check("a name is" in e, "names are checked")
    g.call("hud", action="clear")
    return g


def name_window(g, check):
    """The name window (task 9): rood.dev.show calls the game's vs_battle_displaySceneMessage (its hook gives
    the message and the action); a hud.name_window event once per appearance; held until cleared, else closed
    after its life; never claimed. And hud.<kind> events for the drawn kinds (a damage number)."""
    def windows():
        return [e for e in g.call("hud", action="list")["elements"] if e["live"] and e["kind"] == "name_window"]

    def until(condition, vsyncs=400, step=4):
        for _ in range(0, vsyncs, step):
            if condition():
                return True
            g.run(step)
        return condition()

    r = g.call("lua", action="console", input="names, hits = {}, {} "
               "rood.on('hud.name_window', function(e) names[#names + 1] = e.text .. '#' .. e.index end) "
               "rood.on('hud.damage', function(e) hits[#hits + 1] = e.value end)")
    check(not r.get("error"), f"event handlers ({r.get('error')})")
    g.lua('rood.dev.show("name_window")')
    until(lambda: windows())
    w = windows()
    check(len(w) == 1 and w[0]["text"] and w[0]["held"] and not w[0]["synthetic"] and w[0]["value"] == 184
          and w[0]["variant"] == 11 and w[0]["w"] == 320,
          f"the game's name window from its own call, held ({[(e['text'], e['value'], e['h'], e['y']) for e in w]})")
    g.run(240)
    names = g.lua("return #names, names[1]")["results"]
    check(len(windows()) == 1 and names[0] == 1 and names[1] == f"{w[0]['text'] if w else ''}#184",
          f"held 240 vsyncs, one hud.name_window event ({names})")
    e = g.lua('rood.hud.claim("name_window", function() end)').get("error") or ""
    check("is an event" in e, f"the name window is not claimed ({e.splitlines()[0] if e else e})")
    g.lua('rood.dev.clear("name_window")')
    check(until(lambda: not windows()), "cleared: the window closes")
    g.lua('rood.dev.show("name_window", { index = 185, hold = false })')
    check(until(lambda: windows()) and until(lambda: not windows(), vsyncs=600),
          "a show without hold opens it and closes it after its life")
    names = g.lua("return #names")["results"]
    check(names == [2], f"a second appearance, a second event ({names})")
    g.call("hud", action="show", kind="damage", value=77, hold=False)
    check(until(lambda: g.lua("return hits[1]")["results"] == [77], vsyncs=60), "hud.damage: the number's event")
    g.call("hud", action="clear")


def queries(g, check):
    """rood.game (task 7): Ashley, the room, the mode as the game has them (RPC state)."""
    st = g.call("state")
    r = g.lua("local a, r = rood.game.ashley(), rood.game.room() return a.hp, a.max_hp, a.mp, a.max_mp, a.risk,"
              " a.position.x, a.position.z, #a.statuses, a.battle, r.zone, r.room, r.name, rood.game.mode(),"
              " rood.game.first_person(), rood.game.ashley() == a")["results"]
    want = [st["hp"], st["max_hp"], st["mp"], st["max_mp"], st["risk"], st["player"]["x"], st["player"]["z"]]
    check(r[:7] == want and r[7] == 0 and r[8] is False and r[9:11] == [st["zone"], st["room"]] and r[11]
          and r[12] == "field" and r[13] is False and r[14] is True,
          f"rood.game: Ashley, the room, the mode as the game has them ({r}; state {want}, "
          f"{st['zone']}/{st['room']})")
    e = g.lua("rood.game.ashley().hp = 1").get("error") or ""
    check("read-only" in e, f"rood.game's tables are read-only ({e.splitlines()[0] if e else e})")


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
        found = wait(lambda: any(e["text"] == text and e["held"] for e in elements(k)))
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
    # the budget (plan "Performance"): at most 0.25 ms of Lua per presented frame for the whole HUD, p99 under 1 ms
    check(s["frames"] > 100 and s["mean_ms"] < 0.25 and s["p99_ms"] < 1.0 and s["dropped"] == 0,
          f"rood.hud, five elements: {s['frames']} frames, mean {s['mean_ms']:.3f} ms (budget 0.25), p99 "
          f"{s['p99_ms']:.3f} ms (1), max {s['max_ms']:.3f} ms of Lua; drawing {s['render_mean_ms']:.3f} ms; "
          f"{s['commands']} commands; {s['allocations_per_frame']:.1f} allocations a frame")
    check(abs(s["claim_memory_kb"] - before) < 64 and s["allocations_per_frame"] < 4,
          f"rood.hud's memory flat: {before:.0f} KB then {s['claim_memory_kb']:.0f} KB after 240 vsyncs, "
          f"{s['allocations_per_frame']:.1f} allocations a frame")
    # the classic flavour: the four game kinds drawn at their hooks, at most 0.1 ms of Lua per game frame
    g.call("ui_menu", modern=False)
    g.call("hud", action="set", classic_override=1)
    g.call("hud", action="clear")
    for k, args, *_ in SHOWS[:4]:
        g.call("hud", action="show", kind=k, **args)
    check(wait(lambda: all(elements(k) for k in kinds[:4])), "four game kinds held, the classic look")
    g.call("hud", action="stats", reset=True)
    g.run(240)
    s = stats()
    check(s["classic_frames"] > 50 and s["classic_mean_ms"] < 0.1 and s["classic_p99_ms"] < 1.0 and s["frames"] == 0,
          f"rood.hud, classic flavour: {s['classic_frames']} game frames, mean {s['classic_mean_ms']:.3f} ms "
          f"(budget 0.1), p99 {s['classic_p99_ms']:.3f} ms, max {s['classic_max_ms']:.3f} ms of Lua; "
          f"{s['classic_allocations_per_frame']:.1f} allocations a frame; the presented frames' pass idle")
    g.call("hud", action="set", classic_override=0)
    g.call("ui_menu", modern=True)
    check(wait(lambda: kind("status")["honoured"]), "modern look again")
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
        t0 = awake()  # (awake seconds: a pause of the stopped game is not the reload's)
        main.write_text(source.replace("nudge = 0.5,", f"nudge = {nudge},"))
        seen = False
        while awake() - t0 < 30:  # (a hang guard)
            if any(c.get("text") == "SLOW!" and abs(c["y"] - (53 + nudge)) < 0.01 for c in stats()["last"]):
                seen = True
                break
            time.sleep(0.01)
        loops.append(awake() - t0)
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
