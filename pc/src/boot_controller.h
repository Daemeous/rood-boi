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
/* The D-pad bits the left stick gives (pc_controller_mask has them too). */
unsigned pc_controller_stick_dpad(const PcControllerState*);

/* Bindings (boot_controller.c, settings bind_key_<button> / bind_pad_<button>):
 * which key and which gamepad element press each PS1 button. PS1 buttons
 * are libetc pad bits 0..15 (L2 R2 L1 R1 Triangle Circle Cross Square Select
 * L3 R3 Start Up Right Down Left); their names are pc_pad_button_names. */
#define PC_PAD_BUTTONS 16
/* Binding slots: the 16 PS1 buttons, then the port's own actions, which never
 * reach the game's pad word: PC_BIND_TRAVEL, fast travel's Travel button
 * (fast_travel.c; settings bind_key_travel, bind_pad_travel); the save state
 * slots' save, load, previous and next slot (savestate_slots.c, with the
 * setting save_states on; bind_key_state_save, _state_load,
 * _state_slot_prev, _state_slot_next, and their bind_pad_); the photo mode's
 * button, PC_BIND_PHOTO (a Lua event "input.action" {name "photo"}: the
 * shipped plugin rood.photo enters and leaves the photo mode on it;
 * bind_key_photo F12, bind_pad_photo back+dpup: Select held, then D-pad up).
 * A pad binding may be a chord, "modifier+element" (pc_bind_pad_mod): the
 * element fires while the modifier is held. The game never sees a chord:
 * the element's PS1 buttons are taken out while the modifier is held, and the
 * modifier's own PS1 buttons (Select: the field camera's mode) wait
 * PC_CHORD_WAIT polls for the element (pressed after them; a tap reaches the
 * game when it is let go, for 2 of the game's vsyncs), so normal play never fires it. */
#define PC_BIND_TRAVEL 16
#define PC_BIND_STATE_SAVE 17
#define PC_BIND_STATE_LOAD 18
#define PC_BIND_STATE_SLOT_PREV 19
#define PC_BIND_STATE_SLOT_NEXT 20
#define PC_BIND_STATE_FIRST PC_BIND_STATE_SAVE
#define PC_BIND_STATE_LAST PC_BIND_STATE_SLOT_NEXT
#define PC_BIND_PHOTO 21
#define PC_BIND_REPLAY 22 /* the cutscene player's instant replay (bind_key_replay F8; no pad element by default) */
#define PC_BIND_SLOTS 23
#define PC_BIND_KEYS 2              /* keys per PS1 button */
#define PC_PAD_AXIS(axis) (100 + (axis)) /* a gamepad element that is an axis (the triggers) */
#define PC_PAD_NONE (-1)
/* A trigger bound to a PS1 button presses it past PC_TRIGGER_PRESSED (of
 * 32767); a binding capture takes a trigger past half way and gives it back
 * to the game once it is at or below PC_TRIGGER_PRESSED again (boot_window.c). */
#define PC_TRIGGER_PRESSED 8000
#define PC_TRIGGER_CAPTURE 16384
extern const char* const pc_pad_button_names[PC_PAD_BUTTONS];
extern SDL_Keycode pc_bind_keys[PC_BIND_SLOTS][PC_BIND_KEYS]; /* 0: none */
extern int pc_bind_pad[PC_BIND_SLOTS]; /* SDL_GamepadButton, PC_PAD_AXIS(trigger) or PC_PAD_NONE */
/* A chord's modifier (held with pc_bind_pad's element), PC_PAD_NONE: a single element. The port's actions
 * only (Travel, save states, the photo mode); a binding capture binds a single element. */
extern int pc_bind_pad_mod[PC_BIND_SLOTS];
#define PC_CHORD_WAIT 15 /* polls (vsyncs, ~250 ms) a chord's modifier is kept from the game, waiting */
/* Bumped whenever a binding changes (button prompts follow). */
extern unsigned pc_bind_generation;
/* The PS1 bits a key presses (0: not bound). */
unsigned pc_keyboard_bits(SDL_Keycode key);
/* PS1 button index for a name ("circle", "l1", ...), -1 if none. */
int pc_pad_button_index(const char* name);
/* A slot's settings name: pc_pad_button_names, then "travel", "state_save",
 * "state_load", "state_slot_prev", "state_slot_next", "photo", "replay". */
const char* pc_bind_slot_name(int slot);
/* A slot for a settings name (a PS1 button or an action), -1 if none. */
int pc_bind_slot_index(const char* name);
/* Whether gamepad element `element` is pressed in `s` (a trigger past PC_TRIGGER_PRESSED). */
int pc_controller_element(const PcControllerState* s, int element);
/* Whether `key` is bound to the Travel action. */
int pc_keyboard_travel(SDL_Keycode key);
/* The save state actions `key` is bound to: bit n for slot PC_BIND_STATE_FIRST + n. */
unsigned pc_keyboard_state_actions(SDL_Keycode key);
/* A slot's pad binding as the settings write it: "dpup", "back+dpup". */
const char* pc_bind_pad_text(int slot, char* out, size_t size);
/* Whether `key` is bound to the photo mode's button. */
int pc_keyboard_photo(SDL_Keycode key);
/* Whether `key` is bound to the instant replay (cutscene.cpp). */
int pc_keyboard_replay(SDL_Keycode key);
/* The PS1 buttons bound to one of slot's keys or to its pad element (pressed with it). */
unsigned pc_bind_shared_bits(int slot);
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
/* Of pc_controller_buttons: the D-pad bits only the left stick gives (no D-pad element bound to them
 * is held): rood.input's events leave them out, the stick being the photo camera's. */
unsigned pc_controller_stick_only(void);
int pc_controller_fast_forward(void); /* both stick clicks held: fast-forward (boot_window.c) */
void pc_controller_right_stick(float out[2]);
void pc_controller_left_stick(float out[2]); /* -1..1 (down +y); 0 without a gamepad */
/* L2 (0) / R2 (1) bound to an axis: its travel past the press point (0..1); -1 a button or no gamepad. */
float pc_controller_trigger(int which);
/* The open gamepad's SDL_GamepadType (SDL_GAMEPAD_TYPE_UNKNOWN: none). */
int pc_controller_type(void);
int pc_controller_travel(void); /* the Travel slot's pad element is held */
/* Tests: an SDL virtual gamepad's element pressed or released (attached as the
 * gamepad on first use when none is connected); 0 done, -1 refused. */
int pc_controller_virtual_set(int element, int down);
