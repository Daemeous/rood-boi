# PS1 ties

A map of every way the running game is still shaped like a PlayStation. It is for deciding what to
replace next, and in what order.

Written 2026-11-08 from the code at that commit, by reading it (nothing was built or run). Where an
older document disagrees with the code, the code is trusted and the disagreement is listed in
"Where the docs are out of date" at the end. Anything inferred but not confirmed in code is marked
**unconfirmed**. Sizes are rough judgments in agent time, not measurements:
small (about a day), medium (a few days), large (a week or more), very large (weeks, and it touches
most of the game).

## The short version

The game's logic is native x86-64 code, and no MIPS code runs. But it is still a PS1 program that has
been given a fake PS1 to run on. It once kept a 2 MB "RAM" at the PS1's own addresses (gone since S9 step 6) and builds its
pictures as GPU command packets in that RAM. It stops at every vertical blank to hand control to the
host, and it reads its disc by sector number. It talks to the sound chip through chip registers, and it
sees the controller as a PS1 pad. The host (renderer, audio, input, tools) is modern and sits around
this core. The core itself is the part that still behaves like an emulator.

Two things decide how "emulator-like" it feels:

1. **The game's own frame loop is the clock.** Logic, drawing, audio and input all advance inside the
   game's call to `VSync`, on one thread, at the PS1's 59.94 Hz field rate. The game updates its world
   every 2 fields (about 30 per second), sometimes 4. Everything smoother than that is the host
   inventing in-between pictures.
2. **The picture is still described in PS1 terms.** Even where the renderer draws real 3D models, the
   game still produces packets and ordering tables, and textures still live in a 1024 x 512 16-bit
   "VRAM".

## Summary table

"Feels" is what the player can notice today. Status: **PS1** (fully PS1-shaped), **partly** (a native
path exists next to the PS1 one), **native**.

| ID | Tie | Player feels | Status | Size to replace | Depends on |
|---|---|---|---|---|---|
| T1 | The game's loop blocks in `VSync`; host work runs inside it, on one thread | Frame hitches stall sound and input; no unlocked frame rate | PS1 | very large | T4, T7, T13 |
| T2 | Logic runs at 30 or 20 per second; the host interpolates some things | Characters smooth, HUD, menus and many effects step at 30 | partly | large | T1, T4 |
| T3 | Wall-clock time read as PS1 "scanlines" | Done 2026-11-08 | native | done | none |
| T4 | GPU packets and ordering tables built in RAM, then walked | Draw order and effects follow PS1 rules; widescreen and 2D compromises | partly | very large | T13, T14 |
| T5 | Textures and palettes live in a 1024 x 512 16-bit VRAM; a CPU rasterizer still runs | Texture size, 256-colour limits, palette animation | partly | large | T4 |
| T6 | 320 x 224 display area, never-cleared frame buffer, 4:3 assumptions | Widescreen is a set of fixes on a 4:3 game | partly | large | T4, T7 |
| T7 | The GTE (3D maths coprocessor) as a global register state machine | Nothing directly | native (no shared register state) | done | none |
| T8 | 2D, text and effects drawn as packets (the classic UI, most effect plugins) | Original look; steps at 30; no real hi-res UI unless the modern layer is on | partly | very large | T4, T5 |
| T9 | Disc read by sector number from a raw disc image, through the PS1 load queue | "NOW LOADING" and load times of the original design; needs the user's disc image | PS1 | large | T10, T12 |
| T10 | Overlays: fixed RAM slots, statics reset on load, entry by long jump | Nothing directly (loading screens exist because of it) | PS1 | large | T9, T12 |
| T11 | The build still links the PS1 symbol map and reads the original executable | Nothing directly | PS1 | large | T13, T15 |
| T12 | 2 MB fixed RAM at `0x80000000`, a 970 KB game heap, a stack below 2 GB | Hard limits; nothing can grow | PS1 | very large | T13 |
| T13 | 4-byte "guest pointers" and PS1-layout structs | Nothing directly | PS1 | very large | none |
| T14 | The 1 KB scratchpad used as shared game memory | Nothing directly | PS1 | large | T13 |
| T15 | Globals at PS1 addresses; code named after addresses; raw memory in Zig | Nothing directly | PS1 | very large | T13 |
| T16 | The SDK/BIOS layer is hand-written and covers only what this game calls | Nothing directly (crashes with a message on anything uncovered) | partly | medium | T9, T20 |
| T17 | Save states, rewind and the RPC depend on the memory layout | Nothing directly | PS1 | follows T12, T13 | T12, T13 |
| T18 | Sound: the PS1 sound driver writes chip registers; a software SPU renders them | Sound is the PS1's; off the game thread with a device | partly | large | T9, T1 |
| T19 | CD-XA audio decoded from the disc's sectors | Nothing directly | PS1 | medium | T9 |
| T20 | Controller seen as a 16-button digital pad, one read per field | No analog movement; input latency of the game's frame rate | partly | small | T1 for latency |
| T21 | Movies: PS1 STR video decoded by a software MDEC, from disc | 15-year-old video quality | PS1 | medium | T9 |
| T22 | Saves through the BIOS memory-card calls into 128 KB card images | Saves look like memory cards (and move between emulators) | PS1 | medium | T13 |
| T23 | Threading: the game, the renderer's CPU side, the SPU and the RPC share one thread | Anything slow anywhere costs a game frame | PS1 | part of T1 | T1, T7 |

## T1. The game's loop blocks in `VSync`; the host runs inside it

**What it is.** The game is a PS1-style program that loops forever and calls `VSync(n)` to wait for the
next vertical blank. On the PC, `VSync` does not wait: it runs a whole host "tick" (RPC commands,
plugins, window events and pacing, CD, audio, drawing the picture) and then returns to the game. The
game runs on its own stack, switched to at startup, and the host is the code above `pc_vsync_boundary`
on that stack.

**Player feels.** Frame time is game work plus host work in series, so anything slow in the host (a
shader hitch, a heavy lighting frame) delays the game's next logic step, the sound driver and input
reads together. There is no way to run the display faster than the game's own frame rate except by
re-presenting an old frame with interpolation (T2).

