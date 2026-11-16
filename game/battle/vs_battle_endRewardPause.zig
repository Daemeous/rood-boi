//! Battle setup step: resets the CLUT state and the helper state, then clears D_800F1968 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_endRewardPause (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resets the CLUT state and the battle helper state, then clears D_800F1968.
pub fn vs_battle_endRewardPause() callconv(.c) void {
    c.vs_main_setClutState(1);
    c.vs_render_transformSceneColors(8, 4, 0, 0, 0);
    c.vs_actors_freezeAllActorAnimations();
    c.vs_field_setRoomAnimationsPaused(0);
    c.vs_audio_setRoomAmbienceDucked(0);
    v.g.D_800F1968().* = 0;
}
