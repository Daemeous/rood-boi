//! Tests or clears one slot of D_800F4C38, chosen by a script byte (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800BB504 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Clears slot arg0[1] & 3 of D_800F4C38 when bit 7 of arg0[1] is set; otherwise reports whether that slot is set.
pub fn func_800BB504(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const slot: usize = arg0[1] & 3;
    const slots = v.g.D_800F4C38();
    if ((arg0[1] & 0x80) != 0) {
        slots[slot] = null;
        return 0;
    }
    return @intFromBool(slots[slot] != null);
}
