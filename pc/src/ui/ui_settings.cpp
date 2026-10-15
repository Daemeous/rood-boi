// Modern menus, System tab: the settings pages (ui_settings.h). Each row
// applies through the calls the F1 editor makes (dev_overlay.cpp
// graphics_panel / camera_stick_panel / gameplay_panel,
// lighting_quality.cpp pc_lighting_panel / antialiasing_panel,
// ui_upscale_panel.cpp) and saves settings.cfg debounced.
#include "ui/ui_settings.h"
#include "ui/game_options.h"
#include "ui/ui_tint.h"
#include "antialias.h"
#include <SDL3/SDL_gamepad.h> /* before boot_controller.h: SDL keeps its own linkage */
#include <SDL3/SDL_keycode.h>
extern "C" {
#include "boot_controller.h" /* C declarations without extern "C" */
}
#include "button_prompts.h"
#include "camera_stick.h"
#include "gameplay_walls.h"
#include "lighting_quality.h"
#include "ui_bubbles.h"
#include "ui_text.h"
#include "ui_upscale.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
extern int pc_hw_active, pc_hw_scale_setting, pc_gl_filter, pc_gl_aspect, pc_gte_widescreen, pc_hw_perspective,
    pc_hw_subpixel, pc_hw_interpolate, pc_hw_texture_filter, pc_present_rate;
int pc_hw_set_scale(int scale);
extern int pc_env_global;        /* environment.cpp: the rooms' sun and sky */
extern int pc_lights_enabled;    /* lights.cpp: dynamic lights (lighting key dynamic_lights) */
extern int pc_qol_weapon_slots;  /* project_settings.cpp; weapon_slots.c */
extern int pc_qol_skip_charging; /* project_settings.cpp; boot_window.c */
extern int pc_skip_intro;        /* project_settings.cpp; boot_runtime.c */
extern int pc_qol_map_invert, pc_qol_fp_invert_y; /* project_settings.cpp; MENU5, BATTLE func_8006EC7C */
extern int pc_ui_menus;          /* project_settings.cpp; ui_shell */
extern float pc_ui_menu_opacity; /* project_settings.cpp; ui_theme.cpp theme() */
void pc_settings_save(void);
void pc_settings_touched(const char* key) __attribute__((weak)); /* project_settings.cpp: the user's now */
int pc_aa_msaa_max(void) __attribute__((weak)); /* gpu_hw.c */
/* game_options.c (battle builds) */
int pc_game_option_get(int option) __attribute__((weak));
int pc_game_option_set(int option, int choice) __attribute__((weak));
const char* pc_game_option_unavailable(int option) __attribute__((weak));
/* Hooks of features still to come (ui_settings.h "Hooks"). */
int pc_window_mode_get(void) __attribute__((weak));
void pc_window_mode_set(int mode) __attribute__((weak));
int pc_master_volume_get(void) __attribute__((weak));
void pc_master_volume_set(int volume) __attribute__((weak));
int pc_brightness_get(void) __attribute__((weak));
void pc_brightness_set(int value) __attribute__((weak));
int pc_ui_capture_begin(void) __attribute__((weak));
}

