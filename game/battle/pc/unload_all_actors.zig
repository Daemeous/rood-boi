//! Unloading every actor (D2: 7E05C.s; oracle pc/src/actor_cleanup.c).
const ai = @import("views_71618");
const c = @import("c_actor_cleanup");
const v = @import("views_actor_cleanup");
const psx = @import("common").psx;

/// orig: vs_battle_aiFreeRoomAiState (pc/src/actor_cleanup.c)
/// Unloads the linked actors in order, then clears the navigation grid
/// pointers (D_800F58D0, D_800F58B8, D_800F58C0) and frees and clears the
/// grid record vs_battle_aiRoomAiState.
pub fn unloadAllActors() callconv(.c) void {
    var node: ?*ai.vs_battle_actor = @ptrCast(@alignCast(v.g.vs_battle_actors()[0]));
    while (node) |n| : (node = @ptrCast(n.next)) c.vs_battle_aiFreeActorAiState(n);
    v.g.D_800F58D0().* = null;
    v.g.D_800F58B8().* = null;
    v.g.D_800F58C0().* = null;
    c.vs_main_freeHeap(v.g.vs_battle_aiRoomAiState().*);
    v.g.vs_battle_aiRoomAiState().* = null;
}
