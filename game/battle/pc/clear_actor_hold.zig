//! Clearing an actor hold (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: vs_battle_aiClearActorAiNavigationReset (pc/src/battle_movement.c)
/// Clears actor `id & 0xFF`'s hold flag (0x1A); an empty slot does nothing.
pub fn clearActorHold(id: c_int) callconv(.c) void {
    const state: *ai.vs_battle_aiState = @ptrCast(@alignCast(psx.at(v.g.vs_battle_aiActorAiStates(), id & 0xff).* orelse return));
    state.f1A = 0;
}
