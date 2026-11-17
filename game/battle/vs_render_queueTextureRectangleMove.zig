//! Draw-move packet setup: fills the packet at the scratchpad cursor and advances the cursor past it (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_render_queueTextureRectangleMove (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Sets up a draw-move packet for `rect` at (x, y) and advances the packet cursor past it.
pub fn vs_render_queueTextureRectangleMove(arg0: ?*v.RECT, arg1: c_int, arg2: c_int) callconv(.c) void {
    const drMove: *v.DR_MOVE = @ptrCast(@alignCast(common.scratch.get().unk0));
    c.pc_frame_move(@ptrCast(drMove), @ptrCast(arg0), arg1, arg2);
    common.scratch.get().unk0 = psx.at(drMove, 1);
}
