//! Whether a candidate's target counts as engaged (D2: 72B70.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DB8AC (pc/src/battle_movement.c)
/// Whether a candidate's target counts as engaged in the room's current
/// encounter (D_800F58BC): forced by 0x8E, blocked by 0x98, otherwise the
/// target relation bit 0 plus the room's counters and per-actor mask. The
/// original calls func_800DBCB4 and discards its result.
pub fn canEngageTarget(state: [*]u8, candidate: [*]u8) callconv(.c) c_int {
    const room: [*]u8 = @ptrFromInt(v.g.D_800F58BC().*);
    _ = c.func_800DBCB4(state, candidate);
    if (psx.byteAt(u16, state, 0x8e).* != 0) return 1;
    if (psx.byteAt(u16, state, 0x98).* != 0) return 0;
    const target: usize = psx.byteAt(u32, candidate, 12).* & 0xf;
    if (psx.byteAt(u32, state, @intCast(0x1a4 + target * 4)).* & 1 == 0) return 0; // target < 16

    const count: c_uint = room[0xd];
    if (count < psx.byteAt(u32, room, 0x24).*) return 0;
    // The actor id is a byte: the x86 shift takes the count modulo 32.
    const bit: c_uint = @as(c_uint, 1) << @truncate(@as(c_uint, state[0x88]));
    if (@as(c_uint, psx.byteAt(u16, room, 0x10).*) & bit != 0) return 1;
    return @intFromBool(!(count < room[0x12]));
}