namespace {

const char* const COMING = "Coming in this phase";
const char* const NEEDS_HW = "Needs the hardware renderer";

struct Row;
using GetI = int (*)(const Row&);
using SetI = void (*)(const Row&, int);
using GetF = float (*)(const Row&);
using SetF = void (*)(const Row&, float);
using Why = const char* (*)(const Row&);
using Shown = bool (*)(const Row&);
using ChoiceWhy = const char* (*)(const Row&, int);
using Run = void (*)(const Row&);
using Info = void (*)(const Row&, char*, int);
using Swatch = void (*)(const Row&, unsigned char*);

struct Row {
    const char* id;
    const char* label;
    const char* help;
    int kind;
    int* ivar = nullptr;
    float* fvar = nullptr;
    const char* key = nullptr;              // lighting key (pc_lighting_set)
    const char* const* names = nullptr;     // choice labels
    const char* const* values = nullptr;    // lighting values per choice (else "0"/"1")
    const int* ichoices = nullptr;          // choice i sets *ivar = ichoices[i]
    const float* fchoices = nullptr;        // choice i sets *fvar = fchoices[i]
    int count = 0;
    int arg = 0;                            // game option / PS1 button
    float min = 0, max = 0, step = 1;
    const char* format = nullptr;           // slider value (printf, the value as double or int)
    const char* zero = nullptr;             // slider text at its minimum ("Off")
    bool integer = false;                   // slider value printed as int
    GetI get = nullptr;
    SetI set = nullptr;
    GetF getf = nullptr;
    SetF setf = nullptr;
    Why why = nullptr;
    Shown shown = nullptr;
    ChoiceWhy choice_why = nullptr;
    Run run = nullptr;
    Info info = nullptr;
    Swatch swatch = nullptr; // choice: a colour chip shown with the value
    bool saved = true;   // a change marks settings.cfg dirty
    bool game = false;   // the game's option
    bool hook = false;   // reads and writes through a hook that may be missing (then greyed, showing `def`)
    bool on_release = false; // slider: a pointer drag previews the number, the value applies when it ends
    float def = 0;       // hook row: the value shown while greyed (choice index or slider value)
    Row grey(Why w) const { Row r = *this; r.why = w; return r; }
    Row when(Shown s) const { Row r = *this; r.shown = s; return r; }
    Row choices_why(ChoiceWhy c) const { Row r = *this; r.choice_why = c; return r; }
    Row unsaved() const { Row r = *this; r.saved = false; return r; }
};

const char* const OFF_ON[2] = {"Off", "On"};

Row base(const char* id, const char* label, const char* help, int kind)
{
    Row r;
    r.id = id, r.label = label, r.help = help, r.kind = kind;
    return r;
}
Row toggle(const char* id, const char* label, const char* help, int* var)
{
    Row r = base(id, label, help, PC_UI_SET_TOGGLE);
    r.ivar = var, r.names = OFF_ON, r.count = 2;
    return r;
}
Row choice(const char* id, const char* label, const char* help, int* var, const char* const* names, int count)
{
    Row r = base(id, label, help, PC_UI_SET_CHOICE);
    r.ivar = var, r.names = names, r.count = count;
    return r;
}
// A lighting quality key: read from its variable, written with pc_lighting_set.
Row lighting(const char* id, const char* label, const char* help, int* var, const char* const* names,
    const char* const* values, int count)
{
    Row r = base(id, label, help, values ? PC_UI_SET_CHOICE : PC_UI_SET_TOGGLE);
    r.ivar = var, r.key = id, r.names = names ? names : OFF_ON, r.values = values, r.count = count;
    return r;
}
Row slider(const char* id, const char* label, const char* help, float min, float max, float step, const char* format)
{
    Row r = base(id, label, help, PC_UI_SET_SLIDER);
    r.min = min, r.max = max, r.step = step, r.format = format;
    return r;
}
Row islider(const char* id, const char* label, const char* help, int* var, int min, int max, const char* format)
{
    Row r = slider(id, label, help, (float)min, (float)max, 1, format);
    r.ivar = var, r.integer = true;
    return r;
}
Row fslider(const char* id, const char* label, const char* help, float* var, float min, float max, float step,
    const char* format)
{
    Row r = slider(id, label, help, min, max, step, format);
    r.fvar = var;
    return r;
}
Row action(const char* id, const char* label, const char* help, Run run)
{
    Row r = base(id, label, help, PC_UI_SET_ACTION);
    r.run = run;
    return r;
}
Row game_option(const char* id, const char* label, const char* help, int option, const char* const* names, int count)
{
    Row r = base(id, label, help, names == OFF_ON ? PC_UI_SET_TOGGLE : PC_UI_SET_CHOICE);
    r.names = names, r.count = count, r.arg = option, r.saved = false, r.game = true;
    r.shown = [](const Row&) { return pc_game_option_get != nullptr && pc_game_option_set != nullptr; };
    r.get = [](const Row& r) { return pc_game_option_get(r.arg); };
    r.set = [](const Row& r, int i) { pc_game_option_set(r.arg, i); };
    r.why = [](const Row& r) { return pc_game_option_unavailable ? pc_game_option_unavailable(r.arg) : nullptr; };
    return r;
}

const char* need_hw(const Row&) { return pc_hw_active ? nullptr : NEEDS_HW; }
const char* need_lights(const Row&)
{
    if (!pc_hw_active)
        return NEEDS_HW;
    return pc_lights_enabled ? nullptr : "Needs dynamic lights on";
}
const char* lighting_choice(const Row& r, int i) { return pc_lighting_unavailable(r.key, i); }
const char* need_stick(const Row&) { return pc_camera_stick ? nullptr : "Needs Right stick camera on"; }
const char* need_zoom(const Row& r)
{
    if (const char* why = need_stick(r))
        return why;
    return pc_camera_stick_zoom ? nullptr : "Needs Stick zooms on";
}
const char* need_prompts(const Row& r)
{
    if (const char* why = need_hw(r))
        return why;
    return pc_button_prompts != PC_PROMPTS_ORIGINAL ? nullptr : "Needs button prompts other than Original glyphs";
}

// ---- pages ----

const char* const PAGE_IDS[PC_UI_SETTINGS_PAGES] = {"graphics", "display", "audio", "interface", "controls",
    "bindings", "gameplay"};
const char* const PAGE_TITLES[PC_UI_SETTINGS_PAGES] = {"Graphics", "Display", "Audio", "Interface", "Controls",
    "Bindings", "Gameplay"};
std::vector<Row> pages[PC_UI_SETTINGS_PAGES];

// PS1 buttons in the Bindings page's order (libetc bit indices, PC_PROMPT_*).
const int BINDING_ORDER[PC_PAD_BUTTONS] = {PC_PROMPT_CIRCLE, PC_PROMPT_CROSS, PC_PROMPT_TRIANGLE, PC_PROMPT_SQUARE,
    PC_PROMPT_L1, PC_PROMPT_R1, PC_PROMPT_L2, PC_PROMPT_R2, PC_PROMPT_START, PC_PROMPT_SELECT, PC_PROMPT_L3,
    PC_PROMPT_R3, PC_PROMPT_UP, PC_PROMPT_DOWN, PC_PROMPT_LEFT, PC_PROMPT_RIGHT};
const char* const BUTTON_LABELS[PC_PAD_BUTTONS] = {"L2", "R2", "L1", "R1", "Triangle", "Circle", "Cross", "Square",
    "Select", "L3", "R3", "Start", "Up", "Right", "Down", "Left"};
const char* const BUTTON_IDS[PC_PAD_BUTTONS] = {"bind_l2", "bind_r2", "bind_l1", "bind_r1", "bind_triangle",
    "bind_circle", "bind_cross", "bind_square", "bind_select", "bind_l3", "bind_r3", "bind_start", "bind_up",
    "bind_right", "bind_down", "bind_left"};

int capture_button = -1;
char capture_note[96]; // why the last key pressed during the capture was refused
// A swap's note for the status line ("M moved from Circle to Triangle"), 3 s
// from the tick after it happened.
char swap_note[128];
bool swap_note_pending;
double swap_note_since, ticked;

void build()
{
    if (!pages[0].empty())
        return;
    // Graphics
    static const char* const presets[] = {"Original", "Low", "Medium", "High", "Ultra", "Custom"};
    static const char* const preset_values[] = {"original", "low", "medium", "high", "ultra", "custom"};
    static const char* const gi[] = {"Off", "Screen-space", "Probes", "Path traced"};
    static const char* const gi_values[] = {"off", "screen", "probes", "path"};
    static const char* const shadows[] = {"Off", "Shadow maps", "Ray traced"};
    static const char* const shadow_values[] = {"off", "maps", "traced"};
    static const char* const haze[] = {"Off", "Low", "High"};
    static const char* const haze_values[] = {"off", "low", "high"};
    static const char* const posts[] = {"Off", "FXAA", "SMAA"};
    static const char* const msaa[] = {"Off", "2x", "4x", "8x"};
    static const int msaa_counts[] = {0, 2, 4, 8};
    static const char* const ssaa[] = {"Off", "1.5x", "2x", "3x", "4x"};
    static const float ssaa_factors[] = {0, 1.5f, 2, 3, 4};
    static const char* const downsample[] = {"Area", "Lanczos-3", "Bilinear"};
    std::vector<Row>& g = pages[PC_UI_SETTINGS_GRAPHICS];
    g.push_back(lighting("lighting_preset", "Lighting quality",
        "Sets the lighting settings below for a class of GPU. Changing any of them makes it Custom.",
        &pc_lighting_preset, presets, preset_values, 6)
                    .grey(need_hw)
                    .choices_why([](const Row&, int i) {
                        return i == PC_PRESET_CUSTOM ? "Custom is what changing a lighting setting gives" : nullptr;
                    }));
    g.push_back(lighting("dynamic_lights", "Dynamic lights",
        "Real lights, shadows and haze on top of the game's baked lighting. Off: the original look.",
        &pc_lights_enabled, nullptr, nullptr, 2)
                    .grey(need_hw));
    g.push_back(lighting("gi_method", "Indirect light", "Light bouncing off walls and floors.", &pc_gi_method, gi,
        gi_values, 4)
                    .grey(need_lights)
                    .choices_why(lighting_choice));
    g.push_back(lighting("shadows", "Shadows", "Shadow maps run everywhere; ray traced needs a ray-tracing GPU.",
        &pc_shadow_mode, shadows, shadow_values, 3)
                    .grey(need_lights)
                    .choices_why(lighting_choice));
    g.push_back(lighting("haze", "Haze", "Light scattering in the air. High adds light shafts and volumetric shadows.",
        &pc_haze_quality, haze, haze_values, 3)
                    .grey(need_lights)
                    .choices_why(lighting_choice));
    g.push_back(lighting("ao", "Ambient occlusion", "Darkens creases and the ground under props.", &pc_ao, nullptr,
        nullptr, 2)
                    .grey(need_hw));
    {
        Row r = islider("internal_resolution", "Internal resolution",
            "Times the PS1's resolution the 3D is drawn at. Higher is sharper and costs more.", &pc_hw_scale_setting, 1,
            8, "%dx");
        r.setf = [](const Row&, float v) { pc_hw_set_scale((int)v); };
        r.on_release = true; // each scale rebuilds the render targets: not on every step of a drag
        g.push_back(r.grey(need_hw));
    }
    g.push_back(choice("aa", "Anti-aliasing filter",
        "A post-process pass on edges. SMAA keeps textures and the 2D exactly as drawn.", &pc_gl_aa, posts, 3)
                    .grey(need_hw));
    {
        Row r = choice("msaa", "MSAA", "Multisampled 3D edges; textures stay crisp.", &pc_aa_msaa, msaa, 4);
        r.ichoices = msaa_counts;
        g.push_back(r.grey(need_hw).choices_why([](const Row&, int i) {
            int max = pc_aa_msaa_max ? pc_aa_msaa_max() : 0;
            return i == 0 || msaa_counts[i] <= max ? nullptr : "Not supported by this GPU or renderer";
        }));
    }
    {
        Row r = base("ssaa", "Supersampling",
            "Draws at a multiple of the window's resolution and filters down. Costly.", PC_UI_SET_CHOICE);
        r.fvar = &pc_aa_ssaa, r.fchoices = ssaa_factors, r.names = ssaa, r.count = 5;
        g.push_back(r.grey(need_hw));
    }
    g.push_back(choice("aa_downsample", "Downsample filter", "How supersampling filters down to the window.",
        &pc_aa_downsample, downsample, 3)
                    .grey([](const Row& r) {
                        if (const char* why = need_hw(r))
                            return why;
                        return pc_aa_ssaa > 1 ? nullptr : "Needs supersampling";
                    }));
    g.push_back(toggle("perspective_textures", "Perspective-correct textures",
        "Textures no longer warp on large polygons.", &pc_hw_perspective)
                    .grey(need_hw));
    g.push_back(toggle("subpixel", "Sub-pixel precision", "No vertex jitter: polygons keep their exact positions.",
        &pc_hw_subpixel)
                    .grey(need_hw));
    g.push_back(toggle("smooth_textures", "Smooth textures", "Bilinear filtering on 3D textures. Off: the PS1's texels.",
        &pc_hw_texture_filter)
                    .grey(need_hw));
    g.push_back(toggle("environment", "Sun and sky", "Sunlight, moonlight and sky light in outdoor rooms.",
        &pc_env_global)
                    .grey(need_hw));

    // Display
    static const char* const aspects[] = {"Keep aspect", "Stretch to window", "Integer scale"};
    static const char* const filters[] = {"Nearest", "Bilinear", "Box"};
    static const char* const window_modes[] = {"Windowed", "Borderless", "Fullscreen"};
    std::vector<Row>& d = pages[PC_UI_SETTINGS_DISPLAY];
    d.push_back(toggle("widescreen", "Widescreen 16:9", "The 3D view widens to 16:9; menus and HUD stay in place.",
        &pc_gte_widescreen));
    d.push_back(choice("aspect", "Scaling", "How the picture fills the window.", &pc_gl_aspect, aspects, 3));
    d.push_back(choice("filter", "Filter", "How the picture is scaled to the window's pixels.", &pc_gl_filter, filters, 3));
    d.push_back(toggle("interpolate", "Frame interpolation",
        "Smooth 60 fps motion between the game's frames. Off: the game's own rate.", &pc_hw_interpolate)
                    .grey(need_hw));
    {
        Row r = islider("present_rate", "Images per vsync",
            "In-between images per screen refresh: 2 gives 120 fps on a 120 Hz display.", &pc_present_rate, 1, 8,
            "%d");
        r.shown = [](const Row&) { return pc_hw_active && pc_hw_interpolate; };
        d.push_back(r);
    }
    {
        Row r = choice("window_mode", "Window", "Windowed, borderless (fills the screen) or exclusive fullscreen.",
            nullptr, window_modes, 3);
        r.get = [](const Row&) { return pc_window_mode_get(); };
        r.set = [](const Row&, int i) { pc_window_mode_set(i); };
        r.hook = true, r.def = 0;
        d.push_back(r.grey([](const Row&) {
            return pc_window_mode_get && pc_window_mode_set ? nullptr : COMING;
        }));
    }
    {
        Row r = slider("brightness", "Brightness", "50 is the game's own picture.", 0, 100, 5, "%d");
        r.getf = [](const Row&) { return (float)pc_brightness_get(); };
        r.setf = [](const Row&, float v) { pc_brightness_set((int)v); };
        r.integer = true, r.hook = true, r.def = 50;
        d.push_back(r.grey([](const Row&) { return pc_brightness_get && pc_brightness_set ? nullptr : COMING; }));
    }
    {
        Row r = base("renderer", "Renderer",
            "Chosen when the game starts: start it with --vulkan for Vulkan (64-bit), without for OpenGL.",
            PC_UI_SET_INFO);
        r.info = [](const Row&, char* out, int size) {
            pc_lighting_caps_t c;
            pc_lighting_caps(&c);
            if (!c.hardware)
                std::snprintf(out, (size_t)size, "%s, software", c.vulkan ? "Vulkan" : "OpenGL");
            else if (c.vulkan)
                std::snprintf(out, (size_t)size, "Vulkan%s", c.ray_query ? ", ray tracing" : "");
            else
                std::snprintf(out, (size_t)size, "OpenGL%s", c.gl_compute ? ", compute" : "");
        };
        d.push_back(r.unsaved());
    }

    // Audio
    static const char* const sound[] = {"Stereo", "Mono"};
    std::vector<Row>& a = pages[PC_UI_SETTINGS_AUDIO];
    {
        Row r = slider("master_volume", "Master volume", "Everything the game plays.", 0, 100, 5, "%d%%");
        r.getf = [](const Row&) { return (float)pc_master_volume_get(); };
        r.setf = [](const Row&, float v) { pc_master_volume_set((int)v); };
        r.integer = true, r.hook = true, r.def = 100;
        a.push_back(r.grey([](const Row&) {
            return pc_master_volume_get && pc_master_volume_set ? nullptr : COMING;
        }));
    }
    a.push_back(game_option("sound", "Sound", "Stereo or mono output. Saved with your game, not in settings.",
        PC_GAME_OPT_SOUND, sound, 2));

    // Interface
    static const char* const menus[] = {"Original", "Modern"};
    static const char* const text[] = {"Original glyphs", "Modern fonts"};
    static const char* const upscale[] = {"Nearest", "MMPX"};
    static const char* const lines[] = {"Legacy", "Joined"};
    static const char* const bubbles[] = {"Original", "Vector"};
    static const char* const prompts[] = {"Auto", "PlayStation", "Xbox", "Nintendo", "Keyboard", "Original glyphs"};
    static const char* const styles[] = {"Solid", "Outline", "Colour solid", "Colour outline"};
    std::vector<Row>& u = pages[PC_UI_SETTINGS_INTERFACE];
    u.push_back(choice("ui_menus", "Menus", "The modern menus or the game's own, from the next time the menu opens.",
        &pc_ui_menus, menus, 2));
    u.push_back(fslider("ui_menu_opacity", "Menu opacity",
        "How much the modern menus' backdrop and panels hide the game behind them. 1 is solid.",
        &pc_ui_menu_opacity, 0, 1, 0.02f, "%.2f"));
    {
        Row r = choice("ui_menu_tint", "Menu tint",
            "The colour of the modern menus' backdrop and panels. Le\xc3\xa1 Monde is the theme's own; Custom is the "
            "colour from the sliders below.",
            &pc_ui_menu_tint, pc_ui_tint_names(), PC_UI_TINTS);
        r.swatch = [](const Row&, unsigned char* rgb) {
            int c[3];
            pc_ui_tint_effective(c);
            pc_ui_tint_swatch(c, rgb);
        };
        u.push_back(r);
        static const char* const channel_ids[3] = {"ui_menu_tint_r", "ui_menu_tint_g", "ui_menu_tint_b"};
        static const char* const channel_labels[3] = {"Tint red", "Tint green", "Tint blue"};
        static const char* const channel_help[3] = {
            "The tint's red, 0 to 31 as on the PlayStation. Changing it makes the tint Custom.",
            "The tint's green, 0 to 31 as on the PlayStation. Changing it makes the tint Custom.",
            "The tint's blue, 0 to 31 as on the PlayStation. Changing it makes the tint Custom."};
        for (int c = 0; c < 3; c++) {
            Row s = slider(channel_ids[c], channel_labels[c], channel_help[c], 0, PC_UI_TINT_MAX, 1, "%d");
            s.integer = true, s.arg = c;
            s.getf = [](const Row& r) {
                int rgb[3];
                pc_ui_tint_effective(rgb);
                return (float)rgb[r.arg];
            };
            // the shown colour becomes Custom's, with this channel changed (a preset's values are kept)
            s.setf = [](const Row& r, float v) {
                if (pc_ui_tint_current() != PC_UI_TINT_CUSTOM) {
                    int rgb[3];
                    pc_ui_tint_effective(rgb);
                    for (int k = 0; k < 3; k++)
                        pc_ui_menu_tint_custom[k] = rgb[k];
                    pc_ui_menu_tint = PC_UI_TINT_CUSTOM;
                }
                pc_ui_menu_tint_custom[r.arg] = (int)std::lround(v);
            };
            u.push_back(s);
        }
    }
    u.push_back(choice("ui_text", "Text", "The game's text drawn from modern fonts at full resolution.", &pc_ui_text,
        text, 2)
                    .grey(need_hw));
    u.push_back(choice("ui_upscale", "2D textures",
        "How the HUD, menus and icons are enlarged. MMPX rebuilds smooth curves from the pixels.", &pc_ui_upscale,
        upscale, 2)
                    .grey(need_hw));
    u.push_back(choice("ui_lines", "Lines", "2D lines one PS1 pixel thick without gaps at any resolution.", &pc_ui_lines,
        lines, 2)
                    .grey(need_hw));
    {
        Row r = islider("ui_outline_boost", "Outline boost", "Thicker 2D lines and bubble outlines.",
            &pc_ui_outline_boost, 0, 3, "+%d px");
        r.zero = "Off";
        u.push_back(r.grey(need_hw));
    }
    u.push_back(choice("ui_bubbles", "Talk bubbles", "Vector: each bubble drawn as one smooth shape.", &pc_ui_bubbles,
        bubbles, 2)
                    .grey(need_hw));
    u.push_back(choice("button_prompts", "Button prompts",
        "The buttons the game shows, as the keys or gamepad buttons you use. Auto follows the last device used.",
        &pc_button_prompts, prompts, PC_PROMPTS_MODES)
                    .grey(need_hw));
    u.push_back(choice("button_prompts_style", "Prompt style", "Filled or outlined icons, plain or in colour.",
        &pc_button_prompts_style, styles, PC_PROMPTS_STYLES)
                    .grey(need_prompts));
    u.push_back(fslider("button_prompts_scale", "Prompt size", "The icons' size within the game's button sprites.",
        &pc_button_prompts_scale, 0.4f, 1.0f, 0.05f, "%.2f")
                    .grey(need_prompts));
    u.push_back(toggle("ui_subpixel", "Sub-pixel text", "Even letter spacing while talk bubbles open and close.",
        &pc_ui_subpixel)
                    .grey(need_hw));

    // Controls
    std::vector<Row>& c = pages[PC_UI_SETTINGS_CONTROLS];
    c.push_back(toggle("camera_stick", "Right stick camera",
        "The right stick turns the camera around Ashley and zooms; L1/R1 still turn 45 degrees.", &pc_camera_stick));
    c.push_back(fslider("camera_stick_speed", "Turn speed", "Degrees per second at full tilt.", &pc_camera_stick_speed,
        30, 360, 15, "%.0f deg/s")
                    .grey(need_stick));
    c.push_back(fslider("camera_stick_deadzone", "Dead zone", "Stick travel ignored around the centre.",
        &pc_camera_stick_deadzone, 0, 0.5f, 0.05f, "%.2f")
                    .grey(need_stick));
    c.push_back(fslider("camera_stick_curve", "Response curve", "1 is linear; higher gives finer control near the centre.",
        &pc_camera_stick_curve, 1, 3, 0.1f, "%.1f")
                    .grey(need_stick));
    c.push_back(toggle("camera_stick_invert_x", "Invert turn", "Stick right turns the other way.",
        &pc_camera_stick_invert_x)
                    .grey(need_stick));
    c.push_back(toggle("camera_stick_zoom", "Stick zooms", "Stick up and down zoom in and out.", &pc_camera_stick_zoom)
                    .grey(need_stick));
    c.push_back(toggle("camera_stick_invert_y", "Invert zoom", "Stick up zooms out.", &pc_camera_stick_invert_y)
                    .grey(need_zoom));
    c.push_back(fslider("camera_stick_zoom_speed", "Zoom speed", "How fast the stick zooms.",
        &pc_camera_stick_zoom_speed, 0.25f, 3, 0.25f, "%.2f")
                    .grey(need_zoom));
    c.push_back(toggle("camera_stick_collision", "Stop before walls", "Zoomed out, the camera stays in front of walls.",
        &pc_camera_stick_collision)
                    .grey(need_zoom));
    c.push_back(toggle("camera_stick_r3_reset", "R3 resets the camera",
        "Off: R3 is the game's first-person view.", &pc_camera_stick_r3)
                    .grey(need_stick));
    {
        Row r = fslider("camera_stick_idle_reset", "Reset when idle",
            "Seconds without stick input before the camera turns back.", &pc_camera_stick_idle_reset, 0, 30, 1,
            "after %.0f s");
        r.zero = "Off";
        c.push_back(r.grey(need_stick));
    }
    c.push_back(toggle("camera_stick_keys", "Camera keys", "J and L turn, I and K zoom, O resets.",
        &pc_camera_stick_keys)
                    .grey(need_stick));
    c.push_back(action("reset_camera", "Reset camera", "Back to the game's own angle and distance.",
        [](const Row&) { pc_camera_stick_reset(); })
                    .grey(need_stick)
                    .unsaved());
    c.push_back(toggle("qol_fp_invert_y", "Invert first-person look",
        "In the first-person view up looks down and down looks up (D-pad and right stick).", &pc_qol_fp_invert_y));
    c.push_back(toggle("qol_map_invert", "Invert map panning", "The map screen pans the other way on both axes.",
        &pc_qol_map_invert));
    c.push_back(game_option("vibration", "Vibration",
        "The game's vibration setting. Saved with your game, not in settings.", PC_GAME_OPT_VIBRATION, OFF_ON, 2));

    // Bindings
    std::vector<Row>& b = pages[PC_UI_SETTINGS_BINDINGS];
    for (int i = 0; i < PC_PAD_BUTTONS; i++) {
        int button = BINDING_ORDER[i];
        Row r = base(BUTTON_IDS[button], BUTTON_LABELS[button],
            "The keys and gamepad button that press it. Circle to change.", PC_UI_SET_BINDING);
        r.arg = button;
        r.why = [](const Row&) { return pc_ui_capture_begin ? nullptr : COMING; };
        b.push_back(r);
    }
    b.push_back(action("bind_reset", "Reset to defaults", "Every key and gamepad button back to its default.",
        [](const Row&) { pc_bindings_reset(); }));

    // Gameplay
    static const char* const walls[] = {"Original", "Smart"};
    static const char* const map[] = {"Frame", "Simple", "Off"};
    std::vector<Row>& p = pages[PC_UI_SETTINGS_GAMEPLAY];
    p.push_back(toggle("qol_weapon_slots", "Weapon quick slots",
        "Hold L2 + R2 to swap among 8 weapon slots. Stored like the QoL mod, so saves stay compatible.",
        &pc_qol_weapon_slots));
    static const char* const charging[] = {"Off", "Fast", "Skip"};
    p.push_back(choice("qol_skip_charging", "Battle charging",
        "Spells, items and Break Arts wind up faster before they take effect: Fast 4x, Skip as fast as it runs. "
        "The battle itself is unchanged.",
        &pc_qol_skip_charging, charging, 3));
    p.push_back(choice("gameplay_walls", "Cutaway walls",
        "Smart: walls facing the camera stay unless they hide Ashley. Original: the PS1's.", &pc_gameplay_walls, walls,
        2)
                    .grey(need_hw));
    p.push_back(toggle("gameplay_walls_ghost", "Ghost walls", "Walls in the way stay as a see-through ghost.",
        &pc_gameplay_walls_ghost)
                    .grey([](const Row& r) {
                        if (const char* why = need_hw(r))
                            return why;
                        return pc_gameplay_walls ? nullptr : "Needs Smart cutaway walls";
                    }));
    p.push_back(toggle("skip_intro", "Skip intro",
        "Start at the title, without the logos and the opening movie. From the next start.", &pc_skip_intro));
    p.push_back(game_option("map", "Map", "The small map in the corner: framed, plain or hidden. Saved with your game, not in settings.",
        PC_GAME_OPT_MAP, map, 3));
    p.push_back(game_option("ability_timing", "Ability timing",
        "Signals that help you time battle abilities. Saved with your game, not in settings.",
        PC_GAME_OPT_ABILITY_TIMING, OFF_ON, 2));
    p.push_back(game_option("weapon_status", "Weapon status",
        "Weapon stat changes shown in the lower-left corner. Saved with your game, not in settings.", PC_GAME_OPT_WEAPON_STATUS,
        OFF_ON, 2));
    p.push_back(game_option("armor_status", "Armor status",
        "Armor stat changes shown in the lower-left corner. Saved with your game, not in settings.", PC_GAME_OPT_ARMOR_STATUS,
        OFF_ON, 2));
    p.push_back(game_option("cursor_memory", "Cursor memory",
        "Menus open where you left them. Saved with your game, not in settings.", PC_GAME_OPT_CURSOR_MEMORY, OFF_ON, 2));
    p.push_back(game_option("information", "Information",
        "The help line at the bottom of the screen. Saved with your game, not in settings.", PC_GAME_OPT_INFORMATION, OFF_ON,
        2));
    p.push_back(game_option("puzzle_mode", "Puzzle mode",
        "Timed cube puzzles in rooms you have cleared. Saved with your game, not in settings.", PC_GAME_OPT_PUZZLE_MODE,
        OFF_ON, 2));
}

// The rows shown now on a page (indices into pages[page]).
int shown_index(int page, int row)
{
    build();
    if (page < 0 || page >= PC_UI_SETTINGS_PAGES || row < 0)
        return -1;
    int n = 0;
    const std::vector<Row>& rows = pages[page];
    for (size_t i = 0; i < rows.size(); i++)
        if (!rows[i].shown || rows[i].shown(rows[i])) {
            if (n == row)
                return (int)i;
            ++n;
        }
    return -1;
}
const Row* find(int page, int row)
{
    int i = shown_index(page, row);
    return i < 0 ? nullptr : &pages[page][(size_t)i];
}

// ---- values ----

int get_choice(const Row& r)
{
    if (r.get)
        return r.get(r);
    if (r.ichoices && r.ivar) {
        for (int i = 0; i < r.count; i++)
            if (r.ichoices[i] == *r.ivar)
                return i;
        return -1;
    }
    if (r.fchoices && r.fvar) {
        for (int i = 0; i < r.count; i++)
            if (std::fabs(r.fchoices[i] - *r.fvar) < 1e-4f || (r.fchoices[i] == 0 && *r.fvar <= 1))
                return i;
        return -1;
    }
    if (!r.ivar)
        return -1;
    if (r.kind == PC_UI_SET_TOGGLE)
        return *r.ivar != 0;
    return *r.ivar >= 0 && *r.ivar < r.count ? *r.ivar : -1;
}
void set_choice(const Row& r, int i)
{
    if (r.set)
        r.set(r, i);
    else if (r.key) {
        char value[16];
        std::snprintf(value, sizeof value, "%d", i);
        pc_lighting_set(r.key, r.values ? r.values[i] : value);
    } else if (r.ichoices && r.ivar)
        *r.ivar = r.ichoices[i];
    else if (r.fchoices && r.fvar)
        *r.fvar = r.fchoices[i];
    else if (r.ivar)
        *r.ivar = i;
}
float get_number(const Row& r)
{
    if (r.getf)
        return r.getf(r);
    if (r.fvar)
        return *r.fvar;
    return r.ivar ? (float)*r.ivar : 0.0f;
}
float snap(const Row& r, float v)
{
    if (r.step > 0)
        v = r.min + std::round((v - r.min) / r.step) * r.step;
    return v < r.min ? r.min : v > r.max ? r.max : v;
}
void set_number(const Row& r, float v)
{
    if (r.setf)
        r.setf(r, v);
    else if (r.fvar)
        *r.fvar = v;
    else if (r.ivar)
        *r.ivar = (int)std::lround(v);
}

bool dirty, stamp_pending;
double stamp;
// A drag on an on_release slider: the number shown until the drag ends (pc_ui_settings_preview).
struct {
    int page = -1, row = -1;
    float value = 0;
} preview;
void mark_dirty()
{
    dirty = true;
    stamp_pending = true;
}
// A row changed from System: its settings key is the user's now, even where
// an environment variable set it for this run (project_settings.cpp).
void touched(const Row& r)
{
    if (!pc_settings_touched)
        return;
    bool aa = !std::strcmp(r.id, "msaa") || !std::strcmp(r.id, "ssaa"); // parts of the aa key
    pc_settings_touched(aa ? "aa" : r.id);
}

// ---- binding text ----

void key_label(SDL_Keycode key, char* out, size_t size)
{
    static const struct {
        const char* name;
        const char* label;
    } labels[] = {{"return", "Enter"}, {"lshift", "Left Shift"}, {"rshift", "Right Shift"}, {"lctrl", "Left Ctrl"},
        {"rctrl", "Right Ctrl"}, {"lalt", "Left Alt"}, {"ralt", "Right Alt"}, {"capslock", "Caps Lock"},
        {"pageup", "Page Up"}, {"pagedown", "Page Down"}, {"kp_enter", "Keypad Enter"}, {"kp_plus", "Keypad +"},
        {"minus", "-"}, {"equals", "="}, {"comma", ","}, {"period", "."}, {"slash", "/"}, {"backslash", "\\"},
        {"semicolon", ";"}, {"apostrophe", "'"}, {"leftbracket", "["}, {"rightbracket", "]"}, {"grave", "`"}};
    const char* name = pc_key_name(key);
    if (!name) {
        std::snprintf(out, size, "Key %d", (int)key);
        return;
    }
    for (const auto& l : labels)
        if (!std::strcmp(name, l.name)) {
            std::snprintf(out, size, "%s", l.label);
            return;
        }
    std::snprintf(out, size, "%s", name);
    bool f_key = name[0] == 'f' && std::isdigit((unsigned char)name[1]);
    for (char* p = out; *p; p++)
        if (p == out || f_key || !name[1])
            *p = (char)std::toupper((unsigned char)*p);
}
void pad_label(int element, char* out, size_t size)
{
    static const struct {
        const char* name;
        const char* label;
    } labels[] = {{"south", "South"}, {"east", "East"}, {"west", "West"}, {"north", "North"}, {"back", "Back"},
        {"guide", "Guide"}, {"start", "Start"}, {"leftstick", "Left stick"}, {"rightstick", "Right stick"},
        {"leftshoulder", "Left shoulder"}, {"rightshoulder", "Right shoulder"}, {"dpup", "D-pad up"},
        {"dpdown", "D-pad down"}, {"dpleft", "D-pad left"}, {"dpright", "D-pad right"}, {"misc1", "Misc"},
        {"paddle1", "Paddle 1"}, {"paddle2", "Paddle 2"}, {"paddle3", "Paddle 3"}, {"paddle4", "Paddle 4"},
        {"touchpad", "Touchpad"}, {"lefttrigger", "Left trigger"}, {"righttrigger", "Right trigger"},
        {"none", "None"}};
    const char* name = pc_pad_element_name(element);
    for (const auto& l : labels)
        if (!std::strcmp(name, l.name)) {
            std::snprintf(out, size, "%s", l.label);
            return;
        }
    std::snprintf(out, size, "%s", name);
}
void binding_text(int button, pc_ui_setting_row* out)
{
    out->keys[0] = 0;
    for (int k = 0; k < PC_BIND_KEYS; k++) {
        if (!pc_bind_keys[button][k])
            continue;
        char label[32];
        key_label(pc_bind_keys[button][k], label, sizeof label);
        size_t n = std::strlen(out->keys);
        std::snprintf(out->keys + n, sizeof out->keys - n, "%s%s", n ? ", " : "", label);
    }
    if (!out->keys[0])
        std::snprintf(out->keys, sizeof out->keys, "None");
    pad_label(pc_bind_pad[button], out->pad, sizeof out->pad);
    std::snprintf(out->value, sizeof out->value, "%s / %s", out->keys, out->pad);
}
// "bind_key_<button> a,b" / "none" through the settings parser (a key given
// twice is written once).
void write_keys(int button, SDL_Keycode a, SDL_Keycode b)
{
    std::string value;
    if (b == a)
        b = 0;
    for (SDL_Keycode k : {a, b}) {
        const char* name = k ? pc_key_name(k) : nullptr;
        if (name)
            value += (value.empty() ? "" : ",") + std::string(name);
    }
    std::string key = std::string("bind_key_") + pc_pad_button_names[button];
    pc_binding_setting(key.c_str(), value.empty() ? "none" : value.c_str());
}

void json_text(std::string& s, const char* text)
{
    s += '"';
    for (const char* p = text ? text : ""; *p; p++)
        if (*p == '"' || *p == '\\')
            s += '\\', s += *p;
        else if ((unsigned char)*p >= 32)
            s += *p;
    s += '"';
}
} // namespace

