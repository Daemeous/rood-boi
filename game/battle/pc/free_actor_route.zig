//! Releasing an actor's route record (D2: 7C7E0.s; oracle pc/src/actor_update.c).
const c = @import("c_actor_update");
const v = @import("views_actor_update");
const psx = @import("common").psx;

/// orig: func_800E5240 (pc/src/actor_update.c)
/// Frees actor `id`'s route record (the guest pointer at state +0x18C) and
/// clears the pointer; nothing without a grid, a state or a record.
pub fn freeActorRoute(id: c_uint) callconv(.c) void {
    if (id >= 17) c.pc_unimplemented("actor navigation index");
    const state = v.g.D_800F5878()[id];
    if (v.g.D_800F58B8().* == 0 or state == null) return;
    const route: ?*anyopaque = @ptrFromInt(@as(usize, psx.byteAt(u32, state, 0x18c).*));
    if (route == null) return;
    psx.byteAt(u32, state, 0x18c).* = 0;
    c.vs_main_freeHeapR(route);
}
