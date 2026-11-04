//! Per-frame actor navigation update (D2: 7C7E0.s; oracle pc/src/actor_update.c).
const c = @import("c_actor_update");
const v = @import("views_actor_update");
const psx = @import("common").psx;

/// orig: route_target (pc/src/actor_update.c)
/// Aims an actor state at its target (word 0x160): the state of an actor
/// with byte 0x19B set continues in pc_route_continuation; otherwise the
/// target tile and its 128-unit tile are stored and the actor turns towards
/// it (func_800E19FC).
fn routeTarget(state: [*]u8) void {
    if (state[0x19b] != 0) {
        c.pc_route_continuation(state, psx.addr(state));
        return;
    }
    const target = psx.byteAt(c_uint, state, 0x160).*;
    const x = target & 65535;
    const z = target >> 16;
    psx.byteAt(c_uint, state, 0x17c).* = x;
    psx.byteAt(c_uint, state, 0x180).* = z;
    psx.byteAt(c_uint, state, 0x168).* = (x >> 7) | ((z >> 7) << 16);
    const angle = c.ratan2(@as(c_int, @bitCast(x)) -% psx.byteAt(i16, state, 0x4c).*, @as(c_int, @bitCast(z)) -% psx.byteAt(i16, state, 0x50).*);
    c.func_800E19FC(state, angle);
}

/// orig: func_800E5308 (pc/src/actor_update.c)
/// Updates every linked actor that is not flagged 0xC0: refreshes the
/// scratchpad frame, then for each actor with an AI state (found through the
/// scratchpad's state table) counts down its timers, copies the actor
/// model's tile and position into the state, and either aims it at its
/// target (idle and far), retargets a moving one that left its tile, or
/// updates it and releases its route.
pub fn updateActorNavigation() callconv(.c) void {
    c.func_800E65DC();
    const scratch: [*]c_uint = @ptrFromInt(0x1f800000);
    var actor: ?[*]u8 = @ptrCast(v.g.vs_battle_actors()[0]);
    var visited: c_uint = 0;
    while (actor) |a| : (actor = @ptrFromInt(@as(usize, psx.byteAt(u32, a, 0).*))) {
        visited +%= 1;
        if (visited > 17) c.pc_unimplemented("actor navigation list cycle");
        if (a[8] & 0xc0 != 0) continue;
        const id = psx.byteAt(c_uint, a, 4).*;
        if (id >= 17) c.pc_unimplemented("actor navigation index");
        // An actor without an AI state (no attribute bit 0/2) has a null entry;
        // the original then loads its route record from address 0x18C, the PS1
        // kernel area (read here through guest RAM).
        const entry = scratch[0x37c / 4 + id];
        const base: [*]u8 = @ptrFromInt(@as(usize, if (entry != 0) entry else 0x8000_0000));
        const state_address = psx.byteAt(u32, base, 0x18c).*;
        scratch[0x3fc / 4] = @bitCast(c.func_800E45B4());
        if (state_address == 0) continue;
        const state: [*]u8 = @ptrFromInt(@as(usize, state_address));
        state[0x19a] = 0;
        const countdown = psx.byteAt(u16, state, 0x156);
        if (countdown.* != 0) countdown.* -%= 1;
        const flags = psx.byteAt(c_uint, state, 0x190).*;
        var steps = (flags >> 5) & 127;
        if (steps != 0) steps -%= 1;
        state[6] = 1;
        state[1] = 0;
        state[0x15c] = 16;
        state[2] = 0;
        state[3] = 0;
        a[3] &= 0xfc;
        psx.byteAt(c_uint, state, 0x190).* = (flags & ~@as(c_uint, 0xfe0)) | (steps << 5);
        const model = v.g.D_800F4538()[id];
        if (model == null) c.pc_unimplemented("actor navigation model missing");
        const tile = psx.byteAt(c_uint, model.?, 0x5c).*;
        var x = psx.byteAt(c_uint, model.?, 0x1c).*;
        var z = psx.byteAt(c_uint, model.?, 0x20).*;
        psx.byteAt(c_uint, state, 0x34).* = tile;
        psx.byteAt(c_uint, state, 0x4c).* = x;
        psx.byteAt(c_uint, state, 0x50).* = z;
        const cell = ((tile >> 16) & 255) *% 32 +% (tile & 255);
        if (cell >= 1024) c.pc_unimplemented("actor navigation tile outside grid");
        const ground: c_uint = @as(*align(1) u16, @ptrFromInt(@as(usize, v.g.D_800F58B8().*) +% @as(usize, cell) *% 2)).* & 15;
        x &= 65535;
        z &= 65535;
        psx.byteAt(c_uint, state, 0x120).* = (x >> 6) | ((z >> 6) << 16);
        state[0x154] = @truncate(ground);
        state[0x132] = @truncate(ground);
        scratch[0x3d0 / 4] = 0;
        const target = psx.byteAt(c_uint, state, 0x160).*;
        if (state[0x125] == 0) {
            if (c.func_800E4660(@bitCast((target & 65535) -% x), 0, @bitCast((target >> 16) -% z)) >= 1024) {
                routeTarget(state);
                continue;
            }
        } else if ((psx.byteAt(c_uint, state, 0x168).* & 0xff00ff) != (tile & 0xff00ff)) {
            psx.byteAt(c_uint, state, 0x168).* = ((target & 65535) >> 7) | ((target >> 23) << 16);
            c.func_800E50A0(state, 0, 0);
            continue;
        }
        c.func_800DEEFC(state, 0);
        c.func_800E5240(state[0x88]);
    }
    c.func_800D9538();
}
