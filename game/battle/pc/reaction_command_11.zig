//! Reaction command 11 (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800E4B10 (pc/src/battle_movement.c)
/// Clears the reaction word (0x228), copies the tile at 0x34 to 0x170 and
/// issues actor command 11.
pub fn reactionCommand11(state: *ai.vs_battle_aiState) callconv(.c) void {
    state.counter = 0;
    state.next.movementPosition.raw = state.tile.raw;
    c.vs_battle_aiIssueActorCommand(state, 11);
}
