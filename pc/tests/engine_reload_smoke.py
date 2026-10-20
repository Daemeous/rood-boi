#!/usr/bin/env python3
"""The engine library reloads in place (64 bits, Linux; build_boot.py
--engine-library, pc/src/engine_host.c, engine_reload.c;
pc/docs/plans/2026-10-19-engine-reload.md).

    python3 pc/tests/engine_reload_smoke.py        (ROOD_RENDERER=vulkan: on Vulkan)

Builds pc/build/dev64 (rood_host and librood_engine.so; ROOD_BUILD_LOCK
serialises it) unless ROOD_ENGINE_SMOKE_NO_BUILD=1, and runs the host on a
copy of the library (ROOD_ENGINE_LIBRARY), from the opening checkpoint:

1. Interposition: a setting the engine applies (qol_map_invert) lands in the
   host-owned variable the game reads (read_symbol on the host).
2. RPC reload while paused: the reply names generation 2 at the same vsync,
   the library at its base, no host data left pointing into the old library;
   the game state (RPC snapshot hash) equals the state before; the game is
   still paused; the setting and the host-owned variable kept their values.
3. An engine change takes effect: engine_reload.c is compiled again with
   another ROOD_ENGINE_NOTE (the manifest's command), the library relinked
   with it (the manifest's link) and swapped in; RPC reload's reply carries
   the note.
4. A library that does not load (not ELF) is refused: the reply is an error,
   the previous library runs again and the state is unchanged.
5. The game keeps running: FRAMES vsyncs later its state (guest memory,
   VRAM, native and overlay statics, SPU, the game's stack) equals a second
   game resuming the same moment with no reload; it presented every vsync.
6. Reloads while running, and the resident memory they cost (reported; a
   reload must not cost more than LEAK_LIMIT MB).
7. With the tests' memory card (card_fixtures.card_guard; skipped without
   one), in the save room: a reload is refused while the modern menu is open
   (its state is the engine's: dev_loop.sh retries), goes through once it
   closed, and the menu opens again on the new library.
"""
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
from rood_rpc import Game, RpcError  # noqa: E402

FRAMES = 120
RUNNING_RELOADS = 4
LEAK_LIMIT = 64  # MB of resident memory per reload
STATE_KEYS = ("ram", "scratch", "io", "state", "overlays", "vram")
DEV = ROOT / "pc/build/dev64"
failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)


def state(game):
    h = game.call("snapshot", action="hash")
    return {k: h[k] for k in STATE_KEYS}, h["vsync"]


def rss_mb(pid):
    for line in pathlib.Path(f"/proc/{pid}/status").read_text().splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1]) / 1024
    return 0.0


def build():
    if os.environ.get("ROOD_ENGINE_SMOKE_NO_BUILD") == "1":
        return
    command = [sys.executable, str(ROOT / "pc/tools/build_boot.py"), "--arch", "64", "--engine-library"]
    if os.environ.get("ROOD_BUILD_LOCK"):
        command = ["flock", os.environ["ROOD_BUILD_LOCK"], *command]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        sys.exit(result.stdout[-3000:] + result.stderr[-3000:])


def build_with_note(work, note):
    """The library with engine_reload.c compiled with ROOD_ENGINE_NOTE=note: its path."""
    manifest = json.loads((DEV / "engine_library.json").read_text())
    unit = next(u for u in manifest["units"] if u["object"].endswith("/engine_reload.o"))
    obj = work / "engine_reload_note.o"
    command = list(unit["command"])
    command[command.index("-o") + 1] = str(obj)
    subprocess.run(command + [f'-DROOD_ENGINE_NOTE="{note}"'], cwd=ROOT, check=True)
    link = [str(obj) if a == unit["object"] or a == str(ROOT / unit["object"]) else a for a in manifest["link"]]
    library = work / "librood_engine.note.so"
    link[-1] = str(library)
    subprocess.run(link, cwd=ROOT, check=True)
    return library


