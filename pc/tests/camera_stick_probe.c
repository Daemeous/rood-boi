/* The right-stick camera's logic (pc/src/camera_stick.c) without the game:
 * stick shaping, the ray test, the reset's turn, continuous yaw at a speed
 * proportional to the deflection, L1/R1 turns and locks left alone, zoom
 * within limits, the game's values untouched at zoom 1, the spring arm
 * stopping before a wall, R3 taken from the pad. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camera_stick.h"
#include "scene_capture.h"

static unsigned vsync;
static float stick[2];
static scene_triangle wall[2];
static int wall_count;
unsigned pc_vsync_count(void) { return vsync; }
int pc_rpc_stick(float out[2])
{
    out[0] = stick[0], out[1] = stick[1];
    return 1;
}
const scene_triangle* pc_scene_triangle_data(int* count)
{
    *count = wall_count;
    return wall;
}
unsigned pc_scene_facing(void) { return 0x10000; }
int pc_scene_serial(void) { return 1 + wall_count; } /* a new room when the wall comes and goes */

static int failures;
static void check(int ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}

static pc_spherical_camera camera;
/* One game frame every two vsyncs, as at 30 fps. */
static void frames(int n, int locked)
{
    for (int i = 0; i < n; i++) {
        vsync += 2;
        pc_camera_stick_tick(&camera, locked);
        pc_spherical_values placed = camera.values;
        int look[4] = { 0, 0, 0, 0 };
        pc_camera_stick_place(&placed, look);
    }
}
static int wrap(int d) { return ((d + 2048) & 0xFFF) - 2048; }

