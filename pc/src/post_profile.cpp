// Post-processing profiles (post.h; pc/docs/Post_Processing.md): the field
// table, the layers (presets, the user's settings, the room's items, the
// cutscene context), their resolution and blending, the depth of field's
// focus, auto exposure, the LUT, the convolution kernel, the RPC `post` and
// Details > Post-processing.
//
// Everything is driven by one field table (`fields()` below): parsing and
// writing (settings lines, lighting items, preset JSON, RPC), blending, the
// GPU parameter block and the editor's widgets. A field is added in one
// place, and every layer, file format and the editor know it.
#include "post.h"

#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "dev_state.h"
#include "imgui.h"
#include "light_prefabs.h"
#include "lighting_layers.h"
#include "lights_internal.h"
#include "platform.h"
#include "environment_internal.h"

extern "C" {
extern unsigned pc_vsync_count(void) __attribute__((weak));
extern int pc_battle_event_running(void) __attribute__((weak)); /* scene_submit.inc (battle builds) */
extern int pc_battle_event_id(void) __attribute__((weak));
extern int pc_scene_camera_get(float (*)[3], float*, float*, float*, float*);
extern int pc_game_phase __attribute__((weak)); /* boot_runtime.c: 1 title, 2 attract demo, 0 a game */
extern int pc_battle_field_menu_open(void) __attribute__((weak)); /* weapon_slots.c (battle builds) */
extern int pc_battle_first_person(void) __attribute__((weak)); /* weapon_slots.c: the first-person view is up */
extern int pc_gl_view[4] __attribute__((weak)); /* the presented view in the window (gl_present.c / vk_present.c) */
extern int pc_gl_display[4] __attribute__((weak)); /* the display area in VRAM */
extern int pc_ui_menu_active(void) __attribute__((weak)); /* ui/menu_bridge.c: the modern menus */
extern int pc_option_peek_active(void) __attribute__((weak)); /* ui/option_peek.h: the Option pages' preview */
extern float pc_hw_present_fade(float rect[4]) __attribute__((weak)); /* gpu_hw.c: the shown picture's screen fade */
extern int pc_hw_present_menu_model(void) __attribute__((weak)); /* gpu_hw.c: the shown picture is the menu's model view */
extern int pc_hw_active;
extern int pc_photo_holding(void) __attribute__((weak)); /* photo.cpp: the photo mode holds the game */
void pc_lights_mark_dirty(void);
void pc_settings_save(void);
const char* pc_settings_status(void);
}

int pc_post_stack = 1;
int pc_post_force_context = -1;
int pc_post_fft_size = 512;
int pc_post_fft_available;
int pc_post_active_grade, pc_post_active_bloom;
float pc_post_active_ev;

