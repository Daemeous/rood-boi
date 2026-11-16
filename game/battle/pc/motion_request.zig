//! Actor motion request blocks (D2: 4A0A8.c's inline assembly; oracle pc/src/battle_motion.c).
//! The block is the actor object's unk1848: the kind word, then the packed pair
//! (unk4, unk6), unk8 and the position pair (unk10).
const psx = @import("common").psx;
const c = @import("c_battle_motion");
const v = @import("views_battle_motion");

fn objectOf(id: c_int) *v.D_800F4538_t {
    return v.g.vs_actors_actorRecords()[@intCast(id)].?;
}

/// The u32 at byte 0 or 4 of the request's position pair (unk10: vx and vy, vz and pad).
fn position(a: *v.D_800F4538_t, half: isize) *align(1) c_uint {
    return psx.fieldAt(c_uint, &a.unk1848.step, half * 4);
}

/// The kind word of the request: unk0 to unk3 as one u32.
fn kind(a: *v.D_800F4538_t) *align(1) c_uint {
    return psx.fieldAt(c_uint, &a.unk1848.unk0, 0);
}

/// orig: vs_actors_setDirectMovement (pc/src/battle_motion.c)
/// func_800B64CC with c = a, then byte 0x1848 cleared (the kind becomes 0).
pub fn setMotionRequestParams(id: c_int, a: c_int, b: c_int) callconv(.c) void {
    c.func_800B64CC(id, a, b, a);
    objectOf(id).unk1848.unk0 = 0;
}

/// orig: vs_actors_queueGroundOffset (pc/src/battle_motion.c)
/// Kind 2: the position's two words.
pub fn setMotionRequestPosition(id: c_int, pos: [*]const c_uint) callconv(.c) void {
    const a = objectOf(id);
    position(a, 0).* = pos[0];
    position(a, 1).* = pos[1];
    kind(a).* = 2;
}

/// orig: vs_actors_queueFlightMovement (pc/src/battle_motion.c)
/// Kind 3: `b` in unk8, the low half of `c` in unk6, the position, and
/// bit 5 of byte 0xA set.
pub fn setMotionRequestAnimated(id: c_int, pos: [*]const c_uint, b: c_int, cc: c_int) callconv(.c) void {
    const a = objectOf(id);
    a.unk1848.unk8 = @bitCast(b);
    a.unk1848.unk6 = @bitCast(@as(u16, @truncate(@as(c_uint, @bitCast(cc)))));
    position(a, 0).* = pos[0];
    position(a, 1).* = pos[1];
    psx.fieldAt(u8, &a.unk0._bits_skip, 2).* |= 0x20;
    kind(a).* = 3;
}

/// orig: vs_actors_queueMovementVector (pc/src/battle_motion.c)
/// vs_actors_queueFlightMovement with c = b, then the kind word becomes 4.
pub fn setMotionRequestAnimatedTurn(id: c_int, pos: [*]const c_uint, b: c_int) callconv(.c) void {
    c.vs_actors_queueFlightMovement(id, @constCast(pos), b, b);
    kind(objectOf(id)).* = 4;
}
