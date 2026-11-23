/* SDL3 frontend for the native boot: the window (Vulkan in vk_present.c, or
 * OpenGL in gl_present.c), keyboard and gamepad input and frame pacing. SDL
 * picks the display backend: native Wayland where the compositor supports
 * it, else X11 (SDL_VIDEO_DRIVER=wayland or x11 chooses explicitly). */
#define _POSIX_C_SOURCE 200809L
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "boot_controller.h"
#include "frame_cost.h"
#include "present_service.h"
#include "present_header.h"
#include "render_thread.h"
#include "hw_cmd.h"
#include "render_jitter.h"
extern void pc_jitter(int) __attribute__((weak)); /* render_jitter.c (the R4 harness) */
#include "camera_stick.h"
#include "button_prompts.h"
#include "ui/ui_settings.h" /* binding capture: PC_UI_CAPTURE_* */
#ifdef ROOD_VULKAN
#include "vk_hooks.h" /* ROOD_RENDERER=vulkan: vk_present.c */
#include "shader_reload.h"
#endif
static int focused = 1;
static SDL_Window* window;
static unsigned buttons;
static int travel_key; /* a key bound to the Travel action held (fast travel) */
static unsigned camera_keys; /* J L I K O held (camera_stick.c keyboard fallback) */
static int fast;
/* Fast-forward while held: ` (backtick) or L3 + R3 (boot_gamepad.c). The game
 * runs unpaced, as with --fast; audio drops what it cannot play. */
static int fast_forward_key;
static int fast_forward_held(void) { return focused && (fast_forward_key || pc_controller_fast_forward()); }
/* Battle charging skip (charge_skip.c): the vsyncs of a skipped wind-up
 * that still run (its effect's loading) are not presented and not paced.
 * The sound is not tied to them: the audio thread keeps its own clock. */
extern int pc_charge_skip_hidden(void) __attribute__((weak));
extern int pc_take_pace(void) __attribute__((weak)); /* cutscene.cpp */
static int charge_hidden; /* this vsync's */
static void charge_update(void)
{
    charge_hidden = pc_charge_skip_hidden && pc_charge_skip_hidden();
}
extern int pc_gl_active;
extern SDL_Window* pc_gl_create_window(const char*, int, int);
extern void pc_gl_resize(int, int);
extern void pc_gl_present(const uint16_t*, int, int, int, int);
extern int pc_overlay_event(const SDL_Event*);
extern void pc_ui_settings_flush(void) __attribute__((weak)); /* ui_settings.cpp */
extern void pc_quit(void); /* dev_rpc.c */
extern void pc_savestate_actions(unsigned bits) __attribute__((weak)); /* savestate_slots.c */
int pc_window_enabled;
static void window_mode_apply(void); /* window_mode, below */
void pc_window_place(SDL_PropertiesID props, int width, int height); /* below */
static void window_moved(void);                                      /* remember_window, below */
static int capture_filter(const SDL_Event* event); /* binding capture, below */
static void capture_tick(void);
static unsigned keybit(SDL_Keycode key);
static unsigned camera_keybit(SDL_Keycode key);
#ifdef ROOD_VULKAN
#define PC_VK_ACTIVE pc_vk_on()
#else
#define PC_VK_ACTIVE 0
#endif
/* The NTSC field (vsync) the frame loop paces to: its deadline. */
static struct timespec deadline;
#define FIELD_NS 16683350LL
/* Extra presents (pc_present_rate): wait until k/n of the way from this vsync's deadline to the next (no wait in fast mode: the request says
 * whether this vsync is paced). */
static void wait_fraction(const pc_present_request* r, int k, int n)
{
    if (!r->pace.paced || !r->pace.deadline_ns || n <= 0)
        return;
    long long at_ns = r->pace.deadline_ns + FIELD_NS * k / n;
    struct timespec at = { (time_t)(at_ns / 1000000000LL), (long)(at_ns % 1000000000LL) };
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > at.tv_sec || (now.tv_sec == at.tv_sec && now.tv_nsec >= at.tv_nsec))
        return; /* late already: present now */
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &at, NULL);
}
/* Fast mode (no vsync pacing; tests): the GI cadence budget (ptgi.c). */
int pc_window_fast(void) { return fast; }
/* The window an engine reload keeps (engine_reload.c): the next library's
 * presenter takes it instead of creating one (pc_window_create). */
static SDL_Window* adopted;
SDL_Window* pc_window_create(SDL_PropertiesID props)
{
    if (adopted) {
        SDL_Window* kept = adopted;
        adopted = NULL;
        return kept;
    }
    return SDL_CreateWindowWithProperties(props);
}
/* Before an engine reload: the window (left open), and the run's options. */
SDL_Window* pc_window_unload(int* enabled, int* run_fast)
{
    *enabled = pc_window_enabled;
    *run_fast = fast;
    SDL_Window* kept = window;
    if (kept) {
        SDL_SetWindowRelativeMouseMode(kept, false); /* the free camera's grab is the old engine's */
        if (SDL_TextInputActive(kept))
            SDL_StopTextInput(kept);
    }
    window = NULL;
    /* SDL's video stays initialised (quitting it would close the window): the
     * next library's SDL_Init only counts it once more. */
    return kept;
}
/* After a reload: pc_window_init takes `kept` as the window. */
void pc_window_adopt(SDL_Window* kept) { adopted = kept; }
void pc_window_init(int enabled, int run_fast)
{
    fast = run_fast;
    pc_window_enabled = enabled;
    const char* backend = getenv("ROOD_RENDERER");
    if (backend && !strcmp(backend, "null")) {
        /* Logical pad presence is independent of a window: boot_sdk still
         * delivers RPC/recorded input. Native VRAM stays CPU-owned; never
         * initialize a video driver, hardware target or overlay here. */
        pc_window_enabled = 1;
        /* Settings normally load from the graphical overlay. The loader
         * itself is CPU-only (hardware scale changes guard pc_hw_active). */
        extern void pc_settings_load(void);
        extern int pc_gte_widescreen;
        const char* widescreen = getenv("ROOD_WIDESCREEN");
        if (widescreen)
            pc_gte_widescreen = *widescreen == '1';
        pc_settings_load();
        fprintf(stderr, "[window] null backend: CPU VRAM, RPC input, no video initialization, no pixels\n");
        return;
    }
    if (!enabled)
        return;
    SDL_SetAppMetadata("Vagrant Story (rood-boi)", NULL, "rood-boi");
    /* The game installs its own crash and stack-dump signal handlers, and
     * Ctrl+C stops it at once. */
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    /* Window and mouse coordinates are pixels on every backend (Wayland
     * too), as the presenter, overlay and editor assume. */
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, "1");
    /* Leave the X11 compositor on, like any other window. */
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Cannot open display: %s\n", SDL_GetError());
        exit(1);
    }
    pc_controller_init();
    /* Window size: ROOD_WINDOW=WIDTHxHEIGHT, else the largest 4:3 window
     * (in 240-line steps) within two thirds of the screen height, at least
     * 960 x 720: 1440 x 1080 on a 4K screen. */
    int width = 0, height = 0;
    const char* size = getenv("ROOD_WINDOW");
    if (!size || sscanf(size, "%dx%d", &width, &height) != 2 || width < 320 || height < 240) {
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        int screen_h = mode ? (int)(mode->h * (mode->pixel_density > 0 ? mode->pixel_density : 1)) : 1080;
        int lines = screen_h * 2 / 3 / 240;
        height = 240 * (lines < 3 ? 3 : lines);
        width = height * 4 / 3;
    }
    static const char title[] = "rood-boi | native title diagnostic | arrows, Enter, Z; F1 overlay, F2 editor";
    const char* renderer = getenv("ROOD_RENDERER");
#ifdef ROOD_VULKAN
    /* Vulkan presentation (vk_present.c) by default; OpenGL when asked for
     * (ROOD_RENDERER=gl) or when Vulkan is unavailable. */
    if (!renderer || !*renderer || !strcmp(renderer, "vulkan")) {
        window = pc_vk_create_window(title, width, height);
        if (!window)
            fprintf(stderr, "[window] Vulkan unavailable; using OpenGL\n");
    }
    if (!window)
