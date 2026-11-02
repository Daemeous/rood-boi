//! Compares byte 1 of a record against its masked value (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BB41C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when byte 1 of `arg0` has bits set outside the mask from func_800BD610, else 0.
pub fn func_800BB41C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // The C reads arg0[1] before the call: both loads precede func_800BD610.
    const flags: c_int = arg0[1];
    const mask: c_int = c.func_800BD610();
    return @intFromBool(flags != (flags & mask));
}
