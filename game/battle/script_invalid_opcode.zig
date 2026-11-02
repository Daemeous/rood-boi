//! The event script's invalid-opcode handler (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_invalidOpcode (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Formats the unknown opcode's report into a stack buffer (the original prints nothing) and goes on.
pub fn invalidOpcode(script: [*c]u8, arg1: c_short) callconv(.c) c_int {
    var buf: [64]u8 = undefined;
    _ = c.sprintf(&buf, @constCast("unknown code detected:%d,$%08x,$%02x"), @as(c_int, arg1), psx.addr(script), @as(c_int, script[0]));
    return 0;
}
