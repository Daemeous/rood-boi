#!/usr/bin/env python3
"""Save states survive an engine-only rebuild (64 bits; pc/src/snapshot.c,
pc/tools/snapshot_layout.py).

    ROOD_ARCH=64 python3 pc/tests/snapshot_resume_smoke.py

1. Relinks the game (build_boot.py's link_command.json) with an extra engine
   object in front of every other: the engine's code, read-only data, data
   and bss all move (a stand-in for an edit to a pc/src renderer file). The
   game's pinned symbols must keep their addresses; pc_tick must move.
2. Runs a copy of the game binary to the opening checkpoint, replaces the
   copy with the relinked binary and sends RPC restart {paused}: the process
   saves the auto-snapshot, runs the new executable and resumes there. The
   game state (RPC snapshot hash: guest RAM, scratchpad, I/O page, native
   game statics, overlay statics, VRAM) must equal the state before, and
   after FRAMES more vsyncs it must equal the original build resuming the
   same snapshot and running the same vsyncs. A clean quit (RPC quit with
   ROOD_AUTOSNAP set) writes the auto-snapshot again.
3. A game-code change is refused: the snapshot with another code key exits
   with "the game's code changed" under ROOD_SNAPSHOT, and boots normally
   under ROOD_RESUME. The key itself: changing a byte of a game object's
   code changes its digest; the same change in a host unit's code does not
   (only its state layout counts).
On the 32-bit build (no pinned layout) the test is skipped."""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import checkpoints  # noqa: E402
import rood_rpc  # noqa: E402
import snapshot_diff  # noqa: E402
import snapshot_layout  # noqa: E402
from rood_rpc import Deadline, Game, RpcError, awake  # noqa: E402

FRAMES = 60
STATE_KEYS = ("ram", "scratch", "io", "state", "overlays", "vram")
# The engine moved: code (and so every return address above the boundary),
# read-only data, data and bss.
PAD = """
.section .text.rood_test_pad,"axR",@progbits
.skip 65536, 0xcc
.section .rodata.rood_test_pad,"aR",@progbits
.skip 65536
.section .data.rood_test_pad,"awR",@progbits
.skip 4096
.section .bss.rood_test_pad,"awR",@nobits
.skip 1048576
.section .note.GNU-stack,"",@progbits
"""
failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)


