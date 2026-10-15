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
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import rood_rpc  # noqa: E402

TESTS = ROOT / "pc/tests"
OUT = ROOT / "pc/build/tests"  # --out
PY = sys.executable
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
        Suite("lp64_alias_scan", [PY, str(TESTS / "lp64_alias_scan_test.py")], 60, binary=None,
              note="no data import is a field of another across the LP64 native move "
                   "(pc/tools/lp64_alias_scan.py on pc/build/boot64; synthetic cases always)"),
        Suite("snapshot_equivalence", [PY, str(TESTS / "snapshot_equivalence.py")]
              + (["--card", str(args.save)] if args.save.exists() else []), 600,
              note="save states: booting to each test checkpoint and resuming its snapshot give identical "
                   "game state and frames; makes the checkpoints' save states (first job)"),
        Suite("rpc_smoke", [PY, str(TESTS / "rpc_smoke.py")], 180),
        Suite("rpc_tcp_token_smoke", [PY, str(TESTS / "rpc_tcp_token_smoke.py")], 180,
              note="TCP RPC with a token: refused without it, with a wrong one, HTTP lines; never logged"),
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
        Suite("skip_intro_smoke", [PY, str(TESTS / "skip_intro_smoke.py")], 300,
              note="skip_intro 1: straight to the title (no logos, no movie), same title; off = the original start"),
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
            Suite("fog_sim_smoke", [PY, str(TESTS / "fog_sim_smoke.py"), "--card", str(args.save)], 300,
                  note="interactive fog prototype (sim=1, off by default): Ashley clears her footprint, a wake "
                       "when walking, settles back"),
            Suite("fog_volume_smoke", [PY, str(TESTS / "fog_volume_smoke.py"), "--card", str(args.save)], 420,
                  note="fog volumes' own renderer: thick (opacity >= 0.9), its own opacity cap, depth, a light "
                       "brightens and tints it, dim grey not black in a dark room, still frames steady (haze too)"),
            Suite("glow_stability_smoke", [PY, str(TESTS / "glow_stability_smoke.py"), "--card", str(args.save)], 600,
                  note="save point: one steady glow, stable id, overrides persist; prologue fight: effect glows unshadowed, smooth"),
            Suite("emissive_smoke", [PY, str(TESTS / "emissive_smoke.py"), "--card", str(args.save)], 420,
                  note="emissive texels: 0 is the picture without them (pixel-identical), marked panes glow and "
                       "nothing else changes, curated entries found in their rooms, editor mask saved and "
                       "reloaded; Vulkan: the windows light the wall (path traced)"),
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
                 note="DATA > LOAD slot images = disc data decode, on interpolated frames too (CLUT swaps)")]
          if args.save.exists() else []),
        *([Suite("weapon_slots_smoke", [PY, str(TESTS / "weapon_slots_smoke.py"), "--card", str(args.save)], 300,
                 note="QoL weapon slots: off = the game's L2 overlay; swaps, assign mode, shields, D-pad, drawn; "
                      "the mod's save bytes survive a save and reload")]
          if args.save.exists() else []),
        *([Suite("hud_label_solid_smoke", [PY, str(TESTS / "hud_label_solid_smoke.py"), "--card", str(args.save)],
                 300, note="HUD: the HP / MP labels drawn opaque like their digits (solid black outline, the field "
                           "not showing through), ui_text font and original")]
          if args.save.exists() else []),
        *([Suite("ui_menus_smoke", [PY, str(TESTS / "ui_menus_smoke.py"), "--card", str(args.save)], 300,
                 note="modern menus: off = the game's menu; shell tabs, original Status and back, "
                      "System > Menus from the next open, L2 shortcut stays original, greyed Break arts opens, close")]
          if args.save.exists() else []),
        *([Suite("menu0_levelled_smoke", [PY, str(TESTS / "menu0_levelled_smoke.py"), "--card", str(args.save)], 300,
                 note="original Magic menu, levelled Warlock spell: LV picker with '--' boxes (GPU sprite size "
                      "masked as on hardware), LV1 and LV2 cast: hand-off id, MP drops by the cost")]
          if args.save.exists() else []),
        *([Suite("skip_charging_smoke", [PY, str(TESTS / "skip_charging_smoke.py"), "--card", str(args.save)], 600,
                 note="battle charging off/fast/skip (paced): Solid Shock's and a Cure Root's wind-up charge the "
                      "same vsyncs in 4x / full speed wall time; damage, MP, item and game state identical")]
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
        *([Suite("ui_workshop_smoke", [PY, str(TESTS / "ui_workshop_smoke.py"), "--card", str(args.save)], 300,
                 note="Items > Workshop: same bag, indices and MENUC cursor memories as the original Setup screen "
                      "(MENUC / MENU8) for repair, gems, disassemble (equipped too), assemble, rename, combine x3")]
          if args.save.exists() else []),
        *([Suite("ui_data_smoke", [PY, str(TESTS / "ui_data_smoke.py"), "--card", str(args.save)], 600,
                 note="Data screen (MENU7 under a native frame): save bytes = the original's by pad and by mouse "
                      "(pinned keystream), one write, overwrite No / Yes, load = the original's blocks, errors "
                      "(no card, corrupt, full, blank, refused, unformatted), exits; player cards hashed unchanged")]
          if args.save.exists() else []),
        *([Suite("ui_system_smoke", [PY, str(TESTS / "ui_system_smoke.py"), "--card", str(args.save)], 300,
                 note="System screen through the shell: a row per settings page live + saved (debounce, Cross), "
                      "greyed rows, slider drag, binding capture (blocks navigation, Esc, pad, reset), Data, relaunch")]
          if args.save.exists() else []),
        Suite("system_settings_smoke", [PY, str(TESTS / "system_settings_smoke.py"),
                                        *(["--card", str(args.save)] if args.save.exists() else [])], 360,
              note="System settings: brightness, master volume (WAV), window mode, binding capture (SDL events; "
                   "key works in the field), saved and back after a relaunch with widescreen / perspective kept"),
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
    "probe_gi_smoke", "animation_smoke", "editor_cutaway_smoke", "editor_undo_smoke", "facing_cutaway_smoke",
    "camera_stick_smoke", "editor_snap_smoke", "room_browser_smoke", "route_continuation_smoke", "ending_smoke",
    "light_actors_smoke", "fixture_lighting_smoke", "sky_smoke", "glow_stability_smoke", "lighting_presets_smoke",
    "gi_lag_smoke", "texture_animation_smoke", "save_menu_smoke", "weapon_slots_smoke", "ui_menus_smoke",
    "ui_lines_smoke", "ui_text_smoke", "button_prompts_smoke", "coplanar_order_smoke", "atmosphere_smoke",
    "fog_sim_smoke", "fog_volume_smoke", "save_point_glow_smoke", "emissive_smoke", "ui_ashley_smoke", "ui_ashley_stability_smoke",
    "ui_equip_smoke",
    "ui_system_smoke", "ui_abilities_smoke", "ui_items_smoke", "menu0_levelled_smoke", "post_smoke",
    "transition_fade_smoke", "title_fade_smoke", "ui_loot_smoke", "ui_workshop_smoke", "window_mode_smoke",
    "ui_map_smoke", "ui_manual_smoke", "hud_label_solid_smoke", "damage_numbers_smoke", "platform_shadow_smoke",
    "ui_ashley_backdrop_smoke", "ui_data_smoke",
}


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
        ROOD_HEADLESS=mode,
        ROOD_BUTTON_PROMPTS="original",  # the PS1 glyphs (button_prompts_smoke opts in)
        ROOD_AUDIO=f"wav:{out / 'audio.wav'}" if suite.name.startswith("input:") else "none",
        PYTHONUNBUFFERED="1",
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
    start = time.monotonic()
    with log.open("w") as output:
        p = subprocess.Popen(command, cwd=ROOT, env=env, stdout=output, stderr=subprocess.STDOUT,
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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("patterns", nargs="*", help="run suites whose names contain any of these")
    parser.add_argument("-j", "--jobs", type=int, default=max(1, min(6, (os.cpu_count() or 4) // 4)),
                        help="suites at once (default: CPUs / 4, at most 6)")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--profile", action="store_true",
                        help="log every RPC call's time (<suite>/rpc-profile.txt; pc/tests/profile_report.py)")
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
    parser.add_argument("--no-snapshots", action="store_true",
                        default=os.environ.get("ROOD_TEST_SNAPSHOTS", "1") == "0",
                        help="boot every suite from scratch: no save states of the test checkpoints "
                             "(also ROOD_TEST_SNAPSHOTS=0)")
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
        with ThreadPoolExecutor(args.jobs) as pool:
            for index, suite in order:
                pool.submit(run_suite, suite, index, args, mode, results, lock)
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
          + f" in {total:.0f}s ({mode}, {args.renderer}, -j {args.jobs})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
