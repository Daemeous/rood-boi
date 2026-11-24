# Model studio and more paint channels (Gitea #20) - design

Status: DESIGN; P2 (the stage, summon, orbit and zoom, `neutral` and `flat`, the catalog) is built on branch studio-p2, see `pc/docs/Model_Studio.md`. Design written (worker research, 2026-11-22, read from branches gitea20-aseprite 8bc43abb2 and gitea20-paint-neutral c8bfcadd1; no product code written, nothing run).
Lines marked **(assumption)** are mine and unverified. Names of functions and files are real as read.

## 1. Intent

Painting a model today means finding it in the game (warping to a room with enemies on, `--model-at X,Y`), and only metal and roughness can be painted.
The owner wants the Unreal "asset editor" experience: summon any model on a neutral stage, see it from every side in clean light, make it move or hold a pose, and paint it in Aseprite with the strokes appearing live; and three more channels (height, alpha, emissive) on the same loop.
Why it matters: the paint loop is only as good as how fast you can get a model in front of the painter and how much of the surface it can describe.

## 2. What the owner decided (11-22)

1. A **model studio**: neutral workshop scene (plain backdrop, studio lights, the model centred). Any character, enemy, weapon, shield, prop or other object summoned from a list without finding it in the game. Orbit and zoom. Play, pause and scrub animations, or hold a pose. A lighting switch with at least a neutral studio preset and "as in its home room". Picking a model exports its package and opens its .aseprite files; strokes show on it live.
2. **More paint channels**: HEIGHT (relief; mid-grey is flat), ALPHA (cut-out / see-through), EMISSIVE (glow colour). Same live loop as metal and roughness.

## 3. Goals, non-goals, anti-goals

Goals: **community painting as a first-class feature (section 7)**: others paint, share and layer packs safely; summon-by-list; full engine lighting on the stage (so metal, roughness, relief and glow are visible while painting); every look/feel value is data with a Details panel and RPC; the whole thing scriptable (RPC first, Lua after); painting a model once applies to that model everywhere it appears.
Non-goals: editing geometry, skeletons or animations; painting base colour (still not imported); a player-facing gallery (see Q4); touching the room paint path beyond what the channels need.
Anti-goals: classic mode changes by one pixel (a channel with no painted map must write no plane and change no draw); committing game data (exports and maps stay in `pc/user`, `pc/exports`, `pc/build`); another PS1-tie patch (Off the PS1: the studio gets a single loader seam, see 4.2, so deleting the PS1 loader later does not delete the studio); constants for look values.

## 4. The studio

### 4.1 What exists (read)

- **No standalone scene exists.** A model is only drawn inside a game room, or through MAINMENU's *actor preview* (`vs_menu_frameworkLoadActorPreview / DrawActorPreview / Release`), which the modern Ashley tab drives from `pc/src/ui/ashley_bridge.c`: freeze the world (`vs_field_setFieldRenderingEnabled(0)`), build the clone in actor slot 1, orbit camera, drop the rim copy, ask for the window's resolution, give everything back on teardown. The native renderer clears `room_depth` for such a list, so it draws native (MSAA, interpolated).
- **That preview is lit by `menu_studio.glsl`, a bypass** (`scaled.frag` ~l.642: `c = menu_studio(albedo, n, p)`): no GGX lobe, no relief, no environment, no traced reflections. Metal and roughness would be invisible. So the existing clone path cannot be the painting stage as is.
- **A summon loader exists.** The Bestiary (`MENU9.PRG/592C.c`: `vs_menu9_loadZudFile(id)`, `vs_menu9_parseZudFile`) loads any of ~512 ZUD files by id from a table of disc LBAs, `vs_zud_decode` (`146C.h`: header +0x00 characterId, weapon, weaponCategory, weaponMaterial, shield, shieldMaterial, defaultMaterial, then parts shp, weaponWep, shieldWep, commonSeq, battleSeq), and fills the object-data queue with `vs_battle_populateDataSlot`: dataType 1/2 SHP (disc by id / in memory), 3/4 WEP, 8 SEQ, 7 reload (`BATTLE 30DB0.c vs_battle_processObjectDataQueue`). Characters other than enemies are SHP by `modelId` (`_shpLbaOffsets`), weapons and shields WEP by `D_800E8D00[modelId]`. A ZUD carries the model, its weapon and shield with materials, and its two sequence banks: exactly "model + equipment + animations".
- **Lighting data is per room and separable.** `lighting_layers.h` merges lights, light actors, glows and `set:` overrides for `zoneZZ_roomRR`; `room_settings.cpp` applies them on a room change. The library thumbnail scene (`library_thumbs.h`, `Look`) is a precedent for a neutral grey floor, grid and back wall with a data-driven look.
- **Editor shell** (Outliner, Details, Library, panel docking, `editor_schema.cpp` kinds with the Details rows, RPC per tool, `rood.dev.window` for Lua panels) is where a Studio panel and its Details live. **Photo mode** (`rood.photo`, `pc/src/photo*.cpp`) already holds a frozen game, a free camera with roll/fov and redraws; the studio camera should reuse its camera session rather than invent one.

