# T1 plan: the renderer on its own thread, the game stepping at its fixed rate

Read-only survey of what still ties drawing to the game thread once T4 S1 (the adopted frame) and T5 S0 to S3b (the VRAM
stream, the renderer's own VRAM, fences, the rasterizer's output as events) are in. Companions: `2026-11-12-t4-survey.md`,
`2026-11-12-t5-vram-survey.md` (sections 7 and 8). Line numbers are the t5-s2 tree on 2026-11-12.

## 0. Headline

- After T4 S1 and T5 the renderer's *inputs* are values: the frame (items, native lists, camera record) and the VRAM stream
  with its fences. What still ties it to the game thread is not data but **calls**: roughly forty `pc_hw_*` hooks that the
  draw walk makes in the middle of walking, a handful of readers that take the game's VRAM array or game structs directly,
  and a present path that only runs inside `VSync`.
- The cheapest honest path is **not** "move the renderer, then fix what breaks". It is four moves, each provable on the
  same thread first: (1) make every hook a recorded command (the walk becomes a producer, the renderer a consumer of a
  command queue, still called inline); (2) cut the readers off the game's memory; (3) split the present from the tick; (4)
  turn the inline consumer into a thread with a mailbox. Only step 4 changes concurrency; steps 1 to 3 are single-threaded
  refactors proven by the existing hash lanes at 0 differ.
- Save states, rewind and the RPC are the sharpest risks: all three assume "one thread, the renderer's state is a pure
  function of the game's". Section 4 lists each.
- Size: about 3 to 4 weeks of agent time with two workers, largest in slices R1 and R3. Without the decisions in section 7
  (what happens to the software presenter and the CPU rasterizer) R1 cannot finish.

## 1. What runs on the game thread inside the walk (the hooks)

`draw_packet` (`boot_runtime.c:1339` `pc_draw_packet`, `:1354` `pc_draw_item_packet`, the walk body from `:1417`) is called by the
game (DrawOTag, DrawPrim) and calls the renderer synchronously. Every call below must become a frame command (R1) or a
frame-build-time attribute. Grouped by what they need from the game thread.

