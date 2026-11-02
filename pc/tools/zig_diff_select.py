#!/usr/bin/env python3
"""Which zig_diff specs a change can affect: verify.sh's quick lane runs
pc/tools/zig_diff.py on those only (pc/tests/zig_diff_changed.py), not the
ten shards of every spec (verify.sh --full, CTest zig_diff_*).

    python3 pc/tools/zig_diff_select.py [BASE]          the specs the change since BASE (default main) affects
    python3 pc/tools/zig_diff_select.py --files PATH... the specs these paths affect (no base: a changed
                                                        specs.toml or ownership.toml then means every spec)

A zig_diff run of function F reads (zig_diff.py, Build): F's specs.toml entry
and spec file, F's route in game/ownership.toml (its port file and the views
of its unit), the port file alone (copied with game/common, nothing else of
game/), F's original C unit, and the shared harness. So:

  game/<overlay>/<file>.zig           the functions routed to that file
  pc/tests/zig_diff/<spec>.zig        the functions whose entry names that spec
  pc/tests/zig_diff/specs.toml        the entries that differ from BASE (or are new)
  game/ownership.toml                 the functions whose route differs: zig path, overlay,
                                      unit, or the views of the unit (or new)
  pc/docs/porting/battle/pipeline/**/F.json   F (its pipeline record)
  src/**/UNIT.c                       the functions of that unit
  shared infrastructure (INFRA, game/common/ reachable from common.zig)
                                      every spec: the full shards, with the reason
Anything else changes no zig_diff result. Only functions with a spec count.
"""
import json
import pathlib
import re
import subprocess
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPECS_DIR = "pc/tests/zig_diff"
SPECS_TOML = f"{SPECS_DIR}/specs.toml"
OWNERSHIP = "game/ownership.toml"
PIPELINE = "pc/docs/porting/battle/pipeline/"

# Files every zig_diff run reads: one of them changed means every spec.
INFRA = {
    "pc/tools/zig_diff.py": "the zig_diff harness",
    "pc/tools/ownership.py": "zig_diff's route lookup and body stripping",
    "pc/tools/bakeoff_context.py": "zig_diff's C body extraction",
    "pc/tools/zig_toolchain.py": "the Zig compiler zig_diff runs",
    "pc/tools/zig_views.py": "every unit's generated views",
    "pc/tools/zig_c.py": "every unit's generated c module",
    "pc/tools/battle_statics.py": "the statics zig_diff's reference objects globalise",
    "pc/docs/porting/battle/STATE.tsv": "the statics zig_diff's reference objects globalise (its list)",
    "pc/docs/porting/battle/MEANINGS.tsv": "the views' readable names",
    f"{SPECS_DIR}/main.zig": "the zig_diff harness",
    f"{SPECS_DIR}/zdiff.zig": "the spec API",
    f"{SPECS_DIR}/zdiff_cov.c": "the coverage lane's runtime",
}


class ZigSelection:
    """functions: the sorted specs to run, or None for every spec (fallback
    names why). why: {function: [the paths that selected it]}."""
    def __init__(self, total):
        self.total, self.functions, self.fallback, self.why = total, [], None, {}

    def add(self, functions, path):
        for f in functions:
            self.why.setdefault(f, []).append(path)

    def every(self, reason):
        if self.fallback is None:
            self.fallback = reason

    def summary(self):
        if self.fallback:
            return f"zig_diff: every spec ({self.total}): {self.fallback}"
        return f"zig_diff: {len(self.functions)} changed spec{'' if len(self.functions) == 1 else 's'} (of {self.total})"


def _common_imports(root):
    """game/common files the copied common module reaches from common.zig."""
    seen, todo = set(), ["common.zig"]
    while todo:
        name = todo.pop()
        if name in seen or not (root / "game/common" / name).exists():
            continue
        seen.add(name)
        todo += re.findall(r'@import\("([\w./]+\.zig)"\)', (root / "game/common" / name).read_text())
    return {f"game/common/{n}" for n in seen}


