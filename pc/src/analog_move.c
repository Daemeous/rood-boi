/* Analog movement: Ashley walks and runs by the left stick's angle and
 * travel (BATTLE vs_actors_updateAshleyMovementInput, the player's movement input). The original
 * reads the D-pad (eight directions, or the right-stick-shaped mode 7 pad that
 * the PC never presents); with the setting analog_move on, a left stick past
 * its dead zone gives the heading at any angle instead. The D-pad, the keyboard and the
 * stick's own D-pad bits (menus, and movement with the setting off) are unchanged; R2 still walks.
 *
 * Speed (analog_pace): 1 (default) the stick's pressure is the speed, continuous: from the dead
 * zone's edge (analog_min_speed percent of the run) to the full push (the run), along the curve
 * analog_curve (percent: 100 linear, more is gentler at the start); 0 the original's two paces
 * (at or past analog_run percent of the travel the run, below it the walk R2 held gives).
 *
 * The animation: the original plays one stride cycle (13 frames a foot) at the walk and at the run alike
 * (the run cycle's speed test never passes for Ashley), so the feet slide at the walk. With a continuous speed the cycle's
 * playback rate follows the speed (analog_anim_rate percent: 0 the original's fixed rate, 100 the feet
 * keep up exactly), counted in whole game frames with a carried fraction (pc_analog_anim_steps).
 *
 * The step: the engine truncates each frame's step to whole units, which bends the heading by up to a few degrees
 * at small speeds; pc_analog_step_remainder gives each actor the fraction it lost, carried to the next frame
 * (Ashley under analog_move; the enemies under enemy_free_angle).
 *
 * Interaction (chests, doors, blocks, the one-tile step lock): the game reads the nearest of the four
 * tile directions from Ashley's facing; pc_analog_cardinal keeps the one it chose until the facing has moved
 * analog_hysteresis degrees past the boundary, so a stick held at 45 degrees picks one side and keeps it
 * (only while the stick was the last input: the D-pad's rounding is the original's).
 *
 * Settings in project_settings.cpp (analog_move default on: a gamepad's stick; the keyboard has none). */
#include "analog_state.h"
#include "boot_controller.h"
#include <math.h>
#include <stdlib.h>

extern int pc_analog_move, pc_analog_dead_zone, pc_analog_run, pc_analog_pace, pc_analog_curve, pc_analog_min_speed,
    pc_analog_anim_rate, pc_analog_hysteresis, pc_enemy_free_angle, pc_anim_blend;
extern int pc_rpc_left_stick(float out[2]) __attribute__((weak)); /* dev_rpc.c: input {left_stick} */

#define ACTORS PC_ANALOG_ACTORS
#define sticky pc_analog_st.sticky
#define latch pc_analog_st.latch
#define anim_acc pc_analog_st.anim_acc
#define remainders pc_analog_st.remainders
/* Set by pc_analog_move_get each frame, before the movement and the animation use them: not carried. */
static int active;   /* the stick moved Ashley this frame */
static int run_frac; /* 4096 = the run: this frame's continuous speed (0: not a continuous speed) */
static int pose_acc; /* her stride's carried fraction (1/256 tick) this frame when it plays below the tick rate, else 0 */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The left stick (-1..1, down +y): the RPC's hold, else the gamepad. */
static void stick(float out[2])
{
    if (pc_rpc_left_stick && pc_rpc_left_stick(out))
        return;
    pc_controller_left_stick(out);
}

/* The movement the stick asks for: *x and *z in the pad vector's units (ratan2's arguments, x
 * reversed: left is +, forward is -; 12-bit fixed point of the stick's travel), *frac the speed as a
 * fraction of the run, 4096 = the run (the D-pad's pace), or -1 for the walk (R2's pace). `dpad`: the D-pad
 * is held (the stick's turn to be, or not, the last input). 0 (the d-pad decides) when the setting is off or the
 * stick is inside its dead zone. */
