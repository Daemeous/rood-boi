#!/usr/bin/env python3
"""The suites a change can affect (run.py --changed [BASE]).

    python3 pc/tests/affected.py [BASE] [--all-files] [--files PATH...]

Reads pc/tests/affected.toml (the table: core files, path rules, the
baseline) and derives the rest from the tree itself: test scripts and the
modules they import, and the files pc/CMakeLists.txt compiles into the CTest
probes. A file nothing knows about selects everything. Run on its own it
prints the selection for BASE (default main); --all-files lists the tracked
files no rule covers (each would run the full suite).
"""
import ast
import fnmatch
import pathlib
import re
import subprocess
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
TABLE = ROOT / "pc/tests/affected.toml"


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout


def changed_files(base="main"):
    """Files that differ from where this tree left BASE: commits since the
    merge base, uncommitted edits and untracked files (renames as both
    names)."""
    fork = git("merge-base", base, "HEAD").strip()
    names = git("diff", "--name-only", "--no-renames", fork).split()
    names += git("ls-files", "--others", "--exclude-standard").split()
    return sorted(set(names))


def _match(path, patterns):
    return next((p for p in patterns if fnmatch.fnmatchcase(path, p)), None)


def _imports(path):
    """Top-level module names a Python file imports."""
    try:
        tree = ast.parse(path.read_text(errors="replace"))
    except SyntaxError:
        return set()
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            names |= {a.name.split(".")[0] for a in node.names}
        elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
            names.add(node.module.split(".")[0])
    return names


def script_users(suites):
    """{repository path of a pc/tests or pc/tools script: suite names that run it
    or import it, directly or not}."""
    modules = {p.stem: p for d in ("pc/tools", "pc/tests") for p in sorted((ROOT / d).glob("*.py"))}
    graph = {stem: {m for m in _imports(p) if m in modules and m != stem} for stem, p in modules.items()}
    users = {}
    for suite in suites:
        roots = [pathlib.Path(w) for w in suite.command if w.endswith((".py", ".sh"))] + list(suite.sources)
        seen, todo = set(), [r.stem for r in roots if r.suffix == ".py" and r.stem in modules]
        for r in roots:
            rel = str(r.resolve().relative_to(ROOT)) if r.is_absolute() else str(r)
            users.setdefault(rel, set()).add(suite.name)
        while todo:
            stem = todo.pop()
            if stem in seen:
                continue
            seen.add(stem)
            todo += graph[stem]
        for stem in seen:
            users.setdefault(str(modules[stem].relative_to(ROOT)), set()).add(suite.name)
    return users


def cmake_files():
    """Repository paths pc/CMakeLists.txt names (probe sources and what they
    compile, test scripts, asset directories)."""
    text = (ROOT / "pc/CMakeLists.txt").read_text()
    found = set()
    for word in re.findall(r"(?:src|tests|assets|third_party)/[\w./-]+", text):
        found.add("pc/" + word.rstrip("/"))
    return found


class Selection:
    def __init__(self):
        self.full = []  # (file, why) that run everything
        self.reasons = {}  # suite name -> [why]
        self.quiet = []  # files that select nothing

        self.selecting = set()  # files that selected a suite

    def add(self, names, why, path):
        for name in names:
            self.reasons.setdefault(name, []).append(why)
            self.selecting.add(path)


def select(files, suites, table=None):
    """Which of `suites` (run.py Suite objects) `files` can affect."""
    table = table or tomllib.loads(TABLE.read_text())
    names = [s.name for s in suites]
    sel = Selection()
    core = table.get("core", {}).get("paths", [])
    baseline = table.get("baseline", {})
    rules = table.get("rule", [])
    users = script_users(suites)
    cmake = cmake_files()

    def expand(patterns):
        return sorted({n for p in patterns for n in names if fnmatch.fnmatchcase(n, p)})

    for path in files:
        hit = _match(path, core)
        if hit:
            sel.full.append((path, f"core ({hit})"))
            continue
        known = False
        for rule in rules:
            hit = _match(path, rule["paths"])
            if hit:
                known = True
                sel.add(expand(rule["suites"]), f"{path} ({hit})", path)
        if path in users:
            known = True
            sel.add(sorted(users[path]), f"{path} (runs or imports it)", path)
        elif path.startswith("pc/tests/") and path.endswith((".py", ".sh")) and "/" not in path[len("pc/tests/"):]:
            known = True  # a script no suite runs or imports
        if path in cmake or fnmatch.fnmatchcase(path, "pc/tests/*_probe.*") or \
                fnmatch.fnmatchcase(path, "pc/tests/*_stub*"):
            known = True
            sel.add(expand(["ctest"]), f"{path} (CTest probe or source)", path)
        if not known:
            sel.full.append((path, "no rule covers it"))
            continue
        hit = _match(path, baseline.get("paths", []))
        if hit:
            sel.add(expand(baseline.get("suites", [])), f"{path} (baseline: {hit})", path)
        if path not in sel.selecting:
            sel.quiet.append(path)
    return sel


def report(sel, total, out=print):
    if sel.full:
        out(f"--changed: the full suite ({total}), because:")
        for path, why in sel.full[:12]:
            out(f"  {path}: {why}")
        if len(sel.full) > 12:
            out(f"  ... and {len(sel.full) - 12} more")
        return
    out(f"--changed: {len(sel.reasons)} of {total} suites")
    for name in sorted(sel.reasons):
        why = sel.reasons[name]
        out(f"  {name:<40} {why[0]}" + (f" (+{len(why) - 1} more)" if len(why) > 1 else ""))
    if sel.quiet:
        out(f"  (no suite for: {', '.join(sel.quiet[:8])}{' ...' if len(sel.quiet) > 8 else ''})")


def main():
    import argparse
    sys.path.insert(0, str(ROOT / "pc/tests"))
    import run
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("base", nargs="?", default="main")
    parser.add_argument("--all-files", action="store_true", help="list tracked files no rule covers")
    parser.add_argument("--files", nargs="+", help="these paths instead of the diff (what would they run?)")
    args = parser.parse_args()
    suites = run.suites(run.default_args(), "egl")
    if args.all_files:
        files = git("ls-files").split()
        uncovered = [p for p, why in select(files, suites).full if why == "no rule covers it"]
        print("\n".join(uncovered) or "every tracked file is covered")
        return 0
    report(select(args.files or changed_files(args.base), suites), len(suites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
