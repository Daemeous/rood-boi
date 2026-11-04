//! Behind-the-facing test (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");

/// orig: func_800E47E4 (pc/src/battle_movement.c)
/// Whether `tile` lies behind the actor relative to its facing target (actor
/// 0x15C, or its own model heading when that is >= 16).
pub fn isBehindFacing(state: [*]u8, tile: c_uint) callconv(.c) c_int {
    if (state[0xc] != 0) return 0;
    const current = @as(*align(1) const u32, @ptrCast(state + 0x34)).*;
    const dx: c_int = @as(c_int, @intCast(tile & 0xff)) - @as(c_int, @intCast(current & 0xff)); // bytes
    const dz: c_int = @as(c_int, @intCast((tile >> 16) & 0xff)) - @as(c_int, @intCast((current >> 16) & 0xff));
    const facing = state[0x15c];
    var angle: c_int = undefined;
    if (facing >= 16) {
        const model: [*]u8 = @ptrCast(v.g.D_800F4538()[state[0x88]].?);
        angle = @as(*align(1) const u16, @ptrCast(model + 0x26)).*;
    } else {
        const model: [*]u8 = @ptrCast(v.g.D_800F4538()[facing].?);
        const own_x: c_int = @as(*align(1) const u16, @ptrCast(state + 0x4c)).*;
        const own_z: c_int = @as(*align(1) const u16, @ptrCast(state + 0x50)).*;
        const model_x: c_int = @as(*align(1) const u16, @ptrCast(model + 0x1c)).*;
        const model_z: c_int = @as(*align(1) const u16, @ptrCast(model + 0x20)).*;
        angle = c.ratan2(own_x - model_x, own_z - model_z);
    }
    const turned = 0xc00 -% angle;
    const along = c.rcos(turned) *% dx;
    return @intFromBool(c.rsin(turned) *% dz +% along < 0);
}