### 1a. Draw calls that carry their whole description (already commands in spirit)
`pc_hw_triangle` (`boot_polygons.inc:73`), `pc_hw_line` (`boot_lines.inc:20`), `pc_hw_rect` (`boot_runtime.c:1206`),
`pc_hw_text_quad` / `pc_hw_text_piece_done` (`boot_polygons.inc:369,377`), `pc_hw_native_marker` (`:1449`),
`pc_hw_native_translucent` (`:1460`), `pc_hw_ot_native_area` (`:1337`), `pc_hw_anchor_packet` (`:1440`), `pc_hw_frame_menu_model`
(`:1723`). State setters that precede them: `pc_hw_set_2d`, `pc_hw_set_bounds`, `pc_hw_set_depth`, `pc_hw_set_precise`,
`pc_hw_set_narrowed`, `pc_hw_set_texture_window`. They return a value in two places: `pc_hw_triangle` (true: "covered by a
native draw, skip the CPU rasterizer", `boot_polygons.inc:73`) and `pc_hw_raster_skip` (`:94`, `boot_runtime.c:1190`). Those
return values steer the *game thread's* own work, so they are fences in disguise: the CPU rasterizer's decision depends on
renderer state (stale rectangles). With the rasterizer kept as the oracle (decision of 2026-11-12) the decision must be made
at frame build from game-side data, not by asking the renderer: R1 moves the stale-rectangle bookkeeping (`gpu_hw.c`
`stale_rects`, `mark_stale_kind`, `ot_stale`) to the producer side, where the walk already knows which areas native lists
cover (`ot_has_native`, `:1344`).

### 1b. Hooks that read game memory by address while walking
- `ui_text` popup placement: `pc_ui_text_popup_place`, `_place_item` (`boot_polygons.inc:323-324`, declared
  `boot_runtime.c:63-64`) read the next packets through `drawing_tag` (adjacency in the OT). T4 S1 makes adjacency a frame
  property; the call itself must move to frame build.
- `pc_ui_bubble_packet` (`ui_bubbles.c:624`), `pc_ui_bubbles_frame` (`:430`): bubbles read packet memory and record bubble
  state per frame.
- `ui_text.c`: `pc_ui_text_resolve(vram, clut, ...)` (`:185`), `pc_ui_text_layout_run`, `pc_ui_text_piece`,
  `pc_ui_text_status_quad`, `pc_ui_text_word_quad` and `pc_ui_upscale_lookup` are called from the renderer (`gpu_hw.c:2520,
  2528, 2829, 2892, 2909, 2946`) with the renderer's VRAM (`renderer_vram`, S2). They keep per-glyph caches
  (`pc_ui_upscale_tiles_changed`, `pc_hw_mark`, `gpu_hw.c:1628`) whose invalidation arrives on the game thread. After S2 these
  are renderer-side reads of renderer-side VRAM and need only the cache invalidation to travel as an event (it already
  does: `pc_hw_mark` is the stream's mark).
- The classic layer (`classic/classic_parity.c:237,267` capture, suppress, claim; `classic_bridge.c`; `classic_hook.h`)
  edits guest links and cursors during the walk. T4 survey 6.3 already lists it; it must operate on frame entries.
- HUD bridge claims (`hud_bridge`, the `lift_bridge` lint class, 13 hits): claim emitters that write packets inside the walk
  (T4 S3 converts them to items).
- `pc_button_roles_texels` (`boot_runtime.c:1192`): rewrites sprite UVs by role during the walk (reads VRAM).

### 1c. Record-time readers of the game's VRAM array (S2 left these)
`emissive.cpp:147,1311,1326,1343,1370,1585` (`pc_native_vram`), `environment.cpp:138,199`, `ptgi.c:439`, `vk_present.c:47`
(passes `pc_native_vram()` to `pc_hw_init`, `:409`; `gl_present.c:1197` likewise), and `dev_rpc.c:2639,2838,2881` through
`pc_hw_native_vram()` (reads, and one write at `:2881`). All still read or write the *game's* VRAM array from whatever thread
calls them. Moves: point them at the renderer's VRAM (`pc_hw_renderer_vram`) and have the RPC write go through a stream event.
`emissive`/`environment`/`ptgi` are record-time: they run when the frame is recorded (`gpu_hw.c` record_* functions) and
sample textures to derive lighting data; they run on whichever thread records, so after R4 they run on the renderer thread
with the renderer's VRAM (they need nothing else from the game).

### 1d. Game state the renderer reads
The camera and projection (`pc_gte_*`, `vs_gte_screen`, `pc_scene_camera_get`), `pc_game_phase`, room and actor info for
occlusion (`occlusion.h`), `pc_gte_widescreen`, `pc_ui_ashley_ot_model()` (`:1723`), `pc_fog_vsync` (`:1584`), the draw/display
environment (`pc_draw_env`, `:972`, display_x/y/w/h used by the present at `:1602`). The camera record and the draw/display
env belong in the frame header (T4 S1 target); the rest are small structs that become a once-per-frame snapshot value taken
at frame hand-off.

## 2. The present path

- `pc_tick` (`boot_runtime.c:1551`): per game vsync: `stream_vsync`, `pc_cost_vsync`, `pc_state_diff_vsync`, `pc_rpc_poll`,
  `pc_engine_vsync`, then `tick_frame` (`:1574`) -> `tick_frame_work` (`:1580`): `pc_fog_vsync`, `pc_lua_tick`,
  `pc_charge_skip_vsync`, `pc_window_tick` (`boot_window.c:305`: window events, pacing), `pc_cd_tick`, `pc_audio_frame`, then
  the present (`:1602`: `pc_window_frame(vram, display_x ...)`) and the extra presents at fractions (`:1613`) when
  `pc_present_rate > 1` and interpolation is on (`pc_window_present_rate`, `boot_window.c:757`; `pc_window_wait_fraction`,
  `:67`).
- Everything in that list runs on the game stack, between two game vsyncs. The renderer's *own* work (batch flush, lighting,
  composition, the backend swap) is inside `pc_window_frame`; it is what stalls the game today.
- Interpolation (`pc_hw_interpolate`, `gpu_hw.c` `present_*`, `replay_frame`) needs the previous and the newest recorded frame
  plus the write log (S2). It is renderer-internal state; on a thread it needs the three recording slots to be owned by the
  renderer and filled from the frame queue. Phase is `k/n` of a field (`pc_window_wait_fraction`), not real time: R3 changes
  it to elapsed time since the frame arrived (T4 survey section 5).
- Pacing: `pc_window_tick` sleeps to 59.94 Hz deadlines on the game thread. With the renderer separate, the game thread
  keeps the fixed-rate deadline (logic, audio, input) and the renderer paces to the display (vsync swap).
- Tooling that needs "the frame as presented, now": `pc_capture_display` (`:1505`, RPC screenshot; calls `pc_hw_sync`),
  checkpoints (`:1639,1735,1758`), photo mode (`photo.cpp:359,385,435` call `pc_hw_resync` then render). All are fences
  with a reply: ask the renderer, wait. They already run "between frames" on the game thread, so they become request/reply
  on the mailbox.

## 3. VRAM ownership after T5

State after S3b: the game thread owns the game's `vram[]` (CPU rasterizer, LoadImage, StoreImage) and posts events; the renderer
owns `rvram` and the GPU target. The remaining touch points:
- `pc_hw_before_write` (`boot_runtime.c:395,1105`) reads the renderer's VRAM from the game thread to capture the old data for
  the write log. On a thread the *renderer* captures it when it applies the upload event (it holds the previous contents), so
  the log is renderer-internal and the hook disappears.
- `pc_hw_sync_store` / `pc_hw_sync_texture` (read-backs, the two fence causes in the stream): the game thread blocks until
  the renderer has applied every earlier event and copied the rectangle back, then continues with the pixels. Measured:
  zero to thirteen per route (t5 plan section 8), so a blocking wait is affordable. The reply carries the pixels into the
  game's array (the rasterizer's source).
- `pc_hw_stale_copy` (`boot_runtime.c:1103`): staleness propagation through a VRAM copy; moves with 1a (producer-side
  bookkeeping).
- Raster events (S3b): payload is a copy of the rectangle, so they are self-contained. Volume on a thread: the rasterizer runs
  on most frames (hundreds of rectangles per frame, 70K pixels), so the event payload must be pooled, not malloc'd.
- Snapshot/restore: `pc_hw_resync` (`gpu_hw.c:3534`; called `snapshot.c:890`, `state_rewind.c:199`, `engine_reload.c:152`,
  `photo.cpp:359,385,435`) copies the game's whole VRAM into the target. It is the "resync" event of the stream; on a thread
  it is `post(resync, 1 MB payload)` followed by a wait (the caller continues only when the renderer holds that state).

## 4. Save states, rewind, reload

- `snapshot.c:383` copies the game's VRAM array as part of the state; `:1012` hashes `pc_native_vram()`. Restoring writes the
  game's array and calls `pc_hw_resync` (`:890`). Unchanged by the thread, except that capture must first drain the renderer
  (the game's VRAM must include read-backs the renderer owes it): a `fence_all` before capture.
- `state_rewind.c:199`: the same resync after a rewind step; rewind steps are rapid (per vsync backwards) so a 1 MB resync
  event per step is a bandwidth question, not a correctness one; measure before R4.
- `engine_reload.c:152`: reloads the engine library and resyncs: the renderer's state is host state and survives, the stream
  digest and `vcheck` are `.data.rood_host` and are excluded from snapshots by section (S0).
- The frame queue and the renderer's recordings are host state: they must be empty or discarded on restore (a snapshot cuts at
  a vsync boundary, so the queue is drained at capture and cleared at restore).
