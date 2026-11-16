//! Camera-mode dispatch for battle (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_updateRewardSequence (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the camera routine for `_cameraMode` 3..6 and reports whether one ran.
pub fn vs_battle_updateRewardSequence() callconv(.c) c_int {
    switch (v.g._cameraMode().*) {
        3 => {
            c.vs_battle_updateRewardResultsDialog();
            return 1;
        },
        4 => {
            c.vs_battle_updatePendingLootDialog();
            return 1;
        },
        5 => {
            c.vs_battle_waitForRewardUnlockEffects();
            return 1;
        },
        6 => {
            c.vs_battle_updateRewardUnlockDialog();
            return 1;
        },
        else => return 0,
    }
}
