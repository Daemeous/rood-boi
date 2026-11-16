//! Starts the room's weather effect and its ambient sound (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// The effect's density: the room's unk48[1] extents in 128-unit cells, divided, clamped to 4..128.
fn density(room: *v._mpdRoomSectionB, divisor: c_int) c_int {
    const cells = @divTrunc(
        @divTrunc(@as(c_int, room.unk48[1].s16[0]), 128) *% @divTrunc(@as(c_int, room.unk48[1].s16[1]), 128),
        divisor,
    );
    return @as(c_int, psx.conv(i16, c.vs_battle_clamp(psx.conv(c_short, cells), 4, 128)));
}

/// orig: vs_field_setRoomWeather (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Sets up weather `kind` (0 none, 1..4) from the current room's section B and switches the looping sound
/// vs_field_weatherAmbientSoundId to the kind's (21 for kind 1, none otherwise); with no room, stops the sound.
pub fn vs_field_setRoomWeather(kind: c_int) callconv(.c) void {
    const current = v.g.vs_field_weatherAmbientSoundId();
    if (psx.get(v.g.vs_battle_roomData().sectionB)) |room| {
        const params = v.g.D_800F1D98();
        params[7] = 1;
        params[9] = 1;
        params[8] = 1;
        params[4] = room.unk42;
        var sound: c_int = -2;
        params[5] = room.unk36;
        v.g.vs_field_roomWeatherKind().* = psx.conv(u8, kind);
        v.g.vs_field_weatherIntensity().* = room.unk3C;
        c.vs_field_applyWeatherIntensity();
        c.func_80090B28();

        switch (kind) {
            0 => {},
            1 => {
                c.func_8008F29C(density(room, 4), kind);
                sound = 21;
            },
            2 => c.func_8008F29C(density(room, 4), kind),
            3 => c.func_8008F30C(density(room, 4), kind),
            4 => c.func_8008F9A4(density(room, 2), kind),
            else => {},
        }

        if (current.* != -1) {
            if (current.* >= 0 and @as(c_int, current.*) != sound) {
                c.vs_audio_stopSfx(0x200, current.*);
            }
            if (sound >= 0) {
                c.vs_main_playSfxDefault(0x200, sound);
            }
            current.* = psx.conv(i16, sound);
        }
    } else if (current.* >= 0) {
        c.vs_audio_stopSfx(0x200, current.*);
        current.* = -2;
    }
}
