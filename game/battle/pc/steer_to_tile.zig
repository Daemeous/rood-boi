//! Steering to the packed target tile (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiMoveActorTowardDestinationTile (pc/src/battle_movement.c)
/// With a gate held (0x19B) continues the route through it; on the packed
/// target tile (0x168 against 0x34, bits 8-15 ignored) calls vs_battle_aiRecoverMovement;
/// otherwise stores the target's centre at 0x17C/0x180 and calls vs_battle_aiUpdateNavigationAreaMovement.
pub fn steerToTile(state: *ai.vs_battle_aiState, angle: c_int) callconv(.c) void {
    if (state.gate != 0) {
        c.pc_route_continuation(state, 0);
        return;
    }
    const target: c_uint = state.destination168.destination & 0xff00ff;
    if (target == (state.tile.raw & 0xff00ff)) {
        c.vs_battle_aiRecoverMovement(state, angle);
        return;
    }
    state.moveX = @bitCast((target & 0xff) *% 128 +% 64);
    state.moveZ = @bitCast((target >> 9) +% 64);
    c.vs_battle_aiUpdateNavigationAreaMovement(state, angle);
}
