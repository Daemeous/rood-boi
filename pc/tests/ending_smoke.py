#!/usr/bin/env python3
"""The ending (ENDING.PRG, compiled natively) from a save, twice.

Continues a copy of the memory card's save and asks for the ending (RPC
`ending`, the developer jump that calls vs_main_showEndingAndReturnToTitle
from the field, as the final event script does). Each pass checks:
- the credits run: ENDING.PRG loads, its frame counter advances and the
  scroll (text over the decompressed illustrations) is on screen;
- Start skips them (the fade-out ends the loop);
- the MISSION COMPLETE screen: score, map completion and rank, drawn over the
  ENDSCRnn picture (a tpage with the "reserved" depth 3, read as 15-bit);
- any button leaves it; the title's clear-data save screen is declined
  (Cross, NO) and the title menu is reached;
then the save is continued again and the second pass must give the same
score and rank (the overlay's native statics restart with ENDING.PRG). The
card copy must be unchanged.

    python3 pc/tests/ending_smoke.py --card pc/save/card1.mcd [--out DIR] [--evidence DIR]
"""
import argparse
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402

# ENDING.PRG's data in guest RAM (build/data/ENDING/ENDING.PRG.elf).
CREDITS_DONE = 0x800DC188  # u8: the credits loop ends
CREDITS_FRAMES = 0x800DC18C
RANK = 0x800DC1F0
SCORE = 0x800DC1F8
MAP_COMPLETION = 0x800DC1FC  # percent
RANK_SCREEN_STEP = 0x800DC20C  # the MISSION COMPLETE animation's frame


def mean(path):
    from PIL import Image
    image = Image.open(path).convert("L")
    return sum(image.getdata()) / (image.size[0] * image.size[1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    ap.add_argument("--out")
    ap.add_argument("--evidence", help="save the screens (PNG) here")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ending"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-ending-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    # Continue from the card's first save (checkpoints.py: booted, or its save state).
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", socket_path=socket,
                     extra_env={"ROOD_GI": "0"})

    def log():
        return pathlib.Path(game.log).read_text(errors="replace")

    def crashed():
        text = log()
        return "[UNIMPLEMENTED]" in text or "[crash]" in text

    def u32(address):
        return int.from_bytes(game.read(address, 4), "little")

    def screen(name):
        path = out / f"{name}.png"
        game.present_screenshot(path)
        if args.evidence:
            evidence = pathlib.Path(args.evidence)
            evidence.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, evidence / path.name)
        return mean(path)

    def in_field():
        state = game.state()
        return state.get("player") is not None and not state.get("game_over")

    def continue_save():
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        return state.get("player") is not None and not state.get("game_over")

    def ending(n):
        check(game.call("ending")["requested"], f"pass {n}: ending requested from the field")
        game.run(1200)
        frames = u32(CREDITS_FRAMES)
        check(not crashed() and 900 < frames <= 1200, f"pass {n}: the credits loop runs ({frames} frames)")
        if not check(screen(f"pass{n}-credits") > 30, f"pass {n}: credits on screen"):
            return None
        game.press("start", vsyncs=4, settle=200)
        check(game.read(CREDITS_DONE, 1) == b"\x01", f"pass {n}: Start skips the credits")
        for _ in range(60):
            game.run(30)
            if crashed() or u32(RANK_SCREEN_STEP) > 0x120:
                break
        result = u32(SCORE), u32(MAP_COMPLETION), u32(RANK)
        check(not crashed() and u32(RANK_SCREEN_STEP) > 0x120,
              f"pass {n}: MISSION COMPLETE screen (score {result[0]}, map {result[1]}%, rank {result[2]})")
        check(result[0] <= 999999999 and result[1] <= 100 and result[2] < 16, f"pass {n}: score/map/rank in range")
        check(screen(f"pass{n}-rank") > 20, f"pass {n}: rank screen drawn")
        titles = log().count("TITLE MENU READY")
        game.press("cross", vsyncs=4, settle=240)  # leave the rank screen: the clear-data save screen
        game.press("cross", vsyncs=4, settle=60)  # cancel: "Game is not saved. Save now?"
        game.press("down", vsyncs=4, settle=30)  # NO
        game.press("circle", vsyncs=4, settle=60)
        for _ in range(60):
            if crashed() or log().count("TITLE MENU READY") > titles:
                break
            game.run(60)
        check(not crashed() and log().count("TITLE MENU READY") > titles, f"pass {n}: back at the title menu")
        return result

    try:
        first = second = None
        if check(in_field(), "continued the save with Ashley in the field"):
            first = ending(1)
        if first and not failures:
            game.pause()
            if check(continue_save(), "continued the save again"):
                second = ending(2)
            check(second == first, f"pass 2 gives the same score, map and rank ({second} == {first})")
        check(card.read_bytes() == pathlib.Path(args.card).read_bytes(), "the card copy is unchanged (nothing saved)")
        check(not crashed(), "no [UNIMPLEMENTED] or crash in the game log")
    finally:
        game.close()
    if crashed():
        print("\n".join(line for line in log().splitlines() if "[" in line and "]" in line)[-2000:])
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
