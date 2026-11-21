//! Actor navigation scratch frame (D2: 7DDDC.s; oracle pc/src/actor_update.c).
const c = @import("c_actor_update");

/// orig: vs_battle_aiCacheActorAiWorkingState (pc/src/actor_update.c)
/// Fills the AI movement frame (vs_move) and the navigation pointers (vs_nav).
pub fn fillActorScratch() callconv(.c) void {
    c.pc_actor_scratch();
}