#endif
    /* OpenGL presentation (gl_present.c). */
    window = pc_gl_create_window(title, width, height);
    /* Some drivers have OpenGL only through GLX: use X11 (XWayland) on Wayland when it is there. */
    if (!window && !strcmp(SDL_GetCurrentVideoDriver(), "wayland") && getenv("DISPLAY")) {
        fprintf(stderr, "[window] no OpenGL on Wayland; trying X11 (XWayland)\n");
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11", SDL_HINT_OVERRIDE);
        if (SDL_InitSubSystem(SDL_INIT_VIDEO))
            window = pc_gl_create_window(title, width, height);
        if (!window) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "wayland", SDL_HINT_OVERRIDE);
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
                fprintf(stderr, "Cannot open display: %s\n", SDL_GetError());
                exit(1);
            }
        }
    }
    if (!window) {
        /* (There is no software presenter: the renderer draws every pixel, T5-6.) */
        fprintf(stderr, "Cannot create a Vulkan or OpenGL window: %s\n", SDL_GetError());
        exit(1);
    }
    int version = SDL_GetVersion();
    fprintf(stderr, "[window] SDL %d.%d.%d, %s video\n", SDL_VERSIONNUM_MAJOR(version),
        SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), SDL_GetCurrentVideoDriver());
    window_mode_apply(); /* a saved window_mode (settings, loaded while the presenter started) */
    window_moved();      /* where it opened: the place to remember from the start */
    /* Keys held through an engine reload stay held. */
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    for (int i = 0; keys && i < count; i++)
        if (keys[i]) {
            SDL_Keycode key = SDL_GetKeyFromScancode((SDL_Scancode)i, SDL_KMOD_NONE, false);
            buttons |= keybit(key);
            camera_keys |= camera_keybit(key);
        }
}
/* The PS1 buttons a key presses (the bindings, boot_controller.c; defaults
 * in README "Controls"). */
static unsigned keybit(SDL_Keycode key) { return pc_keyboard_bits(key); }
/* The right-stick camera's keys: J/L turn, I/K zoom, O is R3 (reset). */
static unsigned camera_keybit(SDL_Keycode key)
{
    switch (key) {
    case SDLK_J:
        return 1;
    case SDLK_L:
        return 2;
    case SDLK_I:
        return 4;
    case SDLK_K:
        return 8;
    case SDLK_O:
        return 16;
    default:
        return 0;
    }
}
/* The editor opened (dev_overlay.cpp): keys held for the game now belong to
 * the editor, which takes their releases, so let go of them here. */
void pc_window_release_keys(void) { buttons = camera_keys = 0, travel_key = 0; }
extern unsigned pc_rpc_buttons(void);
/* input_record.c: ROOD_RECORD writes, ROOD_REPLAY replaces, the live input. */
extern unsigned pc_input_buttons(unsigned live, unsigned rpc);
extern int pc_input_stick(float out[2], int live_valid);
/* button_roles.c (battle builds): Circle and Cross exchanged where the
 * options qol_confirm_cross / qol_field_cross_attack cover the game's context. */
extern unsigned pc_button_roles_buttons(unsigned buttons) __attribute__((weak));
/* photo.cpp: a plugin captures the input (rood.input.capture), or a photo mode render runs (the game
 * runs from the frozen moment): the game sees no buttons and no stick. */
extern int pc_photo_input_captured __attribute__((weak));
extern int pc_photo_rendering __attribute__((weak));
extern int pc_photo_input_event(const void* event) __attribute__((weak));
extern int pc_photo_input_escape(const void* event) __attribute__((weak));
extern unsigned pc_photo_input_swallowed __attribute__((weak));
extern void pc_photo_input_tick(void) __attribute__((weak));
/* photo.cpp: the pad and the camera stick the game read while the hold's lead ran, logged and given back to a render. */
extern unsigned pc_vsync_count(void);
extern void pc_photo_history_note_buttons(unsigned vsync, unsigned pad) __attribute__((weak));
extern int pc_photo_history_take_buttons(unsigned vsync, unsigned* pad) __attribute__((weak));
extern void pc_photo_history_note_stick(int which, unsigned vsync, const float v[2], int valid) __attribute__((weak));
extern int pc_photo_history_take_stick(int which, unsigned vsync, float v[2], int* valid) __attribute__((weak));
extern void pc_photo_action(const char* name, const char* device) __attribute__((weak));
static int photo_blind(void)
{
    return (&pc_photo_input_captured && pc_photo_input_captured) || (&pc_photo_rendering && pc_photo_rendering);
}
/* Raw host camera controls must respect binding capture, including the element held
 * after capture completed (not merely whether the settings prompt is visible). */
int pc_window_pad_captured(void) { return capture_filter(NULL); }
static unsigned window_buttons(void);
unsigned pc_window_buttons(void)
{
    unsigned pad = 0;
    if (&pc_photo_rendering && pc_photo_rendering) /* a render: what the game read at this vsync live, or nothing */
        return pc_photo_history_take_buttons && pc_photo_history_take_buttons(pc_vsync_count(), &pad) ? pad : 0;
    pad = window_buttons();
    if (pc_photo_history_note_buttons)
        pc_photo_history_note_buttons(pc_vsync_count(), pad);
    return pad;
}
static unsigned window_buttons(void)
{
    if (photo_blind()) {
        pc_input_buttons(0, 0); /* (a recording's vsync still counted) */
        return 0;
    }
    /* Developer RPC input (dev_rpc.c) applies even while unfocused. */
    unsigned keys = buttons | (pc_camera_stick_keys && (camera_keys & 16) ? 1u << 10 : 0);
    unsigned pad = capture_filter(NULL) ? 0 : pc_controller_buttons(); /* a binding capture takes the pad */
    unsigned result = pc_input_buttons(focused ? keys | pad : 0, pc_rpc_buttons());
    if (&pc_photo_input_swallowed)
        result &= ~pc_photo_input_swallowed; /* the photo mode's safety took it (photo.cpp) */
    if (pc_button_roles_buttons)
        result = pc_button_roles_buttons(result);
    /* R3 resets the right-stick camera instead (camera_stick.c). */
    return pc_camera_stick_buttons(result);
}
/* The camera's stick: the gamepad's right stick and the keys (focused). */
static int window_stick(float out[2]);
int pc_window_stick(float out[2])
{
    int valid = 0;
    if (&pc_photo_rendering && pc_photo_rendering && pc_photo_history_take_stick
        && pc_photo_history_take_stick(0, pc_vsync_count(), out, &valid))
        return valid;
    valid = window_stick(out);
    if (pc_photo_history_note_stick)
        pc_photo_history_note_stick(0, pc_vsync_count(), out, valid);
    return valid;
}
static int window_stick(float out[2])
{
    int valid = focused && !photo_blind();
    out[0] = out[1] = 0;
    if (valid) {
        pc_controller_right_stick(out);
        if (pc_camera_stick_keys) {
            out[0] += (camera_keys & 2 ? 1.0f : 0) - (camera_keys & 1 ? 1.0f : 0);
            out[1] += (camera_keys & 8 ? 1.0f : 0) - (camera_keys & 4 ? 1.0f : 0);
        }
    }
    return pc_input_stick(out, valid);
}
static long long field_start_ns; /* monotonic: when this game field's tick finished its pacing (the frame is presented from here) */
static long long now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}
/* ROOD_TEST_STALL=ms:every (tests, benchmarks): the game thread sleeps ms every that many ticks, as a long stretch of game logic would
 * (what the renderer thread must absorb: present_stall_bench.py). */
