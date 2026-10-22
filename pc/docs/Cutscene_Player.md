# Cutscene player

A developer tool to find graphical problems in cutscenes: reach a scene,
freeze it, step it frame by frame, go back to any earlier frame and play it
again exactly, capture clean frames, and hand a precise frame to someone else.
The instant replay does the same for a glitch seen in ordinary play.
Plan: `pc/docs/plans/2026-10-22-cutscene-player.md`.

Code: `pc/src/cutscene.cpp` (takes, recipes, captures, repros, RPC),
`pc/src/cutscene_panel.cpp` (the window), `pc/src/state_rewind.c` (the
marks), hooks in `input_record.c`, `boot_runtime.c` (`func_800E45B4`),
`dev_rpc.c` (poll), `boot_window.c` (pacing), `scene_submit.inc` (the event
interpreter's place). Data: `pc/assets/cutscenes/catalog.json` (scenes),
`pc/assets/cutscenes/opcodes.json` (opcode names). Tools:
`pc/tools/cutscene_scan.py`, `pc/tools/cutscene_opcodes.py`. Test:
`pc/tests/cutscene_player_smoke.py` (run.py `cutscene_player_smoke`).

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

Going to frame t rewinds to the last mark at or before t and steps there
unpaced. Frames already played **replay** their logged input (the window
says REPLAY), so they are the frames played before, byte for byte (the
smoke test compares per-vsync state digests, the hashes and both pictures).
Past the head the game is **live** again and the take grows. *Cut here*
drops everything after the current frame (to try another branch).

Measured on the opening (64 bits): a mark is about 12.7 MB and costs about
2.5 ms once the ring reuses buffers (about 7 ms for a fresh one).

## Reaching a scene

`pc/assets/cutscenes/catalog.json` lists scenes by recipe:

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

Not reachable yet:

- story cutscenes that need the story's flags (a warp out of story order
  plays the room's entry script with the flags the card has: some scenes
  play differently or not at all; `notes` say what the scan saw);
- events started by doors, trigger regions or items (the scan only
  warps), and zone event files reached only through other events;
- the movies (FMV: not game-state driven);
- a scene whose event waits for a choice the scan's presses do not make
  (the eight entries noted "did not end within the scan's cap": 29/0, 31/0,
  32/1, 32/3, 32/6, 32/17, 34/0, 40/21).

## Controls

The **Cutscene** window (Window > Cutscene; the instant replay key opens
it too):

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
| `status` (default), `list` (the catalog), `catalog` (reload it) | |
| `start` | `scene` (a catalog id) |
| `here`, `stop`, `play`, `pause`, `cut`, `hotkey` | `here`: `name` |
| `step` | `frames` |
| `speed` | `value`: 1, 2 (1/2) or 4 (1/4) |
| `seek` / `back` | `frame` / `frames` |
| `capture` | `count`, `hide_text` |
| `repro`, `jump` | `jump`: `repro` (path), `frame` (optional) |
| `settings` | `interval`, `marks_max`, `budget_mb`, `instant_replay`, `replay_seconds`, `replay_interval`, `armed`, `hide_text` |
| `camera` | `on` |

Seeks, captures, repros, jumps and recipes finish at later vsyncs: wait
until `busy` is false. Also new: `room_goto {action: "reset"}` makes the
Room Browser's warp machinery idle (it is host state: an RPC rewind does
not undo it).

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
