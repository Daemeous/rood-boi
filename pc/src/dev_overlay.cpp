// Developer overlay: Dear ImGui over the OpenGL presentation (F1 toggles).
//
// Input comes from SDL through Dear ImGui's upstream SDL3 platform backend
// (imgui_impl_sdl3) and drawing goes through the upstream OpenGL3 backend.
// Panels: graphics settings (internal resolution, presentation filter,
// aspect), live game state, and run control (pause, frame step). Settings
// persist in pc/build/boot/imgui.ini.
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "emissive.h"
#include "imgui.h"
#include "imgui_internal.h" /* ClearActiveID (RPC editor_input) */
#include "dev_state.h"
#include "render.h"
#include "room_editor.h"
#include "gameplay_walls.h"
#include "editor_overlays.h"
#include "lighting_quality.h"
#include "editor_undo.h"
#include "editor_details.h"
#include "editor_handles.h"
#include "editor_fit_window.h"
#include "editor_library.h"
#include "editor_colour.h"
#include "editor_scene.h"
#include "environment_internal.h"
#include "particles.h"
#include "particles_internal.h"
#include "editor_selection.h"
#include "camera_stick.h"
#include "fast_travel.h"
#include "ui/ui_shell.h"
#include "ui/map_frame.h"
#include "ui/data_frame.h"
#include "ui/ui_workshop.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "ImGuizmo.h"
#ifdef ROOD_VULKAN
#include "vk_hooks.h" /* ROOD_RENDERER=vulkan: drawn by vk_overlay.cpp */
#endif

extern "C" {
extern int pc_hw_active, pc_hw_scale;
int pc_hw_set_scale(int scale);
extern int pc_hw_scale_setting;
extern int pc_gl_filter, pc_gl_aspect, pc_gte_widescreen, pc_hw_perspective, pc_gl_show_depth;
extern int pc_hw_subpixel, pc_hw_interpolate, pc_hw_texture_filter, pc_interp_skeleton,
    pc_interp_holds, pc_interp_uv, pc_present_rate;
unsigned pc_vsync_count(void);
unsigned pc_battle_frame_count(void);
void pc_rpc_set_paused(int paused);
void pc_rpc_request_step(long vsyncs);
int pc_rpc_is_paused(void);
}

extern "C" void pc_settings_load(void);
extern "C" void pc_fast_travel_prompt_draw(void); /* fast_travel_prompt.cpp */
extern "C" int pc_editor_active(void);
extern "C" void pc_editor_toggle(void);
extern "C" int pc_editor_flying(void);
extern "C" void pc_editor_input(void);
extern "C" void pc_editor_panel(void);
extern "C" void pc_editor_grid(void);
static bool active, visible;
static bool show_help; /* Help > Keyboard shortcuts */
static int outliner_tab_request = -1; /* Window menu / toolbar: 0 Outliner, 1 the Library */
static bool place_tab_shown;
/* The Details tabs: the selection's Details first (it follows the
 * selection), then the room-wide settings on their own tabs (RPC editor_ui
 * {details}: by name). */
enum DetailsTab {
    TAB_DETAILS,
    TAB_LIGHTING,
    TAB_INSPECTOR,
    TAB_ATMOSPHERE,
    TAB_POST,
    TAB_GRAPHICS,
    TAB_EMISSIVE,
    TAB_DIAGNOSTICS,
    TAB_COUNT
};
static const char* const details_tabs[TAB_COUNT] = { "details", "lighting", "inspector", "atmosphere", "post", "graphics",
    "emissive", "diagnostics" };
static int details_tab_request = -1;
static int details_tab_shown = TAB_DETAILS;
extern "C" int pc_lights_place_cancel(void);
extern "C" void pc_lights_place_menu(void);
extern "C" void pc_lights_editor_windows(void);
extern "C" bool* pc_lights_fixture_mode(void);
extern "C" int pc_lights_light_fixture(float x, float y);
extern "C" void pc_lights_light_all_fixtures(void);
extern "C" int pc_lights_delete_selection(void);
extern "C" int* pc_lights_gizmo_operation(void);
extern "C" int pc_lights_has_selection(void);
extern "C" void pc_window_release_keys(void) __attribute__((weak));
static bool focus_search_request; /* RPC editor_input {focus "search"}: the Outliner's search field */
static bool clear_focus_request; /* RPC editor_input {focus "none"} */
static double last_time;
static float ui_scale = 1.0f, applied_scale = 0.0f;
static ImGuiStyle base_style;
static SDL_Window* overlay_window;
/* Relative mouse mode while the editor's free camera flies: the pointer is
 * hidden and held, and its motion collects here (window pixels). */
static bool relative;
static float look_dx, look_dy, held_x, held_y;

/* Overlay scale for high-DPI screens: ROOD_UI_SCALE, else the desktop's
 * content scale as SDL reports it (Xft.dpi or GDK_SCALE on X11, the output
 * scale on Wayland); 1.0 is 96 DPI. Fonts rasterize at the scaled size, so
 * text stays sharp. */
static float detect_scale(SDL_Window* window)
{
    const char* env = getenv("ROOD_UI_SCALE");
    if (env && atof(env) > 0)
        return (float)atof(env);
    float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
    if (scale <= 0)
        scale = 1.0f;
    return scale < 1.0f ? 1.0f : scale > 4.0f ? 4.0f : scale;
}
static void apply_scale()
{
    if (ui_scale == applied_scale)
        return;
    ImGuiStyle& style = ImGui::GetStyle();
    style = base_style;
    style.ScaleAllSizes(ui_scale);
    style.FontScaleMain = ui_scale;
    applied_scale = ui_scale;
}

extern "C" void pc_overlay_init(SDL_Window* window, SDL_GLContext context)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "pc/build/boot/imgui.ini";
    /* Numbers are typed as in Unreal: a click (without dragging) on a
     * drag field edits it as text, a drag scrubs; sliders take a
     * double-click (or Ctrl+click). Enter commits, Escape cancels, Tab
     * moves to the next field. */
    io.ConfigDragClickToInputText = true;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().Alpha = 0.95f;
    base_style = ImGui::GetStyle();
    ui_scale = detect_scale(window);
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        if (!ImGui_ImplSDL3_InitForVulkan(window) || !pc_vk_overlay_init()) {
            fprintf(stderr, "[overlay] SDL3/Vulkan backends unavailable\n");
            ImGui::DestroyContext();
            return;
        }
    } else
#endif
    if (!ImGui_ImplSDL3_InitForOpenGL(window, context) || !ImGui_ImplOpenGL3_Init("#version 330 core")) {
        fprintf(stderr, "[overlay] SDL3/OpenGL3 backends unavailable\n");
        ImGui::DestroyContext();
        return;
    }
    /* Gamepads play the game; the overlay never opens them. */
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
    overlay_window = window;
    active = true;
    pc_settings_load();
    fprintf(stderr, "[overlay] Dear ImGui %s ready (scale %.2f); F1 toggles the developer overlay\n",
        IMGUI_VERSION, ui_scale);
}

extern "C" int pc_overlay_visible(void) { return active && visible; }
/* Dear ImGui is up and draws each frame (the modern menus draw through it:
 * pc/src/ui/menu_bridge.c keeps the original menus when it is not). */
extern "C" int pc_overlay_active(void) { return active; }

/* The free camera's mouse motion since the last call, while it holds the
 * mouse in relative mode; otherwise *dx, *dy are left alone and 0 returned. */
extern "C" int pc_overlay_look_delta(float* dx, float* dy)
{
    if (!relative)
        return 0;
    *dx = look_dx, *dy = look_dy;
    look_dx = look_dy = 0;
    return 1;
}

static void set_visible(bool show)
{
    visible = show;
    if (visible) {
        if (pc_window_release_keys) /* the editor takes the keys' releases now */
            pc_window_release_keys();
        return;
    }
    /* A drag the editor's frames would have ended on release ends now: its
     * value stays and its undo step closes (editor_details.cpp, editor_handles.cpp). */
    rood_details::ladder_finish();
    rood_handles::finish();
    /* Tools armed for the next click in the view disarm (the first click after reopening only selects). */
    rood_fit::cancel();
    pc_lights_place_cancel(); /* a Library drag (or a placing ghost) */
    /* Keys and buttons held when the editor closes never see their release. */
    ImGuiIO& io = ImGui::GetIO();
    io.ClearInputKeys();
    io.ClearInputMouse();
}

/* RPC editor_ui {help}: the Keyboard shortcuts window (1 open, 0 closed, -1 reads). */
extern "C" int pc_overlay_set_help(int show)
{
    if (show >= 0)
        show_help = show != 0;
    return show_help;
}
/* RPC editor_ui {tab}: 0 the Outliner, 1 the Library (-1 reads; returns 1
 * when the Library's tab shows). */
extern "C" int pc_overlay_set_tab(int tab)
{
    if (tab >= 0)
        outliner_tab_request = tab;
    return tab >= 0 ? tab == 1 : place_tab_shown;
}
/* RPC editor_ui {details}: select a Details tab by name; returns 0 for none. */
extern "C" int pc_overlay_set_details_tab(const char* name)
{
    for (int i = 0; i < TAB_COUNT; i++)
        if (!strcmp(name, details_tabs[i]))
            return details_tab_request = i, 1;
    return 0;
}
/* RPC editor_ui: the Details tab shown in the last frame. */
extern "C" const char* pc_overlay_details_tab(void) { return details_tabs[details_tab_shown]; }
/* RPC editor_ui: open (1) or close (0) the editor; -1 only reads. */
extern "C" int pc_overlay_set_visible(int show)
{
    if (!active)
        return 0;
    if (show >= 0 && (show != 0) != visible)
        set_visible(show != 0);
    return visible;
}

/* RPC editor_input: keys (a JSON list of names: "delete", "backspace",
 * "escape", "enter", "tab", "f2", a letter or digit) pressed and released
 * through SDL's event queue, so they take the same path as the keyboard
 * (pc_overlay_event, Dear ImGui, the shortcuts); text typed (text input
 * events); focus "search" (the Outliner's search field) or "none"; hold:
 * modifiers ("ctrl", "shift", "alt") pressed and kept down across requests
 * until one with release. Replies with what the editor saw last frame. */
