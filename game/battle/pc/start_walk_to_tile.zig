//! Starting a walk to a tile (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiMoveToTile (pc/src/battle_movement.c)
/// Enters movement mode 2 to walk to `tile` (stored at 0x168): within
/// `range` of the tile centre (L800E5030 in 7C7E0.s; height from
/// vs_field_groundHeightAtPackedTile, squared distances compared unsigned) the walk finishes
/// (vs_battle_aiIssueActorCommand 0), otherwise it moves (vs_battle_aiDispatchAiLocomotionMode 0, heading 0).
pub fn startWalkToTile(state: *ai.vs_battle_aiState, tile_in: c_uint, range: c_int) callconv(.c) void {
    state.destination168.destination = tile_in;
    _ = c.vs_battle_aiSetMovementBehavior(state, 2);
    const tile = state.destination168.destination;
    const y = c.vs_field_groundHeightAtPackedTile(tile);
    const distance: c_uint = @bitCast(c.vs_core_mathSquaredVectorLength(
        @as(c_int, @bitCast((tile & 0xff) << 7)) +% 64 -% state.position.xyz[0],
        y -% state.position.xyz[1],
        @as(c_int, @bitCast(((tile >> 16) & 0xff) << 7)) +% 64 -% state.position.xyz[2],
    ));
    if (distance < @as(c_uint, @bitCast(range *% range))) {
        c.vs_battle_aiIssueActorCommand(state, 0);
    } else {
        c.vs_battle_aiDispatchAiLocomotionMode(state, 0, 0);
    }
}
