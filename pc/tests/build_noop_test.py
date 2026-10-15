"""build_boot.py's incremental build (pc/tools/build_cache.py).

Synthetic cases, always (a scratch tree and gcc): the build stamp sees an
edited, a reverted (touched), a new and a deleted input, an option change
and a changed or missing output; the object cache reuses a compile only
when its command and every file it read are unchanged (headers through
gcc's dependency list, preprocessed .i input), replays its warnings, and
gives back the compiler's own object even after the build edited it; the
result cache keys on file content.

Real builds, when built: each configuration with a build stamp in pc/build
(boot, boot64, win64) is checked with build_boot.py --check, which must say
"up to date" in under 2 s. A stale configuration (sources edited since its
last build) is skipped, as the build would rebuild it."""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from build_cache import ObjectCache, ResultCache, Stamp  # noqa: E402

failures = []


def check(ok, message):
    print(("ok   " if ok else "FAIL ") + message, flush=True)
    if not ok:
        failures.append(message)


def later(path):
    """Make sure the next write gets a new modification time."""
    st = os.stat(path)
    os.utime(path, ns=(st.st_atime_ns, st.st_mtime_ns + 1_000_000))


def synthetic(base):
    src, out = base / "src", base / "out"
    src.mkdir()
    out.mkdir()
    (src / "a.h").write_text("#define A 1\n")
    (src / "a.c").write_text('#include "a.h"\nint a(void) { return A; }\n'
                             "int w(int x) { int y; return x + y; }\n")
    (src / "b.c").write_text("int b(void) { return 2; }\n")
    trees, files = [("src", None)], []
    binary = out / "program"
    binary.write_text("v1")

    stamp = Stamp(base, out, {"opt": 1})
    check(not stamp.up_to_date(trees, files), "stamp: no stamp yet -> build")
    stamp.write(trees, files, [binary])
    check(stamp.up_to_date(trees, files), "stamp: nothing changed -> no-op")
    later(src / "a.h")
    check(not stamp.up_to_date(trees, files), "stamp: header touched -> build")
    stamp.write(trees, files, [binary])
    (src / "c.c").write_text("int c;\n")
    check(not stamp.up_to_date(trees, files), "stamp: new source -> build")
    stamp.write(trees, files, [binary])
    (src / "c.c").unlink()
    check(not stamp.up_to_date(trees, files), "stamp: deleted source -> build")
    stamp.write(trees, files, [binary])
    check(not Stamp(base, out, {"opt": 2}).up_to_date(trees, files), "stamp: other options -> build")
    binary.write_text("v2")
    later(binary)
    check(not stamp.up_to_date(trees, files), "stamp: output changed since -> build")
    stamp.write(trees, files, [binary])
    binary.unlink()
    check(not stamp.up_to_date(trees, files), "stamp: output missing -> build")
    binary.write_text("v1")
    stamp.write(trees, files, [binary])
    extra = base / "lib.a"
    extra.write_text("1")
    stamp.write(trees, files + [extra], [binary])
    extra.write_text("22")
    check(not stamp.up_to_date(trees, files + [extra]), "stamp: listed file changed -> build")
    # Headers outside the trees come from the compiles' dependency lists.
    outside = base / "inc"
    outside.mkdir()
    (outside / "sys.h").write_text("#define S 3\n")
    (src / "s.c").write_text('#include "sys.h"\nint s(void) { return S; }\n')
    stamp = Stamp(base, out, {"opt": 1})
    cache = ObjectCache(base, out, stamp, "toolchain")
    cache.compile(["gcc", "-O2", "-I" + str(outside), "-c", str(src / "s.c"), "-o", str(out / "s.o")], check=True)
    stamp.write(trees, files, [binary])
    check(str(outside / "sys.h") in json.loads(stamp.path.read_text())["deps"],
          "stamp: records headers the compiles read")
    later(outside / "sys.h")
    check(not stamp.up_to_date(trees, files), "stamp: such a header touched -> build")

    # Object cache.
    stamp = Stamp(base, out, {})
    cache = ObjectCache(base, out, stamp, "toolchain")
    command = ["gcc", "-O2", "-Wall", "-Wuninitialized", "-c", "src/a.c", "-o", "out/a.o"]
    calls = []
    real_run = subprocess.run

    def counting_run(args, *rest, **kwargs):
        calls.append(args)
        return real_run(args, *rest, **kwargs)
    subprocess.run = counting_run
    try:
        first = cache.compile(command)
        check(first.returncode == 0 and len(calls) == 1, "object cache: first compile runs gcc")
        original = (out / "a.o").read_bytes()
        subprocess.run(["objcopy", "--redefine-sym=a=renamed", "out/a.o"], cwd=base, check=True)
        second = cache.compile(command)
        check(len(calls) == 2, "object cache: unchanged -> no compile")  # (the objcopy above)
        check((out / "a.o").read_bytes() == original, "object cache: gives back the compiler's object")
        check(second.stderr == first.stderr and "uninitialized" in first.stderr,
              "object cache: replays the compiler's warnings")
        later(src / "a.h")
        cache = ObjectCache(base, out, stamp, "toolchain")
        cache.compile(command)
        check(len(calls) == 2, "object cache: header touched, same content -> no compile")
        (src / "a.h").write_text("#define A 7\n")
        cache.compile(command)
        check(len(calls) == 3, "object cache: header edited -> compile")
        check((out / "a.o").read_bytes() != original, "object cache: the edit is in the object")
        (src / "a.h").write_text("#define A 1\n")
        cache.compile(command)
        check(len(calls) == 4 and (out / "a.o").read_bytes() == original,
              "object cache: edit reverted -> compile, same object as before")
        cache.compile(command[:1] + ["-O1"] + command[2:])
        check(len(calls) == 5, "object cache: other flags -> compile")
        cache.compile(command[:1] + ["-O1"] + command[2:])
        check(len(calls) == 5, "object cache: the same again -> no compile")
        ObjectCache(base, out, stamp, "other toolchain").compile(command)
        check(len(calls) == 6, "object cache: other compiler -> compile")
        # Preprocessed input: no dependency list; the .i is what was read.
        real_run(["gcc", "-E", "src/b.c", "-o", "out/b.i"], cwd=base, check=True)
        pre = ["gcc", "-O2", "-c", "out/b.i", "-o", "out/b.o"]
        cache.compile(pre)
        cache.compile(pre)
        check(len(calls) == 7, "object cache: preprocessed input cached")
        (out / "b.i").write_text((out / "b.i").read_text().replace("return 2", "return 3"))
        cache.compile(pre)
        check(len(calls) == 8, "object cache: preprocessed input edited -> compile")
        broken = ["gcc", "-c", "src/missing.c", "-o", "out/missing.o"]
        check(cache.compile(broken).returncode != 0, "object cache: a failing compile fails")
        check(cache.compile(broken).returncode != 0 and len(calls) == 10, "object cache: failures are not cached")
    finally:
        subprocess.run = real_run

    # Result cache.
    results = ResultCache(out, "toolchain")
    first = results.run(["nm", str(out / "a.o")], base)
    check(results.run(["nm", str(out / "a.o")], base) == first, "result cache: same object -> same output")
    real_run(["objcopy", "--redefine-sym=a=renamed", str(out / "a.o")], check=True)
    check(b"renamed" in results.run(["nm", str(out / "a.o")], base), "result cache: object edited -> new output")
    results.prune()
    check(len(list(results.dir.iterdir())) == 2, "result cache: prune keeps what this build used")


