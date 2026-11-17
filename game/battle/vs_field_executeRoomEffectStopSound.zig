//! Clears the room's section-14 pointer case: calls the host helper on it and sets vs_field_roomEffectSoundId to -1 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_executeRoomEffectStopSound (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Calls vs_audio_stopBankSfx on the room's section 14 when it is set, then sets vs_field_roomEffectSoundId to -1.
pub fn vs_field_executeRoomEffectStopSound(_: ?*v.func_80092F74_t, arg1: ?*v.vs_field_RoomEffectCommand) callconv(.c) c_int {
    const section14 = psx.get(v.g.vs_battle_roomData().section14);
    if (section14) |p| {
        c.vs_audio_stopBankSfx(0x200, @ptrCast(p), @as(c_int, arg1.?.operands[0].u16[1]));
        v.g.vs_field_roomEffectSoundId().* = -1;
    }
    return 0;
}
