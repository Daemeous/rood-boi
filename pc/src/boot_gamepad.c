/* SDL gamepads: the first connected gamepad (or the one ROOD_CONTROLLER
 * names) drives the PS1 pad, mapped by boot_controller.c. SDL's built-in
 * GameController database knows most controllers; SDL_GAMECONTROLLERCONFIG
 * or SDL_GAMECONTROLLERCONFIG_FILE add mappings, as does a community
 * gamecontrollerdb.txt in pc/user/ (or at ROOD_GAMECONTROLLERDB). Gamepads
 * connect and disconnect at any time. */
#define _POSIX_C_SOURCE 200809L
#include "boot_controller.h"
#include "button_prompts.h"
#include <SDL3/SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Gamepad* pad;
static unsigned mask;
static int fast_forward; /* L3 + R3 held (boot_window.c) */
static float right_stick[2]; /* -1..1, SDL's direction (down is +y) */
static float triggers[2];    /* L2, R2: their axis' travel past the press point (0..1), -1 not an axis */
static const char* requested;
static char requested_event[300];

/* ROOD_CONTROLLER=/dev/input/jsN (the old joystick interface) names the same
 * device as its evdev node, which is the path SDL reports. */
static void resolve_joystick_path(const char* path)
{
    if (strncmp(path, "/dev/input/js", 13))
        return;
    char directory[96];
    snprintf(directory, sizeof directory, "/sys/class/input/%s/device", path + 11);
    DIR* dir = opendir(directory);
    if (!dir)
        return;
    for (struct dirent* entry; (entry = readdir(dir));)
        if (!strncmp(entry->d_name, "event", 5)) {
            snprintf(requested_event, sizeof requested_event, "/dev/input/%s", entry->d_name);
            break;
        }
    closedir(dir);
}

/* Without ROOD_CONTROLLER any gamepad; with it, one whose device path or
 * name matches. */
static int wanted(SDL_JoystickID id)
{
    if (!requested)
        return 1;
    const char* path = SDL_GetGamepadPathForID(id);
    const char* name = SDL_GetGamepadNameForID(id);
    if (path && (!strcmp(path, requested) || (*requested_event && !strcmp(path, requested_event))))
        return 1;
    return name && strstr(name, requested);
}

/* The button prompts' device (button_prompts.c; weak: the controller probe
 * links without it): the prompt family of an SDL gamepad type (-1: the
 * keyboard). Generic, Steam and GameCube pads get the Xbox labels SDL's
 * standard layout uses. */
extern void pc_button_prompts_device(int family, int variant) __attribute__((weak));
static void prompts_device(int type)
{
    if (!pc_button_prompts_device)
        return;
    switch (type) {
    case -1:
        pc_button_prompts_device(PC_PROMPTS_KEYBOARD, 0);
        break;
    case SDL_GAMEPAD_TYPE_PS3:
        pc_button_prompts_device(PC_PROMPTS_PLAYSTATION, 3);
        break;
    case SDL_GAMEPAD_TYPE_PS4:
        pc_button_prompts_device(PC_PROMPTS_PLAYSTATION, 4);
        break;
    case SDL_GAMEPAD_TYPE_PS5:
        pc_button_prompts_device(PC_PROMPTS_PLAYSTATION, 5);
        break;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        pc_button_prompts_device(PC_PROMPTS_NINTENDO, 0);
        break;
    case SDL_GAMEPAD_TYPE_XBOX360:
        pc_button_prompts_device(PC_PROMPTS_XBOX, 360);
        break;
    default:
        pc_button_prompts_device(PC_PROMPTS_XBOX, 0);
        break;
    }
}

static void open_gamepad(SDL_JoystickID id)
{
    pad = SDL_OpenGamepad(id);
    if (!pad) {
        fprintf(stderr, "[controller] cannot open gamepad %u: %s\n", (unsigned)id, SDL_GetError());
        return;
    }
    const char* path = SDL_GetGamepadPath(pad);
    fprintf(stderr, "[controller] connected %s: %s, type %s (Circle/east confirms, Cross/south cancels)\n",
        path ? path : "gamepad", SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "gamepad",
        SDL_GetGamepadStringForType(SDL_GetGamepadType(pad)) ? SDL_GetGamepadStringForType(SDL_GetGamepadType(pad))
                                                             : "unknown");
    /* Button prompts: a gamepad just connected is the device in use until
     * the keyboard is pressed. */
    prompts_device(SDL_GetGamepadType(pad));
}

int pc_controller_type(void) { return pad ? (int)SDL_GetGamepadType(pad) : SDL_GAMEPAD_TYPE_UNKNOWN; }

