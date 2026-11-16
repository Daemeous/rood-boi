//! Sets the battle sound and the scale value of the 2842C unit's effect (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_applyWeatherIntensity (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Clamps the effect's level into vs_field_weatherParticleCount, plays its sound if one is set, and stores its scaled value into D_800F1DA4.
pub fn vs_field_applyWeatherIntensity() callconv(.c) void {
    v.g.vs_field_weatherParticleCount().* = psx.conv(i16, c.vs_battle_clamp(
        psx.conv(c_short, @as(c_int, v.g.vs_field_weatherIntensity().*) *% 8),
        4,
        @as(c_int, v.g.vs_field_weatherParticleCapacity().*),
    ));

    if (@as(c_int, v.g.vs_field_weatherAmbientSoundId().*) >= 0) {
        if (psx.at(v.g.vs_field_weatherVolumeByIntensity(), v.g.vs_field_weatherIntensity().*).* != 0) {
            c.vs_main_playSfxDefault(0x200, @as(c_int, v.g.vs_field_weatherAmbientSoundId().*));
            c.vs_audio_fadeSfxVolume(
                0x200,
                @as(c_int, v.g.vs_field_weatherAmbientSoundId().*),
                @as(c_int, psx.at(v.g.vs_field_weatherVolumeByIntensity(), v.g.vs_field_weatherIntensity().*).*),
                8,
            );
        } else {
            c.vs_audio_stopSfx(0x200, @as(c_int, v.g.vs_field_weatherAmbientSoundId().*));
        }
    }

    const scale: i16 = psx.conv(i16, c.vs_battle_clamp(
        psx.conv(c_short, (@as(c_int, v.g.vs_field_weatherIntensity().*) *% 4) +% (@as(c_int, v.g.vs_field_weatherIntensity().*) *% 2)),
        12,
        90,
    ));
    const s: c_int = scale;
    v.g.D_800F1DA4().* = psx.conv(i16, s +% (s >> 1) +% (s >> 2));
}
