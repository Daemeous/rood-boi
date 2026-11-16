//! Aiming at an actor (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiTrackActorPosition (pc/src/battle_movement.c)
/// Aims at actor `id`'s position (0x17C/0x180) and tile (0x168); on a plan
/// (vs_battle_aiUpdateTargetFacing) enters mode 6.
pub fn aimAtActor(state: *ai.vs_battle_aiState, id: c_int) callconv(.c) c_int {
    const model: [*]u8 = @ptrCast(psx.at(v.g.vs_actors_actorRecords(), id).*.?);
    state.moveX = psx.byteAt(i16, model, 0x1c).*;
    state.moveZ = psx.byteAt(i16, model, 0x20).*;
    state.destination168.destination = psx.byteAt(u32, model, 0x5c).*;
    return if (c.vs_battle_aiUpdateTargetFacing(state, id) != 0) c.vs_battle_aiSetMovementBehavior(state, 6) else 0;
}
