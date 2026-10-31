//! Reads a 16-bit little-endian value as two byte reads through func_800D5170
//! (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D5198 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Reads two bytes through func_800D5170 and returns them as a halfword, low byte first.
pub fn func_800D5198(arg0: ?*v.D_800F53B8_t) callconv(.c) c_ushort {
    const lo: u8 = c.func_800D5170(@ptrCast(arg0));
    const hi: u8 = c.func_800D5170(@ptrCast(arg0));
    return psx.conv(c_ushort, @as(c_int, lo) | (@as(c_int, hi) << 8));
}