extern "C" int pc_ui_settings_pages(void) { return PC_UI_SETTINGS_PAGES; }
extern "C" const char* pc_ui_settings_page_title(int page)
{
    return page >= 0 && page < PC_UI_SETTINGS_PAGES ? PAGE_TITLES[page] : nullptr;
}
extern "C" const char* pc_ui_settings_page_id(int page)
{
    return page >= 0 && page < PC_UI_SETTINGS_PAGES ? PAGE_IDS[page] : nullptr;
}
extern "C" int pc_ui_settings_rows(int page)
{
    build();
    if (page < 0 || page >= PC_UI_SETTINGS_PAGES)
        return 0;
    int n = 0;
    for (const Row& r : pages[page])
        n += !r.shown || r.shown(r);
    return n;
}

extern "C" int pc_ui_settings_row(int page, int row, pc_ui_setting_row* out)
{
    const Row* r = find(page, row);
    if (!r || !out)
        return 0;
    std::memset(out, 0, sizeof *out);
    out->id = r->id;
    out->label = r->label;
    out->help = r->help;
    out->kind = r->kind;
    out->button = -1;
    out->index = -1;
    out->game_option = r->game;
    if (r->swatch) {
        out->swatch = 1;
        r->swatch(*r, out->swatch_rgb);
    }
    const char* why = r->why ? r->why(*r) : nullptr;
    out->enabled = why == nullptr;
    if (why)
        std::snprintf(out->reason, sizeof out->reason, "%s", why);
    switch (r->kind) {
    case PC_UI_SET_CHOICE:
    case PC_UI_SET_TOGGLE: {
        out->count = r->count;
        // A greyed hook row (its feature is not in yet) shows its default.
        int i = r->hook && why ? (int)r->def : get_choice(*r);
        out->index = i;
        std::snprintf(out->value, sizeof out->value, "%s", i >= 0 && i < r->count ? r->names[i] : "?");
        if (!why && i >= 0 && r->choice_why)
            if (const char* note = r->choice_why(*r, i))
                std::snprintf(out->reason, sizeof out->reason, "%s", note);
        break;
    }
    case PC_UI_SET_SLIDER: {
        out->min = r->min, out->max = r->max, out->step = r->step;
        float v = r->hook && why ? r->def : get_number(*r);
        if (preview.page == page && preview.row == row && !why)
            v = preview.value; // a drag in progress: its number
        out->number = v;
        if (r->zero && v <= r->min)
            std::snprintf(out->value, sizeof out->value, "%s", r->zero);
        else if (r->integer)
            std::snprintf(out->value, sizeof out->value, r->format, (int)std::lround(v));
        else
            std::snprintf(out->value, sizeof out->value, r->format, (double)v);
        break;
    }
    case PC_UI_SET_BINDING:
        out->button = r->arg;
        binding_text(r->arg, out);
        break;
    case PC_UI_SET_INFO:
        if (r->info)
            r->info(*r, out->value, (int)sizeof out->value);
        break;
    default:
        break;
    }
    return 1;
}

