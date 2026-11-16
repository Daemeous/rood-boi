//! Releasing an actor's route record (D2: 7C7E0.s; oracle pc/src/actor_update.c).
const c = @import("c_actor_update");
const v = @import("views_actor_update");
const ai = @import("views_71618");

/// orig: vs_battle_aiFreeActorNavigationOverride (pc/src/actor_update.c)
/// Frees actor `id`'s route record (the guest pointer at state +0x18C) and
/// clears the pointer; nothing without a grid, a state or a record.
pub fn freeActorRoute(id: c_uint) callconv(.c) void {
    if (id >= 17) c.pc_unimplemented("actor navigation index");
    const state = v.g.vs_battle_aiActorAiStates()[id];
    if (v.g.D_800F58B8().* == 0 or state == null) return;
    const st: *ai.vs_battle_aiState = @ptrCast(@alignCast(state.?));
    const route = st.route orelse return;
    st.route = null;
    c.vs_main_freeHeapR(route);
}
