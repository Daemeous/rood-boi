#!/usr/bin/env python3
"""Rebuild only the engine library of the development split
(pc/build/dev64/librood_engine.so; pc/docs/plans/2026-10-19-engine-reload.md),
for a running rood_host to reload (RPC reload; pc/tools/dev_loop.sh --reload).

    python3 pc/tools/engine_lib.py [--jobs N] [--quiet]

Reads pc/build/dev64/engine_library.json (written by build_boot.py --arch 64
--engine-library): each engine unit's compile command, the library's link
and the host's units. Then:
  1. a change the host is built from (its units, the game's, a header one of
     them includes, the build scripts) cannot be reloaded: exit 2, naming the
     files (build_boot.py --engine-library and a restart take it);
  2. the shader headers are regenerated when pc/shaders changed;
  3. the engine units whose source or headers changed since their object
     (gcc depfiles) are recompiled, in parallel;
  4. the library is linked to a staged file and checked against the boundary
     the host was built with (every function and host-owned variable it
     needs, at the same sizes; nothing undefined the host does not export):
     a mismatch is exit 2 as well;
  5. the staged library replaces librood_engine.so (rename: a running host
     reloads whole files only).
Exit 0: a new library is in place; 3: nothing changed; 1: a compile or link
failed. ROOD_BUILD_LOCK=file serialises with other builds (flock)."""

import argparse
import concurrent.futures
import fcntl
import json
import os
import pathlib
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "pc/build/dev64"
# The build itself: a change to one of these needs build_boot.py.
BUILD_SCRIPTS = ["pc/tools/build_boot.py", "pc/tools/engine_boundary.py", "pc/tools/snapshot_layout.py",
                 "pc/tools/build_cache.py", "pc/tools/event_callbacks.py", "pc/tools/import_types.py",
                 "pc/tools/lp64_report.py", "pc/tools/zig_toolchain.py", "pc/tools/ownership.py"]
SHARED = ["/usr/lib/x86_64-linux-gnu/libstdc++.so.6", "/lib/x86_64-linux-gnu/libc.so.6",
          "/lib/x86_64-linux-gnu/libm.so.6", "/lib/x86_64-linux-gnu/libgcc_s.so.1"]


def mtime(path):
    try:
        return os.stat(path).st_mtime_ns
    except OSError:
        return None


def depfile_paths(path):
    """The prerequisites of a gcc depfile (the target first is skipped)."""
    try:
        text = pathlib.Path(path).read_text()
    except OSError:
        return None
    text = text.replace("\\\n", " ")
    out = []
    for line in text.splitlines():
        if ":" not in line:
            continue
        _, deps = line.split(":", 1)
        out += [ROOT / d if not os.path.isabs(d) else pathlib.Path(d) for d in deps.split()]
    return out


def deps_command(command, depfile):
    """A unit's compile command made to write its depfile only (-MM)."""
    cmd = list(command)
    at = cmd.index("-o")
    del cmd[at:at + 2]
    cmd = [a for a in cmd if a != "-c"]
    return cmd + ["-MM", "-MF", str(depfile), "-MT", "unit"]