static void test_stall(void)
{
    static int ms = -1, every, ticks;
    if (ms < 0) {
        const char* v = getenv("ROOD_TEST_STALL");
        ms = v && sscanf(v, "%d:%d", &ms, &every) == 2 && ms > 0 && every > 0 ? ms : 0;
    }
    if (ms && ++ticks % every == 0) {
        struct timespec at = { ms / 1000, (long)(ms % 1000) * 1000000L };
        nanosleep(&at, NULL);
    }
}
void pc_window_tick(void)
{
    long long paced_start = 0;
    if (!window)
        return;
    test_stall();
    window_mode_apply(); /* a switch asked for since the last tick (pc_window_mode_set) */
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        /* A binding capture (System > Bindings) takes keys and pad buttons first. */
        if (capture_filter(&event))
            continue;
        /* The photo mode's safety (photo.cpp): Escape gives a plugin's input capture or pause back. */
        if (pc_photo_input_escape && pc_photo_input_escape(&event))
            continue;
        /* The developer overlay (F1) takes the events it uses. */
        if ((pc_gl_active || PC_VK_ACTIVE) && pc_overlay_event(&event))
            continue;
        /* The photo mode's key (bind_key_photo): a Lua event, captured or not (photo_input.cpp). */
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && pc_photo_action && pc_keyboard_photo(event.key.key))
            pc_photo_action("photo", "keyboard");
        /* A plugin's input capture takes the keys (photo.cpp; Escape gives them back). */
        if (pc_photo_input_event && pc_photo_input_event(&event))
            continue;
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            /* A settings change still waiting for its debounced save (System) is saved first. */
            if (pc_ui_settings_flush)
                pc_ui_settings_flush();
            fprintf(stderr, "[exit] %s: the window asked to quit\n",
                event.type == SDL_EVENT_QUIT ? "SDL_EVENT_QUIT" : "window close requested");
            pc_quit(); /* dev_rpc.c: the auto-snapshot first when ROOD_AUTOSNAP is set */
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            buttons = camera_keys = 0;
            travel_key = 0;
            focused = 0;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            focused = 1;
            break;
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED:
            window_moved();
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (pc_gl_active)
                pc_gl_resize(event.window.data1, event.window.data2);
#ifdef ROOD_VULKAN
            if (pc_vk_on())
                pc_vk_resize(event.window.data1, event.window.data2);
#endif
            break;
        case SDL_EVENT_KEY_DOWN:
            /* The save state actions bound to it (savestate_slots.c; it may share a PS1 button). */
            if (pc_savestate_actions && !event.key.repeat)
                pc_savestate_actions(pc_keyboard_state_actions(event.key.key));
            buttons |= keybit(event.key.key);
            camera_keys |= camera_keybit(event.key.key);
            if (pc_keyboard_travel(event.key.key))
                travel_key = 1;
            /* The keyboard is the last device used (button prompts). */
            if (keybit(event.key.key) || camera_keybit(event.key.key) || pc_keyboard_travel(event.key.key))
                pc_button_prompts_device(PC_PROMPTS_KEYBOARD, 0);
            /* Unless ` is bound to a PS1 button (bind_key_*). */
            if (event.key.key == SDLK_GRAVE && !keybit(event.key.key))
                fast_forward_key = 1;
            break;
        case SDL_EVENT_KEY_UP:
            buttons &= ~keybit(event.key.key);
            camera_keys &= ~camera_keybit(event.key.key);
            if (pc_keyboard_travel(event.key.key))
                travel_key = 0;
            if (event.key.key == SDLK_GRAVE)
                fast_forward_key = 0;
            break;
        default:
            pc_controller_event(&event);
            break;
        }
    }
    pc_controller_poll();
    if (pc_photo_input_tick)
        pc_photo_input_tick(); /* a plugin's input capture: the pad's and the RPC's buttons as events */
    capture_tick();
    charge_update();
    /* The cutscene player (cutscene.cpp): 0 while it goes to a frame (unpaced), 2 or 4 in slow motion. */
    int pace = pc_take_pace ? pc_take_pace() : 1;
    if (&pc_photo_rendering && pc_photo_rendering) {
        /* A photo mode render's vsyncs run unpaced, the deadline kept: the render's time counts toward
         * the paused redraw's period like a frame's own work. Restarting the pacing from after each
         * render added a whole field to every camera step (about 37 pictures a second). */
    } else if (fast_forward_held() || charge_hidden || pace <= 0)
        deadline.tv_sec = 0; /* pace again from now when it is released (or presents again) */
    else if (!fast || pace > 1) {
        /* Pace to absolute NTSC field deadlines (59.94 Hz) so the frame's own
         * work counts toward the period; resynchronise after long stalls. */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (!deadline.tv_sec)
            deadline = now;
        deadline.tv_nsec += FIELD_NS * (long)(pace > 1 ? pace : 1);
        while (deadline.tv_nsec >= 1000000000) {
            deadline.tv_nsec -= 1000000000;
            ++deadline.tv_sec;
        }
        long long behind = (now.tv_sec - deadline.tv_sec) * 1000000000LL
                         + (now.tv_nsec - deadline.tv_nsec);
        /* ROOD_PERF=1: report frame work time (wake to next tick) per 300 ticks. */
        static int perf = -1, count, late;
        static long long work_sum, work_max;
        static struct timespec woke;
        if (perf < 0)
            perf = getenv("ROOD_PERF") != NULL;
        if (perf && woke.tv_sec) {
            long long work = (now.tv_sec - woke.tv_sec) * 1000000000LL + (now.tv_nsec - woke.tv_nsec);
            work_sum += work;
            work_max = work > work_max ? work : work_max;
            late += work > 16683350;
            if (++count == 300) {
                fprintf(stderr, "[perf] frame work avg %.2f ms, max %.2f ms, %d/300 over budget\n",
                    work_sum / 300 / 1e6, work_max / 1e6, late);
                count = late = 0;
                work_sum = work_max = 0;
            }
        }
        if (behind > 100000000LL)
            deadline = now;
        else
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
        clock_gettime(CLOCK_MONOTONIC, &woke);
        paced_start = deadline.tv_sec * 1000000000LL + deadline.tv_nsec; /* the field's ideal start, not when the sleep returned */
    }
    field_start_ns = paced_start ? paced_start : now_ns(); /* this field began */
}
/* The window's size in pixels (the game thread's to ask SDL: the presenter's copy is in the frame header). */
void pc_window_pixel_size(int* w, int* h)
{
    *w = *h = 0;
    if (window)
        SDL_GetWindowSizeInPixels(window, w, h);
}
extern int pc_display_rgb24;
/* One image of the frame the header (filled at the request: header_for_frame) describes. */
static void window_image(const uint16_t* vram, int x, int y, int w, int h)
{
    if (!window)
        return;
    if (pc_gl_active) {
        pc_gl_present(vram, x, y, w, h);
        return;
    }
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_present_enter();
        pc_vk_present(vram, x, y, w, h);
        pc_present_leave();
        pc_material_graph_poll(); /* the node graphs' generated include, when they changed */
        pc_shader_reload_poll(); /* between frames: shader hot reload */
        return;
    }
#endif
}
/* The game-owned values the presenter reads (present_header.h), copied once per frame request on the game thread: the images of the
 * frame (its interpolated fractions, the display-paced fields) all read this copy. */
static int header_for_frame(void)
{
#ifdef ROOD_VULKAN
    if (window && !pc_gl_active && pc_vk_on())
        return pc_present_header_fill();
#endif
    return -1;
}
/* A frame drawn outside a request (the paused developer view's redraw): its own header. */
struct frame_args {
    const uint16_t* vram;
    int x, y, w, h, header;
};
static void frame_image(void* arg)
{
    struct frame_args* a = arg;
    pc_present_header_use(a->header);
    window_image(a->vram, a->x, a->y, a->w, a->h);
    pc_present_header_release(a->header);
}
void pc_window_frame(const uint16_t* vram, int x, int y, int w, int h)
{
    struct frame_args args = { vram, x, y, w, h, header_for_frame() };
    pc_rt_call(frame_image, &args);
}

