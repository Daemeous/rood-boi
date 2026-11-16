//! Entering vs_battle_exec's menu mode from the field (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_beginBattleActionMenu (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Switches vs_battle_exec to mode 5 (the menu), camera mode 1, keeping the weapon state.
pub fn enterMenuMode(_: c_int, _: c_int) callconv(.c) void {
    v.g.D_800F196C().* = 5;
    v.g._cameraMode().* = 1;
    c.vs_battle_setHudBattleMode((v.g.vs_battle_characterState().*.?.weaponDrawn & 1) | 2);
    c.vs_field_pauseFieldForMenu();
}
