/* Gamepad to PS1 pad mapping. SDL gamepads name buttons by position, never
 * by device-specific index, so east is Circle (the original game's confirm)
 * and south is Cross (cancel) on any controller. */
#include "boot_controller.h"

static const unsigned button_bits[SDL_GAMEPAD_BUTTON_COUNT] = {
    [SDL_GAMEPAD_BUTTON_SOUTH] = 1u << 6, /* Cross */
    [SDL_GAMEPAD_BUTTON_EAST] = 1u << 5, /* Circle, original game's confirm */
    [SDL_GAMEPAD_BUTTON_WEST] = 1u << 7, /* Square */
    [SDL_GAMEPAD_BUTTON_NORTH] = 1u << 4, /* Triangle */
    [SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = 1u << 2,
    [SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = 1u << 3,
    /* Second pad byte: Select, L3, R3, Start (libetc PADselect = 1 << 8). */
    [SDL_GAMEPAD_BUTTON_BACK] = 1u << 8,
    [SDL_GAMEPAD_BUTTON_LEFT_STICK] = 1u << 9,
    [SDL_GAMEPAD_BUTTON_RIGHT_STICK] = 1u << 10,
    [SDL_GAMEPAD_BUTTON_START] = 1u << 11,
    [SDL_GAMEPAD_BUTTON_DPAD_UP] = 1u << 12,
    [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = 1u << 13,
    [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = 1u << 14,
    [SDL_GAMEPAD_BUTTON_DPAD_LEFT] = 1u << 15,
};

unsigned pc_controller_mask(const PcControllerState* s)
{
    unsigned result = 0;
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
        if (s->buttons[i])
            result |= button_bits[i];
    /* The left stick navigates like the D-pad; triggers are L2/R2. */
    int x = s->axes[SDL_GAMEPAD_AXIS_LEFTX], y = s->axes[SDL_GAMEPAD_AXIS_LEFTY];
    if (x < -12000)
        result |= 1u << 15;
    if (x > 12000)
        result |= 1u << 13;
    if (y < -12000)
        result |= 1u << 12;
    if (y > 12000)
        result |= 1u << 14;
    if (s->axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] > 8000)
        result |= 1u;
    if (s->axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] > 8000)
        result |= 1u << 1;
    return result;
}