namespace {

double rl_now_seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------ the fields */

enum Kind { BOOL, FLOAT, ENUM, COLOR, VEC4, TEXT };
enum { LOG = 1, INT = 2 };
struct Field {
    const char* key;
    Kind kind;
    float def[4];
    float lo, hi;
    const char* group; /* "Section|Subsection" (the editor's headers) */
    const char* label;
    const char* tip;
    const char* names; /* ENUM: the values' keys, '\0'-separated */
    const char* labels; /* ENUM: the editor's names */
    unsigned flags;
    int offset = 0; /* into Values::f (fields() fills it) */
    int count() const { return kind == COLOR ? 3 : kind == VEC4 ? 4 : kind == TEXT ? 0 : 1; }
};
#define V1(a) { a, 0, 0, 0 }
#define V3(a, b, c) { a, b, c, 0 }
#define V4(a, b, c, d) { a, b, c, d }
const char* const TONEMAP_NAMES = "none\0aces\0agx\0filmic\0reinhard\0neutral\0";
const char* const TONEMAP_LABELS = "None (original)\0ACES (fitted)\0AgX\0Filmic (Hable)\0Reinhard\0Neutral (Khronos PBR)\0";

std::vector<Field>& fields()
{
    static std::vector<Field> f;
    if (!f.empty())
        return f;
    auto add = [&](Field x) { f.push_back(x); };
    /* Depth of field (gameplay; the cutscene layer turns it off). */
    add({ "dof", BOOL, V1(0), 0, 1, "Lens|Depth of Field", "Depth of field",
        "Blur away from the focal plane (focus on the camera's subject by default: Ashley in the field). Off in\n"
        "cutscenes unless the cutscene layer turns it on.",
        nullptr, nullptr, 0 });
    add({ "dof_model", ENUM, V1(1), 0, 1, "Lens|Depth of Field", "Model",
        "Physical: a camera lens (f-stop, focal length, sensor; the scene's scale from Ashley's height).\n"
        "Artistic: blur grows with the aperture value from the focal plane, whatever the lens.",
        "physical\0artistic\0", "Physical camera\0Artistic\0", 0 });
    add({ "dof_focus", ENUM, V1(4), 0, 4, "Lens|Depth of Field", "Focus",
        "Camera subject (the default): whom the game's camera frames. Ashley in the field; in a battle action the\n"
        "actor the action camera turns to (an enemy's wind-up and cast, Ashley's chain, a counter), the target\n"
        "while targeting (the rules below); eased between them, Ashley whenever no subject is known.\n"
        "On Ashley: her tracked position always. Neither lets a wall in front steal the focus.\n"
        "Screen centre and Cursor: the depth there (the mouse over the picture), measured each frame and eased.\n"
        "Manual: a fixed depth.",
        "ashley\0centre\0manual\0cursor\0subject\0",
        "On Ashley\0Screen centre\0Manual depth\0Cursor\0Camera subject\0", 0 });
    add({ "dof_focus_depth", FLOAT, V1(1024), 64, 8000, "Lens|Depth of Field", "Focus depth",
        "Manual focus: the focal plane's view depth (GTE SZ; the RPC `post` reply gives it in metres).", nullptr,
        nullptr, LOG });
    add({ "dof_focus_area", FLOAT, V1(0.03f), 0.005f, 0.2f, "Lens|Depth of Field", "Focus probe size",
        "Screen centre and Cursor: the radius around the point whose depth is averaged (share of the picture's height).",
        nullptr, nullptr, LOG });
    add({ "dof_fstop", FLOAT, V1(2.8f), 1.2f, 22, "Lens|Depth of Field", "f-stop",
        "Physical: the aperture's f-number. Lower is a wider aperture: more blur away from the focus.", nullptr, nullptr,
        LOG });
    add({ "dof_focal_length", FLOAT, V1(0), 0, 300, "Lens|Depth of Field", "Focal length (mm)",
        "Physical: 0 takes it from the game's field of view and the sensor; otherwise this lens (longer: more blur).",
        nullptr, nullptr, 0 });
    add({ "dof_sensor", FLOAT, V1(24), 4, 60, "Lens|Depth of Field", "Sensor height (mm)",
        "Physical: the picture's height on the sensor (24: full frame, 15.6: APS-C, 13: Four Thirds).", nullptr,
        nullptr, 0 });
    add({ "dof_ashley_height", FLOAT, V1(1.8f), 0.5f, 5, "Lens|Depth of Field", "Scene scale: Ashley's height (m)",
        "Physical: how tall Ashley is in metres; sets the scene's scale (a taller Ashley: a larger world, less blur).",
        nullptr, nullptr, 0 });
    add({ "dof_aperture", FLOAT, V1(4), 0.25f, 16, "Lens|Depth of Field", "Aperture (artistic)",
        "Artistic: how fast blur grows away from the focal plane. Large with focus on Ashley: the miniature / macro look.",
        nullptr, nullptr, LOG });
    add({ "dof_max_blur", FLOAT, V1(8), 0, 32, "Lens|Depth of Field", "Maximum blur",
        "The largest blur radius (the cap), in pixels of a 720-line picture.", nullptr, nullptr, 0 });
    add({ "dof_near_blur", FLOAT, V1(0.5f), 0, 1, "Lens|Depth of Field", "Foreground blur",
        "Blur in front of the focus, as a share of the maximum: walls and props between the camera and Ashley become a\n"
        "soft veil instead of a smear (1: as much as behind; 0: no foreground layer).", nullptr, nullptr, 0 });
    add({ "dof_focus_speed", FLOAT, V1(0.15f), 0, 2, "Lens|Depth of Field", "Focus pull time",
        "Seconds the focus takes to follow a change (0: at once). Camera cuts and room changes snap.", nullptr, nullptr, 0 });
    /* The camera subject's rules: which situation follows which actor (camera_subject() below; data, so a
     * room, a preset or the user, and later a Lua plugin through the same fields, can change them). */
    add({ "dof_subject_others", ENUM, V1(0), 0, 3, "Lens|Camera subject", "Another actor's action",
        "Camera subject focus while an enemy (any actor but Ashley) acts, from its wind-up to the camera's return:\n"
        "the actor (the caster), its target, the game camera's look-at point (between the two), or Ashley.",
        "actor\0target\0look_at\0ashley\0", "The actor\0Its target\0The camera's look-at\0Ashley\0", 0 });
    add({ "dof_subject_ashley", ENUM, V1(0), 0, 3, "Lens|Camera subject", "Ashley's action",
        "Camera subject focus while Ashley acts (an attack, a chain, a spell, an item): Ashley herself, her target,\n"
        "the game camera's look-at point, or Ashley.",
        "actor\0target\0look_at\0ashley\0", "Ashley (the actor)\0Her target\0The camera's look-at\0Ashley\0", 0 });
    add({ "dof_subject_keep_ashley", BOOL, V1(0), 0, 1, "Lens|Camera subject",
        "Keep Ashley when targeted", "Camera subject: an enemy's action aimed at Ashley keeps the focus on her\n"
        "(off: on the enemy, as Another actor's action says).", nullptr, nullptr, 0 });
    add({ "dof_subject_targeting", ENUM, V1(0), 0, 1, "Lens|Camera subject", "Targeting",
        "Camera subject focus while a target is chosen (the targeting sphere, an area point): the target, or Ashley.",
        "target\0ashley\0", "The target\0Ashley\0", 0 });
    add({ "dof_subject_speed", FLOAT, V1(0.4f), 0, 3, "Lens|Camera subject", "Subject change time",
        "Seconds the focus takes to move to a new camera subject (0: at once). Camera cuts still snap.", nullptr,
        nullptr, 0 });
    /* The bokeh: the aperture's shape and character (both depth of fields). */
    add({ "bokeh_blades", FLOAT, V1(0), 0, 9, "Lens|Bokeh", "Aperture blades",
        "0: a round aperture. 5 to 9: a polygon of that many blades (the shape bright out-of-focus points take).",
        nullptr, nullptr, INT });
    add({ "bokeh_curvature", FLOAT, V1(0), 0, 1, "Lens|Bokeh", "Blade curvature", "0 straight blades, 1 round.",
        nullptr, nullptr, 0 });
    add({ "bokeh_rotation", FLOAT, V1(0), 0, 360, "Lens|Bokeh", "Blade rotation", "Degrees.", nullptr, nullptr, 0 });
    add({ "bokeh_squeeze", FLOAT, V1(1), 1, 2, "Lens|Bokeh", "Anamorphic squeeze",
        "Above 1: oval bokeh, narrower than tall (an anamorphic lens).", nullptr, nullptr, 0 });
    add({ "bokeh_edge", ENUM, V1(1), 0, 2, "Lens|Bokeh", "Edge character",
        "Soft: brighter in the middle, fading out. Even: flat discs. Rim: a bright ring (soap-bubble bokeh).",
        "soft\0even\0rim\0", "Soft\0Even\0Rim\0", 0 });
    add({ "bokeh_edge_amount", FLOAT, V1(0.5f), 0, 1, "Lens|Bokeh", "Edge amount", "How strong soft or rim is.",
        nullptr, nullptr, 0 });
    add({ "bokeh_fringe", FLOAT, V1(0), 0, 1, "Lens|Bokeh", "Colour fringe",
        "Red and blue edges on the bokeh (the lens's chromatic aberration out of focus).", nullptr, nullptr, 0 });
    add({ "bokeh_cats_eye", FLOAT, V1(0), 0, 1, "Lens|Bokeh", "Cat's eye",
        "Optical vignetting: toward the frame's edges the bokeh is clipped into a cat's-eye shape (amount at the corners).",
        nullptr, nullptr, 0 });
    add({ "bokeh_highlights", FLOAT, V1(0.5f), 0, 2, "Lens|Bokeh", "Highlight weight",
        "How much bright points dominate the blur, so their bokeh shapes show.", nullptr, nullptr, 0 });
    add({ "bokeh_samples", FLOAT, V1(64), 16, 160, "Lens|Bokeh", "Samples",
        "Kernel samples per pixel (half resolution): more is smoother and costs more.", nullptr, nullptr, INT });
    /* The first-person view's own depth of field (focus on what the player looks at). */
    add({ "dof_fp", BOOL, V1(0), 0, 1, "Lens|First-person Depth of Field", "First-person depth of field",
        "In the first-person view: focus on what is at the centre of the view, refocusing smoothly (this view's own\n"
        "settings below; the lens and bokeh above). The third-person depth of field does not apply there.", nullptr,
        nullptr, 0 });
    add({ "dof_fp_fstop", FLOAT, V1(2), 1.2f, 22, "Lens|First-person Depth of Field", "f-stop", "", nullptr, nullptr,
        LOG });
    add({ "dof_fp_max_blur", FLOAT, V1(8), 0, 32, "Lens|First-person Depth of Field", "Maximum blur",
        "Pixels of a 720-line picture.", nullptr, nullptr, 0 });
    add({ "dof_fp_near_blur", FLOAT, V1(1), 0, 1, "Lens|First-person Depth of Field", "Foreground blur",
        "Blur in front of the focus, as a share of the maximum.", nullptr, nullptr, 0 });
    add({ "dof_fp_focus_speed", FLOAT, V1(0.3f), 0, 2, "Lens|First-person Depth of Field", "Refocus time",
        "Seconds the focus takes to follow what the view looks at.", nullptr, nullptr, 0 });
    add({ "dof_fp_focus_area", FLOAT, V1(0.04f), 0.005f, 0.2f, "Lens|First-person Depth of Field", "Focus probe size",
        "The radius around the centre whose depth is averaged (share of the picture's height).", nullptr, nullptr, LOG });
    add({ "tilt_shift", BOOL, V1(0), 0, 1, "Lens|Tilt-shift", "Tilt-shift",
        "A sharp horizontal band, blur above and below. Off in cutscenes unless the cutscene layer turns it on.", nullptr,
        nullptr, 0 });
    add({ "tilt_centre", FLOAT, V1(0.55f), 0, 1, "Lens|Tilt-shift", "Band centre", "0 top, 1 bottom.", nullptr, nullptr, 0 });
    add({ "tilt_width", FLOAT, V1(0.12f), 0, 0.5f, "Lens|Tilt-shift", "Band half width", "", nullptr, nullptr, 0 });
    add({ "tilt_blur", FLOAT, V1(6), 0, 16, "Lens|Tilt-shift", "Tilt blur", "Pixels of a 720-line picture.", nullptr,
        nullptr, 0 });
    add({ "tilt_feather", FLOAT, V1(0.25f), 0.01f, 1, "Lens|Tilt-shift", "Band feather",
        "How gradually the blur grows past the band: the share of the picture's height it takes to reach Tilt blur.",
        nullptr, nullptr, 0 });
    /* Exposure. */
    add({ "exposure_mode", ENUM, V1(0), 0, 1, "Lens|Exposure", "Metering mode",
        "Manual: exposure compensation only. Auto: the picture's mean brightness adapts toward mid grey within the range.",
        "manual\0auto\0", "Manual\0Auto (eye adaptation)\0", 0 });
    add({ "exposure", FLOAT, V1(0), -4, 4, "Lens|Exposure", "Exposure compensation",
        "Stops (EV): +1 doubles the light.", nullptr, nullptr, 0 });
    add({ "auto_exposure_min", FLOAT, V1(-1), -6, 6, "Lens|Exposure", "Auto: min EV", "", nullptr, nullptr, 0 });
    add({ "auto_exposure_max", FLOAT, V1(1), -6, 6, "Lens|Exposure", "Auto: max EV", "", nullptr, nullptr, 0 });
    add({ "auto_exposure_speed_up", FLOAT, V1(3), 0.05f, 20, "Lens|Exposure", "Auto: speed up",
        "Stops per second toward a brighter exposure (dark to light).", nullptr, nullptr, LOG });
    add({ "auto_exposure_speed_down", FLOAT, V1(1), 0.05f, 20, "Lens|Exposure", "Auto: speed down",
        "Stops per second toward a darker exposure.", nullptr, nullptr, LOG });
    add({ "auto_exposure_target", FLOAT, V1(0.18f), 0.02f, 0.6f, "Lens|Exposure", "Auto: target",
        "The linear mean luminance auto exposure aims for (0.18: mid grey).", nullptr, nullptr, LOG });
    /* Bloom. */
    add({ "bloom_method", ENUM, V1(0), 0, 2, "Lens|Bloom", "Method",
        "Convolution: an FFT with a lens kernel (glare, streaks; Vulkan). Gaussian: a cheap mip pyramid (any renderer).\n"
        "Auto: convolution where the renderer has it.",
        "auto\0gaussian\0convolution\0", "Auto\0Gaussian (cheap)\0Convolution (FFT)\0", 0 });
    add({ "bloom_intensity", FLOAT, V1(0), 0, 4, "Lens|Bloom", "Intensity", "0: no bloom.", nullptr, nullptr, 0 });
    add({ "bloom_threshold", FLOAT, V1(0.7f), 0, 4, "Lens|Bloom", "Threshold",
        "Linear brightness (after exposure) where light starts to bloom.", nullptr, nullptr, 0 });
    add({ "bloom_knee", FLOAT, V1(0.5f), 0, 1, "Lens|Bloom", "Soft knee", "0: a hard threshold.", nullptr, nullptr, 0 });
    add({ "bloom_size", FLOAT, V1(1), 0.25f, 4, "Lens|Bloom", "Size", "How far the glow spreads.", nullptr, nullptr, LOG });
    add({ "bloom_tint", COLOR, V3(1, 1, 1), 0, 2, "Lens|Bloom", "Tint", "", nullptr, nullptr, 0 });
    add({ "bloom_kernel_core", FLOAT, V1(0.6f), 0, 1, "Lens|Bloom", "Kernel: halo share",
        "Convolution kernel (procedural): the share of a soft halo near the light; the rest is a wide glare.", nullptr,
        nullptr, 0 });
    add({ "bloom_kernel_falloff", FLOAT, V1(2.5f), 1.5f, 4, "Lens|Bloom", "Kernel: glare falloff",
        "The glare's power law: larger is tighter.", nullptr, nullptr, 0 });
    add({ "bloom_kernel_streaks", FLOAT, V1(0), 0, 12, "Lens|Bloom", "Kernel: streaks",
        "Diffraction spikes (an even number: lines through the light).", nullptr, nullptr, INT });
    add({ "bloom_kernel_streak_intensity", FLOAT, V1(0.25f), 0, 1, "Lens|Bloom", "Kernel: streak share", "", nullptr,
        nullptr, 0 });
    add({ "bloom_kernel_angle", FLOAT, V1(15), 0, 180, "Lens|Bloom", "Kernel: streak angle", "Degrees.", nullptr, nullptr,
        0 });
    /* Chromatic aberration, vignette. */
    add({ "chromatic_aberration", FLOAT, V1(0), 0, 5, "Lens|Chromatic Aberration", "Intensity",
        "Radial colour fringes (red outward, blue inward), in pixels of a 1080-line picture at the corners.", nullptr,
        nullptr, 0 });
    add({ "chromatic_aberration_start", FLOAT, V1(0), 0, 1, "Lens|Chromatic Aberration", "Start offset",
        "Distance from the centre (0..1 of the way to the corner) where fringes begin.", nullptr, nullptr, 0 });
    add({ "vignette", FLOAT, V1(0), 0, 1, "Lens|Image Effects", "Vignette intensity", "Darkens toward the corners.",
        nullptr, nullptr, 0 });
    add({ "vignette_gain", FLOAT, V1(1.2f), 0.25f, 4, "Lens|Image Effects", "Vignette falloff",
        "How fast the vignette darkens toward the corners for a given intensity (larger: darker, reaching further in).",
        nullptr, nullptr, 0 });
    add({ "vignette_roundness", FLOAT, V1(1), 0, 1, "Lens|Image Effects", "Vignette roundness",
        "1: a circle; 0: the frame's own shape (an ellipse as wide as the picture).", nullptr, nullptr, 0 });
    add({ "lens_distortion", FLOAT, V1(0), -0.5f, 0.5f, "Lens|Image Effects", "Lens distortion",
        "Above 0: barrel (straight lines bow outward, a wide lens), below 0: pincushion; scaled so the corners stay\n"
        "in the picture.", nullptr, nullptr, 0 });
    /* Depth fog (the older screen-space fog, by GTE depth). */
    add({ "depth_fog", BOOL, V1(0), 0, 1, "Rendering Features|Depth fog", "Depth fog",
        "A plain fog by view depth (the room's haze and ground fog are in the Atmosphere tab).", nullptr, nullptr, 0 });
    add({ "depth_fog_color", COLOR, V3(0.05f, 0.04f, 0.06f), 0, 1, "Rendering Features|Depth fog", "Fog colour", "",
        nullptr, nullptr, 0 });
    add({ "depth_fog_amount", FLOAT, V1(0.6f), 0, 1, "Rendering Features|Depth fog", "Fog amount", "", nullptr, nullptr, 0 });
    add({ "depth_fog_start", FLOAT, V1(600), 0, 65535, "Rendering Features|Depth fog", "Fog start (SZ)", "", nullptr,
        nullptr, LOG });
    add({ "depth_fog_end", FLOAT, V1(4000), 0, 65535, "Rendering Features|Depth fog", "Fog end (SZ)", "", nullptr,
        nullptr, LOG });
    add({ "sharpen", FLOAT, V1(0), 0, 1, "Rendering Features|Sharpen", "Sharpen",
        "Contrast-adaptive sharpening of the 3D (never past the neighbours' range: no halos).", nullptr, nullptr, 0 });
    add({ "sharpen_gain", FLOAT, V1(1.5f), 0.25f, 4, "Rendering Features|Sharpen", "Sharpen strength",
        "How much detail Sharpen adds at a given amount (the gain on the difference from the neighbours' mean).",
        nullptr, nullptr, 0 });
    /* White balance and colour grading. */
    add({ "temperature", FLOAT, V1(6500), 1500, 15000, "Color Grading|White Balance", "Temperature",
        "Kelvin of the light that should look white: lower values make the picture cooler (bluer).", nullptr, nullptr, 0 });
    add({ "tint", FLOAT, V1(0), -1, 1, "Color Grading|White Balance", "Tint",
        "Green (-) to magenta (+) along the daylight locus.", nullptr, nullptr, 0 });
    static const char* const regions[4] = { "", "shadows_", "midtones_", "highlights_" };
    static const char* const groups[4] = { "Color Grading|Global", "Color Grading|Shadows", "Color Grading|Midtones",
        "Color Grading|Highlights" };
    static std::string keys[4][5];
    static const char* const params[5] = { "saturation", "contrast", "gamma", "gain", "offset" };
    static const char* const labels[5] = { "Saturation", "Contrast", "Gamma", "Gain", "Offset" };
    static const char* const tips[5] = { "0 grey, 1 unchanged; per channel and master.",
        "Around mid grey (0.18 linear).", "Midtone power: above 1 brightens.", "Multiplies the light.",
        "Adds light (lifts blacks)." };
    for (int r = 0; r < 4; r++)
        for (int p = 0; p < 5; p++) {
            keys[r][p] = std::string(regions[r]) + params[p];
            float d = p == 4 ? 0.0f : 1.0f;
            add({ keys[r][p].c_str(), VEC4, V4(d, d, d, d), p == 4 ? -0.5f : 0.0f, p == 4 ? 0.5f : (p == 0 ? 2.0f : 4.0f),
                groups[r], labels[p], tips[p], nullptr, nullptr, 0 });
        }
    add({ "shadows_max", FLOAT, V1(0.09f), 0, 1, "Color Grading|Misc", "Shadows max",
        "Linear luminance where the shadows' grade has faded out.", nullptr, nullptr, 0 });
    add({ "highlights_min", FLOAT, V1(0.5f), 0, 2, "Color Grading|Misc", "Highlights min",
        "Linear luminance where the highlights' grade starts.", nullptr, nullptr, 0 });
    add({ "lut", TEXT, V1(0), 0, 0, "Color Grading|Misc", "LUT (.cube)",
        "A 3D LUT file (Adobe/Resolve .cube) applied after tone mapping: pc/user/post/luts/ or pc/assets/post/luts/, or a path.",
        nullptr, nullptr, 0 });
    add({ "lut_intensity", FLOAT, V1(1), 0, 1, "Color Grading|Misc", "LUT intensity", "", nullptr, nullptr, 0 });
    /* Film. */
    add({ "tonemapper", ENUM, V1(0), 0, PC_TONEMAP_COUNT - 1, "Film|Tonemapper", "Tonemapper",
        "None keeps the original picture (the lit image's soft shoulder). The others take the lit image as linear HDR.",
        TONEMAP_NAMES, TONEMAP_LABELS, 0 });
    add({ "tonemap_white", FLOAT, V1(4), 1, 16, "Film|Tonemapper", "White point",
        "Linear brightness that maps to white.", nullptr, nullptr, LOG });
    add({ "shoulder_start", FLOAT, V1(0.8f), 0.5f, 1, "Film|Tonemapper", "Soft shoulder start",
        "The original picture's highlights (tonemapper None): above this brightness lit colours bend toward white\n"
        "instead of clipping (1: a plain clip). Keep start + width = 1 for a smooth curve.", nullptr, nullptr, 0 });
    add({ "shoulder_width", FLOAT, V1(0.2f), 0.01f, 0.5f, "Film|Tonemapper", "Soft shoulder width",
        "How softly the shoulder rolls off (the headroom it compresses toward white).", nullptr, nullptr, 0 });
    add({ "grain", FLOAT, V1(0), 0, 1, "Film|Grain", "Grain intensity",
        "Film grain on the 3D: changes every game frame, stays put on interpolated frames and redraws.", nullptr, nullptr,
        0 });
    add({ "grain_size", FLOAT, V1(1.5f), 0.5f, 4, "Film|Grain", "Grain size", "Pixels of a 1080-line picture.", nullptr,
        nullptr, 0 });
    add({ "grain_shadows", FLOAT, V1(1), 0, 2, "Film|Grain", "Response: shadows", "", nullptr, nullptr, 0 });
    add({ "grain_midtones", FLOAT, V1(1), 0, 2, "Film|Grain", "Response: midtones", "", nullptr, nullptr, 0 });
    add({ "grain_highlights", FLOAT, V1(0.4f), 0, 2, "Film|Grain", "Response: highlights", "", nullptr, nullptr, 0 });
    add({ "grain_color", FLOAT, V1(0), 0, 1, "Film|Grain", "Colour grain", "0 monochrome, 1 per channel.", nullptr,
        nullptr, 0 });
    int offset = 0;
    for (Field& x : f) {
        x.offset = offset;
        offset += x.count();
    }
    return f;
}
#undef V1
#undef V3
#undef V4

constexpr int MAX_VALUES = 224;
int field_index(const std::string& key)
{
    static std::map<std::string, int> index;
    if (index.empty())
        for (size_t i = 0; i < fields().size(); i++)
            index[fields()[i].key] = (int)i;
    auto it = index.find(key);
    return it == index.end() ? -1 : it->second;
}
const Field& F(const char* key) { return fields()[field_index(key)]; }

struct Values {
    float f[MAX_VALUES];
    std::string lut;
};
Values defaults()
{
    Values v;
    memset(v.f, 0, sizeof v.f);
    for (const Field& x : fields())
        for (int i = 0; i < x.count(); i++)
            v.f[x.offset + i] = x.def[i];
    return v;
}
float get(const Values& v, const char* key, int i = 0) { return v.f[F(key).offset + i]; }

/* A layer: the fields it sets (Unreal's per-field override flags) and an optional preset under them. */
struct Layer {
    Values v = defaults();
    std::vector<bool> set = std::vector<bool>(fields().size(), false);
    std::string preset;
    bool any() const { return !preset.empty() || std::find(set.begin(), set.end(), true) != set.end(); }
    void clear()
    {
        v = defaults();
        set.assign(fields().size(), false);
        preset.clear();
    }
};
void apply(Values& to, const Layer& l)
{
    for (size_t i = 0; i < fields().size(); i++) {
        if (!l.set[i])
            continue;
        const Field& x = fields()[i];
        if (x.kind == TEXT)
            to.lut = l.v.lut;
        for (int k = 0; k < x.count(); k++)
            to.f[x.offset + k] = l.v.f[x.offset + k];
    }
}

/* ---- values as text ---- */
std::string number(double v) { return lighting::format_number(v, 4); }
std::string field_text(const Field& x, const Values& v)
{
    const float* p = &v.f[x.offset];
    switch (x.kind) {
    case BOOL:
        return p[0] != 0 ? "1" : "0";
    case ENUM: {
        int n = (int)p[0];
        const char* name = x.names;
        for (int i = 0; i < n && *name; i++)
            name += strlen(name) + 1;
        return *name ? name : number(n);
    }
    case COLOR:
        return number(p[0]) + "," + number(p[1]) + "," + number(p[2]);
    case VEC4:
        return number(p[0]) + "," + number(p[1]) + "," + number(p[2]) + "," + number(p[3]);
    case TEXT:
        return v.lut;
    default:
        return number(p[0]);
    }
}
/* Parse `text` into the field; false when it does not fit. */
bool field_parse(const Field& x, Values& v, std::string text)
{
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' || text.back() == '\t'))
        text.pop_back();
    size_t a = text.find_first_not_of(" \t");
    text = a == std::string::npos ? std::string() : text.substr(a);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = text.substr(1, text.size() - 2);
    float* p = &v.f[x.offset];
    if (x.kind == TEXT) {
        v.lut = text;
        return true;
    }
    if (x.kind == BOOL && (text == "true" || text == "on" || text == "false" || text == "off")) {
        p[0] = text == "true" || text == "on" ? 1.0f : 0.0f;
        return true;
    }
    if (x.kind == ENUM) {
        int i = 0;
        for (const char* name = x.names; *name; name += strlen(name) + 1, i++)
            if (text == name) {
                p[0] = (float)i;
                return true;
            }
    }
    std::vector<double> n;
    for (char& c : text)
        if (c == ' ')
            c = ',';
    const char* s = text.c_str();
    while (*s) {
        char* end;
        double d = strtod(s, &end);
        if (end == s || !isfinite(d))
            return false;
        n.push_back(d);
        s = end;
        while (*s == ',')
            s++;
    }
    if (n.empty())
        return false;
    if (x.kind == VEC4 && n.size() == 1) { /* the master alone */
        p[3] = (float)n[0];
        return true;
    }
    if ((int)n.size() < x.count())
        return false;
    for (int i = 0; i < x.count(); i++)
        p[i] = (float)n[i];
    if (x.kind == ENUM || x.kind == BOOL)
        p[0] = (float)(int)fminf(fmaxf(p[0], x.lo), x.hi);
    return true;
}
/* key=value into a layer (the preset key names a preset under it). */
bool layer_set(Layer& l, const std::string& key, const std::string& value)
{
    if (key == "preset") {
        l.preset = value;
        return true;
    }
    int i = field_index(key);
    if (i < 0)
        return false;
    if (!field_parse(fields()[i], l.v, value))
        return false;
    l.set[i] = true;
    return true;
}

/* ------------------------------------------------------------ presets */

struct Preset {
    std::string name, title, description, path;
    Layer layer;
    bool builtin = false;
};
std::string user_dir()
{
    const char* d = getenv("ROOD_POST_PRESETS");
    return d && *d ? d : "pc/user/post";
}
const char* builtin_dir() { return "pc/assets/post"; }
std::map<std::string, Preset> presets;
long long presets_stamp = -1;
void pc_list_dir(const char* dir, std::vector<std::string>* out)
{
    out->clear();
    DIR* d = opendir(dir);
    if (!d)
        return;
    while (struct dirent* e = readdir(d))
        if (e->d_name[0] != '.')
            out->push_back(e->d_name);
    closedir(d);
    std::sort(out->begin(), out->end());
}
long long dir_stamp(const std::string& dir)
{
    long long stamp = 0;
    std::vector<std::string> names;
    pc_list_dir(dir.c_str(), &names);
    for (const std::string& n : names) {
        struct stat st;
        if (!stat((dir + "/" + n).c_str(), &st))
            stamp += PC_MTIME_NS(st) % 1000000007 + (long long)n.size();
    }
    return stamp + (long long)names.size();
}
std::string read_text(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return "";
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        s.append(buf, n);
    fclose(f);
    return s;
}
std::string json_value_text(const prefab::Json& j)
{
    if (j.type == prefab::Json::Bool)
        return j.boolean ? "1" : "0";
    if (j.type == prefab::Json::Number)
        return number(j.number);
    if (j.type == prefab::Json::String)
        return j.string;
    if (j.type == prefab::Json::Array) {
        std::string s;
        for (size_t i = 0; i < j.items.size(); i++)
            s += (i ? "," : "") + number(j.items[i].number);
        return s;
    }
    return "";
}
void load_dir(const std::string& dir, bool builtin)
{
    std::vector<std::string> names;
    pc_list_dir(dir.c_str(), &names);
    for (const std::string& file : names) {
        if (file.size() < 6 || file.substr(file.size() - 5) != ".json")
            continue;
        Preset p;
        p.name = file.substr(0, file.size() - 5);
        p.path = dir + "/" + file;
        p.builtin = builtin;
        prefab::Json j;
        std::string error;
        if (!prefab::json_parse(read_text(p.path).c_str(), j, &error) || j.type != prefab::Json::Object) {
            fprintf(stderr, "[post] %s: %s\n", p.path.c_str(), error.c_str());
            continue;
        }
        if (const prefab::Json* t = j.get("title"))
            p.title = t->string;
        if (const prefab::Json* d = j.get("description"))
            p.description = d->string;
        if (const prefab::Json* v = j.get("values"))
            for (const auto& m : v->members)
                if (!layer_set(p.layer, m.first, json_value_text(m.second)))
                    fprintf(stderr, "[post] %s: unknown or bad field %s\n", p.path.c_str(), m.first.c_str());
        p.layer.preset.clear(); /* presets do not nest */
        presets[p.name] = p;
    }
}
void presets_refresh(bool force = false)
{
    long long stamp = dir_stamp(builtin_dir()) * 31 + dir_stamp(user_dir());
    if (!force && stamp == presets_stamp)
        return;
    presets_stamp = stamp;
    presets.clear();
    load_dir(builtin_dir(), true);
    load_dir(user_dir(), false); /* a user preset of the same name wins */
}
/* Lens presets (pc/assets/post/lens, yours in pc/user/post/lens): looks for
 * the depth of field and its bokeh alone. Applying one writes its fields into
 * a layer (lens_preset_apply), unlike a layer's preset, so the player's own
 * tone mapping, bloom and the rest stay as they are. */
std::map<std::string, Preset> lens_presets;
long long lens_presets_stamp = -1;
void lens_presets_refresh()
{
    std::string b = std::string(builtin_dir()) + "/lens", u = user_dir() + "/lens";
    long long stamp = dir_stamp(b) * 31 + dir_stamp(u);
    if (stamp == lens_presets_stamp)
        return;
    lens_presets_stamp = stamp;
    std::map<std::string, Preset> keep;
    keep.swap(presets);
    load_dir(b, true);
    load_dir(u, false);
    lens_presets.swap(presets);
    presets.swap(keep);
}
/* The fields a lens preset owns: third-person depth of field and bokeh (not
 * the first-person ones, not tilt-shift). */
bool lens_field(const std::string& key)
{
    return (key.compare(0, 3, "dof") == 0 && key.compare(0, 6, "dof_fp") != 0) || key.compare(0, 5, "bokeh") == 0;
}
bool lens_preset_apply(Layer& l, const std::string& name, std::string* error)
{
    lens_presets_refresh();
    auto it = lens_presets.find(name);
    if (it == lens_presets.end()) {
        *error = "no lens preset " + name;
        return false;
    }
    const Layer& p = it->second.layer;
    for (size_t i = 0; i < fields().size(); i++) {
        const Field& x = fields()[i];
        if (!lens_field(x.key))
            continue;
        l.set[i] = p.set[i];
        for (int c = 0; c < x.count(); c++)
            l.v.f[x.offset + c] = p.set[i] ? p.v.f[x.offset + c] : x.def[c];
    }
    return true;
}
const Layer* preset_layer(const std::string& name)
{
    if (name.empty())
        return nullptr;
    presets_refresh();
    auto it = presets.find(name);
    return it == presets.end() ? nullptr : &it->second.layer;
}
bool save_preset(const std::string& name, const Layer& l, std::string* message)
{
    for (char c : name)
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) {
            *message = "preset names take letters, digits, '_' and '-'";
            return false;
        }
    if (name.empty()) {
        *message = "no name";
        return false;
    }
    Values v = defaults();
    if (const Layer* below = preset_layer(l.preset))
        apply(v, *below);
    apply(v, l);
    prefab::Json j = prefab::Json::object(), values = prefab::Json::object();
    j.set("title", prefab::Json::str(name));
    for (size_t i = 0; i < fields().size(); i++) {
        const Field& x = fields()[i];
        bool from_preset = false;
        if (const Layer* below = preset_layer(l.preset))
            from_preset = below->set[i];
        if (!l.set[i] && !from_preset)
            continue;
        if (x.kind == TEXT)
            values.set(x.key, prefab::Json::str(v.lut));
        else if (x.kind == ENUM)
            values.set(x.key, prefab::Json::str(field_text(x, v)));
        else if (x.kind == BOOL)
            values.set(x.key, prefab::Json::boolean_(v.f[x.offset] != 0));
        else if (x.count() == 1)
            values.set(x.key, prefab::Json::num(v.f[x.offset]));
        else {
            prefab::Json a = prefab::Json::array();
            for (int k = 0; k < x.count(); k++)
                a.items.push_back(prefab::Json::num(v.f[x.offset + k]));
            values.set(x.key, a);
        }
    }
    j.set("values", values);
    std::string dir = user_dir();
    pc_mkdir(dir.c_str());
    std::string path = dir + "/" + name + ".json";
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        *message = "cannot write " + path;
        return false;
    }
    std::string text = prefab::json_write(j) + "\n";
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    presets_refresh(true);
    *message = "saved " + path;
    return true;
}

