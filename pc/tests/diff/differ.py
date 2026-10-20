#!/usr/bin/env python3
"""Generate and compare a local corpus. Default full corpus fails on gaps.

Run under a bounded heavy lock; see CORPUS.md. No cross-process snapshot
comparison: every owner run, including diagnostics, rewinds the same mark.
"""
import argparse
import os
import functools
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from diff import corpus, scenarios

MODES = ("call", "tick", "alternate")


def coverage(report, functions):
    seen = {row["function"] for row in report.get("coverage", []) if row.get("calls", 0)}
    return {"called": sorted(seen), "uncalled": sorted(set(functions) - seen),
            "nested_only": sorted(row["function"] for row in report.get("coverage", [])
                                  if row.get("nested", 0) and not row.get("calls", 0)),
            "available": "coverage" in report}


WAIVERS = pathlib.Path(__file__).with_name("waivers.toml")


def waivers(table, subsystem, path=WAIVERS):
    """{function: reason} of one waiver table (waivers.toml)."""
    import tomllib
    data = tomllib.loads(path.read_text()) if path.exists() else {}
    return (data.get(table, {}) if table else data).get(subsystem, {})


def alternate_check(calls, waived=None):
    """Alternate mode alternates per routed call (state_diff.c): every
    function the run called must have run both owned and original, or the
    mode compared nothing for it (unless waived: a function a recipe calls
    once cannot be split)."""
    waived = waived or {}
    if calls is None:
        return {"ok": False, "reason": "the digest result has no per-call alternate counts"}
    called = [c for c in calls if c["owned"] + c["original"]]
    one_sided = [c for c in called if not c["owned"] or not c["original"]]
    failing = [f"{c['function']} ({c['owned']} owned, {c['original']} original)"
               for c in one_sided if c["function"] not in waived]
    result = {"calls": calls, "waived": {c["function"]: waived[c["function"]] for c in one_sided
                                         if c["function"] in waived}}
    if not called:
        return dict(result, ok=False, reason="no routed call ran: alternate compared nothing")
    if failing:
        return dict(result, ok=False, reason="called on one side only: " + ", ".join(failing))
    return dict(result, ok=True)


def root_call_gate(reports, subsystem, functions, path=WAIVERS):
    """Every routed function of the subsystem needs root calls in call mode
    in at least one of the selected recipes, or a waiver with its reason
    (pc/tests/diff/waivers.toml): a subsystem does not pass because one of
    its functions was called."""
    waived = waivers(None, subsystem, path)
    called = set()
    for report in reports.values():
        summary = report.get("modes", {}).get("call", {}).get("coverage_summary", {})
        called |= set(summary.get("called", []))
    missing = [f for f in functions if f not in called and f not in waived]
    return {"ok": not missing, "called": sorted(called & set(functions)), "missing": missing,
            "waived": {f: waived[f] for f in functions if f in waived and f not in called}}


def annotate(report, mapper):
    for mismatch in report.get("mismatches", []):
        address = mismatch.get("first", 0)
        if address and mismatch.get("range") != "spu":
            mismatch["field"] = mapper.names([address]).get(address, hex(address))
    return report


def first_difference(game, work, recording, tick, subsystem, index, mode, mapper, masks=()):
    import snapshot_diff
    states = {}
    for owner in ("original", "owned"):
        game.digest("stop")
        game.replay(None)
        game.rewind()
        game.owner(subsystem, "original" if mode == "control" else owner)
        game.replay(recording)
        if owner == "owned" and mode == "alternate":
            game.digest("compare", alternate=index)
        game.run(tick + 1)
        game.digest("stop")
        path = work / f"{mode}-{owner}-{tick}.snap"
        game.call("snapshot", action="save", path=str(path))
        states[owner] = snapshot_diff.read(path)
    game.replay(None)
    a, b = states["original"]["memory"], states["owned"]["memory"]
    for base in sorted(set(a) | set(b)):
        if base not in a or base not in b or len(a[base]) != len(b[base]):
            return {"tick": tick, "layout_mismatch": hex(base)}
        for offset, (expected, actual) in enumerate(zip(a[base], b[base])):
            if expected != actual:
                address = base + offset
                if any(start <= address < start + size for start, size in masks):
                    continue
                return {"tick": tick, "address": address, "expected": expected,
                        "actual": actual, "field": mapper.names([address]).get(address, hex(address)),
                        "scope": "first snapshot-memory difference; may include bytes outside digest ranges"}
    return {"tick": tick, "unresolved": "no memory byte difference reproduced; inspect snapshots (including SPU/metadata)"}