### 4.2 Architecture

**Stage = our scene, summon = the game's loader, behind one seam.** (assumption: this is the cheapest path that shows full lighting; spike S1 decides it.)

1. **Studio mode** (`pc/src/studio.cpp`, RPC `studio`, editor panel `Studio`): entered from the editor. It borrows the proven freeze/build/draw/tear sequence of `ashley_bridge.c`, but draws with the *normal* lit path, not `menu_studio`. Entering holds the game state like photo mode (pause + camera session) and leaves it exactly as found.
2. **Stage geometry**: a floor and a curved backdrop (cyclorama) built as native meshes through the render mesh API the room and actors use, from a `Look` of data (colour, gradient, floor grid on/off, size). Not a disc room, so no room load, no room scripts, no enemies to wake. Unknown: whether the lighting stack (shadow maps, probes, RT TLAS, haze) accepts a scene with no captured room block (`pc_scene_room_block`). If it does not, the fallback is a real small room warped to with its geometry hidden (`room_goto`, group hide flags) and the stage meshes added; ugly but it works with today's code.
3. **Summon** goes through `studio_load(item)`, the only function that touches `vs_battle_populateDataSlot`. It builds a `vs_battle_objectData` for the item (ZUD: parse, then SHP + WEP slots + both SEQ banks as the Bestiary does; character SHP; weapon or shield WEP alone, standing on a hand-less stand mesh or held by a neutral rig **(assumption)**), pumps `vs_battle_processObjectDataQueue` until empty, and releases with the same sequence as `vs_menu9_freeZudFile`. Needs: a free heap block for the model (the Ashley bridge checks `heap_need`; the studio must do the same), the load queue otherwise empty, actor slot 1 (so one model at a time; a second slot for a comparison view is later).
4. **Catalog** (`pc/assets/studio/catalog.json` shipped, ids/names only, generated by `pc/tools/studio_scan.py`): per entry `{id, kind: character|enemy|weapon|shield|prop|other, name, source: {zud|shp|wep: id}, weapon, shield, home: {zone, room, anchor?}}`. Names from `item_names.c` and `mon_parse.c`. The list is filled from three sources: the ZUD table, the SHP table, the WEP table. **Props and "other objects" are the gap**: chests, doors and set pieces are not in any of those tables I read; a scan that walks rooms (as `material_rollout.py` does, `enemies=True`) and records which object models draw (`material pick` object text, `emissive uses`) is needed to enumerate them. The list also reports models that exist but nothing in the story uses.
5. **Camera**: orbit about the model's bounding-box centre with zoom to fit, yaw/pitch/distance/fov from data (`studio.json`), RMB/Alt orbit, wheel zoom, F frame (all editor conventions), plus RPC `studio camera {yaw, pitch, distance, fov}` for scripts and tests. Reuses the photo camera session.
6. **Lighting presets** (data, `pc/assets/studio/studio.json` and a user copy, tuned live in Details with the value ladder and undo like Menu model light): `neutral` (three-point: key, fill, rim as real engine lights from the existing `prefab::EvalLight` schema, flat sky ambient, no haze, no GI colour bleed, exposure fixed), `flat` (ambient only, for reading colour), `raking` (one hard side light, to read relief), `dark` (one light, to read glow), and `home`. Switching is `studio light {preset}`. **Home room** = the model's recorded `home {zone, room}` lighting layer (`lighting::effective`) applied to the stage with the model placed at the home anchor (the spawn cell in the room's enemy section, or the centroid of that room's lights when none) **(assumption)**. Lights, atmosphere and fog come from the layer; the room's geometry is not there, so shadows and bounce are lighter than in the room. If the owner wants it as in the room exactly, the fallback is to warp there (Q3).
7. **Animation transport**: `studio anim {sequence, play, speed, frame, hold}`; panel: sequence list (common + battle banks, named by index and, where known, by action), play/pause, speed 1, 1/2, 1/4, scrub bar, hold-pose toggle. Facts that bound this: SEQ tracks are streams of constant-velocity segments (`Native_Rendering.md` "Animation in-betweens"), decoded forward one tick at a time, with a crossfade `unk704` on change. So **scrub = reset to tick 0 and run n ticks** (deterministic, cheap for tracks of a few hundred ticks), pause = stop advancing, hold = freeze after a crossfade has finished. Backward play is scrubbing. Needs the sequence length per track (parse the stream once at load).
8. **Equipment**: weapon and shield come with the ZUD (character weapon/shield ids and materials); the panel lists the legal swaps (weapon by category: `weaponCategory` also selects which battle SEQ bank loads) and re-runs `studio_load` with the new WEP; materials (blade, grip) are the `weaponMaterial`/`shieldMaterial` bytes, exposed as a picker.
9. **Plugs into the shell and scripting**: panel `Studio` (Library tab "Models" listing the catalog with filters like the existing Library, search, favourites), Details kind `studio` (camera, look, presets, lights), RPC `studio {action: list|summon|clear|camera|light|anim|equip|paint|state}`, and `rood.studio` Lua (same verbs) after RPC works **(assumption: on the extension API, not part of this design)**.

