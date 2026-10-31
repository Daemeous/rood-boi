//! Sets, clears or replaces flag bits in the per-index table D_800F2260 (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: func_80095B7C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Sets, clears or replaces the low seven bits of D_800F2260[index]; index 0 fills the table with value.
pub fn func_80095B7C(index: c_int, value: c_int) callconv(.c) void {
    if (index != 0) {
        const slot = psx.at(v.g.D_800F2260(), index);
        if ((value & 0x80) != 0) {
            slot.* |= 0x80;
            return;
        }
        if ((value & 0x40) != 0) {
            slot.* &= 0x7F;
            return;
        }
        slot.* &= 0x80;
        slot.* |= psx.conv(u8, value & 0x7F);
        return;
    }
    const memset = @extern(*const fn (?*anyopaque, c_int, usize) callconv(.c) ?*anyopaque, .{ .name = "memset" });
    _ = memset(@ptrCast(v.g.D_800F2260()), value, 0x10);
}
