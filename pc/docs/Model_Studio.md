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

A live game's hold is led (photo.cpp: the last frames replayed to M), so the render's first frame is the replay's first: the studio's scene is built there, and the replay's frames draw it (built only for the replay's last frame the room's own picture stayed on screen). The Studio window is the state line (in the studio / Leave, or Enter) and two columns that scroll on their own, the controls and the catalog (`window.controls_share`), so it works docked short and wide; RPC `studio state` reports what it drew (`window`). Proof of the live editor path: `pc/tests/studio_window_smoke.py`.

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

## The texel locator (Aseprite and the studio, both ways)

Code: `pc/src/studio_locate.cpp` (the maths: no game, no ImGui), `studio.cpp` (capture, marker, cursor, RPC), `render.c render_mesh_posed` (the posed triangles of a drawn model), `pc/tools/paint_live.py` (the bridge) and `pc/tools/aseprite/rood-live/rood-live.lua` (the extension, 1.4.0). Proof: `pc/tests/studio_locate_smoke.py` (the game), `pc/tests/aseprite_test.py` and `aseprite_extension_test.lua` (the bridge and the extension without Aseprite or the game).

### What Aseprite's Lua API can report (the spike; read from the API docs, github.com/aseprite/api, and tried against the installed Aseprite 1.3.18)

- Hover is possible, by polling. There is no mouse-move event (`app.events` has `sitechange`, `beforecommand`, `aftercommand`, `fgcolorchange`, `bgcolorchange`; `sprite.events` has `change`, `filenamechange`; a `Dialog` canvas gets mouse events only over the dialog's own canvas, not the sprite's). But `app.editor` (the active editor; nil in batch mode `-b`) has read-only `spritePos` (the sprite pixel under the cursor: "the pixel that will change if the user clicks") and `mousePos`, and read/write `scroll` (the sprite pixel the view is centred on) and `zoom`. A `Timer` (50 ms) reading `spritePos` is the hover. It has no event for entering or leaving the canvas, so the extension sends only a position inside the canvas that changed.
- Selecting a texel: `sprite.selection:select(Rectangle(x, y, 1, 1))` (tried in batch mode). Scrolling: `app.editor.scroll = {x = x + 0.5, y = y + 0.5}`. Making another open sprite the active one: `app.sprite = sprite`.
- Not needed, but there if hover is ever too much: `app.editor:askPoint{...}` (a modal click on the canvas, `onclick`/`onchange`/`oncancel`), `app.events:on("fgcolorchange")` (the eyedropper), `sprite.events:on("change")`, a selection read from `sprite.selection`, and a custom tool is not scriptable. The hover is switchable (`Rood live: locate hover on/off`; preference `hover`).

### Aseprite to the studio

The extension sends the cursor's pixel as text (`{"locate":{"material": sprite title, "x", "y"}}`) over the live-paint link. The bridge turns the page pixel into the texture it is in (`seed.json` slices: a model's page is boxes of textures, each with its content key; a room texture is the sprite itself) and the place in it, and calls RPC `studio locate {key, x, y}`. The studio finds the texture's uses in the picture (`emissive::use_list`: the page, palette and box it sits at in this picture, which is not the page the sprite was exported from: a model follows its texture slot), turns the texel into a page texel, and `studio_locate::locate` finds every triangle whose texture coordinates cover it: the texel's square is cut out of the triangle in texture space (Sutherland-Hodgman) and carried back onto the posed triangle by barycentres, so the footprint is the texel's exact shape on the surface, per triangle (shared or mirrored coordinates: several footprints). The marker is drawn over the picture by `pc_studio_overlay` (the ImGui foreground, so it is in `present_dump` and a screenshot): the footprint filled and outlined, a ring and a crosshair at its centre, pulsing; where the surface faces away or something is in front, `back_alpha` of it.

### The studio to Aseprite

The cursor over the model (`locate.pick_on`: `hover`, `click` or `off`) picks the texel under it: the nearest triangle at the pixel (the perspective-correct barycentre of its corners' texture coordinates). The reply names the page and palette texel and the texture's key and place in it; the bridge polls RPC `studio picked` (0.1 s), turns the key and place into the sprite title and pixel (the model's own page when several have the texture) and sends `{"select": {...}}`. The extension selects the pixel and, for a click (`locate.click_scrolls`), scrolls the view to it. A hover only selects.

### RPC

`studio {action: locate, key|label|page(+clut), x, y}` (or `clear: true`): the footprints (polygon, centre, window pixel, visible, area), the texture count and the triangle count. `{action: pick, x, y, tell}`: the texel under the window pixel (`tell`, default true, leaves it as the studio's pick for the bridge). `{action: picked}`: the studio's last pick with a serial that grows. `state` gains `capture` (triangles, meshes, whether the picture's capture is valid).

### Data (`locate` in studio.json)

`pick_on`, `click_scrolls`, `occlusion_tolerance` (a fraction of the camera distance), `marker` (the Aseprite cursor's: `color`, `fill_alpha`, `outline_alpha`, `line_width`, `ring_radius`, `ring_width`, `crosshair`, `pulse_hz` (0: steady), `pulse_depth`, `back_alpha`) and `pick` (the studio cursor's own texel, the same members, and `show`). The Studio window lists them like the rest.

### How it holds still

Each render that lands leaves the model's posed triangles and the view the picture was drawn with, only when no change is waiting (a camera change, a re-frame): the marker and the pick agree with the picture on screen to the pixel. While a camera change renders the marker is not drawn (the picture still shows the old view and the new view is not on screen yet); when the picture lands it is at the new place. A running turntable captures every render, so the marker follows the model. Nothing of this touches the game: the overlay is drawn only while the studio is entered, and the studio's leaving gives the game back as before.

### Limits

- Hidden-ness is the CPU's: a footprint is dimmed where another triangle of the model is nearer at its centre (`occlusion_tolerance`), not per pixel of the footprint, and not behind the stage.
- A texel is its page's pixel (palette indices are not looked at): two palettes of one texture are two pages, and the sprite titles name the palette (`_c<clut>`).
- The marker marks the posed model of the last render; models not drawn through the object builders (none in the studio) are not captured.
