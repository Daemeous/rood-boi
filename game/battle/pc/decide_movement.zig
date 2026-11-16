//! The per-frame movement decision (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiDispatchMovementBehavior (pc/src/battle_movement.c)
/// Per-frame movement decision. Dispatch (vs_battle_aiTryExecutePlannedActions), unstick
/// (vs_battle_aiTryRecoverBlockedMovement) and the state's own callback at 0x68 each may claim the
/// frame; otherwise the actor wanders. The callback must be a native
/// function (pc_require_native_callback checks it).
pub fn decideMovement(state: *ai.vs_battle_aiState) callconv(.c) void {
    if (c.vs_battle_aiTryExecutePlannedActions(state) != 0 or c.vs_battle_aiTryRecoverBlockedMovement(state) != 0) return;
    const callback: ?*const fn (?*anyopaque) callconv(.c) c_int = @ptrCast(state.callback);
    if (callback) |call| {
        c.pc_require_native_callback(psx.addr(call), "actor movement callback", 0);
        if (call(state) != 0) return;
    }
    c.vs_battle_aiWanderAroundSavedCenter(state);
}
