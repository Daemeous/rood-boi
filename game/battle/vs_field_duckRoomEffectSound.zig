//! Calls the room section-14 routine with the current room index, using a
//! 0x20 or 0x7F limit depending on the argument (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_duckRoomEffectSound (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Runs the room's section-14 routine for the current room, when one is loaded and the room index is set.
pub fn vs_field_duckRoomEffectSound(arg0: c_int) callconv(.c) void {
    const roomIndex: c_int = @as(c_int, v.g.vs_field_roomEffectSoundId().*);
    if (roomIndex == -1) return;

    const section = psx.get(v.g.vs_battle_roomData().section14) orelse return;
    const sectionArg: [*c]c_int = @ptrCast(section);

    if (arg0 != 0) {
        c.vs_audio_fadeBankSfxVolume(0x200, sectionArg, roomIndex, 0x20, 8);
    } else {
        c.vs_audio_fadeBankSfxVolume(0x200, sectionArg, roomIndex, 0x7F, 8);
    }
}
