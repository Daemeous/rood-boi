//! Clamps a value to the range spanned by two others (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;

/// orig: vs_battle_clamp (src/BATTLE/BATTLE.PRG/2842C.c)
/// Clamps `arg0` to the range between `arg1` and `arg2` (low and high taken from their 16-bit values).
pub fn clamp(arg0: c_short, arg1: c_int, arg2: c_int) callconv(.c) c_int {
    var var_t0: c_short = undefined;
    var var_v1: c_short = undefined;

    // The shifted compare is signed 32-bit and wraps: `<<` discards the bits shifted out.
    if ((arg1 << 16) < (arg2 << 16)) {
        var_t0 = psx.conv(c_short, arg1);
        var_v1 = psx.conv(c_short, arg2);
    } else {
        var_t0 = psx.conv(c_short, arg2);
        var_v1 = psx.conv(c_short, arg1);
    }
    if (@as(c_int, arg0) < @as(c_int, var_t0)) {
        return @as(c_int, var_t0);
    }
    if (@as(c_int, var_v1) < @as(c_int, arg0)) {
        return @as(c_int, var_v1);
    }
    return @as(c_int, arg0);
}
