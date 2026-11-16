//! Calls vs_render_releaseGimImageSet, then returns 4 when vs_event_skipState is 2, else 0 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B71C4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_render_releaseGimImageSet, then returns 4 when vs_event_skipState is 2, else 0.
pub fn func_800B71C4(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_render_releaseGimImageSet();
    return @as(c_int, @intFromBool(@as(c_int, v.g.vs_event_skipState().*) == 2)) * 4;
}