### 4.3 Picking a model starts the paint loop

Today `paint_roundtrip.py export` launches its own game (`material_rollout.launch`) and `live` attaches to a running one by RPC. The studio's game is already running, so:

- Add `--attach` to `export model` (use the game at the RPC socket; skip launch/kill). It calls `material model_dump {model, path}` and `emissive uses`, exactly as now, with the studio's model as the target. Because the model stands alone on the stage the dump is unambiguous (no neighbours, no pose of someone mid-attack: the studio can force the rest pose by holding frame 0).
- RPC `studio paint {item}` (and the panel button **Paint in Aseprite**): exports (`--attach`, `--painter aseprite`), starts the `live` bridge (`paint_live.py`) if it is not running, and opens the `.aseprite` files. The host runs these as detached children from a command template in settings, not hard-coded paths **(assumption: the host may spawn a child; otherwise a small `pc/tools/paint_studio.py` is run by the user and the panel only shows its state)**. Re-picking the same model reuses its package (same folder name from the asset id) and does not overwrite files newer than the last export.
- Strokes already arrive as `material map_set` on a key + object, so they show on the stage the same frame (the paint loop measured about 80 ms with a screenshot).

### 4.4 The model identity problem (the biggest risk; the fix is in 7.2)

Maps live under `<texture key>/@m<model hash>/`. The model hash comes from the mesh content *and names the VRAM page it was loaded into* (`Materials.md` "A model through the same bake": Ashley twice in the Undercity, `dfcf8792` at page 18 and `266d0792` at page 10, same 194 texture keys). The studio will load each model at its own page, so a paint made in the studio would not apply to that model in a room. This defeats "paint it once".
Proposed: a stable asset id independent of the page (hash of the SHP/WEP content ignoring tpage/clut words, or the disc id: `shp:<n>`, `zud:<n>`), accepted as an object name in `materials::channel(key, name, object)` (object names are free-form strings already: `valid chars 0-9a-f g m p +`) and in catalog `object.models`. Texture keys are already content-only, so a map under `<key>/` with no object applies to every user of that texture; the `@m` folder is only for "this model, not others sharing the texels" (a texel two models share is the one limit the doc already lists). **(assumption: the mesh hash is where the page enters; verify in spike S2.)**

## 5. Paint channels

Common to all three, and the refactor that makes them cheap: today `ASE_LAYERS = {metal, roughness}` plus `CHANNELS` in `paint_live.py`, `over_seed`, `neutral_map`, `STANDARD`, `read_ase_channels` and the Lua's `CHANNELS = {Metal, Roughness}` are parallel lists. Make one **channel table** (`name, layer name, blank value, encoding gray|rgb, map file(s), game channel, default visibility/opacity`) in a data file (`pc/tools/aseprite/channels.json`) read by the exporter, importer, bridge and (generated into) the Lua, so the next channel is a row. Generic rule kept: a transparent pixel in a channel layer keeps the game's value; an emptied layer resets the map; an unchanged import writes nothing.
Layer order in the .aseprite file (bottom to top): *Game colour (reference)* locked, *Height*, *Alpha*, *Emissive*, *Metal*, *Roughness*, one visible at a time by the painter.

### 5.1 Height (relief) - renderer ready

- Renderer: complete. `height.png` per texture and per object (`emissive.cpp` ~l.1615 reads `materials::channel(key, "height", u->mat_object)`), Sobel slopes per texel, relief normal in the G-buffer, bends lights, GGX, baked light, indirect and sky; cavities lose ambient. Painted height replaces the auto-height estimate entirely; needs a catalog entry with `bump` > 0 (the paint import's `ensure_entries` already writes one with the matched entry's values).
- .aseprite: layer **Height**, grayscale, 128 = flat, white up, black down, 1 px, nearest. Seed = the game's `height.png` if there is one, else filled 128 (the estimate is run-time only and cannot be exported; stated in the panel, **assumption: acceptable**).
- Import: `height.png` per slice, written only when some pixel differs from the seed (so an untouched layer does not replace the estimate with flat). Live: `map_set channel=height` already accepted by `known_channel`; nothing in C++.
- Shows on the stage under the `raking` preset. Test needs the relief visible: assert the shaded normals differ in a painted region (`material` G-buffer view), not the colour.

### 5.2 Emissive (glow colour)

