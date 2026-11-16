//! The action's phase 6: waiting for the cast flash (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");
const v = @import("views_146C");

/// orig: vs_battle_startReadyActionAnimation (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Once the cast flash (D_800F1868 bit 1) is over, releases the action (vs_battle_startActionAnimation) and calls vs_effects_startEffectTrack(0).
pub fn vs_battle_startReadyActionAnimation() callconv(.c) void {
    if (v.g.D_800F1868().* & 2 == 0) {
        c.vs_battle_startActionAnimation();
        _ = c.vs_effects_startEffectTrack(0);
    }
}
