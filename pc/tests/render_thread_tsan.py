#!/usr/bin/env python3
"""The renderer thread's mailbox (pc/src/render_thread.c) built with ThreadSanitizer and stressed in lockstep and async (render_thread_tsan.c).

    python3 pc/tests/render_thread_tsan.py
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
out = ROOT / "pc/build/tests/render_thread_tsan"
out.mkdir(parents=True, exist_ok=True)
binary = out / "render_thread_tsan"
build = subprocess.run(["gcc", "-fsanitize=thread", "-O1", "-g", "-Wall", "-Werror", "-o", str(binary), str(ROOT / "pc/tests/render_thread_tsan.c"),
                        str(ROOT / "pc/src/render_thread.c"), "-lpthread"], capture_output=True, text=True)
if build.returncode:
    print(build.stdout, build.stderr)
    sys.exit(1)
for mode in ("1", "2"):
    run = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=300)
    print(run.stdout.strip())
    if run.returncode or "ThreadSanitizer" in run.stderr:
        print(run.stderr[-3000:])
        print(f"FAIL: render_thread {mode}")
        sys.exit(1)
print("PASS: render_thread_tsan")
