# Vulkan renderer

Phase 3 of [Modernization_Plan.md](Modernization_Plan.md): the whole frame on
Vulkan, with hardware ray tracing (`VK_KHR_ray_query`) for shadows, indirect
light and haze. 64-bit only (`build_boot.py --arch 64`); OpenGL stays the
default and unchanged until Vulkan has been playtested.

    ROOD_RENDERER=vulkan pc/build/boot64/rood_battle_probe --window
    python3 pc/tests/run.py --arch 64 --renderer vulkan

| Variable | Effect |
| --- | --- |
| `ROOD_RENDERER=vulkan` | Vulkan presentation and rendering (falls back to OpenGL when Vulkan is unavailable) |
| `ROOD_VK_DEVICE=N` | GPU by enumeration index (lists them); default: the first discrete 1.3 GPU |
| `ROOD_VK_RT=0` | no ray tracing (GI/shadow passes unavailable) |
| `ROOD_VK_VALIDATE=1` | Khronos validation layer (needs `VK_LAYER_PATH`; messages as `[vk] validation:`) |
| `ROOD_VK_HEADLESS=1` | render offscreen even with a window system (the SDL offscreen driver always does) |
| `ROOD_PERF=1` | adds `[perf] Vulkan GPU time per presented frame` (timestamps) |
| `ROOD_VK_PIPELINE_CACHE=path` | the pipeline cache file (default `$XDG_CACHE_HOME` or `~/.cache`, `%LOCALAPPDATA%` on Windows: `rood-boi/vulkan-pipelines-<vendor>-<device>.bin`); `0` for none |
| `ROOD_NRD=off` / `build_boot.py --arch 64 --nrd off` | build without NVIDIA NRD (see "NRD") |

## Architecture

The Vulkan path reuses everything above the GL calls. Files:

- Pipeline cache (`vk_core.c`): every pipeline (ours, NRD's, the overlay's)
  is created through one `VkPipelineCache` whose data is kept between runs
  (see `ROOD_VK_PIPELINE_CACHE`). The data's header (vendor, device, the
  driver's cache UUID) is checked before use; a new driver version or shader
  set (`VK_SHADERS_DIGEST`, `vk_shaders_digest.h` from `vk_shaders.py`)
  starts it afresh. It is saved 120 frames after the last new pipeline, and
  only when it grew. Without it (or the driver's own disk cache), a start
  compiles about 2.4 s of pipelines before the first frame and 0.8 s more on
  entering the first room (NRD, GI); with it, none. Pipelines that still
  compile after the first frame log `[vk] pipeline created after frame N`.
- `vk_core.c/.h`: Vulkan loaded through SDL (`SDL_Vulkan_GetVkGetInstanceProcAddr`,
  no link against libvulkan; SDL is built with `SDL_VULKAN` on for 64 bits),
  instance/device (1.3: dynamic rendering, synchronization2, push descriptors,
  geometry shaders, independent blend; ray query and acceleration structures
  when present), two frames in flight, per-frame host-visible upload arenas
  (vertices, uniforms, palettes, staging; chunks of 16 MB with device
  addresses for AS builds), deferred destruction, pipelines, readbacks.
- `vk_hw.c/.h`: the scaled VRAM target and G-buffer (colour, normal, albedo,
  shade, material (RGBA16, packed: `material_pack.glsl`, [Materials.md](Materials.md)), depth; a replay set for interpolation; room depth, the "before"
  depth, native VRAM as R16_UINT) and the operations of `gpu_hw.c`: packet
  batches, fills, uploads, VRAM copies, depth copies, replay copies, dumps,
  picking, VRAM sync readback.
- `vk_render.inc` (included at the end of `render.c`): native lists, the
  translucent and effect sequences, sprite meshes; meshes suballocated from
  16 MB vertex blocks.
- `vk_present.c`: window, swapchain (MAILBOX, else IMMEDIATE, else FIFO; the
  frame loop paces itself as with GL's swap interval 0) or none (headless),
  the present passes of `gl_present.c` (lighting, GI, haze, depth of field,
  tilt-shift, fog, FXAA) into a window-sized image, `present_dump` (bursts
  read back asynchronously when the frame slot comes round), then a blit to
  the swapchain image.
- `vk_overlay.cpp`: Dear ImGui's Vulkan backend (`imgui_impl_vulkan`, same
  upstream commit) on dynamic rendering; `dev_overlay.cpp` builds the UI as
  before.
- `vk_rt.inc` (included at the end of `ptgi.c`): ray-traced GI, shadows and
  haze; `vk_shadows.c`: shadow maps where nothing is traced.
- `pc/shaders/vulkan/`: GLSL 450/460 sources, translated from the GL
  shaders' C strings; `pc/tools/vk_shaders.py` builds the vendored glslang
  (`pc/third_party/glslang`, 16.6.0, BSD/MIT/Apache; see its `UPSTREAM.txt`)
  once into `pc/build/glslang` and compiles them to SPIR-V in
  `pc/build/boot64/vk_shaders.h` (only when a source changed).

Hooks into the GL path's files are small `#ifdef ROOD_VULKAN` blocks
(`vk_hooks.h`): `gpu_hw.c` keeps all of its logic (batching, native and object
ranges, effects, frame recording and interpolation, statistics, VRAM
staleness) and hands each GL call to `vk_hw.c` when `pc_vk_active`; its GL
object names become tokens (`VK_TOKEN_LIVE`, `REPLAY`, `ROOM_DEPTH`, `BEFORE`,
`OBJECT`, `OBJECT_REPLAY`, `VRAM`), so the code that passes framebuffers
around is unchanged. `render.c` and `ptgi.c` include the Vulkan drawing at
their end so the list and scene internals stay private. `boot_window.c`
picks the presenter, `dev_overlay.cpp` the ImGui renderer backend.
`boot_runtime.c` maps guest memory before the window starts (the Vulkan
driver and layers map memory that otherwise landed at `0x80000000`).

### Recording and synchronization

One command buffer is open between presents; everything records into it in
the order the GL path issues its calls. A dynamic-rendering scope stays open
across draws to the same attachments (most packet batches, fills and native
lists share the live target's scope); a transfer, an upload or other
attachments end it, and scopes and transfers are separated by full memory
barriers. All images live in `GENERAL` layout. Readbacks (`gpu_dump`,
picking, VRAM sync, single `present_dump`) submit and wait, as GL's
`glReadPixels` stalls. Per-draw state (the native lists' ~220 bytes of
uniforms, bone palettes) goes into the arena with push descriptors; packet
batches use push constants. Blend modes are pipeline variants; depth
test/write/compare are dynamic state.

Coordinate conventions match GL's exactly for the VRAM target (image row =
VRAM row in both, so `gl_FragCoord` and texel fetches agree); the window
passes use top-down rows, and FXAA and the blur mirror their sample
patterns in y so the result equals GL's.

### Anti-aliasing

`pc/README.md` "Anti-aliasing" describes the methods. On Vulkan: MSAA's
twins are `vk_hw.ms` and `vk_hw.ms_room` (tokens `VK_TOKEN_MS`,
`VK_TOKEN_MS_OBJECT`, `VK_TOKEN_MS_ROOM_DEPTH`; `vk_hw_token_samples`), with
packet pipelines per sample count (2D ones with every sample at the pixel
centre through `VkPipelineSampleLocationsStateCreateInfoEXT`; depth twins
are created `SAMPLE_LOCATIONS_COMPATIBLE_DEPTH`) and a multisampled set of
the native list pipelines (`vk_native_set_for`). Loading the twins is a
fullscreen pass (`ms_load.frag`, `ms_depth.frag`), resolving another
(`ms_resolve.frag`); the native surfaces' depth twin is copied into the
single-sampled one by a rendering scope with a `SAMPLE_ZERO` depth resolve
and nothing drawn. Usable counts: `vk.msaa_counts` (framebuffer and sampled
colour/depth counts). The present chain (`vk_present.c`) is lit image ->
FXAA or SMAA (`smaa_edges/weights/blend.frag`, the reference
`SMAA.hlsl` with `SMAA_GLSL_4`; `vk_shaders.py` adds `pc/third_party/smaa`
to the include path) -> blur -> downsample (`downsample.frag`) -> window,
each pass into the next of two lit-sized images or, last, the window's
view. `ROOD_PERF` sections: `draw` (everything before the GI: the VRAM
target, native lists, MSAA load/resolve), `aa`, `blur`, `downsample`.

### Ray tracing (`vk_rt.inc`)

The scene is `ptgi.c`'s (the captured room, the frame's characters, weapons
and props, emitters, glow overrides and lights, gathered by the same code),
with acceleration structures instead of the CPU BVH:

