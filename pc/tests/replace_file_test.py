#!/usr/bin/env python3
"""pc_replace_file (pc/src/platform.h), which every save goes through
(memory cards, settings, lighting layers, fixtures, prefabs, snapshots):
pc/tests/replace_file_probe.c saves three times in a row over the same file.

Built and run natively, and, when MinGW-w64 and Wine are installed, built for
Windows and run under Wine (one prefix per Wine version, made once and
shared by every checkout: pc/tools/shared_cache.py, wine/<version>): there
rename() fails when the target exists, which once left every memory card save
after the first in card1.mcd.tmp. Without Wine that half is skipped.

    python3 pc/tests/replace_file_test.py
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import shared_cache  # noqa: E402

PROBE = ROOT / "pc/tests/replace_file_probe.c"
MINGW = "x86_64-w64-mingw32-gcc-posix"


def run(command, **kwargs):
    print("$ " + " ".join(map(str, command)), flush=True)
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    sys.stdout.write(result.stdout)
    sys.stdout.write(result.stderr)
    return result.returncode


def main():
    failed = 0
    with tempfile.TemporaryDirectory(prefix="replace-file-") as scratch:
        scratch = pathlib.Path(scratch)
        native = scratch / "replace_file_probe"
        if run(["gcc", "-O2", "-Wall", "-Werror", "-I" + str(ROOT / "pc/src"), str(PROBE), "-o", str(native)]):
            return 1
        failed |= run([str(native), str(scratch)]) != 0
        if not shutil.which(MINGW) or not shutil.which("wine"):
            print("note: no MinGW-w64 or Wine: the Windows half is skipped")
        else:
            exe = scratch / "replace_file_probe.exe"
            if run([MINGW, "-O2", "-Wall", "-Werror", "-D_WIN32_WINNT=0x0A00", "-include",
                    str(ROOT / "pc/src/win_compat.h"), "-I" + str(ROOT / "pc/src"), str(PROBE), "-o", str(exe)]):
                return 1
            prefix = shared_cache.wine_prefix()
            env = shared_cache.wine_env(prefix)
            try:
                failed |= run(["wine", str(exe), "."], cwd=scratch, env=env, timeout=240) != 0
            except subprocess.TimeoutExpired:
                print("FAIL: the probe did not finish under Wine within 240 s")
                failed = 1
    print("FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
