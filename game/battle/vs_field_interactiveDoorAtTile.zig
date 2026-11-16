//! Room section A lookup: finds the entry at a grid cell with a matching key (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_interactiveDoorAtTile (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the room section A entry at (arg0, arg1) with key arg2, or one of the cells beside it by its type, or null.
pub fn vs_field_interactiveDoorAtTile(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) ?*v._mpdRoomSectionA {
    const room = v.g.vs_battle_roomData();
    if (psx.get(room.sectionA)) |first| {
        const temp_t2: c_int = psx.conv(c_int, room.header.sectionALen / 20);
        const base: *v._mpdRoomSectionA = first;
        if (base.trigger.unkA != 0) {
            var i: c_int = 0;
            while (i < temp_t2) : (i +%= 1) {
                const row: *v._mpdRoomSectionA = psx.at(base, i);
                const x: c_int = row.trigger.tileX;
                const y: c_int = row.trigger.tileZ;
                const z: c_int = row.trigger.floorLayer;

                if (x == arg0 and y == arg1 and z == arg2) return row;

                switch (@as(c_int, row.trigger.unk6) -% 4) {
                    0 => {
                        if (x == arg0 and (y -% 1) == arg1 and z == arg2) return row;
                    },
                    1 => {
                        if ((x +% 1) == arg0 and y == arg1 and z == arg2) return row;
                    },
                    2 => {
                        if (x == arg0 and (y +% 1) == arg1 and z == arg2) return row;
                    },
                    3 => {
                        if ((x -% 1) == arg0 and y == arg1 and z == arg2) return row;
                    },
                    else => {},
                }
            }
        }
    }
    return null;
}
