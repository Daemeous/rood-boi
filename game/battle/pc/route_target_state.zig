//! Whether an actor's route has reached its target (D2: 7C7E0.s; oracle pc/src/actor_update.c).
const ai = @import("views_71618");
const c = @import("c_actor_update");
const v = @import("views_actor_update");
const psx = @import("common").psx;

/// orig: vs_battle_aiActorNavigationOverrideStatus (pc/src/actor_update.c)
/// 0 when actor `id`'s route record is on its target tile (the tile word the
/// record points at, masked 0xFF00FF, equals the target at +0x168), 1 while
/// it is elsewhere, 2 without a grid, a state or a route.
pub fn routeTargetState(id: c_uint) callconv(.c) c_int {
    if (id >= 17) c.pc_unimplemented("actor navigation index");
    const state = v.g.vs_battle_aiActorAiStates()[id];
    if (v.g.D_800F58B8().* == null or state == null) return 2;
    const st: *ai.vs_battle_aiState = @ptrCast(@alignCast(state.?));
    const route = st.route orelse return 2;
    const tile_word: *align(1) const c_uint = @ptrCast(route.tilePointer.words.?);
    const tile = tile_word.* & 0xff00ff;
    return @intFromBool(tile != (route.destination168.destination & 0xff00ff));
}
