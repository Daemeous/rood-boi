/* Modern menus, System tab: the settings pages (pc/src/ui/ui_settings.cpp,
 * every build; pc/docs/Modern_Menus.md). A pure model the shell drives and
 * ui_draw draws: pages of rows, each row a choice, toggle, slider, action,
 * binding or read-only info, with its value as text, whether it can change
 * now and why not. Changing a row applies at once through the same calls the
 * F1 editor makes (pc_hw_set_scale, pc_lighting_set, the AA and UI
 * variables, pc_binding_setting / pc_bindings_reset, ...), then saves
 * settings.cfg (pc_settings_save) once nothing has changed for a second
 * (pc_ui_settings_tick) or when the page is left (pc_ui_settings_flush).
 *
 * The game's own options (map, ability timing, sound, ...) come from
 * game_options.c (battle builds; weak here): they change vs_main_settings
 * as the game's Options screen does, are saved with the game (memory card)
 * and never mark settings.cfg dirty. Without the bridge their rows are left
 * out.
 *
 * Rows that later work adds (window mode, master volume, brightness, binding
 * capture) are present now and greyed until their hooks exist (weak, see
 * "Hooks" below).
 *
 * Indices: pages 0..pc_ui_settings_pages()-1; rows 0..pc_ui_settings_rows(page)-1
 * count only the rows shown now (some show only while another setting is on,
 * e.g. Images per vsync with frame interpolation), so the row count may
 * change after a change: the shell clamps its focus. Row ids are stable. */
#ifndef PC_UI_SETTINGS_H
#define PC_UI_SETTINGS_H
#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC_UI_SETTINGS_GRAPHICS, PC_UI_SETTINGS_DISPLAY, PC_UI_SETTINGS_AUDIO, PC_UI_SETTINGS_INTERFACE,
    PC_UI_SETTINGS_CONTROLS, PC_UI_SETTINGS_BINDINGS, PC_UI_SETTINGS_GAMEPLAY, PC_UI_SETTINGS_PAGES
};
/* Row kinds. Choice: one of `count` named values (Left/Right step through the
 * available ones and wrap). Toggle: Off/On (any step flips). Slider: a number
 * from min to max in steps (clamped). Action: Circle runs it. Binding: a PS1
 * button's keys and gamepad element (Circle starts a capture). Info: text. */
enum {
    PC_UI_SET_CHOICE, PC_UI_SET_TOGGLE, PC_UI_SET_SLIDER, PC_UI_SET_ACTION, PC_UI_SET_BINDING, PC_UI_SET_INFO
};
typedef struct {
    const char* id;    /* stable ("widescreen", "bind_circle"); static */
    const char* label; /* static */
    const char* help;  /* one or two sentences; static */
    int kind;          /* PC_UI_SET_* */
    char value[96];    /* the value as shown ("4x", "On", "Z, Return / East") */
    int enabled;       /* 0: greyed (reason says why) */
    /* Greyed: why. Enabled with a reason: the current choice cannot run here
     * and something else is used (a lighting choice this GPU lacks). */
    char reason[192];
    int index, count;  /* choice / toggle: current choice (-1: none matches) and the number of choices */
    float min, max, step, number; /* slider */
    int button;        /* binding: PS1 button (libetc bit index, PC_PROMPT_*); else -1 */
    char keys[48];     /* binding: the keys ("Z, Return"; "None") */
    char pad[32];      /* binding: the gamepad element ("East"; "None") */
    int game_option;   /* 1: the game's option, saved with the game, not in settings.cfg */
    int swatch;        /* choice: a colour chip shown with the value (Menu tint); 0: none */
    unsigned char swatch_rgb[3]; /* its colour, 0..255 */
} pc_ui_setting_row;

int pc_ui_settings_pages(void);
const char* pc_ui_settings_page_title(int page); /* "Graphics" */
const char* pc_ui_settings_page_id(int page);    /* "graphics" */
int pc_ui_settings_rows(int page);
/* Fills *out; 0 when there is no such row. */
int pc_ui_settings_row(int page, int row, pc_ui_setting_row* out);
/* A row by its id (plugins: rood.settings, pc/src/lua/lua_api_settings.cpp): 1 and
 * its page and its index among the rows shown now; 2 when it exists but is not
 * shown now (it depends on another setting); 0 when there is none. */
