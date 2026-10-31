//! Battle list helper: decrements the 5-bit count of the previous entry (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: func_800D5048 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Steps the previous entry's `unk14_11` count down by one (wrapping 0 to 31) and returns 1.
pub fn func_800D5048(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    if (psx.get(arg0.?.previous)) |previous| {
        // --unk14_11 on a u5 bitfield: promoted to int, decremented, truncated to 5 bits.
        const count = psx.getBits(previous, "unk14_11");
        psx.setBits(previous, "unk14_11", psx.conv(u5, @as(c_int, count) -% 1));
    }
    return 1;
}
