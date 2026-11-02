//! Script jump: the address `arg0` plus the signed 16-bit offset stored at `arg0 + 1`
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_battle_script_jumpFwd2 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns the low 32 bits of the address of the script byte the forward jump at `arg0` lands on.
pub fn script_jumpFwd2(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // The offset is read first, then added as C does: a signed short sign-extends,
    // and the sum wraps; the (long) cast keeps the low 32 bits.
    const k: c_short = c.vs_battle_getShort(arg0 + 1);
    const sum: usize = @intFromPtr(arg0) +% @as(usize, @bitCast(@as(isize, k)));
    return @bitCast(@as(u32, @truncate(sum)));
}
