#!/usr/bin/env python3
"""A rename leaves the save states' keys alone (pc/tools/snapshot_layout.py, pc/names/symbols.tsv).

    python3 pc/tests/snapshot_names_test.py

An object that holds a function, a table and a state variable is built with the decomp's names and again with
them renamed (the same bytes, objcopy --redefine-sym and the function's section renamed too), the way a name
import changes a unit. Its pinned digest and its host digest must be equal when the names are mapped back by
the symbol map, and differ when they are not. The end-to-end proof (a state saved by the build before the
rename resumes in the renamed one with an equal state hash) is run by hand for a name import; the keys being
equal is what makes it hold.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import snapshot_layout  # noqa: E402
import symbol_names  # noqa: E402

SOURCE = """
extern int D_80010000[4];
int func_80012345(int a) { return D_80010000[a & 3] + 1; }
int (*table_80012345[2])(int) = { func_80012345, func_80012345 };
const char __anon_1000[] = "text";
const char *anon_user(void) { return __anon_1000; }
__attribute__((section("rood_state_data"))) int D_80010000[4] = { 1, 2, 3, 4 };
"""
RENAMES = {"func_80012345": "vs_test_nextValue", "D_80010000": "vs_test_values"}
ANON_RENAMES = {"__anon_1000": "__anon_2468"}  # Zig numbers anonymous constants by its string pool's size
failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "x.c").write_text(SOURCE)
        old = tmp / "old.o"
        subprocess.run(["gcc", "-c", "-O1", "-ffunction-sections", "-fno-pie", str(tmp / "x.c"), "-o", str(old)], check=True)
        args = []
        for a, b in {**RENAMES, **ANON_RENAMES}.items():
            args += ["--redefine-sym", f"{a}={b}"]
        new = tmp / "new.o"
        subprocess.run(["objcopy", *args, "--rename-section", ".text.func_80012345=.text.vs_test_nextValue",
                        str(old), str(new)], check=True)
        table = tmp / "symbols.tsv"
        table.write_text("".join(f"function\tTEST\t{a}\t{b}\thigh\tT\n" for a, b in RENAMES.items()))
        canon = symbol_names.canonicaliser(table)
        for pinned in (True, False):
            kind = "pinned" if pinned else "host"
            a = snapshot_layout._info(old, pinned)["digest"]
            b = snapshot_layout._info(new, pinned, canon)["digest"]
            check(a == b, f"{kind} digest equal after the rename, names mapped back")
        check(snapshot_layout._info(old, True)["digest"] != snapshot_layout._info(new, True)["digest"],
              "without the map the renamed object's pinned digest differs (the test sees names)")
        check(canon("ending_vs_test_nextValue__b") == "ending_func_80012345__b", "a name inside a longer one is mapped")
        check(canon("vs_test_nextValueX") == "vs_test_nextValueX", "a name inside a word is not")
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
