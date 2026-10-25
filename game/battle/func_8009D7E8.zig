//! Sets a battle actor's flag and counter from a slot of the actor table
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_8009D7E8 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Clears flag 21 of the actor in slot `arg0`; a nonzero `arg1` instead resets counter 1802 and sets it from `arg1`.
pub fn func_8009D7E8(arg0: c_int, arg1: c_int) callconv(.c) void {
    // The C indexes the slot table without a bound check: psx.at keeps that.
    if (psx.at(v.g.D_800F4538(), arg0).*) |temp_a2| {
        if (arg1 == 0) {
            psx.setBits(temp_a2, "unk5AC_21", 0);
        } else {
            temp_a2.unk1802[0] = 0;
            psx.setBits(temp_a2, "unk5AC_21", 1);
            temp_a2.unk1802[1] = psx.conv(u8, @as(c_int, arg1) *% 2);
            psx.setBits(temp_a2, "unk5AC_22", 0);
        }
    }
}
