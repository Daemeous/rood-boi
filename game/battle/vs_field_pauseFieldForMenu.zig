//! Battle display setup: calls the battle draw-state routines in order and sets D_800F1968
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_pauseFieldForMenu (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resets the battle display state and marks D_800F1968 as set.
pub fn vs_field_pauseFieldForMenu() callconv(.c) void {
    c.vs_render_transformRoomPalettes(13, 4, -4, -4, 0);
    c.vs_main_setClutState(0);
    c.vs_actors_unfreezeAllActorAnimations();
    c.vs_field_setRoomAnimationsPaused(1);
    c.vs_field_setMovingPlatformsPaused(1);
    _ = c.vs_field_setMinimapVisible(@as(u8, 0));
    c.vs_audio_setRoomAmbienceDucked(1);
    c.vs_battle_setActorMarkersHidden(1);

    v.g.D_800F1968().* = 1;
}