int pc_ui_settings_find(const char* id, int* page, int* row);
/* A choice's name and why it cannot be picked (NULL: it can); 0: no such choice. */
int pc_ui_settings_choice(int page, int row, int choice, const char** name, const char** reason);
/* Left/Right: delta -1 / +1 (sliders: steps; choices: the next available
 * choice, wrapping; toggles flip). Returns 1 when the value changed. Greyed
 * rows never change. */
int pc_ui_settings_change(int page, int row, int delta);
/* Mouse: set a choice index or a slider value (clamped to its step). Returns 1 when changed. */
int pc_ui_settings_set(int page, int row, double value);
/* A pointer drag on a slider: as set, except on sliders whose change is
 * costly (Internal resolution: the render targets are rebuilt), where the
 * row shows the number and nothing applies until pc_ui_settings_set (the
 * drag's end) or a flush. Returns 1 when the shown value changed. */
int pc_ui_settings_preview(int page, int row, double value);
/* Circle: run an action, or start a binding capture. Returns 1 when something happened. */
int pc_ui_settings_activate(int page, int row);

/* Saving. tick: every frame with a clock in seconds; saves once 1 s passed
 * without a change. flush: save now if anything is unsaved (leaving a page,
 * closing the menu). dirty: something is unsaved. */
void pc_ui_settings_tick(double now);
void pc_ui_settings_flush(void);
int pc_ui_settings_dirty(void);
/* How many saves these settings made so far (tests: the debounced save happened). */
int pc_ui_settings_saves(void);

/* The page as JSON (RPC / tests): {"page":"display","title":...,"rows":[{"id",
 * "label","kind","value","enabled","reason",...}]}. Returns the length. */
int pc_ui_settings_json(int page, char* out, int size);

/* ---- Bindings ----
 * A captured input for PS1 button `button`: kind PC_UI_CAPTURE_KEY with an
 * SDL keycode, or PC_UI_CAPTURE_PAD with a gamepad element (SDL button or
 * PC_PAD_AXIS(trigger)). A key replaces the button's first key (its second
 * stays; its own second key swaps the two). A key another button had is
 * swapped: that button gets this one's former first key in its place (or
 * none). A gamepad element another button had: that button gets this one's
 * former element. No button ends up with the same key twice.
 * Written through pc_binding_setting, so only named keys bind. Returns the
 * other button that was swapped (>= 0), -1 when none, -2 when refused
 * (unnamed or reserved key, bad button). Marks the settings dirty. */
enum { PC_UI_CAPTURE_KEY = 0, PC_UI_CAPTURE_PAD = 1 };
int pc_ui_settings_bind(int button, int kind, int code);
/* The capture the Bindings page started (Circle on a binding row): its PS1
 * button, or -1 when none runs. capture_done delivers the input (kind -1:
 * cancelled) and ends it; returns as pc_ui_settings_bind (-1 when cancelled). */
int pc_ui_settings_capture_button(void);
int pc_ui_settings_capture_done(int kind, int code);
/* Keys a binding may not take, and why (NULL: it may): Esc, F1 and F2 (the
 * editor), Alt and Super (the desktop), J L I K O while the camera keys are
 * on. pc_ui_settings_bind refuses them (-2). A capture that gets one goes on
 * listening: capture_refuse (the window code, before capture_done) returns 1
 * for such a key and keeps its reason for the prompt (capture_note: the
 * reason while that capture runs, else NULL). */
const char* pc_ui_settings_key_reserved(int key);
int pc_ui_settings_capture_refuse(int kind, int code);
const char* pc_ui_settings_capture_note(void);
/* After a swap, for the status line: "M moved from Circle to Triangle; Circle
 * gets S", for 3 s from the next tick (NULL otherwise). */
const char* pc_ui_settings_note(void);

/* ---- Hooks (weak; provided by the features that are not in yet) ----
 * Window mode (boot_window.c):  int pc_window_mode_get(void) (0 windowed,
 *     1 borderless, 2 fullscreen); void pc_window_mode_set(int mode).
 * Master volume (audio_output.c): int pc_master_volume_get(void) (0..100);
 *     void pc_master_volume_set(int volume).
 * Brightness (present pass):     int pc_brightness_get(void) (0..100, 50 =
 *     unchanged); void pc_brightness_set(int value).
 * Binding capture (boot_window.c): int pc_ui_capture_begin(void) starts
 *     listening for the next key or gamepad button (1: started); the window
 *     code then calls pc_ui_settings_capture_done with what it got.
 * Each row is greyed ("Coming in this phase") until both its functions exist. The
 * setters must apply live and their values must be written by
 * pc_settings_write (project_settings.cpp) to be saved. */

#ifdef __cplusplus
}
#endif
#endif
