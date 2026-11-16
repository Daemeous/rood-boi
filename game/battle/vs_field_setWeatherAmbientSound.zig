//! Stores a new value in the battle's pending sound slot and plays its sound
//! (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_setWeatherAmbientSound (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Stores `value` as the pending sound and plays it, returning the previous pending value.
pub fn vs_field_setWeatherAmbientSound(value: c_int) callconv(.c) c_int {
    const previous: c_int = v.g.vs_field_weatherAmbientSoundId().*;
    v.g.vs_field_weatherAmbientSoundId().* = psx.conv(i16, value);

    if (value >= 0) {
        if (psx.at(v.g.vs_field_weatherVolumeByIntensity(), v.g.vs_field_weatherIntensity().*).* != 0) {
            c.vs_main_playSfxDefault(0x200, @as(c_int, psx.conv(i16, value)));
            c.vs_audio_fadeSfxVolume(
                0x200,
                @as(c_int, v.g.vs_field_weatherAmbientSoundId().*),
                @as(c_int, psx.at(v.g.vs_field_weatherVolumeByIntensity(), v.g.vs_field_weatherIntensity().*).*),
                @as(u8, 8),
            );
        } else {
            c.vs_audio_stopSfx(0x200, @as(c_int, psx.conv(i16, value)));
        }
    }
    return previous;
}