extern "C" int pc_overlay_input(const char* keys, const char* text, int ctrl, int shift, const char* focus, const char* hold,
    int release, char* out, int size)
{
    if (!active || !overlay_window) {
        snprintf(out, size, "the overlay is not running");
        return -1;
    }
    if (focus && !strcmp(focus, "search"))
        focus_search_request = true, outliner_tab_request = 0;
    else if (focus && !strcmp(focus, "none"))
        clear_focus_request = true;
    /* hold: modifiers kept down across requests (a handle dragged with Ctrl,
     * Shift or Alt) until a request with release. */
    static SDL_Keymod held;
    static const struct {
        const char* name;
        SDL_Keycode key;
        SDL_Keymod mod;
    } modifiers[] = { { "ctrl", SDLK_LCTRL, SDL_KMOD_LCTRL }, { "shift", SDLK_LSHIFT, SDL_KMOD_LSHIFT },
        { "alt", SDLK_LALT, SDL_KMOD_LALT } };
    auto push_modifier = [](SDL_Keycode key, bool down, SDL_Keymod mods) {
        SDL_Event e;
        memset(&e, 0, sizeof e);
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = SDL_GetWindowID(overlay_window);
        e.key.key = key;
        e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
        e.key.mod = mods;
        e.key.down = down;
        SDL_PushEvent(&e);
    };
    if (release)
        for (const auto& m : modifiers)
            if (held & m.mod) {
                held = (SDL_Keymod)(held & ~m.mod);
                push_modifier(m.key, false, held);
            }
    for (const char* p = hold ? strchr(hold, '"') : nullptr; p; p = strchr(p, '"')) {
        const char* end = strchr(p + 1, '"');
        if (!end)
            break;
        char name[16];
        snprintf(name, sizeof name, "%.*s", (int)(end - p - 1), p + 1);
        p = end + 1;
        bool known = false;
        for (const auto& m : modifiers)
            if (!strcmp(name, m.name)) {
                known = true;
                held = (SDL_Keymod)(held | m.mod);
                push_modifier(m.key, true, held);
            }
        if (!known) {
            snprintf(out, size, "unknown modifier %s (ctrl, shift, alt)", name);
            return -1;
        }
    }
    SDL_Keymod mod = (SDL_Keymod)((ctrl ? SDL_KMOD_LCTRL : 0) | (shift ? SDL_KMOD_LSHIFT : 0) | held);
    for (const char* p = keys ? strchr(keys, '"') : nullptr; p; p = strchr(p, '"')) {
        const char* end = strchr(p + 1, '"');
        if (!end)
            break;
        char name[32];
        snprintf(name, sizeof name, "%.*s", (int)(end - p - 1), p + 1);
        p = end + 1;
        SDL_Keycode key = !strcmp(name, "delete") ? SDLK_DELETE
            : !strcmp(name, "backspace")          ? SDLK_BACKSPACE
            : !strcmp(name, "escape")             ? SDLK_ESCAPE
            : !strcmp(name, "enter")              ? SDLK_RETURN
            : !strcmp(name, "tab")                ? SDLK_TAB
            : !strcmp(name, "f2")                 ? SDLK_F2
            : strlen(name) == 1 && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9'))
            ? (SDL_Keycode)name[0]
            : SDLK_UNKNOWN;
        if (key == SDLK_UNKNOWN) {
            snprintf(out, size, "unknown key %s", name);
            return -1;
        }
        for (int down = 1; down >= 0; down--) {
            SDL_Event e;
            memset(&e, 0, sizeof e);
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.windowID = SDL_GetWindowID(overlay_window);
            e.key.key = key;
            e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
            e.key.mod = mod;
            e.key.down = down != 0;
            SDL_PushEvent(&e);
        }
    }
    if (text && *text) {
        static char held[128]; /* the event points at it until it is read */
        snprintf(held, sizeof held, "%s", text);
        SDL_Event e;
        memset(&e, 0, sizeof e);
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.windowID = SDL_GetWindowID(overlay_window);
        e.text.text = held;
        SDL_PushEvent(&e);
    }
    ImGuiIO& io = ImGui::GetIO();
    char held_names[48] = "";
    for (const auto& m : modifiers)
        if (held & m.mod)
            snprintf(held_names + strlen(held_names), sizeof held_names - strlen(held_names), "%s\"%s\"",
                held_names[0] ? "," : "", m.name);
    snprintf(out, size, "{\"visible\":%s,\"want_text_input\":%s,\"item_active\":%s,\"gizmo\":%d,\"held\":[%s]}",
        visible ? "true" : "false", io.WantTextInput ? "true" : "false", ImGui::IsAnyItemActive() ? "true" : "false",
        *pc_lights_gizmo_operation(), held_names);
    return 0;
}

/* The modern menus' name field (Items > Workshop: a weapon's name) takes the
 * keyboard while it shows and the editor is closed (ui/ui_workshop.h). */
static bool shell_typing() { return !visible && pc_ui_workshop_typing(); }
static bool shell_text_on; /* text input started for it */
/* Typing is armed once no key is held after the field opened: the press that
 * opened it (a key bound to Circle, Enter) neither types nor, held, repeats
 * into the field (an auto-repeated Enter would be Done at once). */
static bool shell_text_armed;
static void shell_text_input()
{
    bool want = shell_typing();
    if (want != shell_text_on && overlay_window) {
        if (want)
            SDL_StartTextInput(overlay_window);
        else if (!visible) /* the editor manages it while it shows */
            SDL_StopTextInput(overlay_window);
        shell_text_on = want;
        shell_text_armed = false;
    }
    if (shell_text_on && !shell_text_armed) {
        int n = 0;
        const bool* keys = SDL_GetKeyboardState(&n);
        bool held = false;
        for (int i = 0; keys && i < n && !held; ++i)
            held = keys[i];
        shell_text_armed = !held;
    }
}

// Returns 1 when the event belongs to the overlay (game input should ignore it).
extern "C" int pc_overlay_event(const SDL_Event* event)
{
    if (!active)
        return 0;
    ImGuiIO& io = ImGui::GetIO();
    /* Mouse positions in window pixels, as the presenter and editor use. */
    SDL_Event scaled = *event;
    float density = SDL_GetWindowPixelDensity(overlay_window);
    if (density > 0 && density != 1) {
        if (scaled.type == SDL_EVENT_MOUSE_MOTION)
            scaled.motion.x *= density, scaled.motion.y *= density;
        if (scaled.type == SDL_EVENT_MOUSE_BUTTON_DOWN || scaled.type == SDL_EVENT_MOUSE_BUTTON_UP)
            scaled.button.x *= density, scaled.button.y *= density;
    }
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        bool down = event->type == SDL_EVENT_KEY_DOWN;
        SDL_Keycode key = event->key.key;
        /* The name field: its editing keys, and no key press reaches the
         * game (the characters come as text input); a release still does,
         * so a button the field's opening press held is let go. F1 still
         * opens the editor. */
        if (shell_typing() && key != SDLK_F1) {
            if (down && shell_text_armed) {
                int k = key == SDLK_BACKSPACE ? PC_UI_WSK_BACKSPACE
                    : key == SDLK_DELETE      ? PC_UI_WSK_DELETE
                    : key == SDLK_LEFT        ? PC_UI_WSK_LEFT
                    : key == SDLK_RIGHT       ? PC_UI_WSK_RIGHT
                    : key == SDLK_HOME        ? PC_UI_WSK_HOME
                    : key == SDLK_END         ? PC_UI_WSK_END
                    : key == SDLK_RETURN || key == SDLK_KP_ENTER ? PC_UI_WSK_ENTER
                    : key == SDLK_ESCAPE      ? PC_UI_WSK_ESCAPE
                                              : -1;
                /* Done and back act on a press, never on its auto-repeat */
                if (event->key.repeat && (k == PC_UI_WSK_ENTER || k == PC_UI_WSK_ESCAPE))
                    k = -1;
                if (k >= 0)
                    pc_ui_workshop_key(k);
            }
            return down;
        }
        /* A field being typed in (a number or a name) takes every key: Escape
         * cancels the edit there instead of closing the editor, and no
         * editor shortcut fires (viewport_keys and the free camera check the
         * same). */
        bool typing = visible && io.WantTextInput;
        /* Escape during a handle drag puts its value back, or stops aiming (editor_handles.cpp). */
        if (key == SDLK_ESCAPE && visible && down && !typing && rood_handles::cancel())
            return 1;
        /* Escape closes the colour popover (editor_colour.cpp). */
        if (key == SDLK_ESCAPE && visible && down && !typing && rood_colour::cancel())
            return 1;
        /* Escape during a value ladder drag cancels it (editor_details.cpp). */
        if (key == SDLK_ESCAPE && visible && down && !typing && rood_details::ladder_cancel())
            return 1;
        /* Escape disarms "fit rect light to window" (editor_fit_window.cpp). */
        if (key == SDLK_ESCAPE && visible && down && !typing && rood_fit::cancel())
            return 1;
        /* Escape during a drag from the Library cancels the drag only. */
        if (key == SDLK_ESCAPE && visible && down && !typing && pc_lights_place_cancel())
            return 1;
        /* Escape or F1: open or close the editor.
         * Closing hands the camera back. */
        if (key == SDLK_F1 || (key == SDLK_ESCAPE && !typing)) {
            if (down && !event->key.repeat) {
                set_visible(!visible);
                if (!visible && pc_editor_active())
                    pc_editor_toggle();
            }
            /* A release still reaches the fields (Escape that cancelled one
             * was pressed while typing; its release comes after). */
            if (!down && visible)
                ImGui_ImplSDL3_ProcessEvent(event);
            return 1;
        }
        if (key == SDLK_F2 && !typing) { /* free camera; opens the editor */
            if (down && !event->key.repeat) {
                pc_editor_toggle();
                if (pc_editor_active())
                    set_visible(true);
            }
            return 1;
        }
        if (!visible)
            return 0;
        ImGui_ImplSDL3_ProcessEvent(event);
        return 1; /* while the editor is open, the keyboard is the editor's (a gamepad still plays) */
    }
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
        if (!visible && shell_typing()) {
            if (event->type == SDL_EVENT_TEXT_INPUT && shell_text_armed)
                pc_ui_workshop_text(event->text.text);
            return 1;
        }
        if (!visible)
            return 0;
        ImGui_ImplSDL3_ProcessEvent(event);
        return 1;
    case SDL_EVENT_MOUSE_MOTION:
        if (relative) {
            look_dx += event->motion.xrel * (density > 0 ? density : 1);
            look_dy += event->motion.yrel * (density > 0 ? density : 1);
            return 1;
        }
        ImGui_ImplSDL3_ProcessEvent(&scaled); /* keep the pointer current for when the editor opens */
        return visible;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        if (!visible) {
            /* The modern menus (pc/src/ui) take clicks and the wheel while
             * they show (the shell, the Map frame, the Loot screen: as
             * ui_draw.cpp pc_ui_shell_wants_mouse); ui_draw hit-tests them
             * against ImGui's pointer. A release always reaches ImGui: the
             * click that closed the shell must not leave its button held
             * there. */
            if (!pc_ui_shell_drawn() && !pc_ui_map_visible() && !pc_ui_loot_visible() && !pc_ui_data_visible()) {
                if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
                    ImGui_ImplSDL3_ProcessEvent(&scaled);
                return 0;
            }
            ImGui_ImplSDL3_ProcessEvent(&scaled);
            return 1;
        }
        ImGui_ImplSDL3_ProcessEvent(&scaled);
        return 1;
    default:
        /* Window focus, pointer enter/leave and display changes; gamepads
         * are left to the game. */
        if (event->type < SDL_EVENT_JOYSTICK_AXIS_MOTION || event->type >= SDL_EVENT_FINGER_DOWN)
            ImGui_ImplSDL3_ProcessEvent(event);
        return 0;
    }
}

/* Hold the mouse (hidden, relative motion) while the free camera flies, and
 * put the pointer back where the drag began. */
static void update_relative_mouse()
{
    bool want = pc_editor_flying() != 0;
    if (want == relative)
        return;
    if (want)
        SDL_GetMouseState(&held_x, &held_y);
    if (!SDL_SetWindowRelativeMouseMode(overlay_window, want) && want)
        return; /* unsupported: fall back to absolute motion */
    if (!want)
        SDL_WarpMouseInWindow(overlay_window, held_x, held_y);
    relative = want;
    look_dx = look_dy = 0;
}

