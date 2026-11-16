//! Finds the door section entry at a tile and copies it into the door
//! buffer, or clears the buffer (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_findEntranceDoorNearTile (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Looks up the room's door entry for tile (x, y) and kind, then records it as the entered door or clears it.
pub fn vs_field_findEntranceDoorNearTile(x: c_int, y: c_int, kind: c_int) callconv(.c) void {
    var var_s0: ?*v._mpdRoomDoorSection_t = null;

    if (psx.get(v.g.vs_battle_roomData().doorSection)) |base| {
        const temp_t4: c_int = psx.conv(c_int, v.g.vs_battle_roomData().header.doorSectionLen / 12);
        var i: c_int = 0;
        while (i < temp_t4) : (i +%= 1) {
            const entry = psx.at(base, i);
            if (kind >= 2 and @as(c_int, psx.getBits(&entry.unk2, "unk0_10")) == kind) break;
            if (@as(c_int, psx.getBits(&entry.unk2, "unk0_10")) == kind) {
                var j: c_int = 0;
                while (j < 9) : (j +%= 1) {
                    if (@as(c_int, psx.getBits(&entry.unk2, "unk0_0")) == (x -% 1) +% (j -% @divTrunc(j, 3) *% 3) and
                        @as(c_int, psx.getBits(&entry.unk2, "unk0_5")) == (y -% 1) +% @divTrunc(j, 3))
                    {
                        break;
                    }
                }
                if (j != 9) break;
            }
        }
        var_s0 = if (i == temp_t4) null else psx.at(base, i);
    }

    if (var_s0) |door| {
        _ = c.vs_main_memcpy(v.g.vs_field_entranceDoorRecord(), door, @sizeOf(v._mpdRoomDoorSection_t));
        const door_id: c_int = c._getDoorId(@as(c_int, door.unk8));
        v.g.vs_battle_doorEntered().* = psx.conv(i16, door_id);
    } else {
        c.vs_main_bzero(v.g.vs_field_entranceDoorRecord(), @sizeOf(v._mpdRoomDoorSection_t));
        v.g.vs_battle_doorEntered().* = -1;
    }
}
