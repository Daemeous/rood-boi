//! Battle overlay helper from pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c: sets a flag and records a bit of its argument.
const psx = @import("common").psx;
const v = @import("views_4A0A8");

/// orig: vs_event_scriptBeginSkippableCutscene (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets vs_event_skipState, stores the inverted bit 0 of arg0[1] in vs_event_stopsMusicOnSkip, and returns 4 when vs_event_isSkipForced is set.
pub fn vs_event_scriptBeginSkippableCutscene(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    v.g.vs_event_skipState().* = 1;
    v.g.vs_event_stopsMusicOnSkip().* = psx.conv(u8, @intFromBool((@as(c_int, arg0[1]) & 1) == 0));
    return @as(c_int, @intFromBool(v.g.vs_event_isSkipForced().* != 0)) *% 4;
}
