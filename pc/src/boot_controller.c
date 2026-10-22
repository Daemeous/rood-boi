/* Gamepad and keyboard to PS1 pad mapping, and the bindings behind it. SDL
 * gamepads name buttons by position, never by device-specific index, so by
 * default east is Circle (the original game's confirm) and south is Cross
 * (cancel) on any controller. The settings rebind any PS1 button to another
 * key or gamepad element (bind_key_<button>, bind_pad_<button>); the button
 * prompts (button_prompts.c) show whatever is bound. No SDL calls here (the
 * controller probe links this alone). */
#include "boot_controller.h"
#include <stdio.h>
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
            { SDLK_LEFT }, { 0 } /* Travel: no key */,                                                  \
            { SDLK_F5 }, { SDLK_F9 }, { SDLK_F6 }, { SDLK_F7 }, /* save states: save, load, slot -, + */ \
            { SDLK_F12 } /* the photo mode (rood.photo) */                                        \
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
            SDL_GAMEPAD_BUTTON_DPAD_LEFT,                                                       \
            SDL_GAMEPAD_BUTTON_BACK /* Travel: shares Select's back */,                          \
            PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, /* save states: no button */      \
            SDL_GAMEPAD_BUTTON_DPAD_UP /* the photo mode: with Back held (DEFAULT_PAD_MOD) */     \
    }
/* Chords' modifiers: the photo mode is Select (Back) + D-pad up. Vagrant Story reads every PS1 button
 * (Select: the field camera's mode; L3 is unused but shares its stick with L3 + R3, fast-forward, and a
 * thumb with the D-pad; R3: the camera's reset) and the Share / Capture button is the system's on many
 * pads, so the photo mode takes a chord the game never sees (pc_controller_poll). */
#define DEFAULT_PAD_MOD                                                                                    \
    {                                                                                                      \
        PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, \
            PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE,      \
            PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE, PC_PAD_NONE,                   \
            SDL_GAMEPAD_BUTTON_BACK                                                                        \
    }
static const SDL_Keycode default_keys[PC_BIND_SLOTS][PC_BIND_KEYS] = DEFAULT_KEYS;
static const int default_pad_mod[PC_BIND_SLOTS] = DEFAULT_PAD_MOD;
int pc_bind_pad_mod[PC_BIND_SLOTS] = DEFAULT_PAD_MOD;
static const int default_pad[PC_BIND_SLOTS] = DEFAULT_PAD;
SDL_Keycode pc_bind_keys[PC_BIND_SLOTS][PC_BIND_KEYS] = DEFAULT_KEYS;
int pc_bind_pad[PC_BIND_SLOTS] = DEFAULT_PAD;
unsigned pc_bind_generation = 1;

void pc_bindings_reset(void)
{
    memcpy(pc_bind_keys, default_keys, sizeof pc_bind_keys);
    memcpy(pc_bind_pad, default_pad, sizeof pc_bind_pad);
    memcpy(pc_bind_pad_mod, default_pad_mod, sizeof pc_bind_pad_mod);
    ++pc_bind_generation;
}

unsigned pc_controller_mask(const PcControllerState* s)
{
    unsigned result = 0;
    for (int i = 0; i < PC_PAD_BUTTONS; ++i)
        if (pc_controller_element(s, pc_bind_pad[i]))
            result |= 1u << i;
    /* The left stick navigates like the D-pad. */
    return result | pc_controller_stick_dpad(s);
}

