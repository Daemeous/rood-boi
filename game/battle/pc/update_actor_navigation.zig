//! Per-frame actor navigation update (D2: 7C7E0.s; oracle pc/src/actor_update.c).
const ai = @import("views_71618");
const c = @import("c_actor_update");
const v = @import("views_actor_update");
const psx = @import("common").psx;
const pad = @import("common").pad;

/// orig: route_target (pc/src/actor_update.c)
/// Aims an actor state at its target (word 0x160): the state of an actor
/// with byte 0x19B set continues in pc_route_continuation; otherwise the
/// target tile and its 128-unit tile are stored and the actor turns towards
/// it (vs_battle_aiUpdateNavigationAreaMovement).
fn routeTarget(state: *ai.vs_battle_aiState) void {
    if (state.gate != 0) {
        c.pc_route_continuation(state, psx.addr(state));
        return;
    }
    const target = state.unk160;
    const x = target & 65535;
    const z = target >> 16;
    state.moveX = @bitCast(x);
    state.moveZ = @bitCast(z);
    state.destination168.destination = (x >> 7) | ((z >> 7) << 16);
    const angle = c.ratan2(@as(c_int, @bitCast(x)) -% state.position.xyz[0], @as(c_int, @bitCast(z)) -% state.position.xyz[2]);
    c.vs_battle_aiUpdateNavigationAreaMovement(state, angle);
}

/// orig: vs_battle_aiUpdateActorNavigationOverrides (pc/src/actor_update.c)
/// Updates every linked actor that is not flagged 0xC0: refreshes the
/// scratchpad frame, then for each actor with an AI state (found through the
/// scratchpad's state table) counts down its timers, copies the actor
/// model's tile and position into the state, and either aims it at its
/// target (idle and far), retargets a moving one that left its tile, or
/// updates it and releases its route.
pub fn updateActorNavigation() callconv(.c) void {
    c.vs_battle_aiCacheActorAiWorkingState();
    const scratch: [*]c_uint = @ptrFromInt(pad.addr(0x0));
    var actor: ?*ai.vs_battle_actor = @ptrCast(@alignCast(v.g.vs_battle_actors()[0]));
    var visited: c_uint = 0;
    while (actor) |a| : (actor = @ptrCast(a.next)) {
        visited +%= 1;
        if (visited > 17) c.pc_unimplemented("actor navigation list cycle");
        if (a.unk8 & 0xc0 != 0) continue;
        const id: c_uint = @bitCast(a.id);
        if (id >= 17) c.pc_unimplemented("actor navigation index");
        // An actor without an AI state (no attribute bit 0/2) has a null entry;
        // the original then loads its route record from address 0x18C, the PS1
        // kernel area (read here through guest RAM).
        const entry = scratch[0x37c / 4 + id];
        const base: *align(4) ai.vs_battle_aiState = @ptrFromInt(@as(usize, if (entry != 0) entry else 0x8000_0000));
        const route = base.route;
        scratch[0x3fc / 4] = @bitCast(c.vs_core_elapsedFrameCounterTicks());
        const state = route orelse continue;
        state.waitTimer = 0;
        const countdown = &state.timer156;
        if (countdown.* != 0) countdown.* -%= 1;
        const flags = state.movementFlags.raw;
        var steps = (flags >> 5) & 127;
        if (steps != 0) steps -%= 1;
        state.flags4.b[2] = 1;
        state.flags0.b[1] = 0;
        state.movementTargetId = 16;
        state.flags0.b[2] = 0;
        state.flags0.b[3] = 0;
        state.movementFlags.raw = (flags & ~@as(c_uint, 0xfe0)) | (steps << 5);
        const model = v.g.vs_actors_actorRecords()[id];
        if (model == null) c.pc_unimplemented("actor navigation model missing");
        const tile = psx.byteAt(c_uint, model.?, 0x5c).*;
        var x = psx.byteAt(c_uint, model.?, 0x1c).*;
        var z = psx.byteAt(c_uint, model.?, 0x20).*;
        state.tile.raw = tile;
        state.position.step.raw = x;
        state.position.word[1] = @bitCast(z);
        const cell = ((tile >> 16) & 255) *% 32 +% (tile & 255);
        if (cell >= 1024) c.pc_unimplemented("actor navigation tile outside grid");
        const ground: c_uint = @as(*align(1) u16, @ptrFromInt(@as(usize, v.g.D_800F58B8().*) +% @as(usize, cell) *% 2)).* & 15;
        x &= 65535;
        z &= 65535;
        state.unk120 = (x >> 6) | ((z >> 6) << 16);
        state.area = @truncate(ground);
        state.terrain132 = @bitCast(@as(u8, @truncate(ground)));
        scratch[0x3d0 / 4] = 0;
        const target = state.unk160;
        if (state.unk125 == 0) {
            if (c.vs_core_mathSquaredVectorLength(@bitCast((target & 65535) -% x), 0, @bitCast((target >> 16) -% z)) >= 1024) {
                routeTarget(state);
                continue;
            }
        } else if ((state.destination168.destination & 0xff00ff) != (tile & 0xff00ff)) {
            state.destination168.destination = ((target & 65535) >> 7) | ((target >> 23) << 16);
            c.vs_battle_aiDispatchAiLocomotionMode(state, 0, 0);
            continue;
        }
        c.vs_battle_aiIssueActorCommand(state, 0);
        c.vs_battle_aiFreeActorNavigationOverride(state.id);
    }
    c.vs_battle_aiSaveMovementRandomState();
}