def run(cmd):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    say = (lambda *a: None) if args.quiet else (lambda *a: print(*a, flush=True))
    manifest_path = OUT / "engine_library.json"
    if not manifest_path.exists():
        print(f"no {manifest_path.relative_to(ROOT)}: run python3 pc/tools/build_boot.py --arch 64 --engine-library",
              file=sys.stderr)
        return 2
    lock = None
    if os.environ.get("ROOD_BUILD_LOCK"):
        lock = open(os.environ["ROOD_BUILD_LOCK"], "a")
        fcntl.flock(lock, fcntl.LOCK_EX)
    started = time.monotonic()
    manifest = json.loads(manifest_path.read_text())
    boundary = json.loads((OUT / "engine_boundary.json").read_text())
    host = OUT / "rood_host"
    host_time = mtime(host) or 0
    pool = concurrent.futures.ThreadPoolExecutor(max(1, args.jobs))

    # 1. What the host is built from.
    newer = [s for s in BUILD_SCRIPTS + [str(manifest_path.relative_to(ROOT))] if (mtime(ROOT / s) or 0) > host_time
             and not s.endswith("engine_library.json")]
    host_deps = set()
    for depfile in manifest["host_depfiles"]:
        host_deps.update(depfile_paths(ROOT / depfile) or [])
    # The host's own units: their headers from -MM (cached while the unit's command and source stay).
    cache_path = OUT / "engine_lib_host_deps.json"
    try:
        cache = json.loads(cache_path.read_text())
    except (OSError, ValueError):
        cache = {}
    def host_unit_deps(unit):
        key = json.dumps(unit["command"])
        known = cache.get(unit["object"])
        if known and known["key"] == key and all((mtime(ROOT / d) or -1) == t for d, t in known["files"].items()):
            return unit["object"], known
        depfile = OUT / (pathlib.Path(unit["object"]).name + ".host.d")
        result = run(deps_command(unit["command"], depfile))
        files = [str(p.relative_to(ROOT)) if str(p).startswith(str(ROOT)) else str(p)
                 for p in (depfile_paths(depfile) or [])] if result.returncode == 0 else []
        return unit["object"], {"key": key, "files": {f: mtime(ROOT / f) or -1 for f in files}}
    for obj, entry in pool.map(host_unit_deps, manifest["host_units"]):
        cache[obj] = entry
        host_deps.update(ROOT / f for f in entry["files"])
    cache_path.write_text(json.dumps(cache))
    newer += sorted(str(p.relative_to(ROOT)) if str(p).startswith(str(ROOT)) else str(p)
                    for p in host_deps if (mtime(p) or 0) > host_time)
    if newer:
        print("not engine-only: the host is built from " + ", ".join(newer[:8]) + (" ..." if len(newer) > 8 else "")
              + "; run python3 pc/tools/build_boot.py --arch 64 --engine-library and restart", file=sys.stderr)
        return 2

    # 2. Shaders: the headers the renderers include.
    shaders = max((mtime(p) or 0 for p in (ROOT / "pc/shaders").rglob("*") if p.is_file()), default=0)
    if any(shaders > (mtime(pathlib.Path(cmd[-1])) or 0) for cmd in manifest["shaders"]):
        for cmd in manifest["shaders"]:
            result = run(cmd)
            if result.returncode:
                print(result.stdout + result.stderr, file=sys.stderr)
                return 1
        say("shaders regenerated")

    # 3. Engine units whose inputs changed.
    def stale(unit):
        obj = ROOT / unit["object"]
        depfile = obj.with_suffix(".d")
        deps = depfile_paths(depfile)
        if deps is None:  # the first time: the depfile from the preprocessor
            result = run(deps_command(unit["command"], depfile))
            deps = depfile_paths(depfile) if result.returncode == 0 else None
        built = mtime(obj)
        if built is None or deps is None:
            return unit
        return unit if any((mtime(d) or built + 1) > built for d in deps) else None
    todo = [u for u in pool.map(stale, manifest["units"]) if u]
    checked = time.monotonic()
    if not todo and (mtime(OUT / "librood_engine.so") or 0) >= max(mtime(ROOT / u["object"]) or 0
                                                                     for u in manifest["units"]):
        say(f"engine library up to date ({checked - started:.2f} s)")
        return 3

    def compile_unit(unit):
        obj = ROOT / unit["object"]
        cmd = unit["command"] + ["-MMD", "-MF", str(obj.with_suffix(".d"))]
        return unit, run(cmd)
    failed = False
    for unit, result in pool.map(compile_unit, todo):
        if result.returncode:
            failed = True
            print(f"{unit['object']}:\n{result.stderr}", file=sys.stderr)
        elif result.stderr and not args.quiet:
            print(result.stderr, end="", file=sys.stderr)
    if failed:
        return 1
    compiled = time.monotonic()

    # 4. Link, staged, and check it against the host's boundary.
    library = OUT / "librood_engine.so"
    staged = library.with_name(library.name + ".new")
    link = manifest["link"][:-1] + [str(staged)]
    result = run(link)
    if result.returncode:
        print(result.stderr, file=sys.stderr)
        return 1
    linked = time.monotonic()
    defined, sizes = set(), {}
    for line in run(["readelf", "--dyn-syms", "-W", str(staged)]).stdout.splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[6] != "UND" and re.fullmatch(r"\d+:", parts[0]):
            name = parts[7].split("@")[0]
            defined.add(name)
            sizes[name] = int(parts[2], 0) if parts[2].isdigit() or parts[2].startswith("0x") else int(parts[2])
    problems = [f"no {n}" for n in boundary["functions"] if n not in defined]
    problems += [f"{n} is {sizes.get(n)} bytes, the host's {d['size']}" for n, d in boundary["data"].items()
                 if sizes.get(n) != d["size"]]
    provided = set(boundary["exports"])
    for shared in SHARED:
        provided |= {p[2].split("@")[0] for p in map(str.split, run(["nm", "-D", "--defined-only", shared]).stdout
                                                     .splitlines()) if len(p) >= 3}
    for line in run(["nm", "-D", "--undefined-only", str(staged)]).stdout.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == "U" and parts[1].split("@")[0] not in provided:
            problems.append(f"needs {parts[1]}, which the host does not export")
    if problems:
        staged.unlink(missing_ok=True)
        print("the library no longer fits the host (" + "; ".join(problems[:8])
              + "): run python3 pc/tools/build_boot.py --arch 64 --engine-library and restart", file=sys.stderr)
        return 2
    os.replace(staged, library)
    say(f"engine library: {len(todo)} unit{'s' if len(todo) != 1 else ''} compiled in {compiled - checked:.2f} s, "
        f"linked in {linked - compiled:.2f} s ({time.monotonic() - started:.2f} s in all)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
