//! Clears the battle's CD queue, background and sound pointers and sets the
//! defaults for the 146C state (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_initializeRoomBackground (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resets the battle's queued CD slots, background layout and table pointers, then runs vs_field_initializeRoomBackgroundColors.
pub fn vs_field_initializeRoomBackground() callconv(.c) void {
    v.g.vs_field_backgroundTextureDiscRequest().* = null;
    v.g.vs_field_backgroundLayoutDiscRequest().* = null;
    v.g.vs_field_loadedBackgroundLayout().* = null;
    v.g.vs_field_legacyBackgroundTimBuffer().* = null;
    v.g.vs_field_weatherParticles().* = null;
    v.g.vs_field_isTiledBackgroundEnabled().* = 0;
    v.g.vs_field_weatherAmbientSoundId().* = -2;
    v.g.vs_field_roomWeatherKind().* = 0;
    v.g.vs_field_isWeatherVariationEnabled().* = 1;
    c.vs_field_initializeRoomBackgroundColors();
}
