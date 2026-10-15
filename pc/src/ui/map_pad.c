/* The map screen's analog controls (map_pad.h). */
#include "map_pad.h"

enum {
    PAD_L2 = 1, PAD_R2 = 2, PAD_START = 0x800, PAD_DPAD = 0xF000,
    TURN = 64,            /* MENU5's rotate step, a frame at full deflection */
    ZOOM = 16,            /* ONE / 256: its zoom step */
    PITCH_MAX = 1024,
    NEAR = 512, FAR = 1024, /* ONE / 8 .. ONE / 4 */
    PAN = 4, PAN_X = 96, PAN_Y = 80
};

static float shape(float v, float deadzone)
{
    float a = v < 0 ? -v : v;
    if (deadzone < 0)
        deadzone = 0;
    if (deadzone > 0.9f)
        deadzone = 0.9f;
    if (a <= deadzone)
        return 0;
    a = (a - deadzone) / (1 - deadzone);
    if (a > 1)
        a = 1;
    return v < 0 ? -a : a;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Whole units out of a carried fraction. */
static int take(float* frac, float add)
{
    *frac += add;
    int whole = (int)*frac; /* (towards zero) */
    *frac -= (float)whole;
    return whole;
}

static int modern(const pc_map_pad_in* in, pc_map_pad_view* v, int cmd)
{
    float x = shape(in->stick[0], in->deadzone), y = shape(in->stick[1], in->deadzone);
    if (x != 0 || y != 0) {
        v->yaw += take(&v->frac[0], TURN * x);
        v->pitch += take(&v->frac[1], -TURN * y); /* stick up: the view rises, as MENU5's left stick */
        if (v->pitch <= 0 || v->pitch >= PITCH_MAX) {
            v->pitch = clamp(v->pitch, 0, PITCH_MAX);
            v->frac[1] = 0;
        }
        return 3;
    }
    unsigned trig = in->held & (PAD_L2 | PAD_R2);
    if ((trig == PAD_L2 || trig == PAD_R2) && !(in->held & (PAD_DPAD | PAD_START))) {
        int r = trig == PAD_R2;
        float t = in->trigger[r] > 0 ? (in->trigger[r] > 1 ? 1 : in->trigger[r]) : 1;
        v->distance += take(&v->frac[2], (r ? -ZOOM : ZOOM) * t);
        if (v->distance <= NEAR || v->distance >= FAR) {
            v->distance = clamp(v->distance, NEAR, FAR);
            v->frac[2] = 0;
        }
        return 2;
    }
    v->frac[0] = v->frac[1] = v->frac[2] = 0;
    return cmd;
}

/* MENU5's own stick code (4D8.c processInput), on the right stick. */
static int original(const pc_map_pad_in* in, pc_map_pad_view* v, int cmd)
{
    int dx = in->stick[0] < -0.5f ? -1 : in->stick[0] > 0.5f ? 1 : 0;
    int dy = in->stick[1] < -0.5f ? -1 : in->stick[1] > 0.5f ? 1 : 0;
    if ((!dx && !dy) || (in->held & PAD_DPAD))
        return cmd;
    if (in->held & PAD_L2) {
        v->yaw += TURN * dx;
        v->pitch = clamp(v->pitch - TURN * dy, 0, PITCH_MAX);
        return 3;
    }
    if (in->held & PAD_R2) {
        if (dy)
            v->distance = clamp(v->distance + ZOOM * dy, NEAR, FAR);
        return 2;
    }
    if (in->held & PAD_START)
        return cmd;
    int step = in->invert ? -PAN : PAN;
    v->gx = clamp(v->gx + step * dx, -PAN_X, PAN_X);
    v->gy = clamp(v->gy + step * dy, -PAN_Y, PAN_Y);
    return cmd;
}

int pc_map_pad_step(const pc_map_pad_in* in, pc_map_pad_view* v, int cmd)
{
    return in->controls == PC_MAP_CONTROLS_ORIGINAL ? original(in, v, cmd) : modern(in, v, cmd);
}
