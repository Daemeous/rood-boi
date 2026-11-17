#!/usr/bin/env python3
"""An effect script whose cursor is not on a command ends safely (BATTLE vs_effects_runScript, boot_runtime.c pc_effect_bad_opcode).

    python3 pc/tests/effect_opcode_smoke.py [--out DIR]

build_compare --casts enemies died in zone 29 room 0 (even alone): Rending Gale (action 188, an enemy's ability; its effect, 135, requests
a secondary effect with id 188) starts a script at block8Data + 188 * 3 = 0x234, where the bound effect package has no stub (its block 8
holds 14): the bytes there are other data and its first "command" is 0x61, past the table's 49 handlers. The game called whatever
the table's following bytes held, and the report of the stop (pc_unimplemented's stack dump, backtrace() off the game's own stack) died of
a segmentation fault as well.

Runs the enemies lane of build_compare on that room alone (BUILD_COMPARE_ENEMY_ROOMS 29,0; a game of its own; the abilities of the room's
enemy, 188 among them): it must finish (its RESULT line), drive action 188 and the ones after it, and the game must say so:
"[effect] command opcode 97 ... out of range: the script ends" in the log, no "[crash]" and no "[UNIMPLEMENTED]".
--out keeps the lane's directory."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT  # noqa: E402

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "effect_opcode"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    binary = os.environ.get("ROOD_BINARY") or str(ROOT / "pc/build/boot64/rood_battle_probe")
    card = os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd")
    env = dict(os.environ, BUILD_COMPARE_ENEMY_ROOMS="29,0")
    done = subprocess.run([sys.executable, str(ROOT / "pc/tools/build_compare.py"), binary, "--play-casts", "--casts", "enemies", "--out", str(out),
                           "--card", card], env=env, capture_output=True, text=True, timeout=600)
    check("\nRESULT" in "\n" + done.stdout, f"the lane finishes (exit {done.returncode}, a RESULT line)")
    check("driving action 188 by enemy" in done.stderr, "action 188 (Rending Gale) is driven")
    check("driving action 224 by enemy" in done.stderr, "the abilities after it are driven (224, the room's last)")
    log = (out / "casts.log").read_text(errors="replace") if (out / "casts.log").exists() else ""
    check("[crash]" not in log and "[UNIMPLEMENTED]" not in log, "no crash and no stop in the game's log")
    check("[effect] command opcode" in log and "the script ends" in log, "the out-of-range command is reported and the script ends")
    print(f"{len(failures)} failure(s)" if failures else "effect_opcode_smoke: all checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
