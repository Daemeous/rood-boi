//! Battle setup step: resets the 16 battle slots, then sets the state word (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_beginAshleyWeaponToggle (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resets each of the 16 battle slots, clears the second reset call, then sets D_800F196C to 10.
pub fn vs_battle_beginAshleyWeaponToggle() callconv(.c) void {
    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        c.func_8009E5C4(i);
    }
    c.vs_battle_setActorBattleMode(0, 2);
    v.g.D_800F196C().* = 10;
}
