#!/usr/bin/env python3
"""Score a model's bake-off ports (pc/docs/porting/battle/BAKEOFF.md): CPU
only, no game run, about half a minute for the eleven functions.

    python3 pc/tools/bakeoff_score.py PORT_DIR [--json OUT] [--iterations N] [--seeds 1,2,3]
    python3 pc/tools/bakeoff_score.py --reference        # the checkout's own reference ports

PORT_DIR holds one Zig module per function, named as pc/tests/zig_diff/
bakeoff.toml says (`room_timer.zig`, ..., `actor_animation.zig`), each
exporting the function with its exact name and signature. For each function
the candidate is compiled in place of the reference port in a scratch copy
(the checkout's game/ is never written: zig_diff.py copies game/common and
the candidate into the scratch work directory) against the 64-bit build's
generated views_<unit>/c_<unit> and the function's original C body, then
pc/tools/zig_diff.py's harness runs both on the spec's random and edge inputs
(the normal run, not the mutation self-check), once per seed.

Per function: present? compiles (and the compiler's error)? per seed: the
iterations run, mismatches (state, call traces, return value), a panic or a
crash (with the first report line), the input classes reached; passed = it
compiled and every seed ran with 0 mismatches, no panic, no crash. Output:
JSON (stdout, or --json OUT) with one entry per function and a summary; a
table on stderr. Exit 0 when every function passed, 1 otherwise, 2 on a
usage error, 77 without a 64-bit build (python3 pc/tools/build_boot.py --arch 64).

Candidate modules may import only "std", "common" (game/common: psx), and the
unit's "views_<unit>" and "c_<unit>", as the reference ports do.
"""
import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import ownership  # noqa: E402
import zig_diff  # noqa: E402

BAKEOFF = ROOT / "pc/tests/zig_diff/bakeoff.toml"


def functions():
    return tomllib.loads(BAKEOFF.read_text())["function"]


def first_report(text):
    return next((l.removeprefix("zdiff: ") for l in text.splitlines()
                 if "MISMATCH" in l or "PANIC" in l or "CRASH" in l or "UNEXPECTED" in l), None)


def run_seed(exe, iterations, seed, timeout):
    try:
        result = subprocess.run([str(exe), str(iterations), str(seed)], capture_output=True, text=True,
                                timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"seed": seed, "exit": None, "timeout": True, "iterations": None, "mismatches": None, "panic": None,
                "crash": None, "classes": {},
                "first": f"no result after {timeout} s (an endless loop?)"}
    text = result.stdout + result.stderr
    summary = re.search(r": (\d+) iterations, seed \d+, (\d+) mismatches", text)
    classes = re.search(r"classes:(.*)", text)
    panic = next((l for l in text.splitlines() if "PANIC" in l), None)
    crash = next((l for l in text.splitlines() if "CRASH" in l or "UNEXPECTED" in l), None)
    return {
        "seed": seed,
        "exit": result.returncode,
        "timeout": False,
        "iterations": int(summary.group(1)) if summary else None,
        "mismatches": int(summary.group(2)) if summary else None,
        "panic": panic.removeprefix("zdiff: ") if panic else None,
        "crash": crash.removeprefix("zdiff: ") if crash else None,
        "first": first_report(text),
        "classes": dict(c.split("=") for c in classes.group(1).split()) if classes else {},
    }


def score_one(entry, spec, port_dir, args, table, scratch):
    name, module = entry["name"], entry["module"]
    candidate = port_dir / f"{module}.zig"
    out = {"function": name, "module": module, "signature": entry["signature"], "difficulty": entry["difficulty"],
           "file": str(candidate), "present": candidate.is_file(), "compiled": False, "compile_error": None,
           "orig_line": None, "runs": [], "passed": False, "seconds": 0.0}
    if not out["present"]:
        return out
    start = time.monotonic()
    text = candidate.read_text(errors="replace")
    out["orig_line"] = bool(re.search(rf"^\s*/// orig: {re.escape(name)} \(", text, re.M))
    work = scratch / name
    work.mkdir(parents=True, exist_ok=True)
    try:
        build = zig_diff.Build(name, spec, args.build, work, table, port_source=candidate)
        exe, _ = build.compile()
        out["compiled"] = True
    except zig_diff.Failure as error:
        message = str(error)
        # The compiler's own lines, with the scratch path made relative.
        out["compile_error"] = message.replace(str(work) + "/", "")[-3000:]
        out["seconds"] = round(time.monotonic() - start, 2)
        return out
    iterations = args.iterations or spec.get("iterations", 20000)
    for seed in args.seeds:
        out["runs"].append(run_seed(exe, iterations, seed, args.timeout))
    out["passed"] = all(r["exit"] == 0 and r["mismatches"] == 0 for r in out["runs"])
    out["seconds"] = round(time.monotonic() - start, 2)
    return out