extern "C" int pc_ui_settings_choice(int page, int row, int choice, const char** name, const char** reason)
{
    const Row* r = find(page, row);
    if (!r || (r->kind != PC_UI_SET_CHOICE && r->kind != PC_UI_SET_TOGGLE) || choice < 0 || choice >= r->count)
        return 0;
    if (name)
        *name = r->names[choice];
    if (reason)
        *reason = r->choice_why ? r->choice_why(*r, choice) : nullptr;
    return 1;
}

static int apply_choice(const Row& r, int target)
{
    int before = get_choice(r);
    if (target == before)
        return 0;
    set_choice(r, target);
    if (get_choice(r) == before)
        return 0;
    if (r.saved) {
        touched(r);
        mark_dirty();
    }
    return 1;
}
static int apply_number(const Row& r, float v)
{
    float before = get_number(r);
    v = snap(r, v);
    if (std::fabs(v - before) < 1e-5f)
        return 0;
    set_number(r, v);
    if (std::fabs(get_number(r) - before) < 1e-5f)
        return 0;
    if (r.saved) {
        touched(r);
        mark_dirty();
    }
    return 1;
}

extern "C" int pc_ui_settings_change(int page, int row, int delta)
{
    const Row* r = find(page, row);
    if (!r || !delta || (r->why && r->why(*r)))
        return 0;
    if (r->kind == PC_UI_SET_SLIDER)
        return apply_number(*r, snap(*r, get_number(*r)) + (float)delta * r->step);
    if (r->kind != PC_UI_SET_CHOICE && r->kind != PC_UI_SET_TOGGLE)
        return 0;
    int current = get_choice(*r);
    int step = delta > 0 ? 1 : -1;
    int i = current >= 0 ? current : (step > 0 ? -1 : r->count);
    for (int tries = 0; tries < r->count; tries++) {
        i = (i + step + r->count) % r->count;
        if (i == current)
            return 0;
        if (!r->choice_why || !r->choice_why(*r, i))
            return apply_choice(*r, i);
    }
    return 0;
}