def compare(game, recording, ticks, subsystem, index, modes, mapper, work,
            functions, diagnose=first_difference, scenario=None):
    results = {}
    # An original/original replay is a required determinism control, not a mask.
    for mode in ("control", *modes):
        game.digest("stop")
        game.replay(None)
        game.rewind()
        game.owner(subsystem, "shadow" if mode == "call" else
                   "original" if mode == "control" else "owned")
        if scenario:
            scenario.reset_witness(game)
        if mode == "call":
            game.call("owner", action="report", reset=True)
        game.replay(recording)
        if mode != "call":
            game.digest("compare", alternate=index if mode == "alternate" else -1)
        game.run(ticks)
        witness = scenario.witness(game) if scenario else {"ok": True}
        if mode == "call":
            report = annotate(game.call("owner", action="report"), mapper)
            report["coverage_summary"] = coverage(report, functions)
            ok = (report.get("calls", 0) > 0 and report.get("count") == 0
                  and report.get("errors") == 0 and not report.get("truncated", False))
        else:
            report = game.digest("result")
            ok = (report.get("error") == 0 and report.get("ticks") == ticks
                  and report.get("recorded") == ticks and report.get("first_tick") == -1)
            if mode == "alternate":
                report["alternate_check"] = alternate_check(report.get("alternate_calls"),
                                                            waivers("alternate_one_side", subsystem))
                ok = ok and report["alternate_check"]["ok"]
            if report.get("first_tick", -1) >= 0:
                report["first_difference"] = diagnose(game, work, recording,
                    report["first_tick"], subsystem, index, mode, mapper)
        report["state_hashes"] = game.call("snapshot", action="hash")
        report["witness"] = witness
        ok = ok and witness["ok"]
        report["ok"] = ok
        results[mode] = report
        game.digest("stop")
        game.replay(None)
        game.owner(subsystem, "original")
        if mode == "control" and not ok:
            break  # No owned result is meaningful without deterministic replay.
    return results


def run_one(scenario, args, work):
    import rood_rpc
    import ownership
    from checkpoints import launch_at
    from field_map import FieldMap
    table = ownership.load()
    sub = next(s for s in table.subsystems if s.name == args.subsystem)
    card = work / "card.mcd"
    shutil.copyfile(args.card, card)
    env = corpus.environment(work, sub.name, getattr(args, "profile", "classic"))
    # Shared checkpoints were generated with the normal startup profile, not
    # this features-off profile. Never import that feature history into a corpus.
    previous = os.environ.get("ROOD_TEST_SNAPSHOTS")
    os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
    try:
        game = launch_at("save_room", card=str(card), fast=True, log=work / "game.log", extra_env=env,
                         socket_path=f"/tmp/rood-corpus-{os.getpid()}.sock")
    finally:
        if previous is None:
            os.environ.pop("ROOD_TEST_SNAPSHOTS", None)
        else:
            os.environ["ROOD_TEST_SNAPSHOTS"] = previous
    try:
        # Pin all other routed units to original too (defaults may change later).
        for unit in table.subsystems:
            game.owner(unit.name, "original")
        mapper = FieldMap(rood_rpc.BINARY, sub.roots, game.read)
        masks = corpus.clock_mask(rood_rpc.BINARY)
        recording, ticks = corpus.generate(game, scenario, work, sub.name, masks=masks)
        manifest = {"binary_sha256": corpus.sha256(rood_rpc.BINARY),
                    "card_sha256": corpus.sha256(args.card), "subsystem": sub.name,
                    "roots": sub.roots, "functions": [name for _, name in sub.functions]}
        corpus.write_json(work / "target.json", manifest)
        results = compare(game, recording, ticks, sub.name, sub.index, args.mode,
                          mapper, work, manifest["functions"], scenario=scenario,
                          diagnose=functools.partial(first_difference, masks=masks))
        return {"status": "passed" if all(r["ok"] for r in results.values()) else "failed",
                "modes": results, "field_map_diagnostics": mapper.diagnostics,
                "coverage_claim": (type(scenario).__doc__ or "").strip().split("\n")[0] if type(scenario) is not scenarios.Scenario
                else "ended enemy action with actor != 0 and woundup == 1 in every run"}
    finally:
        game.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("subsystem", nargs="?", default="battle_action")
    parser.add_argument("--scenario", action="append", choices=scenarios.SCENARIOS)
    parser.add_argument("--mode", nargs="+", choices=MODES, default=list(MODES))
    parser.add_argument("--card", type=pathlib.Path, default=ROOT / "pc/build/test-card1.mcd")
    parser.add_argument("--out", type=pathlib.Path, default=corpus.CACHE)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--profile", choices=sorted(corpus.PROFILES), default="classic",
                        help="classic: every PC feature off (the default oracle profile); features: the "
                             "features whose ROOD_PC hooks change game state on (corpus.py FEATURES)")
    args = parser.parse_args(argv)
    selected = scenarios.select(args.scenario)
    if args.list:
        for scenario in selected:
            print(scenario.name + ": " + ("INCOMPLETE: " + scenario.incomplete if scenario.incomplete else
                                         "implemented candidate (ended enemy-cast witness required)"))
        return 0
    out = corpus.cache_path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="run-", dir=out))
    reports = {}
    for scenario in selected:
        if scenario.incomplete:
            reports[scenario.name] = {"status": "incomplete", "reason": scenario.incomplete}
        else:
            case = work / scenario.name
            case.mkdir()
            try:
                reports[scenario.name] = run_one(scenario, args, case)
            except Exception as error:
                reports[scenario.name] = {"status": "failed", "error": f"{type(error).__name__}: {error}"}
        corpus.write_json(work / "report.json", reports)
        print(scenario.name + ": " + reports[scenario.name]["status"], flush=True)
    import ownership
    sub = next(s for s in ownership.load().subsystems if s.name == args.subsystem)
    gate = root_call_gate({k: v for k, v in reports.items() if v.get("status") != "incomplete"},
                          sub.name, [name for _, name in sub.functions])
    corpus.write_json(work / "root_calls.json", gate)
    print(f"Root calls: {len(gate['called'])}/{len(sub.functions)} functions called"
          + (f", waived: {', '.join(gate['waived'])}" if gate["waived"] else "")
          + ("" if gate["ok"] else f"; FAIL: never called and not waived: {', '.join(gate['missing'])}"))
    print(f"Report: {work / 'report.json'}")
    return 0 if gate["ok"] and all(r["status"] == "passed" for r in reports.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
