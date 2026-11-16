//! Leaving the room's encounter by actor id (D2: 6FA6C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiDisengageActorById (pc/src/battle_movement.c)
/// Makes the AI entity of actor `id` leave the encounter (vs_battle_aiDisengageActor);
/// id 0 and an empty slot do nothing.
pub fn leaveEncounterById(id: c_int) callconv(.c) void {
    if (id == 0) return;
    if (psx.at(v.g.vs_battle_aiActorAiStates(), id).*) |entity| c.vs_battle_aiDisengageActor(entity);
}