int main(void)
{
    char what[200];
    check(pc_camera_stick_shape(0.1f, 0.15f, 1.6f) == 0, "inside the dead zone: 0");
    check(fabsf(pc_camera_stick_shape(1, 0.15f, 1.6f) - 1) < 1e-6f, "full deflection: 1");
    check(fabsf(pc_camera_stick_shape(-1, 0.15f, 1.6f) + 1) < 1e-6f, "sign kept");
    float half = pc_camera_stick_shape(0.575f, 0.15f, 1), curved = pc_camera_stick_shape(0.575f, 0.15f, 2);
    check(fabsf(half - 0.5f) < 1e-4f && fabsf(curved - 0.25f) < 1e-4f, "linear and squared response");

    float o[3] = { 0, 0, 0 }, d[3] = { 0, 0, 1 };
    float a[3] = { -10, -10, 100 }, b[3] = { 10, -10, 100 }, c[3] = { 0, 10, 100 };
    check(fabsf(pc_camera_stick_ray_triangle(o, d, a, b, c, 0, 1000) - 100) < 1e-3f, "ray hits at 100");
    check(fabsf(pc_camera_stick_ray_triangle(o, d, b, a, c, 0, 1000) - 100) < 1e-3f, "two-sided");
    check(pc_camera_stick_ray_triangle(o, d, a, b, c, 100, 1000) < 0, "beyond the range start only");
    check(pc_camera_stick_ray_triangle(o, d, a, b, c, 0, 50) < 0, "within the range end only");

    int dir;
    check(pc_camera_stick_reset_target(0x400, &dir) == 0x400 && dir == 0, "on the grid: no turn");
    check(pc_camera_stick_reset_target(0x4F0, &dir) == 0x400 && dir == 1, "nearest below: as R1");
    check(pc_camera_stick_reset_target(0xF10, &dir) == 0x1000 && dir == 2, "nearest above: as L1 (wraps)");

    camera.values = (pc_spherical_values) { 0x200, -384, 0x900, 3 };
    camera.delta.pitch = 0x200;
    camera.delta.mode = 3;
    camera.initial_distance = 0x900;
    stick[0] = 1;
    frames(5, 0); /* the smoothing settles */
    int before = camera.values.yaw;
    frames(30, 0); /* one second */
    int full = wrap(camera.values.yaw - before);
    snprintf(what, sizeof what, "full right: %d units in a second (120 deg/s = -1365, as R1 turns)", full);
    check(full < -1300 && full > -1420, what);
    check(camera.delta.pitch == camera.values.yaw, "a room entry keeps the stick's yaw (delta.pitch)");
    stick[0] = -0.575f; /* half speed with the default curve? use linear */
    pc_camera_stick_curve = 1;
    frames(5, 0);
    before = camera.values.yaw;
    frames(30, 0);
    int halfway = wrap(camera.values.yaw - before);
    snprintf(what, sizeof what, "half left: %d units (proportional, opposite)", halfway);
    check(halfway > 640 && halfway < 720, what);
    pc_camera_stick_curve = 1.6f;

    before = camera.values.yaw;
    camera.delta.yaw = 2;
    frames(10, 0);
    check(camera.values.yaw == before, "an L1/R1 turn in progress: the stick waits");
    camera.delta.yaw = 0;
    frames(10, 1);
    check(camera.values.yaw == before, "locked (events, combat modes): nothing");
    stick[0] = 0;
    frames(10, 0);
    before = camera.values.yaw;
    frames(10, 0);
    check(camera.values.yaw == before, "stick released: the yaw stays");

    /* Zoom 1: the game's values exactly. */
    pc_spherical_values placed = camera.values;
    int look[4] = { 0, 0, 0, 0 };
    pc_camera_stick_place(&placed, look);
    check(!memcmp(&placed, &camera.values, sizeof placed), "zoom 1: placed from the game's own values");

    stick[1] = 1; /* down: out */
    frames(90, 0);
    stick[1] = 0;
    frames(30, 0);
    placed = camera.values;
    pc_camera_stick_place(&placed, look);
    snprintf(what, sizeof what, "zoomed out to %d (max 1.5 x 0x900 = 3456), pitch %d steeper", placed.distance,
        placed.pitch);
    check(placed.distance > 3400 && placed.distance <= 3456 && placed.pitch < -384, what);
    check(camera.values.distance == 0x900 && camera.values.pitch == -384, "the game's own values unchanged");

    /* A wall across the arm at 2800 units: the arm stops before it. */
    float yaw = camera.values.yaw * (6.2831853f / 4096), p = placed.pitch * (6.2831853f / 4096);
    float dir3[3] = { sinf(yaw), tanf(p), cosf(yaw) };
    float n = sqrtf(dir3[0] * dir3[0] + dir3[1] * dir3[1] + dir3[2] * dir3[2]);
    float centre[3], side[3] = { cosf(yaw), 0, -sinf(yaw) }, up[3];
    for (int k = 0; k < 3; k++)
        dir3[k] /= n, centre[k] = dir3[k] * 2800;
    up[0] = dir3[1] * side[2] - dir3[2] * side[1];
    up[1] = dir3[2] * side[0] - dir3[0] * side[2];
    up[2] = dir3[0] * side[1] - dir3[1] * side[0];
    for (int k = 0; k < 3; k++) {
        wall[0].vertex[0].position[k] = centre[k] - 500 * side[k] - 500 * up[k];
        wall[0].vertex[1].position[k] = centre[k] + 500 * side[k] - 500 * up[k];
        wall[0].vertex[2].position[k] = centre[k] + 500 * up[k];
    }
    wall_count = 1;
    frames(2, 0);
    placed = camera.values;
    pc_camera_stick_place(&placed, look);
    snprintf(what, sizeof what, "a wall at 2800: the camera stops at %d", placed.distance);
    check(placed.distance >= 2304 && placed.distance <= 2800 - 32, what);
    wall_count = 0;
    frames(60, 0);
    placed = camera.values;
    pc_camera_stick_place(&placed, look);
    snprintf(what, sizeof what, "the wall gone: the arm lets out again (%d; the wall's room allowed 1 + its size / 0x900)",
        placed.distance);
    check(placed.distance > 2900, what);

    /* Zooming in stops at the minimum. */
    stick[1] = -1;
    frames(120, 0);
    stick[1] = 0;
    frames(30, 0);
    placed = camera.values;
    pc_camera_stick_place(&placed, look);
    snprintf(what, sizeof what, "zoomed in to %d (min 0.6 x 0x900 = 1382), flatter pitch %d", placed.distance,
        placed.pitch);
    check(placed.distance >= 1380 && placed.distance < 1400 && placed.pitch > -384, what);

    /* Locked for a while: the zoom eases back to the game's distance. */
    frames(30, 1);
    placed = camera.values;
    pc_camera_stick_place(&placed, look);
    check(placed.distance == 0x900 && placed.pitch == -384, "the game holds the camera: its own distance");
    frames(30, 0);

    /* R3: the pad loses it, the camera turns back to the grid and zoom 1. */
    stick[0] = 1;
    frames(7, 0);
    stick[0] = 0;
    frames(5, 0);
    unsigned pad = pc_camera_stick_buttons((1u << 10) | (1u << 3));
    check(pad == (1u << 3), "R3 does not reach the game (R1 does)");
    pc_camera_stick_buttons(0);
    int from = camera.values.yaw;
    frames(1, 0);
    int expected = pc_camera_stick_reset_target(from, &dir);
    snprintf(what, sizeof what, "R3 at yaw %#x: the game's turn to %#x starts (%d)", from, expected, camera.delta.yaw);
    check(dir != 0 && camera.delta.yaw == dir && camera.delta.pitch == expected, what);
    camera.delta.yaw = 0; /* (the game's animation would end the turn) */
    camera.values.yaw = expected & 0xFFF;
    frames(30, 0);
    placed = camera.values;
    pc_camera_stick_place(&placed, look);
    check(placed.distance == 0x900, "R3: zoom back to 1");
    /* R1 from between the lines the stick left: the nearest line below. */
    stick[0] = 0.5f;
    frames(4, 0);
    stick[0] = 0;
    frames(2, 0);
    int at = camera.values.yaw;
    camera.delta.yaw = 1;
    camera.delta.pitch = (at - 0x200) & ~0x1FF;
    pc_camera_stick_turn(&camera);
    snprintf(what, sizeof what, "R1 from the stick's yaw %#x aims at %#x", at, camera.delta.pitch);
    check((at & 0x1FF) && camera.delta.pitch == (at & ~0x1FF) && camera.values.yaw == (at & ~7), what);
    camera.values.yaw = (at + 7) & 0xFFF; /* a yaw the game set itself */
    camera.delta.pitch = (camera.values.yaw - 0x200) & ~0x1FF;
    int game_target = camera.delta.pitch;
    pc_camera_stick_turn(&camera);
    check(camera.delta.pitch == game_target, "R1 from a yaw the game set: the game's own target");
    camera.delta.yaw = 0;
    pc_camera_stick_r3 = 0;
    check(pc_camera_stick_buttons(1u << 10) == (1u << 10), "R3 reset off: R3 is the game's");

    /* Menus (pc_camera_stick_menu): the contexts, the list, a turn there. */
    pc_camera_stick_r3 = 1;
    check(!strcmp(pc_camera_stick_context(5, 1, 0), "command") && !strcmp(pc_camera_stick_context(5, 1, 0x41), "magic")
              && !strcmp(pc_camera_stick_context(5, 1, 0x44), "items")
              && !strcmp(pc_camera_stick_context(5, 1, 0x48), "options")
              && !strcmp(pc_camera_stick_context(5, 1, 0x0C), "loot") && !strcmp(pc_camera_stick_context(2, 12, 0x3F), "loot")
              && !strcmp(pc_camera_stick_context(9, 3, 0x0C), "chest") && !strcmp(pc_camera_stick_context(6, 0, 0x3F), "shortcut")
              && !strcmp(pc_camera_stick_context(11, 0, 0x3F), "message"),
        "menu contexts by field mode, camera mode and currentMenu");
    check(!pc_camera_stick_context(1, 0, 0x3F) && !pc_camera_stick_context(5, 1, 0x3F) && !pc_camera_stick_context(5, 1, 0x1F)
              && !pc_camera_stick_context(2, 1, 0x3F) && !pc_camera_stick_context(9, 1, 0x3F),
        "no menu context in the field, a closed menu, targeting or a chest's framing");
    check(pc_camera_stick_menu_listed("command") && pc_camera_stick_menu_listed("loot") && pc_camera_stick_menu_listed("chest")
              && !pc_camera_stick_menu_listed("items") && !pc_camera_stick_menu_listed("status")
              && !pc_camera_stick_menu_listed("map") && !pc_camera_stick_menu_listed("options")
              && !pc_camera_stick_menu_listed("comma"),
        "default list: the menus that leave the scene in view, not the full-screen ones");
    check(pc_camera_stick_menus_set("items, bogus status") == 1 && !strcmp(pc_camera_stick_menus, "items status")
              && pc_camera_stick_menus_set("none") == 0 && !*pc_camera_stick_menus,
        "camera_stick_menus parsing: commas, unknown names left out, none");
    pc_camera_stick_menus_set(pc_camera_stick_menus_default);
    camera.values.yaw = 0x400, camera.delta.yaw = 0;
    stick[0] = 1;
    int menu_moved = 0;
    for (int i = 0; i < 20; i++)
        vsync += 2, menu_moved += pc_camera_stick_menu(&camera, 5, 1, 0);
    snprintf(what, sizeof what, "the command menu: the stick turns (yaw %#x, %d placements)", camera.values.yaw, menu_moved);
    check(camera.values.yaw != 0x400 && menu_moved > 0, what);
    before = camera.values.yaw;
    for (int i = 0; i < 20; i++)
        vsync += 2, menu_moved = pc_camera_stick_menu(&camera, 5, 1, 0x44);
    check(camera.values.yaw == before && !menu_moved, "items (full screen): the stick does nothing");
    stick[0] = 0;
    for (int i = 0; i < 20; i++)
        vsync += 2, pc_camera_stick_menu(&camera, 5, 1, 0);
    before = camera.values.yaw;
    menu_moved = 0;
    for (int i = 0; i < 10; i++)
        vsync += 2, menu_moved += pc_camera_stick_menu(&camera, 5, 1, 0);
    check(camera.values.yaw == before && !menu_moved, "the stick at rest in a menu: nothing placed");
    pc_camera_stick_reset(); /* waits for the field: no L1/R1 animation in a menu */
    vsync += 2, pc_camera_stick_menu(&camera, 5, 1, 0);
    check(camera.delta.yaw == 0, "a reset in a menu waits for the field");
    frames(1, 0);
    check(camera.delta.yaw != 0 || !(camera.values.yaw & 0x1FF), "... and runs there");

    printf(failures ? "camera_stick_probe: %d failures\n" : "camera_stick_probe: all passed\n", failures);
    return failures != 0;
}
