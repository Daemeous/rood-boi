// The System tab's settings model (pc/src/ui/ui_settings.cpp) without the
// game: pages and rows, value text, wrap and clamp, greyed rows and their
// reasons, the debounced save, bindings. Built twice (CMakeLists.txt): with
// the game options bridge and without the feature hooks, and the other way
// round (UI_SETTINGS_PROBE_HOOKS).
#include "ui/ui_settings.h"
#include "ui/game_options.h"
#include "antialias.h"
#include <SDL3/SDL_gamepad.h> /* before boot_controller.h: SDL keeps its own linkage */
#include <SDL3/SDL_keycode.h>
extern "C" {
#include "boot_controller.h" /* C declarations without extern "C" */
}
#include "button_prompts.h"
#include "camera_stick.h"
#include "lighting_quality.h"
#include "ui/ui_tint.h"
#include "ui_settings_stubs.h"
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
extern int pc_hw_active, pc_hw_interpolate, pc_hw_scale_setting, pc_gl_aspect, pc_gte_widescreen, pc_ui_menus,
    pc_gameplay_walls, pc_button_prompts, pc_qol_map_invert;
}

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static int find(int page, const char* id)
{
    pc_ui_setting_row r;
    for (int i = 0; i < pc_ui_settings_rows(page); i++)
        if (pc_ui_settings_row(page, i, &r) && !std::strcmp(r.id, id))
            return i;
    return -1;
}
static pc_ui_setting_row row(int page, const char* id)
{
    pc_ui_setting_row r;
    std::memset(&r, 0, sizeof r);
    int i = find(page, id);
    if (i < 0 || !pc_ui_settings_row(page, i, &r))
        std::printf("no row %s on page %d\n", id, page);
    return r;
}
static std::string value(int page, const char* id) { return row(page, id).value; }
static int change(int page, const char* id, int delta) { return pc_ui_settings_change(page, find(page, id), delta); }

enum { G = PC_UI_SETTINGS_GRAPHICS, D = PC_UI_SETTINGS_DISPLAY, A = PC_UI_SETTINGS_AUDIO,
    I = PC_UI_SETTINGS_INTERFACE, C = PC_UI_SETTINGS_CONTROLS, B = PC_UI_SETTINGS_BINDINGS,
    P = PC_UI_SETTINGS_GAMEPLAY };

#ifdef UI_SETTINGS_PROBE_HOOKS
const bool game = false;
#else
const bool game = true;
#endif

static void pages()
{
    CHECK(pc_ui_settings_pages() == 7);
    const char* titles[] = {"Graphics", "Display", "Audio", "Interface", "Controls", "Bindings", "Gameplay"};
    for (int p = 0; p < 7; p++)
        CHECK(!std::strcmp(pc_ui_settings_page_title(p), titles[p]));
    CHECK(!std::strcmp(pc_ui_settings_page_id(D), "display"));
    CHECK(pc_ui_settings_page_title(7) == nullptr);
    CHECK(pc_ui_settings_rows(G) == 15);
    CHECK(pc_ui_settings_rows(D) == 8); // Images per vsync shows: interpolation is on
    CHECK(pc_ui_settings_rows(A) == (game ? 2 : 1));
    CHECK(pc_ui_settings_rows(I) == 15); // with Menu opacity, Menu tint, Tint red, green, blue
    CHECK(find(I, "ui_menu_tint") == 2 && find(I, "ui_menu_tint_b") == 5);
    CHECK(pc_ui_settings_rows(C) == (game ? 16 : 15));
    // the look inversions after the stick camera (qol_fp_invert_y, qol_map_invert)
    CHECK(find(C, "qol_fp_invert_y") == find(C, "reset_camera") + 1 && find(C, "qol_map_invert") == find(C, "reset_camera") + 2);
    CHECK(value(C, "qol_map_invert") == "Off" && change(C, "qol_map_invert", 1) == 1 && pc_qol_map_invert == 1);
    pc_qol_map_invert = 0;
    CHECK(pc_ui_settings_rows(B) == 17);
    CHECK(pc_ui_settings_rows(P) == (game ? 11 : 4));
    CHECK(pc_ui_settings_rows(-1) == 0 && pc_ui_settings_rows(7) == 0);
    pc_ui_setting_row r;
    CHECK(!pc_ui_settings_row(G, 99, &r));
    // Images per vsync only while interpolating
    pc_hw_interpolate = 0;
    CHECK(pc_ui_settings_rows(D) == 7);
    CHECK(find(D, "present_rate") < 0);
    pc_hw_interpolate = 1;
    CHECK(find(D, "present_rate") == 4);
    // game options: there with the bridge, gone without
    CHECK((find(P, "puzzle_mode") >= 0) == game);
    CHECK((find(A, "sound") >= 0) == game);
    CHECK((find(C, "vibration") >= 0) == game);
}

