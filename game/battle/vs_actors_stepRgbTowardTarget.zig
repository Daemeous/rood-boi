//! Moves three bytes of one colour triple toward a target triple by a signed step, reporting whether any moved
//! (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;

/// orig: vs_actors_stepRgbTowardTarget (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Steps the three bytes at arg0 by arg2 toward the bytes at arg1, never past them; returns 1 if any byte moved.
pub fn vs_actors_stepRgbTowardTarget(arg0: [*c]u8, arg1: [*c]u8, arg2: c_int) callconv(.c) c_int {
    var var_t0: c_int = 0;
    var i: usize = 0;

    while (i < 3) : (i += 1) {
        if (arg2 < 0) {
            if (arg1[i] < arg0[i]) {
                var_t0 = 1;
                arg0[i] = psx.conv(u8, @as(c_int, arg0[i]) +% arg2);
                if (arg0[i] < arg1[i]) {
                    arg0[i] = arg1[i];
                }
            }
        } else {
            if (arg0[i] < arg1[i]) {
                var_t0 = 1;
                arg0[i] = psx.conv(u8, @as(c_int, arg0[i]) +% arg2);
                if (arg1[i] < arg0[i]) {
                    arg0[i] = arg1[i];
                }
            }
        }
    }

    return var_t0;
}
