# Cutscene player and story scene viewer

Watch any story cutscene as it plays in the story, with a video player's
controls; and, underneath, a developer tool to find graphical problems in
cutscenes: freeze a scene, step it frame by frame, go back to any earlier
frame and play it again exactly, capture clean frames, and hand a precise
frame to someone else. The instant replay does the same for a glitch seen
in ordinary play. Plans: `pc/docs/plans/2026-10-22-cutscene-viewer.md`
(the viewer, how the game starts its scenes, per-scene look design),
`pc/docs/plans/2026-10-22-cutscene-player.md` (the player).

Code: `pc/src/cutscene.cpp` (takes, recipes, story scenes, captures,
repros, RPC), `pc/src/cutscene_panel.cpp` (the window),
`pc/src/cutscene_sequencer.cpp` (event samples and scene lighting keys),
`pc/src/story_boot.c` (story scenes, game side), `pc/src/state_rewind.c`
(the marks), hooks in `input_record.c`, `boot_runtime.c`
(`func_800E45B4`), `dev_rpc.c` (poll), `boot_window.c` (pacing),
`scene_submit.inc` (the event interpreter's place, what it waits for),
`room_warp.c` / `room_catalog.c` (the room's door), and three `ROOD_PC`
hooks in src (`vs_battle_exec`'s frame, `vs_main_execTitle` after the
title, `vs_main_playSfx` with no sound set). Data:
`pc/assets/cutscenes/story.json` (the story's scenes),
`pc/assets/cutscenes/catalog.json` (the player's recipes),
`pc/assets/cutscenes/opcodes.json` (opcode names). Tools:
`pc/tools/story_scan.py`, `pc/tools/story_verify.py`,
`pc/tools/cutscene_scan.py`, `pc/tools/cutscene_opcodes.py`. Tests:
`pc/tests/cutscene_viewer_smoke.py`, `pc/tests/cutscene_player_smoke.py`,
`pc/tests/cutscene_freecam_smoke.py` (run.py `cutscene_viewer_smoke`,
`cutscene_player_smoke`, `cutscene_freecam_smoke`).

## Watching a scene

Open the **Cutscene** window: F1 > Window > Cutscene, or the instant replay
key (**F8** by default), which opens the editor on it from anywhere (RPC
`cutscene {action: "open"}` too).

**First time:** pick a scene in the chapter list, then press the green
**Play scene** button beside its name (double-click still works). Wait for
**Reaching scene** to finish. The status then shows **Playing** or **Paused**,
the exact frame and seconds; errors appear in the window, not only the log.
Transport and timeline editing are disabled until a take is loaded and while
it is busy seeking/loading. Drag the sequencer ruler and release to scrub.
For a different view, enable **Free camera**, then hold **RMB** over the game
viewport and use **WASD** to fly, **Q/E** down/up, mouse to look, wheel to
adjust speed, and **Shift** to boost; release RMB to use the editor again.
On a pad: left stick moves, right stick looks, **L2/R2** move down/up.
While free camera is on, the pad belongs exclusively to navigation rather
than scene selection or transport.

- The list: the story's scenes in story order, grouped by chapter (the
  opening, New Game from the title, first; the ending last); search by
  name, chapter or place; *Key* shows only the scenes the story turns on
  (marked \*), *Verified* only the scenes seen to start and play
  (`story_verify.py`; the others are greyed). The scanned room scenes of
  the player's catalog are under *More*.
- **Play scene** (or double-click, Enter, Cross): the scene starts as it plays
  in the story and plays at once. From the title it is New Game into the
  scene; from a game, the game starts afresh in the scene's room (as
  loading a save does). Your game is kept: **Back to the game** returns to
  it as it was when you first played a scene from it.
- The player: the sequencer ruler shows seconds and exact frames (the part
  already played replays exactly, dragging past it plays on to there),
  previous / next scene, restart, -5 s / +5 s, a frame back / forward,
  play / pause, speed 1 / 1/2 / 1/4, *Auto-advance dialogue* and its
  *Hold* (`cutscene_auto_advance`, `cutscene_dialogue_hold`; story scenes
  only: a box, or an end card, waits that long, then the button it waits
  for is pressed, as a logged input so replays stay exact), *Free camera*,
  *Hide HUD*, *Hide dialogue*,
  **Capture frame**, **Lighting** (pauses and opens scene-only lighting
  Details below the sequencer; it does not edit room lighting files).
- Keys and the pad while the window has focus (the pad is the viewer's
  then, not the game's): Space / Triangle play-pause, Left / Right (L2 /
  R2) a frame, Shift+Left / Right 5 s, Page Up / Down (L1 / R1) the
  previous / next scene, Home restart, d-pad up / down choose a scene and
  Cross plays it, Square captures.
- **Advanced**: the player's controls below (takes, marks, recipes, the
  event script, captures, repros, the instant replay).

### Sequencer and scene lighting

The sequencer at the bottom of the window shares one time scale across all
tracks. Drag its ruler to preview a frame; release to seek exactly. Gold
ticks are rewind marks; the blue line is the playhead (or seek target).
**Visible frames** zooms, **Pan** moves the visible interval, and **Fit**
shows the full scene. The mouse wheel over the ruler zooms around the
pointer; middle-drag pans. Existing playback shortcuts still work.

Event tracks are grouped by script thread and category. Blue diamond keys
are interpreter-place samples observed as the scene plays, **not a complete
instruction execution trace**; unplayed portions have no event keys yet.
Hover a key for its opcode, event, script offset and available argument
bytes (and actor/line ids when known); click it to seek there and select its
Details. Missing arguments are labelled unavailable, never guessed.

Gold lighting keys are separate, authored scene overrides. To author one:

1. Scrub to a moment, press **Lighting** or **Lighting Details**, then
   **New key at playhead**.
2. Choose a **Property**, adjust **Value**, and choose step (the default) or
   **Linear interpolation**. **Key frame** can be edited explicitly.
3. Press **Add key**. Click a lighting diamond to seek and select it; edit
   its Details and press **Update key**, or **Delete key** to remove it.
4. **Save look** writes `pc/user/lighting/cutscenes/<scene id>.json`, in
   the existing lighting user layer (`ROOD_LIGHTS/cutscenes/` when configured).
   **Reload look** discards unsaved edits and reloads the saved look.

The scene lighting layer previews while playing, paused and scrubbing;
it is removed when leaving the take. It never writes the room's lighting
files. The available properties are the backend's supported numeric
lighting controls, not every future track in the design plan.

#### Override data and RPC

Shipped defaults are read from `pc/assets/cutscenes/overrides/<scene id>.json`.
A user-layer document replaces that scene's whole default document, including
an empty `keys` array to remove its defaults. Save never modifies shipped files.
The versioned format is, for example (illustrative values, not a shipped look):

```json
{
  "version": 1,
  "scene": "evt-0038",
  "keys": [
    {"id": 1, "frame": 120, "target": "light_intensity", "value": 0.5, "ease": "linear"},
    {"id": 2, "frame": 240, "target": "light_intensity", "value": 1.0, "ease": "step"}
  ]
}
```

Each key has a unique positive `id`; `(target, frame)` must also be unique.
Before a property's first key, the room's normal lighting is unchanged. From
that key onward, `linear` blends toward the next key; `step` holds until the
next key. The last value holds until the take ends. The Details property list
(and RPC `look` response) supplies supported target names and valid ranges.
No camera-override track or individual placed-light track is implemented yet.

RPC method `cutscene` supports:

- `{ "action": "tracks", "offset": 0, "limit": 100 }`: sampled keys in
  thread rows, plus `total` and `next_offset` for paging.
- `{ "action": "seek", "frame": 120 }`: the same exact seek as clicking a key.
- `{ "action": "look" }`: keys, available targets and resolved file paths.
- `{ "action": "look", "op": "set", "frame": 120,
  "target": "light_intensity", "value": 0.5, "ease": "linear" }`: add a key;
  include its `id` to update it.
- `{ "action": "look", "op": "delete", "id": 1 }`, or `op: "save"` /
  `op: "reload"`: remove, persist or reload scene keys.

### How a story scene starts

Story progress is state flag 130 (0 at the Wine Cellar after the
prologue, 83 before the end); scenes are room scripts, triggers' events
and EVT files scripts chain to (details in the plan). A scene in
`story.json` has the room the game starts in, the story flags just before
it (every other flag 0), the sound effect sets loaded before it, and how
it is reached:

- `entry`: the room's own script plays it on arrival;
- `trigger`: once the room's script is done, its trigger's event is
  started as stepping on the cell or examining the thing would;
- `chain`: the script before it (a room's or a trigger's) is played on,
  unpaced and with its dialogue answered, until the scene's own script
  runs; the take starts there.

`pc/tools/story_scan.py --apply` rebuilds the list from the game's scripts
(keeping names, chapters and notes by id); `pc/tools/story_verify.py
--apply` plays every scene and records `verified` and `frames`.

## A take

A take runs from its start frame (frame 0) to its *head*, the newest frame
played. While it runs:

- a **mark** (an in-memory checkpoint of the whole game state: guest RAM,
  scratchpad, I/O, the native game statics, overlays, VRAM, the game stack,
  the SPU, the interactive fog) is taken every N frames (default 30) into a
  ring of at most M marks (default 64) and a memory budget (default
  2048 MB). A full ring reuses the buffers of the mark it drops. The start
  mark and the newest stay; the others are thinned evenly;
- every input the game reads is **logged** per frame: the pad buttons, the
  camera stick and every value of the scanline estimate (the one host-clock
  input of game state: the AI reads it as a time budget).

Going to frame t rewinds to an earlier mark and steps there unpaced. When
available, a short replay lead-in rebuilds native renderer lists and targets
(the game-state marks do not contain them), avoiding a blank picture when t
is exactly a mark. Frames already played **replay** their logged input (the window
says REPLAY), so they are the frames played before, byte for byte (the
smoke test compares per-vsync state digests, the hashes and both pictures).
Past the head the game is **live** again and the take grows. *Cut here*
drops everything after the current frame (to try another branch).

Measured on the opening (64 bits): a mark is about 12.7 MB and costs about
2.5 ms once the ring reuses buffers (about 7 ms for a fresh one).

## Reaching a scene (the player's recipes)

`pc/assets/cutscenes/catalog.json` lists scenes by recipe (and
`story.json` the story's, above):

| recipe | from | how |
|---|---|---|
| `new_game` | the title menu, the cursor on New Game | presses Start: the opening |
| `warp` | any field room (a loaded game) | the Room Browser's warp with the room's scripts and enemies on; the take starts at the arrival, the entry event running |
| `ending` | a field room, no event running | the developer jump into ENDING.PRG |

plus **From here** (a take now, whatever runs) and **Start a take when an
event script starts** (any cutscene played into).

The 39 warp scenes come from `pc/tools/cutscene_scan.py`: from the test
card's save room it warps to every room with a door in (scripts on) and
follows the entry event, pressing Circle / Cross every 2 s when dialogue
waits. Of 364 rooms, 346 start an entry event, most of them a few frames
long (flags, actors placed); 39 run 300 frames or more and are catalogued
(31 played the same twice: `reliable`). Two rooms crashed the game in the
sound driver (`Sound_UpdateSlidesAndDelays`, 25AC.c:1677) on a warp with
scripts on: 15/0 Prisoners' Niche and 48/3 Underdark Fishmarket; they are
left out. Re-run: `python3 pc/tools/cutscene_scan.py --apply` (about 5
minutes; `--from-scan scan.json --apply --min-frames N` re-applies one).

Not reachable as recipes (the story scenes above are):

- story cutscenes that need the story's flags (a warp out of story order
  plays the room's entry script with the flags the card has: some scenes
  play differently or not at all; `notes` say what the scan saw);
- the movies (FMV: not game-state driven);
- a scene whose event waits for a choice the scan's presses do not make
  (the eight entries noted "did not end within the scan's cap": 29/0, 31/0,
  32/1, 32/3, 32/6, 32/17, 34/0, 40/21).

## Advanced controls

Under **Advanced** in the Cutscene window:

- Scene: the catalog (hover: where it starts from), **Start**, **From
  here**, *Start a take when an event script starts*, *Instant replay*.
- Take: the name, frame and REPLAY / LIVE; the **timeline** (drag to any
  frame; the marks are the ticks under it); **|<** first frame, **<M**
  previous mark, **<1** one frame back, **Play / Pause**, **1>** one
  frame forward, **M>** next mark, **>|** the head; **Speed** 1, 1/2, 1/4;
  *Interpolation* (off: every picture is a game frame); Go to frame; Cut
  here; Stop. Marks: interval, ring size, memory, instant replay seconds
  and interval (saved with File > Save settings: `cutscene_mark_interval`,
  `cutscene_marks_max`, `cutscene_budget_mb`, `instant_replay`,
  `instant_replay_seconds`, `instant_replay_interval`).
- Event script: the event id, its source (zone EVT / room entry / door) and
  file, the runner's state, skippable or skipping; for each live script
  thread its offset in the script buffer, the opcode there and its
  handler's name.
- Capture: **Capture frame**, **Capture next N frames**, *Hide dialogue
  too*, *Free camera* (the photo mode's camera on the held frame; off gives
  the game's camera back exactly).
- Repro: **Copy repro** (the path goes on the clipboard), a path field and
  **Open**.

The **instant replay** (`instant_replay 1`, off by default): a take that
always runs, keeping the last `instant_replay_seconds` (30) with a mark
every `instant_replay_interval` (60) frames: about 30 marks, 380 MB, a
2.5 ms copy once a second. Its key, `bind_key_replay` (default **F8**; F5
F6 F7 F9 are the save states, F12 the photo mode), freezes the game and
opens the window: go back to the glitch, step, capture, write a repro.
Playing on from there continues the buffer.

## Captures and repros

Captures go to `pc/user/cutscenes/<scene>/f<frame>.png` (gitignored;
`ROOD_CUTSCENES` moves the folder). A capture is the photo mode's: it holds
the frozen moment and redraws it with the HUD and this overlay hidden (and
the dialogue with *Hide dialogue too*), at the window's resolution with
post-processing. The redraw runs the game a frame or two from the moment
(then rewinds): the picture is that redraw's. Where the photo mode refuses
to hold (a room transition, loading, a card write) the picture is taken as
shown instead, and the capture says so.

**Copy repro** writes `repro-f<frame>.json` beside them: the scene, the
frame, a save state of the mark at or before it (`.snap`), the take's log
from there (`.log`) and the settings file's path and text. **Open** (or RPC
`cutscene {action: "jump", repro: path}`) loads the state in place, replays
the log and pauses on the frame. Save states are build-keyed: a repro opens
in the same build (64 bits: also after engine-only rebuilds).

## RPC

`cutscene {action, ...}`; the reply is the status (frame, head, marks,
replaying, the event script, the last capture, the repro, a message):

| action | params |
|---|---|
| `status` (default), `list` (the catalog and the story's scenes), `catalog` (reload both), `open` | `open`: `on` |
| `return` (Back to the game) | |
| `start` | `scene` (a catalog id) |
| `here`, `stop`, `play`, `pause`, `cut`, `hotkey` | `here`: `name` |
| `step` | `frames` |
| `speed` | `value`: 1, 2 (1/2) or 4 (1/4) |
| `seek` / `back` | `frame` / `frames` |
| `capture` | `count`, `hide_text` |
| `repro`, `jump` | `jump`: `repro` (path), `frame` (optional) |
| `settings` | `interval`, `marks_max`, `budget_mb`, `instant_replay`, `replay_seconds`, `replay_interval`, `armed`, `hide_text` |
| `camera` | `on`; a pose for the borrowed camera (as `rood.camera.set`): `position`, `target` ([x, y, z], world units, y down), `yaw`, `pitch` (degrees) |

Seeks, captures, repros, jumps and recipes finish at later vsyncs: wait
until `busy` is false. `start` takes a story scene's id too; the status's
`take_event` says what the event interpreter did in the take's live frames
(`frames` it ran, `dialogue` frames a box waited, script `places`, the
event `ids`). Also new: `room_goto {action: "reset"}` makes the
Room Browser's warp machinery idle (it is host state: an RPC rewind does
not undo it).

### What the free camera shows

The free camera draws the moment as the game built it, from another place:

- **The room is the game's set.** Story rooms are built for the game's
  cameras: one-sided walls (the PS1 culls their backs too), no outer shells,
  separate set pieces (the courtyard's close-up floor floats beside the
  manor in 2/0). Seen from elsewhere they show through, float and end in
  black; that is the room as it is (`photo_full_room` adds no geometry: in
  the opening every group is drawn by the game camera too). Groups an event
  script hides (flag 0x100) stay hidden.
- **The game's afterimage is the game camera's.** Shot dissolves and the
  credits' trails draw the previous frame (the other frame buffer) over the
  new one. From the free camera that frame is the game camera's picture, so
  the hardware renderer leaves those sprites out while a free camera flies
  (the game camera's view and native VRAM keep them): the free view is the
  moment itself, with no game shot smeared over it and no rain piling up
  render after render. The hall's credits card (f4200, "Art Direction /
  Character Design") is black behind its text from every camera, as the
  original: `cutscene_freecam_smoke` compares it with the native picture at
  the game camera, with the editor open and from the editor's and the
  viewer's free cameras.
- **Rain stays outside.** The game's rain is projected lines; the PS1's
  ordering table let a nearer wall cover a drop, the port draws them after
  the room's meshes, so a drop's packet is left out of the hardware picture
  (the game's state and native VRAM still draw it) when solid room geometry is above it or between the camera
  and it (`environment.cpp` `pc_weather_drop_hidden`; RPC `environment`
  `rain_probe`, `render_stats` `rain_drops*`).

## Limits

- Exact means the game state. The audio thread's mixing is not replayed;
  the renderer's temporal history (GI, denoisers, frame interpolation) is
  host state, so a replayed *picture* matches when those settle or are off
  (the native picture always matches; with GI off and interpolation off
  the presented picture matched too).
- Inputs outside the pad, the camera stick and the scanline estimate are
  not logged: Lua plugins that change game state, the modern menus (host
  UI), RPC memory writes. A replayed read the log does not hold counts as a
  divergence (shown in the window; 0 in every test).
- A warp scene's take starts at the arrival: the Room Browser's own
  bookkeeping when the entry event ends (the map status, the save point
  states put back) is host state and runs once, on the first pass.
- An engine library reload (`--reload`) ends the take (its log is engine
  state); loading a save state or the RPC's own rewind outside the take
  ends it too.
- Memory card writes are not undone (none happen in cutscenes).
- The overlay's key only works with the hardware renderers (the overlay is
  theirs); RPC `hotkey` works everywhere.
