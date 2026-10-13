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
#ifdef ROOD_VULKAN
#include "vk_hooks.h" /* ROOD_RENDERER=vulkan: vk_present.c */
#endif
static int focused = 1;
static SDL_Window* window;
static SDL_Surface* image;
static unsigned buttons;
static unsigned camera_keys; /* J L I K O held (camera_stick.c keyboard fallback) */
static int fast;
/* Fast-forward while held: ` (backtick) or L3 + R3 (boot_gamepad.c). The game
 * runs unpaced, as with --fast; audio drops what it cannot play. */
static int fast_forward_key;
static int fast_forward_held(void) { return focused && (fast_forward_key || pc_controller_fast_forward()); }
extern int pc_gl_active;
extern SDL_Window* pc_gl_create_window(const char*, int, int);
extern void pc_gl_resize(int, int);
extern void pc_gl_present(const uint16_t*, int, int, int, int);
extern int pc_overlay_event(const SDL_Event*);
int pc_window_enabled;
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
    if (fast || fast_forward_held() || !deadline.tv_sec || n <= 0)
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
void pc_window_init(int enabled, int run_fast)
{
    fast = run_fast;
    pc_window_enabled = enabled;
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
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, 40);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 40);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 640);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 480);
        window = SDL_CreateWindowWithProperties(props);
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
void pc_window_release_keys(void) { buttons = camera_keys = 0; }
extern unsigned pc_rpc_buttons(void);
/* input_record.c: ROOD_RECORD writes, ROOD_REPLAY replaces, the live input. */
extern unsigned pc_input_buttons(unsigned live, unsigned rpc);
extern int pc_input_stick(float out[2], int live_valid);
unsigned pc_window_buttons(void)
{
    /* Developer RPC input (dev_rpc.c) applies even while unfocused. */
    unsigned keys = buttons | (pc_camera_stick_keys && (camera_keys & 16) ? 1u << 10 : 0);
    unsigned result = pc_input_buttons(focused ? keys | pc_controller_buttons() : 0, pc_rpc_buttons());
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
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        /* The developer overlay (F1) takes the events it uses. */
        if ((pc_gl_active || PC_VK_ACTIVE) && pc_overlay_event(&event))
            continue;
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            exit(0);
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            buttons = camera_keys = 0;
            focused = 0;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            focused = 1;
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
            buttons |= keybit(event.key.key);
            camera_keys |= camera_keybit(event.key.key);
            /* The keyboard is the last device used (button prompts). */
            if (keybit(event.key.key) || camera_keybit(event.key.key))
                pc_button_prompts_device(PC_PROMPTS_KEYBOARD, 0);
            /* Unless ` is bound to a PS1 button (bind_key_*). */
            if (event.key.key == SDLK_GRAVE && !keybit(event.key.key))
                fast_forward_key = 1;
            break;
        case SDL_EVENT_KEY_UP:
            buttons &= ~keybit(event.key.key);
            camera_keys &= ~camera_keybit(event.key.key);
            if (event.key.key == SDLK_GRAVE)
                fast_forward_key = 0;
            break;
        default:
            pc_controller_event(&event);
            break;
        }
    }
    pc_controller_poll();
    if (fast_forward_held())
        deadline.tv_sec = 0; /* pace again from now when it is released */
    else if (!fast) {
        /* Pace to absolute NTSC field deadlines (59.94 Hz) so the frame's own
         * work counts toward the period; resynchronise after long stalls. */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (!deadline.tv_sec)
            deadline = now;
        deadline.tv_nsec += 16683350;
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