def swap(source, target):
    """Replace the loaded library's file as a build does (a new file renamed over it)."""
    staged = target.with_name(target.name + ".new")
    shutil.copyfile(source, staged)
    os.replace(staged, target)


def menu_while_open(work, socket, env):
    from card_fixtures import card_guard
    from weapon_slots_smoke import default_card
    if not default_card().exists():
        print("skip: no memory card for the save room (the menu part)")
        return
    with card_guard(work / "cards") as cards:
        game = checkpoints.launch_at("save_room", card=cards.card1, fast=True, socket_path=socket,
                                     log=work / "menu.log", extra_env=env)
        try:
            menu_state = rood_rpc.symbols()["vs_battle_menuState"]
            game.call("ui_menu", modern=True)
            game.press("triangle", vsyncs=3, settle=40)
            check(game.call("ui_menu")["visible"], "the modern menu is open")
            try:
                game.call("reload")
                check(False, "a reload is refused while the modern menu is open")
            except RpcError as error:
                check("retry" in str(error) and "menu" in str(error),
                      f"a reload is refused while the modern menu is open ({error})")
            game.press("triangle", vsyncs=3, settle=120)
            closed = not game.call("ui_menu")["visible"] and game.read(menu_state, 1)[0] == 0x3F
            reply = game.call("reload")
            check(closed and reply.get("reloaded"), "once it closed, the reload goes through")
            game.press("triangle", vsyncs=3, settle=40)
            check(game.call("ui_menu")["visible"] and game.read(menu_state, 1)[0] == 0,
                  "the menu opens again on the new library")
        finally:
            game.close()