/* ------------------------------------------------------------ layers */

Layer user_gameplay, user_cutscene; /* settings post_*, post_cutscene_* */
Layer room_gameplay, room_cutscene; /* `post room`, `post cutscene` items */
std::map<int, Layer> room_events; /* `post event.<id>` */
bool room_gameplay_defined, room_cutscene_defined;
float blend_time = 1.0f; /* settings post_blend_time: seconds */

Layer cutscene_builtin()
{
    Layer l;
    layer_set(l, "dof", "0");
    layer_set(l, "dof_fp", "0");
    layer_set(l, "tilt_shift", "0");
    return l;
}
void apply_with_preset(Values& v, const Layer& l)
{
    if (const Layer* p = preset_layer(l.preset))
        apply(v, *p);
    apply(v, l);
}

/* ---- context ---- */
int context_now = PC_POST_GAMEPLAY, event_now = -1;
unsigned event_clear_since;
void update_context(unsigned vsync)
{
    int running = pc_battle_event_running && pc_battle_event_running();
    if (running) {
        context_now = PC_POST_CUTSCENE;
        event_now = pc_battle_event_id ? pc_battle_event_id() : -1;
        event_clear_since = vsync;
    } else if (context_now == PC_POST_CUTSCENE && vsync - event_clear_since > 10) {
        /* clear for 10 vsyncs: scripts run back to back without a gap of play */
        context_now = PC_POST_GAMEPLAY;
        event_now = -1;
    }
}
int effective_context() { return pc_post_force_context >= 0 ? pc_post_force_context : context_now; }

/* Lens blur (depth of field, tilt-shift) belongs to field play only: never on the title, in an attract
 * demo, in a menu, or without Ashley (nothing to focus on); in a cutscene only when a cutscene layer turns
 * it on. Returns why it is off ("" when it may be on). */
const char* lens_blur_gate(int context)
{
    if (&pc_game_phase && pc_game_phase == 1)
        return "title";
    if (&pc_game_phase && pc_game_phase == 2)
        return "attract";
    if (((pc_battle_field_menu_open && pc_battle_field_menu_open()) || (pc_ui_menu_active && pc_ui_menu_active()))
        && !(pc_option_peek_active && pc_option_peek_active())) /* (the Option pages' preview shows the field) */
        return "menu";
    if (context == PC_POST_CUTSCENE)
        return "";
    struct pc_dev_state s;
    memset(&s, 0, sizeof s);
    if (pc_dev_state)
        pc_dev_state(&s);
    return s.has_player ? "" : "no_player";
}
const char* gate_now = "";
bool frame_menu_model; /* the last present showed the menu's model view (neutral; RPC post menu_model) */

/* Plugins' overrides (rood.post): one layer per owner, in the order they began; never saved. */
std::vector<std::pair<std::string, Layer>> overrides;

Values resolve(int context)
{
    Values v = defaults();
    if (!pc_post_stack)
        return v;
    apply_with_preset(v, user_gameplay);
    apply_with_preset(v, room_gameplay);
    if (context == PC_POST_CUTSCENE) {
        apply(v, cutscene_builtin());
        apply_with_preset(v, user_cutscene);
        apply_with_preset(v, room_cutscene);
        auto e = room_events.find(event_now);
        if (e != room_events.end())
            apply_with_preset(v, e->second);
    }
    for (const auto& o : overrides)
        apply_with_preset(v, o.second); /* (a preset under the owner's own: a photo mode's filter) */
    gate_now = lens_blur_gate(context);
    if (*gate_now) {
        v.f[F("dof").offset] = 0;
        v.f[F("dof_fp").offset] = 0;
        v.f[F("tilt_shift").offset] = 0;
    }
    return v;
}

/* ---- blending ---- */
Values shown = defaults(), from = defaults(), target = defaults();
float tone_from = 0; /* the tonemapper blended from */
unsigned blend_start;
bool blending, started;
int last_context = -1;
float blend_progress = 1;

bool same(const Values& a, const Values& b) { return !memcmp(a.f, b.f, sizeof a.f) && a.lut == b.lut; }
bool enum_like(const Field& x) { return x.kind == ENUM || x.kind == TEXT; }

/* ---- camera cuts (as gpu_hw.c's interpolation tells them) ---- */
float last_r[3][3], last_t[3];
bool have_camera;
bool camera_cut()
{
    float r[3][3], t[3], h, ox, oy;
    if (!pc_scene_camera_get(r, t, &h, &ox, &oy))
        return false;
    bool cut = false;
    if (have_camera) {
        float turn = 0, move = 0, size = 0;
        for (int i = 0; i < 3; i++) {
            move += (last_t[i] - t[i]) * (last_t[i] - t[i]);
            size += t[i] * t[i];
            for (int k = 0; k < 3; k++)
                turn = fmaxf(turn, fabsf(last_r[i][k] - r[i][k]));
        }
        cut = !(turn < 0.2f && move < 0.04f * size + 64.0f * 64.0f);
    }
    memcpy(last_r, r, sizeof r);
    memcpy(last_t, t, sizeof t);
    have_camera = true;
    return cut;
}

/* ---- focus ---- */
float focus_eased = -1;
int focus_room = -1, focus_mode = -1;
bool focus_fp, focus_snap;
enum { FOCUS_ASHLEY, FOCUS_CENTRE, FOCUS_MANUAL, FOCUS_CURSOR, FOCUS_SUBJECT };
/* The focus probe's latest result (pc_post_focus_result): the view depth (GTE SZ, <= 0 none) and the share
 * of 3D around the point; results still in flight from before a jump are skipped. */
