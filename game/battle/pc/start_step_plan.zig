//! Start of a step plan (D2: 7B5DC.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E3DDC (pc/src/battle_movement.c)
/// Starts a step plan. When the plan was empty, forced (bit 16) or the actor
/// is fleeing (byte 4), picks a random turn side (bit 4) and maybe shouts
/// (func_800E4180 kind 1); clears bit 17 when no steps remain; stores the
/// passability of the four axis directions in bits 18-21.
pub fn startStepPlan(state: [*]u8, flags_in: c_uint, @"test": c.direction_test) callconv(.c) c_uint {
    var flags = flags_in;
    state[0x199] = state[1];
    if ((flags & 0xfe0) == 0 or ((flags >> 16) & 1) != 0 or state[4] != 0) {
        flags &= ~@as(c_uint, 0x10000);
        flags = (flags & ~@as(c_uint, 0x10)) | (@as(c_uint, @bitCast(c.func_800E45D4(2))) << 4);
        _ = c.func_800E4180(state, 8, 1, state[0x89]);
    }
    if ((flags & 0xfffff01f) == 0) flags &= ~@as(c_uint, 0x20000);
    return (flags & 0xffc30fff) | (@as(c_uint, @bitCast(c.func_800E3D5C(state, @"test"))) << 18);
}
