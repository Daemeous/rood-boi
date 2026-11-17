//! Draws the move packet at the head of the scratchpad's packet slot and advances the slot past it (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AACDC (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Sets up a 18x36 move packet at (470, 384), draws it to (16, 400), and advances the packet slot past it.
pub fn func_800AACDC() callconv(.c) void {
    var sp10: v.RECT = .{ .x = 470, .y = 384, .w = 18, .h = 36 };

    const temp_s0: [*c]c.DR_MOVE = @ptrCast(@alignCast(common.scratch.get().unk0));

    c.pc_frame_move(@ptrCast(temp_s0), @ptrCast(&sp10), 16, 400);

    // One DR_MOVE past the packet.
    common.scratch.get().unk0 = temp_s0 + 1;
}
