//! Room section 13 lookup: the current room's section 13 table and its entry count (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_items_roomTreasureChests (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the room's section 13 table and writes its entry count to `arg0`; null and 0 when the room has none.
pub fn vs_items_roomTreasureChests(arg0: [*c]c_int) callconv(.c) ?*v._mpdRoomSection13 {
    const room = v.g.vs_battle_roomData();
    if (psx.get(room.section13)) |section| {
        // The quotient is at most u32 max; usize is the C's sizeof type on each target, and the store truncates like C's.
        arg0.* = psx.conv(c_int, @as(usize, room.header.section13Len) / @sizeOf(v._mpdRoomSection13));
        return section;
    }
    arg0.* = 0;
    return null;
}
