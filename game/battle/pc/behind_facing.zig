//! Behind-the-facing test (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");

/// orig: func_800E47E4 (pc/src/battle_movement.c)
/// Whether `tile` lies behind the actor relative to its facing target (actor
/// 0x15C, or its own model heading when that is >= 16).
pub fn isBehindFacing(state: *ai.vs_battle_aiState, tile: c_uint) callconv(.c) c_int {
    if (state.flagsC.b[0] != 0) return 0;
    const current = state.tile.raw;
    const dx: c_int = @as(c_int, @intCast(tile & 0xff)) - @as(c_int, @intCast(current & 0xff)); // bytes
    const dz: c_int = @as(c_int, @intCast((tile >> 16) & 0xff)) - @as(c_int, @intCast((current >> 16) & 0xff));
    const facing = state.movementTargetId;
    var angle: c_int = undefined;
    if (facing >= 16) {
        const model: [*]u8 = @ptrCast(v.g.vs_actors_actorRecords()[state.id].?);
        angle = @as(*align(1) const u16, @ptrCast(model + 0x26)).*;
    } else {
        const model: [*]u8 = @ptrCast(v.g.vs_actors_actorRecords()[facing].?);
        const own_x: c_int = state.position.xyz[0];
        const own_z: c_int = state.position.xyz[2];
        const model_x: c_int = @as(*align(1) const u16, @ptrCast(model + 0x1c)).*;
        const model_z: c_int = @as(*align(1) const u16, @ptrCast(model + 0x20)).*;
        angle = c.ratan2(own_x - model_x, own_z - model_z);
    }
    const turned = 0xc00 -% angle;
    const along = c.rcos(turned) *% dx;
    return @intFromBool(c.rsin(turned) *% dz +% along < 0);
}
