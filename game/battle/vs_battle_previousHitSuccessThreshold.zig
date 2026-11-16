//! Battle helper vs_battle_previousHitSuccessThreshold (pc/src/game/BATTLE/BATTLE.PRG/146C.c): picks a value from the battle flag D_800F1A04.
const v = @import("views_146C");

/// orig: vs_battle_previousHitSuccessThreshold (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns 100 when the battle flag D_800F1A04 is nonzero, else 255.
pub fn vs_battle_previousHitSuccessThreshold(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    return if (v.g.D_800F1A04().* != 0) 100 else 255;
}
