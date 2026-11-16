//! Weapon kill streak: the blade category of the acting character's weapon
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _updateWeaponKillStreak (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Counts a weapon kill streak for the character's blade category, restarting it when the category changes.
pub fn updateWeaponKillStreak(_: c_int) callconv(.c) void {
    const actor = psx.get(v.g.vs_battle_characterState().*.?.stats);

    if (v.g.vs_battle_actionState().*.?.unk8.actionIndex >= 40) {
        return;
    }

    const score = v.g.scoreData();
    const category = actor.?.weapon.blade.category;

    if (score.weaponKillStreakBladeCategory != category) {
        score.weaponKillStreakBladeCategory = category;
        score.weaponKillStreak = 0;
    } else if (score.weaponKillStreak < 100) {
        score.weaponKillStreak +%= 1;
    }
}
