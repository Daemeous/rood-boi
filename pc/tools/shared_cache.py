#!/usr/bin/env python3
"""Build caches shared by every checkout and worktree of rood-boi.

What does not depend on the checkout is built once per machine, under
$ROOD_BUILD_CACHE (default $XDG_CACHE_HOME/rood-boi, else ~/.cache/rood-boi):

    third-party/<name>-<key>/   pinned third-party builds: glslang, SDL3
                                (linux32, linux64, windows), NRD and its
                                MinGW-w64 libraries (nrd_win)
    third-party/<name>-<key>.lock    held while that entry is built
    objects/                    the object cache (build_cache.ObjectCache)
    wine/<version>/             the Wine prefix of pc/tests/replace_file_test.py

<key> is a digest of everything that makes the output differ: the source
(the vendored tree's git tree id plus any uncommitted edits in it, or a pin),
the target, the identity of the compilers and of CMake, and the options. A
different key is a different entry, so nothing is ever rebuilt in place; old
entries stay until removed (`shared_cache.py --list` shows them with their
sizes; delete what is unused).

An entry is built under its lock (flock: one builder per entry across all
worktrees; the others wait, then use it) into a temporary directory and
renamed into place when complete, so a reader never sees a half-built entry.
Builds only read an entry once it exists.

    python3 pc/tools/shared_cache.py [--list]   # the cache, its entries and sizes
"""
import argparse
import contextlib
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPLETE = ".complete"  # written last into an entry before it is renamed into place


def base():
    """The cache directory ($ROOD_BUILD_CACHE, else the user's cache dir)."""
    env = os.environ.get("ROOD_BUILD_CACHE")
    if env:
        return pathlib.Path(env).expanduser().resolve()
    xdg = os.environ.get("XDG_CACHE_HOME")
    return ((pathlib.Path(xdg) if xdg else pathlib.Path.home() / ".cache") / "rood-boi").resolve()


@contextlib.contextmanager
def locked(path, what):
    """Hold an exclusive lock on `path` (created) for the block; prints once
    when it has to wait for another process."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a") as handle:
        if os.name == "nt":
            import msvcrt
            import time

            waited = False
            while True:
                try:
                    msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    if not waited:
                        print(f"Waiting for another process building {what}...", flush=True)
                        waited = True
                    time.sleep(1)
        else:
            import fcntl

            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                print(f"Waiting for another process building {what}...", flush=True)
                fcntl.flock(handle, fcntl.LOCK_EX)
        yield


def digest(parts):
    return hashlib.sha256(json.dumps(parts, sort_keys=True, default=str).encode()).hexdigest()[:16]


def _file_digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def tree_key(path):
    """The content of a source tree in the checkout: git's tree id of it at
    HEAD plus the content of every file that differs from HEAD (edited,
    added, deleted; ignored files excluded). Without git, the content of
    every file."""
    path = pathlib.Path(path)
    rel = path.relative_to(ROOT).as_posix()
    tree = subprocess.run(["git", "rev-parse", f"HEAD:{rel}"], cwd=ROOT, capture_output=True, text=True)
    status = subprocess.run(["git", "status", "--porcelain", "-z", "--no-renames", "--untracked-files=all", "--", rel], cwd=ROOT,
                            capture_output=True, text=True)
    if tree.returncode == 0 and status.returncode == 0:
        parts = [tree.stdout.strip()]
        for item in status.stdout.split("\0"):
            name = item[3:]
            if not name:
                continue
            file = ROOT / name
            parts.append([name, _file_digest(file) if file.is_file() else "deleted"])
        return digest(parts)
    parts = []
    for directory, subdirs, names in os.walk(path):
        subdirs.sort()
        for name in sorted(names):
            file = pathlib.Path(directory, name)
            parts.append([file.relative_to(path).as_posix(), _file_digest(file)])
    return digest(parts)


def entry_path(name, key_parts):
    return base() / "third-party" / f"{name}-{digest(key_parts)}"


def entry(name, key_parts, build):
    """The cache entry `name` for `key_parts`, built when missing:
    build(tmp) fills the directory tmp (empty, on the cache's file system);
    on success it is renamed to the entry. Returns the entry's path."""
    final = entry_path(name, key_parts)
    if (final / COMPLETE).exists():
        return final
    with locked(final.with_name(final.name + ".lock"), f"{name} in {final.parent}"):
        if (final / COMPLETE).exists():  # another worktree built it while this one waited
            return final
        if final.exists():
            shutil.rmtree(final)  # not complete: never published (left by an older layout)
        tmp = final.with_name(f"{final.name}.tmp-{os.getpid()}")
        shutil.rmtree(tmp, ignore_errors=True)
        tmp.mkdir(parents=True)
        try:
            build(tmp)
            (tmp / "key.json").write_text(json.dumps(key_parts, indent=1, sort_keys=True, default=str) + "\n")
            (tmp / COMPLETE).write_text("")
            os.rename(tmp, final)
        except BaseException:
            shutil.rmtree(tmp, ignore_errors=True)
            raise
    return final


def wine_prefix():
    """A Wine prefix for the installed Wine, made once (`wineboot --init`)
    and shared: wine/<version>/prefix. Wine runs any number of processes in
    one prefix (they share its wineserver), so tests use it concurrently.
    A prefix holds absolute paths (its registry), so it is made in place
    under the lock and published by its marker file. None without Wine."""
    wine = shutil.which("wine")
    if not wine:
        return None
    version = subprocess.run([wine, "--version"], capture_output=True, text=True).stdout.strip() or "unknown"
    home = base() / "wine" / "".join(c if c.isalnum() or c in "-." else "_" for c in version)
    prefix, marker = home / "prefix", home / COMPLETE
    if marker.exists():
        return prefix
    with locked(home.with_name(home.name + ".lock"), f"the Wine prefix in {home}"):
        if marker.exists():
            return prefix
        shutil.rmtree(prefix, ignore_errors=True)  # an interrupted init
        prefix.mkdir(parents=True)
        env = wine_env(prefix)
        subprocess.run([wine, "wineboot", "--init"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=600)
        server = shutil.which("wineserver")
        if server:  # wait until it has written the registry
            subprocess.run([server, "-w"], env=env, timeout=600)
        marker.write_text(version + "\n")
    return prefix


def wine_env(prefix):
    """The environment the tests run Wine with (no Mono/Gecko installers, no display)."""
    return dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG="-all", WINEDLLOVERRIDES="mscoree,mshtml=",
                DISPLAY="")


def _size(path):
    total = 0
    for directory, _, names in os.walk(path):
        for name in names:
            try:
                total += os.lstat(os.path.join(directory, name)).st_size
            except OSError:
                pass
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--list", action="store_true", help="the entries and their sizes (default)")
    parser.parse_args()
    root = base()
    print(f"shared build cache: {root}")
    if not root.exists():
        return 0
    rows = []
    for group in ("third-party", "wine"):
        directory = root / group
        if directory.is_dir():
            rows += [(f"{group}/{p.name}", _size(p)) for p in sorted(directory.iterdir()) if p.is_dir()]
    if (root / "objects").is_dir():
        rows.append(("objects", _size(root / "objects")))
    for name, size in rows:
        print(f"{size / 2**20:9.1f} MB  {name}")
    print(f"{sum(s for _, s in rows) / 2**20:9.1f} MB  total")
    return 0


if __name__ == "__main__":
    sys.exit(main())
