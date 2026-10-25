//! Reports the flags of the battle actor in slot `arg0` (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_800A0A74 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Returns bit 9 of the actor's flags in slot `arg0`, plus 2 when bit 11 is set.
pub fn func_800A0A74(arg0: c_int) callconv(.c) c_int {
    const temp_a0 = psx.at(v.g.D_800F4538(), arg0).*;
    if (temp_a0) |actor| {
        var var_v1: c_int = @as(c_int, psx.getBits(actor, "unk5AC_9"));
        if (psx.getBits(actor, "unk5AC_11") != 0) {
            var_v1 |= 2;
        }
        return var_v1;
    }
    // The original has no return on this path; its value is undefined. 0 is used here.
    return 0;
}
