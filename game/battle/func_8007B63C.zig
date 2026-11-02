//! After an action: the message for a newly learned one, else the art or ability to unlock (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007B63C (src/BATTLE/BATTLE.PRG/146C.c)
/// Camera mode 6 and func_8007B410 either way. A still-locked action (not 0) used with no unk40, no unk2C07 and
/// target actor 0 shows message 0xC for it and is marked unlocked; otherwise _isArtOrAbilityUnlocked's art (high
/// half) and ability (low half) go to _setArtAndAbilityToUnlock.
pub fn func_8007B63C() callconv(.c) void {
    const block = v.g.D_800F19CC();
    // vs_main_actions has 256 entries in the views; actionIndex is a u16 the C does not bound: psx.at.
    if (block.*.?.unk8.actionIndex != 0 and
        psx.getBits(psx.at(v.g.actions(), block.*.?.unk8.actionIndex), "unlocked") == 0 and
        block.*.?.unk8.unk4.unk40 == 0 and block.*.?.unk2C07 == 0 and
        block.*.?.unk8.unk4.unk0.targetActor == 0)
    {
        v.g._cameraMode().* = 6;
        c.func_8007B410();
        c.vs_battle_displaySceneMessage(0xC, block.*.?.unk8.actionIndex, 1);
        psx.setBits(psx.at(v.g.actions(), block.*.?.unk8.actionIndex), "unlocked", 1);
        return;
    }
    const unlocked: c_uint = @bitCast(c._isArtOrAbilityUnlocked());
    v.g._cameraMode().* = 6;
    c.func_8007B410();
    c._setArtAndAbilityToUnlock(@bitCast(unlocked >> 16), @bitCast(unlocked & 0xFFFF));
}
