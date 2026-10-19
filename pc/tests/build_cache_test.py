#!/usr/bin/env python3
"""The shared build caches (pc/tools/build_cache.py, pc/tools/shared_cache.py)
on synthetic checkouts in a temporary directory, with a private
$ROOD_BUILD_CACHE (the real cache is never touched):

- the object cache is shared by checkouts at different paths: the same
  source compiled the same way in a second checkout is reused, and the
  object is byte for byte the one a compile there makes (no checkout path
  in it);
- an edited header recompiles only the objects that read it; switching
  the flags or the compiler identity misses;
- the store is pruned to its cap, least recently used first, and a pruned
  object is compiled again;
- a third-party entry is built once when several processes ask at the same
  time (the lock), and a failed build leaves nothing behind.

    python3 pc/tests/build_cache_test.py
"""
import multiprocessing
import os
import pathlib
import shutil
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import build_cache  # noqa: E402
import shared_cache  # noqa: E402

failures = []


def check(condition, what):
    print(("ok    " if condition else "FAIL  ") + what)
    if not condition:
        failures.append(what)


def checkout(base, name, header="#define X 3\n"):
    root = base / name
    (root / "src").mkdir(parents=True)
    (root / "inc").mkdir()
    (root / "out").mkdir()
    (root / "inc/h.h").write_text(header + "static const char *where = __FILE__;\n")
    (root / "src/a.c").write_text('#include "h.h"\nint a(void) { return X + (where[0] != 0); }\n')
    (root / "src/b.c").write_text("int b(void) { return 7; }\n")
    return root


def build(root, identity="gcc-1", flags=("-O2",), env=None):
    """Compile a.c and b.c in `root` like build_boot.py does: (hits, misses)."""
    old = dict(os.environ)
    os.environ.update(env or {})
    try:
        stamp = build_cache.Stamp(root, root / "out", {})
        cache = build_cache.ObjectCache(root, root / "out", stamp, identity)
        for name in ("a", "b"):
            cache.compile(["gcc", *flags, "-g", "-I" + str(root / "inc"), "-c", f"src/{name}.c",
                           "-o", str(root / "out" / f"{name}.o")], check=True)
        return cache.hits, cache.misses
    finally:
        os.environ.clear()
        os.environ.update(old)


def build_entry(name, key, delay, result):
    def make(tmp):
        time.sleep(delay)
        (tmp / "made-by").write_text(str(os.getpid()))
    path = shared_cache.entry(name, key, make)
    result.put((path / "made-by").read_text())


def failing(tmp):
    (tmp / "half").write_text("x")
    raise RuntimeError("build failed")


def main():
    with tempfile.TemporaryDirectory(prefix="build-cache-test-") as scratch:
        scratch = pathlib.Path(scratch)
        os.environ["ROOD_BUILD_CACHE"] = str(scratch / "cache")
        os.environ.pop("ROOD_OBJECT_CACHE", None)
        os.environ.pop("ROOD_OBJECT_CACHE_MAX", None)

        a = checkout(scratch, "main")
        b = checkout(scratch, "worktrees/agent")
        check(build(a) == (0, 2), "first checkout: both objects compiled")
        check(build(b) == (2, 0), "second checkout at another path: both objects reused")
        reused = (b / "out/a.o").read_bytes()
        build(b, env={"ROOD_OBJECT_CACHE": "0"})
        check((b / "out/a.o").read_bytes() == reused, "a reused object is the one a compile there makes")
        check(str(a).encode() not in reused and str(scratch).encode() not in reused,
              "objects name no checkout path (-ffile-prefix-map)")

        (b / "inc/h.h").write_text("#define X 4\nstatic const char *where = __FILE__;\n")
        check(build(b) == (1, 1), "an edited header recompiles only the object that includes it")
        check(build(a) == (2, 0), "the first checkout still reuses its own variant")
        check(build(b) == (2, 0), "and the second its edited one")
        check(build(b, flags=("-O1",)) == (0, 2), "other flags miss")
        check(build(b, identity="gcc-2") == (0, 2), "another compiler identity misses")

        store = build_cache.object_store()
        blobs = build_cache.store_usage(store)
        total = sum(size for _, size, _ in blobs)
        oldest = min(blobs)[2]
        for i, (_, _, path) in enumerate(sorted(blobs)):
            os.utime(path, (1000 + i, 1000 + i))  # a definite use order
        newest = max(blobs)[2]
        removed = build_cache.prune(store, total - 1)
        check(removed is not None and not os.path.exists(oldest) and os.path.exists(newest),
              "over its cap the store drops the least recently used objects")
        check(build_cache.prune(store, total) is None, "under its cap nothing is pruned")
        build_cache.prune(store, 0)
        check(build(a) == (0, 2), "pruned objects are compiled again")

        queue = multiprocessing.Queue()
        workers = [multiprocessing.Process(target=build_entry, args=("demo", {"v": 1}, 0.5, queue))
                   for _ in range(3)]
        for w in workers:
            w.start()
        makers = {queue.get(timeout=60) for _ in workers}
        for w in workers:
            w.join(60)
        check(len(makers) == 1, "three processes asking for one entry: built once, all use it")
        try:
            shared_cache.entry("broken", {"v": 1}, failing)
        except RuntimeError:
            pass
        left = [p.name for p in (scratch / "cache/third-party").iterdir() if p.name.startswith("broken")]
        check(left in ([], ["broken-" + shared_cache.digest({"v": 1}) + ".lock"]),
              "a failed build leaves no entry or temporary directory")
        check(shared_cache.tree_key(ROOT / "pc/third_party/smaa") == shared_cache.tree_key(ROOT / "pc/third_party/smaa"),
              "a vendored tree's key is stable")
        shutil.rmtree(scratch / "cache")
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
