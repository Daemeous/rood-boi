//! Reads two 16-bit fields of a battle object from the D_800F4538 table into caller-supplied ints (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_800A0BA0 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies entry `arg0` of D_800F4538's unk5C2 and unk5C0 into `arg1` and `arg2` when the entry is set.
pub fn func_800A0BA0(arg0: c_int, arg1: [*c]c_int, arg2: [*c]c_int) callconv(.c) void {
    // The C indexes D_800F4538 without a bounds check: psx.at does the same.
    const temp_a0 = psx.at(v.g.D_800F4538(), arg0).*;
    if (temp_a0) |t| {
        arg1.* = @as(c_int, t.unk5C2);
        arg2.* = @as(c_int, t.unk5C0);
    }
}
