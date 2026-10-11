#include "boot_controller.h"
#include <stdio.h>
#include <string.h>
#ifdef ROOD_CONTROLLER_HARDWARE
#include <SDL3/SDL.h>
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while(0)
int main(int argc, char** argv)
{
#ifdef ROOD_CONTROLLER_HARDWARE
    /* --hardware: report the gamepad SDL detects and its current PS1 buttons. */
    if (argc > 1 && !strcmp(argv[1], "--hardware")) {
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
        if (!SDL_Init(0)) {
            fprintf(stderr, "SDL: %s\n", SDL_GetError());
            return 1;
        }
        pc_controller_init();
        for (int i = 0; i < 50; i++) {
            SDL_Event event;
            while (SDL_PollEvent(&event))
                pc_controller_event(&event);
            SDL_Delay(20);
        }
        pc_controller_poll();
        printf("Current PS1 buttons: %04x\n", pc_controller_buttons());
        SDL_Quit();
        return 0;
    }
#else
    (void)argc, (void)argv;
#endif
    PcControllerState s;
    memset(&s, 0, sizeof(s));
    /* Positional buttons: east confirms (Circle), south cancels (Cross). */
    s.buttons[SDL_GAMEPAD_BUTTON_EAST] = 1;
    CHECK(pc_controller_mask(&s) == (1u << 5));
    s.buttons[SDL_GAMEPAD_BUTTON_SOUTH] = 1;
    CHECK(pc_controller_mask(&s) == ((1u << 5) | (1u << 6)));
    s.buttons[SDL_GAMEPAD_BUTTON_EAST] = 0;
    CHECK(pc_controller_mask(&s) == (1u << 6));
    s.buttons[SDL_GAMEPAD_BUTTON_SOUTH] = 0;
    s.buttons[SDL_GAMEPAD_BUTTON_WEST] = s.buttons[SDL_GAMEPAD_BUTTON_NORTH] = 1;
    CHECK(pc_controller_mask(&s) == ((1u << 7) | (1u << 4)));
    memset(&s, 0, sizeof(s));
    s.buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = s.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = 1;
    s.buttons[SDL_GAMEPAD_BUTTON_BACK] = s.buttons[SDL_GAMEPAD_BUTTON_START] = 1;
    s.buttons[SDL_GAMEPAD_BUTTON_LEFT_STICK] = s.buttons[SDL_GAMEPAD_BUTTON_RIGHT_STICK] = 1;
    CHECK(pc_controller_mask(&s) == 0xf0c);
    memset(&s, 0, sizeof(s));
    /* Left stick dead zone, then navigation. */
    s.axes[SDL_GAMEPAD_AXIS_LEFTX] = 11999;
    CHECK(pc_controller_mask(&s) == 0);
    s.axes[SDL_GAMEPAD_AXIS_LEFTX] = -32767;
    CHECK(pc_controller_mask(&s) == (1u << 15));
    s.buttons[SDL_GAMEPAD_BUTTON_DPAD_LEFT] = 1;
    s.axes[SDL_GAMEPAD_AXIS_LEFTX] = 0;
    CHECK(pc_controller_mask(&s) == (1u << 15)); /* releasing the stick must not release the D-pad */
    s.buttons[SDL_GAMEPAD_BUTTON_DPAD_LEFT] = 0;
    s.axes[SDL_GAMEPAD_AXIS_LEFTY] = -20000;
    CHECK(pc_controller_mask(&s) == (1u << 12));
    s.axes[SDL_GAMEPAD_AXIS_LEFTY] = 20000;
    CHECK(pc_controller_mask(&s) == (1u << 14));
    s.axes[SDL_GAMEPAD_AXIS_LEFTY] = 0;
    s.buttons[SDL_GAMEPAD_BUTTON_DPAD_UP] = s.buttons[SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = 1;
    s.buttons[SDL_GAMEPAD_BUTTON_DPAD_DOWN] = 1;
    CHECK(pc_controller_mask(&s) == ((1u << 12) | (1u << 13) | (1u << 14)));
    memset(&s, 0, sizeof(s));
    /* Triggers: L2/R2 past a threshold; the right stick is unused. */
    s.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 8000;
    s.axes[SDL_GAMEPAD_AXIS_RIGHTX] = -32768;
    CHECK(pc_controller_mask(&s) == 0);
    s.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 32767;
    CHECK(pc_controller_mask(&s) == 2);
    s.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 32767;
    CHECK(pc_controller_mask(&s) == 3);
    /* Unmapped buttons (guide, paddles, touchpad) press nothing. */
    memset(&s, 0, sizeof(s));
    s.buttons[SDL_GAMEPAD_BUTTON_GUIDE] = s.buttons[SDL_GAMEPAD_BUTTON_TOUCHPAD] = 1;
    s.buttons[SDL_GAMEPAD_BUTTON_LEFT_PADDLE1] = s.buttons[SDL_GAMEPAD_BUTTON_MISC1] = 1;
    CHECK(pc_controller_mask(&s) == 0);
    puts("PASS: positional gamepad mapping (east confirms, south cancels), shoulders, Start/Select, "
         "stick clicks, stick dead zone, D-pad/stick overlap, triggers");
    return 0;
}
