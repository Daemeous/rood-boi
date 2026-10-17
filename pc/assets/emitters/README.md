# Emitter assets

An emitter asset is the look of a particle emitter (pc/docs/Particles.md):
how its particles spawn, how they move, how they are drawn and lit. An
emitter placed in a room (`emitter <id> asset=<name> ...` in the lighting
files, lighting/README.md "Emitters") takes every field it does not write
from its asset, so editing an asset here changes every emitter using it.
`ROOD_EMITTERS` reads another directory instead (tests).

```json
{
  "kind": "emitter",
  "name": "Dust motes", "category": "Atmosphere", "icon": "dust", "tags": ["dust"],
  "description": "What it is for and where it goes (the library shows it).",
  "quality": "dust",
  "spawn": { "shape": "box", "size": [384, 256, 384], "radius": 64, "count": 900, "lifetime": 14, "lifetime_jitter": 0.5 },
  "modules": {
    "drift": { "velocity": [1.2, 0.4, 0.8] },
    "curl": { "amplitude": 9, "scale": 56, "speed": 0.06 },
    "gravity": { "acceleration": 0 },
    "drag": { "rate": 0 },
    "fade": { "in": 2.5, "out": 3 }
  },
  "render": { "type": "pixel", "pixel_min": 1, "pixel_max": 2, "color": [1, 0.95, 0.85], "brightness": 0.8, "variation": 0.6 },
  "lighting": { "mode": "in_light", "ambient": 0 }
}
```

| JSON | Lighting field | Meaning |
| --- | --- | --- |
| `quality` | `quality` | the graphics setting that scales it: `dust` (Graphics > Dust: Off none, Low `dust_low` of its count, High all) or `always` (all of them, whatever Dust is) |
| `spawn.shape` | `shape` | `box` (in its box), `room` (in the room's bounds), `point` (a ball of `radius`), `disc` (a horizontal disc of `radius`), `surface` (on the floor under its box) |
| `spawn.size`, `spawn.radius` | `size`, `radius` | the box's full extents; the point's or disc's radius (world units) |
| `spawn.count`, `spawn.lifetime`, `spawn.lifetime_jitter` | `count`, `lifetime`, `lifetime_jitter` | particles alive at once (High), seconds each lives, the share of that a particle may live less |
| `modules.drift.velocity` | `drift` | its steady drift, world units a second (y down) |
| `modules.curl.amplitude`, `.scale`, `.speed` | `curl`, `curl_scale`, `curl_speed` | turbulence: how far (units) a smooth swirling field moves it, the swirls' size, how fast they change |
| `modules.gravity.acceleration` | `gravity` | units a second squared, y down (negative: it rises) |
| `modules.drag.rate` | `drag` | per second: it falls at most `gravity / drag` |
| `modules.fade.in`, `.out` | `fade_in`, `fade_out` | seconds to appear and to go |
| `render.type` | `render` | `pixel` (square game pixels, nearest; soft sprites and streaks later) |
| `render.pixel_min`, `.pixel_max` | `pixel_min`, `pixel_max` | its size in game pixels (1..4), the same near and far |
| `render.color`, `.brightness`, `.variation` | `color`, `brightness`, `variation` | its colour (linear), scale, how much dimmer each one may be |
| `lighting.mode`, `.ambient` | `lit`, `ambient` | `in_light` (0: it shows only where light reaches it) or `lit` (1: also `ambient` everywhere) |

A module left out does nothing. `kind`, `name`, `category`, `icon`, `tags`
and `description` are what the editor's library lists.
