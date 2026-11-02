//! Battle helper: passes a three-byte record and its slot to func_800A9C54, then marks the slot in D_800F4B70 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B7A98 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes a three-byte record and its slot to func_800A9C54, then marks the slot in D_800F4B70.
pub fn func_800B7A98(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = arg1;
    // The fourth byte is 0xFF; the record's last byte is uninitialised in the original.
    var sp10: [16]u8 = undefined;
    const short_arg: c_ushort = psx.conv(c_ushort, c.vs_battle_getShort(arg0 + 1));
    const temp_s0: c_ushort = psx.conv(c_ushort, c.func_800BFE50(short_arg));
    sp10[0] = arg0[3];
    sp10[1] = arg0[4];
    sp10[2] = arg0[5];
    sp10[3] = 0xFF;
    if (v.g.D_800F4C2C().* == 2) {
        c.func_800A9C54(temp_s0, @ptrCast(&sp10), 0);
    } else {
        const third: c_int = if (arg0[6] != 0xFF) @as(c_int, arg0[6]) else -1;
        c.func_800A9C54(temp_s0, @ptrCast(&sp10), third);
    }
    c.func_8009DC8C(temp_s0);
    v.g.D_800F4B70()[temp_s0] = 1;
    return 0;
}