/* ---- Window mode (settings window_mode windowed | borderless | fullscreen;
 * System > Display) ----
 * Borderless: fullscreen at the desktop's mode (SDL_SetWindowFullscreenMode
 * NULL). Fullscreen: exclusive, the display mode closest to the desktop's
 * resolution and refresh rate. On Wayland exclusive modes are emulated (the
 * compositor scales): it looks the same as borderless; on X11 it depends on
 * the window manager. Windowed: the size the window had before it went
 * fullscreen. Requested before the window exists (the settings load while
 * the presenter starts), the mode applies when it does. Otherwise a switch
 * applies at the next frame boundary (pc_window_tick, before the events are
 * read), never in the middle of a frame (the System menu sets it from the
 * game's frame). Size changes reach the presenters as
 * SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED (above); the Vulkan presenter
 * recreates its swapchain at the start of the frame after (vk_present.c
 * sync_swapchain). */
static const char* const window_mode_names[3] = { "windowed", "borderless", "fullscreen" };
static int window_mode, window_mode_applied, window_mode_failed; /* failed: the mode that did not apply + 1 */
static int windowed_w, windowed_h;
int pc_window_mode_get(void) { return window_mode; }
void pc_window_mode_set(int mode)
{
    window_mode = mode < 0 ? 0 : mode > 2 ? 2 : mode; /* applied by the next pc_window_tick */
}
static SDL_DisplayID display_target(void);                      /* settings display, below */
static int exclusive_mode(SDL_DisplayID display, SDL_DisplayMode* out); /* settings fullscreen_mode, below */
static int place_pending; /* the display or the exclusive mode changed: apply again at the next tick */
static void window_mode_apply(void)
{
    if (!window || (window_mode == window_mode_applied && !place_pending))
        return;
    place_pending = 0;
    if (window_mode_applied == 0 && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN))
        SDL_GetWindowSize(window, &windowed_w, &windowed_h);
    SDL_DisplayID display = display_target();
    int ok;
    if (window_mode == 0) {
        ok = SDL_SetWindowFullscreen(window, false);
        if (ok && windowed_w > 0 && windowed_h > 0)
            SDL_SetWindowSize(window, windowed_w, windowed_h);
        /* on the display the settings name: centred there when the window is on another */
        if (ok && display && SDL_GetDisplayForWindow(window) != display)
            SDL_SetWindowPosition(window, (int)SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                (int)SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    } else {
        SDL_DisplayMode exclusive;
        const SDL_DisplayMode* mode = NULL;
        /* to the display first (a fullscreen window fills the display it is on), then the mode */
        if (display && SDL_GetDisplayForWindow(window) != display) {
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)
                SDL_SetWindowFullscreen(window, false);
            SDL_SetWindowPosition(window, (int)SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                (int)SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        }
        if (window_mode == 2 && exclusive_mode(display ? display : SDL_GetDisplayForWindow(window), &exclusive))
            mode = &exclusive;
        ok = SDL_SetWindowFullscreenMode(window, mode) && SDL_SetWindowFullscreen(window, true);
    }
    /* A failed switch keeps the mode the window has (what the settings
     * save); the RPC reports which mode failed. */
    window_mode_failed = ok ? 0 : window_mode + 1;
    if (!ok) {
        fprintf(stderr, "[window] cannot switch to %s: %s\n", window_mode_names[window_mode], SDL_GetError());
        window_mode = window_mode_applied;
        return;
    }
    window_mode_applied = window_mode;
}
/* RPC (system): the mode (the last that applied), the last failed switch and what the window reports. */
int pc_window_mode_json(char* out, int size)
{
    SDL_WindowFlags flags = window ? SDL_GetWindowFlags(window) : 0;
    const SDL_DisplayMode* exclusive = window ? SDL_GetWindowFullscreenMode(window) : NULL;
    int w = 0, h = 0;
    if (window)
        SDL_GetWindowSize(window, &w, &h);
    return snprintf(out, size,
        "{\"mode\":\"%s\",\"failed\":%s,\"failed_mode\":\"%s\",\"fullscreen\":%s,\"exclusive\":%s,\"size\":[%d,%d],"
        "\"windowed_size\":[%d,%d],\"video\":\"%s\"}",
        window_mode_names[window_mode], window_mode_failed ? "true" : "false",
        window_mode_failed ? window_mode_names[window_mode_failed - 1] : "",
        flags & SDL_WINDOW_FULLSCREEN ? "true" : "false", exclusive ? "true" : "false", w, h, windowed_w, windowed_h,
        window ? SDL_GetCurrentVideoDriver() : "");
}

/* ---- The display, the exclusive mode, vsync and the window's place (System
 * > Display; settings display, fullscreen_mode, vsync, remember_window,
 * window_rect) ----
 * display "<index> <name>": the screen the window (centred) and fullscreen go
 * on, matched by name, then by index, then the primary ("primary": always
 * the primary). fullscreen_mode "WxH@Hz" | desktop: exclusive fullscreen's
 * mode on that display (desktop: the display's own; a mode it lacks: the
 * closest it has). Both apply at the next frame boundary, like window_mode.
 * vsync 0|1: presents wait for the display's refresh (OpenGL swap interval,
 * Vulkan FIFO; off: the frame loop's own 59.94 Hz pacing only, the default).
 * remember_window 0|1 with window_rect x y w h: the window opens where and
 * as large as it was last left windowed (the presenters' create calls,
 * pc_window_place; only when a display still contains it). */
#define DISPLAYS 8
#define MODES 64
static char display_want[128]; /* "" or "primary": the primary display */
static char mode_want[48] = "desktop";
static int vsync_on, remember_on, rect_known, rect[4];
static struct {
    SDL_DisplayID id;
    char label[96], value[128];
} displays[DISPLAYS];
static int display_count;
static struct {
    int w, h, hz;
    char label[48], value[32];
} modes[MODES];
static int mode_count;
static SDL_DisplayID modes_display;

static void refresh_displays(void)
{
    int n = 0;
    SDL_DisplayID* ids = SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetDisplays(&n) : NULL;
    display_count = 0;
    for (int i = 0; ids && i < n && display_count < DISPLAYS; i++) {
        const char* name = SDL_GetDisplayName(ids[i]);
        displays[display_count].id = ids[i];
        snprintf(displays[display_count].label, sizeof displays[0].label, "%d: %s", i + 1, name ? name : "Display");
        snprintf(displays[display_count].value, sizeof displays[0].value, "%d %s", i, name ? name : "");
        display_count++;
    }
    SDL_free(ids);
}
/* The display the setting names: by name, then by index, then the primary (0: no video). */
static int display_index(void)
{
    refresh_displays();
    if (!display_count)
        return -1;
    int index = -1, primary = 0;
    char name[128] = "";
    if (display_want[0] && strcmp(display_want, "primary") && sscanf(display_want, "%d %127[^\n]", &index, name) >= 1) {
        for (int i = 0; name[0] && i < display_count; i++) {
            const char* n = SDL_GetDisplayName(displays[i].id);
            if (n && !strcmp(n, name))
                return i;
        }
        if (index >= 0 && index < display_count)
            return index;
    }
    SDL_DisplayID p = SDL_GetPrimaryDisplay();
    for (int i = 0; i < display_count; i++)
        if (displays[i].id == p)
            primary = i;
    return primary;
}
static SDL_DisplayID display_target(void)
{
    if (!display_want[0] || !strcmp(display_want, "primary"))
        return 0; /* (where it is: the window manager's choice, as before) */
    int i = display_index();
    return i >= 0 ? displays[i].id : 0;
}
int pc_display_count(void)
{
    refresh_displays();
    return display_count;
}
const char* pc_display_label(int i) { return i >= 0 && i < display_count ? displays[i].label : NULL; }
const char* pc_display_value(int i) { return i >= 0 && i < display_count ? displays[i].value : NULL; }
int pc_display_current(void) { return display_index(); }
void pc_display_choose(const char* value)
{
    snprintf(display_want, sizeof display_want, "%s", value ? value : "");
    place_pending = 1;
    modes_display = 0; /* (the modes listed were another display's) */
}
const char* pc_display_setting(void) { return display_want[0] ? display_want : "primary"; }

/* ROOD_TEST_FULLSCREEN_MODES "WxH@Hz,WxH@Hz,...": tests' exclusive modes in place of the display's (a
 * headless SDL display has none, so Fullscreen mode and its "Keep these settings?" could not be
 * exercised). They are listed and chosen like real ones; going fullscreen with one takes the desktop's
 * mode (no real mode switch). Unset: the display's own. */
