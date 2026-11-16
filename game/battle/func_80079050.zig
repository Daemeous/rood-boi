//! Clears the battle state when the current action slot is free (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80079050 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the two cleanup calls once the action slot at the current index reports free.
pub fn func_80079050() callconv(.c) void {
    const d = v.g.vs_battle_actionState().*.?;
    const index: usize = d.unk0 & 3;
    if (c.vs_battle_aiRebuildActorActionPlans(&d.unk854[index]) == 0) {
        _ = c.func_80069D14();
        _ = c.vs_battle_beginActionTrailFade();
    }
}
