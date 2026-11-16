//! Command byte request with cooldown (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800E48A8 (pc/src/battle_movement.c)
/// With no route (0x18C) and no cooldown (0x155), and a random draw below
/// `range` (always true for powers of two), sets command byte 0xB to `value`
/// and starts a 30-frame cooldown. The original computes a masked byte but
/// stores the raw value.
pub fn requestCommandByte(state: *ai.vs_battle_aiState, value: c_int, range: c_int) callconv(.c) void {
    const roll = c.vs_battle_aiNextActorRandomMasked(range);
    if ((@intFromPtr(state.route) | state.unk155) != 0) return;
    if (!(roll < range)) return;
    state.unk155 = 30;
    psx.get(state.actor).?.unkB = @truncate(@as(c_uint, @bitCast(value)));
}