static const char* test_modes(void)
{
    const char* list = getenv("ROOD_TEST_FULLSCREEN_MODES");
    return list && *list ? list : NULL;
}
static void add_mode(int w, int h, int hz)
{
    int seen = 0;
    for (int j = 1; j < mode_count; j++)
        seen |= modes[j].w == w && modes[j].h == h && modes[j].hz == hz;
    if (seen || w <= 0 || h <= 0 || mode_count >= MODES)
        return;
    modes[mode_count].w = w, modes[mode_count].h = h, modes[mode_count].hz = hz;
    snprintf(modes[mode_count].label, sizeof modes[0].label, hz ? "%d x %d, %d Hz" : "%d x %d", w, h, hz);
    snprintf(modes[mode_count].value, sizeof modes[0].value, "%dx%d@%d", w, h, hz);
    mode_count++;
}
/* The exclusive modes of the target display: deduplicated (size and whole Hz), "Desktop" first. */
static void refresh_modes(void)
{
    int i = display_index(), n = 0;
    SDL_DisplayID id = i >= 0 ? displays[i].id : 0;
    const char* test = test_modes();
    mode_count = 0;
    modes_display = id;
    snprintf(modes[0].label, sizeof modes[0].label, "Desktop");
    snprintf(modes[0].value, sizeof modes[0].value, "desktop");
    mode_count = 1;
    if (test) {
        for (const char* p = test; p; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL) {
            int w = 0, h = 0, hz = 0;
            if (sscanf(p, "%dx%d@%d", &w, &h, &hz) >= 2)
                add_mode(w, h, hz);
        }
        return;
    }
    SDL_DisplayMode** list = id ? SDL_GetFullscreenDisplayModes(id, &n) : NULL;
    for (int k = 0; list && k < n; k++)
        add_mode(list[k]->w, list[k]->h, (int)(list[k]->refresh_rate + 0.5f));
    SDL_free(list);
}
static int exclusive_mode(SDL_DisplayID display, SDL_DisplayMode* out)
{
    int w = 0, h = 0, hz = 0;
    if (test_modes())
        return 0; /* (the desktop's mode) */
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
    if (!desktop)
        return 0;
    if (sscanf(mode_want, "%dx%d@%d", &w, &h, &hz) < 2 || w <= 0 || h <= 0)
        w = desktop->w, h = desktop->h, hz = (int)(desktop->refresh_rate + 0.5f);
    return SDL_GetClosestFullscreenDisplayMode(display, w, h, (float)hz, true, out);
}
int pc_fullscreen_mode_count(void)
{
    refresh_modes();
    return mode_count;
}
const char* pc_fullscreen_mode_label(int i) { return i >= 0 && i < mode_count ? modes[i].label : NULL; }
const char* pc_fullscreen_mode_value(int i) { return i >= 0 && i < mode_count ? modes[i].value : NULL; }
/* The listed mode the setting gives (the closest the display has), 0 Desktop. */
int pc_fullscreen_mode_current(void)
{
    SDL_DisplayMode m;
    refresh_modes();
    if (test_modes()) { /* the listed one the setting names */
        for (int i = 1; i < mode_count; i++)
            if (!strcmp(modes[i].value, mode_want))
                return i;
        return 0;
    }
    if (!strcmp(mode_want, "desktop") || !modes_display || !exclusive_mode(modes_display, &m))
        return 0;
    for (int i = 1; i < mode_count; i++)
        if (modes[i].w == m.w && modes[i].h == m.h && modes[i].hz == (int)(m.refresh_rate + 0.5f))
            return i;
    return 0;
}
void pc_fullscreen_mode_choose(const char* value)
{
    snprintf(mode_want, sizeof mode_want, "%s", value && *value ? value : "desktop");
    if (window_mode == 2)
        place_pending = 1;
}
const char* pc_fullscreen_mode_setting(void) { return mode_want; }

/* settings present_pacing: 0 classic (the frame loop sleeps to 59.94 Hz deadlines, in-betweens at k/n of a field), 1 display (the
 * display's vblank paces the presents, the in-between's phase is the real time since the field began). The game's own stepping
 * is the same either way (deadlines). Display pacing needs the swap to wait for the refresh, so it turns the presenters' vsync
 * on; in fast mode (tests, no pacing) it does nothing. */
int pc_present_pacing;
static double sim_hz(void);
int pc_vsync_get(void) { return vsync_on || (pc_present_pacing == 1 && !fast && sim_hz() <= 0); }
void pc_vsync_set(int on) { vsync_on = on != 0; } /* the presenters follow at their next present */

/* Images per vsync (pc_present_rate): 0 "Match display" is the display's refresh over 59.94 Hz; with vsync on
 * never more than the display shows (more would wait a refresh each and slow the game). */
extern int pc_present_rate __attribute__((weak));
static int display_rate(void)
{
    const SDL_DisplayMode* m = window ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)) : NULL;
    int rate = m && m->refresh_rate > 0 ? (int)(m->refresh_rate / 59.94f + 0.5f) : 1;
    return rate < 1 ? 1 : rate > 8 ? 8 : rate;
}
int pc_window_present_rate(void)
{
    int rate = &pc_present_rate ? pc_present_rate : 1;
    if (rate <= 0)
        rate = display_rate();
    if (vsync_on && rate > display_rate())
        rate = display_rate();
    return rate < 1 ? 1 : rate > 8 ? 8 : rate;
}
extern float pc_present_phase __attribute__((weak));
extern int pc_hw_interpolate __attribute__((weak));
extern int pc_hw_active __attribute__((weak));
extern void pc_hw_sync(int, int, int, int) __attribute__((weak));
extern void pc_hwc_fence_sync(int, int, int, int); /* hw_cmd.c: the read-back of the rectangle into the game's VRAM */
/* Present statistics (RPC present_stats). An image is noted as (field, phase): it shows the game frame of that field blended to that
 * fraction of the field (0 up to 1), so its time on the game's clock is field + phase fields. Intervals between presents are real
 * time; spacing between images is that game-clock time, which is what motion follows (even spacing is even motion). */
