#!/usr/bin/env python3
"""Fetch and build NVIDIA NRD (Real-time Denoisers) for the Vulkan path
tracer's "nrd" denoiser (pc/docs/Vulkan_Renderer.md, "NRD").

NRD is under the NVIDIA RTX SDKs license and is never vendored here: the
CMake project pc/tools/nrd downloads it at a pinned commit (with its
ShaderMake, MathLib and DXC, pinned by NRD) and builds a static library with
embedded SPIR-V, once per machine into the shared build cache
(pc/tools/shared_cache.py: third-party/nrd-<key>, where the key covers the
pin, the options, the compilers and CMake). The first build needs network
access: NRD (~5 MB), ShaderMake and MathLib (<1 MB), and DXC, NRD's HLSL
compiler (a ~500 MB download that unpacks to ~1.7 GB). DXC is only needed to
compile NRD's shaders, so it is deleted after a successful build (the entry
keeps ~40 MB); every later build, in any worktree, reuses the entry and does
not run CMake at all.

    python3 pc/tools/nrd.py [--mode auto|on|off]   # prints the result

Modes (build_boot.py --nrd, else $ROOD_NRD, default auto):
    auto  build it; on failure (offline, no cmake) warn once and build
          without NRD (the failure is remembered: `--nrd on` retries)
    on    build it; fail the build when it cannot
    off   without NRD (the tracer keeps its own filter)
"""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "pc/tools/nrd"
sys.path.insert(0, str(ROOT / "pc/tools"))
import shared_cache  # noqa: E402
from build_cache import tool_identity  # noqa: E402

TARGETS = ["NRD", "ShaderMakeBlob"]  # the libraries, in link order
ENTRY = "@ENTRY@"  # the entry's directory in its saved compile commands


def _windows_host_options(directory):
    if os.name != "nt":
        return []
    # A Windows host (MSYS2) builds NRD with MinGW-w64 directly: the Vulkan
    # path only needs its SPIR-V, not the DXIL/DXBC shaders (whose fxc
    # comes with the Windows SDK), and MathLib needs the abs() shim.
    shim = directory / "abs_overloads.h"
    shim.write_text(ABS_SHIM)
    return ["-DNRD_EMBEDS_DXIL_SHADERS=OFF", "-DNRD_EMBEDS_DXBC_SHADERS=OFF", "-DSHADERMAKE_FIND_FXC=OFF",
            f"-DCMAKE_CXX_FLAGS=-include {shim.as_posix()}"]


def _key():
    # The pin and options live in the CMake project: its text is part of the key.
    return {"cmake_project": (SOURCE / "CMakeLists.txt").read_text(), "host": os.name,
            "env": {k: os.environ.get(k) for k in ("CC", "CXX", "CXXFLAGS")},
            "tools": tool_identity(["c++", "g++", "cmake", "ninja"])}


def _paths(entry):
    """{"include", "libraries", "entry"} of a complete entry, or None."""
    try:
        listing = json.loads((entry / "rood-nrd-paths.json").read_text())
    except (OSError, ValueError):
        return None
    paths = {"include": str(entry / listing["include"]), "libraries": [str(entry / p) for p in listing["libraries"]],
             "entry": str(entry)}
    return paths if all(pathlib.Path(p).exists() for p in paths["libraries"]) else None


class BuildFailed(Exception):
    pass


def _build(tmp, log, quiet):
    if not quiet:
        print(f"NRD: fetching and building (pinned; into {tmp.parent}, log {log})", flush=True)
    if not shutil.which("cmake"):
        log.write_text("cmake not found\n")
        raise BuildFailed()
    generator = ["-G", "Ninja"] if shutil.which("ninja") else []
    with open(log, "w") as out:
        ok = subprocess.run(["cmake", "-S", str(SOURCE), "-B", str(tmp), *generator, "-DCMAKE_BUILD_TYPE=Release",
                             *_windows_host_options(tmp)],
                            stdout=out, stderr=subprocess.STDOUT).returncode == 0
        ok = ok and subprocess.run(["cmake", "--build", str(tmp), "--parallel", str(min(os.cpu_count() or 4, 8))],
                                   stdout=out, stderr=subprocess.STDOUT).returncode == 0
    listing = tmp / "rood-nrd-paths.txt"
    if not ok or not listing.exists():
        raise BuildFailed()
    include, libraries = None, []
    for line in listing.read_text().splitlines():
        kind, _, value = line.partition(" ")
        if kind == "include":
            include = value
        elif kind == "library":
            libraries.append(value)
    if not include or len(libraries) != len(TARGETS) or not all(pathlib.Path(p).exists() for p in libraries):
        raise BuildFailed()
    # DXC (~2 GB unpacked with its archive) only compiled NRD's shaders.
    for name in ("dxc-src", "dxc-subbuild", "dxc-build"):
        shutil.rmtree(tmp / "_deps" / name, ignore_errors=True)
    relative = lambda p: pathlib.Path(p).resolve().relative_to(tmp.resolve()).as_posix()
    (tmp / "rood-nrd-paths.json").write_text(json.dumps(
        {"include": relative(include), "libraries": [relative(p) for p in libraries]}, indent=1) + "\n")
    # The Windows libraries (ensure_windows) replay these compiles with
    # MinGW-w64; the entry is renamed after this, so its path is a placeholder.
    if generator:
        for target in TARGETS:
            commands = subprocess.run(["ninja", "-C", str(tmp), "-t", "commands", target], capture_output=True,
                                      text=True, check=True).stdout
            (tmp / f"rood-commands-{target}.txt").write_text(
                commands.replace(str(tmp.resolve()), ENTRY).replace(str(tmp), ENTRY))


