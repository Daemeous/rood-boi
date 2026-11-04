//! Command byte request with cooldown (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E48A8 (pc/src/battle_movement.c)
/// With no route (0x18C) and no cooldown (0x155), and a random draw below
/// `range` (always true for powers of two), sets command byte 0xB to `value`
/// and starts a 30-frame cooldown. The original computes a masked byte but
/// stores the raw value.
pub fn requestCommandByte(state: [*]u8, value: c_int, range: c_int) callconv(.c) void {
    const roll = c.func_800E45D4(range);
    if ((@as(*align(1) const u32, @ptrCast(state + 0x18c)).* | state[0x155]) != 0) return;
    if (!(roll < range)) return;
    state[0x155] = 30;
    const command: [*]u8 = @ptrFromInt(@as(*align(1) const u32, @ptrCast(state + 0x54)).*);
    command[0xb] = @truncate(@as(c_uint, @bitCast(value)));
}
