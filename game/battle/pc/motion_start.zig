//! Actor move requests (D2: 4A0A8.c's inline assembly; oracle pc/src/battle_motion.c).
const c = @import("c_battle_motion");
const v = @import("views_battle_motion");

/// orig: motion_reset (pc/src/battle_motion.c)
/// Clears the motion flags (byte 0xA bits 5-6), sets bit 6, then hands the
/// target to func_800A208C.
fn motionReset(model: [*]u8, target: ?[*]c_int, speed: c_int, facing: c_int) void {
    model[0xa] = (model[0xa] & 0x9f) | 0x40;
    c.func_800A208C(model, target, speed, facing);
}

/// orig: motion_animated (pc/src/battle_motion.c)
/// Sets motion bits 5-6, plays animation 0x1F (blend 8) and hands the target
/// to func_800A208C.
fn motionAnimated(id: c_int, model: [*]u8, target: ?[*]c_int, speed: c_int, facing: c_int) void {
    model[0xa] |= 0x60;
    _ = c.func_800A0204(id, 0x1f, 0, 8);
    c.func_800A208C(model, target, speed, facing);
}

/// orig: func_800B63AC (pc/src/battle_motion.c)
/// Starts a move: an actor already moving (bit 6 of byte 0xA) restarts it, one
/// with bit 5 animates first; otherwise the low two bits of byte 0x5B0 become 1.
pub fn startMove(id: c_int, target: ?[*]c_int, speed: c_int, facing: c_int) callconv(.c) void {
    const model: [*]u8 = @ptrCast(v.g.D_800F4538()[@intCast(id)].?);
    if (model[0xa] & 0x40 != 0) {
        motionReset(model, target, speed, facing);
        return;
    }
    if (model[0xa] & 0x20 != 0) {
        motionAnimated(id, model, target, speed, facing);
        return;
    }
    model[0x5b0] = (model[0x5b0] & 0xfc) | 1;
    c.func_800A208C(model, target, speed, facing);
}

/// orig: func_800B6404 (pc/src/battle_motion.c)
/// Restarts the actor's move at the target (motion_reset).
pub fn restartMove(id: c_int, target: ?[*]c_int, speed: c_int, facing: c_int) callconv(.c) void {
    motionReset(@ptrCast(v.g.D_800F4538()[@intCast(id)].?), target, speed, facing);
}
