//! Actor navigation scratch frame (D2: 7DDDC.s; oracle pc/src/actor_update.c).
const c = @import("c_actor_update");
const pad = @import("common").pad;

/// orig: vs_battle_aiCacheActorAiWorkingState (pc/src/actor_update.c)
/// Fills the scratchpad's actor navigation frame (at 0x1F800000).
pub fn fillActorScratch() callconv(.c) void {
    c.pc_actor_scratch(@ptrFromInt(pad.addr(0)));
}