def main():
    if os.environ.get("ROOD_ARCH") != "64":
        print("SKIP: the engine library is 64-bit only (run with ROOD_ARCH=64)")
        return 0
    build()
    out = pathlib.Path(os.environ.get("ROOD_OUT") or rood_rpc.OUT)
    work = out / "engine_reload"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    library = work / "librood_engine.so"
    shutil.copyfile(DEV / "librood_engine.so", library)
    original = work / "librood_engine.original.so"
    shutil.copyfile(library, original)
    rood_rpc.BINARY = DEV / "rood_host"
    # The Debug-owned split has host shadow trampolines, unlike the static
    # binary used to prepare the runner's checkpoint cache. Never resume a
    # static stack into that different pinned code layout.
    os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
    os.environ["ROOD_SYMBOLS"] = str(DEV / "rood_host")  # read_symbol: the host's addresses
    socket = f"/tmp/rood-reload-{os.getpid()}.sock"
    env = dict(checkpoints._base_env(work), ROOD_ENGINE_LIBRARY=str(library))
    settings = ["qol_map_invert 1"]

    game = checkpoints.launch_at("opening", fast=True, socket_path=socket, log=work / "game.log", extra_env=env)
    process = game.processes[0]
    try:
        # 1. The engine writes the host's copy.
        before_invert = int.from_bytes(game.read_symbol("pc_qol_map_invert", 4), "little")
        game.call("settings", apply=settings)
        invert = int.from_bytes(game.read_symbol("pc_qol_map_invert", 4), "little")
        check(before_invert == 0 and invert == 1,
              f"a setting the engine applies lands in the host-owned variable ({before_invert} -> {invert})")
        game.call("snapshot", action="save", path=str(work / "start.snap"))
        before, vsync = state(game)

        # 2. Reload, paused.
        started = time.monotonic()
        reply = game.call("reload")
        seconds = time.monotonic() - started
        check(reply.get("reloaded") and reply.get("generation") == 2 and reply.get("vsync") == vsync,
              f"RPC reload swapped the library at vsync {vsync} in {reply.get('ms')} ms ({seconds:.2f} s round trip)")
        check(reply.get("base") == "0x64000000", f"the new library is at its base ({reply.get('base')})")
        check(reply.get("stale_host_words") == 0,
              f"no host data points into the old library {reply.get('stale_host') or ''}".rstrip())
        after, vsync_after = state(game)
        check(after == before and vsync_after == vsync, "the game state after the reload equals the state before")
        status = game.status()
        check(status["paused"], "the game is still paused (the RPC's state carried over)")
        line = game.call("settings", get="qol_map_invert").get("line")
        kept = int.from_bytes(game.read_symbol("pc_qol_map_invert", 4), "little")
        check(line == "qol_map_invert 1" and kept == 1, f"settings survive (\"{line}\", host-owned value {kept})")

        # 3. An engine change takes effect.
        note = f"smoke-{os.getpid()}"
        swap(build_with_note(work, note), library)
        reply = game.call("reload")
        check(reply.get("note") == note and reply.get("generation") == 3,
              f"a rebuilt engine unit runs after the reload (note {reply.get('note')!r})")
        check(state(game)[0] == before, "the state is still unchanged")

        # 4. A library that does not load.
        (work / "broken.so").write_bytes(b"not a library\n")
        swap(work / "broken.so", library)
        try:
            game.call("reload")
            check(False, "a library that does not load is refused")
        except RpcError as error:
            check("did not load" in str(error), f"a library that does not load is refused ({error})")
        check(state(game)[0] == before and game.status()["paused"],
              "the previous library runs again; state and pause unchanged")
        swap(original, library)
        reply = game.call("reload")
        check(reply.get("reloaded") and reply.get("note") == "", "the original library loads again")

        # 5. The game keeps running.
        presents = game.status()["presents"]
        game.run(FRAMES)
        status = game.status()
        check(status["vsync"] == vsync + FRAMES and status["presents"] >= presents + FRAMES,
              f"{FRAMES} vsyncs run and are presented after the reloads")
        game.call("snapshot", action="save", path=str(work / "reloaded-end.snap"))
        reloaded_end, _ = state(game)

        # 6. Reloads while running, and what they cost.
        game.call("pause", paused=False)
        rss = [rss_mb(process.pid)]
        for _ in range(RUNNING_RELOADS):
            reply = game.call("reload")
            rss.append(rss_mb(process.pid))
        game.pause()
        growth = (rss[-1] - rss[0]) / RUNNING_RELOADS
        check(reply.get("reloaded") and game.status()["vsync"] > vsync + FRAMES,
              f"{RUNNING_RELOADS} reloads while the game runs (last {reply.get('ms')} ms)")
        check(growth < LEAK_LIMIT, f"resident memory {rss[0]:.0f} -> {rss[-1]:.0f} MB: {growth:.1f} MB per reload")
    finally:
        game.close()
    text = (work / "game.log").read_text(errors="replace")
    check("[crash]" not in text, "no crash in the log")

    # The same moment with no reload, the same vsyncs.
    game = Game.launch(fast=True, socket_path=socket, log=work / "reference.log",
                       extra_env=dict(env, ROOD_ENGINE_LIBRARY=str(original)), snapshot=work / "start.snap")
    try:
        game.call("settings", apply=settings)
        reference, _ = state(game)
        check(reference == before, "the reference resumes the same state")
        game.run(FRAMES)
        game.call("snapshot", action="save", path=str(work / "reference-end.snap"))
        reference_end, _ = state(game)
    finally:
        game.close()
    # The native statics hold the last vsync's wall-clock time: compared
    # without it, as snapshot_equivalence does, the game stack included.
    differs = [k for k in STATE_KEYS if k != "state" and reloaded_end[k] != reference_end[k]]
    differs += snapshot_diff.diff(snapshot_diff.read(work / "reloaded-end.snap"),
                                  snapshot_diff.read(work / "reference-end.snap"),
                                  snapshot_diff.Symbols(DEV / "rood_host"), stack=True)
    check(not differs, f"{FRAMES} vsyncs on, the reloaded game and one never reloaded are in the same state "
          "(guest memory, VRAM, native and overlay statics, SPU, the game's stack)")
    for line in differs[:20]:
        print("    " + str(line))
    menu_while_open(work, socket, env)
    if not failures:
        for path in work.glob("*.so"):
            path.unlink()
    print("PASS" if not failures else f"FAIL ({len(failures)})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
