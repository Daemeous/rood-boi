#!/usr/bin/env python3
"""Free the disk space a checkout's pc/build holds beyond its builds.

    python3 pc/tools/clean_build.py            # test outputs (what run.py and verify.sh wrote)
    python3 pc/tools/clean_build.py --legacy   # also the per-tree copies the shared cache replaced
    python3 pc/tools/clean_build.py -n         # only list what would go, with sizes

Test outputs: pc/build/verify (verify.sh's logs and suite outputs),
pc/build/tests* (run.py's default --out and other runs' outputs) and the
save states of the test checkpoints, pc/build/test-snapshots (run.py makes
them again for the build it tests). Never the builds themselves, the pinned
test card (pc/build/test-card1.mcd) or the golden frames (pc/build/golden).

--legacy: pc/build/{sdl3,sdl3_64,sdl3_win,glslang,nrd,nrd_win,wineprefix} and
each build's objcache/, which builds from before the shared build cache
(pc/tools/shared_cache.py) kept in every checkout and no longer use.

Only this checkout's pc/build: run it in each worktree to clean that one.
`python3 pc/tests/run.py --clean` is the same as the default.
"""
import argparse
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD = ROOT / "pc/build"
LEGACY = ["sdl3", "sdl3_64", "sdl3_win", "glslang", "nrd", "nrd_win", "wineprefix"]


def size(path):
    if path.is_symlink() or path.is_file():
        return path.lstat().st_size
    total = 0
    for directory, _, names in os.walk(path):
        for name in names:
            try:
                total += os.lstat(os.path.join(directory, name)).st_size
            except OSError:
                pass
    return total


def targets(legacy=False):
    """The paths to remove."""
    found = [BUILD / "verify", BUILD / "test-snapshots"] + sorted(BUILD.glob("tests*"))
    if legacy:
        found += [BUILD / name for name in LEGACY]
        found += [BUILD / name / "objcache" for name in ("boot", "boot64", "win64")]
    return [p for p in found if p.exists() or p.is_symlink()]


def busy(path):
    """True while a run.py holds an output directory's lock in it."""
    if os.name == "nt" or not path.is_dir():
        return False
    import fcntl

    for lock in [path / ".lock", *path.glob("*/.lock")]:
        try:
            with open(lock) as handle:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return True
        except OSError:
            pass
    return False


def clean(legacy=False, dry_run=False):
    total = 0
    for path in targets(legacy):
        if busy(path):
            print(f"{'':9}     {path.relative_to(ROOT)} (kept: a run.py is using it)")
            continue
        n = size(path)
        total += n
        print(f"{n / 2**20:9.1f} MB  {path.relative_to(ROOT)}")
        if not dry_run:
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path)
            else:
                path.unlink()
    print(f"{total / 2**20:9.1f} MB  {'would be freed' if dry_run else 'freed'}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--legacy", action="store_true", help="also the per-tree third-party builds and objcache/")
    parser.add_argument("-n", "--dry-run", action="store_true", help="list only")
    args = parser.parse_args()
    return clean(args.legacy, args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
