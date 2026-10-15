# The fog volumes' own renderer, and a stable haze (2026-10-14)

Status: **built** (Vulkan, OpenGL 32/64-bit), on by default
(`fog_volume_render 1`). Follows
[2026-10-13-interactive-fog.md](2026-10-13-interactive-fog.md).

## Why

The user's playtest (Undercity West, zone 48 room 4; a hand-placed Rolling
fog volume, interaction on, density 0.016, swirl 2.5, detail 0.85, shadow
0.5, fill 0.06-0.08, the room's haze limits raised to 0.85 / 0.35): "It
shimmers a lot, it's not very thick, light doesn't seem to affect it." Also
the ground fog shimmered with the wind at 0 and the fog's time frozen, and
fog in dark rooms read black. The target: dense, billowing volumetric smoke
lit and coloured by the lights near it (orange fire-lit fog rolling around
a figure's legs with the fire glowing in it; green smoke in a vat with lit
rims, dark cores, light bleeding through).

Diagnosis:

- the volumes were folded into the room's froxel haze (cells 2-3 display
  pixels, 64 log slices): a grid sized for thin even haze, too coarse for
  billows, and its stochastic samples (one light picked per cell per frame,
  a random point, a random jitter, a 10 % history) never settled: shimmer;
- one sampled light per cell plus the moon: the fires and glows near a
  volume hardly lit it;
