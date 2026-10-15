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
  after compiling runs as before.
"""

import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import zlib

VERSION = 1  # bump when the stamp or cache format, or what they cover, changes


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


class ObjectCache:
    """Skips compiles whose command and inputs are unchanged."""

    def __init__(self, root, out, stamp, identity):
        self.root = pathlib.Path(root)
        self.dir = pathlib.Path(out) / "objcache"
        self.dir.mkdir(parents=True, exist_ok=True)
        self.stamp = stamp
        self.identity = identity  # compiler identities: a different compiler misses
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

    def compile(self, args, check=False):
        """Run a compile command (gcc/g++, MinGW too) ending in -o OBJ, or
        reuse the cached object. Returns a CompletedProcess with text
        stderr (a cache hit replays the compiler's warnings). check: raise
        on failure like subprocess.run(check=True), printing the errors."""
        obj = pathlib.Path(args[args.index("-o") + 1])
        if not obj.is_absolute():
            obj = self.root / obj
        name = obj.relative_to(self.root).as_posix().replace("/", "%")
        meta_path, pristine, depfile = (self.dir / (name + ".json"), self.dir / name, self.dir / (name + ".d"))
        command = [str(a) for a in args]
        try:
            meta = json.loads(meta_path.read_text())
        except (OSError, ValueError):
            meta = None
        if (meta and meta.get("command") == command and meta.get("identity") == self.identity
                and pristine.exists()
                and all(self._hash(path) == digest for path, digest in meta["deps"].items())):
            shutil.copyfile(pristine, obj)
            self.stamp.deps.update(self._absolute(meta["deps"]))
            return subprocess.CompletedProcess(args, 0, "", meta.get("stderr", ""))
        meta_path.unlink(missing_ok=True)
        result = subprocess.run(command + ["-MD", "-MF", str(depfile)], cwd=self.root, capture_output=True, text=True)
        if result.returncode == 0:
            # A preprocessed input (.i) runs no preprocessor and writes no
            # dependency list: it is the only file read.
            deps = parse_depfile(depfile.read_text()) if depfile.exists() else \
                [a for a in command[1:] if a.endswith((".i", ".ii"))]
            hashes = {path: self._hash(path) for path in deps}
            if deps and all(hashes.values()):
                shutil.copyfile(obj, pristine)
                meta_path.write_text(json.dumps({"command": command, "identity": self.identity,
                                                 "deps": hashes, "stderr": result.stderr}))
                self.stamp.deps.update(self._absolute(deps))
            else:
                self.stamp.deps.update(self._absolute(p for p, h in hashes.items() if h))
        depfile.unlink(missing_ok=True)
        if check and result.returncode:
            sys.stderr.write(result.stderr)
            raise subprocess.CalledProcessError(result.returncode, command)
        return result

    def _absolute(self, paths):
        return {p if os.path.isabs(p) else os.path.normpath(os.path.join(self.root, p)) for p in paths}


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
