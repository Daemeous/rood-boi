#!/usr/bin/env python3
"""Run the port's test suites, several at once, and print one table.

    python3 pc/tests/run.py                 everything (-j by CPU count)
    python3 pc/tests/run.py -j 2 rpc input  suites whose names contain "rpc" or "input"
    python3 pc/tests/run.py --list          the suites, without running them
    python3 pc/tests/run.py --headless xvfb software GL on Xvfb instead of EGL
    python3 pc/tests/run.py --arch 64       the LP64 build (build_boot.py --arch 64)
    python3 pc/tests/run.py --arch 64 --renderer vulkan   the Vulkan renderer (64-bit only)

Suites: the CTest probes (pc/build), the RPC and checkpoint smokes, the X11
window smoke, every input case in start_button_smoke.sh, golden frames and
short soaks (from New Game, and from a copy of pc/save/card1.mcd when present). Each runs in its own process group with a private output
directory pc/build/tests/<suite>/ (ROOD_OUT: game log, frames, scratch memory
cards, its output.log), RPC socket, X display (rood_rpc.start_xvfb) and an
empty lights and prefab directories (ROOD_LIGHTS, ROOD_PREFABS), so parallel runs cannot collide and the
user's settings, lights and saves never leak in.

Headless rendering (rood_rpc.headless_mode): egl renders on a GPU without an X
server (SDL offscreen + EGL device), xvfb on a private Xvfb with software GL.
X11-input suites (window_smoke) always use Xvfb.

The game binaries are not rebuilt here (pc/tools/build_boot.py --battle, and
without --battle for rood_boot); a warning names stale ones. Exit status 0
when every selected suite passed.
"""
import argparse
import fcntl
import os
import pathlib
import re
import shlex
import shutil
import signal
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import rood_rpc  # noqa: E402

TESTS = ROOT / "pc/tests"
OUT = ROOT / "pc/build/tests"  # --out
PY = sys.executable


class Suite:
    def __init__(self, name, command, timeout, binary: "str | None" = "rood_battle_probe", note=""):
        self.name, self.command, self.timeout, self.binary, self.note = name, command, timeout, binary, note

    def scaled(self, factor):
        self.timeout *= factor
        return self


def input_cases(scale):
    """The `run "name" args...` cases of start_button_smoke.sh as suites."""
    text = (TESTS / "start_button_smoke.sh").read_text().replace("\\\n", " ")
    cases = []
    for line in text.splitlines():
        words = shlex.split(line, comments=True)
        if len(words) > 2 and words[0] == "run":
            slug = re.sub(r"[^a-z0-9]+", "-", words[1].lower()).strip("-")
            realtime = "--realtime" in words
            timeout = (600 if realtime else 420) * scale
            cases.append(Suite(f"input:{slug}", [PY, str(TESTS / "input_smoke.py"), *words[2:],
                                                 "--deadline", str(timeout - 30)], timeout, note=words[1]))
    return cases


