//! Free movement toward a heading (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const pad = @import("common").pad;

/// orig: func_800E4B70 (pc/src/battle_movement.c)
/// Held actors (0x1A) go to vs_battle_aiMoveWithHeightFilteredDirections; with a gate held (0x19B) the route
/// continues through it; otherwise sets byte 2, clears scratchpad byte 0x3D0
/// and starts free movement (vs_battle_aiResolveDirectionalMovement mode 1) at `angle & 0xFFF`.
pub fn freeMovement(state: *ai.vs_battle_aiState, angle: c_int) callconv(.c) void {
    if (state.f1A != 0) {
        c.vs_battle_aiMoveWithHeightFilteredDirections(state, angle);
        return;
    }
    if (state.gate != 0) {
        c.pc_route_continuation(state, 0);
        return;
    }
    state.flags0.b[2] = 1;
    pad.move.cache.bits.flyTested = 0;
    c.vs_battle_aiResolveDirectionalMovement(state, angle & 0xfff, 1);
}
