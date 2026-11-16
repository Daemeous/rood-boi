//! Reports whether any of the three entries in the vs_event_splashTweens table has its unk3 flag set (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800B71F4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 if unk3 is nonzero in any of vs_event_splashTweens[0..2], else 0.
pub fn func_800B71F4(_: [*c]u8, _: c_short) callconv(.c) c_int {
    const table = v.g.vs_event_splashTweens();
    const bits: c_int = @as(c_int, table[2].unk3) | (@as(c_int, table[0].unk3) | @as(c_int, table[1].unk3));
    return @intFromBool(bits != 0);
}