**Evidence.**
- `pc/src/boot_sdk.c`: `VSync`, `pc_vblank_wait` (calls `pc_vsync_boundary` or `pc_tick`, then fills the
  pad buffer, then runs the game's VSync callback).
- `pc/src/boot_runtime.c`: `pc_tick`, `tick_frame_work` (RPC, plugins, `pc_window_tick`, `pc_cd_tick`,
  `pc_audio_frame`, then `pc_window_frame`), `run_game_on_low_stack`.
- `pc/src/snapshot.c` header: describes "the game's frames below, the host's above" the vsync boundary.
- `pc/src/game/SLUS_010.40/main.c`: `vs_main_gametimeUpdate` calls `VSync`; `_diskInitRead` and the CD
  queue are polled from the same call.

**Status.** Fully PS1-shaped. Native only in that the loop runs as plain C.

**To replace.** Very large. The game would have to hand a finished frame description to a separate
renderer and keep running. That needs the frame's contents to be a value (T4, T7) and the host's reads
of game state to be safe off the game thread (T13, T17).

**Depends on.** T4 and T7 first (nothing else owns a frame's worth of data); T13 for safe sharing.

## T2. Logic rate, and what the host fills in

**What it is.** The game advances its world once per "game frame", every 2 vsyncs normally, 4 in some
scenes (`vs_gametime_tickspeed`). The host presents once per vsync and, if `pc_hw_interpolate` is on
(it is, by default), draws in-between pictures from the previous and current game frames. It can present
`pc_present_rate` images per vsync (default 1, up to 8, or "match display").

The pacing is the host sleeping until absolute 59.94 Hz deadlines (`FIELD_NS`), whether or not display
sync is on. The interpolation phase is an integer fraction of that field (`k/n`), not real elapsed time.

**Player feels.** Characters, weapons, props, drop shadows, particles and the camera are blended, so
they look smooth. What is still drawn as packets steps at the game's rate: the HUD, menus, text, the
screen-space effect plugins, particles on fixed layers, texture animation and vertex morphs
(`pc/docs/Native_Rendering.md`, "What still steps"). On displays whose refresh is not a whole multiple of
59.94 Hz, the fixed fractions cannot match the display clock; the cadence consequence is
**unconfirmed**.

**Evidence.**
- `pc/src/boot_window.c`: `pc_window_tick` (deadline pacing), `pc_window_wait_fraction`,
  `pc_window_present_rate`, `display_rate`.
- `pc/src/boot_runtime.c`: `tick_frame_work` (the extra presents at fractions).
- `pc/src/gpu_hw.c`: `pc_hw_interpolate = 1`, `pc_present_rate = 1`.
- `pc/docs/Native_Rendering.md`, "Animation in-betweens": the animation system has no fractional time;
  characters change once per game frame.

**Status.** Partly native: the in-betweens are real, but the simulation underneath is PS1-rate and the
host's presentation clock is derived from it.

**To replace.** Large for a true unlocked frame rate: present on the display's clock, from real time
since a frame arrived, with a vsync-aware swap (the doc's own list; `Native_Rendering.md` end). A higher
simulation rate would be much larger, because the game's logic counts ticks (animation, AI, timers) and
nothing here makes them fractional.

**Depends on.** T1 (to present independently of the game's loop) and T4 (to know what is a drawable
object versus a packet that cannot be interpolated).

## T3. Wall-clock time read as PS1 scanlines

**Done 2026-11-08.** The game's elapsed-scanline read (`VSync(1)`, `vs_core_elapsedFrameCounterTicks`) is a fixed step per read from
each vblank (`scanlines_now` in `pc/src/boot_runtime.c`), never the host's clock, so how much the AI does in a frame
no longer depends on how fast the machine is that frame.

## T4. GPU packets and ordering tables

**What it is.** The game builds drawing commands as PS1 GPU packets (polygons, sprites, lines, copies)
in RAM, links them into an ordering table (a linked list bucketed by depth, whose bucket order is the
draw order), and calls `DrawOTag`. The host walks the list in `pc_draw_packet` on the game thread.
Links are 24-bit guest addresses.

Native drawing sits beside this. The room, characters, weapons, props, drop shadows and some particles
are drawn from native meshes (`render.c` lists). They are "placed" in the table by marker packets,
so they keep the PS1's draw order against packets still coming from the game. The game keeps building the
packets for most of them too; the renderer skips what native draws cover.

**Player feels.** Draw order, blending and effect behaviour follow PS1 rules because they are the PS1
rules. Anything the game draws as packets cannot be re-lit, re-textured or interpolated without
special-casing. Widescreen is done by narrowing projection and anchoring packets to screen edges
(T6).

**Evidence.**
- `pc/src/boot_sdk.c`: `pc_frame_table_clear` (what `ClearOTag` and `ClearOTagR` were: registers the table and leaves it empty, no link words) and
  `pc_frame_table_draw` (`DrawOTag`), `DrawPrim`. The lint rule `ordering_table` (`ps1_address_lint.py`, baseline 0) fails on the SDK's names and the
  libgpu link macros in game or engine code. The tripwire is `pc_frame_table_check` (`draw_items.c`, called where a table is drawn): a link in a slot of a
  table, or on a marker, is a packet written into it by hand; it logs `[ot] a raw packet is linked in a table` and `run.py` and `build_compare.py` fail
  on it.
- `pc/src/draw_items.c`: `pc_frame_table_order` (the runs placed in a table, in the order their slots give), `pc_frame_place`.
- `pc/src/boot_runtime.c`: `adopt` (the frame of a table drawn is its runs' marker packets, in that order), `pc_draw_packet`, the marker checks
  (`render_is_marker`, `render_translucent_marker`).
- `pc/src/render.c`: `pc_native_room = 2`, `pc_native_actors = 2`, `pc_native_blend`, `pc_native_objects`,
  `pc_native_shadows`, `pc_native_effects` (all on); `pc_reconstruct = 0`.
- `pc/src/gpu_hw.c`: `pc_hw_begin_ot`, `pc_hw_native_marker`, `pc_hw_effect_packets`.
- `pc/docs/Native_Rendering.md`; `pc/docs/Vulkan_Renderer.md` (Vulkan is the default renderer; OpenGL
  is frozen as a fallback).

**Status.** Largely items and native lists (2026-11-12 survey, `pc/docs/plans/2026-11-12-t4-survey.md`): ENDING, MENUF's leaves,
MENU 0/4/5/7/8/9/D/E, MAINMENU, about 33 effect plug-ins, SCREFF2, battle text, sprites, tiles and stat bars draw as typed
items (`pc/src/draw_items.c`, `pc/docs/plans/2026-11-08-frames-as-data.md`); the room, characters and shadows are native
lists with reference packets. The frame's ordering tables hold no packets and no links (2026-11-17: `pc_frame_table_clear` zeroes a table,
`pc_frame_table_draw` draws the runs placed in it, `pc_frame_table_check` is the tripwire for a packet written into one), the texture
moves are copy items (`pc_frame_move`), the lint rule `gpu_packet_writes` is 0 and no 24-bit link word is written or read in game or
engine code. What is left of T4 is the packets the host still lowers items to and the 3D packet builders' reference packets. No packet or ordering-table import stays pinned (2026-11-21, S9 step 4 / Gitea #2): the mover matched DRAWENV, DISPENV and P_CODE by type name as "GPU packet storage" (they are plain data:
PutDrawEnv and PutDispEnv read fields, a P_CODE is a colour word copied as a value), the text boxes' tables are `u_long [3 * 34]` slots, and the trigger marker's pieces are one record, the tail of
`vs_battle_mapCompletionFlags` (an `int [47]`). What the lint's `vram_pointer` counts (`pc_native_vram()`, the renderer's own VRAM array) and its `io_register` GPU entries
(unbuilt `libpress/LIBPRESS.c`, MDEC and DMA constants) are not RAM-block pins: T5 and the unbuilt file, not S9.

**To replace.** Very large. The code that writes packets is spread through every overlay
(`pc/src/game/BATTLE`, `EFFECT`, `MENU`, `TITLE`, `ENDING`, the packet macros in the shared headers).
Each user must emit typed draw items with explicit sort keys instead. The room and character path shows
the pattern; the remaining work is the long tail. The doc `Native_Rendering.md` lists the effect
plugins as not done.

**Plan.** `pc/docs/plans/2026-11-12-t4-survey.md` (slices S0 and S1 done; S3a before S2).

**Depends on.** T13 and T14 (packets are built through guest pointers and a scratchpad cursor).

## T5. VRAM: the texture and palette store, and the CPU rasterizer

**What it is.** All textures, palettes (CLUTs) and frame buffers live in a 1024 x 512 array of 16-bit
pixels. The game loads images into it with `LoadImage`, animates palettes by overwriting rows of it, and
reads it back with `StoreImage`. A software rasterizer in `boot_runtime.c` still draws into this array
as the reference, and the GPU path mirrors it into an R16UI texture so shaders decode 4-bit, 8-bit and
15-bit texels the way the PS1 does. When native draws cover an area, the CPU rasterizer skips it and
"stale" areas are copied back from the GPU only if the game reads them.

Tools key on this too: material maps are named by a hash of the texels under a texture's UV box, found
in VRAM (`pc/docs/Materials.md`).

**Player feels.** Texture resolution and colour limits are the PS1's; palette flicker and palette-swap
effects work by writing the VRAM array. Menus that read the screen back (a text box over the frame) can
force a GPU-to-CPU copy.

**Evidence.**
- `pc/src/boot_runtime.c`: `vram`, `pc_vram_transfer`, `pixel`, `texture`, the CPU rasterizer.
- `pc/src/gpu_hw.c`: the header comment, `pc_hw_sync`, `pc_hw_raster_skip`, `pc_hw_stale_copy`.
- `pc/src/vk_hw.c`: native VRAM as `R16_UINT`.
- `pc/docs/Native_Rendering.md`, "Without reference packets".

**Status.** Partly native: the display image is native (scaled, lit, post-processed), but its inputs are
still VRAM pixels and the CPU copy of VRAM is still kept exact.

**Render-target capture (the screen copies' way out of VRAM).** A 2D sprite that samples a frame buffer as a
16-bit texture (the menus' cross-fade of the previous screen and the feathers and strips they draw from the
frame, the file menus' rows, the map's darkening and ripples, the opening's dissolves and afterimages:
MAINMENU `_renderBackground`, MENU4 `_fadeScreenLeft` / `_drawMenuBackground`, MENU5 `_darkenBackground`, MENU7 / MENU9 /
MENUE / TITLE file menus, BATTLE `func_800CC128`) used to read a PS1-resolution copy of VRAM made from the
target by a point sample at each PS1 pixel (`vram_from_target`): smooth text at the target's resolution
aliased into blocks, the live text drawn smooth over it ("113 . 113" doubled for the 15 frames of a menu's exit
fade). With the Vulkan renderer such a primitive (`WINDOW_CAPTURE` in its vertices' window word, set when its texels
were served from the target by `own_apply`) samples the render-target capture instead: `pc_vk_hw_capture` copies the
target's colour over the texels it samples into the scratch image at the target's own resolution (binding 4 of the
packet shader), taken before the draw and again for each primitive; a replayed frame takes it from the replay target
(`replay_frame`), so interpolated frames match. Nothing reads VRAM for them: this is the shape T5's
replacement of the VRAM array takes for what reads the frame. OpenGL has no capture: its copy averages each PS1
pixel's block of the target instead of sampling its middle.

**To replace.** Large. Native textures and palettes as assets (decoded once, keyed by asset id, palette
animation as data) would remove both the VRAM array and the CPU rasterizer. That means every
`LoadImage` caller (and the texture-page and CLUT fields in every packet and native mesh) naming an asset
rather than a VRAM position.

**Plan.** `pc/docs/plans/2026-11-12-t5-vram-survey.md`: first an ordered VRAM transfer stream queued with each frame, a
game-side transfer-only mirror and fences for the few readers of the rendered frame (S0 digest, S1 completeness, S2 renderer-side
VRAM from the stream, S3 fences); retiring the CPU rasterizer comes after and is cleanup. The stream is also what lets the
game loop leave `VSync` (T1).

**Depends on.** T4 (packets name texture pages and CLUTs by VRAM position).

## T6. Display area, frame buffer and 4:3 assumptions

**What it is.** The game sets a display area (typically 320 x 224) with `PutDispEnv` and `PutDrawEnv`,
and alternates frame buffers inside VRAM. It never clears the buffer, so stale pixels from older frames
show wherever nothing is drawn (the plan notes the free camera fights this). It culls and
projects for a 4:3 view. 16:9 is done by narrowing the GTE's projected X to 3/4 about the centre,
compressing 2D the same way in the rasterizer, and re-anchoring packets that hug an edge. Several kinds
of 2D (damage numbers, labels, the map's icons) need special handling so they are not narrowed twice.

**Player feels.** Widescreen works but is a stack of corrections; anything the game culls
to its 4:3 view cannot appear at the edges of a wider one (**unconfirmed** case by case). Cutaway
and screen-sized pictures are scaled or anchored rather than redrawn for the wider shape.

**Evidence.**
- `pc/src/boot_runtime.c`: `pc_display_env`, `pc_draw_env`, `pc_display_size`.
- `pc/src/boot_sdk.c`: `PutDispEnv`, `SetDispMask`, `ClearImage`.
- `pc/src/widescreen.h`: the hooks (`pc_gte_anchored`, `PC_ANCHOR_*`, `pc_hw_anchor_packets`).
- `pc/docs/Modernization_Plan.md`, opening section (the free camera and the never-cleared screen).
- `pc/src/screen_owner.c`: a screen that owns the display. The original hides the scene under a picture of opaque 2D tiles
  that covers the 4:3 screen; compressed to 16:9 the picture leaves the view's sides to the scene (Game Over showed a piece
  of the room). When opaque rectangles (opaque texels checked in the renderer's VRAM, T5) cover the whole drawing area, the
  sides' rows no opaque 2D reached are cleared (settings `screen_owner`, `screen_owner_fill`). It reads each 2D rectangle's
  bounds at `pc_hw_set_bounds`, the point packets and draw items both pass, so it survives T4's removal of ordering tables.

**Status.** Partly native.

**To replace.** Large. A view that is described as a camera and a viewport, with the game's culling driven
by that camera, is the real fix. It rides on T4 and T7.

**Depends on.** T4, T7.

## T7. The GTE as a global register machine (done)

**What it was.** The PS1's 3D coprocessor ran as a software library (PsyCross's GTE, MIT) on one global register set:
game code loaded a matrix and a vector, ran an operation, and read results back, so a view set in one function was
consumed in another (and the renderer and game could not run on separate threads).

**What it is now.** No shared register state. The arithmetic is still PsyCross's, bit for bit (rounding, saturation,
FLAG), behind explicit inputs and outputs:
- *Values* (`pc/src/gte_value_core.h`, `gte_value.h`, `game/common/gte.zig`): a view (rotation, translation, projection) and
  a vector in, the registers' results out, on a private register set: project, transform, rotate, RotAverage4, NCLIP,
  AVSZ3, OP, DPCS, the light-matrix MVMVA, ApplyMatrix(SV/LV).
- *The frame view* (`vs_frame_view`, set where the battle's camera is set): what the registers' rotation and translation
  used to carry from the frame start to its consumers. A consumer takes a view from its caller, or the frame's.
- *The projection* (OFX, OFY, H, DQA, DQB, ZSF3, ZSF4) is the screen record in `gte_value.cpp`, set by SetGeomOffset,
  SetGeomScreen and the CTC2 entry point; nothing reads the registers for it.
- *Scratch.* Code that works the coprocessor directly (the ports of the hand-written routines, the SDK's own math
  routines) uses a register context of its own (`vs_gte_ctx`, `gte.Ctx`, one per call) or the calling thread's scratch
  set (`gte_scratch`, `gte_cur` NULL): no state is kept in it between uses.

**Evidence.** `pc/third_party/psycross/ROOD-NOTES.md`; `pc/tests/gte_value_probe.c` (the value functions against the
register sequences they replaced); the `T7` commits of branch gte-t7.

**Status.** Native. The lints that counted the machine (`gte_state_machine`, `gte_sdk_calls`, `gte_register_users`) went
to 0 and were removed.

**Left over.** The SDK routines that work the registers as scratch (RotMatrix, VectorNormal, CompMatrix, ...) are still
PsyCross's register code; a future thread that calls them uses its own scratch set. The projection narrowing
(`pc_gte_widescreen`) and the projection log are still globals (T4).

## T8. 2D, text and effects as packets

**What it is.** The classic HUD, menus, text, loading screens, and most of the effect plugins in
`pc/src/game/EFFECT` and `GIM` are drawn as 2D packets from VRAM sprites and glyphs. The host has
replacements layered on top: `ui_text.c` (glyphs from fonts), `ui_bubbles.c`, `button_prompts.c`,
`ui_upscale.c` (texture magnification), and the modern menus and HUD (`pc/src/ui`, `pc/src/hud`,
`pc/docs/Modern_Menus.md`). With the modern UI off, the game's own sprites and layout are kept on
purpose; only typography is upgraded.

**Player feels.** The original look, at the original update rate, magnified. Menus run on game
frames (steps at 30). The modern menus, where available, are real native UI but read their state out of
PS1-layout structs (T13).

**Evidence.**
- `pc/src/gpu_hw.c` and `pc/src/ui_text.c` (glyph replacement from the packets' code),
  `pc/src/ui_upscale.c`.
- `pc/src/ui/*_bridge.c` (modern menu bridges), `pc/src/hud/*_bridge.c`.
- `pc/src/project_settings.cpp`: `pc_ui_menus` has a static default of 0 here; whether a shipped
  settings file turns it on is **unconfirmed**.
- `pc/docs/Native_Rendering.md`: "Left on packets: the effect renderers ... 2D UI, menus and movies".

**Status.** Partly native.

**To replace.** Very large, but incremental by screen (the modern menu phases show the pace). The effect
plugins are the least explored: the doc says no spell could be cast over RPC to test them.

**Depends on.** T4, T5.

## T9. Disc reads by sector number

**What it is.** The game names every file by its disc position (a sector number, `VS_*_LBA` constants in
`pc/src/game/include/lbaMacros.h`) and loads it through a small asynchronous queue (32 slots, with
priorities and frame countdowns) that the game polls every frame. On the PC the actual read is
synchronous from a raw 2352-byte-sector disc image the user supplies; the queue's states still run so
the game's own logic keeps its timing.

The same disc image is opened by other code: overlay statics (T10), the CD drive for movies and XA
audio (T19, T21), the room catalog tool, and the item name reader.

**Player feels.** The original loading structure ("NOW LOADING", loads at room and menu changes). It
needs the user's disc image at runtime; there is no extracted-file mode.

**Evidence.**
- `pc/src/boot_runtime.c`: `pc_disc_read`.
- `pc/src/game/SLUS_010.40/main.c`: `_diskInitRead` (the PC branch under `ROOD_PC_BOOT_DIAGNOSTIC`, a
  define the PC build always passes: `pc/tools/build_boot.py`), `_processCdQueue`,
  `vs_main_diskLoadFile`, `pc_cd_queue_load`.
- `pc/src/overlay_statics.c`, `pc/src/cd_drive.c`, `pc/src/room_catalog.c`, `pc/src/ui/item_names.c`
  each open the image by name.
- A count (by grep, 2026-11-08): 566 distinct `VS_*_LBA` constants referenced in `pc/src/game` C files.

**Status.** Fully PS1-shaped.

**To replace.** Large. Files named by asset id, loaded by a loader that owns the timing, with the data
formats the game parses unchanged at first. The game's wait-for-loaded loops and the 32-slot queue are
game logic, so removing them is a code change per overlay, not a swap.

**Depends on.** T10, T12 (loaded data is placed into fixed RAM slots and the fixed heap).

## T10. Overlays: fixed slots, restarted statics, long-jump entry

**What it is.** The game is split into overlays that load into fixed RAM addresses: the title, the main
menu, effect plugins and the submenus (each family shares a slot), and BATTLE and ENDING (share another).
The code is compiled in permanently, but the loaded file's bytes still land in RAM as data and
the game reads its tables from there. Because on a PS1 reloading a file resets its variables, the host
snapshots each overlay's initial statics and restores them whenever that file is read
(`pc_overlay_statics_loaded`). Moving between title and battle uses saved jump points
(`__builtin_longjmp`) that replace the PS1's stack reset.

**Player feels.** Nothing directly. This is why menus and effects behave as if reloaded each time.

**Evidence.**
- `pc/src/overlay_statics.c`: `pc_overlay_statics_init`, `pc_overlay_statics_loaded`.
- `pc/tools/build_boot.py`: sections renamed per overlay file; the slot addresses in the source-list
  comments (`0x800F9800`, `0x80102800`, `0x80068800`).
- `pc/src/boot_sdk.c`: `vs_overlay_jumpToTitle`, `vs_overlay_jumpToBattle`, `pc_title_jump`,
  `pc_battle_jump`.

**Status.** Fully PS1-shaped; the code part is native. Since 2026-11-10 the restart table carries each
overlay file's first sector (the game's `VS_<NAME>_LBA`, written by `build_boot.py` into
`overlay_statics_table.c`), so the runtime no longer opens the disc image to walk its directories at startup.
The submenu slot (MENU0-F, `vs_overlay_slots[2]`) has no pinned import left: MENUB's two `items1.h` flags
(`_itemCategoryIconsEnabled`, `_statusCommandState`) are the overlay's own statics. Its load destination is a buffer of
the program (`_submenuSlot`, `main.c`; MENUC, the largest file, is exactly the slot's 0x9800 bytes). What
had kept it at 0x80102800: the main menu's read into slot 1 is 0xF000 bytes (MAINMENU.PRG's 0x9000 and the
menu background after it), so `vs_mainMenu_unpackMenubg` found the background at the submenu slot's start;
it now reads it at slot 1 + 0x9000. `overlay.c` (the MIPS original, not built) is gone.
Every slot but 0 (BATTLE, ENDING) is a buffer of the program now. Slot 1 (MAINMENU.PRG with its background,
INITBTL, SCREFF2) and slot 4 (EFFPURGE) hold nothing pinned. Slot 3 (the effect plug-ins): its 157 pinned
imports are plain data (declared `u_char [][4]`, `int`, `SVECTOR`; none of pointer type) and the link places each at
`pc_plugin_slot + offset` (`imports.ld`). The 2994 in-range words of the plug-in files are jump tables and
other code pointers that no native code reads; `build_boot.py` refuses an effect plug-in source or header
that converts a guest address (`psx_get`, `psx_to_ptr`, `ROOD_GUEST_RAM_BASE`, a 0x80xxxxxx literal), which
is what would turn such a word into a pointer.
Found on the way: in MENUB, `items1.h`'s functions and the rest of the file use two pairs of
`_itemCategoryIconsEnabled` / `_statusCommandState` (the original's static redeclaration made one);
making it one variable changes the loot screen (481 of 717 frames), so it stays as it was.

**To replace.** Large. Overlays become modules with explicit init/reset state; the restart rule becomes
code instead of a memory copy. The effect plugins that reuse function names at one address are the
awkward part.

**Depends on.** T9 and T12.

## T11. The build is tied to the original binaries

**What it is.** The PC build still links the original program's symbol map to learn every global's PS1
address (`main-symbols.elf` and one per overlay, linked with a MIPS linker from the decompilation's
build tree). It also reads the original executable and overlay files to check and generate function
tables, and to size data regions. The original executable is loaded into the fake RAM at startup; it
supplies the game's initial data.

**Player feels.** Nothing directly. It means the game cannot be built, or run, without the original
data and the decompilation's build products.

**Evidence.**
- `pc/tools/build_boot.py`: `link_symbols`, the function-table check ("differs from the overlay"),
  the LP64 import conversion.
- `pc/src/boot_runtime.c`: `main` loads the executable's image into RAM.
- `pc/src/game/README.md`: a fresh clone's setup still extracts game data with the top-level tools.

**Status.** Fully PS1-shaped. How much of the build still depends on the MIPS toolchain is
**unconfirmed** without building.

**To replace.** Large. Replace each address-derived table with data the project owns, which follows
T15 and T13.

**Depends on.** T13, T15.

## T12. Fixed RAM, a fixed heap, a stack below 2 GB

**What it is (S9 step 6, 2026-11-22).** The 2 MB block at `0x80000000` is gone: the host no longer maps it,
the original executable's data is no longer copied anywhere (every import is a native object), and the
BATTLE/TITLE/ENDING overlay slot is a native buffer of the program. What stays: an I/O page that holds
sound chip registers (T18b), the arenas above 4 GB (the game heap, initialised with `0xF2000` bytes, and
the port arena), and a 64 MB game stack at a fixed address below 2 GB.

**Player feels.** Hard budgets: anything that does not fit the PS1's memory cannot exist, which limits
what content the game can hold.

**Evidence.**
- `pc/src/boot_runtime.c`: `main` (`map_fixed` of the arenas), `reserve_game_stack`
  (`GAME_STACK_BASE`), `run_game_on_low_stack`.
- `pc/src/game/SLUS_010.40/main.c` line near `vs_main_initHeap((vs_main_HeapHeader*)0x8010C000,
  0xF2000)` and the same call in `BATTLE/INITBTL.PRG/18.c` and `ENDING/ENDING.PRG/D4.c`.

**Status.** Fully PS1-shaped.

**To replace.** Very large. The heap and the fixed addresses go away only when objects stop holding
guest pointers (T13) and the overlays stop assuming slot addresses (T10).

**Depends on.** T13.

## T13. Four-byte guest pointers and PS1-layout structs

**What it is.** Structs that overlay PS1 data (loaded files, the scratchpad, saves, the packet formats)
keep their 4-byte-pointer layout. Pointer members are declared `psx_ptr(T)` and read and written with
`psx_get` and `psx_set`, so structs keep the PS1 size and field offsets on a 64-bit build. A pointer to
host memory above 4 GB is fatal (`pc_psx_bad_pointer`). Structs that the game builds itself were
changed to native layout.

**Player feels.** Nothing directly.

**Evidence.**
- `pc/src/psx_types.h`.
- `pc/tools/ps1_address_baseline.json` (also `pc/tools/ps1_address_lint.py`, which keeps these
  numbers from rising): `guest_pointer_field` 282, `guest_pointer_access` 2921,
  `address_as_integer` 606.
- `pc/src/psx_sdk_layout.c`: compile-time size and offset checks on the SDK structs.
- `game/common/psx.zig`: the same idea for Zig (`Ptr`, `addr`).

**Status.** Fully PS1-shaped by design: it is how the 64-bit port kept the data files valid.

**To replace.** Very large. One struct at a time, once its data has an owned file format and no other
code overlays it. The lint count is the progress meter.

**Depends on.** Nothing, but it gates T12, T14, T15 and T17.

## T14. The scratchpad as shared game memory

**What it is.** The PS1 has 1 KB of fast memory. The game uses it as shared scratch state: the
current packet cursor and ordering table base, per-actor AI state tables, camera and matrix scratch,
accessed as raw addresses (`0x1F80xxxx`) or by `getScratchAddr`. On the PC this is a real page mapped at
the same address.

**Player feels.** Nothing directly.

**Evidence.**
- Baseline `scratchpad` count: 1020 (2026-11-08; since then the scratchpad is a native block `vs_pad` of 0x408 bytes, reached by `pad.at`, `pad.many`, `pad.addr` and `PC_PAD`; its owner table is pc/docs/memory_layouts/scratchpad-overlays.tsv; the lint's old `scratchpad` rule also counted I/O registers, now `io_register`). By file (grep of the lint's hits): `pc/src/game/BATTLE` 351,
  `game/battle` (Zig) 258, `pc/src/game/MENU` 114, `pc/src/game/EFFECT` 47, `pc/src/game/ENDING` 29,
  `pc/src/scene_submit.inc` 19.
- `pc/src/game/include/scratch.h` (`vs_scratch_t`); `pc/src/boot_runtime.c` maps the page.
- Example of an unnamed shared slot: the AI loops write `*(int*)0x1F8003FC` and index
  `0x1F80037C` by actor id (`BATTLE/BATTLE.PRG/6FC00.c`, `71618.c`).

**Status.** Fully PS1-shaped.

**To replace.** Large. Each slot becomes a named, typed field in an owned struct. The AI state table is
the biggest and the one tied to behaviour.

**Depends on.** T13.

## T15. Globals at PS1 addresses, address-named code, raw memory in Zig

**What it is.** A large part of the game's data is defined by address: the build gives each global the
address it had on the PS1 (`imports.ld`), so tables and structs the original data refers to line up.
About a dozen hard-coded PS1 addresses remain in code (`ps1_address` counts them). Most functions and globals still carry their address as
their name (`vs_render_drawModelFlatPolygons`, `vs_field_gimmickInstances`), a naming fact, not a runtime tie, but it makes the map from
code to behaviour hard to read. Battle's logic is in Zig (1811 `.zig` files under `game/`, 51 subsystems
in `game/ownership.toml`, each function keyed by its PS1 address) and reaches game memory through raw
accessors.

**Player feels.** Nothing directly.

**Evidence.**
- Counts (`pc/tools/ps1_address_baseline.json`, 2026-11-10): `ps1_address` 7, `scratchpad` 52,
  `unnamed_symbol` 20594, `zig_raw_memory` 1036 (mostly in `game/battle`), `address_as_integer` 230.
- `pc/tools/build_boot.py` (`imports.ld`); `game/ownership.toml`; `game/common/psx.zig`.
- 956 data imports in the 64-bit build (`lp64_imports.json`): 655 native, 301 pinned (see below).

**Status.** Fully PS1-shaped in addressing; native in logic. Since 2026-11-10 `ps1_address` is 7 (from
60): what is left is the RAM base (`ROOD_GUEST_RAM_BASE`, `psx.guest_ram_base`), the heap
(`VS_MAIN_HEAP_ADDRESS`), the ordering-table link walk in Zig
(`vs_render_splicePrimitiveList`), and the actor-engagement reads through guest RAM (`no_hostile_near_target.zig`).

**To replace.** Very large, mostly mechanical per unit: rename by meaning, replace the address with a
typed global, and remove the raw access.

**Which globals stay at their PS1 address (survey of the 64-bit build, 2026-11-10).**
The build gives a data import (a global the C only declares) one of two homes: native (defined in
`native_imports.c`, no PS1 address) or pinned (`imports.ld`: the address the original had, inside the
fixed RAM). `pc/build/boot64/lp64_imports.json` (written by `build_boot.py --arch 64`) lists each
import's address, users, declared types and, for a pinned one, `pinned`: the first reason it stays.
956 data imports: 655 native (117 hold pointers, which the LP64 mover widens; 538 are plain data the
mover moves too: scalars, structs and tables, an unsized array of plain elements together with what follows it,
imports whose extents cross as one region), 301 pinned (139 of them in the plug-in slot, placed in
`pc_plugin_slot` by the link, so 162 stay at a PS1 address).

| Where (PS1 address range) | Pinned | Native |
|---|---|---|
| main program (`0x80010000-0x80068800`) | 26 | 126 |
| BATTLE and ENDING slot (`0x80068800-0x800F9800`) | 118 | 529 |
| main menu and effect plug-in slot (`0x800F9800-0x80102800`) | 157 | 0 |
| submenu slot (`0x80102800-0x8010C000`) | 0 | 0 |

Why the 301 are pinned (each counted once, in this order; `lp64_imports.json` says which):
- 11 hold pointers or a type whose layout differs (psx_ptr structs, tables of function or data pointers
  that are not moved natively): T13 has to widen them first. (Before 2026-11-11 this count held every
  unsized array, 206 of them: a table declared `u_char x[]` is plain data whose extent is the only unknown.)
- 0 are GPU packet or ordering-table storage (was 13: a type-name match, and the ordering tables hold slots only since 2026-11-17).
  Any other import whose address is taken may move: a native copy is a pointer like the guest one (the
  address is below 4 GB), the region keeps the guest layout, and a taken import extends to the next import
  because its declared size cannot be trusted (ENDING's image blobs are declared as their 20-byte header).
- 28 have an incomplete type or are over 128 KB; 58 + 22 are unsized arrays, or imports after one, whose
  run (to the next import, and through every import after it that is moved) ends at an import that stays at
  its address: the array may run into it. An unsized array moves only together with the imports that follow
  it, as one native region with the guest layout (each region starts at the guest address's alignment: a
  member keeps its low address bits), each member after an array extended to the next import (the bytes
  between labels are read through the table's base). An import declared with different sizes (D_800E909C:
  `u_char []` here, `vs_actors_TextureAnimationKey [][28]` there; 11 imports) stays: the labels after it lie inside a larger
  table, and moving it with the extent of the next label dropped Ashley's mouth texture in the story scenes.
- 121 are in the plug-in slot: the link places each in `pc_plugin_slot` (see T10), not at its PS1 address.
  2 are shared between BATTLE, INITBTL and the program (one object over several overlays: INITBTL's
  image is small and sits at the start of the slot, so its load resets none of the data past it; BATTLE's
  load restarts the shared data as it does the rest, and INITBTL's code writes its starting values itself).
  ENDING's data
  restarts with ENDING.PRG (`pc_overlay_statics`) and is all native but 13 pointer, packet or
  pointed-at imports. 14 overlap or sit inside a pinned import; 18 share an address with another name; 14 are
  pointed at by a word of a game file. Imports whose extents cross move as one region (the guest layout kept),
  unless one of them stays; overlays that are never loaded together (TITLE's and BATTLE's data share
  addresses) do not count as overlapping.

What the original data really points into (a scan of every PRG, BIN and the main program for words
that fall in an import's range): 116 imports are pointed at by game data and must stay at their
address while the file's bytes are loaded as they are: 6 in the main program (`_trig_table`,
`vs_main_inventoryIndices`, `StCdIntrFlag`, ...), 27 in the BATTLE/ENDING slot (tables in the BATTLE and
ENDING files: `_saveMenuBg_clut`, `_uiTable`, `_menuCopyright`, `D_800F591C` with 114 pointers, ...), 83 in the effect plug-in slot (each plug-in's tables, the highest counts in PLG045,
PLG049 and PLG057). The scan covers the program and every overlay file, not other data files. No test or tool reads a
moved global by a literal address (a grep of `pc/`, `game/` and the tests for each moved address found
none; `ending_smoke.py` reads ENDING's globals by address, and those stay).

What the slots still do:
- `vs_overlay_slots` (`main.c`, T10) is where a loaded file's bytes land: 0x80068800 (BATTLE,
  INITBTL, ENDING), 0x800F9800 (main menu, effect plug-ins, EFFPURGE), 0x80102800 (the submenus). The
  code of every file is native already; the bytes matter only as the initial values of the pinned
  globals above, and as a scratch destination: `load_eff_purge.zig` and `load_menu_prg.zig` load
  into slots 1-3 directly, `MAINMENU.PRG/58EC.c` reads slot 2.
- A slot becomes optional only when every pinned global in it is native or its file no longer
  carries data (the main menu and plug-in slot has 157 pinned, 83 pointed at by data, so it is the
  last).

What moved (`build_boot.py`, the plain-data rule of `lp64_native_imports`): imports that are plain data (no pointers), have a complete
type of at most 256 bytes, nothing else at or inside their address, no word of any game file pointing
into them, an object that does not take their address, and that are in the main program or in
BATTLE's own file image (BATTLE and ENDING data restart with their overlay through
`pc_overlay_statics`; data beyond the file persists as it did). 81 imports: mostly counters and flags
of the sound driver and the battle (`D_8002F544`, `vs_main_*` scalars, `D_800F16E0`..).

To take the rest: widen the 334 (T13), review the 160 by use (packet and DMA destinations are
guest, everything else can move), and wire restart for the plug-in and menu slots.

**Depends on.** T13.

## T16. The SDK and BIOS layer

**What it is.** The original program calls Sony's library (GPU, GTE, CD, sound, pad, memory card) and
the BIOS (events, interrupts, root counters). The PC build keeps the game's calling code but supplies
the hardware side itself: `boot_sdk.c` (draw and display calls, pad calls, events, the single
critical-section flag, `rand`), `cd_drive.c`, `audio_libspu.c`, `memory_card.c`, `mdec.c`, and the
PsyCross GTE. The events and root counters cover only what this game uses; anything else stops with
`pc_unimplemented` and a message. The hand-written MIPS assembly of the original was translated by
hand into C and Zig in `pc/src` and `game/` (for example `battle_gte.c`, `battle_movement.c`,
`actor_*.c`); 97 `INCLUDE_ASM` lines remain in `pc/src/game` as markers for functions that the PC build
replaces (a grep count, includes conditional ones).

**Player feels.** Nothing directly; an uncovered call is a crash with a message (`pc_unimplemented`;
`pc/docs/Unimplemented_Paths.md` lists the sites).

**Status.** Partly native: native behaviour behind PS1 call shapes.

**To replace.** Medium per library. Each replacement is the removal of one family of tie
(T9, T18, T20, T21, T22).

**Evidence.** `pc/src/boot_sdk.c`, `pc/src/cd_drive.c`, `pc/src/audio_libspu.c`, `pc/src/memory_card.c`,
`pc/src/mdec.c`, `pc/docs/Unimplemented_Paths.md`, `pc/ASSEMBLY_INVENTORY.md`.

**Depends on.** T9, T20 (and the others per library).

## T17. Save states, rewind and tools depend on the memory layout

**What it is.** The host features that make this port easy to work on, namely save states, rewind, the
state diff, the RPC and the field mapper, treat the game's state as its memory image plus its native
statics plus the game's call stack, pinned at fixed addresses so a snapshot resumes in a new process.
They work because T12 and T13 hold.

**Player feels.** Nothing directly. It is a cost: changing the memory model needs these features
reworked, or they stop working.

**Evidence.** `pc/src/snapshot.c` (header comment), `pc/src/state_rewind.c`, `pc/src/state_diff.c`,
`pc/tools/snapshot_layout.py`, `pc/tools/field_map.py`.

**Status.** Fully PS1-shaped (by design, and dependent on x86 Linux and 64-bit Windows).

**To replace.** Follows T12 and T13; large once those move, because owned state needs a serialization
format.

**Depends on.** T12, T13.

## T18. Sound: register writes and a software SPU

**What it is.** The game's own sound driver (the PS1's sequencer, compiled natively) writes the
sound chip's voice registers into a plain page at `0x1F801C00` and uploads samples (PS1 ADPCM) into a
512 KB sound memory. The host synchronizes those registers into a software SPU
(`pc/third_party/psycross-audio`, whose reverb and noise were rewritten clean-room) and renders it.
With an audio device, an audio thread runs the driver's 240 Hz tick, the register sync and the render
(`pc_audio_slice`), whenever the device's queue holds less than `audio_latency_ms`. The game thread and it
share the driver's state and the SPU through one recursive lock (`pc_sound_lock`): the audio thread holds it for a
slice, the game thread for each SDK sound call, each sound command (the game's own `DisableEvent` of its timer
event, `vs_audio_mixerDispatchCommand`, masks the tick as the PS1's interrupt mask did) and a save state or rewind. Without a device
(`ROOD_AUDIO=none` or `wav:`, tests, the compare tools) the sound is clocked by the game's fields
(four slices per field on the game thread), because game logic reads the driver's state and a headless run must
depend on the frame count only.

**Player feels.** The PS1's instruments, reverb and sequencing, and a long or hitched game frame no longer
starves or stalls the sound (T18b). The tick is the game thread's data too: a few status words the driver
keeps (channel masks, music ids) are read by game logic without the lock, as the PS1 read them with its
timer interrupt able to land between any two instructions, and the register page's 16-bit words are written
by the driver while the audio thread may sync them (a half-updated voice lasts one slice); neither was run
under a race detector.

**Evidence.**
- `pc/src/audio_libspu.c`: `pc_audio_frame` (the four ticks), `SpuSetIRQ`, `GetRCnt`.
- `pc/src/audio_spu.cpp`: the register page and `pc_spu_sync_registers`, `pc_spu_render`.
- `pc/src/audio_output.c`: the audio thread, the sound lock, the device stream.
- `pc/src/game/include/hw.h`, `sound.h`.

**The arena alias (memory lane): closed 2026-11-18.** For one day the sound driver's parameter words (`Param1`, `Param2`, `PendingSequence`,
`FSoundChannel.unk3C`, the ids that are addresses) were 32 bits naming blobs in the game heap (above 4 GB since S4) through a temporary alias,
`ROOD_ARENA_ALIAS` (0x80200000 plus the offset in the arena). T18 widened them: they are `__UINTPTR_TYPE__` and hold an id (a small number) or a real pointer, a pointer
being told from an id by lying above 0xFFFF. The alias, `psx_word`, `psx_word_ptr`, the arena arm of `psx_addr`, `psx_to_ptr` and `psx_get`, and the `arena_alias` lint meter are deleted.
No 32-bit word names the arenas any more; `psx_addr`'s remaining users are the executable's code addresses (the pinned image is below 4 GB).
Layout table: `pc/docs/memory_layouts/t18-sound-words.tsv`; plan section 3.7.

**Status.** Partly native; off the game thread (T18b, moderate version) when there is an audio device. While a
photo mode render runs the sound lock is held, so the sound is quiet for those frames. CD-XA is still fed per
game field (T19).

**To replace.** Large. Replacing the sample format and the voice model while keeping the sequencing is a
mixer project, and the driver is original code that calls the chip directly.

**Depends on.** T9 (samples load from disc), T1 (to leave the game thread).

## T19. CD-XA audio

**What it is.** Streamed audio (movie sound and any XA streams) is stored as ADPCM sectors and played by
the drive without the CPU. The host's drive (`cd_drive.c`) delivers sectors from the vsync tick at the
disc's rate; `xa_audio.c` decodes them, resamples to 44.1 kHz and feeds the software SPU's CD input.

**Player feels.** Nothing directly.

**Evidence.** `pc/src/cd_drive.c` (`pc_cd_tick`), `pc/src/xa_audio.c`.

**Status.** Fully PS1-shaped. What else uses XA besides movie sound is **unconfirmed**.

**To replace.** Medium: convert once to a modern codec at build time, and play it through the host mixer.

**Depends on.** T9.

## T20. The controller as a digital pad

**What it is.** Each field the host puts the pressed buttons into the pad buffer in the PS1's digital
pad format (`pc_vblank_wait`: `0x41`, then the inverted 16 buttons). The game reads that buffer at its own
rate. Sticks, triggers and the keyboard are mapped onto the 16 digital buttons by the host.
The right stick and the photo mode are host features that do not go through the game's pad.
`PadSetAct` registers the game's actuator buffer; the host reads it each field and drives the gamepad's
motors (`pc_controller_rumble`, small motor on/off, large motor 0..255, scaled by `rumble_strength`). The original
does use it: `vs_main_setVibrateParams` (called each frame in battle and the field, and the title's fades) turns the
vibration blocks of effect and event scripts into the bytes while its Vibration option is On (confirmed by
sampling the bytes in a fight: the large motor ramps on hits, the small one blips).
The left stick can give the game analog movement (`analog_move`, off by default): see Status.

**Player feels.** Digital movement only (as the original); input is read once per field and acted on at the
game's frame, so input can land up to a game frame late (**unconfirmed**; not measured).

**Evidence.** `pc/src/boot_sdk.c` (`PadInitDirect`, `PadGetState`, `PadSetAct`, `pc_vblank_wait`),
`pc/src/boot_window.c` (`pc_window_buttons`), `pc/src/boot_gamepad.c`, `pc/src/input_record.c`.

**Status.** Partly native (host input, camera and photo controls are native; rumble and analog movement
are native, T20a). Analog movement is Ashley's movement input only (`vs_actors_updateAshleyMovementInput`: the stick's angle as the
heading, its travel choosing the game's two paces, the D-pad's run and R2's walk); menus and targeting still
read the digital pad (the stick also sets the D-pad bits, as before). Not recorded by the input recorder (the
left stick is not in its event log), so a replay of a recording made with the setting on plays the D-pad's.

**To replace.** Medium for lower latency (needs T1); analog-stick targeting and menus if wanted.

**Depends on.** T1 for latency.

## T21. Movies

**What it is.** The intro movie (and any other) is PS1 STR video: the host's drive streams its sectors,
the game's code decodes the Huffman stream (a hand translation of the PS1 routine) and a software MDEC
turns it into pixels in VRAM, in 24-bit mode, which the display shows.

**Player feels.** The original video, at its original quality and frame rate (**unconfirmed** that there is
no upscale or replacement).

**Evidence.** `pc/src/mdec.c`, `pc/src/cd_drive.c`, `pc/src/game/TITLE/TITLE.PRG/22C.c` (`_playMovie`),
`pc/src/boot_runtime.c` (`pc_skip_movies`).

**Status.** Fully PS1-shaped.

**To replace.** Medium: re-encode the movies offline, play them through a host video path, and keep the
game's wait-for-end logic with a simple end signal.

**Depends on.** T9.

## T22. Saves through memory cards

**What it is.** The game saves by calling the BIOS card functions (`bu00:`, `bu10:`). The host serves
them from 128 KB card image files, in the PS1 layout (and several emulator container formats), so saves
can move between this port and emulators. Every card operation completes at once.

**Player feels.** The game's own card screens and slots. Saves are portable to emulators.

**Evidence.** `pc/src/memory_card.c`.

**Status.** Fully PS1-shaped; native I/O.

**To replace.** Medium: a native save file with the same fields, plus an importer and exporter for card
images if portability should stay. The game's save and load screens would be talking to something other
than a card, which touches the card UI code.

**Depends on.** T13 (the save structures are PS1-layout).

## T23. Threading

**What it is.** The game, the host's CPU-side rendering, the CPU rasterizer and packet walk, the
sound driver and software SPU, the RPC, the plugin scripts and window events all run on the main
thread in turn. Other threads are only: the audio device's callback (drains the ring), shader compile
jobs, and some background tool jobs (light suggestion, atmosphere, fixture editing). The GPU works
asynchronously (two frames in flight on Vulkan).

**Player feels.** Anything slow in one place costs a game frame. Moving rendering off the game thread is
how the display clock and the game clock would separate.

**Evidence.** `pc/src/boot_runtime.c` (`tick_frame_work`), `pc/src/audio_output.c` (ring and mutex),
`pc/src/shader_reload.cpp`, `pc/src/lights.cpp`, `pc/src/atmosphere.cpp`, `pc/src/fixture_editor.cpp`
(each starts a worker), `pc/docs/Vulkan_Renderer.md` ("two frames in flight").

**Status.** Fully PS1-shaped by default.

**To replace.** Part of T1.

**Depends on.** T1, T7.

## Dependency picture

```
 T13 guest pointers & PS1 layouts ──┬─> T12 fixed RAM / heap / stack ──┬─> T17 save states & tools
 T15 addresses & names              │                                  │
 T14 scratchpad                     ├─> T10 overlays ──> T9 disc reads ├─> T11 build ties
                                    │                    │              │
 T7 GTE state ──┐                   │                    ├─> T19 XA audio
 T4 packets / OTs ──> T5 VRAM       │                    ├─> T21 movies
        │               │           │                    └─> T18 SPU samples
        │               └─> T8 2D / UI / effects
        └─> T6 view & widescreen
 T4 + T7 ──> T1 loop, thread, clock ──> T2 true frame rate, T23, T18 off the game thread, T20 latency
 T3, T16, T20 (analog), T22   mostly independent
```

What this says in plain order:

1. **Nothing in memory can go until the pointers do (T13).** T12, T14, T15, T17 and T11 all wait on
   guest pointers being gone, struct by struct. The lint counts (`ps1_address_lint.py`) are the progress
   meter, and they only go down.
2. **The loop and clock (T1, T2, T23) are the last big step, not the first.** Separating rendering from
   the game's loop needs frames to be values (T4), the GTE not to be a global (T7), and the game's data
   not to be shared by raw address (T13).
3. **The disc chain runs in one direction: overlays (T10), then disc reads (T9), then everything that loads
   from the disc (movies T21, XA T19, sound samples T18).** Doing T9 alone would still leave overlay
   memory behaviour behind it.
4. **Several ties are independent and cheap:** T3 (scanline clock), T20 (analog and rumble), T22 (a native save
   file), parts of T16, and per-screen moves under T8 (the modern UI layer already shows the pattern).

### Which ties the player would notice if removed

- **Frame rate and smoothness (T1, T2):** the biggest. Menus, HUD and many effects would move at the
  display's rate; input latency would drop.
- **Widescreen, resolution and texture quality (T4, T5, T6, T8):** the sharpest visual effect, by screen: no
  4:3 culling, real hi-res UI, textures that are not 256-colour pages.
- **Loading (T9, T10):** possibly instant room and menu loads. Needs the data format work first.
- **Audio (T18, T19):** richer instruments or hitch-free audio. A smaller visible change.
- **Analog input (T20) and movies (T21):** easy to notice, easy to do, and limited by what the game's logic
  and media allow.
- **Invisible, but they decide the speed of everything else:** T7, T12, T13, T14, T15, T17.

## Where the docs are out of date

- `pc/docs/Modernization_Plan.md`: describes a 32-bit ABI, native code pinned below 16 MB for 24-bit
  ordering-table links, and a byte-identical PS1 build checked by `make commit-check`. The 64-bit
  build is the only one (the 32-bit build was retired); the game now lives in `pc/src/game` and is edited
  there ("Nothing compares it with an original", `pc/src/game/README.md`). Its progress notes
  ("32-bit remains the default") are history.
- `pc/docs/LP64_Audit.md`: a snapshot from 2026-10-09 (93 of 211 units failing to compile, no 64-bit link
  yet). The 64-bit build links and runs; use `ps1_address_baseline.json` for current counts.
- `pc/ASSEMBLY_INVENTORY.md`: paths are to the old top-level `src/` tree, and the count (95) differs from
  a current grep (97 `INCLUDE_ASM` lines in `pc/src/game`, including conditional ones).
- `pc/src/boot_runtime.c`'s `next_packet` and a few comments keep 32-bit branches (`sizeof(void*) == 4`)
  that cannot run now.
- `pc/docs/Modern_Menus.md` says the default is `original` "until the first screens are verified". The
  code's static default for `ui_menus` is 0 (`project_settings.cpp`); whether shipped settings change it
  is not confirmed here.
- `pc/src/psx_types.h`'s header says it is for "matched src/ code" whose objects stay byte-identical on
  the PS1. That no longer applies to `pc/src/game`.
- The name `ROOD_PC_BOOT_DIAGNOSTIC` (the define that switches the game's disc reads to the host read) is
  a leftover from the first slice; it is on in the normal build.

## What this map could not confirm

- Whether the build still needs the MIPS toolchain and the decompilation's build products for anything
  beyond symbol addresses and tables (no build was run).
- The current count of data imports at PS1 addresses (the last count, 941, is from 2026-10-09).
- Whether AI timing (T3), edge culling in widescreen (T6), audio underruns (T18) and input latency (T20)
  are actually noticeable in play.
- Which XA streams exist besides movie sound (T19), and whether the original uses vibration (T20).
- Whether a refresh rate that is not a multiple of 59.94 Hz judders with the present-rate scheme (T2).
