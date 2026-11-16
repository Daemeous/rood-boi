//! Room section-3 cell lookup and section-12 entry lookup (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_field_collisionCellAt (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the room's section-3 cell at (width, height), or section 12's entry for arg2 >= 2; null when absent or out of range.
pub fn vs_field_collisionCellAt(width: c_uint, height: c_uint, arg2: c_int) callconv(.c) ?*v._mpdRoomSection3 {
    const room = v.g.vs_battle_roomData();
    const section3 = psx.get(room.section3) orelse return null;
    if (arg2 >= 2) {
        const section12 = psx.get(room.section12) orelse return null;
        return &psx.at(section12, arg2 -% 2).unkB4;
    }
    // The C dereferences the collision map unconditionally here (its first comparison always reads width).
    const cms = psx.get(room.collisionMapSection).?;
    const cmsWidth: c_uint = psx.conv(c_uint, @as(c_int, cms.width));
    const cmsHeight: c_uint = psx.conv(c_uint, @as(c_int, cms.height));
    if (width < cmsWidth and height < cmsHeight) {
        return psx.at(section3, height *% cmsWidth +% width);
    }
    return null;
}
