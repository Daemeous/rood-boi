//! Battle flag copy (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800BEC30 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Copies the flag at D_800F4C34 to D_800F4BB8 when it is 1, and returns 1.
pub fn func_800BEC30() callconv(.c) c_int {
    if (@as(c_int, v.g.D_800F4C34().*) == 1) {
        v.g.D_800F4BB8().* = v.g.D_800F4C34().*;
        return 1;
    }
    return 1;
}