def suites(args, mode="egl"):
    # Software GL on Xvfb is several times slower than a GPU.
    scale = args.timeout_scale * (1 if mode == "egl" else 3)
    return [suite.scaled(scale) if not suite.name.startswith("input:") else suite for suite in [
        Suite("ctest", ["ctest", "--test-dir", str(ROOT / "pc/build"), "--output-on-failure"], 300, binary=None,
              note="CMake probes in pc/build"),
        Suite("lighting_lint", [PY, str(ROOT / "pc/tools/lighting_lint.py")], 60, binary=None,
              note="lighting packs in lighting/: syntax, keys, ids, dangling overrides, rooms"),
        Suite("rpc_smoke", [PY, str(TESTS / "rpc_smoke.py")], 180),
        Suite("gi_smoke", [PY, str(TESTS / "gi_smoke.py")], 300),
        Suite("rt_camera_smoke", [PY, str(TESTS / "rt_camera_smoke.py")], 300,
              note="ray-traced surfaces follow the picture's camera (Vulkan)"),
        Suite("nrd_smoke", [PY, str(TESTS / "nrd_smoke.py")], 300,
              note="NVIDIA NRD denoises the indirect light (Vulkan built with NRD)"),
        Suite("probe_gi_smoke", [PY, str(TESTS / "probe_gi_smoke.py")], 600,
              note="probe GI: converges, energy vs path traced, stable, no wall leak, light/room changes"),
        Suite("interpolation_smoke", [PY, str(TESTS / "interpolation_smoke.py")], 180,
              note="interpolated frames move the camera (opening pan)"),
        Suite("animation_smoke", [PY, str(TESTS / "animation_smoke.py")]
              + (["--card", str(args.save)] if args.save.exists() else []), 300,
              note="character in-betweens: held poses, fast rotations, weapons in hand"),
        Suite("movie_smoke", [PY, str(TESTS / "movie_smoke.py")], 300),
        Suite("window_smoke", [PY, str(TESTS / "window_smoke.py")], 180, binary="rood_boot",
              note="X11 events on Xvfb"),
        Suite("room_smoke", [PY, str(TESTS / "room_smoke.py")], 120),
        Suite("opening_smoke", [PY, str(TESTS / "opening_smoke.py")], 120),
        Suite("scene_smoke", [PY, str(TESTS / "scene_smoke.py")], 300),
        *([Suite("editor_cutaway_smoke", [PY, str(TESTS / "editor_cutaway_smoke.py"), "--card", str(args.save)], 300,
                 note="free camera sees into the save room; game camera unchanged"),
            Suite("editor_undo_smoke", [PY, str(TESTS / "editor_undo_smoke.py"), "--card", str(args.save)], 300,
                  note="undo/redo transactions (actors, glows, groups), Delete, no shortcuts while typing"),
            Suite("facing_cutaway_smoke", [PY, str(TESTS / "facing_cutaway_smoke.py"), "--card", str(args.save)], 300,
                  note="game camera cutaway walls: native = packet path at 8 yaws, during turns (save room, room 13)"),
            Suite("camera_stick_smoke", [PY, str(TESTS / "camera_stick_smoke.py"), "--card", str(args.save)], 600,
                  note="right-stick camera: continuous yaw, facing masks, L1/R1, zoom limits, walls, R3, "
                       "first-person/targeting, RT camera (Vulkan), opening's scripted camera unchanged"),
            Suite("editor_snap_smoke", [PY, str(TESTS / "editor_snap_smoke.py"), "--card", str(args.save)], 300,
                  note="editor overlays (edges, points) and point/edge/surface snapping in the save room"),
            Suite("room_browser_smoke", [PY, str(TESTS / "room_browser_smoke.py"), "--card", str(args.save)], 600,
                  note="Room Browser: rooms from the disc, warps across zones and back; lighting layers in the game"),
            Suite("route_continuation_smoke", [PY, str(TESTS / "route_continuation_smoke.py"), "--card",
                                               str(args.save)], 300,
                  note="enemies holding a room link (gate) continue their route (L800DF8A4) in zone 9 room 5"),
            Suite("ending_smoke", [PY, str(TESTS / "ending_smoke.py"), "--card", str(args.save)], 600,
                  note="ENDING.PRG twice from the save: credits, skip, MISSION COMPLETE rank screen, title"),
            Suite("light_actors_smoke", [PY, str(TESTS / "light_actors_smoke.py"), "--card", str(args.save)], 300,
                  note="Place Actors drag and drop, light actor prefabs, overrides, flicker, glow links (save room)"),
            Suite("fixture_lighting_smoke", [PY, str(TESTS / "fixture_lighting_smoke.py"), "--card", str(args.save)],
                  600, note="generated fixture lights: types, lit, shadow exclusion, glow links, suppressed glows, "
                            "live type edits, catalog save, regenerate (zone 9)"),
            Suite("sky_smoke", [PY, str(TESTS / "sky_smoke.py"), "--card", str(args.save)], 600,
                  note="sun and sky: roofed room unchanged, outdoor room's generated sun shadows and sky fill, "
                       "energy, NRD, probes, shadow-map fallback, save/load, generated tags stable"),
            Suite("atmosphere_smoke", [PY, str(TESTS / "atmosphere_smoke.py"), "--card", str(args.save)], 600,
                  note="haze, ground fog, fog volumes: fog follows height, volumes keep to their bounds, save/load, "
                       "generated forest fog, the moon's shafts in the haze, steady frame to frame"),
            Suite("atmosphere_sweep", [PY, str(ROOT / "pc/tools/atmosphere_sweep.py"), "--quick", "--card",
                                       str(args.save)], 600,
                  note="atmosphere never washes out the view: town, forest, Undercity rooms at several camera "
                       "angles and toward the moon (full sweep: pc/tools/atmosphere_sweep.py)"),
            Suite("fog_sim_smoke", [PY, str(TESTS / "fog_sim_smoke.py"), "--card", str(args.save)], 300,
                  note="interactive fog prototype (sim=1, off by default): Ashley clears her footprint, a wake "
                       "when walking, settles back"),
            Suite("glow_stability_smoke", [PY, str(TESTS / "glow_stability_smoke.py"), "--card", str(args.save)], 600,
                  note="save point: one steady glow, stable id, overrides persist; prologue fight: effect glows unshadowed, smooth")]
          if args.save.exists() else []),
        *([Suite("lighting_presets_smoke", [PY, str(TESTS / "lighting_presets_smoke.py"), "--card", str(args.save)],
                 600, note="lighting quality presets apply, render and persist; the backend's reduced form too"),
            Suite("gi_lag_smoke", [PY, str(TESTS / "gi_lag_smoke.py"), "--card", str(args.save)], 300,
                  note="a path tracer slower than the frame (ROOD_GI_SLOW): no black frame, presents go on (GL)")]
          if args.save.exists() else []),
        *([Suite("texture_animation_smoke", [PY, str(TESTS / "texture_animation_smoke.py"), "--card", str(args.save)],
                 240, note="room texture animations (the save point) play on the native room")]
          if args.save.exists() else []),
        *([Suite("widescreen_smoke", [PY, str(TESTS / "widescreen_smoke.py"), "--card", str(args.save)], 360,
                 note="16:9: fades and pictures cover the view, menu rows reach the edges, title/VKP still/map "
                      "laid out, SetDispMask black; HUD unchanged")]
          if args.save.exists() else []),
        Suite("widescreen_scene_smoke", [PY, str(TESTS / "widescreen_scene_smoke.py")]
              + (["--card", str(args.save)] if args.save.exists() else []), 420,
              note="16:9: the sky panorama and weather motes fill the view's sides (opening's manor; forest)"),
        *([Suite("save_menu_smoke", [PY, str(TESTS / "save_menu_smoke.py"), "--card", str(args.save)], 300,
                 note="DATA > LOAD slot images = disc data decode, on interpolated frames too (CLUT swaps)")]
          if args.save.exists() else []),
        *([Suite("weapon_slots_smoke", [PY, str(TESTS / "weapon_slots_smoke.py"), "--card", str(args.save)], 600,
                 note="QoL weapon slots: off = the game's L2 overlay; swaps, assign mode, shields, D-pad, drawn; "
                      "the mod's save bytes survive a save and reload")]
          if args.save.exists() else []),
        *([Suite("ui_menus_smoke", [PY, str(TESTS / "ui_menus_smoke.py"), "--card", str(args.save)], 300,
                 note="modern menus: off = the game's menu; shell tabs, original Status and back, "
                      "System > Menus from the next open, L2 shortcut stays original, greyed Break arts opens, close")]
          if args.save.exists() else []),
        *([Suite("ui_equip_smoke", [PY, str(TESTS / "ui_equip_smoke.py"), "--card", str(args.save)], 900,
                 note="Equipment screen: same RAM as the original equip page (MENU3) for weapon/shield/armor/"
                      "accessory changes, snapshot, quick swap, mouse, hold on close, bare hands, drawn")]
          if args.save.exists() else []),
        *([Suite("ui_ashley_smoke", [PY, str(TESTS / "ui_ashley_smoke.py"), "--card", str(args.save)], 300,
                 note="Ashley tab: summary = RAM (stats, limbs, effects), live model shown/turned (stick, drag, "
                      "no auto-turn), tabs/Status/weapon change, world and camera restored, 16:9, soft reset")]
          if args.save.exists() else []),
        *([Suite("ui_abilities_smoke", [PY, str(TESTS / "ui_abilities_smoke.py"), "--card", str(args.save)], 300,
                 note="Abilities tab: Chain/Defense mapping = the original MENU2 screen's bytes (set, move, replace, "
                      "off; list, pick, mouse), learned lists, break arts states and greyed reasons, cast and cancel")]
          if args.save.exists() else []),
        *([Suite("ui_system_smoke", [PY, str(TESTS / "ui_system_smoke.py"), "--card", str(args.save)], 300,
                 note="System screen through the shell: a row per settings page live + saved (debounce, Cross), "
                      "greyed rows, slider drag, binding capture (blocks navigation, Esc, pad, reset), Data, relaunch")]
          if args.save.exists() else []),
        Suite("system_settings_smoke", [PY, str(TESTS / "system_settings_smoke.py"),
                                        *(["--card", str(args.save)] if args.save.exists() else [])], 360,
              note="System settings: brightness, master volume (WAV), window mode, binding capture (SDL events; "
                   "key works in the field), saved and back after a relaunch with widescreen / perspective kept"),
        Suite("flipped_sprite_smoke", [PY, str(TESTS / "flipped_sprite_smoke.py")], 180,
              note="textured quads (flipped sprites too) sample only their own texels: CPU, hardware 1x and 4x"),
        Suite("ui_lines_smoke", [PY, str(TESTS / "ui_lines_smoke.py")], 420,
              note="2D lines: 1x = CPU, 4x scale-thick, joins closed, no light dots along outlines; "
                   "vector talk bubbles follow the game's zoom, all shapes; options saved"),
        Suite("ui_upscale_smoke", [PY, str(TESTS / "ui_upscale_smoke.py"),
                                   *(["--card", str(args.save)] if args.save.exists() else [])], 300,
              note="2D/UI MMPX: only 2D changes, atlas = CPU reference, palette kept, cache, VRAM rewrites"),
        Suite("ui_text_smoke", [PY, str(TESTS / "ui_text_smoke.py"),
                                *(["--card", str(args.save)] if args.save.exists() else [])], 300,
              note="ui_text fonts: off = original path; bubble, HUD, menu change only inside glyph boxes, "
                   "text extents kept, settings"),
        *([Suite("button_prompts_smoke", [PY, str(TESTS / "button_prompts_smoke.py"), "--card", str(args.save)], 400,
                 note="button prompts: original = PS1 glyphs; xbox/playstation/nintendo/keyboard icons only inside "
                      "the glyph boxes; auto follows the device; rebinding changes the icon; 1x; settings")]
          if args.save.exists() else []),
        *input_cases(scale),
        Suite("aa_smoke", [PY, str(TESTS / "aa_smoke.py")], 300,
              note="anti-aliasing: each method renders, smooths edges, keeps texels and 2D; interpolation"),
        Suite("coplanar_order_smoke", [PY, str(TESTS / "coplanar_order_smoke.py"),
                                       *(["--card", str(args.save)] if args.save.exists() else [])], 420,
              note="coplanar room surfaces (attract cutscene papers; decals in rooms) paint in PS1 order, stable"),
        Suite("golden_frames", [PY, str(TESTS / "golden_frames.py")], 600,
              note="references in pc/build/golden"),
        Suite("soak", [PY, str(TESTS / "soak.py"), "--frames", str(args.soak_frames), "--max-launches", "2"],
              900, note=f"{args.soak_frames} vsyncs of play from New Game"),
        *([Suite("soak:save", [PY, str(TESTS / "soak.py"), "--frames", str(args.soak_frames), "--max-launches", "2",
                               "--card", str(args.save)], 900,
                 note=f"{args.soak_frames} vsyncs from a copy of {os.path.relpath(args.save, ROOT)}")]
          if args.save.exists() else []),
    ]]


