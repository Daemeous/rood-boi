#!/usr/bin/env python3
"""How far the battle port is: what pc/docs/porting/battle/FUNCTIONS.tsv lists,
against the `/// orig:` lines in game/ (PORTING.md: every ported function
carries one), game/ownership.toml's defaults and the pipeline's records.

    python3 pc/tools/port_progress.py                 this checkout
    python3 pc/tools/port_progress.py --units         and per unit
    python3 pc/tools/port_progress.py --left 146C     the functions 146C still has to port
    python3 pc/tools/port_progress.py --tree WT ...   also what each worktree WT adds (work in flight)
    python3 pc/tools/port_progress.py --json          the same numbers, for a script

A function counts as ported when some game/**/*.zig names it and its unit in
an `/// orig:` line. A D2 function's unit is its oracle, the pc/src file the
kind column names ("PC oracle is pc/src/battle_helpers.c:473"), as in
ownership.toml (the PS1 assembly file is only where it came from). Rows by kind (FUNCTIONS.tsv): "c" is the first pass
(battle C), "D2" the PS1-assembly functions after the flip, "pc-inc" the host
C compiled into battle units, "generated" the effect/event wrappers;
"SLUS boundary" rows stay C and are not counted. Builds and runs nothing.
"""
import argparse
import collections
import json
import pathlib
import re
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
TABLE = "pc/docs/porting/battle/FUNCTIONS.tsv"
ORIG = re.compile(r"^\s*/// orig: (\S+) \(([^)]+)\)", re.M)
DIFFICULTIES = ("S", "M", "L", "XL")


def kind_of(kind):
    """FUNCTIONS.tsv's kind column -> c / D2 / pc-inc / generated / boundary."""
    if kind.startswith("SLUS boundary"):
        return "boundary"
    for name in ("D2", "pc-inc", "generated"):
        if kind.startswith(name):
            return name
    return "c"


ORACLE = re.compile(r"PC oracle is (\S+?):\d+")


def unit_file(path, kind):
    """The unit an `/// orig:` line names: the row's file, except a D2 row
    with a real oracle, whose unit is the pc/src file that is the oracle."""
    oracle = ORACLE.search(kind)
    return oracle.group(1) if oracle else path


def functions(root):
    """[(name, unit file, unit, kind, difficulty)] from FUNCTIONS.tsv."""
    rows = []
    with open(root / TABLE, encoding="utf-8") as table:
        header = next(table).rstrip("\n").split("\t")
        col = {name: i for i, name in enumerate(header)}
        for line in table:
            r = line.rstrip("\n").split("\t")
            path = r[col["file"]].split(" ")[0]
            unit = pathlib.Path(path).stem if path.endswith(".c") else path
            kind = r[col["kind"]]
            rows.append((r[col["function"]], unit_file(path, kind), unit, kind_of(kind), r[col["difficulty"]]))
    return rows


def ported(root):
    """{(name, unit file)} named by the `/// orig:` lines under game/."""
    found = set()
    for path in (root / "game").rglob("*.zig"):
        found.update(ORIG.findall(path.read_text(encoding="utf-8", errors="replace")))
    return found


def owners(root):
    """(subsystems defaulting to owned, all subsystems, functions routed)."""
    data = tomllib.loads((root / "game/ownership.toml").read_text(encoding="utf-8"))
    subs = data.get("subsystem", {}).values()
    return (sum(s.get("default") == "owned" for s in subs), len(subs),
            sum(len(s.get("functions", [])) for s in subs))


def records(root):
    """Pipeline records: {status: count}, and how many have a review verdict."""
    status, reviewed = collections.Counter(), 0
    for path in (root / "pc/docs/porting/battle/pipeline").glob("*/*.json"):
        try:
            r = json.loads(path.read_text(encoding="utf-8"))
        except ValueError:
            continue
        status[r.get("status")] += 1
        reviewed += bool(r.get("reviewed"))
    return status, reviewed


