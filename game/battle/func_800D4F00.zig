//! Unpacks three curve bytes into the block-5 curve entry they select (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4F00 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Reads three curve bytes and stores the packed R, G and B curves in the block-5 entry they select.
pub fn func_800D4F00(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const first = c.func_800D5170(@ptrCast(arg0));
    const second = c.func_800D5170(@ptrCast(arg0));
    const third = c.func_800D5170(@ptrCast(arg0));

    const block = psx.get(v.g.D_800F569C().*.?.block5Data).?;
    // unk4 is a zero-length array; the index is 0..63 and the C indexes past its declared end.
    const dst = psx.at(&block.unk4, first & 0x3F);

    dst.rCurve = psx.conv(u8, (@as(c_int, first) >> 6) +% ((@as(c_int, second) & 0xF) << 2));
    dst.gCurve = psx.conv(u8, (@as(c_int, second) >> 4) +% ((@as(c_int, third) & 3) << 4));
    dst.bCurve = psx.conv(u8, @as(c_int, third) >> 2);
    return 1;
}
