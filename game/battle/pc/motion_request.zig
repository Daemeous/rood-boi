//! Actor motion request blocks (D2: 4A0A8.c's inline assembly; oracle pc/src/battle_motion.c).
//! The block sits at 0x1848 of the actor object: the kind word, then 0x184C/0x184E
//! (the packed pair), 0x1850 and the position pair at 0x1858/0x185C.
const c = @import("c_battle_motion");
const v = @import("views_battle_motion");

fn word(model: [*]u8, offset: usize) *c_uint {
    return @ptrCast(@alignCast(model + offset));
}

fn modelOf(id: c_int) [*]u8 {
    return @ptrCast(v.g.D_800F4538()[@intCast(id)].?);
}

/// orig: func_800B64A8 (pc/src/battle_motion.c)
/// func_800B64CC with c = a, then byte 0x1848 cleared (the kind becomes 0).
pub fn setMotionRequestParams(id: c_int, a: c_int, b: c_int) callconv(.c) void {
    c.func_800B64CC(id, a, b, a);
    modelOf(id)[0x1848] = 0;
}

/// orig: func_800B6500 (pc/src/battle_motion.c)
/// Kind 2: the position's two words at 0x1858/0x185C.
pub fn setMotionRequestPosition(id: c_int, position: [*]const c_uint) callconv(.c) void {
    const model = modelOf(id);
    word(model, 0x1858).* = position[0];
    word(model, 0x185c).* = position[1];
    word(model, 0x1848).* = 2;
}

/// orig: func_800B653C (pc/src/battle_motion.c)
/// Kind 3: `b` at 0x1850, the low half of `c` at 0x184E, the position, and
/// bit 5 of byte 0xA set.
pub fn setMotionRequestAnimated(id: c_int, position: [*]const c_uint, b: c_int, cc: c_int) callconv(.c) void {
    const model = modelOf(id);
    @as(*c_int, @ptrCast(@alignCast(model + 0x1850))).* = b;
    @as(*u16, @ptrCast(@alignCast(model + 0x184e))).* = @truncate(@as(c_uint, @bitCast(cc)));
    word(model, 0x1858).* = position[0];
    word(model, 0x185c).* = position[1];
    model[0xa] |= 0x20;
    word(model, 0x1848).* = 3;
}

/// orig: func_800B6530 (pc/src/battle_motion.c)
/// func_800B653C with c = b, then the kind word becomes 4.
pub fn setMotionRequestAnimatedTurn(id: c_int, position: [*]const c_uint, b: c_int) callconv(.c) void {
    c.func_800B653C(id, @constCast(position), b, b);
    word(modelOf(id), 0x1848).* = 4;
}