def summary(root):
    rows, done = functions(root), ported(root)
    kinds = collections.defaultdict(lambda: [0, 0])
    units = collections.defaultdict(lambda: {"ported": 0, "left": 0, **{d: 0 for d in DIFFICULTIES}})
    left = collections.defaultdict(list)
    for name, path, unit, kind, difficulty in rows:
        if kind == "boundary":
            continue
        is_ported = (name, path) in done
        kinds[kind][0 if is_ported else 1] += 1
        if kind == "c":
            u = units[unit]
            u["ported" if is_ported else "left"] += 1
            if not is_ported:
                u[difficulty] = u.get(difficulty, 0) + 1
                left[unit].append((name, difficulty))
    owned, subsystems, routed = owners(root)
    status, reviewed = records(root)
    return {
        "kinds": {k: {"ported": v[0], "left": v[1]} for k, v in kinds.items()},
        "units": dict(units),
        "left": dict(left),
        "ported_names": sorted(n for n, p, *_ in rows if (n, p) in done),
        "ownership": {"subsystems": subsystems, "default_owned": owned, "routed": routed},
        "pipeline": {"records": dict(status), "reviewed": reviewed},
    }


def bar(done, total, width=30):
    filled = round(width * done / total) if total else 0
    return "#" * filled + "." * (width - filled)


def report(s, show_units):
    print("Battle port (pc/docs/porting/battle/FUNCTIONS.tsv)\n")
    labels = {"c": "pass 1: battle C", "D2": "D2: PS1 assembly", "pc-inc": "host C in battle units",
              "generated": "generated wrappers"}
    for kind in ("c", "D2", "pc-inc", "generated"):
        k = s["kinds"].get(kind, {"ported": 0, "left": 0})
        total = k["ported"] + k["left"]
        pct = 100 * k["ported"] / total if total else 0
        print(f"  {labels[kind]:<24} {bar(k['ported'], total)} {k['ported']:>5} / {total:<5} {pct:5.1f}%")
    left = [u for u in s["units"].values() if u["left"]]
    by_size = {d: sum(u[d] for u in left) for d in DIFFICULTIES}
    print("\n  pass 1 left by difficulty: " + ", ".join(f"{by_size[d]} {d}" for d in DIFFICULTIES))
    o = s["ownership"]
    print(f"  ownership.toml: {o['routed']} functions routed in {o['subsystems']} subsystems, "
          f"{o['default_owned']} run owned code by default")
    p = s["pipeline"]
    statuses = ", ".join(f"{n} {k}" for k, n in sorted(p["records"].items(), key=lambda x: -x[1]))
    print(f"  pipeline records: {statuses}; {p['reviewed']} reviewed")
    if show_units:
        print(f"\n  {'unit':<8} {'ported':>6} {'left':>5}   " + " ".join(f"{d:>3}" for d in DIFFICULTIES))
        for unit, u in sorted(s["units"].items(), key=lambda x: (-x[1]["left"], x[0])):
            print(f"  {unit:<8} {u['ported']:>6} {u['left']:>5}   " + " ".join(f"{u[d]:>3}" for d in DIFFICULTIES))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=pathlib.Path, default=ROOT, help="the checkout to count (default: this one)")
    parser.add_argument("--tree", type=pathlib.Path, nargs="+", default=[],
                        help="worktrees whose additional ports to count against --root")
    parser.add_argument("--units", action="store_true", help="a row per unit")
    parser.add_argument("--left", metavar="UNIT", help="list a unit's unported pass-1 functions")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    s = summary(args.root)
    base = set(s.pop("ported_names"))
    flight = {}
    for tree in args.tree:
        names = set(summary(tree)["ported_names"])
        flight[str(tree)] = len(names - base)
    if args.json:
        s["in_flight"] = flight
        json.dump(s, sys.stdout, indent=1)
        print()
        return 0
    if args.left:
        for name, difficulty in s["left"].get(args.left, []):
            print(f"{name}\t{difficulty}")
        return 0
    report(s, args.units)
    if flight:
        print("\n  in flight (ported in the worktree, not in this checkout):")
        for tree, n in flight.items():
            print(f"    {n:>4}  {tree}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
