//! Marks the room-zone entries for section `arg0` as visited in the state flags (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_field_markSigilDoorUnlocked (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the state flag of the first entry in section `arg0` whose zone and map match the current room.
pub fn vs_field_markSigilDoorUnlocked(arg0: c_int) callconv(.c) void {
    const scene = psx.get(v.g.vs_battle_roomData().sectionF).?;
    const room = psx.at(&scene.rooms, 0);
    const zoneId: c_int = room.zoneId;
    const mapId: c_int = room.mapId;

    // vs_field_lockedDoorRoomPairs[i][k] of the static helper get_D_800E84AC: four bytes per row.
    const table = @as([*][4]u8, @ptrCast(psx.GlobalMany(u8, "vs_field_lockedDoorRoomPairs").ptr()));
    const sections = v.g.vs_field_lockedDoorIndexRanges();
    const flags = v.g.vs_main_stateFlags();

    var i: c_int = @as(c_int, psx.at(sections, arg0).*);
    while (i < @as(c_int, psx.at(sections, arg0 +% 1).*)) : (i +%= 1) {
        const row = psx.at(table, i);
        if ((@as(c_int, row[0]) == zoneId and @as(c_int, row[1]) == mapId) or
            (@as(c_int, row[2]) == zoneId and @as(c_int, row[3]) == mapId))
        {
            psx.at(&flags.unk186, i).* = 1;
            return;
        }
    }
}