int pc_analog_move_get(int* x, int* z, int* frac, int dpad)
{
    active = 0;
    run_frac = 0;
    if (!pc_analog_move) {
        sticky = 0;
        return 0;
    }
    float s[2];
    stick(s);
    float travel = sqrtf(s[0] * s[0] + s[1] * s[1]);
    float dead = clampi(pc_analog_dead_zone, 0, 95) / 100.0f;
    if (travel <= dead || travel <= 0) {
        if (dpad)
            sticky = 0;
        return 0;
    }
    float k = 4096.0f / (travel > 1 ? travel : 1);
    *x = (int)lrintf(-s[0] * k);
    *z = (int)lrintf(s[1] * k);
    if (!(*x || *z))
        return 0;
    if (pc_analog_pace == 0) {
        *frac = travel * 100.0f >= (float)pc_analog_run ? 4096 : -1;
    } else {
        float t = travel >= 1 ? 1.0f : (travel - dead) / (1 - dead);
        float curve = clampi(pc_analog_curve, 25, 400) / 100.0f;
        float low = clampi(pc_analog_min_speed, 0, 50) / 100.0f;
        float f = low + (1 - low) * powf(t, curve);
        *frac = clampi((int)lrintf(f * 4096.0f), 1, 4096);
        run_frac = *frac;
    }
    active = 1;
    sticky = 1;
    return 1;
}

/* The transition screens never read the pad: nothing moves her. */
void pc_analog_move_idle(void)
{
    active = 0;
    run_frac = 0;
}

/* The playback steps of Ashley's stride this frame: `base` whole frames in the original (the animation's
 * speed from the game: 1, 2 hasted); the stride animations walk, run and back (13..16, 19, 20, with the
 * drawn-weapon variants 100 more) follow the speed under a continuous analog pace, and stand still or go
 * on at a fraction of the rate (the fraction carried between frames). Else `base` itself. */
int pc_analog_anim_steps(int slot, int base, int animation)
{
    int id = animation >= 100 ? animation - 100 : animation;
    int stride = (id >= 13 && id <= 16) || id == 19 || id == 20;
    pose_acc = 0;
    if (slot != 0 || !active || !run_frac || pc_analog_anim_rate <= 0 || !stride || base <= 0) {
        anim_acc = 0;
        return base;
    }
    float k = clampi(pc_analog_anim_rate, 0, 100) / 100.0f;
    float rate = 1.0f - (1.0f - run_frac / 4096.0f) * k;
    anim_acc += base * (int)lrintf(rate * 256.0f);
    int steps = anim_acc >> 8;
    anim_acc &= 255;
    pose_acc = anim_acc;
    return steps;
}

/* Where her stride is between its ticks, as 0..255 of the way to the next (0: it is on a tick, or not a slowed stride):
 * what pose_blend.c draws the pose at (scaled by the setting anim_blend, percent). Read once a frame, after pc_analog_anim_steps. */
int pc_analog_pose_progress(void)
{
    int acc = pose_acc * clampi(pc_anim_blend, 0, 100) / 100;
    pose_acc = 0;
    return acc;
}

/* Where actor `index`'s lost step fractions are kept (two ints: x, z, in units / 0x1000000), or NULL when
 * its steps stay as the original truncates them. */
int* pc_analog_step_remainder(int index)
{
    if (index < 0 || index >= ACTORS)
        return 0;
    int gate = index == 0 ? (pc_analog_move && active) : pc_enemy_free_angle != 0;
    if (!gate) {
        remainders[index][0] = remainders[index][1] = 0;
        return 0;
    }
    return remainders[index];
}

/* The nearest of the four tile directions of a facing (0 = -z, 1 = -x, 2 = +z, 3 = +x), given as the game computes it,
 * (facing + 0x200) & 0xFFF: the original's quotient by 0x400, with hysteresis while the stick was the last input. */
int pc_analog_cardinal(int biased)
{
    int facing = biased - 0x200;
    int nearest = biased / 0x400;
    int band = 0x200 + (int)lrintf(clampi(pc_analog_hysteresis, 0, 44) * 4096.0f / 360.0f);
    if (pc_analog_move && sticky && latch >= 0 && band > 0x200) {
        int d = (facing - latch * 0x400) & 0xFFF;
        if (d > 0x800)
            d -= 0x1000;
        if (abs(d) <= band)
            return latch;
    }
    latch = nearest;
    return nearest;
}