#define STAT_RING 600
typedef struct {
    long long field;
    float phase;
} image_note;
static struct {
    long long at[STAT_RING], first, last;
    image_note note[STAT_RING];
    unsigned long long presents, fields, extras, violations, per_field[9];
    int in_field; /* images of the current field so far */
} stats;
static void stat_present(long long field, float phase)
{
    long long t = now_ns();
    if (!stats.presents)
        stats.first = t;
    image_note* n = &stats.note[stats.presents % STAT_RING];
    if (stats.presents) {
        const image_note* was = &stats.note[(stats.presents - 1) % STAT_RING];
        if ((double)(field - was->field) + (double)(phase - was->phase) <= 0)
            ++stats.violations; /* an image shown no later than the one before it */
    }
    n->field = field, n->phase = phase;
    stats.at[stats.presents++ % STAT_RING] = stats.last = t;
    ++stats.in_field;
}
static long long field_counter; /* game fields presented since the start (the statistics' counts restart; this does not) */
static void stat_field_begin(void)
{
    ++field_counter;
    if (stats.fields)
        ++stats.per_field[stats.in_field > 8 ? 8 : stats.in_field];
    stats.in_field = 0;
    ++stats.fields;
}
int pc_present_stats_json(char* out, int size)
{
    int n = stats.presents < STAT_RING ? (int)stats.presents : STAT_RING;
    double sum = 0, sq = 0, lo = 1e18, hi = 0, gsum = 0, gsq = 0, glo = 1e18, ghi = 0;
    int count = 0;
    for (int i = 1; i < n; i++) {
        unsigned long long a = stats.presents - (unsigned long long)n + (unsigned)i - 1, b = a + 1;
        double d = (double)(stats.at[b % STAT_RING] - stats.at[a % STAT_RING]) / 1e6;
        const image_note *na = &stats.note[a % STAT_RING], *nb = &stats.note[b % STAT_RING];
        double g = ((double)(nb->field - na->field) + (double)(nb->phase - na->phase)) * (double)FIELD_NS / 1e6;
        sum += d, sq += d * d, gsum += g, gsq += g * g, ++count;
        lo = d < lo ? d : lo, hi = d > hi ? d : hi, glo = g < glo ? g : glo, ghi = g > ghi ? g : ghi;
    }
    double mean = count ? sum / count : 0, var = count ? sq / count - mean * mean : 0;
    double gmean = count ? gsum / count : 0, gvar = count ? gsq / count - gmean * gmean : 0;
    double seconds = stats.presents > 1 ? (double)(stats.last - stats.first) / 1e9 : 0;
    unsigned long long closed = 0, images = 0;
    for (int k = 1; k <= 8; k++)
        closed += stats.per_field[k], images += stats.per_field[k] * (unsigned)k;
    int used = snprintf(out, (size_t)size,
        "{\"pacing\":%d,\"sim_hz\":%.2f,\"presents\":%llu,\"fields\":%llu,\"presents_per_second\":%.2f,\"fields_per_second\":%.2f,"
        "\"interval_ms\":{\"mean\":%.3f,\"stddev\":%.3f,\"min\":%.3f,\"max\":%.3f},"
        "\"image_spacing_ms\":{\"mean\":%.3f,\"stddev\":%.3f,\"min\":%.3f,\"max\":%.3f},\"order_violations\":%llu,"
        "\"images_per_field\":{\"mean\":%.3f,\"counts\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu]},\"display_hz\":%.2f,\"recent\":[",
        pc_present_pacing, sim_hz(), stats.presents, stats.fields, seconds > 0 ? (double)stats.presents / seconds : 0,
        seconds > 0 ? (double)stats.fields / seconds : 0, mean, var > 0 ? sqrt(var) : 0, count ? lo : 0, hi, gmean,
        gvar > 0 ? sqrt(gvar) : 0, count ? glo : 0, ghi, stats.violations, closed ? (double)images / (double)closed : 0,
        stats.per_field[1], stats.per_field[2], stats.per_field[3], stats.per_field[4], stats.per_field[5], stats.per_field[6],
        stats.per_field[7], stats.per_field[8],
        window && SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window))
            ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window))->refresh_rate : 0.0f);
    int recent = n < 48 ? n : 48;
    for (int i = 0; i < recent && used < size - 64; i++) {
        const image_note* k = &stats.note[(stats.presents - (unsigned long long)recent + (unsigned)i) % STAT_RING];
        used += snprintf(out + used, (size_t)(size - used), "%s[%lld,%.5f]", i ? "," : "", k->field, k->phase);
    }
    used += snprintf(out + used, (size_t)(size - used), "]}");
    return used;
}
void pc_present_stats_reset(void) { memset(&stats, 0, sizeof stats); }

/* The display's vblanks, as the presents see them (display pacing). Real: each present returns on the swap, which anchors a grid of
 * the observed interval (a running average of the intervals between presents: a variable refresh rate or a compositor moves it and
 * the grid follows). Simulated (ROOD_PRESENT_SIM_HZ=hz[:variation], for tests): a fake clock steps vblank to vblank, nothing
 * waits; with a variation (a fraction) each interval is that much longer or shorter by a seeded random amount, as a VRR display's. */
static long long vb_anchor_ns;
static double vb_interval_ns;
static double sim_hz_value = -1, sim_variation;
static long long sim_next_ns; /* the next simulated vblank on the game's clock (field 0 starts at 0) */
static unsigned sim_rng = 12345;
static double sim_hz(void)
{
    if (sim_hz_value < 0) {
        const char* v = getenv("ROOD_PRESENT_SIM_HZ");
        sim_hz_value = v ? atof(v) : 0;
        const char* c = v ? strchr(v, ':') : NULL;
        sim_variation = c ? atof(c + 1) : 0;
    }
    return sim_hz_value;
}
static double sim_random(void) /* -1 to 1 */
{
    sim_rng = sim_rng * 1664525u + 1013904223u;
    return (double)(sim_rng >> 8) / (double)(1 << 23) - 1.0;
}
static long long sim_step(void) { return (long long)(1e9 / sim_hz_value * (1.0 + sim_variation * sim_random())); }
static long long sim_vblank_after(long long t_ns) /* the first simulated vblank at or after t that is not spent */
{
    if (!sim_next_ns)
        sim_next_ns = sim_step();
    while (sim_next_ns < t_ns)
        sim_next_ns += sim_step();
    return sim_next_ns;
}
static long long real_vblank_next(long long now)
{
    double interval = vb_interval_ns;
    if (interval <= 0) {
        const SDL_DisplayMode* m = window ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)) : NULL;
        interval = m && m->refresh_rate > 1 ? 1e9 / m->refresh_rate : (double)FIELD_NS;
    }
    if (!vb_anchor_ns)
        return now + (long long)interval;
    long long k = (long long)((double)(now - vb_anchor_ns) / interval);
    return vb_anchor_ns + (long long)((double)(k < 0 ? 0 : k + 1) * interval);
}
static void real_vblank_seen(void)
{
    long long now = now_ns();
    if (vb_anchor_ns) {
        double gap = (double)(now - vb_anchor_ns);
        if (vb_interval_ns <= 0) {
            vb_interval_ns = gap > 2e6 && gap < 5e7 ? gap : (double)FIELD_NS / 2;
            vb_anchor_ns = now;
            return;
        }
        /* A phase-locked grid: the swap's return time jitters (a wake-up late or early), the display's vblanks do not. The grid point
         * this return belongs to moves by a tenth of the error, and the interval follows the observed spacing slowly (a variable
         * refresh rate or a compositor changes it and the grid follows within a few presents). */
        double n = gap / vb_interval_ns;
        long long whole = (long long)(n + 0.5);
        if (whole < 1)
            whole = 1;
        long long predicted = vb_anchor_ns + (long long)((double)whole * vb_interval_ns);
        double error = (double)(now - predicted);
        if (whole == 1 && gap > 0.6 * vb_interval_ns && gap < 1.6 * vb_interval_ns)
            vb_interval_ns = 0.95 * vb_interval_ns + 0.05 * gap;
        if (error > 0.5 * vb_interval_ns || error < -0.5 * vb_interval_ns || whole > 4) { /* lost the grid (a stall): take this one */
            vb_anchor_ns = now;
            return;
        }
        vb_anchor_ns = predicted + (long long)(0.1 * error);
        return;
    }
    vb_anchor_ns = now;
}
/* Display pacing's images for this game field (the frame just drawn is the field's): one for each vblank that falls inside it (at
 * least one), each blended to the real time its vblank puts it on screen over the field's length, so a refresh that is not a whole
 * multiple of the field (90 Hz: 1, 2, 1, 2 images; 144 Hz: 2, 3, 2, 3, 2) moves evenly. The first image is placed the same way
 * (its vblank follows the field's start by up to a refresh). `limit` caps the images (images per vsync). */
static void present_display_images(const pc_present_request* r, int limit, int blend)
{
    int sim = sim_hz() > 0;
    long long field = field_counter - 1, start = sim ? field * FIELD_NS : r->pace.field_start_ns, end = start + FIELD_NS;
    for (int k = 0; k < (blend ? limit : 1); k++) {
        long long shown = sim ? sim_vblank_after(start) : real_vblank_next(now_ns());
        if (sim && shown >= end && k > 0)
            break;
        if (!sim && k > 0 && shown >= end)
            break;
        float phase = blend ? (float)((double)(shown - start) / (double)FIELD_NS) : 0.0f;
        phase = phase < 0 ? 0 : phase > 0.999f ? 0.999f : phase;
        if (pc_jitter)
            pc_jitter(k ? PC_JITTER_FRACTION : PC_JITTER_PRESENT);
        if (&pc_present_phase)
            pc_present_phase = phase;
        uint64_t cost = pc_cost_begin();
        window_image(r->vram, r->x, r->y, r->w, r->h);
        if (!k)
            pc_cost_end(PC_COST_PRESENT, cost);
        stat_present(field, phase);
        if (k)
            ++stats.extras;
        if (sim) {
            if (shown < end)
                sim_next_ns += sim_step(); /* (this vblank is spent) */
        } else
            real_vblank_seen();
    }
    if (&pc_present_phase)
        pc_present_phase = 0;
}
/* The present service (present_service.h): what a vsync's tick used to do itself. The request's images (pacing, the frame, its fractions)
 * run on the renderer thread when it is on (render_thread.h); the header they read was filled before, on this thread. */