static void values()
{
    // toggle
    CHECK(value(D, "widescreen") == "Off");
    pc_ui_setting_row w = row(D, "widescreen");
    CHECK(w.kind == PC_UI_SET_TOGGLE && w.count == 2 && w.index == 0 && w.enabled && !w.reason[0]);
    CHECK(change(D, "widescreen", 1) == 1);
    CHECK(pc_gte_widescreen == 1 && value(D, "widescreen") == "On");
    CHECK(change(D, "widescreen", 1) == 1 && pc_gte_widescreen == 0); // flips either way
    // choice wraps
    CHECK(value(D, "aspect") == "Keep aspect");
    CHECK(change(D, "aspect", -1) == 1 && pc_gl_aspect == 2 && value(D, "aspect") == "Integer scale");
    CHECK(change(D, "aspect", 1) == 1 && pc_gl_aspect == 0);
    const char* name = nullptr;
    const char* why = nullptr;
    CHECK(pc_ui_settings_choice(D, find(D, "aspect"), 1, &name, &why) && !std::strcmp(name, "Stretch to window") && !why);
    CHECK(!pc_ui_settings_choice(D, find(D, "aspect"), 3, &name, &why));
    // set (mouse)
    CHECK(pc_ui_settings_set(D, find(D, "filter"), 0) == 1 && value(D, "filter") == "Nearest");
    CHECK(pc_ui_settings_set(D, find(D, "filter"), 0) == 0);
    CHECK(pc_ui_settings_set(D, find(D, "filter"), 5) == 0);
    // slider through pc_hw_set_scale, clamped
    CHECK(value(G, "internal_resolution") == "4x");
    CHECK(change(G, "internal_resolution", 1) == 1 && stub.last_scale == 5 && pc_hw_scale_setting == 5);
    CHECK(change(G, "internal_resolution", 10) == 1 && stub.last_scale == 8);
    int calls = stub.set_scale_calls;
    CHECK(change(G, "internal_resolution", 1) == 0 && stub.set_scale_calls == calls); // at the top
    CHECK(pc_ui_settings_set(G, find(G, "internal_resolution"), 0) == 1 && pc_hw_scale_setting == 1);
    pc_ui_settings_set(G, find(G, "internal_resolution"), 4);
    pc_ui_setting_row s = row(G, "internal_resolution");
    CHECK(s.kind == PC_UI_SET_SLIDER && s.min == 1 && s.max == 8 && s.step == 1 && s.number == 4);
    // a pointer drag previews it (the targets rebuild once, at its end); other sliders apply as they go
    calls = stub.set_scale_calls;
    int ir = find(G, "internal_resolution");
    CHECK(pc_ui_settings_preview(G, ir, 2.4) == 1 && value(G, "internal_resolution") == "2x");
    CHECK(pc_ui_settings_preview(G, ir, 6) == 1 && row(G, "internal_resolution").number == 6);
    CHECK(stub.set_scale_calls == calls && pc_hw_scale_setting == 4);
    CHECK(pc_ui_settings_set(G, ir, 6) == 1 && stub.set_scale_calls == calls + 1 && pc_hw_scale_setting == 6);
    CHECK(value(G, "internal_resolution") == "6x");
    pc_ui_settings_preview(G, ir, 3); // a drag the menu closed under: the flush applies it
    pc_ui_settings_flush();
    CHECK(pc_hw_scale_setting == 3 && value(G, "internal_resolution") == "3x");
    pc_ui_settings_set(G, ir, 4);
    CHECK(pc_ui_settings_preview(C, find(C, "camera_stick_speed"), 150) == 1 && pc_camera_stick_speed == 150.0f);
    pc_ui_settings_set(C, find(C, "camera_stick_speed"), 120);
    // float slider with steps; text at the minimum
    CHECK(value(C, "camera_stick_speed") == "120 deg/s");
    CHECK(change(C, "camera_stick_speed", 1) == 1 && pc_camera_stick_speed == 135.0f);
    CHECK(value(C, "camera_stick_idle_reset") == "Off");
    CHECK(change(C, "camera_stick_idle_reset", 2) == 1 && value(C, "camera_stick_idle_reset") == "after 2 s");
    CHECK(pc_ui_settings_set(I, find(I, "button_prompts_scale"), 0.83) == 1);
    CHECK(value(I, "button_prompts_scale") == "0.85");
    CHECK(value(I, "ui_outline_boost") == "Off");
    CHECK(change(I, "ui_outline_boost", 1) == 1 && value(I, "ui_outline_boost") == "+1 px");
    // present_rate text
    CHECK(value(D, "present_rate") == "1");
    // renderer info
    pc_ui_setting_row info = row(D, "renderer");
    CHECK(info.kind == PC_UI_SET_INFO && std::string(info.value) == "OpenGL");
    CHECK(change(D, "renderer", 1) == 0 && pc_ui_settings_activate(D, find(D, "renderer")) == 0);
}

