#!/usr/bin/env python3
"""The routing mechanism (pc/docs/Ownership.md) on i386, x86-64 and MinGW:
a unit compiled at -O2 with its routed definition renamed, the generated
trampoline, and calls from inside the unit, from outside and through a
function pointer reach the owner pc_owner[] selects (owner 3, alternate,
flips per call and counts each side). Prints the log per ABI;
MinGW's runs under wine when present (else the link and the relocations are
checked)."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import ownership  # noqa: E402

EXPECTED = "1 2 101 102 107 203 104 5 106 | 2 1"  # alternate: owned, original, owned; counts
table = ownership.Table([ownership.Subsystem("probe", 0, "battle", "unit.c", "original", [],
                                             [(0, "F")], {})])
failures = []
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    for abi, cc, flags, exe in [("i386", "gcc", ["-m32", "-no-pie", "-fno-pie"], "probe32"),
                                ("sysv64", "gcc", ["-m64", "-no-pie", "-fpie"], "probe64"),
                                ("win64", "x86_64-w64-mingw32-gcc-posix", ["-mcmodel=small", "-static"], "probe.exe")]:
        if not shutil.which(cc):
            failures.append(f"{abi}: no {cc}")
            continue
        pre = subprocess.run([cc, "-E", str(HERE / "unit.c")], capture_output=True, check=True, timeout=60).stdout
        text, counts = ownership.rename_definitions(pre, {"F"})
        assert counts == {"F": 1}, counts
        (d / f"unit-{abi}.i").write_bytes(text)
        (d / f"routing-{abi}.c").write_text(ownership.routing_source(table, abi))
        objects = []
        for name, source in [("unit", d / f"unit-{abi}.i"), ("routing", d / f"routing-{abi}.c"),
                             ("owned", HERE / "owned.c"), ("main", HERE / "main.c")]:
            obj = d / f"{name}-{abi}.o"
            subprocess.run([cc, *[f for f in flags if f not in ("-no-pie", "-static")], "-O2",
                            "-ffunction-sections", "-c", str(source), "-o", str(obj)], check=True, timeout=60)
            objects.append(str(obj))
        subprocess.run([cc, *flags, *objects, "-o", str(d / exe)], check=True, timeout=60)
        if abi == "win64" and not shutil.which("wine"):
            dump = subprocess.run(["x86_64-w64-mingw32-objdump", "-dr", objects[0]], capture_output=True,
                                  text=True, check=True, timeout=60).stdout
            ok = "REL32\tF\n" in dump.replace("  ", "\t") or "IMAGE_REL_AMD64_REL32\tF" in dump
            print(f"{abi}: (no wine) G's calls relocate against F: {ok}")
            if not ok:
                failures.append(abi)
            continue
        runner = ["wine"] if abi == "win64" else []
        env = dict(os.environ)
        if abi == "win64":
            env.update(WINEPREFIX=str(d / "wine"), WINEDEBUG="-all")
        got = subprocess.run([*runner, str(d / exe)], capture_output=True, text=True,
                             check=True, timeout=120, env=env).stdout.strip()
        print(f"{abi}: {got}")
        if got != EXPECTED:
            failures.append(f"{abi}: {got!r}, expected {EXPECTED!r}")
print("FAILED: " + "; ".join(failures) if failures else "routing works on every ABI")
sys.exit(1 if failures else 0)
