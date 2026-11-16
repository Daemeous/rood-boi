//! Per-frame battle timer and camera block update: advances the timed
//! state machine, clears the block when idle, and refreshes it
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_updateEffectCameraMotion (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Advances the battle's timed state, then clears or updates the camera block and refreshes it.
pub fn vs_effects_updateEffectCameraMotion() callconv(.c) void {
    // Captured before any call: the refresh at the end uses this value, not a re-read.
    const temp_s0 = v.g.D_800F55E8().* != 0;

    if (v.g.D_800F55A0().* != 0) {
        switch (v.g.D_800F55E8().*) {
            0 => {},
            1 => c.vs_effects_interpolateEffectCameraLinear(),
            2 => c.vs_effects_interpolateEffectCameraCosine(),
            3 => c.vs_effects_holdEffectCameraStartPose(),
            else => {},
        }
        if (v.g.D_800F54D0().* <= v.g.D_800F55A0().*) {
            v.g.D_800F54D0().* +%= 1;
            if (v.g.D_800F54D0().* > v.g.D_800F55A0().*) {
                v.g.D_800F54D0().* = 0;
                v.g.D_800F55A0().* = 0;
                c.vs_effects_setEffectCameraTransitionMode(0);
            }
        }
    }

    const state = v.g.vs_effects_effectCamera();
    if (v.g.vs_effects_enabledShakeChannels().* != 0) {
        c.vs_effects_updateCameraShakeChannels();
    } else {
        state.unk2C = 0;
        state.unk28 = 0;
        state.unk24 = 0;
        state.unk20 = 0;
    }

    // vs_effects_enabledShakeChannels is read again here: the branch above may have changed it.
    if (v.g.vs_effects_enabledShakeChannels().* != 0 or temp_s0) {
        c.vs_effects_applyEffectCamera(psx.cptr(c.D_800F54D8_t, state));
    }
}