extern "C" int pc_ui_settings_set(int page, int row, double value)
{
    const Row* r = find(page, row);
    if (preview.page == page && preview.row == row)
        preview.page = preview.row = -1;
    if (!r || (r->why && r->why(*r)))
        return 0;
    if (r->kind == PC_UI_SET_SLIDER)
        return apply_number(*r, (float)value);
    if (r->kind != PC_UI_SET_CHOICE && r->kind != PC_UI_SET_TOGGLE)
        return 0;
    int i = (int)value;
    if (i < 0 || i >= r->count || (r->choice_why && r->choice_why(*r, i)))
        return 0;
    return apply_choice(*r, i);
}

extern "C" int pc_ui_settings_preview(int page, int row, double value)
{
    const Row* r = find(page, row);
    if (!r || (r->why && r->why(*r)))
        return 0;
    if (r->kind != PC_UI_SET_SLIDER || !r->on_release)
        return pc_ui_settings_set(page, row, value);
    float v = snap(*r, (float)value);
    bool changed = preview.page != page || preview.row != row || preview.value != v;
    preview.page = page, preview.row = row, preview.value = v;
    return changed;
}
// A drag that never ended (the page or the menu closed under it) applies.
static void apply_preview()
{
    if (preview.page < 0)
        return;
    int page = preview.page, row = preview.row;
    pc_ui_settings_set(page, row, preview.value);
}

