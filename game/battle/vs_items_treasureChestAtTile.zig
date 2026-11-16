//! Room section 13 lookup (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_items_treasureChestAtTile (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Finds the room section 13 record whose unk0, unk2 and unk4 equal arg0, arg1 and arg2; null if none.
pub fn vs_items_treasureChestAtTile(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) ?*v._mpdRoomSection13 {
    const section = psx.get(v.g.vs_battle_roomData().section13);
    if (section) |first| {
        // The quotient fits in 32 bits, so the conversion to int keeps the C's value.
        const count: c_int = psx.conv(c_int, @as(usize, v.g.vs_battle_roomData().header.section13Len) / @sizeOf(v._mpdRoomSection13));
        var i: c_int = 0;
        while (i < count) : (i +%= 1) {
            const entry = psx.at(first, i);
            if (@as(c_int, entry.tileX) == arg0 and @as(c_int, entry.tileZ) == arg1) {
                if (@as(c_int, entry.unk4) == arg2) {
                    return entry;
                }
            }
        }
    }
    return null;
}
