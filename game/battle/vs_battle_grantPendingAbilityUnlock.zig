//! Unlocks the art or ability pending after a battle, or announces a newly unlocked action (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_grantPendingAbilityUnlock (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Announces a newly usable action, or else unlocks the pending art or ability.
pub fn vs_battle_grantPendingAbilityUnlock() callconv(.c) void {
    const d = v.g.vs_battle_actionState().*.?;
    if (d.unk8.actionIndex != 0 and
        psx.getBits(psx.at(v.g.actions(), d.unk8.actionIndex), "unlocked") == 0 and
        d.unk8.primaryTarget.unk40 == 0 and
        d.unk2C07 == 0 and
        d.unk8.primaryTarget.target.targetActor == 0)
    {
        v.g._cameraMode().* = 0xE;
        c.vs_field_pauseFieldForMenu();
        // The action index is read again: the scene message call may change it.
        c.vs_battle_displaySceneMessage(0xC, @as(c_int, v.g.vs_battle_actionState().*.?.unk8.actionIndex), 1);
        psx.setBits(psx.at(v.g.actions(), v.g.vs_battle_actionState().*.?.unk8.actionIndex), "unlocked", 1);
        return;
    }

    // _isArtOrAbilityUnlocked is a unit function with no declaration in c_146C: called by its symbol.
    const isArtOrAbilityUnlocked = @extern(*const fn () callconv(.c) c_uint, .{ .name = "_isArtOrAbilityUnlocked" });
    const temp_v0_2: c_uint = isArtOrAbilityUnlocked();
    v.g._cameraMode().* = 14;
    c.vs_field_pauseFieldForMenu();
    c._setArtAndAbilityToUnlock(
        psx.conv(c_int, temp_v0_2 >> 0x10),
        psx.conv(c_int, temp_v0_2 & 0xFFFF),
    );
}
