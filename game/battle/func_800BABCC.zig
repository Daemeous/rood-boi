//! Battle script helper: looks up a short from the operand and calls one of two host functions by its flag byte (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

extern fn func_800BFE50(c_int) c_int;

/// orig: func_800BABCC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Maps the short at operand+2 through func_800BFE50, then calls func_8006C480 or func_8006C4A4 with it and operand byte 4.
pub fn func_800BABCC(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const temp_v0: c_short = psx.conv(c_short, func_800BFE50(@as(c_int, c.vs_battle_getShort(arg0 + 2))));

    if (arg0[1] != 0) {
        c.func_8006C480(@as(c_int, temp_v0), @as(c_int, arg0[4]));
    } else {
        c.func_8006C4A4(@as(c_int, temp_v0), @as(c_int, arg0[4]));
    }
    return 0;
}
