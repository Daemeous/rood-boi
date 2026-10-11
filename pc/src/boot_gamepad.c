/* SDL gamepads: the first connected gamepad (or the one ROOD_CONTROLLER
 * names) drives the PS1 pad, mapped by boot_controller.c. SDL's built-in
 * GameController database knows most controllers; SDL_GAMECONTROLLERCONFIG
 * or SDL_GAMECONTROLLERCONFIG_FILE add mappings, as does a community
 * gamecontrollerdb.txt in pc/user/ (or at ROOD_GAMECONTROLLERDB). Gamepads
 * connect and disconnect at any time. */
#define _POSIX_C_SOURCE 200809L
#include "boot_controller.h"
#include <SDL3/SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Gamepad* pad;
static unsigned mask;
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

static void open_gamepad(SDL_JoystickID id)
{
    pad = SDL_OpenGamepad(id);
    if (!pad) {
        fprintf(stderr, "[controller] cannot open gamepad %u: %s\n", (unsigned)id, SDL_GetError());
        return;
    }
    const char* path = SDL_GetGamepadPath(pad);
    fprintf(stderr, "[controller] connected %s: %s (Circle/east confirms, Cross/south cancels)\n",
        path ? path : "gamepad", SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "gamepad");
}

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
        fprintf(stderr, "[controller] disconnected\n");
        open_first(event->gdevice.which);
    }
}

void pc_controller_poll(void)
{
    if (!pad) {
        mask = 0;
        return;
    }
    PcControllerState state;
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
        state.buttons[i] = SDL_GetGamepadButton(pad, (SDL_GamepadButton)i);
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++)
        state.axes[i] = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)i);
    mask = pc_controller_mask(&state);
}

unsigned pc_controller_buttons(void) { return mask; }
