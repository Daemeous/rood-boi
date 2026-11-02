//! A section of a P-file block 3 by its offset table (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: _getPFileBlock3section (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The block's address plus its `index`th u16 offset (unchecked: the C indexes the unsized table).
pub fn getPFileBlock3section(block: ?*v.pFileBlock3, index: c_int) callconv(.c) ?*anyopaque {
    const b = block.?;
    const offset: usize = psx.at(&b.offsets, index).*;
    return @ptrFromInt(@intFromPtr(b) +% offset);
}
