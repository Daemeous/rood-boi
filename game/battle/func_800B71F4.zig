//! Reports whether any of the three entries in the D_800F4B30 table has its unk3 flag set (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800B71F4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 if unk3 is nonzero in any of D_800F4B30[0..2], else 0.
pub fn func_800B71F4(_: [*c]u8, _: c_short) callconv(.c) c_int {
    const table = v.g.D_800F4B30();
    const bits: c_int = @as(c_int, table[2].unk3) | (@as(c_int, table[0].unk3) | @as(c_int, table[1].unk3));
    return @intFromBool(bits != 0);
}