float probe_sz = -1, probe_share;
int probe_skip;
/* Cursor focus: the mouse over the picture (held when it leaves), or a point the RPC set. */
float cursor_uv[2] = { 0.5f, 0.5f }, rpc_point[2];
bool rpc_point_set;
void cursor_point(float out[2])
{
    if (rpc_point_set) {
        out[0] = rpc_point[0], out[1] = rpc_point[1];
        return;
    }
    if (ImGui::GetCurrentContext() && &pc_gl_view && pc_gl_view[2] > 0 && pc_gl_view[3] > 0) {
        ImVec2 m = ImGui::GetIO().MousePos;
        float u = (m.x - pc_gl_view[0]) / pc_gl_view[2], w = (m.y - pc_gl_view[1]) / pc_gl_view[3];
        if (u >= 0 && u <= 1 && w >= 0 && w <= 1)
            cursor_uv[0] = u, cursor_uv[1] = w;
    }
    out[0] = cursor_uv[0], out[1] = cursor_uv[1];
}
/* The scene's scale for the physical camera: Ashley's height in world units (her standing model, soles to
 * the top of her head, measured from the save room's picture and depth); dof_ashley_height gives it in
 * metres. */
constexpr float ASHLEY_HEIGHT_UNITS = 180.0f;
/* The lens this present used (RPC `post` lens, the panel's readout). */
struct Lens {
    bool physical, first_person;
    float focal_mm, fstop, sensor_mm, focus_m, far_px, cap;
} lens;
bool bokeh_test; /* the bokeh test card (dof.glsl dof_test_card) */
/* Ashley's chest depth (GTE SZ), or <= 0 when unknown. */
float ashley_depth()
{
    struct pc_dev_state s;
    float r[3][3], t[3], h, ox, oy;
    if (!pc_dev_state || !pc_scene_camera_get(r, t, &h, &ox, &oy))
        return -1;
    memset(&s, 0, sizeof s);
    pc_dev_state(&s);
    if (!s.has_player)
        return -1;
    return r[2][0] * s.x + r[2][1] * (s.y - 96) + r[2][2] * s.z + t[2];
}

/* ---- the camera subject (dof_focus subject) ----
 * Whom the game's camera frames, from the game's state (read only; dev_state.c, scene_submit.inc):
 *   - BATTLE's action state (D_800F196C 2), _cameraMode 4 to 10: from the wind-up on, the action camera
 *     (_handleCombatCameraZoom) steps toward func_800780A8's point, halfway between the performer and its
 *     first target (or its aim point), and holds the performer through the cast, the release, the chain
 *     steps and the hits; 11 is the camera's return to Ashley. That look-at is no actor's: the subject is
 *     the performer (the caster), unless the rules say its target, the look-at itself or Ashley.
 *   - the same state's modes 1 and 3 with a target candidate chosen (the targeting sphere: func_8006E640
 *     steps the camera toward it) and mode 2's area point: the target.
 *   - anything else (field play, the camera following Ashley): Ashley.
 * The rules (dof_subject_*) are post fields, so any layer sets them; Situation -> Choice is one pure
 * function (pick_subject) so a plugin can replace it later without touching how depth is measured. */
struct Situation {
    bool action; /* an action camera (mode 4..10) */
    int performer, target; /* actor slots (-1 none) */
    bool targeting, area; /* modes 1 / 3 with a candidate; mode 2 */
};
enum Choice { CHOOSE_ASHLEY, CHOOSE_ACTOR, CHOOSE_TARGET, CHOOSE_LOOK_AT, CHOOSE_CANDIDATE, CHOOSE_AREA };
struct Rules {
    int others, ashley; /* 0 the actor, 1 its target, 2 the look-at, 3 Ashley */
    bool keep_ashley; /* an action aimed at Ashley keeps her */
    int targeting; /* 0 the target, 1 Ashley */
};
Choice pick_subject(const Situation& s, const Rules& r, const char** why)
{
    *why = "field";
    if (s.action && s.performer >= 0) {
        *why = s.performer == 0 ? "ashley_action" : "action";
        if (s.performer != 0 && r.keep_ashley && s.target == 0) {
            *why = "targets_ashley";
            return CHOOSE_ASHLEY;
        }
        switch (s.performer == 0 ? r.ashley : r.others) {
        case 0: return s.performer == 0 ? CHOOSE_ASHLEY : CHOOSE_ACTOR;
        case 1: return s.target >= 0 ? CHOOSE_TARGET : CHOOSE_LOOK_AT; /* a point aimed at: the camera frames it */
        case 2: return CHOOSE_LOOK_AT;
        default: return CHOOSE_ASHLEY;
        }
    }
    if ((s.targeting || s.area) && r.targeting == 0) {
        *why = "targeting";
        return s.targeting ? CHOOSE_CANDIDATE : CHOOSE_AREA;
    }
    return CHOOSE_ASHLEY;
}
/* The subject this present (RPC `post` subject, the panel's readout). */
struct SubjectNow {
    const char* kind = "ashley"; /* ashley, actor (the performer), target, look_at, point (an area's) */
    const char* why = "field";
    int actor = 0; /* its actor slot (-1: a point) */
    float depth = -1; /* its view depth (GTE SZ; <= 0 unknown) */
    bool fallback; /* the choice had no depth: Ashley instead */
    int state = -1, mode = 0, performer = -1, target = -1; /* the game's: D_800F196C, _cameraMode, the action's */
} subject_now;
std::string subject_key; /* what the focus follows: a change eases over dof_subject_speed */
bool subject_pulling;
/* A world point's view depth (GTE SZ), or <= 0 when there is no camera. */
float point_depth(const float p[3])
{
    float r[3][3], t[3], h, ox, oy;
    if (!pc_scene_camera_get(r, t, &h, &ox, &oy))
        return -1;
    return r[2][0] * p[0] + r[2][1] * p[1] + r[2][2] * p[2] + t[2];
}
/* An actor's chest (its feet 96 units up, as Ashley's: ashley_depth), or false when its slot is empty. */
bool actor_chest(int slot, float out[3])
{
    struct pc_dev_actor actors[17];
    int n = pc_dev_actors ? pc_dev_actors(actors, 17) : 0;
    for (int i = 0; i < n; i++)
        if (actors[i].slot == slot) {
            out[0] = actors[i].pos[0], out[1] = actors[i].pos[1] - 96, out[2] = actors[i].pos[2];
            return true;
        }
    return false;
}
/* The camera subject's depth now (and subject_now); falls back to Ashley's whenever the subject's is unknown. */
float subject_depth(const Values& v)
{
    Situation sit = { false, -1, -1, false, false };
    struct pc_dev_camera cam;
    struct pc_dev_action act;
    memset(&cam, 0, sizeof cam);
    memset(&act, 0, sizeof act);
    bool have_cam = pc_dev_battle_camera && pc_dev_battle_camera(&cam);
    if (have_cam && cam.state == 2 && cam.mode >= 4 && cam.mode <= 10 && pc_dev_battle_action
        && pc_dev_battle_action(&act)) {
        sit.action = true;
        sit.performer = act.actor;
        sit.target = act.target;
    } else if (have_cam && cam.state == 2) {
        sit.targeting = cam.candidate != 0;
        sit.area = cam.area != 0;
    }
    Rules rules = { (int)get(v, "dof_subject_others"), (int)get(v, "dof_subject_ashley"),
        get(v, "dof_subject_keep_ashley") > 0.5f, (int)get(v, "dof_subject_targeting") };
    SubjectNow now;
    if (have_cam)
        now.state = cam.state, now.mode = cam.mode;
    now.performer = sit.performer, now.target = sit.target;
    Choice c = pick_subject(sit, rules, &now.why);
    float p[3];
    bool have = false;
    switch (c) {
    case CHOOSE_ACTOR:
    case CHOOSE_TARGET:
        now.kind = c == CHOOSE_ACTOR ? "actor" : "target";
        now.actor = c == CHOOSE_ACTOR ? sit.performer : sit.target;
        have = now.actor != 0 && actor_chest(now.actor, p);
        if (now.actor == 0) /* Ashley: her own tracked depth */
            now.kind = "ashley";
        break;
    case CHOOSE_LOOK_AT:
        now.kind = "look_at", now.actor = -1;
        memcpy(p, cam.look_at, sizeof p);
        have = true;
        break;
    case CHOOSE_CANDIDATE: /* the targeted actor, at the body part (or the point) the camera turns to */
        now.kind = "target", now.actor = cam.candidate_actor;
        memcpy(p, cam.candidate_pos, sizeof p);
        have = true;
        break;
    case CHOOSE_AREA:
        now.kind = "point", now.actor = -1;
        memcpy(p, cam.area_pos, sizeof p);
        have = true;
        break;
    default:
        break;
    }
    float d = have ? point_depth(p) : -1;
    if (!(d > 16)) { /* Ashley (her choice, or no subject known) */
        now.fallback = strcmp(now.kind, "ashley") != 0;
        if (now.fallback)
            now.why = "fallback";
        now.kind = "ashley", now.actor = 0;
        d = ashley_depth();
    }
    now.depth = d;
    subject_now = now;
    return d;
}

/* ---- auto exposure ---- */
float ev_auto = 0, meter_mean = -100, meter_weight = 0;
bool meter_fresh;

/* ---- LUT ---- */
std::string lut_loaded_name = "\x01";
std::vector<unsigned char> lut_strip;
int lut_size;
unsigned lut_serial;
std::string lut_error;
std::string lut_path(const std::string& name)
{
    struct stat st;
    for (std::string dir : { user_dir() + "/luts", std::string(builtin_dir()) + "/luts" }) {
        std::string p = dir + "/" + name;
        if (!stat(p.c_str(), &st))
            return p;
    }
    return name;
}
void lut_load(const std::string& name)
{
    if (name == lut_loaded_name)
        return;
    lut_loaded_name = name;
    lut_strip.clear();
    lut_size = 0;
    lut_error.clear();
    ++lut_serial;
    if (name.empty())
        return;
    std::string text = read_text(lut_path(name));
    if (text.empty()) {
        lut_error = "cannot read " + lut_path(name);
        fprintf(stderr, "[post] LUT: %s\n", lut_error.c_str());
        return;
    }
    int n = 0;
    float lo[3] = { 0, 0, 0 }, hi[3] = { 1, 1, 1 };
    std::vector<float> rgb;
    const char* p = text.c_str();
    while (*p) {
        const char* end = strchr(p, '\n');
        std::string line(p, end ? (size_t)(end - p) : strlen(p));
        p = end ? end + 1 : p + line.size();
        if (line.empty() || line[0] == '#')
            continue;
        float a, b, c;
        if (!strncmp(line.c_str(), "LUT_3D_SIZE", 11))
            n = atoi(line.c_str() + 11);
        else if (!strncmp(line.c_str(), "DOMAIN_MIN", 10))
            sscanf(line.c_str() + 10, "%f %f %f", &lo[0], &lo[1], &lo[2]);
        else if (!strncmp(line.c_str(), "DOMAIN_MAX", 10))
            sscanf(line.c_str() + 10, "%f %f %f", &hi[0], &hi[1], &hi[2]);
        else if (sscanf(line.c_str(), "%f %f %f", &a, &b, &c) == 3)
            rgb.insert(rgb.end(), { a, b, c });
    }
    if (n < 2 || n > 128 || (int)rgb.size() != n * n * n * 3) {
        lut_error = name + ": not a 3D .cube LUT (LUT_3D_SIZE and N^3 rows)";
        fprintf(stderr, "[post] LUT: %s\n", lut_error.c_str());
        return;
    }
    lut_size = n;
    lut_strip.assign((size_t)n * n * n * 4, 255);
    for (int b = 0; b < n; b++)
        for (int g = 0; g < n; g++)
            for (int r = 0; r < n; r++) {
                const float* s = &rgb[((size_t)(b * n + g) * n + r) * 3];
                unsigned char* d = &lut_strip[((size_t)g * n * n + b * n + r) * 4];
                for (int k = 0; k < 3; k++) {
                    float v = (s[k] - lo[k]) / fmaxf(hi[k] - lo[k], 1e-6f);
                    d[k] = (unsigned char)lrintf(fminf(fmaxf(v, 0.0f), 1.0f) * 255.0f);
                }
            }
}

/* ---- white balance (Bradford adaptation between two whites on the daylight / Planckian locus) ---- */
void locus_xy(float temperature, float tint, float* x, float* y)
{
    double T = fmin(fmax(temperature, 1000.0), 25000.0), xc;
    /* CIE daylight locus above 4000 K, the Planckian locus (Kim et al. cubic fit) below. */
    if (T >= 4000) {
        xc = T <= 7000 ? -4.6070e9 / (T * T * T) + 2.9678e6 / (T * T) + 0.09911e3 / T + 0.244063
                       : -2.0064e9 / (T * T * T) + 1.9018e6 / (T * T) + 0.24748e3 / T + 0.237040;
        *x = (float)xc;
        *y = (float)(-3.0 * xc * xc + 2.87 * xc - 0.275);
    } else {
        xc = T <= 1667 ? 0.0 : -0.2661239e9 / (T * T * T) - 0.2343589e6 / (T * T) + 0.8776956e3 / T + 0.179910;
        double yc = -1.1063814 * xc * xc * xc - 1.34811020 * xc * xc + 2.18555832 * xc - 0.20219683;
        if (T >= 2222)
            yc = -0.9549476 * xc * xc * xc - 1.37418593 * xc * xc + 2.09137015 * xc - 0.16748867;
        *x = (float)xc, *y = (float)yc;
    }
    if (tint != 0) { /* along the locus normal in CIE 1960 uv */
        double u = 4 * *x / (-2 * *x + 12 * *y + 3), v = 6 * *y / (-2 * *x + 12 * *y + 3);
        float x2, y2;
        locus_xy(temperature + 50, 0, &x2, &y2);
        double u2 = 4 * x2 / (-2 * x2 + 12 * y2 + 3), v2 = 6 * y2 / (-2 * x2 + 12 * y2 + 3);
        double du = u2 - u, dv = v2 - v, l = sqrt(du * du + dv * dv);
        if (l > 0) {
            u += -dv / l * tint * 0.05, v += du / l * tint * 0.05;
            *x = (float)(3 * u / (2 * u - 8 * v + 4)), *y = (float)(2 * v / (2 * u - 8 * v + 4));
        }
    }
}
void mul3(const float a[3][3], const float b[3][3], float out[3][3])
{
    float r[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    memcpy(out, r, sizeof r);
}
void white_balance(float temperature, float tint, float out[3][3])
{
    static const float rgb_xyz[3][3] = { { 0.4124564f, 0.3575761f, 0.1804375f }, { 0.2126729f, 0.7151522f, 0.0721750f },
        { 0.0193339f, 0.1191920f, 0.9503041f } };
    static const float xyz_rgb[3][3] = { { 3.2404542f, -1.5371385f, -0.4985314f }, { -0.9692660f, 1.8760108f, 0.0415560f },
        { 0.0556434f, -0.2040259f, 1.0572252f } };
    static const float bradford[3][3] = { { 0.8951f, 0.2664f, -0.1614f }, { -0.7502f, 1.7135f, 0.0367f },
        { 0.0389f, -0.0685f, 1.0296f } };
    static const float bradford_inv[3][3] = { { 0.9869929f, -0.1470543f, 0.1599627f },
        { 0.4323053f, 0.5183603f, 0.0492912f }, { -0.0085287f, 0.0400428f, 0.9684867f } };
    float xs, ys, xd, yd;
    locus_xy(temperature, tint, &xs, &ys);
    locus_xy(6500, 0, &xd, &yd);
    float ws[3] = { xs / ys, 1, (1 - xs - ys) / ys }, wd[3] = { xd / yd, 1, (1 - xd - yd) / yd };
    float ls[3], ld[3];
    for (int i = 0; i < 3; i++) {
        ls[i] = bradford[i][0] * ws[0] + bradford[i][1] * ws[1] + bradford[i][2] * ws[2];
        ld[i] = bradford[i][0] * wd[0] + bradford[i][1] * wd[1] + bradford[i][2] * wd[2];
    }
    float scale[3][3] = { { ld[0] / ls[0], 0, 0 }, { 0, ld[1] / ls[1], 0 }, { 0, 0, ld[2] / ls[2] } };
    float m[3][3];
    mul3(scale, bradford, m);
    mul3(bradford_inv, m, m);
    mul3(m, rgb_xyz, m);
    mul3(xyz_rgb, m, out);
}

bool grade_needed(const Values& v)
{
    Values d = defaults();
    static const char* const keys[] = { "exposure", "chromatic_aberration", "vignette", "grain", "tonemapper",
        "temperature", "tint", "sharpen" };
    for (const char* k : keys)
        if (get(v, k) != get(d, k))
            return true;
    if (get(v, "exposure_mode") == 1 || get(v, "bloom_intensity") > 0)
        return true;
    if (!v.lut.empty() && get(v, "lut_intensity") > 0)
        return true;
    for (const Field& x : fields())
        if (x.kind == VEC4)
            for (int k = 0; k < 4; k++)
                if (v.f[x.offset + k] != x.def[k])
                    return true;
    return false;
}

/* This present's resolved values. */
pc_post_frame frame;
Values frame_values;
float frame_tone_from, frame_tone_mix = 1;

} // namespace

