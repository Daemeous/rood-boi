# Battle flip: hook inventory, inert profile, checklist

Groundwork for the flip (PORTING.md §12 and §20 items 6 and 7, D4;
`../../plans/2026-10-16-ownership-design.md` §10 steps 6 and 7). Nothing here
deletes a hook or changes a routing default. It was written from the sources
at commit `9a402e6`, without building or running the game; the human-side
machine does the runs in section 5.

1. [What the inventory covers and how to read it](#1-the-inventory)
2. [The features](#2-the-features): one row per PC feature, with its setting, its inert value, whether it touches game state and its fate
3. [The inert profile](#3-the-inert-profile): what exists, what was added, what is left open
4. [The other classes](#4-guards-lp64-asm-and-the-rest): what stays in `src/`
5. [The flip checklist](#5-the-flip-checklist)
6. [Open questions](#6-open-questions)
7. [Appendix: every block](#appendix-every-block)

## 1. The inventory

**Scope.** Every preprocessor block in `src/BATTLE/BATTLE.PRG/*.c`/`.h` whose
condition names `ROOD_PC*` (218 blocks: one per `#ifdef`/`#ifndef`, the
`#else` arm belongs to the same row; the 218 are exactly the lines of `src/BATTLE`
that mention `ROOD_PC`), and the 38 in `src/SLUS_010.40/` (`main.c` 30,
`25AC.c` 8). `PC_CLASSIC`, `PC_HUD`,
`PC_ANCHOR_PACKETS`, `PC_GTE_UNNARROWED` and the other hook macros only exist inside
`ROOD_PC` blocks (checked: no `pc_*`/`PC_*` name occurs in `src/BATTLE` outside
them, except the `PC_TEXT_STATUS` wrapper calls in `2842C.c`, which sit beside
the `#define` they depend on and are covered by that row). The C text of the `pc/src/*.inc` files compiled into the units
(`charge_skip.inc`, `scene_submit.inc`, `actor_commands.inc`, `room_shadows.inc`,
`room_callbacks.inc`, 41 functions) and the generated `event_callbacks.inc` /
`effect_callbacks.inc` is listed in the sweep rows 257 to 283 of the appendix; in the main
rows each `#include` is one row. `INITBTL.PRG` and `MAINMENU.PRG` are swept in rows 284 to 314
(the other overlays stay out of scope as in `battle/README.md`).

**Classes.** `F` PC feature (a call into `pc/src` or a changed behaviour that a
setting, a request or a renderer switch turns off); `S` safety guard (reads of PS1
address 0, empty actor slots, `static` made out of a stack array, fail-loud
checks, zeroing of stack residue the PS1 left undefined: the oracle cannot
run on a PC without them); `A` replacement of PS1 assembly (`INCLUDE_ASM`,
`__asm__`, `.inc` replacements, calling-convention adaptations of asm callees);
`L` LP64 adaptation (guest pointers as 32-bit words, `psx_get`/`psx_set`,
`ROOD_PC_LP64`); `O` other (header includes, diagnostics builds
`ROOD_PC_NO_AUDIO` / `ROOD_PC_BOOT_DIAGNOSTIC`, generated glue, SPU register
sync). PORTING.md §12 has four kinds; `O` is for blocks that fit none.

**Columns (appendix).** `state`: whether the block reads or writes anything
shadow mode compares (`yes`, or what), so that removal needs the tick-mode
check of PORTING.md §12 (M0 plan Task 28 pattern: owned code with the feature
on equals the original with its hook); blank means no. `Zig`: whether the
owned port already repeats it, found by searching the owning module in
`game/battle/` for the hook's symbols (`yes`; `no` = function owned but the
symbol absent; `n/p` = function not ported; `decl` = a declaration or macro,
the rows of its uses count; `-` = not a feature). For `S` and `L` rows the
column is blank: whether a Zig port repeats a guard is not decided by a text
search and stays for the shadow runs. It is a search, not a proof. `fate`: what
happens at the flip.

**Counts** (blocks, 256 in all):

| Class | BATTLE | SLUS_010.40 | Total |
|---|---:|---:|---:|
| F: PC feature | 119 (+1 mixed `A+F`) | 4 | 124 |
| S: safety guard | 54 | 4 | 58 |
| A: asm replacement | 20 | 7 | 27 |
| L: LP64 | 15 | 1 | 16 |
| O: other | 9 | 22 | 31 |
| **Total** | **218** | **38** | **256** |

(`A+F` is the `146C.c` block that includes `scene_submit.inc` (asm
replacement of `func_8008AC78`) and `charge_skip.inc` together.) Of the 22
`O` rows in SLUS, 18 are the `ROOD_PC_NO_AUDIO` and `ROOD_PC_BOOT_DIAGNOSTIC`
diagnostic builds (`build_boot.py --no-audio`; the normal build defines
neither); they are not hooks and stay.

**Sweep of 2026-11-05** (appendix rows 257 to 314, 58 rows; lines of `386f31f2b`, not of
`9a402e6`): `.inc` files 27 rows (F 18, A 9), `MAINMENU.PRG` 28 blocks (F 27, O 1),
`INITBTL.PRG` 0 blocks (3 rows for its unguarded `psx_get`/`psx_set`, class L). Two new features,
F24 and F25. The 256-block table above is unchanged.

The 119 + 1 + 4 feature blocks belong to 22 features (F21, the SPU
register sync, ended up as `O`). Battle-called SLUS functions and the sound driver are listed because
battle reaches them (`vs_main_playSfx`, `vs_main_processPadState`,
`pc_cd_queue_load`); the title/boot ones (`vs_main_execTitle`, the disc
diagnostics) are listed for completeness and are outside the battle flip.

## 2. The features

`Setting` is the key in `project_settings.cpp`; `inert` is the value after
the flip. "(new pin)" means the existing `classic` profile did not pin it and
the `inert` profile now does. `State` names what a hook touches that shadow
mode compares. `Tick check` is the PORTING.md §12 rule: owned code with the
feature on must equal the original with its hook, before the hook is deleted.

| id | Feature | Blocks | Setting → inert value | State | Tick check | Zig repeats it | Fate |
|---|---|---:|---|---|---|---|---|
| F1 | Charging skip (`charge_skip.inc`, `pc_charge_skip*`, `pc_cd_queue_load`) | 7 + 4 (.inc) | `qol_skip_charging` 0; `qol_skip_charging_who` 0 (new pin); `_name`/`_hold_*` only matter when on | yes: RNG pin (`pc_charge_skip_reseed`), CD queue, loop `break`s in `vs_battle_exec`, name-window holds, the five file statics (compared, §14) | **yes** (PORTING.md names it) | `_hold` in `func_80070278`, `_name`/`pc_hud_name_window` in `scene_message.zig`; **not** `pc_charge_skip(4/5)`, `_name(0)` in `vs_battle_exec` (not ported); the 15 functions of `charge_skip.inc` are already in `charge_skip.zig` (routed `host = true`) | becomes native owned code behind the same settings (PORTING.md §12); statics move to a C definition reached by `psx.global`; delete the hooks after the check |
| F2 | Right-stick camera: `tick`, `turn`, `place`, `first_person` (+ F2b `pc_camera_stick_menu` in `vs_battle_exec`) | 4 + 1 | `camera_stick` 0 (the default is **1**), `camera_stick_zoom` 0, `camera_stick_keys` 0, `camera_stick_menus` none (new pin) | yes: the follow camera's yaw/zoom, a copy placed before `_setCameraPositionFromAngles`, first-person look steps | **yes** | all four in `func_8006E158`, `set_camera_position_from_angles`, `func_8006EC7C`; `_menu` not (in `vs_battle_exec`) | delete the C blocks after the check |
| F3 | First-person Y invert (`FP_INVERT_Y`) | 1 | `qol_fp_invert_y` 0; the `#else` already defines it as the constant 0 | yes (input to the camera) | yes (the constant 0 is the original) | `func_8006EC7C` reads it | delete: the Zig keeps `pc_qol_fp_invert_y`; at the flip the constant 0 |
| F4 | Fast travel (`pc_fast_travel_begin`, `_menu_bits`, `MAGIC_COMMAND_MASK`) | 3 + 1 (MAINMENU) | `qol_fast_travel` 0 (the default is **1**), `qol_fast_travel_prompt` 0 | yes: state flag `0xB7`, MP/magicUsed, menu bits | **yes** | `func_800735F8`, `func_800C930C`; the `MAGIC_COMMAND_MASK` macro in `vs_battle_renderMenuItem` has its call site in the same ports | delete after the check |
| F5 | Weapon slots (`pc_weapon_slots_process_pad`, `_label`) | 4 | `qol_weapon_slots` 0 | yes: swaps equipment, rewrites the pad word | **yes** | `_label` in `func_800C9FE8`; `process_pad` is in `vs_battle_exec` (not ported) | port with `vs_battle_exec`, check, delete |
| F6 | Modern menus (`pc_ui_menu_*`) | 4 + 4 (MAINMENU) | `ui_menus` 0 | yes: menu state (`D_800F4CC0`), the cast-cancel path | **yes** | `func_800C930C`, `func_800C97BC`, `func_800CAB40` | delete after the check. `menu_bridge.c` also suppresses itself under `ROOD_ORACLE` (`pc_oracle_classic`), a second line of defence |
| F7 | Room warp (editor Room Browser: `pc_room_warp_*`) | 3 | no setting: inert while no warp is requested (`pc_room_warp_poll` returns 0); the runner never requests | yes (starts a room load; enemies/scripts optional) | yes, with a warp requested | `func_80089DC0` has two; `poll` is in `vs_battle_exec` | **open (Q3)**: editor feature, no inert setting |
| F8 | Story boot / cutscene viewer (`pc_story_frame`, `pc_story_title_exit`) | 2 | no setting: inert unless a story scene is requested | yes | yes, when requested | not ported (`vs_battle_exec`, `vs_main_execTitle`) | **open (Q3)** |
| F9 | Quit to title (`pc_quit_title_poll`, in `vs_main_processPadState`) | 1 | no setting: inert unless the option is used | yes (`vs_main_resetGame`) | yes, when used | SLUS boundary: stays C | **open (Q3)** |
| F10 | Editor live camera (`pc_live_camera`) | 1 | no setting: inert without the editor's free camera | no (works on a copy) | no | `func_8007ACB0` | delete C block |
| F11 | Lighting scene capture (`pc_scene_room`, `pc_scene_camera`; `scene_submit.inc` only calls `func_8008B1FC`, no capture text in it) | 1 | `environment` 0 / lighting off in `classic`; the capture is render-side | no, but host state outside the compared ranges: PORTING.md §20 oracle item 7 | no | `func_8008B1FC` | delete C block |
| F12 | Frame completion mark for the renderer (`pc_battle_frame_complete`) | 1 | none needed | no | no | `func_8007629C` | delete C block |
| F13 | Weather render hooks (`pc_weather_drop_packet`, `pc_weather_raining`, `WEATHER_LEFT/RIGHT`) | 4 | `environment` 0, `widescreen` 0 | no | no | `func_8008F440` (`pc_weather_raining` is a host function defined inside a game unit: it moves to `pc/src`) | delete C blocks; move `pc_weather_raining` |
| F14 | Classic / HUD layer (`PC_CLASSIC`, `PC_HUD`, `PC_ACTOR_WORD`, `pc_hud_name_window`, `pc_hud_panels_classic`, `pc_emote_record`) | 49 + 17 (MAINMENU) | no plugin claim honoured: `ui_menus` 0, `hud_classic_override` 0 and all 43 `hud_classic_*` (new pins) | capture only; emit-only suppression turns packets into no-ops; **`pc_emote_record` is a global in the game unit** written inside `func_80095C18` (compared) | yes if a claim is honoured, otherwise the packets are the game's | every block in an owned function (`pc_classic_site` and the rest, 45 of 49; 3 are declarations); `vs_battle_exec`'s status-panel block not yet | **open (Q1)**: the Zig keeps the call, the C text goes; `pc_emote_record` needs a home |
| F15 | Text typesetting (`pc_text_glyph`, `pc_text_status`, `pc_text_char`, `pc_classic_text_*`) | 3 + 5 (MAINMENU) | `ui_text` original (new pin), `button_prompts` original (new pin), `qol_confirm_cross` 0 (in `classic`: the glyph swap edits the OT packets) | packets, when the roles swap Cross and Circle | yes if `qol_confirm_cross` on | `print_variable_width_font_char.zig`, `func_800958A4`, `func_80095A4C` | delete C blocks |
| F16 | Vector talk bubbles (`pc_bubble_rect`, `pc_bubble_frame`) | 3 | `ui_bubbles` original (new pin) | no | no | `func_800CB83C`, `func_800CD3E4` | delete C blocks |
| F17 | Widescreen (`PC_ANCHOR_PACKETS`, `pc_hw_anchor_*`, `PC_GTE_UNNARROWED`, `PC_WIDESCREEN_MARGIN`) | 22 + 4 (.inc) | `widescreen` 0 (in `classic`) | GTE output when on; the `pc_gte_anchored` counter is host state, balanced | with the feature off the code equals the original (the corpus itself); on, there is no equivalence claim | the ports repeat it (the counter and the anchors) | delete C macros; Zig keeps the calls (`psx.weak`) |
| F18 | `pc_opening_image_ready` (test notification) | 1 | none | no | no | `func_800CCA90` | delete C block |
| F19 | Room checkpoint diagnostic (`pc_room_checkpoint_requested`) | 1 | none: inert unless requested | no (renders loaded geometry) | no | not ported (`vs_battle_exec`) | delete with `vs_battle_exec`'s port |
| F20 | RPC sfx counter (`pc_sfx_played`) | 1 | none | no | no | SLUS boundary | delete the hook |
| F22 | Hardware text fade (`_pcFadeBegin/Glyph/End`, `pc_hw_opacity_*`) | 6 | setting `opacity_2d` (`ROOD_OPACITY_2D` overrides): inert when `pc_hw_active` is 0 or `opacity_2d 0` (pinned by the inert profile) | VRAM: the text is not composited into the frame-buffer mirror; the four `_pcFade*` statics | yes if the hardware renderer runs the oracle | the ports repeat it | delete the C blocks and the statics after the check; **Q4 answered** |
| F23 | Opaque HP/MP/RISK label (`_renderStatBar`: sprite flag `0x80`, not `0x180`) | 1 | **no setting**: unconditional PC change | packets | yes | `render_stat_bar.zig` already has `0x80` | **open (Q2)** |
| F24 | Host state queries (`pc_battle_action`, `pc_battle_camera_focus`, `pc_battle_event_*`, `pc_battle_camera_target`: functions defined in `scene_submit.inc`, read by `dev_state.c`, `cutscene*.cpp`, `gameplay_walls.c`, `post_profile.cpp`) | 8 (.inc) | none: read-only, called by host code | no (reads game state, writes only the caller's buffers) | no | 8 of 8 in `scene_submit.zig` (`host = true`; the last four, `battleCameraFocus`, `battleEventThreads`, `battleEventArguments`, `battleEventWaiting`, ported 2026-11-05) | Zig carries them; delete the C copies |
| F25 | Native platform shadows (`pc_platform_shadow_native`, `pc_platform_shadows_end`, in `room_shadows.inc`) | 2 (.inc) | `native_shadows` 0 (already in the `classic` OFF list); the native path also needs `pc_hw_active` | no (the packets are built as the PS1 builds them; the native square is render-side) | no | `room_shadows.zig` (`psx.weak`) | delete the C calls when the `.inc` function is owned; Zig keeps them |

State-touching features that need the tick-mode check before their hook goes:
**F1, F2, F2b, F3, F4, F5, F6, F7, F8, F9, F15, F22, F23** (and F14 if a plugin
claim is honoured). F10 to F13, F16, F18, F19, F20, F24, F25 are render-side, read-only or tests.

## 3. The inert profile

**What exists.** `pc/tests/diff/corpus.py` pins the differential runner's
settings in a profile written to `settings.cfg` and handed to the game as
`ROOD_SETTINGS` (so a user's file never leaks in), together with
`ROOD_ORACLE=1`, `ROOD_DETERMINISTIC=1`, `ROOD_GI=0` and
`ROOD_OWNER=<subsystem>=original`. `OFF` lists the keys the `classic`
profile sets to 0 (widescreen, interpolation, the native renderers, lighting,
the camera stick, the QoL features, fast travel, skip intro, modern menus);
`features` is `classic` with the charging skip on. `differ.py --profile
classic|features` selects one; `pc/tests/run.py` registers each trial recipe in
both (`trial_corpus_*`, `trial_corpus_*_features`).

**What the inventory found missing.** Every key below gates a hook in owned
battle code and was not pinned, so a changed default or a plugin could have made a
corpus run non-inert:

| Pin (new) | Value | Gates |
|---|---|---|
| `ui_text` | `original` | F15 text typesetting |
| `ui_bubbles` | `original` | F16 vector bubbles |
| `button_prompts` | `original` | F15 button glyphs |
| `camera_stick_menus` | `none` | F2b right stick in the menus |
| `qol_skip_charging_who` | `0` | F1 caster set |
| `hud_classic_override` and the 42 `hud_classic_<kind>` | `0` | F14 no classic claim honoured, even with a HUD plugin loaded (they default to **1**) |
| `hud_label_opaque` | `0` | F23 stat-bar label drawn with the original's flag `0x180` |
| `opacity_2d` | `0` | F22 hardware text fade (`pc_hw_opacity_on`); a setting now, `ROOD_OPACITY_2D` only overrides it, so the environment pins nothing (`INERT_ENVIRONMENT` is empty) |

Defaults that are **on** in a game with no settings file, which is why the pins
matter (all but `hud_classic_*` already in `classic`'s `OFF`): `camera_stick`, `camera_stick_zoom`,
`camera_stick_keys`, `qol_fast_travel`, `qol_fast_travel_prompt`,
`native_room`, `interpolate`, `environment`, `fog`, `emissive`; and
`hud_classic_*`, which the new pins cover.

**What was added.** In `pc/tests/diff/corpus.py`: `INERT_PINS`, `HUD_CLASSIC_KINDS`,
`INERT` (= `PROFILE` + the pins), `INERT_ENVIRONMENT`, `PROFILE_ENVIRONMENT`;
`PROFILES["inert"]`; `environment()` merges the profile's environment variables
into the game's. `differ.py --profile inert` is the runner switch (its
`choices` come from `PROFILES`). Existing profiles are unchanged (their
`profile_sha256` in already-recorded corpora stays valid). Tests in
`pc/tests/diff/corpus_test.py` (class `InertProfile`, CPU only, no game files:
`python3 pc/tests/diff/corpus_test.py`):

- the profile is `classic` plus exactly `INERT_PINS`, no key twice, every `OFF` key still 0;
- every key of every profile exists in the settings reader (the entries table and `strcmp` keys of
  `project_settings.cpp`, `ui_text.c`, `button_prompts.c`): a typo or a renamed key fails;
- the 43 `hud_classic_*` keys of `project_settings.cpp` are all pinned, all 0: a new kind fails the test until pinned;
- the words (`original`, `none`) are the readers' own;
- `corpus.environment(..., "inert")` writes the file, points `ROOD_SETTINGS` at it, adds nothing to the environment
  (`INERT_ENVIRONMENT` is empty); `differ.main` accepts `--profile inert` and rejects an unknown one.

Not done by the tests: that the built game applies each line (it needs the
build; checklist step 3 reads the effective values back).

**Left unpinned on purpose.** Renderer-only settings that no battle hook reads and
that cannot change compared state (`ui_lines`, `ui_upscale`, `occlusion`,
`drop_shadows`, `gameplay_walls`, `aa`, `fog_*` detail, `dust`, `save_states`...); the
numeric detail of a feature that is off (`camera_stick_speed`, the fast-travel
prompt's look, the weapon-slot label tables); the editor's own settings. Features F7, F8,
F9, F19 and F20 have no setting: they are inert because nothing requests them in a
runner session (Q3).

**Proposals (not done: outside what this task may edit).**

1. `pc/tests/run.py`: register `trial_corpus_*_inert` beside `_features`
   (`for suffix, profile in (("", "classic"), ("_features", "features"), ("_inert", "inert"))`),
   and the same three entries in `NULL_SAFE`. After the flip the
   inert one is the default and `classic` is retired.
2. Per-feature profiles for the tick-mode checks (step 6): `FEATURES_ON` is
   one list; a check per feature needs the feature on alone with everything else inert.
   The keys that turn each one on: F1 `qol_skip_charging 1` + `qol_skip_charging_who 1`; F2
   `camera_stick 1` (+`camera_stick_zoom`, `camera_stick_keys`, `camera_stick_menus`); F3
   `qol_fp_invert_y 1`; F4 `qol_fast_travel 1` (+`_prompt`); F5 `qol_weapon_slots 1`; F6 `ui_menus 1`;
   F15 `qol_confirm_cross 1`. Their recipes must drive the feature's input (the recipes
   say "classic input only" today, `scenarios.py`).
3. If Q2 or Q4 end in a setting, its inert value goes into `INERT_PINS` in the same commit.

## 4. Guards, LP64, asm and the rest

These stay in `src/` at the flip (PORTING.md §12). Their rows are in the appendix with class `S`, `L`, `A`, `O`.

- **S (58).** (a) Zeroing of stack the PS1 left undefined and the C copies whole into compared state
  (`sp10 = (func_800CF0E8_t) { 0 }` and kin, 23 rows: `func_8006C4C4`, `func_8006F450`, `func_8007138C`,
  `func_800CF7A8`, `func_800D78F0`...); (b) PS1 reads of address 0 mapped to `0x80000000` or skipped
  when an actor slot is empty (`_removeActorAtIndex`, `func_8009DDF4`, `func_800A17BC`, `func_800A97EC`,
  `func_800AEF94`, `func_800D6AEC`...); (c) stack arrays made `static` so a returned pointer stays
  readable (`func_800A4A24` in `3A1A0.c` and `40564.c`); (d) fail-loud checks
  (`pc_require_native_callback`, `pc_unimplemented`); (e) four in SLUS (`25AC.c` NULL music config
  and LFO wave, the two `vs_main_playSfx` current-set tests). They "define where the oracle survives":
  the Zig ports must repeat each one that is in an owned function; the shadow runs report the
  ones they miss. The inventory does not verify guards (the `Zig` cell is blank).
- **L (16).** Guest pointers as 32-bit words (`psx_get`/`psx_set`/`psx_load`, `ROOD_PC_LP64`):
  `146C.c` `func_80074860`, `func_800775C0`, `func_8008A3A0`; `30DB0.c` `func_8009C378`;
  `44F14.c` `func_800AFDE8`; `4A0A8.c` `func_800BB874`; `5BF94.c` `vs_battle_renderMenuItem`,
  `func_800C7EBC`; `6F014.c`; `74088.c`; `58578.c:396` (PsyCross `gte_ldsxy3` takes pointers). The
  views give the Zig the same arithmetic (§5).
- **A (27).** Replacements of PS1 assembly: C bodies for `func_8006CB04`, `func_80074BAC`,
  `func_800E3600`, `func_800B9DB4`, `_fixedPointMult`, `vs_battle_memcpy` and the other `__asm__` blocks of
  `4A0A8.c`, `573B8.c`, `5BF94.c` (`_printFixedWidthFontChar`), the `.inc` replacements
  `actor_commands.inc` (`func_80075554`), `room_shadows.inc` (`func_8008CEB0`), `room_callbacks.inc`,
  and in SLUS `func_8001369C`, `func_80015BAC`, `vs_main_memcpy`, `vs_main_bzero`, the overlay-jump
  `setjmp` targets. They are the original on the PC.
- **O (31).** Header includes of the hook headers (delete with the last hook of their file),
  the generated `event_callbacks.inc` / `effect_callbacks.inc` includes (static wrappers, not routable;
  ported with their callers `func_800BF850`, `func_800D6D24`), the SPU register sync of `25AC.c` (host audio
  correctness), and 18 diagnostic-build blocks that the normal build does not compile.

## 5. The flip checklist

Run on the human-side machine (the game files and the build are there). Steps
1 and 2 are tooling and ports that must exist before anything is flipped; each
later step needs the one before. Commands are from the repository root.

**0. Baseline.** On the flip branch, `python3 pc/tools/port_progress.py --units` and
`--json` (today 1691 of 1693 pass-1 C functions ported, the 2 left are `get_D_800E84AC` and `func_800D7FB4`
(S); D2 161 of 184; 1901 functions routed in the 49 subsystems of `game/ownership.toml`, all
`default = "original"`; the tool's `host C` and `generated wrappers` lines (0 of 41, 0 of 43) count
Zig ports in `FUNCTIONS.tsv`, not routing: the `.inc` functions and the wrappers are routed); `python3 pc/tools/build_boot.py --arch 64`
and `--battle` build; `python3 pc/tests/run.py trial_corpus` is green in `classic` and
`features`. Keep the output: the flip is judged against it.

**1. Tooling that must land first** (each has its own test; none is a flip step):

1. *pc-inc routing.* **Done** (the `.inc` text is covered by the rename; every portable battle function is
   routed today). It was: `pc/tools/ownership.py` renamed definitions in the preprocessed text but the 41
   `pc-inc` functions (FUNCTIONS.tsv kind `pc-inc`: `actor_commands.inc` `func_80075554`,
   `room_shadows.inc` `func_8008CEB0`, `room_callbacks.inc`, `charge_skip.inc`) are routable only
   once the rename also covers the `.inc` text (PORTING.md §13; `ownership.py` has no `.inc`
   handling today). Needed by F1 (charge skip as owned code) and by the A rows of section 4 that
   are owned at the flip.
2. *Generated wrappers.* **Done** (routed today). It was: `event_callbacks.inc` / `effect_callbacks.inc` (43 static wrappers) could not be
   routed: the ports of their callers (`func_800BF850`, `func_800D6D24` units) must carry
   them (and `pc_require_native_callback` / `pc_unimplemented`, S rows). Check with
   `python3 pc/tools/port_progress.py --left 4A0A8` and `--left 5BF94`.
3. *`boot_sdk.c`.* 8 D2 rows live in a file `build_boot.py` does not compile as a unit
   (PORTING.md §20 D2 workflow): they wait for a change that does; they stay C through `c.*`
   and are not a blocker of the battle flip (Q6).
4. *File statics.* `python3 pc/tools/battle_statics.py` globalises the statics `STATE.tsv` lists
   (§6.5): the five `charge_skip.inc` variables, `146C.c`'s six file statics, `5BF94.c`'s four `_pcFade*`.
   F1 at the flip moves the skip's state to a C definition reached by `psx.global`; F22's statics go with
   its hook (step 8). Also `pc_emote_record` (F14, a plain global in `2842C.c` under `ROOD_PC`): it
   needs a definition outside the game unit before its `#ifdef` goes (Q1).
5. *`vs_battle_exec`* (`146C.c`, 569 lines). **Ported** (a root of `battle_146C_port`; the kill recipe does
   not call it at the root, see the step 0 result). It holds the hooks the
   Zig does not repeat yet: F1 `pc_charge_skip(4/5)`, `_name(0)`; F5 `process_pad`; F7 `poll`; F8
   `pc_story_frame`; F2b `camera_stick_menu`; F19 checkpoint; F14 the status-panel block. Port it with the
   settings available as owned code (`psx.weak` for each call, §12), shadow it, and only then flip.
6. *`func_800E5308`.* Stays C while its spec is pending (D2, `pc/src/actor_update.c:131`); the other
   unrouted functions (94: 44 `c` kind, mostly `static` helpers ported with their callers, and 50 D2)
   stay `c.*` calls and keep the flip honest: check
   `python3 pc/tools/port_progress.py --left <unit>` for each of the 23 units.

**2. Inert profile.** `python3 pc/tests/diff/corpus_test.py` (CPU only). Apply proposal 1 of section 3
(`run.py` `_inert` suites) and record the corpora with `--profile inert`:
`python3 pc/tests/diff/differ.py <subsystem> --profile inert --card <save>` per routed subsystem.
They are local-only artifacts under `pc/build/diff-corpus` (never commit them).

**3. Check the profile took effect.** Boot once with the inert environment (`corpus.environment(work,
"battle_146C", "inert")` gives it) and read the effective values back (`project_settings.cpp` has the
writer and the entries table that lists every key); each key of `INERT` must read as pinned. Any
that does not is a reader bug to fix before trusting a corpus.

**4. Shadow corpus over every routed battle function** (`differ.py --mode call`), with the root-call
gate (`waivers.toml`) and the corpus coverage of PORTING.md §20 item 5 (events, rooms and doors, cutscenes,
menus and HUD in BATTLE, effects, traps, loot). Fix mismatches; for each `S` row in an owned function,
confirm the shadow run reaches it (an empty slot, a freed timeline): a guard the corpus never hits is
unproven. `python3 pc/tests/run.py shadow_battle_smoke ownership_smoke`.

**5. Tick mode and alternation** (`--mode tick alternate`) in `inert`, on both architectures
(`build_boot.py --arch 64` and `--arch 32`), on the bisect order the 48 subsystems give.

**6. Tick-mode checks per state-touching hook** (before any is deleted): for each of F1, F2, F2b, F3,
F4, F5, F6, F7, F8, F9, F15, F22, F23 (and F14 with a claim honoured): run the recipe with that
feature on alone (proposal 2 of section 3): original with the hook (still in the C) against owned with
the feature on, tick mode. The hook must still be in `src/` for this, so this step precedes step 9.
F1 first: its hooks (`pc_charge_skip_reseed` and its RNG pin, the CD queue) are the case PORTING.md §12
names. Record each pass in this file's table (a `checked` column) or `battle/CORPUS.md`.

**7. Settle the open questions** Q1 to Q4 (section 6): F14's end state, F23's and F22's setting,
the settingless dev hooks F7, F8, F9, F19.

**8. Flip the default.** In `game/ownership.toml` set `default = "owned"` for the battle subsystems
(48 `[subsystem.battle_*]`/`d2_*` entries; one per unit so `ROOD_OWNER=<name>=original` still
bisects) and rebuild (`pc_subsystem_defaults[]` is generated from the table by `ownership.py`). Run
the whole suite: `python3 pc/tests/run.py` (and the `--changed` lane). Smoke the game on the
inert profile and with the user's settings (the features are now Zig behind their settings; the
settings must still work: `qol_skip_charging 1`, `camera_stick 1`, `ui_menus 1`, `widescreen 1`).

**9. Delete the feature hooks from `src/`**, rows with fate "delete..." in the appendix (not `S`,
`L`, `A`, `O`): F1 to F20, F22, F23 in `src/BATTLE/BATTLE.PRG/*.c` and `src/SLUS_010.40/main.c`,
with the PC header includes at the end (the last hook of a file), the `charge_skip.inc` /
`scene_submit.inc` coupling of `146C.c` (keep `scene_submit.inc`), `pc_emote_record` moved. One commit
per feature so a failure bisects. After it the original is the game without PC features: the
`features` profile no longer means anything on the original side (retire it) and the owned side
reaches the settings through its own code.

**10. Re-record and freeze.** Re-record the corpora with `inert` against the pruned original;
run the full suites on both architectures; update `battle/CORPUS.md`/`tests/diff/CORPUS.md` (the
classic and features profile text), PORTING.md §12 and the README tables; add `FLIP.md`'s status. From
then on each owned change is checked against the oracle with the settings at their inert values
(ownership design §10 step 8).

## 6. Open questions

- **Q1 (F14, classic / HUD layer).** The Zig ports keep calling `pc_classic_site` and
  `pc_hud_*` through `psx.weak`; only the C text is deleted. Is that the intended end state, or is the
  classic layer to be re-expressed as a setting the oracle pins? Today "inert" means *no plugin claim
  honoured* (`pc_hud_honoured`: `hud_classic_override` 0 and the per-kind switches, all pinned 0 now).
  Also `pc_emote_record` is a data definition inside `2842C.c`: where should it live after the block goes?
- **Q2 (F23).** `_renderStatBar` draws the HP/MP/RISK label opaque (flag `0x80`) on the PC with no
  setting; `render_stat_bar.zig` already does the same. At the flip the oracle's original is `0x180`.
  Either the Zig gets a setting whose original value draws `0x180` (and `INERT_PINS` pins it), or this
  stays an accepted difference, which the corpus will report.
- **Q3 (F7, F8, F9, F19: editor and viewer hooks).** Room warp, the story/cutscene viewer, quit to
  title and the room checkpoint diagnostic have no setting; they are inert because no runner session
  requests them. Delete them from `src/` at the flip, as written here, or keep them (they live in
  `vs_battle_exec`/`func_80089DC0`/`main.c`, and the editor would lose them if deleted without an owned
  replacement)?
- **Q4 (F22).** The hardware text fade is switched by the renderer (`pc_hw_active`) and `opacity_2d`
  (answered below: a setting, pinned 0 by the inert profile; `ROOD_OPACITY_2D` still overrides it).
- **Q5 (per-feature profiles).** Proposal 2 of section 3 needs recipes that press the feature's
  input; today's recipes use classic input only. Which features get a recipe (F1 has the `features`
  profile) and which are accepted on the shadow runs alone?
- **Q6 (`boot_sdk.c`).** The task lists it among the things that must land first. Read here as: the 8
  D2 rows in it wait for a unit-compiled build and do not block the battle flip. If it is meant to block it,
  step 1.3 moves to the front.
- **Q7 (the inventory's reach).** Done 2026-11-05: the `.inc` files, `MAINMENU.PRG` and `INITBTL.PRG` are
  swept in appendix rows 257 to 314 (F24 and F25 are new), with the findings after them.
- **Q8 (what the tests prove).** The `Zig` column is a text search; the `S`/`L` columns are blank;
  `state` is read from the code, not measured. The table is a work list for steps 4 to 6, not a result.

### Answers (2026-11-04)

- **Q1:** yes, the end state. The Zig ports keep the `pc_classic_site` / `pc_hud_*` calls through
  `psx.weak`; only the C text goes. `pc_emote_record` moves to `pc/src/hud_bridge.c`.
- **Q2:** a setting, `hud_label_opaque` (default on: the PC's opaque label; off draws the original
  `0x180`). `INERT_PINS` pins it off.
- **Q3:** keep them. Room warp, the story viewer and the room checkpoint stay as calls in the Zig ports
  (`func_80089DC0`, `vs_battle_exec`); only the C copies go. Quit to title stays in the SLUS C.
- **Q4:** a setting, `opacity_2d` (dev-tools panel and RPC); `ROOD_OPACITY_2D` stays as an override.
  `INERT_PINS` pins the setting.
- **Q5:** recipes only for features that change compared state: F1 (its `features` profile), room
  warp, and the HUD with a claim honoured. The rest are accepted on the shadow runs.
- **Q6:** `boot_sdk.c` does not block the flip.
- **Q7:** one sweep of the `.inc` files and the INITBTL/MAINMENU overlays before the flip, listing
  their hooks here.
- **Q8:** steps 4 to 6 fill the `S`/`L` columns from the shadow runs, so the table records results.
- **Also before the flip:** the two uninitialised reads (`skipCameraUpdate` in `vs_battle_exec`,
  `previousArea` in `func_800A5280`) take the value the compiled PS1 code leaves there (read from its
  assembly), and the Zig matches it.

**Done 2026-11-05:** Q1's `pc_emote_record` lives in `pc/src/hud/hud_bridge.c`; Q2's `hud_label_opaque`
and Q4's `opacity_2d` are settings (settings file, dev-tools panel, the `settings` RPC; `ROOD_OPACITY_2D`
still wins when set) pinned 0 in `INERT_PINS`, and `INERT_ENVIRONMENT` is empty; Q7's sweep is in the
appendix. The uninitialised reads are defined under `ROOD_PC` with the ports' values (`skipCameraUpdate`
0; `previousArea` the unchanged area, and func_800A5280's other locals 0), so their waivers are gone.
The four host functions the sweep found without Zig (`pc_battle_camera_focus`, `pc_battle_event_threads`,
`_arguments`, `_waiting`) now have host routes in `scene_submit.zig`.

**Flipped 2026-11-05 (lean flip: the three existing recipes, no new ones).** Every `battle_*`/`d2_*`
subsystem (49) is `default = "owned"`; `ROOD_OWNER=<name>=original` bisects one back. Before it, all 49
were recorded in `inert` on `enemy_cast_wait`, `action_name_window` and `enemy_kill`, and what they found
was fixed: the shadow report works while a routed call spans the RPC's vsync and lists up to 1024
functions (`coverage_dropped` past that); `func_800CF3F8`'s 0xE8-byte copy out of the 0xE4-byte
`func_8006C4C4_t` (a 4-byte over-read, both sides); the uninitialised SVECTOR pads
`_computeStepAndDisplacement` and `func_8007138C` hand the GTE; and `func_800863A4`'s `sp870[2]`, which
`func_800A1B9C` fills with three halfwords: on the host the third lands on the area's shape and `unk1`
(the PS1 frame has room for it). Both sides pin that overlap in an explicit struct; the PS1-faithful
layout changes enemy casts and is a gameplay change left for later. State compares are about 1.6x
faster (`state_diff.c` pools its buffers and skips equal pages).
The full suite then found three more, fixed before the merge: `removeActor`, `removeActorAtIndex` and
`insertActor` dereferenced a stale (misaligned) `next` to compare it (raw guest addresses now); the
development host exported only the owned library's strong imports, so weak hooks (`pc_live_camera`,
the photo camera) bound to null in owned code; and `vs_battle_exec` and `func_8007629C`, which hold the
frame across the vsync, made every snapshot, rewind and reload refuse while owned in the reloadable
library. Those two are the subsystem `battle_frame`, which stays `default = "original"`
(`ROOD_OWNER=battle_frame=owned` runs them as Zig) until the host can cut a stack through the library.
Still open, none a semantic mismatch:
- *Reach.* The three recipes call only part of most subsystems (146C_port 54 of 673 at the root), so
  their root-call gates stay red; ten subsystems are not reached at all. The rest of the 23 recipes of
  step 4 are the cover still owed.
- *Call-mode witness.* In call mode every recipe reports 0 mismatches but its witness fails (no
  ended cast, kill flags unset), as it did before the flip: a shadow-runner interaction to look at.
- *Steps 5 (32-bit) and 6 (per-feature tick checks)* were not run; step 6 must precede step 9, which
  deletes the hooks it compares against.

## Appendix: every block

Line numbers are those of `9a402e6`. Files are in `src/BATTLE/BATTLE.PRG/` except `main.c` and `25AC.c`
(`src/SLUS_010.40/`). `function` is the enclosing function (or the replaced function / the `.inc`). The
`state` column is blank where the block touches nothing the oracle compares. F-ids refer to section 2.

| # | file:lines | function | class | F | what | state | Zig | fate |
|---:|---|---|---|---|---|---|---|---|
| 1 | 146C.c:27-36 | (file scope) | O |  | includes and extern decls of the PC headers for the hooks in this file |  |  | keep |
| 2 | 146C.c:1006-1011 | _removeActorAtIndex | S |  | Empty PS1 slots must not become host null-pointer reads. |  |  | keep |
| 3 | 146C.c:1996-2000 | func_8006C4C4 | S |  | Only some fields are written; the whole request is copied into D_800F53C0/D_800F5230 by func_800CF3F8 | stack only |  | keep |
| 4 | 146C.c:2031-2035 | func_8006C5AC | S |  | sp20.pad is never written and vs_main_panSfx loads the whole word into the GTE | stack only |  | keep |
| 5 | 146C.c:2216-2295 | func_8006CB04 | A |  | func_8006CB04 in C (INCLUDE_ASM on the PS1) |  |  | keep |
| 6 | 146C.c:2800-2810 | func_8006E158 | F | F2 | Right-stick camera (pc/src/camera_stick.c): | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 7 | 146C.c:2822-2831 | func_8006E158 | F | F2 | From a yaw the right stick left between the 45-degree lines, R1 turns to the nearest line, as L1 does. | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 8 | 146C.c:3164-3169 | func_8006EBF8 | F | F3 | `extern int pc_qol_fp_invert_y;` | yes | decl | delete with its call sites |
| 9 | 146C.c:3218-3236 | func_8006EC7C | F | F2 | The right stick looks around (pc/src/camera_stick.c): | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 10 | 146C.c:3383-3387 | func_8006F450 | S |  | Only some fields are written; the whole request is copied into D_800F53C0/D_800F5230 by func_800CF3F8 | stack only |  | keep |
| 11 | 146C.c:3404-3408 | func_8006F4B0 | S |  | Only some fields are written; the whole request is copied into D_800F5230 by func_800CF3F8 | stack only |  | keep |
| 12 | 146C.c:3522-3526 | func_8006F848 | S |  | Only some fields are written; the whole request is copied into D_800F53C0/D_800F5230 by func_800CF3F8 | stack only |  | keep |
| 13 | 146C.c:3797-3800 | func_80070278 | F | F1 | A skipped wind-up's name window stays up a moment longer. | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 14 | 146C.c:4069-4075 | func_80070F28 | S |  | D_800F18EC is NULL until the ability menu first opens; |  |  | keep |
| 15 | 146C.c:4082-4084 | func_80070F28 | S |  | #undef pair of the func_80070F28 low-RAM guard |  |  | keep |
| 16 | 146C.c:4209-4221 | func_8007138C | S |  | The request is copied whole (func_800CF0E8: | stack only |  | keep |
| 17 | 146C.c:4919-4923 | func_800733F8 | S |  | Only some fields are written; the whole request is copied into D_800F53C0/D_800F5230 by func_800CF3F8 | stack only |  | keep |
| 18 | 146C.c:5011-5022 | func_800735F8 | F | F4 | Fast travel (pc/src/fast_travel.c): | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 19 | 146C.c:5231-5235 | func_80073D30 | S |  | Only some fields are written; the whole request is copied into D_800F53C0/D_800F5230 by func_800CF3F8 | stack only |  | keep |
| 20 | 146C.c:5587-5592 | func_80074860 | L |  | Passed as a 32-bit int: needs a guest-range (static) address. |  |  | keep |
| 21 | 146C.c:5694-5835 | func_80074BAC | A |  | func_80074BAC in C (INCLUDE_ASM on the PS1) |  |  | keep |
| 22 | 146C.c:5952-5956 | func_80075554 (actor_commands.inc) | A |  | `#include "pc/src/actor_commands.inc"` |  |  | keep |
| 23 | 146C.c:6022-6028 | func_8007629C | F | F14 | `{` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 24 | 146C.c:6030-6035 | func_8007629C | F | F12 | `{` |  | yes | delete the C block; Zig keeps the call |
| 25 | 146C.c:6497-6501 | func_800771E0 | S |  | Only dataType, index, modelId and actorId are written; the whole record is copied into a slot | stack only |  | keep |
| 26 | 146C.c:6697-6707 | func_800775C0 | L |  | D_800F188C is _zoneContext.zndEnemies (0x800F1880 + 0xC), a 4-byte guest pointer. |  |  | keep |
| 27 | 146C.c:7652-7657 | vs_battle_exec | F | F8 | The cutscene viewer's story scenes (pc/src/story_boot.c): | yes | n/p | port with vs_battle_exec (tick-mode check first) |
| 28 | 146C.c:7658-7665 | vs_battle_exec | F | F5 | QoL weapon slots (pc/src/weapon_slots.c, option qol_weapon_slots): the pad read, then the L2 + R2 swap. | yes | n/p | port with vs_battle_exec (tick-mode check first) |
| 29 | 146C.c:7682-7684 | vs_battle_exec | F | F1 | (a held name window closed in time) | yes | n/p | port with vs_battle_exec (tick-mode check first) |
| 30 | 146C.c:7702-7712 | vs_battle_exec | F | F7 | The editor's Room Browser: a requested warp starts here, as a door would (pc/src/room_warp.c). | yes | n/p | port with vs_battle_exec (tick-mode check first) |
| 31 | 146C.c:8032-8048 | vs_battle_exec | F | F2b | Right-stick camera in the menus that leave the scene in view (pc/src/camera_stick.c, camera_stick_menus): | yes | n/p | port with vs_battle_exec (tick-mode check first) |
| 32 | 146C.c:8085-8088 | vs_battle_exec | F | F1 | `if (pc_charge_skip(4))` | yes | n/p | port with vs_battle_exec (tick-mode check first) |
| 33 | 146C.c:8092-8095 | vs_battle_exec | F | F1 | `if (pc_charge_skip(5))` | yes | n/p | port with vs_battle_exec (tick-mode check first) |
| 34 | 146C.c:8147-8169 | vs_battle_exec | F | F19 | Explicit rendering diagnostic; normal battle logic is unchanged. |  | n/p | port with vs_battle_exec |
| 35 | 146C.c:8173-8188 | vs_battle_exec | F | F14 | Widescreen: the status display (HP, MP, RISK, Ashley's figure, status effects) is anchored to the left... | capture only; emote record is a game-unit global | n/p | port with vs_battle_exec |
| 36 | 146C.c:8276-8288 | _setCameraPositionFromAngles | F | F2 | Right-stick camera (pc/src/camera_stick.c): | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 37 | 146C.c:8353-8365 | func_8007ACB0 | F | F10 | Editor live camera: the view is built from the free camera's position and target instead; |  | yes | delete the C block; Zig keeps the call |
| 38 | 146C.c:8790-8794 | func_8007BE20 | S |  | Only some fields are written; the whole request is copied into D_800F53C0/D_800F5230 by func_800CF3F8 | stack only |  | keep |
| 39 | 146C.c:9377-9382 | func_8007D03C | A |  | calls the asm callee func_800E5154 with its $a0 argument |  |  | keep |
| 40 | 146C.c:13491-13497 | func_80085B10 | S |  | origin and position are read where no callee wrote them (unk40 != 0, or a failed lookup): | stack only |  | keep |
| 41 | 146C.c:13683-13688 | func_8008631C | S |  | Only the fields below are written: | stack only |  | keep |
| 42 | 146C.c:13713-13718 | func_800863A4 | S |  | Only the fields below are written: | stack only |  | keep |
| 43 | 146C.c:15649-15655 | func_80089DC0 | F | F7 | A warped-to room: enemies and scripts optional (pc/src/room_warp.c). | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 44 | 146C.c:15657-15663 | func_80089DC0 | F | F7 | Its enemies are spawned: a warp with enemies off benches them. | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 45 | 146C.c:15783-15793 | func_8008A3A0 | L |  | D_800F18E4 is _zoneContext.mpdEnemies (0x800F1880 + 0x64): |  |  | keep |
| 46 | 146C.c:16076-16081 | func_8008AC78 (scene_submit.inc, charge_skip.inc) | A+F | F1 | `#include "pc/src/scene_submit.inc"` | yes (charge skip statics) | - | keep scene_submit.inc; charge_skip.inc becomes owned code |
| 47 | 146C.c:16085-16093 | func_8008B1FC | F | F11 | Lighting capture: world-space room geometry and this frame's camera. |  | yes | delete the C block; Zig keeps the call |
| 48 | 146C.c:16630-16632 | func_8008C458 | A |  | func_8008C458 is defined by the PC C elsewhere; the PS1 wrapper is left out |  |  | keep |
| 49 | 146C.c:16849-16853 | func_8008CEB0 (room_shadows.inc) | A |  | `#include "pc/src/room_shadows.inc"` |  |  | keep |
| 50 | 146C.c:17692-17699 | func_8008EC48 | F | F17 | Widescreen: its tiles reach past the 4:3 screen's sides; they fill the 16:9 view's (pc/src/widescreen.h). | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 51 | 146C.c:17912-17915 | func_8008F440 | F | F13 | `pc_weather_drop_packet(` |  | yes | delete the C block; Zig keeps the call |
| 52 | 146C.c:17963-17966 | func_8008F440 | F | F13 | `pc_weather_drop_packet(` |  | yes | delete the C block; Zig keeps the call |
| 53 | 146C.c:17999-18005 | func_8008F9A4 | F | F13 | `#define WEATHER_LEFT (-PC_WIDESCREEN_MARGIN)` |  | decl | delete with its call sites |
| 54 | 146C.c:18041-18048 | func_8008FAC8 | F | F17 | Widescreen: a 2D quad built from two projected points (4:3, compressed like the rest of the 2D; | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 55 | 146C.c:18054-18059 | func_8008FAC8 | F | F17 | `PC_GTE_UNNARROWED(` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 56 | 146C.c:18129-18136 | func_8008FDC4 | F | F17 | Widescreen: tiles (2D) at the projected point. | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 57 | 146C.c:18195-18201 | func_8008FDC4 | F | F17 | `PC_GTE_UNNARROWED(depth = RotTransPers((SVECTOR*)particle,` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 58 | 2842C.c:10-13 | (file scope) | F | F17 | `#include "pc/src/widescreen.h"` | GTE output when on (pc_gte_anchored counter balanced when off) | decl | delete with its call sites |
| 59 | 2842C.c:340-348 | pc_weather_raining | F | F13 | The game's room weather, including event-script overrides. |  | n/a (host fn in the unit) | move to pc/src (a host function in a game unit) |
| 60 | 2842C.c:393-416 | func_80090EEC | F | F17 | Widescreen: snow and the falling glints are 2D at projected points, the dust motes 2D over the whole screen; | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 61 | 2842C.c:512-518 | func_80091468 | F | F17 | `#define SKY_LEFT (-0x40000 - (PC_WIDESCREEN_MARGIN << 12))` | GTE output when on (pc_gte_anchored counter balanced when off) | decl | delete with its call sites |
| 62 | 2842C.c:1738-1747 | func_8009406C | F | F14 | PC_CLASSIC: a word over an actor (MISS or a combat label) for the classic layer (classic_hook.h): | capture only; emote record is a game-unit global | decl | delete with its call sites |
| 63 | 2842C.c:1799-1804 | func_800941FC | F | F17 | `PC_GTE_UNNARROWED(` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 64 | 2842C.c:1818-1823 | func_800941FC | F | F17 | `PC_GTE_UNNARROWED(` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 65 | 2842C.c:1837-1841 | func_800941FC | F | F14 | `PC_ACTOR_WORD(1, func_8009506C(screen[0], screen[1], label));` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 66 | 2842C.c:1843-1849 | func_800941FC | F | F14 | PC_HUD: a plugin may draw the number instead (hud_hook.h). | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 67 | 2842C.c:1875-1879 | func_800941FC | F | F14 | `PC_ACTOR_WORD(1, func_8009506C(screen[0], screen[1], label));` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 68 | 2842C.c:1881-1885 | func_800941FC | F | F14 | `PC_ACTOR_WORD(0, func_80095258(screen[0], screen[1], label));` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 69 | 2842C.c:1904-1908 | func_800941FC | F | F14 | `PC_ACTOR_WORD(1, func_8009506C(screen[0], screen[1], label));` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 70 | 2842C.c:1916-1923 | func_800941FC | F | F14 | Widescreen: beside the status display, at the left edge. | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 71 | 2842C.c:1931-1936 | func_800941FC | F | F14 | `PC_HUD(PC_HUD_CHAIN, label, 44, 64,` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 72 | 2842C.c:1947-1952 | func_800941FC | F | F14 | `PC_HUD(PC_HUD_BUFF, label, 8, 172,` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 73 | 2842C.c:2299-2311 | func_800957D0 | F | F15 | ui_text font (pc/src/ui_text.c): the status words' packets, set as words. | packets only (glyph swap edits the OT packets) | decl | delete with its call sites |
| 74 | 2842C.c:2384-2389 | func_80095B7C | F | F14 | PC_CLASSIC: what func_80095C18 decided, as it decides it, for the classic layer (classic_hook.h). | capture only; emote record is a game-unit global | decl | delete with its call sites |
| 75 | 2842C.c:2433-2438 | func_80095C18 | F | F14 | `pc_emote_record.draw = 1, pc_emote_record.kind = kind,` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 76 | 2842C.c:2475-2478 | func_80095C18 | F | F14 | `pc_emote_record.draw = 2, pc_emote_record.direction = flags;` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 77 | 2842C.c:2483-2485 | func_80095C18 | F | F14 | `pc_emote_record.side = side;` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 78 | 2842C.c:2494-2496 | func_80095C18 | F | F14 | `pc_emote_record.side = side;` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 79 | 2842C.c:2540-2544 | func_80095C18 | F | F17 | `PC_GTE_UNNARROWED(RotTransPers(&other, (psx_long*)&position, &depth, &side));` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 80 | 2842C.c:2557-2561 | func_80095C18 | F | F14 | `pc_emote_record.draw = 3, pc_emote_record.kind = kind,` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 81 | 2842C.c:2585-2591 | func_8009627C | F | F14 | `pc_emote_record.draw = 0;` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 82 | 2842C.c:2896-2898 | room_callbacks.inc | A |  | `#include "pc/src/room_callbacks.inc"` |  |  | keep |
| 83 | 30DB0.c:17-19 | (file scope) | F | F17 | `#include "pc/src/widescreen.h"` | GTE output when on (pc_gte_anchored counter balanced when off) | decl | delete with its call sites |
| 84 | 30DB0.c:254-258 | func_80099854 | S |  | Only dataType, index, actorId and material are written; the whole record is copied into the slot | stack only |  | keep |
| 85 | 30DB0.c:1574-1579 | func_8009BE5C | S |  | Only dataType, index, actorId and material are written; the whole record is copied into a queue slot | stack only |  | keep |
| 86 | 30DB0.c:1661-1670 | func_8009C378 | L |  | Relocate the two guest pointers from arg1's copy to arg0's. |  |  | keep |
| 87 | 30DB0.c:2433-2440 | func_8009DDF4 | S |  | An effect can hit an empty actor slot: |  |  | keep |
| 88 | 30DB0.c:2759-2766 | func_8009E5C4 | S |  | The original reads animationId through empty slots (PS1 low RAM); |  |  | keep |
| 89 | 30DB0.c:3767-3771 | func_8009FFA8 | S |  | Falling off the end returns whatever the compiler left in the return register (-1 under GCC 13, -2 under... | stack only |  | keep |
| 90 | 30DB0.c:4375-4379 | func_800A1280 | F | F17 | `PC_GTE_UNNARROWED(arg2->vz = RotTransPers(arg2, &sp10, &sp14, &sp18));` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 91 | 38C1C.c:7-10 | (file scope) | F | F17 | `#include "pc/src/psx_types.h"` | GTE output when on (pc_gte_anchored counter balanced when off) | decl | delete with its call sites |
| 92 | 38C1C.c:38-60 | func_800A141C | A |  | SetRotMatrix/SetTransMatrix for the PS1 inline asm |  |  | keep |
| 93 | 38C1C.c:76-94 | func_800A141C | F | F17 | A screen position for 2D (labels, icons, sound): 4:3, not narrowed (pc/src/widescreen.h). | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 94 | 38C1C.c:206-212 | func_800A1720 | S |  | Empty slot: see func_800A17BC (PS1 address 0 at 0x80000000). |  |  | keep |
| 95 | 38C1C.c:226-238 | func_800A17BC | S |  | An effect can follow an actor slot that is empty (a room entered out of story order: |  |  | keep |
| 96 | 38C1C.c:286-292 | func_800A190C | S |  | An empty slot: the PS1 reads physical address 0 (as 0xFA below). |  |  | keep |
| 97 | 38C1C.c:300-308 | func_800A190C | S |  | Action 0 (func_80072BA8: misc item 357, an unnamed slot whose map entry is 0, listed as usable) frames... |  |  | keep |
| 98 | 38C1C.c:380-386 | func_800A1C10 | S |  | The original reads the model before its NULL check (PS1 low RAM); |  |  | keep |
| 99 | 3A1A0.c:347-353 | func_800A30A0 | S |  | No matching platform: the original reads through NULL, i.e. |  |  | keep |
| 100 | 3A1A0.c:665-671 | func_800A3C00 | S |  | func_8009F530 passes D_800F45E0[0] when no object stacks on the removed one; |  |  | keep |
| 101 | 3A1A0.c:1098-1104 | func_800A4A24 | S |  | The original returns a pointer into this stack array, which stays readable on PS1; |  |  | keep |
| 102 | 3A1A0.c:1117-1123 | func_800A4A24 | S |  | No matching platform: callers read through NULL, i.e. |  |  | keep |
| 103 | 3A1A0.c:2350-2359 | func_800A70DC | S |  | Off the floor func_800A8D64 returns NULL and the original reads PS1 address 0 (the host maps it at... |  |  | keep |
| 104 | 40564.c:351-356 | func_800A4A24 | S |  | static array so the returned pointer stays readable (see 3A1A0.c) |  |  | keep |
| 105 | 40564.c:368-373 | func_800A4A24 | S |  | No matching platform: see 3A1A0.c (reads of PS1 address 0). |  |  | keep |
| 106 | 40564.c:466-475 | func_800A97EC | S |  | A room entered out of story order can run an entry script that walks an enemy the room never spawned (its... |  |  | keep |
| 107 | 40564.c:576-581 | func_800A9988 | S |  | Empty actor slot: see func_800A97EC. |  |  | keep |
| 108 | 40564.c:697-704 | func_800A9D90 | S |  | An event can move an actor whose slot is empty. |  |  | keep |
| 109 | 44F14.c:499-518 | func_800AE47C | S |  | The animation's sequence can be gone: |  |  | keep |
| 110 | 44F14.c:789-796 | func_800AECA0 | S |  | `rotation.pad` and `rotation.t` are never written and the whole matrix is copied into the scratchpad... | stack only |  | keep |
| 111 | 44F14.c:819-827 | func_800AECA0 | S |  | The PS1 copies `source`'s home slot and 28 bytes of the caller's frame (no reader); | stack only |  | keep |
| 112 | 44F14.c:954-958 | func_800AEF94 | S |  | `if (*previous != NULL && (*previous)->unk6E6 == texture)` |  |  | keep |
| 113 | 44F14.c:979-998 | func_800AEF94 | S |  | Attached to another character's bone whose slot is empty (seen in a cutscene after the Minotaur): |  |  | keep |
| 114 | 44F14.c:1043-1048 | func_800AEF94 | S |  | `if (*previousAttached != NULL` |  |  | keep |
| 115 | 44F14.c:1386-1391 | func_800AFDE8 | L |  | The base is the animation's guest address (a 32-bit word). |  |  | keep |
| 116 | 44F14.c:1483-1487 | func_800AFDE8 | L |  | `data -= (u_int)(offset + *(u_int*)psx_get(scratch->data));` |  |  | keep |
| 117 | 4A0A8.c:338-589 | asm block (func_800B63AC...) | A |  | `__asm__(".set push;"` |  |  | keep |
| 118 | 4A0A8.c:2164-2203 | func_800B9DB4 | A |  | PC C for func_800B9DB4 (INCLUDE_ASM on the PS1) |  |  | keep |
| 119 | 4A0A8.c:2989-2994 | func_800BB874 | L |  | Scratchpad slots hold 4-byte guest pointers. |  |  | keep |
| 120 | 4A0A8.c:3010-3014 | func_800BB874 | L |  | `AddPrim(psx_load(void*, (u_int*)p + 2), var_s3++);` |  |  | keep |
| 121 | 4A0A8.c:3021-3027 | func_800BB874 | L |  | `AddPrim(psx_load(void*, (u_int*)p + 2), var_s3);` |  |  | keep |
| 122 | 4A0A8.c:4224-4237 | _fixedPointMult | A |  | _fixedPointMult in C (asm on the PS1) |  |  | keep |
| 123 | 4A0A8.c:4785-4787 | event_callbacks.inc | O |  | `#include "pc/build/boot/event_callbacks.inc"` |  |  | keep |
| 124 | 573B8.c:85-95 | func_800BFBB8 | S |  | `{` |  |  | keep |
| 125 | 573B8.c:232-256 | vs_battle_copyAligned... | A |  | PC: copy halfwords until the remaining size is a multiple of 8, then hand the rest to vs_battle_memcpy. |  |  | keep |
| 126 | 573B8.c:258-296 | vs_battle_memcpy | A |  | PC: copy 8 bytes per step as halfwords until the source reaches its end. |  |  | keep |
| 127 | 573B8.c:298-343 | vs_battle_setSprite* | A |  | `__asm__("glabel vs_battle_setSpriteDefault;"` |  |  | keep |
| 128 | 573B8.c:345-367 | func_800C02A8 | A |  | PC: load the scratchpad matrix (0x1F800014) as rotation and translation. |  |  | keep |
| 129 | 573B8.c:369-409 | vs_battle_playSfx* | A |  | PC: the shared tail plays menu sound `id` through vs_main_playSfxDefault. |  |  | keep |
| 130 | 573B8.c:506-516 | func_800C0700 | S |  | With the target shape freed (func_800C05B4) the original writes through NULL, i.e. |  |  | keep |
| 131 | 58578.c:13-16 | (file scope) | F | F17 | `#include "pc/src/classic/classic_hook.h"` | GTE output when on (pc_gte_anchored counter balanced when off) | decl | delete with its call sites |
| 132 | 58578.c:396-403 | func_800C1384 | L |  | PsyCross gte_ldsxy3 takes pointers (host SDK API difference) |  |  | keep |
| 133 | 58578.c:683-687 | func_800C1DC4 | F | F14 | `pc_classic_target_row pcRow;` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 134 | 58578.c:717-725 | func_800C1DC4 | F | F14 | PC_CLASSIC (classic_hook.h): | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 135 | 58578.c:751-755 | func_800C1DC4 | F | F14 | `if (pc_classic_site)` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 136 | 58578.c:761-766 | func_800C1DC4 | F | F17 | Widescreen: the target line starts at this point (2D). | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 137 | 58578.c:769-775 | func_800C1DC4 | F | F14 | PC_CLASSIC (classic_hook.h): the leader lines, from the projected point to the row, swept in by its age. | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 138 | 58578.c:790-794 | func_800C1DC4 | F | F14 | `if (pc_classic_site)` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 139 | 58578.c:1043-1046 | func_800C28AC | F | F14 | `pc_classic_target_marker pcMarker;` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 140 | 58578.c:1049-1054 | func_800C28AC | F | F17 | Widescreen: the target marker is 2D at this point. | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 141 | 58578.c:1058-1063 | func_800C28AC | F | F14 | PC_CLASSIC (classic_hook.h): the marker's place, lock and the step just taken. | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 142 | 58578.c:1101-1105 | func_800C28AC | F | F14 | `if (pc_classic_site)` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 143 | 5BF94.c:30-33 | (file scope) | F | F17 | `#include "pc/src/classic/classic_hook.h"` | GTE output when on (pc_gte_anchored counter balanced when off) | decl | delete with its call sites |
| 144 | 5BF94.c:1489-1506 | vs_battle_selectTextBox | F | F14 | ui_text_layout line (pc/src/ui_text.c): each character this printer sets. | capture only; emote record is a game-unit global | decl | delete with its call sites |
| 145 | 5BF94.c:1540-1543 | vs_battle_printVariableWidthFontChar | F | F14 | `if (pc_classic_text_char)` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 146 | 5BF94.c:1545-1547 | vs_battle_printVariableWidthFontChar | F | F15 | `pc_text_char(NULL, 0, -1, x, y, 6);` | packets only (glyph swap edits the OT packets) | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 147 | 5BF94.c:1559-1562 | vs_battle_printVariableWidthFontChar | F | F15 | `pc_text_char((void* const[]) { sprite + 2 }, 1, glyphId, x, y,` | packets only (glyph swap edits the OT packets) | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 148 | 5BF94.c:1845-1963 | _printFixedWidthFontChar | A |  | `__asm__("glabel _printFixedWidthFontChar;"` |  |  | keep |
| 149 | 5BF94.c:2086-2091 | func_800C7EBC | L |  | pixels is a stack buffer: plain byte arithmetic. |  |  | keep |
| 150 | 5BF94.c:2112-2157 | func_800C7EBC | F | F22 | _renderTextImmediate with the hardware renderer: | VRAM (text not composited into the frame-buffer mirror) | decl | delete with its call sites |
| 151 | 5BF94.c:2184-2188 | _renderTextImmediate | F | F22 | `buf = _pcFadeBegin(arg0) ? NULL : vs_main_allocHeapR(width * height * 2);` | VRAM (text not composited into the frame-buffer mirror) | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 152 | 5BF94.c:2195-2197 | _renderTextImmediate | F | F22 | skip the VRAM read-back when the text is drawn as glyph sprites | VRAM | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 153 | 5BF94.c:2210-2215 | _renderTextImmediate | F | F22 | `if (buf == NULL)` | VRAM (text not composited into the frame-buffer mirror) | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 154 | 5BF94.c:2220-2223 | _renderTextImmediate | F | F22 | `else if (buf == NULL)` | VRAM (text not composited into the frame-buffer mirror) | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 155 | 5BF94.c:2281-2286 | _renderTextImmediate | F | F22 | `if (buf == NULL) {` | VRAM (text not composited into the frame-buffer mirror) | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 156 | 5BF94.c:2759-2770 | vs_battle_renderMenuItem | L |  | `(void)v;` |  |  | keep |
| 157 | 5BF94.c:2777-2792 | vs_battle_renderMenuItem | F | F4 | Modern menus (pc/src/ui/menu_bridge.c): the shell replaces the top level when the ui_menus setting is 1. | yes | decl | delete with its call sites |
| 158 | 5BF94.c:2802-2807 | func_800C930C | F | F4 | `enabled \|= pc_fast_travel_menu_bits();` | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 159 | 5BF94.c:2818-2823 | func_800C930C | F | F6 | `if (pc_ui_menu_open(mode)) {` | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 160 | 5BF94.c:2963-2967 | func_800C97BC | F | F6 | `if (((var_s0 >= 0x1F) \|\| (temp_s1 == 0)) && !pc_ui_menu_hide_rows()) {` | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 161 | 5BF94.c:3061-3073 | _renderStatBar | F | F23 | HP/MP/RISK label drawn opaque (sprite flag 0x80 instead of 0x180): always on, no setting | packets (sprite flag) | yes (0x80) | needs a setting first (Q2) |
| 162 | 5BF94.c:3214-3218 | func_800C9F98 | F | F5 | QoL weapon slots (pc/src/weapon_slots.c): | yes | decl | delete with its call sites |
| 163 | 5BF94.c:3238-3240 | func_800C9FE8 | F | F14 | `pc_classic_ability_icons icons = { D_800EBCE8, temp_s3, 0, 0 };` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 164 | 5BF94.c:3243-3251 | func_800C9FE8 | F | F5 | This call may emit weapon-slot UI: leave it outside the classic range. | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 165 | 5BF94.c:3262-3264 | func_800C9FE8 | F | F14 | closes the PC_CLASSIC lambda of the ability icons | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 166 | 5BF94.c:3267-3271 | func_800C9FE8 | F | F14 | `icons.group = 8;` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 167 | 5BF94.c:3274-3276 | func_800C9FE8 | F | F14 | closes the PC_CLASSIC lambda of the ability icons | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 168 | 5BF94.c:3280-3287 | func_800C9FE8 | F | F5 | `temp_v0 = pc_weapon_slots_label(i);` | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 169 | 5BF94.c:3301-3303 | func_800C9FE8 | F | F14 | closes the PC_CLASSIC lambda of the ability icons | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 170 | 5BF94.c:3324-3338 | func_800CA2DC | F | F17 | Widescreen anchoring (the caller anchors this display to the left): | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 171 | 5BF94.c:3350-3356 | func_800CA2DC | F | F14 | `extern void pc_hw_anchor_packets(const void*, const void*, int)` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 172 | 5BF94.c:3360-3363 | func_800CA2DC | F | F17 | `if (pc_hw_anchor_packets)` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 173 | 5BF94.c:3368-3371 | func_800CA2DC | F | F14 | battle-mode notice: pulse snapshot for the classic layer | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 174 | 5BF94.c:3384-3386 | func_800CA2DC | F | F14 | `PC_CLASSIC(PC_CLASSIC_SITE_FIGURE, actor, D_800EB9CC & 1, 0, {` | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 175 | 5BF94.c:3397-3399 | func_800CA2DC | F | F14 | closes the PC_CLASSIC lambda of the figure | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 176 | 5BF94.c:3406-3429 | func_800CA2DC | F | F14 | PC_CLASSIC: the gauges, a plugin's in the classic look (pc/src/classic). | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 177 | 5BF94.c:3496-3502 | func_800CA2DC | F | F14 | Capture after the carousel's state steps, before drawing reuses k. | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 178 | 5BF94.c:3516-3520 | func_800CA2DC | F | F14 | break instead of return inside the status-icons PC_CLASSIC range | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 179 | 5BF94.c:3538-3541 | func_800CA2DC | F | F14 | closes the status-icons PC_CLASSIC range | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 180 | 5BF94.c:3585-3589 | func_800CA9C0 | F | F6 | Modern menus (pc/src/ui/menu_bridge.c): | yes | decl | delete with its call sites |
| 181 | 5BF94.c:3598-3605 | func_800CAB40 | F | F6 | No original screen to return to: | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 182 | 5BF94.c:3854-3858 | vs_battle_displaySceneMessage | F | F1 | A name window held past a skipped wind-up closed first. | yes | yes | delete the C block; Zig keeps the call (tick-mode check first) |
| 183 | 5BF94.c:3886-3893 | vs_battle_displaySceneMessage | F | F14 | HUD elements (pc/src/hud/hud_bridge.c): the name window opened. | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 184 | 5BF94.c:4095-4102 | func_800CB83C | F | F16 | Vector talk bubbles (pc/src/ui_bubbles.c): the unrounded rectangle. |  | yes | delete the C block; Zig keeps the call |
| 185 | 5BF94.c:4115-4120 | func_800CB83C | F | F14 | PC_CLASSIC: the box's band (classic_hook.h). | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 186 | 5BF94.c:4130-4135 | func_800CB83C | F | F14 | PC_CLASSIC: the box's frame (classic_hook.h). | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 187 | 5BF94.c:4139-4149 | func_800CB83C | F | F14 | PC_CLASSIC: the glyphs it sets (classic_hook.h); the typewriter stays the game's. | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 188 | 5BF94.c:4162-4168 | func_800CB83C | F | F14 | PC_CLASSIC: the time attack's timer (classic_hook.h); its string the game's. | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 189 | 5BF94.c:4190-4192 | func_800CBBCC | F | F17 | start of the packet range for the image anchor | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 190 | 5BF94.c:4289-4308 | func_800CBBCC | F | F17 | Widescreen: a still that covers the whole screen with picture (the idle cutscene's VKP Headquarters: | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 191 | 5BF94.c:4356-4360 | func_800CC204 | F | F17 | `extern int pc_hw_anchor_recorded(int) __attribute__((weak));` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 192 | 5BF94.c:4394-4409 | func_800CC204 | F | F17 | Widescreen: the wipe crosses the whole 16:9 view as it crosses the 4:3 screen (the opening's credits tear... | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 193 | 5BF94.c:4448-4461 | func_800CC204 | F | F17 | side tiles of the wipe in the widescreen margin | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 194 | 5BF94.c:4463-4467 | func_800CC204 | F | F17 | `if (pc_margin) {` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C block; Zig keeps the call |
| 195 | 5BF94.c:4580-4594 | func_800CCA90 | F | F14 | PC_CLASSIC: the image as the game draws it (classic_hook.h): which, its drawer, CLUT and slot. | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 196 | 5BF94.c:4595-4600 | func_800CCA90 | F | F18 | `if (arg0 == 0 && gim->unk3 == 0x80) {` |  | yes | delete the C block; Zig keeps the call |
| 197 | 5BF94.c:4674-4680 | vs_battle_drawCursor | F | F14 | PC_CLASSIC (classic_hook.h): | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 198 | 5BF94.c:4684-4686 | vs_battle_drawCursor | F | F14 | closes the cursor PC_CLASSIC lambda | capture only; emote record is a game-unit global | yes | delete the C block; Zig keeps the call |
| 199 | 5BF94.c:4907-4909 | func_800CD3E4 | F | F16 | start of the packet range for pc_bubble_frame |  | yes | delete the C block; Zig keeps the call |
| 200 | 5BF94.c:5056-5064 | func_800CD3E4 | F | F16 | Vector talk bubbles (pc/src/ui_bubbles.c): the frame's packets. |  | yes | delete the C block; Zig keeps the call |
| 201 | 5BF94.c:5847-5851 | func_800CF7A8 | S |  | Only some fields are written; the whole request is copied into D_800F53C0/D_800F5230 by func_800CF3F8 | stack only |  | keep |
| 202 | 5BF94.c:5887-5891 | func_800CF8BC | S |  | Only some fields are written; the whole request is copied into D_800F53C0/D_800F5230 by func_800CF3F8 | stack only |  | keep |
| 203 | 5BF94.c:7195-7199 | func_800D2ADC | S |  | `func_800D2904_t* node = NULL;` | stack only |  | keep |
| 204 | 5BF94.c:7744-7748 | func_800D2ADC | S |  | A zero emission count allocates no node. | stack only |  | keep |
| 205 | 5BF94.c:7922-7927 | func_800D4C18 | A |  | calls func_800D6E44 with the list (the PS1 wrapper forwarded $a0) |  |  | keep |
| 206 | 5BF94.c:8080-8091 | func_800D51D8 | S |  | `{` |  |  | keep |
| 207 | 5BF94.c:8302-8307 | func_800D5A98 | S |  | An earlier event of this update freed the timeline (see func_800D6AEC) |  |  | keep |
| 208 | 5BF94.c:8784-8788 | func_800D6AEC | S |  | `if (psx_get(arg0->unkD1C.unk30) == NULL) {` |  |  | keep |
| 209 | 5BF94.c:8792-8800 | func_800D6AEC | S |  | An event can end the effect and free its timeline (func_800D4EC0) while this loop runs; |  |  | keep |
| 210 | 5BF94.c:8861-8863 | func_800D6E24 | A |  | PS1 wrapper func_800D6E24 left out; the PC has func_800D6E44(list) |  |  | keep |
| 211 | 5BF94.c:8865-8869 | effect_callbacks.inc | O |  | `#include "pc/build/boot/effect_callbacks.inc"` |  |  | keep |
| 212 | 6E644.h:54-59 | (file scope) | A |  | prototype of func_800D6E44 with the list argument |  |  | keep |
| 213 | 6F014.c:297-299 | (file scope) | L |  | D_800F5798 lives in D_800F56C8.unkD0 (guest pointer) |  |  | keep |
| 214 | 6F014.c:321-329 | func_800D7814 | L |  | D_800F5798 is D_800F56C8.unkD0 (0x800F56C8 + 0xD0), a 4-byte guest pointer. |  |  | keep |
| 215 | 6F014.c:356-361 | func_800D78F0 | S |  | vs_battle_lerpVector writes vx..vz only, and func_800D5260 copies the whole VECTOR into D_800F5620: | stack only |  | keep |
| 216 | 74088.c:1219-1224 | func_800DEB10 | L |  | `func_800DCAA0_t* temp_a2 = psx_get(entry->unk430[i]);` |  |  | keep |
| 217 | 771A8.c:1066-1070 | func_800E1388 | S |  | Only x and z are written: y and status were stack residue, copied whole into state->destination | stack only |  | keep |
| 218 | 771A8.c:2239-2486 | func_800E3600 | A |  | func_800E3600 in C (INCLUDE_ASM on the PS1) |  |  | keep |
| 219 | 25AC.c:174-181 | func_80015970 | S |  | With no saved music config the original reads (and clears) fields through NULL, i.e. |  |  | keep |
| 220 | 25AC.c:182-190 | func_80015970 | S |  | A depth slide (opcodes DD/DE/DF) on a channel whose LFO was never enabled reads a NULL wave: |  |  | keep |
| 221 | 25AC.c:403-407 | func_800121F0 | L |  | `return var_v0 == (void*)-1 ? (u_long)-1 : psx_to_int(u_long, var_v0);` |  |  | keep |
| 222 | 25AC.c:1087-1193 | func_8001369C | A |  | func_8001369C in C (INCLUDE_ASM on the PS1) |  |  | keep |
| 223 | 25AC.c:1237-1244 | StopSound | O |  | SPU key on/off applied at once (pc_spu_sync_registers): host audio correctness |  |  | keep |
| 224 | 25AC.c:1250-1252 | SetVoiceKeyOn | O |  | SPU key on: sync the register page |  |  | keep |
| 225 | 25AC.c:1259-1261 | SetVoiceKeyOff | O |  | SPU key off: sync the register page |  |  | keep |
| 226 | 25AC.c:2504-2552 | func_80015BAC | A |  | func_80015BAC in C (INCLUDE_ASM on the PS1) |  |  | keep |
| 227 | main.c:8353-8361 | vs_main_execTitle | F | F8 | The cutscene viewer: New Game into a story scene (pc/src/story_boot.c). | yes | n/p | delete from src/ (function stays C) (tick-mode check first) |
| 228 | main.c:8366-8377 | vs_main_execTitle | A |  | setjmp target for vs_overlay_jumpToBattle (overlay jump on the host) |  |  | keep |
| 229 | main.c:8390-8396 | vs_main_exec | A |  | setjmp target for vs_overlay_jumpToTitle |  |  | keep |
| 230 | main.c:8903-8914 | vs_main_processPadState | F | F9 | Quit to title (pc/src/quit_options.c): the soft reset below, taken where it is taken. | yes | n/p | delete from src/ (function stays C) (tick-mode check first) |
| 231 | main.c:9190-9197 | _diskInitRead | O |  | `extern int pc_disc_read(int, unsigned int, void*);` |  |  | keep |
| 232 | main.c:9443-9448 | _diskReset | O |  | `vs_main_disk.state = diskIdle;` |  |  | keep |
| 233 | main.c:9554-9569 | pc_cd_queue_load | F | F1 | pc/src/charge_skip.inc: `slot` read now, as the queue would over the next two vertical blanks (the host's... | yes | n/p | delete from src/ (function stays C) (tick-mode check first) |
| 234 | main.c:9577-9579 | func_80044DD4 | O |  | `func_80012468(psx_to_ptr(void*, arg0));` |  |  | keep |
| 235 | main.c:9613-9617 | func_80044EC8 | O |  | `extern void pc_log(const char*);` |  |  | keep |
| 236 | main.c:9662-9666 | func_80045000 | O |  | `extern void pc_log(const char*);` |  |  | keep |
| 237 | main.c:9689-9693 | func_80045000 | O |  | keep an out-of-line func_80045000 for overlays (build_boot.py) |  |  | keep |
| 238 | main.c:9859-9861 | func_8004552C | O |  | `func_800128A0(vs_main_soundData.unk4, arg2, arg1);` |  |  | keep |
| 239 | main.c:9931-9933 | vs_main_playSfx | O |  | `return;` |  |  | keep |
| 240 | main.c:9961-9970 | vs_main_playSfx | S |  | No sound effect set current (none loaded, or freed): |  |  | keep |
| 241 | main.c:10006-10010 | vs_main_playSfxDefault | F | F20 | dev_rpc.c: counted for the smokes (RPC sfx) |  | n/p | delete from src/ (function stays C) |
| 242 | main.c:10099-10107 | func_80045B28 | S |  | No set current: as in vs_main_playSfx (the cutscene viewer). |  |  | keep |
| 243 | main.c:10122-10125 | func_80045BFC | O |  | Sound-effect volume/pan command; no sequencer runs in this diagnostic. |  |  | keep |
| 244 | main.c:10146-10149 | func_80045C74 | O |  | Sound-parameter command; the diagnostic has no active sequencer. |  |  | keep |
| 245 | main.c:10169-10172 | func_80045CEC | O |  | Sound-parameter command; the diagnostic has no active sequencer. |  |  | keep |
| 246 | main.c:10190-10193 | func_80045D64 | O |  | Stop-sound command; the diagnostic does not start a sound sequencer. |  |  | keep |
| 247 | main.c:10210-10212 | func_80045DC0 | O |  | `func_80012B98();` |  |  | keep |
| 248 | main.c:10325-10328 | vs_main_freeSfx | O |  | `func_80012288(-2, 0);` |  |  | keep |
| 249 | main.c:10349-10352 | func_80046194 | O |  | `func_80012288(-2, 0);` |  |  | keep |
| 250 | main.c:10390-10392 | func_800461CC | O |  | `func_800456EC(temp_s1, var_s0, arg3, arg4);` |  |  | keep |
| 251 | main.c:10468-10471 | func_8004644C | O |  | Positional stop-sound command; no sequencer is active in this diagnostic. |  |  | keep |
| 252 | main.c:10547-10551 | _loadSoundFile | O |  | `extern void pc_log(const char*);` |  |  | keep |
| 253 | main.c:10651-10656 | _loadMenuSound | O |  | `extern void pc_log(const char*);` |  |  | keep |
| 254 | main.c:11866-11868 | _asmNop | A |  | `__asm__(".nop;");` |  |  | keep |
| 255 | main.c:11879-11913 | vs_main_memcpy | A |  | vs_main_memcpy in C (asm on the PS1) |  |  | keep |
| 256 | main.c:11918-11934 | vs_main_bzero | A |  | vs_main_bzero in C (asm on the PS1) |  |  | keep |

### Sweep of 2026-11-05: the `.inc` files and the overlays

Rows 257 to 314. Lines are those of `386f31f2b`; `pc/build/boot/*.inc` are generated (`build_boot.py`, not
committed). `INITBTL.PRG` has no `ROOD_PC` block (the three `L` rows are its unguarded `psx_get`/`psx_set`
uses, listed for completeness; the main table counts only guarded blocks). `MAINMENU.PRG` has 28 (`C48.c` 11,
`58EC.c` 16, `58EC.h` 1). Neither overlay has a route in `game/ownership.toml` (it has the `battle` overlay
only), so no Zig repeats their hooks and the battle flip does not touch them: they stay as they are.

| # | file:lines | function | class | F | what | state | Zig | fate |
|---:|---|---|---|---|---|---|---|---|
| 257 | actor_commands.inc:13-365 | func_80075554 | A |  | C body of the asm function (11 `psx_get` uses inside) |  | yes (routed, `actor_commands.zig`) | keep; routable with pc-inc routing (§5 step 1.1) |
| 258 | scene_submit.inc:28-168 | func_8008AC78 | A |  | C body of the asm function (the area map and room submission; `psx_get`/`psx_set` throughout) |  | yes (routed, `scene_submit.func_8008AC78`) | keep; routable with pc-inc routing |
| 259 | scene_submit.inc:15-26 | anchor_map | F | F17 | `pc_hw_anchor_packets` (weak) right-anchors the area map's packets; externs and the helper | GTE output when on (pc_gte_anchored counter balanced when off) | yes (`scene_submit.anchorMap`, host route) | delete the C block; Zig keeps the call |
| 260 | scene_submit.inc:115-117 | func_8008AC78 | F | F17 | `pc_gte_anchored = 1` / `0` around `func_80098F8C` (map rooms without the GTE's Hor+ narrowing) | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C lines; Zig keeps them |
| 261 | scene_submit.inc:127 | func_8008AC78 | F | F17 | `anchor_map(mapPackets)` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C line; Zig keeps the call |
| 262 | scene_submit.inc:147 | func_8008AC78 | F | F17 | `anchor_map(mapPackets)` | GTE output when on (pc_gte_anchored counter balanced when off) | yes | delete the C line; Zig keeps the call |
| 263 | scene_submit.inc:170-196 | pc_battle_action | F | F24 | Host query for the interactive fog (`dev_state.c`): the running action, phase, chain, aim and target |  | yes (`scene_submit.battleAction`, `host = true`) | Zig carries it; delete the C copy |
| 264 | scene_submit.inc:198-233 | pc_battle_camera_focus | F | F24 | Host query for the depth of field's camera subject (`dev_state.c`, `post_profile.cpp`) |  | yes (`scene_submit.battleCameraFocus`, `host = true`) | Zig carries it; delete the C copy |
| 265 | scene_submit.inc:237 | pc_battle_event_running | F | F24 | `D_800F4C34 != 0` for the host (`post_profile.cpp`, `gameplay_walls.c`, `occlusion.c`, `hud/query_bridge.c`) |  | yes (`battleEventRunning`) | Zig carries it; delete the C copy |
| 266 | scene_submit.inc:242 | pc_battle_event_id | F | F24 | The running event's id (`cutscene.cpp`, `post_profile.cpp`) |  | yes (`battleEventId`) | Zig carries it; delete the C copy |
| 267 | scene_submit.inc:249-261 | pc_battle_event_threads | F | F24 | The event threads' state, offsets, opcodes, handlers (`cutscene.cpp`, weak caller) |  | yes (`scene_submit.battleEventThreads`, `host = true`) | Zig carries it; delete the C copy |
| 268 | scene_submit.inc:266-293 | pc_battle_event_arguments | F | F24 | One event thread's arguments, line and actor (`cutscene_sequencer.cpp`, weak caller) |  | yes (`scene_submit.battleEventArguments`, `host = true`) | Zig carries it; delete the C copy |
| 269 | scene_submit.inc:301-318 | pc_battle_event_waiting | F | F24 | The input mask the running event waits for (`cutscene.cpp`, weak caller) |  | yes (`scene_submit.battleEventWaiting`, `host = true`) | Zig carries it; delete the C copy |
| 270 | scene_submit.inc:321-330 | pc_battle_camera_target | F | F24 | The camera's target point (`gameplay_walls.c`, weak caller) |  | yes (`battleCameraTarget`) | Zig carries it; delete the C copy |
| 271 | room_shadows.inc:15-102 | func_8008CEB0 | A |  | C body of the asm function (moving platforms' shadows; `psx_get`/`psx_set` throughout) |  | yes (routed, `room_shadows.func_8008CEB0`) | keep; routable with pc-inc routing |
| 272 | room_shadows.inc:91-97 | func_8008CEB0 | F | F25 | `pc_platform_shadow_native` (weak) draws each square natively and returns the new packet end (the externs are at 12-14) |  | yes | delete the C block; Zig keeps the call |
| 273 | room_shadows.inc:100-101 | func_8008CEB0 | F | F25 | `pc_platform_shadows_end(start, end)` (weak) hands the finished packet range over |  | yes | delete the C block; Zig keeps the call |
| 274 | room_callbacks.inc:4-63 | (18 wrappers `pc_cb_*`) | A |  | Native versions of the room-effect callbacks, typed to the unit's structs |  | yes (routed, `room_callbacks.zig`, 18 entries) | keep; routed at the flip |
| 275 | room_callbacks.inc:65-87 | D_800E85CC, D_800E85E8 | A |  | The two callback tables, with the wrappers for the PS1's guest function words |  | n/a (data) | keep (data stays C) |
| 276 | event_callbacks.inc:2-12 (generated) | (11 wrappers `pc_event_*`) | A |  | Static wrappers of the event opcode handlers |  | yes (routed, `event_callbacks.zig`) | keep; ported with `func_800BF850`'s unit (§5 step 1.2) |
| 277 | event_callbacks.inc:13-270 (generated) | _opcodeFunctionTable[256] | A |  | The opcode table, built from the wrappers and `vs_battle_invalidOpcode` |  | n/a (data) | keep (data stays C) |
| 278 | effect_callbacks.inc:2-33 (generated) | (32 wrappers `pc_effect_*`) | A |  | Static wrappers of the effect node handlers |  | yes (routed, `effect_callbacks.zig`) | keep; ported with `func_800D6D24`'s unit (§5 step 1.2) |
| 279 | effect_callbacks.inc:34-85 (generated) | D_800EC3F4[49], D_800EC324[1] | A |  | The effect callback tables |  | n/a (data) | keep (data stays C) |
| 280 | charge_skip.inc:49-114 | charge_skip_hold_vsyncs, _kind, pc_charge_skip_state, charge_skip_now | F | F1 | The skip's decision: hold in vsyncs, the action's kind, whether this vsync is a wind-up and Skip applies; statics `charge_skip_from_set`, `charge_skip_from` | yes (the file statics) | yes (routed, `charge_skip.zig`) | Zig is the owned copy; drop the include when `vs_battle_exec` is ported (its `pc_charge_skip(4/5)` calls need it until then) |
| 281 | charge_skip.inc:115-182 | charge_skip_load, pc_charge_skip, pc_charge_skip_pump, _pumped | F | F1 | Runs the effect loader's steps back to back with the CD reads (`pc_cd_queue_load`), then the skip itself | yes (CD queue slot `D_800F568C`, loader state) | yes | as above |
| 282 | charge_skip.inc:183-238 | pc_charge_skip_hold, pc_charge_skip_name | F | F1 | Keeps the name window up past a skipped release; statics `charge_skip_held`, `_held_at`, `_held_vsyncs` | yes (name window, text box 7) | yes | as above |
| 283 | charge_skip.inc:239-328 | pc_charge_skip_name_window, _view, _battle_state, _reseed, _trace, _window, _ability | F | F1 | Host/RPC queries and the RNG pin (`_reseed` re-seeds `randArr`/`randIndex`) | yes (`_reseed`: RNG); the rest read only | yes | as above |
| 284 | src/MENU/MAINMENU.PRG/C48.c:36-48 | (file scope) | F | F4 | `classic_hook.h`, externs of `pc_ui_direct_tick`, `pc_ui_data_armed`, `pc_fast_travel_mask`; `MAGIC_TITLE_MASK` is `pc_fast_travel_mask(0xB7)` (else the constant `0xB7`) | yes (menu bits) | n/p (overlay stays C) | keep (overlay not part of the battle flip) |
| 285 | src/MENU/MAINMENU.PRG/C48.c:379-384 | vs_mainMenu_exec | F | F6 | A direct entry the modern menus take over (the loot) speaks the submenu's protocol: `pc_ui_direct_tick` | yes (`submenuReturn`, submenu state) | n/p | keep |
| 286 | src/MENU/MAINMENU.PRG/C48.c:432-439 | vs_mainMenu_exec | F | F6 | The native Data screen has its own header: no `MENU` command title (`pc_ui_data_armed`) | yes (menu command) | n/p | keep |
| 287 | src/MENU/MAINMENU.PRG/C48.c:446-450 | vs_mainMenu_exec | F | F6 | `targetPosition0 = 320` for the Data screen (the row slides off) | yes (menu item slide) | n/p | keep |
| 288 | src/MENU/MAINMENU.PRG/C48.c:496-503 | vs_mainMenu_exec | F | F6 | The modern menus handed a break art to the game and ended the menu: return when `currentMenu == 0x7F` | yes | n/p | keep |
| 289 | src/MENU/MAINMENU.PRG/C48.c:1009-1016 | vs_mainMenu_drawClassAffinityType | F | F14 | `PC_CLASSIC_SITE_STAT_PAGE` opens: the page's slide, page, subtype and a copy of the stats | capture only | n/p | keep |
| 290 | src/MENU/MAINMENU.PRG/C48.c:1018-1020 | vs_mainMenu_drawClassAffinityType | F | F14 | closes the stat-page `PC_CLASSIC` range | capture only | n/p | keep |
| 291 | src/MENU/MAINMENU.PRG/C48.c:1121-1136 | vs_mainMenu_renderEquipStats | F | F14 | `PC_CLASSIC_SITE_EQUIP_STATS` opens: the panel's place and the values it knows | capture only | n/p | keep |
| 292 | src/MENU/MAINMENU.PRG/C48.c:1187-1190 | vs_mainMenu_renderEquipStats | F | F14 | closes the equipment-stats range | capture only | n/p | keep |
| 293 | src/MENU/MAINMENU.PRG/C48.c:1245-1251 | vs_mainMenu_renderDpPpBars | F | F14 | `PC_CLASSIC_SITE_DP_PP_BARS` opens: the step, the target, DP and PP | capture only | n/p | keep |
| 294 | src/MENU/MAINMENU.PRG/C48.c:1293-1295 | vs_mainMenu_renderDpPpBars | F | F14 | closes the DP/PP bars range | capture only | n/p | keep |
| 295 | src/MENU/MAINMENU.PRG/58EC.c:10-12 | (file scope) | F | F14 | `#include "pc/src/classic/classic_hook.h"` | capture only | decl | keep |
| 296 | src/MENU/MAINMENU.PRG/58EC.c:441-447 | _renderMenuCommand | F | F14 | `PC_CLASSIC_SITE_MENU_COMMAND` opens: the title's sprite, from the action and its slide | capture only | n/p | keep |
| 297 | src/MENU/MAINMENU.PRG/58EC.c:456-459 | _renderMenuCommand | F | F14 | closes the menu-command range | capture only | n/p | keep |
| 298 | src/MENU/MAINMENU.PRG/58EC.c:517-521 | vs_mainMenu_renderCursor | F | F14 | The hand's record (step, place, colour) | capture only | n/p | keep |
| 299 | src/MENU/MAINMENU.PRG/58EC.c:535-539 | vs_mainMenu_renderCursor | F | F14 | `pc_classic_site(PC_CLASSIC_SITE_MENU_CURSOR, ...)` | capture only | n/p | keep |
| 300 | src/MENU/MAINMENU.PRG/58EC.c:929-942 | (after vs_mainMenu_getConfirmedRow) | F | F14 | `pc_classic_mainmenu_read`: a host function defined in the overlay that copies its file statics (row, page, sub menu, command) for `classic/menu_model.c` | reads file statics | n/p | keep; moves to a C definition outside the overlay if MAINMENU is ever ported |
| 301 | src/MENU/MAINMENU.PRG/58EC.c:1253-1264 | (file scope) | F | F15 | `pc_text_char` wrapper over `pc_text_glyph` (weak): each character the row printer sets | packets only | n/p | keep |
| 302 | src/MENU/MAINMENU.PRG/58EC.c:1276-1278 | vs_mainMenu_renderRowLabel | F | F15 | `void* strips[12]` collects the glyph's packets |  | n/p | keep |
| 303 | src/MENU/MAINMENU.PRG/58EC.c:1283-1285 | vs_mainMenu_renderRowLabel | F | F15 | `pc_text_char(NULL, 0, -1, ...)`: the space |  | n/p | keep |
| 304 | src/MENU/MAINMENU.PRG/58EC.c:1309-1311 | vs_mainMenu_renderRowLabel | F | F15 | `strips[i] = prim + 2` |  | n/p | keep |
| 305 | src/MENU/MAINMENU.PRG/58EC.c:1314-1317 | vs_mainMenu_renderRowLabel | F | F15 | `pc_text_char(strips, 12, glyph, ...)` | packets only | n/p | keep |
| 306 | src/MENU/MAINMENU.PRG/58EC.c:1350-1357 | vs_mainMenu_renderTabNavigation | F | F14 | `PC_CLASSIC_SITE_MENU_TABS` opens: the arrows (and L1 / R1), from the mode and the step | capture only | n/p | keep |
| 307 | src/MENU/MAINMENU.PRG/58EC.c:1377-1380 | vs_mainMenu_renderTabNavigation | F | F14 | closes the tabs range | capture only | n/p | keep |
| 308 | src/MENU/MAINMENU.PRG/58EC.c:1531-1536 | (before _renderMenuItem) | F | F14 | `pc_mainmenu_render_row`: the classic look's copy of a row, the row's emit on a copy of its record (called from `hud/hud_bridge.c`) | no (a copy) | n/p | keep |
| 309 | src/MENU/MAINMENU.PRG/58EC.c:1717-1738 | _renderMenuItems | F | F14 | `PC_CLASSIC_SITE_MENU_ROW`: the row's emit, from a copy of its record taken before the emit steps its gradient | capture only | n/p | keep |
| 310 | src/MENU/MAINMENU.PRG/58EC.c:1841-1850 | vs_mainMenu_renderScreen | F | F14 | `PC_CLASSIC_SITE_MENU_BACKGROUND`: the background, from its fade step and slot | capture only | n/p | keep |
| 311 | src/MENU/MAINMENU.PRG/58EC.h:382-384 | (file scope) | O |  | `extern _textHeaders[]` is left out on the PC (the `.c` defines it with its type) |  |  | keep |
| 312 | src/BATTLE/INITBTL.PRG/18.c:314 | func_800F9AD8 | L |  | `psx_get(...unk3C)`: unguarded LP64 macro use (the codemod; INITBTL has no `ROOD_PC` block) |  | n/p (overlay stays C) | keep |
| 313 | src/BATTLE/INITBTL.PRG/18.c:408-456 | func_800F9CCC | L |  | `psx_get` of the character's actor pointer, 21 uses (unguarded LP64 macro) |  | n/p | keep |
| 314 | src/BATTLE/INITBTL.PRG/18.c:543-547 | func_800FA35C | L |  | `psx_set` of the zone context's five guest pointers (unguarded LP64 macro) |  | n/p | keep |

**What the sweep changes in the flip plan**

- `pc_battle_camera_focus`, `pc_battle_event_threads`, `_arguments` and `_waiting` (`scene_submit.inc`,
  F24) had no Zig: ported 2026-11-05 (`scene_submit.zig`, `host = true`), so the `.inc` text can go.
- `scene_submit.inc` stays included at the flip (§5 step 9): `func_8008AC78` is its asm replacement. Its F17
  lines (rows 259 to 262) are hooks inside that replacement; delete them with F17 or leave them, the
  Zig repeats them either way.
- All 15 functions of `charge_skip.inc` already have Zig (`charge_skip.zig`, routed `host = true`); only
  the C calls in `vs_battle_exec` and `pc_cd_queue_load` (main.c) remain for F1.
- F25 needs no pin beyond `native_shadows` 0 (already in `OFF`); with `pc_hw_active` 0 the hook returns
  the packet unchanged.
- `MAINMENU.PRG` hooks (F4, F6, F14, F15) are not battle state: the battle corpora do not run the menu, so no
  tick-mode check applies. The modern-menu ones suppress themselves under `ROOD_ORACLE`
  (`pc_oracle_classic`, `menu_bridge.c`); `pc_ui_data_armed`, `pc_ui_direct_tick` and `pc_fast_travel_mask`
  were not checked for an oracle suppression of their own (uncertain).
