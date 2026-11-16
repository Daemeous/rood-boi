//! Clears a battle record's first halfword between two battle helper calls (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");
const v = @import("views_146C");

/// orig: vs_field_closeBattleActionMenu (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs vs_menu_frameworkReleaseMenuRows, clears the first halfword of arg0, then runs vs_field_resumeFieldAfterMenu.
pub fn vs_field_closeBattleActionMenu(arg0: ?*v.D_800F18EC_t) callconv(.c) void {
    c.vs_menu_frameworkReleaseMenuRows();
    arg0.?.unk0 = 0;
    c.vs_field_resumeFieldAfterMenu();
}