- Determinism (`ROOD_TEST_SNAPSHOTS`, `snapshot_equivalence`, `engine_rewind_smoke`): the game's state must not depend on the
  renderer thread's timing. Today it does in exactly two places: the CPU rasterizer's stale-rectangle skip (1a) and
  read-backs. Both are made deterministic by R1/R2 (the decision comes from game-side data; read-backs are blocking
  fences). That is the correctness criterion for the whole plan.

## 5. Slices

Each slice keeps the game identical and is proven on the existing lanes at 0 differ (`--menus` all routes, `--casts` and
`--casts drive`, the fight, story routes, `--menus` replay hashes with `BUILD_COMPARE_REPLAY=1`, `snapshot_equivalence`,
`engine_rewind_smoke`, `rpc_smoke`) plus the stream digest. "Size" is agent time.

| # | Slice | What | Proof | Size | Risk |
|---|---|---|---|---|---|
| R0 | Meter | Count and time every renderer call by hook, per frame (RPC); a `--queue-check` mode that records the hook calls of a frame and asserts they are all in the list of section 1 | the list is complete on all routes | 1 day | low |
| R1 | Hooks become commands | One command stream: every `pc_hw_*` call in 1a/1b is appended to a per-frame buffer (type, small args, payload) by the game and applied by `pc_hw_consume(buffer)` called inline at DrawOTag end. Return-value hooks (`triangle`, `raster_skip`) become frame-build decisions from game-side bookkeeping (stale rects, `ot_has_native`). `ui_text` popup, bubbles, classic claims, hud claims move to frame build (T4 S1/S3 prerequisite for the last two) | hashes 0 differ with consume inline (the producer/consumer split is invisible); stream digest unchanged; per-hook coverage from R0 | 1.5 to 2 weeks (2 workers) | high: ~40 hooks; the return-value decisions are the real work |
| R2 | Cut the readers | `emissive`, `environment`, `ptgi`, `vk_present`/`gl_present` init, RPC reads and the RPC VRAM write use the renderer's VRAM (RPC write becomes an upload event); `pc_hw_before_write` moves into the upload event; `pc_button_roles_texels` and other walk-time readers get the VRAM through frame data | hashes 0 differ; `ROOD_VRAM_RENDERER=native` removed as the old source; a debug build that poisons the game's array after each event proves nothing the renderer reads comes from it | 3 to 4 days | medium: emissive/environment have caches keyed on tiles |
| R3 | Split the present from the tick | `tick_frame_work` posts "present phase k of n" instead of calling `pc_window_frame`; elapsed-time phase; renderer-side pacing; `pc_capture_display`, checkpoints and photo mode become request/reply. Still inline (the renderer runs the request on the calling thread), so single-threaded and provable | hashes 0 differ at present_rate 1; with `interpolate 1, present_rate 4` replay hashes 0 differ; frame-time tail measured | 4 to 5 days | medium: pacing, window events (`pc_window_tick`) stay on the game thread until R4 |
| R4 | The thread | Renderer thread with a mailbox: frame commands, VRAM events (with pooled payloads), fences (blocking: store, texture, resync, capture) and requests. Triple-buffered frame; game thread paces itself at the fixed rate and never waits except at fences. Backend (GL/Vulkan) context moves to the renderer thread | every lane at 0 differ *and* repeated runs identical (determinism: three runs of each route with random sleeps injected into the renderer thread); `snapshot_equivalence`, `engine_rewind_smoke`, `rpc_smoke`; a run under ThreadSanitizer-style lock checking of the mailbox | 1 week | highest: context ownership, shutdown, hitches |
| R5 | Unlock | Game logic keeps its fixed rate; renderer presents at display rate; frame-time tail and input-latency measurement | measurement, not hashes | 2 to 3 days | low |

Order: R0, R1, R2 can overlap after R1's command format is fixed (two workers: one on 1a/1b producers, one on R2). R3 after R1.
R4 only when R1 to R3 are all at 0 differ with the consumer inline. R5 last.

## 6. How each slice is proven (summary)

R1 to R3 change no behaviour, so the proof is the existing one: same-binary replay hashes (`BUILD_COMPARE_REPLAY=1`,
`--menu-sample 3`, interpolate on, present_rate 4) against the previous slice's build on every route, the fight, the casts,
and the story routes. R4 adds the concurrency proof: the same routes run three times with randomized renderer-thread
delays must produce identical game-state hashes (`battle_state`, `snapshot_equivalence`) and identical frame hashes.

## 7. Decisions needed

1. **The software presenter and `ROOD_RENDERER=null` (the CPU reference frame).** R1 can keep them only if the CPU frame is
   produced from the same command buffer on the game thread. Cheapest: keep the null renderer as a consumer that runs
   inline (no thread) and make the thread an option of the hardware renderer. Needs the owner's answer.
2. **The CPU rasterizer stays** (decided): its output is posted as events (S3b), so on a thread it costs a pooled copy per
   rectangle. If measured too heavy, restrict the oracle to a debug mode that runs the rasterizer without posting.
3. **Blocking fences are acceptable** (read-backs are rare: 0 to 13 per route). If a route is found with many, the fix is
   producer-side (do not sample the frame being drawn), not asynchronous read-back.
4. **GL path**: the OpenGL renderer is contributors' to maintain (retired-32bit memory); the thread work targets Vulkan first
   and keeps GL on the inline consumer.

## 8. Risks

- **Return-value hooks** (1a) are the single biggest unknown: the CPU rasterizer's behaviour depends on the renderer today.
  R0 measures how often the decisions fire; if some depend on GPU state, not frame data, R1 grows.
- **Shared statics.** The hw path is ~6300 lines of `gpu_hw.c` with file-scope state; after R1 only the renderer touches it,
  but R2 must catch every other reader (`dev_overlay.cpp`, `dev_rpc.c` graphics RPC set many `pc_hw_*` globals from the
  RPC thread, `boot_runtime.c` `pc_present_rate`, `pc_hw_interpolate`, `pc_gte_widescreen`). Settings become frame-header
  values or atomics.