extern "C" void pc_ui_upscale_panel(void);
static void graphics_panel()
{
    ImGui::SliderFloat("Glowing surfaces", &pc_emissive, 0.0f, 2.0f, pc_emissive <= 0 ? "Off" : "%.2f");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Emissive texels (window light, magic circles, lava): never darkened, they feed bloom\n"
                          "and, as lights, the ray-traced light. 0: the original picture. Details > Emissive edits them.");
    ImGui::Combo("Dust", &pc_dust, "Off\0Low\0High\0");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Dust motes (particles: pc/docs/Particles.md) in rooms that have them: Off draws none (the\n"
                          "picture as before), Low dust_low of them, High all. Details > Atmosphere places and tunes them.");
    rood_details::kind_section("particles", "Particle settings"); /* particles_editor.cpp */
    ImGui::SliderFloat("Overlay scale", &ui_scale, 0.75f, 4.0f, "%.2fx");
    if (ImGui::IsItemDeactivatedAfterEdit())
        apply_scale();
    if (pc_hw_active) {
        int scale = pc_hw_scale_setting;
        if (ImGui::SliderInt("Internal resolution", &scale, 1, 8, "%dx"))
            pc_hw_set_scale(scale);
        ImGui::Text("Render target %d x %d%s", 1024 * pc_hw_scale, 512 * pc_hw_scale,
            pc_hw_scale != pc_hw_scale_setting ? " (supersampling)" : "");
    } else {
#ifdef ROOD_VULKAN
        if (pc_vk_on())
            ImGui::TextDisabled("Hardware renderer off (ROOD_SCALE=0)");
        else
#endif
            ImGui::TextDisabled("Hardware rasterizer off (ROOD_SCALE=0 or no OpenGL 3.3)");
    }
    static const char* filters[] = { "Nearest", "Bilinear", "Box (texels under each pixel)" };
    ImGui::Combo("Filter", &pc_gl_filter, filters, 3); /* anti-aliasing: Lighting quality > Display */
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("How the target's colours reach window pixels (lighting stays one texel per pixel);\n"
                          "true supersampling: Lighting quality > Display > Supersampling");
    static const char* aspects[] = { "Native aspect", "Stretch to window", "Integer scale" };
    ImGui::Combo("Aspect", &pc_gl_aspect, aspects, 3);
    bool widescreen = pc_gte_widescreen != 0;
    if (ImGui::Checkbox("Widescreen 16:9 (Hor+)", &widescreen))
        pc_gte_widescreen = widescreen;
    if (pc_gte_widescreen)
        ImGui::TextDisabled("3D widens via the GTE; 2D stays 4:3 shaped (hardware path)");
    if (pc_hw_active) {
        bool subpixel = pc_hw_subpixel != 0;
        if (ImGui::Checkbox("Sub-pixel precision (no vertex jitter)", &subpixel))
            pc_hw_subpixel = subpixel;
        bool interpolate = pc_hw_interpolate != 0;
        if (ImGui::Checkbox("Frame interpolation (60 fps motion)", &interpolate))
            pc_hw_interpolate = interpolate;
        if (pc_hw_interpolate) {
            bool skeleton = pc_interp_skeleton != 0;
            ImGui::Indent();
            if (ImGui::Checkbox("Rotation-aware character in-betweens", &skeleton))
                pc_interp_skeleton = skeleton;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Bones turn on arcs through the skeleton (weapons stay in the hand);\n"
                                  "off: linear matrix blending (fast swings shrink limbs)");
            bool holds = pc_interp_holds != 0;
            if (ImGui::Checkbox("Smooth held poses", &holds))
                pc_interp_holds = holds;
            ImGui::SetNextItemWidth(120);
            ImGui::Combo("Texture scrolls", &pc_interp_uv, "Step\0Between frames\0Smooth\0");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Room texture scrolls (the save point's sigil):\n"
                                  "Step: with the game's frames; Between frames: drawn partway between\n"
                                  "them, every game frame exact; Smooth: a constant speed through the\n"
                                  "frames an effect holds between its steps");
            ImGui::SetNextItemWidth(120);
            ImGui::SliderInt("Images per vsync", &pc_present_rate, 1, 8);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Present in-betweens at fractions of a vsync (2: 120 fps, 4: 240 fps);\n"
                                  "the game keeps its own rate");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Poses the game holds for 1-3 frames between changes (animations\n"
                                  "stepping below the frame rate) blend across the hold (one frame of lag)");
            ImGui::Unindent();
        }
        bool filtering = pc_hw_texture_filter != 0;
        if (ImGui::Checkbox("Smooth textures (bilinear, 3D only)", &filtering))
            pc_hw_texture_filter = filtering;
        bool perspective = pc_hw_perspective != 0;
        if (ImGui::Checkbox("Perspective-correct textures", &perspective))
            pc_hw_perspective = perspective;
        {
            /* Cutaway walls in gameplay (gameplay_walls.c). */
            static const char* walls[] = { "Original", "Smart" };
            int mode = pc_gameplay_walls ? 1 : 0;
            ImGui::SetNextItemWidth(120);
            if (ImGui::Combo("Cutaway walls", &mode, walls, 2))
                pc_gameplay_walls = mode;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Original: the game hides every wall facing the camera's side, as on the PS1\n"
                                  "(in rooms that are not a box this leaves black void and invisible walls).\n"
                                  "Smart: such walls are drawn unless they stand between the camera and Ashley\n"
                                  "(or the battle target) or cover the room in front of her; they fade in and out.\n"
                                  "Cutscenes keep the original. Settings key gameplay_walls.");
            if (pc_gameplay_walls) {
                ImGui::SameLine();
                bool ghost = pc_gameplay_walls_ghost != 0;
                if (ImGui::Checkbox("Ghost walls in the way", &ghost))
                    pc_gameplay_walls_ghost = ghost;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Walls in the way stay as a dithered ghost with their edges (opacity below)\n"
                                      "instead of being left out.");
                if (pc_gameplay_walls_ghost) {
                    ImGui::SetNextItemWidth(120);
                    ImGui::SliderFloat("Ghost opacity##walls", &pc_gameplay_walls_opacity, 0, 0.9f, "%.2f");
                }
                ImGui::SetNextItemWidth(120);
                ImGui::SliderFloat("Clearance around Ashley", &pc_gameplay_walls_radius, 0, 256, "%.0f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("World units around Ashley's body (and the battle target) that a wall must\n"
                                      "keep clear of to be drawn.");
            }
        }
        bool native = pc_native_room != 0;
        if (ImGui::Checkbox("Native room geometry", &native))
            pc_native_room = native ? 2 : 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Draw the room from meshes with exact depth instead of the PS1 packets.");
        if (pc_native_room) {
            ImGui::SameLine();
            bool only = pc_native_room == 2;
            if (ImGui::Checkbox("No reference packets", &only))
                pc_native_room = only ? 2 : 1;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Skip the room's opaque packets entirely (less CPU); native VRAM is\n"
                                  "copied back from the hardware target where the game reads it.");
        }
        bool actors = pc_native_actors != 0;
        if (ImGui::Checkbox("Native characters", &actors))
            pc_native_actors = actors ? 2 : 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Draw characters, weapons and props from meshes with bone palettes (exact depth,\n"
                              "interpolated poses, real shadow casters) instead of their PS1 packets.\n"
                              "Needs the native room.");
        if (pc_native_actors) {
            ImGui::SameLine();
            bool only = pc_native_actors == 2;
            if (ImGui::Checkbox("No reference##actors", &only))
                pc_native_actors = only ? 2 : 1;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The CPU no longer rasterizes the packets of characters, drop shadows and\n"
                                  "particles that native draws cover; native VRAM is copied back from the\n"
                                  "hardware target where the game reads it (StoreImage, textures, copies).");
            ImGui::SameLine();
            bool order = pc_native_actor_order != 0;
            if (ImGui::Checkbox("PS1 order within a model", &order))
                pc_native_actor_order = order;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("A character's own polygons paint over each other in the PS1's order (as the\n"
                                  "game looks: hair over scalp, coat over body); off: depth-tested per pixel.\n"
                                  "Against the room and other models depth is exact either way.");
        }
        bool blend = pc_native_blend != 0;
        if (ImGui::Checkbox("Native translucent polygons", &blend))
            pc_native_blend = blend;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Semi-transparent room polygons (window light, glass), characters' translucent\n"
                              "parts and blended (faded, tinted) models drawn from meshes in PS1 ordering-table\n"
                              "order with the PS1 blend modes, instead of their packets.");
        ImGui::SameLine();
        bool objects = pc_native_objects != 0;
        if (ImGui::Checkbox("Native room objects", &objects))
            pc_native_objects = objects;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Other room blocks drawn through the room code (animated floor quads) as\n"
                              "native meshes layered over the room.");
        bool shadows = pc_native_shadows != 0;
        if (ImGui::Checkbox("Native drop shadows", &shadows))
            pc_native_shadows = shadows;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drop shadows under characters drawn as world-space triangles (interpolated with\n"
                              "the actor, hidden behind the room) instead of their PS1 packets.");
        ImGui::SameLine();
        bool effects = pc_native_effects != 0;
        if (ImGui::Checkbox("Native particles", &effects))
            pc_native_effects = effects;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Effect particles drawn as sprites at their world positions (they move with the\n"
                              "camera between game frames; additive ones light the scene) instead of packets.");
        bool reconstruct = pc_reconstruct != 0;
        if (ImGui::Checkbox("Reconstruct 3D from packets", &reconstruct))
            pc_reconstruct = reconstruct;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Keep the GTE projection log, room depth records and packet shadow casters\n"
                              "(the pre-native renderer). Off: retired while every native switch is on;\n"
                              "packets that remain (2D, spell effects) draw as 2D in their place.");
        bool depth = pc_gl_show_depth != 0;
        if (ImGui::Checkbox("Show depth buffer", &depth))
            pc_gl_show_depth = depth;
    }
    pc_ui_upscale_panel(); /* 2D / UI (ui_upscale_panel.cpp) */
}

