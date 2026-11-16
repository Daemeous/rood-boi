//! Sets the unlocked break art and battle ability, then runs the unlock
//! routine (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: _setArtAndAbilityToUnlock (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Records the unlocked break art and battle ability, then calls the unlock routine with (3, 2).
pub fn setArtAndAbilityToUnlock(art: c_int, battleAbility: c_int) callconv(.c) void {
    v.g.vs_battle_unlockedBreakArt().* = psx.conv(u8, art);
    v.g.vs_battle_unlockedBattleAbility().* = psx.conv(u8, battleAbility);
    _ = c.vs_menu_frameworkOpenGameMenu(3, 2);
}
