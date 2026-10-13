/* Gamepad and keyboard to PS1 pad mapping, and the bindings behind it. SDL
 * gamepads name buttons by position, never by device-specific index, so by
 * default east is Circle (the original game's confirm) and south is Cross
 * (cancel) on any controller. The settings rebind any PS1 button to another
 * key or gamepad element (bind_key_<button>, bind_pad_<button>); the button
 * prompts (button_prompts.c) show whatever is bound. No SDL calls here (the
 * controller probe links this alone). */
#include "boot_controller.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char* const pc_pad_button_names[PC_PAD_BUTTONS] = { "l2", "r2", "l1", "r1", "triangle", "circle", "cross",
    "square", "select", "l3", "r3", "start", "up", "right", "down", "left" };

/* The defaults (README "Controls"). */
#define DEFAULT_KEYS                                                                                    \
    {                                                                                                   \
        { SDLK_1 }, { SDLK_2 }, { SDLK_Q }, { SDLK_W }, { SDLK_S }, { SDLK_Z }, { SDLK_X }, { SDLK_A }, \
            { SDLK_TAB }, { 0 }, { 0 }, { SDLK_RETURN }, { SDLK_UP }, { SDLK_RIGHT }, { SDLK_DOWN },    \
            { SDLK_LEFT }                                                                               \
    }
#define DEFAULT_PAD                                                                             \
    {                                                                                           \
        PC_PAD_AXIS(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), PC_PAD_AXIS(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), \
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,                \
            SDL_GAMEPAD_BUTTON_NORTH, /* Triangle */                                            \
            SDL_GAMEPAD_BUTTON_EAST,  /* Circle, the original game's confirm */                 \
            SDL_GAMEPAD_BUTTON_SOUTH, /* Cross */                                               \
            SDL_GAMEPAD_BUTTON_WEST,  /* Square */                                              \
            SDL_GAMEPAD_BUTTON_BACK,  /* Select (libetc PADselect = 1 << 8) */                  \
            SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_START, \
            SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN, \
            SDL_GAMEPAD_BUTTON_DPAD_LEFT                                                        \
    }
static const SDL_Keycode default_keys[PC_PAD_BUTTONS][PC_BIND_KEYS] = DEFAULT_KEYS;
static const int default_pad[PC_PAD_BUTTONS] = DEFAULT_PAD;
SDL_Keycode pc_bind_keys[PC_PAD_BUTTONS][PC_BIND_KEYS] = DEFAULT_KEYS;
int pc_bind_pad[PC_PAD_BUTTONS] = DEFAULT_PAD;
unsigned pc_bind_generation = 1;

void pc_bindings_reset(void)
{
    memcpy(pc_bind_keys, default_keys, sizeof pc_bind_keys);
    memcpy(pc_bind_pad, default_pad, sizeof pc_bind_pad);
    ++pc_bind_generation;
}

unsigned pc_controller_mask(const PcControllerState* s)
{
    unsigned result = 0;
    for (int i = 0; i < PC_PAD_BUTTONS; ++i) {
        int e = pc_bind_pad[i];
        if (e >= 0 && e < SDL_GAMEPAD_BUTTON_COUNT ? s->buttons[e] != 0
            : e >= PC_PAD_AXIS(0) && e < PC_PAD_AXIS(SDL_GAMEPAD_AXIS_COUNT)
                ? s->axes[e - PC_PAD_AXIS(0)] > 8000 /* the triggers (L2 / R2 by default) */
                : 0)
            result |= 1u << i;
    }
    /* The left stick navigates like the D-pad. */
    int x = s->axes[SDL_GAMEPAD_AXIS_LEFTX], y = s->axes[SDL_GAMEPAD_AXIS_LEFTY];
    if (x < -12000)
        result |= 1u << 15;
    if (x > 12000)
        result |= 1u << 13;
    if (y < -12000)
        result |= 1u << 12;
    if (y > 12000)
        result |= 1u << 14;
    return result;
}

unsigned pc_keyboard_bits(SDL_Keycode key)
{
    unsigned bits = 0;
    if (!key)
        return 0;
    for (int i = 0; i < PC_PAD_BUTTONS; i++)
        for (int k = 0; k < PC_BIND_KEYS; k++)
            if (pc_bind_keys[i][k] == key)
                bits |= 1u << i;
    return bits;
}

