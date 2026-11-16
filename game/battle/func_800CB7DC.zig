//! Clears the battle-side state after a cycle: closes the window (6) and,
//! for a pending entry of kind 1, its follow-up (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CB7DC (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs the cleanup for the pending entry in vs_effects_screenEffectOverlay, if there is one.
pub fn func_800CB7DC() callconv(.c) void {
    if (v.g.vs_effects_screenEffectOverlay().* != null) {
        _ = c.func_8007E1C0(6);
        // vs_effects_screenEffectOverlay is reloaded after func_8007E1C0, as the C does.
        if (v.g.vs_effects_screenEffectOverlay().*.?.unk1 == 1) {
            _ = c.vs_render_retireTexturePages(0x1B, 5, 6);
        }
        c.func_800CA97C();
    }
}