extern "C" {
int pc_lights_room(int* z, int* r);
void pc_lights_mark_dirty(void);
void pc_lights_save(void);
void pc_lights_reload(void);
const char* pc_lights_status(void);
int* pc_lights_gizmo_operation(void);
bool* pc_lights_show(void);
bool* pc_atmosphere_show_volumes(void); /* atmosphere.cpp: Show > Fog volumes */
void pc_lights_outliner(const char* filter);
void pc_lights_global_settings(void);
extern int pc_lights_snap;
extern int pc_lights_gizmo_space;
extern float pc_lights_snap_move, pc_lights_snap_angle;
bool pc_room_settings_page(const char* page);
bool pc_environment_page(void); /* environment.cpp: the room's sun and sky */
void pc_lights_glows_outliner(const char* filter);
bool pc_lights_glow_details(void);
bool* pc_lights_probe_mode(void);
void pc_lights_inspector(void);
extern int pc_gl_inset[4], pc_gl_view[4], pc_gl_view_mode;
void pc_settings_load(void);
void pc_settings_save(void);
void pc_settings_poll(void);
const char* pc_settings_status(void);
void pc_lights_poll(void);
void pc_lighting_file_menu(void);
}
#include "room_warp.h"
extern bool pc_room_browser_open;
extern "C" void pc_lights_viewport(void);
extern "C" {
extern int pc_glow_shadows;
extern int pc_gl_compute, pc_rt_shadows, pc_baked_shadows, pc_shadow_samples, pc_gi_enabled, pc_gi_debug, pc_gi_bounces, pc_gi_rays, pc_gi_denoise;
extern int pc_fog_steps;
extern float pc_fog_density, pc_fog_anisotropy, pc_fog_glow;
extern float pc_shadow_history, pc_light_size, pc_gi_strength, pc_gi_resolution, pc_gi_baked_bounce, pc_emitter_light, pc_emitter_radius, pc_contact_radius,
    pc_baked_ambient;
/* Vulkan path tracer only (vk_rt.inc). */
extern float pc_shadow_motion_history __attribute__((weak)), pc_haze_history __attribute__((weak));
extern int pc_vk_active __attribute__((weak));
int pc_gi_denoiser_active(void) __attribute__((weak));
int vk_nrd_built(void) __attribute__((weak));
extern int pc_nrd_glow __attribute__((weak)), pc_nrd_max_frames __attribute__((weak));
}
/* Graphics > Controls: the right-stick camera (camera_stick.c). */
static void camera_stick_panel()
{
    bool on = pc_camera_stick != 0;
    if (ImGui::Checkbox("Right stick turns and zooms the camera", &on))
        pc_camera_stick = on;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stick left/right turns around Ashley, up/down zooms; L1/R1 still turn 45 degrees.\n"
                          "Keyboard: J/L turn, I/K zoom, O resets");
    if (!pc_camera_stick)
        return;
    ImGui::Indent();
    ImGui::SliderFloat("Turn speed", &pc_camera_stick_speed, 30, 360, "%.0f deg/s");
    ImGui::SliderFloat("Dead zone", &pc_camera_stick_deadzone, 0, 0.5f, "%.2f");
    ImGui::SliderFloat("Response curve", &pc_camera_stick_curve, 1, 3, "%.1f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("1: linear; higher: finer control near the centre");
    bool invert_x = pc_camera_stick_invert_x != 0, invert_y = pc_camera_stick_invert_y != 0;
    if (ImGui::Checkbox("Invert turn", &invert_x))
        pc_camera_stick_invert_x = invert_x;
    ImGui::SameLine();
    if (ImGui::Checkbox("Invert zoom", &invert_y))
        pc_camera_stick_invert_y = invert_y;
    bool zoom = pc_camera_stick_zoom != 0;
    if (ImGui::Checkbox("Stick up/down zooms", &zoom))
        pc_camera_stick_zoom = zoom;
    if (pc_camera_stick_zoom) {
        ImGui::SliderFloat("Zoom speed", &pc_camera_stick_zoom_speed, 0.25f, 3, "%.2f");
        ImGui::DragFloatRange2("Zoom range", &pc_camera_stick_zoom_min, &pc_camera_stick_zoom_max, 0.01f, 0.3f, 3,
            "in %.2fx", "out %.2fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Times the game's own distance; zooming out also stops at the room's size");
        ImGui::SliderFloat("Pitch with zoom", &pc_camera_stick_zoom_pitch, 0, 15, "%.1f deg");
        bool collision = pc_camera_stick_collision != 0;
        if (ImGui::Checkbox("Stop before walls when zoomed out", &collision))
            pc_camera_stick_collision = collision;
    }
    bool r3 = pc_camera_stick_r3 != 0;
    if (ImGui::Checkbox("R3 resets the camera", &r3))
        pc_camera_stick_r3 = r3;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Back to the game's 45-degree turn and distance.\nOff: R3 is the game's (first-person view, "
                          "like Start)");
    ImGui::SliderFloat("Reset when idle", &pc_camera_stick_idle_reset, 0, 30,
        pc_camera_stick_idle_reset > 0 ? "after %.0f s" : "off");
    bool keys = pc_camera_stick_keys != 0;
    if (ImGui::Checkbox("Keyboard J/L/I/K/O", &keys))
        pc_camera_stick_keys = keys;
    if (ImGui::Button("Reset camera"))
        pc_camera_stick_reset();
    ImGui::Unindent();
}

extern "C" int pc_qol_map_invert, pc_qol_fp_invert_y; /* project_settings.cpp */
/* Graphics > Controls: the map screen's panning and the first-person view's
 * up/down (the game reads them; off by default). */
static void look_panel()
{
    ImGui::Separator();
    bool map = pc_qol_map_invert != 0;
    if (ImGui::Checkbox("Invert map panning", &map))
        pc_qol_map_invert = map;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The map screen pans the other way on both axes (D-pad, left stick, arrow keys).");
    bool fp = pc_qol_fp_invert_y != 0;
    if (ImGui::Checkbox("Invert first-person up/down", &fp))
        pc_qol_fp_invert_y = fp;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("In the first-person view (Start) up looks down and down looks up, on the D-pad and the\n"
                          "right stick. The right stick always looks around there; left/right are not inverted.");
}

extern "C" int pc_qol_confirm_cross, pc_qol_field_cross_attack; /* project_settings.cpp */
/* Graphics > Controls: Circle and Cross exchanged where the game confirms or
 * attacks (button_roles.c; off by default). */
static void button_roles_panel()
{
    ImGui::Separator();
    bool confirm = pc_qol_confirm_cross != 0;
    if (ImGui::Checkbox("Cross confirms, Circle cancels (menus)", &confirm))
        pc_qol_confirm_cross = confirm;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("From eremtsov's QoL mod. Menus, dialogues, the title, saving and loading, shops and\n"
                          "targeting confirm with Cross and cancel with Circle. Field actions, the L2 shortcuts,\n"
                          "the first-person view and the ability lists keep their buttons.");
    bool field = pc_qol_field_cross_attack != 0;
    if (ImGui::Checkbox("Cross attacks, Circle interacts (field)", &field))
        pc_qol_field_cross_attack = field;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("From eremtsov's QoL mod. In the field Cross draws the weapon and attacks (and confirms\n"
                          "the target); Circle talks, opens and sheathes. Chain and defense abilities keep\n"
                          "Circle, Triangle and Square.");
}

extern "C" int pc_qol_weapon_slots; /* project_settings.cpp; weapon_slots.c */
extern "C" int pc_skip_intro;       /* project_settings.cpp; boot_runtime.c */
extern "C" int pc_qol_skip_charging; /* project_settings.cpp; boot_window.c */
/* Graphics > Gameplay: optional gameplay changes, all off by default. */
static void gameplay_panel()
{
    bool slots = pc_qol_weapon_slots != 0;
    if (ImGui::Checkbox("Weapon slots (L2 + R2 quick swap)", &slots))
        pc_qol_weapon_slots = slots;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("From eremtsov's QoL mod. Hold L2 + R2: the shortcut overlay shows 8 weapon slots;\n"
                          "Circle/Triangle/Square/Cross or Up/Down/Right/Left equips that slot's weapon and shield.\n"
                          "Select: assign mode (the next slot button stores the equipped weapon there);\n"
                          "Start: the equipped shield into every one-handed slot; R2 + Select: store / remove.\n"
                          "Slots are saved in the same bytes as the mod, so memory cards stay interchangeable.");
    ImGui::Combo("Battle charging", &pc_qol_skip_charging, "Off\0Fast (4x)\0Skip (full speed)\0");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Settings qol_skip_charging. A spell's casting, an item's or a Break Art's wind-up before\n"
                          "it takes effect runs at several game frames per displayed frame. Every frame still runs:\n"
                          "damage, MP, items and enemy turns are exactly the original's; only the waiting is shorter.");
    bool skip = pc_skip_intro != 0;
    if (ImGui::Checkbox("Skip intro (from the next start)", &skip))
        pc_skip_intro = skip;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Start at the title without the publisher and developer logos and the opening movie\n"
                          "(settings skip_intro). Later titles, such as after the idle demo, play as the original.");
    bool travel = pc_qol_fast_travel != 0;
    if (ImGui::Checkbox("Fast travel (from the first Circle of Magic)", &travel))
        pc_qol_fast_travel = travel;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Settings qol_fast_travel. The game's own Teleportation between the Circles found, offered\n"
                          "once the prologue is over; the game's own unlock (the Lich) always applies as the game has it.");
    ImGui::Combo("Travel cost", &pc_qol_fast_travel_cost, "Free\0Flat\0Scaled\0Game\0");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Settings qol_fast_travel_cost, before the game's own unlock: free, a flat MP\n"
                          "(qol_fast_travel_flat), the game's cost scaled (qol_fast_travel_scale) or the game's.");
    if (pc_qol_fast_travel_cost == PC_FT_COST_FLAT)
        ImGui::SliderInt("Flat cost (MP)", &pc_qol_fast_travel_flat, 0, 99);
    if (pc_qol_fast_travel_cost == PC_FT_COST_SCALED)
        ImGui::SliderFloat("Cost scale", &pc_qol_fast_travel_scale, 0.1f, 1.0f, "x%.2f");
    bool prompt = pc_qol_fast_travel_prompt != 0;
    if (ImGui::Checkbox("Travel prompt", &prompt))
        pc_qol_fast_travel_prompt = prompt;
    ImGui::SliderFloat("Prompt x", &pc_qol_fast_travel_prompt_x, 0, 1);
    ImGui::SliderFloat("Prompt y", &pc_qol_fast_travel_prompt_y, 0, 1);
    ImGui::SliderFloat("Prompt size", &pc_qol_fast_travel_prompt_scale, 0.5f, 2.0f);
}

extern "C" void pc_memory_card_set_path(int slot, const char* path); /* memory_card.c */
extern "C" const char* pc_memory_card_path_setting(int slot);
extern "C" void pc_memory_card_describe(int slot, char* out, unsigned size);
/* File > Memory cards...: the file each slot uses (settings card1 / card2). */
static bool show_memory_cards;
static void memory_cards_panel()
{
    static char edit[2][512];
    static bool editing[2];
    ImGui::TextWrapped("Card files as PS1 emulators keep them: raw .mcd/.mcr/.srm, DexDrive .gme and VGS .mem/.vgs "
                       "are read and saved in their own format; PSP/PS Vita .vmp is read only. Any other file is "
                       "left alone. A relative path starts in the rood-boi folder.");
    for (int slot = 0; slot < 2; slot++) {
        ImGui::PushID(slot);
        ImGui::SeparatorText(slot ? "Slot 2" : "Slot 1");
        char info[700];
        pc_memory_card_describe(slot, info, sizeof info);
        ImGui::TextWrapped("%s", info);
        if (!editing[slot])
            snprintf(edit[slot], sizeof edit[slot], "%s", pc_memory_card_path_setting(slot));
        ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 9);
        if (ImGui::InputTextWithHint("##path", slot ? "pc/save/card2.mcd" : "pc/save/card1.mcd", edit[slot],
                sizeof edit[slot]))
            editing[slot] = true;
        ImGui::SameLine();
        if (ImGui::Button("Use")) {
            pc_memory_card_set_path(slot, edit[slot]);
            editing[slot] = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Default")) {
            pc_memory_card_set_path(slot, "");
            editing[slot] = false;
        }
        ImGui::PopID();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("A new card is inserted when the game next checks the card (opening Load or Save), never "
                       "during a save; Save settings keeps the choice.");
    ImGui::PopStyleColor();
    if (ImGui::Button("Save settings"))
        pc_settings_save();
}

