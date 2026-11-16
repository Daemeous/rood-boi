//! Battle flag dispatch: forwards the byte at arg0[1] to one of two C routines (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptReleaseObjectModel (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Forwards the flag byte at arg0[1] to one of two C routines, then returns 4 if vs_event_skipState is 2.
pub fn vs_event_scriptReleaseObjectModel(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const flags: u8 = arg0[1];
    if ((flags & 4) != 0) {
        c.vs_actors_releaseSequenceEffectTextures(@as(c_int, flags & 3));
    } else {
        c.vs_actors_unloadSharedSequence(@as(c_int, flags));
    }
    return @as(c_int, @intFromBool(v.g.vs_event_skipState().* == 2)) *% 4;
}
