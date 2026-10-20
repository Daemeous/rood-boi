#!/usr/bin/env python3
"""Rebuild a private game/battle/root.zig, reload it in one host, and witness its write.

64-bit Linux; ROOD_BUILD_LOCK serialises builds. ROOD_ZIG_SMOKE_NO_BUILD=1
uses an already built dev64 host. Only the private game copy is ever edited.
The deliberate wrong write runs in shadow, so the original game's state wins.
"""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import rood_rpc
import snapshot_diff
from checkpoints import launch_at, _base_env
from snapshot_diff import Symbols

DEV = ROOT / "pc/build/dev64"
STATE_KEYS = ("ram", "scratch", "io", "state", "overlays", "vram", "vsync")
FRAMES = 600


def check(ok, message):
    if not ok:
        raise AssertionError(message)
    print("ok:   " + message, flush=True)


def build(command):
    if os.environ.get("ROOD_BUILD_LOCK"):
        command = ["flock", os.environ["ROOD_BUILD_LOCK"], *command]
    start = time.monotonic()
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=300)
    if result.returncode:
        raise RuntimeError(result.stdout[-6000:] + result.stderr[-6000:])
    return time.monotonic() - start


def state(game):
    hashes = game.call("snapshot", action="hash")
    return {key: hashes[key] for key in STATE_KEYS}


