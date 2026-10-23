//! Battle overlay unit 7C4E8: a single field setter (src/BATTLE/BATTLE.PRG/7C4E8.c).
const v = @import("views_7C4E8");

/// orig: func_800E4CE8 (src/BATTLE/BATTLE.PRG/7C4E8.c)
/// Sets the record's unk38 field to -1.
pub fn func_800E4CE8(arg0: ?*v.func_800DEEA4_t2) callconv(.c) void {
    arg0.?.unk38 = -1;
}
