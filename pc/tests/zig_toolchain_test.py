#!/usr/bin/env python3
"""pc/tools/zig_toolchain.py without the network: a fake release tarball is
verified, unpacked into a scratch cache and found; a wrong checksum, a path
escaping the archive and a wrong ROOD_ZIG version are refused."""
import hashlib
import io
import os
import pathlib
import sys
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import zig_toolchain as zt  # noqa: E402

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


def fake_tarball(directory, name, version, evil=False):
    """A .tar.xz like the official one: <stem>/zig printing the version."""
    stem = name.removesuffix(".tar.xz")
    path = directory / name
    with tarfile.open(path, "w:xz") as tar:
        script = f"#!/bin/sh\n[ \"$1\" = version ] && echo {version}\n".encode()
        info = tarfile.TarInfo(f"{stem}/zig")
        info.size, info.mode = len(script), 0o755
        tar.addfile(info, io.BytesIO(script))
        if evil:
            info = tarfile.TarInfo("../escaped")
            info.size = 1
            tar.addfile(info, io.BytesIO(b"x"))
    return path, hashlib.sha256(path.read_bytes()).hexdigest()


with tempfile.TemporaryDirectory() as scratch:
    scratch = pathlib.Path(scratch)
    os.environ["ROOD_ZIG_CACHE"] = str(scratch / "cache")
    os.environ.pop("ROOD_ZIG", None)
    name = f"zig-x86_64-linux-{zt.VERSION}.tar.xz"
    tarball, digest = fake_tarball(scratch, name, zt.VERSION)
    zt.PIN["x86_64-linux"] = (name, digest)
    check(zt.find() is None, "nothing installed: find() is None")
    try:
        zt.ensure()
        check(False, "ensure() without an install stops")
    except SystemExit as e:
        check("--from-tarball" in str(e) and "--fetch" in str(e), "ensure() says how to get it")
    from unittest.mock import patch
    with patch.object(zt, "host", return_value="x86_64-windows"):
        check(zt.find() is None, "unsupported host: optional discovery is None")
        try:
            zt.ensure()
            check(False, "unsupported host: required installation stops")
        except SystemExit as e:
            check("no pinned archive" in str(e), "unsupported host: required installation explains the missing pin")
    zig = zt.unpack(tarball)
    check(zig.exists() and zt.version_of(zig) == zt.VERSION, f"unpacked and runs: {zig}")
    check(zt.find() == zig, "find() returns the unpacked zig")
    check(str(scratch / "cache") in str(zig), "unpacked under ROOD_ZIG_CACHE, not the repository")
    (scratch / "w").mkdir()
    wrong, _ = fake_tarball(scratch / "w", name, zt.VERSION)
    zt.PIN["x86_64-linux"] = (name, "0" * 64)
    try:
        zt.unpack(wrong)
        check(False, "a wrong checksum is refused")
    except SystemExit as e:
        check("SHA-256" in str(e), "a wrong checksum is refused")
    (scratch / "e").mkdir()
    evil, evil_digest = fake_tarball(scratch / "e", name, zt.VERSION, evil=True)
    zt.PIN["x86_64-linux"] = (name, evil_digest)
    os.environ["ROOD_ZIG_CACHE"] = str(scratch / "cache2")
    try:
        zt.unpack(evil)
        check(False, "a path outside the archive's directory is refused")
    except SystemExit as e:
        check("outside" in str(e), "a path outside the archive's directory is refused")
    check(not (scratch / "escaped").exists(), "nothing written outside the cache")
    other, _ = fake_tarball(scratch / "e", "zig-old.tar.xz", "0.15.2")
    with tarfile.open(other) as tar:
        tar.extractall(scratch / "old", filter="data")
    os.environ["ROOD_ZIG"] = str(scratch / "old/zig-old/zig")
    try:
        zt.find()
        check(False, "ROOD_ZIG with another version is refused")
    except SystemExit as e:
        check("0.15.2" in str(e), "ROOD_ZIG with another version is refused")

print("FAILED" if failures else "all zig toolchain checks passed")
sys.exit(1 if failures else 0)
