# Standalone studio (`rood-studio`): spike

Status: spike done, 2026-11-24. Branch `studio-standalone`. The prototype is throwaway (two small hooks and a driver script, below); this note is the finding.

## The short answer

A model cannot be loaded and drawn without the game's guest memory today, because the loaders, the actor records, the texture slots, the emulated VRAM and the model draw path are all the game's own code working on the game's own memory. What can go is the title, the save and the wait: the game binary started in a "studio boot" gets from launch to a held room with a model on screen in under a second of wall time, with no title, no card and no input. So the realistic `rood-studio` is a second executable built from the same objects as the game (the same renderer, materials, loaders, studio code), whose start-up skips the title and goes straight to the studio. Nothing is copied; what you paint is what the game draws, by construction. A loader that needs no game memory (parse a SHP into render meshes natively) is the long road and belongs to the "native meshes" step of the modernisation plan, not to this tool.

## 1. What summoning a model needs from the running game

Path: `studio.cpp summon` reads the files (`pc_ui_disc_read`), `studio_render` hands them to `pc_studio_scene` (`studio.c`), which calls the game's loader and then the game's own frames draw the model.

| Need | Where | Standalone? |
| --- | --- | --- |
| The disc's files (`OBJ/xx.SHP`, `OBJ/xx.WEP`, `MAP/Z...U...ZUD`, `SMALL/MON.BIN`) | `pc_ui_disc_read` (`ui/item_names.c`): a small ISO 9660 reader over the disc image (`ROOD_DISC`, default `disks/SLUS-01040.bin`) | Yes, no game state at all. |
| Guest memory (fixed arena, `ROOD_ARENA_BASE`), the game's heap (`vs_main_initHeap`, done in `_sysReinit`), the BATTLE and INITBTL overlays in their slots (`_loadBattlePrg`) | `boot_runtime.c main`, `SLUS_010.40/main.c` | Not without running the game's start-up. This is the part the studio boot keeps. |
| The loader: `vs_battle_populateDataSlot` (`game/battle/load_shp.zig`, `parse_shp.zig`, `load_wep.zig`, `parse_wep.zig`, `load_seq.zig`) then `vs_battle_processObjectDataQueue` | BATTLE.PRG | Needs the overlay, the heap, the actor record pool (`vs_actors_actorRecords`), a free actor slot (1..9) and its weapon slots. Not separable: the loader writes its result into those records. |
| Textures and CLUTs | `parse_shp.zig`: `LoadImage` of the texture into emulated VRAM at the texture slot's origin (`vs_actors_textureSlotOrigins`, 24 slots), the CLUT table kept in the actor record (`unk11A4`), `_loadMaterialPalette` | Needs VRAM (`ResetGraph`, `_initScreen` ran) and the slot tables. The renderer samples these textures from native VRAM by page, CLUT and box. |
| Animation and pose | `vs_actors_playActorAnimation`, sequence files loaded with `dataType 8` (ZUD battle and common sequences), the bone palette built by `vs_render_projectModelVertices` (GTE) per frame | Game code on the actor record; runs in the game's frames. |
| A room with a player | `pc_studio_room_ready`: the stage is written over the loaded room's geometry block (`pc_room_geometry`), the room's light table `[0]`, the player actor's position as the centre; the room's actors are skipped | Needs a room loaded. The studio is a visitor to a room, not a room of its own. |
| Photo mode hold | the studio is a client of `photo.cpp`: every picture is a few game frames run from the held moment, then the state is rewound | Needs the game running once; after that it is held. |
| Not guest memory, given by hand | sun and sky (`pc_env_studio`), room settings, fog switch, occlusion switch, glows kept | Host side, already standalone code. |

