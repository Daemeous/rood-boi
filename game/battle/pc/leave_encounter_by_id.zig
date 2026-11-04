//! Leaving the room's encounter by actor id (D2: 6FA6C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800D82A8 (pc/src/battle_movement.c)
/// Makes the AI entity of actor `id` leave the encounter (func_800D82CC);
/// id 0 and an empty slot do nothing.
pub fn leaveEncounterById(id: c_int) callconv(.c) void {
    if (id == 0) return;
    if (psx.at(v.g.D_800F5878(), id).*) |entity| c.func_800D82CC(entity);
}
