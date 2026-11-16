/* Analog movement: Ashley walks and runs by the left stick's angle and
 * travel (BATTLE vs_actors_updateAshleyMovementInput, the player's movement input). The original
 * reads the D-pad (eight directions, or the right-stick-shaped mode 7 pad that
 * the PC never presents); with the setting analog_move on, a left stick past
 * its dead zone gives the heading at any angle instead, and its travel the
 * pace: at or past analog_run the run (the pace the D-pad gives), below it the
 * walk (the pace holding R2 gives: the original has only these two). The
 * D-pad, the keyboard and the stick's own D-pad bits (menus, and movement
 * with the setting off) are unchanged; R2 still walks. Settings in
 * project_settings.cpp (analog_move default off, analog_dead_zone, analog_run,
 * percent of the stick's travel). */
#include "boot_controller.h"
#include <math.h>

extern int pc_analog_move, pc_analog_dead_zone, pc_analog_run;
extern int pc_rpc_left_stick(float out[2]) __attribute__((weak)); /* dev_rpc.c: input {left_stick} */

/* The left stick (-1..1, down +y): the RPC's hold, else the gamepad. */
static void stick(float out[2])
{
    if (pc_rpc_left_stick && pc_rpc_left_stick(out))
        return;
    pc_controller_left_stick(out);
}

/* The movement the stick asks for: *x and *z in the pad vector's units (ratan2's arguments, x
 * reversed: left is +, forward is -; 12-bit fixed point of the stick's travel), *run 1 for the run (the
 * D-pad's pace), 0 the walk (R2's). 0 (the d-pad decides) when the setting is off or the stick is inside its dead zone. */
int pc_analog_move_get(int* x, int* z, int* run)
{
    if (!pc_analog_move)
        return 0;
    float s[2];
    stick(s);
    float travel = sqrtf(s[0] * s[0] + s[1] * s[1]);
    float dead = (pc_analog_dead_zone < 0 ? 0 : pc_analog_dead_zone > 95 ? 95 : pc_analog_dead_zone) / 100.0f;
    if (travel <= dead || travel <= 0)
        return 0;
    float k = 4096.0f / (travel > 1 ? travel : 1);
    *x = (int)lrintf(-s[0] * k);
    *z = (int)lrintf(s[1] * k);
    *run = travel * 100.0f >= (float)pc_analog_run;
    return *x || *z;
}
