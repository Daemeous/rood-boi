/* The map screen's right stick and triggers (pc/src/ui/map_pad.c): Modern
 * and Original against MENU5's view statics. */
#include "map_pad.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...)                         \
    do {                                         \
        if (!(cond)) {                           \
            fprintf(stderr, "FAIL %d: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);        \
            fputc('\n', stderr);                 \
            ++failures;                          \
        }                                        \
    } while (0)

enum { L2 = 1, R2 = 2, START = 0x800, UP = 0x1000, RIGHT = 0x2000, DOWN = 0x4000, LEFT = 0x8000 };

static pc_map_pad_view view(void)
{
    pc_map_pad_view v;
    memset(&v, 0, sizeof v);
    v.pitch = 0x238, v.yaw = 0x400, v.distance = 768; /* the reset view (ONE * 3 / 16) */
    return v;
}

static pc_map_pad_in input(int controls)
{
    pc_map_pad_in in;
    memset(&in, 0, sizeof in);
    in.deadzone = 0.15f;
    in.controls = controls;
    return in;
}

/* `frames` steps of `in`; returns the last commands state. */
static int run(const pc_map_pad_in* in, pc_map_pad_view* v, int frames)
{
    int cmd = 0;
    for (int i = 0; i < frames; i++)
        cmd = pc_map_pad_step(in, v, 0);
    return cmd;
}

static void modern(void)
{
    pc_map_pad_in in = input(PC_MAP_CONTROLS_MODERN);
    pc_map_pad_view v = view(), w;

    /* inside the deadzone: nothing */
    in.stick[0] = 0.1f, in.stick[1] = -0.12f;
    w = v;
    CHECK(run(&in, &w, 30) == 0 && w.yaw == v.yaw && w.pitch == v.pitch, "deadzone: no motion (%d %d)", w.yaw,
        w.pitch);

    /* full right: 64 a frame */
    in.stick[0] = 1, in.stick[1] = 0;
    w = v;
    CHECK(run(&in, &w, 10) == 3 && w.yaw == v.yaw + 640 && w.pitch == v.pitch, "full right: yaw +640 (%d)",
        w.yaw - v.yaw);
    /* proportional past the deadzone: (0.575 - 0.15) / 0.85 = half speed */
    in.stick[0] = 0.575f;
    w = v;
    run(&in, &w, 10);
    CHECK(w.yaw - v.yaw >= 318 && w.yaw - v.yaw <= 321, "half deflection: half speed (yaw +%d, want 320)",
        w.yaw - v.yaw);
    /* fractions carry: a slow stick still turns */
    in.stick[0] = 0.16f;
    w = v;
    run(&in, &w, 60);
    CHECK(w.yaw - v.yaw >= 44 && w.yaw - v.yaw <= 46, "slow stick: fractions carried (yaw +%d, want 45)",
        w.yaw - v.yaw);
    /* left turns the other way */
    in.stick[0] = -1;
    w = v;
    run(&in, &w, 4);
    CHECK(w.yaw == v.yaw - 256, "full left: yaw -256 (%d)", w.yaw - v.yaw);

    /* stick up raises the pitch (as MENU5's left stick), clamped to 0..1024 */
    in.stick[0] = 0, in.stick[1] = -1;
    w = v;
    run(&in, &w, 4);
    CHECK(w.pitch == v.pitch + 256, "up: pitch +256 (%d)", w.pitch - v.pitch);
    run(&in, &w, 100);
    CHECK(w.pitch == 1024, "pitch clamped at 1024 (%d)", w.pitch);
    in.stick[1] = 1;
    run(&in, &w, 100);
    CHECK(w.pitch == 0, "pitch clamped at 0 (%d)", w.pitch);

    /* L2 alone zooms out, R2 alone in; clamps ONE / 8 .. ONE / 4 */
    in = input(PC_MAP_CONTROLS_MODERN);
    in.held = L2;
    w = v;
    CHECK(run(&in, &w, 4) == 2 && w.distance == 768 + 64, "L2 alone: distance +64 (%d), state 2", w.distance);
    run(&in, &w, 100);
    CHECK(w.distance == 1024, "L2: distance clamped at 1024 (%d)", w.distance);
    in.held = R2;
    run(&in, &w, 100);
    CHECK(w.distance == 512, "R2: distance clamped at 512 (%d)", w.distance);
    /* analog: half travel, half speed */
    in.held = R2, in.trigger[1] = 0.5f;
    w = v;
    run(&in, &w, 8);
    CHECK(w.distance == 768 - 64, "R2 half travel: distance -64 over 8 frames (%d)", w.distance - 768);
    /* not alone: with the D-pad (MENU5 rotates), the stick (it turns), both triggers, Start: no zoom */
    unsigned others[] = { L2 | UP, L2 | LEFT, R2 | DOWN, L2 | R2, L2 | START };
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++) {
        in = input(PC_MAP_CONTROLS_MODERN);
        in.held = others[i];
        w = v;
        int cmd = pc_map_pad_step(&in, &w, 7);
        CHECK(cmd == 7 && w.distance == 768 && w.yaw == v.yaw && w.pitch == v.pitch,
            "held %#x: MENU5's own (state %d, distance %d)", others[i], cmd, w.distance);
    }
    in = input(PC_MAP_CONTROLS_MODERN);
    in.held = L2, in.stick[0] = 1;
    w = v;
    CHECK(run(&in, &w, 2) == 3 && w.distance == 768 && w.yaw == v.yaw + 128, "L2 + stick: turns, no zoom (%d %d)",
        w.distance, w.yaw - v.yaw);

    /* Modern never pans */
    in = input(PC_MAP_CONTROLS_MODERN);
    in.stick[0] = 1, in.stick[1] = 1;
    w = v;
    run(&in, &w, 10);
    CHECK(w.gx == 0 && w.gy == 0, "Modern: the right stick does not pan (%d %d)", w.gx, w.gy);
}

static void original(void)
{
    pc_map_pad_in in = input(PC_MAP_CONTROLS_ORIGINAL);
    pc_map_pad_view v = view(), w;

    /* the stick alone pans 4 a frame past half way, clamped to +-96 / +-80, following qol_map_invert */
    in.stick[0] = 0.4f;
    w = v;
    run(&in, &w, 10);
    CHECK(w.gx == 0 && w.yaw == v.yaw, "Original: below half way nothing (%d)", w.gx);
    in.stick[0] = 0.8f, in.stick[1] = -0.8f;
    w = v;
    CHECK(run(&in, &w, 3) == 0 && w.gx == 12 && w.gy == -12, "Original: pan right / up (%d %d)", w.gx, w.gy);
    run(&in, &w, 100);
    CHECK(w.gx == 96 && w.gy == -80, "Original: pan clamped (%d %d)", w.gx, w.gy);
    in.invert = 1;
    w = v;
    run(&in, &w, 3);
    CHECK(w.gx == -12 && w.gy == 12, "Original, inverted: pan left / down (%d %d)", w.gx, w.gy);

    /* L2 + stick turns 64 a step, R2 + stick Y zooms 16 */
    in = input(PC_MAP_CONTROLS_ORIGINAL);
    in.held = L2, in.stick[0] = 1, in.stick[1] = -1;
    w = v;
    CHECK(run(&in, &w, 2) == 3 && w.yaw == v.yaw + 128 && w.pitch == v.pitch + 128 && w.gx == 0,
        "Original L2 + stick: turns and tilts (%d %d)", w.yaw - v.yaw, w.pitch - v.pitch);
    in.held = R2, in.stick[0] = 0, in.stick[1] = 1;
    w = v;
    CHECK(run(&in, &w, 2) == 2 && w.distance == 768 + 32, "Original R2 + stick down: zoom out (%d)", w.distance);

    /* L2 or R2 alone: nothing (the PS1 had no trigger zoom) */
    in = input(PC_MAP_CONTROLS_ORIGINAL);
    in.held = L2;
    w = v;
    CHECK(run(&in, &w, 10) == 0 && w.distance == 768, "Original L2 alone: nothing (%d)", w.distance);
    /* with a D-pad bit held the D-pad (MENU5's own step) has the frame */
    in.held = UP, in.stick[0] = 1;
    w = v;
    run(&in, &w, 3);
    CHECK(w.gx == 0, "Original: D-pad held, the stick waits (%d)", w.gx);
}

int main(void)
{
    modern();
    original();
    if (failures) {
        fprintf(stderr, "map_pad_probe: %d failure(s)\n", failures);
        return 1;
    }
    puts("map_pad_probe: ok");
    return 0;
}
