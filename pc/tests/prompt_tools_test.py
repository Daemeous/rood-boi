#!/usr/bin/env python3
"""The model-facing prompt tools: nothing local in what is sent, and the C
the prompts carry is the function's own.

  - bakeoff_context.c_body ends a one-line function on its own line
    (func_8006CE50, func_8007C088) and a multi-line one at its brace;
  - prompt_privacy.sanitise removes the home directory, the checkout, the
    Zig cache, scratch directories and the user name, and check() refuses
    what is left;
  - every prompt spec_bakeoff.py and port_pipeline.py build (and a retry's
    feedback carrying local paths) is free of the home path.

CPU only; needs a 64-bit build (exit 77 without one)."""
import pathlib
import subprocess
import sys
import os
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import bakeoff_context as bc  # noqa: E402
import prompt_privacy  # noqa: E402
import zig_toolchain  # noqa: E402

BUILD = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "pc/build/boot64"
HOME = str(pathlib.Path.home())


def leaks(text):
    return [needle for needle in (HOME, os.path.join(tempfile.gettempdir(), "claude"), str(zig_toolchain.global_cache())) if needle in text]


def main():
    failures = []
    # c_body: the two one-line bake-off functions, from the .c and the .i.
    source = (ROOT / "src/BATTLE/BATTLE.PRG/146C.c").read_text(errors="replace")
    expected = {"func_8006CE50": "void func_8006CE50(void) { func_8008D5A0(0xB4); }",
                "func_8007C088": "int func_8007C088(int arg0) { return vs_battle_actors[arg0]->weaponDrawn & 1; }"}
    texts = [("146C.c", source)]
    unit_i = next(BUILD.glob("*_146C.i"), None)
    if unit_i:
        texts.append((unit_i.name, unit_i.read_text(errors="replace")))
    for label, text in texts:
        for name, body in expected.items():
            got = bc.c_body(text, name)
            if got != body:
                failures.append(f"c_body({label}, {name}): {got!r:.200}")
    multi = bc.c_body(source, "_updateEnemyKills")
    if not multi or not multi.rstrip().endswith("}") or "func_" in multi.split("\n")[-1] or multi.count("{") != multi.count("}"):
        failures.append(f"c_body(_updateEnemyKills) unbalanced: {multi!r:.300}")
    tricky = 'int f(void) { char *s = "}"; /* } */ // }\n return \'}\'; }\nint g(void) { return 1; }\n'
    if bc.c_body(tricky, "f") != tricky.split("\nint g")[0]:
        failures.append(f"c_body skips literals/comments: {bc.c_body(tricky, 'f')!r}")

    # The sanitiser.
    sample = (f"{HOME}/x/y.c:3: error\n{ROOT}/pc/tools/zig_diff.py\n{zig_toolchain.global_cache()}/o/1/a.zig\n"
              f"{os.path.join(tempfile.gettempdir(), 'claude')}-1000/-home-x/abc/scratchpad/work/ref.c:12\n{tempfile.gettempdir()}/tmpabc/x.zig")
    clean = prompt_privacy.sanitise(sample)
    if leaks(clean) or prompt_privacy.problems(clean):
        failures.append(f"sanitise left: {leaks(clean) + prompt_privacy.problems(clean)}: {clean!r}")
    try:
        prompt_privacy.check(sample)
        failures.append("check() accepted text with the home path")
    except prompt_privacy.Leak:
        pass

    if not (BUILD / "imports.ld").exists():
        print(f"prompt_tools_test: no 64-bit build in {BUILD}: the prompt checks are skipped")
        return finish(failures, 77 if not failures else 1)
    with tempfile.TemporaryDirectory(prefix="prompt-tools-") as tmp:
        tmp = pathlib.Path(tmp)
        # spec_bakeoff's prompts (no calls).
        r = subprocess.run([sys.executable, ROOT / "pc/tools/spec_bakeoff.py", "--out", tmp / "spec", "--prompts-only",
                            "--build", BUILD, "--only", "func_8007C088", "--only", "func_800AD494"],
                           capture_output=True, text=True)
        if r.returncode:
            failures.append(f"spec_bakeoff --prompts-only: exit {r.returncode}: {r.stderr[-500:]}")
        for p in (tmp / "spec").glob("*/prompt.md"):
            if leaks(p.read_text()):
                failures.append(f"spec_bakeoff prompt {p.parent.name}: contains {leaks(p.read_text())}")
        # port_pipeline's prompts and a retry's feedback (dry run: no calls).
        pipeline = ROOT / "pc/tools/port_pipeline.py"
        if pipeline.exists():
            r = subprocess.run([sys.executable, pipeline, "--dry-run", "--out", tmp / "pipe", "--build", BUILD,
                                "--leak-probe", "func_8006B214", "_insertActor"], capture_output=True, text=True)
            if r.returncode:
                failures.append(f"port_pipeline --dry-run: exit {r.returncode}: {(r.stderr or r.stdout)[-800:]}")
            sent = list((tmp / "pipe").rglob("*.md"))
            if not sent:
                failures.append("port_pipeline --dry-run wrote no prompts")
            for p in sent:
                if leaks(p.read_text()):
                    failures.append(f"port_pipeline {p.relative_to(tmp)}: contains {leaks(p.read_text())}")
    return finish(failures, 0)


def finish(failures, code):
    for f in failures:
        print("FAIL:", f)
    print("prompt_tools_test: " + ("FAILED" if failures else "passed"))
    return 1 if failures else code


if __name__ == "__main__":
    sys.exit(main())