/* ------------------------------------------------------------ room items (environment.cpp delegates) */

namespace rood_post {
void reset()
{
    room_gameplay.clear();
    room_cutscene.clear();
    room_events.clear();
    room_gameplay_defined = room_cutscene_defined = false;
}
bool apply_item(const lighting::Item& it)
{
    if (it.kind != "post")
        return false;
    Layer* l = nullptr;
    if (it.name == "room")
        l = &room_gameplay, room_gameplay_defined = true;
    else if (it.name == "cutscene")
        l = &room_cutscene, room_cutscene_defined = true;
    else if (!strncmp(it.name.c_str(), "event.", 6))
        l = &room_events[atoi(it.name.c_str() + 6)];
    if (!l)
        return true; /* a name this build does not know: kept by the layers, unused */
    l->clear();
    for (const auto& kv : it.fields)
        if (!layer_set(*l, kv.first, kv.second))
            fprintf(stderr, "[post] post %s: unknown or bad field %s=%s\n", it.name.c_str(), kv.first.c_str(),
                kv.second.c_str());
    return true;
}
static void add_layer(lighting::State& s, const char* name, const Layer& l)
{
    lighting::Item it;
    it.kind = "post", it.name = name;
    if (!l.preset.empty())
        it.set("preset", l.preset);
    for (size_t i = 0; i < fields().size(); i++)
        if (l.set[i])
            it.set(fields()[i].key, field_text(fields()[i], l.v));
    lighting::canonicalize(it);
    s[it.id()] = it;
}
void add_items(lighting::State& s)
{
    if (room_gameplay.any() || room_gameplay_defined)
        add_layer(s, "room", room_gameplay);
    if (room_cutscene.any() || room_cutscene_defined)
        add_layer(s, "cutscene", room_cutscene);
    for (const auto& e : room_events)
        add_layer(s, ("event." + std::to_string(e.first)).c_str(), e.second);
}
} // namespace rood_post

/* ------------------------------------------------------------ the frame */

extern "C" int pc_post_context(void) { return effective_context(); }
extern "C" int pc_post_event_id(void) { return event_now; }

extern "C" void pc_post_frame_get(pc_post_frame* out)
{
    rood_lights::track_room(); /* the room's `post` items */
    unsigned vsync = pc_vsync_count ? pc_vsync_count() : 0;
    static unsigned last_vsync;
    float dt = started ? (float)(vsync - last_vsync) / 59.94f : 0.0f;
    /* The photo mode's pause (photo.cpp): no vsync passes, the focus and the exposure follow real time. */
    static double last_real;
    double real = rl_now_seconds();
    if (pc_photo_holding && pc_photo_holding())
        dt = started && last_real > 0 ? (float)(real - last_real) : 0.0f;
    last_real = real;
    if (dt > 1.0f)
        dt = 1.0f;
    last_vsync = vsync;
    update_context(vsync);
    int context = effective_context();
    bool cut = camera_cut();
    Values t = resolve(context);
    /* The modern menu's Ashley model view (ui/ashley_bridge.c; gpu_hw.c tags the frames that hold it): the
     * model is the only 3D in the picture (the world is frozen and not drawn; the presenters show the
     * held picture of the world behind it, post-processed when it was shown). The model shows as it is:
     * every field neutral (no grade, bloom, vignette, grain, aberration, ...), switched at once both ways.
     * The game's screen fade still applies (below). */
    static bool last_menu_model;
    bool menu_model = pc_hw_present_menu_model && pc_hw_present_menu_model();
    if (menu_model)
        t = defaults();
    /* Blend toward a changed target: from what shows now, over blend_time; snap on a camera cut
     * that comes with a context change (a cutscene starting on a cut), on the first frame, and
     * when the master switch changed. */
    static int last_stack = -1, last_zone = -1, last_room = -1;
    /* Only a room change or a context change blends; an edit (settings, the editor, RPC) applies at once. */
    bool scene_change = context != last_context || rood_lights::zone != last_zone || rood_lights::room != last_room;
    last_zone = rood_lights::zone, last_room = rood_lights::room;
    bool snap = !started || pc_post_stack != last_stack || (cut && context != last_context) || blend_time <= 0
        || menu_model != last_menu_model;
    last_menu_model = menu_model;
    frame_menu_model = menu_model;
    if (!same(t, target) && !scene_change && !blending)
        snap = true;
    if (!same(t, target)) {
        from = shown;
        tone_from = blending ? (frame_tone_mix >= 0.5f ? get(target, "tonemapper") : frame_tone_from)
                             : get(shown, "tonemapper");
        target = t;
        blend_start = vsync;
        blending = true;
    }
    if (snap) {
        from = shown = target;
        blending = false;
    }
    last_stack = pc_post_stack;
    last_context = context;
    started = true;
    float tone_mix = 1;
    if (blending) {
        float s = blend_time > 0 ? (float)(vsync - blend_start) / 59.94f / blend_time : 1.0f;
        if (s >= 1) {
            shown = target;
            blending = false;
            blend_progress = 1;
        } else {
            s = s * s * (3 - 2 * s);
            blend_progress = s;
            for (const Field& x : fields()) {
                for (int k = 0; k < x.count(); k++) {
                    float a = from.f[x.offset + k], b = target.f[x.offset + k];
                    shown.f[x.offset + k] = enum_like(x) ? (s < 0.5f ? a : b) : a + (b - a) * s;
                }
            }
            shown.lut = s < 0.5f ? from.lut : target.lut;
            tone_mix = s;
        }
    } else
        blend_progress = 1;
    frame_values = shown;
    frame_tone_from = blending ? tone_from : get(shown, "tonemapper");
    frame_tone_mix = blending ? tone_mix : 1.0f;
    const Values& v = shown;

    pc_post_frame f;
    memset(&f, 0, sizeof f);
    f.context = context;
    /* Depth of field, tilt-shift, fog: blended on/off as amounts. The first-person view has its own depth
     * of field (dof_fp_*: focus on what the view looks at); the third-person one does not apply there. */
    bool fp = pc_battle_first_person && pc_battle_first_person();
    float dof = get(v, fp ? "dof_fp" : "dof"), tilt = get(v, "tilt_shift"), fog = get(v, "depth_fog");
    f.flags = (dof > 0.001f ? 1 : 0) | (tilt > 0.001f ? 2 : 0) | (fog > 0.001f ? 4 : 0);
    float cap = get(v, fp ? "dof_fp_max_blur" : "dof_max_blur");
    f.dof_radius = cap * dof;
    f.near_blur = get(v, fp ? "dof_fp_near_blur" : "dof_near_blur");
    f.dof_near = (f.flags & 1) && f.near_blur > 0.001f;
    f.tilt[0] = get(v, "tilt_centre"), f.tilt[1] = get(v, "tilt_width"), f.tilt[2] = get(v, "tilt_blur") * tilt;
    f.tilt_feather = fmaxf(get(v, "tilt_feather"), 1e-3f);
    f.shoulder[0] = get(v, "shoulder_start"), f.shoulder[1] = fmaxf(get(v, "shoulder_width"), 1e-3f);
    for (int k = 0; k < 3; k++)
        f.fog[k] = get(v, "depth_fog_color", k);
    f.fog[3] = get(v, "depth_fog_amount") * fog;
    f.fog_range[0] = get(v, "depth_fog_start"), f.fog_range[1] = get(v, "depth_fog_end");
    /* Focus: on Ashley from her tracked position (held while she is not there); the screen centre or the
     * cursor from the probe (the depth there, measured on the GPU, a few frames late); eased with time. A
     * camera cut, a room change, a change of mode or view snaps (to the probe's first fresh result). */
    int mode = fp ? FOCUS_CENTRE : (int)get(v, "dof_focus");
    struct pc_dev_state s;
    memset(&s, 0, sizeof s);
    if (pc_dev_state)
        pc_dev_state(&s);
    bool jump = cut || s.room != focus_room || fp != focus_fp || mode != focus_mode;
    focus_room = s.room, focus_fp = fp, focus_mode = mode;
    if (jump || mode != FOCUS_SUBJECT || !(f.flags & 1)) /* a cut snaps: no subject pull to finish */
        subject_pulling = false;
    if (mode != FOCUS_SUBJECT || !(f.flags & 1)) {
        subject_key.clear();
        subject_now = SubjectNow();
        subject_now.why = "";
    }
    if (!(f.flags & 1)) {
        focus_eased = probe_sz = -1;
    } else if (mode == FOCUS_MANUAL) {
        f.focus_sz = get(v, "dof_focus_depth");
        focus_eased = -1;
    } else {
        float d = -1;
        bool subject_pull = false; /* easing to a new camera subject: dof_subject_speed */
        if (mode == FOCUS_ASHLEY)
            d = ashley_depth();
        else if (mode == FOCUS_SUBJECT) {
            d = subject_depth(v);
            std::string key = std::string(subject_now.kind) + ":" + std::to_string(subject_now.actor);
            if (key != subject_key && !subject_key.empty())
                subject_pulling = true;
            subject_key = key;
            subject_pull = subject_pulling;
        } else {
            f.focus_probe = 1;
            if (mode == FOCUS_CURSOR)
                cursor_point(f.focus_uv);
            else
                f.focus_uv[0] = f.focus_uv[1] = 0.5f;
            f.focus_area = get(v, fp ? "dof_fp_focus_area" : "dof_focus_area");
            if (jump) { /* what is in flight was measured before */
                probe_sz = -1;
                probe_skip = 3;
            }
            d = probe_sz;
        }
        if (jump)
            focus_snap = true;
        if (d > 16) {
            float speed = get(v, fp ? "dof_fp_focus_speed" : subject_pull ? "dof_subject_speed" : "dof_focus_speed");
            if (focus_eased <= 0 || focus_snap || speed <= 0)
                focus_eased = d;
            else
                focus_eased += (d - focus_eased) * (1.0f - expf(-dt / speed));
            focus_snap = false;
            /* the subject change is done once the focus is there (then the ordinary pull time again) */
            if (subject_pulling && fabsf(d - focus_eased) <= 0.02f * d)
                subject_pulling = false;
        }
        f.focus_sz = focus_eased > 0 ? focus_eased : 0;
    }
    /* The blur: radius = dof_radius * clamp(aperture * (1 - focus / z), -1, 1) (the present shaders). The
     * physical camera is a thin lens: a blur circle of f^2 / (N (S1 - f)) * (1 - S1 / S2) on the sensor
     * (focal length f, f-number N, focus distance S1, the point's distance S2), the sensor's height being
     * the picture's; the artistic model takes the aperture as it is. */
    lens.physical = get(v, "dof_model") == 0;
    lens.first_person = fp;
    lens.cap = cap;
    if (lens.physical) {
        float r[3][3], t[3], h = 0, ox, oy;
        float sensor = get(v, "dof_sensor"), focal = get(v, "dof_focal_length");
        if (focal <= 0) { /* from the game's field of view: the projection distance H over the picture's lines */
            float lines = &pc_gl_display && pc_gl_display[3] > 0 ? (float)pc_gl_display[3] : 240.0f;
            focal = pc_scene_camera_get(r, t, &h, &ox, &oy) && h > 0 ? sensor * h / lines : 50.0f;
        }
        float focus = f.focus_sz;
        if (focus <= 0) { /* not measured yet (the shader picks the centre): Ashley, else a room's typical depth */
            float d = ashley_depth();
            focus = d > 16 ? d : 1024.0f;
        }
        float metres = get(v, "dof_ashley_height") / ASHLEY_HEIGHT_UNITS;
        float fstop = get(v, fp ? "dof_fp_fstop" : "dof_fstop");
        float s1 = fmaxf(focus * metres * 1000.0f, 2.0f * focal);
        float circle = focal * focal / (fstop * (s1 - focal)); /* its diameter in mm, at infinity */
        lens.focal_mm = focal, lens.fstop = fstop, lens.sensor_mm = sensor, lens.focus_m = s1 / 1000.0f;
        lens.far_px = circle / sensor * 720.0f * 0.5f; /* its radius in pixels of a 720-line picture */
        f.aperture = cap > 0.01f ? lens.far_px / cap : 0.0f;
    } else {
        f.aperture = get(v, "dof_aperture");
        lens.far_px = f.aperture * cap;
        lens.focus_m = 0;
    }
    /* Exposure. */
    float ev = get(v, "exposure");
    if (get(v, "exposure_mode") == 1) {
        f.meter = 1;
        if (meter_fresh && meter_weight > 0.01f) {
            float want = log2f(fmaxf(get(v, "auto_exposure_target"), 1e-4f)) - meter_mean;
            want = fminf(fmaxf(want, get(v, "auto_exposure_min")), get(v, "auto_exposure_max"));
            float speed = want > ev_auto ? get(v, "auto_exposure_speed_up") : get(v, "auto_exposure_speed_down");
            float step = speed * dt;
            ev_auto = fabsf(want - ev_auto) <= step ? want : ev_auto + (want > ev_auto ? step : -step);
        }
        ev += ev_auto;
    } else if (!menu_model) /* (the menu's model view keeps the world's adaptation for after it) */
        ev_auto = 0;
    f.exposure_scale = exp2f(ev);
    pc_post_active_ev = ev;
    /* The grade pass. */
    f.grade = grade_needed(v) || (blending && grade_needed(from));
    int tonemap = (int)get(v, "tonemapper");
    f.hdr = f.grade && (tonemap != PC_TONEMAP_NONE || (frame_tone_mix < 1 && frame_tone_from != PC_TONEMAP_NONE));
    if (f.grade && get(v, "bloom_intensity") > 0) {
        int method = (int)get(v, "bloom_method");
        f.bloom = method == PC_BLOOM_GAUSSIAN || !pc_post_fft_available ? PC_BLOOM_GAUSSIAN : PC_BLOOM_CONVOLUTION;
    }
    f.bloom_threshold = get(v, "bloom_threshold");
    f.bloom_knee = get(v, "bloom_knee");
    f.bloom_size = get(v, "bloom_size");
    f.bloom_kernel[0] = get(v, "bloom_kernel_streaks");
    f.bloom_kernel[1] = get(v, "bloom_kernel_streak_intensity");
    f.bloom_kernel[2] = get(v, "bloom_kernel_angle");
    f.bloom_kernel[3] = get(v, "bloom_kernel_falloff");
    f.bloom_kernel[4] = get(v, "bloom_kernel_core");
    if (f.grade && !v.lut.empty() && get(v, "lut_intensity") > 0) {
        lut_load(v.lut);
        f.lut = lut_size > 0;
    }
    /* The game's screen fade (a door, a cutscene): the 3D fades after lighting and post-processing in
     * the grade pass, whatever the profile (gpu_hw.c BLEND_FADE). */
    f.fade = pc_hw_present_fade ? pc_hw_present_fade(f.fade_rect) : 0.0f;
    if (f.fade > 0)
        f.grade = 1;
    if (!f.grade)
        f.meter = 0;
    frame = f;
    *out = f;
}

