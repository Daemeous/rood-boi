//! Battle setup: resets the enemy-class entries and the battle flag (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8006C2FC (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the per-class setup for classes 1..15, then the two battle-state calls, and sets D_800F1968.
pub fn func_8006C2FC() callconv(.c) void {
    var i: c_int = 1;
    while (i < 16) : (i +%= 1) {
        c.func_8009E5C4(i);
    }

    _ = c.vs_field_setMinimapVisible(0);
    c.vs_battle_setActorMarkersHidden(1);

    v.g.D_800F1968().* = 1;
}
