//! Applies a script's 5-byte scroll command to the vs_event_weatherTween record
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B9FC0 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the scroll record at once, or stores its previous values and steps it towards the new target.
pub fn func_800B9FC0(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_field_setRoomWeather(@as(c_int, arg0[1] >> 4));
    const s = v.g.vs_event_weatherTween();
    if (arg0[4] == 0) {
        s.intensity = psx.conv(i16, @as(c_int, arg0[1] & 0xF));
        c.vs_field_setWeatherIntensity(@as(c_int, s.intensity));
        s.windDirection = psx.conv(i16, @as(c_int, arg0[2]) *% 16);
        s.unkA = psx.conv(i16, @as(c_int, arg0[3]) *% 4 +% 512);
        c.vs_field_setWeatherWindAngles(@as(c_int, s.windDirection), @as(c_int, s.unkA));
    } else {
        const vv: c_int = @as(c_int, arg0[1] & 0xF);

        s.unk12 = s.intensity;
        s.unk14 = psx.conv(i16, vv -% @as(c_int, s.intensity));
        s.unkC = s.unkA;
        const base: i16 = psx.conv(i16, @as(c_int, s.unkC) -% 512);
        s.unkE = psx.conv(i16, @as(c_int, arg0[3]) *% 4 -% @as(c_int, base));
        s.unk6 = s.windDirection;
        var delta: c_int = @as(c_int, arg0[2]) *% 16 -% @as(c_int, s.unk6);
        if (delta < -2048) {
            delta +%= 4096;
        } else if (delta > 2048) {
            delta -%= 4096;
        }
        s.unk8 = psx.conv(i16, delta);
        s.unk3 = arg0[4];
        s.elapsedFrames = 0;
    }
    return 0;
}
