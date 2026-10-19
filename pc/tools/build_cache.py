#!/usr/bin/env python3
"""Incremental build support for build_boot.py.

Two layers, both conservative (any doubt rebuilds):

- Build stamp: a fingerprint of every input of one build_boot.py
  configuration (the source trees, the decomp's build/ outputs, the tools,
  the compilers, the libraries it links, every header the last build's
  compiles read, the options and environment). When it matches the stamp
  that the last complete build of the same configuration wrote, and that
  build's outputs are untouched, the build is a no-op. The check is a stat
  walk (size and modification time, like make), ~0.1 s.

- Object cache: when the stamp differs, each compile is skipped when its
  command and the content of every file it read (from the compiler's own
  dependency list, -MD) are those of a cached compile. build_boot.py edits
  objects in place after compiling (overlay renames, state sections), so
  the cache keeps the compiler's own output and copies it back; every step
  after compiling runs as before. The cache is shared by every checkout
  and worktree (ObjectCache), with a size cap (prune).

    python3 pc/tools/build_cache.py [--prune] [--max SIZE]   # the object store's size; prune it
"""

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import zlib

import shared_cache

VERSION = 2  # bump when the stamp or cache format, or what they cover, changes
ROOT_MARK = "@ROOT@"  # the checkout's path in the shared object cache's keys


def _stat_line(path):
    try:
        st = os.stat(path)
    except OSError:
        return f"{path}\0missing\n"
    return f"{path}\0{st.st_size}\0{st.st_mtime_ns}\n"


def parse_depfile(text):
    """The prerequisites of a make rule written by gcc -MD (one target)."""
    text = text.replace("\\\n", " ")
    deps = []
    for line in text.splitlines():
        if ":" not in line:
            continue
        # "target: a b c" (paths with spaces are escaped "\ ")
        rest = line.split(": ", 1)[1] if ": " in line else line.split(":", 1)[1]
        token = ""
        i = 0
        while i < len(rest):
            c = rest[i]
            if c == "\\" and i + 1 < len(rest) and rest[i + 1] in " #":
                token += rest[i + 1]
                i += 2
                continue
            if c.isspace():
                if token:
                    deps.append(token)
                token = ""
            else:
                token += c
            i += 1
        if token:
            deps.append(token)
    return deps


class Stamp:
    """The no-op check of one build configuration in one output tree."""

    def __init__(self, root, out, config):
        self.root = pathlib.Path(root)
        self.path = pathlib.Path(out) / "build-stamp.json"
        self.config = config
        self.deps = set()  # files the compiles read (filled while building)

    def _walk(self, trees, suffixes):
        files = []
        for tree, only in trees:
            base = self.root / tree
            if base.is_file():
                files.append(str(base))
                continue
            for directory, subdirs, names in os.walk(base):
                subdirs[:] = sorted(d for d in subdirs if d != "__pycache__")
                for name in names:
                    if only is None or name.endswith(only):
                        files.append(os.path.join(directory, name))
        return files

    def fingerprint(self, trees, files, deps):
        digest = hashlib.sha256()
        digest.update(json.dumps([VERSION, self.config], sort_keys=True).encode() + b"\n")
        everything = sorted(set(self._walk(trees, None)) | {str(f) for f in files} | set(deps))
        for path in everything:
            digest.update(_stat_line(path).encode())
        return digest.hexdigest()

    def previous(self):
        try:
            return json.loads(self.path.read_text())
        except (OSError, ValueError):
            return None

    def up_to_date(self, trees, files):
        """True when the last complete build of this configuration had these
        inputs and its outputs are as it left them."""
        old = self.previous()
        if not old or old.get("version") != VERSION or old.get("config") != self.config:
            return False
        if self.fingerprint(trees, files, old.get("deps", [])) != old.get("inputs"):
            return False
        return all(_stat_line(path) == line for path, line in old.get("outputs", {}).items())

    def invalidate(self):
        try:
            self.path.unlink()
        except FileNotFoundError:
            pass

    def write(self, trees, files, outputs):
        """After a complete build: record its inputs and outputs. The inputs
        are stat'ed now, so a file edited during the build is seen as
        changed only if its edit landed after the compile read it, which the
        next build then redoes (the object cache checks content)."""
        deps = sorted(self.deps)
        self.path.write_text(json.dumps({
            "version": VERSION,
            "config": self.config,
            "inputs": self.fingerprint(trees, files, deps),
            "deps": deps,
            "outputs": {str(p): _stat_line(str(p)) for p in outputs},
        }, indent=1, sort_keys=True) + "\n")


