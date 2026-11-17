//! Approach point refresh (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const pad = @import("common").pad;

/// orig: vs_battle_aiRefreshAiTargetTile (pc/src/battle_movement.c)
/// Refreshes the approach point (0xF4/0xF6, half-tile units) of the target
/// actor 0xE5: its waypoint group when it has one (model byte 0x5D for plain
/// models, tile byte 1 of 0x60 for flagged ones), else its position.
pub fn refreshApproachPoint(state: *ai.vs_battle_aiState) callconv(.c) void {
    const id = state.actorId;
    const model_slot = v.g.vs_actors_actorRecords()[id];
    if (pad.aiState(id) == null) return;
    const model: [*]u8 = @ptrCast(model_slot.?);
    var group: c_int = -1;
    // Uninitialised in the original when group stays -1 (a flagged model whose
    // byte is 1: group = byte - 2 = -1): stack garbage there.
    var x: c_int = 0;
    var z: c_int = 0;
    if ((model[0xa] & 0x1f) == 0) {
        if (model[0x5d] < 2) {
            x = @as(c_int, @as(*align(1) const i16, @ptrCast(model + 0x1c)).*) >> 6;
            z = @as(c_int, @as(*align(1) const i16, @ptrCast(model + 0x20)).*) >> 6;
        } else {
            group = @as(c_int, model[0x5d]) - 2;
        }
    } else {
        const byte: c_int = @intCast((@as(*align(1) const u32, @ptrCast(model + 0x60)).* & 0xff00) >> 8);
        if (byte != 0) {
            group = byte - 2;
        } else {
            x = @as(c_int, @as(*align(1) const i16, @ptrCast(model + 0x4c)).*) >> 6;
            z = @as(c_int, @as(*align(1) const i16, @ptrCast(model + 0x50)).*) >> 6;
        }
    }
    if (group != -1) {
        const waypoint = c.vs_field_movingPlatformTargetWaypoint(group);
        c.vs_battle_aiFindNearestRouteGateEntry(group, waypoint[0], waypoint[2], @ptrCast(&state.targetTileX));
        x = @as(c_int, @as(i8, @bitCast(state.targetTileX))) << 1;
        z = @as(c_int, @as(i8, @bitCast(state.targetTileZ))) << 1;
    }
    state.targetTileX = @truncate(@as(c_uint, @bitCast(x)));
    state.targetTileZ = @truncate(@as(c_uint, @bitCast(z)));
}
