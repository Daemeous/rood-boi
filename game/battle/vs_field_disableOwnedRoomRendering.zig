//! Room section check: whether the battle room data is type 1 for room `arg0` (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_field_disableOwnedRoomRendering (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns 1 and clears the room flag unk1 when the room data's unk0 is 1 and its unk2 equals arg0; else 0.
pub fn vs_field_disableOwnedRoomRendering(arg0: c_int) callconv(.c) c_int {
    const room = v.g.vs_battle_roomData();
    if (room.unk0 == 1 and @as(c_int, room.unk2) == arg0) {
        room.unk1 = 0;
        return 1;
    }
    return 0;
}
