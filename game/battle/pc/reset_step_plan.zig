//! Step plan reset (D2: 7B3C8.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");

/// orig: vs_battle_aiRefreshDetourProgress (pc/src/battle_movement.c)
/// Forgets (0x194/0x198/0x19A) a tile the actor keeps revisiting (5 frames
/// running blocks it for 120), clears 0x156, and stores the step count (0x7E,
/// or 0x1F at random for non-flagged actors) in flags bits 5-11.
pub fn resetStepPlan(state: *ai.vs_battle_aiState, flags: c_uint) callconv(.c) c_uint {
    const tile = state.tile.raw;
    const last = &state.field194;
    var repeats: c_int = if ((last.* & 0xff00ff) == (tile & 0xff00ff)) @as(c_int, state.unk198) +% 1 else 0;
    if (repeats >= 5) {
        state.waitTimer = 120;
        repeats = 0;
    }
    last.* = @bitCast(tile);
    state.unk198 = @truncate(@as(c_uint, @bitCast(repeats)));
    state.timer156 = 0;
    var steps: c_uint = 0x7e;
    if (state.f23 == 0 and c.vs_battle_aiNextActorRandomMasked(4) == 0) steps = 0x7e >> 2;
    return (flags & 0xfffff01f) | (steps << 5);
}
