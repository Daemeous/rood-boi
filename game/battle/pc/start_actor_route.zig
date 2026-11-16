//! Sending an actor to a tile (D2: 7C7E0.s; oracle pc/src/actor_update.c).
const ai = @import("views_71618");
const c = @import("c_actor_update");
const v = @import("views_actor_update");
const psx = @import("common").psx;

/// orig: route_init (pc/src/actor_update.c)
/// Copies actor `id`'s navigation state (a whole state) into `route` and
/// resets its route fields.
fn routeInit(id: c_uint, route: *ai.vs_battle_aiState) void {
    _ = c.vs_main_memcpy(route, v.g.vs_battle_aiActorAiStates()[id], @sizeOf(ai.vs_battle_aiState));
    route.id = @truncate(id);
    route.fB0 = 0x140;
    route.radiusB2 = 3;
    route.movementFlags.raw = 0;
    route.waitTimer = 0;
    route.f29 = 3;
    route.f23 = 1;
}

/// orig: vs_battle_aiSetActorNavigationOverride (pc/src/actor_update.c)
/// Sends actor `id` towards (x, z): releases its reservations, allocates its
/// route record on first use (a whole state) and initialises it with the
/// target. 0 without a grid, a state or memory. As in the assembly, an
/// existing route record is left alone: the state record is copied onto
/// itself and takes the target instead.
pub fn startActorRoute(id: c_uint, x: c_int, z: c_int) callconv(.c) c_int {
    const target: c_uint = (@as(c_uint, @bitCast(z)) << 16) | @as(c_uint, @bitCast(x));
    if (id >= 17) c.pc_unimplemented("actor navigation index");
    const state: ?*ai.vs_battle_aiState = @ptrCast(@alignCast(v.g.vs_battle_aiActorAiStates()[id]));
    if (v.g.D_800F58B8().* == 0 or state == null) return 0;
    c.vs_battle_aiDisengageActor(@ptrCast(state));
    c.vs_battle_aiResetActorAwarenessIndicator(@ptrCast(state));
    c.vs_battle_aiUnlinkEngagedActor(@ptrCast(state));
    var record = state.?;
    if (record.route == null) {
        record = @ptrCast(@alignCast(c.vs_main_allocHeapR(@sizeOf(ai.vs_battle_aiState)) orelse return 0));
        state.?.route = record;
        c.vs_core_memoryClearAlignedWords(record, @sizeOf(ai.vs_battle_aiState));
    }
    routeInit(state.?.id, record);
    record.unk160 = target;
    return 1;
}
