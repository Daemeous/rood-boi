//! Battle unit 4A0A8: a two-state wait step (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");
const v = @import("views_4A0A8");

/// orig: vs_event_commandWaitEffectRelease (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs vs_effects_clearPrimaryEffects and sets vs_event_isEffectWaitPending on the first call; afterwards clears it and returns 0 when func_8007BEE4 reads 0 or 5, else returns 1.
pub fn vs_event_commandWaitEffectRelease(_: [*c]u8, _: c_short) callconv(.c) c_int {
    if (v.g.vs_event_isEffectWaitPending().* == 0) {
        c.vs_effects_clearPrimaryEffects();
        v.g.vs_event_isEffectWaitPending().* = 1;
    } else if (c.func_8007BEE4() == 0 or c.func_8007BEE4() == 5) {
        v.g.vs_event_isEffectWaitPending().* = 0;
        return 0;
    }
    return 1;
}
