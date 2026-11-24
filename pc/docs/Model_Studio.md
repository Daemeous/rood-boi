# The model studio

Design: `pc/docs/plans/2026-11-22-model-studio-and-paint-channels-design.md` (section 4, phase P2). This page is what is built.

A neutral workshop in which any model can be summoned and looked at from every side: a character, an enemy (with its weapon and shield) or a weapon. Code: `pc/src/studio.cpp` (the host: look, camera, catalog, RPC, the Studio window), `pc/src/studio.c` (the game side: the stage and the summon), `pc/src/studio.h`. Proof: `pc/tests/studio_smoke.py`.

## How it holds the game

The studio is a client of the photo mode (`photo.h`). Entering holds the game at a vsync boundary M ("studio" owns the pause, the camera session and the hidden HUD). Every picture is a photo mode render: the game runs a few frames from M, then its state is rewound to M with the picture kept. Two hooks were added to photo parts: `render` (a render is about to run, the game at M) and `landed` (it has ended, the game at M again).

At each render's start the studio calls `pc_studio_scene` once (studio.c):

- the loaded room's geometry block is overwritten with the stage (below), the room's actors are skipped, the room's light triangles are replaced by one colour so the models take the game's shading evenly (`stage.actor_light`), and the model is built in a spare actor slot (1..9) as the Bestiary builds its enemies;
- nothing built is undone: the render ends by rewinding the guest's memory, so what leaving the studio leaves behind is the game as it was held. The state digest (heap, VRAM, overlays, vsync) is equal before and after.

What is not in guest memory is given back by hand: the sun and sky (`pc_env_studio`, a transient layer in environment.cpp), the room settings (`pc_room_look_set`, the cutscene player's transient layer), the fog switch, the occlusion switch. Leaving does one last render of the game as it is (the renderers keep what they built from the last picture: the stage's ray-traced scene), then releases the pause, the camera and the HUD.

Summoning reads the model's files from the disc image (`pc_ui_disc_read`): `OBJ/%02X.SHP` (a character), `OBJ/%02X.WEP` (a weapon, held by a rig model, `model.rig`, whose body is not drawn), `MAP/<file>.ZUD` (an enemy: model, weapon, shield, sequences). A SHP whose texture block names another model's (about a third of them) gets that model loaded first, hidden. The loader ids the models are filed under (`model.loader_id`, 200) are numbers the room's actors do not use.

Framing: the render before measures the world-space box of what it drew (`actor_native.c`, from the bone palette and the vertices); the camera's target and distance come from it, one render later than the summon (the studio asks for that render itself). The stage grows with the camera's reach (`stage.reach`, `stage.max_scale`): a dragon is a model of 1500 units.

## Data

`pc/assets/studio/studio.json` (shipped) with the user's overlay `pc/user/studio/studio.json` (`ROOD_STUDIO`) deep-merged over it: `stage` (radius, fillet, height, grey, facet sizes, reach), `camera` (yaw, elevation, fov, fit, guesses), `model` (facing, animation, equipment, rig, materials), `looks` (`neutral`, `flat`: sun, sky, room settings by their `room_settings.cpp` keys, fog, occlusion). RPC `studio` reads and sets any member by its path and saves the overlay; the Studio window (Window > Studio) shows every number as a row and the catalog as a list. `pc/assets/studio/catalog.json` is ids and file names only, written by `pc/tools/studio_scan.py` (`--check` compares) from the disc's file tree and MENU9's ZUD table; the enemies' names come from the player's own disc (SMALL/MON.BIN) when the studio runs.

Looks: `neutral` is a key light (a sun) and a sky, the room's own lights, glows, haze and fog off; `flat` is ambient light only (the stage is its grey exactly, the model shows its own colour). Fill and rim lights and the movable lights are P9.

## RPC `studio`

`{action}`: `state` (default), `enter`, `leave`, `summon {kind: character|enemy|weapon, id}`, `clear`, `camera {yaw, elevation, zoom, fov, orbit_yaw, orbit_elevation, zoom_by, frame}`, `look {name, set: path, value, save, reload}`, `get {path}`, `list {kind, filter}`. The reply carries the state: entered, settled (no render waiting), framed, the item, the camera, the box, the stage's size, the scene's message.

## Turntable and backdrop

`turntable` (data, window, RPC `studio {action: turntable, model, light, model_speed, light_speed, paused, fixed_dt, reset, model_angle, light_angle}`): the model itself turns (its facing; the camera stays) and/or the key light's azimuth goes round it, degrees a second, negative the other way; each render steps by speed x the time since the last (`fixed_dt` fixes the step for tests), `paused` holds the angles. `backdrop` (`{action: backdrop, mode: stage|solid, color: [r,g,b], preset: black|grey|white}`): `solid` has no stage and no drop shadow and draws the game's own background in one colour (its sky dome's five colours equal, tiles off), exact to the pixel and not lit; the model keeps the look's key light and sky. Interpolation is off while in the studio.

## Known limits

- The stage is a room-format block (so ray tracing, probes and shadows see it). Its black facets of the first build were the old room's texture effects: their per-frame handlers (`vs_field_textureEffectCount` slots, BATTLE 2842C.c) wrote UV bytes at the old room's polygon offsets, and in the stage's layout those bytes are vertex words, which the native room mesh re-decoded (`mesh_follow`) into holes and slivers; `scene_dump` is captured before the writes, so it looked clean. The studio zeroes the count when it builds the stage; the render's rewind puts it back. `studio_smoke` checks 0 black pixels over 15 views and that the room's effects are back after leaving.
- Animations are not scrubbed or chosen yet (P7): every render replays from M, so a model shows the pose of the same few ticks.
- Names: enemies have theirs; characters and weapons are named by their id.
