# Post-processing

The post stack (`pc/src/post.h`, `pc/src/post_profile.cpp`, the presenters'
`pc/src/post_gl.inc` and `pc/src/post_vk.inc`, shaders
`pc/shaders/vulkan/post_grade.glsl`, `post_bloom.glsl`, `bloom_fft*.comp`)
works on the game's 3D picture after lighting, before the 2D/UI is
composited for the player. **Everything is off or neutral by default**: with
no profile set, no extra pass runs and the picture is the original one, bit
for bit (golden frames and the before/after comparison of default frames
check it). Nothing ships turned on: not in code, not in the presets, not in
generated lighting packs.

## What it does, in order

The lit image (the present shader's output: lighting, haze, depth fog)
goes through:

1. anti-aliasing (FXAA / SMAA; MSAA and supersampling are earlier and later),
2. depth of field and tilt-shift (the gather blur),
3. **the grade pass** (only when an effect in it is on):
   1. sharpen (contrast-adaptive: never past the neighbours' range),
   2. chromatic aberration (radial; red outward, blue inward; from a start
      offset),
   3. into linear light (the PS1's display-referred colours: a 2.2 power),
   4. exposure (manual EV, or auto exposure between a min and a max EV with
      separate speeds up and down; metered centre-weighted on the 3D),
   5. bloom (added in linear light),
   6. white balance (temperature and tint, Bradford adaptation),
   7. colour grading as Unreal's ColorCorrect: saturation, contrast, gamma,
      gain, offset, each per channel and master, global x shadows / midtones
      / highlights (blended by luminance, `shadows_max`, `highlights_min`),
   8. vignette,
   9. tone mapping: `none` (the original picture: the lit image's own soft
      shoulder above 0.8, then a clamp), `aces` (the fitted RRT + ODT),
      `agx`, `filmic` (Hable), `reinhard` (extended, on luminance),
      `neutral` (Khronos PBR Neutral); `tonemap_white` maps to white,
   10. back to display encoding, the 3D LUT (`.cube`), film grain;
4. supersampling's downsample, the settings brightness curve, the window.

The 2D/UI the game draws (depth 0: the HUD, text, menus, pictures) passes
through every step untouched: the present shader marks it (alpha 9999), the
blur keeps the mark when a grade pass follows, and the grade pass copies
those pixels as they are; bloom and chromatic aberration never take light
from them. Movies (24-bit display) are presented from VRAM without any of
this. The overlay (modern menus, editor) is drawn afterwards.

### The modern menu's Ashley model

The Ashley tab freezes the world and has the game draw Ashley's model alone
over a flat 2D tile (`pc/src/ui/ashley_bridge.c`). `gpu_hw.c` tags those
frames (`pc_hw_frame_menu_model`, set right after the battle loop's
DrawOTag), and a present that shows one (`pc_hw_present_menu_model`) runs
every field neutral (no grade, bloom, vignette, grain, aberration; the
screen fade still applies; auto exposure keeps its adaptation for after; RPC
`post` reports `menu_model`). While the modern menu is up, every present of
the world keeps the chain's finished picture (before game text and the
brightness curve); a model-view present shows that picture, under the menu's
ground colour at `ui_menu_opacity`, in place of the tile's 2D pixels, in the
present shader before anti-aliasing. So the model is never graded, and the
world behind it keeps the post it was shown with, as on the other tabs.
`pc/tests/ui_ashley_stability_smoke.py` checks both with a strong profile.

With a tonemapper other than `none` the lit image keeps light above 1 (the
present shader's post bit 8 skips its shoulder and clamp) for the tone curve.

### Bloom

- **Gaussian** (both renderers; OpenGL always): the bright part (exposed
  linear light above `bloom_threshold` with a soft knee; Karis-weighted so
  one bright texel cannot flicker) filtered down a mip chain with 13 taps,
  then back up adding a 3 x 3 tent of each smaller level; `bloom_size` sets
  the levels (about 5 at 1).
- **Convolution** (Vulkan compute, the default where available): the
  second level of the same chain is placed in the middle of an n x n domain
  (`post_bloom_fft_size` 256 or 512; the picture's long side fills half of
  it, so the kernel never wraps light across the picture), transformed with
  a shared-memory radix-2 FFT (rows, then columns; red + i green and blue +
  i 0 as two complex signals), multiplied by the kernel's spectrum and
  transformed back. The kernel is our own, procedural
  (`pc_post_kernel_build`): a soft halo (`bloom_kernel_core` of the
  energy), a power-law glare (`bloom_kernel_falloff`), optional diffraction
  streaks (`bloom_kernel_streaks`, `_streak_intensity`, `_angle`), the
  centre texel zero (the light itself stays in the picture), normalised to
  1; it is built, uploaded and transformed only when its parameters change.

### The game's screen fade

Room transitions and cutscenes fade with a full-screen flat quad subtracted
from the screen (BATTLE `_drawScreenTransition`, effect 0, at the front of
the ordering table). Drawn as translucent 2D it would mark the whole
picture as 2D/UI, so lighting, haze and post effects would drop out from
its first frame (the unlit PS1 picture, even a little brighter) and pop back
after the fade-in. `gpu_hw.c` recognises it (a flat, untextured,
subtractive 2D triangle covering the whole drawing area: `BLEND_FADE`) and
subtracts it only from the 2D under it (depth 0; on OpenGL `GL_EQUAL`, on
Vulkan `VK_COMPARE_OP_EQUAL`); the 3D keeps its depth and surface, and the
frame's fade amount (interpolated with the frame on interpolated presents,
`pc_hw_present_fade`) is subtracted from the finished 3D picture as the last
step of the grade pass, which runs for it whatever the profile, over the part
of the view the quad covers (all of it; in widescreen without the cover fix,
the 4:3 middle). 2D drawn
after the fade (in front of it) stays unfaded, as on the PS1. What fills and
uploads left (far depth: a backdrop the game uploaded, such as the title's)
is no 3D either: the quad is drawn a second time at far depth, so it fades
there too (the title's 64 subtractions after Continue, over a picture the
game no longer redraws, darken its uploaded backdrop as on the PS1; the grade
pass also subtracts the frame's amount there, a few levels per frame, not
accumulated). `pc/tests/transition_fade_smoke.py` follows a door and a cross-zone
transition frame by frame; `pc/tests/title_fade_smoke.py` the title's fade
against the software rasteriser.

### Film grain

Value noise on a lattice of `grain_size` view pixels (at 1080 lines), with a
response per shadows / midtones / highlights and optional colour. Its seed
is the game frame the picture shows (`pc_hw_frame_serial`): interpolated
presents and paused redraws show the same grain; the next game frame
changes it.

## Depth of field: field play only

Depth of field and tilt-shift run only in field play with Ashley present.
They are off, whatever any profile says, on the title (`pc_game_phase`: from
boot and on every return to the title), in an attract demo (the title's idle
cutscenes), while a field menu is open (`vs_battle_menuState`, and the modern
menus' shell), and when Ashley is not there (nothing to focus on). RPC `post`
reports why (`lens_blur_off`: `title`, `attract`, `menu`, `no_player`). In a
cutscene the built-in cutscene layer turns them off; a cutscene layer may
turn them on (the hook for authored cutscene depth of field, below).

When on: focus "On Ashley" uses her tracked position (her chest's view
depth), never the depth under a screen point, so a wall in front does not
steal the focus; the focus eases over `dof_focus_speed` seconds of game time,
holds while she is briefly missing, and snaps on a camera cut or a room
change. In front of the focus the blur is `dof_near_blur` of the maximum
(0.5): walls and props between the camera and Ashley become a soft veil
rather than a smear.

## Contexts: gameplay and cutscenes

A cutscene is an event script running: the event runner's state
(`D_800F4C34`, the flag the smart walls already use; `pc_battle_event_running`)
is nonzero from when an event loads until its script ends, for room entry
scripts, the zone's event files and door scripts alike; dialogue waiting for
a button is still the event. The context returns to gameplay after 10
vsyncs without one (scripts run back to back). The event's id
(`pc_battle_event_id`: (source << 12) | file, as `func_800BEB10` gives it)
is known for keyed profiles. The camera cuts the game makes inside a
cutscene are found the way frame interpolation finds them (a jump in camera
rotation or position): a cutscene that starts on a cut switches profiles at
once instead of blending.

## Profiles: the data model

A profile is a set of fields (the table in `post_profile.cpp`: one place
defines each field's key, kind, default, range, editor group and tooltip;
settings, lighting items, preset JSON, the RPC, blending and the editor all
follow it). Like Unreal's post process volumes, a layer only overrides the
fields it sets. Layers, lowest first:

| Layer | Where | Edited by |
| --- | --- | --- |
| defaults | the field table: everything off / neutral | code |
| gameplay preset | settings `post_preset <name>` -> `pc/assets/post/<name>.json` (tracked) or `pc/user/post/<name>.json` (yours, wins) | Details > Post-processing > Preset |
| gameplay values | settings `post_<field> <value>` (with `post_set`: the fields set) | Details > Post-processing (Gameplay) |
| the room | lighting packs item `post room` (generated, authored, user layers like any item) | Details > Post-processing (This room), Save area |
| *in a cutscene:* built-in | depth of field and tilt-shift off | code |
| cutscene preset and values | settings `post_cutscene_preset`, `post_cutscene_<field>` | (Cutscenes) |
| the room's cutscenes | item `post cutscene` | (This room's cutscenes) |
| one event | item `post event.<id>` | lighting files (editor support later) |

A layer may name a preset (`preset=film` in an item, `post_preset` in the
settings): the preset's fields apply under the layer's own. The resolved
profile blends over `post_blend_time` seconds (game time; smoothstep; the
tonemapper too, by mixing both curves) when a room change or a context
change alters it, and snaps on edits, on the first frame and on a cutscene
that starts on a camera cut. `post_stack 0` turns every effect off.

```
# lighting/<pack>/rooms/zone09_room17.txt
post room preset=film exposure=-0.3 vignette=0.4
post cutscene tonemapper=aces exposure=0.5
post event.4096 dof=1 dof_focus=manual dof_focus_depth=900
```

```json
// pc/assets/post/film.json
{ "title": "Film", "description": "...", "values": { "tonemapper": "agx", "exposure": 0.2, "contrast": [1, 1, 1, 1.15] } }
```

Shipped presets (none turns depth of field on): **cinematic** (ACES, a soft
convolution glare, vignette, slight fringes, fine grain), **film** (AgX,
warm white balance, lifted blacks, visible grain), **clean modern** (Khronos
neutral, a subtle bloom, a little sharpening).

### Per-cutscene profiles later (design)

The pieces are in place for authored cutscene looks without new formats:

- **Keyed by scene/event**: `post event.<id>` items in the room's lighting
  file (source-controlled, layered generated / authored / user, reviewed in
  pull requests like lights). The id is the event runner's own, stable
  across runs.
- **Scriptable over time** (to do): an item may gain keyframes, e.g.
  `key.<n>=<frame>:<field>=<value>,...` or a separate
  `pc/assets/post/cutscenes/<zone>_<event>.json` with a track per field,
  sampled by the event's own frame counter so it is deterministic and
  stays in sync with interpolation; the blend machinery already
  interpolates fields between resolved profiles.
- **Focus targets**: `dof_focus` gains `actor` (an actor slot or name the
  script uses) and `point` (a world position), so a cutscene can rack focus
  between speakers.
- **Extensible by others**: a new effect is one row in the field table (plus
  its shader code); every layer, file format, the RPC and the editor pick
  it up.

## Settings, RPC, editor

| Key | Default | Meaning |
| --- | --- | --- |
| `post_stack` | 1 | master switch (0: every post effect off, whatever the profiles) |
| `post_preset`, `post_cutscene_preset` | none | the gameplay / cutscene preset |
| `post_<field>`, `post_cutscene_<field>` | (unset) | the layers' own values; `post_set` / `post_cutscene_set` list the fields set |
| `post_blend_time` | 1 | seconds profiles blend over on room and context changes |
| `post_bloom_fft_size` | 512 | the convolution bloom's domain (256 or 512) |
| `post_flags`, `focus_mode`, `aperture`, `focus_depth`, `dof_radius`, `tilt_shift`, `depth_fog`, `depth_fog_range` | | older keys, still read into the gameplay values where they differ from the defaults |

RPC `post` {`layer` (`gameplay`, `cutscene`, `room`, `room_cutscene`),
`set` {field: value}, `clear` ([fields] or `"all"`), `preset`, `context`
(`auto`, `gameplay`, `cutscene`: a preview), `stack`, `blend_time`,
`fft_size`, `save_preset` (the layer as a user preset), `snap`} replies with
the context (`detected`, `event`), `lens_blur_off`, `grade`, `bloom` (method
used), `ev`, `focus`, `blend` progress, the target and shown values, every
layer and the presets. RPC `graphics` `post` (bits 1 DoF, 2 tilt-shift, 4
depth fog) sets the gameplay values.

Details > Post-processing (the editor): grouped like Unreal's Post Process
Volume (Lens: Depth of Field, Tilt-shift, Exposure, Bloom, Chromatic
Aberration, Image Effects; Rendering Features; Color Grading: White
Balance, Global, Shadows, Midtones, Highlights, Misc; Film: Tonemapper,
Grain), an override checkbox per field, the layer to edit, presets (Save as
preset), a context preview and the live state.

## Cost

Vulkan, RTX-class GPU, ray-traced GI on, save room, GPU sections from RPC
`perf` (ROOD_PERF=1), ms per frame:

| Profile | 1440 x 1080 view | 2880 x 2160 view |
| --- | --- | --- |
| everything off | 0 | 0 |
| grade only (a tonemapper) | 0.01 | 0.03 |
| Gaussian bloom | 0.05 + grade 0.01 | 0.09 + 0.04 |
| convolution bloom (512) | 0.02 + FFT 0.03 + grade 0.01 | 0.04 + 0.03 + 0.02 |
| cinematic | 0.08 | ~0.5 |
| film | 0.08 | ~0.45 |
| clean modern | 0.07 | ~0.5 |
| depth of field (gather blur) | 0.05 | 0.22 |

Tests: `pc/tests/post_smoke.py` (each effect, the 2D/UI untouched, defaults
neutral, contexts, profiles, presets, blending, auto exposure).
