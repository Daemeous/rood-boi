//! Battle script opcode: looks up a target and passes it, with three script bytes, to func_800A0204 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B76A8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Script opcode: passes a looked-up target and three bytes from the script on to func_800A0204.
pub fn func_800B76A8(arg0: [*c]u8) callconv(.c) c_int {
    const b5: c_int = arg0[5];
    const table = v.g.D_800E9B58();
    _ = c.func_800A0204(
        c.func_800BFE50(psx.conv(c_ushort, c.vs_battle_getShort(arg0 + 1))),
        @as(c_int, table[@intCast(b5 >> 6)]) +% @as(c_int, arg0[3]),
        @as(c_int, arg0[4]),
        b5 & 0x3F,
    );
    return 0;
}
