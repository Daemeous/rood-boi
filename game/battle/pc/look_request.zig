//! Actor look-at requests (D2: 4A0A8.c's inline assembly; oracle pc/src/battle_motion.c).
const v = @import("views_battle_motion");

/// orig: look_request (pc/src/battle_motion.c)
/// Writes a look request into the actor object: the target's two words at
/// 0x5EC/0x5F0, then the halfword `b` at 0x5F2 (over the second word's high
/// half), and `kind` into bits 4-6 of byte 0xB.
fn lookRequest(id: c_int, position: [*]const c_int, b: c_int, kind: c_int) void {
    const model: [*]u8 = @ptrCast(v.g.D_800F4538()[@intCast(id)].?);
    @as(*c_int, @ptrCast(@alignCast(model + 0x5ec))).* = position[0];
    @as(*c_int, @ptrCast(@alignCast(model + 0x5f0))).* = position[1];
    @as(*u16, @ptrCast(@alignCast(model + 0x5f2))).* = @truncate(@as(c_uint, @bitCast(b)));
    model[0xb] = (model[0xb] & 0x8f) | @as(u8, @truncate(@as(c_uint, @bitCast(kind))));
}

/// orig: func_800B65E8 (pc/src/battle_motion.c)
/// Look request of kind 0x10 at the position.
pub fn lookAtPosition(id: c_int, position: [*]const c_int, b: c_int) callconv(.c) void {
    lookRequest(id, position, b, 0x10);
}

/// orig: func_800B65F4 (pc/src/battle_motion.c)
/// Look request of kind 0x40 at the position.
pub fn lookAtPositionFast(id: c_int, position: [*]const c_int, b: c_int) callconv(.c) void {
    lookRequest(id, position, b, 0x40);
}