static void lighting_and_aa()
{
    // preset through pc_lighting_set; Custom is skipped
    CHECK(value(G, "lighting_preset") == "High");
    CHECK(change(G, "lighting_preset", 1) == 1 && !std::strcmp(stub.last_lighting, "lighting_preset ultra"));
    CHECK(change(G, "lighting_preset", 1) == 1 && value(G, "lighting_preset") == "Original"); // past Custom
    pc_lighting_preset = PC_PRESET_CUSTOM;
    CHECK(value(G, "lighting_preset") == "Custom");
    CHECK(change(G, "lighting_preset", -1) == 1 && value(G, "lighting_preset") == "Ultra");
    // GI: Screen-space never, Path traced not without ray tracing
    CHECK(value(G, "gi_method") == "Probes");
    CHECK(change(G, "gi_method", 1) == 1 && value(G, "gi_method") == "Off"); // Path traced skipped, wraps
    CHECK(change(G, "gi_method", 1) == 1 && value(G, "gi_method") == "Probes"); // Screen-space skipped
    const char* why = nullptr;
    CHECK(pc_ui_settings_choice(G, find(G, "gi_method"), 3, nullptr, &why) && why && std::strstr(why, "ray queries"));
    stub.traced = 1;
    CHECK(change(G, "gi_method", 1) == 1 && value(G, "gi_method") == "Path traced");
    CHECK(!std::strcmp(stub.last_lighting, "gi_method path"));
    stub.traced = 0;
    // the current choice cannot run here: still enabled, with a note
    pc_ui_setting_row gi = row(G, "gi_method");
    CHECK(gi.enabled && std::strstr(gi.reason, "ray queries"));
    // toggles through pc_lighting_set
    CHECK(change(G, "dynamic_lights", 1) == 1 && !std::strcmp(stub.last_lighting, "dynamic_lights 0"));
    pc_ui_setting_row sh = row(G, "shadows");
    CHECK(!sh.enabled && !std::strcmp(sh.reason, "Needs dynamic lights on"));
    CHECK(change(G, "shadows", 1) == 0);
    change(G, "dynamic_lights", 1);
    // MSAA: 8x is past this backend's maximum (4)
    CHECK(value(G, "msaa") == "Off");
    CHECK(change(G, "msaa", -1) == 1 && pc_aa_msaa == 4); // 8x skipped
    CHECK(change(G, "msaa", 1) == 1 && pc_aa_msaa == 0);
    // supersampling choices; the downsample filter greyed without it
    CHECK(!row(G, "aa_downsample").enabled && !std::strcmp(row(G, "aa_downsample").reason, "Needs supersampling"));
    CHECK(change(G, "ssaa", 1) == 1 && pc_aa_ssaa == 1.5f && value(G, "ssaa") == "1.5x");
    CHECK(row(G, "aa_downsample").enabled);
    CHECK(change(G, "ssaa", -1) == 1 && pc_aa_ssaa == 0);
    // the post filter
    CHECK(value(G, "aa") == "FXAA");
    CHECK(change(G, "aa", 1) == 1 && pc_gl_aa == PC_AA_SMAA);
}

