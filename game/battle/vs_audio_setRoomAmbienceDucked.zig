//! Battle unit 146C: forwards its argument to two battle helpers
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_audio_setRoomAmbienceDucked (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_field_duckRoomEffectSound and then vs_field_duckWeatherAmbientSound with the same argument.
pub fn vs_audio_setRoomAmbienceDucked(arg0: c_int) callconv(.c) void {
    c.vs_field_duckRoomEffectSound(arg0);
    c.vs_field_duckWeatherAmbientSound(arg0);
}
