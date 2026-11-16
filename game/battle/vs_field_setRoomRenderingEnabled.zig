//! Room section flag: stores a byte into the battle room data's unk1 when its unk0 is set (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_field_setRoomRenderingEnabled (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores arg0 (truncated to a byte) into vs_battle_roomData.unk1 when its unk0 is nonzero.
pub fn vs_field_setRoomRenderingEnabled(arg0: c_int) callconv(.c) void {
    const room = v.g.vs_battle_roomData();
    if (room.unk0 != 0) {
        room.unk1 = psx.conv(u8, arg0);
    }
}
