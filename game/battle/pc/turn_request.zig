//! Actor turn and walk requests (D2: 4A0A8.c's inline assembly; oracle pc/src/battle_motion.c).
const c = @import("c_battle_motion");
const v = @import("views_battle_motion");

/// orig: func_800B6580 (pc/src/battle_motion.c)
/// Starts a turn toward position[0] at rate `b` unless one is pending (the low
/// nibble of byte 0xB): clears the halfword at 0x1A, stores the target word at
/// 0x5EC and `b << 16` at 0x5F0, and sets byte 0xB to 1. The linked actor
/// (byte 0x17FD) gets the same request when its id is 2 or more.
pub fn requestTurn(id: c_int, position: [*]const c_int, b: c_int) callconv(.c) void {
    const model: [*]u8 = @ptrCast(v.g.D_800F4538()[@intCast(id)].?);
    if (model[0xb] & 0xf != 0) return;
    const linked: c_int = model[0x17fd];
    @as(*u16, @ptrCast(@alignCast(model + 0x1a))).* = 0;
    @as(*c_int, @ptrCast(@alignCast(model + 0x5ec))).* = position[0];
    @as(*c_uint, @ptrCast(@alignCast(model + 0x5f0))).* = @as(c_uint, @bitCast(b)) << 16;
    model[0xb] = 1;
    if (linked >= 2) c.func_800B6580(linked, @constCast(position), b);
}

/// orig: func_800B668C (pc/src/battle_motion.c)
/// func_800AA8D0, then func_800A249C, both with the same id and `a`.
pub fn advanceMotion(id: c_int, a: i16, b: c_int) callconv(.c) void {
    c.func_800AA8D0(id, a, b);
    c.func_800A249C(id, a);
}
