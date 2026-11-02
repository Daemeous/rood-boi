//! Battle helper that calls the two 4A0A8 routines with table entries and a packed word
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B94D8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007BBD8 with two table entries and a packed 24-bit value when byte 1 is set, else func_8007BCCC.
pub fn func_800B94D8(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    if (arg0[1] != 0) {
        const word: c_uint = (@as(c_uint, arg0[4]) << 24) | (@as(c_uint, arg0[5]) << 16) | (@as(c_uint, arg0[6]) << 8);
        c.func_8007BBD8(
            @as(c_int, v.g.D_800E9BF4()[arg0[3]]),
            @as(c_int, v.g.D_800E9BF8()[arg0[2]]),
            @bitCast(word),
        );
    } else {
        c.func_8007BCCC();
    }
    return 0;
}
