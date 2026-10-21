#!/usr/bin/env python3
"""The classic look's parity gate (plan pc/docs/plans/2026-10-20-classic-ui-in-lua.md
§3; pc/src/classic/classic_parity.h): rood.classic's Lua draw of each kind it
owns against the game's own drawing of the same element in the same frame,
packet for packet.

    python3 pc/tests/classic_parity.py [--card pc/build/test-card1.mcd] [--out DIR]
        [--renderer null|gl|vulkan] [--kind status,chain]

One game from the save_room checkpoint (a copy of the card), the classic look
(ui_menus 0), the null renderer by default (packets need no GPU), the shipped
plugins only (rood.classic on), dev mode (the RPC):
  - parity (RPC classic {parity}): the game draws; at each hook rood.classic's
    claim draws into a shadow slot and both are normalised and compared. Each
    kind's states are held in turn with rood.dev.show's spawners (the game's
    own records): status, every timing word and success word at the ages of
    its slide in, held and slide out; chain, counts 1 (it slides in), 2, 3, 9,
    10 and 99 at the same ages. Each state is held until the gate has compared
    two of its frames (a condition, not a count of vsyncs);
  - waivers (pc/tests/classic/waivers.toml) equal the overrides rood.classic
    declares (its M.look.overrides), and no mismatch outside their fields;
  - the default and the fallback, the parity mode off: rood.classic draws each
    kind (claimed, suppressed, drawn); the kind's hud_classic_<kind> off, a
    draw that raises, the plugin off: the game draws it again, each time;
  - state parity (§3.4): from one mark, a held word with rood.classic drawing,
    then with the game drawing, leaves the game's memory the same every vsync
    (Oracle page digests; the primitive buffers, ordering tables and the
    packet pointer masked).
Reports: <out>/<kind>.json (frames compared, mismatches by
field, the first difference with both packets, waived fields, the states
covered). Exit 0 when every kind is green."""
import argparse
import json
import os
import pathlib
import shutil
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402
from snapshot_diff import Symbols  # noqa: E402

TIMINGS = ["too_fast", "fast", "slow", "too_slow"]
SUCCESS = ["COOL!", "GOOD!", "EXCELLENT!", "RIGHT ON!", "PERFECT!", "WELL TIMED!", "NICE!", "GREAT!"]
WORDS = {"too_fast": "TOO FAST!", "fast": "FAST!", "slow": "SLOW!", "too_slow": "TOO SLOW!"}
CHAIN_COUNTS = [1, 2, 3, 9, 10, 99]
# The ages each kind's whole life must have shown: its slide in and out (func_800958A4: 64, 16, 4, 1 pixels
# at ages 0..3 and 45..42; func_80095A4C: the first chain's 0..3, every count's 90..87).
SLIDES = {"status": {0, 1, 2, 3, 42, 43, 44, 45}, "chain": {0, 1, 2, 3, 87, 88, 89, 90}}
# The gauges' states (Ashley's HUD record written: hp, max_hp, mp, max_mp, risk): digits of one to four
# figures, empty and full bars, RISK ZERO and RISK's bar; then the display's slide out and back.
GAUGES = [(157, 253, 50, 50, 0), (1, 253, 0, 50, 0), (253, 253, 50, 50, 1), (9, 9, 7, 7, 55), (999, 999, 999, 999, 100),
          (1000, 1500, 120, 300, 99), (0, 253, 50, 50, 0)]
PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (vs_battle_actors)


def states(kind):
    """(the show's args, the element's text) for every element of kind the gate plays."""
    if kind == "status":
        for timing in TIMINGS:
            yield {"timing": timing}, WORDS[timing]
        for variant, word in enumerate(SUCCESS):
            yield {"timing": "success", "variant": variant}, word
    elif kind == "chain":
        for count in CHAIN_COUNTS:
            yield {"count": count}, f"{count} CHAIN" + ("" if count == 1 else "S")


def stats_address(g):
    """Ashley's HUD record's currentHP (vs_battle_actors[0] -> +0x3c -> +0x18: HP, max, MP, max, risk)."""
    symbols = rood_rpc.symbols()
    actor = int.from_bytes(g.read(symbols["vs_battle_actors"], PTR), "little")
    record = int.from_bytes(g.read(actor + 0x3C, 4), "little")
    return record + 0x18


