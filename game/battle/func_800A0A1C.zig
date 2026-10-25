//! Sets two flag bits of a D_800F4538 entity slot (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_800A0A1C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets flags unk5AC_9 (from arg1's bit 0) and unk5AC_11 (from arg1's bit 1) of entry arg0, if that entry is set.
pub fn func_800A0A1C(arg0: c_int, arg1: c_int) callconv(.c) void {
    // arg0 is not bounds-checked, as in the C: psx.at indexes the array without a check.
    const temp_a0 = psx.at(v.g.D_800F4538(), arg0).*;
    if (temp_a0) |entity| {
        psx.setBits(entity, "unk5AC_9", psx.conv(u1, arg1));
        psx.setBits(entity, "unk5AC_11", psx.conv(u1, arg1 >> 1));
    }
}