def default_save():
    """pc/save/card1.mcd here, else in the main checkout (from a worktree)."""
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def stale_binaries(selected):
    """Game binaries older than the newest port or game source."""
    newest = max(p.stat().st_mtime for d in ("pc/src", "src") for p in (ROOT / d).rglob("*")
                 if p.suffix in (".c", ".cpp", ".h", ".comp", ".glsl"))
    names = {s.binary for s in selected if s.binary}
    out = []
    for name in sorted(names):
        if os.environ.get("ROOD_ARCH") == "64":  # one LP64 binary serves every suite
            path = ROOT / "pc/build/boot64/rood_battle_probe"
        else:
            path = ROOT / "pc/build/boot" / name
        if not path.exists():
            out.append(f"{name} is missing")
        elif path.stat().st_mtime < newest:
            out.append(f"{name} is older than the sources")
    return out


def run_suite(suite, index, args, mode, results, lock):
    out = OUT / suite.name.replace(":", "-")
    if suite.name.startswith("soak"):
        suite.command += ["--out", str(out / "soak")]
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    log = out / "output.log"
    env = {k: v for k, v in os.environ.items()
           if not k.startswith("ROOD_") and k not in ("DISPLAY", "WAYLAND_DISPLAY", "SDL_VIDEO_DRIVER")}
    env.update(
        ROOD_OUT=str(out),
        ROOD_RPC=f"/tmp/rood-run-{os.getpid()}-{index}.sock",  # sun_path is 108 bytes
        ROOD_CARD1=str(out / "test-card1.mcd"),
        ROOD_CARD2=str(out / "test-card2-absent.mcd"),
        ROOD_LIGHTS=str(out / "lights"),
        ROOD_PREFABS=str(out / "prefabs"),
        ROOD_HEADLESS=mode,
        ROOD_BUTTON_PROMPTS="original",  # the PS1 glyphs (button_prompts_smoke opts in)
        ROOD_AUDIO=f"wav:{out / 'audio.wav'}" if suite.name.startswith("input:") else "none",
        PYTHONUNBUFFERED="1",
        # llvmpipe (Xvfb, or EGL without a GPU) otherwise takes every core per game.
        LP_NUM_THREADS=os.environ.get("LP_NUM_THREADS", "4"),
    )
    for key in ("ROOD_EGL_DEVICE", "ROOD_NATIVE_ROOM", "ROOD_NATIVE_ACTORS", "ROOD_NATIVE_BLEND",
                "ROOD_NATIVE_OBJECTS", "ROOD_NATIVE_SHADOWS", "ROOD_NATIVE_EFFECTS",
                "ROOD_RECONSTRUCT", "ROOD_ARCH", "ROOD_RENDERER", "ROOD_VK_DEVICE", "ROOD_VK_VALIDATE",
                "ROOD_VK_RT"):  # renderer choices to test under, binary
        if key in os.environ:
            env[key] = os.environ[key]
    start = time.monotonic()
    with log.open("w") as output:
        p = subprocess.Popen(suite.command, cwd=ROOT, env=env, stdout=output, stderr=subprocess.STDOUT,
                             start_new_session=True)
        try:
            status = p.wait(timeout=suite.timeout)
            result = {0: "PASS", 77: "SKIP"}.get(status, f"FAIL ({status})")
        except subprocess.TimeoutExpired:
            result = "TIMEOUT"
        finally:
            # The suite's game, Xvfb and helpers share its process group.
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            p.wait()
    elapsed = time.monotonic() - start
    pathlib.Path(env["ROOD_RPC"]).unlink(missing_ok=True)
    text = log.read_text(errors="replace").strip().splitlines()
    summary = next((line for line in reversed(text) if line.startswith(("PASS", "FAIL", "SKIP", "AssertionError",
                                                                          "RuntimeError", "Error"))),
                   text[-1] if text else "")
    with lock:
        results[suite.name] = (result, elapsed, log, summary)
        print(f"{result:<9} {elapsed:6.1f}s  {suite.name}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("patterns", nargs="*", help="run suites whose names contain any of these")
    parser.add_argument("-j", "--jobs", type=int, default=max(1, min(6, (os.cpu_count() or 4) // 4)),
                        help="suites at once (default: CPUs / 4, at most 6)")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--headless", choices=["egl", "xvfb"], help="default: $ROOD_HEADLESS, else egl if it works")
    parser.add_argument("--soak-frames", type=int, default=3000, help="vsyncs of soak play (default 3000)")
    parser.add_argument("--save", type=pathlib.Path, default=default_save(),
                        help="memory card for soak:save (copied, never written; skipped when absent)")
    parser.add_argument("--timeout-scale", type=float, default=1.0, help="multiply every suite's timeout")
    parser.add_argument("--exclude", action="append", default=[], help="skip suites containing this")
    parser.add_argument("--out", help="output directory (default pc/build/tests); one runner per directory")
    parser.add_argument("--arch", choices=["32", "64"], default=os.environ.get("ROOD_ARCH", "32"),
                        help="game binary: 32 (pc/build/boot, default) or 64 (pc/build/boot64; sets ROOD_ARCH)")
    parser.add_argument("--renderer", choices=["gl", "vulkan"],
                        default="vulkan" if os.environ.get("ROOD_RENDERER") == "vulkan" else "gl",
                        help="gl (default) or vulkan (64-bit; sets ROOD_RENDERER)")
    args = parser.parse_args()
    if args.renderer == "vulkan":
        os.environ["ROOD_RENDERER"] = "vulkan"
    else:
        os.environ.pop("ROOD_RENDERER", None)
    os.environ["ROOD_ARCH"] = args.arch  # suites and rood_rpc pick the binary from it
    rood_rpc.BUILD = ROOT / ("pc/build/boot64" if args.arch == "64" else "pc/build/boot")
    rood_rpc.BINARY = rood_rpc.BUILD / "rood_battle_probe"
    global OUT
    if args.out:
        OUT = pathlib.Path(args.out).resolve()

    if args.headless:
        os.environ["ROOD_HEADLESS"] = args.headless
    mode, renderer = rood_rpc.headless_mode()
    selected = [s for s in suites(args, mode)
                if (not args.patterns or any(p in s.name for p in args.patterns))
                and not any(x in s.name for x in args.exclude)]
    if args.list:
        for s in selected:
            print(f"{s.name:<45} {s.timeout:5.0f}s  {s.note}")
        return 0
    if not selected:
        print("no suites match", args.patterns)
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    lock_file = open(OUT / ".lock", "w")
    try:
        fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print(f"another run.py uses {OUT}; pass --out DIR to run concurrently")
        return 2
    print(f"headless: {mode}" + (f" ({renderer})" if renderer else " (software GL on Xvfb)"))
    for warning in stale_binaries(selected):
        print(f"warning: {warning}; rebuild with pc/tools/build_boot.py")
    if any(s.name == "ctest" for s in selected) and (ROOT / "pc/build/build.ninja").exists():
        built = subprocess.run(["ninja", "-C", str(ROOT / "pc/build")], capture_output=True, text=True)
        if built.returncode:
            print(built.stdout[-2000:])
    print(f"{len(selected)} suites, {args.jobs} at a time; logs in {OUT}/<suite>/output.log",
          flush=True)

    results, lock = {}, threading.Lock()
    start = time.monotonic()
    # Longest first so the slow suites do not start last.
    order = sorted(enumerate(selected), key=lambda item: -item[1].timeout)
    with ThreadPoolExecutor(args.jobs) as pool:
        for index, suite in order:
            pool.submit(run_suite, suite, index, args, mode, results, lock)
    total = time.monotonic() - start

    print()
    width = max(len(s.name) for s in selected)
    failed = 0
    for s in selected:
        result, elapsed, log, summary = results.get(s.name, ("NOT RUN", 0, None, ""))
        failed += result not in ("PASS", "SKIP")
        shown = os.path.relpath(log, ROOT) if log else ""
        print(f"{s.name:<{width}}  {result:<9} {elapsed:6.1f}s  {shown}")
        if result != "PASS" and summary:  # why it failed or was skipped
            print(f"{'':<{width}}  {summary[:160]}")
    skipped = sum(results.get(s.name, ("",))[0] == "SKIP" for s in selected)
    print(f"\n{len(selected) - failed - skipped}/{len(selected)} passed" + (f", {skipped} skipped" if skipped else "")
          + f" in {total:.0f}s ({mode}, {args.renderer}, -j {args.jobs})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
