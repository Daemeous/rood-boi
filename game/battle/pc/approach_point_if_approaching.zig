//! Approach point refresh while approaching (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");

/// orig: func_800E48F8 (pc/src/battle_movement.c)
/// Refreshes the approach point (vs_battle_aiRefreshAiTargetTile) while in approach mode (0x89 == 1).
pub fn refreshApproachPointIfApproaching(state: *ai.vs_battle_aiState) callconv(.c) void {
    if (state.unk89 == 1) c.vs_battle_aiRefreshAiTargetTile(state);
}
