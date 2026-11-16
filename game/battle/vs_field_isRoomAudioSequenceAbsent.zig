//! Tests whether the battle room's section 14 pointer is null (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_field_isRoomAudioSequenceAbsent (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns 1 when the battle room's section 14 guest address is zero, else 0.
pub fn vs_field_isRoomAudioSequenceAbsent() callconv(.c) c_int {
    const raw: c_uint = psx.rawOf(v.g.vs_battle_roomData().section14);
    return @intFromBool(raw < 1);
}
