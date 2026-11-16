//! Clears bit 12 of every 16th word of the room's geometry section (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_clearGeometryOcclusion (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears flag 0x1000 on the first word after each 64-byte block of the room's geometry section, for as many blocks as its first word counts.
pub fn vs_field_clearGeometryOcclusion() callconv(.c) void {
    const geometry = psx.get(v.g.vs_battle_roomData().geometrySection);
    // The C forms the cursor before its null test; the cursor is only used inside the test.
    if (geometry == null) return;
    const count: c_int = geometry.?.*;

    var cursor = psx.at(geometry.?, 1);
    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        cursor.* &= ~@as(i32, 0x1000);
        cursor = psx.at(cursor, 16);
    }
}