static void memory_cards_window()
{
    if (!show_memory_cards)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f));
    /* Fixed width; the height follows the content (a long path adds lines). */
    float width = ImGui::GetFontSize() * 40;
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, io.DisplaySize.y));
    if (ImGui::Begin("Memory cards", &show_memory_cards,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
        memory_cards_panel();
    ImGui::End();
}

extern "C" void pc_post_panel(void); /* post_profile.cpp */
static void post_panel() { pc_post_panel(); }

static void game_panel()
{
    ImGui::SeparatorText("Game");
    if (!pc_dev_state) {
        ImGui::TextDisabled("Game state needs the battle build");
        return;
    }
    struct pc_dev_state s;
    memset(&s, 0, sizeof s);
    pc_dev_state(&s);
    ImGui::Text("Zone %d  room %d  start state %d", s.zone, s.room, s.start_state);
    ImGui::Text("Music %d (slot %d)", s.music_id, s.music_slot);
    ImGui::Text("HP %d/%d  MP %d/%d  Risk %d", s.hp, s.max_hp, s.mp, s.max_mp, s.risk);
    if (s.has_player)
        ImGui::Text("Ashley x %d  y %d  z %d  facing %d", s.x, s.y, s.z, s.facing);
    else
        ImGui::TextDisabled("No player actor");
}

static void render_panel()
{
    ImGui::SeparatorText("Native rendering");
    render_stats st;
    render_stats_get(&st);
    ImGui::Text("Room: %s", pc_native_room == 0 ? "legacy packets" : pc_native_room == 1 ? "native (+ reference packets)"
                                                                                       : "native only");
    ImGui::Text("Native: %d lists, %d instances, %d triangles", st.lists, st.instances, st.native_triangles);
    ImGui::Text("Legacy room triangles: %d drawn, %d left to native", st.legacy_room_triangles,
        st.skipped_room_triangles);
    ImGui::Text("Meshes: %d (%d polygons)", st.meshes, st.mesh_polygons);
    ImGui::Text("Characters: %s", pc_native_actors ? "native (+ reference packets)" : "legacy packets");
    ImGui::Text("Native objects: %d lists, %d triangles; %d built", st.object_instances, st.object_triangles,
        st.object_meshes_built);
    ImGui::Text("Object packet triangles: %d drawn, %d left to native", st.legacy_object_triangles,
        st.skipped_object_triangles);
    ImGui::Text("Translucent: %s; %d markers, %d polygons native", pc_native_blend ? "native" : "legacy packets",
        st.translucent_markers, st.translucent_polygons);
    ImGui::Text("Translucent packet triangles: %d drawn, %d left to native", st.legacy_translucent_triangles,
        st.skipped_translucent_triangles);
    ImGui::Text("Room blocks: %d layered lists (%d instances)", st.layered_lists, st.layered_instances);
    ImGui::Text("Drop shadows: %s; %d markers, %d polygons native", pc_native_shadows ? "native" : "legacy packets",
        st.shadow_markers, st.shadow_polygons);
    ImGui::Text("Particles: %s; %d markers, %d sprites native", pc_native_effects ? "native" : "legacy packets",
        st.effect_markers, st.effect_polygons);
    ImGui::Text("Shadow/particle packet triangles: %d drawn, %d left to native", st.legacy_effect_triangles,
        st.skipped_effect_triangles);
    ImGui::Text("Other 3D packet triangles (still reconstructed): %d", st.legacy_other_triangles);
    bool occlusion = pc_native_occlusion != 0;
    if (ImGui::Checkbox("Native surfaces hide packets behind them", &occlusion))
        pc_native_occlusion = occlusion;
    bool room_order = pc_native_room_order != 0;
    if (ImGui::Checkbox("PS1 order for coplanar room surfaces", &room_order))
        pc_native_room_order = room_order;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Room polygons lying on other room polygons (documents on a table, a notice on\n"
                          "a wall, a rug on the floor) paint in the PS1's ordering-table order instead of\n"
                          "z-fighting; occlusion stays per pixel beyond a small slack. Off: plain depth test.");
    extern int pc_room_texture_lod;
    bool lod = pc_room_texture_lod != 0;
    if (ImGui::Checkbox("PS1 half-size textures in the distance", &lod))
        pc_room_texture_lod = lod;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The PS1 draws far room polygons with a half-size copy of their texture (the\n"
                          "page before). Off (default): always the full texture, crisp at any distance.");
}

static void control_panel(double frame_ms)
{
    ImGui::SeparatorText("Run control");
    bool paused = pc_rpc_is_paused();
    if (ImGui::Button(paused ? "Resume" : "Pause"))
        pc_rpc_set_paused(!paused);
    ImGui::SameLine();
    if (ImGui::Button("Step 1"))
        pc_rpc_request_step(1);
    ImGui::SameLine();
    if (ImGui::Button("Step 30"))
        pc_rpc_request_step(30);
    ImGui::Text("vsync %u  battle frame %u", pc_vsync_count(), pc_battle_frame_count());
    ImGui::Text("Present interval %.2f ms", frame_ms);
}

/* ---- the editor: menu bar, toolbar over the viewport, outliner and
 * details docked on the right; the game image fits the space left. ---- */

static void show_toggle(const char* label, int* value);
static void save_area()
{
    pc_lights_save();
}

/* Edit: Undo "<action>", Redo "<action>", Delete, and the history (click an
 * entry to go back or forward to just after it). */
static void edit_menu()
{
    const char* undo = pc_undo_name(0);
    const char* redo = pc_undo_name(1);
    char label[200];
    snprintf(label, sizeof label, undo ? "Undo %s" : "Undo", undo);
    if (ImGui::MenuItem(label, "Ctrl+Z", false, undo != nullptr))
        pc_undo_undo();
    snprintf(label, sizeof label, redo ? "Redo %s" : "Redo", redo);
    if (ImGui::MenuItem(label, "Ctrl+Y", false, redo != nullptr))
        pc_undo_redo();
    ImGui::Separator();
    bool any = !rood_selection::empty();
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, any))
        rood_scene::copy_selection();
    if (ImGui::MenuItem("Paste", "Ctrl+V"))
        rood_scene::paste_clipboard();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, any))
        rood_scene::duplicate_selection();
    if (ImGui::MenuItem("Delete", "Delete", false, any))
        rood_scene::delete_selection();
    rood_library::save_menu_item();
    ImGui::Separator();
    if (ImGui::MenuItem("Hide selection", "H", false, any))
        rood_scene::hide_selection();
    if (ImGui::MenuItem("Hide unselected", "Shift+H", false, any))
        rood_scene::isolate_selection();
    if (ImGui::MenuItem("Show all", "Ctrl+H", false, rood_selection::any_hidden()))
        rood_scene::unhide_all();
    int current, count = pc_undo_history(&current);
    if (ImGui::BeginMenu("Undo history", count > 0)) {
        if (ImGui::MenuItem("(the room as loaded)", nullptr, current == 0))
            pc_undo_jump(0);
        for (int i = 0; i < count; i++) {
            ImGui::PushID(i);
            bool undone = i >= current;
            if (undone)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (ImGui::MenuItem(pc_undo_entry(i), nullptr, i == current - 1))
                pc_undo_jump(i + 1);
            if (undone)
                ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    ImGui::TextDisabled("History: this room visit, %d steps kept", PC_UNDO_LIMIT);
}

static void menu_bar(float* height)
{
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save area", "Ctrl+S"))
            save_area();
        if (ImGui::MenuItem("Reload area"))
            pc_lights_reload();
        pc_lighting_file_menu(); /* Promote to pack (lighting_editor.cpp) */
        ImGui::Separator();
        if (ImGui::MenuItem("Save settings", "Ctrl+Shift+S"))
            pc_settings_save();
        if (ImGui::MenuItem("Reload settings"))
            pc_settings_load();
        ImGui::Separator();
        ImGui::MenuItem("Memory cards...", nullptr, &show_memory_cards);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        edit_menu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Lights and glows", nullptr, pc_lights_show());
    ImGui::MenuItem("Fog volumes", nullptr, pc_atmosphere_show_volumes());
    ImGui::MenuItem("Emitters", nullptr, rood_particles::emitters_shown_flag());
        show_toggle("Room wireframe", &pc_overlay_wire);
        show_toggle("Room points", &pc_overlay_points);
        show_toggle("Character wireframe", &pc_overlay_actor_wire);
        ImGui::Separator();
        bool free_camera = pc_editor_active();
        if (ImGui::MenuItem("Free camera", "F2 / RMB", free_camera))
            pc_editor_toggle();
        ImGui::SliderFloat("UI scale", &ui_scale, 0.75f, 4.0f, "%.2fx");
        if (ImGui::IsItemDeactivatedAfterEdit())
            apply_scale();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Outliner"))
            outliner_tab_request = 0;
        if (ImGui::MenuItem("Library", "Shift+1", place_tab_shown || !rood_library::docked()))
            outliner_tab_request = 1;
        ImGui::MenuItem("Room Browser", "Ctrl+R", &pc_room_browser_open);
        ImGui::MenuItem("Keyboard shortcuts", nullptr, &show_help);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Keyboard shortcuts", nullptr, &show_help);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    bool paused = pc_rpc_is_paused();
    if (ImGui::Button(paused || pc_editor_active() ? "Play" : "Pause")) {
        if (pc_editor_active())
            pc_editor_toggle(); /* back to the game camera, running */
        else
            pc_rpc_set_paused(!paused);
    }
    if (ImGui::Button("Step"))
        pc_rpc_request_step(1);
    int z, r;
    bool dirty = pc_lights_room(&z, &r);
    ImGui::TextDisabled("|  zone %d room %d%s  |  %s", z, r, dirty ? " (unsaved)" : "",
        paused ? (pc_editor_active() ? "Free camera, simulation paused" : "Simulation paused")
               : (pc_editor_active() ? "Free camera, playing" : "Playing"));
    *height = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
}

/* Seeing into rooms from the free camera (room_editor.h): the game's own
 * cutaway walls for the free camera's direction, the black shells left out,
 * and the cutaway between the eye and the focus. */
static void room_view_controls()
{
    bool walls = pc_editor_game_cutaway, shells = pc_editor_hide_masks, cut = pc_editor_cutaway;
    if (ImGui::Checkbox("Cutaway walls", &walls))
        pc_editor_game_cutaway = walls;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Leave out the walls facing the camera, as the game does for its own camera");
    if (ImGui::Checkbox("Hide black shells", &shells))
        pc_editor_hide_masks = shells;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Leave out the black wall tops and outsides that mask the void around a room");
    if (ImGui::Checkbox("Cut to focus (C)", &cut))
        pc_editor_cutaway = cut;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ghost the room surfaces between the camera and the selected light or glow (else Ashley)\n"
                          "when they hide it: drawn faint, with their edges, so the room stays in view");
    if (!pc_editor_cutaway)
        ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
    ImGui::SliderFloat("Radius", &pc_editor_cutaway_radius, 32, 2048, "%.0f", ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
    ImGui::SliderFloat("Ghost opacity", &pc_editor_cutaway_opacity, 0, 0.9f, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How much of a cut surface still shows (dithered, edges kept); 0 cuts it away entirely");
    bool automatic = pc_editor_cutaway_auto != 0;
    if (ImGui::Checkbox("Only when the focus is hidden", &automatic))
        pc_editor_cutaway_auto = automatic;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Cut only while room surfaces hide the focus from the camera (a ray from the eye);\n"
                          "off: always cut what stands between them");
    if (!pc_editor_cutaway)
        ImGui::EndDisabled();
    if (!pc_editor_active())
        ImGui::TextDisabled("Applies to the free camera (RMB in the view or F2).");
}

/* Outliner: the room's groups, with per-group visibility in the free
 * camera's view. */
static void room_groups_outliner(const char* filter)
{
    static pc_room_group_info groups[4096];
    int count = pc_room_editor_groups(groups, 4096);
    if (!count) {
        ImGui::TextDisabled("No room geometry");
        return;
    }
    if (ImGui::SmallButton("Show all"))
        pc_room_editor_set_hidden(-1, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("free camera only");
    for (int i = 0; i < count; i++) {
        const pc_room_group_info& g = groups[i];
        char label[96];
        snprintf(label, sizeof label, "Group %d%s%s%s  (%d polys, %08x)", i, g.black ? " black shell" : "",
            g.cutaway ? " [cut]" : "", g.game_hidden ? " [room hides]" : "", g.polygons, g.flags);
        if (filter && *filter && !strstr(label, filter))
            continue;
        ImGui::PushID(i);
        bool visible = !g.hidden;
        if (ImGui::Checkbox("##visible", &visible))
            pc_room_editor_set_hidden(i, !visible);
        ImGui::SameLine();
        bool dim = g.hidden || g.game_hidden || g.cutaway || (g.black && pc_editor_hide_masks);
        if (dim)
            ImGui::TextDisabled("%s", label);
        else
            ImGui::TextUnformatted(label);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("at %d %d %d", g.position[0], g.position[1], g.position[2]);
        ImGui::PopID();
    }
}

/* ---- the toolbar: one compact row (wrapping when the view is narrow) ---- */

/* Put the next item on this row when `width` more pixels fit, else start a
 * new row. */
static void flow(float width)
{
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    if (ImGui::GetItemRectMax().x + spacing + width <= right)
        ImGui::SameLine();
}
static float button_width(const char* label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
}
/* A gap between groups of tools. */
static void group_gap(float next_width)
{
    float spacing = ImGui::GetStyle().ItemSpacing.x, gap = ImGui::GetFontSize() * 0.9f;
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    if (ImGui::GetItemRectMax().x + spacing + gap + next_width <= right)
        ImGui::SameLine(0, spacing + gap);
}
/* A button that shows an on/off state (highlighted when on). */
static bool toggle_button(const char* label, bool on, const char* tooltip)
{
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    bool clicked = ImGui::Button(label);
    if (on)
        ImGui::PopStyleColor(2);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", tooltip);
    return clicked;
}
static void toggle_int(const char* label, int* value, const char* tooltip)
{
    flow(button_width(label));
    if (toggle_button(label, *value != 0, tooltip))
        *value = !*value;
}

/* Snap settings (the arrow next to the snap toggles). */
static void snap_popup()
{
    ImGui::SeparatorText("Grid");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::DragFloat("Move step", &pc_lights_snap_move, 1, 1, 512, "%.0f u");
    /* Quick choices, powers of two as Unreal's grid sizes. */
    static const float moves[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    for (int i = 0; i < 8; i++) {
        char label[32];
        snprintf(label, sizeof label, "%g", moves[i]);
        if (i)
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        if (toggle_button(label, pc_lights_snap_move == moves[i], nullptr))
            pc_lights_snap_move = moves[i];
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::DragFloat("Rotation step", &pc_lights_snap_angle, 1, 1, 90, "%.1f deg");
    static const float angles[] = { 1, 5, 10, 15, 22.5f, 30, 45, 90 };
    for (int i = 0; i < 8; i++) {
        char label[32];
        snprintf(label, sizeof label, "%g##angle", angles[i]);
        if (i)
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        if (toggle_button(label, pc_lights_snap_angle == angles[i], nullptr))
            pc_lights_snap_angle = angles[i];
    }
    ImGui::SeparatorText("Points and edges");
    bool mid = pc_snap_midpoint != 0;
    if (ImGui::Checkbox("Edge midpoints", &mid))
        pc_snap_midpoint = mid;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Edge snapping prefers an edge's midpoint near the cursor");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::SliderFloat("Search radius", &pc_snap_radius, 4, 48, "%.0f px");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::SliderFloat("Highlight radius", &pc_snap_highlight, 8, 512, "%.0f u", ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Points this close to the snapped one light up, brighter the nearer they are");
    bool show = pc_overlay_snap_show != 0;
    if (ImGui::Checkbox("Show points and edges while snapping", &show))
        pc_overlay_snap_show = show;
    ImGui::SeparatorText("Surface");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::DragFloat("Offset along normal", &pc_snap_offset, 1, 0, 1024, "%.0f u");
    ImGui::TextDisabled("Hold V while dragging: toggle point snapping.\nHold Ctrl while dragging: invert grid snapping.");
}

static void show_toggle(const char* label, int* value)
{
    bool on = *value != 0;
    if (ImGui::MenuItem(label, nullptr, &on))
        *value = on;
}
/* The Show dropdown: what the viewport draws over the picture. */
static void show_popup()
{
    ImGui::MenuItem("Lights and glows", nullptr, pc_lights_show());
    ImGui::MenuItem("Fog volumes", nullptr, pc_atmosphere_show_volumes());
    ImGui::MenuItem("Emitters", nullptr, rood_particles::emitters_shown_flag());
    show_toggle("Room wireframe", &pc_overlay_wire);
    show_toggle("Room points", &pc_overlay_points);
    show_toggle("Character wireframe", &pc_overlay_actor_wire);
    ImGui::Separator();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::SliderFloat("Hidden lines", &pc_overlay_occluded, 0, 1, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Opacity of edges and points behind other surfaces (0 hides them)");
}

static void toolbar(float x, float y, float w, float* height)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::Begin("##toolbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBringToFrontOnFocus);
    float em = ImGui::GetFontSize();
    /* Undo and redo (curved arrows, as Unreal's toolbar). */
    for (int redo = 0; redo < 2; redo++) {
        const char* name = pc_undo_name(redo);
        if (redo)
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::BeginDisabled(name == nullptr);
        float side = ImGui::GetFrameHeight();
        bool clicked = ImGui::Button(redo ? "##redo" : "##undo", ImVec2(side, side));
        ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList* d = ImGui::GetWindowDrawList();
        ImVec2 c((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f + side * 0.08f);
        float r = side * 0.26f, s = redo ? 1.0f : -1.0f;
        /* An arc over the top, ending in an arrowhead pointing down-out. */
        d->PathArcTo(c, r, 3.14159265f * (redo ? 1.05f : -0.05f), 3.14159265f * (redo ? 2.0f : -1.0f), 12);
        d->PathStroke(col, 0, side * 0.09f);
        ImVec2 tip(c.x + s * r, c.y + side * 0.06f);
        d->AddTriangleFilled(ImVec2(tip.x - side * 0.14f, tip.y - side * 0.06f), ImVec2(tip.x + side * 0.14f, tip.y - side * 0.06f),
            ImVec2(tip.x, tip.y + side * 0.14f), col);
        ImGui::EndDisabled();
        if (clicked)
            redo ? pc_undo_redo() : pc_undo_undo();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (name)
                ImGui::SetTooltip(redo ? "Redo %s (Ctrl+Y)" : "Undo %s (Ctrl+Z)", name);
            else
                ImGui::SetTooltip(redo ? "Nothing to redo" : "Nothing to undo");
        }
    }
    /* Place: UE's quick add (a menu of lights and actors, placed at the
     * view's centre); the Library drags them anywhere. */
    group_gap(button_width("+ Place"));
    if (toggle_button("+ Place", place_tab_shown, "Add a light or light actor at the view's centre;\n"
                                                  "or drag one from the Library (beside the Outliner)"))
        ImGui::OpenPopup("place menu");
    if (ImGui::BeginPopup("place menu")) {
        if (ImGui::MenuItem("Open the Library (drag into the view)"))
            outliner_tab_request = 1;
        pc_lights_place_menu();
        ImGui::Separator();
        if (ImGui::MenuItem("Rect light on a window..."))
            rood_fit::arm(rood_selection::Ref());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Click a window pane in the view: a rect light its size, in front of it, facing out");
        if (ImGui::BeginMenu("Fit to window options")) {
            rood_fit::options();
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    /* Rooms: the Room Browser (go to any room to light it). */
    flow(button_width("Rooms"));
    if (toggle_button("Rooms", pc_room_browser_open, "Room Browser (Ctrl+R): every zone and room; go to one to light it"))
        pc_room_browser_open = !pc_room_browser_open;
    /* Light fixture: click a lantern, candle or torch (room polygons or its
     * glow) to give it a light; the arrow lights every fixture in the room. */
    bool* fixture = pc_lights_fixture_mode();
    flow(button_width("Light fixture"));
    if (toggle_button("Light fixture", *fixture, "Light fixture tool (L lights the one under the cursor):\n"
                                                 "click a lantern, candle or torch to put a light at its flame"))
        *fixture = !*fixture;
    flow(ImGui::GetFrameHeight());
    if (ImGui::ArrowButton("##fixturemenu", ImGuiDir_Down))
        ImGui::OpenPopup("fixture menu");
    if (ImGui::BeginPopup("fixture menu")) {
        if (ImGui::MenuItem("Light all fixtures in room..."))
            pc_lights_light_all_fixtures();
        ImGui::EndPopup();
    }
    /* Transform. */
    int* op = pc_lights_gizmo_operation();
    group_gap(button_width("Move"));
    if (toggle_button("Move", *op == 0, "Move tool (W)"))
        *op = 0;
    flow(button_width("Rotate"));
    if (toggle_button("Rotate", *op == 1, "Rotate tool (E): aims spot and rect lights"))
        *op = 1;
    flow(button_width("Scale"));
    if (toggle_button("Scale", *op == 2,
            "Scale tool (R): a light's reach (point, spot) or size and reach (rect), a light actor's size (its lights\n"
            "and their radii with it), a fog volume's size. Never brightness."))
        *op = 2;
    flow(button_width("Local"));
    if (toggle_button(pc_lights_gizmo_space ? "Local" : "World", pc_lights_gizmo_space != 0,
            "Move and rotate along the world's axes or the object's own (scale is always the object's)"))
        pc_lights_gizmo_space = !pc_lights_gizmo_space;
    /* Snapping. */
    group_gap(button_width("Grid"));
    if (toggle_button("Grid", pc_lights_snap != 0, "Snap moves and rotations to the grid (Ctrl while dragging inverts)"))
        pc_lights_snap = !pc_lights_snap;
    flow(em * 3.2f);
    ImGui::SetNextItemWidth(em * 3.2f);
    ImGui::DragFloat("##snapmove", &pc_lights_snap_move, 1, 1, 512, "%.0f");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Grid step (world units); rotation step in the snap settings");
    toggle_int("Point", &pc_snap_point, "Snap to room points (vertices) near the cursor\nHold V while dragging to toggle");
    toggle_int("Edge", &pc_snap_edge, "Snap to the nearest point on a room edge (midpoints: snap settings)");
    toggle_int("Surface", &pc_snap_surface, "Drag along room surfaces, held off them along the normal");
    flow(ImGui::GetFrameHeight());
    if (ImGui::ArrowButton("##snapsettings", ImGuiDir_Down))
        ImGui::OpenPopup("snap settings");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Snap settings");
    if (ImGui::BeginPopup("snap settings")) {
        snap_popup();
        ImGui::EndPopup();
    }
    /* View mode (0-5, the present shader's view_mode), then the path
     * tracer's buffers and depth. */
    group_gap(em * 9);
    ImGui::SetNextItemWidth(em * 9);
    int show = pc_gl_show_depth ? 9 : pc_gi_debug ? 5 + pc_gi_debug : pc_gl_view_mode;
    if (ImGui::Combo("##view", &show,
            "Lit\0Unlit\0Lighting only\0Base colour (textures)\0Vertex colours\0World normals\0"
            "Indirect light\0Shadow visibility\0Baked light visibility\0Depth\0")) {
        pc_gl_view_mode = show <= 5 ? show : 0;
        pc_gi_debug = show >= 6 && show <= 8 ? show - 5 : 0;
        pc_gl_show_depth = show == 9;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("View mode");
    float show_width = button_width("Show") + ImGui::GetFrameHeight();
    flow(show_width);
    ImGui::SetNextItemWidth(show_width);
    if (ImGui::BeginCombo("##show", "Show", ImGuiComboFlags_HeightLarge)) {
        show_popup();
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Lights, room wireframe and points, character wireframe");
    float rooms_width = button_width("See into rooms") + ImGui::GetFrameHeight();
    flow(rooms_width);
    ImGui::SetNextItemWidth(rooms_width);
    if (ImGui::BeginCombo("##rooms", "See into rooms", ImGuiComboFlags_HeightLarge)) {
        room_view_controls();
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Free camera: cutaway walls, black shells, cut to focus");
    /* Lighting quality: Original / Low / Medium / High / Ultra / Custom
     * (every setting: Details > Graphics > Lighting quality). */
    group_gap(ImGui::CalcTextSize("Lighting").x + ImGui::GetStyle().ItemSpacing.x + em * 6.5f);
    pc_lighting_toolbar();
    group_gap(button_width("Inspect"));
    if (toggle_button("Inspect", *pc_lights_probe_mode(), "Inspect (I): click the view to probe a surface"))
        *pc_lights_probe_mode() = !*pc_lights_probe_mode();
    flow(button_width("?"));
    if (ImGui::Button("?"))
        show_help = !show_help;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Keyboard shortcuts (Help menu)");
    *height = ImGui::GetWindowSize().y;
    ImGui::End();
}

/* Help > Keyboard shortcuts: every binding, searchable. */
static void help_window()
{
    if (!show_help)
        return;
    static const char* const shortcuts[][3] = {
        { "Editor", "Esc / F1", "Open or close the editor" },
        { "Editor", "F2", "Free camera on or off (opens the editor)" },
        { "Editor", "Ctrl+S", "Save area (this room's lights and settings: your user layer)" },
        { "Editor", "Ctrl+R", "Room Browser: every zone and room; go to one" },
        { "Editor", "Ctrl+Z", "Undo (Edit menu: the action's name; the history covers this room visit)" },
        { "Editor", "Ctrl+Y / Ctrl+Shift+Z", "Redo" },
        { "Editor", "Delete / Backspace", "Delete the selection (lights, light actors, fog volumes)" },
        { "Editor", "Ctrl+C / Ctrl+V", "Copy the selection / paste it where it was copied (lighting items as text)" },
        { "Editor", "Ctrl+D", "Duplicate the selection (64 units along x; the copies are selected)" },
        { "Editor", "H / Shift+H / Ctrl+H", "Hide the selection / hide everything else / show everything (editor only)" },
        { "Details", "Middle-drag (number)", "Value ladder: up/down picks steps of 10, 1, 0.1, 0.01; left/right adds them; Esc cancels" },
        { "Details", "Reset arrow", "Back to the default, or the preset's or prefab's value" },
        { "Details", "Lock", "Keep Details on its object while you select others" },
        { "Fields", "Click (number)", "Type a value (a drag scrubs it); sliders: double-click or Ctrl+click" },
        { "Fields", "Enter / Tab", "Commit the value / commit and move to the next field" },
        { "Fields", "Esc (typing)", "Cancel the edit (the editor stays open); no shortcut fires while typing" },
        { "Editor", "Ctrl+Shift+S", "Save settings (pc/user/settings.cfg)" },
        { "Camera", "RMB drag", "Look (starts the free camera from the game camera)" },
        { "Camera", "RMB + W A S D", "Fly" },
        { "Camera", "RMB + Q / E", "Fly down / up" },
        { "Camera", "RMB + Shift", "Fly faster" },
        { "Camera", "RMB + wheel", "Flight speed" },
        { "Camera", "Alt + LMB drag", "Orbit" },
        { "Camera", "MMB drag", "Pan" },
        { "Camera", "F", "Frame the selection (a light, actor, glow or fog volume)" },
        { "Select", "Click", "Select a light, light actor (its icon or any of its lights), glow or fog volume (inside its outline)" },
        { "Select", "Shift+click / Ctrl+click", "Add to the selection / add or take out (the view and the Outliner)" },
        { "Select", "Drag on empty space", "Box select lights, light actors and fog volumes (Shift adds, Ctrl toggles); it may start in a fog volume" },
        { "Lights", "Shift+click (empty)", "Move the selected light or actor to the surface (or the snapped point or edge)" },
        { "Lights", "Ctrl+click (empty)", "Add the Library item (a point light by default) at the surface (or the snapped point or edge); in an unselected fog volume too" },
        { "Lights", "Ctrl+click (a glow)", "Place the Library item at the surface there (a selected glow is taken out of the selection instead)" },
        { "Lights", "Alt + drag gizmo", "Duplicate the light or actor (the copy stays behind)" },
        { "Fixtures", "L", "Light the fixture under the cursor (lantern, candle, torch: room polygons or its glow)" },
        { "Fixtures", "Click (Light fixture tool)", "Light the clicked fixture: an actor at its flame, its polygons casting no shadow from it" },
        { "Fixtures", "Light fixture > Light all", "Lights for every flame in the room, to review and add" },
        { "Place", "Drag from the Library", "Place a light, light actor, fixture or fog volume on the surface where it is dropped (snapping as set; a preview follows)" },
        { "Place", "Esc (dragging)", "Cancel the drag (dropping off the view cancels too)" },
        { "Place", "Double-click in the Library", "Place at the surface under the view's centre" },
        { "Place", "Shift+1", "Show the Library (List / Grid, Pop out / Dock on its toolbar; the star: a favourite)" },
        { "Tools", "W", "Move tool" },
        { "Tools", "E", "Rotate tool (aim spot and rect lights, turn actors)" },
        { "Tools", "R", "Scale tool: a light's reach or size, a light actor's size (its lights and radii with it), a fog volume's size; never brightness" },
        { "Tools", "World / Local (toolbar)", "Move and rotate along the world's axes or the object's own; scale is always the object's" },
        { "Tools", "I", "Inspect: click the view to probe a surface" },
        { "Handles", "Drag the ring", "The selected light's or sun's intensity: doubles every 100 pixels to the right" },
        { "Handles", "Drag a dot", "Radius, range, cone, inner cone, a rect's edges (Alt: both edges), barn doors" },
        { "Tools", "Fit to window (a rect light's Details, + Place)", "Click a window pane: the rect light takes its size and sits in front of it, facing out; Esc cancels" },
        { "Handles", "A fog volume's ring / dot / swatch", "Density (doubles every 100 pixels to the right), the soft edge (the inner outline: drag out for a harder edge), its colour" },
        { "Handles", "Ctrl / Shift while dragging", "Fine steps / round values; the value shows by the pointer; Esc puts it back" },
        { "Handles", "Ring's top dot", "A point light's shadows on or off" },
        { "Handles", "Swatch", "The colour popover: Kelvin and tint, a hue wheel, the room's palette, recent colours, eyedropper, Hex / RGB" },
        { "Handles", "Spot's crosshair, then a click", "Aim the spot at the clicked surface (Esc stops)" },
        { "Handles", "Sun: drag its disc", "Its direction across the sky dome (azimuth and elevation)" },
        { "View", "C", "Cut to focus on or off: surfaces hiding the selection (else Ashley) are drawn as ghosts" },
        { "Snapping", "V (hold)", "Toggle point snapping while dragging; shows the snap target under the cursor" },
        { "Snapping", "Ctrl (hold, dragging)", "Invert grid snapping" },
    };
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 46, ImGui::GetFontSize() * 30), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Keyboard shortcuts", &show_help, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    static ImGuiTextFilter filter;
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##filter", "Search shortcuts (keys or actions)", filter.InputBuf,
            IM_ARRAYSIZE(filter.InputBuf)))
        filter.Build();
    if (ImGui::BeginTable("shortcuts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5);
        ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 11);
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& row : shortcuts) {
            char line[256];
            snprintf(line, sizeof line, "%s %s %s", row[0], row[1], row[2]);
            if (!filter.PassFilter(line))
                continue;
            ImGui::TableNextRow();
            for (int c = 0; c < 3; c++) {
                ImGui::TableNextColumn();
                if (c == 0)
                    ImGui::TextDisabled("%s", row[c]);
                else
                    ImGui::TextWrapped("%s", row[c]);
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

static void outliner(float x, float y, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Outliner", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar); /* the tabs are its title */
    /* The Outliner and the Library (UE's Place Actors, with every kind of thing to place). */
    place_tab_shown = false;
    if (ImGui::BeginTabBar("outliner tabs")) {
        ImGuiTabItemFlags outliner_flags = outliner_tab_request == 0 ? ImGuiTabItemFlags_SetSelected : 0;
        ImGuiTabItemFlags place_flags = outliner_tab_request == 1 ? ImGuiTabItemFlags_SetSelected : 0;
        outliner_tab_request = -1;
        if (ImGui::BeginTabItem("Outliner", nullptr, outliner_flags)) {
            static char filter[64];
            ImGui::SetNextItemWidth(-1);
            if (focus_search_request) {
                focus_search_request = false;
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextWithHint("##search", "Search", filter, sizeof filter);
            ImGui::BeginChild("##outliner rows");
            /* A selection made elsewhere opens its section (its row is then
             * scrolled to: editor_scene.cpp row_drawn). */
            if (rood_scene::scroll_due(rood_selection::KIND_LIGHT, rood_selection::KIND_ACTOR))
                ImGui::SetNextItemOpen(true);
            if (ImGui::TreeNodeEx("Lights and actors (this room)", ImGuiTreeNodeFlags_DefaultOpen)) {
                pc_lights_outliner(filter);
                ImGui::TreePop();
            }
            if (pc_gl_compute || (&pc_vk_active && pc_vk_active)) {
                if (rood_scene::scroll_due(rood_selection::KIND_GLOW))
                    ImGui::SetNextItemOpen(true);
                if (ImGui::TreeNodeEx("Glows (from the game)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    pc_lights_glows_outliner(filter);
                    ImGui::TreePop();
                }
            }
            if (rood_scene::scroll_due(rood_selection::KIND_VOLUME))
                ImGui::SetNextItemOpen(true);
            if (ImGui::TreeNodeEx("Fog volumes (this room)", ImGuiTreeNodeFlags_DefaultOpen)) {
                rood_atmo::outliner(filter);
                ImGui::TreePop();
            }
            if (rood_scene::scroll_due(rood_selection::KIND_EMITTER))
                ImGui::SetNextItemOpen(true);
            if (ImGui::TreeNodeEx("Emitters (this room)", ImGuiTreeNodeFlags_DefaultOpen)) {
                rood_particles::outliner(filter);
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Room groups")) {
                room_groups_outliner(filter);
                ImGui::TreePop();
            }
            /* Right-click: the selection's menu (as Edit's). */
            if (ImGui::BeginPopupContextWindow("outliner menu")) {
                bool any = !rood_selection::empty();
                if (ImGui::MenuItem("Copy", "Ctrl+C", false, any))
                    rood_scene::copy_selection();
                if (ImGui::MenuItem("Paste", "Ctrl+V"))
                    rood_scene::paste_clipboard();
                if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, any))
                    rood_scene::duplicate_selection();
                if (ImGui::MenuItem("Delete", "Delete", false, any))
                    rood_scene::delete_selection();
                ImGui::Separator();
                rood_library::save_menu_item();
                ImGui::EndPopup();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        /* The Library (editor_library.cpp): this tab, or its own window when popped out. */
        if (rood_library::docked() && ImGui::BeginTabItem("Library", nullptr, place_flags)) {
            place_tab_shown = true;
            rood_library::panel();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

static void details(float x, float y, float w, float h, double frame_ms)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Details", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 13);
    ImGuiTabItemFlags select[TAB_COUNT] = {};
    /* Selection follows you: a new selection shows in Details, unless Details
     * is locked or the change came from Details itself or from undo. */
    static unsigned seen_serial;
    if (rood_selection::serial() != seen_serial) {
        seen_serial = rood_selection::serial();
        rood_selection::Source s = rood_selection::last_source();
        if (!rood_selection::empty() && !rood_selection::locked() && s != rood_selection::FROM_DETAILS
            && s != rood_selection::FROM_UNDO)
            details_tab_request = TAB_DETAILS;
    }
    if (details_tab_request >= 0)
        select[details_tab_request] = ImGuiTabItemFlags_SetSelected;
    details_tab_request = -1;
    if (ImGui::BeginTabBar("details")) {
        if (ImGui::BeginTabItem("Details", nullptr, select[TAB_DETAILS])) {
            details_tab_shown = TAB_DETAILS;
            rood_details::draw();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lighting", nullptr, select[TAB_LIGHTING])) {
            details_tab_shown = TAB_LIGHTING;
            ImGui::SeparatorText("This room");
            if (pc_room_settings_page("Lighting"))
                pc_lights_mark_dirty();
            if (ImGui::TreeNodeEx("Game glows by kind", 0)) {
                ImGui::TextDisabled("Fixtures and magic glows are also listed one by one in the Outliner;\n"
                                    "effects (attacks, spells, sparks) only as a kind.");
                if (pc_room_settings_page("Glows"))
                    pc_lights_mark_dirty();
                ImGui::TreePop();
            }
            ImGui::TextDisabled("Highlighted values override the project default for this room.");
            if (ImGui::Button("Save area"))
                save_area();
            ImGui::SameLine();
            if (ImGui::Button("Reload area"))
                pc_lights_reload();
            ImGui::TextWrapped("%s", pc_lights_status());
            ImGui::SeparatorText("All rooms");
            pc_lights_global_settings();
            if (pc_hw_active) {
                ImGui::SeparatorText("Quality");
                pc_lighting_toolbar();
                ImGui::TextDisabled("Every setting: Graphics tab > Lighting quality");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Inspector", nullptr, select[TAB_INSPECTOR])) {
            details_tab_shown = TAB_INSPECTOR;
            pc_lights_inspector();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Atmosphere", nullptr, select[TAB_ATMOSPHERE])) {
            details_tab_shown = TAB_ATMOSPHERE;
            pc_environment_page(); /* marks the room dirty itself */
            ImGui::SeparatorText("Haze (this room)");
            if (pc_room_settings_page("Atmosphere"))
                pc_lights_mark_dirty();
            if (ImGui::Button("Save area"))
                save_area();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Post-processing", nullptr, select[TAB_POST])) {
            details_tab_shown = TAB_POST;
            post_panel();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Graphics", nullptr, select[TAB_GRAPHICS])) {
            details_tab_shown = TAB_GRAPHICS;
            if (ImGui::Button("Save settings"))
                pc_settings_save();
            ImGui::SameLine();
            if (ImGui::Button("Reload settings"))
                pc_settings_load();
            ImGui::TextWrapped("%s", pc_settings_status());
            if (pc_hw_active && ImGui::CollapsingHeader("Lighting quality", ImGuiTreeNodeFlags_DefaultOpen))
                pc_lighting_panel();
            if (ImGui::CollapsingHeader("Display and rendering", ImGuiTreeNodeFlags_DefaultOpen))
                graphics_panel();
            rood_details::kind_section("menu_model_light", "Menu model light"); /* ui/menu_light_kind.cpp */
            if (ImGui::CollapsingHeader("Controls")) {
                camera_stick_panel();
                look_panel();
                button_roles_panel();
            }
            if (ImGui::CollapsingHeader("Gameplay"))
                gameplay_panel();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Emissive", nullptr, select[TAB_EMISSIVE])) {
            details_tab_shown = TAB_EMISSIVE;
            pc_emissive_panel(); /* emissive_editor.cpp: glowing textures */
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Diagnostics", nullptr, select[TAB_DIAGNOSTICS])) {
            details_tab_shown = TAB_DIAGNOSTICS;
            if (pc_hw_active)
                pc_editor_panel();
            if (pc_hw_active)
                render_panel();
            game_panel();
            control_panel(frame_ms);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

/* The editor's shortcuts that work wherever the pointer is (the Outliner
 * too): save, undo / redo, delete, hide, copy / paste / duplicate. */
static void editor_shortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift)
            pc_settings_save();
        else
            save_area();
    }
    /* Undo: Ctrl+Z; redo: Ctrl+Y or Ctrl+Shift+Z (held: repeats). */
    if (io.KeyCtrl && !io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, true))
            io.KeyShift ? pc_undo_redo() : pc_undo_undo();
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, true))
            pc_undo_redo();
    }
    /* Delete (or Backspace) deletes the selection, wherever the pointer is
     * (the Outliner too), as long as nothing is being typed. */
    if (!io.KeyCtrl && !io.KeyAlt
        && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
        rood_scene::delete_selection();
    /* Hiding, wherever the pointer is (editor state, never saved in the room):
     * H the selection, Shift+H everything else, Ctrl+H shows everything. */
    if (!io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_H, false)) {
        if (io.KeyCtrl)
            rood_scene::unhide_all();
        else if (io.KeyShift)
            rood_scene::isolate_selection();
        else
            rood_scene::hide_selection();
    }
    /* Copy, paste (where it was copied), duplicate (64 units along x). */
    if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false))
            rood_scene::copy_selection();
        if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            rood_scene::paste_clipboard();
        if (ImGui::IsKeyPressed(ImGuiKey_D, false))
            rood_scene::duplicate_selection();
    }
}

/* Editor shortcuts. None fires while a field is being typed in (a number,
 * a name, a search: the keys are its text, Escape cancels it, Ctrl+Z is its
 * own undo) or while the camera flies; the view's tools only with the
 * pointer over the view. */
static void viewport_keys()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsAnyItemActive()) /* typing, or a slider or button held */
        return;
    /* Flying the camera (right mouse held in the view): the keys are WASD/QE
     * and their modifiers, never editor shortcuts (Ctrl+D would duplicate). */
    bool flying = pc_editor_flying() != 0;
    if (!flying)
        editor_shortcuts();
    if (io.WantCaptureMouse)
        return;
    /* Right mouse in the game view: fly from the game camera (as in UE);
     * the free camera stays until Play. */
    float mx = io.MousePos.x, my = io.MousePos.y;
    /* The modern menus over the game picture have the pointer there. */
    bool in_view = mx >= pc_gl_view[0] && my >= pc_gl_view[1] && mx < pc_gl_view[0] + pc_gl_view[2]
        && my < pc_gl_view[1] + pc_gl_view[3] && !pc_ui_shell_wants_mouse();
    if (!pc_editor_active() && in_view && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && pc_hw_active)
        pc_editor_toggle();
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) /* WASD fly, not shortcuts */
        return;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false))
        *pc_lights_gizmo_operation() = 0;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false))
        *pc_lights_gizmo_operation() = 1;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false)) /* Ctrl+R is the Room Browser */
        *pc_lights_gizmo_operation() = 2;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false))
        *pc_lights_probe_mode() = !*pc_lights_probe_mode();
    if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_1, false))
        outliner_tab_request = 1;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false))
        pc_room_browser_open = !pc_room_browser_open;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false) && in_view)
        pc_lights_light_fixture(mx, my);
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && in_view)
        pc_editor_cutaway = !pc_editor_cutaway; /* See into rooms > Cut to focus */
}