def parse_size(text):
    """Bytes of "10G", "500M", "4096" (K, M, G, T: powers of 1024)."""
    text = str(text).strip().upper().removesuffix("B")
    scale = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30, "T": 1 << 40}.get(text[-1:], 1)
    return int(float(text[:-1] if scale > 1 else text) * scale)


def object_store():
    """The shared object store: $ROOD_BUILD_CACHE/objects (shared_cache.py)."""
    return shared_cache.base() / "objects"


def object_store_limit():
    """The store's size cap: $ROOD_OBJECT_CACHE_MAX (e.g. 10G), default 10 GB."""
    return parse_size(os.environ.get("ROOD_OBJECT_CACHE_MAX", "10G"))


COMPILERS = ("gcc", "g++", "cc", "c++", "gcc-posix", "g++-posix")


class ObjectCache:
    """Skips compiles whose command and inputs are those of a cached compile.

    The store is shared by every checkout and worktree (object_store():
    manifests/ and blobs/). A compile's key is its command with the
    checkout's own path replaced by a placeholder, plus the compiler
    identities, so the same source compiled the same way has the same key in
    every worktree. Its manifest lists the results seen for that key: the
    content of every file the compile read (the compiler's own dependency
    list; paths inside the checkout are kept relative to it) and the hash of
    the object (blobs/, content-addressed). A result is used when every one
    of those files has that content here.

    Every compile also gets -ffile-prefix-map=<checkout>=. (debug info and
    __FILE__ name paths relative to the checkout), so an object names no
    checkout and is the same wherever it was compiled. $ROOD_OBJECT_CACHE=0
    compiles everything (the same objects; nothing read or stored)."""

    MAX_RESULTS = 8  # per key: one per variant of its inputs (edits in other worktrees)

    def __init__(self, root, out, stamp, identity, store=None):
        self.root = pathlib.Path(root)
        self.prefix = str(self.root)
        self.store = pathlib.Path(store) if store else object_store()
        self.enabled = os.environ.get("ROOD_OBJECT_CACHE", "1") not in ("0", "off", "no")
        self.stamp = stamp
        self.identity = identity  # compiler identities: a different compiler misses
        self.hits = self.misses = 0
        self._hashes = {}

    def _hash(self, path):
        full = path if os.path.isabs(path) else os.path.join(self.root, path)
        try:
            st = os.stat(full)
        except OSError:
            return None
        key = (full, st.st_size, st.st_mtime_ns, st.st_ino)
        if key not in self._hashes:
            with open(full, "rb") as f:
                self._hashes[key] = hashlib.sha256(f.read()).hexdigest()
        return self._hashes[key]

    def _portable(self, text):
        return text.replace(self.prefix, ROOT_MARK)

    def _dep_name(self, path):
        """A dependency as a manifest names it: relative when inside the checkout."""
        full = os.path.normpath(path if os.path.isabs(path) else os.path.join(self.prefix, path))
        if full.startswith(self.prefix + os.sep):
            return os.path.relpath(full, self.prefix).replace(os.sep, "/")
        return full

    def _local(self, name):
        return name if os.path.isabs(name) else os.path.join(self.prefix, name)

    def _blob(self, digest):
        return self.store / "blobs" / digest[:2] / digest

    @staticmethod
    def _write(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_name(f"{path.name}.{os.getpid()}.tmp")
        tmp.write_bytes(data)
        os.replace(tmp, path)

    def compile(self, args, check=False):
        """Run a compile command (gcc/g++, MinGW too) ending in -o OBJ, or
        reuse the cached object. Returns a CompletedProcess with text
        stderr (a cache hit replays the compiler's warnings). check: raise
        on failure like subprocess.run(check=True), printing the errors."""
        command = [str(a) for a in args]
        if os.path.basename(command[0]).endswith(COMPILERS):
            command.insert(1, f"-ffile-prefix-map={self.prefix}=.")
        obj = pathlib.Path(command[command.index("-o") + 1])
        if not obj.is_absolute():
            obj = self.root / obj
        key = hashlib.sha256(json.dumps([VERSION, self.identity, [self._portable(a) for a in command]])
                             .encode()).hexdigest()
        manifest = self.store / "manifests" / key[:2] / key
        stderr = self._lookup(manifest, obj) if self.enabled else None
        if stderr is not None:
            self.hits += 1
            return subprocess.CompletedProcess(args, 0, "", stderr)
        self.misses += 1
        depfile = obj.with_name(f"{obj.name}.{os.getpid()}.d")
        result = subprocess.run(command + ["-MD", "-MF", str(depfile)], cwd=self.root, capture_output=True, text=True)
        if result.returncode == 0:
            # A preprocessed input (.i) runs no preprocessor and writes no
            # dependency list: it is the only file read.
            deps = parse_depfile(depfile.read_text()) if depfile.exists() else \
                [a for a in command[1:] if a.endswith((".i", ".ii"))]
            hashes = {path: self._hash(path) for path in deps}
            if deps and all(hashes.values()):
                if self.enabled:
                    self._store(manifest, obj, {self._dep_name(p): h for p, h in hashes.items()}, result.stderr)
                self.stamp.deps.update(self._absolute(deps))
            else:
                self.stamp.deps.update(self._absolute(p for p, h in hashes.items() if h))
        depfile.unlink(missing_ok=True)
        if check and result.returncode:
            sys.stderr.write(result.stderr)
            raise subprocess.CalledProcessError(result.returncode, command)
        return result

    def _lookup(self, manifest, obj):
        """The stderr of a cached result whose inputs match (its object
        copied to obj), or None."""
        try:
            results = json.loads(manifest.read_text())
        except (OSError, ValueError):
            return None
        for result in results:
            if not all(self._hash(self._local(name)) == digest for name, digest in result["deps"].items()):
                continue
            blob = self._blob(result["object"])
            try:
                shutil.copyfile(blob, obj)
            except FileNotFoundError:  # pruned meanwhile
                continue
            for path in (blob, manifest):  # used now (least-recently-used pruning)
                try:
                    os.utime(path)
                except OSError:
                    pass
            self.stamp.deps.update(self._absolute(self._local(name) for name in result["deps"]))
            return result.get("stderr", "").replace(ROOT_MARK, self.prefix)
        return None

    def _store(self, manifest, obj, deps, stderr):
        data = obj.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        blob = self._blob(digest)
        if blob.exists():
            os.utime(blob)
        else:
            self._write(blob, data)
        try:
            results = json.loads(manifest.read_text())
        except (OSError, ValueError):
            results = []
        # Newest first; the same inputs replace their older result. Another
        # worktree writing this manifest at the same moment may drop one
        # result: a later miss, never a wrong object.
        results = [{"deps": deps, "object": digest, "stderr": self._portable(stderr)}] + \
            [r for r in results if r["deps"] != deps][:self.MAX_RESULTS - 1]
        self._write(manifest, json.dumps(results).encode())

    def _absolute(self, paths):
        return {p if os.path.isabs(p) else os.path.normpath(os.path.join(self.root, p)) for p in paths}

    def finish(self):
        """After a build: prune the store to its size cap."""
        if self.enabled:
            removed = prune(self.store, object_store_limit())
            if removed:
                print(f"object cache: pruned {removed[0]} least recently used objects ({removed[1] / 2**20:.0f} MB)")


def store_usage(store):
    """[(mtime, size, path)] of the store's objects."""
    blobs = []
    for directory in sorted(pathlib.Path(store, "blobs").glob("??")):
        with os.scandir(directory) as entries:
            for item in entries:
                try:
                    st = item.stat()
                except OSError:
                    continue
                blobs.append((st.st_mtime, st.st_size, item.path))
    return blobs


def prune(store, limit):
    """Remove the least recently used objects while the store is over
    `limit` bytes (down to 90% of it), and the manifests not used since.
    Returns (count, bytes) removed, or None. One pruner at a time; a build
    that finds its object pruned compiles it."""
    store = pathlib.Path(store)
    blobs = store_usage(store)
    total = sum(size for _, size, _ in blobs)
    if total <= limit:
        return None
    with open(store / ".prune.lock", "a") as lock:
        if os.name != "nt":
            import fcntl

            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                return None  # another build is pruning
        count = removed = 0
        cutoff = None
        for mtime, size, path in sorted(blobs):
            if total - removed <= limit * 0.9:
                break
            try:
                os.unlink(path)
            except OSError:
                continue
            count, removed, cutoff = count + 1, removed + size, mtime
        if cutoff is not None:
            for directory in pathlib.Path(store, "manifests").glob("??"):
                with os.scandir(directory) as entries:
                    for item in entries:
                        try:
                            if item.stat().st_mtime <= cutoff:
                                os.unlink(item.path)
                        except OSError:
                            pass
    return count, removed


class ResultCache:
    """Outputs of deterministic queries (nm, objdump, readelf on an object;
    gdb's type dump; the string encoder) keyed by the tool, its arguments
    and the content of the files it reads. Entries this build did not use
    are dropped when it completes (prune)."""

    def __init__(self, out, identity):
        self.dir = pathlib.Path(out) / "resultcache"
        self.dir.mkdir(parents=True, exist_ok=True)
        self.identity = identity
        self.used = set()
        self._hashes = {}

    def file_hash(self, path):
        st = os.stat(path)
        key = (str(path), st.st_size, st.st_mtime_ns, st.st_ino)
        if key not in self._hashes:
            with open(path, "rb") as f:
                self._hashes[key] = hashlib.sha256(f.read()).hexdigest()
        return self._hashes[key]

    def key(self, parts, files=(), data=b""):
        digest = hashlib.sha256(self.identity.encode())
        digest.update(json.dumps([str(p) for p in parts]).encode())
        for path in files:
            digest.update(self.file_hash(path).encode())
        digest.update(hashlib.sha256(data).digest())
        return digest.hexdigest()

    def get(self, key):
        try:
            value = zlib.decompress((self.dir / key).read_bytes())
        except (OSError, zlib.error):
            return None
        self.used.add(key)
        return value

    def put(self, key, value):
        tmp = self.dir / (key + ".tmp")
        tmp.write_bytes(zlib.compress(value, 1))
        os.replace(tmp, self.dir / key)
        self.used.add(key)

    def run(self, args, root, files=None, input=None):
        """stdout (bytes) of a successful `args` (run in root) that reads
        `files` (default: its last argument) and `input`."""
        files = [args[-1]] if files is None else files
        key = self.key(args, [f if os.path.isabs(str(f)) else os.path.join(root, str(f)) for f in files],
                       input or b"")
        value = self.get(key)
        if value is None:
            value = subprocess.run([str(a) for a in args], cwd=root, check=True, capture_output=True,
                                   input=input).stdout
            self.put(key, value)
        return value

    def prune(self):
        for entry in self.dir.iterdir():
            if entry.name not in self.used:
                entry.unlink(missing_ok=True)


def tool_identity(tools):
    """Stat lines of the compiler drivers and their compiler proper (cc1,
    cc1plus): a toolchain update changes them."""
    lines = []
    for tool in tools:
        path = shutil.which(tool)
        if not path:
            lines.append(f"{tool}\0absent\n")
            continue
        lines.append(_stat_line(os.path.realpath(path)))
        for prog in ("cc1", "cc1plus"):
            if tool.endswith(("gcc", "g++", "gcc-posix", "g++-posix")):
                inner = subprocess.run([path, f"-print-prog-name={prog}"], capture_output=True,
                                       text=True).stdout.strip()
                if os.path.isabs(inner):
                    lines.append(_stat_line(os.path.realpath(inner)))
    return hashlib.sha256("".join(lines).encode()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--prune", action="store_true", help="prune the object store to its cap now")
    parser.add_argument("--max", help="the cap (default $ROOD_OBJECT_CACHE_MAX, else 10G)")
    args = parser.parse_args()
    store = object_store()
    limit = parse_size(args.max) if args.max else object_store_limit()
    blobs = store_usage(store)
    print(f"{store}: {len(blobs)} objects, {sum(s for _, s, _ in blobs) / 2**20:.0f} MB "
          f"(cap {limit / 2**30:.1f} GB)")
    if args.prune:
        removed = prune(store, limit)
        print(f"pruned {removed[0]} objects ({removed[1] / 2**20:.0f} MB)" if removed else "nothing to prune")
    return 0


if __name__ == "__main__":
    sys.exit(main())
