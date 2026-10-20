#!/usr/bin/env python3
"""The REPL bridge (pc/src/lua/lua_repl.cpp, pc/tools/rood_repl.py; plan
pc/docs/plans/2026-10-18-freedom-and-fast-iteration.md F6) as an agent uses
it: rood_repl.py -e against a running game, one process per call.

    python3 pc/tests/repl_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

One game from the save_room checkpoint, the modern look, rood.hud from a copy
of pc/plugins (so the test can save its file):
  - a named session keeps its bindings across rood_repl.py processes; JSON
    replies (values, results, output, ms); an error's exit status, message and
    traceback; multi-line input from a pipe (an open function asks for more);
  - the console's and rood.hud's sandboxes (its module as M, the console's
    globals untouched); inspection: plugins, claims (rood.hud owns status at
    its main.lua), exports, queries (the field), describe;
  - SLOW! summoned and held: rood.hud draws it; its draw function redefined
    from the REPL (M.draw.status) draws the next presented frames (the
    seconds printed); a claim made from the REPL the same; then rood.hud's
    file saved: its own drawing again, and the session says it started over;
  - no plugin faults counted; rood_rpc.py repl the same protocol.
No screenshots: the HUD pass's commands (RPC hud stats)."""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, awake  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402

REPL = [sys.executable, str(ROOT / "pc/tools/rood_repl.py")]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="logs and the plugins' copy")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "repl"
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
    shutil.copytree(ROOT / "pc/plugins/rood.hud", shipped / "rood.hud")
    main_lua = shipped / "rood.hud/main.lua"
    settings = out / "settings.cfg"
    settings.write_text("aspect 4:3\nui_menus 1\nui_text font\n")
    env = {"ROOD_WINDOW": "960x720", "ROOD_SETTINGS": str(settings), "ROOD_PLUGINS": str(out / "plugins"),
           "ROOD_PLUGINS_SHIPPED": str(shipped), "ROOD_PLUGIN_DATA": str(out / "plugin_data")}
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        def repl(*argv, stdin=None):
            """rood_repl.py against this game: (exit status, stdout, stderr)."""
            r = subprocess.run(REPL + ["--socket", g.path] + list(argv), input=stdin, capture_output=True,
                               text=True, timeout=60)
            return r.returncode, r.stdout, r.stderr

        def replies(*argv):
            status, stdout, stderr = repl("--json", *argv)
            try:
                return status, [json.loads(line) for line in stdout.splitlines() if line.strip()], stderr
            except json.JSONDecodeError:
                return status, [], stdout + stderr

        def last_texts():
            return [c.get("text") or "" for c in g.call("hud", action="stats")["last"]]

        def wait(condition, step=1):
            """Run the game a little at a time until condition() (real-time reloads: no vsync counts);
            the guard only bounds a hang (60 awake seconds: a stopped game's pause not counted)."""
            return g.until(condition, step=step, guard=60)

        g.run(4)
        faults = g.lua_plugins()["rood.hud"]["faults"]
        # sessions across processes, JSON replies
        status, r, err = replies("-s", "agent", "-e", "x = 40", "-e", "x + 2, 'two', { a = 1 }")
        check(status == 0 and len(r) == 2 and r[1]["values"] == ["42", '"two"', "{a = 1}"]
              and r[1]["results"][:2] == [42, "two"] and r[1]["session"] == "agent" and "ms" in r[1],
              f"-e forms in one session, printed and JSON ({r}, {err})")
        status, r, _ = replies("-s", "agent", "-e", "x")
        check(status == 0 and r and r[0]["values"] == ["40"], f"the session's bindings in the next process ({r})")
        status, r, _ = replies("-s", "other", "-e", "x")
        check(r and r[0]["values"] == ["nil"], f"another session does not see them ({r})")
        status, stdout, stderr = repl("-s", "agent", "-e", "print('said', x) error('boom')")
        check(status == 1 and "said\t40" in stdout and "boom" in stderr and "stack traceback" in stderr,
              f"an error: exit 1, output kept, message and traceback ({stdout!r}, {stderr!r})")
        status, stdout, stderr = repl("-s", "agent", stdin="function twice(n)\n  return 2 * n\nend\ntwice(x)\n")
        check(status == 0 and stdout.strip() == "80", f"multi-line input from a pipe ({stdout!r}, {stderr!r})")
        # sandboxes and inspection
        status, r, _ = replies("-p", "rood.hud", "-s", "hud", "-e", "M.draw.status, type(M.look), x")
        check(r and r[0]["plugin"] == "rood.hud" and r[0]["values"][0].startswith("function<rood.hud/main.lua:")
              and r[0]["values"][1:] == ['"table"', "nil"], f"rood.hud's sandbox: its module as M ({r})")
        status, r, _ = replies("-s", "hud", "--inspect", "claims")
        claims = {c["kind"]: c for c in (r[0].get("result") or [])} if r else {}
        check(claims.get("status", {}).get("owner") == "rood.hud" and claims["status"]["honoured"]
              and claims["status"]["where"].startswith("rood.hud/main.lua:"), f"claims: rood.hud's ({claims})")
        status, r, _ = replies("-s", "hud", "--inspect", "exports")
        names = [e["name"] for e in r[0]["result"]] if r and r[0].get("ok") else []
        check({"draw", "look", "refresh", "style"} <= set(names), f"exports: rood.hud's module ({names})")
        status, r, _ = replies("-s", "hud", "--inspect", "queries")
        queries = {q["name"]: q.get("value") for q in r[0]["result"]} if r and r[0].get("ok") else {}
        check(queries.get("rood.game.mode()") == "field" and queries.get("rood.game.ashley()", {}).get("hp", 0) > 0,
              f"queries: in the field, Ashley's record ({queries.get('rood.game.mode()')})")
        status, r, _ = replies("-p", "rood.hud", "--inspect", "plugins")
        ids = [p["id"] for p in r[0]["result"]] if r and r[0].get("ok") else []
        check("rood.hud" in ids, f"plugins listed ({ids})")
        status, r, _ = replies("-s", "hud", "--describe", "M.draw.buff")
        d = r[0].get("result", {}) if r else {}
        check(d.get("type") == "function" and d.get("function", {}).get("source") == "rood.hud/main.lua"
              and "style" in d["function"]["upvalues"], f"describe: a draw function's source and upvalues ({d})")
        # a held word: rood.hud draws it; redefined from the REPL, the next frames draw the new one
        g.lua('rood.dev.show("status", { timing = "slow" })')
        check(wait(lambda: "SLOW!" in last_texts()), f"SLOW! held, drawn by rood.hud ({last_texts()})")
        t0 = awake()
        status, r, _ = replies("-s", "hud", "-e",
                               "M.draw.status = function(e, d) d.text(M.style.word, e.text .. ' REPL', e.x, e.y,"
                               " e.rgb) end")
        g.run(1)
        seen = "SLOW! REPL" in last_texts()
        check(status == 0 and seen, f"redefined from the REPL: drawn one vsync later ({last_texts()})")
        print(f"redefinition to drawn: {awake() - t0:.3f} s (one rood_repl.py process)", flush=True)
        status, r, _ = replies("-s", "hud", "-e",
                               "rood.hud.claim('status', function(e, d) d.text(M.style.word, 'CLAIMED', e.x, e.y) end)")
        g.run(1)
        check(status == 0 and "CLAIMED" in last_texts(), f"a claim made from the REPL draws ({last_texts()})")
        # rood.hud's file saved: its own drawing again, the session starts over
        t0 = awake()
        main_lua.write_text(main_lua.read_text() + "-- saved by repl_smoke\n")
        check(wait(lambda: "SLOW!" in last_texts()), f"the file reloaded wins: its drawing ({last_texts()})")
        print(f"file saved to its drawing again: {awake() - t0:.3f} s", flush=True)
        status, r, _ = replies("-s", "hud", "-e", "M.draw.status ~= nil")
        check(r and "rood.hud reloaded" in r[0].get("reset", "") and r[0]["values"] == ["true"],
              f"the session says it started over ({r})")
        g.lua('rood.dev.clear()')
        check(g.lua_plugins()["rood.hud"]["faults"] == faults, "no rood.hud faults counted")
        # rood_rpc.py repl: the same protocol
        r = subprocess.run([sys.executable, str(ROOT / "pc/tools/rood_rpc.py"), "--socket", g.path, "repl", "eval",
                            "x", "--session", "agent"], capture_output=True, text=True, timeout=60)
        check(r.returncode == 0 and json.loads(r.stdout)["values"] == ["40"], f"rood_rpc.py repl eval ({r.stdout})")
        status, stdout, _ = repl("--sessions")
        check("agent" in stdout and "hud" in stdout, f"sessions listed ({stdout!r})")
        status, stdout, _ = repl("--close", "agent")
        check(status == 0 and "agent" not in stdout, "a session closed")
        log = (out / "game.log").read_text(errors="replace")
        check("[crash]" not in log, "no crash in the log")
    finally:
        g.close()
    print(f"{len(failures)} failure(s)" if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
