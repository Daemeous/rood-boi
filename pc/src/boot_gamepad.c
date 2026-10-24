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
static int gamepad_inits; /* SDL_InitSubSystem(SDL_INIT_GAMEPAD) calls to undo (pc_controller_shutdown) */
static unsigned mask;
static int travel; /* the Travel slot's pad element held (fast travel) */
static int fast_forward; /* L3 + R3 held (boot_window.c) */
static unsigned state_actions; /* the save state actions' pad elements held (bit n: PC_BIND_STATE_FIRST + n) */
extern void pc_savestate_actions(unsigned bits) __attribute__((weak)); /* savestate_slots.c */
static int photo_action; /* the photo mode's pad element held (PC_BIND_PHOTO) */
extern void pc_photo_action(const char* name, const char* device) __attribute__((weak)); /* photo_input.cpp */
extern unsigned pc_vsync_count(void) __attribute__((weak)); /* boot_runtime.c */
static float right_stick[2]; /* -1..1, SDL's direction (down is +y) */
static float left_stick[2];  /* the same (photo.cpp's input capture reads it; the game's pad stays digital) */
static unsigned stick_only; /* pc_controller_stick_only */
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
    ++gamepad_inits;
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

/* Chords (boot_controller.h, pc_bind_pad_mod): per port action, its modifier's hold. */
static struct {
    int held;  /* the modifier is down */
    int wait;  /* polls its own PS1 buttons are still kept from the game, waiting for the element */
    int fired; /* the element was pressed with it: kept from the game until it is let go */
    int tap;   /* let go before the wait was over, unused: its PS1 buttons pressed (a tap) ... */
    unsigned tap_vsync; /* ... until the game has run 2 vsyncs from here (it reads the pad once a vsync; a
                         * paused game's redraws poll without reading), or 120 polls pass */
} chords[PC_BIND_SLOTS];
static unsigned vsync_now(void) { return pc_vsync_count ? pc_vsync_count() : 0; }
/* The PS1 buttons bound to `element`. */
static unsigned element_bits(int element)
{
    unsigned bits = 0;
    for (int i = 0; element != PC_PAD_NONE && i < PC_PAD_BUTTONS; i++)
        if (pc_bind_pad[i] == element)
            bits |= 1u << i;
    return bits;
}
/* A port action's binding pressed now (a chord: its element with the modifier held); *mask loses what the
 * game must not see of a chord and gains a modifier's late tap. */
static int action_held(const PcControllerState* state, int slot, unsigned* mask)
{
    int element = pc_bind_pad[slot], modifier = pc_bind_pad_mod[slot];
    if (element == PC_PAD_NONE)
        return 0;
    if (modifier == PC_PAD_NONE)
        return pc_controller_element(state, element);
    int m = pc_controller_element(state, modifier), e = pc_controller_element(state, element);
    if (m && !chords[slot].held) {
        chords[slot].held = 1;
        chords[slot].wait = e ? 0 : PC_CHORD_WAIT; /* (the element first: no chord, the modifier as it is) */
        chords[slot].fired = 0;
    } else if (!m && chords[slot].held) {
        chords[slot].held = 0;
        if (!chords[slot].fired && chords[slot].wait > 0) {
            chords[slot].tap = 120;
            chords[slot].tap_vsync = vsync_now();
        }
        chords[slot].wait = 0;
    }
    int pressed = m && e && (chords[slot].wait > 0 || chords[slot].fired);
    if (pressed)
        chords[slot].fired = 1;
    if (chords[slot].held && (chords[slot].wait > 0 || chords[slot].fired)) {
        *mask &= ~element_bits(modifier);
        if (e)
            *mask &= ~element_bits(element);
    }
    if (chords[slot].tap > 0) {
        *mask |= element_bits(modifier);
        if (--chords[slot].tap > 0 && vsync_now() - chords[slot].tap_vsync >= 2)
            chords[slot].tap = 0;
    }
    if (chords[slot].wait > 0)
        --chords[slot].wait;
    return pressed;
}

