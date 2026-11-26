# rood-studio: a standalone model studio on native decoders

Status: design, 2026-11-24. Milestones 1, 2 and 3 are built and tested ("Milestone 1" to "Milestone 3" at the end). Follows the spike (`2026-11-24-standalone-studio-spike.md`, branch `studio-standalone`): that found the models cannot be drawn without the game's loaders, so this builds the loaders' native twin instead of hiding a game behind a window.

## What it is

`rood-studio` starts, lists the disc's characters, enemies and weapons, shows one model on the studio's stage with the game's renderer and lighting, and (later) paints it live with Aseprite while a game runs beside it. There is no game in it: no title, no save, no room, no overlays, no game loop, no game frame. The models are read from the disc by decoders that write native structures. This is the first piece of "native models": the same decoders are what the game's loaders switch to later, so the game stops copying model files into a PS1-shaped heap.

## What is the same as the game, and why that matters

- The renderer (`render.h` meshes, lists, instances; Vulkan, ray tracing, GI, probes, shadows, NRD, post), the materials and emissive code keyed on content (`model_key.h`), the texture stores (the emulated VRAM image the renderer samples by page, CLUT and box), the lights and the environment. A model painted in the studio is therefore found by the game by the same key, and looks as it does in the game.
- The disc reader `pc_ui_disc_read` (`ui/item_names.c`), which needs no game state.
- The studio's own code where it is independent of the game: the catalog, the looks, the stage geometry, the camera maths, the locate maths, the RPC.

## Where it lives: one source tree, one more entry point

The first cut is a second way into the same executable file: `rood-studio` is the game binary started as `rood-studio` (or with `--studio-app`), and `main` hands over to `pc_rood_studio_run` after the window and renderer are up, before any game code runs. The game's units are linked but never called: the game's `main`, loaders, overlays and loop do not run, no game memory is used by the studio's own code, and the process never opens a card or the title. This is chosen over a second link for now because the build (`pc/tools/build_boot.py`) links the host units and the game units with generated tables that bind them (the pinned arena, the trap and import tables); a host-only link is a build-system project of its own (listed under M4) and does not change what the studio does. The release can ship `rood-studio` as a copy of the binary under another name until then. Everything that is new lives outside the game's units: `pc/src/native_model/` (decoders), `pc/src/rood_studio.c` (the app).

Where the studio still touches game-side state, it is named here as a debt:
1. The renderer reads some state that the game's units define (the draw environment, the GTE screen record, VRAM). The app sets it through the same SDK calls the game uses (`ResetGraph`, `SetDefDrawEnv`, `PutDrawEnv`, `PutDispEnv`, `LoadImage`); they are the host's `boot_sdk.c`, not game code.
2. M1 draws no room scene. The stage (M2) must reach ray tracing, probes and shadows as the room does; the in-game studio does that by writing a room-format block over the loaded room. The standalone studio hands the same block to the renderer's scene capture (`scene_capture.c`) without a loaded room; that function reads `vs_battle_roomData` today and is the one place left to open up.

## Native decoders

Language: C, in `pc/src/native_model/`. Reasons: the studio is C and C++ host code; the decoders are pure data transforms with no game types; they need no Zig-only feature (no overflow-checked arithmetic that the loaders rely on, no comptime); and the game's Zig loaders can call C through their `c` module the way they call `LoadImage` today. The format knowledge already written in Zig (`shp_file.zig`, `wep_file.zig`: 80 lines each, documented layouts) stays the reference until the loaders move; at the seam below the Zig files become thin callers of the C decoders, so the layout is written once.

