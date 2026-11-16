//! Remembering an actor's best target (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: vs_battle_aiRememberAiActionTarget (pc/src/battle_movement.c)
/// Remembers the best candidate's target when there is one (0x470 positive)
/// and the actor is not locked (0x19): 0x38 = target << 24 | current tile,
/// 0x3C = the target's tile, 0x40 = 900-frame memory.
pub fn rememberTarget(state: *ai.vs_battle_aiState) callconv(.c) void {
    if (@as(c_int, @bitCast(state.count470)) <= 0 or state.f19 != 0) return;
    const candidate: [*]const c_uint = @ptrCast(state.entries.candidates[0].?);
    const target: c_uint = candidate[3] & 0xf;
    state.previous.stuckMovementPosition.raw = (target << 24) | (state.tile.raw & 0xffffff);
    const model: [*]u8 = @ptrCast(v.g.vs_actors_actorRecords()[target].?);
    state.unk3C = psx.byteAt(c_uint, model, 0x5c).*;
    state.unk40 = 900;
}
