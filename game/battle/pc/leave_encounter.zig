//! Leaving the room's encounter (D2: 6FA6C.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800D82CC (pc/src/battle_movement.c)
/// Leaves the encounter: clears the actor's bit in the room's mask at 0x10
/// and its timer at 0x8E, blocks re-joining (0x98 = 0x30 unless exactly two
/// actors remain; 0x9A = 90), unlinks the actor (func_800DBF00), drops a
/// member and notifies actor `id` (func_800D820C). Does nothing when the
/// actor's bit is not set.
pub fn leaveEncounter(state: [*]u8) callconv(.c) void {
    const room: [*]u8 = @ptrFromInt(v.g.D_800F58BC().*);
    const id: c_int = state[0x88];
    const mask: c_uint = psx.byteAt(u16, room, 0x10).*;
    // id >= 32 is undefined in C; the x86 shift takes the count modulo 32.
    const bit: c_uint = @as(c_uint, 1) << @truncate(@as(c_uint, @bitCast(id)));
    if (mask & bit == 0) return;
    psx.byteAt(u16, room, 0x10).* = psx.conv(u16, mask -% bit);
    psx.byteAt(u16, state, 0x8e).* = 0;
    const first = v.g.vs_battle_actors()[0].?;
    const second: ?[*]u8 = @ptrFromInt(psx.byteAt(u32, first, 0).*);
    psx.byteAt(u16, state, 0x98).* = if (second != null and psx.byteAt(u32, second.?, 0).* == 0) 0 else 0x30;
    psx.byteAt(u16, state, 0x9a).* = 90;
    c.func_800DBF00(state);
    room[0x12] -%= 1;
    c.func_800D820C(id, 0);
}
