//! Reaction command 12 (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800E4B18 (pc/src/battle_movement.c)
/// The same with actor command 12.
pub fn reactionCommand12(state: *ai.vs_battle_aiState) callconv(.c) void {
    state.counter = 0;
    state.next.movementPosition.raw = state.tile.raw;
    c.vs_battle_aiIssueActorCommand(state, 12);
}