extern "C" void pc_post_meter_result(float mean_log2, float weight)
{
    meter_mean = mean_log2;
    meter_weight = weight;
    meter_fresh = true;
}

extern "C" void pc_post_meter_reduce(const float* texels, int w, int h, int stride)
{
    double sum = 0, share = 0, weights = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const float* t = texels + ((size_t)y * w + x) * stride;
            float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / h - 0.5f;
            double c = 1.0 - 1.2 * (dx * dx + dy * dy); /* centre-weighted metering */
            if (!isfinite(t[0]) || !isfinite(t[1]))
                continue;
            sum += t[0] * c, share += t[1] * c, weights += c;
        }
    pc_post_meter_result(share > 1e-6 ? (float)(sum / share) : 0.0f, weights > 0 ? (float)(share / weights) : 0.0f);
}

extern "C" void pc_post_focus_result(float mean_log2, float share)
{
    if (probe_skip > 0) { /* measured before a jump */
        probe_skip--;
        return;
    }
    probe_share = share;
    if (share > 0.05f && isfinite(mean_log2))
        probe_sz = exp2f(mean_log2);
}

extern "C" void pc_post_dof_pack(pc_post_dof_gpu* out, int lit_w, int lit_h)
{
    memset(out, 0, sizeof *out);
    const Values& v = frame_values;
    float (*g)[4] = out->v;
    float largest = fmaxf(frame.dof_radius, frame.tilt[2]) * lit_h / 720.0f * 0.5f; /* half-size pixels */
    g[1][1] = largest;
    g[1][2] = PC_DOF_TILE;
    g[1][3] = ceilf(largest / PC_DOF_TILE);
    g[2][0] = get(v, "bokeh_blades");
    g[2][1] = get(v, "bokeh_curvature");
    g[2][2] = get(v, "bokeh_rotation") * 3.14159265f / 180.0f;
    g[2][3] = get(v, "bokeh_squeeze");
    g[3][0] = get(v, "bokeh_edge");
    g[3][1] = get(v, "bokeh_edge_amount");
    g[3][2] = get(v, "bokeh_fringe");
    g[3][3] = get(v, "bokeh_cats_eye");
    g[4][0] = get(v, "bokeh_highlights");
    g[4][1] = fmaxf(get(v, "bokeh_samples"), 1.0f);
    g[4][2] = bokeh_test && (frame.flags & 1) ? 1.0f : 0.0f;
    if (g[4][2] != 0)
        g[4][0] = 0; /* the test card shows the kernel as it is (its dots are the highlights) */
    g[4][3] = lit_h > 0 ? (float)lit_w / lit_h : 1.0f;
}

extern "C" const unsigned char* pc_post_lut(int* size, unsigned* serial)
{
    *size = lut_size;
    *serial = lut_serial;
    return lut_size ? lut_strip.data() : nullptr;
}

extern "C" void pc_post_gpu_pack(pc_post_gpu* out, int lit_w, int lit_h, int view_w, int view_h, const float bloom_uv[4])
{
    memset(out, 0, sizeof *out);
    const Values& v = frame_values;
    float (*g)[4] = out->v;
    g[0][0] = frame.exposure_scale;
    g[0][1] = frame.bloom ? get(v, "bloom_intensity") : 0;
    /* Chromatic aberration: pixels at the corners of a 1080-line picture -> lit pixels. */
    g[0][2] = get(v, "chromatic_aberration") * lit_h / 1080.0f;
    g[0][3] = get(v, "chromatic_aberration_start");
    g[1][0] = get(v, "vignette");
    g[1][1] = get(v, "sharpen");
    g[1][2] = get(v, "grain");
    g[1][3] = get(v, "grain_size") * view_h / 1080.0f;
    g[2][0] = get(v, "grain_shadows"), g[2][1] = get(v, "grain_midtones"), g[2][2] = get(v, "grain_highlights");
    g[2][3] = get(v, "grain_color");
    g[3][0] = (float)(pc_hw_frame_serial() % 4096u);
    g[3][1] = (float)view_w, g[3][2] = (float)view_h, g[3][3] = view_h > 0 ? (float)view_w / view_h : 1.0f;
    g[4][0] = frame_tone_from;
    g[4][1] = get(v, "tonemapper");
    g[4][2] = frame_tone_mix;
    g[4][3] = get(v, "tonemap_white");
    float wb[3][3];
    bool wb_on = get(v, "temperature") != 6500 || get(v, "tint") != 0;
    if (wb_on)
        white_balance(get(v, "temperature"), get(v, "tint"), wb);
    else
        memset(wb, 0, sizeof wb), wb[0][0] = wb[1][1] = wb[2][2] = 1;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            g[5 + r][c] = wb[r][c];
    g[8][0] = get(v, "shadows_max");
    g[8][1] = get(v, "highlights_min");
    g[8][2] = frame.lut ? get(v, "lut_intensity") : 0;
    g[8][3] = frame.lut ? (float)lut_size : 0;
    /* Grading: each region's parameters with the global ones folded in (Unreal's
     * ColorCorrect: saturation, contrast, gamma, gain multiply; offsets add), per channel x master. */
    static const char* const params[5] = { "saturation", "contrast", "gamma", "gain", "offset" };
    static const char* const regions[3] = { "shadows_", "midtones_", "highlights_" };
    bool grading = false;
    for (int r = 0; r < 3; r++)
        for (int p = 0; p < 5; p++) {
            const float* gl = &v.f[F(params[p]).offset];
            const float* rg = &v.f[F((std::string(regions[r]) + params[p]).c_str()).offset];
            for (int c = 0; c < 3; c++) {
                float value = p == 4 ? gl[c] + gl[3] + rg[c] + rg[3] : gl[c] * gl[3] * rg[c] * rg[3];
                g[9 + r * 5 + p][c] = value;
                grading |= value != (p == 4 ? 0.0f : 1.0f);
            }
        }
    float tint[3] = { get(v, "bloom_tint", 0), get(v, "bloom_tint", 1), get(v, "bloom_tint", 2) };
    g[24][0] = tint[0], g[24][1] = tint[1], g[24][2] = tint[2];
    g[24][3] = (float)frame.bloom;
    if (bloom_uv)
        memcpy(g[25], bloom_uv, sizeof g[25]);
    g[26][0] = (float)lit_w, g[26][1] = (float)lit_h, g[26][2] = 1.0f / lit_w, g[26][3] = 1.0f / lit_h;
    g[27][0] = grading ? 1.0f : 0.0f;
    g[27][1] = frame.hdr ? 1.0f : 0.0f;
    g[27][2] = wb_on ? 1.0f : 0.0f;
    g[27][3] = g[0][2] > 0 ? 1.0f : 0.0f;
    g[28][0] = frame.fade;
    memcpy(g[29], frame.fade_rect, sizeof g[29]);
    g[30][0] = get(v, "sharpen_gain"), g[30][1] = get(v, "vignette_gain");
    g[30][2] = frame.shoulder[0], g[30][3] = frame.shoulder[1];
    g[31][0] = get(v, "vignette_roundness"), g[31][1] = get(v, "lens_distortion");
}

/* ------------------------------------------------------------ the convolution kernel */

extern "C" unsigned pc_post_kernel_hash(int n, int picture)
{
    float p[8] = { (float)n, (float)picture, frame.bloom_size, frame.bloom_kernel[0], frame.bloom_kernel[1],
        frame.bloom_kernel[2], frame.bloom_kernel[3], frame.bloom_kernel[4] };
    unsigned h = 2166136261u;
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < sizeof p; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
/* Our own lens kernel: a soft halo plus a power-law glare (a disc of light
 * scattered in the lens and the eye), optional diffraction streaks through
 * the centre, faded out before the domain's padding so nothing wraps into
 * the picture. Radially symmetric about texel (0, 0) (point-symmetric with
 * streaks): its spectrum is real. */
extern "C" void pc_post_kernel_build(float* out, int n, int picture)
{
    float size = fmaxf(frame.bloom_size, 0.05f), streaks = floorf(frame.bloom_kernel[0] / 2.0f + 0.5f) * 2.0f;
    float streak_share = streaks > 0 ? fminf(fmaxf(frame.bloom_kernel[1], 0.0f), 1.0f) : 0.0f;
    float angle = frame.bloom_kernel[2] * 3.14159265f / 180.0f, falloff = frame.bloom_kernel[3];
    float core = fminf(fmaxf(frame.bloom_kernel[4], 0.0f), 1.0f);
    float pad = (n - picture) * 0.5f; /* texels of padding around the picture */
    float halo_r = 0.012f * size * picture, glare_r = 0.004f * size * picture;
    float reach = fminf(pad, 0.5f * size * picture);
    double sums[3] = { 0, 0, 0 };
    std::vector<float> parts((size_t)n * n * 3);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float dx = (float)(x <= n / 2 ? x : x - n), dy = (float)(y <= n / 2 ? y : y - n);
            float r = sqrtf(dx * dx + dy * dy);
            float window = r >= reach ? 0.0f : r <= reach * 0.6f ? 1.0f : 0.5f + 0.5f * cosf((r - reach * 0.6f) / (reach * 0.4f) * 3.14159265f);
            float halo = expf(-(r * r) / (2 * halo_r * halo_r));
            float glare = powf(1.0f + (r / glare_r) * (r / glare_r), -0.5f * falloff) * window;
            float streak = 0;
            for (int k = 0; k < (int)streaks / 2; k++) {
                float a = angle + k * 3.14159265f / (streaks / 2);
                float along = fabsf(dx * cosf(a) + dy * sinf(a)), across = fabsf(-dx * sinf(a) + dy * cosf(a));
                streak += expf(-across / 0.7f) * powf(1.0f + along / (0.03f * size * picture), -1.5f);
            }
            streak *= window;
            if (x == 0 && y == 0)
                halo = glare = streak = 0; /* the light itself stays in the picture */
            float* p = &parts[((size_t)y * n + x) * 3];
            p[0] = halo, p[1] = glare, p[2] = streak;
            sums[0] += halo, sums[1] += glare, sums[2] += streak;
        }
    float w[3] = { core * (1 - streak_share), (1 - core) * (1 - streak_share), streak_share };
    for (int k = 0; k < 3; k++)
        w[k] = sums[k] > 0 ? (float)(w[k] / sums[k]) : 0.0f;
    for (size_t i = 0; i < (size_t)n * n; i++)
        out[i] = parts[i * 3] * w[0] + parts[i * 3 + 1] * w[1] + parts[i * 3 + 2] * w[2];
}

/* ------------------------------------------------------------ RPC graphics `post` and settings */

extern "C" void pc_post_set_flags(int flags)
{
    static const char* const keys[3] = { "dof", "tilt_shift", "depth_fog" };
    for (int i = 0; i < 3; i++) {
        int k = field_index(keys[i]);
        user_gameplay.v.f[fields()[k].offset] = (flags >> i) & 1 ? 1.0f : 0.0f;
        user_gameplay.set[k] = true;
    }
}
extern "C" int pc_post_get_flags(void)
{
    Values v = resolve(PC_POST_GAMEPLAY);
    return (get(v, "dof") > 0 ? 1 : 0) | (get(v, "tilt_shift") > 0 ? 2 : 0) | (get(v, "depth_fog") > 0 ? 4 : 0);
}