static void open_first(SDL_JoystickID except)
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count && !pad; i++)
        if (ids[i] != except && wanted(ids[i]))
            open_gamepad(ids[i]);
    SDL_free(ids);
}

void pc_controller_init(void)
{
    requested = getenv("ROOD_CONTROLLER");
    if (requested && !strcmp(requested, "none"))
        return;
    if (requested)
        resolve_joystick_path(requested);
    /* Keep the state current while unfocused; the window gates input. */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "[controller] SDL gamepad support unavailable: %s\n", SDL_GetError());
        return;
    }
    const char* database = getenv("ROOD_GAMECONTROLLERDB");
    if (!database)
        database = "pc/user/gamecontrollerdb.txt";
    FILE* exists = fopen(database, "r");
    if (exists) {
        fclose(exists);
        int added = SDL_AddGamepadMappingsFromFile(database);
        if (added < 0)
            fprintf(stderr, "[controller] %s: %s\n", database, SDL_GetError());
        else
            fprintf(stderr, "[controller] %d mappings from %s\n", added, database);
    }
    open_first(0);
}

void pc_controller_event(const SDL_Event* event)
{
    if (event->type == SDL_EVENT_GAMEPAD_ADDED && !pad && wanted(event->gdevice.which))
        open_gamepad(event->gdevice.which);
    if (event->type == SDL_EVENT_GAMEPAD_REMOVED && pad && event->gdevice.which == SDL_GetGamepadID(pad)) {
        SDL_CloseGamepad(pad);
        pad = NULL;
        mask = 0;
        right_stick[0] = right_stick[1] = 0;
        fprintf(stderr, "[controller] disconnected\n");
        open_first(event->gdevice.which);
        if (!pad)
            prompts_device(-1);
    }
    /* The last device used decides the button prompts (auto). */
    if (pad && ((event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event->gbutton.which == SDL_GetGamepadID(pad))
        || (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && event->gaxis.which == SDL_GetGamepadID(pad)
            && (event->gaxis.value > 16000 || event->gaxis.value < -16000))))
        prompts_device(SDL_GetGamepadType(pad));
}

void pc_controller_poll(void)
{
    if (!pad) {
        mask = 0;
        fast_forward = 0;
        right_stick[0] = right_stick[1] = 0;
        triggers[0] = triggers[1] = -1;
        return;
    }
    PcControllerState state;
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
        state.buttons[i] = SDL_GetGamepadButton(pad, (SDL_GamepadButton)i);
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++)
        state.axes[i] = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)i);
    mask = pc_controller_mask(&state);
    /* Both stick clicks together: fast-forward while held, instead of
     * whatever PS1 buttons they are bound to (L3 and R3 by default; R3
     * resets the camera). */
    fast_forward = state.buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK] && state.buttons[SDL_GAMEPAD_BUTTON_RIGHT_STICK];
    if (fast_forward)
        for (int i = 0; i < PC_PAD_BUTTONS; i++)
            if (pc_bind_pad[i] == SDL_GAMEPAD_BUTTON_LEFT_STICK || pc_bind_pad[i] == SDL_GAMEPAD_BUTTON_RIGHT_STICK)
                mask &= ~(1u << i);
    right_stick[0] = state.axes[SDL_GAMEPAD_AXIS_RIGHTX] / 32767.0f;
    right_stick[1] = state.axes[SDL_GAMEPAD_AXIS_RIGHTY] / 32767.0f;
    for (int i = 0; i < 2; i++) { /* (pad bits 0 and 1: L2, R2) */
        int e = pc_bind_pad[i] - PC_PAD_AXIS(0);
        float t = (float)((e >= 0 && e < SDL_GAMEPAD_AXIS_COUNT ? state.axes[e] : 0) - PC_TRIGGER_PRESSED)
                / (32767 - PC_TRIGGER_PRESSED);
        triggers[i] = e < 0 || e >= SDL_GAMEPAD_AXIS_COUNT ? -1 : t < 0 ? 0 : t > 1 ? 1 : t;
    }
}

unsigned pc_controller_buttons(void) { return mask; }
int pc_controller_fast_forward(void) { return fast_forward; }
/* The right stick (the PS1 pad stays digital: the camera reads it,
 * camera_stick.c); 0 without a gamepad. */
/* L2 (0) or R2 (1) bound to an axis: how far past the press point it is
 * (0..1; the map's zoom, ui/map_bridge.c); -1: a button, or no gamepad. */
float pc_controller_trigger(int which) { return which == 0 || which == 1 ? triggers[which] : -1; }
void pc_controller_right_stick(float out[2])
{
    out[0] = right_stick[0] < -1 ? -1 : right_stick[0];
    out[1] = right_stick[1] < -1 ? -1 : right_stick[1];
}