- **Hitches move, they do not vanish**: a blocking fence during a heavy renderer frame stalls the game exactly as today.
  Measure the fence rate per route (t5 plan section 8) before R4; the measured rate is low.
- **Lua plugins** (`pc_lua_tick`, `boot_runtime.c:1588`, plugin draw hooks): they run on the game thread and may call
  renderer-affecting APIs (`ui_text`, claims). Those APIs need the same command-buffer treatment, listed per plugin API in R1.
- **Photo mode** renders by resuming the game from a frozen state many times (`photo.cpp`): it is a tight loop of
  game-step plus render plus resync. It becomes request/reply per render; likely the slowest consumer of fences.
- Not covered: the audio thread (T18) and input latency work (T20), which follow T1 but are separate.

## 9. R0 results: the hook meter (built; ROOD_HW_METER=1, RPC `hw_meter`, `pc/tools/hw_meter.py`)

`pc/src/hw_meter.[ch]`: all 91 `pc_hw_*` hooks of `gpu_hw.c` count their calls (total, inside the draw walk, from another
thread, per frame, max per frame, time with ROOD_PERF=1); the two return-value hooks log each answer with its reason and what
the reason depends on; `ROOD_HW_QUEUE_CHECK=1` reports, once per hook, any hook called inside the walk that is not a draw
command or state setter (A), native-list hand-over (N), query (Q) or VRAM event (V), the classes section 1 lists. One run of a
scene: `python3 pc/tools/hw_meter.py --scene fight|field|menu|opening` (Vulkan, headless, 200 to 300 game frames). Numbers
below: this tree, one run each, frames = game vsyncs.

**Volume.** Field, fight and main menu: about 4,500 to 4,800 hook calls per game frame, 99% of them inside the walk; the
opening's picture cards: about 330. By class per frame (field / fight / menu): A 3,500 to 3,700, V 750 to 770, Q 230 to 270,
N 12 to 21, P 11 to 12 (the present, once per presented frame), S and T under 25. The producer must record about 4,000 small
records a frame, not forty calls: the command format of R1 has to be a packed array, not a call list. The heavy ones (per
frame, fight): `anchor_packet` 1,410 (max 2,911), `raster_post` 530, `triangle` 330, `set_precise` and `set_depth` 290 each
(always together), `set_2d` and `set_packet` 250, `set_bounds` 250, `set_room` and `is_room_packet` 230, `sync_texture` 217,
`set_flipped_uv` 203, `text_quad` 62. Most are state setters that the walk calls once per primitive with unchanged values:
R1 can fold them into the next draw record's header.

**Cost on the game thread** (meter on, so inflated by the counter and the clock read: read as an upper bound). Class A, per
game frame: 232 us (field), 276 us (menu), 696 us (fight); `native_marker` alone is 600 us in the fight, 145 to 170 us in the
field and menu: it records a native list (the actors or the room) inside the call, and is the one A hook that does real work.
`triangle` is 14 to 25 us, `rect` 6 to 20 us, `native_translucent` 9 to 18 us. V is 15 to 24 us, Q 3 us. So moving the walk's
hook work off the game thread buys about 0.25 to 0.7 ms of a 16.7 ms frame here; the rest of the renderer's cost is in the
present (`target`, 18 to 21 us in the call, the flush and the backend elsewhere), which R3 and R4 move.

**The return-value hooks do not depend on the GPU.**
- `pc_hw_triangle` answers 1 only for a polygon a native list drew (object, effect or room, when `pc_native_actors` or
  `pc_native_room` is 2) and marks its drawing area stale first. Fight: 56,405 of 66,425 calls answered 1 (51,605 object, 4,800
  effect), 10,020 answered 0 (queued for the GPU, the CPU rasterizer draws too). Inputs: the packet and the frame's native
  lists (frame data), the two settings, the classic suppress flag (`pc_hw_suppress`) and the free camera's afterimage test (a
  game-side flag): no GPU state, no timing. Field: 51,232 of 69,824 answered 1 (44,032 object, 4,800 effect, 3,400 room), 17,592 answered 0;
  menu: 78,520 of 113,994 answered 1 (66,138 object, 7,248 effect, 5,134 room), 35,474 answered 0; the opening answered 0 for
  all 14,682 (no native lists).
- `pc_hw_raster_skip` answers from `ot_stale`, a table that exists only between `pc_hw_begin_ot` and `pc_hw_end_ot`
  (`gpu_hw.c:922`), written by `mark_stale` (the triangle answer above and `pc_hw_ot_native_area`) and shrunk when a read-back
  makes an area current (`pc_hw_sync`'s loop; counted as `ot_stale_dropped_by_readback`: 0 in these runs; the VRAM stream's
  blocking fences were 0 `store` and 0 `texture` in the fight). So the decision is a function of the native lists registered
  this ordering table plus the game's own read-backs, in game order: the producer can compute it. Fight: 4,373 of 4,373 calls
  answered 1 (covered), field 4,796 (all covered), menu 19,568 (all covered), the opening 7,586 answered 0 (`skip_inactive`: no stale table, the picture
  cards draw on the CPU rasterizer).
- One real coupling: the read-back path decides between "own copies" (GPU copy, no wait) and a read-back by
  `own_copies_enabled()` and `vram_from_target()` (a capability and the `ROOD_GPU_VRAM_COPY` setting), not by timing.

**What the plan's section 1 left out.** `pc_hw_emissive` (class T) is called from the walk (render.c:2368 when it builds a
native list, `vk_render.inc:411`, `vk_hw.c:443`): 6 to 21 times a frame, and it runs `emissive_sync` (hashes VRAM texels) and
returns the emissive map and whether shaders read it. It is the one hook inside the walk the plan does not list: R1 must
either make the emissive state a frame value or move the call to frame build. `sync_texture` is called 190 to 220 times a
frame but is an early-out unless a stale area covers the sampled texels (blocking fences in the fight: 0), so the fence
count in section 3 is the right number for R4, and the call count is a producer-side gate, not a cost.

