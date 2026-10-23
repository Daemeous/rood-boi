//! Calls the per-bit handler for each set bit of a 29-bit mask (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const c = @import("c_2842C");

/// orig: func_80093FEC (src/BATTLE/BATTLE.PRG/2842C.c)
/// For each bit i below 29 set in `mask`, calls func_8009406C with message 0xFB, index i and `arg3`.
pub fn func_80093FEC(arg0: c_int, _: c_int, arg2: c_int, arg3: c_int) callconv(.c) c_int {
    var i: c_int = 0;
    var var_s1: c_uint = 1;
    while (i < 29) : ({
        i +%= 1;
        var_s1 <<= 1;
    }) {
        if ((psx.conv(c_uint, arg2) & var_s1) != 0) {
            _ = c.func_8009406C(arg0, 0xFB, i, arg3);
        }
    }
    return 0;
}
