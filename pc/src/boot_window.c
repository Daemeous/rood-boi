/* SDL3 frontend for the native boot: the window (OpenGL in gl_present.c, or
 * a software surface), keyboard and gamepad input and frame pacing. SDL
 * picks the display backend: native Wayland where the compositor supports
 * it, else X11 (SDL_VIDEO_DRIVER=wayland or x11 chooses explicitly). */
#define _POSIX_C_SOURCE 200809L
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "boot_controller.h"
#include "camera_stick.h"
#include "button_prompts.h"
#include "ui/ui_settings.h" /* binding capture: PC_UI_CAPTURE_* */
#ifdef ROOD_VULKAN
#include "vk_hooks.h" /* ROOD_RENDERER=vulkan: vk_present.c */
#endif
static int focused = 1;
static SDL_Window* window;
static SDL_Surface* image;
static unsigned buttons;
static int travel_key; /* a key bound to the Travel action held (fast travel) */
static unsigned camera_keys; /* J L I K O held (camera_stick.c keyboard fallback) */
static int fast;
/* Fast-forward while held: ` (backtick) or L3 + R3 (boot_gamepad.c). The game
 * runs unpaced, as with --fast; audio drops what it cannot play. */
static int fast_forward_key;
static int fast_forward_held(void) { return focused && (fast_forward_key || pc_controller_fast_forward()); }
/* Battle charging skip (charge_skip.c): the vsyncs of a skipped wind-up
 * that still run (its effect's loading) are not presented and not paced;
 * what the audio device has no room for meanwhile is dropped
 * (audio_output.c). */