**Not seen in these runs.** No hook was called from another thread (the RPC reads the meter but sets no `pc_hw_*` state in these
scenes); the `set_scale`, `supersample`, `detail_view`, `replay_*` setters appear once per presented frame (class S, about 1
a frame). The `--queue-check` mode lists `emissive` as the only hook in the walk outside A, N, Q and V in all four scenes.

## 9. R4 survey, after R1 to R3 and R5 (what stops the thread today)

Measured with the R0 meter (`pc/tools/hw_meter.py`, scenes fight and menu, 120 frames each, Vulkan) and by reading what the presenter
calls. The draw walk is nearly clean: every hook it calls is a command, a native-list hand-over, a query answered producer-side or
a VRAM event/fence; the only unlisted calls inside the walk are two record-time readers, `renderer_vram` (405K calls in 120 frames of
the fight: `emissive.cpp` sampling textures per primitive through `pc_renderer_vram`) and `emissive` (1.3K to 4K). The VRAM stream
fences are rare (store 0, texture 0 on these scenes). **The walk is therefore not what blocks the thread. The present side is.**

What the Vulkan present path (`vk_present.c`, `vk_rt.inc`, `vk_shadows.c`, `post_vk.inc`, `ptgi.c`, `post_profile.cpp`) reads from
modules that live on the game thread, by owner (the symbols it declares extern; about 100 in all):