extern "C" int pc_ui_settings_activate(int page, int row)
{
    const Row* r = find(page, row);
    if (!r || (r->why && r->why(*r)))
        return 0;
    if (r->kind == PC_UI_SET_ACTION && r->run) {
        r->run(*r);
        if (r->saved)
            mark_dirty();
        return 1;
    }
    if (r->kind == PC_UI_SET_BINDING && pc_ui_capture_begin && pc_ui_capture_begin()) {
        capture_button = r->arg;
        capture_note[0] = 0;
        return 1;
    }
    return 0;
}

static int saves; // pc_ui_settings_saves
static void save_now()
{
    if (!dirty)
        return;
    dirty = stamp_pending = false;
    pc_settings_save();
    ++saves;
}
extern "C" void pc_ui_settings_flush(void)
{
    apply_preview();
    save_now();
}
extern "C" int pc_ui_settings_saves(void) { return saves; }
extern "C" void pc_ui_settings_tick(double now)
{
    ticked = now;
    if (swap_note_pending) {
        swap_note_since = now;
        swap_note_pending = false;
    }
    if (stamp_pending) {
        stamp = now;
        stamp_pending = false;
    }
    if (dirty && now - stamp >= 1.0)
        save_now(); // (a slider drag still in progress stays a preview)
}
extern "C" int pc_ui_settings_dirty(void) { return dirty; }

