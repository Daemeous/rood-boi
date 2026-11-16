//! Leaving the engaged state (D2: 7399C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");

/// orig: vs_battle_aiEndAshleyAlliance (pc/src/battle_movement.c)
/// Leaves the engaged state (0x13): restores the saved movement callback
/// (0x6C -> 0x68) and, unless 0x30 is set, queues the recovery motion.
pub fn leaveEngaged(state: *ai.vs_battle_aiState) callconv(.c) void {
    state.unk126 = 0;
    if (state.flag13 == 0) return;
    state.flag13 = 0;
    c.vs_battle_aiSetActorAwarenessIndicator(state.id, 0x40);
    c.vs_battle_aiInitializeActorAiRelations(state);
    const callback = state.savedCallback;
    state.savedCallback = null;
    if (callback != null) state.callback = callback;
    if (state.f30 != 0) return;
    c.vs_battle_aiSetTemporaryAwarenessIndicator(state, 6, 0x28);
    c.func_800E48A8(state, 2, 0x80);
}
