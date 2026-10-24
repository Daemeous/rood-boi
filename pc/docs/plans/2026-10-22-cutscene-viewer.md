# Story scene viewer (2026-10-22)

User feedback on the cutscene player (`2026-10-22-cutscene-player.md`):
picking a scene mostly warped to a room and played the room's entry script
out of story order, with the test card's flags, so most scenes did nothing;
and the window was built around takes, marks and recipes, not watching.

Goal: pick any story cutscene by chapter and name and it plays as it does
in the story (flags, actors, dialogue, camera), with video-player controls.
The follow-up sequencer implementation is on branch `cutscene-sequencer`.
The original design below remains useful background; the implemented override
schema and first-time workflow are in `pc/docs/Cutscene_Player.md`.

## Sequencer follow-up (2026-10-23)

Implemented:

- Prominent selection-side Play, disabled unloaded transport, visible errors,
  room-loading/reaching stages and playing/paused frame status.
- Free-flight controls independent of F1: RMB + WASD/QE, mouse look, wheel
  speed, Shift boost; pad movement/look and vertical triggers. Photo ownership
  restores the borrowed editor camera and redraws the original game view.
- Frame/seconds ruler, zoom, fractional middle-drag pan, seek-on-release,
  rewind marks and thread/category event keys. Known operand sizes come from
  the interpreter's table; dialogue displays line indices, not extracted text.
- Scene-only numeric lighting/atmosphere keys with step/linear interpolation,
  add/edit/delete Details, explicit save/reload, baseline restoration, and
  paged RPC event tracks. Shipped defaults are
  `pc/assets/cutscenes/overrides/<scene id>.json`; user edits are
  `pc/user/lighting/cutscenes/<scene id>.json` (or the configured lighting
  user directory). These replace the earlier proposed `looks/` / `look.json`
  layout below. No authored look values are shipped with this implementation.
- `cutscene_sequencer_smoke` exercises RPC scene start, sampled keys/seeking,
  lighting images and persistence, and real synthetic SDL camera controls.

Scope boundaries / remaining work:

- Event keys are per-vsync interpreter-place samples, not an exhaustive
  dispatch trace; instructions that start and finish between samples can be
  absent. Unplayed portions of a scene have no event keys yet.
- Actor references resolve only for verified handlers and are displayed as
  indices. Actor-name lookup and viewport actor highlighting remain open.
- Camera override keys/new-shot blending, individual placed-light tracks,
  shot-relative keys, and keyframe undo/redo remain future work.
- Final verification logs are in the worktree's ignored
  `pc/build/sequencer-verification/` and `pc/build/verify/` directories.

Final verification (2026-10-23):

- Renderer restoration now preserves GL/Vulkan presentation targets, native
  lists/meshes and the effective view. Checkpoints belong to camera sessions,
  not the whole photo pause; acquisition is outside budgeted Lua callbacks.
- Final focused runs on implementation `654335d9` passed **7/7 on OpenGL**
  and **7/7 on Vulkan** (`trigger-gl.log`, `trigger-vulkan.log`): sequencer,
  player, viewer, photo foundation, engine reload, mode and full-room suites.
  The sequencer asserts exact camera re-entry in both F1 states, no drift
  after input release, unchanged game hashes/editor state, and a visibly
  drawn original image restored without seeking (zero differing pixels).
- Follow-up: seeking now replays a renderer lead-in instead of landing on a
  game-state mark with missing native targets. The new smoke checks that its
  camera reference is visibly drawn, not a vacuous black-image comparison.
  The focused trace run confirmed a visibly drawn reference, zero differing
  pixels on restore, and identical original view matrices at every begin and
  restore. Pose drift occurred after begin: virtual trigger release used raw
  joystick midpoint rather than its signed released endpoint. This is fixed
  in `82082bbc`; the smoke now also checks no drift after controls release.
  Temporary trace instrumentation is removed.
- Final Linux 64/32-bit, Windows and CMake build sweep passed. An earlier
  standalone CTest run passed 67/67, before renderer checkpoint integration.
- The required `verify.sh --changed main -- -j 6` completed: **all passed**
  in 5632 seconds, using per-step build/heavy locks and nice 10.
  Null: **13/13 passed**. Vulkan: **150 passed, 2 skipped**.
  OpenGL: **149 passed, 3 skipped**. No failures.
  Vulkan skips: `gi_lag_smoke` (OpenGL-only), `golden_frames` (missing references).
  OpenGL skips: `rt_camera_smoke` and `nrd_smoke` (Vulkan-only),
  `golden_frames` (missing references). These skips are not claimed as tested.
- Work is parked on the local `cutscene-sequencer` branch, not merged or
  pushed. Compared with its starting commit `830d5908`, there are no changes
  to original `src/`, `pc/save`, `pc/user`, or `pc/lights`.

