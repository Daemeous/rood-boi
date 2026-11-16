//! Holding an actor (D2: 7DE94.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E6764 (pc/src/battle_movement.c)
/// Releases actor `id`'s tile reservation (vs_battle_aiReleaseActorAiReservations) and marks it held
/// (0x1A = 1); an empty slot does nothing.
pub fn holdActor(id: c_int) callconv(.c) void {
    const state: *ai.vs_battle_aiState = @ptrCast(@alignCast(psx.at(v.g.vs_battle_aiActorAiStates(), id).* orelse return));
    c.vs_battle_aiReleaseActorAiReservations(state);
    state.f1A = 1;
}
