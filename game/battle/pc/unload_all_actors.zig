//! Unloading every actor (D2: 7E05C.s; oracle pc/src/actor_cleanup.c).
const c = @import("c_actor_cleanup");
const v = @import("views_actor_cleanup");
const psx = @import("common").psx;

/// orig: func_800E6F1C (pc/src/actor_cleanup.c)
/// Unloads the linked actors in order, then clears the navigation grid
/// pointers (D_800F58D0, D_800F58B8, D_800F58C0) and frees and clears the
/// grid record D_800F58BC.
pub fn unloadAllActors() callconv(.c) void {
    var node: ?[*]u8 = @ptrCast(v.g.vs_battle_actors()[0]);
    while (node) |n| : (node = @ptrFromInt(@as(usize, psx.byteAt(u32, n, 0).*))) c.func_800E7454(n);
    v.g.D_800F58D0().* = 0;
    v.g.D_800F58B8().* = 0;
    v.g.D_800F58C0().* = 0;
    c.vs_main_freeHeap(@ptrFromInt(@as(usize, v.g.D_800F58BC().*)));
    v.g.D_800F58BC().* = 0;
}
