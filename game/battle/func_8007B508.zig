//! Camera and name-window entry at the start of battle, when a locked action is
//! selected or art/ability unlock state requires it (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// Sets the camera to mode 5, closes the window it opens and runs the follow-up call.
fn setCameraMode5() void {
    v.g._cameraMode().* = 5;
    if (v.g.D_800F1864().* != 0) {
        c.func_8009E070(0, null, 6);
        c.func_8006F848();
    }
    c.vs_field_setMovingPlatformsPaused(1);
}

/// orig: func_8007B508 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Switches to the locked-camera mode when the art/ability or action-unlock check requires it, else runs the common tail.
pub fn func_8007B508() callconv(.c) void {
    // First state-flag call; the C short-circuits the later two calls on the same expression.
    const flag = c.vs_battle_getStateFlag(0xB6);
    if (flag == 0 or c.vs_battle_getStateFlag(0xB6) == 2 or c.vs_battle_getStateFlag(0xB6) == 3) {
        if (c._isArtOrAbilityUnlocked() != 0) {
            setCameraMode5();
            return;
        }
    }

    const d = v.g.vs_battle_actionState().*.?;
    if (d.unk8.actionIndex != 0 and
        psx.getBits(psx.at(v.g.actions(), d.unk8.actionIndex), "unlocked") == 0 and
        d.unk8.unk4.unk40 == 0 and
        d.unk2C07 == 0 and
        d.unk8.unk4.unk0.targetActor == 0)
    {
        setCameraMode5();
        return;
    }

    c.vs_battle_resumeAfterRewards();
}
