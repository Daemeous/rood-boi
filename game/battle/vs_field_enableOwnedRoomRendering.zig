//! Room state check for the battle room data (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_enableOwnedRoomRendering (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Marks the battle room as entered when its state is 1 and its room matches `arg0`; returns 1 if so.
pub fn vs_field_enableOwnedRoomRendering(arg0: c_int) callconv(.c) c_int {
    const room = v.g.vs_battle_roomData();
    if (room.unk0 == 1 and @as(c_int, room.unk2) == arg0) {
        room.unk1 = room.unk0;
        return 1;
    }
    return 0;
}
