//! Address of an entry in a pFileBlock3 section (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D5564 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns the address of entry `index` of `section` in the file block, as an offset from the block's start.
pub fn func_800D5564(block: ?*v.pFileBlock3, section: c_int, index: c_int) callconv(.c) ?*anyopaque {
    const entry: *align(1) v.func_800D55A4_t = @ptrCast(c._getPFileBlock3section(@ptrCast(block), section).?);
    const off = psx.at(&entry.unk4, index).unk2;
    return @ptrFromInt(@intFromPtr(block) +% @as(usize, off));
}