What they produce (`native_model.h`): `pc_nm_model`, owned by the host (malloc'd, `pc_nm_free`): the bones (armature table: length, parent, kind, lock), the vertex groups and vertices, the polygon records as words, the texture (width, height, depth, palette words, texels), the shared-texture id, the sequence block bytes (kept raw for the pose decoder), and for a WEP the palettes and bone info. Functions: `pc_nm_shp_decode`, `pc_nm_wep_decode`, `pc_nm_polygons` (the renderer's `render_polygon`s, with a texture slot's rows added as an argument), `pc_nm_content_key`, `pc_nm_mesh_hash`, `pc_nm_pose_transforms` (the skeleton's chain), `pc_nm_euler` (the actors' Euler order, Rz * Ry * Rx).

Formats are written from this repository's own loaders and the in-game results; nothing is taken from outside the repository (CC0 stays clean). Community notes on the formats were not used.

### The seam for the game's loaders

The game's loader today: read the file into a heap buffer, copy the model block to the heap, add the texture slot's rows to every texture coordinate in place, `LoadImage` the texture and palettes to the slot, and keep pointers in the actor record. The seam is the point just after the file is read: a loader that is handed a `pc_nm_model` instead of a buffer address does the same three things from it (model block as the renderer wants it, `u_add`/`v_add`, `LoadImage`). Order of switching, one format at a time, each behind the in-game studio's parity test (below): WEP (smallest, no skeleton), SHP, ZUD's SHP part, then SEQ (the sequence block the decoder keeps raw today). Nothing in the game changes in M1.

## Textures: the same VRAM the game fills

The renderer samples the emulated VRAM image. The app fills it as the loader does: the texture at the texture slot's origin (`vs_actors_textureSlotOrigins`, `vs_actors_textureSlotRowOffsets`; 24 slots, constants in `rood_studio.c` checked against the game), the palette words at the slot's CLUT rows (768, 228 + slot) and (768, 246 + slot), the tpage and CLUT words as `GetTPage` and `GetClut` make them. Painted materials are keyed by content (`model_key.h`, the VRAM page left out), so which slot the app picks does not matter; the app picks the one the in-game studio gets first for the same model so that pictures can be compared pixel for pixel.

## Pose

Bone matrices are the chain `vs_actors_drawActorModel` builds (`actor_prepare.c`): each bone's turn from its pose angles, composed with its parent's matrix, its place the parent's place less the parent's length along x. M1 poses are given as angles per bone: all zero (the rest pose), or angles read from a file or from the game for tests. M3 decodes the sequence block's first frame (`vs_actors_initializeActorAnimationPose`: per-bone big-endian angle triples, doubled) for the idle animation, and the animation lookup by animation id.

## The stage and the light

M1 draws on a plain backdrop. M2 builds the in-game studio's stage (the lathe: floor, fillet, wall; `studio.c build_stage`) from the same function, moved to a shared file that writes into any buffer, and gives it to the scene capture as the room, so ray tracing, probes and shadows see it; the studio's looks (`neutral`, `flat`), the sun and sky go through `pc_env_studio` and the room-settings layer as they do in the game.

## Proof: the in-game studio is the reference

The in-game studio (`studio.cpp`) already draws every model through the game's own loaders. RPC `studio {action: dump_model, path}` (new) writes what the renderer was given for the model it drew: the polygons, the bone palette, the instance's page, CLUT and colour, and the camera. `pc/tests/rood_studio_smoke.py` boots the game with no title (`ROOD_STUDIO_BOOT`), summons models in the in-game studio, dumps them, runs `rood-studio` on the same models and compares:
1. Decode equality, per model: every polygon's positions, texture coordinates, vertex colours, bones, code, flags, bias; the content key; the texture page and CLUT words. Characters (several), an enemy (ZUD) and a weapon.
2. The pose chain: given the angles recovered from the game's palette, the studio's transforms equal the game's.
3. The picture: the same camera, look and pose; pixel difference reported and explained (the painter's order of the game's lists against the studio's depth test; noise of the path tracer).
Also a catalog sweep: every SHP and WEP the catalog names decodes with no error and a key.

## Milestones

- M1 (built): the decoders (SHP, WEP, ZUD's SHP), the app's entry, the window or headless EGL, texture and CLUT in VRAM, the model in the rest pose on a plain backdrop with the renderer's lighting and path tracing, `--dump` for a picture; the parity test above (decode equality for characters, an enemy, a weapon; picture comparison for characters). Registered in `run.py`.
- M2: the stage as a room scene (RT, probes, shadows), looks, the key light, backdrops, orbit camera, framing (the studio's `fit_camera` maths), the catalog list as the home screen (ImGui), weapon on a rig, enemy equipment.
- M3: the sequence decoder: idle frame of an animation, then the animation list and a scrub; turntable.
- M4: live paint and the locator against `rood-studio`'s own RPC socket, `paint_live.py` pushing to the game's socket and the studio's; a host-only link; the game's loaders switch to the decoders (one format at a time) behind `rood_studio_smoke`.

## Milestone 1: what was built

- `pc/src/native_model/native_model.{h,c}`: SHP, WEP (and a ZUD's SHP) decoders; the polygon builder for both record layouts; the content key and the legacy mesh hash; the skeleton's chain (`pc_nm_pose_transforms`) with the actor's scale and the sequence's scale keys; the rest pose from the model's own sequence block (animation 0, frame 0: turns, scale keys, the root's offset).
- `pc/src/rood_studio.c`: the app. Started as `rood-studio` (a name of the same executable) or `--rood-studio`; `main` hands over after the window and renderer are up. Reads the model from the disc (`pc_ui_disc_read`, catalog ids), puts the texture and palette into VRAM at a texture slot as the loader does, builds the mesh and the stage, then every frame is the frame's items alone: the stage's room list, the model's object list, `pc_frame_draw`, `VSync`. Options: `--character N`, `--enemy N`, `--weapon N`, `--look neutral|flat`, `--frames N --dump FILE.ppm` (a picture after N frames), `--reference FILE --expect-key HEX` (compare with the game's dump, draw with the game's palette and camera), `--check-catalog`.
- `pc/src/studio_stage.{h,c}`: the stage's room-format block and its polygons, moved out of `studio.c`, which now calls it (the in-game studio is unchanged: `studio_smoke` passes).
- The in-game studio's RPC `studio dump_model {path}` and `render_mesh_reference` (render.c): what the renderer was given for a model, the proof's reference.
- `pc/tests/rood_studio_smoke.py`, registered in `run.py` (a heavy suite).

What the proof found (this build, GI on, flat look):
- Decode equality, zero differences in position, texture coordinates, vertex colours, bones, codes, flags, bias, page and CLUT words, and the content key, for characters 3, 0, 16, 179 (coloured records) and 58 (a creature, coloured), an enemy (Zombie Fighter: its SHP out of the ZUD) and a weapon (the dagger). Every model the catalog names decodes: 202 characters, 127 weapons, 78 enemies.
- Pictures from the game's palette and camera: the backdrop is identical to the pixel (0.0 mean difference); characters 3, 0 and 58 differ by 0.04, 0.01 and 0.1 levels on average over the whole picture (0.04 to 0.09 percent of the pixels by more than 24 levels, on the model's edges); the enemy differs on its weapon, which the game's studio draws on the hand bone and M1 does not (0.3 levels, 0.6 percent).
- The skeleton's chain: the turns recovered from the game's palette, put through the studio's chain, give the game's bones back to within 4 units for every bone except the hands the game holds with a pose lock (18 and 19 and their children for character 3): those are not turns of the animation and are the sequence decoder's (M3) to explain.
- Rest pose: the model's own base sequence (animation 0) is the game's idle pose for this character, to the eye; for the others it is the base pose, not their idle animation (idle comes from shared sequence files: M3).

Known differences from the game's picture in M1: no weapon on an enemy, a weapon alone is drawn without its holder's hand place (identity bones), no shadow beneath the model (the game's drop shadow is the actor's, not the renderer's), the sun and sky of the neutral look are the studio's data (the same as the in-game studio's), the model's colour is the studio's flat 0x808080 (the in-game studio's `stage.actor_light`).

### M1 corrections found by looking at the pictures

A weapon's CLUT is its file's first palette followed by the material's palette (`_loadMaterialPalette`: the table `_weaponMaterialPaletteMap` picks one of the file's seven); M1 uploaded only the first, so the blade (the material's colours) was missing. The weapon is now compared by picture as well as by mesh (pixel-identical to the game's). The coloured layout's vertex colours are kept (a flat model takes the studio's colour; a coloured one its vertices').

## Milestone 2: what was built

rood-studio is the in-game studio's code with no game under it. `studio.cpp` has an app mode (`pc_studio_app_begin`): no photo hold, no render requests; every frame the app asks `pc_studio_app_frame` for the studio's state (the model asked for, the stage's measures, the backdrop, the facing the turntable gives, the camera, the look) and tells it what it measured (`pc_studio_app_measured`: the model's world box, for the camera's fit, and its meshes, for the locator). So the catalog, the looks and their data, the camera and its framing, the turntable, the backdrops, the Studio window's controls and RPC `studio` are the same code in both programs.

- Enemy equipment and a weapon on its hand bone: a ZUD's weapon and shield (`weaponWep`, `shieldWep` parts, their materials) are WEP files placed by the holder's bone that carries the code 0xF0 / 0xF1 (`pc_nm_find_bone`); their bones are the weapon chain (`pc_nm_weapon_transforms`: bone 0 the hand's matrix, the others from the weapon's own armatures and bone info). A weapon alone is held by the studio's rig (`model.rig`, `OBJ/00.SHP`), which is not drawn. Proved against the game's dump of an enemy's axe: polygons, page and CLUT words and key equal, and the bones from the game's hand matrix to 0.006 in rotation and 0.6 units in place.
- The camera maths, orbit, zoom, framing: the studio's. The pointer on the picture orbits (drag) and zooms (wheel), F frames, T starts the turntable; RPC `studio camera` and the sliders do the same.
- Backdrops and looks: `neutral` and `flat` (their sun, sky and room settings, applied every frame as the in-game studio does with every render, so a settings reload cannot give the room's own values back), and the solid backdrop: no stage, a tile of the colour drawn first, exact to the level (VRAM holds five bits a channel, a tile does not).
- The home screen is rood-studio's own: the catalog (kind, filter, list) on the left, the controls on the right, the picture in between and behind; the Studio window's two halves (`controls_body`, `catalog_body`) are the same functions the editor's Studio window uses. The keyboard and pointer go to the panels (`pc_studio_app_active` in `dev_overlay.cpp`). Not the developer editor: it is available (F1) but not the front.
- Tests: `rood_studio_smoke.py` also starts the app and drives it through RPC: the panels are on screen, summon, orbit, zoom, the three solid backdrops to the level, the turntable, an enemy with its axe, a weapon alone.

Known limits after M2: characters show the base pose of their own SHP (the idle comes with the sequence files: M3); the held-weapon hand pose locks are not applied; no drop shadow; the catalog names enemies (the game's MON.BIN) but characters and weapons by id; the panels are fixed to the window's edges (no docking); the editor's other panels (the locator's) work through RPC but have no screen yet.

## Milestone 3: what was built

The sequence decoder (`native_model.c`, written from the Zig in `game/battle`: `animation_lookup`, `vs_actors_initializeActorAnimationPose`, `vs_actors_decodeAnimationTrack`, `func_800AFA28`) and the screen around it.

- Tables and numbering: a model's own table (animation 0, the SHP's), a ZUD's common table (1..99) and battle table (100..199). `pc_nm_find_animation` and `pc_nm_list_animations` find them as the game's lookup does; the game's own choice for the studio is animation 1 plus the weapon-drawn 100, falling back to the model's own (so a character stands in its SHP's animation 0, an enemy in its battle table's 101).
- The player: one track for each bone's turns, scale keys and the root offset, each a first vector and packets of frame deltas, decoded a frame at a time (`pc_nm_anim_pose(time)`: the pose after `time` steps; going back restarts the track). Time runs at the field's 30 frames a second.
- Pose holds (`vs_actors_setActorBonePoseLock`, events 56 and 57): read out of the animation's event script (the table of command lengths is the game's); a held bone stays where it stood when the event fired, whatever its parents do, until the release. The idle animations of every model tried have none (the "held hands" of character 3 in M2's list were the proof's own error: the sequence's pose matches the game's bones to 2.4 units); the holds are in 32 events of a few enemies' attack animations (the Golem, enemy 127, animations 164 to 167: bones 22 and 23, frames 17 to 41).
- The screen: Animation (the model's own numbers and "auto"), Play, Speed, Frame (a slider to scrub); RPC `studio animation {id, time, play, speed}`; the state's `sequence` lists the model's animations, their length and the one shown.
- No drop shadow is added: the in-game studio hides the actors' drop shadows (`pc_studio_hide_bodies` bit 17); the key light's own shadow on the stage is the renderer's and is there in both.

What the proof found (`rood_studio_smoke.py`): the animation decoded here fits the game's bone matrices to within 4 units on every bone, at the game's time (the game's render replays a few frames from where it held, so the test finds the time that fits best and it is always a frame near the start): characters 3, 0, 16, 179, 58 (animation 0), enemies 14, 4, 26, 40, 70, 95 (animation 101 and the like, 30 to 91 frames), and two other animations started in the game some frames in (`model.animation_frame`, new: `vs_actors_playActorAnimationAtFrame`): enemy 14's 116 and the Golem's 164 at frame 10.

The holds, checked against the game playing the animation itself. The game's start-at-a-frame cannot show them (it runs the events in one step, no frame drawn between, so the matrix a hold saves is whatever the actor held before: its bones 22 to 29 collapse to one point). The studio's `live` RPC (a test aid: `live {kind, id}` builds the model once in the game as it runs, not held; `live_dump {path}` writes what the renderer draws of it) lets the game's own frames play the Golem's animation 164 from its first frame. The game then shows the Golem at one animation frame for each two vsyncs; the decoder's pose fits the game's bones to within 4 to 5 units before the holds (frame 14), and, with the holds, to within 12 units after them (the game's frames 40 and 47, animation frames 20 to 23: bone 23 held since frame 17), against 50 to 90 units with the hold at the frame before the event and 30 to 80 with none. The residue is the animation's half-frame: the game's pose between two of the decoder's frames. The hold is therefore the bone as it stands in the frame the event fires in (not the one before), frozen against its parents' later motion. `animation.holds` (studio data; off: the decoder plays the animation without them) switches them; they are on. `rood_studio_smoke.py` checks the Golem at the game's frames 14, 40 and 47.

## Milestone 4a: live paint and the locator on the app's socket

- `paint_live.py` (the Aseprite bridge) paints into a list of RPC sockets: `Bridge(folder, rpc=[...])`, `--rpc SOCKET` repeatable, by default `pc/build/boot/rpc-studio.sock` (rood-studio, `pc/run-studio.sh`) and `rpc-live.sock` (the game, `pc/run-game.sh`), each when it runs and picked up when it starts. A stroke goes to every target that is up (`material map_set` / `map_clear`); a target that goes away is dropped and reconnected on the next stroke, and one that comes up (or restarts) gets everything again. The texel locator and the studio's pick talk to the first target that is up (the studio's, else the game's own studio). The extension's greeting gains `targets` (which are up).
- rood-studio's side needed nothing new in the studio's data path: the export (`paint_roundtrip.py export model --game SOCK --model studio`, which finds the model by looking out from the middle of the picture), `material map_set`, `studio locate` and `studio picked` work on the app as on the game, because the app registers its meshes and content keys with the same emissive and capture code. One change to the export: a running game that was not paused is left running (it was left stopped at the frame the export stepped to, which in rood-studio is the whole program).
- Proof (`rood_studio_smoke.py`, on the running app): the model's paint package is exported from its socket; a Metal stroke over the whole page changes the picture, an emptied layer puts it back (0.5 levels), and a second target receives the same map (237 calls: a model's page is boxes of textures); Aseprite's cursor on a pixel is found on the model (`studio locate`: triangles >= 1); a click on the model selects its texel in Aseprite (`picked`); `aseprite_test.py` covers the bridge's targets offline (several up, one dropped, none up, the default sockets).
- Not done and not tried: Aseprite itself (not installed here: the extension's messages are the test's), a game and rood-studio running together (one game process at a time here; the second target of the test is a recorder).

## Estimate after M4a

The host-only link (a second executable of the host units alone: the build script's work, no change to what the studio does): about 1 to 1.5 days. The game's loaders switching to the decoders one format at a time behind the parity test: about 1.5 days for WEP and SHP, the sequence loader after. About 3.5 to 4 days from here.

## The picture does not depend on the machine

A picture asked for (`--dump`) made `rood_studio_smoke` vary under load (a character's edges, 0.06% of the pixels on a quiet machine, 0.26% on a busy one). Cause: with `--fast` the game thread runs ahead of the presenter, so the number of images presented by a given frame (what the path tracer's history, the denoiser and the exposure follow) was the machine's, not the game's. rood-studio now waits for each frame's image to be presented before it makes the next when it is asked for a picture; the strict limits are back (0.2% for a character against the game's studio) and so is the turntable's 0.25 s step.

## M4c: the game's loaders read through the native decoders

One format at a time, each its own commit, each proven the same way: the studio's parity test, the model suites (animation, weapon slots, two-model enemy, port arena, menu models, texture animation), the old-binary against new-binary comparison (fight frames, casts, drops, every actor's state, audio, and the equipment/status/items/magic/arts/abilities menu model views), and the lint.

- WEP: the game's loader takes the file's layout (counts, offsets, palettes, texture, bone info) and the polygon fix-up from native_model. No frame differs (0 of 120). No de-PS1 meter moved.
- SHP, including the ZUD's: the same for the shape file; the ZUD part decoder is the native one and the game's own copy is deleted. 0 of 120 frames differ. No meter moved.
- SEQ: sequence blocks, shared sets and the per-table animation record lookup are read by native_model; the Zig reader (seq_file) is deleted. 0 of 120 frames differ, menus identical. unnamed_field fell 11360 to 11359.

The format knowledge now exists once, in native_model (shared with the game through a header with no includes). What is still shaped like the PS1: the guest-side animation track decode and the per-actor pose state, which read the animation streams in place. That is the next seam.

## M4b: rood-studio is its own executable

`pc/build/boot64/rood-studio` (and `pc/build/win64/rood-studio.exe`) is linked from the host's objects alone: no game unit, no function tables, no overlay statics, no trap table, no pinned snapshot layout. build_boot.py links it after the game's executable, from the same objects (every object that is not one of the game's). The host's units name 23 game symbols (a vsync, a few drawing helpers, the loaded room's blocks, some tables); `pc/src/rood_studio_host.c` answers them for this executable only: a vsync is the host's tick, the room is the block the studio shows, the game's tables are empty. The game's executable still hands over to the studio when started as `rood-studio` or with `--rood-studio`; that stays, since it costs nothing and the options are the same.

Proof: rood_studio_smoke runs against the host-only executable (the same pictures within the same limits as against the game's studio, the poses, the paint bridge, the RPC) and studio_smoke and the game's lanes are unchanged. The executable is 79 MB with compressed debug information, against 88 MB for the game's.
The zoom check in rood_studio_smoke now measures the model against a solid black backdrop: against the stage its height depended on how the lit stage looked that frame.

## The pose-state step: one track stepper

The packet format of an animation track (first vector, packets of frame deltas) is read in one place, native_model's `pc_nm_track_step`, with the track's state a plain struct (`pc_nm_track`: position in the stream, the frame the delta lasts to, the delta, the value). The studio's player and the game's actors both use it: the game's `vs_actors_decodeAnimationTrack` no longer reads the stream byte by byte; it fills a `pc_nm_track` from the actor's pose block, steps it over the frame range and stores the result back. The first vectors are read by `pc_nm_initial_vector`. The actor's pose block is unchanged (the position and frame still sit in the pad fields of the bone vectors, as the PS1 kept them, and only that one function maps between the two), so save states, rewind and the memory card are unaffected and need no new version. Moving the state out of the pad fields into their own native arrays would change the pose block's size and every copy of it (the blend, the restore, the weapon chain); it is left for the actor-state rewrite.
No frame differs from the old binary (0 of 120, casts, drops, every actor's state, audio, the five menus' model views); lint: zig_raw_memory 563 to 561.
