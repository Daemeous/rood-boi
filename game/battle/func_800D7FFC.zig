//! Stores a value in the first word of D_800F5638 (src/BATTLE/BATTLE.PRG/6F014.c).
const v = @import("views_6F014");

/// orig: func_800D7FFC (src/BATTLE/BATTLE.PRG/6F014.c)
/// Stores its argument in the first word of D_800F5638.
pub fn func_800D7FFC(arg0: c_int) callconv(.c) void {
    v.g.D_800F5638().unk0 = arg0;
}
