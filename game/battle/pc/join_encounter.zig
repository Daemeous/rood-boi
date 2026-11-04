//! Joining the room's encounter (D2: 6FA6C.s; oracle pc/src/battle_movement.c).
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800D836C (pc/src/battle_movement.c)
/// Joins the room's encounter. Already engaged (0x8E) succeeds; a full
/// encounter (count 0x0D below members 0x12) or a block (0x98 or D_800F58E0)
/// fails. Otherwise sets the engagement timer (1, or 0x20 after a recent
/// leave at 0x9A), adds the actor's bit to the mask at 0x10, bumps the
/// members and pushes the actor on the room's engaged list (head 0x1C, tail
/// 0x20, link 0x1A0).
pub fn joinEncounter(state: [*]u8) callconv(.c) c_int {
    const room: [*]u8 = @ptrFromInt(v.g.D_800F58BC().*);
    if (psx.byteAt(u16, state, 0x8e).* != 0) return 1;
    const members: c_uint = room[0x12];
    if (room[0xd] < members) return 0;
    if ((psx.byteAt(u16, state, 0x98).* | v.g.D_800F58E0().*) != 0) return 0;
    state[0x12c] = 0;
    // The actor id is a byte: the x86 shift takes the count modulo 32.
    const bit: c_uint = @as(c_uint, 1) << @truncate(@as(c_uint, state[0x88]));
    psx.byteAt(u16, room, 0x10).* = psx.conv(u16, @as(c_uint, psx.byteAt(u16, room, 0x10).*) | bit);
    room[0x12] = psx.conv(u8, members +% 1);
    psx.byteAt(u16, state, 0x8e).* = if (psx.byteAt(u16, state, 0x9a).* != 0) 0x20 else 1;
    const head = psx.byteAt(u32, room, 0x1c).*;
    if (head == 0) psx.byteAt(u32, room, 0x20).* = psx.addr(state);
    psx.byteAt(u32, state, 0x1a0).* = head;
    psx.byteAt(u32, room, 0x1c).* = psx.addr(state);
    return 1;
}