extern int pc_charge_skip_hidden(void) __attribute__((weak));
extern void pc_audio_output_turbo(int on) __attribute__((weak)); /* audio_output.c */
static int charge_hidden; /* this vsync's */
static void charge_update(void)
{
    int hidden = pc_charge_skip_hidden && pc_charge_skip_hidden();
    if (hidden != charge_hidden && pc_audio_output_turbo)
        pc_audio_output_turbo(hidden);
    charge_hidden = hidden;
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
/* Extra presents (pc_present_rate): wait until k/n of the way from this
 * vsync's deadline to the next (no wait in fast mode). */
void pc_window_wait_fraction(int k, int n)
{
    if (fast || fast_forward_held() || charge_hidden || !deadline.tv_sec || n <= 0)
        return;
    struct timespec at = deadline;
    long long ns = at.tv_nsec + FIELD_NS * k / n;
    at.tv_sec += (time_t)(ns / 1000000000LL);
    at.tv_nsec = (long)(ns % 1000000000LL);
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
    if (image)
        SDL_DestroySurface(image);
    image = NULL;
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
        fprintf(stderr, "[window] null backend: CPU VRAM, RPC input, no video initialization\n");
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
    /* Vulkan presentation (vk_present.c) when asked for; OpenGL otherwise
     * or when Vulkan is unavailable. */
    if (renderer && !strcmp(renderer, "vulkan")) {
        window = pc_vk_create_window(title, width, height);
        if (!window)
            fprintf(stderr, "[window] Vulkan unavailable; using OpenGL\n");
    }
    if (!window)
#endif
    /* OpenGL presentation when available (gl_present.c), else a software surface. */
    window = pc_gl_create_window(title, width, height);
    /* Some drivers (32-bit NVIDIA) have OpenGL only through GLX: rather than
     * a software surface on Wayland, use X11 (XWayland) when it is there. */
    int software = renderer && (!strcmp(renderer, "x11") || !strcmp(renderer, "software"));
    if (!window && !software && !strcmp(SDL_GetCurrentVideoDriver(), "wayland") && getenv("DISPLAY")) {
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
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
        pc_window_place(props, 640, 480);
        window = pc_window_create(props);
        SDL_DestroyProperties(props);
        image = SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_XRGB8888);
        if (!window || !image) {
            fprintf(stderr, "Cannot create window: %s\n", SDL_GetError());
            exit(1);
        }
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
unsigned pc_window_buttons(void)
{
    /* Developer RPC input (dev_rpc.c) applies even while unfocused. */
    unsigned keys = buttons | (pc_camera_stick_keys && (camera_keys & 16) ? 1u << 10 : 0);
    unsigned pad = capture_filter(NULL) ? 0 : pc_controller_buttons(); /* a binding capture takes the pad */
    unsigned result = pc_input_buttons(focused ? keys | pad : 0, pc_rpc_buttons());
    if (pc_button_roles_buttons)
        result = pc_button_roles_buttons(result);
    /* R3 resets the right-stick camera instead (camera_stick.c). */
    return pc_camera_stick_buttons(result);
}
/* The camera's stick: the gamepad's right stick and the keys (focused). */
int pc_window_stick(float out[2])
{
    int valid = focused;
    out[0] = out[1] = 0;
    if (focused) {
        pc_controller_right_stick(out);
        if (pc_camera_stick_keys) {
            out[0] += (camera_keys & 2 ? 1.0f : 0) - (camera_keys & 1 ? 1.0f : 0);
            out[1] += (camera_keys & 8 ? 1.0f : 0) - (camera_keys & 4 ? 1.0f : 0);
        }
    }
    return pc_input_stick(out, valid);
}
void pc_window_tick(void)
{
    if (!window)
        return;
    window_mode_apply(); /* a switch asked for since the last tick (pc_window_mode_set) */
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        /* A binding capture (System > Bindings) takes keys and pad buttons first. */
        if (capture_filter(&event))
            continue;
        /* The developer overlay (F1) takes the events it uses. */
        if ((pc_gl_active || PC_VK_ACTIVE) && pc_overlay_event(&event))
            continue;
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            /* A settings change still waiting for its debounced save (System) is saved first. */
            if (pc_ui_settings_flush)
                pc_ui_settings_flush();
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
    capture_tick();
    charge_update();
    if (fast_forward_held() || charge_hidden)
        deadline.tv_sec = 0; /* pace again from now when it is released (or presents again) */
    else if (!fast) {
        /* Pace to absolute NTSC field deadlines (59.94 Hz) so the frame's own
         * work counts toward the period; resynchronise after long stalls. */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (!deadline.tv_sec)
            deadline = now;
        deadline.tv_nsec += FIELD_NS;
        if (deadline.tv_nsec >= 1000000000) {
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
    }
}
extern int pc_display_rgb24;
extern int pc_display_masked __attribute__((weak)); /* boot_sdk.c SetDispMask */
void pc_window_frame(const uint16_t* vram, int x, int y, int w, int h)
{
    if (!window)
        return;
    if (pc_gl_active) {
        pc_gl_present(vram, x, y, w, h);
        return;
    }
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        pc_vk_present(vram, x, y, w, h);
        return;
    }
#endif
    for (int yy = 0; yy < 480; yy++) {
        uint32_t* out = (uint32_t*)((unsigned char*)image->pixels + yy * image->pitch);
        for (int xx = 0; xx < 640; xx++) {
            const uint16_t* row = vram + ((y + yy * h / 480) & 511) * 1024;
            unsigned r, g, b;
            if (pc_display_rgb24) {
                /* 24-bit display: R, G, B bytes packed across halfwords. */
                int byte = x * 2 + (xx * w / 640) * 3;
                unsigned char rgb[3];
                for (int i = 0; i < 3; i++, byte++)
                    rgb[i] = byte & 1 ? row[(byte >> 1) & 1023] >> 8 : row[(byte >> 1) & 1023] & 255;
                r = rgb[0], g = rgb[1], b = rgb[2];
            } else {
                unsigned c = row[(x + xx * w / 640) & 1023];
                r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
            }
            out[xx] = &pc_display_masked && pc_display_masked ? 0 : (r << 16) | (g << 8) | b; /* SetDispMask(0): black */
        }
    }
    SDL_Surface* surface = SDL_GetWindowSurface(window);
    if (!surface || !SDL_BlitSurfaceScaled(image, NULL, surface, NULL, SDL_SCALEMODE_NEAREST)
        || !SDL_UpdateWindowSurface(window)) {
        static int reported;
        if (!reported++)
            fprintf(stderr, "[window] cannot draw the software surface: %s\n", SDL_GetError());
    }
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

int pc_vsync_get(void) { return vsync_on; }
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

/* RPC (system): the display back-ends' state. */
int pc_window_display_json(char* out, int size)
{
    int i = display_index(), n = 0, r[4] = { 0, 0, 0, 0 };
    refresh_modes();
    pc_window_rect_get(r);
    n += snprintf(out + n, (size_t)(size - n),
        "{\"display\":\"%s\",\"display_index\":%d,\"displays\":%d,\"fullscreen_mode\":\"%s\",\"modes\":%d,"
        "\"mode_index\":%d,\"vsync\":%s,\"present_rate\":%d,\"display_rate\":%d,\"remember_window\":%s,"
        "\"window_rect\":[%d,%d,%d,%d]}",
        pc_display_setting(), i, display_count, mode_want, mode_count, pc_fullscreen_mode_current(),
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