static void greyed()
{
    // no hardware renderer
    pc_hw_active = 0;
    pc_ui_setting_row r = row(G, "internal_resolution");
    CHECK(!r.enabled && !std::strcmp(r.reason, "Needs the hardware renderer"));
    int calls = stub.set_scale_calls;
    CHECK(change(G, "internal_resolution", 1) == 0 && stub.set_scale_calls == calls);
    CHECK(pc_ui_settings_set(G, find(G, "internal_resolution"), 2) == 0);
    CHECK(!row(I, "ui_text").enabled && row(I, "ui_menus").enabled);
    CHECK(row(D, "widescreen").enabled && !row(D, "interpolate").enabled);
    CHECK(find(D, "present_rate") < 0);
    CHECK(std::string(row(D, "renderer").value) == "OpenGL, software");
    pc_hw_active = 1;
    // dependent rows
    pc_camera_stick = 0;
    r = row(C, "camera_stick_speed");
    CHECK(!r.enabled && !std::strcmp(r.reason, "Needs Right stick camera on"));
    CHECK(!row(C, "reset_camera").enabled && pc_ui_settings_activate(C, find(C, "reset_camera")) == 0);
    pc_camera_stick = 1;
    pc_camera_stick_zoom = 0;
    CHECK(!std::strcmp(row(C, "camera_stick_zoom_speed").reason, "Needs Stick zooms on"));
    pc_camera_stick_zoom = 1;
    CHECK(row(C, "camera_stick_zoom_speed").enabled);
    pc_button_prompts = 5;
    CHECK(!row(I, "button_prompts_style").enabled);
    pc_button_prompts = 0;
    CHECK(!row(P, "gameplay_walls_ghost").enabled);
    pc_gameplay_walls = 1;
    CHECK(row(P, "gameplay_walls_ghost").enabled);
    pc_gameplay_walls = 0;
    // the hooks of later features
    pc_ui_setting_row wm = row(D, "window_mode"), br = row(D, "brightness"), mv = row(A, "master_volume");
    pc_ui_setting_row bind = row(B, "bind_circle");
    if (game) {
        CHECK(!wm.enabled && !std::strcmp(wm.reason, "Coming in this phase") && wm.index == 0
            && std::string(wm.value) == "Windowed");
        CHECK(!br.enabled && br.number == 50 && std::string(br.value) == "50");
        CHECK(!mv.enabled && std::string(mv.value) == "100%");
        CHECK(!bind.enabled && !std::strcmp(bind.reason, "Coming in this phase"));
        CHECK(change(D, "window_mode", 1) == 0 && change(D, "brightness", 1) == 0);
        CHECK(pc_ui_settings_activate(B, find(B, "bind_circle")) == 0 && pc_ui_settings_capture_button() == -1);
    } else {
        CHECK(wm.enabled && br.enabled && mv.enabled && bind.enabled);
        CHECK(change(D, "window_mode", -1) == 1 && stub.window_mode == 2 && value(D, "window_mode") == "Fullscreen");
        CHECK(change(D, "brightness", 1) == 1 && stub.brightness == 55 && value(D, "brightness") == "55");
        CHECK(change(A, "master_volume", 1) == 0); // at 100
        CHECK(change(A, "master_volume", -2) == 1 && stub.volume == 90 && value(A, "master_volume") == "90%");
        pc_ui_settings_flush();
    }
}

