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
    with open(log, "w") as out:
        if ok:
            ok = subprocess.run(["cmake", "-S", str(SOURCE), "-B", str(BUILD), *generator, "-DCMAKE_BUILD_TYPE=Release"],
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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=["auto", "on", "off"])
    args = parser.parse_args()
    paths = ensure(args.mode)
    print(paths if paths else "NRD: off")
    return 0


if __name__ == "__main__":
    sys.exit(main())
