//! Clears the room's flag and, when the room has a section 14 and a valid
//! index in vs_field_roomEffectSoundId, calls the section-14 handler (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_restoreRoomEffectSoundVolume (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Sets vs_field_isRoomEffectSoundRestored to 1, then runs the room's section-14 handler when its index is not -1 and the section exists.
pub fn vs_field_restoreRoomEffectSoundVolume() callconv(.c) void {
    v.g.vs_field_isRoomEffectSoundRestored().* = 1;
    if (v.g.vs_field_roomEffectSoundId().* != -1) {
        if (psx.get(v.g.vs_battle_roomData().section14)) |section14| {
            c.vs_audio_fadeBankSfxVolume(0x200, @ptrCast(section14), @as(c_int, v.g.vs_field_roomEffectSoundId().*), 0x7F, 0);
        }
    }
}
