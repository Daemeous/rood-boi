//! Whether an actor's route has reached its target (D2: 7C7E0.s; oracle pc/src/actor_update.c).
const c = @import("c_actor_update");
const v = @import("views_actor_update");
const psx = @import("common").psx;

/// orig: func_800E5154 (pc/src/actor_update.c)
/// 0 when actor `id`'s route record is on its target tile (the tile word the
/// record points at, masked 0xFF00FF, equals the target at +0x168), 1 while
/// it is elsewhere, 2 without a grid, a state or a route.
pub fn routeTargetState(id: c_uint) callconv(.c) c_int {
    if (id >= 17) c.pc_unimplemented("actor navigation index");
    const state = v.g.D_800F5878()[id];
    if (v.g.D_800F58B8().* == 0 or state == null) return 2;
    const route: ?[*]u8 = @ptrFromInt(@as(usize, psx.byteAt(u32, state, 0x18c).*));
    if (route == null) return 2;
    const tile_word: *align(1) const c_uint = @ptrFromInt(@as(usize, psx.byteAt(u32, route.?, 0x64).*));
    const tile = tile_word.* & 0xff00ff;
    return @intFromBool(tile != (psx.byteAt(c_uint, route.?, 0x168).* & 0xff00ff));
}