| Owner (game-thread module) | What the present reads | Kind |
|---|---|---|
| `lights.cpp`, `lights_*`, `probe_overlay.cpp` | `pc_lights_view`, `pc_lights_enabled`, `pc_lights_world`, `pc_lights_glow_drive`, fixture glow modes and shapes, `pc_material_graph_values` | per-frame values: a lights block |
| `scene_capture.c` | camera, its inverse, projection, positions, serial | frame header (the T7 record) |
| `ptgi.c`, `shadows.c`, `glows.c` | the CPU-built scene (triangles, emitters, casters, bounds) the GPU passes upload | built on the game thread; must be handed over as an immutable per-frame block |
| `lua_photo_post.cpp` (Lua) | post fields, overrides, preset names, value text | Lua state: cannot be read off-thread; resolved on the game thread into a values block |
| `ui/menu_bridge.c`, `ui/ui_theme.cpp`, `ui/menu_light.cpp`, `ashley_bridge.c` | menu active, backdrop, the Ashley model's light | frame header |
| `weapon_slots.c`, `query_bridge.c`, `cutscene.cpp`, `option_peek.c` | first-person, field-menu open, battle event, option peek flags | frame header (booleans) |
| `photo.cpp` | capture wanted, the captured pixels' return | request and reply |
| dev overlay and editors (ImGui, `pc_overlay_frame`, `editor_view.cpp`, `emissive_editor.cpp`, `material_survey.cpp`, `lighting_quality.cpp`) | build their UI from game state each present, then record into the command buffer | UI built on the game thread, its draw data copied and rendered on the renderer thread |
| `gl_present.c` / `boot_window.c` | brightness, shadow softness, capture and eyedropper requests, the SDL window itself | settings snapshot; SDL events stay on the game (main) thread |
| `gpu_hw.c` (the renderer's own) | casters, emissives, lists, depth texture, gbuffer, history cut, native texture: already renderer-side but read by the other modules above on the game thread | to be read only on the renderer thread |

So R4 is not "move the consumer": the consumer's inputs beyond the frame (adopted frame, commands, VRAM stream, present requests)
are about twenty subsystems' state read live. A thread that runs the present reads them concurrently with the game writing them:
a data race, not a timing question, which the jitter gate would only sometimes catch.

### Revised slices to the thread (each provable inline first, as R1 to R3 were)

| # | Slice | What | Size | Proof |
|---|---|---|---|---|
| R4a | Present frame header | One struct per frame, filled on the game thread at the present request (lights, camera and projection record, menu flags, settings, brightness, photo flags, the resolved Lua post values); the presenter reads only it. The off-thread poison check: in a debug build the presenter's externs are replaced by reads of the header, and a canary mode scrambles the game-side values after the request is posted | 1.5 to 2 weeks | replay-hash lanes 0 differ; the scramble run identical |
| R4b | Scene blocks | The CPU-built scene (ptgi, shadows, glows, scene_capture lists) becomes a per-frame immutable block handed over with the request (double buffered) | 1 to 2 weeks | GI/shadow lanes with ROOD_GI=1: 0 differ; same scramble |
| R4c | UI draw data | The dev overlay and editors build on the game thread and hand ImGui's draw data (copied) to the renderer; their game-state reads stay on the game thread | 3 to 5 days | overlay screenshots equal; the editors' smokes |
| R4d | The emissive record-time readers | `emissive.cpp` (per primitive texture sampling from VRAM during the walk, 405K calls in 120 frames) moves behind the VRAM stream or onto the renderer (3D-maths worker's area) | in their queue | meter shows no unlisted call in the walk |
| R4e | The thread | Mailbox of present requests, command buffers, VRAM events, fences; the Vulkan context on the renderer thread; setting `render_thread` (off by default; null, software and GL inline); snapshot, rewind and photo drain the queue first | 1 week | `render_jitter_smoke` with real scheduling (3 routes x 3 seeds, full state hash), replay lanes 0 differ, rpc/snapshot smokes |

About five to seven weeks of agent time in all, the largest part R4a and R4b. They are the same kind of work as R1 (turning live reads
into values), and each is useful alone: a frame header and scene blocks also make a save-state-free replay of the present possible.
Without R4a and R4b a renderer thread would race, so the thread itself (R4e) is blocked on them and is not started.

## 10. R1 progress (t1-r1)

- Done and proven (7 replay routes and the fight lane 0 differ, control 0, audio identical, each against the previous build): draw
  records (state setters fold into the next draw record), native-list marks, the triangle decision producer-side
  (`pc_hw_triangle_decide`; `pc_hw_raster_skip` is a producer query), raster_post / mark_raster / mark with the pixel payload and a
  producer-side pending list (`hw_cmd.c`).
- Emissive: `pc_hw_emissive` is no longer a hook; it returns the frame value (`emissive_shader_on`, `emissive_texture`). The map is
  brought up to date once by `pc_hw_emissive_frame()` where a frame's draws begin (a batch, a native list). Not moved to frame
  build: `emissive_sync` reads game state (the uses noted by `render_list_add` during the walk, VRAM, the current room) and the
  first draw of a frame can come mid-walk, so syncing earlier changes when uses match. Those reads move with R2/R4 (the renderer
  owning its copy of the state). The hook count is 90.
- `text_quad` / `text_piece_done`: the answer is producer-side (`pc_hw_text_quad_decide`: the layout setting, the scale, the suppress
  flag, the quad's shape and the font's glyph tables; the renderer's VRAM is only checked for existing, `identify` reads tables).
  The producer keeps its own "a piece is being drawn" flag; the piece and its end are records (`C_TEXT_PIECE`, with the packet
  address, and `C_TEXT_PIECE_DONE`). Neither is a hook any more; the hook count is 88.

### R4a status

Done (frame header, `present_header.h`, filled on the game thread at each present request and the only thing the presenter proper
(`vk_present.c`, `post_vk.inc`) reads of the game): 37 settings (R4a-1), and the scene (projection, camera and its inverse, room
bounds), the light list and the baked shadow's parameters, the menu (active, backdrop, the model's light), the post profile,
photo-capture and emissive flags, and lighting quality's answers (ao traced, ssao for traced and not, low haze density, baked scale);
`pc_lighting_sync` (which writes the settings) now runs in the fill. Proof: replay-hash lanes 0 differ (data, storage, platforms, story
with GI), `ROOD_PRESENT_CANARY=1` 0 differ, `snapshot_equivalence`, `rpc_smoke`.

The canary has two tools: poison (the variables, levels 1 and 2) and call guards (`ROOD_PRESENT_CANARY_CALLS=1`: a game-state getter
called inside the present is counted and reported; `ROOD_PRESENT_CANARY_ABORT=1` aborts at the first). Guards sit in the getters the
present was seen to call.

Residual live reads inside the present, from the guards: the path-traced GI, probe and fog-volume code (ptgi.c, vk_rt.inc, vk_probes.inc),
which calls `pc_scene_bounds`, `pc_scene_camera_get`, `pc_scene_camera_inverse`, `pc_lights_list`, `pc_lighting_ao_traced` and
`pc_emissive_active` itself and reads 17 settings (R4b); and the presenter's own `pc_lights_shadow_pick` (stateful: it keeps the
casters of the last frames, so it moves with the shadow code to R4b). `pc_hw_*` calls of the renderer's own state are not game reads.

### R4b status (settings and scene getters)

Done: the Vulkan GI, probe, fog-volume and particle code (vk_rt.inc, vk_probes.inc, vk_fogvol.inc, vk_particles.inc, probe_gi.inc, the CPU
side of ptgi.c) reads 39 more settings (76 in the header) and the scene getters through the frame header (`pc_ph_scene_*`,
`pc_ph_lights_list`, `pc_ph_lighting_*`: header values inside a present, the live ones outside it, for the editors' and RPC dumps' own calls):
camera and its inverse, room bounds, the scene and UV serials, the animated triangles' indices (up to 4096), the whole light list,
ao traced, the traced haze density. Canary level 1 poisons all of it with GI on and off: 0 differ; replay lanes 0 differ with it off.

Left, from the call guards (`ROOD_PRESENT_CANARY_CALLS=1 ROOD_PRESENT_CANARY_TRACE=1` prints each first caller):
- `particles.cpp` `pc_particles_gpu_frame(w, h, ...)` gathers the particle simulation and projects it at the presenter's resolution: it
  needs a split into a simulation snapshot (a record on the game thread) and the projection (the renderer);
- `ptgi.c` `update_emitters` calls `pc_emissive_active` and the emissive room/object light functions: the emissive records (R4d);
- `pc_lights_shadow_pick` (stateful shadow caster pick), `pc_glows_update`, `pc_medium_gpu_get`, `pc_env_gpu_get`, `pc_fogvol_gpu_get`
  (atmosphere and environment modules' GPU inputs, built from game-thread settings), `pc_scene_triangle_data` (the captured room: a
  by-value record when its serial changes), `pc_probe_lights_base`, `pc_vk_movers` (render.c, R4d's);
- `gte_widescreen`, read by the renderer's own record-time and replay code (the adopted frame's header).

### R4b status (particles, world lights, environment, the captured room)

Also done, through the frame header: the particles' game-thread half (`pc_particles_gpu_snapshot`, gathered at the fill at a placeholder
display size; the presenter's `pc_particles_gpu_frame(w, h, ...)` takes the copy and sets its own size), the CPU-side GI's world lights
(`pc_lights_world`, at most 16) and the environment record (`pc_env_gpu_get`), and the captured room's triangles (a renderer-side copy,
refreshed when the scene serial changes, and its animated triangles' UVs when the UV serial does; `ROOD_PRESENT_CANARY_TRIANGLES=1`
compares it with the live array at every fill: no difference on platforms and story with GI on). The particle pass could not be exercised
here (no emitter draws in the headless rooms tried), so its proof is the hashes and the guards, not a drawn mote.

