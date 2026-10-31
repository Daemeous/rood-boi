//! Copies an int from a pointer into the global D_800F5618 (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: func_800D5294 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores the int that arg0 points to into D_800F5618.
pub fn func_800D5294(arg0: [*c]c_int) callconv(.c) void {
    v.g.D_800F5618().* = arg0.*;
}
