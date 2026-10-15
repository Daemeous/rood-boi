/* Post-processing profiles and the post stack (pc/docs/Post_Processing.md).
 *
 * A post profile is a set of named values (post_profile.cpp's field table:
 * depth of field, tilt-shift, depth fog, exposure, bloom, chromatic
 * aberration, vignette, film grain, tone mapping, white balance, colour
 * grading, LUT, sharpen). Like Unreal's post process volumes every layer
 * only overrides the fields it sets, lowest first:
 *
 *   built-in defaults (everything off / neutral: the original picture)
 *   the gameplay preset (settings post_preset, pc/assets/post/<name>.json)
 *   the user's gameplay values (settings post_<field>)
 *   the room's `post room` item (lighting packs: generated, authored, user)
 *   -- in a cutscene (an event script runs) also: --
 *   the built-in cutscene layer (no depth of field, no tilt-shift)
 *   the cutscene preset (post_cutscene_preset) and values (post_cutscene_<field>)
 *   the room's `post cutscene` item, then the event's `post event.<id>` item
 *
 * The result blends over post_blend_time seconds (game time) when it
 * changes (a room change, gameplay <-> cutscene), and snaps on a camera cut.
 * The presenters read one record per present (pc_post_frame_get) and a
 * packed parameter block for the grade pass (pc_post_gpu, read by
 * pc/shaders/vulkan/post_grade.glsl as PG(i)). */
#ifndef ROOD_POST_H
#define ROOD_POST_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_TONEMAP_NONE, PC_TONEMAP_ACES, PC_TONEMAP_AGX, PC_TONEMAP_FILMIC, PC_TONEMAP_REINHARD, PC_TONEMAP_NEUTRAL,
    PC_TONEMAP_COUNT };
enum { PC_BLOOM_AUTO, PC_BLOOM_GAUSSIAN, PC_BLOOM_CONVOLUTION };
enum { PC_POST_GAMEPLAY, PC_POST_CUTSCENE };

#define PC_POST_GPU_VEC4S 40
typedef struct {
    float v[PC_POST_GPU_VEC4S][4];
} pc_post_gpu;

/* This present's post effects, resolved and blended. */
typedef struct {
    int context; /* PC_POST_GAMEPLAY / PC_POST_CUTSCENE */
    /* Depth of field, tilt-shift, depth fog (the present shader's `post` bits 1, 2, 4). */
    int flags;
    float focus_sz; /* 0: the shader picks (screen centre) */
    float dof_radius, aperture, near_blur; /* px at 720 lines; near: the share of it in front of the focus */
    float tilt[3]; /* centre, half width, radius (px at 720) */
    float fog[4], fog_range[2];
    /* The grade pass (post_grade.glsl): run at all; the lit image may exceed 1 (tone mapped there). */
    int grade, hdr;
    int bloom; /* 0 none, PC_BLOOM_GAUSSIAN, PC_BLOOM_CONVOLUTION */
    float bloom_threshold, bloom_knee, bloom_size, exposure_scale;
    float bloom_kernel[5]; /* convolution kernel: streaks, streak intensity, angle (deg), falloff, core */
    int meter; /* auto exposure: meter this frame's picture (pc_post_meter_result) */
    int lut; /* a LUT is loaded and used (pc_post_lut) */
    float fade; /* the game's screen fade (gpu_hw.c pc_hw_present_fade): subtracted from the 3D last */
    float fade_rect[4]; /* where it lands: u0, v0, u1, v1 of the view */
} pc_post_frame;

/* Resolve and blend for this present (once per present; the presenters
 * call it before drawing). view_h: the view's height (scales pixel sizes). */
void pc_post_frame_get(pc_post_frame* out);
/* The grade pass's parameters for a lit image of lit_w x lit_h shown in a
 * view of view_w x view_h. bloom_uv: the convolution result's mapping
 * (scale.xy, offset.xy) from the view's uv, 0 for the Gaussian pyramid. */
void pc_post_gpu_pack(pc_post_gpu* out, int lit_w, int lit_h, int view_w, int view_h, const float bloom_uv[4]);
/* Auto exposure: the metered picture's mean log2 luminance (of 3D pixels;
 * weight: their share, 0 none), a few frames late. */
void pc_post_meter_result(float mean_log2, float weight);
/* The same from the meter image (post_bloom.glsl pb_meter: r the mean log2 luminance x share, g the
 * share of 3D, per texel; rows of floats, `stride` floats per texel), centre-weighted. */
void pc_post_meter_reduce(const float* texels, int w, int h, int stride);
#define PC_POST_METER_W 32
#define PC_POST_METER_H 24
/* The LUT (settings/profile `lut`): an RGBA8 strip of size*size x size
 * texels (blue slices side by side), and a serial that changes when it does;
 * NULL when none. */
const unsigned char* pc_post_lut(int* size, unsigned* serial);
/* The convolution kernel for an n x n domain whose picture's long side is
 * `picture` texels: centred on texel (0, 0), wrapping, normalised (its sum
 * 1, the centre texel 0). Written to out[n * n] (floats). Returns a hash of
 * the parameters (rebuild when it changes). */
unsigned pc_post_kernel_hash(int n, int picture);
void pc_post_kernel_build(float* out, int n, int picture);
/* The game frame the picture shows (gpu_hw.c; film grain's seed). */
unsigned pc_hw_frame_serial(void);

/* The current context (event scripts), its event id (or -1) and the forced one (-1 auto). */
int pc_post_context(void);
int pc_post_event_id(void);
extern int pc_post_force_context;
/* Master switch (settings post_stack; 0: every post effect off, as without profiles). */
extern int pc_post_stack;
/* Convolution bloom's domain size (settings post_bloom_fft_size: 256 or 512). */
extern int pc_post_fft_size;
/* Whether the renderer can run the convolution bloom (Vulkan compute; set by vk_present.c). */
extern int pc_post_fft_available;
/* Last present's state for RPC/diagnostics: grade pass ran, bloom method used, EV. */
extern int pc_post_active_grade, pc_post_active_bloom;
extern float pc_post_active_ev;

/* RPC graphics `post` (bits 1 DoF, 2 tilt-shift, 4 depth fog): the user's gameplay values; and back. */
void pc_post_set_flags(int flags);
int pc_post_get_flags(void);

/* Settings (project_settings.cpp): a post_* key (1 when it was one); every set value written. */
int pc_post_settings_parse(const char* key, const char* rest);
void pc_post_settings_write(void* file);
/* RPC `post` (post_profile.cpp). */
int pc_post_rpc(const char* params, char* out, int size);
/* Editor: Details > Post-processing. */
void pc_post_panel(void);

#ifdef __cplusplus
}
#endif
#endif