static void present_images(void* arg)
{
    const pc_present_request* r = arg;
    stat_field_begin();
    pc_present_header_use(r->header);
    int interp = r->pace.interp;
    if (r->pace.pacing == 1 && (sim_hz() > 0 || r->pace.paced)) {
        int limit = r->pace.limit;
        present_display_images(r, limit, interp);
        pc_present_header_release(r->header);
        return;
    }
    if (pc_jitter)
        pc_jitter(PC_JITTER_PRESENT);
    uint64_t cost = pc_cost_begin();
    window_image(r->vram, r->x, r->y, r->w, r->h);
    pc_cost_end(PC_COST_PRESENT, cost);
    stat_present(field_counter - 1, 0.0f);
    /* More images per vsync (frame interpolation at its fractions): present_rate, its "match display" and the vsync cap. */
    int rate = r->pace.rate;
    if (interp && rate > 1) {
        rate = rate > 8 ? 8 : rate;
        for (int k = 1; k < rate; k++) {
            wait_fraction(r, k, rate);
            if (pc_jitter)
                pc_jitter(PC_JITTER_FRACTION);
            pc_present_phase = (float)k / (float)rate;
            window_image(r->vram, r->x, r->y, r->w, r->h);
            stat_present(field_counter - 1, pc_present_phase);
            ++stats.extras;
        }
        pc_present_phase = 0;
    }
    pc_present_header_release(r->header);
}
void pc_present_post(const pc_present_request* in)
{
    if (in->kind == PC_PRESENT_SYNC) {
        if (pc_hw_sync)
            pc_hwc_fence_sync(in->x, in->y, in->w, in->h); /* a request: the renderer reads the rectangle back, this side takes it into its VRAM */
        return;
    }
    pc_present_request r = *in;
    r.header = header_for_frame();
    r.pace.pacing = pc_present_pacing;
    r.pace.paced = !fast && !fast_forward_held() && !charge_hidden;
    r.pace.rate = pc_window_present_rate();
    r.pace.limit = &pc_present_rate && pc_present_rate > 0 ? (pc_present_rate > 8 ? 8 : pc_present_rate) : 8;
    r.pace.interp = &pc_present_phase && &pc_hw_interpolate && &pc_hw_active && pc_hw_interpolate && pc_hw_active;
    r.pace.deadline_ns = deadline.tv_sec ? deadline.tv_sec * 1000000000LL + deadline.tv_nsec : 0;
    r.pace.field_start_ns = field_start_ns;
    sim_hz(); /* (read once here: the environment, for the service that follows) */
    pc_hwc_post(); /* the walk's records go first (the frame is drawn from what they made) */
    pc_rt_post(present_images, &r, sizeof r);
}

int pc_window_remember_get(void) { return remember_on; }
void pc_window_remember_set(int on) { remember_on = on != 0; }
/* The window's place as last left windowed: 1 and x y w h when known. */
int pc_window_rect_get(int out[4])
{
    if (!rect_known)
        return 0;
    memcpy(out, rect, sizeof rect);
    return 1;
}
void pc_window_rect_set(const int r[4])
{
    if (r[2] < 160 || r[3] < 120)
        return;
    memcpy(rect, r, sizeof rect);
    rect_known = 1;
}
/* A rectangle a display contains (at least its top-left 64 x 64: the title bar is reachable). */
static int rect_visible(const int r[4])
{
    int n = 0, ok = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    for (int i = 0; ids && i < n && !ok; i++) {
        SDL_Rect b;
        if (SDL_GetDisplayBounds(ids[i], &b))
            ok = r[0] >= b.x && r[1] >= b.y && r[0] + 64 <= b.x + b.w && r[1] + 64 <= b.y + b.h;
    }
    SDL_free(ids);
    return ok;
}
extern int pc_settings_peek(const char* key, char* out, int size) __attribute__((weak)); /* project_settings.cpp */
/* The presenters' window: where and how large it opens (their create properties; one helper for the
 * three create sites). The remembered place when remember_window is on (read from the file: the settings
 * load after the window exists) and a display contains it; else (40, 40) at the size asked for. ROOD_WINDOW
 * (tests) always gives the size. */
void pc_window_place(SDL_PropertiesID props, int width, int height)
{
    int x = 40, y = 40, r[4];
    char line[96];
    if (!getenv("ROOD_WINDOW") && pc_settings_peek && pc_settings_peek("remember_window", line, sizeof line)
        && atoi(line) && pc_settings_peek("window_rect", line, sizeof line)
        && sscanf(line, "%d %d %d %d", &r[0], &r[1], &r[2], &r[3]) == 4 && r[2] >= 160 && r[3] >= 120
        && rect_visible(r)) {
        x = r[0], y = r[1], width = r[2], height = r[3];
        pc_window_rect_set(r);
        remember_on = 1;
    }
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
}
extern void pc_ui_settings_touch(void) __attribute__((weak)); /* ui_settings.cpp: something to save */
/* Moved or resized while windowed: the place to remember (saved debounced when remember_window is on). */
static void window_moved(void)
{
    int r[4];
    if (!window || (SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)))
        return;
    SDL_GetWindowPosition(window, &r[0], &r[1]);
    SDL_GetWindowSize(window, &r[2], &r[3]);
    if (rect_known && !memcmp(r, rect, sizeof rect))
        return;
    pc_window_rect_set(r);
    if (remember_on && pc_ui_settings_touch)
        pc_ui_settings_touch();
}

/* `text` as a JSON string's contents: a display's name is the monitor's own
 * (`27"`, a backslash), and the settings may hold any text. */
static void json_text(char* out, size_t size, const char* text)
{
    size_t n = 0;
    for (; *text && n + 7 < size; text++) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\')
            out[n++] = '\\', out[n++] = (char)c;
        else if (c < 0x20)
            n += (size_t)snprintf(out + n, size - n, "\\u%04x", c);
        else
            out[n++] = (char)c;
    }
    out[n] = 0;
}

/* RPC (system): the display back-ends' state. */
int pc_window_display_json(char* out, int size)
{
    int i = display_index(), n = 0, r[4] = { 0, 0, 0, 0 };
    char display[256], mode[sizeof mode_want * 6];
    refresh_modes();
    pc_window_rect_get(r);
    json_text(display, sizeof display, pc_display_setting());
    json_text(mode, sizeof mode, mode_want);
    n += snprintf(out + n, (size_t)(size - n),
        "{\"display\":\"%s\",\"display_index\":%d,\"displays\":%d,\"fullscreen_mode\":\"%s\",\"modes\":%d,"
        "\"mode_index\":%d,\"vsync\":%s,\"present_rate\":%d,\"display_rate\":%d,\"remember_window\":%s,"
        "\"window_rect\":[%d,%d,%d,%d]}",
        display, i, display_count, mode, mode_count, pc_fullscreen_mode_current(),
        vsync_on ? "true" : "false", pc_window_present_rate(), display_rate(), remember_on ? "true" : "false", r[0],
        r[1], r[2], r[3]);
    return n;
}

/* ---- Binding capture (System > Bindings, ui_settings.cpp) ----
 * pc_ui_capture_begin: the next key press or gamepad button (or trigger past
 * half way) goes to pc_ui_settings_capture_done instead of the game and the
 * overlay. Keys without a settings name are ignored and reserved keys
 * (pc_ui_settings_key_reserved: the prompt says why) refused, and the
 * capture goes on; Escape cancels. A capture started from the gamepad (the
 * last device used) cancels after 5 s without gamepad input, as a pad has no
 * Escape. Releases pass through (keys held before the capture let go
 * normally). The input that ended the capture stays the capture's until it
 * is released: the captured (or Escape) key's auto-repeats and text, and the
 * captured pad element (now bound to a PS1 button, they would press it). */
