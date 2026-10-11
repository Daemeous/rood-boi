# Lighting packs

Per-room lighting for the PC port: lights, light actors (prefab instances),
overrides of the game's own glows and texture effects, and per-room settings.
It is data, authored in the editor (`pc/run-game.sh`, Escape) and kept here
as small text files that diff and merge well. It contains no game data: only
positions, colours and parameters authored for this port.

## Layers

A room's lighting is the merge of these layers, lowest first:

| Layer | Path | Tracked | Who writes it |
| --- | --- | --- | --- |
| generated | `lighting/<pack>/generated/zoneZZ_roomRR.txt` | yes | a generator (e.g. fixtures found in the room), regenerated wholesale; never edit by hand |
| authored | `lighting/<pack>/rooms/zoneZZ_roomRR.txt` | yes | people: hand edits and the editor's **Promote to pack**; pull requests |
| user | `pc/user/lighting/zoneZZ_roomRR.txt` | no | the editor's **Save area** (your own tweaks) |

Several packs stack in the order of the settings key `lighting_packs`
(`pc/user/settings.cfg`, default `default`; e.g. `lighting_packs default,moody`):
each pack's generated then authored layer, then the next pack's, then the
user layer. **Promote** writes the authored layer of the highest pack, or of
`lighting_active_pack` when set. Project defaults for the per-room settings
may also come from `lighting/<pack>/defaults.txt` (`set <key> <value>` lines),
under the user's `pc/user/lighting/defaults.txt`.

Every item has a stable id, and a higher layer changes a lower layer's item
by naming its id:

| Kind | Id | What |
| --- | --- | --- |
| `light` | `light:<14 hex digits>` | a point, spot or rect light |
| `actor` | `actor:<14 hex digits>` | a light actor (prefab instance) |
| `glow` | `glow:<glow key>` | an override of one of the game's glows (the key the editor shows; `@x,y,z` for old position-only ones) |
| `uv` | `uv:<1-64>` | an override of a room texture effect (speed, freeze) |
| `set` | `set:<key>` | a per-room setting (`baked_lighting`, `haze_density`, ...: `pc/src/room_settings.cpp`) |

The editor makes ids from the time and a random part, so ids made by
different people do not collide and sort in the order they were made.
Generated items take deterministic ids from what they were generated from
(`pc/tools/lighting_format.py` `generated_id("actor", "candle", fixture_key)`
gives `actor:gen.candle.<hash>`), so regenerating keeps every id, and the
overrides that name them, valid.

## File format (version 2)

```
rood-lighting 2
# zone 9 room 17: a comment (anywhere; also after an entry)
actor 68ea4c1200a3f1 prefab=candle pos=448,-40,576 rot=90,0,0 seed=7 intensity=1.4
override actor:gen.candle.5be1c0d2a9 intensity=0.6 !flicker
glow room:0000123@448,-40,576 pos=448,-40,576 intensity=0.5
light 68ea4c1d0071c9 name=Moonlight type=spot pos=120,-300,96 dir=0.2,1,0 color=0.6,0.7,1 intensity=2 shadows=1
delete light:gen.window.77c1a0e4f2
set haze_density value=0.0006
uv 3 speed=0.5
```

- The first line that is not blank or a comment is the header `rood-lighting 2`
  (files without it are the old `pc/lights` format, read as a user layer and
  rewritten in this format when the editor saves).
- One entry per line, one entry per id per file, sorted by id:
  - `<kind> <name> key=value ...` defines an item (id `kind:name`), or
    replaces one from a lower layer;
  - `override <id> key=value ... !key` changes fields of a lower layer's item
    (`!key` puts a field back to its default);
  - `delete <id>` removes a lower layer's item (to switch one off but keep it,
    `override <id> enabled=0`).
- Values are numbers, comma vectors (`x,y,z`) or words; `"quoted"` when they
  hold spaces (`\"` and `\\` inside). Numbers have fixed precision (positions
  0.1, colours 0.001, angles 0.01, ...), no trailing zeros. Fields at their
  defaults are left out.
- Light fields: `name type(point|spot|rect) pos dir color intensity radius
  cone size(w,h) shadows enabled`. Actor fields: `name prefab pos rot(yaw,pitch,roll)
  seed enabled glow glowkey fixture fixture_radius fixture_shadows`, and any
  parameter of its prefab (`pc/assets/prefabs/*.json`) it overrides. Glow:
  `pos muted intensity tint size reach`. Texture effect: `speed freeze`.
  Setting: `value`.

An override or delete whose id no lower layer defines is **dangling** (the
item was deleted or regenerated under it). It is never dropped: the editor
lists it under Outliner > Layers (and keeps it in your user layer until you
discard it), RPC `lighting` reports it and the lint fails on it.

## Editor

- **Save area** (Ctrl+S) writes your user layer: only what differs from the
  packs (`override` lines for changed lights and actors, definitions for new
  items, `delete` for removed ones).
- The Outliner marks each light and actor with its layers: `[G]` generated,
  `[P]` pack (authored), `[U]` yours only, `[G>P>U]` defined in a generated
  layer and changed in the authored one and by you. Right-click a row:
  **Revert to the layers below** (undoable), **Promote to pack**, Copy id.
  Outliner > Layers lists the files, what you deleted (Restore) and problems.
- **File > Promote room to pack** / **Promote selection to pack** saves, then
  moves your user-layer entries for the room (or the selection) into the
  pack's authored file, keeping its other lines and comments.
- Every layer file reloads live; when a pack changes on disk while you have
  unsaved edits (a `git pull`), your edits are kept on top of it.

## Contributing lighting

See "Contributing lighting" in [pc/README.md](../pc/README.md#contributing-lighting).
In short: light rooms in the editor, Promote to `default`, run
`python3 pc/tools/lighting_lint.py`, commit only `lighting/`.
