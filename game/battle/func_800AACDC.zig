//! Draws the move packet at the head of the scratchpad's packet slot and advances the slot past it (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AACDC (src/BATTLE/BATTLE.PRG/40564.c)
/// Sets up a 18x36 move packet at (470, 384), draws it to (16, 400), and advances the packet slot past it.
pub fn func_800AACDC() callconv(.c) void {
    var sp10: v.RECT = .{ .x = 470, .y = 384, .w = 18, .h = 36 };

    // Packet slot at scratchpad 0x1F800000: a 4-byte guest address on every target.
    const slot: *u32 = @ptrFromInt(0x1F800000);
    const temp_s0: [*c]c.DR_MOVE = @ptrFromInt(@as(usize, slot.*));

    c.SetDrawMove(temp_s0, psx.cptr(c.RECT, &sp10), 16, 400);
    c.func_8007A824(temp_s0);

    // psx_addr(temp_s0 + 1): one DR_MOVE past the packet.
    slot.* = psx.addr(temp_s0 + 1);
}
