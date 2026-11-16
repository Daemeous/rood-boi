//! Acquires or releases the flag at D_800F4C08 for a caller's request byte (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA108 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// While arg0[1] is set, takes the flag at D_800F4C08 and, if taken, saves the id from vs_field_setWeatherAmbientSound(-1) in vs_event_suspendedAmbientSoundId; when clear, releases it.
pub fn func_800BA108(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    if (arg0[1] != 0) {
        if (v.g.D_800F4C08().* != 0) return 0;
        // The flag is a byte: the returned value is truncated and the stored byte is what is tested next.
        v.g.D_800F4C08().* = psx.conv(u8, c.vs_field_isRoomAudioSequenceAbsent());
        if (v.g.D_800F4C08().* != 0) {
            v.g.vs_event_suspendedAmbientSoundId().* = psx.conv(i16, c.vs_field_setWeatherAmbientSound(-1));
        }
    } else if (v.g.D_800F4C08().* != 0) {
        _ = c.vs_field_setWeatherAmbientSound(@as(c_int, v.g.vs_event_suspendedAmbientSoundId().*));
        v.g.D_800F4C08().* = 0;
    }
    return 0;
}
