//! Battle script opcode helper: calls func_800A01C8 with a function of the script's short and a table-based value (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B822C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800A01C8 with the short at arg0+1 (through func_800BFE50), a table value plus arg0[3], and arg0[5]'s low six bits.
pub fn func_800B822C(arg0: [*c]u8) callconv(.c) c_int {
    // Argument evaluation order: vs_battle_getShort, then func_800BFE50, then the table read and the rest.
    _ = c.func_800A01C8(
        c.func_800BFE50(psx.conv(c_ushort, c.vs_battle_getShort(arg0 + 1))),
        @as(c_int, v.g.D_800E9B58()[@as(usize, arg0[5] >> 6)]) +% @as(c_int, arg0[3]),
        @as(c_int, arg0[5] & 0x3F),
        0,
    );
    return 0;
}
