//! Adds an amount to a halfword of the record that func_8009195C finds
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_2842C");

/// orig: func_80091A78 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Adds `amount` to the halfword at offset 2 of record `index`; returns 1 if the record exists, else 0.
pub fn func_80091A78(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    const found: ?*anyopaque = c.func_8009195C(arg0);
    const temp_v0: ?[*]align(1) u16 = @ptrCast(found);
    if (temp_v0) |record| {
        record[1] = psx.conv(u16, @as(c_int, record[1]) +% arg1);
        return 1;
    }
    return 0;
}
