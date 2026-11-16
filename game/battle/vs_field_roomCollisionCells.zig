//! Room data accessor for the battle room's section 3 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_roomCollisionCells (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the battle room's section 3 (the room data's section3 pointer), or null.
pub fn vs_field_roomCollisionCells() callconv(.c) ?*v._mpdRoomSection3 {
    return psx.get(v.g.vs_battle_roomData().section3);
}