extern "C" const char* pc_ui_settings_key_reserved(int code)
{
    switch ((SDL_Keycode)code) {
    case SDLK_ESCAPE:
        return "Esc opens the editor and cancels this";
    case SDLK_F1:
        return "F1 opens the editor";
    case SDLK_F2:
        return "F2 is the editor's free camera";
    case SDLK_LALT:
    case SDLK_RALT:
    case SDLK_LGUI:
    case SDLK_RGUI:
        return "Alt and Super belong to the desktop";
    case SDLK_J:
    case SDLK_L:
    case SDLK_I:
    case SDLK_K:
    case SDLK_O:
        return pc_camera_stick_keys ? "J, L, I, K and O move the camera (Controls > Camera keys)" : nullptr;
    default:
        return nullptr;
    }
}

extern "C" int pc_ui_settings_bind(int button, int kind, int code)
{
    if (button < 0 || button >= PC_PAD_BUTTONS)
        return -2;
    int other = -1;
    char moved[32] = "", gets[32] = "";
    if (kind == PC_UI_CAPTURE_KEY) {
        SDL_Keycode key = (SDL_Keycode)code;
        if (!key || !pc_key_name(key) || pc_ui_settings_key_reserved(code))
            return -2;
        SDL_Keycode first = pc_bind_keys[button][0], second = pc_bind_keys[button][1];
        if (first == key)
            return -1;
        if (second == key) { // its own second key becomes the first
            write_keys(button, key, first);
            mark_dirty();
            return -1;
        }
        // The first other button with the key takes this one's first key in its place (or none); any
        // further one (a hand-edited file) just loses it.
        for (int b = 0; b < PC_PAD_BUTTONS; b++)
            for (int k = 0; k < PC_BIND_KEYS; k++)
                if (b != button && pc_bind_keys[b][k] == key) {
                    SDL_Keycode keys[PC_BIND_KEYS] = {pc_bind_keys[b][0], pc_bind_keys[b][1]};
                    keys[k] = other < 0 ? first : 0;
                    if (other < 0)
                        other = b;
                    write_keys(b, keys[0], keys[1]);
                    break;
                }
        if (other >= 0) {
            key_label(key, moved, sizeof moved);
            if (first)
                key_label(first, gets, sizeof gets);
        }
        write_keys(button, key, second);
    } else if (kind == PC_UI_CAPTURE_PAD) {
        const char* name = pc_pad_element_name(code);
        if (code == PC_PAD_NONE || pc_pad_element_from_name(name) != code)
            return -2;
        int old = pc_bind_pad[button];
        if (old == code)
            return -1;
        for (int b = 0; b < PC_PAD_BUTTONS; b++)
            if (b != button && pc_bind_pad[b] == code) {
                other = b;
                std::string key = std::string("bind_pad_") + pc_pad_button_names[b];
                pc_binding_setting(key.c_str(), pc_pad_element_name(old));
                pad_label(code, moved, sizeof moved);
                if (old != PC_PAD_NONE)
                    pad_label(old, gets, sizeof gets);
                break;
            }
        std::string key = std::string("bind_pad_") + pc_pad_button_names[button];
        pc_binding_setting(key.c_str(), name);
    } else
        return -2;
    if (other >= 0) {
        int n = std::snprintf(swap_note, sizeof swap_note, "%s moved from %s to %s", moved, BUTTON_LABELS[other],
            BUTTON_LABELS[button]);
        if (gets[0] && n > 0 && n < (int)sizeof swap_note)
            std::snprintf(swap_note + n, sizeof swap_note - (size_t)n, "; %s gets %s", BUTTON_LABELS[other], gets);
        swap_note_pending = true;
    }
    mark_dirty();
    return other;
}
extern "C" const char* pc_ui_settings_note(void)
{
    return swap_note[0] && (swap_note_pending || ticked - swap_note_since < 3.0) ? swap_note : nullptr;
}
extern "C" const char* pc_ui_settings_capture_note(void)
{
    return capture_button >= 0 && capture_note[0] ? capture_note : nullptr;
}
extern "C" int pc_ui_settings_capture_refuse(int kind, int code)
{
    const char* why = kind == PC_UI_CAPTURE_KEY ? pc_ui_settings_key_reserved(code) : nullptr;
    if (!why || capture_button < 0)
        return 0;
    char label[32];
    key_label((SDL_Keycode)code, label, sizeof label);
    std::snprintf(capture_note, sizeof capture_note, "%s: %s", label, why);
    return 1;
}
extern "C" int pc_ui_settings_capture_button(void) { return capture_button; }
extern "C" int pc_ui_settings_capture_done(int kind, int code)
{
    int button = capture_button;
    capture_button = -1;
    if (button < 0)
        return -2;
    if (kind < 0)
        return -1;
    return pc_ui_settings_bind(button, kind, code);
}

