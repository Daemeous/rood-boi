//! Sets the battle state word D_800F196C to 6 and calls vs_field_pauseFieldForMenu (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_beginShortcutMenu (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the battle state word to 6, then runs the routine that follows it.
pub fn vs_field_beginShortcutMenu() callconv(.c) void {
    v.g.D_800F196C().* = 6;
    c.vs_field_pauseFieldForMenu();
}