void pc_controller_poll(void)
{
    if (!pad) {
        mask = 0;
        travel = 0;
        state_actions = 0;
        photo_action = 0;
        fast_forward = 0;
        right_stick[0] = right_stick[1] = 0;
        left_stick[0] = left_stick[1] = 0;
        stick_only = 0;
        triggers[0] = triggers[1] = -1;
        return;
    }
    PcControllerState state;
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
        state.buttons[i] = SDL_GetGamepadButton(pad, (SDL_GamepadButton)i);
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++)
        state.axes[i] = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)i);
    unsigned game = pc_controller_mask(&state);
    {
        unsigned stick = pc_controller_stick_dpad(&state), elements = 0;
        for (int i = 0; i < PC_PAD_BUTTONS; ++i)
            if ((stick & (1u << i)) && pc_controller_element(&state, pc_bind_pad[i]))
                elements |= 1u << i;
        stick_only = stick & ~elements;
    }
    travel = action_held(&state, PC_BIND_TRAVEL, &game);
    /* The save state actions: on a press (savestate_slots.c). */
    unsigned held = 0;
    for (int s = PC_BIND_STATE_FIRST; s <= PC_BIND_STATE_LAST; s++)
        if (action_held(&state, s, &game))
            held |= 1u << (s - PC_BIND_STATE_FIRST);
    if (pc_savestate_actions && (held & ~state_actions))
        pc_savestate_actions(held & ~state_actions);
    state_actions = held;
    /* The photo mode's button: on a press, a Lua event (rood.photo; any input capture or none). */
    int photo = action_held(&state, PC_BIND_PHOTO, &game);
    /* Travel shares Back by default: it sees Back at once (fast travel's prompt is its own), but not once the
     * photo mode's chord fired with it. */
    if (pc_bind_pad_mod[PC_BIND_PHOTO] == pc_bind_pad[PC_BIND_TRAVEL] && chords[PC_BIND_PHOTO].held
        && chords[PC_BIND_PHOTO].fired)
        travel = 0;
    mask = game;
    if (photo && !photo_action && pc_photo_action)
        pc_photo_action("photo", "pad");
    photo_action = photo;
    /* Both stick clicks together: fast-forward while held, instead of
     * whatever PS1 buttons they are bound to (L3 and R3 by default; R3
     * resets the camera). */
    fast_forward = state.buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK] && state.buttons[SDL_GAMEPAD_BUTTON_RIGHT_STICK];
    if (fast_forward)
        for (int i = 0; i < PC_PAD_BUTTONS; i++)
            if (pc_bind_pad[i] == SDL_GAMEPAD_BUTTON_LEFT_STICK || pc_bind_pad[i] == SDL_GAMEPAD_BUTTON_RIGHT_STICK)
                mask &= ~(1u << i);
    left_stick[0] = state.axes[SDL_GAMEPAD_AXIS_LEFTX] / 32767.0f;
    left_stick[1] = state.axes[SDL_GAMEPAD_AXIS_LEFTY] / 32767.0f;
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
unsigned pc_controller_stick_only(void) { return stick_only & mask; }
/* The Travel slot's pad element held (fast travel). */
int pc_controller_travel(void) { return travel; }
int pc_controller_fast_forward(void) { return fast_forward; }
/* The right stick (the PS1 pad stays digital: the camera reads it,
 * camera_stick.c); 0 without a gamepad. */
/* L2 (0) or R2 (1) bound to an axis: how far past the press point it is
 * (0..1; the map's zoom, ui/map_bridge.c); -1: a button, or no gamepad. */
float pc_controller_trigger(int which) { return which == 0 || which == 1 ? triggers[which] : -1; }
void pc_controller_left_stick(float out[2])
{
    out[0] = left_stick[0] < -1 ? -1 : left_stick[0];
    out[1] = left_stick[1] < -1 ? -1 : left_stick[1];
}
void pc_controller_right_stick(float out[2])
{
    out[0] = right_stick[0] < -1 ? -1 : right_stick[0];
    out[1] = right_stick[1] < -1 ? -1 : right_stick[1];
}

/* Tests (RPC system {virtual_pad, down}): an SDL virtual gamepad (SDL's
 * standard layout) that becomes the gamepad when none is connected, so a
 * test presses a pad element through the same path a real pad takes
 * (pc_controller_poll, the bindings), unlike system {pad}, which only pushes
 * an event (the binding capture). 0 done, -1 refused (a real gamepad is in
 * use, SDL has no virtual joysticks, an unknown element). */
static SDL_Joystick* virtual_pad;
static SDL_JoystickID virtual_id;
int pc_controller_virtual_set(int element, int down)
{
    if (!virtual_pad) {
        if (pad || !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            return -1;
        ++gamepad_inits;
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "rood virtual gamepad (tests)";
        SDL_JoystickID id = virtual_id = SDL_AttachVirtualJoystick(&desc);
        if (!id || !(virtual_pad = SDL_OpenJoystick(id))) {
            fprintf(stderr, "[controller] virtual gamepad: %s\n", SDL_GetError());
            return -1;
        }
        open_gamepad(id);
        if (!pad)
            return -1;
        /* Virtual joystick axes are signed. SDL maps a trigger's full
         * -32768..32767 range to gamepad 0..32767: raw zero is half pressed,
         * not released. Initialise both endpoints before the first poll. */
        SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
        SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    }
    if (element >= PC_PAD_AXIS(0) && element - PC_PAD_AXIS(0) < SDL_GAMEPAD_AXIS_COUNT) {
        int axis = element - PC_PAD_AXIS(0);
        int rest = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? -32768 : 0;
        return SDL_SetJoystickVirtualAxis(virtual_pad, axis, (Sint16)(down ? 32767 : rest)) ? 0 : -1;
    }
    if (element >= 0 && element < SDL_GAMEPAD_BUTTON_COUNT)
        return SDL_SetJoystickVirtualButton(virtual_pad, element, down != 0) ? 0 : -1;
    return -1;
}

/* Before an engine reload (engine_reload.c): the gamepads this library
 * opened are closed and SDL's gamepad subsystem released; the next library
 * opens them again. */
void pc_controller_shutdown(void)
{
    if (pad)
        SDL_CloseGamepad(pad);
    pad = NULL;
    mask = 0;
    if (virtual_pad) {
        SDL_CloseJoystick(virtual_pad);
        SDL_DetachVirtualJoystick(virtual_id);
    }
    virtual_pad = NULL;
    for (; gamepad_inits > 0; gamepad_inits--)
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}
