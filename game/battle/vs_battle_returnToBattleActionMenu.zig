//! Battle state reset: action index, camera mode and two follow-up calls (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");
const v = @import("views_146C");

/// orig: vs_battle_returnToBattleActionMenu (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the battle's mode and action index, then runs the character-state and follow-up routines.
pub fn vs_battle_returnToBattleActionMenu() callconv(.c) void {
    v.g.D_800F196C().* = 5;
    v.g.vs_battle_actionState().*.?.unk8.actionIndex = 0;
    v.g._cameraMode().* = 1;
    c.vs_battle_setHudBattleMode((v.g.vs_battle_characterState().*.?.weaponDrawn & 1) | 2);
    c.vs_menu_frameworkResumeAfterActionSelection();
    c.vs_field_pauseFieldForMenu();
}