int pc_pad_button_index(const char* name)
{
    for (int i = 0; i < PC_PAD_BUTTONS; i++)
        if (!strcasecmp(name, pc_pad_button_names[i]))
            return i;
    return -1;
}

/* ---- names ---- */

static const struct {
    const char* name;
    SDL_Keycode key;
} key_names[] = {
    { "return", SDLK_RETURN }, { "enter", SDLK_RETURN }, { "tab", SDLK_TAB }, { "space", SDLK_SPACE },
    { "escape", SDLK_ESCAPE }, { "backspace", SDLK_BACKSPACE }, { "up", SDLK_UP }, { "down", SDLK_DOWN },
    { "left", SDLK_LEFT }, { "right", SDLK_RIGHT }, { "lshift", SDLK_LSHIFT }, { "rshift", SDLK_RSHIFT },
    { "lctrl", SDLK_LCTRL }, { "rctrl", SDLK_RCTRL }, { "lalt", SDLK_LALT }, { "ralt", SDLK_RALT },
    { "capslock", SDLK_CAPSLOCK }, { "insert", SDLK_INSERT }, { "delete", SDLK_DELETE }, { "home", SDLK_HOME },
    { "end", SDLK_END }, { "pageup", SDLK_PAGEUP }, { "pagedown", SDLK_PAGEDOWN }, { "minus", SDLK_MINUS },
    { "equals", SDLK_EQUALS }, { "comma", SDLK_COMMA }, { "period", SDLK_PERIOD }, { "slash", SDLK_SLASH },
    { "backslash", SDLK_BACKSLASH }, { "semicolon", SDLK_SEMICOLON }, { "apostrophe", SDLK_APOSTROPHE },
    { "leftbracket", SDLK_LEFTBRACKET }, { "rightbracket", SDLK_RIGHTBRACKET }, { "grave", SDLK_GRAVE },
    { "kp_enter", SDLK_KP_ENTER }, { "kp_plus", SDLK_KP_PLUS }, { "f1", SDLK_F1 }, { "f2", SDLK_F2 },
    { "f3", SDLK_F3 }, { "f4", SDLK_F4 }, { "f5", SDLK_F5 }, { "f6", SDLK_F6 }, { "f7", SDLK_F7 },
    { "f8", SDLK_F8 }, { "f9", SDLK_F9 }, { "f10", SDLK_F10 }, { "f11", SDLK_F11 }, { "f12", SDLK_F12 },
};