def _routes(text):
    """{function: (overlay, unit, zig path, the unit's views)} of an ownership.toml."""
    subs = tomllib.loads(text).get("subsystem", {})
    views = {}
    for s in subs.values():
        v = views.setdefault(s.get("unit"), {"types": set(), "globals": set()})
        for k in v:
            v[k] |= set(s.get("views", {}).get(k, []))
    out = {}
    for s in subs.values():
        unit = s.get("unit")
        unit_views = tuple(sorted(views[unit]["types"])), tuple(sorted(views[unit]["globals"]))
        for f in s.get("functions", []):
            out[f["name"]] = (s.get("overlay"), unit, f.get("zig"), unit_views)
    return out


def _differing(old, new):
    return sorted(k for k in new if old.get(k) != new[k])


def select(files, old_text=None, root=ROOT):
    """The specs `files` can affect. old_text(path): the file's text at the
    base (None: absent there); without it a changed specs.toml or
    ownership.toml cannot be diffed and means every spec."""
    specs = tomllib.loads((root / SPECS_TOML).read_text())["function"]
    sel = ZigSelection(len(specs))
    try:
        routes = _routes((root / OWNERSHIP).read_text())
    except (OSError, tomllib.TOMLDecodeError) as error:
        sel.every(f"{OWNERSHIP} unreadable ({error})")
        routes = {}
    port_files = {}
    for function, (overlay, unit, zig, _) in routes.items():
        if zig:
            port_files.setdefault(f"game/{overlay}/{zig.split('.')[0]}.zig", set()).add(function)
    units = {}
    for function, (_, unit, _, _) in routes.items():
        units.setdefault(unit, set()).add(function)
    common = _common_imports(root)
    by_spec = {}
    for function, entry in specs.items():
        by_spec.setdefault(f"{SPECS_DIR}/{entry['spec']}", set()).add(function)

    for path in files:
        if path in INFRA:
            sel.every(f"{path} changed ({INFRA[path]})")
        elif path in common:
            sel.every(f"{path} changed (game/common, which every port compiles with)")
        elif path in port_files:
            sel.add(port_files[path], path)
        elif path in by_spec:
            sel.add(by_spec[path], path)
        elif path in (SPECS_TOML, OWNERSHIP):
            if old_text is None:
                sel.every(f"{path} changed and there is no base to diff it against")
                continue
            old = old_text(path)
            try:
                if path == SPECS_TOML:
                    before = tomllib.loads(old)["function"] if old else {}
                    sel.add(_differing(before, specs), path)
                else:
                    sel.add(_differing(_routes(old) if old else {}, routes), path)
            except (tomllib.TOMLDecodeError, KeyError) as error:
                sel.every(f"{path} at the base unreadable ({error})")
        elif path.startswith(PIPELINE) and path.endswith(".json"):
            name = pathlib.PurePosixPath(path).stem
            try:
                name = json.loads((root / path).read_text()).get("function", name)
            except (OSError, ValueError):
                pass  # deleted or not a record: the file name
            sel.add([name], path)
        elif path in units:
            sel.add(units[path], path)
    sel.why = {f: p for f, p in sel.why.items() if f in specs}
    sel.functions = None if sel.fallback else sorted(sel.why)
    return sel


def base_text(revision, root=ROOT):
    """old_text for select(): a file's text at `revision` (None: absent)."""
    def read(path):
        r = subprocess.run(["git", "show", f"{revision}:{path}"], cwd=root, capture_output=True, text=True)
        return r.stdout if r.returncode == 0 else None
    return read


def main():
    import argparse
    sys.path.insert(0, str(ROOT / "pc/tests"))
    import affected
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("base", nargs="?", default="main")
    parser.add_argument("--files", nargs="+", help="these paths instead of the diff")
    args = parser.parse_args()
    if args.files:
        sel = select(args.files)
    else:
        sel = select(affected.changed_files(args.base), base_text(affected.base_revision(args.base)))
    print(sel.summary())
    for f in sel.functions or []:
        print(f"  {f:<40} {', '.join(sel.why[f])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
