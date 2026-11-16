//! Alerting an actor (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E4C64 (pc/src/battle_movement.c)
/// Sets actor `id`'s 0x13F countdown to 30 and continues into vs_battle_aiRememberAiActionTarget
/// with that actor; id 0 and empty slots do nothing.
pub fn alertActor(id: c_int) callconv(.c) void {
    if (id == 0) return;
    const state: *ai.vs_battle_aiState = @ptrCast(@alignCast(psx.at(v.g.vs_battle_aiActorAiStates(), id).* orelse return));
    state.minDelay = 30;
    c.vs_battle_aiRememberAiActionTarget(state);
}