extern "C" int pc_ui_settings_json(int page, char* out, int size)
{
    if (!out || size <= 0)
        return 0;
    std::string s = "{\"page\":";
    json_text(s, pc_ui_settings_page_id(page));
    s += ",\"title\":";
    json_text(s, pc_ui_settings_page_title(page));
    s += ",\"dirty\":";
    s += dirty ? "true" : "false";
    s += ",\"rows\":[";
    static const char* const kinds[] = {"choice", "toggle", "slider", "action", "binding", "info"};
    int n = pc_ui_settings_rows(page);
    for (int i = 0; i < n; i++) {
        pc_ui_setting_row r;
        pc_ui_settings_row(page, i, &r);
        s += i ? ",{\"id\":" : "{\"id\":";
        json_text(s, r.id);
        s += ",\"label\":";
        json_text(s, r.label);
        s += ",\"kind\":\"";
        s += kinds[r.kind];
        s += "\",\"value\":";
        json_text(s, r.value);
        s += ",\"enabled\":";
        s += r.enabled ? "true" : "false";
        if (r.reason[0]) {
            s += ",\"reason\":";
            json_text(s, r.reason);
        }
        char extra[160];
        if (r.kind == PC_UI_SET_CHOICE || r.kind == PC_UI_SET_TOGGLE)
            std::snprintf(extra, sizeof extra, ",\"index\":%d,\"count\":%d", r.index, r.count);
        else if (r.kind == PC_UI_SET_SLIDER)
            std::snprintf(extra, sizeof extra, ",\"number\":%g,\"min\":%g,\"max\":%g,\"step\":%g", (double)r.number,
                (double)r.min, (double)r.max, (double)r.step);
        else if (r.kind == PC_UI_SET_BINDING)
            std::snprintf(extra, sizeof extra, ",\"button\":%d", r.button);
        else
            extra[0] = 0;
        s += extra;
        if (r.swatch) {
            std::snprintf(extra, sizeof extra, ",\"swatch\":\"#%02x%02x%02x\"", r.swatch_rgb[0], r.swatch_rgb[1],
                r.swatch_rgb[2]);
            s += extra;
        }
        if (r.game_option)
            s += ",\"game\":true";
        s += "}";
    }
    s += "]}";
    std::snprintf(out, (size_t)size, "%s", s.c_str());
    return (int)s.size() < size ? (int)s.size() : size - 1;
}
