//! Battle overlay unit 4A0A8: returns 4 when vs_event_skipState is 2, else 0 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandYieldEffectRelease (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007BF04, then returns 4 if vs_event_skipState is 2 and 0 otherwise.
pub fn vs_event_commandYieldEffectRelease(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8007BF04();
    return @as(c_int, @intFromBool(v.g.vs_event_skipState().* == 2)) * 4;
}