- the room's haze limits (opacity 0.55, light 0.15) capped every volume;
- nothing lit fog where no light reaches: black in a dark room;
- (the haze's phase function has its lobe toward the eye's side, `+2 g mu`
  in `medium_phase`; its limits and the atmosphere sweep are tuned around
  it, so it is left as it is; the volumes' renderer uses the usual sign.)

## What

Hand-placed volumes (the first 8 enabled ones of a room) leave the haze and
get their own renderer (`pc/shaders/vulkan/fog_volume.glsl`, record
`atmosphere.h pc_fogvol_gpu`, built by `atmosphere.cpp pc_fogvol_gpu_get`;
the medium record's 4.w is the first volume the haze passes include). The
room's haze and ground fog keep the froxels, made stable.

### Density

The volume's shape (box with rounded corners in plan, or sphere; soft edge
`edge`) times its height profile (`taper`: 0 even, 1 thinning toward its
top) times its wisps (the baked noise at the wisp size: one texture fetch
where the haze's gradient noise costs four octaves) times the interactive
fog's 2D field (64 x 64, bilinear) times the baked 64^3 Perlin-Worley
detail, two octaves (period `detail_size`, else 0.6 x the room's: about
300 units, and 2.7 x finer) carried along by the field's flow over
staggered 2.5 s cycles, eroding the soft parts and making billows. Sampled
at every step of a per-pixel march (48 samples over the volume's part of
the ray at half the internal resolution, with the temporal accumulation
about 16 x that), where the froxels had one sample per cell.

A small 3D simulation (a 32^3 or 48^3 velocity and density grid per volume
on the GPU, the 2D field as its floor source) is the next step if rising,
overturning smoke is wanted (a brazier's column, smoke pouring over a
ledge); not built.

### Lighting

Per sample, for each volume it is in:

- its **six most important lights** (`pc_lights_list`: lights, light
  actors and fixtures, fires and torches included; and the glows,
  `pc_fog_glows`: the room's glowing polygons, effects, the save point's
  column as a segment light), ranked by brightness x falloff at the
  volume's nearest point, kept in stable slots (a light keeps its slot
  while it stays among the six);
- the **sun or moon**;
- a phase function (Schlick's fit of Henyey-Greenstein, `phase` 0.3:
  brighter looking toward a fire through its smoke) and one
  multiple-scattering octave (`fog_volume_scattering` 0.5: g / 2, the
  square root of the transmittance: thick fog stays bright and soft);
- a **halo** near each light (`fog_volume_halo` 2: up to 3 x within about
  100 units): the fire glows in its smoke;
- **shadows**: Vulkan keeps per volume a 32^3 grid of the transmittance
  toward each light slot, the sun and an ambient term (its own density
  over 8 steps, x 0.5 + `shadow`; with ray queries, when the path tracer
  built this frame's acceleration structure, one any-hit ray to each light
  and the sun: walls and Ashley shadow the fog), half of it updated a frame
  and blended; OpenGL estimates each shadow from the volume's mean density
  over its depth toward the light (a ray-box exit);
- an **ambient** term (`ambient`, `fog_volume_ambient`): the room's baked
  light around the volume (area- and distance-weighted vertex colours of
  the captured room, x 0.45 for a typical texture's albedo) plus the sky,
  x (0.5 + 0.5 x its ambient transmittance): fog in a dark room is dim
  grey, not black. (The probe GI field would be the better source where it
  runs; the baked estimate works on every quality and renderer.)
- its `fill`, and its own **glow** (`glow`, `glow_core`: smoke lit from
  within, gathered at its base: a vat, embers);
- x `brightness`; the ray's transmittance never under 1 - `opacity`.

No room haze limit applies to them.

### Sampling, stability, compositing

- Vulkan (`vk_fogvol.inc`): light grid, march, temporal, all compute.
  March: the ray's parts inside volumes only (empty space skipped), up to
  the scene's depth under the pixel (fog behind geometry is hidden, in front
  hides it; 2D and nothing drawn: the far side), stopping under 0.3 %
  transmittance. Offsets: interleaved gradient noise fixed per pixel plus
  the golden ratio x (frame mod 16). Temporal: the fog point reprojected,
  history rejected where the scene depth behind changed (disocclusion, a
  character crossing), clamped to the neighbourhood (wider while still),
  weight 1 / frames down to `fog_volume_history` (0.06), a 3 x 3
  depth-aware filter first. Present: joint bilateral upsample, `c x T + L`
  in linear light before the haze, the post effects and the UI.
- OpenGL (`gl_present.c`): the same march per pixel in the present,
  `fog_volume_steps_gl` (20) samples, offsets fixed per pixel (a still
  picture is exactly still), the analytic shadows.
- Haze froxels (`rt_haze.comp`): every random choice cycles through 16
  frames; history 96 % a frame while still, up to 30 % new as a froxel
  moves 1.5 cells; a 1-2-1 filter across slices on top of the 3 x 3.

## Measured

Vulkan RT (RTX PRO 6000, shared with two other agents' tests), 1440 x
1080, Ultra, internal scale 4, `ROOD_PERF=1`:

| Undercity West (median of 5 x 120 frames, GPU idle otherwise) | grid | march | temporal | total |
| --- | --- | --- | --- | --- |
| the user's volume (900 x 140 x 900) | 0.07 | 0.38 | 0.02 | 0.47 ms |
| + a Thick smoke volume (400 x 220 x 400) | 0.13 | 0.54 | 0.01 | 0.68 ms |
| both, after walking (closer camera) | 0.09 | 0.42 | 0.02 | 0.53 ms |
| first version (four-octave ALU wisps), one volume | 0.07 | 1.8 | 0.05 | 1.9 ms |

The haze froxels: 0.075-0.085 ms, unchanged. On the GPU shared with other
agents' tests the same passes read 2-5 x higher (time-sliced).

Still frames (paused, the game redrawing, fog time frozen, light flicker
off), mean change between consecutive presented frames / 99.9th
percentile, 0..255, Undercity West:

| | before | after |
| --- | --- | --- |
| room haze + ground fog (Vulkan froxels) | 0.12 / 2.0 | 0.03 / 0.7 |
| the user's volume (Vulkan) | 0.29 / 6.0 | 0.04 / 1.0 |
| OpenGL (both) | flicker only | 0 / 0 |

(With the lights' flicker on, the lit picture itself changes: 0.05 / 3 on
OpenGL before and after.)

`pc/tests/fog_volume_smoke.py` (all three configurations): a dense volume
reaches opacity >= 0.9 (its densest tenth lets 0.000 through) and its own
`opacity` 0.5 caps it at 0.49-0.50 with `haze_max_opacity` 0.2; a volume
under the floor stays hidden; a red light inside raises its red 0.22 ->
0.63 (Vulkan) / 0.44 -> 0.90 (OpenGL) and the red/green ratio 2 x, and it
returns when the light goes; in Undercity West its light is 0.26-0.34
(0.18-0.25 without the ambient term); still frames as above.

## Settings

`fog_volume_render` 1, `fog_volume_resolution` 0.5, `fog_volume_steps` 48,
`fog_volume_steps_gl` 20, `fog_volume_history` 0.06, `fog_volume_ambient`
1, `fog_volume_glow` 0.6, `fog_volume_scattering` 0.5, `fog_volume_halo` 2,
`fog_volume_shadows` 1. Per volume (lighting packs, presets, RPC, editor):
`opacity` 1, `brightness` 1, `phase` 0.3, `ambient` 1, `glow` 0,0,0,
`glow_core` 0.5, `taper` 0, `detail_size` 0. RPC `environment`
{`fog_volumes`: {... `debug` 1 their light alone, 2 their transmittance}}.

## Later

- A small 3D simulation per volume (above).
- Probe GI as the ambient term where the probes run.
- Volumes past the first 8 still go to the haze.
- Emitters (vents, a brazier's smoke column) feeding the field.
