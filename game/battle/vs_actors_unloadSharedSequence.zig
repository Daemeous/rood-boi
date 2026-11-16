//! Battle helper wrapper: forwards its argument to vs_content_releaseActorSequence with flag 1
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_actors_unloadSharedSequence (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_content_releaseActorSequence with the given value and the flag 1, discarding its result.
pub fn vs_actors_unloadSharedSequence(arg0: c_int) callconv(.c) void {
    _ = c.vs_content_releaseActorSequence(arg0, 1);
}
