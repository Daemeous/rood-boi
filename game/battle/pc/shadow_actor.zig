//! Actor drop shadow (D2: 4B16C.s; oracle pc/src/actor_shadow.c).
const c = @import("c_actor_shadow");

/// orig: func_800B4B14 (pc/src/actor_shadow.c)
/// Draws the actor's drop shadow with the scratchpad at 0x1F800000.
pub fn shadowActor(actor: ?*anyopaque) callconv(.c) void {
    c.pc_actor_shadow(@ptrCast(actor), @ptrFromInt(0x1f800000));
}
