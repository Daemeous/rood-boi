//! Reports whether the unit's helper `func_8008F0FC` returns zero (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9F70 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when func_8008F0FC returns 0, else 0; the arguments are unused.
pub fn func_800B9F70(_: [*c]u8, _: c_short) callconv(.c) c_int {
    return @intFromBool(c.func_8008F0FC() == 0);
}
