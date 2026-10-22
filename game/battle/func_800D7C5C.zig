//! Runs the handler of each set flag bit 24..31 of D_800F5874 and clears it, then frees the pfile buffer (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// orig: func_800D7C5C (src/BATTLE/BATTLE.PRG/6F014.c)
/// Runs the handler of each set flag bit 24..31 of D_800F5874 and clears it, then frees the pfile buffer.
pub fn func_800D7C5C() callconv(.c) void {
    var i: c_int = 24;
    while (i < 32) : (i +%= 1) {
        const flags: c_int = v.g.D_800F5874().*;
        // i is 24..31: the shift amount is always in range.
        const bit: c_int = @bitCast(@as(c_uint, 1) << @intCast(i));
        if ((flags & bit) != 0) {
            _ = c.func_8007E0A8(psx.conv(c_uint, i), 1, 2);
            // Reads D_800F5874 again: the handler may have changed it.
            v.g.D_800F5874().* &= ~bit;
        }
    }
    if (v.g.vs_battle_pfileBuf().*) |buf| {
        c.vs_main_freeHeapR(buf);
        v.g.vs_battle_pfileBuf().* = null;
    }
}
