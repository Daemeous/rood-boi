//! Battle overlay unit 4A0A8: a slot check and release (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BA6E0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Checks the slot named by arg0[1]; returns 1 if its second check passes, else releases it and returns 0.
pub fn func_800BA6E0(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const temp_s0: c_int = arg0[1];
    if (c.func_8004550C(temp_s0) != 0) {
        if (c.func_800454B8(temp_s0) != 0) {
            return 1;
        }
        _ = c.func_80044DF4(temp_s0);
    }
    return 0;
}
