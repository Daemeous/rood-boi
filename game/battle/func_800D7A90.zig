//! Guest address of a block-2 entry: the block's base plus a 16-bit offset read from the block
//! (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: func_800D7A90 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Returns the address of a block-2 entry: the block base plus the u16 at word `arg0 + 2`.
pub fn func_800D7A90(arg0: c_int) callconv(.c) ?*anyopaque {
    const base: *u16 = psx.get(v.g.D_800F569C().*.?.block2Data).?;
    // arg0 + 2 wraps as int; the C does not check the index against the block size.
    const off: u16 = psx.at(base, arg0 +% 2).*;
    // (u_char*)base + off: guest address arithmetic in 32 bits.
    const addr: u32 = psx.addr(base) +% @as(u32, off);
    return @ptrFromInt(@as(usize, addr));
}