extern "C" int pc_post_settings_parse(const char* key, const char* rest)
{
    if (strncmp(key, "post_", 5) && strcmp(key, "focus_mode") && strcmp(key, "aperture") && strcmp(key, "focus_depth")
        && strcmp(key, "dof_radius") && strcmp(key, "tilt_shift") && strcmp(key, "depth_fog")
        && strcmp(key, "depth_fog_range"))
        return 0;
    std::string value = rest;
    /* The settings before post profiles (post_flags, focus_mode, ...): the user's gameplay values,
     * set only where they differ from the defaults (so a preset still shows through). */
    float n[4] = { 0, 0, 0, 0 };
    int count = sscanf(rest, "%f %f %f %f", &n[0], &n[1], &n[2], &n[3]);
    auto legacy = [&](const char* field, const std::string& text) {
        Layer probe;
        if (layer_set(probe, field, text)) {
            int i = field_index(field);
            const Field& x = fields()[i];
            bool differs = false;
            for (int k = 0; k < x.count(); k++)
                differs |= probe.v.f[x.offset + k] != x.def[k];
            if (differs)
                layer_set(user_gameplay, field, text);
        }
    };
    if (!strcmp(key, "post_flags")) {
        if (count >= 1)
            for (int i = 0; i < 3; i++)
                legacy(i == 0 ? "dof" : i == 1 ? "tilt_shift" : "depth_fog", ((int)n[0] >> i) & 1 ? "1" : "0");
        return 1;
    }
    if (!strcmp(key, "focus_mode")) /* 0, on Ashley, was the default then: the camera subject is now */
        return count >= 1 && (int)n[0] != 0 ? (legacy("dof_focus", std::to_string((int)n[0])), 1) : 1;
    if (!strcmp(key, "aperture"))
        return count >= 1 ? (legacy("dof_aperture", number(n[0])), 1) : 1;
    if (!strcmp(key, "focus_depth"))
        return n[0] > 0 ? (legacy("dof_focus_depth", number(n[0])), 1) : 1;
    if (!strcmp(key, "dof_radius"))
        return count >= 1 ? (legacy("dof_max_blur", number(n[0])), 1) : 1;
    if (!strcmp(key, "tilt_shift") && count >= 3) {
        legacy("tilt_centre", number(n[0]));
        legacy("tilt_width", number(n[1]));
        legacy("tilt_blur", number(n[2]));
        return 1;
    }
    if (!strcmp(key, "depth_fog") && count >= 4) {
        legacy("depth_fog_color", number(n[0]) + "," + number(n[1]) + "," + number(n[2]));
        legacy("depth_fog_amount", number(n[3]));
        return 1;
    }
    if (!strcmp(key, "depth_fog_range") && count >= 2) {
        legacy("depth_fog_start", number(n[0]));
        legacy("depth_fog_end", number(n[1]));
        return 1;
    }
    if (strncmp(key, "post_", 5))
        return 1;
    std::string k = key + 5;
    if (k == "stack")
        return pc_post_stack = atoi(rest) != 0, 1;
    if (k == "blend_time")
        return blend_time = (float)atof(rest), 1;
    if (k == "bloom_fft_size") {
        int s = atoi(rest);
        pc_post_fft_size = s >= 512 ? 512 : 256;
        return 1;
    }
    Layer* l = &user_gameplay;
    if (!strncmp(k.c_str(), "cutscene_", 9)) {
        l = &user_cutscene;
        k = k.substr(9);
    }
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' '))
        value.pop_back();
    size_t a = value.find_first_not_of(" \t");
    value = a == std::string::npos ? std::string() : value.substr(a);
    if (k == "set") { /* the fields this layer sets (written first: the editor's undo restores the set) */
        std::vector<bool> keep = l->set;
        l->set.assign(fields().size(), false);
        size_t p = 0;
        while (p < value.size()) {
            size_t e = value.find(',', p);
            std::string name = value.substr(p, e == std::string::npos ? std::string::npos : e - p);
            int i = field_index(name);
            if (i >= 0)
                l->set[i] = true;
            p = e == std::string::npos ? value.size() : e + 1;
        }
        return 1;
    }
    if (k == "preset") {
        l->preset = value == "none" ? std::string() : value;
        return 1;
    }
    if (!layer_set(*l, k, value))
        fprintf(stderr, "[settings] %s: unknown post field or bad value '%s'\n", key, value.c_str());
    return 1;
}

extern "C" void pc_post_settings_write(void* file)
{
    FILE* f = (FILE*)file;
    fprintf(f, "post_stack %d\n", pc_post_stack);
    fprintf(f, "post_blend_time %s\n", number(blend_time).c_str());
    fprintf(f, "post_bloom_fft_size %d\n", pc_post_fft_size);
    const Layer* layers[2] = { &user_gameplay, &user_cutscene };
    const char* prefix[2] = { "post_", "post_cutscene_" };
    for (int i = 0; i < 2; i++) {
        const Layer& l = *layers[i];
        fprintf(f, "%spreset %s\n", prefix[i], l.preset.empty() ? "none" : l.preset.c_str());
        std::string set;
        for (size_t k = 0; k < fields().size(); k++)
            if (l.set[k])
                set += (set.empty() ? "" : ",") + std::string(fields()[k].key);
        fprintf(f, "%sset %s\n", prefix[i], set.c_str());
        for (size_t k = 0; k < fields().size(); k++)
            if (l.set[k]) {
                std::string text = field_text(fields()[k], l.v);
                if (fields()[k].kind == TEXT)
                    text = "\"" + text + "\"";
                fprintf(f, "%s%s %s\n", prefix[i], fields()[k].key, text.c_str());
            }
    }
}

/* ------------------------------------------------------------ RPC `post` */

namespace {
std::string jstr(const std::string& s) { return prefab::json_quote(s); }
std::string values_json(const Values& v)
{
    std::string s = "{";
    for (size_t i = 0; i < fields().size(); i++) {
        const Field& x = fields()[i];
        s += (i ? "," : "") + jstr(x.key) + ":";
        if (x.kind == ENUM || x.kind == TEXT)
            s += jstr(field_text(x, v));
        else if (x.kind == BOOL)
            s += v.f[x.offset] != 0 ? "true" : "false";
        else if (x.count() == 1)
            s += number(v.f[x.offset]);
        else
            s += "[" + field_text(x, v) + "]";
    }
    return s + "}";
}
std::string layer_json(const Layer& l)
{
    std::string s = "{\"preset\":" + jstr(l.preset) + ",\"set\":{";
    bool first = true;
    for (size_t i = 0; i < fields().size(); i++)
        if (l.set[i]) {
            s += (first ? "" : ",") + jstr(fields()[i].key) + ":" + jstr(field_text(fields()[i], l.v));
            first = false;
        }
    return s + "}}";
}
Layer* layer_named(const std::string& name)
{
    if (name.empty() || name == "gameplay")
        return &user_gameplay;
    if (name == "cutscene")
        return &user_cutscene;
    if (name == "room")
        return &room_gameplay;
    if (name == "room_cutscene")
        return &room_cutscene;
    return nullptr;
}
} // namespace

/* params: {layer: "gameplay" | "cutscene" | "room" | "room_cutscene", set: {field: value}, clear: [fields] | "all",
 * preset: name ("" none) for that layer, context: "auto" | "gameplay" | "cutscene", stack, blend_time,
 * fft_size, save_preset: name (the layer's values into the user's presets), snap: true, focus_point: [u, v]
 * (cursor focus there; null: the mouse), bokeh_test: true (the test card)}. The reply: the context, the
 * resolved values (target) and those shown now (blending), the lens (physical camera), the focus probe,
 * whether the first-person view is up, every layer and the presets. Room
 * layers are saved with lighting {action: "save"}; the others with settings {action: "save"}. */
extern "C" int pc_post_rpc(const char* params_text, char* out, int size)
{
    prefab::Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != prefab::Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_lights::track_room();
    std::string layer_name;
    if (const prefab::Json* j = params.get("layer"))
        layer_name = j->string;
    Layer* l = layer_named(layer_name);
    if (!l) {
        snprintf(out, size, "layer: gameplay, cutscene, room or room_cutscene");
        return -1;
    }
    bool room_edit = l == &room_gameplay || l == &room_cutscene, changed = false;
    if (const prefab::Json* j = params.get("clear")) {
        if (j->type == prefab::Json::String && j->string == "all")
            l->clear();
        for (const prefab::Json& k : j->items) {
            int i = field_index(k.string);
            if (i >= 0) {
                l->set[i] = false;
                const Field& x = fields()[i];
                for (int c = 0; c < x.count(); c++)
                    l->v.f[x.offset + c] = x.def[c];
            }
        }
        changed = true;
    }
    if (const prefab::Json* j = params.get("preset")) {
        l->preset = j->string;
        changed = true;
    }
    if (const prefab::Json* j = params.get("lens_preset")) {
        std::string error;
        if (!lens_preset_apply(*l, j->string, &error)) {
            snprintf(out, size, "lens_preset: %s", error.c_str());
            return -1;
        }
        changed = true;
    }
    if (const prefab::Json* j = params.get("set")) {
        for (const auto& m : j->members)
            if (!layer_set(*l, m.first, json_value_text(m.second))) {
                snprintf(out, size, "set: unknown field or bad value: %s", m.first.c_str());
                return -1;
            }
        changed = true;
    }
    if (room_edit && changed) {
        if (l == &room_gameplay)
            room_gameplay_defined = room_gameplay.any();
        else
            room_cutscene_defined = room_cutscene.any();
        pc_lights_mark_dirty();
    }
    if (const prefab::Json* j = params.get("context"))
        pc_post_force_context = j->string == "gameplay" ? PC_POST_GAMEPLAY : j->string == "cutscene" ? PC_POST_CUTSCENE : -1;
    if (const prefab::Json* j = params.get("stack"))
        pc_post_stack = j->type == prefab::Json::Bool ? j->boolean : j->number != 0;
    if (const prefab::Json* j = params.get("blend_time"))
        blend_time = (float)j->number;
    if (const prefab::Json* j = params.get("fft_size"))
        pc_post_fft_size = j->number >= 512 ? 512 : 256;
    if (const prefab::Json* j = params.get("snap"))
        if (j->boolean)
            started = false; /* the next present snaps */
    if (const prefab::Json* j = params.get("focus_point")) { /* cursor focus at this point (null: the mouse) */
        rpc_point_set = j->type == prefab::Json::Array && j->items.size() == 2;
        if (rpc_point_set)
            rpc_point[0] = (float)j->items[0].number, rpc_point[1] = (float)j->items[1].number;
    }
    if (const prefab::Json* j = params.get("bokeh_test"))
        bokeh_test = j->type == prefab::Json::Bool ? j->boolean : j->number != 0;
    std::string saved;
    if (const prefab::Json* j = params.get("save_preset")) {
        if (!save_preset(j->string, *l, &saved)) {
            snprintf(out, size, "save_preset: %s", saved.c_str());
            return -1;
        }
    }
    presets_refresh();
    std::string names;
    for (const auto& p : presets)
        names += (names.empty() ? "" : ",") + jstr(p.first);
    lens_presets_refresh();
    std::string lens_names;
    for (const auto& p : lens_presets)
        lens_names += (lens_names.empty() ? "" : ",") + jstr(p.first);
    std::string events;
    for (const auto& e : room_events)
        events += (events.empty() ? "" : ",") + jstr(std::to_string(e.first)) + ":" + layer_json(e.second);
    std::string s = "{\"context\":" + jstr(effective_context() == PC_POST_CUTSCENE ? "cutscene" : "gameplay")
        + ",\"detected\":" + jstr(context_now == PC_POST_CUTSCENE ? "cutscene" : "gameplay")
        + ",\"lens_blur_off\":" + jstr(gate_now) + ",\"phase\":" + std::to_string(&pc_game_phase ? pc_game_phase : 0)
        + ",\"forced\":" + (pc_post_force_context >= 0 ? "true" : "false") + ",\"event\":" + std::to_string(event_now)
        + ",\"stack\":" + (pc_post_stack ? "true" : "false") + ",\"blend_time\":" + number(blend_time)
        + ",\"blend\":" + number(blend_progress) + ",\"grade\":" + (pc_post_active_grade ? "true" : "false")
        + ",\"bloom\":" + jstr(pc_post_active_bloom == PC_BLOOM_CONVOLUTION ? "convolution"
                                  : pc_post_active_bloom == PC_BLOOM_GAUSSIAN ? "gaussian"
                                                                               : "none")
        + ",\"fft_available\":" + (pc_post_fft_available ? "true" : "false") + ",\"fft_size\":"
        + std::to_string(pc_post_fft_size) + ",\"ev\":" + number(pc_post_active_ev) + ",\"focus\":"
        + number(frame.focus_sz) + ",\"subject\":{\"kind\":" + jstr(subject_now.why[0] ? subject_now.kind : "")
        + ",\"why\":" + jstr(subject_now.why) + ",\"actor\":" + std::to_string(subject_now.actor) + ",\"depth\":"
        + number(subject_now.depth) + ",\"fallback\":" + (subject_now.fallback ? "true" : "false") + ",\"pulling\":"
        + (subject_pulling ? "true" : "false") + ",\"state\":" + std::to_string(subject_now.state) + ",\"mode\":"
        + std::to_string(subject_now.mode) + ",\"performer\":" + std::to_string(subject_now.performer)
        + ",\"action_target\":" + std::to_string(subject_now.target) + "}"
        + ",\"first_person\":" + (lens.first_person ? "true" : "false")
        + ",\"lens\":{\"model\":" + jstr(lens.physical ? "physical" : "artistic") + ",\"focal_mm\":"
        + number(lens.focal_mm) + ",\"fstop\":" + number(lens.fstop) + ",\"sensor_mm\":" + number(lens.sensor_mm)
        + ",\"focus_m\":" + number(lens.focus_m) + ",\"far_px\":" + number(lens.far_px) + ",\"cap\":"
        + number(lens.cap) + "},\"probe\":{\"on\":" + (frame.focus_probe ? "true" : "false") + ",\"sz\":"
        + number(probe_sz) + ",\"share\":" + number(probe_share) + ",\"uv\":[" + number(frame.focus_uv[0]) + ","
        + number(frame.focus_uv[1]) + "]},\"bokeh_test\":" + (bokeh_test ? "true" : "false") + ",\"fade\":" + number(frame.fade) + ",\"menu_model\":" + (frame_menu_model ? "true" : "false") + ",\"flags\":" + std::to_string(frame.flags) + ",\"lut\":{\"size\":"
        + std::to_string(lut_size) + ",\"error\":" + jstr(lut_error) + "},\"zone\":" + std::to_string(rood_lights::zone)
        + ",\"room\":" + std::to_string(rood_lights::room) + ",\"target\":" + values_json(target)
        + ",\"shown\":" + values_json(shown) + ",\"layers\":{\"gameplay\":" + layer_json(user_gameplay)
        + ",\"cutscene\":" + layer_json(user_cutscene) + ",\"room\":" + layer_json(room_gameplay)
        + ",\"room_cutscene\":" + layer_json(room_cutscene) + ",\"events\":{" + events + "}},\"presets\":[" + names
        + "],\"lens_presets\":[" + lens_names + "]" + (saved.empty() ? "" : ",\"saved\":" + jstr(saved)) + "}";
    snprintf(out, size, "%s", s.c_str());
    return (int)s.size() < size ? 0 : -1;
}

/* ------------------------------------------------------------ Details > Post-processing */