unsigned pc_controller_stick_dpad(const PcControllerState* s)
{
    unsigned result = 0;
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

/* The port's own actions' settings names, from PC_BIND_TRAVEL on. */
static const char* const action_names[PC_BIND_SLOTS - PC_PAD_BUTTONS] = { "travel", "state_save", "state_load",
    "state_slot_prev", "state_slot_next", "photo" };

const char* pc_bind_slot_name(int slot)
{
    return slot >= 0 && slot < PC_PAD_BUTTONS ? pc_pad_button_names[slot]
        : slot >= PC_PAD_BUTTONS && slot < PC_BIND_SLOTS ? action_names[slot - PC_PAD_BUTTONS]
                                                         : "";
}

int pc_bind_slot_index(const char* name)
{
    int i = pc_pad_button_index(name);
    for (int a = 0; i < 0 && a < PC_BIND_SLOTS - PC_PAD_BUTTONS; a++)
        if (!strcasecmp(name, action_names[a]))
            i = PC_PAD_BUTTONS + a;
    return i;
}

int pc_controller_element(const PcControllerState* s, int e)
{
    return e >= 0 && e < SDL_GAMEPAD_BUTTON_COUNT ? s->buttons[e] != 0
        : e >= PC_PAD_AXIS(0) && e < PC_PAD_AXIS(SDL_GAMEPAD_AXIS_COUNT)
        ? s->axes[e - PC_PAD_AXIS(0)] > PC_TRIGGER_PRESSED /* the triggers (L2 / R2 by default) */
        : 0;
}

int pc_keyboard_travel(SDL_Keycode key)
{
    return key && (pc_bind_keys[PC_BIND_TRAVEL][0] == key || pc_bind_keys[PC_BIND_TRAVEL][1] == key);
}

unsigned pc_keyboard_state_actions(SDL_Keycode key)
{
    unsigned bits = 0;
    for (int s = PC_BIND_STATE_FIRST; key && s <= PC_BIND_STATE_LAST; s++)
        if (pc_bind_keys[s][0] == key || pc_bind_keys[s][1] == key)
            bits |= 1u << (s - PC_BIND_STATE_FIRST);
    return bits;
}

int pc_keyboard_photo(SDL_Keycode key)
{
    return key && (pc_bind_keys[PC_BIND_PHOTO][0] == key || pc_bind_keys[PC_BIND_PHOTO][1] == key);
}

/* The PS1 buttons that share one of `slot`'s keys or its pad element: a
 * press of the slot presses them too (the Travel action takes them out of
 * that frame when it fires). */
unsigned pc_bind_shared_bits(int slot)
{
    unsigned bits = 0;
    if (slot < 0 || slot >= PC_BIND_SLOTS)
        return 0;
    for (int i = 0; i < PC_PAD_BUTTONS; i++) {
        int shared = i != slot && pc_bind_pad[slot] != PC_PAD_NONE && pc_bind_pad[i] == pc_bind_pad[slot];
        for (int k = 0; k < PC_BIND_KEYS; k++)
            for (int m = 0; m < PC_BIND_KEYS; m++)
                if (i != slot && pc_bind_keys[slot][k] && pc_bind_keys[slot][k] == pc_bind_keys[i][m])
                    shared = 1;
        if (shared)
            bits |= 1u << i;
    }
    return bits;
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
    { "righttrigger", PC_PAD_AXIS(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) },
    /* the left stick pushed right / down (tests: RPC system {virtual_pad}) */
    { "leftx", PC_PAD_AXIS(SDL_GAMEPAD_AXIS_LEFTX) }, { "lefty", PC_PAD_AXIS(SDL_GAMEPAD_AXIS_LEFTY) },
    { "none", PC_PAD_NONE },
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

/* "element" or "modifier+element" (the settings' form). */
const char* pc_bind_pad_text(int slot, char* out, size_t size)
{
    if (slot < 0 || slot >= PC_BIND_SLOTS)
        return "none";
    if (pc_bind_pad_mod[slot] == PC_PAD_NONE || pc_bind_pad[slot] == PC_PAD_NONE)
        snprintf(out, size, "%s", pc_pad_element_name(pc_bind_pad[slot]));
    else
        snprintf(out, size, "%s+%s", pc_pad_element_name(pc_bind_pad_mod[slot]), pc_pad_element_name(pc_bind_pad[slot]));
    return out;
}

/* ---- settings ---- */

int pc_binding_setting(const char* key, const char* value)
{
    int pad = !strncmp(key, "bind_pad_", 9);
    if (!pad && strncmp(key, "bind_key_", 9))
        return 0;
    int button = pc_bind_slot_index(key + 9);
    if (button < 0) {
        fprintf(stderr, "[settings] %s: unknown PS1 button or action (%s, travel, state_save, ...)\n", key,
            pc_pad_button_names[5]);
        return 1;
    }
    char text[96] = "";
    sscanf(value, " %95[^\r\n]", text);
    if (pad) {
        /* "element", or a chord "modifier+element" (the port's actions only). */
        char name[64] = "", *plus;
        sscanf(text, "%63s", name);
        int modifier = PC_PAD_NONE;
        if ((plus = strchr(name, '+'))) {
            *plus = 0;
            modifier = pc_pad_element_from_name(name);
            memmove(name, plus + 1, strlen(plus + 1) + 1);
            if (button < PC_PAD_BUTTONS || modifier == -2 || modifier == PC_PAD_NONE) {
                fprintf(stderr, "[settings] %s: a chord needs a port action and a known modifier (back+dpup)\n", key);
                return 1;
            }
        }
        int element = pc_pad_element_from_name(name);
        if (element == -2)
            fprintf(stderr, "[settings] %s: unknown gamepad element '%s'\n", key, name);
        else {
            pc_bind_pad[button] = element;
            pc_bind_pad_mod[button] = element == PC_PAD_NONE || element == modifier ? PC_PAD_NONE : modifier;
        }
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
    for (int i = 0; i < PC_BIND_SLOTS; i++) {
        if (memcmp(pc_bind_keys[i], default_keys[i], sizeof default_keys[i])) {
            fprintf(f, "bind_key_%s", pc_bind_slot_name(i));
            int any = 0;
            for (int k = 0; k < PC_BIND_KEYS; k++) {
                const char* name = pc_bind_keys[i][k] ? pc_key_name(pc_bind_keys[i][k]) : NULL;
                if (name)
                    fprintf(f, "%s%s", any++ ? "," : " ", name);
            }
            fprintf(f, "%s\n", any ? "" : " none");
        }
        if (pc_bind_pad[i] != default_pad[i] || pc_bind_pad_mod[i] != default_pad_mod[i]) {
            char chord[64];
            fprintf(f, "bind_pad_%s %s\n", pc_bind_slot_name(i), pc_bind_pad_text(i, chord, sizeof chord));
        }
    }
}
