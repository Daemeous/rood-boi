//! Passes a mapped short from arg0+1 and the byte at arg0[3] to func_8007C8A4 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800BA35C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007C8A4 with func_800BFE50 of the short at arg0+1 and the byte at arg0[3]; returns 0.
pub fn func_800BA35C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8007C8A4(
        c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(arg0 + 1))),
        @as(c_int, arg0[3]),
    );
    return 0;
}
