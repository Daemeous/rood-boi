//! Battle setup step: resets the CLUT state and the battle display flags (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_beginRewardPause (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the battle setup calls and sets the battle-ready flag.
pub fn vs_battle_beginRewardPause() callconv(.c) void {
    c.vs_render_transformRoomPalettes(0xD, 4, -4, -4, 0);
    c.vs_main_setClutState(0);
    c.vs_actors_unfreezeAllActorAnimations();
    c.vs_field_setRoomAnimationsPaused(1);
    _ = c.vs_field_setMinimapVisible(0);
    c.vs_audio_setRoomAmbienceDucked(1);
    v.g.D_800F1968().* = 1;
}