// Draw the overlay over the presented frame (call before swapping buffers).
extern "C" void pc_overlay_frame(int width, int height)
{
    if (!active)
        return;
    ImGuiIO& io = ImGui::GetIO();
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double t = now.tv_sec + now.tv_nsec * 1e-9;
    double dt = last_time > 0 ? t - last_time : 1.0 / 60;
    last_time = t;
    if (applied_scale == 0.0f)
        apply_scale();
    /* Data changed on disk (settings, room files) takes effect live. */
    static double last_poll;
    if (t - last_poll > 0.5) {
        last_poll = t;
        pc_settings_poll();
        pc_lights_poll();
    }
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        pc_vk_overlay_new_frame();
    else
#endif
        ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    io.DeltaTime = dt > 0 ? (float)dt : 1.0f / 60;
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    if (clear_focus_request) { /* RPC editor_input {focus "none"} */
        clear_focus_request = false;
        ImGui::ClearActiveID();
        ImGui::SetWindowFocus(nullptr);
    }
    if (pc_editor_active()) {
        pc_editor_input();
        pc_editor_grid();
    }
    update_relative_mouse();
    /* The undo history's base: the state before the panels run (only while
     * the editor shows: gameplay pays nothing; RPC edits commit themselves). */
    static bool was_visible;
    if (visible && !was_visible)
        pc_undo_rebase();
    was_visible = visible;
    if (visible) {
        rood_scene::frame(); /* the selection follows the room's objects (editor_scene.cpp) */
        pc_undo_frame_begin();
    }
    pc_room_browser_frame(); /* a warp arriving: the free camera and pause come back */
    if (visible) {
        float bar = 0, tools = 0;
        menu_bar(&bar);
        float panel = (float)width * 0.3f;
        float least = ImGui::GetFontSize() * 26;
        if (panel < least)
            panel = least;
        if (panel > width * 0.5f)
            panel = width * 0.5f;
        float view_w = width - panel;
        toolbar(0, bar, view_w, &tools);
        /* The Library gets more of the column (its tiles are the point of it). */
        float split = place_tab_shown ? 0.55f : 0.35f;
        outliner(view_w, bar, panel, (height - bar) * split);
        details(view_w, bar + (height - bar) * split, panel, (height - bar) * (1 - split), dt * 1000.0);
        rood_library::window(); /* the Library when popped out */
        pc_gl_inset[0] = 0;
        pc_gl_inset[1] = (int)(bar + tools);
        pc_gl_inset[2] = (int)panel;
        pc_gl_inset[3] = 0;
        if (pc_hw_active)
            pc_lights_viewport();
        viewport_keys();
        rood_colour::draw(); /* the colour popover (editor_colour.cpp) */
        rood_details::ladder_frame(); /* a value ladder being dragged (editor_details.cpp) */
        pc_lights_editor_windows();
        pc_room_browser_window();
        help_window();
        memory_cards_window();
        /* What the panels changed becomes a transaction (once released). */
        pc_undo_frame_end();
    } else {
        memset(pc_gl_inset, 0, sizeof pc_gl_inset);
    }
    pc_ui_draw_frame((float)dt); /* modern menus (pc/src/ui) over the game picture */
    if (!visible)
        pc_fast_travel_prompt_draw(); /* the Travel prompt on a Circle of Magic (not over the editor) */
    shell_text_input();
    ImGui::Render();
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return; /* vk_present.c records the draw data (pc_vk_overlay_render) */
#endif
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
