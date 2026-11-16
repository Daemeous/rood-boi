//! Accessor for the battle ability block vs_menu_frameworkActionSelection (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_menu_frameworkActionSelectionState (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns the address of the ability block vs_menu_frameworkActionSelection.
pub fn vs_menu_frameworkActionSelectionState() callconv(.c) ?*v.D_800F18EC_t {
    return v.g.vs_menu_frameworkActionSelection();
}
