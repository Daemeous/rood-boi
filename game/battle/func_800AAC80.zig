//! Draw-move packet setup: fills the packet at the scratchpad cursor and advances the cursor past it (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AAC80 (src/BATTLE/BATTLE.PRG/40564.c)
/// Sets up a draw-move packet for `rect` at (x, y) and advances the packet cursor past it.
pub fn func_800AAC80(arg0: ?*v.RECT, arg1: c_int, arg2: c_int) callconv(.c) void {
    // Scratchpad slot 0 holds the packet cursor as a 4-byte guest address.
    const cursor: *u32 = @ptrFromInt(0x1F800000);
    const drMove: *v.DR_MOVE = @ptrFromInt(@as(usize, cursor.*));
    c.SetDrawMove(psx.cptr(c.DR_MOVE, drMove), @ptrCast(arg0), arg1, arg2);
    c.func_8007A824(psx.cptr(c.DR_MOVE, drMove));
    const next: *v.DR_MOVE = @ptrFromInt(@intFromPtr(drMove) +% @sizeOf(v.DR_MOVE));
    cursor.* = psx.addr(next);
}
