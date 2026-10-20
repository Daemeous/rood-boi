#!/usr/bin/env python3
"""Hot swap a trial-port function (pc/docs/porting/battle/TRIAL.md): edit its
Zig in a private copy of game/, rebuild the development library, reload it in
the same host PID while paused, and witness the edit in shadow mode
(zig_hotswap_smoke's method), then restore it and witness a clean run.

    ROOD_ARCH=64 python3 pc/tests/trial_hotswap.py SUBSYSTEM FUNCTION FILE OLD NEW SCENARIO \
        --card CARD --work DIR

FILE is relative to game/; OLD must occur once and NEW is a deliberately wrong
replacement. SCENARIO is a pc/tests/diff/scenarios.py recipe that calls
FUNCTION; it is set up once and marked, and each attempt rewinds to the mark
(enemy_kill also presses its three Circles). Needs the development host
(build_boot.py --arch 64 --engine-library) built first; ROOD_BUILD_LOCK
serialises the library builds. Prints the evidence as JSON and HOTSWAP PASS
or FAIL. Not a registered suite: the trial's evidence tool."""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from checkpoints import launch_at, _base_env  # noqa: E402
from diff import corpus, scenarios  # noqa: E402

DEV = ROOT / "pc/build/dev64"


def build(command):
    start = time.monotonic()
    if os.environ.get("ROOD_BUILD_LOCK"):
        command = ["flock", os.environ["ROOD_BUILD_LOCK"], *command]
    result = subprocess.run(command, cwd=ROOT, text=True,
                            capture_output=True, timeout=300)
    if result.returncode:
        raise RuntimeError(result.stdout[-4000:] + result.stderr[-4000:])
    return time.monotonic() - start


def main():
    p = argparse.ArgumentParser()
    p.add_argument("subsystem")
    p.add_argument("function")
    p.add_argument("file")  # relative to game/
    p.add_argument("old")
    p.add_argument("new")
    p.add_argument("scenario")
    p.add_argument("--card", required=True)
    p.add_argument("--work", required=True)
    p.add_argument("--frames", type=int, default=1200)
    a = p.parse_args()
    work = pathlib.Path(a.work)
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    source = work / "game"
    shutil.copytree(ROOT / "game", source)
    library = work / "librood_game.so"
    lib_cmd = [sys.executable, str(ROOT / "pc/tools/game_lib.py"), "--source-root", str(source), "--out", str(library)]
    build(lib_cmd)
    card = work / "card.mcd"
    shutil.copyfile(a.card, card)
    env = dict(_base_env(work), **corpus.environment(work, "battle_action"), ROOD_GAME_LIBRARY=str(library))
    rood_rpc.BINARY = DEV / "rood_host"
    os.environ["ROOD_SYMBOLS"] = str(rood_rpc.BINARY)
    os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
    socket = f"/tmp/rood-trial-hotswap-{os.getpid()}.sock"
    game = launch_at("save_room", card=str(card), fast=True, socket_path=socket, log=work / "game.log", extra_env=env)
    process = game.processes[0]
    pid = process.pid
    evidence = {"subsystem": a.subsystem, "function": a.function, "pid": pid}
    scenario = scenarios.SCENARIOS[a.scenario]
    try:
        for unit in game.owner()["subsystems"]:
            game.owner(unit["name"], "original")
        scenario.setup(game)
        game.mark()

        def attempt(label):
            game.rewind()
            game.owner(a.subsystem, "shadow")
            game.call("owner", action="report", reset=True)
            start_vsync = game.status()["vsync"]
            if a.scenario == "enemy_kill":
                for _ in range(3):
                    game.press("circle", 3, 20)
            started = time.monotonic()
            for frames in range(1, a.frames + 1):
                game.run(1)
                report = game.call("owner", action="report")
                row = next((r for r in report["coverage"] if r["function"] == a.function), None)
                if report["errors"] or report["count"] or (row and row["calls"]):
                    break
            game.pause()
            game.owner(a.subsystem, "original")
            return dict(label=label, frames=frames, start_vsync=start_vsync, wall_s=time.monotonic() - started,
                        calls=row and row["calls"], count=report["count"], errors=report["errors"],
                        mismatches=report["mismatches"][:2])

        evidence["clean_before"] = attempt("clean library")
        path = source / a.file
        clean = path.read_text()
        assert clean.count(a.old) == 1, f"{a.old!r} not found once in {path}"
        game.rewind()
        t0 = time.monotonic()
        path.write_text(clean.replace(a.old, a.new))
        build_s = build(lib_cmd)
        r0 = time.monotonic()
        reply = game.call("reload", target="zig")
        reload_s = time.monotonic() - r0
        assert process.poll() is None and process.pid == pid
        edited = attempt("edited library")
        # Name the first differing byte while the game is paused right after
        # the call: the mapper over the subsystem's roots, and its offset from
        # each root pointer's current target (a heap buffer has no type).
        import ownership
        from field_map import FieldMap
        sub = next(s for s in ownership.load(source / "ownership.toml").subsystems if s.name == a.subsystem)
        if edited["mismatches"]:
            first = edited["mismatches"][0]["first"]
            edited["field"] = FieldMap(rood_rpc.BINARY, sub.roots, game.read).names([first])[first]
            symbols = rood_rpc.symbols()
            for root in sub.roots:
                if root in symbols:
                    target = int.from_bytes(game.read(symbols[root], 8), "little")
                    if target and 0 <= first - target < 4096:
                        edited["field"] += f" = (*{root})+{first - target:#x}"
        edited.update(build_s=build_s, reload_s=reload_s, reload=reply, edit_to_witness_s=time.monotonic() - t0)
        evidence["edited"] = edited
        path.write_text(clean)
        build(lib_cmd)
        evidence["restore_reload"] = game.call("reload", target="zig")
        evidence["clean_after"] = attempt("restored library")
        evidence["same_pid"] = process.poll() is None and process.pid == pid
    finally:
        game.close()
    log = (work / "game.log").read_text(errors="replace")
    evidence["crash_in_log"] = "[crash]" in log
    print(json.dumps(evidence, indent=1))
    ok = (evidence["clean_before"]["calls"] and not evidence["clean_before"]["count"]
          and evidence["edited"]["count"] and evidence["edited"]["mismatches"][0]["function"] == a.function
          and evidence["clean_after"]["calls"] and not evidence["clean_after"]["count"]
          and evidence["same_pid"] and not evidence["crash_in_log"])
    print("HOTSWAP", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
