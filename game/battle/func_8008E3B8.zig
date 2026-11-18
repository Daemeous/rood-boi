//! Room section A lookup for the battle room data (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: func_8008E3B8 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the battle room's section A table and writes its entry count to `arg0`, or null and 0 when absent.
pub fn func_8008E3B8(arg0: [*c]c_int) callconv(.c) ?*v._mpdRoomSectionA {
    const room = v.g.vs_battle_roomData();
    const raw: usize = @intFromPtr(psx.get(room.sectionA));
    if (raw != 0) {
        arg0[0] = psx.conv(c_int, room.header.sectionALen / @sizeOf(v._mpdRoomSectionA));
        // The C returns the stored address whatever its alignment, so it is stored by its bits:
        // building a pointer from an address that is not aligned for the struct would trap here.
        var out: ?*v._mpdRoomSectionA = null;
        @as(*usize, @ptrCast(&out)).* = raw;
        return out;
    }
    arg0[0] = 0;
    return null;
}