def symbols(binary):
    out = subprocess.run(["nm", "-n", str(binary)], capture_output=True, text=True, check=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table.setdefault(parts[2], int(parts[0], 16))
    return table


def relink(work):
    """The game linked again with the engine moved; its path."""
    command = json.loads((rood_rpc.BUILD / "link_command.json").read_text())
    pad_source, pad = work / "engine_pad.s", work / "engine_pad.o"
    pad_source.write_text(PAD)
    subprocess.run(["gcc", "-m64", "-c", str(pad_source), "-o", str(pad)], check=True)
    output = command.index("-o") + 1
    moved = work / "rood_moved"
    command[output] = str(moved)
    first = command.index("-Wl,--gc-sections") + 1  # before every object
    command[first:first] = [str(pad)]
    subprocess.run(command, cwd=ROOT, check=True)
    return moved


def state(game):
    h = game.call("snapshot", action="hash")
    return {k: h[k] for k in STATE_KEYS}, h["vsync"]


def wait_listening(path, process, old, timeout=120):
    """The restarted game's socket (a new file: never the old process's); `timeout`: a hang guard
    (awake seconds: a stopped game's pause is not counted)."""
    deadline = Deadline(timeout)
    gone = False
    while True:
        try:
            gone = gone or os.stat(path).st_ino != old
        except OSError:
            gone = True
        if gone and rood_rpc.listening(path):
            return
        if process.poll() is not None:
            raise RpcError(f"the game exited (status {process.returncode})")
        if deadline.passed():
            raise RpcError("the restarted game did not open its socket")
        time.sleep(0.02)


def main():
    if os.environ.get("ROOD_ARCH") != "64":
        print("SKIP: the pinned layout is 64-bit only (run with ROOD_ARCH=64)")
        return 0
    out = pathlib.Path(os.environ.get("ROOD_OUT") or rood_rpc.OUT)
    work = out / "snapshot_resume"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    original = rood_rpc.BINARY
    socket = f"/tmp/rood-resume-{os.getpid()}.sock"
    env = dict(checkpoints._base_env(work), ROOD_AUTOSNAP=str(work / "last.snap"))

    # 1. The engine moves, the game does not.
    moved = relink(work)
    a, b = symbols(original), symbols(moved)
    start, end = a["__start_rood_game_text"], a["rood_game_end"]
    pinned = {n: v for n, v in a.items() if start <= v < end}
    check(len(pinned) > 1000 and all(b.get(n) == v for n, v in pinned.items()),
          f"the relinked game keeps all {len(pinned)} pinned symbols at their addresses")
    check(a["pc_tick"] != b["pc_tick"] and a["pc_snap_boundary"] != b["pc_snap_boundary"],
          f"the engine moved (pc_tick {a['pc_tick']:#x} -> {b['pc_tick']:#x})")

    # 2. Restart into the relinked binary at the same moment.
    running = work / "rood_game"
    shutil.copy2(original, running)
    rood_rpc.BINARY = running
    game = checkpoints.launch_at("opening", fast=True, socket_path=socket, log=work / "game.log", extra_env=env)
    process = game.processes[0]
    before, vsync = state(game)
    shutil.copy2(moved, work / "rood_game.new")
    os.replace(work / "rood_game.new", running)  # as a rebuild replaces it
    started = awake()
    old_socket = os.stat(socket).st_ino
    reply = game.call("restart", paused=True)
    game.file.close()
    game.sock.close()
    wait_listening(socket, process, old_socket)
    game = Game(socket, game.processes)
    after, vsync_after = state(game)
    seconds = awake() - started
    check(reply.get("vsync") == vsync and vsync_after == vsync,
          f"restart resumed at the vsync it was asked at ({vsync}, {seconds:.2f} s to the socket)")
    check(after == before, "the game state after the restart equals the state before it")
    log = work / "game.log"
    check("[snapshot] restarting" in log.read_text(errors="replace"), "the process ran the new executable")
    shutil.copyfile(work / "last.snap", work / "at-restart.snap")
    game.run(FRAMES)
    later_moved, _ = state(game)
    game.call("snapshot", action="save", path=str(work / "moved-end.snap"))
    game.call("quit")  # with ROOD_AUTOSNAP: saved at the next vsync, then exits
    try:
        process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        process.kill()
    for p in game.processes[1:]:
        p.terminate()
    text = log.read_text(errors="replace")
    check(f"saved {work / 'last.snap'} at vsync {vsync + FRAMES}" in text, "a clean quit wrote the auto-snapshot")

    # The original build from the same snapshot, the same vsyncs.
    rood_rpc.BINARY = original
    game = Game.launch(fast=True, socket_path=socket, log=work / "reference.log", extra_env=env,
                       snapshot=work / "at-restart.snap")
    try:
        reference, _ = state(game)
        check(reference == before, "the original build resumes the same state")
        game.run(FRAMES)
        later_original, _ = state(game)
        game.call("snapshot", action="save", path=str(work / "original-end.snap"))
    finally:
        game.close()
    # The native statics hold the last vsync's wall-clock time: compared
    # without it, as snapshot_equivalence does, the game stack included.
    differs = [k for k in STATE_KEYS if k != "state" and later_moved[k] != later_original[k]]
    differs += snapshot_diff.diff(snapshot_diff.read(work / "moved-end.snap"),
                                  snapshot_diff.read(work / "original-end.snap"),
                                  snapshot_diff.Symbols(original), stack=True)
    check(not differs, f"{FRAMES} vsyncs later the relinked and the original build are in the same state "
          "(guest memory, VRAM, native and overlay statics, SPU, the game's stack)")
    for line in differs[:20]:
        print("    " + str(line))

    # 3. A game-code change is refused.
    data = bytearray((work / "at-restart.snap").read_bytes())
    data[16] ^= 0xFF  # the header's code key (snapshot.c: magic, version, pointer size, code key)
    (work / "other-code.snap").write_bytes(bytes(data))
    rood_rpc.BINARY = moved
    try:
        Game.launch(fast=True, socket_path=socket, log=work / "refused.log", extra_env=env,
                    snapshot=work / "other-code.snap").close()
        check(False, "a snapshot of other game code is refused (ROOD_SNAPSHOT)")
    except RpcError:
        text = (work / "refused.log").read_text(errors="replace")
        check("the game's code changed" in text, "a snapshot of other game code is refused (ROOD_SNAPSHOT)")
    game = Game.launch(fast=True, socket_path=socket, log=work / "lenient.log",
                       extra_env=dict(env, ROOD_RESUME=str(work / "other-code.snap")))
    try:
        game.pause()
        booted = game.status()["vsync"] < 200
    finally:
        game.close()
    text = (work / "lenient.log").read_text(errors="replace")
    check(booted and "booting normally" in text, "ROOD_RESUME with a refused snapshot boots normally")

    # The key: game code counts, host code does not.
    def flipped(name, pinned_object):
        copy = work / name
        shutil.copyfile(rood_rpc.BUILD / name, copy)
        elf = snapshot_layout.Elf(copy)
        text_section = next(s for s in elf.sections if s["name"].startswith(".text") and s["size"] > 16)
        data = bytearray(copy.read_bytes())
        data[text_section["offset"] + 8] ^= 0x55
        changed = work / ("changed-" + name)
        changed.write_bytes(bytes(data))
        return (snapshot_layout._info(copy, pinned_object)["digest"]
                != snapshot_layout._info(changed, pinned_object)["digest"])
    check(flipped("sdk.o", True), "a change to a game unit's code changes the code key")
    check(not flipped("gpu_hw.o", False), "a change to a host unit's code keeps the layout key")

    if not failures:
        for name in ("rood_moved", "rood_game"):
            (work / name).unlink(missing_ok=True)  # 100 MB each
    print("PASS" if not failures else f"FAIL ({len(failures)})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
