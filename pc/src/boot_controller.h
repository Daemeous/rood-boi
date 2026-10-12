#pragma once
#include <SDL3/SDL_gamepad.h>
/* A gamepad in SDL's positional layout (south/east/west/north face buttons,
 * whatever the labels), as read from the GameController database mapping. */
typedef struct {
    unsigned char buttons[SDL_GAMEPAD_BUTTON_COUNT];
    short axes[SDL_GAMEPAD_AXIS_COUNT];
} PcControllerState;
/* boot_controller.c: PS1 pad bits for a gamepad state (no SDL calls). */
unsigned pc_controller_mask(const PcControllerState*);
/* boot_gamepad.c: SDL gamepad discovery, hot-plug and polling. */
union SDL_Event;
void pc_controller_init(void);
void pc_controller_event(const union SDL_Event*);
void pc_controller_poll(void);
unsigned pc_controller_buttons(void);
void pc_controller_right_stick(float out[2]);
