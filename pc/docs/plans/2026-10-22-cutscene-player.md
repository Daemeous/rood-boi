# Cutscene player (2026-10-22)

Most graphical issues the user now sees are in cutscenes, which play once,
fast, and cannot be stepped, replayed or scrubbed. This is a developer tool
to reach a cutscene, freeze it, step it frame by frame, go back to any
earlier moment and replay it exactly, capture clean frames and hand a precise
frame to someone else. A rolling "instant replay" buffer does the same for a
glitch seen during ordinary play.

## What it builds on

- `state_rewind.c`: in-memory marks at the vsync boundary (game memory, the
  game stack, SPU, fog). Today two slots (the RPC's, the photo mode's); the
  player needs a ring of them, so the slots become a table with a range
  reserved for the player (`PC_REWIND_TAKE_FIRST ...`).
- `input_record.c`: pad and camera stick keyed by absolute vsync. The player
  keeps its own in-memory log (not a file) through a hook there.
- `boot_runtime.c` `func_800E45B4`: the scanline estimate the AI reads as a
  time budget is host time. It is the one host-clock input of game state;
  a take logs what it returned per vsync and returns the same values when it
  replays (ordinary play is untouched: values are only recorded).
- `dev_rpc.c` pause / step (`pc_rpc_set_paused`, `pc_rpc_request_step`) and
  its per-vsync poll, where marks may be taken and rewinds started.
- Photo mode (`photo.cpp`): the hold that re-renders a frozen moment,
  `pc_photo_ui_hide` and `pc_photo_capture` (clean PNG, UI hidden, through
  the presenters' capture hook).
- Room Browser (`room_warp.c`, RPC `room_goto {scripts: true}`): warping
  into a room with its entry event script left on.
- Event scripts (BATTLE.PRG 4A0A8.c / 573B8.c): `D_800F4C34` is the event
  runner's state (0 none, 1-3 loading, 4 running), the id is
  `(D_800F4BE0 << 12) | _evtFile` (`pc_battle_event_id`), `D_800F4C38[4]`
  are the four script threads' program counters into the script buffer
  `D_800F4C30`, `_opcodeFunctionTable` the handlers, `D_800F4C2C` the skip
  state. Entering a room starts its entry script (146C.c
  `func_800BEC58(1, ...)`).

## Scenes: what can be reached, and how

A scene is a *recipe* to its first frame, kept as data in
`pc/assets/cutscenes/catalog.json` (id, name, recipe, notes, what is known
to work):

| recipe | how | reliable |
|---|---|---|
| `new_game` | at the title menu (empty card, or the cursor on New Game) press Start; frame 0 is the press | yes: the opening, the same route as the test checkpoints `opening` / `first_bubble` |
| `warp` | from any field room: Room Browser warp to {zone, room, entry} with the room's scripts ON; frame 0 is the arrival | where a scan saw the room start an event (`pc/tools/cutscene_scan.py`); out of story order the flags differ, so a scene may play differently from the story |
| `ending` | from the field: the developer jump into ENDING.PRG (RPC `ending`) | yes (ending_smoke) |
| `here` | the current moment, whatever runs (any cutscene the user has played into) | always: the generic case |

Plus *arming*: "start a take when an event script starts" catches any
cutscene reached by playing. And the instant replay buffer (below).

Not reachable yet: story cutscenes that need story flags from earlier play
(warping alone does not set them) unless a save is at that point; FMV movies
(not game-state driven: a player over the movie is out of scope); event
scripts started by door or trigger regions (a scan only warps, it does not
walk). The catalog says so per entry.

## A take

A take is a timeline from frame 0 (its start mark) to its *head* (the
furthest frame played):

- an automatic mark every N frames (tunable, default 30) in a ring of at
  most M marks (tunable, default 64; the oldest dropped first, the start
  mark kept while a scene take is short enough);
- a log of every input the game read per frame: pad buttons, camera stick,
  and the scanline values;
- going back = rewind to the nearest mark at or before the target frame,
  then run forward (paused at the target) with the logged input; the frames
  replayed are byte-identical to the first time;
- past the head, live input is logged again (the timeline grows); "cut here"
  drops everything after the current frame (to take another branch).

Controls (overlay "Cutscene" window and RPC `cutscene`): play / pause, step
1, slow motion 1/2 and 1/4 (the game steps one vsync every 2 or 4 paced
vsyncs; audio stutters), scrub (timeline slider over frames with the marks
shown, jump to frame), interval and ring size, the event script readout:
event id, source (zone EVT / room entry / door), state, each live thread's
offset in the script buffer and the opcode there with its handler's name.

## Instant replay

A setting (`instant_replay`, off by default; developer tool) keeps a
rolling take always running: marks every N frames over the last ~30 s
(tunable: seconds, interval, memory budget) and the input log. A bound key
(`bind_key_replay`, default F8; F5/F9/F6/F7 are save states, F12 the photo
mode) freezes the game and opens the Cutscene window on that buffer:
scrub back to the glitch, step, capture, write the repro. Cost is measured
and shown (ms per mark, MB held). Marks only copy memory: game state and
timing are unchanged (the scanline values are logged, not replaced).

## Capture and repro

- "Capture frame": photo-mode hold (re-renders the frozen moment), HUD and
  overlay hidden (dialogue text optional), PNG at window resolution to
  `pc/user/cutscenes/<scene>/f<frame>.png` (gitignored).
- "Capture next N": one capture per frame for N frames.
- "Copy repro": writes `pc/user/cutscenes/<scene>/repro-f<frame>.json`
  ({scene, frame, start snapshot, input log, graphics settings}) and puts
  its path on the clipboard. The take's start is saved as a file snapshot
  (`start.snap`) and the log beside it, so `cutscene {action: "jump",
  repro: path}` (or the window's "Open repro") loads the start, replays the
  log to the frame and pauses there: the same frame, in any later session
  of the same build (snapshots are build-keyed; 64 bits survive engine-only
  rebuilds).

## Stretch

Free camera while paused: the photo mode's camera (`pc_photo_camera_*`)
on the held moment, reset on resume.

## Limits

- Exactness covers game state (RAM, scratchpad, overlays, VRAM, SPU,
  fog). The audio thread mixing on its own clock is not replayed; the
  renderer's temporal history (TAA-like accumulation, GI, denoisers) is host
  state, so a replayed frame's *picture* matches when those settle or are
  off; the native PS1 picture (`screenshot`) always matches.
- Marks hold copies of the state (several MB each): the ring's size and
  the buffer's seconds are bounded by a memory budget.
- Rewinding does not undo memory card writes (none happen in cutscenes).
- A rewind needs a mark, so no frame before the take's start.
- An engine library reload ends the take (its log is engine state).

## Tests

`pc/tests/cutscene_player_smoke.py`: the opening from the title (no card):
play to frame N with marks, state digest + native picture at N, scrub back
to an earlier mark, replay forward, the same digest and picture at N;
capture writes a PNG; a repro jump lands on the same frame (same digest).
Instant replay: buffer on during play, freeze, scrub back, replay, same
state. CTest: the slot table in `state_rewind_test`.
