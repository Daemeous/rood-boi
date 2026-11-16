//! Sets D_800F196C and calls the routine at vs_field_resumeFieldAfterMenu (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_endShortcutMenu (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Raises the D_800F196C flag and runs vs_field_resumeFieldAfterMenu.
pub fn vs_field_endShortcutMenu() callconv(.c) void {
    v.g.D_800F196C().* = 1;
    c.vs_field_resumeFieldAfterMenu();
}