SDL_Keycode pc_key_from_name(const char* name)
{
    if (!name[0])
        return 0;
    if (!name[1] && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z')))
        return (SDL_Keycode)(name[0] | 0x20); /* SDLK_A.. are the lower-case letters */
    if (!name[1] && name[0] >= '0' && name[0] <= '9')
        return (SDL_Keycode)name[0];
    for (size_t i = 0; i < sizeof key_names / sizeof key_names[0]; i++)
        if (!strcasecmp(name, key_names[i].name))
            return key_names[i].key;
    return 0;
}

const char* pc_key_name(SDL_Keycode key)
{
    static char letter[2];
    if ((key >= 'a' && key <= 'z') || (key >= '0' && key <= '9')) {
        letter[0] = (char)key, letter[1] = 0;
        return letter;
    }
    for (size_t i = 0; i < sizeof key_names / sizeof key_names[0]; i++)
        if (key_names[i].key == key)
            return key_names[i].name;
    return NULL;
}

/* SDL's own gamepad names (SDL_GetGamepadStringForButton), positional
 * aliases first. */
static const struct {
    const char* name;
    int element;
} pad_names[] = {
    { "south", SDL_GAMEPAD_BUTTON_SOUTH }, { "east", SDL_GAMEPAD_BUTTON_EAST }, { "west", SDL_GAMEPAD_BUTTON_WEST },
    { "north", SDL_GAMEPAD_BUTTON_NORTH }, { "back", SDL_GAMEPAD_BUTTON_BACK }, { "guide", SDL_GAMEPAD_BUTTON_GUIDE },
    { "start", SDL_GAMEPAD_BUTTON_START }, { "leftstick", SDL_GAMEPAD_BUTTON_LEFT_STICK },
    { "rightstick", SDL_GAMEPAD_BUTTON_RIGHT_STICK }, { "leftshoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
    { "rightshoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER }, { "dpup", SDL_GAMEPAD_BUTTON_DPAD_UP },
    { "dpdown", SDL_GAMEPAD_BUTTON_DPAD_DOWN }, { "dpleft", SDL_GAMEPAD_BUTTON_DPAD_LEFT },
    { "dpright", SDL_GAMEPAD_BUTTON_DPAD_RIGHT }, { "misc1", SDL_GAMEPAD_BUTTON_MISC1 },
    { "paddle1", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 }, { "paddle2", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1 },
    { "paddle3", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2 }, { "paddle4", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2 },
    { "touchpad", SDL_GAMEPAD_BUTTON_TOUCHPAD }, { "lefttrigger", PC_PAD_AXIS(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) },
    { "righttrigger", PC_PAD_AXIS(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) }, { "none", PC_PAD_NONE },
};

int pc_pad_element_from_name(const char* name)
{
    for (size_t i = 0; i < sizeof pad_names / sizeof pad_names[0]; i++)
        if (!strcasecmp(name, pad_names[i].name))
            return pad_names[i].element;
    return -2;
}

const char* pc_pad_element_name(int element)
{
    for (size_t i = 0; i < sizeof pad_names / sizeof pad_names[0]; i++)
        if (pad_names[i].element == element)
            return pad_names[i].name;
    return "none";
}

/* ---- settings ---- */

int pc_binding_setting(const char* key, const char* value)
{
    int pad = !strncmp(key, "bind_pad_", 9);
    if (!pad && strncmp(key, "bind_key_", 9))
        return 0;
    int button = pc_pad_button_index(key + 9);
    if (button < 0) {
        fprintf(stderr, "[settings] %s: unknown PS1 button (%s, ...)\n", key, pc_pad_button_names[5]);
        return 1;
    }
    char text[96] = "";
    sscanf(value, " %95[^\r\n]", text);
    if (pad) {
        char name[32] = "";
        sscanf(text, "%31s", name);
        int element = pc_pad_element_from_name(name);
        if (element == -2)
            fprintf(stderr, "[settings] %s: unknown gamepad element '%s'\n", key, name);
        else
            pc_bind_pad[button] = element;
    } else {
        /* One or two keys, comma-separated; "none" unbinds. */
        SDL_Keycode keys[PC_BIND_KEYS] = { 0 };
        int count = 0;
        for (char* part = strtok(text, ", \t"); part && count < PC_BIND_KEYS; part = strtok(NULL, ", \t")) {
            if (!strcasecmp(part, "none"))
                continue;
            SDL_Keycode k = pc_key_from_name(part);
            if (k)
                keys[count++] = k;
            else
                fprintf(stderr, "[settings] %s: unknown key '%s'\n", key, part);
        }
        memcpy(pc_bind_keys[button], keys, sizeof keys);
    }
    ++pc_bind_generation;
    return 1;
}

void pc_binding_settings_write(FILE* f)
{
    /* Only what differs from the defaults. */
    for (int i = 0; i < PC_PAD_BUTTONS; i++) {
        if (memcmp(pc_bind_keys[i], default_keys[i], sizeof default_keys[i])) {
            fprintf(f, "bind_key_%s", pc_pad_button_names[i]);
            int any = 0;
            for (int k = 0; k < PC_BIND_KEYS; k++) {
                const char* name = pc_bind_keys[i][k] ? pc_key_name(pc_bind_keys[i][k]) : NULL;
                if (name)
                    fprintf(f, "%s%s", any++ ? "," : " ", name);
            }
            fprintf(f, "%s\n", any ? "" : " none");
        }
        if (pc_bind_pad[i] != default_pad[i])
            fprintf(f, "bind_pad_%s %s\n", pc_pad_button_names[i], pc_pad_element_name(pc_bind_pad[i]));
    }
}
