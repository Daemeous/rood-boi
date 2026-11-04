//! Sending an actor to a tile (D2: 7C7E0.s; oracle pc/src/actor_update.c).
const c = @import("c_actor_update");
const v = @import("views_actor_update");
const psx = @import("common").psx;

/// orig: route_init (pc/src/actor_update.c)
/// Copies actor `id`'s navigation state (0x484 bytes) into `route` and
/// resets its route fields.
fn routeInit(id: c_uint, route: [*]u8) void {
    _ = c.vs_main_memcpy(route, v.g.D_800F5878()[id], 0x484);
    route[0x88] = @truncate(id);
    psx.byteAt(i16, route, 0xb0).* = 0x140;
    route[0xb2] = 3;
    psx.byteAt(c_uint, route, 0x190).* = 0;
    route[0x19a] = 0;
    route[0x29] = 3;
    route[0x23] = 1;
}

/// orig: func_800E527C (pc/src/actor_update.c)
/// Sends actor `id` towards (x, z): releases its reservations, allocates its
/// route record on first use (0x484 bytes) and initialises it with the
/// target. 0 without a grid, a state or memory. As in the assembly, an
/// existing route record is left alone: the state record is copied onto
/// itself and takes the target instead.
pub fn startActorRoute(id: c_uint, x: c_int, z: c_int) callconv(.c) c_int {
    const target: c_uint = (@as(c_uint, @bitCast(z)) << 16) | @as(c_uint, @bitCast(x));
    if (id >= 17) c.pc_unimplemented("actor navigation index");
    const state: ?[*]u8 = @ptrCast(v.g.D_800F5878()[id]);
    if (v.g.D_800F58B8().* == 0 or state == null) return 0;
    c.func_800D82CC(state);
    c.func_800D821C(state);
    c.func_800DBF00(state);
    var record = state.?;
    if (psx.byteAt(u32, state.?, 0x18c).* == 0) {
        record = @ptrCast(c.vs_main_allocHeapR(0x484) orelse return 0);
        psx.byteAt(u32, state.?, 0x18c).* = psx.addr(record);
        c.func_800E4288(record, 0x484);
    }
    routeInit(state.?[0x88], record);
    psx.byteAt(c_uint, record, 0x160).* = target;
    return 1;
}
