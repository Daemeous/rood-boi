#!/usr/bin/env python3
"""Run the port's test suites, several at once, and print one table.

    python3 pc/tests/run.py                 everything (-j by CPU count)
    python3 pc/tests/run.py -j 2 rpc input  suites whose names contain "rpc" or "input"
    python3 pc/tests/run.py --list          the suites, without running them
    python3 pc/tests/run.py --headless xvfb software GL on Xvfb instead of EGL
    python3 pc/tests/run.py --renderer vulkan   the Vulkan renderer (64-bit only)
    python3 pc/tests/run.py --arch 32       the 32-bit build (pc/build/boot; its suite is no longer kept up)

Suites: the CTest probes (pc/build), the RPC and checkpoint smokes, the X11
window smoke, every input case in start_button_smoke.sh, golden frames and
short soaks (from New Game, and from the pinned test card pc/build/test-card1.mcd, a copy of pc/save/card1.mcd made once; $ROOD_TEST_CARD overrides). Each runs in its own process group with a private output
directory pc/build/tests/<suite>/ (ROOD_OUT: game log, frames, scratch memory
cards, its output.log), RPC socket, X display (rood_rpc.start_xvfb) and an
empty lights and prefab directories (ROOD_LIGHTS, ROOD_PREFABS), so parallel runs cannot collide and the
user's settings, lights and saves never leak in.

Headless rendering (rood_rpc.headless_mode): egl renders on a GPU without an X
server (SDL offscreen + EGL device), xvfb on a private Xvfb with software GL.
X11-input suites (window_smoke) always use Xvfb. window_mode_smoke switches
a real window (fullscreen on the desktop for a few seconds; the only way to
reach the Vulkan swapchain) only when asked:
    ROOD_TEST_DESKTOP=1 python3 pc/tests/run.py window_mode   (else headless)

Save states (pc/tests/checkpoints.py, pc/src/snapshot.c): suites that start
from a test checkpoint (the card's save room, New Game after the opening
pan, ...) resume a save state of it instead of booting. snapshot_equivalence
runs first: it makes them for this build in pc/build/test-snapshots/ and
proves each exact against booting (game state and frames); one that is not
stays unused and its suites boot. Suites about the boot path always boot.
    python3 pc/tests/run.py --no-snapshots  every suite boots (ROOD_TEST_SNAPSHOTS=0)
One directory of save states is kept per configuration (arch, renderer,
headless mode): a new game binary replaces it. `run.py --clean` removes the
test outputs (pc/tools/clean_build.py).

Suites start longest first by the last run's times (<out>/durations.json).
--profile adds <suite>/rpc-profile.txt and python.prof (profile_report.py).

The game binaries are not rebuilt here (pc/tools/build_boot.py --battle, and
without --battle for rood_boot); a warning names stale ones. Exit status 0
when every selected suite passed.
"""
import argparse
import fcntl
import hashlib
import json
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
import traceback

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import rood_rpc  # noqa: E402

TESTS = ROOT / "pc/tests"
OUT = ROOT / "pc/build/tests"  # --out
PY = sys.executable
IONICE = shutil.which("ionice")
# --profile: run a Python suite under cProfile, keeping its exit status
# (python -m cProfile swallows SystemExit).
PROFILED = """import cProfile, os, runpy, sys
out, sys.argv = sys.argv[1], sys.argv[2:]
sys.path.insert(0, os.path.dirname(os.path.abspath(sys.argv[0])))
profile, code = cProfile.Profile(), 0
try:
    profile.runcall(runpy.run_path, sys.argv[0], run_name="__main__")
except SystemExit as e:
    code = e.code
finally:
    profile.dump_stats(out)
sys.exit(code)
"""


class Suite:
    """gpu selects the GPU lane (--gpu-jobs), independently of binary dependencies.
    Explicit null game suites use the CPU lane (--cpu-jobs). sources: files the suite
    reads besides its command's scripts (--changed, affected.py)."""
    def __init__(self, name, command, timeout, binary: "str | None" = "rood_battle_probe", note="", sources=(), gpu=None):
        self.name, self.command, self.timeout, self.binary, self.note = name, command, timeout, binary, note
        self.gpu = binary is not None if gpu is None else gpu
        self.sources = list(sources)

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
                                                 "--deadline", str(timeout - 30)], timeout, note=words[1],
                                sources=[TESTS / "start_button_smoke.sh"]))
    return cases


# The Zig trial port's corpus runs: (suite label, subsystem, recipe, the Zig modules it covers).
def port_modules(subsystem):
    """The Zig modules a subsystem's functions route to (game/ownership.toml)."""
    import tomllib
    table = tomllib.loads((ROOT / "game/ownership.toml").read_text())["subsystem"][subsystem]
    return sorted({ROOT / "game" / table["overlay"] / (f["zig"].split(".")[0] + ".zig")
                   for f in table["functions"] if "zig" in f})


TRIAL_CORPUS = [("kill", "battle_146C", "enemy_kill", ["score.zig"]),
                ("transform", "battle_44F14", "enemy_cast_wait", ["actor_transform.zig"]),
                ("name_window", "battle_5BF94", "action_name_window", ["scene_message.zig"])]


