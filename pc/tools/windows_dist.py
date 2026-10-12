#!/usr/bin/env python3
"""Assemble a playable Windows folder from the --target windows build.

    python3 pc/tools/build_boot.py --target windows
    python3 pc/tools/windows_dist.py /mnt/c/Games/rood-boi

The folder gets rood.exe (static: no DLLs to ship), play.bat, the files the
game reads relative to its working directory (data/ from your disc, the
lighting packs, pc/assets) and empty pc/user, pc/save, pc/lights. The disc
image is copied only with --disc (it is ~740 MB; or place it at
disks/SLUS-01040.bin yourself). Existing saves and settings in the folder
are kept. Like data/, the result contains your own game data: keep it to
yourself.
"""
import argparse
import pathlib
import shutil

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
    args = parser.parse_args()
    exe = ROOT / "pc/build/win64/rood.exe"
    if not exe.exists():
        raise SystemExit(f"{exe.relative_to(ROOT)} is missing: run pc/tools/build_boot.py --target windows")
    if not (ROOT / "data/SLUS_010.40").exists():
        raise SystemExit("data/ is missing: run the decompilation's make first (README, Build step 3)")
    dest = args.destination
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exe, dest / "rood.exe")
    (dest / "play.bat").write_bytes(PLAY_BAT.replace("\n", "\r\n").encode())
    for tree in ["data", "lighting", "pc/assets"]:
        shutil.copytree(ROOT / tree, dest / tree, dirs_exist_ok=True)
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
