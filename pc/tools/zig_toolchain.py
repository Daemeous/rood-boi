#!/usr/bin/env python3
"""The pinned Zig toolchain for the owned game code in game/ (pc/docs/Ownership.md).

    python3 pc/tools/zig_toolchain.py                     the zig binary's path (exit 2: not installed)
    python3 pc/tools/zig_toolchain.py --from-tarball F    verify a release tarball already on disk, unpack it
    python3 pc/tools/zig_toolchain.py --fetch             download the pinned tarball from ziglang.org
                                                          (~55 MB; only with the user's OK), then unpack it

A tarball is checked against PIN's SHA-256 before anything is unpacked, and is
unpacked into $ROOD_ZIG_CACHE (default ${XDG_CACHE_HOME:-~/.cache}/rood-boi/zig),
never into the repository; worktrees share it. ROOD_ZIG=/path/to/zig uses an
existing install whose `zig version` is the pinned one. Zig's global cache
lives next to the toolchain (global_cache()).
"""
import argparse
import hashlib
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

VERSION = "0.17.0"
# host -> (archive, SHA-256). Never guess a checksum: a new host's entry is
# copied from https://ziglang.org/download/index.json and checked against the
# release's minisign signature (Task 3).
PIN = {
    "x86_64-linux": ("zig-x86_64-linux-0.17.0.tar.xz",
                     "1cbe9df9f27e6b78d14ccbca43b6703a404ef79ef1c463de901d7f088d4e2026"),
}
URL = "https://ziglang.org/download/{version}/{archive}"


def host():
    machine = {"amd64": "x86_64", "x86_64": "x86_64"}.get(platform.machine().lower(), platform.machine().lower())
    return f"{machine}-{'windows' if os.name == 'nt' else 'linux'}"


def cache_root():
    if os.environ.get("ROOD_ZIG_CACHE"):
        return pathlib.Path(os.environ["ROOD_ZIG_CACHE"])
    base = os.environ.get("XDG_CACHE_HOME") or str(pathlib.Path.home() / ".cache")
    return pathlib.Path(base) / "rood-boi" / "zig"


def _pin():
    if host() not in PIN:
        raise SystemExit(f"Zig {VERSION}: no pinned archive for host {host()} (add it to PIN, Task 3)")
    return PIN[host()]


def _install_dir():
    archive, _ = _pin()
    return cache_root() / VERSION / archive.removesuffix(".tar.xz").removesuffix(".zip")


def global_cache():
    return cache_root() / VERSION / "global-cache"


def version_of(zig):
    result = subprocess.run([str(zig), "version"], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def find():
    """The pinned zig, or None. ROOD_ZIG must be the pinned version."""
    override = os.environ.get("ROOD_ZIG")
    if override:
        found = version_of(override)
        if found != VERSION:
            raise SystemExit(f"ROOD_ZIG={override} is Zig {found}, not the pinned {VERSION}")
        return pathlib.Path(override)
    zig = _install_dir() / ("zig.exe" if os.name == "nt" else "zig")
    return zig if zig.exists() and version_of(zig) == VERSION else None


def ensure():
    zig = find()
    if zig is None:
        raise SystemExit(
            f"Zig {VERSION} is not installed for this build. Either\n"
            f"  python3 pc/tools/zig_toolchain.py --from-tarball PATH   (a {_pin()[0]} you already have), or\n"
            f"  python3 pc/tools/zig_toolchain.py --fetch               (downloads it from ziglang.org), or\n"
            f"  ROOD_ZIG=/path/to/zig                                    (an existing {VERSION} install)")
    return zig


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def unpack(tarball):
    """Verify `tarball` against the pin and unpack it into the cache; returns the zig binary."""
    archive, digest = _pin()
    actual = sha256(tarball)
    if actual != digest:
        raise SystemExit(f"{tarball}: SHA-256 {actual}, the pin says {digest}: refusing to unpack")
    target = _install_dir()
    stem = target.name
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent) as scratch:
        with tarfile.open(tarball) as tar:
            for member in tar.getmembers():
                if not (member.name == stem or member.name.startswith(stem + "/")):
                    raise SystemExit(f"{tarball}: {member.name} is outside {stem}/: refusing to unpack")
            tar.extractall(scratch, filter="data")
        if target.exists():
            shutil.rmtree(target)
        os.replace(pathlib.Path(scratch) / stem, target)
    zig = target / ("zig.exe" if os.name == "nt" else "zig")
    if version_of(zig) != VERSION:
        raise SystemExit(f"{zig}: does not report version {VERSION}")
    return zig


def fetch():
    archive, _ = _pin()
    url = URL.format(version=VERSION, archive=archive)
    cache_root().mkdir(parents=True, exist_ok=True)
    download = cache_root() / archive
    print(f"Downloading {url} ...", flush=True)
    with urllib.request.urlopen(url) as response, open(download, "wb") as f:
        shutil.copyfileobj(response, f)
    try:
        return unpack(download)
    finally:
        download.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--from-tarball", type=pathlib.Path)
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    if args.from_tarball:
        print(unpack(args.from_tarball))
    elif args.fetch:
        print(fetch())
    else:
        zig = find()
        if zig is None:
            print(f"Zig {VERSION} is not installed (see --help)", file=sys.stderr)
            return 2
        print(zig)
    return 0


if __name__ == "__main__":
    sys.exit(main())
