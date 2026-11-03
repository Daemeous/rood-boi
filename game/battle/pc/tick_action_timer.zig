//! An actor update state's action timer (D2: 6FA6C.s; oracle pc/src/battle_helpers.c).
const c = @import("c_battle_helpers");

/// orig: func_800D8280 (pc/src/battle_helpers.c)
/// Counts down the byte timer at 0x12A (a zero timer is left alone). When it
/// reaches zero the state's actor (byte 0x88) is sent 0, otherwise the
/// halfword at 0x138.
pub fn tickActionTimer(state: [*]u8) callconv(.c) void {
    if (state[0x12a] == 0) return;
    state[0x12a] -= 1;
    const actor: c_int = state[0x88];
    if (state[0x12a] == 0) {
        _ = c.func_800D820C(actor, 0);
    } else {
        _ = c.func_800D820C(actor, @as(*align(1) const u16, @ptrCast(state + 0x138)).*);
    }
}