def suites(args, mode="egl"):
    # Software GL on Xvfb is several times slower than a GPU.
    scale = args.timeout_scale * (3 if mode == "xvfb" else 1)
    return [suite.scaled(scale) if not suite.name.startswith("input:") else suite for suite in [
        # zig_diff (every spec's self-check, six shards of 1800 s each) grows with
        # each ported unit: the suite's budget follows it (181 specs after wave 1).
        Suite("ctest", ["ctest", "--test-dir", str(ROOT / "pc/build"), "--output-on-failure", "-j", "4"], 2400, binary=None,
              note="CMake probes in pc/build", sources=[ROOT / "pc/tools/ownership.py",
                  ROOT / "game/ownership.toml", TESTS / "ownership_test.py", TESTS / "routing_probe/run.py",
                  TESTS / "routing_probe/unit.c", TESTS / "routing_probe/owned.c", TESTS / "routing_probe/main.c",
                  TESTS / "owner_switch_probe.c", ROOT / "pc/src/ownership.c",
                  TESTS / "state_diff_test.c", ROOT / "pc/src/state_diff.c", ROOT / "pc/src/state_diff.h",
                  TESTS / "shadow_test.c", TESTS / "shadow_route_test.c",
                  ROOT / "game/common/route.zig", ROOT / "game/common/shadow_route_probe.zig",
                  ROOT / "pc/src/shadow.c", ROOT / "pc/src/shadow.h",
                  ROOT / "pc/tools/field_map.py", TESTS / "field_map_test.py", TESTS / "diff/masks.py",
                  TESTS / "diff/corpus_test.py", TESTS / "diff/corpus.py", TESTS / "diff/differ.py", TESTS / "diff/scenarios.py",
                  TESTS / "state_rewind_test.c", ROOT / "pc/src/state_rewind.c",
                  TESTS / "input_record_test.c", ROOT / "pc/src/input_record.c"]),
        *([Suite("null_backend_smoke", [PY, str(TESTS / "null_backend_smoke.py")], 300,
                 gpu=False, note="CPU null backend: no display or GPU resources")]
          if args.renderer == "null" else []),
        Suite("battle_corpus_candidate", [PY, str(TESTS / "diff/differ.py"), "battle_action",
              "--scenario", "enemy_cast_wait", "--card", str(args.save)], 300,
              note="one witnessed enemy-cast recipe; NOT full task20 coverage",
              sources=[TESTS / "diff/corpus.py", TESTS / "diff/scenarios.py", ROOT / "pc/tools/field_map.py"]),
        # The Zig trial port (pc/docs/porting/battle/TRIAL.md): each ported function's
        # subsystem in control, shadow, owned and alternating runs of a recipe that calls it,
        # in the classic profile and again with the state-changing PC features on.
        *[Suite(f"trial_corpus_{label}{suffix}", [PY, str(TESTS / "diff/differ.py"), subsystem,
                "--scenario", scenario, "--card", str(args.save), "--profile", profile], 300,
                note=f"trial port: {subsystem} on the {scenario} recipe, {profile} profile",
                sources=[TESTS / "diff/corpus.py", TESTS / "diff/scenarios.py", ROOT / "pc/tools/field_map.py",
                         TESTS / "diff/waivers.toml", *[ROOT / "game/battle" / module for module in modules]])
          for label, subsystem, scenario, modules in TRIAL_CORPUS
          for suffix, profile in (("", "classic"), ("_features", "features"))],
        Suite("shadow_battle_smoke", [PY, str(TESTS / "shadow_battle_smoke.py"), "--card", str(args.save)], 300,
              note="experimental routed battle shadow; classic oracle profile",
              sources=[ROOT / "pc/src/shadow.c", ROOT / "game/common/route.zig"]),
        # The 38C1C port shadowed inside battle_action: its routed bone helpers
        # (func_800A152C, func_800A1AF8) run between func_8007138C's caller and
        # its partly written effect request, whose dead-stack bytes diverged
        # until the request was zeroed (pc/docs/Oracle.md, known hazards).
        Suite("shadow_battle_smoke_38C1C", [PY, str(TESTS / "shadow_battle_smoke.py"), "--card", str(args.save),
              "--owner", "battle_38C1C_port=shadow"], 300,
              note="battle_action shadow with battle_38C1C_port shadowed (the effect-request oracle gap)",
              sources=[ROOT / "pc/src/shadow.c", ROOT / "game/common/route.zig", TESTS / "shadow_battle_smoke.py",
                       ROOT / "src/BATTLE/BATTLE.PRG/146C.c", ROOT / "src/BATTLE/BATTLE.PRG/38C1C.c",
                       *port_modules("battle_38C1C_port")]),
        Suite("ownership_smoke", [PY, str(TESTS / "ownership_smoke.py")], 300,
              note="oracle checkpoint, rewind and replay", sources=[ROOT / "pc/src/state_rewind.c",
                  ROOT / "pc/src/state_diff.c", ROOT / "pc/src/snapshot.c", ROOT / "pc/src/input_record.c"]),
        Suite("lighting_lint", [PY, str(ROOT / "pc/tools/lighting_lint.py")], 60, binary=None,
              note="lighting packs in lighting/: syntax, keys, ids, dangling overrides, rooms"),
        Suite("emissive_catalog", [PY, str(TESTS / "emissive_catalog_test.py")], 300, binary=None,
              note="curated emissive entries: masks, tints, light settings in range; with the disc, every room's "
                   "emissive glows within the 32 traced, window lights generated and at most 16 a room"),
        Suite("lp64_alias_scan", [PY, str(TESTS / "lp64_alias_scan_test.py")], 60, binary=None,
              note="no data import is a field of another across the LP64 native move "
                   "(pc/tools/lp64_alias_scan.py on pc/build/boot64; synthetic cases always)"),
        Suite("engine_saved_state_test", [PY, str(TESTS / "engine_saved_state_test.py")], 60, binary=None,
              sources=[ROOT / "pc/tools/engine_boundary.py", ROOT / "pc/tools/engine_lib.py",
                       ROOT / "pc/tools/snapshot_layout.py"],
              note="split engine refuses stranded saved sections before publishing, including private statics"),
        Suite("checkpoints_test", [PY, str(TESTS / "checkpoints_test.py")], 30, binary=None,
              sources=[TESTS / "checkpoints.py"],
              note="checkpoint launch failures close the mocked game and preserve the original exception"),
        Suite("build_cache", [PY, str(TESTS / "build_cache_test.py")], 120, binary=None,
              note="shared build caches: objects reused across checkouts and identical to a compile there, "
                   "edits/flags/compiler miss, LRU cap, third-party entries built once under the lock"),
        Suite("fast_travel_scan",[PY, str(TESTS / "fast_travel_scan_test.py")], 120, binary=None,
              note="fast travel R9: every room and event script walked op by op; every Circle records itself; "
                   "no script tests unkB5 or writes a Circle state other than 1 unless blocks.json mirrors it"),
        Suite("snapshot_equivalence", [PY, str(TESTS / "snapshot_equivalence.py")]
              + (["--card", str(args.save)] if args.save.exists() else []), 600,
              note="save states: booting to each test checkpoint and resuming its snapshot give identical "
                   "game state and frames; makes the checkpoints' save states (first job)"),
        Suite("snapshot_resume_smoke", [PY, str(TESTS / "snapshot_resume_smoke.py")], 300,
              note="64-bit: the engine relinked elsewhere resumes a snapshot through RPC restart at the same "
                   "moment and state (and as the original build does 60 vsyncs on); other game code refused"),
        Suite("engine_reload_smoke", [PY, str(TESTS / "engine_reload_smoke.py")], 600,
              note="64-bit: the engine library (build_boot.py --engine-library) reloads in place at a vsync: "
                   "state, pause and settings survive, a rebuilt unit takes effect, a broken library falls "
                   "back, and 120 vsyncs on the game equals one never reloaded"),
        *([Suite("photo_engine_reload_smoke", [PY, str(TESTS / "photo_foundation_smoke.py"), "--card", str(args.save),
                       "--engine-reload"], 400,
                 note="64-bit: the photo mode foundation on the split build, then an engine reload while a plugin "
                      "holds the pause, camera, hidden layers, post and input: all given back, the moment kept")]
          if args.save.exists() else []),
        Suite("engine_rewind_smoke", [PY, str(TESTS / "engine_rewind_smoke.py"), "--card", str(args.save)], 600,
              note="64-bit split host: no reload, one mark and two rewind/600-frame GPU replays are byte exact",
              sources=[ROOT / "pc/src/render_state.c", ROOT / "pc/src/render.c",
                       ROOT / "pc/tools/engine_boundary.py", ROOT / "pc/tools/build_boot.py"]),
        Suite("zig_hotswap_smoke", [PY, str(TESTS / "zig_hotswap_smoke.py"), "--card", str(args.save)], 900,
              note="64-bit Linux: edited private Zig battle source rebuilt/reloaded in one PID; paused state "
                   "unchanged, shadow names the wrong field/frame, clean restore and running reload",
              sources=[ROOT / "pc/tools/game_lib.py", ROOT / "game/battle/root.zig",
                       ROOT / "game/common/route.zig", ROOT / "pc/src/game_host.inc"]),
        Suite("rpc_smoke", [PY, str(TESTS / "rpc_smoke.py")], 180),
        Suite("rpc_tcp_token_smoke", [PY, str(TESTS / "rpc_tcp_token_smoke.py")], 180,
              note="TCP RPC with a token: refused without it, with a wrong one, HTTP lines; never logged"),
        Suite("lua_smoke", [PY, str(TESTS / "lua_smoke.py")], 300,
              note="plugins in one game: every Lua test (rood.test), runaway and failing scripts stopped "
                   "within their budgets, sandbox, capabilities, hot reload, settings from Lua"),
        Suite("gi_smoke", [PY, str(TESTS / "gi_smoke.py")], 300),
        Suite("rt_camera_smoke", [PY, str(TESTS / "rt_camera_smoke.py")], 300,
              note="ray-traced surfaces follow the picture's camera (Vulkan)"),
        Suite("nrd_smoke", [PY, str(TESTS / "nrd_smoke.py")], 300,
              note="NVIDIA NRD denoises the indirect light (Vulkan built with NRD)"),
        Suite("probe_gi_smoke", [PY, str(TESTS / "probe_gi_smoke.py")], 600,
              note="probe GI: converges, energy vs path traced, stable, no wall leak, light/room changes"),
        Suite("interpolation_smoke", [PY, str(TESTS / "interpolation_smoke.py")], 180,
              note="interpolated frames move the camera (opening pan)"),
        *([Suite("interp_menu_2d_smoke", [PY, str(TESTS / "interp_menu_2d_smoke.py"), "--card", str(args.save)],
                 600, note="16:9, interpolation on (present_rate 1 and 3): original Status, Equip affinity "
                           "and card list in-between images keep the game's 2D (no stray glyphs or lines, no "
                           "double-blended title; issues #8, #14); the title's card screen leaves no bars in the "
                           "view's sides")]
          if args.save.exists() else []),
        Suite("animation_smoke", [PY, str(TESTS / "animation_smoke.py")]
              + (["--card", str(args.save)] if args.save.exists() else []), 300,
              note="character in-betweens: held poses, fast rotations, weapons in hand"),
        Suite("movie_smoke", [PY, str(TESTS / "movie_smoke.py")], 300),
        Suite("skip_intro_smoke", [PY, str(TESTS / "skip_intro_smoke.py")], 300,
              note="skip_intro 1: straight to the title (no logos, no movie), same title; off = the original start"),
        Suite("verify_args_test", [PY, str(TESTS / "verify_args_test.py")], 30, binary=None,
              sources=[ROOT / "pc/tools/verify.sh"],
              note="verify.sh changed revisions stay separate from null suite patterns"),
        Suite("build_noop_test", [PY, str(TESTS / "build_noop_test.py")], 120, binary=None,
              note="build_boot.py: a second build with nothing changed does nothing (< 2 s); the build "
                   "stamp and object cache notice edits, reverts, new files and option changes"),
        Suite("replace_file_test", [PY, str(TESTS / "replace_file_test.py")], 300, binary=None,
              note="saves replace their file again and again (pc_replace_file), natively and as a "
                   "Windows build under Wine when installed"),
        Suite("window_smoke", [PY, str(TESTS / "window_smoke.py")], 180, binary="rood_boot",
              note="X11 events on Xvfb"),
        Suite("room_smoke", [PY, str(TESTS / "room_smoke.py")], 120),
        Suite("opening_smoke", [PY, str(TESTS / "opening_smoke.py")], 120),
        Suite("scene_smoke", [PY, str(TESTS / "scene_smoke.py")], 300),
        *([Suite("editor_cutaway_smoke", [PY, str(TESTS / "editor_cutaway_smoke.py"), "--card", str(args.save)], 300,
                 note="free camera sees into the save room; game camera unchanged"),
            Suite("editor_undo_smoke", [PY, str(TESTS / "editor_undo_smoke.py"), "--card", str(args.save)], 300,
                  note="undo/redo transactions (actors, glows, groups), Delete, no shortcuts while typing"),
            Suite("editor_selection_smoke", [PY, str(TESTS / "editor_selection_smoke.py"), "--card", str(args.save)],
                  420, note="editor selection: RPC, clicks (lights, actors, glows, fog volumes by ray), Shift/Ctrl, "
                            "box select, Outliner scroll, Details follows, hide/isolate, copy/paste/duplicate, "
                            "delete, F"),
            Suite("editor_schema_smoke", [PY, str(TESTS / "editor_schema_smoke.py"), "--card", str(args.save)], 300,
                  note="property schema: lint, RPC get/set/reset per kind (lights, actors, fog presets), Details drawn "
                       "from it (categories, search, lock, reset arrow), the value ladder (middle-drag, Esc)"),
            Suite("editor_transform_smoke", [PY, str(TESTS / "editor_transform_smoke.py"), "--card", str(args.save)],
                  300, note="W/E/R and World/Local; R scales a light's reach or size, an actor's lights and radii, a fog "
                            "volume's size, never brightness; the scale gizmo's centre with the mouse"),
            Suite("barn_doors_smoke", [PY, str(TESTS / "barn_doors_smoke.py"), "--card", str(args.save)], 300,
                  note="light shapes in the picture: a rect's barn doors cut its spill beside it, a spot's inner cone "
                       "sets its edge (OpenGL; Vulkan with --arch 64 --renderer vulkan)"),
            Suite("editor_handles_smoke", [PY, str(TESTS / "editor_handles_smoke.py"), "--card", str(args.save)], 600,
                  note="viewport handles (mouse drags): each changes one field, Ctrl fine, Shift snap, Esc, one undo "
                       "step, hidden while flying; point/spot/rect/sun handles; the colour popover and eyedropper"),
            Suite("emissive_entries_smoke", [PY, str(TESTS / "emissive_entries_smoke.py"), "--card", str(args.save)],
                  300, note="emissive entries: Duplicate and Delete (a confirm first) by RPC and the panel, one undo step "
                            "each, undo and redo; the texture view: wheel zoom about the pointer's texel, middle-drag "
                            "pan, Fit, hover readout, rect drags and brush dabs on the right texels at any zoom"),
            Suite("editor_library_smoke", [PY, str(TESTS / "editor_library_smoke.py"), "--card", str(args.save)], 600,
                  note="the library: sources, filters, favourites, thumbnails (deterministic, cached), drops on a floor "
                       "and a wall per kind (one undo step, selected), a real drag from a tile, the panel's views, "
                       "Save as prefab round trips (lights, an actor, a fog volume)"),
            Suite("facing_cutaway_smoke", [PY, str(TESTS / "facing_cutaway_smoke.py"), "--card", str(args.save)], 300,
                  note="game camera cutaway walls: native = packet path at 8 yaws, during turns (save room, room 13)"),
            Suite("camera_stick_smoke", [PY, str(TESTS / "camera_stick_smoke.py"), "--card", str(args.save)], 300,
                  note="right-stick camera: continuous yaw, facing masks, L1/R1, zoom limits, walls, R3, "
                       "first-person/targeting, RT camera (Vulkan), opening's scripted camera unchanged"),
            Suite("camera_options_smoke", [PY, str(TESTS / "camera_options_smoke.py"), "--card", str(args.save)],
                  300, note="map panning and first-person up/down inversions off and on; the right stick looks "
                            "around in first person; R3 leaves it"),
            Suite("editor_snap_smoke", [PY, str(TESTS / "editor_snap_smoke.py"), "--card", str(args.save)], 300,
                  note="editor overlays (edges, points) and point/edge/surface snapping in the save room"),
            Suite("editor_roomload_smoke", [PY, str(TESTS / "editor_roomload_smoke.py"), "--card", str(args.save)],
                  600, note="editor open through room loads (warps, a walked door) stepped every vsync: its room "
                            "groups are the loaded block's or none (never a freed one), no crash"),
            Suite("room_browser_smoke", [PY, str(TESTS / "room_browser_smoke.py"), "--card", str(args.save)], 300,
                  note="Room Browser: rooms from the disc, warps across zones and back; lighting layers in the game"),
            Suite("route_continuation_smoke", [PY, str(TESTS / "route_continuation_smoke.py"), "--card",
                                               str(args.save)], 300,
                  note="enemies holding a room link (gate) continue their route (L800DF8A4) in zone 9 room 5"),
            Suite("platform_shadow_smoke", [PY, str(TESTS / "platform_shadow_smoke.py"), "--card", str(args.save)],
                  300, note="a moving platform's square shadow (Rue Vermillion, zone 32 room 3) darkens the water "
                            "under it like the packet path, not black"),
            Suite("two_model_enemy_smoke", [PY, str(TESTS / "two_model_enemy_smoke.py"), "--card", str(args.save)],
                  300, note="a Ghost (two-model enemy, zone 13 room 4) through _zoneContext.zndEnemies; leaving a "
                            "room frees its spawn table (game heap steady); the LP64 import aliases"),
            Suite("ending_smoke", [PY, str(TESTS / "ending_smoke.py"), "--card", str(args.save)], 300,
                  note="ENDING.PRG twice from the save: credits, skip, MISSION COMPLETE rank screen, title"),
            Suite("light_actors_smoke", [PY, str(TESTS / "light_actors_smoke.py"), "--card", str(args.save)], 300,
                  note="Place Actors drag and drop, light actor prefabs, overrides, flicker, glow links (save room)"),
            Suite("fixture_lighting_smoke", [PY, str(TESTS / "fixture_lighting_smoke.py"), "--card", str(args.save)],
                  600, note="generated fixture lights: types, lit, shadow exclusion, glow links, suppressed glows, "
                            "live type edits, catalog save, regenerate (zone 9)"),
            Suite("sky_smoke", [PY, str(TESTS / "sky_smoke.py"), "--card", str(args.save)], 300,
                  note="sun and sky: roofed room unchanged, outdoor room's generated sun shadows and sky fill, "
                       "energy, NRD, probes, shadow-map fallback, save/load, generated tags stable"),
            Suite("atmosphere_smoke", [PY, str(TESTS / "atmosphere_smoke.py"), "--card", str(args.save)], 300,
                  note="haze, ground fog, fog volumes: fog follows height, volumes keep to their bounds, save/load, "
                       "generated forest fog, the moon's shafts in the haze, steady frame to frame"),
            Suite("atmosphere_sweep", [PY, str(ROOT / "pc/tools/atmosphere_sweep.py"), "--quick", "--card",
                                       str(args.save)], 300,
                  note="atmosphere never washes out the view: town, forest, Undercity rooms at several camera "
                       "angles and toward the moon (full sweep: pc/tools/atmosphere_sweep.py)"),
            Suite("void_fog_smoke", [PY, str(TESTS / "void_fog_smoke.py"), "--card", str(args.save)], 300,
                  note="the void past cut-away walls (Undercity, zone 48 room 0) stays black with the fog and "
                       "haze on; fog still on the floor; interior/void switches; ghost walls; a volume draws"),
            Suite("fog_sim_smoke", [PY, str(TESTS / "fog_sim_smoke.py"), "--card", str(args.save)], 300,
                  note="interactive fog prototype (sim=1, off by default): Ashley clears her footprint, a wake "
                       "when walking, settles back"),
            Suite("fog_volume_smoke", [PY, str(TESTS / "fog_volume_smoke.py"), "--card", str(args.save)], 300,
                  note="fog volumes' own renderer: thick (opacity >= 0.9), its own opacity cap, depth, a light "
                       "brightens and tints it, dim grey not black in a dark room, still frames steady (haze too)"),
            Suite("particles_smoke", [PY, str(TESTS / "particles_smoke.py"), "--card", str(args.save)], 600,
                  note="particles: emitters round-trip through the lighting file; motes deterministic, counted by "
                       "Dust; Dust Off pixel-identical, shadowed motes never show, lit ones are square game pixels; "
                       "editor select/move/reset/delete; GPU/CPU cost; the town house's dust (34/0)"),
            Suite("glow_stability_smoke", [PY, str(TESTS / "glow_stability_smoke.py"), "--card", str(args.save)], 600,
                  note="save point: one steady glow, stable id, overrides persist; prologue fight: effect glows unshadowed, smooth"),
            Suite("emissive_smoke", [PY, str(TESTS / "emissive_smoke.py"), "--card", str(args.save)], 300,
                  note="emissive texels: 0 is the picture without them (pixel-identical), marked panes glow and "
                       "nothing else changes, curated entries found in their rooms, editor mask saved and "
                       "reloaded, scopes (room, surfaces, models) and show other uses, filtered masks still "
                       "lights; Vulkan: the coals light the hood (path traced), narrowed too"),
            Suite("save_point_glow_smoke", [PY, str(TESTS / "save_point_glow_smoke.py"), "--card", str(args.save)], 300,
                  note="standing in a save point keeps its light (its glow is the column's side); Ashley still "
                       "shadows the floor behind him (High, Ultra)")]
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
        Suite("widescreen_undrawn_smoke", [PY, str(TESTS / "widescreen_undrawn_smoke.py")], 420,
              note="16:9: every pixel of the view drawn every frame through the opening (no trails); "
                   "the background sphere's old placement left the sides undrawn"),
        *([Suite("save_menu_smoke", [PY, str(TESTS / "save_menu_smoke.py"), "--card", str(args.save)], 300,
                 note="DATA > LOAD slot images = disc data decode, on interpolated frames too (CLUT swaps); "
                      "16:9 save confirm rows; Game Over > LOAD without the room flickering in the side")]
          if args.save.exists() else []),
        *([Suite("narrowed_2d_smoke", [PY, str(TESTS / "narrowed_2d_smoke.py"), "--card", str(args.save)], 600,
                 note="16:9: menu bars and headers never drawn as projected 2D with the menu's Ashley on screen "
                      "(render_stats, stat bands in one piece, even header letters); battle number popups over an "
                      "enemy in their compressed boxes on every landing frame; the area-targeting floor "
                      "fill drawn projected (no bands beside it) with smooth edges")]
          if args.save.exists() else []),
        *([Suite("weapon_slots_smoke", [PY, str(TESTS / "weapon_slots_smoke.py"), "--card", str(args.save)], 300,
                 note="QoL weapon slots: off = the game's L2 overlay; swaps, assign mode, shields, D-pad, drawn; "
                      "the mod's save bytes survive a save and reload")]
          if args.save.exists() else []),
        *([Suite("hud_label_solid_smoke", [PY, str(TESTS / "hud_label_solid_smoke.py"), "--card", str(args.save)],
                 300, note="HUD: the HP / MP labels drawn opaque like their digits (solid black outline, the field "
                           "not showing through), ui_text font and original")]
          if args.save.exists() else []),
        *([Suite("hud_bench_smoke", [PY, str(TESTS / "hud_bench_smoke.py"), "--card", str(args.save)], 300,
                 note="Lua HUD layer: SLOW! summoned and held (rood.dev.show), claimed by a plugin (suppressed, "
                      "drawn), hot-reloaded while held; the game's own drawing with no claim, a draw error, the "
                      "plugin off, the classic look")]
          if args.save.exists() else []),
        *([Suite("classic_parity", [PY, str(TESTS / "classic_parity.py"), "--card", str(args.save)], 600, gpu=False,
                 note="the classic look in Lua: rood.classic's timing words and chain counts packet for packet "
                      "against the game's own in the same frames (the null renderer), waivers = its declared "
                      "overrides, on by default, the game's drawing as the fallback, state parity")]
          if args.save.exists() else []),
        *([Suite("classic_hud_smoke", [PY, str(TESTS / "classic_hud_smoke.py"), "--card", str(args.save)], 300,
                 note="rood.classic on a renderer: its typeset decides the status words' typography (one space, on "
                      "screen at 16:9), hot-reloaded (timed), the REPL, place as data (the gate sees dx), the same "
                      "picture as the game's")]
          if args.save.exists() else []),
        *([Suite("repl_smoke", [PY, str(TESTS / "repl_smoke.py"), "--card", str(args.save)], 300,
                 note="Lua REPL bridge: rood_repl.py -e sessions across processes, sandboxes, inspection, "
                      "rood.hud's draw redefined live (next frame), then its file reloaded wins")]
          if args.save.exists() else []),
        *([Suite("photo_foundation_smoke", [PY, str(TESTS / "photo_foundation_smoke.py"), "--card", str(args.save)],
                 300, note="the photo mode foundation in Lua: pause (the state digest held), the free camera (orbit, "
                           "fov, roll), post overrides, the HUD and 2D hidden, a PNG photo, input capture (Start, "
                           "Escape), the example plugin's session; everything given back, settings.cfg unchanged")]
          if args.save.exists() else []),
        *([Suite("photo_mode_smoke", [PY, str(TESTS / "photo_mode_smoke.py"), "--card", str(args.save)], 300,
                 note="the shipped photo mode (rood.photo): refused in a menu; F12 enters (held, HUD hidden, the "
                      "input and camera its own), each card row live through rood.post, the camera keys, a PNG, "
                      "Cross / the pad button / Start leave with everything restored (state digest, post values, "
                      "the picture, settings.cfg)")]
          if args.save.exists() else []),
        *([Suite("frame_cost_smoke", [PY, str(TESTS / "frame_cost_smoke.py"), "--card", str(args.save)], 300,
                 note="per-frame cost (ROOD_PERF counters): no read-backs (sphere, Status rows: GPU copies), "
                      "emissive not re-hashed, CPU skips stale 2D; pictures = the old paths'")]
          if args.save.exists() else []),
        *([Suite("button_roles_smoke", [PY, str(TESTS / "button_roles_smoke.py"), "--card", str(args.save)], 300,
                 note="QoL button roles: Cross confirms (settings file, title and loading, menus, the modern "
                      "menus' tabs), Cross attacks in the field; each alone, both, neither; the ability lists")]
          if args.save.exists() else []),
        *([Suite("button_roles_symbols_smoke", [PY, str(TESTS / "button_roles_symbols_smoke.py"), "--card",
                       str(args.save)], 300,
                 note="QoL button roles: the game's own ○ / × sprites and text glyphs follow Cross confirms "
                      "(original drawing, ui_text font, prompts); text naming unmoved buttons keeps them")]
          if args.save.exists() else []),
        *([Suite("ui_menus_smoke", [PY, str(TESTS / "ui_menus_smoke.py"), "--card", str(args.save)], 300,
                 note="modern menus: off = the game's menu; shell tabs, original Status and back, "
                      "System > Menus from the next open, L2 shortcut stays original, greyed Break arts opens, close")]
          if args.save.exists() else []),
        *([Suite("menu0_levelled_smoke", [PY, str(TESTS / "menu0_levelled_smoke.py"), "--card", str(args.save)], 300,
                 note="original Magic menu, levelled Warlock spell: LV picker with '--' boxes (GPU sprite size "
                      "masked as on hardware), LV1 and LV2 cast: hand-off id, MP drops by the cost")]
          if args.save.exists() else []),
        *([Suite("fast_travel_smoke", [PY, str(TESTS / "fast_travel_smoke.py"), "--card", str(args.save)], 1200,
                 note="fast travel (original menus): unlock from the first Circle, free cost, R6 audit, Room Browser "
                      "hygiene, travel then save then decode then Continue, refusals, the game's own unlock, the "
                      "Travel button, the prompt (the game picture's upper right, the modern UI's font; old "
                      "settings migrate)")]
          if args.save.exists() else []),
        *([Suite("fast_travel_shell_smoke", [PY, str(TESTS / "fast_travel_smoke.py"), "--card", str(args.save),
                                             "--menus", "on"], 1200,
                 note="fast travel with the modern menus: Magick > Teleport on the same unlock, cost, mask and "
                      "refusals; save (System > Data) and Continue; the Travel button")]
          if args.save.exists() else []),
        *([Suite("skip_charging_smoke", [PY, str(TESTS / "skip_charging_smoke.py"), "--card", str(args.save)], 1500,
                 note="battle charging skip (paced): Solid Shock, a Cure Root and a Break Art off and on: the same "
                      "outcome, release view, flags, score and clean state; skipped wind-ups short and unpresented; "
                      "with the random numbers pinned, the enemies (who: all) act the same; the holds shown first "
                      "(mine / enemy: paced, their length, clamped to the original's) and a defence press after an "
                      "enemy's release judged as off")]
          if args.save.exists() else []),
        *([Suite("charge_skip_soak", [PY, str(TESTS / "charge_skip_soak.py"), "--card", str(args.save)], 1500,
                 note="battle charging skip soak (unpaced): every spell of the four schools, 40 misc items "
                      "(grimoires too) and the equipped weapon's Break Arts: wind-ups <= 32 vsyncs, none "
                      "presented, a 2000-vsync watchdog, clean afterwards")]
          if args.save.exists() else []),
        *([Suite("item357_smoke", [PY, str(TESTS / "charge_skip_soak.py"), "--card", str(args.save),
                                   "--first", "357", "--items", "1", "--only", "items", "--mode", "off"], 600,
                 note="misc item 357 (unnamed, action 0) used in battle, charging skip off: BATTLE frames the empty "
                      "actor slot 1 (read from PS1 address 0, as the PS1); no crash, back in the field, clean")]
          if args.save.exists() else []),
        *([Suite("heal_audio_smoke", [PY, str(TESTS / "heal_audio_smoke.py"), "--card", str(args.save)], 300,
                 note="Cure Root in the field (audio recorded): its streamed sound fed by the SPU interrupt and "
                      "keyed off at its end; afterwards the room's level and no repeating phrase (no endless echo)")]
          if args.save.exists() else []),
        *([Suite("heal_audio_noirq_smoke",
                 [PY, str(TESTS / "heal_audio_smoke.py"), "--card", str(args.save), "--no-irq"], 300,
                 note="the same without the SPU interrupt (ROOD_SPU_IRQ=0): the driver's stop command, sent "
                      "between two driver ticks, still keys the looping stream off (key on/off writes not lost)")]
          if args.save.exists() else []),
        *([Suite("ui_equip_smoke", [PY, str(TESTS / "ui_equip_smoke.py"), "--card", str(args.save)], 900,
                 note="Equipment screen: same RAM as the original equip page (MENU3) for weapon/shield/armor/"
                      "accessory changes, snapshot, quick swap, mouse, hold on close, bare hands, drawn")]
          if args.save.exists() else []),
        *([Suite("ui_ashley_smoke", [PY, str(TESTS / "ui_ashley_smoke.py"), "--card", str(args.save)], 300,
                 note="Ashley tab: summary = RAM (stats, limbs, effects), live model shown/turned (stick, drag, "
                      "no auto-turn), tabs/Status/weapon change, world and camera restored, 16:9, soft reset")]
          if args.save.exists() else []),
        *([Suite("ui_ashley_stability_smoke", [PY, str(TESTS / "ui_ashley_stability_smoke.py"), "--card",
                       str(args.save)], 300,
                 note="Ashley tab model, lit (GI, traced shadows, haze): every image shows it, no flicker "
                      "still or turning (live and interpolated images lit alike), window-resolution detail view")]
          if args.save.exists() else []),
        *([Suite("ui_ashley_detail_smoke", [PY, str(TESTS / "ui_ashley_detail_smoke.py"), "--card",
                       str(args.save)], 300,
                 note="Ashley tab model at 4x, 8 views: studio light on, the game's rim copy left out, no thin "
                      "holes in the clone alone (object culling at drawn positions), no bright slivers inside it")]
          if args.save.exists() else []),
        *([Suite("ui_ashley_backdrop_smoke", [PY, str(TESTS / "ui_ashley_backdrop_smoke.py"), "--card",
                       str(args.save)], 300,
                 note="Ashley tab opened first, low ui_menu_opacity: a world picture kept (backdrop_kept), the "
                      "world behind as on Equipment; back from the original Status: no HUD in the picture")]
          if args.save.exists() else []),
        *([Suite("ui_abilities_smoke", [PY, str(TESTS / "ui_abilities_smoke.py"), "--card", str(args.save)], 300,
                 note="Abilities tab: Chain/Defense mapping = the original MENU2 screen's bytes (set, move, replace, "
                      "off; list, pick, mouse), learned lists, break arts states and greyed reasons, cast and cancel")]
          if args.save.exists() else []),
        *([Suite("ui_magick_smoke", [PY, str(TESTS / "ui_magick_smoke.py"), "--card", str(args.save)], 300,
                 note="Magick tab: snapshot = RAM (schools, learned spells, MP costs, levels), Heal's hand-off = "
                      "the original MENU0's, cast and cancel, LV2 (Right / chip), greyed (MP, silence), Teleport")]
          if args.save.exists() else []),
        *([Suite("ui_items_smoke", [PY, str(TESTS / "ui_items_smoke.py"), "--card", str(args.save)], 300,
                 note="Items tab: same inventory bytes as the original MENU3 for discard (refused, gems, quantity, "
                      "all), sort, move/merge; Use's hand-off, completed and cancelled; keys; quick slot; mouse")]
          if args.save.exists() else []),
        *([Suite("ui_loot_smoke", [PY, str(TESTS / "ui_loot_smoke.py"), "--card", str(args.save)], 300,
                 note="Loot screen (direct entry 12): same bag, indices, key flags, result, heap, HUD / world flags "
                      "and Items memories as the original MENUB for take, take all, keys, leave, the save room's "
                      "real chest, chests, disassemble (weapon, shield), consolidate, organize (discard, quantity, "
                      "merge, sort), difficulty filter, two lists, No paths; mouse; soft reset")]
          if args.save.exists() else []),
        *([Suite("ui_map_smoke", [PY, str(TESTS / "ui_map_smoke.py"), "--card", str(args.save)], 300,
                 note="Map frame (MENU5 with the shell's chrome): same map status, mapPaling, cursor memories, "
                      "settings, menu / HUD state, scene buffer, camera and VRAM as the original map screen by pad "
                      "and by pointer; areas, completion, pad and pointer limits, the area list, paling, close, "
                      "soft resets, 16:9")]
          if args.save.exists() else []),
        *([Suite("ui_manual_smoke", [PY, str(TESTS / "ui_manual_smoke.py"), "--card", str(args.save)], 300,
                 note="Quick manual (MENUE native): manualDisplayState, cursor memories, HUD/world flags, heap as "
                      "the original by pad and pointer; sounds played = the original's (pad) and the manual's "
                      "requests (pad, pointer); text = disc decode for 14 topics; sprite/rule counts, a blink, "
                      "canvas pixels; button art as device prompts; 16:9; reset")]
          if args.save.exists() else []),
        *([Suite("ui_score_smoke", [PY, str(TESTS / "ui_score_smoke.py"), "--card", str(args.save)], 600,
                 note="Score (MENU9 native, its own _calculateProgress): scoredata, roomFlags, cursor memories, "
                      "score / rank, sounds and the close state as the original by pad; boundaries against a port "
                      "of the rules; Square idempotent; text = MENU9's statics; MON.BIN = _monBinData; pointer at "
                      "4:3 and 16:9 (one SDL click); theme reload and RPC override; card copy unchanged")]
          if args.save.exists() else []),
        *([Suite("ui_workshop_smoke", [PY, str(TESTS / "ui_workshop_smoke.py"), "--card", str(args.save)], 300,
                 note="Items > Workshop: same bag, indices and MENUC cursor memories as the original Setup screen "
                      "(MENUC / MENU8) for repair, gems, disassemble (equipped too), assemble, rename, combine x3")]
          if args.save.exists() else []),
        *([Suite("ui_data_smoke", [PY, str(TESTS / "ui_data_smoke.py"), "--card", str(args.save)], 600,
                 note="Data screen (MENU7 under a native frame): save bytes = the original's by pad and by mouse "
                      "(pinned keystream), one write, overwrite No / Yes, load = the original's blocks, errors "
                      "(no card, corrupt, full, blank, refused, unformatted), exits; player cards hashed unchanged")]
          if args.save.exists() else []),
        *([Suite("ui_container_smoke", [PY, str(TESTS / "ui_container_smoke.py"), "--card", str(args.save)], 2400,
                 note="Container (MENU7 framed, MENUD native): at MENUD's exit the bag, the storage, containerEmpty, "
                      "the menu state and pointers = the original's by pad (sorts, discard, merge / swap, staging, "
                      "cancel warning and revert, no card: each checked done) and by mouse (Remove, prompts, "
                      "save list), cursor memories = by pad; saved cards byte-identical (pinned keystream); "
                      "soft resets; player cards hashed unchanged")]
          if args.save.exists() else []),
        *([Suite("ui_system_smoke", [PY, str(TESTS / "ui_system_smoke.py"), "--card", str(args.save)], 300,
                 note="System screen through the shell: a row per settings page live + saved (debounce, Cross), "
                      "greyed rows, slider drag, binding capture (blocks navigation, Esc, pad, reset), Data, relaunch")]
          if args.save.exists() else []),
        Suite("system_settings_smoke", [PY, str(TESTS / "system_settings_smoke.py"),
                                        *(["--card", str(args.save)] if args.save.exists() else [])], 360,
              note="System settings: brightness, master volume (WAV), window mode, binding capture (SDL events; "
                   "key works in the field), saved and back after a relaunch with widescreen / perspective kept"),
        *([Suite("options_classic_smoke", [PY, str(TESTS / "options_classic_smoke.py"), "--card", str(args.save)], 400,
                 note="Option pages in the game's Option screen (classic): entries after the nine rows, rows = the "
                      "page model's, values live + saved on leaving, the flyout, a sub-page, a plugin's page (header "
                      "skipped, action, hot reload), cursor memory past nine, Triangle; vs_main_settings and the "
                      "card unchanged; goldens of the Display page (original, font) when present")]
          if args.save.exists() else []),
        *([Suite("options_confirm_smoke", [PY, str(TESTS / "options_confirm_smoke.py"), "--card", str(args.save)], 400,
                 note="Keep these settings? (classic and modern): applied at once, the old value saved until kept, "
                      "KEEP / Cross / no answer (3 s, real time) / RPC keep; modern and classic rows = the page "
                      "model's for every core page")]
          if args.save.exists() else []),
        Suite("window_mode_smoke", [PY, str(TESTS / "window_mode_smoke.py"),
                                    *(["--card", str(args.save)] if args.save.exists() else [])], 300,
              note="window mode cycled both ways twice (save room): no device loss, presents go on, windowed "
                   "image = window size; real window (Vulkan swapchain) only with ROOD_TEST_DESKTOP=1, else headless"),
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
        *([Suite("damage_numbers_smoke", [PY, str(TESTS / "damage_numbers_smoke.py"), "--card", str(args.save)], 300,
                 note="battle number popups (poison damage, regen heal; a stat change): original = PS1 digits; "
                      "font draws them in place, legible, the game's value; UI text scale 2 about their middle")]
          if args.save.exists() else []),
        *([Suite("button_prompts_smoke", [PY, str(TESTS / "button_prompts_smoke.py"), "--card", str(args.save)], 400,
                 note="button prompts: original = PS1 glyphs; xbox/playstation/nintendo/keyboard icons only inside "
                      "the glyph boxes; auto follows the device; rebinding changes the icon; 1x; settings")]
          if args.save.exists() else []),
        *input_cases(scale),
        Suite("transition_fade_smoke", [PY, str(TESTS / "transition_fade_smoke.py"), "--card", str(args.save)], 600,
              note="door and cross-zone fades darken the lit picture monotonically to black and back (no unlit pop)"),
        Suite("title_fade_smoke", [PY, str(TESTS / "title_fade_smoke.py")]
              + (["--card", str(args.save)] if args.save.exists() else []), 300,
              note="the title's fade after Continue = the original frame by frame (4:3: no black box over the "
                   "menu, the backdrop fades; 16:9 brightness)"),
        Suite("post_smoke", [PY, str(TESTS / "post_smoke.py"), "--card", str(args.save)], 600,
              note="post stack: each effect changes only the 3D, defaults neutral, presets, profiles save/load and "
                   "blend, lens blur only in field play (not title, attract, cutscene, menu)"),
        *([Suite("tunables_smoke", [PY, str(TESTS / "tunables_smoke.py"), "--card", str(args.save)], 300,
                 note="tunables in data: settings keys read back and reach their modules, post shoulder / tilt "
                      "feather / sharpen and vignette gains change the 3D (defaults pixel-identical), prompt "
                      "colours recolour the icons, smart walls' timing, fog presets reload live")]
          if args.save.exists() else []),
        Suite("dof_smoke", [PY, str(TESTS / "dof_smoke.py"), "--card", str(args.save)], 600,
              note="physical depth of field: off by default, blur grows with lower f-stops and away from the focus, "
                   "bokeh blade count / squeeze / cat's eye on the test card, unblurred title, attract, cutscene, "
                   "menu; first-person focus follows the centre (eased); Vulkan GPU ms"),
        Suite("dof_subject_smoke", [PY, str(TESTS / "dof_subject_smoke.py"), "--card", str(args.save)], 300,
              note="depth of field on the camera subject (the default): Ashley in the field (as On Ashley), an "
                   "enemy's caster from its wind-up (eased, sharper than on Ashley), Ashley after; a rule as data; "
                   "the targeted actor"),
        Suite("occlusion_smoke", [PY, str(TESTS / "occlusion_smoke.py"), "--card", str(args.save)], 300,
              note="occlusion in play: a beam hiding Ashley fades around her (eased, a stable dither, the rest "
                   "solid), silhouettes tint her hidden pixels; in battle a wall hiding an enemy near her fades"),
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
    """The tests' own pinned card: $ROOD_TEST_CARD, else pc/build/test-card1.mcd
    (here, else in the main checkout), never the player's live card. The pin is
    made once from pc/save/card1.mcd (a copy; replace it to re-pin); suites that
    expect the save room need a card saved there."""
    env = os.environ.get("ROOD_TEST_CARD")
    if env:
        return pathlib.Path(env)
    roots = [ROOT]
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                            capture_output=True, text=True).stdout.strip()
    if common and pathlib.Path(common).parent != ROOT:
        roots.append(pathlib.Path(common).parent)
    for root in roots:
        pinned = root / "pc/build/test-card1.mcd"
        if pinned.exists():
            return pinned
    for root in roots:
        live = root / "pc/save/card1.mcd"
        if live.exists():
            pinned = root / "pc/build/test-card1.mcd"
            pinned.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(live, pinned)
            return pinned
    return ROOT / "pc/build/test-card1.mcd"


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


# Suites that start from a test checkpoint (checkpoints.launch_at): they wait
# while snapshot_equivalence makes the save states, so it runs first beside
# a suite that does not.
CHECKPOINT_SUITES = {
    "probe_gi_smoke", "animation_smoke", "editor_cutaway_smoke", "editor_undo_smoke", "editor_selection_smoke", "editor_schema_smoke", "editor_transform_smoke", "barn_doors_smoke", "editor_handles_smoke", "facing_cutaway_smoke",
    "camera_stick_smoke", "editor_snap_smoke", "editor_roomload_smoke", "room_browser_smoke", "route_continuation_smoke", "ending_smoke",
    "light_actors_smoke", "fixture_lighting_smoke", "sky_smoke", "glow_stability_smoke", "lighting_presets_smoke",
    "gi_lag_smoke", "texture_animation_smoke", "save_menu_smoke", "narrowed_2d_smoke", "weapon_slots_smoke", "ui_menus_smoke",
    "ui_lines_smoke", "ui_text_smoke", "button_prompts_smoke", "button_roles_symbols_smoke", "coplanar_order_smoke", "atmosphere_smoke",
    "void_fog_smoke",
    "fog_sim_smoke", "fog_volume_smoke", "particles_smoke", "save_point_glow_smoke", "emissive_smoke", "ui_ashley_smoke", "ui_ashley_stability_smoke",
    "ui_equip_smoke",
    "ui_system_smoke", "ui_abilities_smoke", "ui_items_smoke", "menu0_levelled_smoke", "fast_travel_smoke", "fast_travel_shell_smoke", "post_smoke", "dof_smoke", "dof_subject_smoke", "occlusion_smoke",
    "transition_fade_smoke", "title_fade_smoke", "ui_loot_smoke", "ui_workshop_smoke", "window_mode_smoke",
    "options_classic_smoke", "options_confirm_smoke",
    "ui_map_smoke", "ui_manual_smoke", "hud_label_solid_smoke", "damage_numbers_smoke", "platform_shadow_smoke",
    "ui_ashley_backdrop_smoke", "ui_data_smoke", "ui_container_smoke", "interp_menu_2d_smoke",
    "ui_ashley_detail_smoke", "ui_score_smoke", "snapshot_resume_smoke", "engine_reload_smoke", "frame_cost_smoke",
    "hud_bench_smoke", "classic_hud_smoke",
    "repl_smoke", "photo_foundation_smoke", "photo_engine_reload_smoke", "photo_mode_smoke",
}


# Game suites heavy on the GPU (the path tracer, probe GI, NRD; the light
# they check after N vsyncs): at most --heavy-jobs of them run at once,
# whatever -j is; with more, the shared GPU made them fail. Keep it to
# those: in a full run the capped ones form a queue of their own (their
# summed time / --heavy-jobs is a floor for the run's wall time).
HEAVY_SUITES = {
    "glow_stability_smoke", "save_point_glow_smoke", "probe_gi_smoke", "gi_smoke", "gi_lag_smoke", "nrd_smoke",
    "rt_camera_smoke", "sky_smoke", "lighting_presets_smoke", "emissive_smoke", "ui_ashley_stability_smoke",
}


def cpu_temperature():
    """The CPU package temperature in degrees C from hwmon (coretemp's
    "Package id 0", k10temp's Tctl, else the hottest coretemp/k10temp/zenpower
    input), or None where there is none."""
    best = None
    for hwmon in pathlib.Path("/sys/class/hwmon").glob("hwmon*"):
        try:
            if (hwmon / "name").read_text().strip() not in ("coretemp", "k10temp", "zenpower"):
                continue
            for label in hwmon.glob("temp*_label"):
                value = int(label.with_name(label.name.replace("_label", "_input")).read_text()) / 1000
                if label.read_text().strip() in ("Package id 0", "Tctl", "Tdie"):
                    return value
                best = value if best is None else max(best, value)
        except (OSError, ValueError):
            continue
    return best


class ThermalGate:
    """Before each suite starts: if the CPU package is at --thermal-pause
    degrees or more, wait (no new suite starts; the running ones go on) until
    it is at --thermal-resume or less, for at most --thermal-wait seconds."""

    def __init__(self, args):
        self.pause, self.resume, self.limit = args.thermal_pause, args.thermal_resume, args.thermal_wait
        self.lock = threading.Lock()
        self.waited = 0.0

    def wait(self, name):
        if not self.pause:
            return
        with self.lock:  # one waiter reports; the others queue behind it
            t = cpu_temperature()
            if t is None or t < self.pause:
                return
            print(f"thermal: CPU package {t:.0f} C (>= {self.pause}); {name} waits for {self.resume} C", flush=True)
            start = time.monotonic()
            while t is not None and t > self.resume and time.monotonic() - start < self.limit:
                time.sleep(2)
                t = cpu_temperature()
            self.waited += time.monotonic() - start
            print(f"thermal: {t:.0f} C after {time.monotonic() - start:.0f} s; {name} starts", flush=True)


def run_lanes(order, args, run):
    """Run `order` ((index, suite) pairs, in that order of preference) in two
    lanes: the game suites (--gpu-jobs at once, of them at most --heavy-jobs
    of HEAVY_SUITES) and the rest (CTest, lints, scans, build checks;
    --cpu-jobs), so a lint never takes a game's slot or waits for one. A free
    game slot takes the first waiting suite it may run, so a capped heavy
    suite never holds a slot while it waits."""
    cond = threading.Condition()
    waiting = {True: [item for item in order if item[1].gpu], False: [item for item in order if not item[1].gpu]}
    heavy = [0]
    gate = ThermalGate(args)

    def take(gpu):
        with cond:
            while True:
                items = waiting[gpu]
                if not items:
                    return None
                # snapshot_equivalence first (the checkpoint suites wait for its save states: a
                # heavy one in every slot would wait for it forever), then a heavy suite while
                # one of their slots is free: their queue starts at once
                free = gpu and heavy[0] < args.heavy_jobs
                for item in sorted(items, key=lambda item: (item[1].name != "snapshot_equivalence",
                                                            not (free and item[1].name in HEAVY_SUITES))):
                    if not gpu or item[1].name not in HEAVY_SUITES or heavy[0] < args.heavy_jobs:
                        items.remove(item)
                        if gpu and item[1].name in HEAVY_SUITES:
                            heavy[0] += 1
                        return item
                cond.wait()  # only capped heavy suites left: until one ends

    def worker(gpu):
        while (item := take(gpu)) is not None:
            index, suite = item
            try:
                gate.wait(suite.name)
                run(suite, index)
            except Exception:  # the runner's own error: the suite shows as NOT RUN
                traceback.print_exc()
            finally:
                with cond:
                    if gpu and suite.name in HEAVY_SUITES:
                        heavy[0] -= 1
                    cond.notify_all()

    threads = [threading.Thread(target=worker, args=(gpu,)) for gpu, n in ((True, args.gpu_jobs), (False, args.cpu_jobs))
               for _ in range(max(1, n))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return gate.waited


def file_hash(path, h=None):
    h = h or hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h


def snapshot_cache(config):
    """pc/build/test-snapshots/<config>-<hash of the game binary and the
    disc data>: the checkpoints' save states for this build (older builds'
    directories of the config are removed)."""
    h = file_hash(rood_rpc.BINARY)
    for data in (ROOT / "data/SLUS_010.40", ROOT / "disks/SLUS-01040.bin"):
        if data.exists():
            st = data.stat()
            h.update(f"{data.name} {st.st_size} {st.st_mtime_ns}".encode())
    base = ROOT / "pc/build/test-snapshots"
    directory = base / f"{config}-{h.hexdigest()[:16]}"
    if base.exists():
        for old in base.glob(f"{config}-*"):
            if old != directory:
                shutil.rmtree(old, ignore_errors=True)
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def schedule(selected, timings, config):
    """Longest first by the last run's times (a quarter of the timeout for a
    suite without one), so the lanes finish together; snapshot_equivalence
    first, beside the longest suite that does not wait for its save states."""
    try:
        known = json.loads(timings.read_text()).get(config, {})
    except (OSError, ValueError):
        known = {}
    items = sorted(enumerate(selected), key=lambda item: -known.get(item[1].name, item[1].timeout / 4))
    first = [item for item in items if item[1].name == "snapshot_equivalence"]
    rest = [item for item in items if item[1].name != "snapshot_equivalence"]
    if first:
        free = next((item for item in rest if item[1].name not in CHECKPOINT_SUITES), None)
        if free:
            rest.remove(free)
            rest.insert(0, free)
    return first + rest


def record_durations(timings, config, results):
    try:
        data = json.loads(timings.read_text())
    except (OSError, ValueError):
        data = {}
    data.setdefault(config, {}).update({name: round(elapsed, 1) for name, (result, elapsed, _, _) in results.items()
                                        if result != "SKIP"})
    timings.write_text(json.dumps(data, indent=1, sort_keys=True) + "\n")


def run_suite(suite, index, args, mode, results, lock):
    out = OUT / suite.name.replace(":", "-")
    if suite.name.startswith("soak"):
        suite.command += ["--out", str(out / "soak")]
    command = suite.command
    if args.stall and command[0] == PY:  # its game stopped for a while (stall_inject.py)
        command = [PY, str(TESTS / "stall_inject.py"), *shlex.split(args.stall), "--"] + command[1:]
    if args.profile and command[0] == PY:  # the test's own Python time too (python.prof)
        command = [PY, "-c", PROFILED, str(out / "python.prof")] + command[1:]
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
        ROOD_PLUGINS=str(out / "plugins"),  # none of the user's plugins (pc/user/plugins)
        ROOD_PLUGIN_DATA=str(out / "plugin_data"),
        ROOD_HEADLESS=mode,
        ROOD_BUTTON_PROMPTS="original",  # the PS1 glyphs (button_prompts_smoke opts in)
        ROOD_AUDIO=f"wav:{out / 'audio.wav'}" if suite.name.startswith("input:") else "none",
        PYTHONUNBUFFERED="1",
        # A game being played beside the tests keeps the GPU first (Vulkan global priority).
        **({"ROOD_GPU_PRIORITY": "low"} if args.nice else {}),
        # The GPU is what limits the suites at once: a paused game nobody is talking to redraws
        # the same picture less often (after each request it still redraws at once).
        ROOD_PAUSED_REDRAW_HZ=os.environ.get("ROOD_PAUSED_REDRAW_HZ", "20"),
        # NVIDIA's OpenGL spins a core while it waits for the GPU (a frame-in-flight fence); with
        # the GPU shared by several games most of each game's time is that wait: sleep instead.
        __GL_YIELD=os.environ.get("__GL_YIELD", "USLEEP"),
        **({"ROOD_RPC_PROFILE": str(out / "rpc-profile.txt")} if args.profile else {}),
        # Save states of the test checkpoints (checkpoints.py), or none.
        **({"ROOD_TEST_SNAPSHOT_DIR": str(args.snapshot_dir)} if args.snapshot_dir else {"ROOD_TEST_SNAPSHOTS": "0"}),
        # llvmpipe (Xvfb, or EGL without a GPU) otherwise takes every core per game.
        LP_NUM_THREADS=os.environ.get("LP_NUM_THREADS", "4"),
        # The Vulkan pipeline cache (vk_core.c) of the test runs, not the user's.
        ROOD_VK_PIPELINE_CACHE=str(out.parent / "vk-pipelines.bin"),
        # ROOD_TEST_DESKTOP=1: window_mode_smoke opens a real window (fullscreen for a few seconds) on this
        # display; every other suite stays headless.
        **({"ROOD_TEST_DESKTOP": "1", "ROOD_DESKTOP_DISPLAY": os.environ["DISPLAY"]}
           if os.environ.get("ROOD_TEST_DESKTOP") == "1" and os.environ.get("DISPLAY") else {}),
    )
    for key in ("ROOD_EGL_DEVICE", "ROOD_NATIVE_ROOM", "ROOD_NATIVE_ACTORS", "ROOD_NATIVE_BLEND",
                "ROOD_NATIVE_OBJECTS", "ROOD_NATIVE_SHADOWS", "ROOD_NATIVE_EFFECTS",
                "ROOD_RECONSTRUCT", "ROOD_ARCH", "ROOD_RENDERER", "ROOD_VK_DEVICE", "ROOD_VK_VALIDATE",
                "ROOD_VK_RT", "ROOD_VK_PIPELINE_CACHE"):  # renderer choices to test under, binary
        if key in os.environ:
            env[key] = os.environ[key]
    if args.nice and IONICE:  # idle I/O class: the player's disk reads first
        command = [IONICE, "-c", "3", *command]
    start = time.monotonic()
    with log.open("w") as output:
        p = subprocess.Popen(command, cwd=ROOT, env=env, stdout=output, stderr=subprocess.STDOUT,
                             start_new_session=True, preexec_fn=(lambda: os.nice(args.nice)) if args.nice else None)
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
    if suite.name == "snapshot_equivalence" and args.snapshot_dir:
        (args.snapshot_dir / "building").unlink(missing_ok=True)  # the checkpoints' tests may go on
    pathlib.Path(env["ROOD_RPC"]).unlink(missing_ok=True)
    text = log.read_text(errors="replace").strip().splitlines()
    summary = next((line for line in reversed(text) if line.startswith(("PASS", "FAIL", "SKIP", "AssertionError",
                                                                          "RuntimeError", "Error"))),
                   text[-1] if text else "")
    with lock:
        results[suite.name] = (result, elapsed, log, summary)
        print(f"{result:<9} {elapsed:6.1f}s  {suite.name}", flush=True)


def make_parser():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("patterns", nargs="*", help="run suites whose names contain any of these")
    parser.add_argument("-j", "--jobs", type=int, default=int(os.environ.get("ROOD_TEST_JOBS", "3")),
                        help="game suites at once (default 3, $ROOD_TEST_JOBS: kind to the machine's "
                             "temperature; more runs little faster, the GPU being the limit); same as --gpu-jobs")
    parser.add_argument("--gpu-jobs", type=int, help="game suites at once (default: -j)")
    parser.add_argument("--heavy-jobs", type=int, default=2,
                        help="of those, the GPU-heavy ones (HEAVY_SUITES: path tracing) at once (default 2)")
    parser.add_argument("--cpu-jobs", type=int, default=2,
                        help="suites without a game (CTest, lints, scans, build checks) at once, beside the "
                             "game suites (default 2)")
    parser.add_argument("--thermal-pause", type=float, default=float(os.environ.get("ROOD_THERMAL_PAUSE", "102")),
                        help="no new suite starts while the CPU package is this hot (C; default 102, 0 = off: the chip throttles itself at 105)")
    parser.add_argument("--thermal-resume", type=float, default=float(os.environ.get("ROOD_THERMAL_RESUME", "96")),
                        help="... until it has cooled to this (C; default 96)")
    parser.add_argument("--thermal-wait", type=float, default=900,
                        help="the longest one such wait (s; default 900), then the suite starts anyway")
    parser.add_argument("--wait-cool", action="store_true",
                        help="only wait as before a suite (--thermal-pause / --thermal-resume), then exit "
                             "(pc/tools/verify.sh runs it between steps)")
    parser.add_argument("--changed", nargs="?", const="main", metavar="BASE",
                        help="only the suites a change since BASE (default main) can affect "
                             "(pc/tests/affected.toml; core or unknown files run everything); "
                             "put -- before suite patterns when omitting BASE")
    parser.add_argument("--changed-files", nargs="+", metavar="PATH",
                        help="as --changed, for these repository paths instead of a diff")
    parser.add_argument("--nice", type=int, default=int(os.environ.get("ROOD_TEST_NICE", "10")),
                        help="CPU niceness of the suites (default 10; 0 = as is), with idle I/O priority and "
                             "a low GPU priority where the driver has one (ROOD_GPU_PRIORITY=low)")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--clean", action="store_true",
                        help="remove this checkout's test outputs (pc/build/verify, pc/build/tests*, "
                             "pc/build/test-snapshots; pc/tools/clean_build.py), then exit")
    parser.add_argument("--profile", action="store_true",
                        help="log every RPC call's time (<suite>/rpc-profile.txt; pc/tests/profile_report.py)")
    parser.add_argument("--headless", choices=["egl", "xvfb"], help="default: $ROOD_HEADLESS, else egl if it works")
    parser.add_argument("--soak-frames", type=int, default=3000, help="vsyncs of soak play (default 3000)")
    parser.add_argument("--save", type=pathlib.Path, default=default_save(),
                        help="memory card for soak:save (copied, never written; skipped when absent)")
    parser.add_argument("--timeout-scale", type=float, default=1.0, help="multiply every suite's timeout")
    parser.add_argument("--stall", metavar="OPTIONS",
                        help="run each Python suite under pc/tests/stall_inject.py OPTIONS (its game SIGSTOPped "
                             "for a while at a chosen moment, as the thermal guard does), e.g. "
                             "--stall \"--seconds 5 --write settings.cfg\"")
    parser.add_argument("--exclude", action="append", default=[], help="skip suites containing this")
    parser.add_argument("--out", help="output directory (default pc/build/tests); one runner per directory")
    parser.add_argument("--arch", choices=["32", "64"], default=os.environ.get("ROOD_ARCH", "64"),
                        help="game binary: 64 (pc/build/boot64, default) or 32 (pc/build/boot); sets ROOD_ARCH")
    parser.add_argument("--renderer", choices=["gl", "vulkan", "null"],
                        default=os.environ.get("ROOD_RENDERER") if os.environ.get("ROOD_RENDERER") in ("vulkan", "null") else "gl",
                        help="gl, vulkan (64-bit), or null (CPU-only oracle suites)")
    parser.add_argument("--no-snapshots", action="store_true",
                        default=os.environ.get("ROOD_TEST_SNAPSHOTS", "1") == "0",
                        help="boot every suite from scratch: no save states of the test checkpoints "
                             "(also ROOD_TEST_SNAPSHOTS=0)")
    return parser


def default_args():
    """The options run.py takes with no arguments (for affected.py)."""
    return make_parser().parse_args([])


def main():
    args = make_parser().parse_args()
    if args.wait_cool:
        ThermalGate(args).wait("the next step")
        return 0
    if args.clean:
        import clean_build
        return clean_build.clean()
    if args.renderer in ("vulkan", "null"):
        os.environ["ROOD_RENDERER"] = args.renderer
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
    every = suites(args, mode)
    if args.renderer == "null":
        # Only explicitly CPU-safe game suites; never run screenshots or GPU
        # probes in this lane merely because they can launch the game binary.
        safe = {"null_backend_smoke", "shadow_battle_smoke", "shadow_battle_smoke_38C1C", "ownership_smoke", "battle_corpus_candidate",
                "zig_hotswap_smoke", "engine_rewind_smoke", "classic_parity",
                *(f"trial_corpus_{t[0]}{suffix}" for t in TRIAL_CORPUS for suffix in ("", "_features"))}
        every = [s for s in every if s.binary is None or s.name in safe]
        for suite in every:
            suite.gpu = False
        args.no_snapshots = True
    selected = [s for s in every
                if (not args.patterns or any(p in s.name for p in args.patterns))
                and not any(x in s.name for x in args.exclude)]
    if args.changed or args.changed_files:
        import affected
        selection = affected.select(args.changed_files or affected.changed_files(args.changed), every)
        if not args.no_snapshots and any(name in CHECKPOINT_SUITES for name in selection.reasons):
            # it makes (or finds) the save states those suites resume
            selection.reasons.setdefault("snapshot_equivalence", ["the checkpoint suites resume its save states"])
        affected.report(selection, len(every))
        if not selection.full:
            selected = [s for s in selected if s.name in selection.reasons]
    if args.list:
        for s in selected:
            print(f"{s.name:<45} {s.timeout:5.0f}s  {s.note}")
        return 0
    if not selected:
        if args.changed or args.changed_files:
            print("nothing to run for this change")
            return 0
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
    args.gpu_jobs = args.gpu_jobs or args.jobs
    print(f"{len(selected)} suites, {args.gpu_jobs} game ({args.heavy_jobs} heavy) and {args.cpu_jobs} other at a time"
          + (f", nice {args.nice}" if args.nice else "") + f"; logs in {OUT}/<suite>/output.log", flush=True)

    config = f"{args.arch}-{args.renderer}-{mode}"
    args.snapshot_dir = None
    if not args.no_snapshots:
        args.snapshot_dir = snapshot_cache(config)
        if any(s.name == "snapshot_equivalence" for s in selected):
            (args.snapshot_dir / "building").write_text("run.py: snapshot_equivalence is making the cache\n")
        print(f"save states of the test checkpoints: {os.path.relpath(args.snapshot_dir, ROOT)} "
              "(--no-snapshots boots every suite)")
    else:
        print("save states off: every suite boots")

    results, lock = {}, threading.Lock()
    start = time.monotonic()
    timings = OUT / "durations.json"
    order = schedule(selected, timings, config)
    try:
        cooled = run_lanes(order, args, lambda suite, index: run_suite(suite, index, args, mode, results, lock))
    finally:
        if args.snapshot_dir:
            (args.snapshot_dir / "building").unlink(missing_ok=True)
    total = time.monotonic() - start
    record_durations(timings, config, results)

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
          + f" in {total:.0f}s ({mode}, {args.renderer}, -j {args.gpu_jobs} + {args.cpu_jobs}"
          + (f"; {cooled:.0f}s waiting for the CPU to cool" if cooled else "") + ")")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