def real_builds():
    for tree in ["boot", "boot64", "win64"]:
        stamp = ROOT / "pc/build" / tree / "build-stamp.json"
        if not stamp.exists():
            print(f"skip pc/build/{tree}: never built with a build stamp")
            continue
        config = json.loads(stamp.read_text())["config"]
        args = ["--arch", config["arch"]] + (["--target", "windows"] if config["target"] == "windows" else []) \
            + (["--battle"] if config["battle"] and config["arch"] == "32" else []) \
            + (["--no-audio"] if config["no_audio"] else []) \
            + (["--nrd", "off"] if config["arch"] == "64" and not config["nrd"][0] else [])
        start = time.monotonic()
        result = subprocess.run([sys.executable, str(ROOT / "pc/tools/build_boot.py"), "--check", *args],
                                cwd=ROOT, capture_output=True, text=True, timeout=600)
        elapsed = time.monotonic() - start
        if result.returncode == 1:
            print(f"skip pc/build/{tree}: changed since its last build ({result.stdout.strip()})")
            continue
        check(result.returncode == 0, f"pc/build/{tree}: build_boot.py --check {' '.join(args)} ran "
              f"({result.stdout.strip()}{result.stderr.strip()[-300:]})")
        check(elapsed < 2.0, f"pc/build/{tree}: up to date, checked in {elapsed:.2f} s (< 2 s)")


base = pathlib.Path(tempfile.mkdtemp(prefix="build-noop-"))
try:
    synthetic(base)
finally:
    shutil.rmtree(base)
real_builds()
print(f"{len(failures)} failure(s)" if failures else "build_noop_test: all checks passed")
sys.exit(1 if failures else 0)