- Renderer today: **amount only, gated by an emissive catalog entry.** A texel glows if an entry in `emissive.json` matches its texture and has `mask.map = "emissive"`; the amount (0..255, R8) is read per VRAM cell (`emissive.glsl` rows 516-1027), the colour is texel colour x entry tint x strength (or the flat tint when flag 8 `flat`). It feeds bloom, the glow light list and RT (`Emissive.md`, `Glows.md`). The material master also lists an `emissive` slot (`materials.cpp` l.514) but I found no consumer of it for planes **(assumption)**.
- Gaps: (a) `emissive.cpp` ~l.1274 reads the map with `materials::channel(u->key, e.mask.map)`, **without the object folder**, so a model-specific emissive map is not read (height and roughness do read it); (b) the entry must exist: `ensure_entries` writes only the material catalog, not `emissive.json`; (c) no colour per texel.
- Format: layer **Emissive**, RGB(A): the pixel's colour is the glow colour, its brightness the amount (amount = max(R,G,B); colour = RGB / amount), transparent = keep the game's (none by default). Files: `emissive.png` (amount, as now, so existing consumers and the editor still work) and `emissive_color.png` (RGB8, new; the loader gets `read_png_rgb`; the `Channel` type gets a 3-byte form). Brightness above 1 is the entry's `strength` (HDR); painted pixels do not exceed 1 **(assumption)**.
- Renderer work: read the object folder in (a); three new colour planes beside the amount cells (the slot map image is 4096 wide; planes are cell-for-cell), `emissive_apply` uses them when set instead of texel x tint; `map_set` takes `format: "rgb"` and the colour channel name (`emissive_color`). The slot-map rebuild already happens on a map change, so live strokes follow the existing `files_generation` bump. `ensure_entries` also writes an `emissive.json` entry (`mask: {map: "emissive"}`, `object.models`, `light` 0 by default so it blooms but does not light the room until the owner chooses) with the same "carry what the game had" rule.
- Stage: `dark` preset shows it; the engine bloom shows glow without extra work.

### 5.3 Alpha (cut-out / see-through)

- Renderer today: **binary, from the texture itself.** A texel whose PS1 value is 0 is discarded (`native_frag.glsl` l.245 `if (t == 0u) discard`, `hw_frag.glsl`, `rt_common.glsl`, `probe_bvh.glsl`); fences, grates and glass are "cut-out triangles", split from the opaque ones when the TLAS is built (`Vulkan_Renderer.md` l.216, 234: opaque (mask 1) and cut-out (mask 1) instances). Translucency is separate: STP-bit texels blend with the page's PS1 mode in a different pass (`Native_Rendering.md` "Translucent draws"); translucent surfaces write **no material** (`Materials.md` "Next"). There is no per-texel alpha map. A painted base colour cannot make a texel appear where the PS1 had 0, since the game's colour is the base.
- Proposal (assumption, Q2): a new material channel **`alpha`** (`known_channel`, blank 255): value < 128 discards the texel (cut-out), reduced-only at first. Partial values (see-through) use the screen-door the renderer already has for fades (`cover < bayer4(...)`, `native_frag.glsl` l.125/139/190) so they cost no sorting and work in shadows and RT; real blended translucency is out of scope (needs the translucent pass to carry material, a bigger change).
- Format: layer **Alpha**, grayscale, white = solid, black = gone, gray = dithered; transparent = keep. Files `alpha.png` per slice, per object.
- Renderer work: an eighth material plane (the seventh already holds the graph slot); `material_texel` result gains `alpha`; discard in `native_frag.glsl` and `hw_frag.glsl`; the **same test in the RT any-hit (`rt_common.glsl`) and the software probe BVH (`probe_bvh.glsl`)**, else shadows and reflections show the cut-away part; the CPU side classifies a mesh's triangles opaque vs cut-out when building the TLAS, so a painted alpha must mark its model's triangles cut-out (and rebuild on live change). Shadow maps: check `shadow.frag`, which did not match my search for texel tests **(unchecked)**.
- Stage: a mid-grey backdrop with the `flat` preset makes cut-outs readable; the model's triangles behind show through.

## 6. Risks and unknowns (ranked)

