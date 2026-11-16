//! Forwards to vs_actors_sharedSequenceBankId (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8007CC8C (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_actors_sharedSequenceBankId and discards its result.
pub fn func_8007CC8C() callconv(.c) void {
    _ = c.vs_actors_sharedSequenceBankId();
}