static void saving()
{
    pc_ui_settings_flush();
    int saves = stub.saves;
    CHECK(!pc_ui_settings_dirty());
    // a change saves after one quiet second
    change(D, "widescreen", 1);
    CHECK(pc_ui_settings_dirty());
    pc_ui_settings_tick(10.0);
    pc_ui_settings_tick(10.9);
    CHECK(stub.saves == saves);
    change(D, "widescreen", 1); // another change restarts the second
    pc_ui_settings_tick(10.95);
    pc_ui_settings_tick(11.5);
    CHECK(stub.saves == saves);
    pc_ui_settings_tick(11.95);
    CHECK(stub.saves == saves + 1 && !pc_ui_settings_dirty());
    pc_ui_settings_tick(20.0);
    CHECK(stub.saves == saves + 1); // nothing more to save
    // flush (leaving the page) saves at once, once
    change(I, "ui_menus", 1);
    CHECK(pc_ui_menus == 1 && value(I, "ui_menus") == "Modern");
    CHECK(pc_ui_settings_dirty());
    pc_ui_settings_flush();
    CHECK(stub.saves == saves + 2);
    pc_ui_settings_flush();
    CHECK(stub.saves == saves + 2);
    change(I, "ui_menus", 1);
    CHECK(pc_ui_menus == 0);
    pc_ui_settings_flush();
    CHECK(stub.saves == saves + 3);
    // no change, nothing to save
    CHECK(change(D, "renderer", 1) == 0 && !pc_ui_settings_dirty());
    // actions that only act (Reset camera) do not save
    CHECK(pc_ui_settings_activate(C, find(C, "reset_camera")) == 1 && !pc_ui_settings_dirty());
}

static void game_options()
{
    if (!game)
        return;
    int saves = stub.saves;
    pc_ui_setting_row map = row(P, "map");
    CHECK(map.kind == PC_UI_SET_CHOICE && map.count == 3 && std::string(map.value) == "Frame" && map.game_option);
    CHECK(change(P, "map", 1) == 1 && stub.game[PC_GAME_OPT_MAP] == 1 && value(P, "map") == "Simple");
    CHECK(change(P, "map", 2) == 1 && value(P, "map") == "Off");
    CHECK(change(P, "map", 1) == 1 && value(P, "map") == "Frame"); // wraps
    CHECK(!pc_ui_settings_dirty()); // saved with the game, not settings.cfg
    CHECK(value(A, "sound") == "Stereo" && change(A, "sound", 1) == 1 && stub.game[PC_GAME_OPT_SOUND] == 1);
    CHECK(value(P, "ability_timing") == "On" && change(P, "ability_timing", 1) == 1 && value(P, "ability_timing") == "Off");
    // vibration: greyed
    pc_ui_setting_row v = row(C, "vibration");
    CHECK(!v.enabled && !std::strcmp(v.reason, "No effect on PC yet") && std::string(v.value) == "On");
    int sets = stub.game_sets;
    CHECK(change(C, "vibration", 1) == 0 && stub.game_sets == sets);
    // puzzle mode greyed while the game locks it
    CHECK(row(P, "puzzle_mode").enabled);
    stub.puzzle_disabled = 1;
    pc_ui_setting_row pz = row(P, "puzzle_mode");
    CHECK(!pz.enabled && std::strstr(pz.reason, "middle of a puzzle"));
    CHECK(change(P, "puzzle_mode", 1) == 0);
    stub.puzzle_disabled = 0;
    CHECK(change(P, "puzzle_mode", 1) == 1 && stub.game[PC_GAME_OPT_PUZZLE_MODE] == 0);
    pc_ui_settings_tick(100);
    pc_ui_settings_tick(102);
    CHECK(stub.saves == saves);
}

