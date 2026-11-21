//! Actor drop shadow (D2: 4B16C.s; oracle pc/src/actor_shadow.c).
const c = @import("c_actor_shadow");

/// orig: func_800B4B14 (pc/src/actor_shadow.c)
/// Draws the actor's drop shadow, projecting through the actor transform block (vs_xform).
pub fn shadowActor(actor: ?*anyopaque) callconv(.c) void {
    c.pc_actor_shadow(@ptrCast(actor));
}
