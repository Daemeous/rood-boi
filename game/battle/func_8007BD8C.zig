//! Battle unit 146C: selects one of two engine toggles by its argument (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8007BD8C (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls the two toggles with 1 and 0 for a nonzero argument, 0 and 1 otherwise, and returns the argument.
pub fn func_8007BD8C(arg0: c_int) callconv(.c) c_int {
    if (arg0 != 0) {
        c.vs_battle_setHudHidden(1);
        _ = c.vs_field_setMinimapVisible(0);
    } else {
        c.vs_battle_setHudHidden(0);
        _ = c.vs_field_setMinimapVisible(1);
    }
    return arg0;
}