static void bindings()
{
    pc_bindings_reset();
    pc_ui_settings_flush();
    int saves = stub.saves;
    // order: Circle first
    pc_ui_setting_row c;
    CHECK(pc_ui_settings_row(B, 0, &c) && !std::strcmp(c.id, "bind_circle") && c.kind == PC_UI_SET_BINDING);
    CHECK(c.button == PC_PROMPT_CIRCLE && !std::strcmp(c.label, "Circle"));
    CHECK(!std::strcmp(c.keys, "Z") && !std::strcmp(c.pad, "East") && !std::strcmp(c.value, "Z / East"));
    CHECK(!std::strcmp(row(B, "bind_start").keys, "Enter"));
    CHECK(!std::strcmp(row(B, "bind_l3").keys, "None") && !std::strcmp(row(B, "bind_l2").pad, "Left trigger"));
    // key already Cross's: swapped
    CHECK(pc_ui_settings_bind(PC_PROMPT_CIRCLE, PC_UI_CAPTURE_KEY, SDLK_X) == PC_PROMPT_CROSS);
    CHECK(pc_bind_keys[PC_PROMPT_CIRCLE][0] == SDLK_X && pc_bind_keys[PC_PROMPT_CROSS][0] == SDLK_Z);
    CHECK(pc_ui_settings_dirty());
    // the swap's note: on the status line for 3 s from the next tick
    CHECK(pc_ui_settings_note() && !std::strcmp(pc_ui_settings_note(), "X moved from Cross to Circle; Cross gets Z"));
    pc_ui_settings_tick(500);
    pc_ui_settings_tick(502.5);
    CHECK(pc_ui_settings_note() != nullptr);
    pc_ui_settings_tick(503.5);
    CHECK(pc_ui_settings_note() == nullptr);
    CHECK(stub.saves == saves + 1 && !pc_ui_settings_dirty()); // (the debounce saved meanwhile)
    saves = stub.saves;
    // a free key: the first key replaced, nobody else changes
    CHECK(pc_ui_settings_bind(PC_PROMPT_CIRCLE, PC_UI_CAPTURE_KEY, SDLK_F5) == -1);
    CHECK(pc_bind_keys[PC_PROMPT_CIRCLE][0] == SDLK_F5 && value(B, "bind_circle") == "F5 / East");
    CHECK(pc_ui_settings_bind(PC_PROMPT_CIRCLE, PC_UI_CAPTURE_KEY, SDLK_F5) == -1); // no change
    // an unnamed key cannot be saved: refused
    CHECK(pc_ui_settings_bind(PC_PROMPT_CIRCLE, PC_UI_CAPTURE_KEY, 0x40000999) == -2);
    CHECK(pc_ui_settings_bind(99, PC_UI_CAPTURE_KEY, SDLK_A) == -2);
    // reserved keys: the editor's, the desktop's, the camera keys while they are on
    for (SDL_Keycode k : {SDLK_ESCAPE, SDLK_F1, SDLK_F2, SDLK_LALT, SDLK_RALT, SDLK_J, SDLK_O})
        CHECK(pc_ui_settings_bind(PC_PROMPT_CIRCLE, PC_UI_CAPTURE_KEY, k) == -2 && pc_ui_settings_key_reserved(k));
    CHECK(!pc_ui_settings_key_reserved(SDLK_LSHIFT) && !pc_ui_settings_key_reserved(SDLK_F5));
    pc_camera_stick_keys = 0;
    CHECK(pc_ui_settings_bind(PC_PROMPT_CIRCLE, PC_UI_CAPTURE_KEY, SDLK_J) == -1);
    pc_camera_stick_keys = 1;
    // a swap never leaves a button with the same key twice: Cross has M and Circle's first key J
    pc_binding_setting("bind_key_cross", "m,j");
    CHECK(pc_ui_settings_bind(PC_PROMPT_CIRCLE, PC_UI_CAPTURE_KEY, SDLK_M) == PC_PROMPT_CROSS);
    CHECK(pc_bind_keys[PC_PROMPT_CROSS][0] == SDLK_J && pc_bind_keys[PC_PROMPT_CROSS][1] == 0);
    CHECK(pc_bind_keys[PC_PROMPT_CIRCLE][0] == SDLK_M);
    // gamepad: south is Cross's, swapped
    CHECK(pc_ui_settings_bind(PC_PROMPT_CIRCLE, PC_UI_CAPTURE_PAD, SDL_GAMEPAD_BUTTON_SOUTH) == PC_PROMPT_CROSS);
    CHECK(pc_bind_pad[PC_PROMPT_CIRCLE] == SDL_GAMEPAD_BUTTON_SOUTH && pc_bind_pad[PC_PROMPT_CROSS] == SDL_GAMEPAD_BUTTON_EAST);
    CHECK(pc_ui_settings_bind(PC_PROMPT_L3, PC_UI_CAPTURE_PAD, PC_PAD_AXIS(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
        == PC_PROMPT_R2);
    CHECK(!std::strcmp(row(B, "bind_r2").pad, "Left stick"));
    pc_ui_settings_flush();
    CHECK(stub.saves == saves + 1);
    // reset defaults (an action that saves)
    pc_ui_setting_row reset = row(B, "bind_reset");
    CHECK(reset.kind == PC_UI_SET_ACTION && reset.enabled);
    CHECK(pc_ui_settings_activate(B, find(B, "bind_reset")) == 1);
    CHECK(pc_bind_keys[PC_PROMPT_CIRCLE][0] == SDLK_Z && pc_bind_pad[PC_PROMPT_CROSS] == SDL_GAMEPAD_BUTTON_SOUTH);
    CHECK(pc_ui_settings_dirty());
    pc_ui_settings_flush();
    if (!game) {
        // capture: Circle on a binding row starts one; what arrives binds that button
        CHECK(pc_ui_settings_activate(B, find(B, "bind_square")) == 1 && stub.captures == 1);
        CHECK(pc_ui_settings_capture_button() == PC_PROMPT_SQUARE);
        CHECK(pc_ui_settings_capture_done(PC_UI_CAPTURE_KEY, SDLK_D) == -1);
        CHECK(pc_bind_keys[PC_PROMPT_SQUARE][0] == SDLK_D && pc_ui_settings_capture_button() == -1);
        pc_ui_settings_activate(B, find(B, "bind_square"));
        // a reserved key is refused with a reason; the capture goes on
        CHECK(!pc_ui_settings_capture_note());
        CHECK(pc_ui_settings_capture_refuse(PC_UI_CAPTURE_KEY, SDLK_F1) == 1);
        CHECK(pc_ui_settings_capture_note() && std::strstr(pc_ui_settings_capture_note(), "F1"));
        CHECK(pc_ui_settings_capture_refuse(PC_UI_CAPTURE_KEY, SDLK_D) == 0);
        CHECK(pc_ui_settings_capture_button() == PC_PROMPT_SQUARE);
        CHECK(pc_ui_settings_capture_done(-1, 0) == -1 && pc_ui_settings_capture_button() == -1); // cancelled
        CHECK(!pc_ui_settings_capture_note());
        CHECK(pc_ui_settings_capture_done(PC_UI_CAPTURE_KEY, SDLK_D) == -2); // none running
        pc_bindings_reset();
    }
}

static bool rgb_is(int page, int r, int g, int b)
{
    return value(page, "ui_menu_tint_r") == std::to_string(r) && value(page, "ui_menu_tint_g") == std::to_string(g)
        && value(page, "ui_menu_tint_b") == std::to_string(b);
}
static bool swatch_is(const pc_ui_setting_row& r, int red, int green, int blue)
{
    return r.swatch && r.swatch_rgb[0] == red && r.swatch_rgb[1] == green && r.swatch_rgb[2] == blue;
}

// Menu tint: presets set the sliders; a slider makes the tint Custom with the shown colour.
static void tint()
{
    pc_ui_settings_flush();
    CHECK(value(I, "ui_menu_tint") == "Le\xc3\xa1 Monde" && rgb_is(I, 8, 7, 7));
    pc_ui_setting_row t = row(I, "ui_menu_tint");
    CHECK(t.kind == PC_UI_SET_CHOICE && t.count == PC_UI_TINTS && swatch_is(t, 66, 58, 58));
    pc_ui_setting_row red = row(I, "ui_menu_tint_r");
    CHECK(red.kind == PC_UI_SET_SLIDER && red.min == 0 && red.max == 31 && red.step == 1 && !red.swatch);
    CHECK(change(I, "ui_menu_tint", 1) == 1 && pc_ui_menu_tint == PC_UI_TINT_CLASSIC);
    CHECK(value(I, "ui_menu_tint") == "Classic" && rgb_is(I, 4, 8, 25) && pc_ui_settings_dirty());
    CHECK(swatch_is(row(I, "ui_menu_tint"), 33, 66, 206));
    char buf[16384];
    pc_ui_settings_json(I, buf, sizeof buf);
    CHECK(std::strstr(buf, "\"swatch\":\"#2142ce\""));
    // a slider: Custom, from the preset's colour
    CHECK(change(I, "ui_menu_tint_r", 1) == 1 && pc_ui_menu_tint == PC_UI_TINT_CUSTOM);
    CHECK(pc_ui_menu_tint_custom[0] == 5 && pc_ui_menu_tint_custom[1] == 8 && pc_ui_menu_tint_custom[2] == 25);
    CHECK(value(I, "ui_menu_tint") == "Custom" && rgb_is(I, 5, 8, 25));
    // presets around it; Custom comes back with its colour
    CHECK(change(I, "ui_menu_tint", -1) == 1 && pc_ui_menu_tint == PC_UI_TINT_VERDANT && rgb_is(I, 3, 10, 6));
    CHECK(change(I, "ui_menu_tint", 1) == 1 && pc_ui_menu_tint == PC_UI_TINT_CUSTOM && rgb_is(I, 5, 8, 25));
    // the same value: no change, the preset stays
    pc_ui_settings_set(I, find(I, "ui_menu_tint"), PC_UI_TINT_CLASSIC);
    CHECK(pc_ui_settings_set(I, find(I, "ui_menu_tint_b"), 25) == 0 && pc_ui_menu_tint == PC_UI_TINT_CLASSIC);
    // clamped; Custom
    CHECK(pc_ui_settings_set(I, find(I, "ui_menu_tint_b"), 40) == 1 && pc_ui_menu_tint == PC_UI_TINT_CUSTOM);
    CHECK(rgb_is(I, 4, 8, 31) && pc_ui_menu_tint_custom[2] == 31);
    // at the top: no change
    CHECK(change(I, "ui_menu_tint_b", 1) == 0 && pc_ui_menu_tint_custom[2] == 31);
    CHECK(change(I, "ui_menu_tint_g", -1) == 1 && rgb_is(I, 4, 7, 31));
    // the custom colour's swatch
    CHECK(swatch_is(row(I, "ui_menu_tint"), 33, 58, 255));
    // back to the default
    pc_ui_settings_set(I, find(I, "ui_menu_tint"), PC_UI_TINT_LEA_MONDE);
    pc_ui_menu_tint_custom[0] = 8, pc_ui_menu_tint_custom[1] = 7, pc_ui_menu_tint_custom[2] = 7;
    CHECK(value(I, "ui_menu_tint") == "Le\xc3\xa1 Monde");
    pc_ui_settings_flush();
    // the module's names
    CHECK(pc_ui_tint_from_name("crimson") == PC_UI_TINT_CRIMSON && pc_ui_tint_from_name("5") == PC_UI_TINT_CUSTOM);
    CHECK(pc_ui_tint_from_name("blue") == -1 && pc_ui_tint_from_name("6") == -1 && pc_ui_tint_from_name("") == -1);
    CHECK(!std::strcmp(pc_ui_tint_id(PC_UI_TINT_LEA_MONDE), "lea_monde") && pc_ui_tint_id(PC_UI_TINTS) == nullptr);
    pc_ui_menu_tint = 9; // out of range: Leá Monde
    CHECK(pc_ui_tint_current() == PC_UI_TINT_LEA_MONDE);
    pc_ui_menu_tint = 0;
}

static void json()
{
    char buf[16384];
    int n = pc_ui_settings_json(D, buf, sizeof buf);
    CHECK(n > 0 && n < (int)sizeof buf);
    CHECK(std::strstr(buf, "{\"page\":\"display\",\"title\":\"Display\""));
    CHECK(std::strstr(buf, "\"id\":\"widescreen\",\"label\":\"Widescreen 16:9\",\"kind\":\"toggle\""));
    CHECK(std::strstr(buf, "\"kind\":\"info\""));
    pc_ui_settings_json(P, buf, sizeof buf);
    CHECK((std::strstr(buf, "\"game\":true") != nullptr) == game);
    pc_ui_settings_json(B, buf, sizeof buf);
    CHECK(std::strstr(buf, "\"id\":\"bind_circle\"") && std::strstr(buf, "\"button\":5"));
    char small[16];
    n = pc_ui_settings_json(G, small, sizeof small);
    CHECK(n == 15 && std::strlen(small) == 15);
}

int main()
{
    pages();
    values();
    lighting_and_aa();
    greyed();
    saving();
    game_options();
    bindings();
    tint();
    json();
    if (failures) {
        std::printf("ui_settings_probe: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("ui_settings_probe%s: ok\n", game ? "" : " (hooks)");
    return 0;
}
