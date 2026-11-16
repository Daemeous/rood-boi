//! Battle overlay setup: resets the enemy-class counters and sets D_800F1968 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");
const v = @import("views_146C");

/// orig: func_8006C350 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls func_8009E5C4 for classes 1..15, then vs_battle_setActorMarkersHidden(1), and sets D_800F1968 to 1.
pub fn func_8006C350() callconv(.c) void {
    var i: c_int = 1;
    while (i < 16) : (i +%= 1) {
        c.func_8009E5C4(i);
    }

    c.vs_battle_setActorMarkersHidden(1);

    v.g.D_800F1968().* = 1;
}