- **Room**: its captured triangles (one static mesh per room geometry
  block) in a device buffer, split once per room (and re-checked a second
  later, as textures may still be arriving) into fully opaque triangles
  (untextured, or no clear or translucent texel under their UVs, tested
  against the CPU's VRAM) and texel cut-outs (fences, grates, glass). Two
  BLASes; the opaque one is a `FORCE_OPAQUE` instance, so rays never run
  shader code for it (first room: 1830 opaque, 54 cut-out triangles).
  Decals floating a few units off a surface are still skipped: a committed
  opaque hit within the skip distance restarts the query past it.
  Room texture effects (the save point's scrolling sigil; animated
  materials, `Native_Rendering.md`) rewrite their triangles' UV words in
  the buffer in command order (`vrt_room_uvs`, `vkCmdUpdateBuffer` between
  barriers) when the scene's UVs change, so hits read the current texels.
- **Movers**: the object lists of the frame being presented
  (`pc_hw_present_lists`), each instance at the palette blend the image
  was drawn with (replays interpolate). `rt_skin.comp` skins each instance
  from its bone palette on the GPU into world-space shading triangles (two
  per polygon; non-casters inactive, the same ones every frame) in a
  two-slot mover buffer, and writes each triangle's position in the
  previous traced frame. Each instance (mesh, occurrence) keeps its own
  BLAS: built once, refit while the topology stays, rebuilt every 32
  updates.
- **TLAS** per update: room opaque (mask 1), room cut-outs (mask 1), one
  instance per mover (mask 2, custom index `MOVER_BIT` | first triangle).
  Shading triangles are 64 bytes whose first three vec4s are the
  positions, so BLASes read them in place (stride 16, indices 4t..4t+2).

Passes (compute):

1. **Surface** (`rt_surface.comp`): world position and normal per GI pixel
   from the G-buffer, and its *previous* position: static surfaces stay;
   on moving G-buffer pixels a short ray from the eye (mask 2) finds the
   mover triangle under the pixel and takes the same barycentric point on
   its previous pose (previous-pose reprojection).
   The camera is the one the *presented picture* was drawn with
   (`pc_scene_camera_get` -> `gpu_hw.c` `pc_hw_view_camera`: the room list
   camera of the recording that drew the display area), and the movers are
   that recording's object lists. The game builds a frame's ordering table
   with that frame's camera, draws it into the off-screen buffer and shows
   it a frame later, so the GTE camera captured for the scene runs up to
   two game frames ahead of the picture; reconstructing through it slid
   every surface (and so every shadow) along and across the view rays
   whenever the camera turned or followed Ashley (tens of units, 40 % of
   the pixels mid-turn), settling only when the camera stopped.
2. **Trace** (`rt_trace.comp`): shadow visibility of the shadow-casting
   lights (spherical or rect area lights; any number of them: *Many
   lights* below), indirect light, light from the
   glows (small spherical emitters, one picked per pixel in proportion to
   its unshadowed light; shadowed when "Glows cast shadows" is on),
   baked-light visibility (contact and glow shadows of characters) and the
   lighting signature. Bit 31 of the signature marks a pixel whose
   visibility a *moving* blocker touched.
3. **Haze** (`rt_haze.comp`, `rt_haze_integrate.comp`): a froxel volume
   (`haze_cell` x `haze_cell` display pixels per cell, 64 logarithmic depth
   slices, the slices side by side in 2D images): one blue-noise jittered
   sample per froxel with one light or glow picked by its unshadowed
   in-scattering and one shadow ray (light shafts, volumetric shadows of
   characters), one ray to the sun's disc and one to the sky (below, "Haze,
   ground fog and sun shafts"), blended with the reprojected previous
   volume (`pc_haze_history`, 0.1), then integrated front to back through
   the room's medium (Beer-Lambert; distances in world units through the
   scaled GTE view matrix). It depends only on the camera, so nothing drawn
   can cut holes into it.
4. **Temporal** (`rt_temporal.comp`): reprojects the previous position;
   nearest strict history, kept only where the signature matches;
   visibility and glow light touched by a moving blocker (now or in the
   history) keep a short history (`pc_shadow_motion_history`, 6 frames);
   second moments of indirect luminance, visibility and glow luminance
   give each signal's variance (short histories count as uncertain).
5. **Filter** (`rt_atrous.comp`): four a-trous iterations (3x3 at steps 1,
   2, 4, 8), SVGF style: geometry stops on normal and plane distance (the
   tolerance grows with the pixel footprint), and each signal (indirect,
   visibility, baked visibility, glow light) stops on its own difference
   over its standard deviation (variance carried through the iterations):
   noise is blurred, converged shadow edges are kept.

The present pass (`scaled.frag`) upsamples GI from its 2x2 GI pixels
weighted by surface match (best of the 3x3 when none matches), adds
indirect + glow light, and applies the haze to every 3D pixel at its depth,
unlit ones (effects, particles, glows) included.

Probe GI (`gi_method probes`, Medium/High): [Probe_GI.md](Probe_GI.md);
its field replaces the traced indirect light (`probe_apply.comp`, or
`probe_surface.comp` when nothing is traced).

Denoiser (`gi_denoiser`): **NRD** (default where built with it) runs NVIDIA
NRD's RELAX diffuse on the indirect light and keeps the filter above for the
other signals; **filter** is the filter above for everything; **raw** shows
single samples (no history). See "NRD" below.

Without ray tracing (`ROOD_VK_RT=0`, or a device without ray queries), or
with traced shadows off, lights use shadow maps (`vk_shadows.c`):
`shadows.c`'s cube maps of the room and casters as an atlas of four slots
of six 512x512 faces in GL cube orientations, looked up as GL's cube sampler
does (8 taps on a small cone). They match the GL path's cube maps (mean
difference 0.12, first room).

Editor: the inspector's GI probe reads the last result back on Vulkan (RPC
`gi_probe {u, v}` too), the CPU trace behind its source list traces the
room and casters by brute force. The legacy separate renderer (OpenGL only)
is disabled on Vulkan; the free camera drives the live view, which has GI,
haze, picking and gizmos. Seeing into rooms from it (cutaway walls for the
free camera's yaw, black shells left out, the cutaway to the focus in
`native.geom`/`native.frag`, per-group visibility): Native_Rendering.md,
"Seeing into rooms"; the traced scene keeps the whole room.

Light actors (2026-10-11; README "Lighting the room's fixtures"): lights
come from `pc_lights_world` once per frame with their animation (flicker,
pulse) already evaluated from a per-present clock, so the trace and present
passes agree. Two per-light values ride in unused lanes: `light_right.w` is a
point/spot light's own emitting radius (`point_size(i)`, else
`light_size`), `light_up.w` how far short of the light shadow rays stop (a
lit fixture's polygons cast no shadow from it; ptgi.c the same). Animated
lights and history: direct light is computed per frame in the present pass
from the frame's lights (no smear); the accumulated signals (shadow
visibility weighted by light luminance, indirect bounce, glow light of a
driven glow, the haze volume) average the flicker over their history (6-48
frames), which reads as a steady bounce/haze under a flickering direct light;
the lighting signature is unaffected (it tests visibility, not intensity).

Settings: the GI panel's controls drive the same variables; on Vulkan
"Moving-shadow history" and "Haze history blend" replace "Haze march
steps". Lighting quality presets (README "Lighting quality presets",
`lighting_quality.cpp`) choose what runs: `gi_method` path sets
`pc_gi_enabled` (the trace's `indirect`), `shadows` traced sets
`pc_rt_shadows`, haze High enables the froxel volume
(`pc_lighting_traced_haze_density`), and with `ao` on a ray-query device the
trace adds two short AO rays per GI pixel into the baked-light visibility
(`rt_trace.comp` `traced_ao`, UBO `f2`), so the passes also run for AO alone
(Low and Medium on a ray-tracing GPU). Without ray queries the present pass
does screen-space AO, PCF/PCSS shadow-map filtering and the unshadowed Low
haze (`scaled.frag`). Integrated GPUs start at GI resolution 1.5 (3 on discrete GPUs).
`ROOD_PERF=1` also prints GPU time per section (`vk_section`): gi scene
(skinning, BLAS refits, TLAS), surface, trace, haze, temporal, filter,
present.

### NRD (`vk_nrd.cpp`, `rt_nrd_in.comp`, `rt_nrd_out.comp`)

The "Ultra" denoiser, following NVIDIA's RELAX sample usage: NVIDIA NRD
4.18 RELAX diffuse over the path-traced indirect light, on the same Vulkan
device and command buffer as the rest of the frame (no readback):

- **Inputs** (`rt_nrd_in.comp`, at GI resolution, after our temporal and
  a-trous passes): the raw indirect irradiance of this update (albedo stays
  out: `scaled.frag` multiplies the nearest-sampled textures back, as for
  the filter) with the mean first-bounce hit distance (`rt_trace.comp`
  writes it, binding 10); world normals packed as
  `NRD_FrontEnd_PackNormalAndRoughness` (R10G10B10A2 encoding, roughness 1);
  linear view depth; world-space motion (previous - current: 0 on the room,
  characters and props through their previous pose, `rt_surface.comp`). No
  surface: view depth beyond the denoising range (NRD skips it).
- **Camera**: the GTE camera's rows carry different scales (X stretched for
  the pixel aspect) and NRD inverts view matrices as orthonormal, so NRD gets
  the normalized rows (Gram-Schmidt from the forward row) with the scales
  folded into its projection (`clip.w` = view depth; NRD's uv convention
  `clip.xy / w * (0.5, -0.5) + 0.5`). Checked: GI positions project back to
  their own pixels within 0.15-0.3 px.
