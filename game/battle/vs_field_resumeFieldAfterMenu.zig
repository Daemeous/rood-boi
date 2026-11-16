//! Clears the battle CLUT state and the overlay's flag at D_800F1968 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_resumeFieldAfterMenu (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resets the battle CLUT state and the battle overlay's flag at D_800F1968.
pub fn vs_field_resumeFieldAfterMenu() callconv(.c) void {
    c.vs_main_setClutState(1);
    c.vs_render_transformSceneColors(8, 4, 0, 0, 0);
    c.vs_actors_freezeAllActorAnimations();
    c.vs_field_setRoomAnimationsPaused(0);
    c.vs_field_setMovingPlatformsPaused(0);
    _ = c.vs_field_setMinimapVisible(1);
    c.vs_audio_setRoomAmbienceDucked(0);
    c.vs_battle_setActorMarkersHidden(0);

    v.g.D_800F1968().* = 0;
}
