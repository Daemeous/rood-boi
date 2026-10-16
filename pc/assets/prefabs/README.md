# Light actor prefabs

Built-in light actors for the editor (authored for this port; no game data).
Each `ID.json` is one prefab, its id the file name. Your own and edited
prefabs live in `pc/user/prefabs` (`ROOD_PREFABS`) and replace a built-in with
the same id; both directories reload live. Code: `pc/src/light_prefabs.h`.

```json
{
  "name": "Candle",              // shown in Place Actors and the Outliner
  "category": "Fire",            // Place Actors groups by it
  "icon": "candle",              // candle, candelabra, lantern, torch, brazier, fireplace,
                                 // window, magic, light:point, light:spot, light:rect
  "description": "...",
  "placement": { "align": "up", "surface_offset": 0, "fixture_radius": 14 },
  "params": [ { "key": "intensity", "label": "Intensity", "group": "Light",
                "type": "float", "default": 0.9, "min": 0, "max": 6, "unit": "", "log": false,
                "tooltip": "..." } ],
  "lights": [ { "name": "Flame", "type": "point", "offset": [0, "-flame_height", 0],
                "color": "kelvin(temperature) * tint", "intensity": "intensity", ... } ],
  "glow": { "mode": "glow_mode", "scale": "glow_intensity",
            "suggest": { "hue": "warm", "max_size": 4.5, "priority": 1, "flames": true, "flame_max": 3 } }
}
```

`//` comments and trailing commas are accepted.

**placement**: `align` `up` keeps the actor upright on a surface, `normal`
turns its forward (+z) along the surface normal (wall torches, windows,
fireplaces); `surface_offset` lifts the pivot off the surface;
`fixture_radius`: when the actor lights a room fixture, room polygons within
this distance of its lights cast no shadow from them.

**params**: `type` `float` (`min`, `max`, `log`, `unit`), `color` (default
`[r, g, b]` 0-1), `bool` (`true`/`false`) or `enum` (`options` list; default
an option name). `group` makes a Details section. An actor stores only the
parameters it overrides.

**lights**: `type` `point`, `spot` or `rect`; every other field is a number,
an expression string or a list of three (a vector):

| Field | Default | Meaning |
| --- | --- | --- |
| `offset` | 0 | from the pivot in the actor's frame: x right, **y down** (above is negative y, as in the game), z forward |
| `direction` | [0, 0, 1] | spot / rect facing, actor frame |
| `color` | 1 | display-space colour (the renderer adds light in display space) |
| `intensity`, `radius` | 1, 600 | as plain lights |
| `cone` | 45 | spot cone, degrees |
| `inner` | 0 | spot inner cone, degrees: full brightness inside it (0: the outer quarter of the cone fades) |
| `size` | 128 | rect width, height |
| `barn_angle`, `barn_length` | 45, 0 | rect barn doors: angle from its facing (0 straight ahead, 90 folded flat) and length (0: none; scales with the actor) |
| `shadows` | 0 | casts traced shadows |
| `light_size` | 0 | emitting radius for soft shadows (0: the room's light size) |
| `flicker`, `flicker_speed` | 0, 8 | intensity noise amount (0-1, mean-preserving) and rate (Hz) |
| `jitter` | 0 | the light wanders this far (units) at half the flicker rate |
| `pulse`, `pulse_speed` | 0, 0.5 | sinusoidal intensity pulse amount and rate (Hz) |
| `enabled` | 1 | |
| `phase` | 0 | a number: decorrelates lights of one actor |

Expressions: numbers, parameter names, `+ - * /`, parentheses, `vec(x, y, z)`,
`kelvin(K)` (blackbody colour, 1000-12000 K), `rgb(r, g, b)` (0-255),
`min`, `max`, `clamp(x, lo, hi)`, `mix(a, b, t)`, `select(c, a, b)`, `pow`,
`sqrt`, `abs`; vectors and scalars mix componentwise.

**glow**: `mode` (0 leave, 1 drive: the linked game glow's emission follows
the first light's flicker/pulse times `scale`, 2 suppress: mute it).
`suggest` makes the prefab a candidate for *Light this fixture* and *Light
all fixtures*: for glows by `hue` (`warm`, `cool`, `any`; `none` never),
emitting size (`min_size`, `max_size`, 2-8) and `min_brightness`; `merge`
folds glows this close into one actor (a large sigil); for flames found in
textures when `flames` is true, by strength (`flame_min`, `flame_max`). The
highest `priority` wins.

Animation is a deterministic function of time and the actor's seed (saved
with it), so the same time always gives the same lights (RPC `actors
{action: "sample", time}`).
