//! Approaching an actor (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;
const pad = @import("common").pad;

/// orig: vs_battle_aiApproachActor (pc/src/battle_movement.c)
/// Approaches actor `id`. Uses the model's waypoint group when allowed,
/// otherwise its tile; then plans with vs_battle_aiUpdateTargetApproachMovement and on success enters
/// mode 1. A missing cached actor still enters mode 1 for nonzero ids, as
/// the original's leftover $v0 (id * 4) does.
pub fn approachActor(state: *ai.vs_battle_aiState, id: c_int, distance: c_int, option: c_int) callconv(.c) c_int {
    const model_slot = psx.at(v.g.vs_actors_actorRecords(), id).*;
    const cached = pad.aiState(id);
    var result: c_int = undefined;
    if (cached == null) {
        result = id *% 4;
    } else {
        const model: [*]u8 = @ptrCast(model_slot.?);
        const busy: c_int = state.flags & 0x38;
        var group: c_int = 0;
        var grouped = false;
        if (state.actorId != id) {
            @as(*align(1) u32, @ptrCast(&state.bE4)).* = 0;
            @as(*align(1) u32, @ptrCast(&state.bE8)).* = 0;
        }
        state.actorId = psx.conv(u8, id);
        state.range = distance;
        if (busy == 0 and model[0xa] & 0x1f != 0) {
            const tile = psx.byteAt(u32, model, 0x60).*;
            state.destination168.destination = tile;
            if (tile & 0xff00 != 0) {
                group = @as(c_int, @intCast((tile & 0xff00) >> 8)) -% 2;
                grouped = true;
            }
        } else if ((@as(c_int, state.f29) | busy) != 0 and model[0xc] & 0xf != 0) {
            group = @as(c_int, model[0xc] & 0xf) -% 2;
            grouped = true;
        } else {
            state.destination168.destination = psx.byteAt(u32, model, 0x5c).*;
        }
        if (grouped) {
            const waypoint = c.vs_field_movingPlatformTargetWaypoint(group);
            c.vs_battle_aiFindNearestRouteGateEntry(group, waypoint[0], waypoint[2], &state.destination168.targetMovementPoint.b[0]);
            const tile = state.destination168.destination;
            state.cachedDestination.raw = tile;
            @as(*align(1) u32, @ptrCast(&state.targetTileX)).* = ((tile & 0xff) << 1) | (((tile >> 16) & 0xff) << 1);
        }
        result = c.vs_battle_aiUpdateTargetApproachMovement(state, option);
    }
    return if (result != 0) c.vs_battle_aiSetMovementBehavior(state, 1) else 0;
}