def ensure(mode=None, quiet=False):
    """{"include": dir, "libraries": [...], "entry": dir} or None (built without NRD)."""
    mode = mode or os.environ.get("ROOD_NRD") or "auto"
    if mode in ("0", "off", "no"):
        return None
    forced = mode in ("1", "on", "yes")
    key = _key()
    entry = shared_cache.entry_path("nrd", key)
    paths = _paths(entry) if (entry / shared_cache.COMPLETE).exists() else None
    if paths:
        return paths  # cached: no CMake run
    failed_file = entry.with_name(entry.name + ".failed")
    if not forced and failed_file.exists():
        return None  # failed before with this key; `--nrd on` retries
    log = entry.with_name(entry.name + ".log")
    log.parent.mkdir(parents=True, exist_ok=True)
    try:
        entry = shared_cache.entry("nrd", key, lambda tmp: _build(tmp, log, quiet))
        paths = _paths(entry)
    except BuildFailed:
        paths = None
    if paths:
        failed_file.unlink(missing_ok=True)
        return paths
    tail = "".join(log.read_text(errors="replace").splitlines(True)[-15:]) if log.exists() else "cmake not found\n"
    if forced:
        sys.stderr.write(tail)
        raise SystemExit(f"NRD: build failed (see {log})")
    failed_file.write_text("see " + log.name + "\n")
    sys.stderr.write(f"NRD: build failed, building without it (the GI keeps its own filter; see {log};"
                     " `build_boot.py --arch 64 --nrd on` retries)\n")
    return None


MINGW = "x86_64-w64-mingw32-"
# MathLib calls abs() on floats unqualified; MinGW's C++ headers leave
# only C's int abs() global (glibc's also bring std::abs's overloads).
ABS_SHIM = "#include <cstdlib>\n#include <cmath>\nusing std::abs;\n"


def _build_windows(tmp, nrd_entry, quiet):
    if not quiet:
        print("NRD: compiling for Windows (MinGW-w64)", flush=True)
    shim = tmp / "abs_overloads.h"
    shim.write_text(ABS_SHIM)
    libraries = json.loads((nrd_entry / "rood-nrd-paths.json").read_text())["libraries"]
    for target, library in zip(TARGETS, libraries):
        commands = (nrd_entry / f"rood-commands-{target}.txt").read_text().replace(ENTRY, str(nrd_entry))
        objects = []
        for line in commands.splitlines():
            args = line.split()
            if not args or "-c" not in args or not args[0].endswith(("c++", "g++", "clang++")):
                continue
            # The compile only: no dependency files, PIC or warnings-as-errors
            # (MinGW's headers warn where glibc's do not).
            command, skip = [MINGW + "g++-posix", "-include", str(shim)], False
            for a in args[1:]:
                if skip:
                    skip = False
                elif a in ("-MT", "-MF", "-o"):
                    skip = True
                elif a not in ("-MD", "-fPIC", "-Werror"):
                    command.append(a)
            source = pathlib.Path(args[args.index("-c") + 1])
            obj = tmp / "obj" / target / (source.stem + ".obj")
            obj.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(command + ["-o", str(obj)], cwd=nrd_entry, check=True)
            objects.append(str(obj))
        subprocess.run([MINGW + "ar", "qcs", str(tmp / pathlib.Path(library).name), *objects], check=True)
    shutil.rmtree(tmp / "obj")


def ensure_windows(paths, quiet=False):
    """The same NRD for build_boot.py --target windows: NRD's C++ compiled by
    MinGW-w64 into static libraries (shared cache entry nrd_win-<key>),
    reusing the Linux build's configuration and its SPIR-V shaders (SPIR-V
    is the same for every host, so DXC does not run again): it replays the
    Linux build's own compile commands (`ninja -t commands`, saved in its
    entry). `paths` is ensure()'s result; returns the Windows {"include",
    "libraries"}, or None."""
    if not paths:
        return None
    if os.name == "nt":
        return paths  # ensure() built it with MinGW-w64 already
    nrd_entry = pathlib.Path(paths["entry"])
    if not all((nrd_entry / f"rood-commands-{t}.txt").exists() for t in TARGETS):
        sys.stderr.write("NRD: the Windows build replays the Linux build's Ninja commands; "
                         f"install Ninja and remove {nrd_entry} to rebuild it\n")
        return None
    key = {"nrd": nrd_entry.name, "shim": ABS_SHIM,
           "tools": tool_identity([MINGW + "g++-posix", MINGW + "ar"])}
    entry = shared_cache.entry("nrd_win", key, lambda tmp: _build_windows(tmp, nrd_entry, quiet))
    return {"include": paths["include"],
            "libraries": [str(entry / pathlib.Path(p).name) for p in paths["libraries"]], "entry": str(entry)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=["auto", "on", "off"])
    args = parser.parse_args()
    paths = ensure(args.mode)
    print(paths if paths else "NRD: off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
