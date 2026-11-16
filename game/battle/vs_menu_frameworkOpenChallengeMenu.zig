//! Battle helper: runs vs_menu_frameworkOpenGameMenu(0xF, 2) and sets D_800F4FDB to 1 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_menu_frameworkOpenChallengeMenu (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls the menu state handler with 0xF and flags D_800F4FDB.
pub fn vs_menu_frameworkOpenChallengeMenu() callconv(.c) void {
    _ = c.vs_menu_frameworkOpenGameMenu(0xF, 2);
    v.g.D_800F4FDB().* = 1;
}