- **Output** (`rt_nrd_out.comp`): RELAX's radiance replaces the rgb of the
  filter's result; the shadow visibility (alpha) and the extra term (baked
  visibility, glow light) stay the filter's. With `nrd_glow` the glows'
  light goes through NRD too (and the extra term's glow becomes 0).
- **History**: reset (`CLEAR_AND_RESTART`) on a resize, a new room capture,
  a change of the lights (which lights, their type, reach and shadows, a
  position moved by 32 units or more: not their flicker or sway, which
  change every frame and until 2026-10-12 reset NRD on every update in any
  room with a candle) or of the
  amounts that change the signal (baked bounce, emitter light, bounces), a
  camera cut (eye moved > 256 units or turned > ~37 degrees between
  updates), and after any update without NRD (denoiser switched). NRD's own
  timer reads the wall clock; it gets the monotonic time between updates
  (1-100 ms) instead.
- **Settings**: RELAX defaults (30 frames, fast history 6, anti-firefly on);
  `nrd_frames` sets the history (overlay "NRD history"). RPC `nrd_tune
  "key=value,..."` (or `ROOD_NRD_TUNE`) overrides RELAX settings for
  experiments (fast, phi, clamp, prepass, iterations, minlum, antilag,
  hitweight, fix, firefly).
- **Vulkan objects**: NRD's pipelines from its embedded SPIR-V; its
  descriptor sets (resources; constant buffer + immutable samplers)
  allocated per dispatch from a pool per frame slot; the constants in the
  frame's upload arena. Not push descriptors for the constants/samplers set:
  with them, history stopped accumulating (length stuck at 1, no validation
  message) whenever NRD's images were re-created after it had run once (a
  GI resolution change); the first instance was fine. The device enables
  `shaderStorageImageRead/WriteWithoutFormat` and extended storage formats
  when present (DXC declares NRD's storage images without formats).

Toggle: overlay Graphics > GI "Denoiser" (Off / Temporal + a-trous filter /
NVIDIA NRD RELAX; with NRD: "Glow light through NRD", "NRD history"), RPC
`graphics {gi_denoiser: "nrd" | "filter" | "raw"}` (the reply reports
`gi_denoiser`, `gi_denoiser_active`, `nrd_built`, `nrd_available`; the old
`gi_denoise` bool maps to nrd/raw), project settings key `gi_denoiser`
(0 raw, 1 filter, 2 NRD; an old `denoise 0` still means raw). "NRD" is the
default (`pc_gi_denoise` 2); where NRD cannot run (OpenGL, 32 bits, built
without it, no ray queries, or NRD failed to initialize) it means the
filter, so those paths are unchanged.

Other signals: SIGMA (NRD's shadow denoiser) wants one light's penumbra per
input; our visibility is a weighted mix of every shadow-casting light (and
the baked visibility a mix of contact occlusion and glow shadows), so they
keep the filter. The glows' light through RELAX was less steady when still
(indirect-only view 0.17 vs 0.08 frame-to-frame) and further from the
reference while walking, so it is off by default. Light sampling: direct
light sums every dynamic light (no selection noise) and one glow is picked
per pixel in proportion to its unshadowed contribution (power x falloff x
cosine), which already acts as a power-weighted emitter CDF.

**Building it.** NRD is under the NVIDIA RTX SDKs license (redistribution
under terms "at least as protective" as NVIDIA's), so it is never vendored
or committed: `pc/tools/nrd/CMakeLists.txt` (ours) downloads it with CMake
`FetchContent` at commit `7033ffbc48dbd74713194555abc13da8d7de4bcb` (the
archive's SHA-256 is checked) into `pc/build/nrd` and
builds `libNRD.a` + `libShaderMakeBlob.a` (static, oct-packed normals,
linear roughness, no quad intrinsics, SPIR-V only). NRD's CMake fetches
ShaderMake (pinned commit), MathLib (tag v11) and DXC (v1.9.2607), which
compiles NRD's HLSL shaders to SPIR-V embedded in the library: our vendored
glslang (`vk_shaders.py`) only compiles our GLSL, including the two NRD
adapter passes, and never sees NRD's shaders. `build_boot.py --arch 64`
calls `pc/tools/nrd.py`: the first build needs network access (NRD ~5 MB,
DXC a ~500 MB download unpacking to ~1.7 GB) and a few minutes; DXC is
deleted after a successful build (`pc/build/nrd` keeps ~40 MB), and later
builds reuse the libraries without running CMake (a stamp of the pin and
options). `--nrd auto` (default; `$ROOD_NRD`): build without NRD when the
fetch or build fails (warned once, remembered until `--nrd on`); `--nrd
on`: fail instead; `--nrd off`: without NRD (`vk_nrd.cpp` compiles to a
stub, the denoiser setting falls back to the filter). Log:
`pc/build/nrd/rood-nrd-build.log`.

Test: `pc/tests/nrd_smoke.py` (run.py suite `nrd_smoke`; Vulkan 64-bit with
NRD, else skipped): New Game, the first room after its opening pan with a
light in view; NRD is the default and runs, history accumulates, energy is
kept (displayed vs raw indirect within 15 %), consecutive updates differ
~1 % as much as the raw samples, switching to the filter and back restarts
the history, a GI resolution change re-creates NRD and it recovers, no
validation messages. RPC `gi_dump {path, light: true}` dumps the raw and
displayed indirect light and NRD's history length per GI pixel.

### Many lights (`light_list.h`, `rt_lights.glsl`, `scaled.frag`)

A room's fixtures can be dozens of lights (the octagonal candle hall, zone
26 room 0, has 67 candles; a town square a dozen lanterns). The renderers
used to take 16, the nearest the camera, which left whole walls of a hall
unlit and popped lights on and off as the camera moved. On Vulkan with ray
queries every light now counts, at a cost that stays nearly flat:

- **One list**: `pc_lights_list` (lights.cpp) packs every enabled light of
  the frame (plain lights, every light of every actor, the editor's ghosts)
  into `pc_light_gpu` records (position and reach, colour times intensity
  and type, direction and cone, rect extents and the light's own size, the
  fixture's shadow skip, shadow flag), up to `PC_LIGHTS_MAX` (1024). Its
  order is stable (the room file's), so per-pixel bits keyed on it (the
  lighting signature) stay put from frame to frame. The present pass and
  the trace each get it as a storage buffer from the frame arena (binding
  16 of the present, 11 of the trace, 10 of the haze).
- **Textured windows** (`window_pattern.glsl`, `window_pattern.h`): generated
  rects carry texture-key/UV triangle references, not extracted mask grids.
  CPU packing conservatively integrates the live emissive map into a 256-cell luminance CDF,
  preserves its mean radiance in the light colour, and supplies a directional
  aperture's incident vector/angular spread. `pc_light_gpu` is **1152 bytes**:
  the original 112-byte prefix, 64 `vec4` CDF values and one `vec4` direction.
  Conservative source-texel integrals are cached by mapping/grid, catalog/map
  generation and live texture content; changing a painted channel invalidates
  the cache, and palette-qualified identities never choose an unrelated use.
  Keep the present, trace and particle shader strides identical. The present
  now uses the same packing function as tracing. Pattern/mode/mean/direction
  changes invalidate NRD history. A receiver's finite angular footprint
  is integrated over the mask and shadow samples are conditioned on its bright
  area, giving projected structure with distance-dependent softness; diffuse
  mode only changes source distribution. See [Emissive.md](Emissive.md#patterned-window-apertures-vulkan)
  for controls, intensity semantics, the GL fallback and visual/perf harness.
- **Direct light, unshadowed** (`scaled.frag`): every light's falloff,
  cosine and cone per pixel (ALU only; most lights are out of reach of a
  given pixel and cost a compare).
- **Light shapes** (`light_shape.glsl`; OpenGL's copy in `light_shape.h`):
  a spot fades from its inner cone to its cone (inner 0: the outer quarter
  of the cone's cosine range, the old edge); a rect lights only in front of
  it, times its barn doors: per axis, the share of its width a point sees
  past the two doors (length and angle from its facing). They travel in
  slots the list had free: a spot's `right.x` (cos of the inner half angle),
  a rect's `dir.w` and `right.w` (length x cos and sin of the doors' angle).
  Every evaluation (present, trace, GI bounces, haze, probes) multiplies the
  falloff by it, so traced and mapped shadows need nothing new. The fog
  volumes' own renderer (`fog_volume.glsl`, three vectors per light) keeps
  the old spot edge and has no barn doors.
- **Shadows** (`visibility()` in `rt_trace.comp`): the present multiplies
  the shadow casters' unshadowed sum by one traced visibility, the ratio
  `sum(w_i V_i) / sum(w_i)` with `w_i` each caster's unshadowed luminance at
  the pixel. When at most `EXACT_LIGHTS` (8) casters reach the pixel each
  is traced `shadow_samples` times, as before (zone 9's rooms trace the
  same rays). Past that, `8 x shadow_samples` shadow rays go to
  casters picked in proportion to `w_i` (inversion of the running sum): an
  unbiased estimate of the same ratio whose noise the temporal accumulation
  and filter average out. Rays per pixel are bounded whatever the count.
- **GI bounces** (`direct()`): up to 8 lights each traced; past that one
  light per bounce hit, picked in proportion to its unshadowed light
  (weighted reservoir sampling in one pass, weight `all / w_pick`).
- **Haze** (`rt_haze.comp`): every light and glow is a candidate for the
  froxel's single shadow ray, picked the same way in one pass (it used to
  take the first 48 sources).
- **Signature**: one bit per caster that reaches the pixel, the first 12 in
  list order (with 12 or fewer lights in the room, one bit per caster as
  before).
- **Flicker**: the visibility is a ratio of the same lights' weights, so a
  candle's flicker changes the unshadowed sum (exact, every frame) but
  hardly the ratio; nothing is reused across frames that would smear it.
  No reservoir reuse between pixels or frames (ReSTIR) is needed at these
  counts; it is the next step if rooms ever hold hundreds of shadowed
  lights.

Measured (RTX, 1600x1200 window, scale 3, GI at 960x672, ROOD_PERF; RPC
`actors {budget: 16}` gives the old cap for comparison):

| Room | Lights | GPU ms/frame, 16 kept | all lights |
| --- | --- | --- | --- |
| zone 9 room 5 (candles, a fire) | 8 | 0.79 | 0.79 |
| zone 26 room 0 (candle hall) | 67 | 1.32 | 1.55 |

The difference in the hall is the present's unshadowed loop over 67 lights
(+0.16 ms) and the trace's per-pixel weights (+0.09 ms); the shadow rays are
the same. Temporal noise (mean absolute difference of two frames 4 vsyncs
apart with the light animation frozen, 0-255): hall 0.21 with 16 lights,
0.12 with all 67 (fewer lights switching in and out of view); zone 9 room 5
0.29 before and after.

Paths without ray queries keep a budget (lights.cpp): Vulkan's present takes
every light unshadowed and renders shadow maps for the 4 most important
casters (`pc_lights_shadow_pick`); OpenGL's present takes 64 (16 with its
path-traced visibility, whose trace has 16 slots), the editor's GL view and
the probes 16. Past a budget the most important are kept: brightness x
(reach / distance from the camera)^2 (1 inside its reach), x 0.3 for a light
whose sphere is outside the view cone, x 1.5 for lights kept last frame
(hysteresis: no popping as the camera moves). A clustered (tiled) GL
present taking every light is not done.

### Sun and sky (`environment.cpp`, `environment.glsl`)

Outdoor rooms have a directional **sun** (the moon, mostly) and a **sky**
(per room: `env sun` / `env sky` items of the lighting layers,
lighting/README.md "Environment"; generated for rooms open to the sky by
`pc/tools/outdoor.py` + `generate_lighting.py`; README "Sun and sky").
The renderers read one packed record per frame, `pc_env_gpu_get`
(environment.h): the direction toward the sun (world, y down) and its
angular radius, its colour x intensity and whether it casts shadows, the
sky's zenith / horizon / ground radiance x intensity, its sun tint and the
ambient fallback's strength. It is appended to the trace UBO
(`rt_lights.glsl` `Trace`: the path tracer, the haze, the probes), the
present UBO and, on OpenGL, uniforms of ptgi.c's trace and the present;
`pc/shaders/vulkan/environment.glsl` is the shared GLSL (OpenGL through
`gl_shaders.py` `PC_GLSL_ENVIRONMENT`). A room without them: the record is
zeros and nothing changes (the passes, branches and history hashes are as
before).

- **Not one of the lights.** The sun stays out of the light list and its
  many-light sampling (`light_list.h`, `direct()`, `visibility()`), so it
  always gets its own rays and never competes with a candle for one.
  `rt_trace.comp` (and ptgi.c's GL trace, the same functions):
  - *Direct light*: the present adds `sun colour x max(n.L, 0) x
    visibility`. `visibility_with_sun()` folds the sun into the traced
    visibility as one more term of the same ratio `sum(w V) / sum(w)` (its
    weight: its unshadowed luminance at the pixel), with `shadow_samples`
    rays to points of its disc (uniform over the cone of its angular
    radius: soft shadows that widen with distance from the blocker); so the
    present's single traced visibility covers the lights and the sun, and
    where only the sun reaches (a street with no lantern) it is exactly the
    sun's. The signature gets bit 19 (the sun's centre ray), so a moving
    sun shadow (Ashley's) rejects history like a light's.
  - *Bounces*: every path vertex gets `sun_light()` (one ray to its disc)
    besides `direct()`; every probe ray's hit too (`probe_trace.glsl`).
  - *Sky*: a GI ray that leaves the room returns `env_sky(d)` (zenith ->
    horizon -> ground, plus `sun_tint` x the sun's colour around it; never
    the sun's disc, which is sampled directly) times its throughput, so a
    floor under an arch or a canopy sees less sky: sky occlusion is the
    trace. NRD's hit distance counts such a miss as 4096 units. The probes'
    misses store the sky's radiance (before: black), and `probe_state.comp`
    gives the sun and sky a steady share of the probe's flicker scale (they
    never flicker). The NRD and probe history hashes include the record, so
    an edit restarts them once.
- **Without traced shadows** (Low/Medium; Vulkan without ray queries; GL
  without compute): the sun's **shadow map**, one 1024 x 1024 orthographic
  map along the sun covering the room's bounding sphere, with the room and
  this frame's casters (`shadows.c` `pc_shadow_sun_matrix` +
  `pc_shadows_sun_update`; on Vulkan a tile below the cube rows of
  `vk_shadows.c`'s atlas, drawn with the cube pipeline). The cube maps'
  shader is reused: the "light" is a far point along the sun, and distance
  to it grows monotonically along each texel's ray, so it compares as a
  depth. Lookup: 8 rotated taps (`env_sun_map_visibility`), radius from the
  sun's angular size, slope-scaled bias. Redrawn only when the room, the
  casters or the sun change. Shadows Off: unshadowed. On OpenGL it needs a
  17th texture unit (unit 16; every GL 4 driver has 32), else unshadowed.
- **Sky without GI** (no path tracing and no probes): `env_ambient(n)`, a
  hemisphere term (sky mean for upward faces, ground for downward, x
  `ambient`, 0.5-0.6) times the screen-space AO where it is on. Subtle by
  design: there is no occlusion beyond the AO.
- Energy: a GI pixel's indirect light is the cosine-weighted mean radiance
  arriving, so an open floor under a uniform sky of radiance L gets L x its
  albedo; `pc/tests/sky_smoke.py` checks no surface exceeds the brightest
  sky, that NRD's displayed light keeps the raw samples' mean (+9 % in the
  forest) and that the probes bring the sky too (1.4-1.5 x the path
  tracer's there: their usual over-estimate plus sky leaking between
  probes in open air).
- **Haze**: the sun and the sky light it with their own rays (next section).

Measured (RTX PRO 6000, `ROOD_PERF=1`, headless, scale 3, 300 frames after
120 to settle; the generated moon and sky vs the settings key `environment`
off in the same room):

| Room, preset | GPU ms/frame off -> on | where |
| --- | --- | --- |
| Snowfly Forest (40/0), Ultra | 0.754 -> 0.816 (+0.06) | trace 0.064 -> 0.119 (sun rays per pixel and per bounce, sky misses) |
| Rue Lejour (32/15), Ultra | 0.724 -> 0.757 (+0.03) | trace 0.050 -> 0.075 |
| Snowfly Forest, Medium | 0.351 -> 0.411 (+0.06) | the probes now run (no lights there before: +0.04), the sun's map +0.01 |
| Rue Lejour, Medium | 0.393 -> 0.409 (+0.02) | probe trace +0.002, the sun's map +0.01 (redrawn as Ashley moves) |

NRD, the filters and the haze are unchanged (same images, same counts).

### Haze, ground fog and sun shafts (`atmosphere.cpp`, `medium.glsl`)

The air of a room is one participating medium, packed per frame by
`pc_medium_gpu_get` (`atmosphere.h`: 165 vec4s, plus one small 3D texture, `pc_medium_texture`: the detail noise and the interactive fog's fields, binding 11 of `rt_haze` and 17 of `scaled.frag`; `medium.glsl` reads them
through the includer's `MEDIUM(i)`; appended to the haze's and the
integrate pass's UBOs and the present UBO, uniforms `medium_` on OpenGL):

- the **haze**: uniform inside the room's bounds (room setting
  `haze_density`; the lighting quality picks the traced or the Low density),
  `haze_outside` of it (default 1) above the floor grid's cells without a
  floor, and fading out within `haze_under_floor` units under the floor
  (below, "The room's interior and the void"; with `haze_interior` 0 the
  whole bounds, as before);
- the **ground fog** (`env fog`): `density x exp(-(base y - y) / height)`
  above its base (y down), its base `offset` units above **the floor under
  the point** (`scene_capture.c` `pc_scene_floor_grid`: a 24 x 24 top-down
  grid over the room's bounds, per cell the lowest up-facing level face,
  each cell the lowest of its 3 x 3; cells without a floor (no up-facing
  face up to `haze_interior_slope` degrees that is not a black shell)
  stay empty: no fog over them (the void outside the walls); holes up to
  twice `haze_interior_fill` cells across count as floor (a closing,
  `floor_grid.c`); every cell still has a height (the lowest of its 3 x 3,
  then grown outward); packed two 12-bit steps per float, the height (with
  `haze_interior`: 11 bits of height and a no-floor bit), bilinear in
  `medium_floor`; below, "The room's interior and the void"), and under
  its base fading within one more scale height
  (no column holds an unbounded depth of it), times drifting **wisps**:
  three octaves of gradient (Perlin) noise with a quintic fade and an
  integer hash, the coordinates domain-warped by one more slow octave
  (curling wisps, not round blobs), squashed vertically into sheets, mean
  1, `noise` its amount; the field drifts with the wind in world units per
  *game* second (`atmosphere.cpp` `medium_time`: vsyncs / 59.94, so it
  stops with the game, follows fast-forward and never depends on the
  presented frame rate; default wind 4,0,1.5, the moods 1-3 units/s), the
  finer octaves a little faster, the warp evolving slowly (2026-10-13
  playtest: the old two octaves of value noise read as blotches with a
  visible lattice, and drifted at 6-12 units/s of wall-clock time);
- up to 16 **fog volumes** (`fog <id>`): boxes (turned about y) or
  ellipsoids, a smoothstep edge over `edge` of their half size, their own
  wisps; the first 8 are drawn by their own renderer (record 4.w: the
  first volume the haze passes include; below, "Fog volumes' own
  renderer"), the rest by the haze;
- per point: extinction, a scattering colour (density x albedo) and an
  emission (each part's `fill` light, and where nothing traces the sky its
  mean radiance x `sky` x 0.5 for the cover it does not see);
- **limits** (record vec4 88; room settings, `atmosphere.h`), applied by
  every integrator (froxel integrate, both `low_haze`, ptgi.c's march, the
  GL present's transmittance): the ray's transmittance never falls below
  1 - `haze_max_opacity` (`medium_step` ends the step where it reaches it,
  so the light stays consistent with it), the light scattered toward the
  eye passes a soft knee at `haze_max_light` (`medium_limit_light`, luminance),
  the phase function peaks at `haze_max_forward` (`medium_phase`), and the
  sun's in-scattering in shadowed air keeps 1 - `haze_shafts` of it
  (`medium_sun_visibility`: the shafts' contrast).

Why (2026-10-13 playtest: Rue Vermillion, 32/0, whole views uniform
blue-grey or near-white at some camera angles): the room's main floor was
its roofs (y -1400; the street is near 0, bounds -2528..0), and the fog had
its full density everywhere under its base, so a view along the street
crossed ~1000 units of it (optical depth ~2: 13 % of the scene left), lit by
the moon through forward scattering (g 0.55: phase 7.7). The floor grid puts
the fog on the street; the limits make any future mistake survivable.
`pc/tools/atmosphere_sweep.py` (every room with a haze or a ground fog; the
game camera, the free camera orbiting Ashley at 8 yaws, alternately looking
down and level, and toward the moon; detail = Weber contrast of the
gradients with / without the atmosphere >= 0.5, veil = share of pixels
where the atmosphere's light is over 40/255 and brighter than the scene <=
0.3) fails the old medium (`--legacy`: limits off, `floor_grid` 0) in
Rue Vermillion at 6 of 10 poses (detail 0.08-0.35, veil up to 0.99) and
passes the new one; the `atmosphere_sweep` suite runs four rooms.

#### The room's interior and the void (2026-10-16)

The report: the Undercity (zone 48 room 0, Ashley at about (972, 0,
1221)). The game cuts away the camera-side walls (as on the PS1), and the
void around the room showed glowing mist: the ground fog followed a floor
grid whose empty cells grew from their neighbours (so it lay over the
cells outside the walls), the haze filled the room's bounds box, and the
black shells the void is made of (Native_Rendering.md "Seeing into
rooms": each wall's thickness, top and outside, every vertex colour 0)
took the haze like any wall. Three layers, each a room setting (data, the
Room settings panel's Atmosphere page, RPC `environment {haze: {interior,
interior_fill, interior_slope, interior_edge, outside, under_floor, void,
void_level}}`, booleans or numbers, and the reply's `haze` object), on by
default; with `haze_interior` and `haze_void` 0 the picture is exactly the
old one (the old grid, the old 12-bit heights, no void test):

1. **The floor grid knows where the room is** (`scene_capture.c`,
   `floor_grid.c`; `pc_scene_floor_cells`): a cell has a floor where an
   up-facing face covers it no steeper than `haze_interior_slope` (37..89
   degrees, default 60; heights still come from the level faces, |n.y| >=
   0.8 |n|). With `haze_interior` on, black shells (room groups whose every
   captured triangle has vertex colour 0 and no raw texture: `room_native.c`
   `group_is_black`'s test, per group, so a black face in a lit group still
   counts) count for neither, so the fog no longer lies on black wall tops;
   off, they count as before. A closing with a square of radius
   `haze_interior_fill` cells (0..4, default 1) fills holes up to twice
   that across (past the grid's edge counts as floor while eroding only
   next to an edge cell with a floor, so a floor reaching the bounds keeps
   its edge and an empty strip along the edge stays empty; a concave corner
   is never filled). The RPC reply's `fog.floor_grid.occupied` lists the
   cells (0 empty, 1 floor, 2 a filled hole).
2. **The medium keeps to the interior** (`haze_interior`, default 1;
   `medium.glsl` `medium_room_air`, one look-up of the grid per sample):
   the cells pack their heights in 11 bits plus a no-floor bit (the cells
   without a floor keep the heights grown from their neighbours'). The
   ground fog is multiplied by the cells' bilinear floor share through a
   smoothstep around a half (`haze_interior_edge`, 0 hard .. 0.5 a cell
   each side, default 0.2; the edge is at most one cell, bounds / 24, off
   the true wall). The uniform haze keeps `haze_outside` (0..1, default 1)
   of itself above the cells without a floor: the game camera usually sits
   outside the room's walls (that is why they are cut away), and that air
   between it and the room is part of every room's tuned mood; 0 confines
   the haze to the interior too. Under the floor the haze fades out within
   `haze_under_floor` units (0..256, default 32): under the column's own
   floor over the interior, under the room's lowest floor (record 165.w)
   over the cells without one, so the void below the room is clear but the
   air down to the floor is not. The grid is packed for the ground fog,
   for the haze with the interior on, and for `haze_debug 2`; a haze
   without the interior needs no grid look-ups (`medium_maybe` returns at
   once). `fit_to_floor` and `atmosphere_sweep.py`'s `floor_at` read the
   heights, unchanged.
3. **Void pixels keep the room's air only up to where their ray leaves the
   interior** (`haze_void`, default 1; `medium_void_pixel`,
   `medium_void_reach`, `scaled.frag`, `gl_present.c`): a present pixel is
   void when nothing is drawn there (depth 1), or its G-buffer shade is a
   lit 3D surface (shade alpha 0.5 flat or 1 textured; never 0.25 emissive
   or 0 unlit) whose vertex colour is at most `haze_void_level` (0..32 of
   255, default 0: exactly black): what the PS1 draws black whatever its
   texture. Such a pixel's ray is sampled at 16 points from its surface
   back toward the eye; the last one in the room's interior air (inside
   the bounds, over the cells with a floor, at most `haze_under_floor`
   under it) is its reach, and the haze (the traced froxels at that depth,
   or haze Low's march to that point; OpenGL's traced haze uses haze Low's
   march there) stops there. The void past a cut-away wall, whose ray
   never crosses the room, keeps none of it: clean darkness. A black
   doorway, pit or prop seen across a fogged floor keeps the fog in front
   of it, so it is never cut out of the fog with a hard edge. Without the
   interior the reach is 0 (void pixels keep none of the room's air). Depth
   1 pixels never had the haze in the present (only its debug view). This
   works on both renderers and both room paths (`native_room` 0 and 2)
   without group ids in the G-buffer. The Vulkan present fills its medium
   record every frame, so every haze path reads the switch.

What changed in the look, measured (the 3D view's mean luma, of 255,
against the switches off): at the report's pose, the user's settings
(haze Low, the room's own fog and haze), Vulkan, 20.1 -> 22.5: almost all
of it the ground fog the old grid laid over the cells outside the walls,
where it dimmed the floor seen through it and glowed over the void; the
haze alone (no ground fog) 30.5 -> 30.8 (`haze_outside` 1; an earlier
version that confined the haze to the interior made rooms seen from
outside a third brighter). `atmosphere_sweep.py` records the change per
pose (`interior`) and fails a game-camera pose that moves by more than 6:
every room with atmosphere on Vulkan (97 rooms, 347 poses, two free-camera
yaws), median |change| 0.1, 40 poses over 1, 12 over 2, 2 over 4; the
largest with the game's camera 48/14 +5.1 (brighter: less of the old fog
over the cells outside its walls), and with a free camera 40/10 +12 (the
camera sits outside the walls, over cells without a floor, and looked
through the old fog grown over them). Outdoors the haze under a terrain's
edge (below the room's lowest floor) goes too, and with it some shadowed
air: in the forest (40/6) the moon's shafts measured over the whole view
lose a little contrast (`atmosphere_smoke` measures them with the interior
off, 10th percentile of shadowed / unshadowed 0.74; with it 0.81).
GPU time at 48/0 (the RPC `perf` frame time, Vulkan, haze Low / High, with
and without the ground fog): within 0.012 ms of the switches off; OpenGL
within 0.02 ms (run to run noise).

**Fog volumes keep to the room too** ("Confine to room", per volume, on
by default; 2026-10-16, the user's real complaint: a volume fitted to the
Undercity's floor showed its mist over the void past the cut-away walls).
Until then they were exempt (an artist placed them). Now a confined volume's
density (`medium.glsl` `medium_confine`: in `medium_volume` and
`medium_volume_coarse` for the haze passes, in `fog_volume.glsl`
`fv_density` for its own renderer and its light grid) is multiplied by the
room's interior exactly as the ground fog's is: inside the room's bounds,
the cells' bilinear floor share through the same soft edge
(`haze_interior_edge`), fading out within `haze_under_floor` units under the
column's floor. And on a void pixel (`medium_void_pixel`) the confined
volumes stop at the ray's reach (`medium_confined_reach`, the haze's
`medium_void_reach`; a pixel with nothing drawn at once, as the haze never
lies there: the first version kept a tall volume's fog over the floor in
front of such pixels, 115 of 255 over the void in the test), in the own
renderer's march too
(`fv_march`'s `t_confined`: `fogvol_march.comp` reads the shade target,
binding 7; OpenGL's `fogvol_at`), so a volume over the floor seen in front
of a black doorway keeps its fog there but none shows over the void. The
record: the volume's fill.w + 16 (`medium_volume_confined`; + 8 a sphere,
the field's layer + 1). The floor grid is packed whenever a drawn volume is
confined and the interior is on (with no haze and no ground fog too). With
`haze_interior` 0 a confined volume is the old one (no grid look-ups, no
void stop); `haze_void` 0 keeps the clip to the interior but drops the void
stop, as for the haze. Off (a lighting item's `confine=0`, the Details'
Basics "Confine to room", RPC `environment {volumes: [{id, confine}]}`, the
reply's `confine`): the volume draws wherever it is, for one meant to sit
outside the room (over a pit, beyond a window); the volumes past the first 8
(the haze passes) still stop at a void pixel's reach either way.
Measured (`void_fog_smoke`, 48/0 at the yaw showing the most void, a purely
emissive volume fitted to the floor, its light alone, of 255): over the
void pixels whose ray never crosses the room 142 before (Vulkan) -> 0.1
(Vulkan) and 0.01 (OpenGL); over the floor 144.6 vs 144.7 unconfined. Each
confined sample costs one more look-up of the grid's four cells.

**Fit to floor area** (Details; RPC `environment {fit_volume: id}`) and the
**Library's fog drop** (`editor_library` `volume_drop`, RPC
`editor_library {action: floor}`) fit to the floor grid's cells with a floor
(`pc_scene_floor_cells`) connected to the one under the volume (a cell
without a floor starts from the nearest one with a floor) within 12 units of
its height, and of those the largest box of cells around the start whose
cells lie at least 90 % on that area (a ragged or L-shaped floor's bounding
box would hang over the void): before, the flood fill followed the grid's
heights alone, and the cells outside the walls carry heights grown from
their neighbours, so in the Undercity it covered the whole bounds box. The
drop's fallback (no level area there) is the extent of the cells with a
floor, not the bounds.

The record: 90.w the switches (`PC_MEDIUM_GRID` 1, `PC_MEDIUM_INTERIOR` 2,
`PC_MEDIUM_VOID` 4; `medium.glsl` `MEDIUM_FLAG_*`), 6.w
`haze_under_floor`, 7.w the void's level, (level + 0.5) / 255, 165 the
room's air (x `haze_outside`, y z the interior's edge, w the room's lowest
floor): 166 vec4s (`PC_MEDIUM_VEC4S`, every shader's `medium_[]`).
`pc_medium_hash` includes the interior's settings and the scene serial
(the haze's history restarts when the interior changes).
**`haze_debug 2`** (RPC `graphics {haze_debug: 2}`, Atmosphere tab "Show
where the room's air is") paints red where a pixel is void, green the
interior share at the surface, blue a void pixel's reach (both renderers).

Left as they are: dynamic lights may still add a little light to a black
shell (the test compares with the atmosphere off, so it measures the
medium alone); the post profile's depth fog (`depth_fog_*`, off by default
and in no room's profile; a camera-distance tint over every pixel, the far
plane included) is a separate effect; `ptgi.c` still marches the GL haze
on void GI pixels (the present no longer uses it there; a few pixels of
bleed into floor pixels next to the void through `gi_at`'s 4 x 4 haze
average are accepted). The `gameplay_walls smart` look with ghosts (cut
walls drawn again, or translucent) works in this room as an alternative;
what void it leaves stays clean.

Tests: `pc/tests/void_fog_smoke.py` (warps to the report's spot; the void
from the vertex colours, `view_mode 4`). First the user's view: the game's
own camera, haze Low, the room's own fog and haze: the void is as without
them, and the room's own fog made visible (its shape, purely emissive) is
black over the void where the old code shows 13 of 255 on Vulkan and 15
on OpenGL (the mist of the report; with the room's real colours it is
faint, since its fill light is); the interior alone lowers it (the shells
right by the floor lie within a grid cell of it), the void test alone
takes all of it; the haze alone keeps its mood. Then at the yaw showing
the most void: the room's fog and haze still dim the floor; a bright
emissive ground fog's light is black over the void pixels whose ray never
crosses the room (most of them) and clearly there over the floor; the void
test alone clears them; `haze {interior: false, void: false}` brings the
old mist back; `haze_debug 2` marks the void red and the floor green; a
dark surface inside the room classed as void (`haze_void_level` 32) keeps
the fog in front of it; the cell under Ashley has a floor; smart walls
with ghosts; a fog volume at Ashley draws; Fit to floor area and the
Library's drop stay on the cells with a floor; a volume fitted to the floor
is black over the void and as before over the floor, and shows over the void
with `confine` off or the room's switches off; one over the whole room and
past it is black over the void confined and shows there unconfined. The CTest probe `floor_grid_probe` checks the cells
(an L-shaped room, holes, fill 0..2, empty strips along the grid's edge,
heights as before).

Traced (haze High; Vulkan ray queries, or OpenGL compute in `ptgi.c`):

- `rt_haze.comp` stores, per froxel, the light arriving per unit of
  scattering times the phase (Henyey-Greenstein, g = `haze_anisotropy`):
  the picked light or glow with its shadow ray, **the sun** (one ray to a
  point of its disc: shafts behind arches, branches and characters) and
  **the sky** (one ray in a direction over the sphere, `env_sky` x `sky`
  where it escapes: the air under a canopy, an arch or a roof loses the
  sky, which keeps the shafts' contrast). Samples: interleaved gradient
  noise over the froxels offset by the R2/R3 sequences per frame (blue-noise
  jitter, stratified in time) inside the cell and over the disc; froxels
  where the medium cannot be (`medium_maybe`, its noise at its peak) skip
  everything. History: the reprojected previous volume; it restarts when
  the medium's items or the sun change (`pc_medium_hash`, `pc_env_hash`).
  Every random choice (the jitter, the light picked, the point on it, the
  sun's and the sky's rays) cycles through 16 frames and the history keeps
  96 % a frame while the froxel stays put (`pc_haze_history` 0.04; up to
  30 % new as it moves over 1.5 cells): a still picture converges to the
  cycle's average and stays still (2026-10-14 playtest: ground fog
  shimmered with the camera, the wind and the fog's time all still; frame
  to frame change 0.12 -> 0.03 of 255 on average, 99.9th percentile 2.0 ->
  0.7, Undercity West, `fog_volume_smoke`).
- The froxel stores the medium's own contribution, not just the light:
  rgb = scattering x the light arriving + emission per unit length, a =
  extinction (both x 1000 for half floats; a + 1 marks valid history),
  evaluated at the froxel's jittered point and blended over frames with the
  reprojected history, so each froxel converges to the medium's **average
  over its cell**.
- `rt_haze_integrate.comp` walks each column front to back, one
  Beer-Lambert step per slice with that average (a 3 x 3 tent over the
  neighbouring cells and a 1-2-1 over the neighbouring slices first): `T x L x (1 - e^(-ext d)) / ext`,
  transmittance `e^(-ext d)`, the limits applied. The present samples it at
  each pixel's depth (bilinear in the cell, linear between slices):
  depth-aware by construction, and the PS1 texels are never filtered.
  (Until 2026-10-13 it sampled the medium at two fixed points per slice: a
  fog layer or a volume's edge thinner than a slice, 9 % of the depth with
  64 log slices, came out as visible slices and steps.)
- OpenGL (`ptgi.c` `haze()`): the same per march step (`fog_steps`, 4) at GI
  resolution with the sun's and the sky's rays, the result in the GI extra
  image's gba (temporal filter as before; the a-trous filter blurs the haze
  across polygons whatever their normals, only not across big jumps in
  distance: with the GI's normal weights it showed one shade per polygon);
  the present applies the transmittance from 16 steps of the same medium,
  offset per pixel by interleaved gradient noise (fixed steps left slices).

Fog volumes' own renderer (`vk_fogvol.inc`, `fog_volume.glsl`; every
quality with haze on; design: `pc/docs/plans/2026-10-14-fog-volume-renderer.md`):

- `fogvol_light[_rt].comp`: per volume (up to 8) a 32^3 grid over its
  turned box, two RGBA16F 3D images (32 x 32 x 256: one 32-slice slab a
  volume) of the transmittance toward its six lights (a: 0-3, b: 4, 5), the
  sun or moon and an ambient term (b.zw): 8 steps of its full density
  toward each, 6 in two of eight mostly-upward directions a frame for the
  ambient; with this frame's acceleration structure (the path tracer ran)
  an any-hit ray to each light and the sun. Half the cells a frame (a
  checkerboard), each at a jittered point, blended (35 %); a volume's change
  or a light changing slot restarts its cells (`pc_fogvol_info`).
- `fogvol_march.comp`: an RGBA16F image at `fog_volume_resolution` of the
  internal resolution (light, transmittance) and an RGBA32F one (the scene
  depth it marched to, where its light comes from): the ray's parts inside
  the volumes (box/sphere intersections, merged), `fog_volume_steps`
  samples spread over them, offset by interleaved gradient noise + the
  golden ratio x (frame mod 16), up to the depth under the pixel (the
  scaled target's; 2D and nothing drawn: the volumes' far side); per
  sample each volume's density (`fv_density`), its lights through the grid
  (`fv_shade`: Schlick phase, a multiple-scattering octave with g / 2 and
  the square root of the transmittance, a halo near each light, the
  ambient x (0.5 + 0.5 its grid term), fill, glow), Beer-Lambert, its own
  opacity cap; it stops under 0.3 % transmittance.
- `fogvol_temporal.comp`: the march filtered over its 3 x 3 neighbours of
  a similar depth; the fog point reprojected into the previous camera; the
  history there (bilinear) unless the scene depth behind it is not what
  this pixel's scene point had (6 % + 6 units: disocclusion, a character
  moving across), clamped to the neighbourhood's mean +- 1.25-4 sigma
  (wider while still), weight max(1 / frames, `fog_volume_history`) up to
  64 frames; ping-pong images.
- `scaled.frag` `fogvol_at`: the 2 x 2 march pixels around each pixel,
  bilinear x exp(-|depth difference| / (3 % + 4)), the closest depth when
  none matches; `c x T + L` before the haze (bindings 18, 19).

Low (Medium preset; Vulkan without ray queries; nothing traced this frame):
`low_haze` in `scaled.frag` / `gl_present.c`, 8 IGN-jittered steps of the
medium per pixel, every light unshadowed, the sun through its shadow map
(`env_sun_map_point`, one tap) or unshadowed, the sky as the emission above.

Debug: RPC `graphics {haze_debug: 1}` (editor: "Show the haze alone") draws
the haze's light alone over black; `environment {fog_time}` stops the drift
(tests). `pc/tests/atmosphere_smoke.py`: fog light follows its height (a layer
3 units tall leaves under 40 % of a 32-unit one), volumes light only their bounds, save
and reload, the forest's generated fog, the moon's shafts (haze light with
the moon's shadows vs without: 10th percentile ratio 0.72 Vulkan, 0.74 OpenGL; 90th
1.04 and 1.00), steady frames (mean change 0.59 (Vulkan, cell 2) and 0.06 of 255 between consecutive
frames, fog drift stopped). Khronos validation (1.4 layer) is quiet on the
atmosphere, sky, presets and probe smokes (`vk_descriptors_push` now skips a
push with no writes, which the newer layer reports).

Measured (RTX PRO 6000, `ROOD_PERF=1`, 1440 x 1080 window, 300 frames
after 120; the generated atmosphere vs fog off and haze density 0 in the
same room; totals vary by about 0.05 ms between runs):

| Room, preset | GPU ms/frame off -> on | haze |
| --- | --- | --- |
| Snowfly Forest (40/6), Ultra (cell 4) | 0.82 -> 0.90 | `haze` 0.038 ms; cell 3 0.040, cell 2 0.044 |
| Snowfly Forest, High | 0.45 -> 0.52 | `haze` 0.032 ms |
| Snowfly Forest, Medium (Low haze) | 0.52 -> 0.59 | in `present`: 0.07 -> 0.19 ms |
| Snowfly Forest, Low | 0.39 -> 0.40 | off (haze off: no fog) |
| Undercity West (48/4), Ultra | 0.69-0.85 (noise) | `haze` 0.033 ms; cell 2 0.034 |
| Undercity West, High | 0.45 -> 0.60 | `haze` 0.019 ms |
| Undercity West, Medium | 0.45 -> 0.51 | in `present` |

So Ultra takes `haze_cell` 2 (sharper shafts for 0.006 ms), High 3.

**Particles** (`vk_particles.inc`, `particles_main.glsl`; pc/docs/Particles.md):
the port's dust motes are one compute pass after the fog volumes' passes
(perf section `particles`), only when a room's emitters draw motes (Dust Off:
no pass, nothing bound). Two pipelines: `particles.comp` (motes lit
unshadowed) and `particles_rt.comp` (`PARTICLES_RT`: a ray query per light
source and the sun, against this frame's acceleration structure, when the
path tracer built one). Bindings: 0 the UBO (`particle_math.h`
`pc_particles_gpu`: emitters, camera, sun and sky, glows, the floor grid), 1-3
and 9 `rt_common.glsl`'s (the TLAS, triangles, VRAM, movers; declared but
unused without ray queries), 4 the light list (`pc_light_gpu`), 5 the key
image (R32_UINT, a texel per game pixel of the display, cleared to
`0xFFFFFFFF` with `vkCmdClearColorImage` first), 6 the colour image (RGBA32F,
256 x 64, a texel per mote). One invocation per mote evaluates its closed form
(`particles_sim.glsl`), lights it, writes its colour and, when it is lit,
`imageAtomicMin`s `depth << 16 | mote` into its game pixels. The present pass
binds the two images at 21 and 22 and `scaled.frag` composites them
(`particles.glsl`: whole game pixels, depth-tested, before the fog volumes
and the haze). Measured: 0.02 ms a frame for 4096 motes (960 x 720, Ultra).

## Status (2026-10-11): NRD

Measured on the NVIDIA RTX PRO 6000 (shared with other jobs: absolute
times vary by up to 2x between runs, so compare within a run), GI
resolution 3 (960x672 GI pixels), a test point light.

- GPU time per presented frame (ROOD_PERF, one launch, modes alternating):
  save room 0.65-0.68 ms with the filter, 0.89 ms with NRD (section
  `gi nrd` 0.24 ms; `gi filter` 0.12 ms still runs for the visibility
  signals); first room 0.78 -> 1.06 ms (`gi nrd` 0.22 ms).
- Still (save room, indirect + glow light alone, `gi_debug 1`, floor crop):
  frame-to-frame difference 0.196 (filter) -> 0.072-0.081 (NRD), raw 22;
  residual grain the same (1.05 vs 1.03). In the final image the indirect
  light is a small part: frame-to-frame 0.84 vs 0.82-0.88 (idle animation).
- Walking (save room, the same scripted walk in each launch, 40 presents,
  indirect alone): frame-to-frame 3.31 (filter) vs 2.83 (NRD); error
  against a 16-ray raw reference 3.92 vs 3.90 at full resolution, 1.36 vs
  1.43 after a 4 px blur. The filter's indirect light shows mottled
  low-frequency blotches on walls and floor around Ashley while she moves;
  NRD's is clean.
- Opening camera pan (first room, a light added 90 vsyncs before): error
  against the 16-ray reference 10.08 (filter) vs 9.66 (NRD), blurred 4.81
  vs 4.34. The filter still showed a dark silhouette from before the light
  change (history kept where the lighting signature matched, replaced
  slowly); NRD resets on light changes and shows none.
- Energy: displayed/raw mean indirect 0.92-0.93 with NRD (0.97 without
  RELAX's anti-firefly), 0.98 with the filter.
- Khronos validation layer (the Steam runtime's copy through a manifest in
  `VK_LAYER_PATH`): no messages with NRD (nrd_smoke, including the resize).

## Status (2026-10-10)

- Shadows fixed in world space while the camera moves (save room, the
  user's point light by the candle, R1 turn and walking): world-space
  shadow visibility off by more than 0.3 on 3.8 % of the surface cells
  during a turn before, 0.56 % after (0.38 % when settled); walking 5.0 %
  -> 0.97 %. The OpenGL path shares the camera (and had the same slide).
  Regression test `rt_camera_smoke` (Vulkan; a camera pan in the first
  room: every GI update's world positions lie on the room geometry), and
  RPC `gi_dump {path, count}` (GI positions, normals and visibility with the
  update's camera) for measuring shadows in world space.

Stages 1 (backend), 2 (raster parity) and 3 (ray tracing) are done; the
renderer stays opt-in until the user playtests it.

- Full suite: `run.py --arch 64 --renderer vulkan` 17/17, `--arch 64` (GL)
  17/17, 32-bit 17/17. Golden frames (Vulkan against the GPU's GL
  references): all 8 pass, 0.00-0.05 mean (raster-only frames unchanged;
  `room-gi` 0.02). No validation messages (first room with GI, haze,
  bursts, moving characters; also without ray tracing).
- Lighting vs GL (first room, a test point light with shadows, 1280x960):
  mean difference 0.8-1.2 without haze, 0.9-1.8 with the default haze
  (haze brightness within 2% of GL's per-pixel march). The differences
  are the denoiser (cleaner moving shadows) and the haze model.
- Moving shadows: GL/old Vulkan visibility keeps faint smeared outlines
  behind a moving character's shadow (strict history + long accumulation);
  with motion and the short moving-blocker history they are gone (game
  camera, idle animation, visibility debug view). Static frames are as
  stable as before (paused consecutive-frame difference 0.12 vs 0.08 of
  255).
- Glow light (save room, the save point's glow with glow shadows on, light
  debug view): the old build's glow light was grainy noise everywhere
  (one stochastic glow sample per pixel, no variance guidance); now it is
  clean with soft shadows of Ashley and the barrels (consecutive-frame
  difference 1.28 -> 0.76 while she animates).
- Haze: unlit pixels (the save point's column, effects, glows) now get the
  haze their neighbours get; before, they stayed un-hazed (bright, crisp
  islands in dense haze).
- GPU time (first room with a shadowed light, haze, GI at defaults):
  NVIDIA RTX PRO 6000: 0.6-0.8 ms per frame in all (gi scene 0.10, surface
  0.02, trace 0.11, haze 0.03, temporal 0.03, filter 0.13, present
  0.08 ms); the GL path spends 3.0-3.6 ms of CPU per scene update building
  its BVH. Intel Arc (Arrow Lake iGPU): 70-75 ms before (GI 3 per display
  pixel), now 16.5 ms at the integrated default (GI 1.5: scene 2.0, surface
  0.7, trace 3.2, haze 1.1, temporal 1.4, filter 2.4, present 3.8 ms) and
  42 ms at GI 3 (57 ms for the old build in the same load). The Intel GPU
  shares the CPU's power and memory: numbers vary with machine load.

## Next

- Sun and sky: a time-of-day per zone if the story's hours should show;
  cascades for the sun's shadow map in very large rooms (one 1024 map covers
  the room's bounding sphere).
- Fog: an advected 2D density (FluidNinja-style) that flows around Ashley
  and the actors instead of noise alone; self-shadowing of thick fog (light
  attenuated toward the sun); the editor's separate GL view applies only the
  uniform haze's transmittance (it uses the trace's haze light, so fog there
  is approximate).
- **Weather** (out of scope so far; design): rain and snow as GPU particles
  in a camera-following box (streaks for rain, slow drifting flakes for
  snow), occluded by a top-down depth map of the room so they stop under
  roofs and arches, lit by the same lights (cheap: per-particle light from
  the haze volume at its position); wet surfaces as a per-room wetness that
  darkens the albedo and adds a sharp specular of the sun and lights to
  level faces (ripples from the same noise as the fog); lightning as a
  short-lived directional light (the sun's slot: its shadows come free on
  every tier) with a flash curve, the sky radiance pulsed with it and a
  delayed thunder cue; per-room `env weather` items like `env fog`.

- NRD: try RELAX with the lighting signature as history confidence
  (`IN_DIFF_CONFIDENCE`, a deterministic rejection); REBLUR or
  SIGMA per light if the shadow visibility should move to NRD too; skip the
  filter's indirect channel when NRD replaces it; measure on the Intel iGPU
  (GI resolution 1.5 there).

- Playtest, then make Vulkan the default.
- Glows: tune emitter sizes and glow strength per room in a playtest; ReSTIR-style
  reuse only if many glows overlap (one pick per pixel is proportional to
  the unshadowed light). The first room's glow light stays black in the
  debug view (old and new builds): check which of its glows reach the
  camera's surfaces.
- Opaque/cut-out split for mover meshes too (movers still run the texel
  test on every candidate).
- Intel: the filter and temporal passes read storage images; sampled
  reads or packed guides would roughly halve them.
- Retire the GL path's cube maps and CPU BVH once Vulkan is the default.
