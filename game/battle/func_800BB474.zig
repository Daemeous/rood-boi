//! Per-slot countdown in the battle table D_800F4C58 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: func_800BB474 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Arms slot `arg1` from `arg0[1]` when it is idle, else counts it down; returns 1 while it runs.
pub fn func_800BB474(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    const slot = psx.at(v.g.D_800F4C58(), arg1);
    if (slot.* == 0) {
        slot.* = psx.conv(i16, arg0[1]);
        return 1;
    }
    slot.* = psx.conv(i16, @as(c_int, slot.*) -% 1);
    // The C shifts the promoted int left by 16 (wrapping); the sign test is on that product.
    if (@as(c_int, slot.*) *% 0x10000 <= 0) {
        slot.* = 0;
        return 0;
    }
    return 1;
}