def verdict(result):
    if not result["present"]:
        return "missing"
    if not result["compiled"]:
        return "compile error"
    if result["passed"]:
        return "pass"
    for r in result["runs"]:
        if r["timeout"]:
            return "timeout"
        if r["panic"]:
            return "panic"
        if r["crash"]:
            return "crash"
    return "mismatch"


def reference_dir(scratch, entries, table):
    """The checkout's reference modules, copied (--reference)."""
    out = scratch / "reference"
    out.mkdir()
    for entry in entries:
        sub, zig_path = zig_diff.find_route(table, entry["name"])
        shutil.copy(ROOT / "game" / sub.overlay / (zig_path.split(".")[0] + ".zig"), out / f"{entry['module']}.zig")
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port_dir", nargs="?", type=pathlib.Path)
    parser.add_argument("--reference", action="store_true", help="score the checkout's reference ports")
    parser.add_argument("--json", type=pathlib.Path, help="write the JSON here (default: stdout)")
    parser.add_argument("--iterations", type=int, help="per seed (default: each spec's, specs.toml)")
    parser.add_argument("--seeds", default="1,2,3", help="comma-separated harness seeds (default 1,2,3)")
    parser.add_argument("--only", action="append", help="score only this function (repeatable)")
    parser.add_argument("--timeout", type=int, default=120, help="seconds per harness run")
    parser.add_argument("-j", "--jobs", type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument("--build", type=pathlib.Path, default=ROOT / "pc/build/boot64")
    parser.add_argument("--work", type=pathlib.Path, help="scratch directory (default: a new temporary one, removed)")
    args = parser.parse_args(argv)
    if bool(args.port_dir) == args.reference:
        parser.error("give PORT_DIR or --reference")
    args.seeds = [int(s) for s in args.seeds.split(",") if s]
    if not (args.build / "imports.ld").exists():
        print(f"bakeoff_score: {args.build} has no 64-bit build (python3 pc/tools/build_boot.py --arch 64)",
              file=sys.stderr)
        return 77
    entries = functions()
    if args.only:
        unknown = set(args.only) - {e["name"] for e in entries}
        if unknown:
            parser.error(f"not bake-off functions: {', '.join(sorted(unknown))}")
        entries = [e for e in entries if e["name"] in args.only]
    specs = tomllib.loads((zig_diff.SPECS / "specs.toml").read_text())["function"]
    table = ownership.load()
    scratch = args.work or pathlib.Path(tempfile.mkdtemp(prefix="bakeoff-score-"))
    scratch.mkdir(parents=True, exist_ok=True)
    try:
        port_dir = reference_dir(scratch, entries, table) if args.reference else args.port_dir.resolve()
        if not port_dir.is_dir():
            parser.error(f"{port_dir}: not a directory")
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            results = list(pool.map(lambda e: score_one(e, specs[e["name"]], port_dir, args, table, scratch), entries))
    finally:
        if not args.work:
            shutil.rmtree(scratch, ignore_errors=True)
    for r in results:
        r["verdict"] = verdict(r)
    summary = {
        "port_dir": "reference" if args.reference else str(args.port_dir),
        "functions": len(results),
        "present": sum(r["present"] for r in results),
        "compiled": sum(r["compiled"] for r in results),
        "passed": sum(r["passed"] for r in results),
        "verdicts": {v: sum(r["verdict"] == v for r in results) for v in sorted({r["verdict"] for r in results})},
        "seeds": args.seeds,
        "seconds": round(sum(r["seconds"] for r in results), 1),
    }
    text = json.dumps({"summary": summary, "functions": results}, indent=2)
    if args.json:
        args.json.write_text(text + "\n")
    else:
        print(text)
    for r in results:
        runs = r["runs"]
        if not r["compiled"]:
            detail = re.search(r"\S+: error: .*", r["compile_error"] or "")
            detail = detail.group(0) if detail else (r["compile_error"] or "").strip().split("\n")[-1]
        else:
            detail = next((x["first"] for x in runs if x["first"]), f"{sum(x['iterations'] or 0 for x in runs)} iterations")
        print(f"{r['verdict']:>13}  {r['function']:28} {r['module']:18} {detail[:110]}", file=sys.stderr)
    print(f"bakeoff_score: {summary['passed']}/{summary['functions']} passed "
          f"({summary['compiled']} compiled, {summary['present']} present)", file=sys.stderr)
    return 0 if summary["passed"] == summary["functions"] else 1


if __name__ == "__main__":
    sys.exit(main())
