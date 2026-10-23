//! Stores two values into fields unk138 and unk12A of a battle state block (src/BATTLE/BATTLE.PRG/6FA60.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6FA60");

/// orig: func_800D8260 (src/BATTLE/BATTLE.PRG/6FA60.c)
/// Writes `wide` to the block's unk138 halfword and `narrow` to its unk12A byte.
pub fn func_800D8260(block: ?*v.func_800DEEA4_t2, wide: c_int, narrow: c_int) callconv(.c) void {
    const state = block.?;
    state.unk138 = psx.conv(u16, wide);
    state.unk12A = psx.conv(u8, narrow);
}
