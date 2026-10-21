/* The engine library's side of a reload (build_boot.py --engine-library;
 * pc/docs/plans/2026-10-19-engine-reload.md). The host's loader
 * (engine_host.c) calls rood_engine_unload in the library going away and
 * rood_engine_resume in the one that follows; between them only the host's
 * carry store (pc_engine_carry_put/take) holds what crosses.
 *
 * What crosses: every setting as currently set (saved or not), the RPC's
 * socket, clients and pause, the interactive fog's fields, the window and
 * what the overlay showed. The GPU (device, context, every resource), Dear
 * ImGui, the gamepads and the Lua plugins are shut down here and started
 * again by the next library, as at a resumed save state: it rebuilds what
 * it derives from the game (pc_hw_resync: the hardware target from native
 * VRAM). */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h" /* pc_capture_file */
#include "vk_hooks.h" /* pc_vk_on */

/* engine_host.c */
extern int pc_engine_carry_put(const char* name, const void* data, size_t size);
extern void* pc_engine_carry_take(const char* name, size_t* size);
/* the engine's own */
extern SDL_Window* pc_window_unload(int* enabled, int* run_fast);
extern void pc_window_adopt(SDL_Window* kept);
extern void pc_window_init(int enabled, int run_fast);
extern void pc_settings_write(FILE* f);
extern void pc_settings_apply(const char* text); /* project_settings.cpp, as the editor's undo */
extern void pc_overlay_shutdown(int* was_visible, int* editor);
extern void pc_overlay_restore(int was_visible, int editor);
extern void pc_gl_shutdown(void);
extern void pc_vk_shutdown(void);
extern void vk_nrd_shutdown(void);
extern void pc_controller_shutdown(void);
extern void pc_rpc_unload(void);
extern void pc_rpc_resume(void);
extern int pc_lights_busy(void);
extern int pc_fixture_regen_busy(void);
extern void pc_atmosphere_settle(void);
extern int pc_ui_shell_drawn(void);
extern int pc_ui_loot_visible(void);
extern int pc_ui_map_visible(void);
extern int pc_ui_data_visible(void);
extern int pc_fog_state_size(void);
extern void pc_fog_state_save(void* out);
extern int pc_fog_state_load(const void* in, int size);
extern void pc_lua_init(void);
extern void pc_hw_resync(void);
extern int pc_photo_engine_unload(char* why, int size);

struct window_carry {
    SDL_Window* window;
    int enabled, fast, overlay_visible, editor;
};

/* The interface between engine_host.c and this file: the host refuses a
 * library with another. */
int rood_engine_abi(void) { return 1; }

/* A word a build of the library can carry (-DROOD_ENGINE_NOTE="..."; the
 * reload smoke test builds one): RPC reload's reply shows it. */
#ifndef ROOD_ENGINE_NOTE
#define ROOD_ENGINE_NOTE ""
#endif
const char* rood_engine_note(void) { return ROOD_ENGINE_NOTE; }

/* Before the host unloads this library: 0 when it may (everything that
 * crosses is in the carry store and the GPU is torn down), else -1 with
 * why, and nothing was changed. */
int rood_engine_unload(char* why, int size)
{
    /* Library code running on another thread: refused, or waited for. */
    if (pc_lights_busy()) {
        snprintf(why, (size_t)size, "retry: the light suggestion is still running");
        return -1;
    }
    if (pc_fixture_regen_busy()) {
        snprintf(why, (size_t)size, "retry: the fixture generator is still running");
        return -1;
    }
    /* The modern menus' state is the engine's (the screen, focus, model
     * view) while the game's bridges think them open: a new library would
     * start with them closed and the game would follow. Not carried: the
     * reload waits until they close (dev_loop.sh retries). */
    const char* open = pc_ui_shell_drawn() ? "the menu" : pc_ui_loot_visible() ? "the loot screen"
        : pc_ui_map_visible() ? "the map" : pc_ui_data_visible() ? "the save screen" : NULL;
    if (open) {
        snprintf(why, (size_t)size, "retry: %s is open (the reload goes through once it closes)", open);
        return -1;
    }
    /* The photo mode (photo.cpp): refused while a render runs the game from its frozen moment; else every
     * plugin's pause, camera, hidden layers and input capture are given back here (the state is the
     * moment's: renders rewind), and the plugins, started again by the next library, ask again. */
    if (pc_photo_engine_unload(why, size))
        return -1;
    pc_atmosphere_settle();
    size_t n = 0;
    char* text = pc_capture_file(pc_settings_write, &n);
    if (text)
        pc_engine_carry_put("settings", text, n + 1);
    free(text);
    int fog_size = pc_fog_state_size();
    void* fog = malloc((size_t)fog_size + 1);
    if (fog) {
        pc_fog_state_save(fog);
        pc_engine_carry_put("fog", fog, (size_t)fog_size);
    }
    free(fog);
    pc_rpc_unload();
    struct window_carry w = { 0 };
    pc_overlay_shutdown(&w.overlay_visible, &w.editor); /* while the renderer it draws with is up */
    vk_nrd_shutdown();
    if (pc_vk_on())
        pc_vk_shutdown();
    else
        pc_gl_shutdown();
    pc_controller_shutdown();
    w.window = pc_window_unload(&w.enabled, &w.fast);
    pc_engine_carry_put("window", &w, sizeof w);
    return 0;
}

/* In the library loaded next: start as main does (the window, the
 * renderer, the overlay and the settings file; Lua), then what was carried. */
void rood_engine_resume(void)
{
    size_t n = 0;
    struct window_carry* w = pc_engine_carry_take("window", &n);
    if (w && n != sizeof *w) {
        free(w);
        w = NULL;
    }
    if (w && w->window)
        pc_window_adopt(w->window);
    pc_window_init(w ? w->enabled : 0, w ? w->fast : 0);
    char* text = pc_engine_carry_take("settings", &n);
    if (text)
        pc_settings_apply(text);
    free(text);
    pc_lua_init();
    pc_rpc_resume();
    void* fog = pc_engine_carry_take("fog", &n);
    if (fog && !pc_fog_state_load(fog, (int)n))
        fprintf(stderr, "[engine] the interactive fog's state does not fit: it starts at rest\n");
    free(fog);
    if (w)
        pc_overlay_restore(w->overlay_visible, w->editor);
    free(w);
    pc_hw_resync(); /* the hardware VRAM target from native VRAM, as after a resumed save state */
}
