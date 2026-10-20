// What the ui_settings probe's stubs recorded (ui_settings_stubs.cpp).
#pragma once
struct Stub {
    int saves = 0;
    int set_scale_calls = 0, last_scale = 0;
    int lighting_calls = 0;
    char last_lighting[64] = "";
    int msaa_max = 4;
    int traced = 0, vulkan = 0;
    // game options (choice indices, game_options.h order): Frame map, timing on,
    // weapon/armor status on, cursor memory on, information on, puzzle mode on,
    // stereo, vibration on
    int game[9] = {0, 1, 1, 1, 1, 1, 1, 0, 1};
    int game_sets = 0;
    int puzzle_disabled = 0;
    int window_mode = 0, volume = 100, brightness = 50, captures = 0;
    char fullscreen_mode[32] = "desktop"; // settings fullscreen_mode (the hooks' modes: desktop, 1920x1080@60, 1280x720@60)
};
extern Stub stub;