namespace {
int edit_layer; /* 0 gameplay, 1 cutscenes, 2 this room, 3 this room's cutscenes */
char preset_name[64];
std::string panel_status;

void tooltip(const char* text)
{
    if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", text);
}
/* One field: its override checkbox (Unreal's) and its widget, showing the layer's value when set, else the
 * value it inherits. Returns true when it changed. */
bool field_row(Layer& l, int i, const Values& inherited)
{
    const Field& x = fields()[i];
    ImGui::PushID(i);
    bool on = l.set[i], changed = false;
    if (ImGui::Checkbox("##set", &on)) {
        l.set[i] = on;
        if (on) /* start from what it shows now */
            for (int k = 0; k < x.count(); k++)
                l.v.f[x.offset + k] = inherited.f[x.offset + k];
        if (on && x.kind == TEXT)
            l.v.lut = inherited.lut;
        changed = true;
    }
    tooltip("Override: this layer sets the value (else it comes from the layers below).");
    ImGui::SameLine();
    Values& v = on ? l.v : const_cast<Values&>(inherited);
    Values before = v;
    float* p = &v.f[x.offset];
    ImGui::BeginDisabled(!on);
    switch (x.kind) {
    case BOOL: {
        bool b = p[0] != 0;
        if (ImGui::Checkbox(x.label, &b))
            p[0] = b ? 1.0f : 0.0f;
        break;
    }
    case ENUM: {
        int e = (int)p[0];
        if (ImGui::Combo(x.label, &e, x.labels))
            p[0] = (float)e;
        break;
    }
    case COLOR:
        ImGui::ColorEdit3(x.label, p, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        break;
    case VEC4: {
        ImGui::SliderFloat(x.label, &p[3], x.lo, x.hi, "%.3f");
        ImGui::Indent();
        ImGui::DragFloat3("##rgb", p, 0.005f, x.lo, x.hi, "%.3f");
        tooltip("Red, green, blue (times the master above).");
        ImGui::Unindent();
        break;
    }
    case TEXT: {
        char buf[160];
        snprintf(buf, sizeof buf, "%s", v.lut.c_str());
        if (ImGui::InputText(x.label, buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue))
            v.lut = buf;
        break;
    }
    default:
        if (x.flags & INT) {
            int n = (int)p[0];
            if (ImGui::SliderInt(x.label, &n, (int)x.lo, (int)x.hi))
                p[0] = (float)n;
        } else
            ImGui::SliderFloat(x.label, p, x.lo, x.hi, "%.3f", (x.flags & LOG) ? ImGuiSliderFlags_Logarithmic : 0);
    }
    ImGui::EndDisabled();
    tooltip(x.tip);
    if (on && !same(before, v))
        changed = true;
    else if (!on)
        const_cast<Values&>(inherited) = before;
    ImGui::PopID();
    return changed;
}
} // namespace

extern "C" void pc_post_panel(void)
{
    rood_lights::track_room();
    presets_refresh();
    if (!pc_hw_active) {
        ImGui::TextDisabled("Post-processing needs the hardware renderer.");
        return;
    }
    bool stack = pc_post_stack != 0;
    if (ImGui::Checkbox("Post-processing on", &stack))
        pc_post_stack = stack;
    tooltip("Off: every effect off (the original picture), whatever the profiles say. Settings key post_stack.");
    int ctx = effective_context();
    ImGui::SameLine();
    ImGui::TextDisabled("now: %s%s", ctx == PC_POST_CUTSCENE ? "cutscene" : "gameplay",
        pc_post_force_context >= 0 ? " (preview)" : "");
    if (event_now >= 0 && ctx == PC_POST_CUTSCENE) {
        ImGui::SameLine();
        ImGui::TextDisabled("event %d", event_now);
    }
    int preview = pc_post_force_context + 1;
    if (ImGui::Combo("Preview", &preview, "Auto (game state)\0Gameplay\0Cutscene\0"))
        pc_post_force_context = preview - 1;
    tooltip("Show a context's profile whatever the game does (not saved).");
    ImGui::SliderFloat("Blend time", &blend_time, 0, 5, "%.2f s");
    tooltip("Seconds profiles blend over when they change (room changes, gameplay <-> cutscene). A cutscene that\n"
            "starts on a camera cut switches at once. Settings key post_blend_time.");
    ImGui::SeparatorText("Edit");
    ImGui::RadioButton("Gameplay", &edit_layer, 0);
    tooltip("Your post-processing everywhere (settings: post_*).");
    ImGui::SameLine();
    ImGui::RadioButton("Cutscenes", &edit_layer, 1);
    tooltip("On top of gameplay while an event script runs; by default only turns depth of field and tilt-shift off\n"
            "(the original look). Settings: post_cutscene_*.");
    ImGui::RadioButton("This room", &edit_layer, 2);
    tooltip("The room's `post room` item (lighting packs; Save area writes it).");
    ImGui::SameLine();
    ImGui::RadioButton("This room's cutscenes", &edit_layer, 3);
    tooltip("The room's `post cutscene` item.");
    Layer* layers[4] = { &user_gameplay, &user_cutscene, &room_gameplay, &room_cutscene };
    Layer& l = *layers[edit_layer];
    /* What this layer inherits: the layers below it. */
    Values inherited = defaults();
    apply_with_preset(inherited, user_gameplay);
    if (edit_layer >= 1) {
        if (edit_layer != 1)
            apply_with_preset(inherited, room_gameplay);
        if (edit_layer != 2) {
            apply(inherited, cutscene_builtin());
            if (edit_layer == 3)
                apply_with_preset(inherited, user_cutscene);
        }
    }
    if (edit_layer == 0)
        inherited = defaults();
    if (const Layer* p = preset_layer(l.preset))
        apply(inherited, *p);
    /* Presets. */
    std::string current = l.preset.empty() ? "(none)" : l.preset;
    bool changed = false;
    if (ImGui::BeginCombo("Preset", current.c_str())) {
        if (ImGui::Selectable("(none)", l.preset.empty()))
            l.preset.clear(), changed = true;
        for (const auto& p : presets) {
            std::string label = (p.second.title.empty() ? p.first : p.second.title) + (p.second.builtin ? "" : " (yours)");
            if (ImGui::Selectable(label.c_str(), l.preset == p.first))
                l.preset = p.first, changed = true;
            if (!p.second.description.empty())
                tooltip(p.second.description.c_str());
        }
        ImGui::EndCombo();
    }
    tooltip("A named profile under this layer's own values (pc/assets/post, yours in pc/user/post).");
    lens_presets_refresh();
    if (ImGui::BeginCombo("Lens preset", "Apply...")) {
        for (const auto& p : lens_presets) {
            std::string label = (p.second.title.empty() ? p.first : p.second.title) + (p.second.builtin ? "" : " (yours)");
            if (ImGui::Selectable(label.c_str(), false)) {
                std::string error;
                if (lens_preset_apply(l, p.first, &error))
                    changed = true;
                else
                    panel_status = error;
            }
            if (!p.second.description.empty())
                tooltip(p.second.description.c_str());
        }
        ImGui::EndCombo();
    }
    tooltip("Writes a depth of field and bokeh look into this layer (pc/assets/post/lens, yours in\n"
            "pc/user/post/lens); every other effect stays as it is. Tweak the fields after.");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    ImGui::InputText("##name", preset_name, sizeof preset_name);
    ImGui::SameLine();
    if (ImGui::Button("Save as preset")) {
        save_preset(preset_name, l, &panel_status);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear layer")) {
        l.clear();
        changed = true;
    }
    tooltip("Every override of this layer off (its preset too).");
    /* The fields, grouped like Unreal's Post Process Volume. */
    const char* open_group = nullptr;
    bool group_open = false, sub_open = false;
    std::string last_section, last_sub;
    for (size_t i = 0; i < fields().size(); i++) {
        const Field& x = fields()[i];
        std::string group = x.group, section = group.substr(0, group.find('|')),
                    sub = group.find('|') == std::string::npos ? "" : group.substr(group.find('|') + 1);
        if (section != last_section) {
            if (sub_open)
                ImGui::TreePop();
            sub_open = false;
            last_section = section;
            last_sub.clear();
            group_open = ImGui::CollapsingHeader(section.c_str(),
                section == "Lens" || section == "Film" ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        }
        if (!group_open)
            continue;
        if (sub != last_sub) {
            if (sub_open)
                ImGui::TreePop();
            last_sub = sub;
            sub_open = ImGui::TreeNodeEx(sub.c_str(), sub == "Depth of Field" || sub == "Bloom" || sub == "Exposure"
                        || sub == "Tonemapper" ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        }
        if (!sub_open)
            continue;
        changed |= field_row(l, (int)i, inherited);
    }
    if (sub_open)
        ImGui::TreePop();
    (void)open_group;
    if (changed && edit_layer >= 2) {
        if (edit_layer == 2)
            room_gameplay_defined = room_gameplay.any();
        else
            room_cutscene_defined = room_cutscene.any();
        pc_lights_mark_dirty();
    }
    ImGui::Separator();
    if (edit_layer < 2) {
        if (ImGui::Button("Save settings"))
            pc_settings_save();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", pc_settings_status());
    } else
        ImGui::TextDisabled("Room layers are saved with the room: Lighting > Save area (Ctrl+S).");
    if (!panel_status.empty())
        ImGui::TextWrapped("%s", panel_status.c_str());
    if (!lut_error.empty())
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "LUT: %s", lut_error.c_str());
    if (frame.flags & 1) {
        if (lens.physical)
            ImGui::TextDisabled("Lens%s: %.0f mm f/%.1f, focus %.2f m; blur far away %.1f px (cap %.0f)",
                lens.first_person ? " (first person)" : "", lens.focal_mm, lens.fstop, lens.focus_m, lens.far_px,
                lens.cap);
        else
            ImGui::TextDisabled("Depth of field%s: artistic, focus SZ %.0f", lens.first_person ? " (first person)" : "",
                frame.focus_sz);
        if (subject_now.why[0]) {
            if (subject_now.actor > 0)
                ImGui::TextDisabled("Camera subject: %s %d (%s), SZ %.0f%s", subject_now.kind, subject_now.actor,
                    subject_now.why, subject_now.depth, subject_pulling ? ", pulling focus" : "");
            else
                ImGui::TextDisabled("Camera subject: %s (%s), SZ %.0f%s", subject_now.kind, subject_now.why,
                    subject_now.depth, subject_pulling ? ", pulling focus" : "");
        }
    }
    ImGui::Checkbox("Bokeh test card", &bokeh_test);
    tooltip("While depth of field is on: the 3D replaced by small white dots, blurred by the maximum, to see the\n"
            "bokeh's shape (blades, squeeze, edge, fringe, cat's eye toward the edges). Not saved.");
    ImGui::TextDisabled("Grade pass %s, bloom %s, exposure %+.2f EV, blend %.0f%%", pc_post_active_grade ? "on" : "off",
        pc_post_active_bloom == PC_BLOOM_CONVOLUTION ? "convolution" : pc_post_active_bloom == PC_BLOOM_GAUSSIAN ? "gaussian" : "off",
        pc_post_active_ev, blend_progress * 100);
}

/* ------------------------------------------------------------ plugins' overrides (rood.post) */

namespace { /* (inside: Windows has INT, BOOL and FLOAT of its own) */
bool numeric(const Field& x) { return x.kind == FLOAT || x.kind == COLOR || x.kind == VEC4; }
bool integral(const Field& x) { return (x.flags & INT) != 0; }
bool whole(const Field& x) { return integral(x) || x.kind == BOOL || x.kind == ENUM; }
bool logarithmic(const Field& x) { return (x.flags & LOG) != 0; }
} // namespace

extern "C" int pc_post_field_count(void) { return (int)fields().size(); }
extern "C" int pc_post_field_find(const char* key) { return key ? field_index(key) : -1; }
extern "C" int pc_post_field_info(int index, pc_post_field_desc* out)
{
    if (index < 0 || index >= (int)fields().size())
        return 0;
    const Field& x = fields()[index];
    static const char* const kinds[] = { "bool", "float", "enum", "color", "vec4", "text" };
    memset(out, 0, sizeof *out);
    out->key = x.key;
    out->kind = kinds[x.kind];
    out->group = x.group;
    out->label = x.label;
    out->tip = x.tip;
    out->names = x.names;
    out->labels = x.labels;
    out->count = x.count();
    out->lo = x.lo, out->hi = x.hi;
    memcpy(out->def, x.def, sizeof out->def);
    out->log = logarithmic(x);
    out->integer = whole(x);
    return 1;
}
extern "C" int pc_post_value_text(const char* key, char* out, int size)
{
    if (key && !strcmp(key, "preset") && size > 0) { /* the newest override's preset ("" none) */
        std::string name;
        for (const auto& o : overrides)
            if (!o.second.preset.empty())
                name = o.second.preset;
        snprintf(out, (size_t)size, "%s", name.c_str());
        return 0;
    }
    int i = key ? field_index(key) : -1;
    if (i < 0 || size <= 0)
        return -1;
    const char* gate = gate_now;
    Values v = resolve(effective_context());
    gate_now = gate;
    snprintf(out, (size_t)size, "%s", field_text(fields()[i], v).c_str());
    return 0;
}
extern "C" int pc_post_override_set(const char* owner, const char* key, const char* value, char* why, int size)
{
    if (key && !strcmp(key, "preset")) { /* a named profile under the owner's values ("": none) */
        presets_refresh();
        std::string name = value ? value : "";
        if (!name.empty() && !preset_layer(name)) {
            snprintf(why, (size_t)size, "no post preset '%s' (pc/assets/post, pc/user/post)", name.c_str());
            return -1;
        }
        auto it = std::find_if(overrides.begin(), overrides.end(), [&](const auto& o) { return o.first == owner; });
        if (it == overrides.end()) {
            if (name.empty())
                return 0;
            overrides.emplace_back(owner, Layer());
            it = overrides.end() - 1;
        }
        it->second.preset = name;
        if (!it->second.any())
            overrides.erase(it);
        return 0;
    }
    int i = key ? field_index(key) : -1;
    if (i < 0) {
        snprintf(why, (size_t)size, "no post field '%s' (rood.post.list())", key ? key : "");
        return -1;
    }
    const Field& x = fields()[i];
    auto it = std::find_if(overrides.begin(), overrides.end(), [&](const auto& o) { return o.first == owner; });
    Layer trial = it == overrides.end() ? Layer() : it->second;
    if (!field_parse(x, trial.v, value ? value : "")) {
        snprintf(why, (size_t)size, "'%s' does not fit %s", value ? value : "", x.key);
        return -1;
    }
    if (numeric(x))
        for (int k = 0; k < x.count(); k++) {
            float& f = trial.v.f[x.offset + k];
            f = fminf(fmaxf(f, x.lo), x.hi);
            if (integral(x))
                f = roundf(f);
        }
    trial.set[i] = true;
    if (it == overrides.end())
        overrides.emplace_back(owner, trial);
    else
        it->second = trial;
    return 0;
}
extern "C" void pc_post_override_clear(const char* owner, const char* key)
{
    auto it = std::find_if(overrides.begin(), overrides.end(), [&](const auto& o) { return o.first == owner; });
    if (it == overrides.end())
        return;
    int i = key ? field_index(key) : -1;
    if (key && !strcmp(key, "preset")) {
        it->second.preset.clear();
        if (!it->second.any())
            overrides.erase(it);
    } else if (key && i >= 0) {
        it->second.set[i] = false;
        const Field& x = fields()[i];
        for (int k = 0; k < x.count(); k++)
            it->second.v.f[x.offset + k] = x.def[k];
        if (!it->second.any())
            overrides.erase(it);
    } else if (!key)
        overrides.erase(it);
}
extern "C" int pc_post_override_count(const char* owner)
{
    int n = 0;
    for (const auto& o : overrides)
        if (!owner || o.first == owner)
            n += (int)std::count(o.second.set.begin(), o.second.set.end(), true) + !o.second.preset.empty();
    return n;
}
extern "C" int pc_post_preset_names(char* out, int size)
{
    presets_refresh();
    int at = 0, n = 0;
    for (const auto& p : presets) {
        int len = (int)p.first.size() + 1;
        if (at + len + 1 > size)
            break;
        memcpy(out + at, p.first.c_str(), (size_t)len);
        at += len, ++n;
    }
    if (size > 0)
        out[at < size ? at : size - 1] = 0;
    return n;
}
