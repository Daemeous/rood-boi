//! Battle drawing area: the 320 x 240 view at the frame buffer `arg1` names (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const sprite_item = @import("common").sprite_item;

/// orig: vs_render_setGimFramebufferDrawArea (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets the drawing area to the 320 x 240 view of the frame buffer at x = `arg1` (0 or 320) and the draw offset there.
pub fn vs_render_setGimFramebufferDrawArea(arg0: [*c]c_uint, arg1: c_int) callconv(.c) void {
    sprite_item.area(arg0, arg1 & 0x3FF, 0, 320, 240, .{ arg1, 0 });
}