Left inside the present, from the guards: `pc_medium_gpu_get` (atmosphere; reads the room's floor grid and bounds, two calls with
different densities), `pc_lights_shadow_pick` (stateful), `pc_glows_update`, `pc_fogvol_gpu_get`, `pc_medium_hash`/`pc_env_hash`,
`pc_probe_lights_base`, and the emissive functions (R4d's).

### R4b-4 (the atmosphere)

Done: the medium at the two hazes the presenter asks for (traced and low, at the scene bounds), the fog volumes, the medium and environment
change hashes, the probes' base lights and the presenter's environment record come from the header (`pc_ph_medium_gpu_get` and the others
match the call's arguments against what the fill computed, and fall back to the live function, which the guards then report, when they
differ: none did on platforms or story with GI on). Guards now list only the emissive functions (R4d), `pc_lights_shadow_pick` and
`pc_glows_update` (ptgi's emitter update) as reads of game state inside the present.

### R4c (the overlay and editors)

Done: the developer overlay, the editors, the modern menus' ImGui drawing and the HUD panels (everything `pc_overlay_frame` builds) are built on the game
thread, from the present's header fill (`pc_overlay_build`, last in the fill: the panels read and edit the live settings after the header copied
them, as the present's own overlay did), at the window size the last present left. The renderer gets a copy of the draw data
(`ImDrawList::CloneOutput` per list; `pc_vk_overlay_snapshot`), which `pc_vk_overlay_render` draws; the presenter calls nothing of the UI.
Effects of the move: a panel that shows a render result (the render timings, the presented view's rectangle) shows the last present's, one frame
older; a window resize shows the UI built for the old size scaled for one frame. Left for R4e: ImGui's texture updates (`draw_data->Textures`,
the font atlas and the user textures the backend uploads inside RenderDrawData) still point into the context, and the backend's descriptor sets are made
on whichever thread builds the UI: the renderer thread must run the texture updates from a queued copy, or ImGui's own texture requests become
records.

## 10. R4e review: what stands between here and the thread (2026-11-13)

State of the present: the call guards (`ROOD_PRESENT_CANARY_CALLS=1`) list one live read of game state inside the presenter and everything it
runs, with GI on: `pc_emissive_active` (R4d's snapshot, not yet merged). Everything else the presenter and the GI, probe, fog-volume,
particle and overlay code needed is in the frame header, copied on the game thread (settings, scene, lights, atmosphere, the particles'
and the shadow pick's inputs, the captured room, the UI's draw data). Canary level 1 poisons the settings and the proofs are 0 differ.

What R4e still has to settle, in the order I would do it:

1. **The emissive snapshot** (R4d, `t1-r1` d237ed62b and its next commit), and **ptgi's `update_emitters`** with `pc_glows_update`: it builds the
   glows from the room's triangles, the emissive lights and the renderer's effect lists, then `pc_glows_update` (stable ids, smoothing,
   the editor's overrides: game-side state) turns them into the emitter arrays. It is a producer step: run it at the header fill and carry the
   emitter arrays (`emitter_pos/col/shape/shadow`, `emitter_count`) in the header; the renderer-side lists it reads (`pc_hw_emissives`) are
   then the walk's records. Blocked on the emissive snapshot.
2. **VRAM events carry their payload.** `rvram_apply` copies an upload, fill or copy from the game's array when the renderer applies the
   record, which is the same moment on one thread. On a thread the record must carry the pixels (as the raster post does); the read-back
   fences (`sync_store`, `sync_texture`, `pc_hw_sync` for captures) become request and reply. The stream already names every such event
   (kind 1 to 6), so the payloads are the stream's.
3. **Game-thread callers of the renderer's internals**: the RPC dumps (`pc_hw_dump`, `pc_hw_redraw`, the `gpu_*` RPCs), the editors
   (`pc_hw_native_texture`, `pc_hw_casters`, `pc_hw_caster_surfaces`, `pc_hw_present_all_lists`), photo mode (`pc_hw_photo_*`, `pc_hw_resync`),
   the engine reload and the save-state restore (`pc_hw_resync`), the settings that set `pc_hw_*` globals (scale, subpixel, perspective,
   texture filter, interpolate). Each becomes a request on the mailbox (with a reply where it returns something) or a header setting.
4. **ImGui's texture updates** (R4c's note): the backend uploads the font atlas and user textures inside `RenderDrawData`; the texture list
   must travel with the draw data (a copy of the pending updates), and descriptor sets are made on the renderer thread.
5. **`gte_widescreen`** and the other values the renderer's record-time and replay code read live (`gpu_hw.c`): the adopted frame's header
   (T4's frame value) carries them per frame.
6. **The window.** SDL events and pacing stay on the game thread; the swapchain, its resize and the present move with the Vulkan context
   (the R5 present service and its pacing then run on the renderer thread, `present_stats` read through a request). A resize is a message.
7. **Snapshot, rewind, photo, engine reload**: `fence_all` (drain the mailbox and the GPU) before capture, then a `resync` request after restore;
   the renderer's own state (the write log, recordings, caches) is host state and is rebuilt by `pc_hw_resync`.
8. **The gate**: `render_jitter_smoke` with the noise at real thread scheduling (the points are the mailbox's), the replay lanes, the
   canaries (poison at level 2 once `gte_widescreen` moves), and the full state hash. Plus a TSan build of the mailbox itself.

Size, with the emissive snapshot landed: items 2 and 3 are the bulk (about 2 weeks: they touch every renderer entry point the game
calls), 4 to 6 about a week, 7 and 8 a few days. Risk: the Vulkan context moving threads (queue and device are fine; ImGui's
backend and the swapchain are the checks) and shutdown ordering.

### R4e status (t1-r4e)

Items 1, 4, 5 and 6a are done inline (0 differ against 455e72ea5: menus lane, fight at 16:9, interpolation at present_rate 4 with replay
hashes, canary, pacing, ui smokes):

- **1 emitters**: `pc_ptgi_emitters_gather` (glows from the room's emissive triangles, the emissive lights and the effects' lists, through
  glows.c's ids, smoothing and overrides) runs on the game thread at the header fill (when GI, the shadows, the AO or the probes want them,
  or the fog volumes and particles ask) and the header carries `emitter_*`; the presenter's `update_emitters` copies them.
- **4 UI textures**: at the overlay snapshot the game thread turns each pending texture create, update and destroy of ImGui's own list into
  an operation carrying its pixels (answering the context as the backend would: status OK, the texture id a stable handle) and the cloned
  draw commands name their texture by handle; the renderer applies the operations on `ImTextureData` shadows of its own (the backend
  uploads and destroys by its own protocol) and maps handles to descriptor sets as it draws (vk_overlay.cpp).
- **5 widescreen**: `C_SET_WIDESCREEN` in the stream, emitted ahead of the next record and at each header fill; gpu_hw.c reads its own
  `hw_wide`. Canary level 1 poisons `pc_gte_widescreen` now; level 2 poisons the renderer's own `pc_hw_scale` (what the settings request
  still has to carry) and must differ.
- **6a header per request**: the header is filled once per FRAME request, not per image; the interpolated fractions and the display-paced
  fields read it. The dev overlay (ImGui) is built once per frame and redrawn on the extra images (it was rebuilt per image). The window's
  pixel size is in the header (SDL is the game thread's).
- **6b the thread, lockstep** (`render_thread.[ch]`, setting `render_thread`, `ROOD_RENDER_THREAD=1`): the hw_cmd flush, the present
  service's images (`present_images`) and the SYNC fence run on a renderer thread (Vulkan only), the game thread waiting for each call.
  Nothing overlaps, so direct reaches into the renderer are harmless; `ROOD_RT_TRACE=1` prints, at exit, each `pc_hw_*` hook the game
  thread still called directly. That list is the 3D-maths worker's item 3 (requests on the mailbox) and item 2 (payloads).

**Items 2, 3 and 7 (3D-maths worker, t1-r4e-vram), as they stand:**

- **3 game-thread callers: done, the list is empty.** Every renderer entry the game thread calls is a record, a request with a reply or a header
  setting (`hw_calls.c`: the request keeps its name, applies every record the caller made, then runs gpu_hw.c's `_now` function on the
  renderer's thread; on that thread it just runs). Requests: the RPC dumps and redraws (`pc_hw_dump`, `pc_hw_redraw[_present]`), pick,
  display rect, overlay stats, `pc_hw_present_lists`/`_all_lists`, the casters (a game-side copy refreshed when the serial moves), the native
  VRAM texture handle, the picture's camera (`pc_hw_view_camera`, `replay_camera`, `replay_projection`: the answer is kept until something more
  is posted, `pc_rt_pushed`), `pc_hw_present_world`, `_menu_model`, `pc_hw_detail_view`, `pc_hw_supersample`, `pc_hw_init`, the scale setter,
  the photo hooks (their reply hands the producer's copy the stale set) and `pc_hw_forget_timeline`. Records: `pc_hwc_shape_triangle` (the
  talk bubbles' exact positions), `pc_hwc_skip_present` (the photo and charge-skip frames), and the draw settings (perspective, texture
  filter, subpixel, interpolate, menu studio: `C_SET_SETTINGS` ahead of the next record and at each header fill; the renderer draws from its
  own copies, and `ROOD_PRESENT_CANARY=3` poisons the game's variables during the presenter: `present_canary_smoke`).
  The gate: the RPC `render_thread` lists the hooks the game thread called directly while the thread was on, and `rood_rpc.py` fails a run
  that ends in render_thread 2 with any (`ROOD_RT_DIRECT_ALLOW=1` silences it). `ROOD_RENDER_THREAD=2 python3 pc/tests/run.py <suite>`
  runs a suite under it. Over the photo, editor, emissive, ui, fog, occlusion and shadow smokes in render_thread 2 the list is empty (28 suites).
  `hw_calls_smoke` makes every request once in render_thread 0, 1 and 2 (and changes two settings at run time); `build_boot.py` refuses any
  function that is called and defined nowhere but `SystemError` (a renamed hook was a trap for a day: `pc_hw_native_texture`).
- **2 VRAM payload: done for the readers.** The records already carry the pixels they put into the renderer's array (upload, fill, copy,
  raster post). What was left was the game thread reading that array by pointer (`pc_renderer_vram`: lighting's environment, the emitters'
  texels, emissive, the RPC's prompt tools; 2.4 million calls in an emissive run). `pc_hw_renderer_vram` and `pc_hw_emissive_vram` are now
  requests that copy the array after every record the caller made, kept until something more is posted (`pc_rt_pushed`); on the renderer's
  own thread, or with the thread off, they are the array. A copy published at the renderer's frame boundary was not chosen: the readers
  would see the VRAM of whichever frame the thread had reached, which differs with scheduling (the frames are to be equal in every mode);
  the request's copy is the VRAM as of the caller's own records in every mode. Cost: a 1 MB copy and a wait for the thread, once per
  frame in which a reader asks. (`rvram_put` with no pixels, the read-backs and the renderer's resyncs, still copies from the game's array at apply
  time; gpu_hw.c's comment says it runs on the game's thread; not re-examined here.)

- **7 snapshot, rewind, photo, engine reload:** photo prepare, mark, restore and drop and the timeline forget are requests (each applies
  the caller's records first, so the queue is drained in order); the resync after a restore goes through `pc_renderer_rq`. The photo
  hooks' stale set comes back in the reply. Proven in render_thread 0, 1 and 2 by `photo_mode_smoke`, `photo_foundation_smoke`,
  `photo_pace_smoke` and `photo_full_room_smoke` (the photo smokes pass in render_thread 2 with no direct hook). Snapshot, rewind and engine reload were not re-run in mode 2 here.

Async (the game thread posting without waiting) needs: the header copied into a slot of the request, `PC_HWC_SYNC` a no-op on the
renderer thread, the pacing inputs (`fast`, `charge_hidden`, `fast_forward_held`, `sim_hz`) in the request, the record buffer posted by
copy, and the direct-hook list at zero.
