//! Compares the value from func_80044D94 against the short at arg0 + 1 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BA7BC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when the high half of func_80044D94's result is below the short at `arg0 + 1`, else 0.
pub fn func_800BA7BC(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // Call order as in the C: func_80044D94 first, then vs_battle_getShort.
    const temp_s1: c_int = c.func_80044D94() >> 0x10;
    const limit: c_int = c.vs_battle_getShort(arg0 + 1);
    return @intFromBool(temp_s1 < limit);
}
