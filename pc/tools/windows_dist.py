#!/usr/bin/env python3
"""Assemble a playable Windows folder from the --target windows build.

    python3 pc/tools/build_boot.py --target windows
    python3 pc/tools/windows_dist.py /mnt/c/Games/rood-boi

The folder gets rood.exe (static: no DLLs to ship; stripped, so resolve crash
addresses with the build's own pc/build/win64/rood.exe), play.bat, the files
the game reads relative to its working directory (data/ from your disc, the
lighting packs, pc/assets, the fonts in pc/third_party/fonts), the licences
of the code linked into it (licenses/) and empty pc/user, pc/save,
pc/lights. The disc
image is copied only with --disc (it is ~740 MB; or place it at
disks/SLUS-01040.bin yourself). Existing saves and settings in the folder
are kept. Like data/, the result contains your own game data: keep it to
yourself.
"""
import argparse
import os
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]

PLAY_BAT = r"""@echo off
rem rood-boi for Windows (pc/tools/windows_dist.py): the Vulkan renderer,
rem settings in pc\user\settings.cfg. Arguments go to the game (e.g.
rem --skip-movies); the log goes to rood.log. Escape or F1 opens the editor.
cd /d "%~dp0"
if not defined ROOD_RENDERER set ROOD_RENDERER=vulkan
if not defined ROOD_GI set ROOD_GI=1
if not defined ROOD_SETTINGS set ROOD_SETTINGS=pc/user/settings.cfg
if not exist pc\build\boot mkdir pc\build\boot
rood.exe --window %* 2> rood.log
if errorlevel 1 (
    echo rood.exe stopped with an error; the end of rood.log:
    powershell -NoProfile -Command "Get-Content rood.log -Tail 25"
    pause
)
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("destination", type=pathlib.Path)
    parser.add_argument("--disc", action="store_true", help="also copy disks/SLUS-01040.bin")
    parser.add_argument("--callo-build", default="win64_callo",
                        help="with --callo: the build_boot.py output folder under pc/build to take it from "
                        "(win64_callo_biginv for --big-inventory)")
    parser.add_argument("--callo", action="store_true",
                        help="also add the Callo Merlose mod build (build_boot.py --target windows --callo) "
                        "as rood-callo.exe with play-callo.bat, sharing the folder's data and saves")
    args = parser.parse_args()
    exe = ROOT / "pc/build/win64/rood.exe"
    if not exe.exists():
        raise SystemExit(f"{exe.relative_to(ROOT)} is missing: run pc/tools/build_boot.py --target windows")
    if not (ROOT / "data/SLUS_010.40").exists():
        raise SystemExit("data/ is missing: run the decompilation's make first (README, Build step 3)")
    dest = args.destination
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe, dest / "rood.exe")
    # Without its debug information (over 100 MB); the fixed image base keeps
    # crash addresses valid for the unstripped build.
    # (On a Windows host, MSYS2's MinGW-w64 tools have plain names.)
    strip = "strip" if os.name == "nt" else "x86_64-w64-mingw32-strip"
    subprocess.run([strip, "--strip-debug", str(dest / "rood.exe")], check=True)
    (dest / "play.bat").write_bytes(PLAY_BAT.replace("\n", "\r\n").encode())
    if args.callo:
        callo = ROOT / "pc/build" / args.callo_build / "rood.exe"
        if not callo.exists():
            raise SystemExit(f"{callo} is missing: run build_boot.py --target windows --callo")
        shutil.copy2(callo, dest / "rood-callo.exe")
        subprocess.run([strip, "--strip-debug", str(dest / "rood-callo.exe")], check=True)
        bat = PLAY_BAT.replace("rood.exe", "rood-callo.exe").replace("rood.log", "rood-callo.log")
        (dest / "play-callo.bat").write_bytes(bat.replace("\n", "\r\n").encode())
    for tree in ["data", "lighting", "pc/assets", "pc/third_party/fonts"]:
        shutil.copytree(ROOT / tree, dest / tree, dirs_exist_ok=True)
    # Licences: this project's, the vendored code linked into rood.exe, NRD
    # and its dependencies (when the build fetched them) and the MinGW-w64
    # runtime (winpthreads), all statically linked.
    licenses = dest / "licenses"
    licenses.mkdir(exist_ok=True)
    mingw_licenses = [("mingw-w64.txt", "/usr/share/doc/mingw-w64-common/copyright")]
    if os.name == "nt":  # MSYS2: <prefix>/share/licenses next to <prefix>/bin/gcc.exe
        share = pathlib.Path(shutil.which("gcc") or ".").resolve().parents[1] / "share/licenses"
        mingw_licenses = [("mingw-w64.txt", str(share / "crt/COPYING.MinGW-w64-runtime.txt")),
                          ("winpthreads.txt", str(share / "winpthreads/COPYING"))]
    for name, source in [
        ("LICENSE.txt", "LICENSE"),
        ("THIRD_PARTY.md", "THIRD_PARTY.md"),
        ("SDL3.txt", "pc/third_party/SDL3/LICENSE.txt"),
        ("imgui.txt", "pc/third_party/imgui/LICENSE.txt"),
        ("ImGuizmo.txt", "pc/third_party/ImGuizmo/LICENSE"),
        ("smaa.txt", "pc/third_party/smaa/LICENSE.txt"),
        ("psycross.txt", "pc/third_party/psycross/LICENSE"),
        ("psycross-audio.txt", "pc/third_party/psycross-audio/LICENSE"),
        ("NRD.txt", "pc/build/nrd/_deps/nrd-src/LICENSE.txt"),
        ("ShaderMake.txt", "pc/build/nrd/_deps/shadermake-src/LICENSE.txt"),
        ("MathLib.txt", "pc/build/nrd/_deps/mathlib-src/LICENSE.txt"),
        *mingw_licenses,
    ]:
        if (ROOT / source).exists():
            shutil.copy2(ROOT / source, licenses / name)
    for empty in ["disks", "pc/user", "pc/save", "pc/lights", "pc/build/boot"]:
        (dest / empty).mkdir(parents=True, exist_ok=True)
    disc = ROOT / "disks/SLUS-01040.bin"
    if args.disc and disc.exists():
        shutil.copy2(disc, dest / "disks/SLUS-01040.bin")
    missing = "" if (dest / "disks/SLUS-01040.bin").exists() else \
        " (put your disc image at disks\\SLUS-01040.bin, or rerun with --disc)"
    print(f"{dest}: run play.bat{missing}")


if __name__ == "__main__":
    main()