def tour_gauges(g, check, counters):
    """The gauges through their states and the display's slide; the ages are the states compared."""
    import struct
    at = stats_address(g)
    original = g.read(at, 10)
    covered = []
    try:
        for hp, max_hp, mp, max_mp, risk in GAUGES:
            g.write(at, struct.pack("<hhhhH", hp, max_hp, mp, max_mp, risk))
            before = counters("gauges")["frames"]
            ok = g.until(lambda: counters("gauges")["frames"] >= before + 2, guard=60)
            c = counters("gauges")
            if check(bool(ok), f"gauges HP {hp}/{max_hp} MP {mp}/{max_mp} risk {risk}: compared ({c['frames']} frames, "
                               f"{c['mismatched']} mismatched, '{c['text']}')"):
                covered.append({"hp": hp, "max_hp": max_hp, "mp": mp, "max_mp": max_mp, "risk": risk})
        # the slide: out (D_800EB9AF, as a menu opening sets it) until hidden, then back
        symbols = rood_rpc.symbols()
        before = counters("gauges")["frames"]
        g.write(symbols["D_800EB9AF"], b"\x01")
        out = g.until(lambda: int.from_bytes(g.read(symbols["D_800EB9B0"], 4), "little") == 0x200000, step=1, guard=60)
        g.write(symbols["D_800EB9AF"], b"\x00")
        back = g.until(lambda: int.from_bytes(g.read(symbols["D_800EB9B0"], 4), "little") == 0, step=1, guard=60)
        frames = counters("gauges")["frames"] - before
        if check(bool(out) and bool(back) and frames >= 6, f"gauges: the display slid out and back ({frames} frames "
                                                            f"compared)"):
            covered.append({"slide": "out and back"})
    finally:
        g.write(at, original)
    return covered


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="logs and reports (default pc/build/tests/classic_parity)")
    parser.add_argument("--renderer", choices=["null", "gl", "vulkan"], default="null")
    parser.add_argument("--kind", default="status,chain,gauges", help="the kinds to check (comma-separated)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "classic_parity"
    out.mkdir(parents=True, exist_ok=True)
    with card_guard(out / "cards", source=args.card) as cards:
        return run(out, cards.card1, args.renderer, [k for k in args.kind.split(",") if k])


def run(out, card, renderer, kinds):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    plugins = out / "plugins"  # (no player plugins: only the shipped ones)
    shutil.rmtree(plugins, ignore_errors=True)
    plugins.mkdir(parents=True)
    shutil.rmtree(out / "plugin_data", ignore_errors=True)
    settings = out / "settings.cfg"
    settings.write_text("ui_menus 0\nui_text font\naspect 16:9\ninterpolate 0\n")
    env = {"ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(plugins), "ROOD_PLUGIN_DATA": str(out / "plugin_data"),
           "ROOD_PLUGINS_SHIPPED": str(ROOT / "pc/plugins"), "ROOD_CLASSIC_PARITY": "1", "ROOD_DETERMINISTIC": "1"}
    if renderer == "null":
        env.update({"ROOD_RENDERER": "null", "SDL_VIDEO_DRIVER": "invalid-null-classic-parity"})
        # (the run's save states are made for its own renderer: boot to the checkpoint instead)
        if os.environ.get("ROOD_RENDERER") != "null":
            os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
    elif renderer == "vulkan":
        env["ROOD_RENDERER"] = "vulkan"
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        plugin = g.lua_plugins().get("rood.classic")
        if not check(plugin and plugin.get("state") == "loaded", f"rood.classic loaded ({plugin and plugin.get('state')})"):
            return 1
        g.call("classic", parity=True, reset=True)
        check(g.call("classic")["parity"], "the parity mode is on")

        def counters(kind):
            return g.call("classic")["kinds"][kind]

        waivers = tomllib.loads((ROOT / "pc/tests/classic/waivers.toml").read_text())
        declared = {}  # M.look.overrides as "kind=a,b;kind=..."
        for item in str(repl(g, "rood.classic", "local t = {} for k, v in pairs(M.look.overrides) do "
                                                  "t[#t + 1] = k .. '=' .. table.concat(v, ',') end "
                                                  "return table.concat(t, ';')")).strip('"').split(";"):
            if "=" in item:
                key, names = item.split("=", 1)
                declared[key] = [n for n in names.split(",") if n]
        reports = {}
        for kind in kinds:
            covered = []
            ages = set()
            if kind == "gauges":
                covered = tour_gauges(g, check, counters)
            for show, text in states(kind):
                # held (rood.dev.show: the game's own spawner, the age pinned) until the gate compared two of
                # its frames showing this element's text
                g.call("hud", action="show", kind=kind, hold=True, age=10, **show)
                before = counters(kind)["frames"]
                ok = g.until(lambda: (lambda c: c["frames"] >= before + 2 and c["text"] == text)(counters(kind)),
                             guard=60)
                if not check(bool(ok), f"{kind} {text}, held: compared ({counters(kind)})"):
                    continue
                # then its whole life played out (slide in, held, slide out): every frame compared
                g.call("hud", action="clear")
                g.call("hud", action="show", kind=kind, hold=False, **show)
                seen, before = set(), counters(kind)["frames"]

                def gone():
                    live = [e for e in g.call("hud", action="list")["elements"]
                            if e["kind"] == kind and e["text"] == text and e["live"] and not e["synthetic"]]
                    seen.update(e["age"] for e in live)
                    return not live and seen
                ok = g.until(gone, step=1, guard=60)
                frames = counters(kind)["frames"] - before
                check(bool(ok) and frames >= len(seen), f"{kind} {text}, its life: {frames} frames compared, ages "
                                                      f"{min(seen) if seen else '-'}..{max(seen) if seen else '-'}")
                ages |= seen
                covered.append({**show, "text": text, "ages": sorted(seen)})
            g.call("hud", action="clear")
            slides = SLIDES.get(kind, set())
            check(slides <= ages, f"{kind}: its slides covered" + (f" (ages {sorted(slides - ages)} missing)"
                                                                     if not slides <= ages else ""))
            c = counters(kind)
            waived = waivers.get(kind, {})
            allowed = {f for w in waived.values() for f in w.get("fields", [])}
            unwaived = {f: n for f, n in c["fields"].items() if f not in allowed}
            check(c["frames"] > 0 and c["skipped"] == 0,
                  f"{kind}: {c['frames']} frames compared, {c['skipped']} skipped ({c['skip'] or 'none'})")
            check(not unwaived, f"{kind}: {c['matched']} matched, {c['mismatched']} mismatched; unwaived fields "
                                f"{unwaived or 'none'}" + (f"; first {json.dumps(c.get('first'))}" if unwaived else ""))
            names = sorted((declared or {}).get(kind) or [])
            check(names == sorted(waived), f"{kind}: overrides declared {names} = waived {sorted(waived)}")
            reports[kind] = {"kind": kind, "frames": c["frames"], "matched": c["matched"],
                             "mismatched": c["mismatched"], "skipped": c["skipped"], "fields": c["fields"],
                             "first": c.get("first"), "waived": {k: v for k, v in waived.items()},
                             "declared": names, "coverage": covered, "states": len(covered),
                             "green": c["frames"] > 0 and c["skipped"] == 0 and not unwaived}
        g.call("classic", parity=False)
        check(not g.call("classic")["parity"], "the parity mode is off")

        for kind in kinds:
            default_and_fallback(g, kind, check)
        state_parity(g, check)
        report_dir = out
        for kind, report in reports.items():
            (report_dir / f"{kind}.json").write_text(json.dumps(report, indent=1))
            print(f"report: {report_dir / (kind + '.json')}: {report['frames']} frames, {report['states']} states, "
                  f"{'green' if report['green'] else 'RED'}")
    finally:
        g.close()
    print(f"{'PASS' if not failures else 'FAIL'}: classic parity ({len(failures)} failures)")
    return 1 if failures else 0


def repl(g, plugin, code):
    """A value from the plugin's sandbox (its module is M), as JSON."""
    reply = g.repl(op="eval", plugin=plugin, code=code)
    values = reply.get("values") or reply.get("results") or []
    if not reply.get("ok", True) or not values:
        raise RpcError(f"repl {code}: {reply}")
    value = values[0]
    return value.get("value", value) if isinstance(value, dict) and "value" in value else value


SHOW = {"status": {"timing": "slow", "age": 10}, "chain": {"count": 3, "age": 40}}


def default_and_fallback(g, kind, check):
    """The parity mode off: rood.classic draws the kind by default; with its setting off, a draw that
    raises, or the plugin off, the game's own drawing each time."""
    def stats():
        return g.call("hud", action="stats")["kinds"][kind]

    if kind in SHOW:  # (the gauges are up whenever the field is)
        g.call("hud", action="show", kind=kind, hold=True, **SHOW[kind])
    s0 = stats()
    drawn = g.until(lambda: (lambda s: s["drawn"] >= s0["drawn"] + 2 and s["claimed_by"] == "rood.classic"
                             and s["honoured"])(stats()), guard=60)
    s = stats()
    check(bool(drawn) and s["suppressed"] > s0["suppressed"],
          f"{kind}: drawn by rood.classic by default (honoured, suppressed {s['suppressed'] - s0['suppressed']}, "
          f"drawn {s['drawn'] - s0['drawn']})")

    def game_draws(why):
        s1 = stats()
        ok = g.until(lambda: (lambda s: s["fallback"] >= s1["fallback"] + 2 and not s["honoured"])(stats()), guard=60)
        s2 = stats()
        check(bool(ok) and s2["drawn"] == s1["drawn"],
              f"{kind}, {why}: the game draws it (fallback {s2['fallback'] - s1['fallback']}, Lua draws "
              f"{s2['drawn'] - s1['drawn']})")

    def lua_draws(why):
        s1 = stats()
        ok = g.until(lambda: (lambda s: s["drawn"] >= s1["drawn"] + 2 and s["honoured"])(stats()), guard=60)
        check(bool(ok), f"{kind}, {why}: rood.classic draws it again")

    g.call("settings", apply=f"hud_classic_{kind} 0")
    game_draws(f"hud_classic_{kind} 0")
    g.call("settings", apply=f"hud_classic_{kind} 1")
    lua_draws(f"hud_classic_{kind} 1")
    repl(g, "rood.classic", f"M.draw.{kind} = function() error('a broken classic draw') end return true")
    faults = stats()["faults"]
    game_draws("a draw that raises")
    check(stats()["faults"] > faults and stats()["claim_failed"], f"{kind}: the fault counted, the claim failed")
    g.call("lua", action="reload", plugin="rood.classic")
    lua_draws("the plugin reloaded")
    g.call("lua", action="enable", plugin="rood.classic", enabled=False)
    game_draws("rood.classic off")
    g.call("lua", action="enable", plugin="rood.classic", enabled=True)
    lua_draws("rood.classic on")
    g.call("hud", action="clear")


def state_parity(g, check, ticks=120):
    """§3.4: SLOW! held (and the gauges up), from one mark: rood.classic drawing them, then the game; every
    vsync's state
    (guest memory, the native game state, VRAM) the same but for the packets (the primitive buffers,
    the ordering tables, the packet pointer) and the host clock (last_vsync, as snapshot_diff and the
    oracle's own proof mask it). Both runs start from a rewind (the mark's)."""
    symbols = rood_rpc.symbols()
    pointer = 8 if pathlib.Path(rood_rpc.BINARY).read_bytes()[4] == 2 else 4  # (ELF class: the arrays' pointers)
    masks = [[0x1F800000, 4]]  # vs_scratch.unk0, the packet pointer
    for name, size in (("D_8005E0C0", 0x20000), ("D_80055C80", 0x822 * 4)):  # the frame's two of each
        for i in range(2):
            address = int.from_bytes(g.read(symbols[name] + pointer * i, pointer), "little")
            masks.append([address, size])
    masks += [[a, n] for a, n, name in Symbols(rood_rpc.BINARY).rows if name == "last_vsync" and n == 2 * pointer]
    g.call("hud", action="show", kind="status", hold=True, timing="slow", age=10)
    start = g.call("hud", action="stats")["kinds"]["status"]["drawn"]
    g.until(lambda: g.call("hud", action="stats")["kinds"]["status"]["drawn"] >= start + 2, guard=60,
            what="SLOW! drawn by rood.classic")
    g.pause()
    g.mark()
    g.rewind()
    g.digest("record", ticks=ticks, masks=masks)
    lua_drawn = g.call("hud", action="stats")["kinds"]["status"]["drawn"]
    g.until(lambda: g.digest("result")["recorded"] >= ticks, guard=120, what="the digest recorded")
    lua_drawn = g.call("hud", action="stats")["kinds"]["status"]["drawn"] - lua_drawn
    g.rewind()
    g.call("settings", apply=["hud_classic_status 0", "hud_classic_chain 0", "hud_classic_gauges 0"])
    fallback = g.call("hud", action="stats")["kinds"]["status"]["fallback"]
    g.digest("compare")
    g.until(lambda: g.digest("result")["ticks"] >= ticks, guard=120, what="the digest compared")
    result = g.digest("result")
    fallback = g.call("hud", action="stats")["kinds"]["status"]["fallback"] - fallback
    g.call("settings", apply=["hud_classic_status 1", "hud_classic_chain 1", "hud_classic_gauges 1"])
    g.call("hud", action="clear")
    check(lua_drawn > 0 and fallback > 0 and result.get("error") == 0 and result.get("first_tick") == -1
          and result.get("ticks") == result.get("recorded"),
          f"state parity: {result.get('ticks')} of {result.get('recorded')} vsyncs the same (rood.classic drew "
          f"{lua_drawn} frames, the game {fallback}; first difference {result.get('first_tick')} "
          f"{result.get('first_pages')})")


if __name__ == "__main__":
    sys.exit(main())
