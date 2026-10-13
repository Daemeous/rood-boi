#pragma once
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>
#include <stdio.h>
/* A gamepad in SDL's positional layout (south/east/west/north face buttons,
 * whatever the labels), as read from the GameController database mapping. */
typedef struct {
    unsigned char buttons[SDL_GAMEPAD_BUTTON_COUNT];
    short axes[SDL_GAMEPAD_AXIS_COUNT];
} PcControllerState;
/* boot_controller.c: PS1 pad bits for a gamepad state (no SDL calls). */
unsigned pc_controller_mask(const PcControllerState*);

/* Bindings (boot_controller.c, settings bind_key_<button> / bind_pad_<button>):
 * which key and which gamepad element press each PS1 button. PS1 buttons
 * are libetc pad bits 0..15 (L2 R2 L1 R1 Triangle Circle Cross Square Select
 * L3 R3 Start Up Right Down Left); their names are pc_pad_button_names. */
#define PC_PAD_BUTTONS 16
#define PC_BIND_KEYS 2              /* keys per PS1 button */
#define PC_PAD_AXIS(axis) (100 + (axis)) /* a gamepad element that is an axis (the triggers) */
#define PC_PAD_NONE (-1)
/* A trigger bound to a PS1 button presses it past PC_TRIGGER_PRESSED (of
 * 32767); a binding capture takes a trigger past half way and gives it back
 * to the game once it is at or below PC_TRIGGER_PRESSED again (boot_window.c). */
#define PC_TRIGGER_PRESSED 8000
#define PC_TRIGGER_CAPTURE 16384
extern const char* const pc_pad_button_names[PC_PAD_BUTTONS];
extern SDL_Keycode pc_bind_keys[PC_PAD_BUTTONS][PC_BIND_KEYS]; /* 0: none */
extern int pc_bind_pad[PC_PAD_BUTTONS]; /* SDL_GamepadButton, PC_PAD_AXIS(trigger) or PC_PAD_NONE */
/* Bumped whenever a binding changes (button prompts follow). */
extern unsigned pc_bind_generation;
/* The PS1 bits a key presses (0: not bound). */
unsigned pc_keyboard_bits(SDL_Keycode key);
/* PS1 button index for a name ("circle", "l1", ...), -1 if none. */
int pc_pad_button_index(const char* name);
/* Key and gamepad element names as in the settings ("z", "return", "f1";
 * "south", "east", "leftshoulder", "lefttrigger", "dpup", ...). */
SDL_Keycode pc_key_from_name(const char* name);
const char* pc_key_name(SDL_Keycode key); /* NULL: no name for it */
int pc_pad_element_from_name(const char* name);
const char* pc_pad_element_name(int element);
/* Settings lines: 1 when `key` was a binding. */
int pc_binding_setting(const char* key, const char* value);
void pc_binding_settings_write(FILE* f);
void pc_bindings_reset(void);

/* boot_gamepad.c: SDL gamepad discovery, hot-plug and polling. */
union SDL_Event;
void pc_controller_init(void);
void pc_controller_event(const union SDL_Event*);
void pc_controller_poll(void);
unsigned pc_controller_buttons(void);
int pc_controller_fast_forward(void); /* both stick clicks held: fast-forward (boot_window.c) */
void pc_controller_right_stick(float out[2]);
/* The open gamepad's SDL_GamepadType (SDL_GAMEPAD_TYPE_UNKNOWN: none). */
int pc_controller_type(void);
