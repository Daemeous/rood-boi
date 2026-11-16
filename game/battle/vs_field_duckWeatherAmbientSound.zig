//! Forwards the vs_field_weatherAmbientSoundId attribute to vs_audio_fadeSfxVolume, halving the vs_field_weatherVolumeByIntensity entry when arg0 is set (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_duckWeatherAmbientSound (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Calls vs_audio_fadeSfxVolume with vs_field_weatherAmbientSoundId and the vs_field_weatherVolumeByIntensity entry at vs_field_weatherIntensity, halved when arg0 is set.
pub fn vs_field_duckWeatherAmbientSound(arg0: c_int) callconv(.c) void {
    if (v.g.vs_field_weatherAmbientSoundId().* >= 0) {
        if (arg0 != 0) {
            const value: c_int = @as(c_int, psx.at(v.g.vs_field_weatherVolumeByIntensity(), v.g.vs_field_weatherIntensity().*).*);
            c.vs_audio_fadeSfxVolume(0x200, @as(c_int, v.g.vs_field_weatherAmbientSoundId().*), value >> 2, 8);
        } else {
            const value: c_int = @as(c_int, psx.at(v.g.vs_field_weatherVolumeByIntensity(), v.g.vs_field_weatherIntensity().*).*);
            c.vs_audio_fadeSfxVolume(0x200, @as(c_int, v.g.vs_field_weatherAmbientSoundId().*), value, 8);
        }
    }
}