Cost of what is needed: from `ROOD_STUDIO_BOOT=1` the room has its player at about vsync 300-400 (a New Game's first room; the opening script is still running and the studio holds it). At the fast pacing that is about 0.3 s. At 59.94 Hz it would be 5-6 s, so a real tool starts unpaced and returns to normal pacing when the studio has taken hold.

Model kinds: character (SHP, about a third name another model's texture block: that one is loaded first, hidden), enemy (ZUD: model, weapon, shield, two sequence files), weapon (WEP held by a rig model whose body is not drawn). All three went through unchanged in the prototype.

## 2. What the renderer needs to draw one posed model

- The public mesh API (`render.h`: `render_mesh_create`, `render_list_begin`, `render_list_add`, `render_instance` with a bone palette) is clean and does not know about the game. It can draw a list without the game loop. But its inputs are made by the game: a model's mesh is built in `actor_native.c` from the polygon records the game's packet builders (`vs_render_drawModel*Polygons`) walk, cached by buffer address and a content hash; its bone palette is the per-bone matrices the game's GTE emulation computed that frame; the list is linked into the frame's ordering table at a marker packet; the camera is the GTE view the game set (`render_camera`: rows, H, OFX/OFY). The textures are not objects, they are VRAM boxes.
- Without the game loop one would have to supply: a mesh builder from the SHP (the work of `parse_shp.zig`, `actor_native.c`), a bone palette from sequence data (the SEQ player), emulated VRAM filled with the model's textures and CLUTs, a camera, lights, and a room-format scene for ray tracing, probes and shadows (the studio's stage is a room block precisely so those see it).
- Materials are keyed by content (`model_key.h`): a hash of the model's records with the VRAM page left out, so a painted material finds the model in the studio as in the game. This keying already works without VRAM addresses, which helps a later native route.
- Window and GPU: `pc_window_init`, the Vulkan renderer (ray tracing needs it), the GI history needs frames after a change (the studio waits presents). Headless EGL / `ROOD_VK_HEADLESS` and the null renderer exist (null draws nothing, so it can not show a studio picture).

## 3. Library split already there

- `build_boot.py --arch 64 --engine-library` (dev64): `rood_host` (the game's units, guest memory, the CPU rasteriser, SDL) plus `librood_engine.so` (everything else in pc/: window glue, RPC, renderers, UI, studio) reloaded in place. It is a development split, Linux 64 only, and the split line is "game state in the host, tools and renderer in the library": the library can not run without the host's game. It is not a studio library.
- The Zig hot swap (`librood_game.so`) swaps owned game functions; they operate on guest memory too.
- Headless boots: EGL (`headless_env` in `rood_rpc.py`), xvfb, null renderer (`NULL_SAFE` suites). The test checkpoints resume save states of the game.
- Precedent for "boot the game into a place with no title": the cutscene viewer (`story_boot.c`, `pc_story_title_exit` hook in `vs_main_execTitle`) starts New Game in a chosen room with chosen story flags. The studio boot is the same idea.
- No lean model-only library exists. The nearest are `studio_locate.cpp` (maths only) and the disc reader.

## 4. Prototype

What it is: two small edits and a driver.

- `pc/src/game/TITLE/TITLE.PRG/22C.c`, `vs_title_exec`: after the game's own `_initGameData` and `_initEnvironment`, if a weak hook `pc_studio_boot_skip_title()` says so, take New Game (`_setTitleExitFlags(menuItemNewGame)`) and return, never drawing the title or reading input.
- `pc/src/studio.cpp`: that hook, true when `ROOD_STUDIO_BOOT` is set.
- A driver script (removed once the real tool replaced it; `pc/tests/rood_studio_smoke.py` starts the game the same way): launches the game binary headless (EGL, Vulkan) with `ROOD_STUDIO_BOOT=1`, a card path that does not exist, settings from a private file; steps 450 vsyncs; RPC `studio enter`, `backdrop solid grey`, `summon {kind, id}`; waits for a framed picture; `present_dump`.

Results (wall clock from process start to the saved picture, including Vulkan start-up, one game process, nice 15): 0.7-0.8 s for a character, an enemy (Zombie Fighter, with weapon) and a weapon (id 1); with ray traced GI on (`ROOD_GI=1`) also 0.7 s. A wait below about vsync 300 is refused by the photo mode ("cannot pause now: a room transition"); 400 and above worked every time. Pictures: character 3, enemy 14, weapon 1 and character 3 with GI, on a plain grey backdrop (the studio's solid backdrop, the same renderer and materials as the game).

What was faked:

- The game still ran its start-up and 300-450 frames of the first room (opening script playing); nothing is rebuilt without it. The "standalone" is a start-up path, not a smaller program.
- The first room is the New Game opening room, whatever it holds; a real tool picks a quiet room (door through `pc_story_request`) so the held game is plain.
- No new window layout: the pictures come from `present_dump`; the Studio window, the dock and the game's own output were not shaped. Real desktop window not tried (EGL headless only).
- Driven by a script over RPC; no model list on start-up (the Studio window's catalog does that once entered), no auto-enter.
- Audio was off (`ROOD_AUDIO=none`); a real run must silence the opening's music.
- It is the same binary: no second executable target built.

## 5. Route and estimate

Route recommended: studio boot in the same objects, as a second executable (A below). Reasons: it reuses every feature (summon, orbit, looks, turntable, backdrops, locator, live paint, RPC) unchanged because it is the same code in the same process model; what you see is the game's picture; the spike shows it starts in under a second; and a model-only loader (C) is a rewrite of the loaders, the SEQ player and the VRAM/texture handling that does not exist yet.

A. Real `rood-studio` as a second executable of the same objects (agent time, one worker, from reading the spike's files and the studio tests):

| Step | Basis | Estimate |
| --- | --- | --- |
| Studio boot proper: a flag or argv[0] (`rood-studio`) selecting it; skip logos and intro; unpaced until the studio holds, then normal pacing; mute the room's sound and music; start in a quiet room through the story door path; auto-enter the studio; no autosave, no Resume, no card write (the game never saves without the player) | `story_boot.c` is the template (180 lines); the hook is 8 lines (done here) | 2-3 h |
| The window: the Studio window as the main surface (docked full, game picture hidden or small, catalog first, no "leave" button, quit), first-run layout, a CLI for `rood-studio character 3` | studio window code exists; layout and ini work | 2-3 h |
| Build target and launcher: second output name from the same object cache (one link, no extra compile), `run-studio.sh`, release packaging notes, doc | `build_boot.py` outputs; few lines | 1-2 h |
| Test: a studio boot smoke (boot with no card, enter, summon one of each kind, leave nothing behind, a picture equal to the in-game studio's for the same item and look); keep `studio_smoke` and `studio_locate_smoke` passing | the existing smokes are the template | 2-3 h |
| Painting beside the game: `paint_live.py` pushes `material map_set/map_clear` over one RPC socket; make it a list of sockets (the game's and the studio's), connect and reconnect each, report which are up, the studio's `picked` poll stays on the studio's socket; the extension's greeting shows both | `push`, `clear`, `connect` are about 40 lines; `aseprite_test.py` tests the bridge without a game | 2-3 h |
| Both together on one machine: two processes with their own RPC sockets, own `pc/user` overlays or shared materials; check the game hot-loads what the studio painted (the map pushed to both; saved files reach the game by its watcher); GPU use of two processes | needs a run with both | 1-2 h |

Total A: about 10-16 agent hours (one to two working days of agent time), most of it in the test and the window layout, little in the engine. Risks: a studio boot that must not depend on the opening room's script (choose a quiet room); the held game's memory (about the game's own) and a second GPU process next to a game on a shared card; Windows (the 32-bit and Windows builds are the contributors' to maintain).

C. A model-only studio with no game memory (a native loader): not recommended now. Basis: `parse_shp.zig`, `parse_wep.zig`, `load_seq.zig` and the actor-record texture/CLUT handling are about 2000 lines of game code that write guest memory, the SEQ player is game code on the actor, the mesh builder takes the packet records; replacing them is the native-meshes step (one to two agent weeks for characters, enemies and weapons with animation and the texture-slot behaviour matched, plus a parity test against the game for each model). Worth it only if the studio must run where the game's start-up is unwanted; every later feature (looks, painting) would still be reused from `studio.cpp`.

## What to do next

1. Make A's first step the real thing: the studio boot flag, quiet room, unpaced start, muted, auto-enter.
2. Decide the second executable's name and whether the `studio` RPC alone is enough (a launcher script setting the environment is a zero-link version of the same thing).
3. Keep C as a note for the native-meshes work: the content key and the studio's catalog are already independent of VRAM addresses.