def reload_paused(game, process, pid):
    before = state(game)
    check(game.status()["paused"], "paused before Zig reload")
    start = time.monotonic()
    reply = game.call("reload", target="zig")
    seconds = time.monotonic() - start
    check(reply.get("target") == "zig" and reply["generation"] > 1, f"Zig library reloaded: {reply}")
    check(process.pid == pid and process.poll() is None, f"same live host PID {pid}")
    check(state(game) == before and game.status()["paused"],
          "all snapshot hashes, vsync and pause unchanged across reload")
    return seconds, reply


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD", ROOT / "pc/build/test-card1.mcd"))
    args = parser.parse_args()
    if os.environ.get("ROOD_ARCH") != "64" or not sys.platform.startswith("linux"):
        print("SKIP: Zig library smoke requires Linux and ROOD_ARCH=64")
        return 77
    card = pathlib.Path(args.card)
    if not card.exists():
        print("SKIP: no pinned test card", card)
        return 77
    if os.environ.get("ROOD_ZIG_SMOKE_NO_BUILD") != "1":
        build([sys.executable, str(ROOT / "pc/tools/build_boot.py"), "--arch", "64", "--engine-library"])
    out = pathlib.Path(os.environ.get("ROOD_OUT") or rood_rpc.OUT)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="zig-hotswap-", dir=out) as directory:
        work = pathlib.Path(directory)
        source = work / "game"
        shutil.copytree(ROOT / "game", source)
        library = work / "librood_game.so"
        command = [sys.executable, str(ROOT / "pc/tools/game_lib.py"),
                   "--source-root", str(source), "--out", str(library)]
        build(command)
        private_card = work / "card.mcd"
        shutil.copyfile(card, private_card)
        settings = work / "settings.txt"
        settings.write_text("ui_menus 0\nwidescreen 0\ninterpolate 0\nqol_skip_charging 0\ncamera_stick 0\n")
        env = dict(_base_env(work), ROOD_ORACLE="1", ROOD_OWNER="battle_action=original",
                   ROOD_SETTINGS=str(settings), ROOD_GI="0", ROOD_GAME_LIBRARY=str(library))
        rood_rpc.BINARY = DEV / "rood_host"
        os.environ["ROOD_SYMBOLS"] = str(rood_rpc.BINARY)
        # Existing checkpoint files belong to the ordinary binary, not this host.
        os.environ["ROOD_TEST_SNAPSHOTS"] = "0"
        game = launch_at("save_room", card=str(private_card), fast=True,
                         socket_path=f"/tmp/rood-zig-{os.getpid()}.sock",
                         log=work / "game.log", extra_env=env)
        process = game.processes[0]
        pid = process.pid
        timings = []
        try:
            game.call("room_goto", zone=48, room=10, enemies=True, scripts=False)
            for _ in range(80):
                game.run(20)
                if game.call("room_goto", action="status")["phase"] in (0, 4):
                    break
            check((game.state()["zone"], game.state()["room"]) == (48, 10), "enemy-cast room loaded")
            game.shadow("battle_action", True)
            check(game.owner()["subsystems"][0]["owner"] == "shadow", "RPC subsystem shadow on")
            game.call("lua", action="console", input="shadow battle_action off")
            check(game.owner()["subsystems"][0]["owner"] == "original", "console subsystem shadow off")
            game.call("lua", action="console", input="owner battle_action shadow")
            socket = f"/tmp/rood-zig-{os.getpid()}.sock"
            cli = subprocess.run([sys.executable, str(ROOT / "pc/tools/rood_rpc.py"),
                                  "--socket", socket, "reload", "zig"],
                                 capture_output=True, text=True, timeout=15, check=True)
            check(json.loads(cli.stdout)["target"] == "zig", "Python CLI reload zig")
            loop = subprocess.run(["bash", str(ROOT / "pc/tools/dev_loop.sh"), "--zig", "--no-build"],
                                  env=dict(os.environ, ROOD_RPC=socket), capture_output=True,
                                  text=True, timeout=15, check=True)
            check("generation" in loop.stdout, "dev_loop Zig reload wiring")
            before = state(game)
            game.call("lua", action="console", input="reload zig")
            check(state(game) == before, "console Zig reload does not advance paused state")
            game.mark()
            # Compare equivalent resume paths: rewind re-enters the marked
            # vsync via pc_tick_resumed, not midway through the original poll.
            game.rewind()
            game.call("owner", action="report", reset=True)
            game.run(FRAMES)
            baseline = game.call("owner", action="report")
            baseline_path = work / "baseline.snap"
            game.call("snapshot", action="save", path=str(baseline_path))
            baseline_end = snapshot_diff.read(baseline_path)
            state_symbols = snapshot_diff.Symbols(DEV / "rood_host")
            check(baseline["calls"] > 0 and baseline["count"] == baseline["errors"] == 0,
                  f"clean library exercises battle shadow without mismatches: {baseline}")
            function = max(baseline["coverage"], key=lambda row: row["calls"])["function"]
            address = next(address for address, size, name in Symbols(DEV / "rood_host").rows
                           if name == "vs_main_stateFlags")
            path = source / "battle/root.zig"
            clean = path.read_text()
            pattern = rf'(        orig\("{re.escape(function)}",[^\n]+;\n)'
            edited, count = re.subn(pattern, r'\1        vs_main_stateFlags[0] ^= 1;\n', clean)
            check(count == 1, f"located owned implementation of {function} in private source")
            edited = edited.replace('const common = @import("common");',
                                    'const common = @import("common");\nextern var vs_main_stateFlags: [1]u8;', 1)
            # Twice proves restoration and subsequent reloads are not a one-shot path.
            for cycle in range(2):
                game.rewind()
                game.call("owner", action="report", reset=True)
                start = time.monotonic()
                path.write_text(edited)
                edit_seconds = time.monotonic() - start
                build_seconds = build(command)
                reload_seconds, reply = reload_paused(game, process, pid)
                execution_start = time.monotonic()
                first = None
                before_frame = game.status()["vsync"]
                for frames in range(1, FRAMES + 1):
                    game.run(1)
                    report = game.call("owner", action="report")
                    if report["errors"]:
                        raise AssertionError(f"shadow execution failed: {report}")
                    if report["count"]:
                        first = report["mismatches"][0]
                        break
                check(first is not None, "edited Zig implementation executed within the witnessed recipe")
                check(first["function"] == function and first["first"] == address
                      and before_frame <= first["frame"] <= game.status()["vsync"],
                      f"wrong source write caught: {function} vs_main_stateFlags[0] at frame {first['frame']}: {first}")
                timings.append(dict(cycle=cycle + 1, pid=pid, function=function, field="vs_main_stateFlags[0]",
                                    frame=first["frame"], frames_until_execution=frames,
                                    edit_s=edit_seconds, build_s=build_seconds, reload_s=reload_seconds,
                                    first_execution_s=time.monotonic() - execution_start,
                                    edit_to_execution_s=time.monotonic() - start, reload=reply))
                path.write_text(clean)
                build(command)
                reload_paused(game, process, pid)
                game.rewind()
                game.call("owner", action="report", reset=True)
                before = game.status()
                game.run(FRAMES)
                restored = game.call("owner", action="report")
                after = game.status()
                restored_path = work / "restored.snap"
                game.call("snapshot", action="save", path=str(restored_path))
                restored_state = snapshot_diff.read(restored_path)
                # The established equivalence comparator excludes only the
                # native last_vsync wall clock and the saved stack's host frames.
                if os.environ.get("ROOD_RENDERER") == "null":
                    differences = snapshot_diff.diff(baseline_end, restored_state, state_symbols)
                    check(not differences and restored_state["vsync"] == baseline_end["vsync"],
                          f"restored deterministic frames equal the unmodified run: {differences}")
                else:
                    # GPU presentation history is outside the rewind image.
                    # Run the byte-exact replay oracle on the registered null
                    # lane; GPU lanes still require full preservation at reload,
                    # clean per-call shadow, actual execution and presentation.
                    check(restored_state["vsync"] == baseline_end["vsync"], "restored GPU run reaches the same frame")
                check(restored["calls"] > 0 and restored["count"] == restored["errors"] == 0,
                      "restored clean library runs the same recipe without wrong writes")
                check(after["vsync"] == before["vsync"] + FRAMES
                      and (os.environ.get("ROOD_RENDERER") == "null" or
                           after["presents"] >= before["presents"] + FRAMES),
                      f"game continues for {FRAMES} frames after reload (presents {before['presents']} -> {after['presents']})")
            # State cannot silently migrate into a reloadable module. A failed
            # build must leave the previously published library byte-for-byte.
            published = library.read_bytes()
            mutable, count = re.subn(pattern, '        reload_counter +%= 1;\\n' + r'\1', clean)
            check(count == 1, "mutable-state probe inserted in private source")
            mutable = mutable.replace('const common = @import("common");',
                                      'const common = @import("common");\nvar reload_counter: u32 = 0;', 1)
            path.write_text(mutable)
            try:
                build(command)
                raise AssertionError("library-owned mutable state was accepted")
            except RuntimeError as error:
                check("no mutable statics" in str(error), "mutable library state refused")
            check(library.read_bytes() == published, "failed build preserves published library")
            path.write_text(clean)
            # A broken publication must not unload the current implementation.
            _, before_reply = reload_paused(game, process, pid)
            before = state(game)
            bad = library.with_suffix(".bad")
            bad.write_bytes(b"not an ELF library")
            os.replace(bad, library)
            try:
                game.reload("zig")
                raise AssertionError("broken library was accepted")
            except rood_rpc.RpcError as error:
                check("load" in str(error), f"broken library refused: {error}")
            check(state(game) == before and process.poll() is None, "failed load preserves live game and state")
            route_path = source / "common/route.zig"
            clean_route = route_path.read_text()
            export = '@export(&f, .{ .name = name ++ "__candidate" });'
            check(clean_route.count(export) == 1, "found private candidate export generator")
            route_path.write_text(clean_route.replace(export,
                f'if (!std.mem.eql(u8, name, "{function}")) ' + export))
            build(command)
            route_path.write_text(clean_route)
            try:
                game.reload("zig")
                raise AssertionError("missing route export was accepted")
            except rood_rpc.RpcError as error:
                check("missing" in str(error), f"missing export refused: {error}")
            check(state(game) == before, "incomplete candidate did not publish any slots")
            signature = f'pub fn {function}() callconv(.c) void {{'
            check(clean.count(signature) == 1, "selected smoke function has a no-argument ABI")
            path.write_text(clean.replace(signature,
                f'pub fn {function}(extra: c_int) callconv(.c) void {{ _ = extra;').replace(
                    f'@TypeOf({function})', 'fn () callconv(.c) void'))
            build(command)
            try:
                game.reload("zig")
                raise AssertionError("incompatible function signature was accepted")
            except rood_rpc.RpcError as error:
                check("incompatible signature" in str(error), f"ABI change refused: {error}")
            check(state(game) == before, "ABI failure preserved game state")
            path.write_text(clean)
            build(command)
            _, recovered = reload_paused(game, process, pid)
            check(recovered["generation"] == before_reply["generation"] + 1, "failures did not advance generation")

            # Put an owned frame on the real game stack across a nested vsync.
            # The reload must refuse without stepping the paused game.
            live, count = re.subn(pattern, '        _ = VSync(0);\\n' + r'\1', clean)
            check(count == 1, "live-frame probe inserted in private owned implementation")
            live = live.replace('const common = @import("common");',
                                'const common = @import("common");\nextern fn VSync(c_int) callconv(.c) c_int;', 1)
            path.write_text(live)
            build(command)
            reload_paused(game, process, pid)
            game.rewind()
            game.owner("battle_action", "owned")
            refused = False
            for _ in range(FRAMES):
                game.run(1)
                before = state(game)
                try:
                    game.reload("zig")
                except rood_rpc.RpcError as error:
                    check("owned frame live" in str(error), f"live-frame reload refused: {error}")
                    check(state(game) == before, "refused reload does not advance or change state")
                    for action in ("mark", "save"):
                        try:
                            game.call("snapshot", action=action, path=str(work / "unsafe.snap"))
                            raise AssertionError(f"{action} retained an unloadable owned frame")
                        except rood_rpc.RpcError as error:
                            check("owned frame live" in str(error), f"unsafe {action} refused")
                    refused = True
                    break
            check(refused, "live owned frame observed and refused at nested vsync")
            game.owner("battle_action", "original")
            game.run(1)  # return out of the suspended owned frame
            path.write_text(clean)
            build(command)
            reload_paused(game, process, pid)
            game.owner("battle_action", "owned")
            game.call("pause", paused=False)
            running_frame = game.status()["vsync"]
            reply = game.call("reload", target="zig")
            deadline = time.monotonic() + 10
            while game.status()["vsync"] <= running_frame and time.monotonic() < deadline:
                time.sleep(0.01)
            check(reply.get("target") == "zig" and not game.status()["paused"]
                  and game.status()["vsync"] > running_frame and process.poll() is None,
                  "owned game continues running through an unpaused Zig reload in the same PID")
            game.pause()
        finally:
            game.close()
            shutil.copyfile(work / "game.log", out / "zig-hotswap.log")
            (out / "zig-hotswap-timings.json").write_text(json.dumps(timings, indent=2) + "\n")
        check("[crash]" not in (out / "zig-hotswap.log").read_text(errors="replace"), "no crash in host log")
        print(json.dumps(timings, indent=2), flush=True)
    print("PASS Zig hot swap")
    return 0


if __name__ == "__main__":
    sys.exit(main())