## How the game starts its scenes (research)

All in BATTLE.PRG (`src/BATTLE/BATTLE.PRG/4A0A8.c`, `146C.c`, `573B8.c`).

**The event runner.** One event runs at a time: `D_800F4C34` is its state
(0 none, 1 asked for, 2/3 loading, 4 running), `D_800F4BE0` its source and
`_evtFile` its number; `func_800BEB10` gives the id `(source << 12) | file`.
Sources: **0** an EVT file (`EVENT/nnnn.EVT`, read from the disc into the
script buffer `D_800F4C30`), **1** the room's own script (the MPD's script
section, copied into the buffer on loading), **2** a script of the room's
door section (`_mpdRoomDoorSection`, indexed). `func_800BF850` runs it each
frame: four script threads (`D_800F4C38`), ops dispatched through
`_opcodeFunctionTable`, lengths in `D_800E9C30` (BATTLE.PRG data).

**What starts an event** (`func_800BEC58(type, ...)`):

| type | when | caller |
|---|---|---|
| 1 | entering a room: the room's script (source 1) when it has one, else the trigger table's entry triggers | `func_80089DC0` (146C.c, room load) |
| 2 | Ashley steps onto a new cell (rooms with `unk0_13`) | the field's movement |
| 4 | Circle on an actor or object (examine) | the field loop |
| 8 (0xA, 0xC) | after a kill / a fall | the battle code |
| 0x10 | after a battle | `func_800738E4` |
| 0x20 | each field frame | the field loop |
| 0x40 | forced: the game over event (event 0) | `func_800792E4` |

The **trigger table** is the MPD section the decompilation calls
`_mpdClearedSection` (u16 offsets, at most 16, 0xFFFF ends; each entry: a
type mask, the event id (bit 15: a door-section script), then conditions
until 0xFF). Conditions: 1 Ashley on a cell, 2 in a region, 3 / 15 a state
flag compared with a value / another flag (0 <=, 0x10 >=, 0x20 ==, 0x30 !=,
0x40 <, 0x50 >), 4 an actor present, 5 / 7 / 12 values copied into flags,
6 two actors (who examined whom), 8 the examined object, 9 Ashley not busy,
10 an item carried, 11 an actor's cell, 13 / 14 an actor on a cell / in a
region, 16 an actor dead, 0xFE negates the next. 576 triggers in the
game's 300-odd scripted rooms, 52 distinct EVT files.

**Scripts hand on to each other.** Op 0xF7 (`func_800BB5AC`) chains: when
the script ends, EVT file N is loaded and runs (the event id becomes N).
Op 0x68 (`func_800B98C0`) changes room: with bit 2 of its 8th byte the
script goes on in the new room; without it the event ends as the new room
loads (`_loadMpdClearedScriptDoorSections`, `D_800F4BE2 == 3`) and the new
room's own script takes over. Most long scenes are a room script that
chains to one or more EVT files and moves through rooms.

**Story state.** `vs_main_stateFlags` (`main.h` `vs_main_flags_t`, 0x440
bytes) is the flag array the scripts test and set (ops 0x40 / 0x41 test, 0x50
/ 0x51 set: `vs_battle_getStateFlag` / `vs_battle_setStateFlag`). Bytes
16-31 are cleared per event (`func_800BF5F0`), 32-63 per room load (unless
continuing a save), 64-127 per zone load (`func_800BEB9C`); 128 and up are
kept (the save's). **Flag 130 (0x82) is the story step**: room scripts test
it (`if step == 46: chain EVT 38`) and every story scene sets it to the next
step; it runs from 0 (the Wine Cellar after the prologue) to 83. Others in
the story's way: 170 / 171 (a scene handing over to the next room's script),
174-190 (per-area progress), the key / sigil / chest / Circle bytes.

**Sound effect sets** (`vs_main_soundData`, three slots) are loaded by
scripts (op 0x85, wait 0x86, current 0x88, free 0x87) and stay loaded
across rooms; a later scene may play a set an earlier room loaded. A set
still marked loaded is not loaded again (`vs_main_loadSfxSlot`).

### The cleanest way to start a given scene

What loading a save does: the load menu (MENU7 260.c) fills
`vs_main_stateFlags` and the "last door" `D_80061068` (zone, room, cell,
facing), then `vs_main_jumpToBattle`: BATTLE starts afresh
(`func_800FA35C`) in that room, whose entry script runs with those flags.
The title's attract demos do the same with fixed rooms
(`_setTitleExitFlags` 3/4). So a story scene starts the same way:

