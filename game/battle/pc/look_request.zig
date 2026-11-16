//! Actor look-at requests (D2: 4A0A8.c's inline assembly; oracle pc/src/battle_motion.c).
const psx = @import("common").psx;
const v = @import("views_battle_motion");

/// orig: look_request (pc/src/battle_motion.c)
/// Writes a look request into the actor object: the target's two words at
/// 0x5EC/0x5F0, then the halfword `b` at 0x5F2 (over the second word's high
/// half), and `kind` into bits 4-6 of byte 0xB.
fn lookRequest(id: c_int, position: [*]const c_int, b: c_int, kind: c_int) void {
    const a = v.g.vs_actors_actorRecords()[@intCast(id)].?;
    // unk5EC is the SVECTOR the words land in: vx and vy, then vz and the pad.
    psx.fieldAt(c_int, &a.unk5EC, 0).* = position[0];
    psx.fieldAt(c_int, &a.unk5EC, 4).* = position[1];
    psx.fieldAt(u16, &a.unk5EC, 6).* = @truncate(@as(c_uint, @bitCast(b)));
    const flags = psx.fieldAt(u8, &a.unk0._bits_skip, 3);
    flags.* = (flags.* & 0x8f) | @as(u8, @truncate(@as(c_uint, @bitCast(kind))));
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
