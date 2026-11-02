//! Resolves two script shorts into a battle entry, runs the scratch lookup on it and marks the entry done (src/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B7EF0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Resolves the entry named by the script's first short, looks up the second short's record into a local, and marks the entry done.
pub fn func_800B7EF0(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // sp10 is uninitialised in the original; zeroed here, and the lookup writes what the scratch call reads.
    var sp10: v.func_8006EBF8_t = std.mem.zeroes(v.func_8006EBF8_t);

    // func_800BFE50 takes a u_short: the script short is converted as C converts it.
    const temp_s0: c_int = c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(arg0 + 1)));
    _ = c.func_800A1108(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(arg0 + 3))), &sp10);

    var var_a2: c_int = -1;
    const temp_s0_2: c_int = temp_s0 & 0xFFFF;
    if (arg0[5] != 0xFF) {
        var_a2 = @as(c_int, arg0[5]);
    }
    _ = c.func_800A9CDC(temp_s0_2, @ptrCast(&sp10.unk0.unk0.fields), var_a2);
    // temp_s0_2 is 0..65535 after the mask: the index is in range of the table.
    v.g.D_800F4B70()[@as(usize, @intCast(temp_s0_2))] = 1;
    return 0;
}
