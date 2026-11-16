//! Passes the room's section 14 buffer to the battle section loader, then records the count
//! (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_executeRoomEffectPlaySound (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Runs the section 14 loader with the count from arg1 and stores that count in vs_field_roomEffectSoundId when section 14 is present.
pub fn vs_field_executeRoomEffectPlaySound(_: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    if (psx.get(v.g.vs_battle_roomData().section14)) |section| {
        if (arg1.?.operands[0].u16[0] == 0 or v.g.vs_field_isRoomEffectSoundRestored().* != 0) {
            c.vs_audio_playBankSfxCentered(0x200, @ptrCast(section), @as(c_uint, arg1.?.operands[0].u16[1]));
        } else {
            c.vs_audio_playBankSfx(0x200, @ptrCast(section), @as(c_uint, arg1.?.operands[0].u16[1]), 0x80, 0);
        }
        v.g.vs_field_roomEffectSoundId().* = psx.conv(i16, arg1.?.operands[0].u16[1]);
    }
    return 0;
}
