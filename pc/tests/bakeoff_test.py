#!/usr/bin/env python3
"""The bake-off tools (pc/docs/porting/battle/BAKEOFF.md) against the
reference ports: every prompt carries its function's signature, C and
declarations; the scorer passes the eleven reference ports and fails a
wrong one, a missing one and one that does not compile. CPU only; needs a
64-bit build (exit 77 without one)."""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "pc/build/boot64"


def main():
    if not (BUILD / "imports.ld").exists():
        print(f"bakeoff_test: no 64-bit build in {BUILD}: skipped")
        return 77
    entries = tomllib.loads((ROOT / "pc/tests/zig_diff/bakeoff.toml").read_text())["function"]
    failures = []
    with tempfile.TemporaryDirectory(prefix="bakeoff-test-") as tmp:
        tmp = pathlib.Path(tmp)
        subprocess.run([sys.executable, ROOT / "pc/tools/bakeoff_context.py", "--all", tmp / "prompts", "--no-guide",
                        "--build", BUILD], check=True, capture_output=True)
        for e in entries:
            text = (tmp / "prompts" / f"{e['name']}.md").read_text()
            for needle in (e["signature"], f"/// orig: {e['name']} (", f" {e['name']}(", "## `views_", "## `c_"):
                if needle not in text:
                    failures.append(f"prompt {e['name']}: lacks {needle!r}")
        candidates = tmp / "candidates"
        subprocess.run([sys.executable, ROOT / "pc/tools/bakeoff_score.py", "--reference", "--seeds", "1",
                        "--iterations", "2000", "--build", BUILD, "--json", tmp / "reference.json"], capture_output=True)
        reference = json.loads((tmp / "reference.json").read_text())
        if reference["summary"]["passed"] != len(entries):
            failures.append(f"reference ports: {reference['summary']}")
        candidates.mkdir()
        # A wrong one, one that does not compile, the rest missing.
        lerp = (ROOT / "game/battle/vector_lerp.zig").read_text()
        (candidates / "vector_lerp.zig").write_text(lerp.replace("const start1: c_int = src[2];", "const start1: c_int = src[4];"))
        timer = (ROOT / "game/battle/room_timer.zig").read_text()
        (candidates / "room_timer.zig").write_text(timer.replace("c.func_8008D5A0(0xB4);", "c.func_8008D5A0(0xB4)"))
        shutil.copy(ROOT / "game/battle/actor_weapon.zig", candidates / "actor_weapon.zig")
        result = subprocess.run([sys.executable, ROOT / "pc/tools/bakeoff_score.py", candidates, "--seeds", "1",
                                 "--iterations", "2000", "--build", BUILD, "--json", tmp / "candidates.json"],
                                capture_output=True)
        verdicts = {r["function"]: r["verdict"] for r in json.loads((tmp / "candidates.json").read_text())["functions"]}
        expected = {"vs_battle_lerpSvector": "mismatch", "func_8006CE50": "compile error", "func_8007C088": "pass",
                    "func_800A0204": "missing"}
        for name, verdict in expected.items():
            if verdicts.get(name) != verdict:
                failures.append(f"scorer: {name}: {verdicts.get(name)}, expected {verdict}")
        if result.returncode != 1:
            failures.append(f"scorer: exit {result.returncode} with failing candidates, expected 1")
    for f in failures:
        print("FAIL:", f)
    print("bakeoff_test: " + ("FAILED" if failures else "passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