extern int pc_ui_settings_capture_done(int kind, int code);
extern int pc_ui_settings_capture_button(void);
extern int pc_ui_settings_capture_refuse(int kind, int code);
extern const char* pc_ui_settings_capture_note(void);
static int capturing, capture_pad, last_input_pad, capture_last = -3;
static Uint64 capture_since;
static int pad_held = PC_PAD_NONE; /* the captured gamepad element, until released */
static SDL_Keycode key_held;      /* the key that ended the capture, until released */
int pc_ui_capture_begin(void)
{
    if (!window)
        return 0;
    capturing = 1;
    capture_pad = last_input_pad;
    capture_since = SDL_GetTicks();
    return 1;
}
static void capture_end(int kind, int code)
{
    capturing = 0;
    capture_last = pc_ui_settings_capture_done(kind, code);
}
static int is_trigger(int axis) { return axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER; }
/* With an event: 1 when the capture took it. Without: 1 while the gamepad
 * belongs to the capture (pc_window_buttons). */
static int capture_filter(const SDL_Event* e)
{
    /* The settings ended it (the menu closed, the shell cancelled). */
    if (capturing && pc_ui_settings_capture_button() < 0)
        capturing = 0;
    if (!e)
        return capturing || pad_held != PC_PAD_NONE;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        last_input_pad = 0;
        if (key_held && e->key.key == key_held)
            return 1; /* the captured key, still held: its repeats */
        if (!capturing)
            return 0;
        if (!e->key.repeat && e->key.key == SDLK_ESCAPE) {
            key_held = SDLK_ESCAPE;
            capture_end(-1, 0);
        } else if (!e->key.repeat && pc_key_name(e->key.key)
            && !pc_ui_settings_capture_refuse(PC_UI_CAPTURE_KEY, (int)e->key.key)) {
            key_held = e->key.key;
            capture_end(PC_UI_CAPTURE_KEY, (int)e->key.key);
        }
        return 1;
    case SDL_EVENT_KEY_UP:
        if (key_held && e->key.key == key_held)
            key_held = 0;
        return 0;
    case SDL_EVENT_TEXT_INPUT:
        return capturing || key_held;
    case SDL_EVENT_WINDOW_FOCUS_LOST: /* its release goes elsewhere */
        key_held = 0;
        return 0;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        last_input_pad = 1;
        if (!capturing)
            return 0;
        pad_held = e->gbutton.button;
        capture_end(PC_UI_CAPTURE_PAD, e->gbutton.button);
        return 1;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (pad_held == e->gbutton.button)
            pad_held = PC_PAD_NONE;
        return 0;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (e->gaxis.value > 16000 || e->gaxis.value < -16000)
            last_input_pad = 1;
        /* Released where the game stops counting it pressed (boot_controller.c). */
        if (pad_held == PC_PAD_AXIS(e->gaxis.axis) && e->gaxis.value <= PC_TRIGGER_PRESSED)
            pad_held = PC_PAD_NONE;
        if (!capturing)
            return 0;
        if (is_trigger(e->gaxis.axis) && e->gaxis.value > PC_TRIGGER_CAPTURE) {
            pad_held = PC_PAD_AXIS(e->gaxis.axis);
            capture_end(PC_UI_CAPTURE_PAD, PC_PAD_AXIS(e->gaxis.axis));
        } else if (e->gaxis.value > 16000 || e->gaxis.value < -16000)
            capture_since = SDL_GetTicks(); /* the pad is in use: not idle */
        return 1;
    case SDL_EVENT_GAMEPAD_REMOVED:
        pad_held = PC_PAD_NONE;
        return 0;
    default:
        return 0;
    }
}
static void capture_tick(void)
{
    if (capturing && pc_ui_settings_capture_button() < 0)
        capturing = 0;
    if (capturing && capture_pad && SDL_GetTicks() - capture_since > 5000)
        capture_end(-1, 0);
}
/* RPC (system): the capture's state (key_held / pad_held: what ended it, still held; refused: why the last key
 * pressed was refused); the last result (pc_ui_settings_capture_done: the other button swapped, -1 none or
 * cancelled, -2 refused; -3 none yet). */
int pc_capture_json(char* out, int size)
{
    const char* refused = capturing ? pc_ui_settings_capture_note() : NULL; /* (its reasons have no quotes) */
    return snprintf(out, size,
        "{\"listening\":%s,\"button\":%d,\"pad_timeout\":%s,\"pad_held\":%d,\"key_held\":\"%s\",\"refused\":\"%s\","
        "\"last\":%d}",
        capturing ? "true" : "false", capturing ? pc_ui_settings_capture_button() : -1, capture_pad ? "true" : "false",
        pad_held, key_held && pc_key_name(key_held) ? pc_key_name(key_held) : "", refused ? refused : "",
        capture_last);
}
/* RPC (system): a key or gamepad event pushed into SDL's queue as if the
 * device sent it (tests; the capture and the game read it as real input). */
int pc_window_inject_key(const char* name, int down)
{
    SDL_Keycode key = pc_key_from_name(name);
    if (!window || !key)
        return -1;
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.windowID = SDL_GetWindowID(window);
    e.key.key = key;
    e.key.scancode = SDL_GetScancodeFromKey(key, NULL);
    e.key.down = down != 0;
    return SDL_PushEvent(&e) ? 0 : -1;
}
/* RPC (system mouse): the pointer moved to window pixel (x, y), then a
 * button (1 left, 2 middle, 3 right) pressed or released there, or with
 * button 0 a wheel step (`down` > 0 up, < 0 down), pushed into SDL's queue
 * as the mouse would send them (tests: the overlay's event filter and ImGui
 * see real input, unlike the ui_menu pointer RPC). */
int pc_window_inject_mouse(float x, float y, int button, int down)
{
    if (!window)
        return -1;
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(window);
    e.motion.x = x, e.motion.y = y;
    /* Synthetic absolute points also carry the relative movement between
     * injections, as a real SDL motion event does. The first point anchors
     * the synthetic pointer without turning a relative-mode camera. */
    static int injected_position_valid;
    static float injected_x, injected_y;
    if (injected_position_valid) {
        e.motion.xrel = x - injected_x;
        e.motion.yrel = y - injected_y;
    }
    injected_x = x, injected_y = y;
    injected_position_valid = 1;
    if (!SDL_PushEvent(&e))
        return -1;
    memset(&e, 0, sizeof e);
    if (button == 0) {
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.windowID = SDL_GetWindowID(window);
        e.wheel.y = (float)down;
        e.wheel.mouse_x = x, e.wheel.mouse_y = y;
    } else {
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = SDL_GetWindowID(window);
        e.button.button = (Uint8)button;
        e.button.down = down != 0;
        e.button.clicks = 1;
        e.button.x = x, e.button.y = y;
    }
    return SDL_PushEvent(&e) ? 0 : -1;
}
int pc_window_inject_pad(const char* name, int down)
{
    int element = pc_pad_element_from_name(name);
    if (!window || element < 0)
        return -1;
    SDL_Event e;
    memset(&e, 0, sizeof e);
    if (element >= PC_PAD_AXIS(0)) {
        e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        e.gaxis.axis = (Uint8)(element - PC_PAD_AXIS(0));
        e.gaxis.value = down ? 32767 : 0;
    } else {
        e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
        e.gbutton.button = (Uint8)element;
        e.gbutton.down = down != 0;
    }
    return SDL_PushEvent(&e) ? 0 : -1;
}

/* The Travel binding (fast travel, fast_travel.c): held now (focused; not
 * while a binding capture takes the pad), and the PS1 buttons sharing its key
 * or pad element (taken out of that frame when it fires). */
int pc_window_travel_held(void)
{
    return focused && (travel_key || (!capture_filter(NULL) && pc_controller_travel()));
}
unsigned pc_window_travel_shared(void) { return pc_bind_shared_bits(PC_BIND_TRAVEL); }
