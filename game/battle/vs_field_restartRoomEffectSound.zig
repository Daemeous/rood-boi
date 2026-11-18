//! Calls the routine at the room's section 14 with vs_field_roomEffectSoundId when both are set (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_restartRoomEffectSound (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Calls vs_audio_playBankSfxCentered with room section 14 and vs_field_roomEffectSoundId when both are set.
pub fn vs_field_restartRoomEffectSound() callconv(.c) void {
    if (v.g.vs_field_roomEffectSoundId().* != -1) {
        // The section14 address is a guest address that need not be aligned for u32: pass its bits as an unaligned pointer, as the C does.
        const raw: usize = @intFromPtr(psx.get(v.g.vs_battle_roomData().section14));
        if (raw != 0) {
            const F = *const fn (c_int, ?*anyopaque, c_uint) callconv(.c) void;
            @as(F, @ptrCast(&c.vs_audio_playBankSfxCentered))(
                0x200,
                @ptrFromInt(raw),
                psx.conv(c_uint, v.g.vs_field_roomEffectSoundId().*),
            );
        }
    }
}