1. **Model identity** (4.4): paint in the studio not applying in rooms. Spike S2 first.
2. **Full lighting without a room** (4.2.2): the stack may assume a captured room block; fallback exists (hidden real room), which costs a warp and is not "independent of PS1 room loading".
3. **Alpha across every pass** (5.3): native, packet, RT any-hit, probe BVH, TLAS classification, shadows. The most invasive; ship last and cut-out only first.
4. **Enumerating props/"other objects"** (4.2.4): not in the tables I read; needs a scan and may find only what rooms draw.
5. **Scrub and hold** (4.2.7): SEQ decode is stateful; scrub-by-replay is cheap but not verified on long tracks, and crossfade state must be cleared on hold.
6. **Heap and slots**: the clone needs a free heap block; summoning repeatedly must release exactly; actor slot 1 is shared with the game's own previews (the Ashley tab, menus must tear the studio down first, as Ashley's tab does).
7. **Emissive colour planes** grow the slot-map image; check the Vulkan limit the variants already stretch (4096 wide) and the OpenGL twin (GL has no variants/scopes: the studio and the new channels are Vulkan first **(assumption: 64-bit Vulkan is the target; OpenGL is contributors')**).
8. **Home-room lighting** is an approximation without the room's geometry (shadows, bounce) and needs a recorded anchor per model.
9. **Aseprite side**: 6 layers x many pages per model (a model's page is a 256 x 256 atlas, one file per page and palette); fine for the extension, but the Lua file regenerates from the channel table or drifts.
10. Host spawning the painter and bridge (4.3).

11. **Game-derived pixels leaking into packs** (7.1): the copy tests refuse obvious derivation only; a hand-traced copy of the game's art passes. Mitigation is policy (attestation, licence) plus the installer-side re-check, not proof.
12. **Layer generalisation in `materials.cpp`** (7.4) touches the loader every draw depends on; classic frames must stay identical with no packs. **Windows** is unverified (7.7).

## 7. Community painting (owner requirement, 11-22)

The owner cannot paint every model; other people must be able to paint and share easily and safely. No announcement, listing or publicity is proposed: this is tooling that makes a contributor able to work, nothing more.

### 7.1 The paint pack and what is safe to share

A **paint pack** is a plugin folder (the project's one plugin system, `Extensions.md`: `pc/user/plugins/<author.pack>/`, `plugin.json`, ids `author.name`):

```
plugin.json          manifest (7.3)
main.lua             optional; a pack needs no code (declarative: the host reads the folders below)
pack/maps/<texture key>/[@<model key>/]<channel>.png   metal, roughness, height, alpha: 8-bit gray; emissive: gray amount;
                                                       emissive_color: RGB8 (only where the colour differs from the texel's)
pack/entries.json    numbers only: the catalog entries the maps need (bump, base roughness...), merged by id like the user catalog
pack/coverage.json   generated: model keys, texture keys, channels, painted fraction (no pixels)
README.md, LICENSE   author's text
```

**Safe to share**: the contributor's own maps (pure hand-painted numbers), `entries.json`, `coverage.json`, the manifest. **Never shared**: the base colour; the `Game colour (reference)` layer; any `.aseprite` file (each embeds the game's texels as that layer: a pack is *built from* the .aseprite files, never contains them); `paint.gltf`, `tex/`, `seed/`, `reference.png`, `scene.obj` (game geometry and colour); screenshots of the game unless the owner says so (Q1). A key (10 hex digits of a hash of texel indices) and a size are not game pixels (the committed fixture and emissive catalogs already use them).

**Enforced in the tool, not by trust.** `pack build` (reads the contributor's `.aseprite` files and layer table, strips the reference layer, writes `pack/`) and `pack check` (7.6) refuse:
1. any file outside the whitelist above (a stray `.aseprite`, `.gltf`, `reference.png`, `tex/` fails the build and the check), any PNG that is not 8-bit gray (or RGB8 for `emissive_color`), carries alpha, ICC, text or other ancillary chunks, or is over 256 x 256 (the existing shipped-map rule, `check_material_maps.py`, reused);
2. a channel that **is a copy of game texels**: with the contributor's own base colour as the comparison, refuse when the map equals the base colour's luminance or one of its channels, or is a lookup of it (each base luminance level maps to a single map value over >= 95% of its texels, or Spearman |rho| >= 0.98), or equals the seed (the game's baked or shipped map, which is derived from game art: `material_bake.py`) over >= 95% of texels (an untouched layer is "nothing painted", not a pack). Emissive colour equal to the texel's own colour is not stored (amount only) rather than refused;
3. a manifest that is incomplete (7.3).
`pack check` runs again on **the installer's machine with the installer's own game data** (everyone who installs a pack owns the game), so a hand-edited pack that slipped past the contributor's build is still refused at install. Limit, stated honestly: the tool refuses obvious derivation; it cannot prove originality. The manifest carries the contributor's attestation (`"own_work": true`) and the licence they chose. **(assumption: thresholds are tunable data in `pc/tools/pack_rules.json`, not constants.)**

### 7.2 Stable keys across machines (model identity by content only)

Today a model id is a hash that includes the VRAM page the model loaded at (Ashley: `dfcf8792` at page 18 and `266d0792` at page 10), so one character has two ids and a pack made on one machine may not match another. Texture keys are already content-only and stay.

Proposed **model content key**: `m` + 10 hex digits (the legacy ids are 8; the length tells them apart, both pass `valid_object` in `materials.cpp`): a hash of the mesh's groups, vertices and polygon records with the **location-bearing words masked**: the texture-page x/y bits of each polygon's tpage word and the CLUT word (palette identity comes instead from the existing palette content key), keeping the depth and blend bits and the page-local UVs, which are content. Same model, same key on every machine and every load. **(assumption: the mesh cache hash in `emissive.cpp`/`render_mesh` is where the page enters; spike S2 confirms and measures collisions across the whole catalog; a collision (two different models, same key) must fail the spike.)**

**Migration of existing user data** (the owner's `pc/user`): `python3 pc/tools/paint_roundtrip.py rekey` (needs the game, once). The game answers RPC `material model_ids {model}` -> `{legacy, content}` for every model it can load (the studio catalog loads each in turn; models never drawn in this run stay unmapped and are reported). For each user catalog entry with `object.models` and each `<key>/@m<8 hex>/` map folder, the tool copies to the content key (never moves: the old stays until `--drop-legacy`), rewrites the entry, and writes `pc/user/lighting/model_aliases.json` `{legacy: content}`, which the game also reads so that old packages (`seed.json` records the legacy `model`) and a not-yet-migrated tree keep working for one release. Where two legacy ids (the two Ashleys) map to one content key and both have maps, the newer file wins and the conflict is listed. After migration the package's `seed.json` carries both ids.

### 7.3 Manifest

`plugin.json` as `Extensions.md` documents (`id`, `name`, `version`, `api_version`, `author`, `description`, `capabilities`) plus a `paint_pack` block:

```
"paint_pack": { "format": 1, "licence": "CC0-1.0" | "CC-BY-4.0" (the list is data; the owner decides which are allowed, Q1),
  "own_work": true, "authors": ["..."], "attribution": "required text for CC-BY",
  "game": { "build": "<port version>", "content_catalog": "<hash of the census key set it was made against>" },
  "covers": { "models": ["m<10 hex>"...], "channels": ["metal", "roughness", ...] },   // generated, checked against coverage.json
  "preview": "local" }
```
The `licence` and `own_work` fields are mandatory; `pack build` refuses without them. CC0 matches the project; CC-BY needs the attribution shown (the Studio panel prints it with the pack's name, so it is satisfied wherever the pack is used). Version fields use the plugin loader's `x.y.z`. `game.content_catalog` lets the tool say "made for a different content set" (a different disc dump or language build changes some keys), a warning, not a refusal.
**Preview image**: default `"local"`: the installer's machine renders each model's preview itself (`studio shot`, the stage with the pack applied) into `pc/build/paint_previews/`, so no game pixels ever travel. A contributor-supplied screenshot is possible only if the owner allows it (Q1), and then would show the model in the contributor's colours (game pixels).

### 7.4 Layering and conflicts

`materials.cpp` has three fixed layers (`enum { USER, SHIPPED, BAKED }`, the winning file per channel). Generalise to an ordered list: **user (the owner's own paint, including live strokes) > packs in order > shipped > baked**, still per channel file, so a **partial pack** (some models, some channels) falls through to the next layer for what it lacks, exactly as today. Pack order: `pc/user/paint_packs.json` `{ "order": ["a.pack", "b.pack"], "models": { "m<key>": "<pack id or 'user' or 'none'>" }, "channels": { "<pack>:<model>": [..] } }`; default order is the plugin loader's (shipped before user, then by id), first in the list wins. `models` is the **per-model "which pack wins"** (and "none" shows the game's own); the Studio panel sets it from a dropdown and shows, per model and channel, which layer supplies each map. `pack conflicts` lists every (model, channel) painted by more than one layer. Catalog entries from packs merge by id with the same replace-by-id rule as the user catalog, in layer order; a pack's entries are namespaced by its id so two packs cannot collide. Live strokes always land in the user layer (`map_set`, as today). Disabling a pack plugin removes its layer at the next hot reload.

### 7.5 Coordination: a census and progress, no publicity

`pc/tools/paint_census.py` (RPC `studio census`) writes `pc/user/paint/census.json` from the running game: every paintable model (content key, name, kind, source ids) and each texture's key, size and texel count, derived at run time from the player's own disc; **never committed** (keys, sizes and names only, no pixels; whether it may be committed is the owner's call, default no). Per model and channel it computes **progress**: painted fraction of texels (differs from the seed) and which layer supplies it. `pack progress [--pack PATH]` and the Studio Models list (columns: channels done as dots, percent, supplying pack) show what is left; a pack's own `coverage.json` carries its half, so the owner or anyone can merge coverage across packs without game data. A contributor who wants to say "I am doing these" does so by setting `"covers"` in a draft manifest; nothing is posted anywhere.

### 7.6 Onboarding and `pack check`

- **One command**: `python3 pc/tools/paint_setup.py`: checks Python, numpy and Pillow, finds Aseprite (PATH, then standard install dirs on each OS, or a path in `pc/user/paint/config.json`), builds the extension (`aseprite/build_extension.py`) and installs it into Aseprite's user extensions folder (assumption: copying the package there works; else it opens the file and tells the user to accept), writes the config (RPC address, pack author id), and ends by printing the five commands below. The studio itself is part of the game (editor panel `Studio`); nothing else to install.
- **Guide**: one page `pc/docs/Painting_Guide.md` (summon, paint, save, `pack new`, `pack build`, `pack check`, what is never shared). Short because the tools do the checking.
- **Verbs** (one tool, `pc/tools/paint_pack.py`): `new <id>` (skeleton + manifest), `build` (layers to maps, rules of 7.1, coverage), `check PACK` (below), `install PACK` (copy to `pc/user/plugins`, check first), `conflicts`, `progress`, `rekey`.
- **`pack check`** lints: manifest complete and licence in the allowed list; plugin id and version valid; every model key and texture key exists in the census (a warning when no census yet); every map's size equals its texture's; file whitelist and PNG format; the no-game-pixels tests (the strong ones when the installer's game data is present); `entries.json` values in range; `coverage.json` matches the maps; total size. Exit code 0/1 and a JSON report (`--json`) so CI can run it.

### 7.7 Windows (contributors)

Read from the code, not run: what is portable and what is not.
- **Portable**: the `.aseprite` reader/writer (pure Python + zlib), `miniws.py` (stdlib sockets, localhost WebSocket), `watch` (polls mtimes every 0.2 s, no inotify), the Lua extension (Aseprite's Lua `WebSocket` is the same on every OS), `os.replace`, pathlib. `rood_rpc` already has a `tcp:PORT[:TOKEN]` mode (a per-client token), which is what Windows must use.
- **Linux-only today**: the default RPC endpoint is a Unix socket path (`rood_rpc.DEFAULT_SOCKET`; the paint tools must default to `tcp:` on Windows, from `config.json`); `paint_roundtrip.py export` *launches* its own game through `material_rollout.launch` (Xvfb and EGL headless, `_die_with_parent`/`prctl`) so on Windows export must be `--attach` to the running game (the studio design needs `--attach` anyway); `shutil.which("aseprite")` finds only a PATH install (Steam and installer paths are not on it); `paint_roundtrip.py` imports `room_blender`/`material_rollout`/`rood_rpc` at module import, so any Linux-only import-time call (the parent-death hook) would stop the tools starting **(unchecked: make the Linux bits lazy and test)**; Python, numpy and Pillow must be installed by the user (the Windows dist does not ship them **(assumption)**). The local WebSocket listener may trigger a Windows firewall prompt (bind 127.0.0.1 only).
- Tests for the pack tools (`check`, `build`, `rekey`) must run without the game and without Aseprite so they can run on a Windows CI runner; the live loop is tested against a real Aseprite on Linux, and by hand on Windows.

## 8. Phases (each useful alone, each with its proof)

- **P0 spikes (about a day of agent time each)**: S1 a floor mesh and a ZUD loaded through `studio_load` under the full lit path, screenshot shows a visibly metallic response. S2 the same character at two pages: does the mesh hash change, and which masked words make it stable; collisions across every loadable model. S3 animation: per-sequence tick counts; scrub-by-replay equals straight play (bone matrices). Proof: three small scripts and their numbers in the plan; they decide 4.2.2 and 7.2.
- **P1 channel table + Height** (no studio): the refactor and the Height layer, rooms and models. Proof: `aseprite_test.py` (layers read/write, transparent keeps, unchanged import writes nothing, the new rows), `paint_roundtrip_smoke.py` (a painted height texel changes only nearby shaded normals; classic frames unchanged), `aseprite_smoke.py` live.
- **P2 Studio stage, summon, orbit/zoom, `neutral` and `flat`, catalog**: `studio_smoke.py` (EGL headless): summon an enemy, a character, a weapon; background pixels equal the Look; model centred; orbit changes the picture; leaving restores the game (state hash, golden frame); classic frames identical.
- **P3 Stable model key + migration + paint from the studio**: content key (7.2), `rekey`, `export --attach`, `studio paint`. Proof: a test model loaded at two pages has one key; `rekey` on a fixture user tree moves maps and entries and writes aliases, an unchanged tree stays byte-identical; paint a texel in the studio and see it in the model's room.
- **P4 Paint packs** (the community core): layer list in `materials.cpp`, `paint_pack.py build|check|install|conflicts`, manifest, rules file, per-model winner. Proof, all without game or Aseprite: unit tests that a pack with a copy of luminance, a lookup of luminance, a stray `.aseprite`, a `tex/` folder, an alpha-channel PNG, a missing licence each fail `check`; a clean pack passes; two packs and the user layer resolve in the stated order per channel and per model (`material` RPC reports the supplying layer); a partial pack falls through; classic frames identical with no packs.
- **P5 Census, progress, onboarding**: `paint_census.py`, Studio list columns, `paint_setup.py`, the guide. Proof: census of a fixture run lists every studio-catalog model with keys and sizes; progress of the P4 test pack equals its known painted fraction; `paint_setup.py --dry-run` reports found/missing pieces on Linux and (by test double) Windows paths.
- **P6 Windows pass**: `--attach` default on Windows, `tcp:` default, lazy Linux imports, Aseprite path discovery. Proof: pack tool tests pass on a Windows runner; a manual live-stroke run recorded once.
- **P7 Animation transport and equipment**: proof as earlier (bone palette at frame n equals n ticks played; hold: zero differing pixels over 30 frames).
- **P8 Emissive** (object folder, `emissive_color`, planes, `emissive.json` entry): painted amount and colour show at that colour in the emissive view; an unpainted model has no entry and an identical frame; packs carry `emissive_color`.
- **P9 Home-room lighting + `raking`**: stage lights equal `lighting::effective(zone, room)` via RPC; the picture differs from `neutral`.
- **P10 Alpha**: cut-out first (native, packet, RT any-hit, probe BVH, TLAS), dithered partials after. Painted-out texels show the backdrop, cast no shadow and no reflection (RT on); classic frames identical.
Targeted tests per phase; one full verify on main per merge batch.

## 9. What is left to the builder

Exact RPC parameter names; the `studio` Details schema; sequence names; the Studio panel layout; Library "Models" thumbnails (snapshot from the stage, cached in `pc/build`, or none); preset naming and per-user storage; the Lua API shape; fixtures; grey values; the thresholds in `pack_rules.json`; how a stroke's live view resolves layers (always the user layer) in the panel; the allowed-licence list's UI.

## 10. Questions only the owner can answer (four)

1. **Sharing policy**: which licences may a pack use (CC0 only, or CC0 and CC-BY, or the contributor's choice among them), and may a pack include screenshots of the game (they carry game pixels) or must previews always be rendered locally on the installer's machine (my default)?
2. **Alpha and emissive meaning**: is cut-out plus dithered partial see-through enough (no true blended glass)? Does a painted emissive pixel mean "glow exactly this colour at this brightness" (my proposal), and may a painted glow light the room or only bloom?
3. **Who wins and what ships**: may an accepted pack be committed under `pc/assets` after review (only its own maps; the existing shipped-map rule would be extended), or do packs always stay user-installed? And in a conflict, does the owner's own paint always beat every pack (my default)?
4. **Home room**: is "the room's lights and fog on the stage, without its walls" enough, or must it be the real room with the model standing in it? (I assume the studio is dev-tools-only, not a player-facing gallery.)

## 11. Owner answers (2026-11-22)

1. **Licences**: the contributor chooses CC0 or CC-BY. Previews are always rendered locally; no game screenshots in packs.
2. **Emissive**: the painted colour is the glow colour and its brightness is the glow's brightness (black: none, darker: dimmer). A per-preset / per-pack strength multiplier (data) reaches beyond what 8-bit colour can say. A glow may light the room, switchable per pack. **Alpha**: on/off plus a dithered fade for partials (the PS1 look).
3. **Shipping**: reviewed packs may be committed under `pc/assets` as shipped art: every layer except base colour, which is never shipped. The owner's own paint always wins over packs.
4. **Studio lighting**: the home room's lights and fog on the stage (not the real room) is enough, **provided the studio has lighting controls**: a head lamp (a light on the camera), a directional (sun) light, a sky light, point lights, with their positions and directions movable (gizmos in the view and numbers in a panel), all as data presets the owner can save.

### Builder's calls pending the owner (2026-11-23, P1)

- **Painted height adds to the texture's own estimate.** The owner asked that a painted height shade the way a baked one does, `baked_texture_relief` included, so painted maps (the user's layer, maps set live) take a share of the texture's estimate on top, set by the new tunable `painted_texture_relief` (default 1; 0 makes the painting alone the whole relief, as 5.1 first said). It keeps the first stroke from changing texels it did not touch. This is the builder's reading, not yet confirmed by the owner. Shipped maps stay the whole relief.
- A Height layer left as exported writes and pushes nothing; erasing it where the game had no height map takes the live map away (`material map_clear`) so the estimate shows again.
- Height is read only from the Aseprite layer, not from other painters' PNG exports.
- **P3 (builder's calls):** the content key is the spike's v-normalised hash widened to 64 bits and cut to 40 (10 hex digits, `m` + 10, as 7.2 says); the legacy 8 digit id keeps matching in place (entries and `@m<legacy>` folders are never renamed, so an untouched tree works byte for byte); old ids reach a model loaded at another page through `model_aliases.json` and the `rekey` tool, which copies and never moves. `rekey` needs the model loaded to learn its legacy ids; the second Ashley id appears only after a visit to a room that loads her at the other page (`--visit`). Not done (P3 in the plan names them, the brief did not): `export --attach`, `studio paint`, the game answering RPC for models not loaded.