1. **flags**: the story flags just before the scene (from story order:
   the persistent writes of every earlier scene; every other flag 0;
   bytes 0-1, difficulty and a cleared game, stay the player's);
2. **room**: its first room as the last door (a door into it, or a cell
   on its floor for rooms only events reach, as the Room Browser makes);
3. **start**: in the game, `vs_main_jumpToBattle` from the top of the field
   frame (after freeing the sound sets: their heap starts afresh); at the
   title, New Game with the flags and door applied as the title exits;
4. **sounds**: the sets loaded before it, loaded as the room loads;
5. then the room's **entry script plays the scene**; a trigger's scene is
   started once the entry script is done, as the trigger starts it
   (`_evtFile`, `D_800F4BE0`, `D_800F4C34 = 1`, `func_80073898`); a scene
   that is a script chained or handed over to is reached by playing its
   script on (unpaced, dialogue answered) until it runs in its room.

No save is needed: inventory and equipment are the game's (from New Game,
or the game played), which matters little for how a scene looks (Ashley's
gear shows on his model; a scene that checks an item is rare). The
player's game is kept: the first scene played from a game takes a save
state first, and **Back to the game** returns to it.

### The scene list (`pc/tools/story_scan.py` -> `pc/assets/cutscenes/story.json`)

The scan walks the scripts concretely (`walk`): from a new game's flags,
the prologue from zone 1 room 0 (New Game) through the rooms its scripts
move to; then for story step v = 0, 1, ...: every room's script and every
trigger whose flag conditions hold (tested after the room's entry script,
which sets room flags the triggers test) is walked with the flags as they
are: flag ops as the interpreter does them, jumps, counted loops, forked
threads, chains (0xF7) and room handovers (0x68) followed, sound sets
tracked. The scripts that move flag 130 on from v (the least far first)
are step v's scenes; their persistent writes make the next step's flags.
Each segment of a run (the first script, each EVT file chained to, each
room handed over to) is a scene:

```json
{"id": "evt-0038", "name": "...", "chapter": "...", "step": 45,
 "zone": 40, "room": 24,            // the room the game starts in
 "plays_in": [40, 24], "start": "chain",   // entry | trigger | chain
 "event": 38,                        // the scene's own event id
 "force": 8194,                      // a trigger's event started there (-1)
 "until": 38, "until_room": [40, 24], // played on to this event (-1)
 "parent": "evt-0078", "texts": 19, "sets_step": 47,
 "sound": [0, 0, 0, 0], "flags": {"130": 45, "170": 1, ...},
 "frames": 2600, "verified": true}
```

Ids are stable across rescans (`evt-NNNN`, `room-ZZZ-RR-sSSS`,
`door-ZZZ-RR-I-sSSS`); `--apply` keeps the hand-written fields (name,
chapter, major, notes, entry / cell, skip, verified, frames) by id. The
file holds ids, rooms, event numbers and flag indices only; the names
are ours. `pc/tools/story_verify.py` plays every scene in the game and
records `verified` / `frames`.

Limits of the walk: conditions other than flags (cells, actors, items)
are taken as met; a scene the story reaches by a flag other than 130
without moving it (an optional scene, a boss's death scene that sets no
step) is listed only when it is part of a run that does; FMV movies are
not scripts.

## The viewer (UI)

The Cutscene window's front page (cutscene_panel.cpp; F1 > Window >
Cutscene, or the instant replay key, F8 by default):

- the story's scenes grouped by chapter, with search and a "Verified"
  filter; the player catalog's scenes (opening, ending, scanned rooms)
  under "More";
- **Play** (or double-click, Enter, Cross on the pad): the scene starts as
  above and plays at once; **Back to the game**;
