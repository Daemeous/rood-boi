//! Finds the section 9 room entry that matches a (arg0, arg1, arg2) key (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_field_findRoomTriggerAtTile (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the room's section 9 entry whose three keys match, or null; a match with a low flag value is hidden while a state flag is set.
pub fn vs_field_findRoomTriggerAtTile(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) ?*v._mpdRoomSection9 {
    const room = v.g.vs_battle_roomData();

    if (psx.get(room.section9)) |base| {
        const count: c_int = psx.conv(c_int, @as(usize, room.header.section9Len) / @sizeOf(v._mpdRoomSection9));
        var i: c_int = 0;
        while (i < count) : (i +%= 1) {
            const e = psx.at(base, i);
            if (@as(c_int, e.unk0) == arg0 and @as(c_int, e.unk2) == arg1 and @as(c_int, e.unk4) == arg2) {
                const flags = v.g.vs_main_stateFlags();
                if ((flags.unk20 != 0 or flags.unk40 != 0) and @as(c_int, e.unk6) < 16) {
                    return null;
                } else {
                    return e;
                }
            }
        }
    }
    return null;
}
