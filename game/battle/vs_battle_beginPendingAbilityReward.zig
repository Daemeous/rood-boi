//! Opens the ability camera when an art or ability is unlocked or the current battle action is still locked; otherwise runs the plain tail (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_beginPendingAbilityReward (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Picks the ability camera or the plain tail for the battle state in D_800F18FC.
pub fn vs_battle_beginPendingAbilityReward() callconv(.c) void {
    switch (v.g.D_800F18FC().*) {
        0 => c.func_8006F5FC(),
        2 => {
            if (c._isArtOrAbilityUnlocked() != 0 or blk: {
                const d = v.g.vs_battle_actionState().*.?;
                break :blk d.unk8.actionIndex != 0 and
                    psx.getBits(psx.at(v.g.actions(), d.unk8.actionIndex), "unlocked") == 0 and
                    d.unk8.primaryTarget.unk40 == 0 and
                    d.unk2C07 == 0 and
                    d.unk8.primaryTarget.target.targetActor == 0;
            }) {
                v.g._cameraMode().* = 0xD;
                c.func_8009E070(0, null, 6);
                c.func_8006F848();
                c.vs_field_setMovingPlatformsPaused(1);
            } else {
                c.func_8006F5FC();
            }
        },
        else => {
            if (blk: {
                const d = v.g.vs_battle_actionState().*.?;
                break :blk d.unk8.actionIndex != 0 and
                    psx.getBits(psx.at(v.g.actions(), d.unk8.actionIndex), "unlocked") == 0 and
                    d.unk8.primaryTarget.unk40 == 0 and
                    d.unk2C07 == 0 and
                    d.unk8.primaryTarget.target.targetActor == 0;
            }) {
                v.g._cameraMode().* = 0xD;
                c.func_8009E070(0, null, 6);
                c.func_8006F848();
                c.vs_field_setMovingPlatformsPaused(1);
            } else {
                c.func_8006F5FC();
            }
        },
    }
}
