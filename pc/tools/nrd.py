#!/usr/bin/env python3
"""Fetch and build NVIDIA NRD (Real-time Denoisers) for the Vulkan path
tracer's "nrd" denoiser (pc/docs/Vulkan_Renderer.md, "NRD").

NRD is under the NVIDIA RTX SDKs license and is never vendored here: the
CMake project pc/tools/nrd downloads it at a pinned commit (with its
ShaderMake, MathLib and DXC, pinned by NRD) into pc/build/nrd and builds a
static library with embedded SPIR-V. The first build needs network access:
NRD (~5 MB), ShaderMake and MathLib (<1 MB), and DXC, NRD's HLSL compiler
(a ~500 MB download that unpacks to ~1.7 GB). DXC is only needed to compile
NRD's shaders, so it is deleted after a successful build (pc/build/nrd keeps
~40 MB); later builds reuse the library and do not run CMake at all unless
the pin or the options change (then everything is fetched again).

    python3 pc/tools/nrd.py [--mode auto|on|off]   # prints the result

Modes (build_boot.py --nrd, else $ROOD_NRD, default auto):
    auto  build it; on failure (offline, no cmake) warn once and build
          without NRD (the failure is remembered: `--nrd on` retries)
    on    build it; fail the build when it cannot
    off   without NRD (the tracer keeps its own filter)
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "pc/tools/nrd"
BUILD = ROOT / "pc/build/nrd"


def _stamp():
    # The pin and options live in the CMake project: its text is the key.
    return (SOURCE / "CMakeLists.txt").read_text()


def _paths():
    listing = BUILD / "rood-nrd-paths.txt"
    if not listing.exists():
        return None
    include, libraries = None, []
    for line in listing.read_text().splitlines():
        kind, _, value = line.partition(" ")
        if kind == "include":
            include = value
        elif kind == "library":
            libraries.append(value)
    if not include or not libraries or not all(pathlib.Path(p).exists() for p in libraries):
        return None
    return {"include": include, "libraries": libraries}


def ensure(mode=None, quiet=False):
    """{"include": dir, "libraries": [...]} or None (built without NRD)."""
    mode = mode or os.environ.get("ROOD_NRD") or "auto"
    if mode in ("0", "off", "no"):
        return None
    forced = mode in ("1", "on", "yes")
    stamp_file, failed_file = BUILD / "rood-nrd.stamp", BUILD / "rood-nrd-failed.txt"
    stamp = _stamp()
    paths = _paths()
    if paths and stamp_file.exists() and stamp_file.read_text() == stamp:
        return paths  # cached: no CMake run
    if not forced and failed_file.exists() and failed_file.read_text() == stamp:
        return None  # failed before with this pin; `--nrd on` retries
    BUILD.mkdir(parents=True, exist_ok=True)
    log = BUILD / "rood-nrd-build.log"
    if not quiet:
        print(f"NRD: fetching and building (pinned; log {log.relative_to(ROOT)})", flush=True)
    generator = ["-G", "Ninja"] if shutil.which("ninja") else []
    if (BUILD / "CMakeCache.txt").exists() and not (BUILD / "build.ninja").exists() and generator:
        generator = []  # keep the generator an earlier configure chose
    ok = shutil.which("cmake") is not None
    options = []
    if os.name == "nt":
        # A Windows host (MSYS2) builds NRD with MinGW-w64 directly: the Vulkan
        # path only needs its SPIR-V, not the DXIL/DXBC shaders (whose fxc
        # comes with the Windows SDK), and MathLib needs the abs() shim.
        shim = BUILD / "abs_overloads.h"
        shim.write_text(ABS_SHIM)
        options = ["-DNRD_EMBEDS_DXIL_SHADERS=OFF", "-DNRD_EMBEDS_DXBC_SHADERS=OFF", "-DSHADERMAKE_FIND_FXC=OFF",
                   f"-DCMAKE_CXX_FLAGS=-include {shim.as_posix()}"]
    with open(log, "w") as out:
        if ok:
            ok = subprocess.run(["cmake", "-S", str(SOURCE), "-B", str(BUILD), *generator, "-DCMAKE_BUILD_TYPE=Release",
                                 *options],
                                stdout=out, stderr=subprocess.STDOUT).returncode == 0
        if ok:
            ok = subprocess.run(["cmake", "--build", str(BUILD), "--parallel", str(min(os.cpu_count() or 4, 8))],
                                stdout=out, stderr=subprocess.STDOUT).returncode == 0
    paths = _paths() if ok else None
    if paths:
        # DXC (~2 GB unpacked with its archive) only compiled NRD's shaders.
        for name in ("dxc-src", "dxc-subbuild", "dxc-build"):
            shutil.rmtree(BUILD / "_deps" / name, ignore_errors=True)
        stamp_file.write_text(stamp)
        if failed_file.exists():
            failed_file.unlink()
        return paths
    tail = "".join(log.read_text(errors="replace").splitlines(True)[-15:]) if log.exists() else "cmake not found\n"
    if forced:
        sys.stderr.write(tail)
        raise SystemExit(f"NRD: build failed (see {log})")
    failed_file.write_text(stamp)
    sys.stderr.write(f"NRD: build failed, building without it (the GI keeps its own filter; see {log};"
                     " `build_boot.py --arch 64 --nrd on` retries)\n")
    return None


WIN_BUILD = ROOT / "pc/build/nrd_win"
MINGW = "x86_64-w64-mingw32-"
# MathLib calls abs() on floats unqualified; MinGW's C++ headers leave
# only C's int abs() global (glibc's also bring std::abs's overloads).
ABS_SHIM = "#include <cstdlib>\n#include <cmath>\nusing std::abs;\n"


def ensure_windows(paths, quiet=False):
    """The same NRD for build_boot.py --target windows: NRD's C++ compiled by
    MinGW-w64 into static libraries, reusing the Linux build's configuration
    and its SPIR-V shaders (nrd-src/_Shaders; SPIR-V is the same for every
    host, so DXC does not run again). Replays the Linux build's own compile
    commands (`ninja -t commands`). `paths` is ensure()'s result; returns
    the Windows {"include", "libraries"}, or None."""
    if not paths:
        return None
    if os.name == "nt":
        return paths  # ensure() built it with MinGW-w64 already
    stamp = _stamp() + "\n" + "\n".join(paths["libraries"])
    stamp_file = WIN_BUILD / "rood-nrd.stamp"
    libraries = [str(WIN_BUILD / pathlib.Path(p).name) for p in paths["libraries"]]
    if stamp_file.exists() and stamp_file.read_text() == stamp and all(pathlib.Path(p).exists() for p in libraries):
        return {"include": paths["include"], "libraries": libraries}
    if not (BUILD / "build.ninja").exists() or not shutil.which("ninja"):
        sys.stderr.write("NRD: the Windows build replays the Linux build's Ninja commands; "
                         "rebuild pc/build/nrd with Ninja installed\n")
        return None
    if not quiet:
        print("NRD: compiling for Windows (MinGW-w64)", flush=True)
    WIN_BUILD.mkdir(parents=True, exist_ok=True)
    shim = WIN_BUILD / "abs_overloads.h"
    shim.write_text(ABS_SHIM)
    for library, target in zip(paths["libraries"], ["NRD", "ShaderMakeBlob"]):
        commands = subprocess.run(["ninja", "-C", str(BUILD), "-t", "commands", target], capture_output=True,
                                  text=True, check=True).stdout.splitlines()
        objects = []
        for line in commands:
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
            obj = WIN_BUILD / target / (source.stem + ".obj")
            obj.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(command + ["-o", str(obj)], cwd=BUILD, check=True)
            objects.append(str(obj))
        archive = WIN_BUILD / pathlib.Path(library).name
        if archive.exists():
            archive.unlink()
        subprocess.run([MINGW + "ar", "qcs", str(archive), *objects], check=True)
    stamp_file.write_text(stamp)
    return {"include": paths["include"], "libraries": libraries}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=["auto", "on", "off"])
    args = parser.parse_args()
    paths = ensure(args.mode)
    print(paths if paths else "NRD: off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
