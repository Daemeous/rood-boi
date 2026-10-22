//! Byte lookup in the table D_800E9C30 (src/BATTLE/BATTLE.PRG/573B8.c).
const v = @import("views_573B8");

/// orig: func_800BFE34 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Returns the byte of D_800E9C30 selected by the byte at arg0.
pub fn func_800BFE34(arg0: [*c]u8) callconv(.c) c_int {
    const table = v.g.D_800E9C30();
    return @as(c_int, table[arg0[0]]);
}