- the player: a scrub bar over the scene's length in minutes and seconds
  (the part played so far replays exactly from the marks; dragging past it
  plays on to there), previous / next scene, restart, -5 s / +5 s, one frame
  back / forward, play / pause, speed 1 / 1/2 / 1/4, **Auto-advance
  dialogue** (story scenes only: a box or an end card waits
  `cutscene_dialogue_hold` vsyncs, then the button it waits for is pressed:
  a logged input, so replays stay exact), free
  camera (the photo mode's), Hide HUD, Hide dialogue, Capture frame,
  **Lighting** (pauses and opens the Lighting tab, which edits the room the
  scene plays in);
- keys and the pad while the window has focus (the overlay never opens
  gamepads, so the viewer reads the pad itself and the game gets none
  then): Space / Triangle play-pause, Left / Right (L2 / R2) a frame,
  Shift+Left / Right 5 s, Page Up / Down (L1 / R1) previous / next scene,
  Home restart, d-pad up / down choose and Cross plays, Square captures;
- *Key* (the 32 scenes marked major, shown with \*) and *Verified* filters;
- **Advanced**: the cutscene player as it was (takes, marks, recipes, the
  event script's threads and opcodes, captures, repros, instant replay).

RPC `cutscene`: `list` gives the story fields too (chapter, step,
start_event, texts, verified, major); `start {scene}` plays a story scene;
`return` goes back to the game; the status has `take_event` (frames an
event ran in the take, the script places it was at, the event ids).

## Next: per-scene looks and the timeline (design)

**Keys.** A moment in a scene is `(scene id, frame)`: the scene id is
stable (above) and a take's frame 0 is where the scene starts (the arrival
or the event's start), so a frame is reproducible (the take replays
exactly). A *shot* is a span between camera cuts; the renderer already
detects cuts for interpolation (`f5315202`), so shots can be numbered per
scene and keyed `(scene id, shot)` or `(scene id, shot, frame in shot)`,
which survives small timing changes better than a raw frame.

**Overrides as data.** `pc/user/cutscenes/<scene id>/look.json` (authored,
the user's) and a shipped `pc/assets/cutscenes/looks/<scene id>.json`:

```json
{"scene": "evt-0038",
 "tracks": [
  {"what": "room_lighting", "keys": [{"shot": 0, "values": {"exposure": 0.2, "ambient": [..]}},
                                      {"frame": 840, "values": {...}, "ease": "linear"}]},
  {"what": "light", "id": "zone40_room24/torch_2", "keys": [{"frame": 0, "values": {"intensity": 0}}]},
  {"what": "fog", "keys": [...]}, {"what": "post", "keys": [...]}]}
```

Each track names an existing tunable: the room's lighting settings (the
Lighting tab's per-room values, `pc_room_settings_page("Lighting")`), a
placed light (`pc/lights/<room>.txt` entries), the outdoor / environment
values (`pc/assets/environment/outdoor.json`, `atmosphere.json`), fog
presets, post-processing. While a take of that scene plays, the override
layer applies the track values at the current frame on top of the room's
own (never written into the room's files), and drops them when the take
ends. Values are interpolated between keys (step or linear).

**Authoring.** In the viewer: scrub to a moment, open Lighting (the button
exists: it opens the room's tab today), change values; a "Key this" toggle
records the change into the scene's track at the current frame (or shot)
instead of the room. Scrubbing replays the scene exactly, so the result is
seen at once at any moment; the renderer's temporal history settles in a
few frames. Save writes `look.json`; RPC `cutscene {action: "look", ...}`
and a `rood.cutscene` Lua table give tools and plugins the same.

**The sequencer.** A timeline under the scrub bar with one row per event
script thread (the ops it ran per frame, from the take: dialogue boxes,
camera moves, actor moves, sound, room changes; the opcode names are in
`opcodes.json`) and, below them, the look override tracks with their keys.
The take already logs per frame what the scrubbing needs; the thread rows
need the interpreter's place per frame (the `take_event` places, kept per
frame instead of as a set).

## Status (2026-10-22)

- Research: done (above).
- `story_scan.py` / `story.json`: 94 scenes from the prologue (New Game) to
  step 83 (the light before the ending), in 9 chapters, named by us (32
  marked key). `story_verify.py` (a full run, 64 bits, headless, then the
  two fixes re-run): 84 start and play their event; 11 are scripts with
  nothing to watch (`skip`: not shown); 1 is listed but not verified
  (`door-009-11-0-s006`: a short door script, two lines). With the opening
  and ending recipes, 85 scenes show in the viewer.
- Engine: `story_boot.c` (game side: the boot, sound sets, a trigger's
  event), cutscene.cpp's story recipe (title or game, the follow of chained
  scripts, Back to the game), auto-advance of story scenes' dialogue, the
  take's `take_event`; three `ROOD_PC` src hooks, commit-check matching.
- Viewer UI: done (above). Entry points: F1 > Window > Cutscene, F8 (the
  instant replay key) from anywhere, RPC `cutscene {action: "open"}`. No
  Lua binding yet.
- Tests: `cutscene_viewer_smoke` (five scenes from five chapters from a
  fresh load, exact seeking, Back to the game); `cutscene_player_smoke`
  unchanged (the player's recipes, the instant replay).

Open gaps:

- Scenes the story reaches without moving flag 130 (optional scenes, a
  boss's death scene, Ashley's flashbacks gated by other flags) are listed
  only when part of a run that moves it.
- The walk takes non-flag conditions as met; a scene whose script depends
  on Ashley's position, an actor or an item may play a different branch.
- Sound effect sets are tracked from the scripts; where the walk misses a
  load, `vs_main_playSfx` is silent instead of reading a missing set
  (`ROOD_PC`).
- A trigger's scene starts once the room's own script is idle (60 frames)
  or when the room starts it itself; a room whose script never ends and
  never starts it would not get there (none left in the list).
- The opening (New Game) is only from the title (the player's recipe);
  scenes start from the title or a game, not from other overlays (menus,
  the ending).
- Inventory, equipment and HP are the game's (New Game's from the title).
